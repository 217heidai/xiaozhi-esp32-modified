# 对话后日志检查（2026-11-12 08:38）

## 这次抓到了什么

用户反馈「已完成对话」后抓串口日志，但 **CP2104 自动下载电路在打开串口时触发了一次 `POWERON_RESET`**（预设置 `dtr/rts=False` 也挡不住），对话过程的日志被冲掉，抓到的是一次全新启动（22 s，153 行）。

## 启动日志结论（全绿）

| 项目 | 结果 |
| --- | --- |
| 激活持久化 | 不再出现验证码，直接 `登录服务器...` → MQTT `api.tenclass.net:8883` 连上 → `Activation done` → `activating -> idle` → `待命` |
| 中文唤醒词 | `wakenet9l_tts1h8_你好小智` 加载、`EspWakeWord: Wake word(wn9_nihaoxiaozhi_tts)`、`LiteAudioEngine: Initialized, WakeNet: yes` |
| 音频通道 | `Wm8978AudioCodec: Duplex I2S channels created (MCLK on GPIO0)`、`Set input enable to true`、中文启动提示语 |
| Wi-Fi | 自动连回 `OpenWrt`，IP `192.168.3.110` |
| 稳定性 | 无 panic / abort，本次启动一次成功（没有 RTCWDT 二次复位），内存稳定 |

**每次启动固定出现一次** `E (7322) Wm8978AudioCodec: I2S read failed or timed out`：全双工模式下 RX 是 slave，BCLK/WS 要等 TX 通道使能；`AudioService` 先 `Set input enable to true` 再 `Set output enable to true`，中间那 200 ms 读超时。随后 TX 起来，唤醒词引擎与录音通道均正常，30 s 内不再复现。修法：`EnableInput(true)` 时先拉起 TX，并在输入仍启用期间保持 TX 不关（需重编重烧）。

## 从「已完成对话」能推断的

用户能完整走完一次对话，说明：

- 麦克风取左声道（`AUDIO_CODEC_INPUT_RIGHT_CHANNEL=0`）是对的，录音上行有效
- 播放下行有效，立体声 slot 改动后 I2S 写入没有持续报错
- 状态机 `idle → connecting → listening → speaking → idle` 能跑通，MQTT/服务端链路正常

耳机**左右两边**的实际听感仍需戴耳机人工确认（串口只能证明数据链路）。

## 已挂实时抓取

后台脚本已保持串口打开，持续写入 `/tmp/esp32_lin_chat_live.log`（约 7 分钟窗口）。用户再触发一次对话（KEY1 单击或喊「你好小智」）后，可逐项核对：

- `StateMachine` 状态流转与 `Set input/output enable`
- 是否再出现 `I2S read/write failed`（尤其每次进入 listening 时）
- MQTT / WebSocket 收发、ASR / TTS 相关日志
- 唤醒词触发（`Wake word detected`）
- 内存是否随对话次数稳定、有无 panic

## 相关文档

- `VERIFY.md`：烧录与首次启动
- `VERIFY-network.md`：配网与无屏激活
- `VERIFY-zh.md`：中文界面与立体声修复
- `VERIFY-activation.md`：激活排查（结论已更新）
- `VERIFY-activated.md`：激活成功复核
