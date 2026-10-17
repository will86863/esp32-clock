package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
)

// Game 一个被监控的程序。
type Game struct {
	ID      string `json:"id"`      // 稳定 id（用于统计关联），程序自动生成
	Exe     string `json:"exe"`     // 可执行文件完整路径
	Process string `json:"process"` // 进程名，如 eldenring.exe（由 Exe 推导，可手工修正）
	Display string `json:"display"` // 自定义英文显示名，≤16 ASCII
	AppID   string `json:"appid"`   // Steam appid（非 Steam 程序为空）
	Source  string `json:"source"`  // 来源：exe/lnk/url/steam
}

// Config 程序配置。
type Config struct {
	ESP32IP     string `json:"esp32_ip"`
	ESP32Port   int    `json:"esp32_port"`
	IntervalSec int    `json:"interval_sec"`
	Autostart   bool   `json:"autostart"`
	StartHidden bool   `json:"start_hidden"` // 开机启动时最小化
	Games       []Game `json:"games"`
}

func defaultConfig() Config {
	return Config{
		ESP32IP:     "192.168.5.86", // 固件 MEMORY.md 记录的固定 IP
		ESP32Port:   5005,           // 固件 GAME_UDP_PORT
		IntervalSec: 1,
		Autostart:   false,
		StartHidden: true,
		Games:       []Game{},
	}
}

// Target 返回 ip:port 形式的 UDP 目标。
func (c Config) Target() string {
	ip := strings.TrimSpace(c.ESP32IP)
	if ip == "" {
		return ""
	}
	return fmt.Sprintf("%s:%d", ip, c.ESP32Port)
}

var (
	cfgMu   sync.RWMutex
	cfgPath string
)

// appDataDir 返回配置与统计数据的存放目录：
// %APPDATA%\GameTime\（不放在 exe 同目录，避免 Program Files 无写权限）
func appDataDir() string {
	if d := os.Getenv("APPDATA"); d != "" {
		p := filepath.Join(d, "GameTime")
		_ = os.MkdirAll(p, 0o755)
		return p
	}
	return "."
}

// initPaths 在启动时调用一次。
func initPaths() {
	cfgPath = filepath.Join(appDataDir(), "config.json")
}

// LoadConfig 读取配置；文件不存在时写入默认配置。
func LoadConfig() (Config, error) {
	cfgMu.RLock()
	p := cfgPath
	cfgMu.RUnlock()

	cfg := defaultConfig()
	data, err := os.ReadFile(p)
	if err != nil {
		if os.IsNotExist(err) {
			_ = SaveConfig(cfg)
			return cfg, nil
		}
		return cfg, err
	}
	if err := json.Unmarshal(data, &cfg); err != nil {
		return defaultConfig(), fmt.Errorf("config.json 解析失败: %w", err)
	}
	normalizeConfig(&cfg)
	return cfg, nil
}

// SaveConfig 原子写入配置（先写 .tmp 再改名）。
func SaveConfig(cfg Config) error {
	cfgMu.RLock()
	p := cfgPath
	cfgMu.RUnlock()

	if p == "" {
		return fmt.Errorf("配置路径未初始化")
	}
	normalizeConfig(&cfg)
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	tmp := p + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, p)
}

func normalizeConfig(c *Config) {
	if c.ESP32Port <= 0 || c.ESP32Port > 65535 {
		c.ESP32Port = 5005
	}
	if c.IntervalSec <= 0 {
		c.IntervalSec = 1
	}
	if c.IntervalSec > 60 {
		c.IntervalSec = 60
	}
	if c.ESP32IP == "" {
		c.ESP32IP = "192.168.5.86"
	}
	if c.Games == nil {
		c.Games = []Game{}
	}
	for i := range c.Games {
		g := &c.Games[i]
		if g.Process == "" && g.Exe != "" {
			g.Process = filepath.Base(g.Exe)
		}
		if g.Display == "" {
			g.Display = sanitizeDisplay(strings.TrimSuffix(g.Process, ".exe"))
		}
	}
}

// ============================================================
// 显示名校验 —— 固件约束的唯一入口
// ============================================================

// DisplayNameMaxLen 是固件 OLED 能完整居中显示的最大字符数。
// 依据：SmallDesktopDisplay.ino 的 oledDrawGame() 用 (128-len*8)/2 居中，
// 字体 8px 宽 → 128/8 = 16。
const DisplayNameMaxLen = 16

// ValidateDisplay 校验显示名是否符合固件约束。
// 返回清洗后的值与错误；err 非空表示不可保存。
func ValidateDisplay(s string) (string, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return "", fmt.Errorf("显示名不能为空")
	}
	for _, r := range s {
		if r > 127 {
			return s, fmt.Errorf("只能使用英文/数字/符号（固件字库为 ASCII），发现非 ASCII 字符 %q", r)
		}
		if r < 32 {
			return s, fmt.Errorf("包含不可见控制字符")
		}
	}
	if len(s) > DisplayNameMaxLen {
		return s, fmt.Errorf("最多 %d 个字符，当前 %d 个（OLED 一行只有 %d 个字符宽）",
			DisplayNameMaxLen, len(s), DisplayNameMaxLen)
	}
	return s, nil
}

// sanitizeDisplay 把任意名字尽力压成合法显示名：去非 ASCII、截断、兜底。
func sanitizeDisplay(s string) string {
	s = strings.TrimSpace(s)
	var b strings.Builder
	for _, r := range s {
		if r > 127 || r < 32 {
			continue
		}
		b.WriteRune(r)
	}
	out := strings.TrimSpace(b.String())
	if out == "" {
		out = "GAME"
	}
	if len(out) > DisplayNameMaxLen {
		out = strings.TrimSpace(out[:DisplayNameMaxLen])
	}
	if out == "" {
		out = "GAME"
	}
	return out
}
