package main

import (
	"bufio"
	"encoding/json"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"
)

// ============================================================
// 会话统计
//
// 存储：%APPDATA%\GameTime\sessions.jsonl，每行一条会话记录（追加写）。
// 为什么用 JSONL 而不是数据库：零依赖、追加即持久、崩溃最多丢最后一行，
// 10k 条会话约 1MB，对单机统计完全够用。
// ============================================================

// SessionRecord 一条已完成的会话。
type SessionRecord struct {
	GameID  string `json:"game_id"`  // 关联 Game.ID
	Process string `json:"process"`  // 进程名
	Display string `json:"display"`  // 当时的显示名（改名后历史仍可读）
	Start   int64  `json:"start"`    // unix 秒
	End     int64  `json:"end"`      // unix 秒
	Dur     int64  `json:"dur"`      // 秒
	Day     string `json:"day"`      // 本地日期 YYYY-MM-DD
}

// activeSnapshot 落盘用的「进行中会话」快照。
// 作用：程序被强杀后重启，能把上一次未收尾的会话补记完整。
type activeSnapshot struct {
	GameID  string `json:"game_id"`
	Process string `json:"process"`
	Display string `json:"display"`
	Start   int64  `json:"start"`
}

var (
	storeMu     sync.Mutex
	sessionsMu  sync.RWMutex
	sessions    []SessionRecord // 内存缓存，避免每次统计都读盘
	sessionsOK  bool
	sessionsPth string
	activePth   string
)

func initStats() {
	dir := appDataDir()
	sessionsPth = filepath.Join(dir, "sessions.jsonl")
	activePth = filepath.Join(dir, "session_active.json")
	recoverOrphanSession()
	_ = loadSessions()
}

// loadSessions 全量读入内存。
func loadSessions() error {
	f, err := os.Open(sessionsPth)
	if err != nil {
		if os.IsNotExist(err) {
			sessionsMu.Lock()
			sessions = nil
			sessionsOK = true
			sessionsMu.Unlock()
			return nil
		}
		return err
	}
	defer f.Close()

	out := make([]SessionRecord, 0, 512)
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	first := true
	for sc.Scan() {
		line := sc.Bytes()
		if first {
			line = stripBOM(line)
			first = false
		}
		if len(line) == 0 {
			continue
		}
		var r SessionRecord
		if json.Unmarshal(line, &r) != nil {
			continue // 坏行跳过，不影响其它记录
		}
		out = append(out, r)
	}
	sessionsMu.Lock()
	sessions = out
	sessionsOK = true
	sessionsMu.Unlock()
	return sc.Err()
}

