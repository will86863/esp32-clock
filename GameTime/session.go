package main

import (
	"time"
)

// ============================================================
// 会话状态机
//
// 规则（用户确认）：
//   - 当前程序 = 已添加程序里「创建时间最晚」的匹配进程（方案 A）
//   - up       = 本次会话已运行秒数，从「该进程创建时间」起算
//   - 不记录空闲时间：没有已添加程序在跑时不写会话
//   - today    = 已添加程序今日会话秒数之和（无程序运行时发给 ESP32）
//
// 为什么 up 用进程创建时间而不是本程序开始观察的时间：
// 上位机被强杀后重启，时长能无缝接续，不会从 0 重新计。
// ============================================================

// activeSession 正在进行的会话。
type activeSession struct {
	GameID  string    // 对应 Game.ID
	Process string    // 进程名（原始大小写）
	Start   time.Time // 会话起点 = 进程创建时间
}

// pickLatest 从进程表里挑出「创建时间最晚」的已添加程序。
//
// games 为已添加程序，procs 为当前全部进程。
// 返回命中的 Game 指针与对应进程；没有命中返回 nil。
func pickLatest(games []Game, procs []ProcInfo) (*Game, *ProcInfo) {
	// 建索引：归一化进程名 → Game（同名取第一个，避免重复添加时歧义）
	byName := make(map[string]*Game, len(games))
	for i := range games {
		g := &games[i]
		k := normProcess(g.Process)
		if k == "" {
			continue
		}
		if _, dup := byName[k]; !dup {
			byName[k] = g
		}
	}
	if len(byName) == 0 {
		return nil, nil
	}

	var bestGame *Game
	var bestProc *ProcInfo
	for i := range procs {
		p := &procs[i]
		g, ok := byName[normProcess(p.Name)]
		if !ok {
			continue
		}
		if bestProc == nil || p.Created.After(bestProc.Created) {
			bestGame, bestProc = g, p
		}
	}
	return bestGame, bestProc
}

// sessionUp 返回会话已运行秒数（向下取整，负数归零）。
func sessionUp(s *activeSession, now time.Time) int64 {
	if s == nil {
		return 0
	}
	sec := int64(now.Sub(s.Start).Seconds())
	if sec < 0 {
		return 0
	}
	return sec
}
