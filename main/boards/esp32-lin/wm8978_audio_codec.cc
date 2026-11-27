#include "wm8978_audio_codec.h"
#include "config.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cassert>
#include <cstring>

#define TAG "Wm8978AudioCodec"

namespace {

// The register values below reproduce the final configuration of the vendor
// BSP (components/bsp/wm8978.c + bsp.c) that is known to bring up this board:
// 16-bit I2S slave, MCLK from the pin, MIC -> ADC -> DAC -> output mixer.
// NOTE: the vendor BSP uses a heavily attenuated DAC digital volume (see
// kRegLeftDacVolume) and a very low headphone level (HPvol_Set(15,15)). Our
// kMaxAnalogGain is 20~35 dB above that, which is now the prime suspect for
// the residual hiss; see NOISE-ROOT-CAUSE.md before touching either value.
constexpr uint8_t kRegReset = 0;
constexpr uint8_t kRegPower1 = 1;
constexpr uint8_t kRegPower2 = 2;
constexpr uint8_t kRegPower3 = 3;
constexpr uint8_t kRegAudioInterface = 4;
constexpr uint8_t kRegClocking = 6;
constexpr uint8_t kRegDacControl = 10;
constexpr uint8_t kRegLeftDacVolume = 11;
constexpr uint8_t kRegRightDacVolume = 12;
constexpr uint8_t kRegAdcControl = 14;
constexpr uint8_t kRegEq1 = 18;
constexpr uint8_t kRegBeepControl = 43;
constexpr uint8_t kRegInputControl = 44;
constexpr uint8_t kRegLeftInpPga = 45;
constexpr uint8_t kRegRightInpPga = 46;
constexpr uint8_t kRegLeftAdcBoost = 47;
constexpr uint8_t kRegRightAdcBoost = 48;
constexpr uint8_t kRegOutputControl = 49;
constexpr uint8_t kRegLeftMixer = 50;
constexpr uint8_t kRegRightMixer = 51;
constexpr uint8_t kRegLout1Volume = 52;
constexpr uint8_t kRegRout1Volume = 53;
constexpr uint8_t kRegLout2Volume = 54;
constexpr uint8_t kRegRout2Volume = 55;

// bit 8 of the volume pair registers latches the new volume into both channels
constexpr uint16_t kVolumeUpdate = 0x100;
// bit 6 mutes the output when no volume is requested
constexpr uint8_t kVolumeMute = 0x40;

// R10 DAC Control value: bit3 DACOSR128 (128x oversampling, best SNR) +
// bit2 AMUTE (hardware auto-mute). Per WM8978 datasheet, AMUTE applies an
// ANALOGUE mute once the DAC sees 1024 consecutive zero samples (~21 ms at
// 24 kHz stereo) and releases it as soon as a non-zero sample arrives.
// This is exactly the standby-hiss case: with auto_clear_after_cb the I2S TX
// sends zeros whenever the app has no frame, so the codec mutes itself during
// non-playback time without any software state/timer guessing. The judgement
// is DATA CONTENT, not elapsed time, so it cannot false-mute a TTS sentence
// pause the way the old idle-mute timer did (see VERIFY-knob.md).
// bit6 SOFTMUTE is intentionally left 0: its polarity conflicts between
// datasheet revisions (see NOISE-ROOT-CAUSE.md H2). Was 0x008 before 2026-11-16.
constexpr uint8_t kDacControlValue = 0x00C;

// Top of the analog output gain range (LOUT1/ROUT1 and LOUT2/ROUT2 are 0..63).
// The WM8978 output driver's noise floor scales with this gain, so running it
// at the 63 maximum makes the quiet/idle noise audible as a hiss at high volume
// (even while playing, during the silent gaps). Capping it trades a little peak
// loudness for a clean noise floor. Tune 40..63: raise for louder max, lower if
// any hiss remains.
// 2026-11-16 增益收敛记录：50 -> 60（嫌最大音量低）-> 58 -> 56（仍有底噪）-> 50。
// 50 是 11-12 当时确认「干净」的锚点，但 11-16 复测仍有噪音，未闭环。
// ⚠️ 数值更正：datasheet R52/R53 是 1 dB/档（0=-57dB、57=0dB、63=+6dB），
// 不是早期注释写的 0.75 dB；故 50 = -7 dB、60 = +3 dB，60->50 实降 10 dB。
// 厂商 BSP 的耳机音量只有 15（-42 dB），我们默认 vol=70 时是 35（-22 dB），
// 高了 20 dB；叠加 DAC 数字音量 180（-37.5 dB）的过度衰减，
// 形成「数字域白衰减 + 模拟域被迫开大」的错配，噪声 floor 被一起放大。
// 继续降档无意义：应在数字域给足电平、模拟域压低，或启用芯片 AUTO-MUTE。
// 完整根因、判别实验与方案排序见 NOISE-ROOT-CAUSE.md（本轮未改任何寄存器值）。
constexpr uint8_t kMaxAnalogGain = 50;

// --- 长期稳定运行兜底常量 -------------------------------------------------
// 兜底A：I2S 写绝不用 portMAX_DELAY 无限等。全双工时钟/DMA 若意外停摆，
// 有界超时能让播放任务活着返回，而不是整条音频链永久卡死。
constexpr uint32_t kWriteTimeoutMs = 200;
// 连续写失败达此次数，重建 TX 通道自愈（避免偶发单次抖动就折腾硬件）。
constexpr uint32_t kWriteFailRecoverThreshold = 20;
// 兜底B：I2C 控制写偶发毛刺不永久拉死 codec；失败重试几次，之后每次
// 调用仍会再尝试，一旦成功即自恢复。仅在持续失败达阈值才降级告警。
constexpr int kI2cMaxRetries = 3;
constexpr uint32_t kI2cFailDegradeThreshold = 10;

}  // namespace

