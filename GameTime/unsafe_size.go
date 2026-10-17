package main

import "unsafe"

// processEntry32Probe 与 windows.ProcessEntry32 布局一致，仅用于取大小。
type processEntry32Probe struct {
	Size            uint32
	CntUsage        uint32
	ProcessID       uint32
	DefaultHeapID   uintptr
	ModuleID        uint32
	CntThreads      uint32
	ParentProcessID uint32
	PriClassBase    int32
	Flags           uint32
	ExeFile         [windows_MAX_PATH]uint16
}

// windows_MAX_PATH 对应 Windows 的 MAX_PATH。
const windows_MAX_PATH = 260

// unsafeSizeofProcessEntry32 返回 PROCESSENTRY32W 的字节大小。
//
// 抽到独立文件，把 unsafe 的使用收敛在一处，其余业务代码保持纯逻辑。
func unsafeSizeofProcessEntry32() int {
	return int(unsafe.Sizeof(processEntry32Probe{}))
}
