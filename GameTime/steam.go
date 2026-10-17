package main

import (
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"

	"golang.org/x/sys/windows/registry"
)

// ============================================================
// Steam 快捷方式解析
//
// 能力边界（已在本机实测确认）：
//   .url → appid        100% 可靠（正则取 steam://rungameid/<id>）
//   appid → 游戏名       100% 可靠（appmanifest_<id>.acf 的 "name"）
//   appid → 安装目录     100% 可靠（appmanifest 的 "installdir" + libraryfolders.vdf 多库）
//   appid → exe 文件名   本地无权威来源（appinfo.vdf 是复杂二进制 VDF，不做解析）
//                        → 扫安装目录列候选，交给用户手动确认（用户已确认此方案）
//
// 额外：注册表 HKCU\Software\Valve\Steam\RunningAppID 是 Steam 自己维护的
// 「当前正在运行的游戏」，100% 准确，用作兜底校验。
// ============================================================

// SteamGame appmanifest 里能拿到的信息。
type SteamGame struct {
	AppID      string `json:"appid"`
	Name       string `json:"name"`
	InstallDir string `json:"installdir"`
	LibPath    string `json:"libpath"`    // 该游戏所在的 Steam 库根目录
	FullPath   string `json:"fullpath"`   // common\<installdir> 的绝对路径
	Installed  bool   `json:"installed"`
}

// ExeCandidate 安装目录里的候选 exe。
type ExeCandidate struct {
	Path      string `json:"path"`
	Name      string `json:"name"`
	Size      int64  `json:"size"`
	SizeText  string `json:"sizetext"`
	Icon      string `json:"icon"`      // data URL（PNG），可能为空
	Preferred bool   `json:"preferred"` // 启发式推荐
	Reason    string `json:"reason"`    // 推荐/排除理由
}

var reRunningAppID = regexp.MustCompile(`(?i)steam://rungameid/(\d+)`)

// ParseSteamURL 从 .url 内容里取 appid。
func ParseSteamURL(content string) string {
	m := reRunningAppID.FindStringSubmatch(content)
	if len(m) < 2 {
		return ""
	}
	return m[1]
}

// steamRoot 返回 Steam 安装目录。
func steamRoot() string {
	// 优先注册表（HKCU 的 SteamPath 最常见）
	if k, err := registry.OpenKey(registry.CURRENT_USER, `Software\Valve\Steam`, registry.QUERY_VALUE); err == nil {
		defer k.Close()
		if v, _, err := k.GetStringValue("SteamPath"); err == nil && v != "" {
			return filepath.Clean(strings.ReplaceAll(v, "/", `\`))
		}
	}
	if k, err := registry.OpenKey(registry.LOCAL_MACHINE, `SOFTWARE\WOW6432Node\Valve\Steam`, registry.QUERY_VALUE); err == nil {
		defer k.Close()
		if v, _, err := k.GetStringValue("InstallPath"); err == nil && v != "" {
			return filepath.Clean(v)
		}
	}
	for _, p := range []string{
		`C:\Program Files (x86)\Steam`,
		`C:\Program Files\Steam`,
		`D:\Steam`,
	} {
		if st, err := os.Stat(p); err == nil && st.IsDir() {
			return p
		}
	}
	return ""
}

// steamLibraries 返回所有 Steam 库根目录（含主库），顺序去重。
//
// 解析 libraryfolders.vdf 的 "path" 字段；老格式是纯路径值，
// 新格式是 path 子键，两种都要兼容。
func steamLibraries() []string {
	root := steamRoot()
	if root == "" {
		return nil
	}
	libs := []string{root}
	seen := map[string]bool{strings.ToLower(root): true}

	vdf := filepath.Join(root, "steamapps", "libraryfolders.vdf")
	data, err := os.ReadFile(vdf)
	if err != nil {
		return libs
	}
	// 抓所有 "path" 的字符串值，以及老格式下直接当值的盘符路径
	for _, m := range regexp.MustCompile(`"path"\s+"([^"]+)"`).FindAllStringSubmatch(string(data), -1) {
		p := filepath.Clean(strings.ReplaceAll(m[1], `\\`, `\`))
		if !seen[strings.ToLower(p)] {
			seen[strings.ToLower(p)] = true
			libs = append(libs, p)
		}
	}
	for _, m := range regexp.MustCompile(`"(\d+)"\s+"([A-Za-z]:[^"]+)"`).FindAllStringSubmatch(string(data), -1) {
		p := filepath.Clean(strings.ReplaceAll(m[2], `\\`, `\`))
		if !seen[strings.ToLower(p)] {
			seen[strings.ToLower(p)] = true
			libs = append(libs, p)
		}
	}
	return libs
}

// findAppManifest 在所有库里找 appmanifest_<appid>.acf。
func findAppManifest(appid string) (*SteamGame, bool) {
	for _, lib := range steamLibraries() {
		p := filepath.Join(lib, "steamapps", "appmanifest_"+appid+".acf")
		data, err := os.ReadFile(p)
		if err != nil {
			continue
		}
		g := &SteamGame{AppID: appid, LibPath: lib, Installed: true}
		if m := regexp.MustCompile(`"name"\s+"([^"]*)"`).FindStringSubmatch(string(data)); len(m) > 1 {
			g.Name = m[1]
		}
		if m := regexp.MustCompile(`"installdir"\s+"([^"]*)"`).FindStringSubmatch(string(data)); len(m) > 1 {
			g.InstallDir = m[1]
		}
		if g.InstallDir != "" {
			g.FullPath = filepath.Join(lib, "steamapps", "common", g.InstallDir)
			if st, err := os.Stat(g.FullPath); err != nil || !st.IsDir() {
				g.Installed = false
			}
		}
		return g, true
	}
	return nil, false
}

