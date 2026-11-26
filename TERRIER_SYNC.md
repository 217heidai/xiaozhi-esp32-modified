# HachimoDock 小狗移植：同步上游手册

本分支（`hachimo-movecall-moji`）在小智官方固件基础上，把 HachimoDock 的
西高地小狗做成 Moji 板的表情，并把屏幕改成只显示小狗。本文档说明改了
什么、以及以后怎么跟上游官方改动同步。

## 基线

- 上游：`https://github.com/78/xiaozhi-esp32`
- 基线提交：`a3134949`（Merge pull request #2451 from Vitalymt/fix/power-save-display-key，2026-11-24）
- 板子：Movecall Moji ESP32-S3（`movecall/moji-esp32s3`），240×240 圆屏
- 语言 / 唤醒词是构建参数，不在代码里：`--language zh-CN --wake-word nihaoxiaozhi`

## 改了什么

共 15 个代码/配置/测试文件、319 行增、11 行删（相对上游基线 `a3134949`，含后续帧延时 `3af79eed`、轮播顺序 `3f12e08`、时长取整与错误清标志 `edd58fc` 三批改动）；另把 GIF 预览移到 `docs/images/terrier/`（16 张，含找回的 4 张）、加 16 段 EFC
（`main/assets/emoji/terrier/`，6,232,509 字节，约 5.95MB；zlib+legacy 编码，禁用 PRED，见“已知坑”）。
注意：`05de81f` 提交的是旧的带 PRED 版本；当前 zlib+legacy 版已随 `ee14a33`（去掉PRED，重新生成efc，2026-11-26）提交。二进制文件需手动提交，连接器推不了二进制。
所有代码改动都用 `CONFIG_PET_TERRIER_EMOTIONS` 开关包起来，开关一关就
和官方一模一样（见“开关”一节）。

| 文件 | 改动 | 作用 |
|---|---|---|
| `main/Kconfig.projbuild` | 新增 `PET_TERRIER_EMOTIONS`（默认开，仅 Moji 板） | 总开关 |
| `main/CMakeLists.txt` | Moji 板表情集按 `PET_TERRIER_EMOTIONS` 开关选 `terrier` / Noto；构建时把 `assets/emoji/<名>` 也列为依赖 | 打包小狗 EFC |
| `main/idf_component.yml` | 新增 `espressif/esp_efc_codec`（仅限 EFC 支持的目标） | EFC 解码器依赖 |
| `sdkconfig.defaults.esp32s3` | 开 `CONFIG_ESP_EFC_LVGL_WIDGET` | EFC 播放器 |
| `scripts/build_default_assets.py` | `get_emoji_collection_path` 增加 `main/assets/emoji/<名>` 回退；打包认 `.efc` | 找到本地表情集 |
| `main/display/lvgl_display/lvgl_image.{h,cc}` | 新增 `IsEfc()`（EFC1 魔数） | 识别 EFC 片段 |
| `main/application.cc` / `.h` | 状态→情绪映射、待机轮播（见下） | 对话联动 + 轮播 |
| `main/display/lcd_display.cc` | 非微信 SetupUI 藏三栏；`SetChatMessage` / `SetHideSubtitle` 只在配网、激活时取消隐藏聊天栏；`SetStatus` 同步开关状态栏；EFC 片段用 `lv_efc` 播放器播，`lv_efc_set_frame_delay(efc_image_, 125)` 把帧延时定为 125ms（8fps；EFC 默认 33ms/帧=30fps） | 正常只剩小狗，配网/激活可见；播 EFC，速度正常 |
| `main/display/lvgl_display/lvgl_display.cc` | `UpdateStatusBar` 里时钟分支包上 `#if !CONFIG_PET_TERRIER_EMOTIONS`；新增 `ShouldShowSetupUi()`：只在配网、激活状态下显示界面 | 不显示时间；配网/激活可见 |
| `main/boards/movecall/moji-esp32s3/README.md` | 小狗移植说明 | 文档 |
| `main/assets/emoji/terrier/`（新增） | 16 段 EFC，240×240，8fps | 表情资源 |

行为（开关开时）：

- 待机 → `neutral`、聆听 → `listening`、说话 → `speaking`；服务端下发
  的 `thinking` / `happy` / `surprised` / `error` 也显示小狗片段；
  没有对应片段的服务端表情回落为内置 Noto emoji 字体字形。
