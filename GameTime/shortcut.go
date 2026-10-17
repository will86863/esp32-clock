package main

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"unicode/utf16"
)

// ============================================================
// 快捷方式解析（纯解析，不碰 COM）
//
// .lnk 的二进制格式（MS-SHLLINK）里有多个字段可能带目标路径：
//   - LinkInfo 结构里的 LocalBasePath（ANSI，系统代码页）
//   - LinkInfo 里的 LocalBasePathUnicode（UTF-16LE）
//   - StringData 里的 RELATIVE_PATH / WORKING_DIR
//
// 实测：中文路径 .lnk 的 UTF-16LE 字段是完整的，
// 而 ANSI 字段才是 GBK —— 所以我们优先找 UTF-16LE 明文，
// 完全不依赖系统代码页，也不需要 COM。
//
// 为什么放弃 COM IShellLink：手写 vtable 调用在本机反复触发
// PC=0x0 访问冲突（不同 Windows 版本的接口实现不一致），
// 而纯解析无外部依赖、可测试、不会崩。
// ============================================================

// lnkCandidate 从 .lnk 里提取到的一个候选目标路径。
type lnkCandidate struct {
	Path  string
	Score int // 越高越可信
}

// resolveLnk 解析 .lnk 的目标路径。
func resolveLnk(lnkPath string) (string, error) {
	data, err := os.ReadFile(lnkPath)
	if err != nil {
		return "", err
	}
	if len(data) < 0x4C {
		return "", fmt.Errorf("文件太小，不是有效的快捷方式")
	}
	// HeaderSize 必须是 0x4C，CLSID 必须是 00021401-0000-0000-C000-000000000046
	if u32(data, 0) != 0x4C {
		return "", fmt.Errorf("不是有效的 .lnk（头长度不符）")
	}
	if u32(data, 4) != 0x00021401 {
		return "", fmt.Errorf("不是有效的 .lnk（CLSID 不符）")
	}

	cands := collectLnkPaths(data)
	if len(cands) == 0 {
		return "", fmt.Errorf("没有从快捷方式里找到目标路径")
	}

	// 先按分数排序，再优先返回「文件真实存在」的那个。
	// 为什么必须校验存在性：碎片拼接会产生形如
	// "D:\Q]9,\Program Files\..." 的假路径（二进制噪声被当成路径段），
	// 只有真实存在的文件才可信。
	sort.SliceStable(cands, func(i, j int) bool { return cands[i].Score > cands[j].Score })

	for _, c := range cands {
		p := expandEnv(c.Path)
		if p == "" {
			continue
		}
		if st, err := os.Stat(p); err == nil && !st.IsDir() {
			return p, nil
		}
	}
	// 都不存在时退回最高分（可能是便携版/已卸载，仍让用户看到线索）
	best := expandEnv(cands[0].Path)
	if best == "" {
		return "", fmt.Errorf("目标路径为空")
	}
	return best, nil
}

// collectLnkPaths 扫描 .lnk 二进制，收集所有看起来像路径的字符串。
func collectLnkPaths(data []byte) []lnkCandidate {
	var out []lnkCandidate

	// 1) UTF-16LE 字符串（最可靠：不受代码页影响）
	for _, s := range utf16Strings(data) {
		if looksLikeExePath(s) {
			out = append(out, lnkCandidate{Path: s, Score: 100})
		}
	}

	// 2) ANSI 字符串（GBK/系统代码页）作为兜底
	ansi := ansiStrings(data)
	for _, s := range ansi {
		if looksLikeExePath(s) {
			out = append(out, lnkCandidate{Path: s, Score: 60})
		}
	}

	// 3) 拼接被 NUL 切碎的路径片段。
	//    实测 Ubisoft Connect.lnk 把 "C:\" / "Program Files (x86)" / "Ubisoft" /
	//    "Ubisoft Game Launcher" / "UbisoftConnect.exe" 拆成独立字符串，
	//    需要按顺序拼回完整路径。
	for _, joined := range joinPathFragments(data) {
		if looksLikeExePath(joined) {
			out = append(out, lnkCandidate{Path: joined, Score: 90})
		}
	}

	return out
}

