/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

namespace Ui {
class ElasticScroll;
} // namespace Ui

namespace HistoryView {

class TouchScroll final : public base::has_weak_ptr {
public:
	TouchScroll(
		Ui::ElasticScroll *scroll,
		Fn<bool(QPoint delta, bool pressed)> fallback);

	void start(QPoint travel);
	[[nodiscard]] bool fling(QPoint delta);
	void release();
	void finish(bool cancelled = false);

	// Returns false if this object was destroyed during the step.
	[[nodiscard]] bool drag(QPoint delta);

private:
	bool scrollBy(QPoint delta, bool pressed);

	Ui::ElasticScroll *_scroll = nullptr;
	Fn<bool(QPoint delta, bool pressed)> _fallback;
	bool _elastic = false;

};

} // namespace HistoryView
