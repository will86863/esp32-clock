import React from 'react'
import {
  makeStyles,
  tokens,
  Text,
  Card,
  Button,
  Input,
  Field,
  Switch,
  Divider,
  SpinButton,
} from '@fluentui/react-components'
import {
  Save24Regular,
  FolderOpen24Regular,
  PlugConnected24Regular,
  ArrowExit24Regular,
} from '@fluentui/react-icons'
import { App as api, Config } from '../api'

const useStyles = makeStyles({
  wrap: { display: 'flex', flexDirection: 'column', gap: '18px', maxWidth: '760px' },
  card: { padding: '18px 20px', display: 'flex', flexDirection: 'column', gap: '12px' },
  row: { display: 'flex', gap: '14px', flexWrap: 'wrap' },
  field: { flex: '1 1 200px', minWidth: '160px' },
  actions: { display: 'flex', gap: '8px', alignItems: 'center', marginTop: '4px' },
  muted: { color: tokens.colorNeutralForeground3 },
})

export default function SettingsPage({
  config,
  onChanged,
  notify,
}: {
  config: Config | null
  onChanged: () => Promise<void> | void
  notify: (t: string, b: string, i?: 'success' | 'error' | 'info') => void
}) {
  const s = useStyles()
  const [ip, setIp] = React.useState('')
  const [port, setPort] = React.useState(5005)
  const [interval, setIntervalSec] = React.useState(1)
  const [autostart, setAutostart] = React.useState(false)
  const [startHidden, setStartHidden] = React.useState(true)
  const [saving, setSaving] = React.useState(false)

  React.useEffect(() => {
    if (!config) return
    setIp(config.esp32_ip)
    setPort(config.esp32_port)
    setIntervalSec(config.interval_sec)
    setAutostart(config.autostart)
    setStartHidden(config.start_hidden)
  }, [config])

  // 以注册表为准校正自启开关
  React.useEffect(() => {
    api
      .autostartState()
      .then((v) => setAutostart(v))
      .catch(() => {})
  }, [])

  const save = async () => {
    if (!config) return
    setSaving(true)
    try {
      await api.saveConfig({
        ...config,
        esp32_ip: ip.trim(),
        esp32_port: port || 5005,
        interval_sec: interval || 1,
        autostart,
        start_hidden: startHidden,
      })
      const real = await api.setAutostart(autostart)
      setAutostart(real)
      await onChanged()
      notify('已保存', '设置立即生效', 'success')
    } catch (e: any) {
      notify('保存失败', String(e?.message ?? e), 'error')
    } finally {
      setSaving(false)
    }
  }

  return (
    <div className={s.wrap}>
      <div>
        <Text size={500} weight="semibold" as="h2">
          设置
        </Text>
        <div>
          <Text size={200} className={s.muted}>
            ESP32 与启动行为
          </Text>
        </div>
      </div>

      <Card className={s.card}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <PlugConnected24Regular />
          <Text weight="semibold">ESP32 连接</Text>
        </div>
        <Text size={200} className={s.muted}>
          固件监听 UDP 5005。IP 填屏幕显示的固定地址（默认 192.168.5.86）。
        </Text>
        <div className={s.row}>
          <Field label="IP 地址" className={s.field}>
            <Input value={ip} onChange={(_, d) => setIp(d.value)} placeholder="192.168.5.86" />
          </Field>
          <Field label="端口" className={s.field}>
            <SpinButton
              value={port}
              min={1}
              max={65535}
              onChange={(_, d) => {
                const v = Number(d.value)
                if (Number.isFinite(v)) setPort(v)
              }}
            />
          </Field>
          <Field label="发送间隔（秒）" className={s.field}>
            <SpinButton
              value={interval}
              min={1}
              max={60}
              onChange={(_, d) => {
                const v = Number(d.value)
                if (Number.isFinite(v)) setIntervalSec(v)
              }}
            />
          </Field>
        </div>
        <Text size={100} className={s.muted}>
          固件超过 5 秒收不到数据会显示 NO GAME，所以间隔不建议超过 3 秒。
        </Text>
      </Card>

      <Card className={s.card}>
        <Text weight="semibold">启动行为</Text>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <div>
            <Text>开机自动启动</Text>
            <div>
              <Text size={200} className={s.muted}>
                写入 HKCU\...\Run，登录 Windows 后自动运行
              </Text>
            </div>
          </div>
          <Switch checked={autostart} onChange={(_, d) => setAutostart(d.checked)} />
        </div>
        <Divider />
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <div>
            <Text>开机启动时最小化到托盘</Text>
            <div>
              <Text size={200} className={s.muted}>
                不弹出窗口，只留托盘图标
              </Text>
            </div>
          </div>
          <Switch checked={startHidden} onChange={(_, d) => setStartHidden(d.checked)} />
        </div>
      </Card>

      <Card className={s.card}>
        <Text weight="semibold">数据与维护</Text>
        <Text size={200} className={s.muted}>
          配置与统计保存在 %APPDATA%\GameTime\
        </Text>
        <div className={s.actions}>
          <Button icon={<FolderOpen24Regular />} onClick={() => api.openDataDir()}>
            打开数据目录
          </Button>
          <Button icon={<ArrowExit24Regular />} onClick={() => api.quit()}>
            退出程序
          </Button>
        </div>
      </Card>

      <div className={s.actions}>
        <Button appearance="primary" icon={<Save24Regular />} disabled={saving} onClick={save}>
          {saving ? '保存中…' : '保存设置'}
        </Button>
      </div>
    </div>
  )
}
