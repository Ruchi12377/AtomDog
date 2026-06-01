#!/usr/bin/env python3
"""Split a sprite sheet PNG into a grid of individual PNG files."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def content_bbox(tile: Image.Image, alpha_threshold: int = 10) -> tuple[int, int, int, int] | None:
    """Return (left, top, right, bottom) of non-transparent pixels, or None if empty."""
    if tile.mode != "RGBA":
        tile = tile.convert("RGBA")
    alpha = tile.getchannel("A")
    mask = alpha.point(lambda a: 255 if a > alpha_threshold else 0)
    return mask.getbbox()


def trim_edge_bleed(
    bbox: tuple[int, int, int, int],
    tile_w: int,
    tile_h: int,
    edge_trim: int,
) -> tuple[int, int, int, int]:
    """Shrink bbox on sides that touch the cell edge (neighbor sprite bleed)."""
    left, top, right, bottom = bbox
    if left == 0:
        left = min(left + edge_trim, right)
    if top == 0:
        top = min(top + edge_trim, bottom)
    if right == tile_w - 1:
        right = max(right - edge_trim, left)
    if bottom == tile_h - 1:
        bottom = max(bottom - edge_trim, top)
    return left, top, right, bottom


def content_centroid(tile: Image.Image, alpha_threshold: int = 48) -> tuple[float, float] | None:
    """Alpha-weighted centroid of opaque pixels (ignores faint edge halos)."""
    rgba = tile.convert("RGBA")
    px = rgba.load()
    w, h = rgba.size
    sum_x = 0.0
    sum_y = 0.0
    sum_w = 0.0
    for y in range(h):
        for x in range(w):
            a = px[x, y][3]
            if a > alpha_threshold:
                weight = float(a)
                sum_x += x * weight
                sum_y += y * weight
                sum_w += weight
    if sum_w <= 0:
        return None
    return sum_x / sum_w, sum_y / sum_w


def center_on_canvas(
    tile: Image.Image,
    canvas_w: int,
    canvas_h: int,
    *,
    bbox_threshold: int = 10,
    centroid_threshold: int = 48,
    edge_trim: int = 3,
) -> Image.Image:
    if tile.mode != "RGBA":
        tile = tile.convert("RGBA")

    bbox = content_bbox(tile, bbox_threshold)
    if bbox is None:
        return Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))

    bbox = trim_edge_bleed(bbox, tile.width, tile.height, edge_trim)
    content = tile.crop(bbox)

    centroid = content_centroid(content, centroid_threshold)
    if centroid is None:
        centroid = content_centroid(content, bbox_threshold)
    if centroid is not None:
        cx, cy = centroid
        x = int(round(canvas_w / 2 - cx))
        y = int(round(canvas_h / 2 - cy))
    else:
        x = (canvas_w - content.width) // 2
        y = (canvas_h - content.height) // 2

    canvas = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    canvas.paste(content, (x, y), content)
    return canvas


def split_sprite_sheet(
    src: Path,
    out_dir: Path,
    cols: int,
    rows: int,
    prefix: str,
    center: bool,
    edge_trim: int,
    centroid_threshold: int,
) -> list[Path]:
    img = Image.open(src)
    width, height = img.size

    if width % cols != 0 or height % rows != 0:
        raise ValueError(
            f"Image size {width}x{height} is not evenly divisible by {cols}x{rows}"
        )

    tile_w = width // cols
    tile_h = height // rows
    out_dir.mkdir(parents=True, exist_ok=True)

    written: list[Path] = []
    index = 0
    for row in range(rows):
        for col in range(cols):
            left = col * tile_w
            top = row * tile_h
            tile = img.crop((left, top, left + tile_w, top + tile_h))
            if center:
                tile = center_on_canvas(
                    tile,
                    tile_w,
                    tile_h,
                    edge_trim=edge_trim,
                    centroid_threshold=centroid_threshold,
                )
            out_path = out_dir / f"{prefix}{index}.png"
            tile.save(out_path, format="PNG")
            written.append(out_path)
            index += 1

    return written


def main() -> None:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=here / "faces.png",
        help="Source sprite sheet (default: faces.png next to this script)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=here,
        help="Directory for output PNGs (default: same as script)",
    )
    parser.add_argument("--cols", type=int, default=4, help="Number of columns")
    parser.add_argument("--rows", type=int, default=3, help="Number of rows")
    parser.add_argument(
        "--prefix",
        default="miro_",
        help="Output filename prefix (default: miro_)",
    )
    parser.add_argument(
        "--no-center",
        action="store_true",
        help="Skip centering non-transparent content in each tile",
    )
    parser.add_argument(
        "--edge-trim",
        type=int,
        default=3,
        help="Pixels to ignore on cell edges when content touches the border (default: 3)",
    )
    parser.add_argument(
        "--centroid-threshold",
        type=int,
        default=48,
        help="Alpha threshold for centroid calculation (default: 48)",
    )
    args = parser.parse_args()

    paths = split_sprite_sheet(
        args.input,
        args.output_dir,
        args.cols,
        args.rows,
        args.prefix,
        center=not args.no_center,
        edge_trim=args.edge_trim,
        centroid_threshold=args.centroid_threshold,
    )
    print(f"Wrote {len(paths)} tiles ({args.cols}x{args.rows}) to {args.output_dir}")
    for path in paths:
        print(f"  {path.name}")

    if not args.no_center:
        print("Tip: run export_faces_jpg.py to generate AtomS3R-friendly miro_*.jpg")


if __name__ == "__main__":
    main()
