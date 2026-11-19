# WhyEngineer Audio Board ESP32-LIN (`esp32-lin`)

小智固件移植：经典 ESP32 音频开发板，**没有显示屏**，设备状态由板载共阳 RGB LED 呈现。

资料来源：`/Users/heidai/Develop/esp32/资料/esp32-lin`（原理图 `esp32_lin_v0b.pdf`、厂商 BSP `components/bsp`）。

## 硬件

| 项目 | 规格 |
| --- | --- |
| 主控 | ESP32-WROVER（实机检测：ESP32-D0WDQ6 v1.0，40 MHz 晶振） |
| Flash | 4 MB（esptool 检测），使用 `partitions/v2/4m.csv` |
| PSRAM | 4 MB，板级 `config.json` 中启用 `CONFIG_SPIRAM` |
| 音频编解码 | WM8978，2-wire 控制地址 `0x1A`，16-bit I2S 从模式 |
| 功放 | PAM8403，由 WM8978 LOUT1/ROUT1 驱动 |
| 输入 | 板载麦克风 + 3.5 mm 耳机座 |
| 网络 | Wi-Fi（`WifiBoard`） |
| 显示 | 无，使用 `NoDisplay` |
| 状态指示 | 共阳 RGB LED（低电平点亮） |
| 其它 | SD 卡座、音量电位器（已接入） |

## 引脚

| 功能 | GPIO |
| --- | --- |
| I2S MCLK | 0（I2S0 输出 256×fs，勿再当 BOOT 键使用） |
| I2S BCLK / WS | 33 / 25 |
| I2S DOUT（播放） / DIN（录音） | 26 / 27 |
| WM8978 I2C SDA / SCL | 19 / 18 |
| KEY_1 / KEY_2 / KEY_3 | 34 / 35 / 32（低电平有效，板载外部上拉） |
| LED 红 / 绿 / 蓝 | 5 / 21 / 22 |
| SD 卡检测 / 音量电位器 | 23 / 36（ADC1_CH0，已接入） |

采样率固定为输入/输出 24000 Hz（WM8978 为全双工单 I2S 时钟，两者必须一致）。

## RGB 状态灯

| 设备状态 | 颜色 |
| --- | --- |
| 启动中 | 蓝色快闪（100 ms） |
| Wi-Fi 配网中 | 蓝色慢闪（500 ms） |
| 空闲 | 熄灭 |
| 连接服务器 | 蓝色常亮 |
| 聆听（未检测到人声 / 检测到人声） | 红色 / 品红常亮 |
| 播报中、通知中 | 绿色常亮 |
| 升级中 | 黄色快闪（100 ms） |
| 激活中 | 黄色慢闪（500 ms） |
| 致命错误 | 红色快闪（100 ms） |

## 按键

| 按键 | 操作 |
| --- | --- |
| KEY_1 (GPIO34) | 单击切换对话；启动阶段单击进入 Wi-Fi 配网 |
| KEY_2 (GPIO35) | 长按 3 秒进入 Wi-Fi 配网 |
| KEY_3 (GPIO32) | 单击触发唤醒词「你好小智」 |

## 编译与烧录

