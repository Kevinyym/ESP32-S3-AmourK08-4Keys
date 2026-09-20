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
#include "radio/radio_player.h"
#include "settings.h"

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
#include <vector>

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
        InitializeRadioTools();
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
    AmourK08TextDisplay* display_ = nullptr;
    std::atomic<uint32_t> music_search_generation_{0};
    std::atomic<bool> music_worker_running_{false};
    std::mutex music_status_mutex_;
    std::string music_status_ = "idle";
    std::string music_title_;
    std::string music_queue_query_;
    int music_queue_index_ = 0;
    int music_queue_total_ = 0;
    bool nas_music_session_active_ = false;
    std::unique_ptr<RadioPlayer> radio_player_;
    std::mutex radio_mutex_;
    std::string radio_status_ = "idle";
    std::string radio_title_;
    int radio_index_ = 0;
    std::vector<int> radio_favorites_;
    std::atomic<uint32_t> radio_session_generation_{0};
    std::atomic<uint32_t> active_radio_player_generation_{0};
    std::atomic<bool> radio_external_session_active_{false};

    struct RadioStation {
        const char* name;
        const char* category;
        const char* url;
    };
    // These direct HTTP MP3 streams are shared with the local K08 NetRadio
    // reference project. Keep the initial directory music-focused so a name
    // can be selected reliably by voice.
    static constexpr std::array<RadioStation, 31> kRadioStations = {{
        {"清晨音乐台", "音乐", "http://lhttp.qtfm.cn/live/4915/64k.mp3"},
        {"怀旧好声音", "怀旧", "http://lhttp.qtfm.cn/live/1223/64k.mp3"},
        {"浙江音乐调频", "音乐", "http://lhttp.qtfm.cn/live/4866/64k.mp3"},
        {"成都年代音乐", "怀旧", "http://lhttp.qtfm.cn/live/20211686/64k.mp3"},
        {"厦门音乐广播", "音乐", "http://lhttp.qtfm.cn/live/1739/64k.mp3"},
        {"上海经典947", "古典", "http://lhttp.qtfm.cn/live/267/64k.mp3"},
        {"山东经典音乐广播", "经典", "http://lhttp.qtfm.cn/live/20240/64k.mp3"},
        {"年代音乐1022", "怀旧", "http://lhttp.qtfm.cn/live/20500066/64k.mp3"},
        {"湖北经典音乐广播", "经典", "http://lhttp.qtfm.cn/live/1296/64k.mp3"},
        {"天津TIKI FM", "音乐", "http://lhttp.qtfm.cn/live/20003/64k.mp3"},
        {"四川城市之音", "音乐", "http://lhttp.qtfm.cn/live/1111/64k.mp3"},
        {"江苏音乐广播", "音乐", "http://lhttp.qtfm.cn/live/4936/64k.mp3"},
        {"长沙城市之声", "音乐", "http://lhttp.qtfm.cn/live/4237/64k.mp3"},
        {"安徽音乐广播", "音乐", "http://lhttp.qtfm.cn/live/1947/64k.mp3"},
        {"北京音乐广播", "音乐", "http://lhttp.qtfm.cn/live/332/64k.mp3"},
        {"广东音乐之声", "音乐", "http://lhttp.qtfm.cn/live/1260/64k.mp3"},
        {"上海LoveRadio", "流行", "http://lhttp.qtfm.cn/live/273/64k.mp3"},
        {"上海动感101", "流行", "http://lhttp.qtfm.cn/live/274/64k.mp3"},
        {"苏州音乐广播", "音乐", "http://lhttp.qtfm.cn/live/2803/64k.mp3"},
        {"深圳飞扬971", "音乐", "http://lhttp.qtfm.cn/live/1271/64k.mp3"},
        {"AsiaFM亚洲粤语台", "粤语", "http://lhttp.qtfm.cn/live/15318569/64k.mp3"},
        {"500首华语经典", "经典", "http://lhttp.qtfm.cn/live/5022308/64k.mp3"},
        {"杭州FM90.7", "音乐", "http://lhttp.qtfm.cn/live/15318146/64k.mp3"},
        {"欧美音乐88.7", "国际音乐", "http://lhttp.qtfm.cn/live/15318703/64k.mp3"},
        {"上海KFM981", "流行", "http://lhttp.qtfm.cn/live/5022023/64k.mp3"},
        {"80后音悦台", "怀旧", "http://lhttp.qtfm.cn/live/20207761/64k.mp3"},
        {"经典FM1008", "经典", "http://lhttp.qtfm.cn/live/20212227/64k.mp3"},
        {"AsiaFM亚洲经典台", "经典", "http://lhttp.qtfm.cn/live/5021912/64k.mp3"},
        {"Easy FM", "国际音乐", "http://lhttp.qtfm.cn/live/5022391/64k.mp3"},
        {"中国校园之声", "综合", "http://lhttp.qtfm.cn/live/20091/64k.mp3"},
        {"亚洲音乐台", "音乐", "http://lhttp.qtfm.cn/live/5022405/64k.mp3"},
    }};

    struct MusicSearchRequest {
        AmourK08FourKeysBoard* board;
        uint32_t generation;
        std::string query;
        bool play;
        int offset;
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
                              "&limit=5&offset=" + std::to_string(request.offset);
            if (http->Open("GET", url)) {
                int status = http->GetStatusCode();
                if (status >= 200 && status < 300) {
                    // A successful HTTP response is valid until a concrete
                    // read or JSON validation error says otherwise.
                    error.clear();
                    std::array<char, 1024> buffer;
                    const int64_t deadline = esp_timer_get_time() + 10000000;
                    // The ESP HTTP wrapper may expose a zero body length for a
                    // keep-alive response even though the server sent a valid
                    // Content-Length. Read to the wrapper's end marker instead.
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
                            // Some keep-alive connections report a TCP close
                            // after the final body bytes. Let JSON validation
                            // decide whether the already collected response is
                            // complete; a truly partial response will fail that
                            // validation below.
                            if (response.empty()) {
                                error = "读取搜索结果失败";
                            } else {
                                ESP_LOGW(TAG, "Music search read ended after %u bytes",
                                         static_cast<unsigned>(response.size()));
                            }
                            break;
                        }
                        if (count == 0) {
                            break;
                        }
                        response.append(buffer.data(), count);
                    }
                    if (response.size() > kMaxResponseBytes) {
                        error = "搜索结果过大";
                    }
                    if (error.empty() && response.empty()) {
                        error = "搜索响应为空";
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
            if (root == nullptr) {
                error = "搜索结果格式错误";
            }
            cJSON* tracks = root ? cJSON_GetObjectItem(root, "tracks") : nullptr;
            cJSON* total = root ? cJSON_GetObjectItem(root, "total") : nullptr;
            if (cJSON_IsNumber(total) && total->valuedouble >= 0 && total->valuedouble <= 10000) {
                total_matches = total->valueint;
            }
            if (error.empty() && !cJSON_IsArray(tracks)) {
                error = "没有找到可播放歌曲";
            } else if (error.empty()) {
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
        {
            std::lock_guard<std::mutex> lock(music_status_mutex_);
            music_status_ = "accepted";
            music_title_ = title;
            music_queue_query_ = request.query;
            music_queue_index_ = request.offset;
            music_queue_total_ = total_matches;
        }
        Application::GetInstance().PlayMusic("http://10.0.0.228:8090/tracks/" + id + ".ogg", title,
                                             [this, generation = request.generation]() {
                                                 return generation ==
                                                        music_search_generation_.load();
                                             });
    }

    ReturnValue StartMusicSearch(const std::string& query, bool play, int offset = 0) {
        if (query.empty() || query.size() > 256) {
            return std::string("error: query must be 1-256 bytes");
        }
        bool expected = false;
        if (!music_worker_running_.compare_exchange_strong(expected, true)) {
            return std::string("busy: a search is already running");
        }
        uint32_t generation = ++music_search_generation_;
        SetMusicStatus("searching", query);
        auto* request = new MusicSearchRequest{this, generation, query, play, offset};
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
        app.RegisterNasMusicPlaybackCallback([this](MusicPlaybackState state) {
            std::string title;
            {
                std::lock_guard<std::mutex> lock(music_status_mutex_);
                nas_music_session_active_ = state == MusicPlaybackState::kPending ||
                                            state == MusicPlaybackState::kPlaying;
                title = music_title_;
            }
            ESP_LOGI(TAG, "NAS player page update: state=%d, title='%s'",
                     static_cast<int>(state), title.c_str());
            if (state == MusicPlaybackState::kPending) {
                display_->ShowNasMusicPlayer(title.c_str(), true);
            } else if (state == MusicPlaybackState::kPlaying) {
                display_->ShowNasMusicPlayer(title.c_str(), false);
            } else {
                display_->HideNasMusicPlayer();
            }
        });
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
        mcp.AddTool("self.music.stop",
                    "停止当前本机媒体：NAS 本地音乐或网络电台。用户说停止播放、暂停、不要听了、"
                    "关掉音乐或电台时调用；即使当前播放的是网络电台也必须调用。",
                    PropertyList(),
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

    void SetRadioStatus(std::string status, std::string title = {}) {
        std::lock_guard<std::mutex> lock(radio_mutex_);
        radio_status_ = std::move(status);
        if (!title.empty() || radio_status_ == "idle") {
            radio_title_ = std::move(title);
        }
    }

    void ShowRadioPlayer(int index, bool connecting) {
        if (index < 0 || index >= static_cast<int>(kRadioStations.size())) {
            return;
        }
        bool favorite = false;
        {
            std::lock_guard<std::mutex> lock(radio_mutex_);
            favorite = std::find(radio_favorites_.begin(), radio_favorites_.end(), index) !=
                       radio_favorites_.end();
        }
        const auto& station = kRadioStations[static_cast<size_t>(index)];
        display_->ShowRadioPlayer(station.name, station.category, connecting, index + 1,
                                  static_cast<int>(kRadioStations.size()), favorite);
    }

    int FindRadioStation(const std::string& query) const {
        if (query.empty()) {
            return -1;
        }
        for (size_t index = 0; index < kRadioStations.size(); ++index) {
            const std::string name(kRadioStations[index].name);
            if (name.find(query) != std::string::npos || query.find(name) != std::string::npos) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    void LoadRadioFavorites() {
        Settings settings("k08_radio");
        const std::string saved = settings.GetString("favorites");
        size_t begin = 0;
        while (begin < saved.size()) {
            const size_t end = saved.find('|', begin);
            const std::string name = saved.substr(begin, end == std::string::npos ? std::string::npos
                                                                              : end - begin);
            const int index = FindRadioStation(name);
            if (index >= 0 && std::find(radio_favorites_.begin(), radio_favorites_.end(), index) ==
                                  radio_favorites_.end()) {
                radio_favorites_.push_back(index);
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }
    }

    void SaveRadioFavoritesLocked() const {
        std::string saved;
        for (int index : radio_favorites_) {
            if (index < 0 || index >= static_cast<int>(kRadioStations.size())) {
                continue;
            }
            if (!saved.empty()) {
                saved += '|';
            }
            saved += kRadioStations[static_cast<size_t>(index)].name;
        }
        Settings settings("k08_radio", true);
        settings.SetString("favorites", saved);
    }

    ReturnValue AddCurrentRadioFavorite() {
        int index = -1;
        bool connecting = false;
        {
            std::lock_guard<std::mutex> lock(radio_mutex_);
            if (radio_status_ != "pending" && radio_status_ != "connecting" &&
                radio_status_ != "playing") {
                return std::string("error: no radio station is playing");
            }
            if (std::find(radio_favorites_.begin(), radio_favorites_.end(), radio_index_) !=
                radio_favorites_.end()) {
                return std::string("already favorite: ") +
                       kRadioStations[static_cast<size_t>(radio_index_)].name;
            }
            constexpr size_t kMaxRadioFavorites = 12;
            if (radio_favorites_.size() >= kMaxRadioFavorites) {
                return std::string("error: favorites list is full");
            }
            index = radio_index_;
            connecting = radio_status_ != "playing";
            radio_favorites_.push_back(index);
            SaveRadioFavoritesLocked();
        }
        ShowRadioPlayer(index, connecting);
        return std::string("added favorite: ") + kRadioStations[static_cast<size_t>(index)].name;
    }

    ReturnValue RemoveRadioFavorite(const std::string& station) {
        int index = -1;
        bool update_view = false;
        bool connecting = false;
        std::string name;
        {
            std::lock_guard<std::mutex> lock(radio_mutex_);
            if (station.empty()) {
                if (radio_status_ == "pending" || radio_status_ == "connecting" ||
                    radio_status_ == "playing") {
                    index = radio_index_;
                }
            } else {
                index = FindRadioStation(station);
            }
            if (index < 0) {
                return std::string("error: station not found");
            }
            const auto favorite = std::find(radio_favorites_.begin(), radio_favorites_.end(), index);
            if (favorite == radio_favorites_.end()) {
                return std::string("error: station is not a favorite");
            }
            name = kRadioStations[static_cast<size_t>(index)].name;
            update_view = index == radio_index_ &&
                          (radio_status_ == "pending" || radio_status_ == "connecting" ||
                           radio_status_ == "playing");
            connecting = radio_status_ != "playing";
            radio_favorites_.erase(favorite);
            SaveRadioFavoritesLocked();
        }
        if (update_view) {
            ShowRadioPlayer(index, connecting);
        }
        return std::string("removed favorite: ") + name;
    }

    ReturnValue ListRadioFavorites() {
        std::lock_guard<std::mutex> lock(radio_mutex_);
        if (radio_favorites_.empty()) {
            return std::string("no favorite radio stations");
        }
        std::string names;
        for (int index : radio_favorites_) {
            if (index < 0 || index >= static_cast<int>(kRadioStations.size())) {
                continue;
            }
            if (!names.empty()) {
                names += "、";
            }
            names += kRadioStations[static_cast<size_t>(index)].name;
        }
        return std::string("favorites ") + std::to_string(radio_favorites_.size()) + ": " + names;
    }

    int FindFavoriteRadioStation(const std::string& station) {
        std::lock_guard<std::mutex> lock(radio_mutex_);
        if (radio_favorites_.empty()) {
            return -1;
        }
        if (station.empty()) {
            return radio_favorites_.front();
        }
        const int index = FindRadioStation(station);
        return std::find(radio_favorites_.begin(), radio_favorites_.end(), index) !=
                       radio_favorites_.end()
                   ? index
                   : -1;
    }

    ReturnValue SearchRadioStations(const std::string& query) const {
        std::string results;
        int matches = 0;
        constexpr int kMaxListedStations = 8;
        for (const auto& station : kRadioStations) {
            const std::string name(station.name);
            const std::string category(station.category);
            if (!query.empty() && name.find(query) == std::string::npos &&
                category.find(query) == std::string::npos && query.find(name) == std::string::npos) {
                continue;
            }
            ++matches;
            if (matches <= kMaxListedStations) {
                if (!results.empty()) {
                    results += "、";
                }
                results += name;
            }
        }
        if (matches == 0) {
            return std::string("no station found in the preset directory");
        }
        auto response = std::string("found ") + std::to_string(matches) + ": " + results;
        if (matches > kMaxListedStations) {
            response += "…";
        }
        return response;
    }

    int PickRandomRadioStation(const std::string& category) const {
        int candidates = 0;
        for (const auto& station : kRadioStations) {
            if (category.empty() || category == station.category) {
                ++candidates;
            }
        }
        if (candidates == 0) {
            return -1;
        }

        // A radio choice does not require cryptographic randomness. The microsecond
        // timer prevents consecutive requests from always selecting the first item.
        int selected = static_cast<int>(esp_timer_get_time() % candidates);
        for (size_t index = 0; index < kRadioStations.size(); ++index) {
            if (!category.empty() && category != kRadioStations[index].category) {
                continue;
            }
            if (selected-- == 0) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    ReturnValue PlayRadioStation(int index) {
        if (index < 0 || index >= static_cast<int>(kRadioStations.size())) {
            return std::string("error: station not found");
        }
        const auto station = kRadioStations[static_cast<size_t>(index)];
        const uint32_t session_generation = ++radio_session_generation_;
        const bool switch_existing_stream = radio_external_session_active_.load();
        // Invalidate callbacks from the old stream immediately. The actual player
        // generation is recorded only after the new HTTP task has been created.
        active_radio_player_generation_.store(0);
        {
            std::lock_guard<std::mutex> lock(radio_mutex_);
            radio_index_ = index;
            radio_status_ = "pending";
            radio_title_ = station.name;
        }
        ShowRadioPlayer(index, true);

        if (switch_existing_stream) {
            // The first station uses Application::PlayExternalMedia so it can
            // close the cloud voice channel. Subsequent switches must stay in
            // that same external-media session: restarting the application
            // lifecycle on every station made repeated key presses return to idle.
            Application::GetInstance().Schedule([this, station, session_generation]() {
                if (session_generation != radio_session_generation_.load() || !radio_player_) {
                    return;
                }
                active_radio_player_generation_.store(0);
                SetRadioStatus("connecting", station.name);
                if (!radio_player_->Start(station.url, station.name)) {
                    radio_external_session_active_.store(false);
                    SetRadioStatus("error", "电台播放启动失败");
                    display_->HideRadioPlayer();
                    Application::GetInstance().FinishExternalMedia(false, "电台播放启动失败");
                    return;
                }
                active_radio_player_generation_.store(radio_player_->GetGeneration());
            });
            return std::string("accepted: switching to ") + station.name;
        }

        Application::GetInstance().PlayExternalMedia(
            station.name,
            [this, station]() {
                SetRadioStatus("connecting", station.name);
                if (!radio_player_ || !radio_player_->Start(station.url, station.name)) {
                    return false;
                }
                active_radio_player_generation_.store(radio_player_->GetGeneration());
                radio_external_session_active_.store(true);
                return true;
            },
            [this]() {
                if (radio_player_) {
                    radio_player_->Stop();
                }
                // A user stop invalidates the current UI session. During a switch,
                // PlayRadioStation has already set this to zero, so the new player
                // page remains visible while the old stream is being cancelled.
                if (active_radio_player_generation_.exchange(0) != 0) {
                    ++radio_session_generation_;
                    display_->HideRadioPlayer();
                }
                radio_external_session_active_.store(false);
                SetRadioStatus("idle");
            });
        return std::string("accepted: starting ") + station.name;
    }

    void InitializeRadioTools() {
        LoadRadioFavorites();
        radio_player_ = std::make_unique<RadioPlayer>(Application::GetInstance().GetAudioService());
        radio_player_->OnStateChanged(
            [this](RadioPlayer::State state, const std::string& title, uint32_t player_generation) {
            if (player_generation != active_radio_player_generation_.load()) {
                ESP_LOGD(TAG, "Ignoring stale radio state callback, generation=%lu",
                         static_cast<unsigned long>(player_generation));
                return;
            }
            const uint32_t session_generation = radio_session_generation_.load();
            if (state == RadioPlayer::State::kConnecting) {
                SetRadioStatus("connecting", title);
                const int index = FindRadioStation(title);
                ShowRadioPlayer(index, true);
            } else if (state == RadioPlayer::State::kPlaying) {
                SetRadioStatus("playing", title);
                const int index = FindRadioStation(title);
                ShowRadioPlayer(index, false);
            } else {
                Application::GetInstance().Schedule(
                    [this, state, title, player_generation, session_generation]() {
                        if (player_generation != active_radio_player_generation_.load() ||
                            session_generation != radio_session_generation_.load()) {
                            return;
                        }
                        active_radio_player_generation_.store(0);
                        radio_external_session_active_.store(false);
                        if (state == RadioPlayer::State::kFailed) {
                            SetRadioStatus("error", title);
                            display_->HideRadioPlayer();
                            Application::GetInstance().FinishExternalMedia(false, title);
                        } else {
                            SetRadioStatus("idle");
                            display_->HideRadioPlayer();
                            Application::GetInstance().FinishExternalMedia(true);
                        }
                    });
            }
        });

        auto& mcp = McpServer::GetInstance();
        mcp.AddTool("self.radio.play",
                    "播放预置网络电台。station 填写电台名称，例如上海动感101、北京音乐广播；"
                    "仅支持当前目录中的 HTTP MP3 电台。",
                    PropertyList({Property("station", kPropertyTypeString)}),
                    [this](const PropertyList& properties) -> ReturnValue {
                        const auto station = properties["station"].value<std::string>();
                        const int index = FindRadioStation(station);
                        if (index < 0) {
                            return std::string("error: station not found in the preset directory");
                        }
                        return PlayRadioStation(index);
                    });
        mcp.AddTool("self.radio.search",
                    "搜索预置网络电台目录，按名称或分类返回最多八个匹配节目。用于回答有什么电台或"
                    "在播放前确认台名；query 可填写上海、经典、怀旧、粤语或国际音乐等关键词。",
                    PropertyList({Property("query", kPropertyTypeString)}),
                    [this](const PropertyList& properties) -> ReturnValue {
                        return SearchRadioStations(properties["query"].value<std::string>());
                    });
        mcp.AddTool("self.radio.random",
                    "随机播放预置网络电台。category 可留空，或填写音乐、经典、怀旧、流行、"
                    "粤语、古典、国际音乐、综合；用于用户说随便放一台或想听某类电台。",
                    PropertyList({Property("category", kPropertyTypeString, "")}),
                    [this](const PropertyList& properties) -> ReturnValue {
                        const auto category = properties["category"].value<std::string>();
                        const int index = PickRandomRadioStation(category);
                        if (index < 0) {
                            return std::string("error: category not found in the preset directory");
                        }
                        return PlayRadioStation(index);
                    });
        mcp.AddTool("self.radio.favorite_add",
                    "收藏当前正在播放或连接中的网络电台。用户说收藏这台、加入电台收藏时调用。",
                    PropertyList(), [this](const PropertyList&) -> ReturnValue {
                        return AddCurrentRadioFavorite();
                    });
        mcp.AddTool("self.radio.favorite_remove",
                    "取消收藏的网络电台。station 可留空以取消当前电台，或填写收藏中的电台名称。",
                    PropertyList({Property("station", kPropertyTypeString, std::string())}),
                    [this](const PropertyList& properties) -> ReturnValue {
                        return RemoveRadioFavorite(properties["station"].value<std::string>());
                    });
        mcp.AddTool("self.radio.favorite_list", "列出已收藏的网络电台。", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue { return ListRadioFavorites(); });
        mcp.AddTool("self.radio.favorite_play",
                    "播放收藏的网络电台。station 可留空以播放第一台收藏，或填写收藏中的电台名称。",
                    PropertyList({Property("station", kPropertyTypeString, std::string())}),
                    [this](const PropertyList& properties) -> ReturnValue {
                        const int index =
                            FindFavoriteRadioStation(properties["station"].value<std::string>());
                        if (index < 0) {
                            return std::string("error: favorite station not found");
                        }
                        return PlayRadioStation(index);
                    });
        mcp.AddTool("self.radio.next", "切换到预置网络电台目录中的下一台。", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue {
                        int next = 0;
                        {
                            std::lock_guard<std::mutex> lock(radio_mutex_);
                            next = (radio_index_ + 1) % static_cast<int>(kRadioStations.size());
                        }
                        return PlayRadioStation(next);
                    });
        mcp.AddTool("self.radio.previous", "切换到预置网络电台目录中的上一台。", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue {
                        int previous = 0;
                        {
                            std::lock_guard<std::mutex> lock(radio_mutex_);
                            previous = (radio_index_ + static_cast<int>(kRadioStations.size()) - 1) %
                                       static_cast<int>(kRadioStations.size());
                        }
                        return PlayRadioStation(previous);
                    });
        mcp.AddTool("self.radio.stop",
                    "停止当前网络电台。用户说停止电台、关掉电台、不要听电台、停止播放或暂停"
                    "且当前在播电台时必须调用，不能只用文字回复。",
                    PropertyList(),
                    [this](const PropertyList&) -> ReturnValue {
                        Application::GetInstance().StopMusic();
                        SetRadioStatus("idle");
                        return std::string("stopped");
                    });
        mcp.AddTool("self.radio.status", "查询网络电台当前播放、连接或错误状态。", PropertyList(),
                    [this](const PropertyList&) -> ReturnValue {
                        std::lock_guard<std::mutex> lock(radio_mutex_);
                        if (radio_status_ == "playing" || radio_status_ == "connecting" ||
                            radio_status_ == "pending") {
                            return radio_status_ + ": " + radio_title_;
                        }
                        if (radio_status_ == "error") {
                            return std::string("error: ") + radio_title_;
                        }
                        return std::string("idle");
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
        volume_up_button_.OnLongPress([this]() {
            if (IsRadioActive()) {
                ESP_LOGI(TAG, "Long-press +: next radio station");
                ScheduleRadioStep(1);
            } else if (IsNasMusicActive()) {
                ESP_LOGI(TAG, "Long-press +: next NAS music track");
                ScheduleMusicStep(1);
            } else {
                ScheduleVolumeSet(100);
            }
        });
        volume_down_button_.OnClick([this]() { ScheduleVolumeChange(-10); });
        volume_down_button_.OnLongPress([this]() {
            if (IsRadioActive()) {
                ESP_LOGI(TAG, "Long-press -: previous radio station");
                ScheduleRadioStep(-1);
            } else if (IsNasMusicActive()) {
                ESP_LOGI(TAG, "Long-press -: previous NAS music track");
                ScheduleMusicStep(-1);
            } else {
                ScheduleVolumeSet(0);
            }
        });
    }

    bool IsRadioActive() {
        std::lock_guard<std::mutex> lock(radio_mutex_);
        return radio_status_ == "playing";
    }

    bool IsNasMusicActive() {
        const auto playback_state = Application::GetInstance().GetMusicPlaybackState();
        if (playback_state != MusicPlaybackState::kPending &&
            playback_state != MusicPlaybackState::kPlaying) {
            return false;
        }
        std::lock_guard<std::mutex> lock(music_status_mutex_);
        // The application state is the authority here. The UI callback is allowed
        // to be delivered slightly before or after a subtitle update, so using
        // its cached flag alone occasionally made a playing NAS track look idle
        // to the long-press handler.
        return music_queue_total_ > 0 && !music_queue_query_.empty();
    }

    void ScheduleMusicStep(int direction) {
        auto& app = Application::GetInstance();
        app.Schedule([this, direction]() {
            const auto playback_state = Application::GetInstance().GetMusicPlaybackState();
            std::string query;
            int offset = 0;
            {
                std::lock_guard<std::mutex> lock(music_status_mutex_);
                if ((playback_state != MusicPlaybackState::kPending &&
                     playback_state != MusicPlaybackState::kPlaying) ||
                    music_queue_total_ <= 1 || music_queue_query_.empty()) {
                    display_->ShowNotification("没有可切换的歌曲");
                    return;
                }
                offset = (music_queue_index_ + direction + music_queue_total_) % music_queue_total_;
                query = music_queue_query_;
            }
            ESP_LOGI(TAG, "NAS music step: direction=%d, offset=%d, query='%s'",
                     direction, offset, query.c_str());
            // The Ogg player owns the application's notifying state while a
            // track is playing. Stop it first, then queue the lookup after that
            // state transition; otherwise PlayMusic rejects the new track as
            // busy even though the search itself succeeded.
            Application::GetInstance().StopMusic();
            Application::GetInstance().Schedule([this, query = std::move(query), offset]() {
                StartMusicSearch(query, true, offset);
            });
        });
    }

    void ScheduleRadioStep(int direction) {
        auto& app = Application::GetInstance();
        app.Schedule([this, direction]() {
            int index = -1;
            {
                std::lock_guard<std::mutex> lock(radio_mutex_);
                if (radio_status_ != "playing") {
                    return;
                }
                const int count = static_cast<int>(kRadioStations.size());
                index = (radio_index_ + direction + count) % count;
            }
            PlayRadioStation(index);
        });
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
