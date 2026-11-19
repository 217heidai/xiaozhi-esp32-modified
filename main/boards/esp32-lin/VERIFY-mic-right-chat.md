# 右路麦克风试音日志复核（2026-11-12 08:51）

用户「已录制语音」后，读取后台实时串口日志 `/tmp/esp32_lin_mic_right_live.log`（197 行）。本轮固件为 `input right`（`AUDIO_CODEC_INPUT_RIGHT_CHANNEL=1`）。

## 结论：右路麦克风全链路打通

| 环节 | 日志证据 |
| --- | --- |
| 启动确认右路 | `WM8978 initialized (24000 Hz, headphone LOUT1/ROUT1 output, input right)` |
| 唤醒词检测 | `Application: Wake word detected: 你好小智 (state: 3)` |
| 状态机流转 | `idle -> connecting -> listening -> speaking -> listening` |
| MQTT 会话 | `MQTT: Session ID: 1e6ad220` |
| **录音上行 / ASR** | `Application: >> 音量80。`（右路拾音被正确识别成文字） |
| MCP 工具 | `Application: << % self.audio_speaker.set_volume...` → `AudioCodec: Set output volume to 80` |
| TTS 回复 | `Application: << 音量已经调到八十了，就这么点事儿，还专门喊我。` |
| 中文界面 | `Display: SetStatus: 聆听中...` / `说话中...` |

## 关键验证点

- **整个对话过程没有再出现 I2S 读写错误**。启动期 `E (6813) Wm8978AudioCodec: I2S read failed or timed out` 仍只出现一次，印证此前判断：ESP32 全双工 RX 是 slave，BCLK/WS 由 TX 提供，`AudioService` 先开输入后开输出，第一帧扑空；随后 TX 使能即恢复。
- 无 panic / abort，`free sram` 稳定在 170–178 KB。
- `AudioCodec: Set output volume to 80` 说明驱动的 `SetOutputVolume()` → `ApplyOutputVolume()` 写 WM8978 音量寄存器链路在运行时也生效（MCP 工具端到端打通）。
- 激活状态、Wi-Fi、中文界面、唤醒词模型均保持正常，无需重新配网/激活。

## 左右两路对比

| 声道 | 验证情况 |
| --- | --- |
| 左路（此前默认 `0`） | 用户已完整完成过一次对话 → 可用 |
| 右路（本轮 `1`） | 唤醒词 + ASR + MCP 工具 + TTS 全通 → 可用 |

两路都能用。是否改成 `(L+R)/2` 混合见 `MIC-INPUT.md`：混合能同时利用两路拾音、噪声略降，但两个麦间距近时可能有相位干涉（声音发空），需实机确认。当前配置保持右路。

## 待确认

1. 耳机左右声道实际听感（立体声修复的最终验收点）
2. 默认策略：保持右路 / 切回左路 / 改混合
3. 启动期那行 I2S 超时是否要修（`EnableInput(true)` 时先拉起 TX 提供时钟）

## 完整关键日志

```text
I (1113) Wm8978AudioCodec: WM8978 initialized (24000 Hz, headphone LOUT1/ROUT1 output, input right)
I (6403) MQTT: Connected to endpoint
I (6403) StateMachine: State: activating -> idle
I (6603) EspWakeWord: Wake word(wn9_nihaoxiaozhi_tts),freq: 16000, chunksize: 512
I (6603) LiteAudioEngine: Initialized, WakeNet: yes
I (6613) AudioCodec: Set input enable to true
E (6813) Wm8978AudioCodec: I2S read failed or timed out
I (6813) AudioCodec: Set output enable to true
I (85833) Application: Wake word detected: 你好小智 (state: 3)
I (85833) StateMachine: State: idle -> connecting
I (86003) MQTT: Session ID: 1e6ad220
I (86023) Application: Wake word detected: 你好小智
I (86023) StateMachine: State: connecting -> listening
W (86023) Display: SetStatus: 聆听中...
I (88743) Application: >> 音量80。
I (88753) StateMachine: State: listening -> speaking
I (89943) Application: << % self.audio_speaker.set_volume...
I (89943) AudioCodec: Set output volume to 80
I (92263) Application: << 音量已经调到八十了，就这么点事儿，还专门喊我。
I (94863) StateMachine: State: speaking -> listening
```

## 相关文档

- `MIC-INPUT.md`：双麦硬件分析与方案对比
- `VERIFY-mic-right.md`：右路固件改动与烧录
- `VERIFY-activated.md`：激活成功复核
- `VERIFY-chat.md`：对话后日志检查