- 正常使用只显示小狗：不显示时间、顶部图标栏、状态栏（“待命”）、底部聊天栏。
- 配网、激活时例外：状态栏和底部聊天栏会显示（热点网址、激活提示），回到待机自动藏回；顶部图标栏始终隐藏。按 BOOT 键进配网那句“进入配网模式...”提示仍不可见（触发时状态还没切到配网），但随后 Alert 发的热点信息能正常显示。
- 待机轮换：开机先放一次 `welcome`，之后每段播满一遍就切到下一个
  （`neutral` → `begging` → `playing` → `eating` → `daydreaming` →
  `reading` → `browsing` → `traveling`，循环；按“狗的一天”排，每一步都是上一步的自然结果）。
  切歌时机按每段自身的时长（9 秒 / 8 秒 / 7 秒）；时长向下取整，begging 7.25 秒→7 秒、reading 8.5 秒→8 秒，尾巴直接切掉，不重播开头。
  一轮对话说完回到待机时放一次 `done`（打勾），然后继续轮换。
- 16 段全在资源里：EFC+zlib+legacy 压缩后 16 段合计 6,232,509 字节（约 5.95MB），总资源 7,813,544 字节，
  余量约 575KB（见“资源预算”）。

## 开关

`CONFIG_PET_TERRIER_EMOTIONS`（`menuconfig` → Xiaozhi Assistant → 本板相关）：

- 开（默认）：小狗 + 只剩小狗的屏幕 + 待机轮播。
- 关：恢复官方状态映射（待机 / 聆听都是 `neutral`，说话时不设情绪）
  与官方 UI（时间、状态栏、聊天栏都回来）。注意：`DEFAULT_EMOJI_COLLECTION`
  是写死 `terrier` 的，关掉开关后仍需把它改回 `noto-color-emoji_64`
  才能恢复 Noto 彩色 emoji 图片（见 Moji 板 README）。

## 同步上游

本分支就是官方仓库加上面这份小改动，同步就是拉官方新版再把改动合上去。
改动小、都有开关，正常情况下直接 rebase 就能过。

```bash
# 1. 取 main 最新版（fork 的 main 与上游 78/xiaozhi-esp32 保持一致；同步前先确认 fork 的 main 已同步上游）
git fetch origin

# 2. 把本分支 rebase 到 main 最新版
git rebase origin/main

# 3. 有冲突时逐个解。最可能冲突的是 main/application.cc
#    （官方常改状态机）。原则：保留 #if CONFIG_PET_TERRIER_EMOTIONS
#    包起来的小狗分支，#else 里保持官方原样；不要把小狗逻辑
#    写进 #else，也不要删官方新增的状态处理。
#    解完继续：
git rebase --continue

# 4. 验证（见“构建验证”）
```

如果不想 rebase，用 merge 也可以（`git merge origin/main`），同样按上面
的原则解冲突。rebase 历史更干净，merge 更不容易丢改动，任选其一。

### 容易冲突 / 要留意的点

- `main/application.cc`：`HandleStateChangedEvent` 与时钟 tick 里的小狗分支。
  官方如果重构了状态处理，先看懂官方新写法，再把小狗分支贴到对应的
  新位置，不要硬套旧行号。
- `main/display/lcd_display.cc`：只有非微信 SetupUI 末尾的藏三栏与
  `SetChatMessage` / `SetHideSubtitle` 里的两处 `#if !CONFIG_PET_TERRIER_EMOTIONS`。
  微信分支里只有 EFC 播放器的创建（与经典分支同款），其余小狗逻辑仍在经典分支。
- `scripts/build_default_assets.py` 与 `main/CMakeLists.txt`：改动小，
  官方如果改了同一函数，按“新增回退分支”的思路合。
- `main/assets/emoji/terrier/`：纯新增目录，官方没有，不会冲突。
  同步后如果官方改了 assets 分区大小或表情打包方式，先确认 16 段 EFC
  仍能塞进分区（见“资源预算”）。
- `main/Kconfig.projbuild`：只新增一个选项，几乎不会冲突。
- 根 `README.md`：本分支把它重写成了中文版，上游每次改它都会冲突；解法是保留本分支的版本（它没有 QQ 群号等上游内容，无需合上游的改动）。

## 资源预算