// appendSession 追加一条会话记录（内存 + 磁盘）。
func appendSession(r SessionRecord) {
	if r.Dur <= 0 {
		return // 不记录 0 秒会话（用户要求：不记录空闲时间）
	}
	if r.Day == "" {
		r.Day = todayKey(time.Unix(r.Start, 0))
	}

	sessionsMu.Lock()
	sessions = append(sessions, r)
	sessionsMu.Unlock()

	storeMu.Lock()
	defer storeMu.Unlock()
	f, err := os.OpenFile(sessionsPth, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	if err != nil {
		return
	}
	defer f.Close()
	if b, err := json.Marshal(r); err == nil {
		_, _ = f.Write(append(b, '\n'))
	}
}

// writeActive 记录当前进行中的会话快照（每轮监控更新一次）。
func writeActive(s *activeSnapshot) {
	storeMu.Lock()
	defer storeMu.Unlock()
	if s == nil {
		_ = os.Remove(activePth)
		return
	}
	b, err := json.Marshal(s)
	if err != nil {
		return
	}
	_ = os.WriteFile(activePth, b, 0o644)
}

// recoverOrphanSession 处理上次异常退出留下的进行中会话。
//
// 场景：上位机被强杀 / 断电，session_active.json 还在。
// 做法：用快照里的 start 到「现在」补一条记录。
// 注意：如果进程其实早退出了，这段会被多算 —— 但相比丢整段会话，
// 用户明确更在意「不丢时长」，故取此策略。
func recoverOrphanSession() {
	data, err := os.ReadFile(activePth)
	if err != nil {
		return
	}
	var s activeSnapshot
	if json.Unmarshal(data, &s) != nil || s.Start <= 0 {
		_ = os.Remove(activePth)
		return
	}
	now := time.Now().Unix()
	// 上限 24 小时，防止时钟错乱产生荒谬数据
	if dur := now - s.Start; dur > 0 && dur <= 24*3600 {
		appendSession(SessionRecord{
			GameID:  s.GameID,
			Process: s.Process,
			Display: s.Display,
			Start:   s.Start,
			End:     now,
			Dur:     dur,
			Day:     todayKey(time.Unix(s.Start, 0)),
		})
	}
	_ = os.Remove(activePth)
}

// ============================================================
// 聚合查询（供前端图表）
// ============================================================

// DayTotal 某一天的合计。
type DayTotal struct {
	Day   string `json:"day"`
	Total int64  `json:"total"`
}

// GameTotal 某个程序的合计。
type GameTotal struct {
	GameID  string `json:"game_id"`
	Display string `json:"display"`
	Process string `json:"process"`
	Total   int64  `json:"total"`
	Runs    int    `json:"runs"`
}

// StatsResult 统计页需要的全部数据。
type StatsResult struct {
	TodayTotal int64       `json:"today_total"`
	Today      []GameTotal `json:"today"`      // 今日按程序分组
	Days       []DayTotal  `json:"days"`       // 近 N 天合计
	Recent     []SessionRecord `json:"recent"` // 最近会话
}

// QueryStats 返回统计页数据。days 为折线图天数（7/30）。
func QueryStats(days int) StatsResult {
	if days <= 0 {
		days = 7
	}
	sessionsMu.RLock()
	all := make([]SessionRecord, len(sessions))
	copy(all, sessions)
	sessionsMu.RUnlock()

	now := time.Now()
	today := todayKey(now)

	res := StatsResult{Today: []GameTotal{}, Days: []DayTotal{}, Recent: []SessionRecord{}}

	// 今日按程序聚合
	byGame := map[string]*GameTotal{}
	for _, s := range all {
		if s.Day != today {
			continue
		}
		key := s.GameID
		if key == "" {
			key = s.Process
		}
		gt, ok := byGame[key]
		if !ok {
			gt = &GameTotal{GameID: s.GameID, Display: s.Display, Process: s.Process}
			byGame[key] = gt
		}
		gt.Total += s.Dur
		gt.Runs++
		res.TodayTotal += s.Dur
	}
	for _, gt := range byGame {
		res.Today = append(res.Today, *gt)
	}
	sort.Slice(res.Today, func(i, j int) bool {
		if res.Today[i].Total != res.Today[j].Total {
			return res.Today[i].Total > res.Today[j].Total
		}
		return res.Today[i].Display < res.Today[j].Display
	})

	// 近 N 天合计（补齐空缺日，折线才有连续横轴）
	dayMap := map[string]int64{}
	for _, s := range all {
		dayMap[s.Day] += s.Dur
	}
	for i := days - 1; i >= 0; i-- {
		d := now.AddDate(0, 0, -i)
		k := todayKey(d)
		res.Days = append(res.Days, DayTotal{Day: k, Total: dayMap[k]})
	}

	// 最近 50 条会话（倒序）
	start := len(all) - 50
	if start < 0 {
		start = 0
	}
	for i := len(all) - 1; i >= start; i-- {
		res.Recent = append(res.Recent, all[i])
	}

	return res
}

// TodayTotalSeconds 返回今日已添加程序的合计秒数（发给 ESP32 的 today 字段）。
func TodayTotalSeconds() int64 {
	sessionsMu.RLock()
	defer sessionsMu.RUnlock()
	today := todayKey(time.Now())
	var sum int64
	for _, s := range sessions {
		if s.Day == today {
			sum += s.Dur
		}
	}
	return sum
}
