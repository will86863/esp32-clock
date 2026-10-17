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
    <FluentProvider theme={dark ? webDarkTheme : webLightTheme} className={styles.root}>
      <App dark={dark} onToggleDark={() => setDark((d) => !d)} />
    </FluentProvider>
  )
}

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <Root />
  </React.StrictMode>,
)
