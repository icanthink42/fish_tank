#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage


IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".tif", ".tiff"}


def clear_outside_border(pixels: np.ndarray) -> np.ndarray:
    rgb = pixels[:, :, :3].astype(np.float32) / 255.0
    alpha = pixels[:, :, 3]
    maximum = rgb.max(axis=2)
    minimum = rgb.min(axis=2)
    chroma = maximum - minimum
    saturation = chroma / np.maximum(maximum, 1e-6)
    visible = alpha > 12

    small = visible.sum() < 20000
    if small:
        sat_cutoff, dark_cutoff, weak_cutoff = 0.25, 0.35, 0.09
        border_min, fish_min = 4, 30
        dilate, erode = 1, 2
    else:
        ring = visible & ~ndimage.binary_erosion(visible, iterations=6)
        if not ring.any():
            ring = visible
        sat_cutoff = max(0.25, float(np.quantile(saturation[ring], 0.9)) + 0.15)
        dark_cutoff = float(np.clip(np.median(maximum[ring]) - 0.3, 0.25, 0.6))
        weak_cutoff = max(0.09, float(np.quantile(chroma[ring], 0.9)) + 0.04)
        border_min, fish_min = 12, 200
        dilate, erode = 2, 3

    strong = ((saturation > sat_cutoff) | (maximum < dark_cutoff)) & visible
    weak = (chroma > weak_cutoff) & visible
    border = ndimage.binary_propagation(strong, mask=strong | weak)

    labels, count = ndimage.label(border)
    if count:
        sizes = np.bincount(labels.ravel())
        big = sizes >= border_min
        big[0] = False
        border = big[labels]

    fish = ndimage.binary_dilation(border, iterations=dilate)
    fish = ndimage.binary_fill_holes(fish)
    fish = ndimage.binary_erosion(fish, iterations=erode)

    labels, count = ndimage.label(fish)
    if count:
        sizes = np.bincount(labels.ravel())
        big = sizes >= fish_min
        big[0] = False
        fish = big[labels]

    result = alpha.copy()
    result[~fish] = 0
    return result


def process_image(source: Path, destination: Path, padding: int) -> tuple[int, int]:
    image = Image.open(source).convert("RGBA")
    pixels = np.asarray(image).copy()

    pixels[:, :, 3] = clear_outside_border(pixels)
    ys, xs = np.nonzero(pixels[:, :, 3] > 12)
    if not len(xs):
        raise ValueError("no foreground was found")

    left = max(0, int(xs.min()) - padding)
    top = max(0, int(ys.min()) - padding)
    right = min(image.width, int(xs.max()) + padding + 1)
    bottom = min(image.height, int(ys.max()) + padding + 1)

    destination.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(pixels).crop((left, top, right, bottom)).save(destination)
    return right - left, bottom - top


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Tightly crop fish images to their non-transparent content."
    )
    parser.add_argument("input", nargs="?", type=Path, default=Path("before"))
    parser.add_argument("output", nargs="?", type=Path, default=Path("after"))
    parser.add_argument("--padding", type=int, default=2, help="transparent pixels around the crop")
    args = parser.parse_args()

    if args.padding < 0:
        parser.error("--padding cannot be negative")
    if not args.input.is_dir():
        parser.error(f"input directory does not exist: {args.input}")

    sources = sorted(p for p in args.input.iterdir() if p.suffix.lower() in IMAGE_SUFFIXES)
    if not sources:
        parser.error(f"no supported images found in {args.input}")

    failures = 0
    for source in sources:
        destination = args.output / f"{source.stem}.png"
        try:
            width, height = process_image(source, destination, args.padding)
            print(f"{source.name} -> {destination} ({width}x{height})")
        except Exception as exc:
            failures += 1
            print(f"ERROR: {source}: {exc}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
