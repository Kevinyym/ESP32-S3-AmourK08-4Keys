#include "wifi_board.h"

#include "amour_k08_text_display.h"
#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "http.h"
#include "led/circular_strip.h"
#include "mcp_server.h"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#define TAG "AmourK08"

class PaControlledAudioCodec : public NoAudioCodecSimplex {
public:
    PaControlledAudioCodec()
        : NoAudioCodecSimplex(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                              AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                              AUDIO_I2S_SPK_GPIO_DOUT, I2S_STD_SLOT_LEFT, AUDIO_I2S_MIC_GPIO_SCK,
                              AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN, I2S_STD_SLOT_LEFT) {
        gpio_config_t io_config = {};
        io_config.pin_bit_mask = 1ULL << AUDIO_CODEC_PA_PIN;
        io_config.mode = GPIO_MODE_OUTPUT;
        io_config.pull_up_en = GPIO_PULLUP_DISABLE;
        io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_config.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_config));
        ESP_ERROR_CHECK(gpio_set_level(AUDIO_CODEC_PA_PIN, 0));
    }

    void EnableOutput(bool enable) override {
        // This mutex serializes the complete I2S/PA transition. The base codec
        // uses its own data mutex, so do not reuse it here.
        std::lock_guard<std::mutex> lock(output_state_mutex_);
        if (enable == output_enabled_) {
            return;
        }

        if (enable) {
            NoAudioCodecSimplex::EnableOutput(true);
            ESP_ERROR_CHECK(gpio_set_level(AUDIO_CODEC_PA_PIN, 1));
        } else {
            ESP_ERROR_CHECK(gpio_set_level(AUDIO_CODEC_PA_PIN, 0));
            NoAudioCodecSimplex::EnableOutput(false);
        }
    }

private:
    std::mutex output_state_mutex_;
};

