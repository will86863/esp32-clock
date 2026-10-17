import React from 'react'
import {
  makeStyles,
  tokens,
  Text,
  Card,
  CardHeader,
  Badge,
  Spinner,
} from '@fluentui/react-components'
import {
  Timer24Regular,
  PlugConnected24Regular,
  PlugDisconnected24Regular,
  CalendarToday24Regular,
} from '@fluentui/react-icons'
import { Config, LiveStatus } from '../api'
import { fmtHMS, fmtHuman } from '../util'

const useStyles = makeStyles({
  wrap: { display: 'flex', flexDirection: 'column', gap: '20px' },
  hero: {
    padding: '28px 32px',
    borderRadius: tokens.borderRadiusXLarge,
    background: `linear-gradient(135deg, ${tokens.colorBrandBackground2} 0%, ${tokens.colorNeutralBackground1} 100%)`,
    border: `1px solid ${tokens.colorNeutralStroke2}`,
  },
  heroTop: { display: 'flex', alignItems: 'center', gap: '10px', marginBottom: '6px' },
  big: {
    fontFamily: 'ui-monospace, Consolas, monospace',
    fontSize: '64px',
    fontWeight: 700,
    letterSpacing: '2px',
    lineHeight: 1.05,
    fontVariantNumeric: 'tabular-nums',
    color: tokens.colorNeutralForeground1,
  },
  row: { display: 'flex', gap: '16px', flexWrap: 'wrap' },
  card: { flex: '1 1 240px', minWidth: '240px' },
  cardVal: {
    fontSize: '26px',
    fontWeight: 600,
    fontVariantNumeric: 'tabular-nums',
  },
  muted: { color: tokens.colorNeutralForeground3 },
  dot: {
    width: '8px',
    height: '8px',
    borderRadius: '50%',
    display: 'inline-block',
    marginRight: '6px',
  },
})

export default function OverviewPage({
  live,
  config,
}: {
  live: LiveStatus | null
  config: Config | null
}) {
  const s = useStyles()

  if (!live) {
    return (
      <div style={{ display: 'flex', gap: 12, alignItems: 'center' }}>
        <Spinner size="tiny" /> <Text>正在连接后台…</Text>
      </div>
    )
  }

  const gameCount = config?.games?.length ?? 0

  return (
    <div className={s.wrap}>
      <div className={s.hero}>
        <div className={s.heroTop}>
          <Timer24Regular />
          <Text size={300} weight="semibold" className={s.muted}>
            {live.running ? '正在运行' : '当前没有已添加的程序在运行'}
          </Text>
          {live.running ? (
            <Badge appearance="filled" color="success" size="small">
              RUNNING
            </Badge>
          ) : (
            <Badge appearance="tint" color="subtle" size="small">
              IDLE
            </Badge>
          )}
        </div>

        <div style={{ marginBottom: '4px' }}>
          <Text size={500} weight="semibold">
            {live.running ? live.name : 'NO GAME'}
          </Text>
          {live.running && live.exename ? (
            <Text size={200} className={s.muted} style={{ marginLeft: 10 }}>
              {live.exename}
            </Text>
          ) : null}
        </div>

        <div className={s.big}>{fmtHMS(live.running ? live.up : 0)}</div>

        <Text size={200} className={s.muted}>
          {live.running
            ? 'OLED 正在显示这个程序的本次会话时长'
            : `OLED 显示今日总时长 ${fmtHMS(live.today)}`}
        </Text>
      </div>

      <div className={s.row}>
        <Card className={s.card}>
          <CardHeader
            image={<CalendarToday24Regular />}
            header={<Text weight="semibold">今日总时长</Text>}
            description={
              <Text size={200} className={s.muted}>
                已添加程序的合计
              </Text>
            }
          />
          <div className={s.cardVal}>{fmtHMS(live.today)}</div>
          <Text size={200} className={s.muted}>
            {fmtHuman(live.today)}
          </Text>
        </Card>

        <Card className={s.card}>
          <CardHeader
            image={
              live.esp32ok ? <PlugConnected24Regular /> : <PlugDisconnected24Regular />
            }
            header={<Text weight="semibold">ESP32 连接</Text>}
            description={
              <Text size={200} className={s.muted}>
                {live.target || '未配置'}
              </Text>
            }
          />
          <div style={{ display: 'flex', alignItems: 'center', marginBottom: 8 }}>
            <span
              className={s.dot}
              style={{
                background: live.esp32ok
                  ? tokens.colorPaletteGreenBackground3
                  : tokens.colorPaletteRedBackground3,
              }}
            />
            <Text weight="semibold">{live.esp32ok ? '发送正常' : '发送失败'}</Text>
          </div>
          {live.esp32err ? (
            <Text size={200} style={{ color: tokens.colorPaletteRedForeground1 }}>
              {live.esp32err}
            </Text>
          ) : (
            <Text size={200} className={s.muted}>
              每秒发送一帧 UDP 数据
            </Text>
          )}
        </Card>

        <Card className={s.card}>
          <CardHeader
            image={<Timer24Regular />}
            header={<Text weight="semibold">已添加程序</Text>}
            description={
              <Text size={200} className={s.muted}>
                在「程序」页增删
              </Text>
            }
          />
          <div className={s.cardVal}>{gameCount}</div>
          <Text size={200} className={s.muted}>
            {gameCount === 0 ? '还没有添加任何程序' : '显示最后启动的那个'}
          </Text>
        </Card>
      </div>
    </div>
  )
}
