# NAS 音乐 HTTP 服务

这是供 K08 设备直接访问的局域网服务。它只读扫描 FLAC 文件，按相对路径生成稳定 ID，并在后台串行预转码。搜索只返回转码完成的曲目，因此设备请求音频时不会等待 FFmpeg。CUE 暂不分轨。

## 部署

已确认环境为 DS923+ / DSM 7.3.2，NAS 地址 `10.0.0.228`，音乐目录 `/volume1/music`：

```sh
cd services/nas-music
docker compose up -d --build
docker compose logs -f nas-music
```

在 DSM 7.3.2 的 Container Manager 中也可以直接部署：在 File Station 建立 `/volume2/docker/nas-music`，上传本目录的 `Dockerfile`、`docker-compose.yml` 和 `nas_music.py`（`test_nas_music.py` 可选）。然后打开 Container Manager → 项目 → 创建，项目路径选择该目录，来源选择上传的 `docker-compose.yml`，项目名使用 `nas-music`，构建并启动即可。若界面只提供 YAML 编辑器，就粘贴同目录 `docker-compose.yml` 的内容。不要把 `us-stock-mcp` 项目中的 YAML 或 Dockerfile 混用。

更新 `nas_music.py` 时，先停止并删除旧的 `nas-music` **容器**，再构建并启动项目。只构建镜像不会替换正在运行的容器。音乐文件和 `nas-music-cache` named volume 不会因删除容器而删除。更新后用 `curl -i http://10.0.0.228:8090/health` 检查响应头应包含 `Connection: keep-alive`。

启动后在 NAS 或同一局域网电脑上检查：

```sh
curl http://10.0.0.228:8090/health
curl 'http://10.0.0.228:8090/search?q=%E5%AD%99%E7%87%95%E5%A7%BF&limit=5'
```

健康检查中的 `indexed` 应接近音乐目录中的 FLAC 数量；首次启动时 `ready` 会从 0 逐步增加。只有 `ready` 曲目才会返回搜索结果。Container Manager 日志出现 `indexed ... tracks` 后即可等待转码；这一步不需要停止美股 MCP 容器。

Compose 将源目录只读挂载为 `/music:ro`，将转码结果保存到 Docker named volume，避免 DSM 共享目录 UID 权限问题。端口只绑定 NAS 的局域网地址 `10.0.0.228:8090`；不要配置公网端口转发。服务不需要密钥，也不包含 MCP 桥接进程。

首次启动后 FFmpeg 会逐首转换，104 个 FLAC 可能需要一段时间。转换期间健康检查可用，只有 ready 曲目会出现在搜索中。重启会复用缓存。源文件相对路径改变后会得到新 ID，旧缓存不会被提供；可在维护窗口删除 named volume 来回收旧文件。

转码格式已经按固件 `main/audio/demuxer/ogg_demuxer.cc` 固定为 Ogg Opus、单声道、24 kHz、48 kbps、60 ms 帧。转码会移除源 FLAC 的标签及封面图，避免很大的 `OpusTags` 数据包超过 K08 的 Ogg 解复用缓冲区。服务版本升级后会自动重新转码缓存中的曲目；原始 FLAC 不会被修改。

## API

- `GET /health`：返回 `status`、`indexed`、`ready`、`error`。
- `GET /search?q=中文&limit=5&offset=0`：返回 `{"tracks":[{"id":"...","title":"..."}],"total":N}`；limit 为 1–5，offset 默认为 0，`total` 是全部已就绪的匹配数，`tracks` 从 offset 起最多返回 limit 首。K08 用 offset 实现上一首和下一首，超出首尾时循环到另一端。查询会忽略标题中的连字符、空格等分隔符，并检索文件相对目录；因此 `孙燕姿 我要的幸福` 能匹配 `孙燕姿 - 我要的幸福`，`周杰伦/七里香/晴天.flac` 也能用“周杰伦”或“周杰伦 晴天”找到。
- `GET /tracks/<64位小写十六进制ID>.ogg`：返回已缓存音频。

本地运行测试：

```sh
python3 -m unittest -v test_nas_music.py
```

2026-09-19 已在 DS923+ 的 Container Manager 部署验证：`/health` 返回
`{"status":"ok","indexed":344,"ready":344,"error":0}`。K08 实机已通过官方后台调用
`self.music.play`，搜索 NAS 并成功播放歌曲。
