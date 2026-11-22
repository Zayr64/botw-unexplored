"""Textures of a Switch BNTX (inside a BFRES or a layout archive) as RGBA images.

Only what BotW's icons use: R8, R8G8, R8G8B8A8, BC4 and BC5. The first mip of each texture is de-swizzled from the
Tegra X1 block-linear layout. One- and two-channel textures come out as grey (R) with alpha (G, or R alone): the
game tints them in its layouts.
"""
import math
import struct

FORMAT_R8 = 0x02     # high byte of the BRTI format; the low byte is the data type (unorm 1, srgb 6)
FORMAT_R8G8 = 0x09
FORMAT_RGBA8 = 0x0B
FORMAT_BC4 = 0x1D
FORMAT_BC5 = 0x1E


def _bc4_block(block):
    """The 16 values of a BC4 block (8 bytes), row by row."""
    r0, r1 = block[0], block[1]
    bits = int.from_bytes(block[2:8], "little")
    if r0 > r1:
        palette = [r0, r1] + [((7 - i) * r0 + i * r1) // 7 for i in range(1, 7)]
    else:
        palette = [r0, r1] + [((5 - i) * r0 + i * r1) // 5 for i in range(1, 5)] + [0, 255]
    return [palette[(bits >> (3 * k)) & 7] for k in range(16)]


def _decode_bc(linear, width, height, two_channels):
    """BC4 (one channel) or BC5 (two) blocks, already in linear order, to RGBA: grey from the first channel, alpha
    from the second (or from the first, for BC4)."""
    bw, bh = math.ceil(width / 4), math.ceil(height / 4)
    size = 16 if two_channels else 8
    out = bytearray(width * height * 4)
    for by in range(bh):
        for bx in range(bw):
            off = (by * bw + bx) * size
            r = _bc4_block(linear[off:off + 8])
            g = _bc4_block(linear[off + 8:off + 16]) if two_channels else r
            for k in range(16):
                x, y = bx * 4 + k % 4, by * 4 + k // 4
                if x < width and y < height:
                    p = (y * width + x) * 4
                    out[p:p + 4] = bytes((r[k], r[k], r[k], g[k]))
    return bytes(out)


def _deswizzle(data, width, height, bpp, block_height):
    """Block-linear (GOBs of 64 bytes x 8 rows, `block_height` GOBs tall) to linear rows."""
    pitch = width * bpp
    gobs_x = math.ceil(pitch / 64)
    out = bytearray(pitch * height)
    block_rows = 8 * block_height
    for y in range(height):
        base_y = (y // block_rows) * 512 * block_height * gobs_x + ((y % block_rows) // 8) * 512
        yy = y % 8
        row_part = ((yy % 8) // 2) * 64 + (yy % 2) * 16
        dst = y * pitch
        for x in range(0, pitch, 16):  # 16 bytes stay together in a GOB
            addr = base_y + (x // 64) * 512 * block_height + ((x % 64) // 32) * 256 + ((x % 32) // 16) * 32 + row_part
            n = min(16, pitch - x)
            out[dst + x:dst + x + n] = data[addr:addr + n]
    return bytes(out)


def textures(blob, wanted=None):
    """(name, width, height, rgba bytes) of each texture of the first BNTX found in `blob` in a format above, or only
    those whose name `wanted(name)` accepts."""
    start = blob.find(b"BNTX\0\0\0\0")
    if start < 0:
        return []
    bntx = blob[start:]
    out = []
    pos = 0
    while True:
        i = bntx.find(b"BRTI", pos)
        if i < 0:
            break
        pos = i + 4
        fmt = struct.unpack_from("<I", bntx, i + 0x1C)[0]
        width, height = struct.unpack_from("<ii", bntx, i + 0x24)
        block_height_log2 = struct.unpack_from("<i", bntx, i + 0x34)[0]
        name_addr, _parent, ptrs_addr = struct.unpack_from("<QQQ", bntx, i + 0x60)
        name_len = struct.unpack_from("<H", bntx, name_addr)[0]
        name = bntx[name_addr + 2:name_addr + 2 + name_len].decode("ascii", "replace")
        kind = fmt >> 8
        if kind not in (FORMAT_R8, FORMAT_R8G8, FORMAT_RGBA8, FORMAT_BC4, FORMAT_BC5) or (wanted and not wanted(name)):
            continue
        data_addr = struct.unpack_from("<Q", bntx, ptrs_addr)[0]
        size = struct.unpack_from("<I", bntx, i + 0x50)[0]
        raw = bntx[data_addr:data_addr + size]
        block_height = 1 << block_height_log2
        if kind == FORMAT_RGBA8:
            rgba = _deswizzle(raw, width, height, 4, block_height)
        elif kind in (FORMAT_BC4, FORMAT_BC5):
            bpp = 16 if kind == FORMAT_BC5 else 8
            linear = _deswizzle(raw, math.ceil(width / 4), math.ceil(height / 4), bpp, block_height)
            rgba = _decode_bc(linear, width, height, kind == FORMAT_BC5)
        else:
            channels = 2 if kind == FORMAT_R8G8 else 1
            linear = _deswizzle(raw, width, height, channels, block_height)
            rgba = bytearray(width * height * 4)
            for p in range(width * height):
                r = linear[p * channels]
                a = linear[p * channels + 1] if channels == 2 else r
                rgba[p * 4:p * 4 + 4] = bytes((r, r, r, a))
            rgba = bytes(rgba)
        out.append((name, width, height, rgba))
    return out
