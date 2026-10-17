import React from 'react'
import {
  makeStyles,
  tokens,
  Text,
  Card,
  Button,
  Spinner,
  Badge,
  Table,
  TableHeader,
  TableRow,
  TableHeaderCell,
  TableBody,
  TableCell,
} from '@fluentui/react-components'
import {
  DataBarVertical24Regular,
  ArrowSync24Regular,
  Timer24Regular,
} from '@fluentui/react-icons'
import { App as api, LiveStatus, StatsResult } from '../api'
import { fmtHMS, fmtHuman, fmtDay, fmtClock } from '../util'

const useStyles = makeStyles({
  wrap: { display: 'flex', flexDirection: 'column', gap: '18px' },
  head: { display: 'flex', alignItems: 'center', justifyContent: 'space-between' },
  actions: { display: 'flex', gap: '8px', alignItems: 'center' },
  chartCard: { padding: '16px 18px' },
  chartTitle: { marginBottom: '12px', display: 'flex', alignItems: 'center', gap: '8px' },
  empty: {
    padding: '40px 20px',
    textAlign: 'center',
    color: tokens.colorNeutralForeground3,
  },
  barLabel: {
    fontSize: '12px',
    fill: tokens.colorNeutralForeground2,
  },
})

export default function StatsPage({ live }: { live: LiveStatus | null }) {
  const s = useStyles()
  const [days, setDays] = React.useState(7)
  const [stats, setStats] = React.useState<StatsResult | null>(null)
  const [loading, setLoading] = React.useState(true)

  const load = React.useCallback(async () => {
    setLoading(true)
    try {
      const r = await api.getStats(days)
      setStats(r)
    } catch {
      /* 忽略 */
    } finally {
      setLoading(false)
    }
  }, [days])

  React.useEffect(() => {
    load()
  }, [load])

  // 今日有正在进行的会话时，图表里把它算进去（实时感更好）
  const liveExtra = live?.running ? live.up : 0

  return (
    <div className={s.wrap}>
      <div className={s.head}>
        <div>
          <Text size={500} weight="semibold" as="h2">
            时长统计
          </Text>
          <div>
            <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
              只统计已添加程序的运行时长，不记录空闲时间
            </Text>
          </div>
        </div>
        <div className={s.actions}>
          <Button
            appearance={days === 7 ? 'primary' : 'secondary'}
            size="small"
            onClick={() => setDays(7)}
          >
            近 7 天
          </Button>
          <Button
            appearance={days === 30 ? 'primary' : 'secondary'}
            size="small"
            onClick={() => setDays(30)}
          >
            近 30 天
          </Button>
          <Button size="small" icon={<ArrowSync24Regular />} onClick={load} title="刷新" />
        </div>
      </div>

      {loading && !stats ? (
        <div style={{ display: 'flex', gap: 12, alignItems: 'center', padding: 24 }}>
          <Spinner size="tiny" /> <Text>正在加载…</Text>
        </div>
      ) : null}

      {stats ? (
        <>
          {/* 今日汇总 */}
          <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap' }}>
            <Card style={{ flex: '1 1 220px' }}>
              <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
                今日总时长
              </Text>
              <Text size={700} weight="semibold" style={{ fontVariantNumeric: 'tabular-nums' }}>
                {fmtHMS(stats.today_total + liveExtra)}
              </Text>
              <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
                {fmtHuman(stats.today_total + liveExtra)}
              </Text>
            </Card>
            <Card style={{ flex: '1 1 220px' }}>
              <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
                今日活跃程序
              </Text>
              <Text size={700} weight="semibold">
                {stats.today.length}
              </Text>
              <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
                {stats.today.length > 0 ? `最久：${stats.today[0].display}` : '今天还没玩'}
              </Text>
            </Card>
            <Card style={{ flex: '1 1 220px' }}>
              <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
                当前状态
              </Text>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginTop: 4 }}>
                <Text size={600} weight="semibold">
                  {live?.running ? live.name : '空闲'}
                </Text>
                {live?.running ? (
                  <Badge appearance="filled" color="success" size="small">
                    {fmtHMS(live.up)}
                  </Badge>
                ) : null}
              </div>
            </Card>
          </div>

          {/* 今日各程序柱状图 */}
          <Card className={s.chartCard}>
            <div className={s.chartTitle}>
              <DataBarVertical24Regular />
              <Text weight="semibold">今日各程序时长</Text>
            </div>
            {stats.today.length === 0 ? (
              <div className={s.empty}>
                <Text>今天还没有记录</Text>
              </div>
            ) : (
              <TodayBars items={stats.today} live={live} />
            )}
          </Card>

          {/* 近 N 天折线图 */}
          <Card className={s.chartCard}>
            <div className={s.chartTitle}>
              <Timer24Regular />
              <Text weight="semibold">近 {days} 天趋势</Text>
            </div>
            <DayTrend items={stats.days} />
          </Card>

          {/* 最近会话 */}
          <Card className={s.chartCard}>
            <div className={s.chartTitle}>
              <Text weight="semibold">最近会话</Text>
            </div>
            {stats.recent.length === 0 ? (
              <div className={s.empty}>
                <Text>还没有会话记录</Text>
              </div>
            ) : (
              <Table size="small">
                <TableHeader>
                  <TableRow>
                    <TableHeaderCell>程序</TableHeaderCell>
                    <TableHeaderCell>开始</TableHeaderCell>
                    <TableHeaderCell>时长</TableHeaderCell>
                    <TableHeaderCell>进程</TableHeaderCell>
                  </TableRow>
                </TableHeader>
                <TableBody>
                  {stats.recent.map((r, i) => (
                    <TableRow key={`${r.start}-${i}`}>
                      <TableCell>
                        <Text weight="semibold">{r.display || r.process}</Text>
                      </TableCell>
                      <TableCell>
                        <Text size={200}>{fmtClock(r.start)}</Text>
                      </TableCell>
                      <TableCell>
                        <Text size={200} style={{ fontVariantNumeric: 'tabular-nums' }}>
                          {fmtHMS(r.dur)}
                        </Text>
                      </TableCell>
                      <TableCell>
                        <Text size={100} style={{ color: tokens.colorNeutralForeground3 }}>
                          {r.process}
                        </Text>
                      </TableCell>
                    </TableRow>
                  ))}
                </TableBody>
              </Table>
            )}
          </Card>
        </>
      ) : null}
    </div>
  )
}

