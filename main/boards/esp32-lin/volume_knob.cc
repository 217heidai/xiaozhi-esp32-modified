#include "volume_knob.h"

#include "audio_codec.h"
#include "board.h"
#include "config.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstdlib>

#define TAG "VolumeKnob"

namespace {

// GPIO36 (SENSOR_VP) is ADC1 channel 0 and is input-only.
constexpr adc_channel_t kAdcChannel = ADC_CHANNEL_0;
constexpr int kPollIntervalMs = 100;
constexpr int kAveragingWindow = 5;
constexpr int kVolumeStep = 2;                   // ignore ADC noise smaller than 2 volume steps
constexpr int64_t kMinApplyIntervalUs = 300000;  // 300 ms: limits NVS commits
constexpr int kStartupDelayMs = 2000;            // let the app construct the codec

}  // namespace

VolumeKnob::VolumeKnob() {
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_config, &adc_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 init failed: %s", esp_err_to_name(err));
        adc_handle_ = nullptr;
        return;
    }

    // The 10K / 5K divider tops out around 1.1V, so 0 dB attenuation is enough.
    // ADC1 does not conflict with Wi-Fi (only ADC2 does).
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(adc_handle_, kAdcChannel, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 channel 0 config failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(adc_handle_);
        adc_handle_ = nullptr;
        return;
    }

    // Pin to CPU1: CPU0 is saturated by the WakeNet9 inference running in the
    // audio_input task, and an extra runnable task there starves IDLE0 enough
    // to trigger the task watchdog every 20 s. CPU1 only has IDLE1 and Wi-Fi.
    xTaskCreatePinnedToCore(PollTask, "volume_knob", 2560, this, 1, &task_handle_, 1);
    ESP_LOGI(TAG, "Volume knob enabled (GPIO36 / ADC1_CH0, 12-bit, 0 dB, CPU1)");
}

VolumeKnob::~VolumeKnob() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
    }
    if (adc_handle_ != nullptr) {
        adc_oneshot_del_unit(adc_handle_);
    }
}

void VolumeKnob::PollTask(void* arg) { static_cast<VolumeKnob*>(arg)->PollLoop(); }

int VolumeKnob::ReadAveragedRaw() {
    int raw = 0;
    if (adc_oneshot_read(adc_handle_, kAdcChannel, &raw) != ESP_OK) {
        return -1;
    }
    samples_[sample_index_] = raw;
    sample_index_ = (sample_index_ + 1) % kAveragingWindow;
    if (sample_count_ < kAveragingWindow) {
        ++sample_count_;
    }
    long sum = 0;
    for (int i = 0; i < sample_count_; ++i) {
        sum += samples_[i];
    }
    return static_cast<int>(sum / sample_count_);
}

void VolumeKnob::PollLoop() {
    // The codec is a function-local static created by the application on the
    // main task; wait until startup has finished before touching it.
    vTaskDelay(pdMS_TO_TICKS(kStartupDelayMs));
    AudioCodec* codec = Board::GetInstance().GetAudioCodec();

    while (true) {
        int raw = ReadAveragedRaw();
        if (raw >= 0) {
            const int span = VOLUME_KNOB_RAW_MAX - VOLUME_KNOB_RAW_MIN;
            int volume = (raw - VOLUME_KNOB_RAW_MIN) * 100 / span;
            volume = std::clamp(volume, 0, 100);
            // Rough voltage estimate for end-stop calibration from the log.
            const int millivolts = raw * 1100 / 4095;

            if (last_target_ < 0) {
                // Keep the persisted / server-set volume at boot; the knob takes
                // over as soon as it is actually turned.
                last_target_ = volume;
                ESP_LOGI(TAG, "initial raw=%d (~%d mV) -> volume %d (not applied)", raw, millivolts,
                         volume);
            } else if (std::abs(volume - last_target_) >= kVolumeStep) {
                const int64_t now = esp_timer_get_time();
                if (now - last_apply_us_ >= kMinApplyIntervalUs) {
                    codec->SetOutputVolume(volume);
                    last_target_ = volume;
                    last_apply_us_ = now;
                    ESP_LOGI(TAG, "raw=%d (~%d mV) -> SetOutputVolume(%d)", raw, millivolts,
                             volume);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
    }
}
