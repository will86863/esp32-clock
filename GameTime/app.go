package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"
)

// App 暴露给前端的 API 集合。
type App struct {
	ctx       context.Context
	startTime time.Time
	version   string
}

func NewApp(version string) *App {
	return &App{startTime: time.Now(), version: version}
}

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
	// 注册拖放监听：必须在 startup 里，此时 Wails 运行时已就绪
	if fileDropHandler != nil {
		runtime.OnFileDrop(ctx, fileDropHandler)
	}
	// 启动托盘
	startTray()

	// -minimized：延迟隐藏窗口（此时窗口刚创建，直接 Hide 可能被后续 Show 覆盖）
	if startHiddenFlag {
		go func() {
			defer func() { _ = recover() }()
			time.Sleep(700 * time.Millisecond)
			runtime.WindowHide(ctx)
			log.Printf("[app] 已最小化到托盘（-minimized）")
		}()
	}
	log.Printf("[app] 启动完成，拖放监听已注册")
}

// TakeDropPath 取走最近一次拖入的路径（前端轮询取走）。
// 返回空串表示没有待处理路径。
func (a *App) TakeDropPath() string {
	dropMu.Lock()
	defer dropMu.Unlock()
	if len(dropPending) == 0 {
		return ""
	}
	p := dropPending[0]
	dropPending = dropPending[1:]
	return p
}

// ============================================================
// 配置
// ============================================================

// GetConfig 返回当前配置。
func (a *App) GetConfig() Config {
	cfg, err := LoadConfig()
	if err != nil {
		return defaultConfig()
	}
	return cfg
}

// SaveConfig 保存配置。保存后监控循环下一轮自动生效（无需重启）。
func (a *App) SaveConfig(cfg Config) (Config, error) {
	// 逐条校验显示名
	for i := range cfg.Games {
		d, err := ValidateDisplay(cfg.Games[i].Display)
		if err != nil {
			return cfg, fmt.Errorf("「%s」的显示名无效：%w", cfg.Games[i].Process, err)
		}
		cfg.Games[i].Display = d
	}
	if err := SaveConfig(cfg); err != nil {
		return cfg, err
	}
	return cfg, nil
}

// ============================================================
// 实时状态
// ============================================================

// GetStatus 返回概览页实时状态。
func (a *App) GetStatus() LiveStatus {
	l := getLive()
	l.Version = a.version
	l.StartedAt = a.startTime.Unix()
	cfg, _ := LoadConfig()
	l.Autostart = cfg.Autostart
	if l.Target == "" {
		l.Target = cfg.Target()
	}
	return l
}

// ============================================================
// 添加程序
// ============================================================

// PickFile 弹出系统文件对话框选择程序，返回解析结果。
// 走 Wails 内置对话框（WebView2 原生），不手写 OPENFILENAMEW。
func (a *App) PickFile() ResolveResult {
	if a.ctx == nil {
		return ResolveResult{Error: "程序尚未初始化"}
	}
	p, err := runtime.OpenFileDialog(a.ctx, runtime.OpenDialogOptions{
		Title: "选择要监控的程序",
		Filters: []runtime.FileFilter{
			{DisplayName: "程序与快捷方式 (*.exe;*.lnk;*.url)", Pattern: "*.exe;*.lnk;*.url"},
			{DisplayName: "所有文件 (*.*)", Pattern: "*.*"},
		},
	})
	if err != nil {
		return ResolveResult{Error: "打开文件对话框失败：" + err.Error()}
	}
	if p == "" {
		return ResolveResult{Error: ""} // 用户取消，静默
	}
	return ResolvePath(p)
}

// ResolvePath 解析拖入的路径（.exe / .lnk / .url）。
func (a *App) ResolvePath(path string) ResolveResult {
	return ResolvePath(path)
}

// ListCandidates 返回 Steam 游戏的候选 exe 列表（含大小与图标）。
func (a *App) ListCandidates(appid string) []ExeCandidate {
	g := LookupSteamApp(appid)
	if g == nil || !g.Installed {
		return nil
	}
	return ListExeCandidates(g, 30)
}

