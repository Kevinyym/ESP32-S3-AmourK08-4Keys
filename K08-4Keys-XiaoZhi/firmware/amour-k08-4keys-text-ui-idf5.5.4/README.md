# K08 文字优先界面固件

XiaoZhi 2.4.2，板型 `amour-k08-4keys`，ESP32-S3-WROOM-1-N16R8。使用 ESP-IDF 5.5.4 构建。

## 界面变化

- 顶部状态栏 24 px，下方 216 px 用于对话。
- 16 px 内置字体，多行换行、紧凑气泡；用户消息靠右，助手消息靠左，气泡内文字左对齐。
- 最多保留 20 条消息，新增消息自动滚动到最新内容。当前没有按键翻页功能，较长回复滚动后可能无法回看前文。
- 有聊天内容时隐藏表情；清空聊天后显示约 30 px 的静态机器人。
- 沿用原有音频、屏幕方向与裁切、三颗功能键及 IP5306 硬件电源键配置。

## 验证

Canonical 命令 `python3 scripts/build.py amour-k08-4keys --name amour-k08-4keys` 返回 0；应用镜像 2,823,104 字节，应用分区剩余 32%。67 项构建脚本测试、修改文件格式检查通过。完整日志、文件摘要见本目录。

本版尚未烧录实机，ESP-IDF 6.0.2 构建也未验证。上板请检查短句与长回复换行、最新消息滚动、表情不遮字、状态栏，以及音量键、模式键和语音功能。构建日志保留了基线在 IDF 5.5.4 下的未知 `MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` 配置警告；构建成功不代表完成联网回归测试。

## 烧录

原版本保存在相邻的 `amour-k08-4keys-idf5.5.4` 目录。

推荐已有配置的设备使用以下分片命令（在本目录执行，将 `PORT` 替换为设备串口）。该布局不写入 NVS 分区，且初始化 OTA 选择以启动本次写入的应用：

```bash
python -m esptool --chip esp32s3 -p PORT -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0xd000 ota_data_initial.bin 0x20000 xiaozhi.bin 0x800000 generated_assets.bin
```

也可将一体镜像 `amour-k08-4keys-v2.4.2-text-ui-idf5.5.4-full.bin` 从 **0x0** 烧录。一体镜像包含分区间填充，会覆盖 NVS 中的配网等设置，烧录后需重新配网。

不要仅凭固定地址单独烧录应用来更新正在运行的 OTA 设备；其当前启动槽位可能不同。

```bash
shasum -a 256 -c SHA256SUMS
```
