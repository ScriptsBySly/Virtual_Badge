#!/usr/bin/env python3
import argparse
import os

try:
    from PIL import Image
except Exception as exc:
    raise SystemExit("Pillow is required. Install with: python3 -m pip install Pillow") from exc


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def resize_contain_nearest(img, width, height, background=(0, 0, 0, 0)):
    img = img.convert("RGBA")
    out = Image.new("RGBA", (width, height), background)
    img.thumbnail((width, height), Image.NEAREST)
    x = (width - img.width) // 2
    y = (height - img.height) // 2
    out.paste(img, (x, y), img)
    return out


def composite_images(base_path, overlay_path):
    base = Image.open(base_path).convert("RGBA")
    overlay = Image.open(overlay_path).convert("RGBA")
    if overlay.size != base.size:
        composed = base.copy()
        composed.paste(overlay, (0, 0), overlay)
    else:
        composed = Image.alpha_composite(base, overlay)
    return composed


def write_raw(img, out_path):
    pixels = img.convert("RGBA").tobytes()
    out = bytearray()
    for i in range(0, len(pixels), 4):
        r, g, b, a = pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]
        if a != 255:
            r = (r * a) // 255
            g = (g * a) // 255
            b = (b * a) // 255
        v = rgb565(r, g, b)
        out.append((v >> 8) & 0xFF)
        out.append(v & 0xFF)
    with open(out_path, "wb") as f:
        f.write(out)


def main():
    ap = argparse.ArgumentParser(description="Composite overlays into base images and output RAW files.")
    ap.add_argument("--width", type=int, default=128, help="Target width (default: 128)")
    ap.add_argument("--height", type=int, default=160, help="Target height (default: 160)")
    ap.add_argument("--out-dir", default=".", help="Output directory (default: current directory)")
    args = ap.parse_args()

    mappings = [
        ("HU_Happy.png", "Heart_1.png", "HUHAPPY.RAW"),
        ("HD_Happy.png", "Heart_2.png", "HDHAPPY.RAW"),
        ("HU_Sad_New.png", "Broken_1.png", "HUSADNEW.RAW"),
        ("HD_Sad_New.png", "Broken_2.png", "HDSADNEW.RAW"),
        ("HU_Mad.png", "Mad_1.png", "HUMAD.RAW"),
        ("HD_Mad.png", "Mad_2.png", "HDMAD.RAW"),
    ]

    for base_name, overlay_name, out_name in mappings:
        if not os.path.exists(base_name):
            raise SystemExit(f"Missing base image: {base_name}")
        if not os.path.exists(overlay_name):
            raise SystemExit(f"Missing overlay image: {overlay_name}")

        composed = composite_images(base_name, overlay_name)
        composed = resize_contain_nearest(composed, args.width, args.height)
        out_path = os.path.join(args.out_dir, out_name)
        write_raw(composed, out_path)
        print(f"{base_name} + {overlay_name} -> {out_path}")


if __name__ == "__main__":
    main()
