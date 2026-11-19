#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <model_path.h>
#include "esp_ae_rate_cvt.h"
#include "esp_audio_enc.h"
#include "esp_audio_types.h"
#include "esp_opus_dec.h"
#include "esp_opus_enc.h"

#include "audio_codec.h"
#include "audio_debugger.h"
#include "audio_engine.h"
#include "fixed_queue.h"
#include "ogg_demuxer.h"
#include "protocol.h"

/*
 * There are two types of audio data flow:
 * 1. (MIC) -> [Audio Engine] -> {Encode Queue} -> [Opus Encoder] -> {Send Queue} -> (Server)
 * 2. (Server) -> {Decode Queue} -> [Opus Decoder] -> {Playback Queue} -> (Speaker)
 *
 * We use dedicated tasks for input, output, and Opus encoding/decoding.
 *
 * Decode Queue and Send Queue are the main queues, because Opus packets are quite smaller than PCM
 * packets.
 *
 */

#define OPUS_FRAME_DURATION_MS 60
#define MAX_ENCODE_TASKS_IN_QUEUE 2
// The playback queue doubles as the jitter buffer for incoming TTS packets.
// At 2 frames (120 ms) a UDP packet arriving ~200 ms late drained the queue and
// produced an audible gap. 4 frames (240 ms) removed most gaps but two
// back-to-back out-of-order packets still punched through; 6 frames (360 ms)
// cover that. This firmware has no voice barge-in during speaking, so a deeper
// queue costs only start-up latency (60 ms per frame) and a few KB of SRAM.
#define MAX_PLAYBACK_TASKS_IN_QUEUE 6
#define MAX_DECODE_PACKETS_IN_QUEUE (1200 / OPUS_FRAME_DURATION_MS)
#define MAX_SEND_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)
#define AUDIO_TESTING_MAX_DURATION_MS 10000
#define MAX_TIMESTAMPS_IN_QUEUE 3

#define AUDIO_POWER_TIMEOUT_MS 15000
#define AUDIO_POWER_CHECK_INTERVAL_MS 1000

#define AS_EVENT_AUDIO_TESTING_RUNNING (1 << 0)
#define AS_EVENT_WAKE_WORD_RUNNING (1 << 1)
#define AS_EVENT_AUDIO_PROCESSOR_RUNNING (1 << 2)
#define AS_EVENT_AUDIO_INPUT_STOP_REQUEST (1 << 4)

#define AS_OPUS_GET_FRAME_DRU_ENUM(duration_ms)                  \
    ((duration_ms) == 5     ? ESP_OPUS_ENC_FRAME_DURATION_5_MS   \
     : (duration_ms) == 10  ? ESP_OPUS_ENC_FRAME_DURATION_10_MS  \
     : (duration_ms) == 20  ? ESP_OPUS_ENC_FRAME_DURATION_20_MS  \
     : (duration_ms) == 40  ? ESP_OPUS_ENC_FRAME_DURATION_40_MS  \
     : (duration_ms) == 60  ? ESP_OPUS_ENC_FRAME_DURATION_60_MS  \
     : (duration_ms) == 80  ? ESP_OPUS_ENC_FRAME_DURATION_80_MS  \
     : (duration_ms) == 100 ? ESP_OPUS_ENC_FRAME_DURATION_100_MS \
     : (duration_ms) == 120 ? ESP_OPUS_ENC_FRAME_DURATION_120_MS \
                            : -1)

#define AS_OPUS_ENC_CONFIG()                                                                   \
    {                                                                                          \
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_16K,                                              \
        .channel = ESP_AUDIO_MONO,                                                             \
        .bits_per_sample = ESP_AUDIO_BIT16,                                                    \
        .bitrate = ESP_OPUS_BITRATE_AUTO,                                                      \
        .frame_duration =                                                                      \
            (esp_opus_enc_frame_duration_t)AS_OPUS_GET_FRAME_DRU_ENUM(OPUS_FRAME_DURATION_MS), \
        .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,                                    \
        .complexity = 0,                                                                       \
        .enable_fec = false,                                                                   \
        .enable_dtx = true,                                                                    \
        .enable_vbr = true,                                                                    \
    }

