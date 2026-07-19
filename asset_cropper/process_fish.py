#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".tif", ".tiff"}


def process_image(source: Path, destination: Path, padding: int) -> tuple[int, int]:
    image = Image.open(source).convert("RGBA")
    alpha = np.asarray(image)[:, :, 3]

    ys, xs = np.nonzero(alpha > 12)
    if not len(xs):
        raise ValueError("no foreground was found")

    left = max(0, int(xs.min()) - padding)
    top = max(0, int(ys.min()) - padding)
    right = min(image.width, int(xs.max()) + padding + 1)
    bottom = min(image.height, int(ys.max()) + padding + 1)

    destination.parent.mkdir(parents=True, exist_ok=True)
    image.crop((left, top, right, bottom)).save(destination)
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
