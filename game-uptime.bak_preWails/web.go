package main

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strings"
)

//go:embed web/index.html
var indexHTML string

func startWebServer() {
	cfg, err := loadConfig()
	if err != nil {
		cfg = defaultConfig()
	}
	port := cfg.WebPort

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		fmt.Fprint(w, indexHTML)
	})

	http.HandleFunc("/api/config", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		switch r.Method {
		case http.MethodGet:
			cur, err := loadConfig()
			if err != nil {
				cur = defaultConfig()
			}
			json.NewEncoder(w).Encode(cur)
		case http.MethodPost:
			var c Config
			if err := json.NewDecoder(r.Body).Decode(&c); err != nil {
				http.Error(w, `{"error":"invalid json"}`, http.StatusBadRequest)
				return
			}
			// 归一化进程名（去 .exe 空格）
			for i := range c.Games {
				c.Games[i].Process = strings.TrimSpace(c.Games[i].Process)
			}
			if err := saveConfig(c); err != nil {
				http.Error(w, `{"error":"save failed"}`, http.StatusInternalServerError)
				return
			}
			// 同步自启状态
			_ = setAutostart(c.Autostart)
			fmt.Fprint(w, `{"ok":true}`)
		default:
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		}
	})

	http.HandleFunc("/api/autostart", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		var req struct {
			Enable bool `json:"enable"`
		}
		json.NewDecoder(r.Body).Decode(&req)
		if err := setAutostart(req.Enable); err != nil {
			http.Error(w, `{"error":"failed"}`, http.StatusInternalServerError)
			return
		}
		cur, _ := loadConfig()
		cur.Autostart = req.Enable
		_ = saveConfig(cur)
		fmt.Fprint(w, `{"ok":true}`)
	})

	http.HandleFunc("/api/status", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		json.NewEncoder(w).Encode(getStatus())
	})

	http.HandleFunc("/api/quit", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		fmt.Fprint(w, `{"ok":true}`)
		go func() { os.Exit(0) }()
	})

	addr := fmt.Sprintf("127.0.0.1:%d", port)
	go func() {
		if err := http.ListenAndServe(addr, nil); err != nil {
			fmt.Println("Web 服务启动失败:", err)
		}
	}()
}
