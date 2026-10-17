package main

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"sync"
	"time"
)

// Status 当前监控状态（供 Web 页面展示）
type Status struct {
	Running bool   `json:"running"`
	Name    string `json:"name"`
	Up      int64  `json:"up"` // 秒
}

var (
	statusMu sync.RWMutex
	status   = Status{Running: false}
)

func updateStatus(s Status) {
	statusMu.Lock()
	status = s
	statusMu.Unlock()
}

func getStatus() Status {
	statusMu.RLock()
	defer statusMu.RUnlock()
	return status
}

// findLatestRunningGame 返回当前最后启动的匹配进程的显示名与已运行秒数
func findLatestRunningGame(cfg *Config) (name string, up int64, running bool) {
	procs, err := listProcesses()
	if err != nil {
		return "", 0, false
	}
	now := time.Now()
	var latest *ProcessInfo
	var latestGame *Game
	for i := range cfg.Games {
		g := &cfg.Games[i]
		want := normName(g.Process)
		if want == "" {
			continue
		}
		for j := range procs {
			p := &procs[j]
			if normName(p.Name) == want {
				if latest == nil || p.Created.After(latest.Created) {
					latest = p
					latestGame = g
				}
			}
		}
	}
	if latest == nil {
		return "", 0, false
	}
	up = int64(now.Sub(latest.Created).Seconds())
	if up < 0 {
		up = 0
	}
	return displayName(*latestGame, latest.Name), up, true
}

// payload 发送给 ESP32 的 JSON 结构
type payload struct {
	Running bool   `json:"running"`
	Name    string `json:"name"`
	Up      int64  `json:"up"`
}

func sendUDP(cfg *Config, name string, up int64, running bool) error {
	addr, err := net.ResolveUDPAddr("udp4", fmt.Sprintf("%s:%d", cfg.ESP32IP, cfg.ESP32Port))
	if err != nil {
		return err
	}
	conn, err := net.DialUDP("udp4", nil, addr)
	if err != nil {
		return err
	}
	defer conn.Close()

	data, _ := json.Marshal(payload{Running: running, Name: name, Up: up})
	_, err = conn.Write(data)
	return err
}

func main() {
	// 调试参数：-list 打印匹配进程后退出；-once 执行一次并打印结果
	args := os.Args[1:]
	listOnly := false
	once := false
	for _, a := range args {
		if a == "-list" {
			listOnly = true
		}
		if a == "-once" {
			once = true
		}
	}

	cfg, err := loadConfig()
	if err != nil {
		fmt.Println("加载配置失败:", err)
		cfg = defaultConfig()
	}

	if listOnly {
		procs, err := listProcesses()
		if err != nil {
			fmt.Println("枚举进程失败:", err)
			return
		}
		fmt.Printf("共枚举到 %d 个进程\n", len(procs))
		for _, g := range cfg.Games {
			for _, p := range procs {
				if normName(p.Name) == normName(g.Process) {
					fmt.Printf("命中: %s (PID %d, 启动于 %s)\n", p.Name, p.PID, p.Created.Format("15:04:05"))
				}
			}
		}
		return
	}

	if once {
		name, up, running := findLatestRunningGame(&cfg)
		fmt.Printf("running=%v name=%q up=%d\n", running, name, up)
		return
	}

	// 开机自启状态同步（幂等）
	if cfg.Autostart {
		_ = setAutostart(true)
	}

	// 启动管理 Web 服务
	go startWebServer()

	// 主监控循环
	for {
		cur, err := loadConfig()
		if err != nil {
			cur = cfg
		}
		name, up, running := findLatestRunningGame(&cur)
		updateStatus(Status{Running: running, Name: name, Up: up})
		_ = sendUDP(&cur, name, up, running)
		time.Sleep(time.Duration(cur.IntervalSec) * time.Second)
	}
}