```sh
source /Users/heidai/esp/esp-idf/export.sh
# 中文界面 + 中文唤醒词（不带参数时默认是英文 en-US / Hi ESP）
python3 scripts/build.py esp32-lin --name esp32-lin --language zh-CN --wake-word nihaoxiaozhi
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

本机 ESP-IDF v6.1 的 Python 虚拟环境是 `idf6.1_py3.12_env`，若 `export.sh` 找不到
`idf6.1_py3.13_env`，先执行：

```sh
export PATH="/Users/heidai/.espressif/python_env/idf6.1_py3.12_env/bin:$PATH"
source /Users/heidai/esp/esp-idf/export.sh
```

## 音频实现

- I2S0 全双工 24 kHz、16-bit Philips、**STEREO + SLOT_BOTH**：WM8978 是立体声 codec，驱动把
  单声道 PCM 复制到左右两个 slot 再写出，录音时按 `AUDIO_CODEC_INPUT_MODE` 取单路或左右混合。
  早期版本用了 MONO + LEFT slot，会让耳机只有一边响，已修正。
- `AUDIO_CODEC_INPUT_MODE`（`config.h`）：`0` 只取左、`1` 只取右、`2` 左右混合 `(L+R)/2`（默认 `2`）。两个板载麦克风各接一路差分输入，混合能同时利用两路拾音。
- 全双工 RX 的时钟由 TX 提供：`EnableInput(true)` 会先拉起 TX、并在录音期间保持，消除启动瞬间的 I2S 读超时。
- 实机验收：耳机左右声道均有声音（立体声修复通过）；左路、右路与混合模式均完成过对话。
- `AUDIO_CODEC_USE_SPK_OUTPUT`（`config.h`）：默认 `0`，音量写到 LOUT1/ROUT1 并静音
  LOUT2/ROUT2，与厂商 BSP 一致。若板子改版把功放接到 LOUT2/ROUT2，改成 `1`。

## 播放中偶发「顿一下」：根因与修复（2026-11-13 ~ 11-16）

> 现象：长故事 / TTS 播放中偶尔出现 60~180 ms 的短促断裂，像「语音断了又立即播」，
> 与音量、嘶嘶声无关。曾长期误判为「公网 UDP 随机丢包」，下面是推翻该误判后的定案。

### 根因：设备播放端丢弃了协议提供的 60 ms 时间轴

UDP 加密音频包头（见 `docs/mqtt-udp_zh.md` 4.2）：

```
|type 1B|flags 1B|payload_len 2B|ssrc 4B|timestamp 4B|sequence 4B|payload|
```

- **`timestamp == sequence × 60ms` 严格成立**（18 条接收基线 + 跳号两端，偏差全 0）。
  所以 `sequence` 是**时间槽编号**，不是「已发送包计数」。
- 协议文档 4.3 明写「容错处理：**允许轻微的序列号跳跃**，记录警告」，4.4「序列号异常：
  记录警告，**但仍处理数据包**」——**跳号是协议预期语义**，服务端在 TTS 分句停顿处
  按时间槽跳过空槽；`flags` 当前未使用、`ssrc` 代码完全没解析。
  **把「跳号」直接等同于「丢包」是整个排查过程最早、也最致命的错误起点。**
- 6 份日志 / 53 次跳号统计：跳号距最近 `Application: <<`（句子起点）中位 **1460 ms**，
  蒙特卡洛随机对照 **7400 ms**；52.8% 落在 1.5 s 内、81% 落在 3 s 内
  → **强贴分句边界，不是随机网络丢包**。
- `AudioStreamPacket.timestamp` 一路传到 `AudioTask.timestamp`（`audio_service.cc` 解码任务），
  但 `AudioOutputTask` 里唯一引用它的地方在 `#if CONFIG_USE_SERVER_AEC` 下，而该项**未开启**
  → **时间轴被整个丢弃**；播放是纯队列驱动，来一帧立刻写 I2S，队列一空 I2S 就停。
  日志里 `OUT_GOT` 空洞间隔 660~840 ms（正常 600 ms / 10 帧），正好等于缺 1~4 帧 × 60 ms，
  与 `d_seq` 一一对应。

### 为什么所有「调参数」都无效（别再重复）

匀速流（60 ms 到达 / 60 ms 消费）下**队列稳态深度 ≈ 0**，
只加大 `MAX_PLAYBACK_TASKS_IN_QUEUE` 上限**不会自然攒货**。以下都试过且无显著效果：

| 尝试 | 结果 |
| --- | --- |
| 播放队列 2 → 4 → 6 | 短空窗 3 → 2 → 2，部分缓解、不根治 |
| CPU 160 → 240 MHz、flash/PSRAM 40 → 80 MHz | 顿挫无变化（WDT 归零但现象仍在） |
| `LWIP_UDP_RECVMBOX_SIZE` 6 → 32、`udp_receive` prio 1 → 5 | 归一化丢包率 4.03 → 1.44 次/分，但短测波动达 6.8 倍，统计不显著 |
| WiFi RX 静态/动态/BA 6/8/6 → 16/32/16 | 8.06 → 5.66 次/分（绝对次数同为 9 次），泊松 95% 区间大幅重叠，**无效** |
| 协议层按 sequence 重排 | 作废：`duplicate/old` 恒 0，晚到旧包根本没来 |
| opus 原生 PLC | 作废：反汇编 `esp_opus_dec_decode` 确证不读 `frame_recover`，vendor 库不支持 |

> 通用 `sdkconfig.defaults` 里那段注释「copied from ESP-HI (ESP32C3) to reduce memory usage」
> 的 WiFi 缓冲配置，对本板 WROVER（4 MB PSRAM）确实是错配，板级 `config.json` 已覆盖回
> 16/32/16；但它**不是顿挫主因**，不要把它当结论。

### 修复（在 `main/audio/audio_service.{h,cc}`）

**不是「补帧造假」**：静音是协议时间轴上本来存在的空槽，目的是让 I2S **输出流不中断**。

