package main

import (
	"bytes"
	"encoding/base64"
	"image"
	"image/png"
	"sync"
	"syscall"
	"unsafe"
)

// ============================================================
// exe 图标提取
//
// 用于「候选列表里显示每个 exe 的图标」——用户选程序时能一眼认出。
// 走 Shell32 的 ExtractIconEx + 手工 GetDIBits 转 RGBA。
// 全程无 CGO、无外部依赖。
// ============================================================

var (
	shell32 = syscall.NewLazyDLL("shell32.dll")
	user32  = syscall.NewLazyDLL("user32.dll")
	gdi32   = syscall.NewLazyDLL("gdi32.dll")

	procExtractIconExW = shell32.NewProc("ExtractIconExW")
	procDestroyIcon    = user32.NewProc("DestroyIcon")
	procGetIconInfo    = user32.NewProc("GetIconInfo")
	procGetObjectW     = gdi32.NewProc("GetObjectW")
	procGetDIBits      = gdi32.NewProc("GetDIBits")
	procDeleteObject   = gdi32.NewProc("DeleteObject")
	procCreateCompatDC = gdi32.NewProc("CreateCompatibleDC")
	procDeleteDC       = gdi32.NewProc("DeleteDC")
)

type iconInfo struct {
	FIcon    int32
	XHotspot uint32
	YHotspot uint32
	HbmMask  syscall.Handle
	HbmColor syscall.Handle
}

type bitmap struct {
	BmType       int32
	BmWidth      int32
	BmHeight     int32
	BmWidthBytes int32
	BmPlanes     uint16
	BmBitsPixel  uint16
	BmBits       uintptr
}

type bitmapInfoHeader struct {
	BiSize          uint32
	BiWidth         int32
	BiHeight        int32
	BiPlanes        uint16
	BiBitCount      uint16
	BiCompression   uint32
	BiSizeImage     uint32
	BiXPelsPerMeter int32
	BiYPelsPerMeter int32
	BiClrUsed       uint32
	BiClrImportant  uint32
}

type rgbQuad struct {
	Blue     byte
	Green    byte
	Red      byte
	Reserved byte
}

type bitmapInfo struct {
	Header bitmapInfoHeader
	Colors [1]rgbQuad
}

var (
	iconCacheMu sync.Mutex
	iconCache   = map[string]string{}
)

// iconDataURL 返回 exe 图标的 PNG data URL；失败返回空串。
// 结果按路径缓存，避免列表刷新时反复解压图标。
func iconDataURL(exePath string) string {
	if exePath == "" {
		return ""
	}
	iconCacheMu.Lock()
	if v, ok := iconCache[exePath]; ok {
		iconCacheMu.Unlock()
		return v
	}
	iconCacheMu.Unlock()

	url := extractIconPNG(exePath)

	iconCacheMu.Lock()
	iconCache[exePath] = url
	iconCacheMu.Unlock()
	return url
}

// extractIconPNG 提取第一个图标并编码为 PNG data URL。
func extractIconPNG(exePath string) string {
	p, err := syscall.UTF16PtrFromString(exePath)
	if err != nil {
		return ""
	}
	var large, small syscall.Handle
	// 只取大图标；nIcons=1
	n, _, _ := procExtractIconExW.Call(
		uintptr(unsafe.Pointer(p)),
		0,
		uintptr(unsafe.Pointer(&large)),
		uintptr(unsafe.Pointer(&small)),
		1,
	)
	if n == 0 || large == 0 {
		if small != 0 {
			procDestroyIcon.Call(uintptr(small))
		}
		return ""
	}
	defer procDestroyIcon.Call(uintptr(large))
	if small != 0 {
		defer procDestroyIcon.Call(uintptr(small))
	}

	img := hIconToRGBA(large)
	if img == nil {
		return ""
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		return ""
	}
	return "data:image/png;base64," + base64.StdEncoding.EncodeToString(buf.Bytes())
}

// hIconToRGBA 把 HICON 转成 *image.RGBA。
func hIconToRGBA(h syscall.Handle) *image.RGBA {
	var ii iconInfo
	if ret, _, _ := procGetIconInfo.Call(uintptr(h), uintptr(unsafe.Pointer(&ii))); ret == 0 {
		return nil
	}
	if ii.HbmColor != 0 {
		defer procDeleteObject.Call(uintptr(ii.HbmColor))
	}
	if ii.HbmMask != 0 {
		defer procDeleteObject.Call(uintptr(ii.HbmMask))
	}
	if ii.HbmColor == 0 {
		return nil
	}

	var bm bitmap
	if ret, _, _ := procGetObjectW.Call(
		uintptr(ii.HbmColor),
		unsafe.Sizeof(bm),
		uintptr(unsafe.Pointer(&bm)),
	); ret == 0 {
		return nil
	}
	w, hgt := int(bm.BmWidth), int(bm.BmHeight)
	if w <= 0 || hgt <= 0 || w > 512 || hgt > 512 {
		return nil
	}

	hdc, _, _ := procCreateCompatDC.Call(0)
	if hdc == 0 {
		return nil
	}
	defer procDeleteDC.Call(hdc)

	// 32 位 BGRA，负高度表示自上而下
	var bi bitmapInfo
	bi.Header.BiSize = uint32(unsafe.Sizeof(bi.Header))
	bi.Header.BiWidth = int32(w)
	bi.Header.BiHeight = -int32(hgt)
	bi.Header.BiPlanes = 1
	bi.Header.BiBitCount = 32
	bi.Header.BiCompression = 0 // BI_RGB

	buf := make([]byte, w*hgt*4)
	ret, _, _ := procGetDIBits.Call(
		hdc,
		uintptr(ii.HbmColor),
		0,
		uintptr(hgt),
		uintptr(unsafe.Pointer(&buf[0])),
		uintptr(unsafe.Pointer(&bi)),
		0, // DIB_RGB_COLORS
	)
	if ret == 0 {
		return nil
	}

	img := image.NewRGBA(image.Rect(0, 0, w, hgt))
	opaque := false
	for y := 0; y < hgt; y++ {
		for x := 0; x < w; x++ {
			i := (y*w + x) * 4
			b, g, r, a := buf[i], buf[i+1], buf[i+2], buf[i+3]
			if a != 0 {
				opaque = true
			}
			o := img.PixOffset(x, y)
			img.Pix[o] = r
			img.Pix[o+1] = g
			img.Pix[o+2] = b
			img.Pix[o+3] = a
		}
	}
	// 有些老图标 alpha 全 0，此时按不透明处理（否则整图透明看不见）
	if !opaque {
		for i := 3; i < len(img.Pix); i += 4 {
			img.Pix[i] = 255
		}
	}
	return img
}
