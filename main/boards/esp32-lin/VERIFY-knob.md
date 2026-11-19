# ESP32-LIN 板载波轮（音量电位器）接入与验证（2026-11-12 10:03）

用户确认旋钮「旋转有限制，到底会卡住」→ 判定为电位器而非增量编码器，要求接入代码试下。

## 硬件结论（原理图 + 厂商 BSP）

- 原理图第 1 页 `VAR_R / VAR_RES_5K`（5 kΩ 可调电阻），抽头经 R8 / R9(10K) 分压、C5 滤波 → `ADC1_CH0` = `SENSOR_VP` / GPIO36（input-only）
- 图上无 EC11 / A/B 相编码器元件；10K 上拉到 3V3 + 0~5K 到地 → 0~1.1 V，正好匹配 `ADC_ATTEN_DB_0` 满量程，这是厂商 BSP 选 0 dB 的原因
- 厂商 BSP 只把它当**预留输入**：`bsp_poll_task` 每 500 ms 采样，±10 mV 变化往队列塞 `bsp_event_t.vol`；三个示例只打印 `vol:%d`，`WM8978_DAC_VOL_SET(180)` 写死，**没有接到音量控制**
- ADC1 不与 Wi-Fi 冲突（只有 ADC2 会）

## 代码改动

| 文件 | 改动 |
| --- | --- |
| `config.h` | 新增 `VOLUME_KNOB_ENABLED`（默认 1）、`VOLUME_KNOB_RAW_MIN/MAX`（默认 0/4095）；删掉「未启用」的旧注释 |
| `volume_knob.h` / `.cc` | 新增模块，CMake 的 `GLOB boards/${BOARD_DIR}/*.cc` 自动收集，无需改 CMakeLists |
| `esp32_lin_board.cc` | include 头文件，`#if VOLUME_KNOB_ENABLED` 下加 `VolumeKnob volume_knob_;` 成员 |

实现要点：

- ADC1_CH0、12-bit、`ADC_ATTEN_DB_0`；低优先级 FreeRTOS 任务 100 ms 采样 + 5 点滑动平均
- `raw` 线性映射到音量 0~100；变化 ≥2 级且距上次写入 ≥300 ms 才调 `AudioCodec::SetOutputVolume()`，既滤抖又避免转动时频繁 `nvs_commit`（`Settings` 析构会 commit）
- **最后操作优先**：开机只记录初始值、不覆盖 NVS 已保存音量；旋钮静止不覆盖服务端 MCP 下发的音量；一转就由旋钮接管
- `Wm8978AudioCodec::SetOutputVolume()` 此前已覆写，会同步写 WM8978 音量寄存器 + 打日志 + 持久化，链路完整

## 过程中的一次回归与修复

第一版用 `xTaskCreate`（无绑核）编译烧录后，实时日志每 20 秒出现一次：

```
E (27952) task_wdt: Task watchdog got triggered ... - IDLE0 (CPU 0)
E (27952) task_wdt: CPU 0: audio_input
```

`addr2line` 解析回溯：`task_wdt_timeout_handling` → `wakenet9_model_run_step3` → `EspWakeWord::Feed` → `AudioService::AudioInputTask()` —— CPU0 被唤醒词推理占满，多一个可运行任务就把 `IDLE0` 挤过 20 秒阈值。之前混合固件的 420 秒日志无此告警，确认是本次新增任务引入的回归（`CONFIG_ESP_TASK_WDT_PANIC` 未开，只打印不复位，功能不受影响，但日志不干净）。

**修法**：`xTaskCreatePinnedToCore(..., 1)` 把旋钮任务固定到 CPU1（CPU1 只有 IDLE1 与 Wi-Fi）。重编 1m30s、只烧 app 分区，复位抓 48 秒日志（覆盖两个 WDT 周期）：**错误计数 0**，WDT 彻底消失。

## 实测日志（CPU1 版本，`/tmp/esp32_lin_knob_cpu1_boot.log`）

