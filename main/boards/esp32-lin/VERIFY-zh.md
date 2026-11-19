# 中文界面与立体声修复验机记录（2026-11-11 18:5x）

接续 `VERIFY.md`（烧录启动）与 `VERIFY-network.md`（配网激活）。用户反馈两点：默认说英语、耳机插上只有一边有声音。

## 一、语言

- 原因：`scripts/build.py` 默认 `en-US` / `hiesp`，不带 `--language` 就是英文。
- 重新构建：`python3 scripts/build.py esp32-lin --name esp32-lin --language zh-CN --wake-word nihaoxiaozhi`
- 构建结果：`language: zh-CN`、`wake_word: wn9_nihaoxiaozhi_tts`、`BUILD_EXIT=0`；`xiaozhi.bin` 0x18b980，app 分区 47% 空闲；中文提示音 ogg 重新生成并打包，`generated_assets.bin` 561004 B < 1 MB 分区。
- 运行时证据：`Display: ShowNotification: 扫描 Wi-Fi...`、`连接 OpenWrt...`、`已连接 OpenWrt`、`SetStatus: 检查新版本...`、`Alert [link] 激活设备: xiaozhi.me`；`MODEL_LOADER: Successfully load srmodels` 说明中文唤醒词模型加载成功。

## 二、耳机单边无声

- 根因：初版驱动把 I2S 配成 `I2S_SLOT_MODE_MONO` + `I2S_STD_SLOT_LEFT`，而 WM8978 是立体声 codec，右声道 slot 没有数据，于是只有一边出声。
- 修复（`wm8978_audio_codec.cc`）：
  - slot 改为 `I2S_SLOT_MODE_STEREO` + `I2S_STD_SLOT_BOTH`
  - `Write()` 把单声道 PCM 复制到左右两个 slot 再写 I2S；`Read()` 从立体声帧里只取麦克风所在声道
  - 用成员 `tx_buffer_` / `rx_buffer_` 复用缓冲，避免音频路径重复分配
  - `config.h` 新增 `AUDIO_CODEC_INPUT_RIGHT_CHANNEL`（默认 `0` 取左声道；录音无声时改 `1`）
- 验证：编译、烧录、启动均无 I2S 报错；`AudioCodec: Set output enable to true` 后播放激活提示音正常。**左右实际听感仍需戴耳机人工确认**（串口只能证明数据链路，听不到声音）。

## 三、烧录与启动

- esptool 分项写入 5 个分区，全部 `Hash of data verified`；**未擦 NVS**，用户已配的 Wi-Fi 凭证保留，重启后自动连回 `OpenWrt`（`192.168.3.110`）。
- `Board: ... SKU=esp32-lin`、4 MB PSRAM、`Wm8978AudioCodec: Duplex I2S channels created (MCLK on GPIO0)`、`WM8978 initialized (24000 Hz, headphone LOUT1/ROUT1 output)`。
- 激活码仍是 `670976`，状态 `starting -> activating`；无 panic / abort，`free sram` 稳定。
- 本次上电又出现一次 `flash read err, 1000` + `RTCWDT_RTC_RESET` 后二次启动成功，与固件无关，属板子上电时序固有现象（且非必现）。

## 关键日志摘录

```text
I (1073) Board: UUID=5417521d-34c8-4aee-af83-c5842f3fde4f SKU=esp32-lin
I (1093) Wm8978AudioCodec: Duplex I2S channels created (MCLK on GPIO0)
I (1113) Wm8978AudioCodec: WM8978 initialized (24000 Hz, headphone LOUT1/ROUT1 output)
I (1113) AudioCodec: Audio codec started
I (1253) WifiBoard: Starting WiFi connection attempt
I (1343) WifiStation: Scanning saved channel 6
W (1353) Display: ShowNotification: 扫描 Wi-Fi...
I (1473) WifiBoard: WiFi connecting to OpenWrt
W (1473) Display: ShowNotification: 连接 OpenWrt...
I (1953) wifi:connected with OpenWrt, aid = 9, channel 6, BW20
I (3003) WifiStation: Got IP: 192.168.3.110
W (3003) Display: ShowNotification: 已连接 OpenWrt
I (3013) StateMachine: State: starting -> activating
I (3163) MODEL_LOADER: Successfully load srmodels
I (3163) Assets: Skipping UI assets: LVGL display is unavailable
W (3183) Display: SetStatus: 检查新版本...
I (4603) Ota: Current is the latest version
W (4603) Display: SetStatus: 激活设备
W (4603) Application: Alert [link] 激活设备: xiaozhi.me
670976
I (4623) AudioCodec: Set output enable to true
I (4633) AudioService: Resampling audio from 16000 to 24000
I (14163) Application: Activating... 1/10
```

## 仍需人工确认

1. 戴耳机听左右声道是否都有声音（本次修复的核心验收点）。
2. 若仍单边：先确认耳机与插座接触，再检查 `config.h` 的 `AUDIO_CODEC_USE_SPK_OUTPUT` 输出路由（改 `1` 重编），最后才怀疑硬件。
3. 麦克风录音（无声则把 `AUDIO_CODEC_INPUT_RIGHT_CHANNEL` 改 `1`）。
4. 去 `xiaozhi.me` 输入 `670976` 完成激活；激活期间别按 KEY1、别喊唤醒词。
5. RGB LED 颜色、按键、唤醒词、OTA。

## 激活状态复核（用户反馈「已激活」后）

抓了 26 s 与 75 s 两段日志，设备仍停在 `starting -> activating`：

- 仍打印 `Alert [link] 激活设备: xiaozhi.me` 和 `670976`
- `Activating... 1/10` → `5/10`，每轮 `HttpClient` 连 `api.tenclass.net:443` 后关闭；无 `Ota: Activation successful`、无 `Activation done`、无 HTTP 错误行

代码依据（`main/application.cc` / `main/ota.cc`）：

- `CheckNewVersion()` 只要 OTA 响应里仍带 `activation`，就会显示激活码并进入 10 轮轮询
- `Ota::Activate()` POST `/activate`：HTTP **200** → `Activation successful` 并 break；HTTP **202** → `ESP_ERR_TIMEOUT`（服务器表示尚未绑定，设备延时 3 s 再问）
- 日志里每轮「建连 → 关闭」且无错误，正是 202 的表现，说明服务器侧仍判定该设备未激活

排查清单：

1. 确认 `xiaozhi.me` 登录的是目标账号，重新输入 `670976` 并**点提交/绑定**，不是只填输入框
2. 网页应提示激活成功，设备列表出现该设备（MAC `30:ae:a4:5f:0b:10`）
3. 成功后设备下一轮应返回 200，日志出现 `Ota: Activation successful` → `Activation done` → `State: activating -> idle`，且重启后不再显示激活码
4. 若网页确实成功但设备仍 202，重启设备重新拉一次 OTA 配置
