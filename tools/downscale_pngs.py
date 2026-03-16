#!/usr/bin/env python3
import argparse
import os
from pathlib import Path

try:
    from PIL import Image
except Exception as exc:
    raise SystemExit(
        "Pillow is required. Install with: python3 -m pip install Pillow"
    ) from exc


def resize_contain(src: Image.Image, width: int, height: int, background: str) -> Image.Image:
    src = src.convert("RGBA")
    out = Image.new("RGBA", (width, height), background)
    src.thumbnail((width, height), Image.NEAREST)
    x = (width - src.width) // 2
    y = (height - src.height) // 2
    out.paste(src, (x, y), src)
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description="Downscale PNGs to fit a target display.")
    ap.add_argument("--width", type=int, default=128, help="Target width (default: 128)")
    ap.add_argument("--height", type=int, default=160, help="Target height (default: 160)")
    ap.add_argument("--background", default="#00000000", help="Background color (default: transparent)")
    ap.add_argument("--out-dir", default="scaled", help="Output directory (default: scaled)")
    ap.add_argument("--overwrite", action="store_true", help="Overwrite files in place")
    ap.add_argument("paths", nargs="*", help="PNG files or directories (default: current directory)")
    args = ap.parse_args()

    if args.width <= 0 or args.height <= 0:
        raise SystemExit("Width/height must be positive.")

    if args.overwrite:
        out_dir = None
    else:
        out_dir = Path(args.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)

    targets = []
    if args.paths:
        for p in args.paths:
            path = Path(p)
            if path.is_dir():
                targets.extend(path.glob("*.png"))
            else:
                targets.append(path)
    else:
        targets.extend(Path.cwd().glob("*.png"))

    for src_path in targets:
        if not src_path.is_file():
            continue
        with Image.open(src_path) as img:
            out = resize_contain(img, args.width, args.height, args.background)
            if out_dir is None:
                dst_path = src_path
            else:
                dst_path = out_dir / src_path.name
            out.convert("RGBA").save(dst_path)
            print(f"{src_path} -> {dst_path}")


if __name__ == "__main__":
    main()
