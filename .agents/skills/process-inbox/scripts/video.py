#!/usr/bin/env python3
"""Turn a supplied reference video into timed, citable frames.

Subcommands:
	probe    print size, codec, frame count, frame timing and digest as JSON
	extract  write frames as frame-NNN-TTTTms[-label].png
	sheet    write one labelled contact sheet of a frame range

Frame numbers are decoded frame indices from 0. Times are each frame's own
presentation timestamp minus the first frame's, so they stay exact for
variable-frame-rate screen recordings, where index / fps drifts.

Needs ffmpeg and ffprobe on PATH; `sheet` also needs Pillow.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


CROP_PATTERN = re.compile(r"(\d+)x(\d+)\+(\d+)\+(\d+)")


class VideoError(Exception):
	pass


def require_tool(name):
	if not shutil.which(name):
		raise VideoError(
			f"{name} is not on PATH. Install ffmpeg (brew install ffmpeg, "
			"apt install ffmpeg, or winget install ffmpeg) and retry."
		)


def run(args):
	result = subprocess.run(args, capture_output=True, text=True)
	if result.returncode:
		raise VideoError(f"{args[0]} failed:\n{result.stderr.strip()}")
	return result.stdout


def frame_times(video):
	require_tool("ffprobe")
	output = run([
		"ffprobe", "-v", "error", "-select_streams", "v:0",
		"-show_entries", "frame=best_effort_timestamp_time",
		"-of", "csv=p=0", str(video),
	])
	values = [float(line.split(",")[0]) for line in output.split() if line.strip()]
	if not values:
		raise VideoError(f"No video frames decoded from {video}")
	start = values[0]
	return [value - start for value in values]


def sha256(path):
	digest = hashlib.sha256()
	with open(path, "rb") as stream:
		for chunk in iter(lambda: stream.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def parse_frames(value, count):
	"""Parse '0,30,119-134' or 'all' into sorted unique indices."""
	if value in (None, "all"):
		return list(range(count))
	result = set()
	for part in value.split(","):
		part = part.strip()
		if not part:
			continue
		if "-" in part:
			first, last = (int(x) for x in part.split("-", 1))
			result.update(range(first, last + 1))
		else:
			result.add(int(part))
	bad = [index for index in result if index < 0 or index >= count]
	if bad:
		raise VideoError(f"Frames out of range 0-{count - 1}: {sorted(bad)[:10]}")
	return sorted(result)


def parse_crop(value):
	if not value:
		return None
	match = CROP_PATTERN.fullmatch(value)
	if not match:
		raise VideoError(f"Crop must be WxH+X+Y in video pixels, not {value!r}")
	return tuple(int(x) for x in match.groups())


def filter_chain(indices, crop, scale):
	# One select term per contiguous run keeps the expression short.
	runs = []
	for index in indices:
		if runs and runs[-1][1] == index - 1:
			runs[-1][1] = index
		else:
			runs.append([index, index])
	terms = "+".join(
		f"eq(n\\,{a})" if a == b else f"between(n\\,{a}\\,{b})"
		for a, b in runs
	)
	chain = [f"select='{terms}'"]
	if crop:
		w, h, x, y = crop
		chain.append(f"crop={w}:{h}:{x}:{y}")
	if scale != 1:
		# Enlarge with hard pixels for measuring; shrink smoothly for overviews.
		flags = "neighbor" if scale > 1 else "area"
		chain.append(f"scale=trunc(iw*{scale}):trunc(ih*{scale}):flags={flags}")
	return ",".join(chain)


def decode(video, indices, crop, scale, directory):
	"""Decode the chosen frames as PNGs in index order; return their paths."""
	require_tool("ffmpeg")
	pattern = Path(directory) / "raw-%06d.png"
	run([
		"ffmpeg", "-v", "error", "-i", str(video),
		"-vf", filter_chain(indices, crop, scale),
		"-fps_mode", "passthrough", str(pattern),
	])
	paths = sorted(Path(directory).glob("raw-*.png"))
	if len(paths) != len(indices):
		raise VideoError(
			f"Decoded {len(paths)} frames, expected {len(indices)}; "
			"the stream may not decode deterministically."
		)
	return paths


def frame_name(index, time, count, label):
	width = max(3, len(str(count - 1)))
	name = f"frame-{index:0{width}d}-{round(time * 1000):04d}ms"
	return f"{name}-{label}.png" if label else f"{name}.png"


def command_probe(args):
	require_tool("ffprobe")
	stream = json.loads(run([
		"ffprobe", "-v", "error", "-select_streams", "v:0",
		"-show_entries", "stream=codec_name,width,height,r_frame_rate,avg_frame_rate",
		"-show_entries", "format=duration", "-of", "json", str(args.video),
	]))
	info = stream["streams"][0]
	times = frame_times(args.video)
	deltas = [b - a for a, b in zip(times, times[1:])]
	nominal = info["r_frame_rate"]
	numerator, denominator = (int(x) for x in nominal.split("/"))
	period = denominator / numerator if numerator else 0
	result = {
		"file": Path(args.video).name,
		"sha256": sha256(args.video),
		"bytes": Path(args.video).stat().st_size,
		"codec": info["codec_name"],
		"width": info["width"],
		"height": info["height"],
		"nominal_fps": nominal,
		"frames": len(times),
		"last_frame_ms": round(times[-1] * 1000, 3),
		"duration_s": float(stream["format"]["duration"]),
	}
	if deltas:
		result["min_interval_ms"] = round(min(deltas) * 1000, 3)
		result["max_interval_ms"] = round(max(deltas) * 1000, 3)
		# Constant when every interval is within a millisecond of nominal.
		result["constant_frame_rate"] = all(
			abs(delta - period) < 0.001 for delta in deltas
		)
	print(json.dumps(result, indent=2))


def command_extract(args):
	times = frame_times(args.video)
	indices = parse_frames(args.frames, len(times))
	out = Path(args.out)
	out.mkdir(parents=True, exist_ok=True)
	crop = parse_crop(args.crop)
	written = []
	with tempfile.TemporaryDirectory() as directory:
		paths = decode(args.video, indices, crop, args.scale, directory)
		for index, path in zip(indices, paths):
			target = out / frame_name(index, times[index], len(times), args.label)
			shutil.move(str(path), target)
			written.append(target.name)
	print(json.dumps({"out": str(out), "written": len(written),
		"first": written[0], "last": written[-1]}, indent=2))


def load_font(size):
	from PIL import ImageFont
	for name in ("DejaVuSansMono.ttf", "Menlo.ttc", "consola.ttf", "Arial.ttf"):
		try:
			return ImageFont.truetype(name, size)
		except OSError:
			continue
	return ImageFont.load_default()


def command_sheet(args):
	try:
		from PIL import Image, ImageDraw
	except ImportError:
		raise VideoError("sheet needs Pillow: python3 -m pip install --user pillow")
	times = frame_times(args.video)
	indices = parse_frames(args.frames, len(times))[::args.step]
	if len(indices) > 200:
		raise VideoError(f"{len(indices)} frames is too many for one sheet; raise --step")
	crop = parse_crop(args.crop)
	with tempfile.TemporaryDirectory() as directory:
		paths = decode(args.video, indices, crop, args.scale, directory)
		images = [Image.open(path).convert("RGB") for path in paths]
	cell_w, cell_h = images[0].size
	label_h = max(16, cell_w // 10)
	font = load_font(label_h - 4)
	columns = min(args.columns, len(images))
	rows = math.ceil(len(images) / columns)
	gap = 4
	sheet = Image.new(
		"RGB",
		(columns * (cell_w + gap) + gap, rows * (cell_h + label_h + gap) + gap),
		(255, 0, 255),
	)
	draw = ImageDraw.Draw(sheet)
	for slot, (index, image) in enumerate(zip(indices, images)):
		x = gap + (slot % columns) * (cell_w + gap)
		y = gap + (slot // columns) * (cell_h + label_h + gap)
		draw.rectangle([x, y, x + cell_w - 1, y + label_h - 1], fill=(0, 0, 0))
		draw.text(
			(x + 2, y + 1),
			f"f{index} {round(times[index] * 1000)}ms",
			fill=(255, 255, 255),
			font=font,
		)
		sheet.paste(image, (x, y + label_h))
	out = Path(args.out)
	out.parent.mkdir(parents=True, exist_ok=True)
	sheet.save(out)
	print(json.dumps({"out": str(out), "frames": len(indices),
		"first": indices[0], "last": indices[-1], "size": sheet.size}, indent=2))


def main():
	parser = argparse.ArgumentParser(description=__doc__,
		formatter_class=argparse.RawDescriptionHelpFormatter)
	commands = parser.add_subparsers(dest="command", required=True)

	probe = commands.add_parser("probe")
	probe.add_argument("video")
	probe.set_defaults(handler=command_probe)

	def add_selection(sub):
		sub.add_argument("video")
		sub.add_argument("--frames", default="all",
			help="'all' or indices and ranges, e.g. 0,30,119-134")
		sub.add_argument("--crop", help="WxH+X+Y in video pixels")
		sub.add_argument("--scale", type=float, default=1,
			help="above 1 enlarges nearest-neighbour, below 1 shrinks")
		sub.add_argument("--out", required=True)

	extract = commands.add_parser("extract")
	add_selection(extract)
	extract.add_argument("--label", help="name suffix, e.g. corner or card")
	extract.set_defaults(handler=command_extract)

	sheet = commands.add_parser("sheet")
	add_selection(sheet)
	sheet.add_argument("--step", type=int, default=1)
	sheet.add_argument("--columns", type=int, default=8)
	sheet.set_defaults(handler=command_sheet)

	args = parser.parse_args()
	try:
		args.handler(args)
	except VideoError as error:
		print(f"error: {error}", file=sys.stderr)
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
