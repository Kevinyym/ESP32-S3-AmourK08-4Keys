# K08 NAS 音乐固件

XiaoZhi 2.4.2，板型 `amour-k08-4keys`，ESP32-S3-WROOM-1-N16R8，使用 ESP-IDF 5.5.4 构建。

## 功能

- 调用 `self.music.play` 在 NAS `10.0.0.228:8090` 搜索并播放本地音乐。
- 调用 `self.music.stop` 停止播放；`self.music.status` 返回播放状态和当前曲名。
- 调用 `self.music.search` 仅检索曲库，不播放；随后调用 `self.music.status` 可得到匹配总数及前五首歌。NAS 必须使用同项目 `services/nas-music/nas_music.py` 的最新版本，才能返回真实总数。
- 屏幕显示搜索、播放和错误状态。播放中短按模式键会立即停止音乐并回到待机；再次短按才进入对话。
- NAS 服务只向设备提供已预转码的 Ogg Opus 流。
- 点歌参数会自动去除“播放”“我想听”、中文书名号等自然语言修饰。例如
  `周杰伦的《以父之名》` 会按 `以父之名` 搜索；串口日志会显示原始关键词、规范化
  关键词、HTTP 状态和搜索失败原因。

## 验证

完整 ESP-IDF 5.5.4 构建成功。应用镜像为 0x2B4B70 字节，最小应用分区为 0x3F0000 字节，剩余 0x13B490（31%）。Ogg 流兼容性测试和项目构建脚本测试已通过。NAS 的 `/health` 已验证返回 `indexed: 344`、`ready: 344`。

2026-09-19 实机验证成功：官方后台调用 `self.music.play` 后，K08 能搜索 `10.0.0.228:8090`，在官方工具会话未主动结束时发送标准 `goodbye` 关闭语音通道，并播放 NAS 中的 Ogg Opus 歌曲。本包还固定了 HTTP 接收缓冲区的锁范围，避免音频流背压与断连回调引起看门狗复位。

2026-09-19 已实机验证模式键停止：音乐播放时首按会从 `notifying` 直接回到 `idle`，不会同时开启云端会话；第二次短按会等音频连接完全关闭后自动进入对话，避免显示长时间“连接中”。播放中的语音唤醒打断仍建议在日常使用前单独验证。

## 烧录

先在本目录校验文件：

```bash
shasum -a 256 -c SHA256SUMS
```

推荐按分区烧录（将 `PORT` 换成实际串口）。该方式保留 NVS 中已有的配网信息：

```bash
python -m esptool --chip esp32s3 -p PORT -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0xd000 ota_data_initial.bin 0x20000 xiaozhi.bin 0x800000 generated_assets.bin
```

`merged-binary.bin` 是从偏移 `0x0` 开始的一体镜像，可用于需要整片烧录的工具；它包含分区间填充，会覆盖设备的 NVS 配网数据，烧录后需要重新配网。
