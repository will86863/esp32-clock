package main

import (
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
)

// ProcessInfo 进程名 + 创建时间
type ProcessInfo struct {
	Name    string
	PID     uint32
	Created time.Time
}

// listProcesses 枚举系统中所有进程及其创建时间
func listProcesses() ([]ProcessInfo, error) {
	snap, err := windows.CreateToolhelp32Snapshot(windows.TH32CS_SNAPPROCESS, 0)
	if err != nil {
		return nil, err
	}
	defer windows.CloseHandle(snap)

	var entry windows.ProcessEntry32
	entry.Size = uint32(unsafe.Sizeof(entry))

	var out []ProcessInfo
	err = windows.Process32First(snap, &entry)
	for err == nil {
		name := windows.UTF16ToString(entry.ExeFile[:])
		if created, err2 := processCreationTime(entry.ProcessID); err2 == nil {
			out = append(out, ProcessInfo{Name: name, PID: entry.ProcessID, Created: created})
		}
		err = windows.Process32Next(snap, &entry)
	}
	return out, nil
}

// processCreationTime 获取进程创建时间（FILETIME -> time.Time）
func processCreationTime(pid uint32) (time.Time, error) {
	handle, err := windows.OpenProcess(windows.PROCESS_QUERY_LIMITED_INFORMATION, false, pid)
	if err != nil {
		return time.Time{}, err
	}
	defer windows.CloseHandle(handle)

	var creation, exit, kernel, user windows.Filetime
	if err := windows.GetProcessTimes(handle, &creation, &exit, &kernel, &user); err != nil {
		return time.Time{}, err
	}
	return filetimeToTime(creation), nil
}

// filetimeToTime FILETIME（1601-01-01 起 100ns 间隔）转 time.Time
func filetimeToTime(ft windows.Filetime) time.Time {
	n := int64(ft.HighDateTime)<<32 | int64(ft.LowDateTime)
	const epochDiff = 116444736000000000 // 1601 到 1970 的 100ns 间隔数
	return time.Unix(0, (n-epochDiff)*100)
}
