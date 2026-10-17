// Wails 运行时绑定。
// 由 wails build 自动生成到 frontend/wailsjs/go/main/App.*，
// 但为了在 npm run dev 下也能独立开发，这里做一层薄封装：
// 有生成的绑定就用，没有就走 fetch 兜底（开发期）。

export type Game = {
  id: string
  exe: string
  process: string
  display: string
  appid: string
  source: string
}

export type Config = {
  esp32_ip: string
  esp32_port: number
  interval_sec: number
  autostart: boolean
  start_hidden: boolean
  games: Game[]
}

export type LiveStatus = {
  running: boolean
  name: string
  up: number
  today: number
  esp32ok: boolean
  esp32err: string
  target: string
  lastsent: number
  exename: string
  version: string
  autostart: boolean
  startedat: number
}

export type ExeCandidate = {
  path: string
  name: string
  size: number
  sizetext: string
  icon: string
  preferred: boolean
  reason: string
}

export type SteamGame = {
  appid: string
  name: string
  installdir: string
  libpath: string
  fullpath: string
  installed: boolean
}

export type ResolveResult = {
  kind: string
  input: string
  exe: string
  steam: SteamGame | null
  candidates: ExeCandidate[] | null
  suggested: string
  message: string
  error: string
}

export type GameTotal = {
  game_id: string
  display: string
  process: string
  total: number
  runs: number
}

export type DayTotal = { day: string; total: number }

export type SessionRecord = {
  game_id: string
  process: string
  display: string
  start: number
  end: number
  dur: number
  day: string
}

export type StatsResult = {
  today_total: number
  today: GameTotal[]
  days: DayTotal[]
  recent: SessionRecord[]
}

type AppAPI = {
  GetConfig(): Promise<Config>
  SaveConfig(cfg: Config): Promise<Config>
  GetStatus(): Promise<LiveStatus>
  PickFile(): Promise<ResolveResult>
  ResolvePath(path: string): Promise<ResolveResult>
  TakeDropPath(): Promise<string>
  NotifyDropReady(): Promise<void>
  DropReady(): Promise<boolean>
  LogFromFrontend(tag: string, msg: string): Promise<void>
  ListCandidates(appid: string): Promise<ExeCandidate[] | null>
  AddGame(g: Partial<Game>): Promise<Game>
  UpdateGame(g: Game): Promise<Game>
  DeleteGame(id: string): Promise<void>
  CheckDisplay(s: string): Promise<[string, string]>
  GetStats(days: number): Promise<StatsResult>
  SetAutostart(on: boolean): Promise<boolean>
  AutostartState(): Promise<boolean>
  OpenDataDir(): Promise<void>
  OpenGameFolder(path: string): Promise<void>
  HideWindow(): Promise<void>
  Quit(): Promise<void>
}

// 运行时探测：wails build 会注入 window.go.main.App
declare global {
  interface Window {
    go?: { main?: { App?: AppAPI } }
    runtime?: {
      OnFileDrop?: (
        cb: (x: number, y: number, paths: string[]) => void,
        useDropTarget?: boolean,
      ) => void
      OnFileDropOff?: () => void
      WindowHide?: () => void
      Quit?: () => void
    }
  }
}

function api(): AppAPI {
  const a = window.go?.main?.App
  if (!a) {
    throw new Error('Wails 绑定未加载（请用 wails dev 或 wails build 运行）')
  }
  return a
}

export const App = {
  getConfig: () => api().GetConfig(),
  saveConfig: (cfg: Config) => api().SaveConfig(cfg),
  getStatus: () => api().GetStatus(),
  pickFile: () => api().PickFile(),
  resolvePath: (p: string) => api().ResolvePath(p),
  takeDropPath: () => api().TakeDropPath(),
  notifyDropReady: () => api().NotifyDropReady(),
  dropReady: () => api().DropReady(),
  listCandidates: (appid: string) => api().ListCandidates(appid),
  addGame: (g: Partial<Game>) => api().AddGame(g as Game),
  updateGame: (g: Game) => api().UpdateGame(g),
  deleteGame: (id: string) => api().DeleteGame(id),
  checkDisplay: (s: string) => api().CheckDisplay(s),
  getStats: (days: number) => api().GetStats(days),
  setAutostart: (on: boolean) => api().SetAutostart(on),
  autostartState: () => api().AutostartState(),
  openDataDir: () => api().OpenDataDir(),
  openGameFolder: (p: string) => api().OpenGameFolder(p),
  hideWindow: () => api().HideWindow(),
  quit: () => api().Quit(),
}

// ============================================================
// 拖放注册
//
// Wails v2 的 DOM dragover/dragleave/drop 监听，是前端调用
// window.runtime.OnFileDrop() 时才挂上的（见 runtime/desktop/draganddrop.js）。
// 只在 Go 侧写 runtime.OnFileDrop 不会生效 —— 事件永远不会派发到 Go。
// 实测：未调用时日志里一条 [drop] 记录都没有。
//
// 第二个参数传 false：不要求落点元素带 --wails-drop-target，
// 整个窗口任意位置都能接收拖放，比限定一个虚线框好用。
// ============================================================
// 已注册过就不再重复注册，否则回调会被叠加、
// 一次拖放触发多次解析（实测日志里出现过多条「前端已订阅拖放」）。
let dropRegistered = false

export function registerFileDrop(onPaths: (paths: string[]) => void): () => void {
  const rt = window.runtime
  if (!rt?.OnFileDrop) {
    console.warn('[drop] 运行时未就绪，拖放不可用')
    return () => {}
  }
  // 用「最新的回调」覆盖旧的：模块级变量，重复注册时只保留最后一次
  dropCallback = onPaths

  if (!dropRegistered) {
    dropRegistered = true
    rt.OnFileDrop((_x, _y, paths) => {
      // 始终转发给当前最新的回调，避免组件重挂后回调失效
      if (paths && paths.length > 0) dropCallback?.(paths)
    }, false)

    // 告知 Go 侧前端已挂上监听（只报一次）
    try {
      void App.notifyDropReady()
    } catch {
      /* 绑定未就绪时忽略 */
    }
  }

  // 组件卸载时只解绑回调，不撤销 DOM 监听
  // （其它页面可能仍在使用；撤销后再注册会重复挂监听）
  return () => {
    if (dropCallback === onPaths) dropCallback = null
  }
}

let dropCallback: ((paths: string[]) => void) | null = null

// onDropDetach 在程序退出前彻底解除监听（当前未使用，保留 API 完整性）。
export function unregisterFileDrop(): void {
  if (!dropRegistered) return
  dropRegistered = false
  dropCallback = null
  try {
    window.runtime?.OnFileDropOff?.()
  } catch {
    /* 忽略 */
  }
}
