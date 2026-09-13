# 群晖 DS923+ / DSM 7.3.2 部署

本服务运行在 Container Manager 的 Linux amd64 容器中，由 `bridge.py` 主动连接小智后台，启动 `server.py` 并查询 Alpaca。无需映射端口、公网 IP、反向代理或 Web Station。NAS 必须保持联网、开机，允许 DNS 及出站 HTTPS/WSS；构建时还需要访问 Docker Hub 和 PyPI。

## 1. 准备目录和文件

在套件中心安装 Container Manager。在 File Station 建立共享文件夹内的 `docker/us-stock-mcp`，本次实际路径为 `/volume2/docker/us-stock-mcp`；其他设备按实际存储卷调整。

把以下文件从 Mac 的本服务目录上传到该目录，保持结构：

```text
Dockerfile
.dockerignore
docker-compose.yml
requirements.lock.txt
bridge.py
server.py
stock_service.py
smoke_test.py
tests/（其中的 .py 文件）
```

再单独上传当前已验证的 `.env`。确认隐藏文件实际存在，没有变成 `.env.txt`，且 `STOCK_DATA_MODE=live`。不要复制 Mac 的 `.venv`、`__pycache__` 或整个固件工程。分享部署目录时务必排除 `.env`。

`.dockerignore` 只允许必要源码进入构建上下文，密钥不会写入镜像。Compose 从 `.env` 注入环境变量；有 NAS 容器管理权限的人员仍可查看这些变量，应限制项目目录和管理权限。

## 2. 创建项目并构建

打开 Container Manager → 项目 → 新增/创建：

- 项目名称：`us-stock-mcp`。
- 路径：刚才的项目文件夹。
- 来源：选择/上传提供的 `docker-compose.yml`，或将其内容复制到编辑器。
- 不启用 Web Station 门户。
- 先构建，不启动；若向导只能构建并启动，请先停止 Mac 桥接。

构建会下载 Python 镜像，并安装锁定的依赖。本次已在 DS923+ 成功构建并运行；后续重新构建仍以实际日志为准。若失败，保留报错，不要自行取消依赖锁定或关闭 TLS 校验。

## 3. 切换到 NAS

在 Mac 运行桥接的终端按 Ctrl+C，确认停止，再启动 NAS 项目。同一个接入点不要同时运行两个实例。

打开容器日志，预期看到：

```text
小智 MCP WebSocket 已连接，正在启动本地行情服务。
本地 MCP 正在处理请求：ListToolsRequest
```

结束小智的旧对话，重新唤起并说“分析纳斯达克100最近30个交易日的走势”。日志应出现 `收到行情工具调用：get_nasdaq100_overview`。确认回答来自真实数据，并说明 QQQ、IEX 来源和成交时间。仅显示“运行中”或收到心跳不代表工具调用成功。

## 4. 运行与更新

- `unless-stopped` 在进程异常退出或 Docker 服务恢复时自动启动；手动停止的容器保持停止。
- 网络中断由桥接按 5–60 秒退避重连，配置错误仍需人工修复。
- 日志轮转配置为每文件 5 MB，保留 3 个文件。
- 修改 `.env` 后要重新创建容器，单纯重启不会更新 Compose 注入的环境变量。可通过项目的构建/重新部署操作应用配置。
- 修改 Python 源码或依赖后，重新构建并部署镜像；文件未挂载进容器，普通重启不会加载 NAS 上的新源码。
- 项目更新或重连后，重新开始小智对话，避免旧会话工具信息引发 `unknown tool`。
- 回退到 Mac 时先停止 NAS 项目，再启动原 Mac 桥接。

若使用 SSH，可在项目目录执行以下命令重新构建并创建容器（需要相应管理权限；不要运行会展开显示密钥的配置输出命令）：

```sh
sudo docker compose up -d --build --force-recreate
sudo docker compose logs --tail=80 -f
```

本地模拟协议检查可在容器终端运行 `python smoke_test.py`，它明确使用模拟数据，不连接小智后台，也不证明真实行情可用。

## 本次部署排查经验

- 必须使用 `services/us-stock-mcp/` 内的 Dockerfile 和 Compose 文件。正确首行为 `FROM python:3.12-slim-bookworm`；若日志出现 `espressif/idf`，说明误用了固件编译环境的 Dockerfile，应停止错误构建并核对文件及项目路径。
- `Pulling fs layer`、`Removing intermediate container` 是正常构建过程；日志里的 `[1A`、`[2K` 等是终端控制符显示问题。进度看似不变时先滚动到底部查看最新输出。
- 构建期间项目操作可能全部禁用，关闭日志窗口不会取消构建。若必须停止整个 Container Manager 套件，应先评估其对其他容器服务的影响。
- 本容器日志默认使用 UTC，比北京时间少 8 小时。工具返回的成交时间包含时区；无需为日志显示差异修改行情时间计算。
- 只有心跳或工具调用日志不能证明返回成功；还需核对工具结果和小智实际播报。本次已由用户完成这一步确认。

## 验证状态

2026-09-13：Mac 端已有 22 项自动化测试通过。用户在 DS923+ / DSM 7.3.2 上完成镜像构建、容器运行及 K08 真实行情语音调用，确认功能实现。NAS 日志显示心跳、`get_nasdaq100_overview` 和 `CallToolRequest`。长期稳定性及 NAS 重启后的恢复尚未单独验证。

官方操作参考：[Container Manager 项目](https://kb.synology.com/en-us/DSM/help/ContainerManager/docker_project)。
