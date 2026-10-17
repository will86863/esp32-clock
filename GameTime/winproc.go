package main

import (
	"strings"
	"time"

	"golang.org/x/sys/windows"
)

// ProcInfo 一个进程的名字与启动时间。
type ProcInfo struct {
	Name    string
	PID     uint32
	Created time.Time
}

// listProcesses 用 Toolhelp 快照枚举全部进程及其创建时间。
//
// 为什么需要创建时间：判断「最后运行的程序」= 创建时间最晚的那个匹配进程（方案 A）。
// 拿不到创建时间的进程（权限不足/已退出）直接跳过，避免误判。
func listProcesses() ([]ProcInfo, error) {
	snap, err := windows.CreateToolhelp32Snapshot(windows.TH32CS_SNAPPROCESS, 0)
	if err != nil {
		return nil, err
	}
	defer windows.CloseHandle(snap)

	var entry windows.ProcessEntry32
	entry.Size = uint32(unsafeSizeofProcessEntry32())

	var out []ProcInfo
	err = windows.Process32First(snap, &entry)
	for err == nil {
		name := windows.UTF16ToString(entry.ExeFile[:])
		if name != "" {
			if created, e2 := processCreationTime(entry.ProcessID); e2 == nil {
				out = append(out, ProcInfo{Name: name, PID: entry.ProcessID, Created: created})
			}
		}
		err = windows.Process32Next(snap, &entry)
	}
	return out, nil
}

// processCreationTime 读取进程创建时间。
func processCreationTime(pid uint32) (time.Time, error) {
	h, err := windows.OpenProcess(windows.PROCESS_QUERY_LIMITED_INFORMATION, false, pid)
	if err != nil {
		return time.Time{}, err
	}
	defer windows.CloseHandle(h)

	var creation, exit, kernel, user windows.Filetime
	if err := windows.GetProcessTimes(h, &creation, &exit, &kernel, &user); err != nil {
		return time.Time{}, err
	}
	return filetimeToTime(creation), nil
}

// filetimeToTime FILETIME（1601-01-01 起的 100ns 计数）转 time.Time。
func filetimeToTime(ft windows.Filetime) time.Time {
	n := int64(ft.HighDateTime)<<32 | int64(ft.LowDateTime)
	const epochDiff = 116444736000000000 // 1601 → 1970 的 100ns 间隔
	return time.Unix(0, (n-epochDiff)*100)
}

// normProcess 进程名归一化：小写、去 .exe、去空白。
func normProcess(s string) string {
	s = strings.ToLower(strings.TrimSpace(s))
	s = strings.TrimSuffix(s, ".exe")
	return strings.TrimSpace(s)
}