// LookupSteamApp 按 appid 查 Steam 游戏信息。
func LookupSteamApp(appid string) *SteamGame {
	if g, ok := findAppManifest(appid); ok {
		return g
	}
	return &SteamGame{AppID: appid, Installed: false}
}

// RunningAppID 返回 Steam 当前正在运行的 appid（"0" 或空表示没有）。
func RunningAppID() string {
	k, err := registry.OpenKey(registry.CURRENT_USER, `Software\Valve\Steam\ActiveProcess`, registry.QUERY_VALUE)
	if err == nil {
		defer k.Close()
		if v, _, err := k.GetIntegerValue("ActiveUser"); err == nil && v == 0 {
			// ActiveUser=0 表示未登录，继续尝试下面
		}
		if v, _, err := k.GetIntegerValue("AppId"); err == nil {
			if v > 0 {
				return strconv.FormatUint(uint64(v), 10)
			}
		}
	}
	// 备用位置
	if k2, err := registry.OpenKey(registry.CURRENT_USER, `Software\Valve\Steam`, registry.QUERY_VALUE); err == nil {
		defer k2.Close()
		if v, _, err := k2.GetIntegerValue("RunningAppID"); err == nil && v > 0 {
			return strconv.FormatUint(uint64(v), 10)
		}
	}
	return ""
}

// exeBlocklist 明确不是游戏主程序的 exe 关键字（小写匹配）。
var exeBlocklist = []string{
	"unins", "uninst", "setup", "installer", "vcredist", "vc_redist",
	"dxsetup", "directx", "dotnet", "crashhandler", "crashreport",
	"crashpad", "unitycrashhandler", "report", "updater", "update.exe",
	"launcher_installer", "easyanticheat", "eac_", "battleye", "be_",
	"steamerrorreporter", "notification_helper", "touchup", "helper.exe",
}

// exePreferredKeywords 更像主程序的关键字（小写匹配）。
var exePreferredKeywords = []string{"game", "start", "play", "win64", "shipping"}

// ListExeCandidates 扫描游戏安装目录，列出候选 exe（含大小与图标）。
//
// 排序：优先推荐的在前，其次体积大的在前（主程序通常比工具大）。
func ListExeCandidates(game *SteamGame, limit int) []ExeCandidate {
	if game == nil || game.FullPath == "" || !game.Installed {
		return nil
	}
	if limit <= 0 {
		limit = 20
	}

	var out []ExeCandidate
	_ = filepath.Walk(game.FullPath, func(p string, info os.FileInfo, err error) error {
		if err != nil {
			return nil // 无权限的目录直接跳过
		}
		if info.IsDir() {
			// 跳过明显不是游戏本的子目录，减少误报
			base := strings.ToLower(info.Name())
			if base == "redist" || base == "redistributables" || base == "_commonredist" ||
				base == "directx" || base == "dotnet" || base == "vcredist" ||
				base == "installer" || base == "support" {
				return filepath.SkipDir
			}
			return nil
		}
		if !strings.EqualFold(filepath.Ext(p), ".exe") {
			return nil
		}
		// 限制搜索深度，避免超大目录树卡住
		rel, _ := filepath.Rel(game.FullPath, p)
		if strings.Count(rel, string(os.PathSeparator)) > 3 {
			return nil
		}

		lower := strings.ToLower(filepath.Base(p))
		blocked := ""
		for _, b := range exeBlocklist {
			if strings.Contains(lower, b) {
				blocked = b
				break
			}
		}
		cand := ExeCandidate{
			Path:     p,
			Name:     filepath.Base(p),
			Size:     info.Size(),
			SizeText: humanSize(info.Size()),
			Icon:     iconDataURL(p),
		}
		if blocked != "" {
			cand.Reason = "疑似安装/组件程序（" + blocked + "）"
		} else {
			cand.Preferred = true
			cand.Reason = "候选主程序"
			for _, k := range exePreferredKeywords {
				if strings.Contains(lower, k) {
					cand.Reason = "候选主程序（含关键字 " + k + "）"
					break
				}
			}
		}
		out = append(out, cand)
		return nil
	})

	sort.SliceStable(out, func(i, j int) bool {
		if out[i].Preferred != out[j].Preferred {
			return out[i].Preferred
		}
		return out[i].Size > out[j].Size
	})

	// 只保留前 limit 个：优先保留推荐的，被排除的仅在数量不足时补充
	var picked []ExeCandidate
	for _, c := range out {
		if c.Preferred {
			picked = append(picked, c)
		}
	}
	for _, c := range out {
		if !c.Preferred && len(picked) < limit {
			picked = append(picked, c)
		}
	}
	if len(picked) > limit {
		picked = picked[:limit]
	}
	return picked
}

// humanSize 人类可读体积。
func humanSize(n int64) string {
	const unit = 1024
	if n < unit {
		return itoa(n) + " B"
	}
	div, exp := int64(unit), 0
	for m := n / unit; m >= unit; m /= unit {
		div *= unit
		exp++
	}
	val := float64(n) / float64(div)
	// 保留一位小数
	whole := int64(val)
	frac := int64((val - float64(whole)) * 10)
	suffix := []string{"KB", "MB", "GB", "TB"}[exp]
	if exp >= 2 {
		return itoa(whole) + "." + itoa(frac) + " " + suffix
	}
	return itoa(whole) + " " + suffix
}