// AddGame 添加一个程序到监控列表。
func (a *App) AddGame(g Game) (Game, error) {
	cfg, err := LoadConfig()
	if err != nil {
		return g, err
	}

	exe := strings.TrimSpace(g.Exe)
	if exe == "" {
		return g, fmt.Errorf("没有选择程序文件")
	}
	if st, err := os.Stat(exe); err != nil || st.IsDir() {
		return g, fmt.Errorf("程序文件不存在：%s", exe)
	}

	proc := filepath.Base(exe)
	if g.Process != "" {
		proc = g.Process
	}

	// 去重：同一进程名不重复添加（固件只有一个名字位，重复无意义）
	for _, old := range cfg.Games {
		if normProcess(old.Process) == normProcess(proc) {
			return g, fmt.Errorf("「%s」已经在列表里了（显示名 %s）", proc, old.Display)
		}
	}

	disp := strings.TrimSpace(g.Display)
	if disp == "" {
		disp = sanitizeDisplay(strings.TrimSuffix(proc, ".exe"))
	}
	disp, err = ValidateDisplay(disp)
	if err != nil {
		return g, err
	}

	ng := Game{
		ID:      newID(),
		Exe:     exe,
		Process: proc,
		Display: disp,
		AppID:   g.AppID,
		Source:  g.Source,
	}
	if ng.Source == "" {
		ng.Source = "exe"
	}

	cfg.Games = append(cfg.Games, ng)
	if err := SaveConfig(cfg); err != nil {
		return ng, err
	}
	return ng, nil
}

// UpdateGame 修改已有程序（主要是改显示名）。
func (a *App) UpdateGame(g Game) (Game, error) {
	cfg, err := LoadConfig()
	if err != nil {
		return g, err
	}
	for i := range cfg.Games {
		if cfg.Games[i].ID != g.ID {
			continue
		}
		d, err := ValidateDisplay(g.Display)
		if err != nil {
			return g, err
		}
		cfg.Games[i].Display = d
		if g.Exe != "" && g.Exe != cfg.Games[i].Exe {
			cfg.Games[i].Exe = g.Exe
			cfg.Games[i].Process = filepath.Base(g.Exe)
		}
		if err := SaveConfig(cfg); err != nil {
			return g, err
		}
		return cfg.Games[i], nil
	}
	return g, fmt.Errorf("找不到这个程序（可能已被删除）")
}

// DeleteGame 从列表移除程序（历史统计保留）。
func (a *App) DeleteGame(id string) error {
	cfg, err := LoadConfig()
	if err != nil {
		return err
	}
	out := cfg.Games[:0]
	found := false
	for _, g := range cfg.Games {
		if g.ID == id {
			found = true
			continue
		}
		out = append(out, g)
	}
	if !found {
		return fmt.Errorf("找不到这个程序")
	}
	cfg.Games = out
	return SaveConfig(cfg)
}

// CheckDisplay 校验显示名（前端实时校验也可调这个）。
func (a *App) CheckDisplay(s string) (string, string) {
	v, err := ValidateDisplay(s)
	if err != nil {
		return v, err.Error()
	}
	return v, ""
}

// ============================================================
// 统计
// ============================================================

// GetStats 返回统计页数据，days 为折线天数（7 或 30）。
func (a *App) GetStats(days int) StatsResult {
	return QueryStats(days)
}

// ============================================================
// 杂项
// ============================================================

// SetAutostart 开关开机自启，并同步写入配置。
func (a *App) SetAutostart(on bool) (bool, error) {
	if err := SetAutostart(on); err != nil {
		return !on, err
	}
	cfg, err := LoadConfig()
	if err == nil {
		cfg.Autostart = on
		_ = SaveConfig(cfg)
	}
	return on, nil
}

// AutostartState 查询开机自启真实状态（以注册表为准）。
func (a *App) AutostartState() bool {
	return IsAutostartOn()
}

// OpenDataDir 打开配置与统计数据所在目录。
func (a *App) OpenDataDir() {
	dir := appDataDir()
	runtime.BrowserOpenURL(a.ctx, "file:///"+filepath.ToSlash(dir))
}

// OpenGameFolder 打开程序所在目录。
func (a *App) OpenGameFolder(path string) {
	dir := filepath.Dir(path)
	runtime.BrowserOpenURL(a.ctx, "file:///"+filepath.ToSlash(dir))
}

// HideWindow 隐藏主窗口到托盘。
func (a *App) HideWindow() {
	if a.ctx == nil {
		return
	}
	runtime.WindowHide(a.ctx)
}

// Quit 退出程序。
func (a *App) Quit() {
	shutdownApp()
}

// newID 生成短随机 id。
func newID() string {
	var b [8]byte
	if _, err := rand.Read(b[:]); err != nil {
		return fmt.Sprintf("%d", time.Now().UnixNano())
	}
	return hex.EncodeToString(b[:])
}
