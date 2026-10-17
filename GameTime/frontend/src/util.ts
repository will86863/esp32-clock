// 时长格式化与显示名约束
// 显示名规则来自固件：char gameName[24] + OLED 一行 16 字符宽

export const DISPLAY_MAX = 16

export function fmtHMS(sec: number): string {
  const s = Math.max(0, Math.floor(sec || 0))
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const ss = s % 60
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(ss).padStart(2, '0')}`
}

export function fmtHuman(sec: number): string {
  const s = Math.max(0, Math.floor(sec || 0))
  if (s < 60) return `${s} 秒`
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  if (h > 0) return m > 0 ? `${h} 小时 ${m} 分` : `${h} 小时`
  return `${m} 分钟`
}

export function fmtClock(unixSec: number): string {
  if (!unixSec) return '—'
  const d = new Date(unixSec * 1000)
  const p = (n: number) => String(n).padStart(2, '0')
  return `${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`
}

export function fmtDay(day: string): string {
  if (!day) return ''
  const [, m, d] = day.split('-')
  return `${Number(m)}/${Number(d)}`
}

// 显示名校验，返回错误文案（空串表示合法）
export function checkDisplay(s: string): string {
  const v = (s || '').trim()
  if (!v) return '显示名不能为空'
  for (const ch of v) {
    if (ch.codePointAt(0)! > 127) return '只能使用英文/数字/符号（OLED 字库为 ASCII）'
  }
  if (v.length > DISPLAY_MAX) return `最多 ${DISPLAY_MAX} 个字符，当前 ${v.length} 个`
  return ''
}