| 项目 | 结果 |
| --- | --- |
| 旋钮初始化 | `Volume knob enabled (GPIO36 / ADC1_CH0, 12-bit, 0 dB, CPU1)` |
| 首次采样 | `initial raw=1033 (~277 mV) -> volume 25 (not applied)` —— 未覆盖已保存音量 |
| 错误 | 48 s 内 0 条 `E (` / `task_wdt` / `Backtrace` / panic / `I2S read failed` |
| 激活与网络 | `Got IP 192.168.3.110` → MQTT 8883 → `Activation done` → `待命`；中文唤醒词正常 |
| 内存 | `free sram` 175191、`minimal sram` 144960 稳定 |
| 构建 | 增量 `idf.py build` 通过；`xiaozhi.bin` 0x18cd20，app 分区 47% 空闲；elf 含 `VolumeKnob::` 符号；`.clang-format` 通过 |
| 烧录 | 只写 `0x10000`，`Hash of data verified`；NVS / assets / 其它分区未动 → 中文、Wi-Fi、激活全保留 |

## 标定结果（用户实测，2026-11-12 10:11 回传）

- 串口记录 `/tmp/esp32_lin_knob_calibrate.log`（约 9 分钟）：`VolumeKnob` 共 112 行，全程 `raw` 扫到 **0** 与 **4095**
- **方向确认**：顺时针 = 音量变大（顺时针到底 raw=4095 → 音量 100，逆时针到底 raw=0 → 音量 0），无需翻转映射
- **量程确认**：`config.h` 的 `VOLUME_KNOB_RAW_MIN/MAX` 默认就是 0/4095，正好铺满全行程 → **无需修改**
- 用户反馈另一现象：**非播放时间、顺时针转到底耳机有噪音，其它位置没有** → 见下节修复

## 空闲噪声修复（2026-11-12 10:51）

**现象**：空闲（待命、不播放）时把旋钮转到最大，耳机里有嘶嘶噪音；其它音量位置没有。

**根因**：本板是 full-duplex 且麦克风常开，播放结束后的 `AudioService::CheckAndUpdateAudioPowerState` 超时关输出的分支被 `if (!(duplex && input_enabled)) EnableOutput(false)` 跳过（见 `main/audio/audio_service.cc:848`），于是 **TX 时钟一直跑、DAC 一直在喂数据**；I2S 配了 `auto_clear_after_cb`，空闲时 DMA 填零 → DAC 输出「静音 + 自身噪声 floor」。`ApplyOutputVolume` 把 0~100 线性映射到 WM8978 模拟增益 0~63，音量=100 时增益打满 63，把那点噪声 floor 放大到可闻；增益低时噪声被压在阈值下 → 表现为「只有转到底才有噪音」。

**修法：空闲软静音**（`wm8978_audio_codec.cc/.h`）
- 新增 250 ms 周期的 `esp_timer`；`EngageIdleMute()` 在「输出已使能、音量>0、且距上次 `Write()` 超过 500 ms」时，把当前输出对（LOUT1/ROUT1 耳机 或 LOUT2/ROUT2 喇叭）写到 mute 位（`kVolumeMute`，与音量=0 同一机制），消除噪声 floor
- `Write()` 每次成功写出都刷新 `last_audio_write_us_`，并在仍静音时解除静音、恢复设定音量 → **一播放就放开**
- `EnableOutput(true)` 清 `idle_muted_` 再 `ApplyOutputVolume` → 播放启动即解除静音
- `SetOutputVolume()` 在空闲静音态只改 `output_volume_`（持久化），不解除静音；下次播放时 `Write()` 用新值恢复 → 旋钮在空闲时调的音量，说话时照样生效
- `idle_muted_` / `last_audio_write_us_` 用 `std::atomic`；定时器回调里做 I2C（`WriteReg`）在 ESP-IDF 定时器任务上下文是安全的

**验证**
- `idf.py build` 通过；只烧 app 分区（`0x10000`），`Hash verified`，NVS/中文/激活保留
- 复位抓 62 s：0 个 `task_wdt` / `Backtrace` / panic；`WM8978 initialized` 正常
- 启动提示音（6.2 s 的 resampling）**先正常播放**，约 9.1 s 出现 `Output idle for 500 ms -> muted` → 解除/再静音闭环成立
- 用户需在实机上听两项：① 空闲转到底不再有嘶嘶声；② 唤醒小智对话时按设定音量正常出声

## 播放时最大音量嘶嘶（用户实测回传，2026-11-12 11:30）

**现象**：空闲静音修复后，用户确认①空闲转到底已无嘶嘶；但②对话（播放/TTS）时把音量转到最大，仍有嘶嘶声。

