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
| 音量加/减 | GPIO 9 / GPIO 21 | 短按步进 10，长按最大音量/静音 |
| RGB 状态灯 | GPIO 48，4 颗 | WS2812，接入固件 `Led` 状态接口 |
| 红色 LED | GPIO 47 | 启动时保持关闭，留作后续独立指示 |

物理电源键直接连接 IP5306，不连接 ESP32 GPIO，固件不会注册该按键。I2C（SDA 1、SCL 2）、TF 卡（SCK 12、MOSI 11、MISO 13、CS 10）以及 AHT20/MPU6050 等传感器仅在 `config.h` 中保留引脚定义，本版固件不初始化这些扩展。

## 显示参数

屏幕逻辑尺寸为 240×240，颜色反转开启，颜色顺序为 BGR。原有 LovyanGFX 配置为 rotation 3、`offset_y=-80`；转换到 `esp_lcd` 后使用 `swap_xy=true`、`mirror_x=false`、`mirror_y=true`、`offset_x=80`、`offset_y=0`。负偏移不能直接照搬到 `esp_lcd`。这些参数来自现有 K08 程序的驱动变换推导，仍需在实机上检查方向、裁切和红蓝色序。

## 编译

项目基线和首选环境为 ESP-IDF 6.0.2：

```bash
source /path/to/esp-idf/export.sh
python3 scripts/build.py amour-k08-4keys --name amour-k08-4keys
```

默认的 `sdkconfig.defaults` 与 `sdkconfig.defaults.esp32s3` 已配置 16 MB flash 和 Octal PSRAM 模式，适用于 N16R8 模组，启动后仍应从日志确认实际识别到 8 MB PSRAM。

若只能使用 ESP-IDF 5.5.4，可将其作为本地兼容性检查；该结果不能替代 6.0.2 构建验证。

## 验证状态

2026-09-12 使用 ESP-IDF 5.5.4 执行上述 canonical 构建命令，配置和编译均成功完成。构建确认选择了 `esp32s3`、`CONFIG_BOARD_TYPE_AMOUR_K08_4KEYS=y`、16 MB flash、Octal PSRAM、简体中文资源和 AFE 唤醒词。生成的应用镜像为 2,819,392 字节，最小应用分区剩余 32%。可直接烧录的合并镜像、分片、校验值和完整日志见 [`firmware/amour-k08-4keys-idf5.5.4`](../../../firmware/amour-k08-4keys-idf5.5.4/README.md)。

尚未使用首选的 ESP-IDF 6.0.2 构建，也未烧录实机。显示方向与色序、麦克风左右声道、音频功放时序、三键和四颗 RGB 灯仍需硬件验证。

## 移植基线

本目录随 XiaoZhi 2.4.2 源码复制。源工作区的 `main/boards/bread-compact-wifi/compact_wifi_board.cc` 带有一项未提交的 SSD1306 `esp_lcd_panel_io_i2c_config_t` 零初始化兼容修改，该修改已随源码保留。复制时排除了 `.git`、`build`、`managed_components`、旧 `sdkconfig`、本地虚拟环境与固件产物；保留 `sdkconfig.defaults*`、资源、`dependencies.lock`、LICENSE 和构建所需隐藏文件。