struct AudioServiceCallbacks {
    std::function<void(void)> on_send_queue_available;
    std::function<void(const std::string&)> on_wake_word_detected;
    std::function<void(bool)> on_vad_change;
    std::function<void(void)> on_audio_testing_queue_full;
    // Fired when the decode/playback queues and their in-flight work are drained.
    std::function<void(void)> on_playback_drained;
    std::function<void(uint32_t playback_id, uint32_t media_position_ms)> on_playback_progress;
};

enum AudioTaskType {
    kAudioTaskTypeEncodeToSendQueue,
    kAudioTaskTypeEncodeToTestingQueue,
    kAudioTaskTypeDecodeToPlaybackQueue,
};

struct AudioTask {
    AudioTaskType type;
    std::vector<int16_t> pcm;
    uint32_t timestamp = 0;
    uint32_t playback_id = 0;
    uint32_t media_position_ms = 0;
};

struct DebugStatistics {
    uint32_t input_count = 0;
    uint32_t decode_count = 0;
    uint32_t encode_count = 0;
    uint32_t playback_count = 0;
    uint32_t encode_drop_count = 0;
};

class AudioService {
public:
    AudioService();
    ~AudioService();

    void Initialize(AudioCodec* codec);
    void Start();
    void Stop();
    void EncodeWakeWord();
    std::unique_ptr<AudioStreamPacket> PopWakeWordPacket();
    const std::string& GetLastWakeWord() const;
    std::vector<std::string> GetWakeWords() const;
    bool IsVoiceDetected() const { return voice_detected_; }
    bool IsIdle();
    bool IsPlaybackIdle();
    // 看门狗用：累计成功输出帧数，单调递增、原子可读。播放任务卡死时停止增长。
    uint64_t GetOutputFrameCount() const { return output_frame_count_.load(); }
    bool IsWakeWordRunning() const {
        return xEventGroupGetBits(event_group_) & AS_EVENT_WAKE_WORD_RUNNING;
    }
    bool IsAudioProcessorRunning() const {
        return xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING;
    }
    bool IsAfeWakeWord();

    void EnableWakeWordDetection(bool enable);
    void ReleaseWakeWordResources();
    void EnableVoiceProcessing(bool enable);
    void EnableAudioTesting(bool enable);
    void EnableDeviceAec(bool enable);

    void SetCallbacks(AudioServiceCallbacks& callbacks);

    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait = false);
    std::unique_ptr<AudioStreamPacket> PopPacketFromSendQueue();
    void PlaySound(const std::string_view& sound);
    bool ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples);
    void ResetDecoder();
    void SetModelsList(srmodel_list_t* models_list);