// ============================================================
// 今日各程序横向柱状图（手写 SVG）
// 为什么不用 @fluentui/react-charting：它的 peer 依赖锁在 @fluentui/react v8，
// 与 v9 是两套体系，硬凑会让包体积和样式双双失控。
// ============================================================

function TodayBars({
  items,
  live,
}: {
  items: { game_id: string; display: string; process: string; total: number; runs: number }[]
  live: LiveStatus | null
}) {
  const s = useStyles()
  const [w, setW] = React.useState(640)
  const ref = React.useRef<HTMLDivElement>(null)

  React.useEffect(() => {
    const el = ref.current
    if (!el) return
    const ro = new ResizeObserver(() => setW(el.clientWidth))
    ro.observe(el)
    setW(el.clientWidth)
    return () => ro.disconnect()
  }, [])

  // 把正在进行的会话实时加进对应条
  const merged = items.map((it) => ({
    ...it,
    total: it.total + (live?.running && live.name === it.display ? live.up : 0),
  }))
  const max = Math.max(...merged.map((m) => m.total), 1)

  const labelW = 108
  const valueW = 78
  const rowH = 30
  const gap = 8
  const barH = 18
  const h = Math.max(merged.length * (rowH + gap), 40)

  return (
    <div ref={ref} style={{ width: '100%' }}>
      <svg width={w} height={h} role="img" aria-label="今日各程序时长">
        {merged.map((m, i) => {
          const y = i * (rowH + gap)
          const barMaxW = Math.max(20, w - labelW - valueW)
          const bw = Math.max(2, (m.total / max) * barMaxW)
          const isLive = live?.running && live.name === m.display
          return (
            <g key={m.game_id || m.process}>
              <text x={0} y={y + barH - 3} className={s.barLabel}>
                {m.display.length > 14 ? m.display.slice(0, 13) + '…' : m.display}
              </text>
              <rect
                x={labelW}
                y={y}
                width={barMaxW}
                height={barH}
                rx={4}
                fill={tokens.colorNeutralBackground3}
              />
              <rect
                x={labelW}
                y={y}
                width={bw}
                height={barH}
                rx={4}
                fill={isLive ? tokens.colorBrandBackground2Pressed : tokens.colorBrandBackground}
              />
              <text
                x={labelW + bw + 8}
                y={y + barH - 3}
                className={s.barLabel}
                style={{ fontVariantNumeric: 'tabular-nums' }}
              >
                {fmtHMS(m.total)}
              </text>
            </g>
          )
        })}
      </svg>
    </div>
  )
}

