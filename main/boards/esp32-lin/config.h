#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// WhyEngineer Audio Board ESP32-LIN (ESP32-WROVER, 4 MB flash, 4 MB PSRAM)
// Schematic: esp32_lin_v0b.pdf, vendor BSP: components/bsp

// Audio: WM8978 codec on I2S0, full duplex, 16-bit Philips I2S.
// A duplex codec must use the same sample rate for capture and playback.
#define AUDIO_INPUT_SAMPLE_RATE 24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_0  // I2S0 MCLK / GPIO0 CLK_OUT1
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_33
#define AUDIO_I2S_GPIO_WS GPIO_NUM_25
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_26  // ESP32 -> WM8978 (playback)
#define AUDIO_I2S_GPIO_DIN GPIO_NUM_27   // WM8978 -> ESP32 (capture)

#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_19
#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_18
#define AUDIO_CODEC_WM8978_ADDR 0x1A

// The vendor BSP drives the PAM8403 amplifier from LOUT1/ROUT1 (headphone
// outputs) and mutes LOUT2/ROUT2. Change to 1 if a board revision wires the
// amplifier to the speaker outputs instead.
#define AUDIO_CODEC_USE_SPK_OUTPUT 0

// 两个麦克风各接一路差分输入：0 = 只取左，1 = 只取右，2 = 左右混合 (L+R)/2。
#define AUDIO_CODEC_INPUT_MODE 2

// Buttons: active low, external pull-ups on the board. GPIO34/35 are
// input-only pins, so no internal pull-up is requested.
#define BOOT_BUTTON_GPIO GPIO_NUM_34         // KEY_1: toggle chat / Wi-Fi config at startup
#define WIFI_CONFIG_BUTTON_GPIO GPIO_NUM_35  // KEY_2: long press -> Wi-Fi config
#define WAKE_BUTTON_GPIO GPIO_NUM_32         // KEY_3: invoke wake word

// RGB status LED: common anode, a low level turns the colour on.
#define LED_RED_GPIO GPIO_NUM_5
#define LED_GREEN_GPIO GPIO_NUM_21
#define LED_BLUE_GPIO GPIO_NUM_22

// On-board 5 kOhm volume potentiometer (VAR_RES_5K): wiper -> R8/R9(10K)
// divider -> C5 -> ADC1_CH0 / GPIO36 (SENSOR_VP). A 10K pull-up to 3V3 with
// 0..5K to ground yields 0..1.1V, which matches ADC_ATTEN_DB_0 full scale.
// Set to 0 to disable the knob and keep server-controlled volume only.
#define VOLUME_KNOB_ENABLED 1

// Raw ADC range mapped linearly to volume 0..100. Defaults cover the full
// 12-bit range; adjust after reading the logged raw values at both end stops.
#define VOLUME_KNOB_RAW_MIN 0
#define VOLUME_KNOB_RAW_MAX 4095

// SD card detect = GPIO23 is not used by this port, kept for reference.

#endif  // _BOARD_CONFIG_H_
