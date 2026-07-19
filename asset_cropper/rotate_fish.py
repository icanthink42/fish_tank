#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


ANGLES = {
    "fish_1_u": 90,
    "fish_2_u": -25,
    "fish_3_u": -90,
    "fish_4_u": 90,
    "fish_5_u": 90,
    "fish_6_u": -150,
    "fish_7_u": -90,
    "fish_8_u": 125,
    "fish_9_u": 90,
    "fish_10_u": -90,
    "fish_11_u": -90,
    "fish_12_u": 95,
}


def rotate_image(source: Path, destination: Path, angle: float, padding: int) -> tuple[int, int]:
    image = Image.open(source).convert("RGBA")
    rotated = image.rotate(angle, resample=Image.BICUBIC, expand=True)

    alpha = np.asarray(rotated)[:, :, 3]
    ys, xs = np.nonzero(alpha > 12)
    if not len(xs):
        raise ValueError("no foreground was found")

    left = max(0, int(xs.min()) - padding)
    top = max(0, int(ys.min()) - padding)
    right = min(rotated.width, int(xs.max()) + padding + 1)
    bottom = min(rotated.height, int(ys.max()) + padding + 1)

    destination.parent.mkdir(parents=True, exist_ok=True)
    rotated.crop((left, top, right, bottom)).save(destination)
    return right - left, bottom - top


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Rotate cropped fish so they all face left."
    )
    parser.add_argument("input", nargs="?", type=Path, default=Path("after"))
    parser.add_argument("output", nargs="?", type=Path, default=Path("final"))
    parser.add_argument("--padding", type=int, default=2, help="transparent pixels around the crop")
    args = parser.parse_args()

    if not args.input.is_dir():
        parser.error(f"input directory does not exist: {args.input}")

    failures = 0
    for source in sorted(args.input.glob("*.png")):
        if source.stem not in ANGLES:
            failures += 1
            print(f"ERROR: {source}: no rotation angle defined")
            continue
        destination = args.output / source.name
        try:
            width, height = rotate_image(source, destination, ANGLES[source.stem], args.padding)
            print(f"{source.name} -> {destination} ({width}x{height})")
        except Exception as exc:
            failures += 1
            print(f"ERROR: {source}: {exc}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