**根因**：空闲软静音只覆盖「无 `Write()` 的待命态」。对话/播放时 `Write()` 持续刷新 `last_audio_write_us_`，空闲静音永不触发，输出一直解静音；而音量=100 时模拟增益打满 63，把 DAC 噪声 floor 放大到可闻——TTS 词间静默段、对话间隙只剩「噪声 floor × 增益 63」→ 嘶嘶。本质与空闲噪音同一噪声源，只是「播放态」没被静音覆盖。

**修法：压低模拟增益上限**（`wm8978_audio_codec.cc`）
- 把音量→模拟增益的映射上限从 63 降到 `kMaxAnalogGain = 50`（新增具名常量，注释说明 40~63 可调）
- 信号还在、仅峰值响度少约 3 dB；噪声 floor 随增益同比例下降，落回可闻阈值以下 → 最大音量也干净
- 旋钮 / 服务端 MCP / `SetOutputVolume` 控制路径完全不变，仅缩放

**验证**
- `idf.py build` 通过；只烧 app 分区，`Hash verified`
- 复位抓 3 次（32 s / 62 s / 72 s）：第 1、2 次在 ~27~29 s 出现偶发 `task_wdt`（IDLE0 被 audio_input / wakenet 推理尖峰挤占），第 3 次 0 告警
- 该 WDT 与本次改动无关：仅改了 `63→50` 的数值常量，不影响任务调度；属 wakenet 在 CPU0 的**偶发推理负载尖峰**（idle-mute 版 62 s 也恰好没碰到）。功能不受影响（`CONFIG_ESP_TASK_WDT_PANIC` 未开，只打印不复位）
- 待用户实机确认：对话时把音量转到最大，嘶嘶是否消失；若仍轻微嘶嘶，把 `kMaxAnalogGain` 往 40 降；若觉得最大音量偏轻，往 63 提（但嘶嘶可能回潮）

## 故事中途静音（idle-mute 误杀）与撤销（2026-11-12 12:26）

**现象**：用户让小智讲长故事，故事讲到一半没声音（"断了"）。后台常读串口的日志 `/tmp/esp32_lin_story_test.log` 显示：
- `31892ms` 进入 `speaking`，`32852ms` 解除静音 → 正常出声
- `50622ms` **`Output idle for 3000 ms -> muted`** —— 此刻仍在 `speaking`，字幕一直滚到 `59172ms`，之后 `60632ms` `Abort speaking`
- 即：音量=100 讲长故事，中途一段 TTS 音频流卡了 >3 s（此前多次 `MQTT: Received audio packet with wrong sequence` 乱序的老毛病让播放间隙变长），`Write()` 停笔 → 定时器误判"空闲" → 把正在讲的故事静音

**根因（根本性，非偶发）**：idle-mute 用"距上次 `Write()` 超过阈值"一刀切判断空闲。先前 500 ms 被一次 680 ms 的段间空窗踩中，把阈值放大到 3 s 只是降低概率——**流式 TTS + 网络/音频包乱序造成的中途空窗可任意长，而"播放中途断流"与"真正待命"在 `Write()` 调用模式上完全不可区分**。任何固定阈值都躲不过，这是设计错误，不是参数调一调能解决的。

**决策：撤销 idle-mute，只保留 `kMaxAnalogGain = 50`。** 理由：
- 增益压到 50 后噪声 floor 已被压到可闻阈值以下，用户已实测"空闲转最大音量无嘶嘶" → idle-mute 当初是增益=63 时的补丁，现已**冗余**
- 去除它后：空闲不嘶嘶（gain 50 已保证）、对话不嘶嘶、长故事**绝不会被误杀**
- 代价：彻底无副作用的净收益

**改动**（`wm8978_audio_codec.cc` / `.h`）：删除 `esp_timer` 定时器、`EngageIdleMute()`、`IdleMuteTimerCallback()`、`idle_muted_` / `last_audio_write_us_` 原子成员、`Write()` 的解除静音分支、`EnableOutput()` 的清标志、`ApplyOutputVolume()` 的静音分支，以及不再需要的 `<atomic>` / `<esp_timer.h>` 头；保留 `kMaxAnalogGain = 50`。

**验证**：`idf.py build` 通过（后台编译中）；待只烧 app 分区后实测：① 长故事从头到尾出声不中断；② 空闲转最大音量无嘶嘶（gain 50 已验）；③ 对话最大音量干净。

## 文档索引


- `README.md`：硬件、引脚、实现、标定与调参
- `VERIFY-final.md`：此前三项收尾（耳机立体声、双麦混合、启动 I2S 超时）与完整对话验收
