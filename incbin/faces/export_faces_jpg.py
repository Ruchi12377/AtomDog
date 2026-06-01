#!/usr/bin/env python3
"""Export centered face PNGs to small JPEGs for AtomS3R (embedded via incbin)."""

from __future__ import annotations

import argparse
import io
from pathlib import Path

from PIL import Image

# M5.Display.fillScreen(TFT_BLACK) in MiroSpriteFace.h
FILL_RGB565 = 0x0000


def rgb565_to_rgb(color: int) -> tuple[int, int, int]:
    r = ((color >> 11) & 0x1F) * 255 // 31
    g = ((color >> 5) & 0x3F) * 255 // 63
    b = (color & 0x1F) * 255 // 31
    return r, g, b


def rgba_to_jpeg_bytes(
    image: Image.Image,
    background: tuple[int, int, int],
    size: int,
    quality: int,
) -> bytes:
    rgba = image.convert("RGBA")
    rgb = Image.new("RGB", rgba.size, background)
    rgb.paste(rgba, mask=rgba.getchannel("A"))
    if rgb.width != size or rgb.height != size:
        rgb = rgb.resize((size, size), Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    rgb.save(
        buf,
        format="JPEG",
        quality=quality,
        optimize=True,
        subsampling="4:2:0",
    )
    return buf.getvalue()


def iter_face_pngs(input_dir: Path, pattern: str, count: int) -> list[Path]:
    if count > 0:
        paths = [input_dir / f"miro_{i}.png" for i in range(count)]
        return [p for p in paths if p.is_file()]
    return sorted(p for p in input_dir.glob(pattern) if p.is_file())


def export_faces(
    input_dir: Path,
    *,
    pattern: str,
    count: int,
    size: int,
    quality: int,
    background: tuple[int, int, int],
) -> list[tuple[Path, int]]:
    written: list[tuple[Path, int]] = []
    for png_path in iter_face_pngs(input_dir, pattern, count):
        jpg_path = png_path.with_suffix(".jpg")
        data = rgba_to_jpeg_bytes(
            Image.open(png_path),
            background,
            size,
            quality,
        )
        jpg_path.write_bytes(data)
        written.append((jpg_path, len(data)))
    return written


def main() -> None:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=here,
        help="Directory containing miro_*.png (default: script directory)",
    )
    parser.add_argument(
        "--pattern",
        default="miro_*.png",
        help="PNG glob when --count is 0 (default: miro_*.png)",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=6,
        help="Export miro_0..miro_{count-1} only (default: 6 basic expressions)",
    )
    parser.add_argument(
        "--size",
        type=int,
        default=112,
        help="Output square size in pixels (default: 112)",
    )
    parser.add_argument(
        "--quality",
        type=int,
        default=40,
        help="JPEG quality 1-95 (default: 40)",
    )
    parser.add_argument(
        "--bg",
        type=lambda s: int(s, 0),
        default=FILL_RGB565,
        help="Background RGB565 color matching fillScreen (default: 0x0000 black)",
    )
    args = parser.parse_args()

    if not 1 <= args.quality <= 95:
        raise SystemExit("--quality must be between 1 and 95")
    if args.size < 32:
        raise SystemExit("--size must be at least 32")

    bg = rgb565_to_rgb(args.bg)
    files = export_faces(
        args.input_dir,
        pattern=args.pattern,
        count=args.count,
        size=args.size,
        quality=args.quality,
        background=bg,
    )
    if not files:
        raise SystemExit(f"No files matched {args.pattern!r} in {args.input_dir}")

    total = sum(n for _, n in files)
    print(
        f"Exported {len(files)} JPEGs @ {args.size}px q={args.quality} "
        f"(bg RGB{bg}, total {total:,} bytes, avg {total // len(files):,})"
    )
    for path, nbytes in files:
        print(f"  {path.name}: {nbytes:,} bytes")


if __name__ == "__main__":
    main()
