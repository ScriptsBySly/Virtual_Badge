#!/usr/bin/env python3
import argparse
import os
import struct
import zlib

PNG_SIG = b"\x89PNG\r\n\x1a\n"


def read_chunks(png_bytes):
    if not png_bytes.startswith(PNG_SIG):
        raise ValueError("Not a PNG")
    i = len(PNG_SIG)
    while i < len(png_bytes):
        if i + 8 > len(png_bytes):
            break
        length = struct.unpack(">I", png_bytes[i:i+4])[0]
        ctype = png_bytes[i+4:i+8]
        i += 8
        data = png_bytes[i:i+length]
        i += length
        crc = png_bytes[i:i+4]
        i += 4
        yield ctype, data, crc


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def unfilter_scanlines(raw, width, height, bpp):
    stride = width * bpp
    out = bytearray(height * stride)
    i = 0
    o = 0
    prev = bytearray(stride)
    for row in range(height):
        ftype = raw[i]
        i += 1
        cur = bytearray(raw[i:i+stride])
        i += stride
        if ftype == 0:  # None
            pass
        elif ftype == 1:  # Sub
            for x in range(stride):
                left = cur[x - bpp] if x >= bpp else 0
                cur[x] = (cur[x] + left) & 0xFF
        elif ftype == 2:  # Up
            for x in range(stride):
                cur[x] = (cur[x] + prev[x]) & 0xFF
        elif ftype == 3:  # Average
            for x in range(stride):
                left = cur[x - bpp] if x >= bpp else 0
                up = prev[x]
                cur[x] = (cur[x] + ((left + up) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for x in range(stride):
                left = cur[x - bpp] if x >= bpp else 0
                up = prev[x]
                up_left = prev[x - bpp] if x >= bpp else 0
                cur[x] = (cur[x] + paeth(left, up, up_left)) & 0xFF
        else:
            raise ValueError(f"Unsupported filter type {ftype}")

        out[o:o+stride] = cur
        o += stride
        prev = cur
    return out


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def png_to_rgb565_array(path):
    with open(path, "rb") as f:
        data = f.read()

    width = height = None
    bit_depth = color_type = None
    interlace = None
    idat = bytearray()

    for ctype, cdata, _ in read_chunks(data):
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", cdata
            )
            if comp != 0 or filt != 0:
                raise ValueError("Unsupported PNG compression/filter")
        elif ctype == b"IDAT":
            idat.extend(cdata)
        elif ctype == b"IEND":
            break

    if width is None:
        raise ValueError("Missing IHDR")
    if interlace != 0:
        raise ValueError("Interlaced PNG not supported")
    if bit_depth != 8:
        raise ValueError(f"Unsupported bit depth {bit_depth}")
    if color_type not in (2, 6):
        raise ValueError(f"Unsupported color type {color_type}")

    bpp = 3 if color_type == 2 else 4
    raw = zlib.decompress(idat)
    pixels = unfilter_scanlines(raw, width, height, bpp)

    out = []
    for i in range(0, len(pixels), bpp):
        r = pixels[i]
        g = pixels[i + 1]
        b = pixels[i + 2]
        if bpp == 4:
            a = pixels[i + 3]
            if a != 255:
                r = (r * a) // 255
                g = (g * a) // 255
                b = (b * a) // 255
        out.append(rgb565(r, g, b))

    return width, height, out


def resize_contain_nearest(src_pixels, src_w, src_h, dst_w, dst_h, background=0x0000):
    if src_w <= 0 or src_h <= 0 or dst_w <= 0 or dst_h <= 0:
        raise ValueError("Invalid dimensions")

    scale_num = min(dst_w / src_w, dst_h / src_h)
    scaled_w = max(1, int(round(src_w * scale_num)))
    scaled_h = max(1, int(round(src_h * scale_num)))

    x_off = (dst_w - scaled_w) // 2
    y_off = (dst_h - scaled_h) // 2

    dst = [background] * (dst_w * dst_h)
    for y in range(scaled_h):
        src_y = int(y * src_h / scaled_h)
        row_src = src_y * src_w
        row_dst = (y + y_off) * dst_w + x_off
        for x in range(scaled_w):
            src_x = int(x * src_w / scaled_w)
            dst[row_dst + x] = src_pixels[row_src + src_x]
    return dst


def rle_encode_rgb565(pixels):
    if not pixels:
        return []
    out = []
    run_color = pixels[0]
    run_len = 1
    for c in pixels[1:]:
        if c == run_color and run_len < 0xFFFF:
            run_len += 1
            continue
        out.append((run_len, run_color))
        run_color = c
        run_len = 1
    out.append((run_len, run_color))
    return out


def write_header(images, out_path, *, rle=False):
    with open(out_path, "w", newline="\n") as f:
        f.write("#pragma once\n")
        f.write("#include <avr/pgmspace.h>\n\n")
        for name, (w, h, data) in images.items():
            f.write(f"#define {name}_WIDTH {w}\n")
            f.write(f"#define {name}_HEIGHT {h}\n")
            if rle:
                runs = rle_encode_rgb565(data)
                f.write(f"#define {name}_PIXELS ({w}u * {h}u)\n")
                f.write(f"static const uint16_t PROGMEM {name}_RLE[] = {{\n")
                # Store (run_len, color) pairs.
                flat = []
                for run_len, color in runs:
                    flat.append(run_len)
                    flat.append(color)
                for i in range(0, len(flat), 12):
                    chunk = ", ".join(f"0x{v:04X}" for v in flat[i:i+12])
                    f.write(f"    {chunk},\n")
                f.write("};\n\n")
            else:
                f.write(f"static const uint16_t PROGMEM {name}[] = {{\n")
                for i in range(0, len(data), 12):
                    chunk = ", ".join(f"0x{v:04X}" for v in data[i:i+12])
                    f.write(f"    {chunk},\n")
                f.write("};\n\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--resize", default="", help="Resize to WxH (nearest, contain). Example: 128x160")
    ap.add_argument("--background", default="0x0000", help="Background RGB565 (hex). Default: 0x0000")
    ap.add_argument("--rle", action="store_true", help="Emit RLE-compressed image data")
    ap.add_argument("images", nargs="+")
    args = ap.parse_args()

    resize_w = resize_h = None
    if args.resize:
        if "x" not in args.resize:
            raise SystemExit("--resize must be WxH, e.g. 128x160")
        w_s, h_s = args.resize.lower().split("x", 1)
        resize_w = int(w_s, 10)
        resize_h = int(h_s, 10)
        if resize_w <= 0 or resize_h <= 0:
            raise SystemExit("--resize dimensions must be positive")

    background = int(args.background, 16)
    if not (0 <= background <= 0xFFFF):
        raise SystemExit("--background must be a 16-bit hex value, e.g. 0x0000")

    images = {}
    for path in args.images:
        base = os.path.splitext(os.path.basename(path))[0]
        name = base.upper()
        name = "IMG_" + "".join(ch if ch.isalnum() else "_" for ch in name)
        w, h, data = png_to_rgb565_array(path)
        if resize_w is not None:
            data = resize_contain_nearest(data, w, h, resize_w, resize_h, background=background)
            w, h = resize_w, resize_h
        images[name] = (w, h, data)

    write_header(images, args.out, rle=args.rle)


if __name__ == "__main__":
    main()