// joinPathFragments 尝试把被 NUL 分隔的路径片段拼成完整 exe 路径。
//
// 实测 Ubisoft Connect.lnk 的布局：
//
//	[100] "/C:\"  [137] "Program Files (x86)"  [259] "Ubisoft"
//	[345] "Ubisoft Game Launcher"  [473] "UbisoftConnect.exe"
//
// 段间隔可以到 120+ 字节（中间夹着其它结构体），起点还可能带前导 "/"。
// 所以规则是：找到以盘符结尾的根片段，向后按顺序累积，直到某段以 .exe 结尾。
func joinPathFragments(data []byte) []string {
	type seg struct {
		text  string
		start int
		end   int
	}
	var segs []seg
	start := -1
	for i := 0; i <= len(data); i++ {
		printable := i < len(data) && data[i] >= 0x20 && data[i] < 0x7F
		if printable {
			if start < 0 {
				start = i
			}
		} else if start >= 0 {
			if i-start >= 2 {
				segs = append(segs, seg{text: string(data[start:i]), start: start, end: i})
			}
			start = -1
		}
	}

	var out []string
	for i := 0; i < len(segs); i++ {
		t := strings.TrimLeft(segs[i].text, `/\`)
		// 根片段：以盘符 + 反斜杠结尾，如 "C:\"
		if !isDriveRoot(t) {
			continue
		}

		cur := t
		for j := i + 1; j < len(segs) && j <= i+10; j++ {
			nx := segs[j].text
			// 段间可以隔开，但不能跨太远（同一 LinkInfo 结构内通常 < 200 字节）
			if segs[j].start-segs[j-1].end > 220 {
				break
			}
			if !strings.HasSuffix(cur, `\`) {
				cur += `\`
			}
			cur += nx
			if strings.HasSuffix(strings.ToLower(nx), ".exe") {
				break
			}
		}
		if strings.HasSuffix(strings.ToLower(cur), ".exe") {
			out = append(out, cur)
		}
	}
	return out
}

// isDriveRoot 判断是否形如 "C:\" 或 "C:"。
func isDriveRoot(s string) bool {
	if len(s) == 2 {
		return isAlpha(s[0]) && s[1] == ':'
	}
	if len(s) == 3 {
		return isAlpha(s[0]) && s[1] == ':' && (s[2] == '\\' || s[2] == '/')
	}
	return false
}

func isAlpha(b byte) bool {
	return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z')
}

// utf16Strings 从字节流里扫出 UTF-16LE 字符串（连续可打印宽字符）。
func utf16Strings(data []byte) []string {
	var out []string
	var buf []uint16
	flush := func() {
		if len(buf) >= 4 {
			s := string(utf16.Decode(buf))
			if s != "" {
				out = append(out, s)
			}
		}
		buf = buf[:0]
	}
	for i := 0; i+1 < len(data); i += 2 {
		c := uint16(data[i]) | uint16(data[i+1])<<8
		if isPrintableWide(c) {
			buf = append(buf, c)
		} else {
			flush()
		}
	}
	flush()
	return out
}

// ansiStrings 从字节流里扫出 ASCII/ANSI 字符串。
func ansiStrings(data []byte) []string {
	var out []string
	var buf []byte
	flush := func() {
		if len(buf) >= 6 {
			out = append(out, string(buf))
		}
		buf = buf[:0]
	}
	for _, c := range data {
		if c >= 0x20 && c < 0x7F {
			buf = append(buf, c)
		} else {
			flush()
		}
	}
	flush()
	return out
}

func isPrintableWide(c uint16) bool {
	return (c >= 0x20 && c < 0x7F) || (c >= 0x4E00 && c <= 0x9FFF) || c == 0x3000
}

// looksLikeExePath 判断字符串是否像指向 exe 的路径。
func looksLikeExePath(s string) bool {
	t := strings.TrimSpace(s)
	if len(t) < 5 {
		return false
	}
	low := strings.ToLower(t)
	if !strings.HasSuffix(low, ".exe") {
		return false
	}
	// 必须含路径分隔符或盘符，排除 "foo.exe" 这类碎片
	if !strings.ContainsAny(t, `\/`) && !strings.Contains(t, ":") {
		return false
	}
	// 排除明显的非目标字符串
	if strings.Contains(low, "shell32.dll") || strings.Contains(low, ".dll,") {
		return false
	}
	return true
}

// expandEnv 展开 %VAR% 与 ~ 等环境变量。
func expandEnv(p string) string {
	p = strings.TrimSpace(p)
	p = strings.Trim(p, "\x00")
	// %windir%\explorer.exe 这类：Go 的 os.Expand 只认 $VAR / ${VAR}，
	// 所以先把 %VAR% 手工替换掉。
	if strings.Contains(p, "%") {
		var b strings.Builder
		for i := 0; i < len(p); i++ {
			if p[i] != '%' {
				b.WriteByte(p[i])
				continue
			}
			j := strings.IndexByte(p[i+1:], '%')
			if j < 0 {
				b.WriteByte(p[i])
				continue
			}
			name := p[i+1 : i+1+j]
			if v, ok := os.LookupEnv(name); ok && v != "" {
				b.WriteString(v)
			} else {
				b.WriteString("%" + name + "%")
			}
			i += j + 1
		}
		p = b.String()
	}
	return p
}

// u32 读取小端 uint32。
func u32(b []byte, off int) uint32 {
	if off+4 > len(b) {
		return 0
	}
	return uint32(b[off]) | uint32(b[off+1])<<8 | uint32(b[off+2])<<16 | uint32(b[off+3])<<24
}

// ============================================================
// 路径类型与解析入口
// ============================================================

// PathKind 拖入/选择的路径类型。
type PathKind string

const (
	KindExe     PathKind = "exe"
	KindLnk     PathKind = "lnk"
	KindURL     PathKind = "url"
	KindDir     PathKind = "dir"
	KindUnknown PathKind = "unknown"
)

// ResolveResult 解析结果。
type ResolveResult struct {
	Kind       PathKind       `json:"kind"`
	Input      string         `json:"input"`
	Exe        string         `json:"exe"`
	Steam      *SteamGame     `json:"steam"`
	Candidates []ExeCandidate `json:"candidates"`
	Suggested  string         `json:"suggested"`
	Message    string         `json:"message"`
	Error      string         `json:"error"`
}

// ClassifyPath 判断路径类型。
func ClassifyPath(p string) PathKind {
	st, err := os.Stat(p)
	if err != nil {
		return KindUnknown
	}
	if st.IsDir() {
		return KindDir
	}
	switch strings.ToLower(filepath.Ext(p)) {
	case ".exe":
		return KindExe
	case ".lnk":
		return KindLnk
	case ".url":
		return KindURL
	}
	return KindUnknown
}

// ResolvePath 把用户拖入/选择的路径解析成可监控的程序。
func ResolvePath(p string) ResolveResult {
	p = strings.TrimSpace(strings.Trim(p, `"`))
	res := ResolveResult{Input: p}

	if p == "" {
		res.Error = "路径为空"
		return res
	}

	kind := ClassifyPath(p)
	res.Kind = kind

	switch kind {
	case KindDir:
		res.Error = "拖进来的是文件夹。请拖入程序的 .exe，或桌面上的快捷方式（.lnk / Steam 的 .url）。"
		return res

	case KindUnknown:
		res.Error = "无法识别这个文件。支持 .exe、.lnk（快捷方式）、.url（Steam 快捷方式）。"
		return res

	case KindExe:
		res.Exe = p
		res.Suggested = sanitizeDisplay(strings.TrimSuffix(filepath.Base(p), ".exe"))
		res.Message = "已添加可执行文件"
		return res

	case KindLnk:
		target, err := resolveLnk(p)
		if err != nil || target == "" {
			res.Error = fmt.Sprintf("解析快捷方式失败：%v。可以改用「选择程序…」直接选 exe。", err)
			return res
		}
		if strings.EqualFold(filepath.Ext(target), ".url") {
			return resolveSteamShortcut(p, target, &res)
		}
		if strings.EqualFold(filepath.Ext(target), ".lnk") {
			if t2, e2 := resolveLnk(target); e2 == nil && t2 != "" {
				target = t2
			}
		}
		if !strings.EqualFold(filepath.Ext(target), ".exe") {
			res.Error = fmt.Sprintf("快捷方式指向的不是 exe：%s", target)
			return res
		}
		res.Exe = target
		res.Suggested = sanitizeDisplay(strings.TrimSuffix(filepath.Base(target), ".exe"))
		res.Message = "已从快捷方式解析到程序"
		return res

	case KindURL:
		return resolveSteamShortcut(p, p, &res)
	}

	return res
}

// resolveSteamShortcut 处理 .url（Steam 游戏快捷方式）。
func resolveSteamShortcut(origPath, urlPath string, res *ResolveResult) ResolveResult {
	res.Input = origPath
	data, err := os.ReadFile(urlPath)
	if err != nil {
		res.Error = "读取 .url 文件失败：" + err.Error()
		return *res
	}
	appid := ParseSteamURL(string(data))
	if appid == "" {
		res.Error = "这个 .url 不是 Steam 游戏快捷方式（里面没有 steam://rungameid/）。"
		return *res
	}

	game := LookupSteamApp(appid)
	res.Steam = game
	if game.Name != "" {
		res.Suggested = sanitizeDisplay(game.Name)
	}

	if !game.Installed {
		res.Message = fmt.Sprintf("识别到 Steam 游戏「%s」(appid %s)，但没找到已安装的目录，请手动选择 exe。",
			fallbackStr(game.Name, "未知"), appid)
		return *res
	}

	res.Candidates = ListExeCandidates(game, 20)
	switch {
	case len(res.Candidates) == 0:
		res.Message = fmt.Sprintf("识别到「%s」，但安装目录里没找到 exe，请手动选择。", game.Name)
	case len(res.Candidates) == 1:
		res.Exe = res.Candidates[0].Path
		res.Message = fmt.Sprintf("识别到「%s」，目录里只有一个 exe，已自动选中。", game.Name)
	default:
		if RunningAppID() == appid {
			res.Message = fmt.Sprintf("识别到「%s」，Steam 显示它正在运行。请确认哪个是主程序。", game.Name)
		} else {
			res.Message = fmt.Sprintf("识别到「%s」，目录里有 %d 个 exe，请选择主程序（已按可能性排序）。",
				game.Name, len(res.Candidates))
		}
	}
	return *res
}

func fallbackStr(s, def string) string {
	if strings.TrimSpace(s) == "" {
		return def
	}
	return s
}
