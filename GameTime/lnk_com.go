package main

import (
	"fmt"
	"syscall"
	"unsafe"
)

// ============================================================
// .lnk 解析 —— 走 Windows 官方 COM（IShellLinkW + IPersistFile）
//
// 为什么最终选 COM 而不是纯二进制解析：
//   纯解析要自己处理 ANSI(GBK)/UTF-16 两种编码、8.3 短名、
//   变长 StringData 的奇偶对齐，实测会把中文路径拼成 "REMEMB~1"、
//   把英文路径拼出 "锞\Ubisoft\锞" 这类垃圾，非常脆弱。
//   COM 是系统自己的实现，中文路径、相对路径、MSI 广告快捷方式全都能正确处理。
//
// vtable 布局（关键，错了就 PC=0x0 崩溃）：
//   IUnknown      : QueryInterface=0, AddRef=1, Release=2
//   IPersist      : GetClassID=3
//   IPersistFile  : IsDirty=4, Load=5, Save=6, SaveCompleted=7, GetCurFile=8
//   IShellLinkW   : Resolve=0? 不——IShellLinkW 继承 IUnknown，
//                   GetPath=3, GetIDList=4, SetIDList=5, GetDescription=6 ...
// ============================================================

type comGUID struct {
	Data1 uint32
	Data2 uint16
	Data3 uint16
	Data4 [8]byte
}

var (
	ole32DLL           = syscall.NewLazyDLL("ole32.dll")
	procCoCreateInst   = ole32DLL.NewProc("CoCreateInstance")
	procCoInitializeEx = ole32DLL.NewProc("CoInitializeEx")
	procCoUninitialize = ole32DLL.NewProc("CoUninitialize")
)

var (
	clsShellLink = comGUID{0x00021401, 0x0000, 0x0000,
		[8]byte{0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}
	iidShellLinkW = comGUID{0x000214F9, 0x0000, 0x0000,
		[8]byte{0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}
	iidPersistFile = comGUID{0x0000010B, 0x0000, 0x0000,
		[8]byte{0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}
)

// comVtbl 调用 vtable 第 index 项（自动把 this 作为第一个参数）。
func comVtbl(p unsafe.Pointer, index int, args ...uintptr) (uintptr, error) {
	vt := *(*unsafe.Pointer)(p)
	fn := *(*uintptr)(unsafe.Add(vt, uintptr(index)*unsafe.Sizeof(uintptr(0))))
	if fn == 0 {
		return 0, fmt.Errorf("vtable 第 %d 项为空", index)
	}
	all := make([]uintptr, 0, len(args)+1)
	all = append(all, uintptr(p))
	all = append(all, args...)
	r, _, _ := syscall.SyscallN(fn, all...)
	if int32(r) < 0 {
		return r, fmt.Errorf("COM 调用失败 hr=0x%08X", uint32(r))
	}
	return r, nil
}

func comRelease(p unsafe.Pointer) {
	if p == nil {
		return
	}
	vt := *(*unsafe.Pointer)(p)
	if fn := *(*uintptr)(unsafe.Add(vt, 2*unsafe.Sizeof(uintptr(0)))); fn != 0 {
		_, _, _ = syscall.SyscallN(fn, uintptr(p))
	}
}

// resolveLnkCOM 用 IShellLinkW 拿 .lnk 的目标路径。
// 调用方需在同一个 OS 线程上（Wails 的 UI 线程满足）。
func resolveLnkCOM(lnkPath string) (string, error) {
	// STA：与桌面 shell 一致
	hr, _, _ := procCoInitializeEx.Call(0, 0x2 /*COINIT_APARTMENTTHREADED*/)
	if int32(hr) >= 0 {
		defer procCoUninitialize.Call()
	}
	// RPC_E_CHANGED_MODE(0x80010106) 等负值时继续尝试，只是不配对 Uninitialize

	var psl unsafe.Pointer
	r, _, _ := procCoCreateInst.Call(
		uintptr(unsafe.Pointer(&clsShellLink)),
		0,
		1, // CLSCTX_INPROC_SERVER
		uintptr(unsafe.Pointer(&iidShellLinkW)),
		uintptr(unsafe.Pointer(&psl)),
	)
	if uint32(r) != 0 || psl == nil {
		return "", fmt.Errorf("创建 IShellLink 失败 hr=0x%08X", uint32(r))
	}
	defer comRelease(psl)

	var ppf unsafe.Pointer
	if _, err := comVtbl(psl, 0, // QueryInterface
		uintptr(unsafe.Pointer(&iidPersistFile)),
		uintptr(unsafe.Pointer(&ppf)),
	); err != nil {
		return "", fmt.Errorf("QueryInterface(IPersistFile) 失败: %w", err)
	}
	defer comRelease(ppf)

	w, err := syscall.UTF16PtrFromString(lnkPath)
	if err != nil {
		return "", err
	}
	// IPersistFile::Load(path, STGM_READ=0)
	if _, err := comVtbl(ppf, 5, uintptr(unsafe.Pointer(w)), 0); err != nil {
		return "", fmt.Errorf("加载 .lnk 失败: %w", err)
	}

	// IShellLinkW::GetPath(buf, cch, pfd, fFlags)
	buf := make([]uint16, 1024)
	var findData [0x200]byte // WIN32_FIND_DATAW 输出缓冲
	if _, err := comVtbl(psl, 3,
		uintptr(unsafe.Pointer(&buf[0])),
		uintptr(len(buf)),
		uintptr(unsafe.Pointer(&findData[0])),
		0,
	); err != nil {
		return "", fmt.Errorf("GetPath 失败: %w", err)
	}
	target := syscall.UTF16ToString(buf)
	if target == "" {
		return "", fmt.Errorf("快捷方式没有目标路径")
	}
	return target, nil
}