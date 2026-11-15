/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "tgw/tgw_backend.h"
#include "tgw/tgw_painter.h"

namespace Tgw {

class Canvases final {
public:
	explicit Canvases(QtBackend::Diagnostic diagnostic);
	~Canvases();

	void run(
		int id,
		int width,
		int height,
		const std::vector<twlite::CanvasOp> &ops,
		const QtBackend &backend);
	[[nodiscard]] bool read(
		int id,
		int x,
		int y,
		int width,
		int height,
		std::vector<uint8_t> &rgba);
	void release(int id);
	void refill();
	[[nodiscard]] const QImage *bitmap(int id) const;

private:
	class Executor;

	enum class Limit : uchar {
		Work,
		Memory,
	};

	[[nodiscard]] QImage *allocate(int id, QSize size);
	void discard(int id);
	[[nodiscard]] QSize size(int id) const;
	[[nodiscard]] bool chargePixels(int64 pixels);
	void exhaust();
	void noteLimit(Limit limit);
	void noteMode(int index);
	void diagnose(std::string_view kind, std::string_view detail) const;

	const QtBackend::Diagnostic _diagnostic;
	base::flat_map<int, QImage> _bitmaps;
	base::flat_map<int, QSize> _sizes;
	PaintAllowance _allowance;
	int64 _bytes = 0;
	int64 _pixels = 0;
	uchar _notedModes = 0;
	uchar _notedLimits = 0;
	bool _exhausted = false;

};

} // namespace Tgw
