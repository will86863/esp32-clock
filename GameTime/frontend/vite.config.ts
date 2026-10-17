import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// Wails v2 约定：前端产物必须落在 dist/，由 Go 侧 go:embed 打进 exe。
// base 用相对路径，保证 file:// 与内嵌资源两种加载方式都能取到资源。
export default defineConfig({
  plugins: [react()],
  base: './',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    chunkSizeWarningLimit: 2048,
  },
  server: {
    port: 34115,
    strictPort: true,
  },
})
