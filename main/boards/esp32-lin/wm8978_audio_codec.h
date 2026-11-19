#ifndef _WM8978_AUDIO_CODEC_H_
#define _WM8978_AUDIO_CODEC_H_

#include "audio_codec.h"

#include <atomic>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_timer.h>
#include <mutex>
#include <vector>

// Board-specific driver for the Wolfson/Cirrus WM8978 codec used on the
// WhyEngineer Audio Board ESP32-LIN. The WM8978 uses a 2-wire control
// interface where the first byte carries the 7-bit register address plus the
// 9th data bit, so it cannot use the generic I2cDevice register helpers.
class Wm8978AudioCodec : public AudioCodec {
private:
    i2c_master_dev_handle_t i2c_device_ = nullptr;
    std::mutex register_mutex_;
    std::mutex data_mutex_;
    bool use_spk_output_;
    bool codec_available_ = true;
    bool tx_enabled_ = false;
    bool rx_enabled_ = false;
    uint32_t write_fail_streak_ = 0;   // 连续 I2S 写失败计数（兜底A：超时自愈）
    uint32_t i2c_fail_streak_ = 0;     // 连续 I2C 控制写失败计数（兜底B：降级/自恢复）
    void RecoverTxLocked();            // caller must hold data_mutex_（兜底A）
    void ApplyChannelPowerLocked();  // caller must hold data_mutex_
    std::vector<int16_t> tx_buffer_;
    std::vector<int16_t> rx_buffer_;

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout,
                              gpio_num_t din);
    void WriteReg(uint8_t reg, uint16_t value);
    void InitializeCodec();
    void ApplyOutputVolume();

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    Wm8978AudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,
                     gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout,
                     gpio_num_t din, uint8_t i2c_address, bool use_spk_output);
    virtual ~Wm8978AudioCodec();

    virtual void Start() override;
    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
    // WM8978 AUTO-MUTE mutes the analogue output after 1024 consecutive zero
    // samples. Keep this board quirk here, not in the shared AudioService:
    // a sparse +/-1 LSB dither on a silent frame prevents a false mute during
    // TTS sentence pauses; real audio (samples beyond +/-1) is untouched.
    virtual void OutputData(std::vector<int16_t>& data) override;
};

#endif  // _WM8978_AUDIO_CODEC_H_
