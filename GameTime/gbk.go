package main

import (
	"syscall"
	"unicode/utf16"
	"unsafe"
)

// ============================================================
// ANSI（代码页 = 当前系统，简体中文即 GBK/936）解码
//
// .lnk 里的 ANSI 路径段按系统代码页存储，直接当 ASCII 读会把中文
// 变成乱码或被截断（实测中文目录的 .lnk 只能拼出 "REMEMB~1" 这种碎片）。
//
// 这里走 Windows 官方 MultiByteToWideChar(CP_ACP)：
//   - 不引入 golang.org/x/text 依赖
//   - 不硬编码任何码表
//   - 系统是什么代码页就按什么解
// ============================================================

var (
	kernel32         = syscall.NewLazyDLL("kernel32.dll")
	procMByteToWide  = kernel32.NewProc("MultiByteToWideChar")
)

const cpACP = 0 // CP_ACP：当前系统 ANSI 代码页

// decodeGBK 把一段 ANSI/GBK 字节解成 UTF-8 字符串。
// 解码失败时退回原始字节（保证调用方总能拿到可用字符串）。
func decodeGBK(b []byte) (string, error) {
	if len(b) == 0 {
		return "", nil
	}
	// 先按「每字节最多 2 个宽字符」分配上界，避免两次调用
	buf := make([]uint16, len(b)*2+2)
	n, _, errno := procMByteToWide.Call(
		uintptr(cpACP),          // CodePage
		0,                       // dwFlags
		uintptr(unsafe.Pointer(&b[0])),
		uintptr(len(b)),         // cbMultiByte
		uintptr(unsafe.Pointer(&buf[0])),
		uintptr(len(buf)),       // cchWideChar
	)
	if n == 0 {
		// errno 已是 error 类型，直接返回
		return string(b), errno
	}
	return string(utf16.Decode(buf[:n])), nil
}