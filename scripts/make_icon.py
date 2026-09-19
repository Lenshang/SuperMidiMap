# 生成 resources/app.ico —— 深色圆角底板 + 彩色垫面，与程序主题一致
# 大尺寸(>=64)画 4x4 垫面，小尺寸自动简化为 2x2，每个尺寸独立绘制保证锐利。
# 重新生成: python scripts/make_icon.py
import io
import os
import struct

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "resources", "app.ico")

PLATE = (0x1A, 0x1C, 0x23, 255)   # 深色底板（与程序主题一致）
PALETTE = [
    (0x4C, 0xC2, 0xFF, 255),      # 蓝
    (0x35, 0xD0, 0xBA, 255),      # 青
    (0x8B, 0x7C, 0xF6, 255),      # 紫
    (0xF2, 0xB2, 0x4C, 255),      # 橙
]
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def draw_icon(size: int) -> Image.Image:
    """绘制单个尺寸的图标，4x 超采样抗锯齿。"""
    ss = size * 4
    img = Image.new("RGBA", (ss, ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # 底板：几乎占满画布的圆角方形
    m = max(1, int(ss * 0.015))
    d.rounded_rectangle([m, m, ss - 1 - m, ss - 1 - m],
                        radius=int(ss * 0.22), fill=PLATE)

    # 小尺寸 2x2 简化，大尺寸 4x4 全设计
    if size >= 64:
        n, pad, gap = 4, 0.115, 0.035
    else:
        n, pad, gap = 2, 0.17, 0.06

    pad_px = int(ss * pad)
    gap_px = int(ss * gap)
    grid = ss - 2 * pad_px
    cell = (grid - (n - 1) * gap_px) / n
    radius = int(cell * 0.28)

    for r in range(n):
        for c in range(n):
            if n == 4:
                color = PALETTE[((r * 4 + c) + r) % 4]   # 与 makeAppIcon 相同的排布
            else:
                color = PALETTE[{(0, 0): 0, (0, 1): 1, (1, 0): 2, (1, 1): 3}[(r, c)]]
            x0 = pad_px + int(c * (cell + gap_px))
            y0 = pad_px + int(r * (cell + gap_px))
            d.rounded_rectangle([x0, y0, int(x0 + cell), int(y0 + cell)],
                                radius=radius, fill=color)

    return img.resize((size, size), Image.LANCZOS)


def to_dib(img: Image.Image) -> bytes:
    """转为未压缩 32bpp DIB（ICO 传统格式）——rc.exe 与所有 Windows 组件都支持。"""
    w, h = img.size
    px = img.load()
    xor = bytearray(w * h * 4)
    i = 0
    for y in range(h - 1, -1, -1):          # DIB 自底向上
        for x in range(w):
            r, g, b, a = px[x, y]
            xor[i], xor[i + 1], xor[i + 2], xor[i + 3] = b, g, r, a
            i += 4
    and_row = ((w + 31) // 32) * 4          # 1bpp 掩码，行按 4 字节对齐
    and_mask = bytearray(and_row * h)       # 全 0 = 不透明，透明度由 alpha 决定
    bih = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                      len(xor) + len(and_mask), 0, 0, 0, 0)
    return bih + bytes(xor) + bytes(and_mask)


def write_ico(path: str) -> None:
    images = {s: draw_icon(s) for s in SIZES}
    blobs = [to_dib(images[s]) for s in SIZES]

    header = struct.pack("<HHH", 0, 1, len(SIZES))
    directory = b""
    offset = 6 + 16 * len(SIZES)
    for s, blob in zip(SIZES, blobs):
        wh = 0 if s >= 256 else s          # 0 表示 256
        directory += struct.pack("<BBBBHHII", wh, wh, 0, 0, 1, 32,
                                 len(blob), offset)
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(header + directory + b"".join(blobs))
    print(f"written: {os.path.abspath(path)}  "
          f"({len(SIZES)} sizes, {os.path.getsize(path)} bytes)")

    # 输出预览条便于人工检查
    strip = Image.new("RGBA", (sum(s + 8 for s in SIZES) + 8, 272), (64, 64, 70, 255))
    x = 8
    for s in SIZES:
        im = images[s]
        if s < 256:
            im = im.resize((s * 1, s * 1))
        strip.paste(im, (x, 264 - s), im)
        x += s + 8
    strip.save(os.path.join(os.path.dirname(path), "icon_preview.png"))
    print("preview: icon_preview.png")


if __name__ == "__main__":
    write_ico(OUT)
