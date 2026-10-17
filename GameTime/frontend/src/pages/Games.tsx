import React from 'react'
import {
  makeStyles,
  tokens,
  Text,
  Button,
  Input,
  Field,
  Dialog,
  DialogSurface,
  DialogTitle,
  DialogBody,
  DialogContent,
  DialogActions,
  Table,
  TableHeader,
  TableRow,
  TableHeaderCell,
  TableBody,
  TableCell,
  TableCellLayout,
  Badge,
  Spinner,
  Divider,
  Tooltip,
} from '@fluentui/react-components'
import {
  Add24Regular,
  FolderOpen24Regular,
  Delete24Regular,
  Edit24Regular,
  Checkmark24Regular,
  Dismiss24Regular,
  Open24Regular,
} from '@fluentui/react-icons'
import { App as api, Config, Game, LiveStatus, ResolveResult, ExeCandidate } from '../api'
import { checkDisplay, DISPLAY_MAX } from '../util'

const useStyles = makeStyles({
  wrap: { display: 'flex', flexDirection: 'column', gap: '16px' },
  head: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: '12px',
  },
  actions: { display: 'flex', gap: '8px', alignItems: 'center' },
  dropZone: {
    border: `2px dashed ${tokens.colorNeutralStroke1}`,
    borderRadius: tokens.borderRadiusXLarge,
    padding: '28px 20px',
    textAlign: 'center',
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    gap: '10px',
    transition: 'all .15s ease',
    backgroundColor: tokens.colorNeutralBackground2,
  },
  dropZoneActive: {
    border: `2px dashed ${tokens.colorBrandStroke1}`,
    backgroundColor: tokens.colorBrandBackground2,
  },
  nameCell: { display: 'flex', alignItems: 'center', gap: '10px' },
  path: {
    fontSize: '12px',
    color: tokens.colorNeutralForeground3,
    wordBreak: 'break-all',
    maxWidth: '520px',
  },
  candRow: {
    display: 'flex',
    alignItems: 'center',
    gap: '12px',
    padding: '10px 12px',
    borderRadius: tokens.borderRadiusMedium,
    border: `1px solid ${tokens.colorNeutralStroke2}`,
    cursor: 'pointer',
    marginBottom: '8px',
  },
  candRowSel: {
    border: `2px solid ${tokens.colorBrandStroke1}`,
    backgroundColor: tokens.colorBrandBackground2,
  },
  candIcon: { width: '32px', height: '32px', flexShrink: 0 },
  candMeta: { flex: 1, minWidth: 0 },
  candName: { fontWeight: 600, wordBreak: 'break-all' },
  empty: {
    padding: '48px 20px',
    textAlign: 'center',
    color: tokens.colorNeutralForeground3,
  },
})

type AddState = {
  open: boolean
  loading: boolean
  result: ResolveResult | null
  exe: string
  display: string
  steamName: string
  appid: string
  source: string
  error: string
}

const emptyAdd: AddState = {
  open: false,
  loading: false,
  result: null,
  exe: '',
  display: '',
  steamName: '',
  appid: '',
  source: '',
  error: '',
}

