# 激活状态复核（用户反馈「已激活」之后）

日期：2026-11-11　板型 `esp32-lin`　固件：中文 / `wn9_nihaoxiaozhi_tts`

## 结论

**设备侧仍未完成激活。** 用户网页端操作后，设备日志依旧显示激活码并持续轮询，服务器没有下发「已激活」回执。中文界面、Wi-Fi、音频链路均正常，与激活无关。

## 日志证据（26 s + 75 s 两段）

- `StateMachine: State: starting -> activating`，始终没有 `activating -> idle`
- 仍打印 `Application: Alert [link] 激活设备: xiaozhi.me` 与 `670976`
- `Application: Activating... 1/10` → `5/10`，每轮 `HttpClient: Established new connection to api.tenclass.net:443` 后 `HTTP connection closed`
- **没有** `Ota: Activation successful`、**没有** `Application: Activation done`、**没有** `Failed to activate, code:` / panic / abort
- 网络、HTTPS、PSRAM、模型加载、I2S 全部正常

## 代码依据

`main/application.cc`：`CheckNewVersion()` 只要 OTA 响应里仍带 `activation` 对象，就会 `ShowActivationCode()` 并进入 10 轮轮询；只有 `!HasActivationCode() && !HasActivationChallenge()` 才跳出循环。

`main/ota.cc`：`Ota::Activate()` POST `/activate`：

| HTTP | 固件行为 |
| --- | --- |
| 200 | `ESP_LOGI("Activation successful")`，break 轮询 |
| 202 | `ESP_ERR_TIMEOUT`，延时 3 s 再问（服务器表示尚未绑定） |
| 其它 | 打印 `Failed to activate, code:` 并延时 10 s |

日志中每轮「建连 → 关闭」且无任何错误行，正是 **202** 的表现：设备和服务器通信正常，但服务器仍判定该设备未激活。10 轮跑完后 `ActivationTask` 仍会发 `MAIN_EVENT_ACTIVATION_DONE` 并打印 `Activation done` 进 idle，那只是本地流程结束，**不代表绑定成功**；重启后又会重新显示激活码。

## 排查清单

1. 确认 `xiaozhi.me` 登录的是目标账号。
2. 重新输入设备激活码 `670976`，**点提交 / 绑定按钮**，不是只填输入框；网页应提示激活成功。
3. 设备列表里应出现这台设备（MAC `30:ae:a4:5f:0b:10`）。
4. 提交成功后，设备下一轮 POST 应返回 200，日志出现 `Ota: Activation successful` → `Application: Activation done` → `State: activating -> idle`，且重启后不再显示激活码。
5. 若网页确实显示成功但设备仍是 202，重启设备重新拉一次 OTA 配置，再抓日志确认。

## 其它链路（本轮一并复核，均正常）

- 中文状态文本：`扫描 Wi-Fi...` / `已连接 OpenWrt` / `检查新版本...` / `激活设备`
- Wi-Fi 凭证持久化：重启自动连回 `OpenWrt`，IP `192.168.3.110`
- `MODEL_LOADER: Successfully load srmodels`（中文唤醒词模型）
- `Wm8978AudioCodec: Duplex I2S channels created (MCLK on GPIO0)`，立体声 slot 无 I2S 报错
- `Assets: The assets map size is 576 KB (partition 1024 KB)`，无屏路径正确跳过 UI 资源

## 相关文档

- `VERIFY.md`：烧录与首次启动
- `VERIFY-network.md`：配网、无屏激活方法、运行期日志
- `VERIFY-zh.md`：中文界面与立体声修复

---

**次日复核（2026-11-12 08:30）：用户完成网页绑定后已激活成功。** 重启日志不再出现激活码，直接 `登录服务器...` → MQTT 8883 连上 → `Activation done` → `activating -> idle` → `待命`，中文唤醒词模型加载正常。详见 `VERIFY-activated.md`。
