# Amour K08 4Keys

本板型面向 Armour K08 四键音箱改造板，主控为 ESP32-S3-WROOM-1-N16R8。它使用 Wi-Fi、独立的扬声器/麦克风 I2S 控制器、1.3 英寸 240×240 方形 ST7789 屏和 4 颗串联 WS2812。

板型和固件变体均使用独立 OTA 身份 `amour-k08-4keys`，不可选择其他 K08 板型替代。

## 已接入硬件

| 功能 | GPIO / 参数 | 说明 |
| --- | --- | --- |
| ICS43434 麦克风 | BCLK 39、WS 41、DIN 40 | LR 接地，左声道，16 kHz 输入 |
| MAX98357 扬声器 | BCLK 6、WS 7、DOUT 5 | 24 kHz 输出 |
| 功放使能 | GPIO 4 | 高电平有效；随音频输出启停 |
| ST7789 | SCLK 8、MOSI 18、DC 16、RST 17 | SPI2 mode 3，CS 未连接 |
| 背光 | GPIO 15 | 高电平有效 PWM |
| 模式键 | GPIO 14 | 短按切换对话，长按进入配网 |
| 音量加/减 | GPIO 9 / GPIO 21 | 短按步进 10；电台播放时长按切台，NAS 音乐播放时长按切歌，其他状态下长按最大音量/静音 |
| RGB 状态灯 | GPIO 48，4 颗 | WS2812，接入固件 `Led` 状态接口 |
| 红色 LED | GPIO 47 | 启动时保持关闭，留作后续独立指示 |

物理电源键直接连接 IP5306，不连接 ESP32 GPIO，固件不会注册该按键。I2C（SDA 1、SCL 2）、TF 卡（SCK 12、MOSI 11、MISO 13、CS 10）以及 AHT20/MPU6050 等传感器仅在 `config.h` 中保留引脚定义，本版固件不初始化这些扩展。

## 显示参数

屏幕逻辑尺寸为 240×240，颜色反转开启，颜色顺序为 BGR。原有 LovyanGFX 配置为 rotation 3、`offset_y=-80`；转换到 `esp_lcd` 后使用 `swap_xy=true`、`mirror_x=false`、`mirror_y=true`、`offset_x=80`、`offset_y=0`。负偏移不能直接照搬到 `esp_lcd`。这些参数来自现有 K08 程序的驱动变换推导；用户已确认方向和裁切正常，红蓝色序未单独反馈。

默认使用适合 240×240 屏幕的微信聊天样式。顶栏固定为 24 px，聊天区占用剩余 216 px；内置文本字体为 16 px，消息左对齐并自动换行，最多保留 20 条消息，加入新内容后滚动到最新消息。存在聊天内容时表情始终隐藏；聊天清空后显示资源包中的 128×128 `neutral` 彩色表情，资源缺失时回退为静态机器人图标。

网络电台和 NAS 音乐均会覆盖对话区，使用独立播放页，顶栏仍可见。电台页显示直播状态、电台名、分类、预置序号与收藏状态；NAS 页显示缓冲或播放状态、歌曲名、`DS923+ · 本地曲库` 和 Ogg Opus 局域网播放信息。标题均静态裁切而不滚动，避免影响阅读。停止、播放失败或切换另一种本地媒体时自动恢复对话界面。

NAS 音乐通过 `10.0.0.228:8090` 服务按点歌关键词建立循环队列：长按 `+` 下一首、长按 `-` 上一首。长按判断以应用层实际媒体状态为准，不依赖界面刷新时序；切歌会先结束当前 Ogg 播放会话，再查询并启动目标曲目。此功能依赖最新 `services/nas-music/nas_music.py` 的 `/search?offset=` 接口；服务仍为旧版本时应先更新并重新创建 Docker 容器。

## 编译

项目基线和首选环境为 ESP-IDF 6.0.2：

```bash
source /path/to/esp-idf/export.sh
python3 scripts/build.py amour-k08-4keys --name amour-k08-4keys
```

默认的 `sdkconfig.defaults` 与 `sdkconfig.defaults.esp32s3` 已配置 16 MB flash 和 Octal PSRAM 模式，适用于 N16R8 模组，启动后仍应从日志确认实际识别到 8 MB PSRAM。

若只能使用 ESP-IDF 5.5.4，可将其作为本地兼容性检查；该结果不能替代 6.0.2 构建验证。

## 验证状态

2026-09-20 使用 ESP-IDF 5.5.4 构建当前本地媒体固件成功，应用镜像为 3,364,544 字节（`0x3356C0`），最小应用分区剩余 19%。当前固件包、分片镜像和 SHA-256 见 [NAS 音乐固件包](../../../firmware/amour-k08-4keys-nas-music-idf5.5.4/README.md)。NAS 服务的 4 项单元与 HTTP 集成测试均已通过。

用户已完成当前版本的初步实机测试，网络电台换台不再退出播放页，NAS 播放页与本地媒体基础流程可用；后续继续进行多电台长时播放、反复切台、Wi-Fi 断线恢复、语音停止/唤醒打断及普通对话、美股 MCP 回归测试。尚未使用首选的 ESP-IDF 6.0.2 构建。四颗 RGB 灯及未接入的传感器仍未验证。

## 移植基线

本目录随 XiaoZhi 2.4.2 源码复制。源工作区的 `main/boards/bread-compact-wifi/compact_wifi_board.cc` 带有一项未提交的 SSD1306 `esp_lcd_panel_io_i2c_config_t` 零初始化兼容修改，该修改已随源码保留。复制时排除了 `.git`、`build`、`managed_components`、旧 `sdkconfig`、本地虚拟环境与固件产物；保留 `sdkconfig.defaults*`、资源、`dependencies.lock`、LICENSE 和构建所需隐藏文件。
