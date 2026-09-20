#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

class AudioService;

// Direct HTTP MP3 radio player. It deliberately supports only plain MP3
// streams in the first release; HLS, playlists and AAC require separate
// parsers and must not be guessed from a URL.
class RadioPlayer {
public:
    enum class State { kIdle, kConnecting, kPlaying, kFailed };

    explicit RadioPlayer(AudioService& audio_service);
    ~RadioPlayer();

    bool Start(std::string url, std::string title);
    void Stop();
    State GetState() const { return state_.load(); }
    uint32_t GetGeneration() const { return generation_.load(); }
    std::string GetTitle() const;
    void OnStateChanged(std::function<void(State, const std::string&, uint32_t)> callback);

private:
    struct Request {
        RadioPlayer* player;
        uint32_t generation;
        std::string url;
        std::string title;
    };

    AudioService& audio_service_;
    std::atomic<State> state_{State::kIdle};
    std::atomic<uint32_t> generation_{0};
    mutable std::mutex mutex_;
    std::string title_;
    std::function<void(State, const std::string&, uint32_t)> callback_;

    static void TaskEntry(void* arg);
    void Run(const Request& request);
    bool IsCancelled(uint32_t generation) const;
    void SetState(State state, std::string title, uint32_t generation);
};
