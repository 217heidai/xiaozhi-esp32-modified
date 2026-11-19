# 右路麦克风试音固件（2026-11-12 08:49）

用户要求「现在切到右路，我试下」，用于对比两个板载麦克风哪一路干净。

## 改动

- `config.h`：`AUDIO_CODEC_INPUT_RIGHT_CHANNEL` 从 `0` 改成 `1`，`Read()` 改为取 `rx_buffer_[2 * i + 1]`
- `wm8978_audio_codec.cc`：启动日志增加 `input right/left`，便于从串口确认当前取的是哪一路
- 仅改这两个文件，仓库 `.clang-format` 校验通过

## 构建与烧录

- 增量 `idf.py build`，59 s，`BUILD_EXIT=0`；`xiaozhi.bin` 0x18b9a0，app 分区 47% 空闲
- **只写 `0x10000 build/xiaozhi.bin`**，`Hash of data verified`；bootloader、分区表、ota_data、assets、**NVS 均未触碰**
- 因此中文界面（`zh-CN` / `你好小智`）、Wi-Fi 凭证（`OpenWrt`）、服务器激活状态全部保留，无需重新配网或激活

## 验证

- 启动日志应出现 `WM8978 initialized (24000 Hz, headphone LOUT1/ROUT1 output, input right)`
- 已挂串口实时写入 `/tmp/esp32_lin_mic_right_live.log`（约 7 分钟窗口），用户试音后可核对状态流转、I2S 读写、收发包与唤醒词

## 待用户反馈

1. 右路麦克风拾音是否清晰、有没有串功放回声
2. 与之前左路对比，哪一路更适合作为默认
3. 之后再定：择一使用，还是按 `MIC-INPUT.md` 的推荐改成 `(L+R)/2` 混合

## 相关文档

- `MIC-INPUT.md`：双麦硬件分析与方案对比
- `VERIFY-activated.md`：激活成功复核
- `VERIFY-chat.md`：对话后日志检查
