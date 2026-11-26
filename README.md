# xiaozhi-esp32-modified — ESP32-LIN 音频板

这是 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 的一个改造分支，基线为上游提交 `a3134949`（2026-11-24）。目标只有一个：把小智固件移植到 WhyEngineer ESP32-LIN 音频开发板（ESP32-WROVER + WM8978 + PAM8403，无屏幕，RGB 灯显示状态），并解决这块板在长 TTS 播放中的断声、顿一下和待命底噪问题。

上游小智项目本身的介绍、支持的开发板、MCP 等内容请看上游仓库；这里只写本分支改了什么。同步上游的方法与逐 hunk 取舍清单见 [板级 README](main/boards/esp32-lin/README.md) 的「同步官方更新」小节。

## 改了什么

### 1. 新板：esp32-lin

WM8978 编解码器（I2C 0x1A，I2S 从模式，全双工 24 kHz）、PAM8403 功放、双麦差分输入、音量电位器（ADC1_CH0）、三个按键、共阳 RGB 状态灯，全部接入。无屏幕，设备状态用灯色表示，激活码走串口日志。板级文件在 `main/boards/esp32-lin/`，注册只动了 `CMakeLists.txt` 和 `Kconfig.projbuild` 各几行。

### 2. 长 TTS 断声：根因是播放端丢了协议时间轴

服务端按 60 ms 时间槽给音频编号，分句时允许跳号；设备端原来收一帧播一帧，队列一排空 I2S 就停，跳号处就断声。修复在 `main/audio/audio_service.{h,cc}`：起播预缓冲 4 帧（带 250 ms 超时，短回复不会卡住）、队列空时用静音分片续流（3×20 ms，片间重查队列让位真实帧）、播放队列从 2 帧加深到 6 帧当抖动缓冲。板内 A/B 自测：稳态断流 3→0，最坏单次 I2S 阻塞 39 ms→26 ms。

### 3. 待命嘶嘶：WM8978 硬件 AUTO-MUTE

开启 R10 的 bit2，芯片检测到 1024 个连续零样本就把模拟输出静音，非零样本到达立即解除——判据是数据内容不是时间，所以不会误杀 TTS 分句停顿。配套地，静音续流帧由板级 `Wm8978AudioCodec::OutputData()` 注入 ±1 LSB dither（约 −90 dBFS），避免分句停顿被误判成长静音。用户实测：待命嘶嘶消失、唤醒首句无 pop。增益上限从 63 一路试到 50，11-16 复测仍有残响，这一项**未闭环**，根因和候选方案见 [NOISE-ROOT-CAUSE.md](main/boards/esp32-lin/NOISE-ROOT-CAUSE.md)。

### 4. 长期稳定兜底

I2S 写不再用 `portMAX_DELAY` 无限等（200 ms 有界超时 + 连续失败重建通道）、I2C 写重试三次且成功自恢复、speaking 状态下队列有积压却 12 秒无输出才判定卡死并回 idle（队列空不算，避免误杀服务端暂停）。累计 26+ 分钟实机验证：0 WDT / 0 E 级 / 0 I2S 告警 / 内存无泄漏 / 零重启。

### 5. 中文 + 唤醒词

界面 `zh-CN`，唤醒词「你好小智」。

## 编译与烧录

编译（ESP-IDF release/v6.1）：

```bash
python3 scripts/build.py esp32-lin --language zh-CN
```

只烧 app 分区（`0x10000`），保留 NVS 里的激活和 WiFi 配置：

```bash
python -m esptool --chip esp32 -p /dev/cu.usbserial-XXXX -b 460800 \
  --before default-reset --after hard-reset write_flash \
  --flash-mode dio --flash-size 4MB --flash-freq 80m 0x10000 build/xiaozhi.bin
```

注意 `build.py` 会重置 sdkconfig，每次都要带 `--language zh-CN`，否则语言变回英文。

## 改了什么（文件清单）

| 文件 | 改动 |
| --- | --- |
| `main/boards/esp32-lin/` | 新板目录：codec、板级初始化、音量旋钮、验机记录和根因分析 |
| `main/audio/audio_service.{h,cc}` | 预缓冲超时、静音续流分片、播放队列 2→6、输出帧计数、持锁写时间戳 |
| `main/application.{h,cc}` | speaking 饥饿看门狗 |
| `main/CMakeLists.txt`、`main/Kconfig.projbuild` | 注册 esp32-lin 板型 |
| `README.md` | 本分支说明（本文件） |

板级完整文档（接线、引脚、A/B 数据、同步上游清单、现场验收 checklist）：[`main/boards/esp32-lin/README.md`](main/boards/esp32-lin/README.md)