Wm8978AudioCodec::Wm8978AudioCodec(void* i2c_master_handle, int input_sample_rate,
                                   int output_sample_rate, gpio_num_t mclk, gpio_num_t bclk,
                                   gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                                   uint8_t i2c_address, bool use_spk_output)
    : use_spk_output_(use_spk_output) {
    assert(input_sample_rate == output_sample_rate);

    duplex_ = true;
    input_reference_ = false;
    input_channels_ = 1;
    output_channels_ = 1;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_gain_ = 25;

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_address,
        .scl_speed_hz = 100 * 1000,
        .scl_wait_us = 0,
        .flags =
            {
                .disable_ack_check = 0,
            },
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(
        static_cast<i2c_master_bus_handle_t>(i2c_master_handle), &device_config, &i2c_device_));

    CreateDuplexChannels(mclk, bclk, ws, dout, din);
    InitializeCodec();
    ApplyOutputVolume();

    ESP_LOGI(
        TAG, "WM8978 initialized (%d Hz, %s output, input %s)", output_sample_rate_,
        use_spk_output_ ? "speaker LOUT2/ROUT2" : "headphone LOUT1/ROUT1",
        AUDIO_CODEC_INPUT_MODE == 2 ? "mix" : (AUDIO_CODEC_INPUT_MODE == 1 ? "right" : "left"));
}

Wm8978AudioCodec::~Wm8978AudioCodec() {
    if (rx_handle_ != nullptr) {
        i2s_channel_disable(rx_handle_);
    }
    if (tx_handle_ != nullptr) {
        i2s_channel_disable(tx_handle_);
    }
}

