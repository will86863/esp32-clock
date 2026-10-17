package main

import (
	_ "embed"
	"log"
	"runtime"
	"sync"

	"github.com/energye/systray"
	wruntime "github.com/wailsapp/wails/v2/pkg/runtime"
)

// ============================================================
// 系统托盘
//
// 用 energye/systray 而不是 Wails 内置托盘：
// Wails v2 本身没有内置托盘；v3 beta 的托盘点击回调实测不触发。
// energye/systray 成熟、支持左键单击回调、Windows 下吃 .ico。
//
// 注意两点（都是实测结论）：
//   1. Windows 版托盘图标必须是 .ico，给 PNG 会静默失败
//   2. 菜单项用 item.Click(fn) 注册回调，没有 ClickedCh 通道
// ============================================================

//go:embed assets/tray.ico
var trayIcon []byte

var (
	trayStartOne sync.Once
	trayMenuAuto *systray.MenuItem
)

// startTray 启动托盘（阻塞在自带消息循环里，需独立 OS 线程）。
func startTray() {
	trayStartOne.Do(func() {
		go func() {
			// systray 需要独占一个 OS 线程跑 Win32 消息循环
			runtime.LockOSThread()
			defer runtime.UnlockOSThread()
			systray.Run(onTrayReady, onTrayExit)
		}()
	})
}

func onTrayReady() {
	if len(trayIcon) > 0 {
		systray.SetIcon(trayIcon)
	}
	systray.SetTitle("GameTime")
	systray.SetTooltip("GameTime · 游戏时长监控")

	mOpen := systray.AddMenuItem("打开主界面", "显示 GameTime 窗口")
	systray.AddSeparator()
	mAuto := systray.AddMenuItemCheckbox("开机自动启动", "登录 Windows 后自动运行（最小化到托盘）", IsAutostartOn())
	mHide := systray.AddMenuItem("最小化到托盘", "隐藏窗口")
	systray.AddSeparator()
	mQuit := systray.AddMenuItem("退出程序", "停止监控并退出")

	trayMenuAuto = mAuto
	log.Printf("[tray] 托盘已就绪（energye/systray，图标 %d 字节）", len(trayIcon))

	// 左键单击 = 打开主界面（用户最需要的动作）
	systray.SetOnClick(func(menu systray.IMenu) {
		showMainWindow()
	})
	// 右键显式弹菜单
	systray.SetOnRClick(func(menu systray.IMenu) {
		_ = menu.ShowMenu()
	})

	mOpen.Click(func() { showMainWindow() })
	mHide.Click(func() { hideMainWindow() })
	mAuto.Click(func() {
		if mAuto.Checked() {
			if err := SetAutostart(false); err != nil {
				log.Printf("[tray] 关闭自启失败: %v", err)
				return
			}
			mAuto.Uncheck()
		} else {
			if err := SetAutostart(true); err != nil {
				log.Printf("[tray] 开启自启失败: %v", err)
				return
			}
			mAuto.Check()
		}
		if cfg, err := LoadConfig(); err == nil {
			cfg.Autostart = mAuto.Checked()
			_ = SaveConfig(cfg)
		}
	})
	mQuit.Click(func() {
		systray.Quit()
		shutdownApp()
	})
}

func onTrayExit() {}

// syncTrayAutostart 让托盘菜单勾选状态跟随设置页的改动。
func syncTrayAutostart(on bool) {
	if trayMenuAuto == nil {
		return
	}
	if on {
		trayMenuAuto.Check()
	} else {
		trayMenuAuto.Uncheck()
	}
}

// showMainWindow 显示并聚焦主窗口。
func showMainWindow() {
	if mainApp == nil || mainApp.ctx == nil {
		return
	}
	ctx := mainApp.ctx
	go func() {
		defer func() { _ = recover() }()
		wruntime.WindowShow(ctx)
		wruntime.WindowUnminimise(ctx)
		// 置顶再取消，把窗口提到前台（SetForegroundWindow 常被系统拒绝）
		wruntime.WindowSetAlwaysOnTop(ctx, true)
		wruntime.WindowSetAlwaysOnTop(ctx, false)
	}()
}

// hideMainWindow 隐藏主窗口。
func hideMainWindow() {
	if mainApp == nil || mainApp.ctx == nil {
		return
	}
	ctx := mainApp.ctx
	go func() {
		defer func() { _ = recover() }()
		wruntime.WindowHide(ctx)
	}()
}
