# game-uptime · 游戏时长后台程序

监控电脑上的游戏进程，把「最后启动的那个游戏」的运行时长通过局域网 UDP 每秒发给 ESP32，由 0.96 寸 OLED 显示。Go 编译的单文件后台程序，无黑窗、无运行时依赖。

## 快速开始

1. 把 `game-uptime.exe` 放到任意固定目录（例如 `D:\game-uptime\`），双击运行。
   - 程序无窗口，直接后台运行。
   - 首次运行会在同目录自动生成 `config.json`。
2. 浏览器打开管理页面：**http://127.0.0.1:8899**
   - 添加要监控的进程（如 `eldenring.exe`）、自定义显示名（留空则显示进程名）
   - 填 ESP32 的固定 IP 和端口
   - 勾选「开机自动启动」、保存
3. 完事。OLED 上就会显示游戏名和 `HH:MM:SS` 时长。

## config.json 字段

| 字段 | 说明 |
|---|---|
| `esp32_ip` | ESP32 的固定 IP |
| `esp32_port` | ESP32 UDP 监听端口（固件默认 5005） |
| `interval_sec` | 发送间隔（秒），默认 1 |
| `web_port` | 管理页面端口，默认 8899（改后需重启程序） |
| `autostart` | 开机自启（写入 HKCU Run 注册表键，Web 页开关同步） |
| `games[].process` | 要监控的进程名，大小写不限，`.exe` 可带可不带 |
| `games[].display` | 自定义显示名，**只支持英文**（OLED 字库为 ASCII）；留空显示进程名 |

- 可添加多个进程，OLED 只显示**最后启动**的那一个。
- 游戏退出 / 电脑断开 / ESP32 超过 5 秒收不到数据 → OLED 显示 `NO GAME 00:00:00`。
- 修改配置保存后立即生效，无需重启程序（改 `web_port` 除外）。

## 每秒发送的数据格式

```json
{"running":true,"name":"ELDEN RING","up":12345}
```

## 开机自启原理

写入当前用户注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`（键名 `GameUptime`），登录后自动后台启动，无需管理员权限。

## 从源码编译（可选）

```
go build -trimpath -ldflags "-s -w -H=windowsgui" -o game-uptime.exe .
```

依赖仅 `golang.org/x/sys`（进程创建时间、注册表）。调试参数：`-list` 打印命中进程，`-once` 执行一次监控。
