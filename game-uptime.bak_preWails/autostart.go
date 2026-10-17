package main

import (
	"errors"
	"os"

	"golang.org/x/sys/windows/registry"
)

const autostartValueName = "GameUptime"

// setAutostart 通过 HKCU Run 键设置/取消开机自启
func setAutostart(enable bool) error {
	exe, err := os.Executable()
	if err != nil {
		return err
	}
	k, err := registry.OpenKey(
		registry.CURRENT_USER,
		`Software\Microsoft\Windows\CurrentVersion\Run`,
		registry.SET_VALUE|registry.QUERY_VALUE,
	)
	if err != nil {
		return err
	}
	defer k.Close()

	if enable {
		return k.SetStringValue(autostartValueName, exe)
	}
	err = k.DeleteValue(autostartValueName)
	if errors.Is(err, registry.ErrNotExist) {
		return nil // 本来就没有，视为已取消
	}
	return err
}