- 起播预缓冲 `kPlaybackPrefillFrames = 4`，**只在每次播放流开始（`ResetDecoder()`）时生效**，
  见下方「二次修正」。
- `AudioOutputTask` 由无限 `wait()` 改为 `wait_for(20 ms)`：队列空且 1 秒内有真实帧时，
  按 60 ms 帧率喂静音帧保持 I2S 连续；超过 1 秒无真实帧则停止保活并重新 arm 预缓冲，
  待命态不会让 DAC 一直开着。
- 静音帧**不计入** `output_frame_count_`（speaking 饥饿看门狗仍以真实帧为准）。
- **不要**回退成「等下一帧到了再事后补静音」：那 180 ms 里 I2S 已经断流，事后补不回来
  （A/B 实测该方案只能把断流 4 → 2）。

### 二次修正：预缓冲复臂时机错误，把「细碎断流」换成了「240 ms 静音」（2026-11-16）

第一版修复上线后用户仍反馈「播放有掉帧的感觉」。复查发现是自己引入的新缺陷：

- 第一版在**每次 `pop_front()` 把队列取空时**重新置位 `prefill_pending_`。
  而稳态队列深度本来就在 0~1 之间震荡（见上文根因），于是每一次瞬时见底都会触发
  「等攒够 4 帧再播」；
- 而 `wait_for` 的 `!ready` 分支当时仍在**无条件喂静音**——包括「队列里已经有 1~3 帧、
  只是在等预缓冲」的时候。结果：**明明有真实帧可用，却先塞了 4 帧静音（4 × 60 ms = 240 ms）**，
  真实音频被整体顺延，听感就是「掉了一块」而不是「断了」。
- 判据差别：原先修的是「I2S 停」（物理断流），这次坏的是「时间轴被静音顶替」
  （流没断，但内容被推迟/覆盖），所以板内 `AUDIO_WRITE_RATE` 判据**看不出来**，
  这也是它躲过上一轮 A/B 的原因。

修正（`audio_service.{h,cc}`）：

1. 预缓冲只在 `last_real_frame_us_ == 0`（本次播放流还没出过真实帧）时生效，
   中途不再回退到「攒够 4 帧」；
2. 删除 `pop_front()` 之后的 `prefill_pending_ = true` 复臂；
3. `!ready` 分支的静音注入加严：**只有「队列确实为空」且 1 秒内有真实帧**才喂静音，
   队列非空一律继续等待，绝不拿静音顶替已排队的音频。

一句话原则：**静音只能填「时间轴上本来没有音频的空槽」，永远不能拿来填「队列里已经有帧」的空档。**

### 自主 A/B 验证（板内，不依赖服务端 / 麦克风 / 扬声器 / 人）

诊断固件曾在 `AudioService` 内置 `SelfTestTask()`：上电 20 s 后按 60 ms 自己生成静音 PCM 帧
注入播放队列，每 250 帧跳 3 个时间槽模拟分句停顿，注入帧 `timestamp = slot × 60 ms`；
A/B 两阶段各 60 s、刺激完全相同。判据是 `AUDIO_WRITE_RATE` 是否低于满速 97920 B/s
（真实 I2S 断流），**不是** `OUT_GOT` 取帧节奏。验证完脚手架与全部调试打印已清除。

| 方案 | A 断流 | B 断流 | 判定 |
| --- | --- | --- | --- |
| 仅起播预缓冲 4 帧 | 3 | 3 | 无效：存货被消耗后队列仍见底 |
| 按 timestamp 事后补静音 | 4 | 2 | 机制错：下一帧到达才补，断流已发生 |
| **队列空即时保活 + 预缓冲（最终采用）** | 3 | **0**（稳态） | 有效 |

最终一轮：A 稳态三次分句停顿（36180 / 51480 / 65880 ms）**全部 I2S 断流**；
B 同样三次停顿（95880 / 110880 / 125880 ms）**0 断流**，每次由 3~4 帧静音覆盖。
全窗口 B 仅多 1 次掉速，位于 A→B 切换后 480 ms 的队列重置边界，不是播放停顿。
三轮均 0 崩溃 / 0 WDT / 0 I2S 错误。

### 相关配置与状态

- 保留：I2S 有限超时 + 通道自愈、I2C 重试自恢复、speaking 饥饿看门狗、
  全双工电源管理（播放期间不 disable 共用时钟的 RX）、`kMaxAnalogGain = 50`、
  板级 `udp_receive` 提权 1 → 5、CPU/flash/PSRAM 240/80/80、WiFi RX 16/32/16、
  `LWIP_UDP_RECVMBOX_SIZE = 32`、`MAX_PLAYBACK_TASKS_IN_QUEUE = 6`。