// ============================================================
// 近 N 天折线图（手写 SVG）
// ============================================================

function DayTrend({ items }: { items: { day: string; total: number }[] }) {
  const [w, setW] = React.useState(640)
  const ref = React.useRef<HTMLDivElement>(null)

  React.useEffect(() => {
    const el = ref.current
    if (!el) return
    const ro = new ResizeObserver(() => setW(el.clientWidth))
    ro.observe(el)
    setW(el.clientWidth)
    return () => ro.disconnect()
  }, [])

  const padL = 8
  const padR = 8
  const padT = 14
  const padB = 26
  const h = 200
  const innerW = Math.max(20, w - padL - padR)
  const innerH = h - padT - padB

  const max = Math.max(...items.map((i) => i.total), 1)
  const n = items.length
  const stepX = n > 1 ? innerW / (n - 1) : 0

  const pts = items.map((it, i) => {
    const x = padL + i * stepX
    const y = padT + innerH - (it.total / max) * innerH
    return { x, y, ...it }
  })

  const line = pts.map((p, i) => `${i === 0 ? 'M' : 'L'}${p.x.toFixed(1)},${p.y.toFixed(1)}`).join(' ')
  const area =
    pts.length > 0
      ? `${line} L${pts[pts.length - 1].x.toFixed(1)},${padT + innerH} L${pts[0].x.toFixed(1)},${
          padT + innerH
        } Z`
      : ''

  // 横轴标签：30 天时隔几个显示，避免重叠
  const labelEvery = n > 14 ? Math.ceil(n / 8) : n > 7 ? 2 : 1
  const hours = (v: number) => (v / 3600).toFixed(v >= 3600 ? 0 : 1)

  return (
    <div ref={ref} style={{ width: '100%' }}>
      <svg width={w} height={h} role="img" aria-label="近几天时长趋势">
        {/* 网格线 */}
        {[0, 0.5, 1].map((r) => (
          <line
            key={r}
            x1={padL}
            x2={w - padR}
            y1={padT + innerH * r}
            y2={padT + innerH * r}
            stroke={tokens.colorNeutralStroke2}
            strokeDasharray={r === 1 ? undefined : '3 3'}
          />
        ))}
        {/* 面积 */}
        {area ? <path d={area} fill={tokens.colorBrandBackground2} opacity={0.5} /> : null}
        {/* 折线 */}
        {line ? (
          <path d={line} fill="none" stroke={tokens.colorBrandBackground} strokeWidth={2} />
        ) : null}
        {/* 数据点 + 提示 */}
        {pts.map((p, i) => (
          <g key={p.day}>
            <circle cx={p.x} cy={p.y} r={3} fill={tokens.colorBrandBackground}>
              <title>
                {p.day} · {fmtHMS(p.total)}
              </title>
            </circle>
            {i % labelEvery === 0 || i === n - 1 ? (
              <text
                x={p.x}
                y={h - 8}
                textAnchor="middle"
                fontSize={11}
                fill={tokens.colorNeutralForeground3}
              >
                {fmtDay(p.day)}
              </text>
            ) : null}
          </g>
        ))}
        {/* 纵轴最大刻度 */}
        <text x={padL} y={padT - 3} fontSize={11} fill={tokens.colorNeutralForeground3}>
          {hours(max)}h
        </text>
      </svg>
    </div>
  )
}
