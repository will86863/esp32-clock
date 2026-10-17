package main

import (
	"fmt"
	"os"
	"strings"

	"golang.org/x/sys/windows/registry"
)

// ============================================================
// 开机自启
//
// 写 HKCU\Software\Microsoft\Windows\CurrentVersion\Run，键名 GameTime。
// 命令行带 -minimized，实现「开机启动后默认最小化到托盘」。
// 路径必须加引号 —— 否则 Program Files 这类带空格的路径会启动失败。
// ============================================================

const (
	autostartKeyName = "GameTime"
	runRegPath       = `Software\Microsoft\Windows\CurrentVersion\Run`
)

// autostartCommand 返回写入注册表的命令行。
func autostartCommand() string {
	exe, err := os.Executable()
	if err != nil {
		exe = os.Args[0]
	}
	return fmt.Sprintf(`"%s" -minimized`, exe)
}

// SetAutostart 启用/禁用开机自启。
func SetAutostart(on bool) error {
	k, err := registry.OpenKey(registry.CURRENT_USER, runRegPath, registry.SET_VALUE)
	if err != nil {
		return err
	}
	defer k.Close()

	if !on {
		err := k.DeleteValue(autostartKeyName)
		if err == registry.ErrNotExist {
			return nil
		}
		return err
	}
	return k.SetStringValue(autostartKeyName, autostartCommand())
}

// IsAutostartOn 查询当前是否已启用。
func IsAutostartOn() bool {
	k, err := registry.OpenKey(registry.CURRENT_USER, runRegPath, registry.QUERY_VALUE)
	if err != nil {
		return false
	}
	defer k.Close()
	v, _, err := k.GetStringValue(autostartKeyName)
	if err != nil || v == "" {
		return false
	}
	// 存在即可视为开启；顺带校正路径（exe 被移动过就更新）
	exe, e2 := os.Executable()
	if e2 == nil && !strings.Contains(strings.ToLower(v), strings.ToLower(exe)) {
		_ = SetAutostart(true)
	}
	return true
}
