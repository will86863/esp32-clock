# -*- coding: utf-8 -*-
"""
生成「带描边」的 2bpp 数字掩膜资源  ——  V1.6.6 方案A

思路
----
现有字模 font/W_3660_iN.h / W_1830_iN.h 是"白字 + 纯黑底"的 JPEG（亮度严格两极，
52.4% 纯黑 + 47.6% 纯白，过渡带 0%），所以可以干净地二值化，再向外做形态学膨胀，
得到描边环。最后压成 4 级 2bpp 掩膜：

    0 = 透明      -> 绘制时取背景像素（键控）
    1 = 描边过渡  -> 描边色按 ~55% 与背景混合（让外缘不硬）
    2 = 描边实心  -> 描边色
    3 = 笔画      -> 笔画色

为什么比现在的 JPEG 方案好
  · 体积：JPEG 那套约 42KB flash，2bpp 掩膜只有约 8KB
  · 性能：不再需要每秒 6 次 JPEG 解码
  · 能力：描边是"独立的一级"，可以和笔画用两种颜色

描边厚度（用户要求"最合适的就行"）
  · 大字 36x60（笔画约 8px 宽）: 1px 实心 + 1px 过渡  ≈ 视觉 1.5px
  · 小字 18x30（笔画约 4px 宽）: 1px 实心             ≈ 视觉 1.0px

掩膜会向外留出描边的余量（pad），绘制时按 (x-pad, y-pad) 贴，这样描边不会被裁掉。

用法: python tools/gen_digit_mask.py
产物: SmallDesktopDisplay/font/DigitMask.h  +  tools/digit_mask_preview.png
"""
import io
import os
import re
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT = os.path.join(ROOT, 'SmallDesktopDisplay', 'font')
OUT_H = os.path.join(FONT, 'DigitMask.h')
OUT_PNG = os.path.join(HERE, 'digit_mask_preview.png')

# ---- 两套字模的配置: (前缀, 宽, 高, 实心描边半径, 过渡环半径) ----
SETS = [
    ('A', 'W_3660', 36, 60, 1, 2),
    ('B', 'W_1830', 18, 30, 1, 1),
]

# 绘制时的配色（与固件里"自动取反"一致：亮字配黑边 / 暗字配白边）
FILL_BRIGHT = (255, 255, 255)
OUTLINE_FOR_BRIGHT = (0, 0, 0)


def load_gray(path):
    """从 .h 里抽出 JPEG 字节并解码成灰度图"""
    txt = io.open(path, encoding='utf-8', errors='ignore').read()
    m = re.search(r'\{(.*?)\}', txt, re.S)
    if not m:
        raise RuntimeError('no array in ' + path)
    data = bytes(int(v, 16) for v in re.findall(r'0[xX]([0-9a-fA-F]{2})', m.group(1)))
    p = os.path.join(tempfile.gettempdir(), '_genmask_tmp.jpg')
    with open(p, 'wb') as f:
        f.write(data)
    return Image.open(p).convert('L')


def to_binary(im, thr=127):
    w, h = im.size
    px = im.load()
    return w, h, [[1 if px[x, y] > thr else 0 for x in range(w)] for y in range(h)]


def disk(r):
    """半径 r 的圆盘结构元（r=1 时含斜向，得到完整 3x3）"""
    R = r + 0.5
    return [(dx, dy)
            for dy in range(-r, r + 1)
            for dx in range(-r, r + 1)
            if dx * dx + dy * dy <= R * R]


def dilate(g, w, h, offs, pad):
    """膨胀；坐标系向外扩 pad 像素，返回值也带 pad 偏移"""
    W, H = w + 2 * pad, h + 2 * pad
    out = [[0] * W for _ in range(H)]
    for y in range(h):
        row = g[y]
        for x in range(w):
            if row[x]:
                for dx, dy in offs:
                    nx, ny = x + dx + pad, y + dy + pad
                    if 0 <= nx < W and 0 <= ny < H:
                        out[ny][nx] = 1
    return out


def build_levels(g, w, h, r_in, r_out):
    pad = r_out
    d_in = dilate(g, w, h, disk(r_in), pad)
    d_out = dilate(g, w, h, disk(r_out), pad) if r_out != r_in else d_in
    W, H = w + 2 * pad, h + 2 * pad
    lv = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            if 0 <= y - pad < h and 0 <= x - pad < w and g[y - pad][x - pad]:
                lv[y][x] = 3
            elif d_in[y][x]:
                lv[y][x] = 2
            elif d_out[y][x]:
                lv[y][x] = 1
    return lv, W, H, pad


