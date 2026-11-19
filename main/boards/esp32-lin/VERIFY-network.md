# ESP32-LIN 配网后运行期日志（2026-11-11 18:27）

接续 `VERIFY.md` 的烧录验证。用户完成 Wi-Fi 配网后再次抓取 25 秒串口日志（115200，pyserial）。

说明：本次打开串口时板子发生了一次 `POWERON_RESET`（我本意不复位），但 NVS 中的 Wi-Fi 凭证保留，重启后自动重连成功；这次启动一次通过，没有出现上一轮的 `flash read err, 1000` / RTCWDT 二次复位，说明该现象并非必现。

## 关键结论

| 项目 | 结果 | 日志证据 |
| --- | --- | --- |
| Wi-Fi 配网 | 已保存并持久化，重启自动连回 `OpenWrt`（WPA2-PSK，RSSI -33） | `Found AP: OpenWrt`、`connected with OpenWrt`、`Got IP: 192.168.3.110` |
| 状态机 | `starting -> activating`，不再进入配网模式 | `StateMachine: State: starting -> activating` |
| 网络协议栈 | 扫描、认证、关联、DHCP 全链路正常 | `state: init -> auth -> assoc -> run`、`sta ip: 192.168.3.110` |
| Assets 分区 | map 576 KB / 分区 1024 KB，校验 127 ms | `The assets map size is 576 KB` |
| 唤醒词模型 | 加载成功 | `MODEL_LOADER: Successfully load srmodels` |
| 无屏路径 | 正确跳过 UI 资源 | `Skipping UI assets: LVGL display is unavailable` |
| 激活 / OTA | HTTPS 连上 `api.tenclass.net:443`，版本为最新，factory 分区跳过 OTA | `Current is the latest version` |
| 音频输出 | 播放激活提示音，`output enable=true`，16k→24k 重采样，无 I2S 报错 | `Resampling audio from 16000 to 24000` |
| 当前状态 | **等待激活**：`xiaozhi.me`，验证码 `670976` | `Alert [link] Activation: xiaozhi.me / 670976`、`Activating... 1/10` |
| 稳定性 | 无 panic / abort / 看门狗，`free sram` 稳定 | `204959 → 207963` |

日志中唯一的 "error-ish" 命中是 MCP 工具名 `self.reboot`，不是错误。

## 无屏板怎么激活（本板重点）

这块板没有屏幕，但激活码**不依赖屏幕**：

- 服务器下发的 `activation.message` 是 `xiaozhi.me` + 换行 + `670976`。`Application::Alert` 先 `ESP_LOGW` 把整段打到串口；`Display::SetChatMessage` 的默认实现（NoDisplay 未覆盖）也会输出 `Role:system` 和内容逐行。所以验证码在 115200 串口日志里能看到两遍。
- 喇叭只播放静态 `activation.ogg` 提示音，**不会朗读 6 位数字**，别指望靠听的拿码。
- 两次抓日志都是 `670976`，是按设备 ID 下发的稳定码，重启不变，激活成功后失效。

操作步骤：

1. 串口看码：`idf.py -p /dev/cu.usbserial-0181F5E6 monitor`（或任意串口工具，115200），找 `Alert [link] Activation: xiaozhi.me` 下一行的 6 位数字。
2. 浏览器打开 `xiaozhi.me` → 登录 → 控制台添加/激活设备 → 输入 `670976`。
3. 设备每轮 `Activating... n/10` 会调用 `ota_->Activate()` 轮询，绑定成功后日志出现 `Activation done`，状态进 idle；10 轮没结果就重启重来（码不变）。
4. 激活期间**别按 KEY1、也别喊唤醒词**：`HandleToggleChatEvent` 和 `HandleWakeWordDetectedEvent` 在 `kDeviceStateActivating` 下会直接 `SetDeviceState(kDeviceStateIdle)` 跳过激活，设备会停在未绑定状态。

## 仍需人工确认

- 目视 RGB LED：激活中应为黄色 500 ms 慢闪；进 idle 后熄灭；聆听红、播报绿。
- 按 KEY1 切换对话、KEY2 长按 3 秒重新配网、KEY3 触发唤醒词。
- 对着板载麦克风说话，确认录音上行；若喇叭无声，把 `config.h` 中 `AUDIO_CODEC_USE_SPK_OUTPUT` 改为 `1` 重新编译。

## 完整运行期日志

