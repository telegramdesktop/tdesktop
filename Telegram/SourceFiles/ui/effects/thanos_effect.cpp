/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/thanos_effect.h"

#include "ui/effects/thanos_effect_renderer.h"
#include "ui/gl/gl_detection.h"
#include "ui/gl/gl_surface.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "base/qt_signal_producer.h"

#include <QtGui/QWindow>

namespace Ui {

bool ThanosEffect::Supported() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (PowerSaving::On(PowerSaving::kChatEffects)
		|| PowerSaving::On(PowerSaving::kAnimations)) {
		return false;
	}
	if (!GL::WidgetsRhiEnabled()) {
		return false;
	}
	return GL::CheckRhiCapabilities().compute;
#else
	return false;
#endif
}

void ThanosEffect::WarmUp() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (PowerSaving::On(PowerSaving::kChatEffects)
		|| PowerSaving::On(PowerSaving::kAnimations)) {
		return;
	}
	if (!GL::WidgetsRhiEnabled()) {
		return;
	}
	(void)GL::CheckRhiCapabilities();
#endif
}

ThanosEffect::ThanosEffect(not_null<QWidget*> parent)
: _parent(parent)
, _animation([=] {
	if (const auto w = surfaceWidget()) {
		w->update();
	}
}) {
}

ThanosEffect::~ThanosEffect() = default;

QWidget *ThanosEffect::surfaceWidget() const {
	return _surface ? _surface->rpWidget() : nullptr;
}

void ThanosEffect::ensureSurface() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	if (_surface) {
		return;
	}

	auto devicePixelRatio = [&]() -> rpl::producer<float64> {
		const auto initial = float64(_parent->devicePixelRatioF());
		const auto handle = _parent->windowHandle();
		if (!handle) {
			return rpl::single(initial);
		}
		return rpl::single(
			initial
		) | rpl::then(base::qt_signal_producer(
			handle,
			&QWindow::screenChanged
		) | rpl::map([parent = _parent](QScreen*) {
			return float64(parent->devicePixelRatioF());
		}));
	}();

	auto renderer = std::make_unique<ThanosEffectRenderer>(
		std::move(devicePixelRatio));
	_renderer = renderer.get();

	_renderer->allDone() | rpl::on_next([weak = base::make_weak(this)] {
		if (const auto strong = weak.get()) {
			strong->_animation.stop();
		}
		crl::on_main(weak, [weak] {
			if (const auto strong = weak.get()) {
				strong->hideSurface();
				strong->_allDone.fire({});
			}
		});
	}, _lifetime);

	_surface = GL::CreateSurface(
		_parent,
		GL::ChosenRenderer{
			.renderer = std::move(renderer),
			.backend = GL::Backend::QRhi,
		});

	if (const auto w = surfaceWidget()) {
		w->setAttribute(Qt::WA_TransparentForMouseEvents);
		w->setAttribute(Qt::WA_AlwaysStackOnTop);
		w->setGeometry(_parent->rect());
		w->hide();
	}
#endif
}

void ThanosEffect::showSurface() {
	if (const auto w = surfaceWidget()) {
		w->setGeometry(_parent->rect());
		// Defer show until the current call stack returns to the event
		// loop, so that all items from a batch deletion are added
		// before the first render. Without this, w->show() triggers
		// an immediate platform compositing pass with only the first
		// item visible.
		Ui::PostponeCall(w, [w] {
			w->show();
			w->raise();
		});
		_animation.start();
	}
}

void ThanosEffect::hideSurface() {
	_animation.stop();
	if (const auto w = surfaceWidget()) {
		w->hide();
	}
}

void ThanosEffect::addItem(QImage snapshot, QRect rect) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	ensureSurface();
	if (!_renderer) {
		return;
	}

	const auto wasAnimating = _renderer->hasActiveItems();

	_renderer->addItem({
		.snapshot = std::move(snapshot),
		.rect = QRectF(rect),
	});

	if (!wasAnimating) {
		showSurface();
	}
#endif
}

bool ThanosEffect::animating() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	return _renderer && _renderer->hasActiveItems();
#else
	return false;
#endif
}

rpl::producer<> ThanosEffect::allDone() const {
	return _allDone.events();
}

void ThanosEffect::setGeometry(QRect rect) {
	if (const auto w = surfaceWidget()) {
		if (w->isVisible()) {
			w->setGeometry(rect);
		}
	}
}

void ThanosEffect::raise() {
	if (const auto w = surfaceWidget()) {
		w->raise();
	}
}

} // namespace Ui