- 最终固件：编译 0 error / 0 warning、烧录 `Hash verified`、75 s 启动自检
  0 崩溃 / 0 WDT / 0 I2S-I2C 告警 / 看门狗 0 误触发，内存 157947~158047 稳定无泄漏。
- **仍未验证**：官方 ESP32-S3 板为何不顿（从未抓过该板串口日志，不排除它走 WebSocket
  通道）；真人在真实 TTS 对话下的听感。根因与修复不依赖这两条。
- 调试期日志 `/tmp/esp32_lin_selftest{,2,3}.log`、分析脚本 `/tmp/analyze_selftest.py`
  为临时文件，重跑需重新加诊断打印。
- 可复用的排查流程（症状分类、物理判据、板内 A/B 自测脚手架、分层清单、反面案例）
  已整理成 `docs/audio-playback-debug-playbook_zh.md`，换板子遇到同类问题先看那份。

## 板载波轮（音量电位器，已接入）

这个旋钮**不是旋转编码器**。原理图第 1 页是 `VAR_R / VAR_RES_5K`（5 kΩ 可调电阻），
抽头经 R8 / R9（10 kΩ）、C5 组成分压滤波后送进 `ADC1_CH0`，即 ESP32 的
`SENSOR_VP` / **GPIO36**（input-only）。图上没有 EC11 之类带 A/B 相输出的编码器元件；
实物上手转到底会停住、有角度行程，已由用户确认，就是电位器。

厂商 BSP 按「音量旋钮」预留了它：`bsp_init()` 配 `ADC_WIDTH_BIT_12` +
`ADC_ATTEN_DB_0`（约 0~1.1 V 量程），`bsp_poll_task()` 每 500 ms 读一次
`adc1_get_raw(ADC_CHANNEL_0)`，变化超过 ±10 mV 就把 `bsp_event_t.vol` 塞进队列
（字段名 `vol` 即设计意图）。但三个示例只把 `vol:%d` 打到日志，
`WM8978_DAC_VOL_SET(180)` 是写死的 —— **厂商自己也没把旋钮接到音量控制上**。
本移植把它接完了。

### 实现（`volume_knob.h` / `volume_knob.cc`）

- ADC1_CH0 / GPIO36，12-bit + `ADC_ATTEN_DB_0`；10K 上拉 + 0~5K 到地，分压上限约
  1.1 V，正好匹配 0 dB 满量程（这也是厂商选 `DB_0` 的原因）。ADC1 不与 Wi-Fi 冲突，
  只有 ADC2 会。
- 低优先级 FreeRTOS 任务 **固定在 CPU1**（`xTaskCreatePinnedToCore`），100 ms 采样 +
  5 点滑动平均，`raw` 线性映射到音量 0~100
  （`config.h` 的 `VOLUME_KNOB_RAW_MIN` / `VOLUME_KNOB_RAW_MAX` 可标定）。
  CPU0 被 WakeNet9 唤醒词推理占满，任务若跑在 CPU0 会让 `IDLE0` 喂不上狗、
  每 20 秒打一次 `task_wdt` 回溯；绑到 CPU1 后 48 秒实测零告警。
- 变化 ≥2 级且距上次写入 ≥300 ms 才调 `AudioCodec::SetOutputVolume()`：既滤掉临界抖动，
  又避免转动时频繁 `nvs_commit`。该函数已覆写，会同步写 WM8978 寄存器并持久化。
- **最后操作优先**：开机只记录初始值、不覆盖 NVS 里已保存的音量；旋钮静止时也不会
  覆盖服务端 MCP `self.audio_speaker.set_volume` 下发的音量；用户一转就由旋钮接管。
- `VOLUME_KNOB_ENABLED=0` 可整体关闭。关闭后音量仅由服务端控制。
- **空闲噪声——通过压低模拟增益上限解决（不再用 idle 软静音）**：本板 full-duplex + 麦克风常开，
  播放结束后框架不会关 TX（`audio_service.cc` 的 `if (!(duplex && input_enabled)) EnableOutput(false)`
  被跳过），DAC 一直喂零、音量打满时模拟增益把噪声 floor 放大成可闻嘶嘶声。早先版本曾在
  `Wm8978AudioCodec` 里加定时器做"空闲静音"，但它靠"距上次 `Write()` 多久"猜空闲，流式 TTS +
  音频包乱序会让播放中途空窗超过阈值，**把正在讲的故事静音误杀**（见 `VERIFY-knob.md`）。
  最终改为直接把音量→模拟增益的映射上限从 63 降到 `kMaxAnalogGain = 50`，当时（11-12）
  判为噪声落回可闻阈值以下、长故事不再误杀，该定时器机制已移除。
  ⚠️ 但 11-16 复测（上限 60/58/56/50 逐档）用户均反馈仍有噪音，**此结论已不成立**；
  查明是增益结构错配 + 待命态输出级常开，见 `NOISE-ROOT-CAUSE.md`。
  2026-11-16 用户反馈「最大音量偏低」，上限曾提到 60，因 60/58/56 均有底噪，最终回到 **50**。

