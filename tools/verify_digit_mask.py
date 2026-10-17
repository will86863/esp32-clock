# -*- coding: utf-8 -*-
"""
校验 DigitMask.h 的打包与固件里的解包是否一致，并渲染"自动取反"两个方向的预览。

固件里的解包（.ino drawDigitMask）:
    lv = (mRow[rx >> 2] >> (6 - ((rx & 3) << 1))) & 0x03

本脚本用**同一公式**从 DigitMask.h 反解出级别，再和"直接从源字模重算一遍"的级别
逐像素比对。若有任何 off-by-one 都会在这里暴露。

用法: python tools/verify_digit_mask.py
"""
import io
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_digit_mask as G          # 复用生成器里的逻辑（它被 __main__ 保护着）

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT = os.path.join(ROOT, 'SmallDesktopDisplay', 'font')
MASK_H = os.path.join(FONT, 'DigitMask.h')
OUT_PNG = os.path.join(HERE, 'digit_mask_auto_invert.png')


def parse_header():
    """从 DigitMask.h 里取出 10xN 的数组，返回 {(key): (W,H,stride,pad,[bytes...])}"""
    txt = io.open(MASK_H, encoding='utf-8').read()
    res = {}
    for key, prefix, w, h, r_in, r_out in G.SETS:
        MW = int(re.search(r'#define DIGITM_%s_MW\s+(\d+)' % key, txt).group(1))
        MH = int(re.search(r'#define DIGITM_%s_MH\s+(\d+)' % key, txt).group(1))
        ST = int(re.search(r'#define DIGITM_%s_STRIDE\s+(\d+)' % key, txt).group(1))
        PD = int(re.search(r'#define DIGITM_%s_PAD\s+(\d+)' % key, txt).group(1))
        m = re.search(r'const uint8_t DIGITM_%s\[10\]\[\d+\] = \{(.*?)\n\};' % key, txt, re.S)
        body = m.group(1)
        glyphs = []
        for gm in re.finditer(r'\{(.*?)\},', body, re.S):
            vals = re.findall(r'0[xX]([0-9a-fA-F]{2})', gm.group(1))
            glyphs.append(bytes(int(v, 16) for v in vals))
        assert len(glyphs) == 10, '%s 只解析到 %d 张' % (key, len(glyphs))
        res[key] = (w, h, MW, MH, ST, PD, glyphs)
    return res


def main():
    parsed = parse_header()
    total_px = 0
    bad = 0
    print('%-4s %-6s %-14s %-10s %-8s %s' % ('集', '数字', '掩膜', '每张字节', '像素数', '逐像素比对'))
    for key, prefix, w, h, r_in, r_out in G.SETS:
        W, H, MW, MH, ST, PD, glyphs = parsed[key]
        for n in range(10):
            # 期望值：直接从源字模重算（与生成器同一套逻辑）
            im = G.load_gray(os.path.join(FONT, '%s_i%d.h' % (prefix, n)))
            gw, gh, g = G.to_binary(im)
            lv, EW, EH, epad = G.build_levels(g, gw, gh, r_in, r_out)
            assert (EW, EH, epad) == (MW, MH, PD), '尺寸不一致'
            exp = bytes(G.pack_2bpp(lv, EW, EH)[1])
            got = glyphs[n]

            # 用固件里那条公式反解，并与期望值逐像素比对
            mism = 0
            for y in range(MH):
                row = got[y * ST:(y + 1) * ST]
                for x in range(MW):
                    val = (row[x >> 2] >> (6 - ((x & 3) << 1))) & 0x03
                    if val != lv[y][x]:
                        mism += 1
            total_px += MW * MH
            bad += mism
            assert got == exp, '第 %d 张字节流与重算不一致' % n
            if n < 2 or mism:
                print('%-4s %-6d %-14s %-10d %-8d %s' %
                      (key, n, '%dx%d' % (MW, MH), len(got), MW * MH,
                       'OK' if mism == 0 else '★%d 处不符' % mism))
    print()
    print('逐像素比对总像素数 = %d，不一致 = %d  ->  %s' %
          (total_px, bad, '打包/解包完全一致' if bad == 0 else '★有错'))

    # ---- 渲染"自动取反"两个方向 ----
    bg_path = os.path.join(ROOT, 'misaka111.jpg')
    bg = Image.open(bg_path).convert('RGB').resize((240, 240)) if os.path.exists(bg_path) \
        else Image.new('RGB', (240, 240), (90, 110, 140))

    def render(fill):
        # 自动取反规则（同 .ino 的 digitOutlineColor）：按笔画色亮度取反
        lum = 0.30 * fill[0] + 0.59 * fill[1] + 0.11 * fill[2]
        oc = (0, 0, 0) if lum > 110 else (255, 255, 255)
        scr = bg.copy()
        px = scr.load()

        def blit(key, n, x, y):
            w, h, MW, MH, ST, PD, glyphs = parsed[key]
            got = glyphs[n]
            for ry in range(MH):
                sy = y - PD + ry
                if not (0 <= sy < 240):
                    continue
                row = got[ry * ST:(ry + 1) * ST]
                for rx in range(MW):
                    sx = x - PD + rx
                    if not (0 <= sx < 240):
                        continue
                    v = (row[rx >> 2] >> (6 - ((rx & 3) << 1))) & 0x03
                    if v == 0:
                        continue
                    if v == 3:
                        c = fill
                    elif v == 2:
                        c = oc
                    else:
                        b = px[sx, sy]
                        c = tuple(int(oc[i] * 0.55 + b[i] * 0.45) for i in range(3))
                    px[sx, sy] = c

        blit('A', 1, 20, 82); blit('A', 2, 60, 82)
        blit('A', 3, 101, 82); blit('A', 4, 141, 82)
        blit('B', 5, 182, 112); blit('B', 6, 202, 112)
        return scr, oc

    left, ocL = render((255, 255, 255))       # 亮字 -> 黑边
    right, ocR = render((40, 40, 40))         # 暗字 -> 白边
    out = Image.new('RGB', (480, 240))
    out.paste(left, (0, 0))
    out.paste(right, (240, 0))
    out.resize((960, 480), Image.NEAREST).save(OUT_PNG)
    print()
    print('自动取反预览: %s' % OUT_PNG)
    print('  左：笔画 #FFFFFF(亮) -> 描边 %s' % ('黑' if sum(ocL) == 0 else '白'))
    print('  右：笔画 #282828(暗) -> 描边 %s' % ('黑' if sum(ocR) == 0 else '白'))


if __name__ == '__main__':
    main()
