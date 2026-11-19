# ESP32-LIN 移植最终验收（2026-11-12 08:59）

用户三项确认：① 耳机两边均有声音；② 麦克风改两路混合；③ 修复启动 I2S 超时。以下为代码改动与实测日志结果。

## 1. 耳机立体声 —— 实机验收通过

- 早期版本 I2S 配成 `MONO + SLOT_LEFT`，WM8978 是立体声 codec，右声道 slot 无数据 → 耳机只有一边响
- 改为 `STEREO + SLOT_BOTH`，`Write()` 把单声道 PCM 复制到左右两个 slot
- **用户实机确认：耳机左右两边均有声音**，该项闭环

## 2. 双麦克风混合

- `config.h`：新增 `AUDIO_CODEC_INPUT_MODE`（`0` 只取左 / `1` 只取右 / `2` 混合），默认 `2`，替代旧的 `AUDIO_CODEC_INPUT_RIGHT_CHANNEL`
- `Read()`：从立体声帧取左右两路，用 `int32_t` 求 `(L+R)/2` 防溢出；编译期 `#if` 选择，无运行时分支开销
- 两个板载麦克风（J2L1 / J2R1 差分输入，分别进 WM8978 左右 PGA）都利用上
- 启动日志实测：`WM8978 initialized (24000 Hz, headphone LOUT1/ROUT1 output, input mix)`
- 保留单路切换：若两麦相位干涉导致声音发空、识别变差，把宏改成 `0` 或 `1` 即可

## 3. 启动 I2S 读超时 —— 已根治

- 原因：ESP32 全双工模式下 RX 是 slave，BCLK/WS 由 TX 通道提供；`AudioService` 先 `Set input enable to true` 再 `Set output enable to true`，第一帧 RX 读拿不到时钟，200 ms 后超时打印 `E Wm8978AudioCodec: I2S read failed or timed out`
- 修法：新增 `tx_enabled_` 状态；`EnableInput(true)` 先拉起 TX 再 enable RX，录音期间保持 TX 不关（`EnableOutput(false)` 时若输入仍启用也不关 TX）；`EnableInput(false)` 且输出也关闭时才关 TX
- 实测启动日志：**不再出现** `I2S read failed or timed out`；`Set input enable to true` (7612 ms) → `Set output enable to true` (7622 ms)，中间不再有 200 ms 空档

## 构建与烧录

- 增量 `idf.py build` 51 s，`BUILD_EXIT=0`；`xiaozhi.bin` 0x18ba30，app 分区 47% 空闲；仓库 `.clang-format` 校验通过
- **只写 `0x10000 build/xiaozhi.bin`**，`Hash of data verified`；NVS、assets、bootloader、分区表、ota_data 均未触碰
- 中文界面（`zh-CN` / `你好小智`）、Wi-Fi 凭证、服务器激活状态全部保留

## 启动日志核验（`/tmp/esp32_lin_mix_live.log`，247 行 / 12 KB，一次上电 + 一轮完整对话）

| 项目 | 结果 |
| --- | --- |
| 输入模式 | `input mix` |
| I2S 错误 | 无 `I2S read/write failed`、无 panic / abort / error / timeout |
| 激活状态 | MQTT `api.tenclass.net:8883` 连上 → `Activation done` → `activating -> idle` → `待命` |
| 中文与唤醒词 | `wakenet9l_tts1h8_你好小智`、`LiteAudioEngine: Initialized, WakeNet: yes`、中文提示语 |
| Wi-Fi | 自动连回 `OpenWrt`（`192.168.3.110`） |
| 音频通道 | `Set input enable to true` → `Set output enable to true`，16k→24k 重采样正常 |
| PSRAM / assets | 4 MB PSRAM、assets 576 KB / 1024 KB、无屏路径正确跳过 UI 资源 |

## 混合输入完整对话验证（09:06，用户「已对话」后复核）

同一份实时日志 `/tmp/esp32_lin_mix_live.log` 覆盖了挂载后 353.7 s 开始的一轮完整对话：

| 时间 (ms) | 事件 |
| --- | --- |
| 353742 | `idle -> connecting`，Wi-Fi 切 PERFORMANCE（关省电） |
| 353912 | MQTT `Session ID: 00341bdd` |
| 353942 | **`Wake word detected: 你好小智`**（混合输入下唤醒词命中）→ `connecting -> listening` |
| 358812 | ASR `>> 音量8士。` |
| 358822 | `listening -> speaking` |
| 359842 / 360842 | MCP 工具 `self.get_device_status`、`self.audio_speaker.set_volume` → **`Set output volume to 80`** |
| 362172 | TTS「音量已经调到80啦，现在听着刚刚好～」 |
| 367032 | `speaking -> listening`，继续拾音 |
| 375302 | 第二轮 ASR `>> 退出。` → `speaking` |
| 376432 | TTS「好嘞，我退下啦，拜拜～」 |
| 379562 | `speaking -> listening`，`Received goodbye message`，`Closing audio channel` |
| 379592 | `listening -> idle`，`待命`，Wi-Fi 回 LOW_POWER |