相关日志：

```
I (1072) VolumeKnob: Volume knob enabled (GPIO36 / ADC1_CH0, 12-bit, 0 dB, CPU1)
I (3222) VolumeKnob: initial raw=1033 (~277 mV) -> volume 25 (not applied)
I (xxxx) VolumeKnob: raw=2100 (~564 mV) -> SetOutputVolume(51)
I (xxxx) AudioCodec: Set output volume to 51
```

### 标定与调参

- **已标定（2026-11-12 用户实测）**：旋钮从一端转到另一端，`raw` 实测覆盖 **0（逆时针到底→音量 0）**
  到 **4095（顺时针到底→音量 100）**，方向为「顺时针变大」，`config.h` 的默认
  `RAW_MIN=0` / `RAW_MAX=4095` 正好铺满全行程，**无需改动**。
- 若换板或分压改了，`raw` 量程会偏移：把旋钮转到两个端点从日志读真实 min/max 写回
  `config.h` 即可（增量重编约 1 分钟）。
- 若高端 `raw` 明显偏低或读数饱和，说明实际电压超出 ~1.1 V，改 `ADC_ATTEN_DB_11`
  并重新标定；`volume_knob.cc` 里的 `millivolts` 估算值也要按新衰减调整。
- 旋钮顺时针/逆时针与音量增减方向若相反，把映射改成 `100 - volume` 即可。
- **最大音量嘶嘶（11-12 当时的结论，⚠️ 11-16 复测未闭环）**：音量→WM8978 模拟增益的映射上限从 63 降到
  `kMaxAnalogGain = 50`（在 `wm8978_audio_codec.cc` 顶部，注释说明 40~63 可调）。
  原因：音量打满时模拟增益把 DAC 噪声 floor 放大到可闻；压低上限后噪声随增益
  同比例下降、落回可闻阈值以下，峰值响度仅少约 3 dB。这同时解决了"空闲转到底嘶嘶"和
  "对话最大音量嘶嘶"两个问题，且不会误杀播放中的音频。若仍轻微嘶嘶往 40 降；
  若觉得最大音量偏轻往 63 提（但嘶嘶可能回潮）。**注意：不要再回头加 idle 软静音定时器——
  它已撤销，会误杀正在播放的故事。**
- **上限调整记录（2026-11-16）**：用户反馈默认最大音量偏低，`kMaxAnalogGain` 50 → 60；
  60/58/56 下均仍有底噪，最终**回到 50（当前值，11-12 实测干净锚点）**。
  ⚠️ **dB 换算已更正**：按 WM8978 datasheet（R52/R53，
  `000000 = −57 dB`、`111001(57) = 0 dB`、`111111(63) = +6 dB`）是 **1 dB/档**，
  不是早期记录的 0.75 dB。故 15 = −42 dB、20 = −37 dB、35 = −22 dB、
  **50 = −7 dB**、56 = −1 dB、58 = +1 dB、**60 = +3 dB**、63 = +6 dB；
  **60→50 实际降了 10 dB，不是 3 dB**。已经降了 10 dB 仍有噪音，
  更说明噪声不在这一级之后，或这一级被开得远比原始设计高。
  完整核算见 `NOISE-ROOT-CAUSE.md` §2。
- **数字 DAC 音量的注释是错的，这里更正**：`kRegLeftDacVolume/RightDacVolume = 180`
  写在 `InitializeCodec()`（不在 `Start()`），厂商 BSP `bsp.c:200 WM8978_DAC_VOL_SET(180)`
  经 `wm8978.c:232` 确认是**直接写寄存器、无内部换算**。按 datasheet 公式
  `0.5 × (X − 255) dB`，180 = **−37.5 dB**，而 `255 = 0 dB`；
  早期注释/文档写的「约 −6 dB」不成立（本板若为兼容芯片则待实测，见报告 H1）。
  已确认 `kRegEq1 = 0x02C` 对应 EQ 0 dB、DAC limiter 未启用，提数字电平不会引入额外增益。
  **这意味着增益分配本身是错配的**：数字侧白衰减 37.5 dB，模拟侧被迫比厂商 BSP
  （`HPvol_Set(15,15)` = −42 dB）高 20~35 dB 去补，噪声 floor 一起被放大。
  这才是「最大音量偏低 + 底噪」的共同根因。方案见 `NOISE-ROOT-CAUSE.md` §4 方案 2。