export default function GamesPage({
  config,
  live,
  onChanged,
  notify,
}: {
  config: Config | null
  live: LiveStatus | null
  onChanged: () => Promise<void> | void
  notify: (t: string, b: string, i?: 'success' | 'error' | 'info') => void
}) {
  const s = useStyles()
  const [add, setAdd] = React.useState<AddState>(emptyAdd)
  const [dragOver, setDragOver] = React.useState(false)
  const [editing, setEditing] = React.useState<string | null>(null)
  const [editVal, setEditVal] = React.useState('')
  const [busy, setBusy] = React.useState('')

  const games = config?.games ?? []

  // ---------- 添加：文件对话框 ----------
  const pickFile = async () => {
    setAdd({ ...emptyAdd, open: true, loading: true })
    try {
      const r = await api.pickFile()
      if (!r || (!r.error && !r.exe && !r.steam)) {
        setAdd(emptyAdd) // 用户取消
        return
      }
      applyResolve(r)
    } catch (e: any) {
      setAdd({ ...emptyAdd, open: true, error: String(e?.message ?? e) })
    }
  }

  const resolveAndOpen = React.useCallback(async (path: string) => {
    setAdd({ ...emptyAdd, open: true, loading: true })
    try {
      const r = await api.resolvePath(path)
      applyResolve(r)
    } catch (e: any) {
      setAdd({ ...emptyAdd, open: true, error: String(e?.message ?? e) })
    }
  }, [])

  const applyResolve = (r: ResolveResult) => {
    const cands = r.candidates ?? []
    let exe = r.exe || ''
    if (!exe && cands.length === 1) exe = cands[0].path
    setAdd({
      open: true,
      loading: false,
      result: r,
      exe,
      display: r.suggested || '',
      steamName: r.steam?.name || '',
      appid: r.steam?.appid || '',
      source: r.steam ? 'steam' : r.kind,
      error: r.error || '',
    })
  }

  // ---------- 拖放 ----------
  // Wails v2 的 file-drop 只能从 Go 侧拿到真实绝对路径（浏览器拿不到）。
  // Go 侧收到后暂存，这里轮询 TakeDropPath 取走。
  React.useEffect(() => {
    let alive = true

    const pollDrop = async () => {
      if (!alive) return
      try {
        const p = await api.takeDropPath()
        if (p) await resolveAndOpen(p)
      } catch {
        /* 绑定未就绪时忽略 */
      }
    }

    const onDragOver = (e: DragEvent) => {
      e.preventDefault()
      if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy'
      setDragOver(true)
    }
    const onDragLeave = (e: DragEvent) => {
      if (e.relatedTarget === null) setDragOver(false)
    }
    const onDrop = (e: DragEvent) => {
      // 必须阻止默认行为，否则 WebView 会尝试「打开」被拖入的文件
      e.preventDefault()
      setDragOver(false)
      // 路径由 Go 侧异步送达，稍后轮询取走
      window.setTimeout(pollDrop, 120)
    }

    window.addEventListener('dragover', onDragOver)
    window.addEventListener('dragleave', onDragLeave)
    window.addEventListener('drop', onDrop, true) // 捕获阶段抢在默认行为之前

    const timer = window.setInterval(pollDrop, 700)
    return () => {
      alive = false
      window.clearInterval(timer)
      window.removeEventListener('dragover', onDragOver)
      window.removeEventListener('dragleave', onDragLeave)
      window.removeEventListener('drop', onDrop, true)
    }
  }, [resolveAndOpen])

  // ---------- 提交添加 ----------
  const submitAdd = async () => {
    if (!add.exe) {
      setAdd((a) => ({ ...a, error: '请先选择具体的主程序 exe' }))
      return
    }
    const derr = checkDisplay(add.display)
    if (derr) {
      setAdd((a) => ({ ...a, error: derr }))
      return
    }
    setBusy('add')
    try {
      await api.addGame({
        exe: add.exe,
        display: add.display.trim(),
        appid: add.appid,
        source: add.source || 'exe',
      })
      setAdd(emptyAdd)
      await onChanged()
      notify('已添加', add.display.trim(), 'success')
    } catch (e: any) {
      setAdd((a) => ({ ...a, error: String(e?.message ?? e) }))
    } finally {
      setBusy('')
    }
  }

  // ---------- 改名 ----------
  const startEdit = (g: Game) => {
    setEditing(g.id)
    setEditVal(g.display)
  }

  const saveEdit = async (g: Game) => {
    const err = checkDisplay(editVal)
    if (err) {
      notify('显示名不合法', err, 'error')
      return
    }
    setBusy(g.id)
    try {
      await api.updateGame({ ...g, display: editVal.trim() })
      setEditing(null)
      await onChanged()
      notify('已保存', editVal.trim(), 'success')
    } catch (e: any) {
      notify('保存失败', String(e?.message ?? e), 'error')
    } finally {
      setBusy('')
    }
  }

  const del = async (g: Game) => {
    setBusy(g.id)
    try {
      await api.deleteGame(g.id)
      await onChanged()
      notify('已移除', `${g.display}（历史统计保留）`, 'info')
    } catch (e: any) {
      notify('移除失败', String(e?.message ?? e), 'error')
    } finally {
      setBusy('')
    }
  }

  const runningId = React.useMemo(() => {
    if (!live?.running) return ''
    const hit = games.find((g) => g.display === live.name)
    return hit?.id ?? ''
  }, [live, games])

  const cands: ExeCandidate[] = add.result?.candidates ?? []

  return (
    <div className={s.wrap}>
      <div className={s.head}>
        <div>
          <Text size={500} weight="semibold" as="h2">
            监控的程序
          </Text>
          <div>
            <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
              OLED 显示「最后启动」的那个程序；没有程序运行时显示今日总时长
            </Text>
          </div>
        </div>
        <div className={s.actions}>
          <Button appearance="primary" icon={<FolderOpen24Regular />} onClick={pickFile}>
            选择程序…
          </Button>
        </div>
      </div>

      {/* 拖放区 */}
      <div className={`${s.dropZone} ${dragOver ? s.dropZoneActive : ''}`}>
        <Add24Regular />
        <Text weight="semibold">把程序或快捷方式拖到这里</Text>
        <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
          支持 .exe、桌面快捷方式 .lnk、Steam 游戏快捷方式 .url
        </Text>
      </div>

      {/* 列表 */}
      {games.length === 0 ? (
        <div className={s.empty}>
          <Text size={400}>还没有添加任何程序</Text>
          <div style={{ marginTop: 8 }}>
            <Text size={200}>点「选择程序…」或直接把桌面快捷方式拖到上面</Text>
          </div>
        </div>
      ) : (
        <Table size="small">
          <TableHeader>
            <TableRow>
              <TableHeaderCell style={{ width: '26%' }}>显示名（OLED）</TableHeaderCell>
              <TableHeaderCell style={{ width: '52%' }}>程序路径</TableHeaderCell>
              <TableHeaderCell style={{ width: '10%' }}>来源</TableHeaderCell>
              <TableHeaderCell style={{ width: '12%' }}>操作</TableHeaderCell>
            </TableRow>
          </TableHeader>
          <TableBody>
            {games.map((g) => (
              <TableRow key={g.id}>
                <TableCell>
                  <TableCellLayout>
                    <div className={s.nameCell}>
                      {editing === g.id ? (
                        <>
                          <Field
                            validationState={checkDisplay(editVal) ? 'error' : 'none'}
                            validationMessage={checkDisplay(editVal) || undefined}
                            style={{ width: 200 }}
                          >
                            <Input
                              value={editVal}
                              autoFocus
                              maxLength={DISPLAY_MAX + 8}
                              contentAfter={
                                <Text size={100} style={{ color: tokens.colorNeutralForeground3 }}>
                                  {editVal.length}/{DISPLAY_MAX}
                                </Text>
                              }
                              onChange={(_, d) => setEditVal(d.value)}
                              onKeyDown={(e) => {
                                if (e.key === 'Enter') saveEdit(g)
                                if (e.key === 'Escape') setEditing(null)
                              }}
                            />
                          </Field>
                          <Button
                            size="small"
                            appearance="subtle"
                            icon={<Checkmark24Regular />}
                            onClick={() => saveEdit(g)}
                          />
                          <Button
                            size="small"
                            appearance="subtle"
                            icon={<Dismiss24Regular />}
                            onClick={() => setEditing(null)}
                          />
                        </>
                      ) : (
                        <>
                          <Text weight="semibold" size={400}>
                            {g.display}
                          </Text>
                          {runningId === g.id ? (
                            <Badge appearance="filled" color="success" size="small">
                              运行中
                            </Badge>
                          ) : null}
                          <Tooltip content="修改显示名" relationship="label">
                            <Button
                              size="small"
                              appearance="subtle"
                              icon={<Edit24Regular />}
                              onClick={() => startEdit(g)}
                            />
                          </Tooltip>
                        </>
                      )}
                    </div>
                  </TableCellLayout>
                </TableCell>
                <TableCell>
                  <TableCellLayout>
                    <div className={s.path}>{g.exe}</div>
                    <Text size={100} style={{ color: tokens.colorNeutralForeground3 }}>
                      进程 {g.process}
                    </Text>
                  </TableCellLayout>
                </TableCell>
                <TableCell>
                  <Badge appearance="tint" size="small">
                    {g.appid ? 'Steam' : g.source || 'exe'}
                  </Badge>
                </TableCell>
                <TableCell>
                  <div style={{ display: 'flex', gap: 4 }}>
                    <Tooltip content="打开所在文件夹" relationship="label">
                      <Button
                        size="small"
                        appearance="subtle"
                        icon={<Open24Regular />}
                        onClick={() => api.openGameFolder(g.exe)}
                      />
                    </Tooltip>
                    <Tooltip content="移除" relationship="label">
                      <Button
                        size="small"
                        appearance="subtle"
                        icon={<Delete24Regular />}
                        disabled={busy === g.id}
                        onClick={() => del(g)}
                      />
                    </Tooltip>
                  </div>
                </TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      )}

      {/* 添加确认对话框 */}
      <Dialog
        open={add.open}
        onOpenChange={(_, d) => {
          if (!d.open) setAdd(emptyAdd)
        }}
      >
        <DialogSurface style={{ maxWidth: 720 }}>
          <DialogBody>
            <DialogTitle>添加程序</DialogTitle>
            <DialogContent>
              {add.loading ? (
                <div style={{ display: 'flex', gap: 12, alignItems: 'center', padding: 24 }}>
                  <Spinner size="tiny" /> <Text>正在解析…</Text>
                </div>
              ) : (
                <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
                  {add.result?.steam ? (
                    <div
                      style={{
                        padding: '12px 14px',
                        borderRadius: 8,
                        background: tokens.colorNeutralBackground2,
                        border: `1px solid ${tokens.colorNeutralStroke2}`,
                      }}
                    >
                      <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
                        <Badge appearance="filled" color="brand" size="small">
                          Steam
                        </Badge>
                        <Text weight="semibold">{add.steamName || '未知游戏'}</Text>
                        <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>
                          appid {add.appid}
                        </Text>
                      </div>
                      <div className={s.path} style={{ marginTop: 4 }}>
                        {add.result.steam.fullpath || '未找到安装目录'}
                      </div>
                    </div>
                  ) : null}

                  {add.result?.message ? (
                    <Text size={200} style={{ color: tokens.colorNeutralForeground2 }}>
                      {add.result.message}
                    </Text>
                  ) : null}

                  {/* 候选 exe 列表（含图标与体积） */}
                  {cands.length > 1 ? (
                    <div>
                      <Divider style={{ marginBottom: 10 }}>
                        <Text size={200}>选择主程序</Text>
                      </Divider>
                      <div style={{ maxHeight: 300, overflowY: 'auto' }}>
                        {cands.map((c) => (
                          <div
                            key={c.path}
                            className={`${s.candRow} ${add.exe === c.path ? s.candRowSel : ''}`}
                            onClick={() => setAdd((a) => ({ ...a, exe: c.path }))}
                          >
                            {c.icon ? (
                              <img className={s.candIcon} src={c.icon} alt="" />
                            ) : (
                              <div className={s.candIcon} />
                            )}
                            <div className={s.candMeta}>
                              <div className={s.candName}>{c.name}</div>
                              <Text size={100} style={{ color: tokens.colorNeutralForeground3 }}>
                                {c.sizetext} · {c.reason}
                              </Text>
                            </div>
                            {c.preferred ? (
                              <Badge appearance="tint" color="success" size="small">
                                推荐
                              </Badge>
                            ) : null}
                            {add.exe === c.path ? <Checkmark24Regular /> : null}
                          </div>
                        ))}
                      </div>
                    </div>
                  ) : null}

                  <Field
                    label="程序文件"
                    validationMessage={add.exe ? undefined : '请选择或手动指定主程序 exe'}
                    validationState={add.exe ? 'none' : 'warning'}
                  >
                    <Input
                      value={add.exe}
                      onChange={(_, d) => setAdd((a) => ({ ...a, exe: d.value }))}
                      placeholder="C:\Games\Game\game.exe"
                    />
                  </Field>

                  <Field
                    label="显示名（OLED 上显示，纯英文，最多 16 字符）"
                    validationState={checkDisplay(add.display) ? 'error' : 'none'}
                    validationMessage={checkDisplay(add.display) || undefined}
                  >
                    <Input
                      value={add.display}
                      maxLength={DISPLAY_MAX + 8}
                      onChange={(_, d) => setAdd((a) => ({ ...a, display: d.value }))}
                      contentAfter={
                        <Text size={100} style={{ color: tokens.colorNeutralForeground3 }}>
                          {add.display.length}/{DISPLAY_MAX}
                        </Text>
                      }
                    />
                  </Field>

                  {add.error ? (
                    <Text style={{ color: tokens.colorPaletteRedForeground1 }}>{add.error}</Text>
                  ) : null}
                </div>
              )}
            </DialogContent>
            <DialogActions>
              <Button appearance="secondary" onClick={() => setAdd(emptyAdd)}>
                取消
              </Button>
              <Button
                appearance="primary"
                disabled={add.loading || !add.exe || !!checkDisplay(add.display)}
                onClick={submitAdd}
              >
                {busy === 'add' ? '添加中…' : '添加'}
              </Button>
            </DialogActions>
          </DialogBody>
        </DialogSurface>
      </Dialog>
    </div>
  )
}
