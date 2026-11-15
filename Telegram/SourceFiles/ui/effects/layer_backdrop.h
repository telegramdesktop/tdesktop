/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "ui/effects/animations.h"

namespace Ui {

class LayerWidget;
class RpWidget;

[[nodiscard]] QImage CaptureUnderLayer(
	not_null<LayerWidget*> layer,
	LayerWidget *underlying = nullptr,
	Fn<void()> beforeRender = nullptr);

// Content covered by a layer skips its paint as overlapped, except while
// CaptureUnderLayer renders it.
[[nodiscard]] bool CapturingUnderLayer();

class LayerBackdrop final : public base::has_weak_ptr {
public:
	LayerBackdrop(
		not_null<RpWidget*> owner,
		Fn<QImage()> capture,
		Fn<int(bool night)> overlayAlpha);

	void start();
	void start(QImage image);
	[[nodiscard]] bool started() const;
	void invalidate();

	void paint(QPainter &p) const;
	void paintDim(QPainter &p, float64 opacity) const;
	[[nodiscard]] rpl::producer<> changes() const;

private:
	void checkStale();
	void checkCache();
	void cache();
	void ready(QImage image, bool night, int generation);
	void startFade();

	const not_null<RpWidget*> _owner;
	const Fn<QImage()> _capture;
	const Fn<int(bool night)> _overlayAlpha;
	QImage _back;
	QImage _image;
	QImage _next;
	Animations::Simple _fade;
	base::Timer _timer;
	rpl::event_stream<> _changes;
	crl::time _lastAreaChangeTime = 0;
	int _generation = 0;
	int _readyGeneration = 0;
	bool _caching = false;
	bool _night = false;
	rpl::lifetime _lifetime;

};

} // namespace Ui
