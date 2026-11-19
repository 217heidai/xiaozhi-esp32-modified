# 激活成功复核（2026-11-12 08:30）

用户在 `xiaozhi.me` 完成绑定后，复位抓 30 s 启动日志（115200，pyserial）。

## 结论：激活已落地

| 证据 | 日志 |
| --- | --- |
| 不再下发激活码 | 6 位码检测为空，无 `Alert 激活设备` |
| 直接登录服务器 | `Display: SetStatus: 登录服务器...` |
| MQTT 连接建立 | `MQTT: Connecting to endpoint api.tenclass.net:8883` → `MQTT: Connected to endpoint` |
| 本地激活流程完成 | `Application: Activation done` |
| 进入待命 | `StateMachine: State: activating -> idle`、`Display: SetStatus: 待命` |
| 中文唤醒词模型 | `MC Quantized wakenet9: ...你好小智...`、`EspWakeWord: Wake word(wn9_nihaoxiaozhi_tts)` |
| 唤醒词引擎就绪 | `LiteAudioEngine: Initialized, WakeNet: yes` |
| 录音通道开启 | `AudioCodec: Set input enable to true` |
| 中文提示语 | `Startup wake word hint: 说「你好小智」就能唤醒我。` |

Wi-Fi 仍自动连回 `OpenWrt`（`192.168.3.110`），PSRAM、assets（576 KB / 1024 KB）、无屏跳过 UI 资源、WM8978 立体声 I2S（MCLK GPIO0）均正常，中文界面文本正常。

## 一处启动期小瑕疵（不影响使用）

`E (8493) Wm8978AudioCodec: I2S read failed or timed out` 只在启动时出现一次。

原因：ESP32 全双工模式下 RX 被切成 slave，BCLK/WS 由 TX 通道提供；`AudioService` 启动时先
`Set input enable to true` 再 `Set output enable to true`，中间那一次 RX 读拿不到时钟而超时。
随后 TX 使能，唤醒词引擎与录音通道均正常，30 s 内没有再出现该错误。

修法（如需）：`EnableInput(true)` 时若 TX 未使能则先拉起 TX 提供时钟，并在输入仍启用期间保持
TX 不关。需要重编重烧，当前不影响功能。

## 仍需人工实测

1. 戴耳机听左右声道（立体声修复的最终验收点）
2. 按 KEY1 或喊「你好小智」触发对话，看 LED 红/绿状态与服务器回包
3. 对麦克风说话确认录音上行（若无声把 `AUDIO_CODEC_INPUT_RIGHT_CHANNEL` 改成 `1`）
4. KEY2 长按重新配网、KEY3 唤醒词、音量 MCP 工具、OTA

## 关键日志摘录

```text
I (1343) wifi:mode : sta (30:ae:a4:5f:0b:10)
I (5033) WifiStation: Got IP: 192.168.3.110
W (5033) Display: ShowNotification: 已连接 OpenWrt
I (5043) StateMachine: State: starting -> activating
I (5203) MODEL_LOADER: Successfully load srmodels
I (5203) Assets: Skipping UI assets: LVGL display is unavailable
W (5213) Display: SetStatus: 检查新版本...
I (6673) Ota: Current is the latest version
W (6673) Display: SetStatus: 登录服务器...
I (6673) MQTT: Connecting to endpoint api.tenclass.net:8883
I (6673) MQTT: Resolved MQTT host api.tenclass.net -> 47.112.110.152
I (8073) MQTT: Connected to endpoint
I (8073) Application: Activation done
I (8073) StateMachine: State: activating -> idle
W (8103) Display: SetStatus: 待命
MC Quantized wakenet9: wakenet9l_tts1h8_你好小智_3_0.631_0.635
I (8273) EspWakeWord: Wake word(wn9_nihaoxiaozhi_tts),freq: 16000, chunksize: 512
I (8283) LiteAudioEngine: Initialized, WakeNet: yes
I (8283) AudioCodec: Set input enable to true
E (8493) Wm8978AudioCodec: I2S read failed or timed out
I (8493) AudioCodec: Set output enable to true
I (8493) AudioService: Resampling audio from 16000 to 24000
I (8493) Application: Startup wake word hint: 说「你好小智」就能唤醒我。
```

## 相关文档

- `VERIFY.md`：烧录与首次启动
- `VERIFY-network.md`：配网、无屏激活方法、运行期日志
- `VERIFY-zh.md`：中文界面与立体声修复
- `VERIFY-activation.md`：昨日「尚未激活」的排查记录（结论已由本文件更新）
