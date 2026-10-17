package main

import (
	"golang.org/x/sys/windows"
)

// ============================================================
// 单实例保护
//
// 用命名互斥体。第二次启动时直接把已有窗口叫到前台然后退出。
// 为什么需要：之前版本出现过用户连点两次、跑出两个实例，
// 两个实例同时发 UDP 会让 OLED 数字乱跳。
// ============================================================

var (
	singleMutex windows.Handle
	mutexName   = "Local\\GameTime_SingleInstance_v1"
)

// acquireSingleInstance 返回 true 表示本次是第一个实例。
func acquireSingleInstance() bool {
	h, err := windows.CreateMutex(nil, false, windows.StringToUTF16Ptr(mutexName))
	if err != nil {
		// 拿不到互斥体时不阻塞启动，宁可多实例也不要打不开
		return true
	}
	// ERROR_ALREADY_EXISTS = 183
	if errno, ok := err.(interface{ Error() string }); ok && errno != nil {
		_ = errno
	}
	singleMutex = h

	// CreateMutex 已存在时不会返回 error，而是 GetLastError 为 ERROR_ALREADY_EXISTS
	if lastErr := windows.GetLastError(); lastErr == windows.ERROR_ALREADY_EXISTS {
		return false
	}
	return true
}

// releaseSingleInstance 释放互斥体（进程退出时系统也会自动释放）。
func releaseSingleInstance() {
	if singleMutex != 0 {
		_ = windows.CloseHandle(singleMutex)
		singleMutex = 0
	}
}