资源分区上限 8,388,608 字节。当前 16 段 EFC 合计 6,232,509 字节，
加其它资源约 1,581,035 字节，总资源 7,813,544 字节，余量约 575KB。
加新表情前先算总大小；超了就降色、降帧或去掉一段，不要直接加。

## 已知坑：EFC 不要用 PRED 编码

esp_efc_codec 1.1.0 的 PRED（预测）解码有 bug。不要用 `pred` 编码，始终用 `zlib+legacy`。

- **现象**：带 PRED 的 EFC 片段一播放就崩。2026-11-26 实测：开机后 `welcome` 播完，第一次 MQTT/TLS 发布时触发 `LoadProhibited` 崩溃，每次必现。
- **根因**：解码第 0 帧时，PRED 解码器越界写零了 TLS 全局互斥量 `mbedtls_threading_psa_globaldata_mutex` 的信号量（`psa_sem` 0x3fcadbfc → 0，互斥量指针本身完好，堆完整），随后 `psa_aead_encrypt` → `mbedtls_mutex_lock` → `xQueueSemaphoreTake(NULL)` 断言崩溃。PRED 是 esp_efc_codec 1.1.0 新加的功能，官方自己的 test_app 从没测过（官方测试文件都是 1.0.0 时代的，无 PRED、无 240×240、无 TLS）；官方 README 推荐的 `node encode_gif.mjs smile.gif smile.efc 4 z pred` 正是会崩的编码。
- **证据**：二分验证——官方 `giphy_z.efc`（zlib，无 PRED，flags `0x04`）播得好好的；raw 无压缩 welcome 也好好的；zlib+legacy welcome（无 PRED）播完 72 帧+循环都好好的；一换成任何 zlib+PRED 片段（flags `0x0c`）就在第 0 帧解码时清零互斥量。关 SIMD、堆涂毒都排除了其它可能。
- **修复**：16 段全部重编码为 zlib+legacy（flags `0x04`），另加 `lv_efc_set_frame_delay(efc_image_, 125)` 修正 8fps 播放速度（EFC 默认 33ms/帧=30fps，动画是 8fps，原先快 3.75 倍）。修复后硬件验证通过（2026-11-26）：激活完成、MQTT 已连、idle 稳定 100+ 秒。
- **以后更新**：重新编码任何片段都用 `node encode_gif.mjs <in.gif> <out.efc> 4 z legacy`，不要用 `pred`。编码器在 `esp_efc_codec/pc/js/dist/encode_gif.mjs`（Node 18+，不用 npm install）。提交前验一下 flags：文件第 6 个字节（偏移 5）应为 `0x04`（zlib+legacy），`0x0c` 就是带 PRED 的，千万别提交。如果以后升级 esp_efc_codec，先拿一个带 PRED 的片段在真机上验证（播完 + MQTT 发布不崩）再全量换，不要只看版本号。

## 构建验证

同步后、或改动代码后，用同一条命令构建并核对：

```bash
source ~/esp/esp-idf/export.sh   # ESP-IDF release/v6.1
python3 scripts/build.py movecall/moji-esp32s3 --language zh-CN --wake-word nihaoxiaozhi
```

核对项：

- 构建退出码 0。
- `build/generated_assets.bin` ≤ 8,388,608 字节；用其中 `index.json`
  确认 `emoji_collection` 恰好 16 个（`neutral` / `thinking` /
  `listening` / `speaking` / `error` / `happy` / `surprised` /
  `eating` / `daydreaming` / `playing` / `begging` / `reading` /
  `traveling` / `browsing` / `welcome` / `done`），
  且 EFC 魔数（EFC1）计数为 16；16 段 flags 均为 `0x04`（zlib+legacy，无 PRED）——文件第 6 个字节（偏移 5）是 `0x04` 才对，`0x0c` 是带 PRED 的，见“已知坑”。
- 语言 `zh-CN`、唤醒词 `wn9_nihaoxiaozhi_tts`、`CONFIG_PET_TERRIER_EMOTIONS=1`、
  `CONFIG_ESP_EFC_LVGL_WIDGET=1`。
- 烧录：程序刷 `0x20000` 与 `0x410000` 双槽位；资源刷 `0x800000`；
  NVS（`0x9000`）不动。编过不等于硬件验证，上板确认待机轮播、
  对话切换与只剩小狗三点。
