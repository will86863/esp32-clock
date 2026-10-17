// 生成应用图标：Tabler Icons 的 device-gamepad-2 + clock
// 来源：https://github.com/tabler/tabler-icons  (MIT License)
//
// 产出：
//   assets/appicon.png   512x512  应用图标（Wails 窗口 + exe）
//   assets/app.ico       7 尺寸   exe 资源图标
//   assets/tray.ico      多尺寸   系统托盘（Windows 只认 .ico）
//   frontend/public/icon.png     侧边栏 logo
//
// 用法：node tools/gen_icons.mjs
//
// sharp 装在前端目录（frontend/node_modules），这里用 createRequire 显式解析，
// 避免 ESM 按脚本自身位置（tools/）去找 node_modules 而找不到。

import { createRequire } from 'node:module'
import { mkdir, writeFile } from 'node:fs/promises'
import path from 'node:path'

const ROOT = path.resolve(import.meta.dirname, '..')
const FRONTEND = path.join(ROOT, 'frontend')
const require = createRequire(path.join(FRONTEND, 'package.json'))
const sharp = require('sharp')

const ASSETS = path.join(ROOT, 'assets')
const PUBLIC = path.join(FRONTEND, 'public')

// Tabler Icons —— device-gamepad-2 (MIT)
const GAMEPAD_PATHS = `
  <path d="M12 5h3.5a5 5 0 0 1 0 10h-5.5l-4.015 4.227a2.3 2.3 0 0 1 -3.923 -2.035l1.634 -8.173a5 5 0 0 1 4.904 -4.019h3.4" />
  <path d="M14 15l4.07 4.284a2.3 2.3 0 0 0 3.925 -2.023l-1.6 -8.232" />
  <path d="M8 9v2" />
  <path d="M7 10h2" />
  <path d="M14 10h2" />
`

// 24x24 网格上的手柄，放大到 512 画布中心
function appIconSVG(size = 512) {
  // Tabler 字形在 24x24 网格上实际占约 20x16，缩放到画布的 62%
  const target = size * 0.62
  const s = target / 24
  const offset = (size - target) / 2
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#2b88d8"/>
      <stop offset="100%" stop-color="#005a9e"/>
    </linearGradient>
    <linearGradient id="glow" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#ffffff" stop-opacity="0.28"/>
      <stop offset="100%" stop-color="#ffffff" stop-opacity="0"/>
    </linearGradient>
  </defs>
  <rect width="${size}" height="${size}" rx="${size * 0.22}" fill="url(#bg)"/>
  <rect width="${size}" height="${size * 0.5}" rx="${size * 0.22}" fill="url(#glow)"/>
  <g transform="translate(${offset},${offset}) scale(${s})"
     fill="none" stroke="#ffffff" stroke-width="2"
     stroke-linecap="round" stroke-linejoin="round">
    ${GAMEPAD_PATHS}
  </g>
</svg>`
}

// 托盘图标：单色字形（深色主题用白色，浅色主题用深灰）
function traySVG(size = 64, color = '#ffffff') {
  const target = size * 0.86
  const s = target / 24
  const offset = (size - target) / 2
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}">
  <g transform="translate(${offset},${offset}) scale(${s})"
     fill="none" stroke="${color}" stroke-width="2"
     stroke-linecap="round" stroke-linejoin="round">
    ${GAMEPAD_PATHS}
  </g>
</svg>`
}

// 由若干 PNG 手工拼 ICO。
// 关键：ICO 目录必须按尺寸从大到小或从小到大一致排列，
// 且每张图各自编码；上次踩过的坑是「基底图必须最大」。
function buildICO(pngs) {
  // pngs: [{size, buf}]
  const count = pngs.length
  const header = Buffer.alloc(6)
  header.writeUInt16LE(0, 0) // reserved
  header.writeUInt16LE(1, 2) // type = icon
  header.writeUInt16LE(count, 4)

  const dir = Buffer.alloc(16 * count)
  let offset = 6 + 16 * count
  const datas = []
  pngs.forEach((p, i) => {
    const b = i * 16
    dir.writeUInt8(p.size >= 256 ? 0 : p.size, b + 0) // width
    dir.writeUInt8(p.size >= 256 ? 0 : p.size, b + 1) // height
    dir.writeUInt8(0, b + 2) // color count
    dir.writeUInt8(0, b + 3) // reserved
    dir.writeUInt16LE(1, b + 4) // planes
    dir.writeUInt16LE(32, b + 6) // bit count
    dir.writeUInt32LE(p.buf.length, b + 8)
    dir.writeUInt32LE(offset, b + 12)
    offset += p.buf.length
    datas.push(p.buf)
  })
  return Buffer.concat([header, dir, ...datas])
}

async function renderPNG(svg, size) {
  return sharp(Buffer.from(svg), { density: 384 })
    .resize(size, size, { fit: 'contain', background: { r: 0, g: 0, b: 0, alpha: 0 } })
    .png()
    .toBuffer()
}

async function main() {
  await mkdir(ASSETS, { recursive: true })
  await mkdir(PUBLIC, { recursive: true })

  // 1) 应用主图标 512
  const svgApp = appIconSVG(512)
  const app512 = await renderPNG(svgApp, 512)
  await writeFile(path.join(ASSETS, 'appicon.png'), app512)
  await writeFile(path.join(PUBLIC, 'icon.png'), await renderPNG(svgApp, 128))
  console.log('appicon.png / icon.png 完成')

  // 2) exe 图标：7 个尺寸，从大到小
  const appSizes = [256, 128, 64, 48, 32, 24, 16]
  const appPNGs = []
  for (const sz of appSizes) {
    appPNGs.push({ size: sz, buf: await renderPNG(svgApp, sz) })
  }
  const ico = buildICO(appPNGs)
  await writeFile(path.join(ASSETS, 'app.ico'), ico)
  console.log(`app.ico 完成 (${appPNGs.length} 尺寸, ${ico.length} 字节)`)

  // 3) 托盘图标：白色字形（深色任务栏）
  const traySizes = [64, 48, 32, 24, 16]
  const trayPNGs = []
  for (const sz of traySizes) {
    trayPNGs.push({ size: sz, buf: await renderPNG(traySVG(sz, '#ffffff'), sz) })
  }
  const trayIco = buildICO(trayPNGs)
  await writeFile(path.join(ASSETS, 'tray.ico'), trayIco)
  console.log(`tray.ico 完成 (${trayPNGs.length} 尺寸, ${trayIco.length} 字节)`)

  // 4) 校验：每张 PNG 都能在 ICO 里被找到
  for (const p of appPNGs) {
    if (!ico.includes(p.buf)) throw new Error(`app.ico 缺少 ${p.size}px 图像`)
  }
  for (const p of trayPNGs) {
    if (!trayIco.includes(p.buf)) throw new Error(`tray.ico 缺少 ${p.size}px 图像`)
  }
  console.log('ICO 内含图像校验通过')
}

main().catch((e) => {
  console.error(e)
  process.exit(1)
})