void Wm8978AudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
                                            gpio_num_t dout, gpio_num_t din) {
    i2s_chan_config_t chan_cfg = {
        .id = XIAOZHI_I2S_PORT(0),
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg =
            {
                .sample_rate_hz = (uint32_t)output_sample_rate_,
                .clk_src = I2S_CLK_SRC_DEFAULT,
#if SOC_I2S_HW_VERSION_2
                .ext_clk_freq_hz = 0,
#endif
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            },
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                     .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                     .slot_mode = I2S_SLOT_MODE_STEREO,
                     .slot_mask = I2S_STD_SLOT_BOTH,
                     .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
                     .ws_pol = false,
                     .bit_shift = true,
#if SOC_I2S_HW_VERSION_2
                     .left_align = true,
                     .big_endian = false,
                     .bit_order_lsb = false
#endif
        },
        .gpio_cfg = {.mclk = mclk,
                     .bclk = bclk,
                     .ws = ws,
                     .dout = dout,
                     .din = din,
                     .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_LOGI(TAG, "Duplex I2S channels created (MCLK on GPIO%d)", mclk);
}

void Wm8978AudioCodec::WriteReg(uint8_t reg, uint16_t value) {
    // WM8978 2-wire format: [register address << 1 | D8] [D7..D0]
    uint8_t buffer[2] = {
        static_cast<uint8_t>((reg << 1) | ((value >> 8) & 0x01)),
        static_cast<uint8_t>(value & 0xFF),
    };
    // 兜底B：I2C 毛刺重试，绝不把 codec_available_ 一锤子置死。这里始终
    // 尝试写入（不 early-return），成功即自恢复；连续失败计数仅用于日志节流
    // 和降级告警，不阻断后续任何一次寄存器/音量写入。
    esp_err_t err = ESP_OK;
    for (int attempt = 0; attempt < kI2cMaxRetries; ++attempt) {
        err = i2c_master_transmit(i2c_device_, buffer, sizeof(buffer), 100);
        if (err == ESP_OK) {
            break;
        }
    }
    if (err != ESP_OK) {
        if (++i2c_fail_streak_ <= 3 || i2c_fail_streak_ % kI2cFailDegradeThreshold == 0) {
            ESP_LOGW(TAG, "WM8978 I2C write reg %u failed: %s (streak=%u)", reg,
                     esp_err_to_name(err), i2c_fail_streak_);
        }
        if (i2c_fail_streak_ >= kI2cFailDegradeThreshold) {
            codec_available_ = false;  // 纯诊断标记，不门禁写入
        }
    } else {
        if (i2c_fail_streak_ > 0) {
            ESP_LOGI(TAG, "WM8978 I2C recovered after %u failures", i2c_fail_streak_);
        }
        i2c_fail_streak_ = 0;
        codec_available_ = true;
    }
}

void Wm8978AudioCodec::InitializeCodec() {
    std::lock_guard<std::mutex> lock(register_mutex_);

    WriteReg(kRegReset, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Digital audio interface: 16-bit Philips I2S, codec is the slave, MCLK
    // comes from the ESP32 (256 * fs through I2S0 MCLK on GPIO0).
    WriteReg(kRegAudioInterface, 0x010);
    WriteReg(kRegClocking, 0x000);

    WriteReg(kRegPower1, 0x0FB);
    WriteReg(kRegPower2, 0x1BF);
    WriteReg(kRegPower3, 0x1EF);
    WriteReg(kRegBeepControl, 0x010);
    WriteReg(kRegLeftAdcBoost, 0x100);
    WriteReg(kRegRightAdcBoost, 0x100);
    WriteReg(kRegOutputControl, 0x002);
    WriteReg(kRegDacControl, kDacControlValue);  // DACOSR128 + AMUTE
    WriteReg(kRegAdcControl, 0x008);

    // MIC -> PGA -> ADC, DAC -> output mixer
    WriteReg(kRegInputControl, 0x033);
    WriteReg(kRegLeftInpPga, 25);
    WriteReg(kRegRightInpPga, 25 | kVolumeUpdate);
    WriteReg(kRegLeftMixer, 0x001);
    WriteReg(kRegRightMixer, 0x001);
    WriteReg(kRegEq1, 0x02C);

    // Digital DAC volume: vendor BSP bsp.c:200 calls WM8978_DAC_VOL_SET(180),
    // which writes 180 straight into R11/R12 (no internal mapping).
    // Per WM8978 datasheet Table 20/28: attenuation = 0.5*(X-255) dB,
    // so 180 = -37.5 dB (NOT the "-6 dB" claimed by an earlier comment).
    // Keep 180 for now: changing it needs an A/B listening test (H1 in
    // NOISE-ROOT-CAUSE.md). 255 = 0 dB, 220 = -17.5 dB.
    WriteReg(kRegLeftDacVolume, 180);
    WriteReg(kRegRightDacVolume, 180 | kVolumeUpdate);
}

void Wm8978AudioCodec::ApplyOutputVolume() {
    std::lock_guard<std::mutex> lock(register_mutex_);
    // 兜底B：不再因 codec_available_ 降级而跳过音量写入。WriteReg 自身带重试
    // 与自恢复，只有持续尝试才能在总线/器件恢复后第一时间把音量重新写对。

    int volume = output_volume_;
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }
    uint8_t level = static_cast<uint8_t>(volume * kMaxAnalogGain / 100);
    if (level == 0) {
        level = kVolumeMute;
    }

    if (use_spk_output_) {
        // Keep LOUT1/ROUT1 muted and drive LOUT2/ROUT2.
        WriteReg(kRegLout1Volume, kVolumeMute);
        WriteReg(kRegRout1Volume, kVolumeMute | kVolumeUpdate);
        WriteReg(kRegLout2Volume, level);
        WriteReg(kRegRout2Volume, level | kVolumeUpdate);
    } else {
        // Keep LOUT2/ROUT2 muted and drive LOUT1/ROUT1.
        WriteReg(kRegLout2Volume, kVolumeMute);
        WriteReg(kRegRout2Volume, kVolumeMute | kVolumeUpdate);
        WriteReg(kRegLout1Volume, level);
        WriteReg(kRegRout1Volume, level | kVolumeUpdate);
    }
}

int Wm8978AudioCodec::Read(int16_t* dest, int samples) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (!input_enabled_) {
        memset(dest, 0, samples * sizeof(int16_t));
        return samples;
    }
    auto needed = (size_t)samples * 2;
    if (rx_buffer_.size() < needed) {
        rx_buffer_.resize(needed);
    }
    size_t bytes_read = 0;
    constexpr uint32_t kReadTimeoutMs = 200;
    if (i2s_channel_read(rx_handle_, rx_buffer_.data(), needed * sizeof(int16_t), &bytes_read,
                         kReadTimeoutMs) != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed or timed out");
        return 0;
    }
    int frames = bytes_read / (2 * sizeof(int16_t));
    // Two on-board microphones feed the left/right PGA; mix or select at compile time.
    for (int i = 0; i < frames; ++i) {
        int32_t left = rx_buffer_[2 * i];
        int32_t right = rx_buffer_[2 * i + 1];
#if AUDIO_CODEC_INPUT_MODE == 2
        dest[i] = (int16_t)((left + right) / 2);
#elif AUDIO_CODEC_INPUT_MODE == 1
        dest[i] = (int16_t)right;
#else
        dest[i] = (int16_t)left;
#endif
    }
    return frames;
}

