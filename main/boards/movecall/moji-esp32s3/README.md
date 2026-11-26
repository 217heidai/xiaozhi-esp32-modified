# 编译配置命令

**配置编译目标为 ESP32S3：**

```bash
idf.py set-target esp32s3
```

**打开 menuconfig：**

```bash
idf.py menuconfig
```

**选择板子：**

```
Xiaozhi Assistant -> Board Type -> Movecall Moji 小智AI衍生版
```


**编译：**

```bash
idf.py build
```

# 西高地小狗表情（HachimoDock 移植）

本板默认把 HachimoDock 的西高地小狗视频以 EFC 表情包进 assets 分区
（`main/assets/emoji/terrier/`，16 段，240×240，8fps，共约 5.95MB），
代替默认的 Noto 彩色 emoji。状态映射（`CONFIG_PET_TERRIER_EMOTIONS`，
默认开启）为：待机 `neutral`、聆听 `listening`、说话 `speaking`；
服务端下发的 `thinking` / `happy` / `surprised` / `error`
也会显示对应的小狗片段；没有对应片段的服务端表情回落为内置 Noto
emoji 字体字形。正常使用时屏幕只显示小狗，不显示时间、状态栏与聊天
记录。待机时小狗会轮换：开机先放一次 `welcome`，之后按每段时长轮换（时长向下取整，begging、reading 切掉尾巴；`neutral` → `begging` →
`playing` → `eating` → `daydreaming` → `reading` → `browsing` →
`traveling`，循环）。一轮对话说完回到待机时放一次 `done`（打勾），然后继续轮换。关闭该选项可恢复默认 neutral 表情（仍需把
`DEFAULT_EMOJI_COLLECTION` 改回 `noto-color-emoji_64` 才能恢复 Noto
彩色 emoji 图片）。