- **若 50 下仍能听到噪音 → 根因不在增益，转查「待命态输出级常开」**：
  见 `audio_service.cc::CheckAndUpdateAudioPowerState()`，
  `AUDIO_POWER_TIMEOUT_MS = 15000` 到期后本应 `EnableOutput(false)`，但
  `if (!(codec_->duplex() && codec_->input_enabled()))` 这层保护在全双工 + 唤醒词常开
  （`last_input_time_` 持续更新，input 永不超时）的条件下**永远不成立** ⇒ TX / DAC /
  LOUT1 一路常开，待命时 DAC 喂零，输出级按当前音量放大纯噪声。
  降 `kMaxAnalogGain` 只是把这个噪声等比例缩小，并没有关掉源头。
  **零成本判别实验**：唤醒后待命静置 30 秒不动、不说话，听噪音有没有变小或消失；
  再唤一次让它说话，看噪音是否回来。有变化 → 就是电源管理这条；
  全程不变 → 查供电纹波、地回路或负载本身。
  ⚠️ **更正**：早期这里写的「正解是用状态机 idle 信号 mute」不是最优解。
  WM8978 自带硬件 **AUTO-MUTE（R10 bit2）**：检测到 1024 个连续零样本自动模拟静音，
  非零样本到达立即解除（24 kHz 下约 21 ms），判据是数据内容而非时间，
  **不会误杀正在播的故事**，也不用改状态机或 I2S 时钟。厂商 BSP 与我们都没开这一位。
  **11:30 已实施**：R10 由 `0x008` → `0x00C`（`kDacControlValue`），只此一个位；
  用户 11:28 确认「嘶嘶 + 非播放时间」与本方案对症。编译烧录通过、启动日志干净。
  **听感回报（12:02）**：1 待命嘶嘶 ✅ 消失、2 唤醒首句 ✅ 正常、
  3 长故事分句停顿「似乎出现了一次」（12:16 确认是「顿一下 / 断续」）。
  **12:50 已闭环**：板内 A/B 自测排除软件播放边界（drops=0、lag_max ≤31 ms），
  定位为 AMUTE 在分句停顿被全零静音帧误触发、下一帧解除时的模拟瞬态。
  修复：静音保活帧注入稀疏 ±1 LSB dither（每 128 样本一个），
  立体声流最长连续零 127 帧 ≪ 1024 ⇒ 播放期 AMUTE 不触发；
  待命时保活停止、I2S auto_clear 发纯零 ⇒ AMUTE 照常静音。
  详见 `NOISE-ROOT-CAUSE.md` §0.2 / §0.3。
  另清除了 `OpusCodecTask` **持锁区**残留的一行 `AUDDBG` 调试打印
  （每 50 解码帧一次）；针对性 A/B 证明它不造成可测延迟，属该删的坏实践，
  但**不是**顿一下的主因。
- **静音帧分片 + 零堆分配（13:xx，生产长期稳定）**：`AudioOutputTask` 启动时
  预分配一次 20 ms 分片缓冲（原来每 60 ms 在音频路径 `new` 2.8 KB，长跑会碎片化），
  把 60 ms 静音槽拆为 3×20 ms，片间重查队列让位真实帧。
  配对 A/B：平均 lag 无显著差异（短测的 63% 是预热效应，已自我否定），
  但**最坏单次 I2S 阻塞 39 ms → 26 ms（−33%）**。分片后最长连续零仍 127 帧 < 1024，
  AMUTE 判据不变。详见 `NOISE-ROOT-CAUSE.md` §0.4。
- **I2S 写超时单位（11-18 修正）**：`i2s_channel_write()` 的第四参数叫 `timeout_ms`，
  驱动内部自己做 `pdMS_TO_TICKS()`。此前代码传了 `pdMS_TO_TICKS(kWriteTimeoutMs)`，
  在 `CONFIG_FREERTOS_HZ=100` 下实际只有 20 ms 而非设计的 200 ms。已改为直接传
  `kWriteTimeoutMs`。**注意**：历史 26+ 分钟日志里从未出现写超时（DMA 6×240 帧缓冲很深，
  60 ms 恒定帧流下 buffer 一直有空位），所以这是参数语义错误、无实测危害，
  不要把它当成之前偶发卡顿的原因。同文件的 `i2s_channel_read()` 传裸毫秒一直是对的。