int Wm8978AudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (!output_enabled_) {
        // Nothing was written; report 0 so callers counting successful
        // output (the speaking watchdog) do not mistake this for a frame out.
        return 0;
    }
    auto needed = (size_t)samples * 2;
    if (tx_buffer_.size() < needed) {
        tx_buffer_.resize(needed);
    }
    // WM8978 is a stereo codec. Duplicate the mono PCM into both slots so that
    // neither headphone channel stays silent (a MONO slot leaves one side muted).
    for (int i = 0; i < samples; ++i) {
        tx_buffer_[2 * i] = data[i];
        tx_buffer_[2 * i + 1] = data[i];
    }
    size_t bytes_written = 0;
    // timeout_ms is in milliseconds: the driver applies pdMS_TO_TICKS itself.
    // Do NOT convert here (previous code did, giving 20 ms at CONFIG_FREERTOS_HZ=100).
    esp_err_t err = i2s_channel_write(tx_handle_, tx_buffer_.data(),
                                      needed * sizeof(int16_t), &bytes_written,
                                      kWriteTimeoutMs);
    if (err != ESP_OK) {
        // 兜底A：绝不用 portMAX_DELAY 无限等。超时即返回（播放任务存活，不会
        // 连锁堵死解码/播放队列）；只有连续失败达阈值才重建通道自愈。
        ++write_fail_streak_;
        if (write_fail_streak_ <= 3 || write_fail_streak_ % kWriteFailRecoverThreshold == 0) {
            ESP_LOGW(TAG, "I2S write timeout/failed: %s (streak=%u)", esp_err_to_name(err),
                     write_fail_streak_);
        }
        if (write_fail_streak_ >= kWriteFailRecoverThreshold) {
            RecoverTxLocked();
        }
        return 0;
    }
    write_fail_streak_ = 0;  // 成功即清零
    return bytes_written / (2 * sizeof(int16_t));
}

void Wm8978AudioCodec::Start() {
    AudioCodec::Start();
    ApplyOutputVolume();
}

void Wm8978AudioCodec::SetOutputVolume(int volume) {
    AudioCodec::SetOutputVolume(volume);
    ApplyOutputVolume();
}

