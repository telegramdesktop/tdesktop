/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "ui/effects/animations.h"

#include <QImage>

namespace Ui {
class RpWidget;
class RpWidgetWrap;
} // namespace Ui

namespace Ui {

class ThanosEffectRenderer;

class ThanosEffect final : public base::has_weak_ptr {
public:
	explicit ThanosEffect(not_null<QWidget*> parent);
	~ThanosEffect();

	void addItem(QImage snapshot, QRect rect);

	[[nodiscard]] bool animating() const;

	[[nodiscard]] rpl::producer<> allDone() const;

	void setGeometry(QRect rect);
	void raise();

	[[nodiscard]] static bool Supported();
	// Runs the (potentially slow, 10–300ms) QRhi capability probe
	// synchronously on the main thread and caches the result. Safe to
	// call multiple times. If not called, `Supported()` will lazily
	// probe on first use — prefer warming it up during idle time so the
	// first message deletion doesn't hitch.
	static void WarmUp();

private:
	void ensureSurface();
	void showSurface();
	void hideSurface();
	[[nodiscard]] QWidget *surfaceWidget() const;

	const not_null<QWidget*> _parent;

	std::unique_ptr<RpWidgetWrap> _surface;
	[[maybe_unused]] ThanosEffectRenderer *_renderer = nullptr;

	Ui::Animations::Basic _animation;

	rpl::event_stream<> _allDone;
	rpl::lifetime _lifetime;

};

} // namespace Ui
