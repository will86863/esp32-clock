package main

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"syscall"
)

// ============================================================
// 打开文件夹 / 资源管理器
//
// 为什么不用 runtime.BrowserOpenURL("file:///..."):
//   该 API 是给 http(s) 用的，内部走 ShellExecute 的 URL 分支。
//   对 file:/// 目录它往往既不报错也不打开（被当作"URL 导航"处理），
//   实测在 WebView2 宿主里点「打开文件夹」毫无反应。
//
// 这里直接调 shell32!ShellExecuteW：
//   verb = "open", file = 文件夹绝对路径, 参数为空
// 系统会用默认关联（资源管理器）打开，这是最可靠的方式。
// ============================================================

// SW_SHOWNORMAL
const swShownormal = 1

// openPath 用系统默认方式打开文件或文件夹。
func openPath(path string) error {
	if path == "" {
		return fmt.Errorf("路径为空")
	}
	// 规范化：去掉尾部斜杠、转绝对路径（相对路径 ShellExecute 会按 exe 目录解析）
	abs, err := filepath.Abs(path)
	if err != nil {
		abs = path
	}
	// 目录必须存在，否则资源管理器会弹"找不到"且返回值也可能是成功
	if st, err := os.Stat(abs); err != nil {
		return fmt.Errorf("路径不存在：%s", abs)
	} else if !st.IsDir() {
		// 是文件就打开它的所在目录并选中它
		return revealInExplorer(abs)
	}

	// 直接用 explorer.exe 打开指定目录。
	//
	// 为什么不用 ShellExecuteW(verb="open")：
	// 实测它会让资源管理器"复用最近一个窗口"，实际打开的是别的位置
	// （例如程序自己所在的 build\bin），而不是传入的目录。
	// explorer.exe <绝对路径> 是确定性的：明确指定要打开哪个文件夹。
	return startExplorerAt(abs)
}

// startExplorerAt 用 explorer.exe 打开指定目录。
func startExplorerAt(dir string) error {
	// explorer.exe 在 Windows 目录根下，不在 System32（实测 System32 下没有）。
	// 依次尝试几个常见位置，最后回退到依赖 PATH 解析。
	windir := os.Getenv("WINDIR")
	if windir == "" {
		windir = os.Getenv("SystemRoot")
	}
	if windir == "" {
		windir = `C:\Windows`
	}
	candidates := []string{
		filepath.Join(windir, "explorer.exe"),
		filepath.Join(windir, "System32", "explorer.exe"),
		"explorer.exe",
	}
	exe := ""
	for _, c := range candidates {
		if c == "explorer.exe" {
			exe = c // 最后兜底，交给 CreateProcess 的搜索路径
			break
		}
		if _, err := os.Stat(c); err == nil {
			exe = c
			break
		}
	}
	if exe == "" {
		exe = "explorer.exe"
	}
	exePtr, err := syscall.UTF16PtrFromString(exe)
	if err != nil {
		return err
	}
	// 关键：CreateProcess 的 lpCommandLine 必须是完整命令行，
	// 第一个 token 是程序名，不能只传参数。
	// 之前只传 `"<目录>"`，explorer 把整个字符串当成"打开方式"解释，
	// 结果打开的是资源管理器首页（::{F874310E-...}）而不是目标文件夹。
	cmdline := `"` + exe + `" "` + dir + `"`
	argsPtr, err := syscall.UTF16PtrFromString(cmdline)
	if err != nil {
		return err
	}

	var si syscall.StartupInfo
	si.Flags |= syscall.STARTF_USESHOWWINDOW
	si.ShowWindow = swShownormal
	var pi syscall.ProcessInformation

	if err := syscall.CreateProcess(
		exePtr, argsPtr, nil, nil, false,
		0, nil, nil, &si, &pi,
	); err != nil {
		return fmt.Errorf("启动资源管理器失败: %w", err)
	}
	// 等一小会儿，能尽早发现"启动即失败"的情况
	_, _ = syscall.WaitForSingleObject(pi.Process, 1500)
	_ = syscall.CloseHandle(pi.Thread)
	_ = syscall.CloseHandle(pi.Process)
	return nil
}

// revealInExplorer 在资源管理器中选中指定文件。
// 用 explorer.exe /select, 是 Windows 的官方做法。
func revealInExplorer(filePath string) error {
	abs, err := filepath.Abs(filePath)
	if err != nil {
		abs = filePath
	}
	// explorer 的 /select, 后面必须紧跟逗号，且参数整体传给它
	comspec := os.Getenv("COMSPEC")
	if comspec == "" {
		comspec = `C:\Windows\System32\cmd.exe`
	}
	// 用 cmd /c start 避免 explorer 的退出码歧义
	arg := `/c start "" explorer.exe /select,"` + abs + `"`
	return runHidden(comspec, arg)
}

// runHidden 启动一个不显示窗口的进程。
// args 是「参数部分」，函数会自动把 exe 拼到命令行最前面
// （CreateProcess 要求 lpCommandLine 以程序名开头）。
func runHidden(exe, args string) error {
	exePtr, err := syscall.UTF16PtrFromString(exe)
	if err != nil {
		return err
	}
	cmdline := `"` + exe + `" ` + args
	argsPtr, err := syscall.UTF16PtrFromString(cmdline)
	if err != nil {
		return err
	}
	var si syscall.StartupInfo
	si.Flags |= syscall.STARTF_USESHOWWINDOW
	si.ShowWindow = 0 // SW_HIDE
	var pi syscall.ProcessInformation

	// CREATE_NO_WINDOW = 0x08000000
	const createNoWindow = 0x08000000
	if err := syscall.CreateProcess(
		exePtr, argsPtr, nil, nil, false,
		createNoWindow, nil, nil, &si, &pi,
	); err != nil {
		return err
	}
	_ = syscall.CloseHandle(pi.Thread)
	_ = syscall.CloseHandle(pi.Process)
	return nil
}

// logOpenResult 统一记录打开结果，便于排查。
func logOpenResult(what, path string, err error) {
	if err != nil {
		log.Printf("[open] %s 失败: %s -> %v", what, path, err)
		return
	}
	log.Printf("[open] %s 成功: %s", what, path)
}