- **累计约 26 分钟实机验证**：含 394 s 带注入长压测与 **620 s 最终固件无干预待机**；
  全部 0 WDT / 0 E级 / 0 I2S告警 / 0 调试残留 / drops=0；`minimal sram` 全程
  122720 不变、`free sram` 156823~156983 无下降趋势、时间轴无回绕 ⇒ 零重启。
  ⚠️ 板内自测绕过解码/网络路径，真实 TTS 端到端链路未覆盖，见报告「能力边界」
  与 §0.5「仍无法板内证明的部分」。
  `kMaxAnalogGain=50` / R11-R12=180 / R3=0x1EF 均未动，避免叠改动无法归因。
  完整方案排序、风险、5 个判别实验见 `NOISE-ROOT-CAUSE.md` §0.1。
  历史结论：63 全开 → 可闻嘶嘶；50 → 11-12 当时判为干净但 11-16 复测仍有噪音；60/58/56 → 仍有噪音。

## 无屏激活

设备没有屏幕，激活码**不看屏幕、只看串口日志**：`Application::Alert` 与
`Display::SetChatMessage` 的默认实现会把 `xiaozhi.me` 和 6 位验证码打到 115200 串口；
喇叭只播静态 `activation.ogg`，不会念数字。打开 `xiaozhi.me` 登录后输入验证码即可，
详见 `VERIFY-network.md` 的「无屏板怎么激活」。激活期间不要按 KEY1、不要喊唤醒词，
否则会跳过激活直接进 idle。

## 注意事项

- 板型标识 `esp32-lin` 独立，不要复用其它板型的固件，避免 OTA 串板。
- 4 MB Flash 空间紧张：不要在此板型启用大字体、Emote 主题或额外资源包。
- MCLK 由 I2S0 在 GPIO0 生成，不要为其它功能占用 GPIO0。
- 验机记录索引：`VERIFY-final.md` 是最终结论；`VERIFY.md`（烧录与启动）、
  `VERIFY-network.md`（配网与无屏激活）、`VERIFY-zh.md`（中文与立体声）、
  `VERIFY-mic-right-chat.md` / `VERIFY-final.md`（单路与混合输入对话）、
  `VERIFY-knob.md`（音量电位器接入、WDT 回归修复与标定方法）。
- 专题研究：`NOISE-ROOT-CAUSE.md`（输出底噪根因、增益结构错配、5 个判别实验与 5 套方案）；
  `docs/audio-playback-debug-playbook_zh.md`（音频连续性排查方法论，跨板复用）。
- **板内自检复验方法**（代码不常驻 core，为减小官方 merge 冲突面）：修复预缓冲超时
  与 speaking 看门狗时，曾用临时脚手架做过板内 A/B：A 注入 2 帧短流验证首帧
  338 ms 吐出（否则卡死到看门狗 abort）、B 切 speaking 队列空 14 s 验证不误杀
  （abort=0、state 保持 speaking）。脚手架已删；原始记录见 `NOISE-ROOT-CAUSE.md`
  与本目录 `VERIFY-final.md`。需要复验时照该思路临时加注入任务，跑完即删。
- **同步官方更新（merge）清单**：`origin` = `78/xiaozhi-esp32.git`，HEAD 为官方版本。
  `git pull` 后按下面处理本板的 core 改动（`esp32-lin/` 是新增目录，官方不会碰）：
  - **必留、冲突手工解**：`CMakeLists.txt` 的 `BOARD_TYPE_ESP32_LIN` 分支、
    `Kconfig.projbuild` 的 `BOARD_TYPE_ESP32_LIN` 选项——板级注册，官方加板时插同处。
  - **音频修复 hunk，官方若改同函数需重验**：`audio_service.cc` 的 `AudioOutputTask`
    （预缓冲 250 ms 超时、静音保活 3×20 ms 分片、持锁写时间戳）、`ResetDecoder`
    （时间戳清零）、`OpusCodecTask`（持锁区不加日志）；`audio_service.h` 的
    `MAX_PLAYBACK_TASKS_IN_QUEUE=6` 与预缓冲/保活成员。官方更新这些函数后，
    用真机长故事重跑一次播放，确认无「顿一下 / 断声 / 短回复卡住」。
  - **看门狗 hunk，官方若改主循环需重验**：`application.cc` 的 CLOCK_TICK 分支、
    `application.h` 的 3 个 `wd_` 成员。改后用「`Speaking stalled: no audio` 日志应为 0」加真机验证。
  - **板级行为，已下沉不占 core**：WM8978 AUTO-MUTE dither 在
    `wm8978_audio_codec.cc::OutputData()` 里，官方动 core 音频链路不受影响。
  - 同步后固定动作：`python3 scripts/build.py esp32-lin --language zh-CN` 编译
    （build.py 会重置 sdkconfig，**必须带 `--language zh-CN`**），烧 `0x10000`，
    抓 45 s 启动日志确认 0 WDT / 0 E 级 / 内存稳定。