**结论**

- **零错误**：全日志 `I2S read failed` 出现 0 次；无 `E (` 级日志、无 error / failed / timeout / panic / abort / assert / WDT；仅一次 `POWERON_RESET`，连早期偶发的 `flash read err` 都没出现。启动超时根治在「启动 + 整轮对话」两个场景都生效。
- **混合输入不劣化识别**：唤醒词正常命中，两轮 ASR 均出文本，服务端把 `音量8士` 正确解析为 80 并写入 WM8978。对照右路单声道日志，同一指令曾识别为 `>> 音量80。` 和 `>> 嗯80。` —— 都有单字偏差，混合未见明显变差。
- **状态机一圈无卡死**：`idle→connecting→listening→speaking→listening→speaking→listening→idle`，两轮对话 + goodbye 正常收尾、音频通道正确关闭。
- **内存稳定**：`free sram` 203963 → 178115（对话后 178055），`minimal sram` 全程 149600 不变，无泄漏迹象；4 MB PSRAM、`SKU=esp32-lin`、中文资源与 `wn9_nihaoxiaozhi_tts` 均正常。
- **时钟时序**：`Set input enable` (7612 ms) → `Set output enable` (7622 ms)，10 ms，无等待空档。

### 验收状态（09:10 定稿）

- **主观听感验收通过**：用户确认混合后「没有别扭」，无发空 / 变糊 / 双麦相位干涉迹象。`AUDIO_CODEC_INPUT_MODE=2` 定为最终配置，不再调整。
- 保底开关：`config.h` 改 `0`（左）或 `1`（右）可退回单路，增量编译约 1 分钟、只烧 `0x10000`。后续换麦克风位置或改板时再评估。
- 仍未实机验证：RGB LED 颜色、KEY2 长按配网 / KEY3 唤醒词、扬声器（PAM8403）输出、OTA。

## 播放中偶发「顿一下」—— 已闭环（2026-11-16）

- 根因：设备播放端丢弃了协议给出的 `timestamp = sequence × 60 ms` 时间轴，按「收一帧播一帧」处理；UDP 一旦短时抖动或跳号，输出流就断流，听感即「顿一下」。
- 修法在 `main/audio/audio_service.{h,cc}`：队列空且距上一真实帧 < 1 s 时输出静音帧保活 I2S，配合 `kPlaybackPrefillFrames = 4` 预缓冲；`ResetDecoder()` 一并复位 `last_real_frame_us_`。
- 板内 A/B 自测（不用服务端 / 麦克风 / 扬声器，以 `AUDIO_WRITE_RATE` 掉速判定 I2S 断流）：稳态断流次数 3 → 0。
- 完整根因、错误起点、已排除清单、三轮 A/B 数据、未验证项见 `README.md`「播放中偶发『顿一下』：根因与修复」。
- 可复用排查流程（症状分类 / 物理判据 / 板内 A/B 脚手架 / 分层清单 / 反面案例）见
  `docs/audio-playback-debug-playbook_zh.md`，换板移植遇同类问题照那份走。
- 未验证：官方 ESP32-S3 板串口日志未抓取、真人长对话听感未录音，均已在 README 中标注。

## 文档索引

- `README.md`：硬件、引脚、状态灯、编译烧录、无屏激活；**「播放中偶发『顿一下』：根因与修复」为音画连续性问题的权威说明**
- `NOISE-ROOT-CAUSE.md`：**输出底噪根因研究与方案清单（2026-11-16，未闭环）** ——
  增益结构错配、待命态输出级常开、WM8978 AUTO-MUTE、5 个零成本判别实验、5 套方案排序
- `docs/audio-playback-debug-playbook_zh.md`：音频连续性排查方法论（跨板复用）
- `MIC-INPUT.md`：双麦硬件分析与方案对比
- `VERIFY.md` / `VERIFY-network.md` / `VERIFY-activation.md` / `VERIFY-activated.md`：烧录、配网、激活
- `VERIFY-zh.md`：中文界面与立体声修复
- `VERIFY-chat.md` / `VERIFY-mic-right.md` / `VERIFY-mic-right-chat.md`：对话与右路试音