private:
    AudioCodec* codec_ = nullptr;
    AudioServiceCallbacks callbacks_;
    std::unique_ptr<AudioEngine> audio_engine_;
    std::unique_ptr<AudioDebugger> audio_debugger_;
    void* opus_encoder_ = nullptr;
    void* opus_decoder_ = nullptr;
    std::mutex decoder_mutex_;
    std::mutex input_resampler_mutex_;
    esp_ae_rate_cvt_handle_t input_resampler_ = nullptr;
    esp_ae_rate_cvt_handle_t output_resampler_ = nullptr;

    // Encoder/Decoder state
    int encoder_sample_rate_ = 16000;
    int encoder_duration_ms_ = OPUS_FRAME_DURATION_MS;
    int encoder_frame_size_ = 0;
    int encoder_outbuf_size_ = 0;
    int decoder_sample_rate_ = 0;
    int decoder_duration_ms_ = OPUS_FRAME_DURATION_MS;
    int decoder_frame_size_ = 0;
    DebugStatistics debug_statistics_;
    int64_t last_encode_drop_log_time_ = 0;
    srmodel_list_t* models_list_ = nullptr;

    EventGroupHandle_t event_group_;

    // Audio encode / decode
    TaskHandle_t audio_input_task_handle_ = nullptr;
    TaskHandle_t audio_output_task_handle_ = nullptr;
    TaskHandle_t opus_codec_task_handle_ = nullptr;
    std::mutex audio_queue_mutex_;
    std::condition_variable audio_queue_cv_;
    // Testing records up to AUDIO_TESTING_MAX_DURATION_MS, then swaps into the
    // decode queue. Both queues must share this capacity so swap() is valid.
    static constexpr size_t kAudioTestingPacketCapacity =
        AUDIO_TESTING_MAX_DURATION_MS / OPUS_FRAME_DURATION_MS;
    FixedQueue<std::unique_ptr<AudioStreamPacket>, kAudioTestingPacketCapacity> audio_decode_queue_;
    FixedQueue<std::unique_ptr<AudioStreamPacket>, MAX_SEND_PACKETS_IN_QUEUE> audio_send_queue_;
    FixedQueue<std::unique_ptr<AudioStreamPacket>, kAudioTestingPacketCapacity>
        audio_testing_queue_;
    FixedQueue<AudioTask, MAX_ENCODE_TASKS_IN_QUEUE> audio_encode_queue_;
    FixedQueue<AudioTask, MAX_PLAYBACK_TASKS_IN_QUEUE> audio_playback_queue_;
    bool decode_in_flight_ = false;
    bool output_in_flight_ = false;
    bool playback_drained_notified_ = true;

    // Jitter-buffer pre-roll. Under a steady 60 ms packet rate the playback
    // queue stays almost empty, so merely raising MAX_PLAYBACK_TASKS_IN_QUEUE
    // never creates headroom. The server numbers UDP packets by 60 ms time
    // slots (timestamp == sequence * 60) and skips slots at TTS sentence
    // boundaries (docs/mqtt-udp.md 4.3 explicitly tolerates sequence jumps).
    // Deliberately filling the queue before consuming the first frame gives the
    // output path a small reserve when a stream (re)starts. It applies **only
    // before the first real frame of a generation**: re-arming it mid-stream
    // turned every transient empty (steady state depth is ~0) into 4 frames
    // (240 ms) of injected silence in place of already-queued audio, which was
    // heard as "dropped frames". Mid-stream gaps are handled by the silence
    // keep-alive below, not by pre-roll.
    static constexpr size_t kPlaybackPrefillFrames = 4;
    // A short reply can decode to fewer than kPlaybackPrefillFrames frames and
    // then stop. Waiting forever for frame 4 stalled it until the watchdog killed
    // the session. Once the first frame is queued, start within this window even
    // if the queue never fills (250 ms ≈ the normal 4-frame pre-roll time).
    static constexpr int64_t kPrefillTimeoutUs = 250 * 1000;
    bool prefill_pending_ = true;
    int64_t prefill_first_frame_us_ = 0;

    // The server numbers UDP packets by 60 ms time slots and skips slots at
    // TTS sentence boundaries (docs/mqtt-udp.md 4.3). When the playback queue
    // runs dry mid-stream, keep I2S fed with silence instead of stopping and
    // restarting the output stream. Stop after 1 s with no real frame so an
    // idle device does not keep the DAC running forever.
    int64_t last_real_frame_us_ = 0;
    uint32_t playback_generation_ = 0;
    // For server AEC
    FixedQueue<uint32_t, MAX_TIMESTAMPS_IN_QUEUE> timestamp_queue_;

    bool audio_engine_initialized_ = false;
    bool voice_detected_ = false;
#if CONFIG_USE_DEVICE_AEC
    bool device_aec_enabled_ = true;
#else
    bool device_aec_enabled_ = false;
#endif
    std::atomic<bool> service_stopped_{true};
    std::atomic<bool> audio_input_need_warmup_{false};
    std::atomic<uint64_t> output_frame_count_{0};  // 成功输出帧累计（看门狗判据）

    esp_timer_handle_t audio_power_timer_ = nullptr;
    std::chrono::steady_clock::time_point last_input_time_;
    std::chrono::steady_clock::time_point last_output_time_;

    void AudioInputTask();
    void AudioOutputTask();
    void OpusCodecTask();
    void PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm);
    bool InitializeAudioEngine();
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    void CheckAndUpdateAudioPowerState();
    bool IsPlaybackDrainedLocked() const;
    bool MarkPlaybackDrainedLocked();
};

#endif
