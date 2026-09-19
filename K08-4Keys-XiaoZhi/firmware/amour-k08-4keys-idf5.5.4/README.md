# Amour K08 4Keys 固件包

本目录是 XiaoZhi 2.4.2 的 `amour-k08-4keys` 固件包，面向 ESP32-S3-WROOM-1-N16R8。2026-09-12 使用 ESP-IDF 5.5.4 和 canonical 命令完成全量构建：

```bash
python3 scripts/build.py amour-k08-4keys --name amour-k08-4keys
```

命令返回 0。构建配置为 ESP32-S3、16 MB flash、Octal PSRAM、简体中文资源和 AFE 唤醒词。`xiaozhi.bin` 为 2,819,392 字节；最小应用分区为 0x3f0000 字节，剩余 32%。

## 文件

| 文件 | 用途 |
| --- | --- |
| `amour-k08-4keys-v2.4.2-idf5.5.4-full.bin` | 包含 bootloader、分区表、OTA 数据、应用和资源的一体镜像，从 0x0 烧录 |
| `xiaozhi.bin` | 应用镜像，分片烧录地址 0x20000 |
| `bootloader.bin` | bootloader，分片烧录地址 0x0 |
| `partition-table.bin` | 分区表，分片烧录地址 0x8000 |
| `ota_data_initial.bin` | OTA 初始数据，分片烧录地址 0xd000 |
| `generated_assets.bin` | 中文字体与资源，分片烧录地址 0x800000 |
| `manifest.json` | 板型、工具链、构建结果、烧录布局、文件大小和校验值 |
| `SHA256SUMS` | 镜像和日志的 SHA-256 校验值 |
| `build-idf5.5.4.log` | clean 后 canonical 全量构建日志 |
| `host-tests.log` | 构建脚本 host 单元测试日志，67 项通过 |

烧录一体镜像时，将 `PORT` 换成实际串口：

```bash
python -m esptool --chip esp32s3 -p PORT -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 amour-k08-4keys-v2.4.2-idf5.5.4-full.bin
```

也可烧录各分片：

```bash
python -m esptool --chip esp32s3 -p PORT -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0xd000 ota_data_initial.bin 0x20000 xiaozhi.bin 0x800000 generated_assets.bin
```

烧录前可在本目录核验文件：

```bash
shasum -a 256 -c SHA256SUMS
```

该固件尚未在首选的 ESP-IDF 6.0.2 下构建，也未烧录实机。显示方向与色序、麦克风左右声道、功放启停、三键和四颗 RGB 灯需要在目标硬件上验证。
