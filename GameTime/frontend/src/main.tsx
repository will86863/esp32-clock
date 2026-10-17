import React from 'react'
import ReactDOM from 'react-dom/client'
import {
  FluentProvider,
  webLightTheme,
  webDarkTheme,
  makeStyles,
  tokens,
} from '@fluentui/react-components'
import App from './App'
import './styles.css'

const useStyles = makeStyles({
  root: {
    height: '100vh',
    // 注意：背景色不能放在 FluentProvider 上。
    // Fluent 的 Dialog 会复用 FluentProvider 作为 Portal 容器，
    // 若 Provider 自带不透明背景，弹窗打开时会生成一个
    // z-index:1000000 的全屏不透明层，把整个页面盖住
    // （表现为"弹窗时其它页面白屏，关掉就恢复"）。
    // 所以背景挂在这个外层 div 上，Provider 保持透明。
  },
  shell: {
    height: '100vh',
    backgroundColor: tokens.colorNeutralBackground2,
  },
})

function Root() {
  const styles = useStyles()
  const [dark, setDark] = React.useState(
    window.matchMedia?.('(prefers-color-scheme: dark)').matches ?? false,
  )

  React.useEffect(() => {
    const mq = window.matchMedia('(prefers-color-scheme: dark)')
    const onChange = (e: MediaQueryListEvent) => setDark(e.matches)
    mq.addEventListener('change', onChange)
    return () => mq.removeEventListener('change', onChange)
  }, [])

  return (
    <FluentProvider theme={dark ? webDarkTheme : webLightTheme}>
      <div className={styles.shell}>
        <App dark={dark} onToggleDark={() => setDark((d) => !d)} />
      </div>
    </FluentProvider>
  )
}

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <Root />
  </React.StrictMode>,
)