```text
ets Jun  8 2016 00:22:57

rst:0x1 (POWERON_RESET),boot:0x3f (SPI_FAST_FLASH_BOOT)
configsip: 0, SPIWP:0xee
clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00
mode:DIO, clock div:2
load:0x3fff0040,len:1400
load:0x40078000,len:15632
load:0x40080400,len:3252
entry 0x400805e4
I (43) quad_psram: This chip is ESP32-D0WD
I (43) esp_psram: Found 4MB PSRAM device
I (43) esp_psram: Speed: 40MHz
I (43) esp_psram: PSRAM initialized, cache is in low/high (2-core) mode.
I (49) cpu_start: Multicore app
I (938) esp_psram: SPI SRAM memory test OK
I (945) cpu_start: GPIO 3 and 1 are used as console UART I/O pins
I (946) cpu_start: Pro cpu start user code
I (946) cpu_start: cpu freq: 160000000 Hz
I (948) app_init: Application information:
I (951) app_init: Project name:     xiaozhi
I (955) app_init: App version:      2.5.3
I (959) app_init: Compile time:     Nov 11 2026 16:09:05
I (964) app_init: ELF file SHA256:  19b2e26c9...
I (968) app_init: ESP-IDF:          v6.1-dirty
I (973) efuse_init: Min chip rev:     v0.0
I (976) efuse_init: Max chip rev:     v3.99 
I (980) efuse_init: Chip rev:         v1.0
I (985) heap_init: Initializing. RAM available for dynamic allocation:
I (991) heap_init: At 3FFAE6E0 len 00001920 (6 KiB): DRAM
I (996) heap_init: At 3FFBC800 len 00023800 (142 KiB): DRAM
I (1001) heap_init: At 3FFE0440 len 00003AE0 (14 KiB): D/IRAM
I (1006) heap_init: At 3FFE4350 len 0001BCB0 (111 KiB): D/IRAM
I (1012) heap_init: At 40090E24 len 0000F1DC (60 KiB): IRAM
I (1018) esp_psram: Adding pool of 4096K of PSRAM memory to heap allocator
I (1025) spi_flash: detected chip: gd
I (1027) spi_flash: flash io: dio
I (1033) main_task: Started on CPU0
I (1033) esp_psram: Reserving pool of 32K of internal memory for DMA/internal allocations
I (1033) main_task: Calling app_main()
I (1073) Board: UUID=5417521d-34c8-4aee-af83-c5842f3fde4f SKU=esp32-lin
I (1073) button: IoT Button Version: 4.2.3
I (1073) StateMachine: State: unknown -> starting
W (1083) Display: Role:system
W (1083) Display:      esp32-lin/2.5.3
W (1083) i2s_common: the rx channel on I2S0 is switched from master to slave for full-duplex mode
I (1093) Wm8978AudioCodec: Duplex I2S channels created (MCLK on GPIO0)
I (1113) Wm8978AudioCodec: WM8978 initialized (24000 Hz, headphone LOUT1/ROUT1 output)
I (1113) AudioCodec: Audio codec started
I (1133) Application: After board/audio init
I (1133) SystemInfo: free sram: 271619 minimal sram: 239556
I (1133) MCP: Add tool: self.get_device_status
I (1133) MCP: Add tool: self.audio_speaker.set_volume
I (1143) MCP: Add tool: self.get_system_info [user]
I (1143) MCP: Add tool: self.reboot [user]
I (1143) MCP: Add tool: self.upgrade_firmware [user]
I (1153) MCP: Add tool: self.assets.set_download_url [user]
I (1153) MCP: 6 tools registered: 2 visible to the model, 4 user-only
I (1163) WifiManager: Initializing...
I (1183) wifi:wifi driver task: 3ffd0324, prio:23, stack:6656, core=0
I (1183) wifi:wifi firmware version: e12a754
I (1183) wifi:wifi certification version: v7.0
I (1183) wifi:config NVS flash: disabled
I (1183) wifi:config nano formatting: disabled
I (1193) wifi:Init data frame dynamic rx buffer num: 8
I (1193) wifi:Init dynamic rx mgmt buffer num: 5
I (1203) wifi:Init management short buffer num: 32
I (1203) wifi:Init dynamic tx buffer num: 32
I (1213) wifi:Init static rx buffer size: 1600
I (1213) wifi:Init static rx buffer num: 6
I (1213) wifi:Init dynamic rx buffer num: 8
I (1223) wifi_init: rx ba win: 6
I (1223) wifi_init: accept mbox: 6
I (1223) wifi_init: tcpip mbox: 32
I (1233) wifi_init: udp mbox: 6
I (1233) wifi_init: tcp mbox: 6
I (1233) wifi_init: tcp tx win: 5760
I (1243) wifi_init: tcp rx win: 5760
I (1243) wifi_init: tcp mss: 1440
I (1243) WifiManager: Initialized
I (1253) WifiBoard: Starting WiFi connection attempt
I (1253) WifiManager: Starting station
I (1253) WifiStation: Station DHCP hostname: Xiaozhi-0B10
I (1263) phy_init: phy_version 4863,a3a4459,Oct 28 2025,14:30:06
I (1343) wifi:mode : sta (30:ae:a4:5f:0b:10)
I (1343) wifi:enable tsf
I (1353) WifiStation: Scanning saved channel 6
I (1353) WifiBoard: WiFi scanning
W (1353) Display: ShowNotification: Scanning Wi-Fi...
I (1473) WifiStation: Found AP: OpenWrt, BSSID: dc:d8:7c:2b:79:1f, RSSI: -30, Channel: 6, Authmode: 3
I (1483) WifiBoard: WiFi connecting to OpenWrt
W (1483) Display: ShowNotification: Connect to OpenWrt...
W (1483) wifi:Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2
I (1803) wifi:new:<6,0>, old:<1,0>, ap:<255,255>, sta:<6,0>, prof:1, snd_ch_cfg:0x0
I (1803) wifi:state: init -> auth (0xb0)
I (1813) wifi:state: auth -> assoc (0x0)
I (1823) wifi:state: assoc -> run (0x10)
I (1953) wifi:connected with OpenWrt, aid = 9, channel 6, BW20, bssid = dc:d8:7c:2b:79:1f
I (1953) wifi:security: WPA2-PSK, phy: bgn, rssi: -33, cipher(pairwise:0x3, group:0x3), pmf:0
I (1963) wifi:pm start, type: 1

I (1963) wifi:dp: 1, bi: 102400, li: 10, scale listen interval from 1024000 us to 1024000 us
I (2003) wifi:<ba-add>idx:0 (ifx:0, dc:d8:7c:2b:79:1f), tid:6, ssn:2, winSize:64
I (2023) wifi:AP's beacon interval = 102400 us, DTIM period = 1
I (2993) esp_netif_handlers: sta ip: 192.168.3.110, mask: 255.255.255.0, gw: 192.168.3.1
I (2993) WifiStation: Got IP: 192.168.3.110
I (2993) WifiBoard: Connected to WiFi: OpenWrt
W (2993) Display: ShowNotification: Connected to OpenWrt
I (3003) Application: Network connected
I (3003) StateMachine: State: starting -> activating
I (3013) Assets: The storage free size is 3648 KB
I (3013) Assets: The assets map size is 576 KB (partition 1024 KB)
I (3153) Assets: The checksum calculation time is 127 ms
I (3153) MODEL_LOADER: Successfully load srmodels
I (3163) Assets: Skipping UI assets: LVGL display is unavailable
W (3163) Display: Role:system
W (3163) Display:      
W (3163) Display: SetEmotion: robot_2
W (3163) Display: SetEmotion: neutral
W (3173) Display: SetStatus: Checking for new version...
I (3173) Ota: Current version: 2.5.3
I (3193) wifi:<ba-add>idx:1 (ifx:0, dc:d8:7c:2b:79:1f), tid:5, ssn:3, winSize:64
I (3203) wifi:<ba-del>idx:1, tid:5
I (3203) wifi:<ba-add>idx:1 (ifx:0, dc:d8:7c:2b:79:1f), tid:0, ssn:1, winSize:64
I (4443) HttpClient: Established new connection to api.tenclass.net:443 protocol=https cost=1260
I (4603) HttpClient: HTTP connection closed
I (4613) Ota: Current is the latest version
I (4613) Ota: Running from factory partition, skipping
W (4613) Display: SetStatus: Activation
W (4613) Application: Alert [link] Activation: xiaozhi.me
670976
W (4623) Display: SetStatus: Activation
W (4623) Display: SetEmotion: link
W (4623) Display: Role:system
W (4623) Display:      xiaozhi.me
670976
I (4633) AudioCodec: Set output enable to true
I (4643) AudioService: Resampling audio from 16000 to 24000
I (12133) SystemInfo: free sram: 204959 minimal sram: 163036
I (15793) Application: Activating... 1/10
I (17043) HttpClient: Established new connection to api.tenclass.net:443 protocol=https cost=1250
I (22133) SystemInfo: free sram: 207963 minimal sram: 157456
```
