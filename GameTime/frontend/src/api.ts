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
