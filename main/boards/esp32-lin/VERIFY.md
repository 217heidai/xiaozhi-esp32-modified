# ESP32-LIN 烧录与启动验证记录

日期：2026-11-11　固件：xiaozhi 2.5.3 / ESP-IDF v6.1 / 板型 `esp32-lin`

## 烧录

- 端口：`/dev/cu.usbserial-0181F5E6`（CP2104 USB-UART）
- 写入内容：bootloader `0x1000`、分区表 `0x8000`、ota_data `0xd000`、`xiaozhi.bin` `0x10000`、`generated_assets.bin` `0x300000`，`--flash-mode dio --flash-size 4MB --flash-freq 40m`
- 结果：`FLASH_EXIT=0`，5 个分区均 `Hash of data verified`
- 烧录后用 `--after no-reset` 留在 bootloader，再手动复位以便抓到完整启动日志

## 启动验证结论

| 项目 | 结果 | 日志证据 |
| --- | --- | --- |
| PSRAM | 4 MB 检测通过，内存测试 OK，4096K 加入 heap | `Found 4MB PSRAM device`、`SPI SRAM memory test OK` |
| Flash | 4 MB / DIO，芯片 gd | `detected chip: gd`、`flash io: dio` |
| 板型标识 | `esp32-lin` 正确上报 | `Board: UUID=... SKU=esp32-lin` |
| 状态机 | `unknown → starting → wifi_configuring` | `StateMachine` 两行 |
| WM8978 I2C | 寄存器写入无报错 | 无 `WM8978 write reg ... failed` |
| I2S / MCLK | 双工通道建立，MCLK 在 GPIO0 | `Duplex I2S channels created (MCLK on GPIO0)` |
| 音频输出 | 24 kHz、LOUT1/ROUT1，播放配网提示音时 `output enable=true`，18 秒后结束，无 I2S 写错误 | `WM8978 initialized (24000 Hz, ...)`、`Resampling audio from 16000 to 24000` |
| Wi-Fi 配网 | 热点 `Xiaozhi-0B11`、Web `http://192.168.4.1`、DNS/DHCP 均启动 | `Access Point started`、`Web server started` |
| MCP | 6 个工具注册成功 | `MCP: 6 tools registered` |
| 稳定性 | 30 秒内无 panic / abort / 看门狗循环，内存稳定 | `free sram: 210175 → 210035` |

## 已知现象（非本次移植引入）

- 每次上电先出现 `flash read err, 1000` 与 `rst:0x10 (RTCWDT_RTC_RESET)`，第二次启动才成功。烧录前板上 MicroPython 启动日志也有同样两行，属于该板上电/Flash 时序的固有现象，不影响运行。
- `W i2s_common: the rx channel on I2S0 is switched from master to slave for full-duplex mode`：ESP-IDF 全双工模式下 RX 跟随 TX 时钟的标准提示。
- `W phy_init: failed to load RF calibration data`：首次启动无校准数据，已自动全校准并保存。
- `W SsidManager: NVS namespace wifi doesn't exist`：新设备未存 Wi-Fi 凭证，因此进入配网模式，符合预期。

## 仍需人工确认

- RGB LED 实际颜色与闪烁（配网时应为蓝色 500 ms 慢闪）
- KEY1 / KEY2 / KEY3 的点击、长按 3 秒配网、唤醒词触发
- 麦克风录音、扬声器与耳机实际发声及音量；若无声，把 `config.h` 的 `AUDIO_CODEC_USE_SPK_OUTPUT` 改成 `1` 重新编译
- 配网后连接服务器、唤醒词、OTA

## 完整启动日志

