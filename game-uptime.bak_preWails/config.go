package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// Game 单个被监控的进程
type Game struct {
	Process string `json:"process"` // 进程名，例如 eldenring.exe
	Display string `json:"display"` // 自定义显示名，为空则用进程名
}

// Config 程序配置
type Config struct {
	ESP32IP     string `json:"esp32_ip"`     // ESP32 固定 IP
	ESP32Port   int    `json:"esp32_port"`   // ESP32 UDP 监听端口
	IntervalSec int    `json:"interval_sec"` // 发送间隔（秒）
	WebPort     int    `json:"web_port"`     // 本机管理页面端口
	Autostart   bool   `json:"autostart"`    // 是否开机自启
	Games       []Game `json:"games"`        // 监控进程列表
}

func defaultConfig() Config {
	return Config{
		ESP32IP:     "192.168.1.100",
		ESP32Port:   5005,
		IntervalSec: 1,
		WebPort:     8899,
		Autostart:   false,
		Games:       []Game{},
	}
}

func configPath() string {
	exe, err := os.Executable()
	if err != nil {
		return "config.json"
	}
	return filepath.Join(filepath.Dir(exe), "config.json")
}

func loadConfig() (Config, error) {
	cfg := defaultConfig()
	data, err := os.ReadFile(configPath())
	if err != nil {
		if os.IsNotExist(err) {
			// 首次运行，写入默认配置
			_ = saveConfig(cfg)
			return cfg, nil
		}
		return cfg, err
	}
	if err := json.Unmarshal(data, &cfg); err != nil {
		return cfg, fmt.Errorf("解析 config.json 失败: %w", err)
	}
	// 校验并修正
	if cfg.ESP32Port <= 0 {
		cfg.ESP32Port = 5005
	}
	if cfg.IntervalSec <= 0 {
		cfg.IntervalSec = 1
	}
	if cfg.WebPort <= 0 {
		cfg.WebPort = 8899
	}
	return cfg, nil
}

func saveConfig(cfg Config) error {
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	path := configPath()
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, path) // 原子替换，避免读端读到半写文件
}

// normName 进程名归一化（小写、去 .exe），用于匹配
func normName(s string) string {
	s = strings.ToLower(strings.TrimSpace(s))
	return strings.TrimSuffix(s, ".exe")
}

// displayName 返回进程的显示名：自定义名优先，否则用进程名（去 .exe，保留原始大小写）
func displayName(g Game, procName string) string {
	if strings.TrimSpace(g.Display) != "" {
		return g.Display
	}
	return strings.TrimSuffix(procName, ".exe")
}