void Wm8978AudioCodec::RecoverTxLocked() {
    // 兜底A：连续写超时，判定 TX DMA/时钟卡死，做一次干净的通道重建。
    // 关键：全双工下 RX 是 TX 时钟的 slave，必须先关 RX 再关 TX，再按
    // ApplyChannelPowerLocked() 的既定顺序（TX 先、RX 后）重新拉起，避免
    // 在重建过程中出现"RX 无时钟"的中间态。重建后重写音量/路由寄存器。
    // 调用方持有 data_mutex_；在 Write() 内调用（未持 register_mutex_）。
    ESP_LOGW(TAG, "Recovering stalled I2S channels (streak=%u)", write_fail_streak_);
    if (rx_enabled_) {
        i2s_channel_disable(rx_handle_);
        rx_enabled_ = false;
    }
    if (tx_enabled_) {
        i2s_channel_disable(tx_handle_);
        tx_enabled_ = false;
    }
    write_fail_streak_ = 0;
    ApplyChannelPowerLocked();  // 依据 input/output 逻辑状态重新 enable
    ApplyOutputVolume();        // 恢复 DAC/输出模拟增益与路由
    ESP_LOGW(TAG, "I2S channel recovery done");
}

void Wm8978AudioCodec::ApplyChannelPowerLocked() {
    // Full-duplex caveat: this board's RX channel is a clock slave of TX
    // (BCLK/WS are generated by the TX channel). Disabling EITHER channel
    // while the other is streaming resets the shared clock domain and leaves
    // an in-flight i2s_channel_write(..., portMAX_DELAY) blocked forever.
    // That was the root cause of long TTS going silent at ~15 s: the
    // framework's AUDIO_POWER_TIMEOUT_MS (15 s) input-idle timer calls
    // EnableInput(false) mid-playback, and the old code disabled RX here
    // unconditionally, killing the playback task and backing up every queue.
    //
    // Rule: only tear the channels down when BOTH directions are idle. While
    // any audio is flowing, keep the already-enabled channels untouched. RX
    // stays enabled (but unread) after capture stops — harmless, and it keeps
    // the clock domain stable for the ongoing playback.
    const bool need_clock = input_enabled_ || output_enabled_;

    if (need_clock) {
        // TX must come up first: it provides the clock for the RX slave.
        if (!tx_enabled_) {
            ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
            tx_enabled_ = true;
        }
        if (input_enabled_ && !rx_enabled_) {
            ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
            rx_enabled_ = true;
        }
    } else if (tx_enabled_ || rx_enabled_) {
        // Both directions idle -> safe to power down. Disable RX before TX.
        if (rx_enabled_) {
            ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
            rx_enabled_ = false;
        }
        if (tx_enabled_) {
            ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
            tx_enabled_ = false;
        }
    }
}

void Wm8978AudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    // Update the logical flag first (also logs), then reconcile the shared
    // duplex channels. Never toggle a channel while playback is active.
    AudioCodec::EnableInput(enable);
    ApplyChannelPowerLocked();
}

void Wm8978AudioCodec::EnableOutput(bool enable) {
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (enable == output_enabled_) {
            return;
        }
        AudioCodec::EnableOutput(enable);
        ApplyChannelPowerLocked();
    }
    ApplyOutputVolume();
}

int Wm8978AudioCodec::OutputData(std::vector<int16_t>& data) {
    // Board-specific WM8978 AUTO-MUTE workaround, kept out of core AudioService.
    // A silent keep-alive frame is all zeros; 1024 consecutive zeros make the
    // codec mute its analogue output and the unmute at the next real frame is
    // heard as a hiccup. One +/-1 LSB sample every 128 (~-90 dBFS, zero DC mean)
    // breaks that detector. The longest zero run becomes 127, far below 1024.
    // Real audio is not all-zero, so it is never modified.
    // A silence frame is all zeros, or already carries our own +/-1 dither
    // (the core reuses the same buffer). Treat +/-1 as zero so the pattern is
    // re-applied identically every frame instead of relying on stale contents.
    bool silent = true;
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] > 1 || data[i] < -1) {
            silent = false;
            break;
        }
    }
    if (silent) {
        for (size_t i = 0; i < data.size(); i += 128) {
            data[i] = ((i / 128) & 1) ? 1 : -1;
        }
    }
    return AudioCodec::OutputData(data);
}