```text
ets Jun  8 2016 00:22:57

rst:0x1 (POWERON_RESET),boot:0x3f (SPI_FAST_FLASH_BOOT)
flash read err, 1000
ets_main.c 371 
ets Jun  8 2016 00:22:57

rst:0x10 (RTCWDT_RTC_RESET),boot:0x3f (SPI_FAST_FLASH_BOOT)
configsip: 0, SPIWP:0xee
clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
mode:DIO, clock div:2
load:0x3fff0040,len:1400
load:0x40078000,len:15632
load:0x40080400,len:3252
entry 0x400805e4
I (43) quad_psram: This chip is ESP32-D0WD
I (44) esp_psram: Found 4MB PSRAM device
I (44) esp_psram: Speed: 40MHz
I (44) esp_psram: PSRAM initialized, cache is in low/high (2-core) mode.
I (49) cpu_start: Multicore app
I (938) esp_psram: SPI SRAM memory test OK
I (946) cpu_start: GPIO 3 and 1 are used as console UART I/O pins
I (946) cpu_start: Pro cpu start user code
I (946) cpu_start: cpu freq: 160000000 Hz
I (948) app_init: Application information:
I (952) app_init: Project name:     xiaozhi
I (956) app_init: App version:      2.5.3
I (959) app_init: Compile time:     Nov 11 2026 16:09:05
I (965) app_init: ELF file SHA256:  19b2e26c9...
I (969) app_init: ESP-IDF:          v6.1-dirty
I (973) efuse_init: Min chip rev:     v0.0
I (977) efuse_init: Max chip rev:     v3.99 
I (981) efuse_init: Chip rev:         v1.0
I (985) heap_init: Initializing. RAM available for dynamic allocation:
I (991) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM
I (996) heap_init: At 3FFBC800 len 00023800 (142 KiB): DRAM
I (1001) heap_init: At 3FFE0440 len 00003AE0 (14 KiB): D/IRAM
I (1007) heap_init: At 3FFE4350 len 0001BCB0 (111 KiB): D/IRAM
I (1012) heap_init: At 40090E24 len 0000F1DC (60 KiB): IRAM
I (1018) esp_psram: Adding pool of 4096K of PSRAM memory to heap allocator
I (1026) spi_flash: detected chip: gd
I (1028) spi_flash: flash io: dio
I (1033) main_task: Started on CPU0
I (1033) esp_psram: Reserving pool of 32K of internal memory for DMA/internal allocations
I (1033) main_task: Calling app_main()
I (1073) Board: UUID=5417521d-34c8-4aee-af83-c5842f3fde4f SKU=esp32-lin
I (1073) button: IoT Button Version: 4.2.3
I (1073) StateMachine: State: unknown -> starting
W (1073) Display: Role:system
W (1083) Display:      esp32-lin/2.5.3
W (1083) i2s_common: the rx channel on I2S0 is switched from master to slave for full-duplex mode
I (1093) Wm8978AudioCodec: Duplex I2S channels created (MCLK on GPIO0)
I (1103) Wm8978AudioCodec: WM8978 initialized (24000 Hz, headphone LOUT1/ROUT1 output)
I (1103) AudioCodec: Audio codec started
I (1123) Application: After board/audio init
I (1123) SystemInfo: free sram: 271779 minimal sram: 239716
I (1123) MCP: Add tool: self.get_device_status
I (1123) MCP: Add tool: self.audio_speaker.set_volume
I (1133) MCP: Add tool: self.get_system_info [user]
I (1133) MCP: Add tool: self.reboot [user]
I (1133) MCP: Add tool: self.upgrade_firmware [user]
I (1143) MCP: Add tool: self.assets.set_download_url [user]
I (1143) MCP: 6 tools registered: 2 visible to the model, 4 user-only
I (1153) WifiManager: Initializing...
I (1173) wifi:wifi driver task: 3ffd0284, prio:23, stack:6656, core=0
I (1173) wifi:wifi firmware version: e12a754
I (1173) wifi:wifi certification version: v7.0
I (1173) wifi:config NVS flash: disabled
I (1173) wifi:config nano formatting: disabled
I (1183) wifi:Init data frame dynamic rx buffer num: 8
I (1183) wifi:Init dynamic rx mgmt buffer num: 5
I (1193) wifi:Init management short buffer num: 32
I (1193) wifi:Init dynamic tx buffer num: 32
I (1203) wifi:Init static rx buffer size: 1600
I (1203) wifi:Init static rx buffer num: 6
I (1203) wifi:Init dynamic rx buffer num: 8
I (1213) wifi_init: rx ba win: 6
I (1213) wifi_init: accept mbox: 6
I (1213) wifi_init: tcpip mbox: 32
I (1223) wifi_init: udp mbox: 6
I (1223) wifi_init: tcp mbox: 6
I (1223) wifi_init: tcp tx win: 5760
I (1233) wifi_init: tcp rx win: 5760
I (1233) wifi_init: tcp mss: 1440
I (1233) WifiManager: Initialized
W (1233) SsidManager: NVS namespace wifi doesn't exist
I (2743) StateMachine: State: starting -> wifi_configuring
I (2743) WifiManager: Starting config AP
I (2743) DnsServer: Starting DNS server
I (2743) wifi:Set ps type: 0, coexist: 0

I (2743) phy_init: phy_version 4863,a3a4459,Oct 28 2025,14:30:06
W (2753) phy_init: failed to load RF calibration data (0x1102), falling back to full calibration
I (2823) phy_init: Saving new calibration data due to checksum failure or outdated calibration data, mode(2)
I (2843) wifi:mode : sta (30:ae:a4:5f:0b:10) + softAP (30:ae:a4:5f:0b:11)
I (2843) wifi:enable tsf
I (2843) wifi:Total power save buffer number: 16
I (2843) wifi:Init max length of beacon: 752/752
I (2853) wifi:Init max length of beacon: 752/752
I (2853) WifiConfigurationAp: Access Point started with SSID Xiaozhi-0B11
I (2863) esp_netif_lwip: DHCP server started on interface WIFI_AP_DEF with IP: 192.168.4.1
I (2873) WifiConfigurationAp: Web server started
I (2873) WifiBoard: WiFi config mode entered
W (2883) Application: Alert [gear] Wi-Fi Configuration Mode: Hotspot: Xiaozhi-0B11 Config URL: http://192.168.4.1
W (2883) Display: SetStatus: Wi-Fi Configuration Mode
W (2893) Display: SetEmotion: gear
W (2893) Display: Role:system
W (2893) Display:      Hotspot: Xiaozhi-0B11 Config URL: http://192.168.4.1
I (2903) AudioCodec: Set output enable to true
I (2913) AudioService: Resampling audio from 16000 to 24000
I (12123) SystemInfo: free sram: 210175 minimal sram: 160768
I (20903) AudioCodec: Set output enable to false
I (22123) SystemInfo: free sram: 210035 minimal sram: 160768
```
