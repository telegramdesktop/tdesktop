# Fix FrameGenerator destination buffer bounds

FFmpeg::FrameGenerator now sizes and validates the writable destination
storage before any pixel write, so a centered native BGRA copy or a
swscale conversion into a letterboxed frame cannot run past the storage:
the storage helpers accept a minimum row pitch that covers the shifted,
SIMD-rounded converter span, the native path copies only the visible BGRA
bytes of each row for both source stride signs, margins and row padding
are cleared by the actual dimension differences, decoded plane metadata
is validated first, and rotated output is produced from a private
pre-rotation canvas instead of the caller's returned image.

Discovered by Anthropic's OSS Scanner, as vulnerability ANT-2026-K8086Q1Y.