- **逐 hunk 可丢弃性分级**（merge 冲突时按此决定取舍；改造后 core 共 183 行）：

  | 级别 | hunk | 冲突时怎么办 | 丢了会怎样 |
  | --- | --- | --- | --- |
  | **A 必留** | `CMakeLists.txt` BOARD_DIR 分支（2）、`Kconfig.projbuild` BOARD_TYPE 选项（3） | 手工保留，官方加板插同处易解 | 板子无法注册、编译不过 |
  | **A 必留** | `audio_service.cc::AudioOutputTask` 整组：`wait`→`wait_for(20ms)`、预缓冲超时、静音保活 3×20 ms、`last_real_frame_us_` 持锁写（~90）；`ResetDecoder` 时间戳清零（3） | **整组保留，不可拆**——互相依赖，单独恢复官方任一块都会坏 | 回到"收一帧播一帧"，UDP 抖动/分句停顿重现「顿一下」；短回复卡死 |
  | **A 必留** | `audio_service.h`：`MAX_PLAYBACK_TASKS_IN_QUEUE 2→6`、预缓冲/保活成员、`GetOutputFrameCount`（~40） | 手工保留。**唯一修改官方已有行的是队列深度 2→6**，官方若也动这行选 6 | 队列退到 2 帧，抖动缓冲消失 |
  | **B 可丢** | `application.cc` CLOCK_TICK 看门狗（35）、`application.h` 的 `wd_` 成员（5） | 官方大改主循环且冲突难解时，可整块丢弃 | 失去"播放真卡死自动回 idle"兜底；正常播放/待命不受影响，卡死后需手动重启会话 |
  | **B 可丢** | `audio_service.cc::OpusCodecTask` 的"持锁区不加日志"注释（5） | 纯说明，随便丢 | 无功能影响，仅失去"别在锁内打日志"的提醒 |
  | **— 不占 core** | WM8978 dither 在 `wm8978_audio_codec.cc::OutputData()`；UDP 优先级在板级 | 官方动 core 时完全不受影响 | — |

  优先级：**A 丢了板子就坏，必须逐行保；B 丢了只是少个兜底或提醒，可直接用官方版**。
  A 组内部有依赖：`wait_for` + 预缓冲 + 保活 + 时间戳清零是一套，官方改 `AudioOutputTask`
  时最省的做法是整段用我们的版本覆盖，别逐 hunk merge。
- **首次实际同步记录（2026-11-17）**：`a73e9a7 → aaf1eb7`（31 提交 / 83 文件，官方升到 v2.6.0）。
  远端碰了 `CMakeLists.txt`、`Kconfig`、`application.cc/.h`，但 hunk 位置与我们的不重叠；
  `audio_service.cc/.h` 官方完全没碰。**用 `git stash push -u` → `git merge --no-edit origin/main`
  → `git stash pop` 三步完成，零冲突**（git 三方合并自动处理）。
  注意：fetch 浅克隆可能 SSL 中断，先 `git config http.postBuffer 524288000` 再
  `git fetch --depth=50 origin main`。合并后编译需过官方新增的
  `-Werror=frame-larger-than=1536`（本次通过）；烧录后 90 s 日志：
  App v2.6.0、OTA/MQTT/激活全通、0 WDT/E级/panic/I2S告警、内存无泄漏。
- 已实机验收：耳机左右声道、双麦混合听感（无发空 / 变糊）、唤醒词「你好小智」、
  录音上行与 ASR、TTS 下行、MCP 音量工具、Wi-Fi 自动重连、服务器激活持久化、
  电位器 ADC 采样与启动零告警、  电位器两端标定（raw 0~4095、顺时针变大）。
  ⚠️ 「模拟增益压到 50 后不再嘶嘶」仅是 11-12 当时的结论；11-16 复测（含 60/58/56
  逐档收敛）用户仍反馈有噪音，故**未闭环**，根因与候选方案见 `NOISE-ROOT-CAUSE.md`。
- 仍未实机验证：RGB LED 颜色、KEY2 长按配网 / KEY3 唤醒词、扬声器（PAM8403）输出、OTA。
