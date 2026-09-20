#include "radio_player.h"

#include "audio_service.h"
#include "board.h"
#include "http.h"

#include <esp_log.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

extern "C" {
#include "decoder/esp_audio_dec_default.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "simple_dec/esp_audio_simple_dec_default.h"
}

namespace {
// Stop() is cooperative. Keeping the read timeout short makes the mode-key
// stop action responsive even while a radio server is temporarily silent.
constexpr int kHttpTimeoutMs = 1000;
constexpr size_t kReadBufferSize = 4096;
constexpr size_t kPcmBufferSize = 8192;
constexpr size_t kTaskStackSize = 8192;
constexpr UBaseType_t kTaskPriority = 4;
const char* TAG = "RadioPlayer";

bool RegisterDecoders() {
    static bool registered = false;
    if (!registered) {
        esp_audio_dec_register_default();
        esp_audio_simple_dec_register_default();
        registered = true;
    }
    return esp_audio_simple_check_audio_type(ESP_AUDIO_SIMPLE_DEC_TYPE_MP3) == ESP_AUDIO_ERR_OK;
}
}  // namespace

RadioPlayer::RadioPlayer(AudioService& audio_service) : audio_service_(audio_service) {}

RadioPlayer::~RadioPlayer() { Stop(); }

bool RadioPlayer::Start(std::string url, std::string title) {
    if (url.rfind("http://", 0) != 0) {
        const uint32_t generation = ++generation_;
        SetState(State::kFailed, "仅支持 HTTP MP3 电台", generation);
        return false;
    }
    Stop();
    const uint32_t generation = ++generation_;
    auto* request = new Request{.player = this, .generation = generation, .url = std::move(url),
                                .title = std::move(title)};
    SetState(State::kConnecting, request->title, generation);
    if (xTaskCreate(TaskEntry, "radio_player", kTaskStackSize, request, kTaskPriority, nullptr) != pdPASS) {
        delete request;
        SetState(State::kFailed, "电台任务创建失败", generation);
        return false;
    }
    return true;
}

void RadioPlayer::Stop() {
    ++generation_;
    // Do not invoke the callback here. A callback is delivered from the
    // application event loop, so an old stream's explicit stop could otherwise
    // arrive after a newly selected station has begun and clear its state.
    if (state_.load() != State::kIdle) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.store(State::kIdle);
        title_.clear();
    }
}

std::string RadioPlayer::GetTitle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return title_;
}

void RadioPlayer::OnStateChanged(std::function<void(State, const std::string&, uint32_t)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
}

bool RadioPlayer::IsCancelled(uint32_t generation) const { return generation != generation_.load(); }

void RadioPlayer::SetState(State state, std::string title, uint32_t generation) {
    std::function<void(State, const std::string&, uint32_t)> callback;
    std::string event_title = title;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != generation_.load()) {
            return;
        }
        state_.store(state);
        if (state == State::kIdle) {
            title_.clear();
        } else if (!title.empty()) {
            title_ = std::move(title);
        }
        callback = callback_;
    }
    if (callback) {
        callback(state, event_title, generation);
    }
}

void RadioPlayer::TaskEntry(void* arg) {
    std::unique_ptr<Request> request(static_cast<Request*>(arg));
    request->player->Run(*request);
    vTaskDelete(nullptr);
}