class AmourK08FourKeysBoard : public WifiBoard {
public:
    AmourK08FourKeysBoard()
        : mode_button_(MODE_BUTTON_GPIO, false, 2000),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeRedLed();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeMusicTools();
        GetBacklight()->RestoreBrightness();
    }

    AudioCodec* GetAudioCodec() override {
        static PaControlledAudioCodec audio_codec;
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    Led* GetLed() override {
        static CircularStrip led(RGB_LED_GPIO, RGB_LED_COUNT);
        return &led;
    }

private:
    Button mode_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    LcdDisplay* display_ = nullptr;
    std::atomic<uint32_t> music_search_generation_{0};
    std::atomic<bool> music_worker_running_{false};
    std::mutex music_status_mutex_;
    std::string music_status_ = "idle";
    std::string music_title_;

    struct MusicSearchRequest {
        AmourK08FourKeysBoard* board;
        uint32_t generation;
        std::string query;
        bool play;
    };

    static std::string UrlEncode(const std::string& value) {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(value.size() * 3);
        for (unsigned char ch : value) {
            if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                encoded.push_back(static_cast<char>(ch));
            } else {
                encoded.push_back('%');
                encoded.push_back(kHex[ch >> 4]);
                encoded.push_back(kHex[ch & 0x0f]);
            }
        }
        return encoded;
    }

    static void TrimAsciiWhitespace(std::string& value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            value.clear();
            return;
        }
        const auto last = value.find_last_not_of(" \t\r\n");
        value = value.substr(first, last - first + 1);
    }

    static void EraseAll(std::string& value, const char* token) {
        const size_t token_length = std::strlen(token);
        for (size_t position = value.find(token); position != std::string::npos;
             position = value.find(token, position)) {
            value.erase(position, token_length);
        }
    }

    static std::string NormalizeMusicQuery(std::string query) {
        // The cloud model may send values such as “周杰伦的《以父之名》” or
        // “播放以父之名”. Prefer the quoted song name, then remove common
        // conversational words so the NAS receives a title keyword.
        const auto quoted_begin = query.find("《");
        const auto quoted_end = quoted_begin == std::string::npos
                                    ? std::string::npos
                                    : query.find("》", quoted_begin + std::strlen("《"));
        if (quoted_begin != std::string::npos && quoted_end != std::string::npos &&
            quoted_end > quoted_begin + std::strlen("《")) {
            query = query.substr(quoted_begin + std::strlen("《"),
                                 quoted_end - quoted_begin - std::strlen("《"));
        }

        TrimAsciiWhitespace(query);
        static constexpr const char* kPrefixes[] = {
            "请帮我播放", "请播放", "播放一下", "播放", "我想听", "我要听", "想听",
            "来一首",     "放一首", "歌曲",     "音乐",
        };
        bool removed_prefix = true;
        while (removed_prefix && !query.empty()) {
            removed_prefix = false;
            for (const char* prefix : kPrefixes) {
                const size_t length = std::strlen(prefix);
                if (query.compare(0, length, prefix) == 0) {
                    query.erase(0, length);
                    TrimAsciiWhitespace(query);
                    removed_prefix = true;
                    break;
                }
            }
        }

        static constexpr const char* kDecorations[] = {
            "《", "》", "“", "”", "‘", "’", "\"", "，", "。", "！", "？", "!", "?",
        };
        for (const char* decoration : kDecorations) {
            EraseAll(query, decoration);
        }
        static constexpr const char* kSuffixes[] = {"这首歌", "这首歌曲", "这首音乐"};
        for (const char* suffix : kSuffixes) {
            const size_t length = std::strlen(suffix);
            if (query.size() >= length &&
                query.compare(query.size() - length, length, suffix) == 0) {
                query.erase(query.size() - length);
                break;
            }
        }
        TrimAsciiWhitespace(query);
        return query;
    }

    void SetMusicStatus(std::string status, std::string title = {}) {
        std::lock_guard<std::mutex> lock(music_status_mutex_);
        music_status_ = std::move(status);
        music_title_ = std::move(title);
    }

    void CancelMusicSearch() {
        ++music_search_generation_;
        if (music_worker_running_.load()) {
            SetMusicStatus("cancelled");
        }
    }

    static bool IsTrackId(const char* id) {
        if (id == nullptr || std::strlen(id) != 64) {
            return false;
        }
        for (size_t i = 0; i < 64; ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(id[i]))) {
                return false;
            }
        }
        return true;
    }

    static void MusicSearchTask(void* arg) {
        std::unique_ptr<MusicSearchRequest> request(static_cast<MusicSearchRequest*>(arg));
        request->board->RunMusicSearch(*request);
        request->board->music_worker_running_.store(false);
        request.reset();
        vTaskDelete(nullptr);
    }

    void RunMusicSearch(const MusicSearchRequest& request) {
        constexpr size_t kMaxResponseBytes = 16 * 1024;
        std::string error = "搜索失败";
        std::string response;
        auto http = GetNetwork()->CreateHttp(0);
        if (http) {
            http->SetTimeout(5000);
            // Keep the TCP connection open until this task closes it. The ESP HTTP
            // client can deadlock when a peer closes a response concurrently with
            // its receive callback.
            http->SetKeepAlive(true);
            http->SetHeader("Accept", "application/json");
            const std::string normalized_query = NormalizeMusicQuery(request.query);
            ESP_LOGI(TAG, "Music search query: raw='%s', normalized='%s'", request.query.c_str(),
                     normalized_query.c_str());
            std::string url = "http://10.0.0.228:8090/search?q=" +
                              UrlEncode(normalized_query.empty() ? request.query : normalized_query) +
                              "&limit=5";
            if (http->Open("GET", url)) {
                int status = http->GetStatusCode();
                if (status >= 200 && status < 300) {
                    std::array<char, 1024> buffer;
                    const int64_t deadline = esp_timer_get_time() + 10000000;
                    while (response.size() <= kMaxResponseBytes) {
                        if (request.generation != music_search_generation_.load()) {
                            error = "cancelled";
                            break;
                        }
                        if (esp_timer_get_time() >= deadline) {
                            error = "搜索响应超时";
                            break;
                        }
                        int count = http->Read(buffer.data(), buffer.size());
                        if (count < 0) {
                            error = "读取搜索结果失败";
                            break;
                        }
                        if (count == 0) {
                            error.clear();
                            break;
                        }
                        response.append(buffer.data(), count);
                    }
                    if (response.size() > kMaxResponseBytes) {
                        error = "搜索结果过大";
                    }
                    ESP_LOGI(TAG, "Music search HTTP %d, response bytes=%u", status,
                             static_cast<unsigned>(response.size()));
                } else {
                    error = "NAS 搜索服务返回错误";
                }
            } else {
                error = "无法连接 NAS";
            }
            http->Close();
        }

        std::string id;
        std::string title;
        std::string matches;
        int total_matches = 0;
        if (error.empty()) {
            cJSON* root = cJSON_ParseWithLength(response.data(), response.size());
            cJSON* tracks = root ? cJSON_GetObjectItem(root, "tracks") : nullptr;
            cJSON* total = root ? cJSON_GetObjectItem(root, "total") : nullptr;
            if (cJSON_IsNumber(total) && total->valuedouble >= 0 && total->valuedouble <= 10000) {
                total_matches = total->valueint;
            }
            if (!cJSON_IsArray(tracks)) {
                error = "没有找到可播放歌曲";
            } else {
                const int count = cJSON_GetArraySize(tracks);
                if (total_matches == 0) {
                    total_matches = count;
                }
                for (int index = 0; index < count && index < 5; ++index) {
                    cJSON* track = cJSON_GetArrayItem(tracks, index);
                    cJSON* id_json = track ? cJSON_GetObjectItem(track, "id") : nullptr;
                    cJSON* title_json = track ? cJSON_GetObjectItem(track, "title") : nullptr;
                    if (!cJSON_IsString(id_json) || !IsTrackId(id_json->valuestring) ||
                        !cJSON_IsString(title_json) || title_json->valuestring[0] == '\0' ||
                        std::strlen(title_json->valuestring) > 256) {
                        continue;
                    }
                    if (id.empty()) {
                        id = id_json->valuestring;
                        title = title_json->valuestring;
                    }
                    if (!matches.empty()) {
                        matches += " | ";
                    }
                    matches += title_json->valuestring;
                }
                if (id.empty()) {
                    error = "没有找到可播放歌曲";
                }
            }
            cJSON_Delete(root);
        }

        if (!error.empty()) {
            ESP_LOGW(TAG, "Music search failed: %s", error.c_str());
        }

        if (request.generation != music_search_generation_.load()) {
            return;
        }
        if (!error.empty()) {
            SetMusicStatus("error: " + error);
            Application::GetInstance().Schedule(
                [error]() { Board::GetInstance().GetDisplay()->ShowNotification(error.c_str()); });
            return;
        }

        if (!request.play) {
            matches = "共" + std::to_string(total_matches) + "首，前" +
                      std::to_string(std::min(total_matches, 5)) + "首：" + matches;
            SetMusicStatus("found", matches);
            return;
        }
        SetMusicStatus("accepted", title);
        Application::GetInstance().PlayMusic("http://10.0.0.228:8090/tracks/" + id + ".ogg", title,
                                             [this, generation = request.generation]() {
                                                 return generation ==
                                                        music_search_generation_.load();
                                             });
    }

    ReturnValue StartMusicSearch(const std::string& query, bool play) {
        if (query.empty() || query.size() > 256) {
            return std::string("error: query must be 1-256 bytes");
        }
        bool expected = false;
        if (!music_worker_running_.compare_exchange_strong(expected, true)) {
            return std::string("busy: a search is already running");
        }
        uint32_t generation = ++music_search_generation_;
        SetMusicStatus("searching", query);
        auto* request = new MusicSearchRequest{this, generation, query, play};
        if (xTaskCreate(MusicSearchTask, "music_search", 6144, request, 2, nullptr) != pdPASS) {
            delete request;
            music_worker_running_.store(false);
            SetMusicStatus("error: cannot start search worker");
            return std::string("error: cannot start search worker");
        }
        return std::string(play ? "accepted: searching NAS music library" :
                                  "accepted: searching NAS music library without playback");
    }

    void InitializeMusicTools() {
        auto& app = Application::GetInstance();
        app.RegisterMusicRequestCancelCallback([this]() { CancelMusicSearch(); });
        auto& mcp = McpServer::GetInstance();
        mcp.AddTool("self.music.play",
                    "搜索 NAS 曲库并异步播放匹配度最高的可用歌曲。query 只填写歌名或歌手关键词，"
                    "不要包含‘播放’等命令词。调用会立即返回 accepted；请用 self.music.status "
                    "查询搜索或播放结果。",
                    PropertyList({Property("query", kPropertyTypeString)}),
                    [this](const PropertyList& properties) -> ReturnValue {
                        return StartMusicSearch(properties["query"].value<std::string>(), true);
                    });
        mcp.AddTool("self.music.search",
                    "只搜索 NAS 曲库，不播放音乐。用于回答曲库是否有某首歌或某位歌手；"
                    "query 只填写歌名或歌手关键词。调用后用 self.music.status 读取最多三条匹配结果。",
                    PropertyList({Property("query", kPropertyTypeString)}),
                    [this](const PropertyList& properties) -> ReturnValue {
                        return StartMusicSearch(properties["query"].value<std::string>(), false);
                    });
        mcp.AddTool("self.music.stop", "取消待处理的点歌或停止当前音乐。", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue {
                        CancelMusicSearch();
                        Application::GetInstance().StopMusic();
                        SetMusicStatus("idle");
                        return std::string("stopped");
                    });
        mcp.AddTool("self.music.status", "查询 NAS 点歌的搜索和播放状态。", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue {
                        auto core_state = Application::GetInstance().GetMusicPlaybackState();
                        std::lock_guard<std::mutex> lock(music_status_mutex_);
                        if (core_state == MusicPlaybackState::kPlaying) {
                            return std::string("playing: ") + music_title_;
                        }
                        if (core_state == MusicPlaybackState::kPending) {
                            return std::string("pending: ") + music_title_;
                        }
                        if (core_state == MusicPlaybackState::kFailed) {
                            return std::string("error: playback failed");
                        }
                        if (music_status_ == "accepted") {
                            return std::string("idle");
                        }
                        if (music_status_ == "found") {
                            return std::string("found: ") + music_title_;
                        }
                        return music_status_;
                    });
    }

    void InitializeRedLed() {
        gpio_config_t io_config = {};
        io_config.pin_bit_mask = 1ULL << RED_LED_GPIO;
        io_config.mode = GPIO_MODE_OUTPUT;
        io_config.pull_up_en = GPIO_PULLUP_DISABLE;
        io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_config.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_config));
        ESP_ERROR_CHECK(gpio_set_level(RED_LED_GPIO, 0));
    }

    void InitializeSpi() {
        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = DISPLAY_MOSI_PIN;
        bus_config.miso_io_num = GPIO_NUM_NC;
        bus_config.sclk_io_num = DISPLAY_CLK_PIN;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

        display_ = new AmourK08TextDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                           DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                           DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        mode_button_.OnClick([this]() {
            CancelMusicSearch();
            auto& app = Application::GetInstance();
            app.Schedule([&app]() { app.ToggleChatState(); });
        });
        mode_button_.OnLongPress([this]() {
            CancelMusicSearch();
            auto& app = Application::GetInstance();
            app.StopMusic();
            app.Schedule([this]() { EnterWifiConfigMode(); });
        });

        volume_up_button_.OnClick([this]() { ScheduleVolumeChange(10); });
        volume_up_button_.OnLongPress([this]() { ScheduleVolumeSet(100); });
        volume_down_button_.OnClick([this]() { ScheduleVolumeChange(-10); });
        volume_down_button_.OnLongPress([this]() { ScheduleVolumeSet(0); });
    }

    void ScheduleVolumeChange(int delta) {
        auto& app = Application::GetInstance();
        app.Schedule([this, delta]() {
            auto* codec = GetAudioCodec();
            const int volume = std::clamp(codec->output_volume() + delta, 0, 100);
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });
    }

    void ScheduleVolumeSet(int volume) {
        auto& app = Application::GetInstance();
        app.Schedule([this, volume]() {
            GetAudioCodec()->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(volume == 0 ? Lang::Strings::MUTED
                                                       : Lang::Strings::MAX_VOLUME);
        });
    }
};

DECLARE_BOARD(AmourK08FourKeysBoard);
