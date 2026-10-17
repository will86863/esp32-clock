import React from 'react'
import {
  makeStyles,
  tokens,
  Text,
  Button,
  Badge,
  Toaster,
  useToastController,
  useId,
  Toast,
  ToastTitle,
  ToastBody,
} from '@fluentui/react-components'
import {
  Home24Regular,
  AppsList24Regular,
  DataBarVertical24Regular,
  WeatherMoon24Regular,
  WeatherSunny24Regular,
  Settings24Regular,
} from '@fluentui/react-icons'
import {
  NavDrawer,
  NavDrawerBody,
  NavDrawerHeader,
  NavItem,
  NavSectionHeader,
} from '@fluentui/react-nav'

import { App as api, Config, LiveStatus } from './api'
import OverviewPage from './pages/Overview'
import GamesPage from './pages/Games'
import StatsPage from './pages/Stats'
import SettingsPage from './pages/Settings'

export type PageKey = 'overview' | 'games' | 'stats' | 'settings'

const useStyles = makeStyles({
  root: {
    display: 'flex',
    height: '100vh',
    overflow: 'hidden',
  },
  nav: {
    width: '232px',
    flexShrink: 0,
  },
  navHeader: {
    display: 'flex',
    alignItems: 'center',
    gap: '10px',
    padding: '16px 20px 12px',
  },
  logo: {
    width: '28px',
    height: '28px',
    borderRadius: '8px',
    flexShrink: 0,
  },
  brand: {
    display: 'flex',
    flexDirection: 'column',
    lineHeight: 1.15,
  },
  main: {
    flex: 1,
    minWidth: 0,
    display: 'flex',
    flexDirection: 'column',
    overflow: 'hidden',
    backgroundColor: tokens.colorNeutralBackground1,
  },
  content: {
    flex: 1,
    overflowY: 'auto',
    padding: '28px 32px 40px',
  },
  footer: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
    padding: '8px 12px',
    borderTop: `1px solid ${tokens.colorNeutralStroke2}`,
  },
})

export default function App({
  dark,
  onToggleDark,
}: {
  dark: boolean
  onToggleDark: () => void
}) {
  const styles = useStyles()
  const [page, setPage] = React.useState<PageKey>('overview')
  const [config, setConfig] = React.useState<Config | null>(null)
  const [live, setLive] = React.useState<LiveStatus | null>(null)
  const [loadError, setLoadError] = React.useState('')

  const toasterId = useId('toaster')
  const { dispatchToast } = useToastController(toasterId)

  const notify = React.useCallback(
    (title: string, body: string, intent: 'success' | 'error' | 'info' = 'info') => {
      dispatchToast(
        <Toast>
          <ToastTitle>{title}</ToastTitle>
          {body ? <ToastBody>{body}</ToastBody> : null}
        </Toast>,
        { intent, timeout: intent === 'error' ? 8000 : 3000 },
      )
    },
    [dispatchToast],
  )

  // 首次加载配置
  const reloadConfig = React.useCallback(async () => {
    try {
      const c = await api.getConfig()
      setConfig(c)
      setLoadError('')
    } catch (e: any) {
      setLoadError(String(e?.message ?? e))
    }
  }, [])

  React.useEffect(() => {
    reloadConfig()
  }, [reloadConfig])

  // 每秒轮询实时状态
  React.useEffect(() => {
    let alive = true
    const tick = async () => {
      try {
        const s = await api.getStatus()
        if (alive) setLive(s)
      } catch {
        /* 绑定未就绪时忽略 */
      }
    }
    tick()
    const t = window.setInterval(tick, 1000)
    return () => {
      alive = false
      window.clearInterval(t)
    }
  }, [])

  const { openNav, onNavToggle } = { openNav: true, onNavToggle: () => {} }

  // 切页：先清理上一页可能残留的弹窗遮罩，再切。
  //
  // 背景：Fluent 的 Dialog 会在 document.body 下渲染遮罩层（.fui-DialogSurface__backdrop
  // 或 role=dialog 的包裹节点）。如果切页时上一页正开着弹窗，组件被卸载后遮罩会
  // 留在 DOM 里 —— 表现就是切到的新页面一片空白、完全点不动。
  //
  // 之前试过「四个页面常驻挂载」来避免卸载，但实测那会让 WebView2 整体不绘制（白屏），
  // 所以仍然用条件渲染，改在切页这一处主动清理残留。
  const switchPage = React.useCallback((next: PageKey) => {
    // 先切页（卸载旧页），再在下一帧清残留 —— 顺序反了会清掉新页自己的弹窗
    setPage(next)
    requestAnimationFrame(() => {
      requestAnimationFrame(() => {
        document
          .querySelectorAll('.fui-DialogSurface, .fui-DialogSurface__backdrop')
          .forEach((el) => {
            // 只删 body 直属的浮层，页面内的正常内容不动
            if (el.parentElement === document.body) el.remove()
          })
        // Dialog 打开时会锁滚动/禁用指针事件，卸载后可能没复原
        document.body.style.removeProperty('overflow')
        document.body.style.removeProperty('pointer-events')
        document.body.style.removeProperty('padding-right')
        document.documentElement.style.removeProperty('overflow')
      })
    })
  }, [])

  return (
    <div className={styles.root}>
      <NavDrawer
        open={true}
        type="inline"
        selectedValue={page}
        onNavItemSelect={(_, data) =>
          switchPage((data.value as PageKey) ?? 'overview')
        }
        className={styles.nav}
      >
        <NavDrawerHeader>
          <div className={styles.navHeader}>
            <img className={styles.logo} src="/icon.png" alt="" />
            <div className={styles.brand}>
              <Text weight="semibold" size={300}>
                GameTime
              </Text>
              <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
                游戏时长监控
              </Text>
            </div>
          </div>
        </NavDrawerHeader>

        <NavDrawerBody>
          <NavSectionHeader>监控</NavSectionHeader>
          <NavItem value="overview" icon={<Home24Regular />}>
            概览
          </NavItem>
          <NavItem value="games" icon={<AppsList24Regular />}>
            程序
          </NavItem>
          <NavSectionHeader>数据</NavSectionHeader>
          <NavItem value="stats" icon={<DataBarVertical24Regular />}>
            统计
          </NavItem>
          <NavSectionHeader>系统</NavSectionHeader>
          <NavItem value="settings" icon={<Settings24Regular />}>
            设置
          </NavItem>
        </NavDrawerBody>

        <div className={styles.footer}>
          <Button
            appearance="subtle"
            icon={dark ? <WeatherSunny24Regular /> : <WeatherMoon24Regular />}
            onClick={onToggleDark}
            title="切换深色/浅色"
          />
          {live?.version ? (
            <Badge appearance="tint" color="informative" size="small">
              v{live.version}
            </Badge>
          ) : null}
        </div>
      </NavDrawer>

      <div className={styles.main}>
        <div className={styles.content}>
          {loadError ? (
            <div style={{ color: tokens.colorPaletteRedForeground1 }}>
              <Text weight="semibold">无法加载配置</Text>
              <div>{loadError}</div>
            </div>
          ) : null}

          {page === 'overview' && <OverviewPage live={live} config={config} />}
          {page === 'games' && (
            <GamesPage
              config={config}
              live={live}
              onChanged={reloadConfig}
              notify={notify}
            />
          )}
          {page === 'stats' && <StatsPage live={live} />}
          {page === 'settings' && (
            <SettingsPage config={config} onChanged={reloadConfig} notify={notify} />
          )}
        </div>
      </div>

      <Toaster toasterId={toasterId} position="bottom-end" />
    </div>
  )
}