def pack_2bpp(lv, W, H):
    stride = (W * 2 + 7) // 8
    data = bytearray(stride * H)
    for y in range(H):
        row = lv[y]
        for x in range(W):
            v = row[x]
            if v:
                data[y * stride + (x * 2) // 8] |= (v << (6 - ((x * 2) % 8)))
    return stride, bytes(data)


def main():
    blocks = []          # 写入 .h 的 C 代码
    stats = []           # 统计
    rendered = {}        # 用于渲染预览

    for key, prefix, w, h, r_in, r_out in SETS:
        all_masks = []
        W = H = stride = 0
        for n in range(10):
            path = os.path.join(FONT, '%s_i%d.h' % (prefix, n))
            if not os.path.exists(path):
                raise RuntimeError('缺文件: ' + path)
            im = load_gray(path)
            if im.size != (w, h):
                raise RuntimeError('%s 尺寸应为 %dx%d，实际 %s' % (prefix, im.size, w, h))
            gw, gh, g = to_binary(im)
            lv, W, H, pad = build_levels(g, gw, gh, r_in, r_out)
            stride, data = pack_2bpp(lv, W, H)
            assert len(data) == stride * H
            all_masks.append((lv, data))
        stats.append((key, w, h, r_in, r_out, pad, W, H, stride, stride * H))
        rendered[key] = (W, H, pad, [m[0] for m in all_masks])

        # 生成 C 代码
        L = []
        L.append('#define DIGITM_%s_W      %d' % (key, w))
        L.append('#define DIGITM_%s_H      %d' % (key, h))
        L.append('#define DIGITM_%s_PAD    %d' % (key, pad))
        L.append('#define DIGITM_%s_MW     %d' % (key, W))
        L.append('#define DIGITM_%s_MH     %d' % (key, H))
        L.append('#define DIGITM_%s_STRIDE %d' % (key, stride))
        L.append('const uint8_t DIGITM_%s[10][%d] = {' % (key, stride * H))
        for n, (_, data) in enumerate(all_masks):
            L.append('  { // %d   %d bytes' % (n, len(data)))
            for i in range(0, len(data), stride * 8):
                L.append('    ' + ', '.join('0x%02X' % b for b in data[i:i + stride * 8]) + ',')
            L.append('  },')
        L.append('};')
        blocks.append('\n'.join(L))

    header = [
        '// ===== 自动生成，请勿手改 =====',
        '// 由 tools/gen_digit_mask.py 生成（V1.6.6 时钟数字描边·方案A）',
        '//',
        '// 每像素 2bit，行内 MSB 优先：',
        '//   0 = 透明      -> 取背景像素（键控）',
        '//   1 = 描边过渡  -> 描边色 x55% 与背景混合',
        '//   2 = 描边实心  -> 描边色',
        '//   3 = 笔画      -> 笔画色',
        '//',
        '// 掩膜四周留了 PAD 像素给描边，绘制时要贴在 (x-PAD, y-PAD)。',
        '// 重新生成: python tools/gen_digit_mask.py',
        '',
        '#ifndef DIGIT_MASK_H',
        '#define DIGIT_MASK_H',
        '',
        '#include <stdint.h>',
        '',
        '\n\n'.join(blocks),
        '',
        '#endif',
        '',
    ]
    with io.open(OUT_H, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(header))

    # ---- 控制台报告 ----
    print('生成完成: %s' % OUT_H)
    print()
    print('%-4s %-10s %-16s %-14s %-8s %-8s %s' %
          ('集', '源字模', '源尺寸', '描边(r实+r过)', '掩膜尺寸', '行跨', '每字/总'))
    total = 0
    for key, w, h, r_in, r_out, pad, W, H, stride, size in stats:
        total += size * 10
        print('%-4s %-10s %-16s %-14s %-8s %-8s %d / %d B' %
              (key, [s[1] for s in SETS if s[0] == key][0], '%dx%d' % (w, h),
               '%d+%d' % (r_in, r_out), '%dx%d' % (W, H), stride, size, size * 10))
    print()
    print('两套合计 flash 占用: %d 字节 (%.1f KB)' % (total, total / 1024.0))
    print('对比原来的 JPEG 字模约 42KB  -> 省约 %.0fKB' % (42 - total / 1024.0))

    # ---- 渲染预览（真实布局 + 自动取反的黑描边）----
    try:
        bg_path = os.path.join(ROOT, 'misaka111.jpg')
        bg = Image.open(bg_path).convert('RGB').resize((240, 240)) if os.path.exists(bg_path) \
            else Image.new('RGB', (240, 240), (90, 110, 140))
        screen = bg.copy()
        px = screen.load()

        def blit(key, n, x, y, fill):
            Wd, Hd, pad, levels = rendered[key]
            lv = levels[n]
            oc = OUTLINE_FOR_BRIGHT if sum(fill) > 3 * 110 else (255, 255, 255)
            for yy in range(Hd):
                for xx in range(Wd):
                    v = lv[yy][xx]
                    if v == 0:
                        continue
                    tx, ty = x - pad + xx, y - pad + yy
                    if not (0 <= tx < 240 and 0 <= ty < 240):
                        continue
                    if v == 3:
                        c = fill
                    elif v == 2:
                        c = oc
                    else:
                        b = px[tx, ty]
                        c = tuple(int(oc[i] * 0.55 + b[i] * 0.45) for i in range(3))
                    px[tx, ty] = c

        blit('A', 1, 20, 82, FILL_BRIGHT)
        blit('A', 2, 60, 82, FILL_BRIGHT)
        blit('A', 3, 101, 82, FILL_BRIGHT)
        blit('A', 4, 141, 82, FILL_BRIGHT)
        blit('B', 5, 182, 112, FILL_BRIGHT)
        blit('B', 6, 202, 112, FILL_BRIGHT)
        screen.resize((480, 480), Image.NEAREST).save(OUT_PNG)
        print()
        print('预览已生成: %s' % OUT_PNG)
    except Exception as e:
        print()
        print('预览渲染失败(不影响资源生成): %r' % (e,))


if __name__ == '__main__':
    main()
