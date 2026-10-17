/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/layer_backdrop.h"

#include "ui/image/image_prepare.h"
#include "ui/layers/layer_widget.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

constexpr auto kCacheBackgroundFastTimeout = crl::time(200);
constexpr auto kCacheBackgroundFullTimeout = crl::time(1000);
constexpr auto kFadeBackgroundDuration = crl::time(200);

// Thread: Main.
[[nodiscard]] bool IsNightMode() {
	return (st::windowBg->c.lightnessF() < 0.5);
}

[[nodiscard]] QImage ProcessBackground(QImage image, int alpha, int scale) {
	const auto size = image.size();
	auto p = QPainter(&image);
	p.fillRect(
		QRect(QPoint(), image.size() / image.devicePixelRatio()),
		QColor(16, 16, 16, alpha));
	p.end();
	return Images::DitherImage(
		Images::BlurLargeImage(
			std::move(image).scaled(
				size / scale,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation),
			24).scaled(
				size,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation));
}

[[nodiscard]] QWidget *WidgetAbove(not_null<QWidget*> widget) {
	const auto &siblings = widget->parentWidget()->children();
	const auto from = siblings.indexOf(widget.get()) + 1;
	for (auto i = from; i < siblings.size(); ++i) {
		const auto sibling = qobject_cast<QWidget*>(siblings[i]);
		if (sibling && !sibling->isWindow()) {
			return sibling;
		}
	}
	return nullptr;
}

} // namespace

QImage CaptureUnderLayer(
		not_null<LayerWidget*> layer,
		LayerWidget *underlying) {
	const auto stack = layer->parentWidget();
	const auto target = stack->parentWidget();
	SendPendingMoveResizeEvents(target);

	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		layer->size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);

	const auto raiseUnderlying = underlying
		&& (underlying->parentWidget() == stack)
		&& !underlying->isHidden();
	const auto hiding = raiseUnderlying ? layer.get() : stack;
	const auto shown = !hiding->isHidden();
	const auto focused = shown && InFocusChain(hiding);
	const auto above = raiseUnderlying ? WidgetAbove(underlying) : nullptr;
	if (shown) {
		if (focused) {
			target->setFocus();
		}
		hiding->hide();
	}
	if (raiseUnderlying) {
		underlying->raise();
	}
	const auto restore = gsl::finally([&] {
		if (above) {
			underlying->stackUnder(above);
		}
		if (shown) {
			hiding->show();
			if (focused) {
				if (layer->isHidden()) {
					stack->setFocus();
				} else {
					layer->setInnerFocus();
				}
			}
		}
	});
	auto p = QPainter(&image);
	RenderWidget(p, target, QPoint(), layer->geometry());
	p.end();
	return image;
}

LayerBackdrop::LayerBackdrop(
	not_null<RpWidget*> owner,
	Fn<QImage()> capture,
	Fn<int(bool night)> overlayAlpha)
: _owner(owner)
, _capture(std::move(capture))
, _overlayAlpha(std::move(overlayAlpha))
, _timer([=] { checkCache(); }) {
	_owner->sizeValue(
	) | rpl::on_next([=] {
		checkStale();
	}, _lifetime);

	style::PaletteChanged(
	) | rpl::on_next([=] {
		checkStale();
	}, _lifetime);
}

void LayerBackdrop::start() {
	const auto night = IsNightMode();
	auto image = _capture();
	if (image.isNull()) {
		return;
	}
	_night = night;
	_readyGeneration = _generation;
	_image = ProcessBackground(
		std::move(image),
		_overlayAlpha(night),
		style::ConvertScale(4));
}

bool LayerBackdrop::started() const {
	return !_image.isNull();
}

void LayerBackdrop::invalidate() {
	++_generation;
	checkStale();
}

void LayerBackdrop::paint(QPainter &p) const {
	const auto target = _owner->rect();
	const auto faded = _fade.value(1.);
	if (faded < 1.) {
		p.drawImage(target, _back);
		if (faded > 0.) {
			const auto opacity = p.opacity();
			p.setOpacity(opacity * faded);
			p.drawImage(target, _image);
			p.setOpacity(opacity);
		}
	} else {
		p.drawImage(target, _image);
	}
}

rpl::producer<> LayerBackdrop::changes() const {
	return _changes.events();
}

void LayerBackdrop::checkStale() {
	if (!started()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto &ready = _next.isNull() ? _image : _next;
	if (ready.size() == _owner->size() * ratio
		&& _night == IsNightMode()
		&& _readyGeneration == _generation) {
		_timer.cancel();
	} else if (!_caching && !_timer.isActive()) {
		_lastAreaChangeTime = crl::now();
		_timer.callOnce(kCacheBackgroundFastTimeout);
	}
}

void LayerBackdrop::checkCache() {
	if (_caching || _timer.isActive()) {
		return;
	}
	const auto now = crl::now();
	if (now - _lastAreaChangeTime < kCacheBackgroundFullTimeout
		&& QGuiApplication::mouseButtons() != 0) {
		_timer.callOnce(kCacheBackgroundFastTimeout);
		return;
	}
	cache();
}

void LayerBackdrop::cache() {
	_caching = true;
	const auto weak = base::make_weak(this);
	const auto night = IsNightMode();
	const auto alpha = _overlayAlpha(night);
	const auto scale = style::ConvertScale(4);
	const auto generation = _generation;
	auto image = _capture();
	if (image.isNull()) {
		_caching = false;
		return;
	}
	crl::async([=, image = std::move(image)]() mutable {
		auto result = ProcessBackground(std::move(image), alpha, scale);
		crl::on_main([=, result = std::move(result)]() mutable {
			if (const auto strong = weak.get()) {
				strong->ready(std::move(result), night, generation);
			}
		});
	});
}

void LayerBackdrop::ready(QImage image, bool night, int generation) {
	_caching = false;

	const auto required = _owner->size() * style::DevicePixelRatio();
	if (image.size() == required
		&& night == IsNightMode()
		&& generation == _generation) {
		_next = std::move(image);
		_night = night;
		_readyGeneration = generation;
		if (!_fade.animating()) {
			startFade();
		}
		_owner->update();
		_changes.fire({});
	} else if (_image.size() != required) {
		_timer.callOnce(kCacheBackgroundFastTimeout);
	} else {
		checkStale();
	}
}

void LayerBackdrop::startFade() {
	if (_next.isNull()) {
		return;
	}
	_back = std::move(_image);
	_image = base::take(_next);
	_fade.start([=] {
		_owner->update();
		_changes.fire({});
		if (!_fade.animating()) {
			_back = QImage();
			startFade();
		}
	}, 0., 1., kFadeBackgroundDuration);
}

} // namespace Ui