void RadioPlayer::Run(const Request& request) {
    if (!RegisterDecoders()) {
        if (!IsCancelled(request.generation)) {
            SetState(State::kFailed, "MP3 解码器不可用", request.generation);
        }
        return;
    }
    esp_audio_simple_dec_cfg_t config = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
        .dec_cfg = nullptr,
        .cfg_size = 0,
        .use_frame_dec = false,
    };
    esp_audio_simple_dec_handle_t decoder = nullptr;
    if (esp_audio_simple_dec_open(&config, &decoder) != ESP_AUDIO_ERR_OK || decoder == nullptr) {
        if (!IsCancelled(request.generation)) {
            SetState(State::kFailed, "MP3 解码器启动失败", request.generation);
        }
        return;
    }
    std::unique_ptr<void, decltype(&heap_caps_free)> pcm(
        heap_caps_malloc(kPcmBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), heap_caps_free);
    if (!pcm) {
        esp_audio_simple_dec_close(decoder);
        if (!IsCancelled(request.generation)) {
            SetState(State::kFailed, "PSRAM 音频缓冲不足", request.generation);
        }
        return;
    }

    bool started = false;
    std::string decode_error;
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    if (http) {
        http->SetTimeout(kHttpTimeoutMs);
        http->SetKeepAlive(true);
        http->SetHeader("Accept", "audio/mpeg, audio/mp3, */*");
        http->SetHeader("Icy-MetaData", "0");
        if (http->Open("GET", request.url) && http->GetStatusCode() >= 200 &&
            http->GetStatusCode() < 300) {
            std::array<uint8_t, kReadBufferSize> input{};
            while (!IsCancelled(request.generation)) {
                const int count = http->Read(reinterpret_cast<char*>(input.data()), input.size());
                if (count <= 0) {
                    ESP_LOGW(TAG, "Radio stream ended or failed: %d", http->GetLastError());
                    break;
                }
                esp_audio_simple_dec_raw_t raw = {
                    .buffer = input.data(), .len = static_cast<uint32_t>(count), .eos = false,
                    .consumed = 0, .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
                };
                while (raw.len > 0 && !IsCancelled(request.generation)) {
                    esp_audio_simple_dec_out_t output = {
                        .buffer = static_cast<uint8_t*>(pcm.get()), .len = kPcmBufferSize,
                        .needed_size = 0, .decoded_size = 0,
                    };
                    const auto ret = esp_audio_simple_dec_process(decoder, &raw, &output);
                    if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH || ret != ESP_AUDIO_ERR_OK) {
                        ESP_LOGW(TAG, "MP3 decode failed: %d", ret);
                        raw.len = 0;
                        break;
                    }
                    if (raw.consumed == 0) {
                        break;
                    }
                    raw.buffer += raw.consumed;
                    raw.len -= raw.consumed;
                    if (output.decoded_size == 0) {
                        continue;
                    }
                    esp_audio_simple_dec_info_t info = {};
                    if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK ||
                        info.bits_per_sample != 16 || info.sample_rate == 0 ||
                        (info.channel != 1 && info.channel != 2)) {
                        decode_error = "不支持的 MP3 音频参数";
                        raw.len = 0;
                        break;
                    }
                    const auto* samples = static_cast<const int16_t*>(pcm.get());
                    const size_t sample_count = output.decoded_size / sizeof(int16_t);
                    std::vector<int16_t> mono;
                    if (info.channel == 1) {
                        mono.assign(samples, samples + sample_count);
                    } else {
                        mono.reserve(sample_count / 2);
                        for (size_t i = 0; i + 1 < sample_count; i += 2) {
                            mono.push_back(static_cast<int16_t>((static_cast<int32_t>(samples[i]) + samples[i + 1]) / 2));
                        }
                    }
                    if (!audio_service_.PushPcmToPlaybackQueue(std::move(mono), info.sample_rate)) {
                        raw.len = 0;
                        break;
                    }
                    if (!started && !IsCancelled(request.generation)) {
                        started = true;
                        SetState(State::kPlaying, request.title, request.generation);
                    }
                }
                if (!decode_error.empty()) {
                    break;
                }
            }
        } else {
            ESP_LOGW(TAG, "Radio HTTP request failed: %d", http->GetStatusCode());
        }
        http->Close();
    }
    esp_audio_simple_dec_close(decoder);
    if (!IsCancelled(request.generation)) {
        if (started && decode_error.empty()) {
            // Live radio servers can close an HTTP connection without ending
            // the programme. Treat a post-playback EOF as a reconnect, never
            // as a completed track that returns the device to idle.
            ESP_LOGW(TAG, "Radio stream closed; reconnecting: %s", request.title.c_str());
            SetState(State::kConnecting, request.title, request.generation);
            vTaskDelay(pdMS_TO_TICKS(300));
            if (!IsCancelled(request.generation)) {
                auto* retry = new Request{.player = this,
                                          .generation = request.generation,
                                          .url = request.url,
                                          .title = request.title};
                if (xTaskCreate(TaskEntry, "radio_retry", kTaskStackSize, retry, kTaskPriority,
                                nullptr) == pdPASS) {
                    return;
                }
                delete retry;
            }
            if (!IsCancelled(request.generation)) {
                SetState(State::kFailed, "电台重连失败", request.generation);
            }
        } else {
            SetState(State::kFailed,
                     decode_error.empty() ? "电台连接或解码失败" : std::move(decode_error),
                     request.generation);
        }
    }
}
