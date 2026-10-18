/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/layer_backdrop.h"

#include "ui/image/image_prepare.h"
#include "ui/layers/layer_widget.h"
#include "ui/widgets/shadow.h"
#include "ui/cached_special_layer_shadow_corners.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

#include "styles/style_layers.h"

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

// WHY: the stack's private BackgroundWidget also shadows the hidden view's
// box and offers no way to skip it, so its paint with no current layer is
// repeated here.
void PaintSpecialLayerBackground(QPainter &p, QRect outer, QRect box) {
	auto sides = RectPart::Left | RectPart::Right;
	const auto topCorners = (box.y() > 0);
	const auto bottomCorners = (box.y() + box.height() < outer.height());
	if (topCorners) {
		sides |= RectPart::Top;
	}
	if (bottomCorners) {
		sides |= RectPart::Bottom;
	}
	const auto clip = topCorners || bottomCorners;
	if (clip) {
		p.setClipRegion(QRegion(outer)
			- box.marginsRemoved({ st::boxRadius, 0, st::boxRadius, 0 })
			- box.marginsRemoved({ 0, st::boxRadius, 0, st::boxRadius }));
	}
	Shadow::paint(
		p,
		box,
		outer.width(),
		st::boxRoundShadow,
		SpecialLayerShadowCorners(),
		sides);
	if (clip) {
		p.setClipping(false);
	}
	p.fillRect(outer, st::layerBg);
}

[[nodiscard]] std::vector<QPointer<QWidget>> ShownChildren(
		not_null<QWidget*> parent) {
	auto result = std::vector<QPointer<QWidget>>();
	for (const auto child : parent->children()) {
		const auto widget = qobject_cast<QWidget*>(child);
		if (widget && !widget->isWindow() && !widget->isHidden()) {
			result.emplace_back(widget);
		}
	}
	return result;
}

void RenderPart(
		QPainter &p,
		not_null<QWidget*> widget,
		QPoint position,
		QRect region,
		QPoint origin) {
	const auto rect = region.intersected({ position, widget->size() });
	auto part = QRegion(rect.translated(-position));
	const auto mask = widget->mask();
	if (!mask.isEmpty()) {
		part &= mask;
	}
	if (part.isEmpty() || !widget->updatesEnabled()) {
		return;
	}
	// render() puts the top-left of the masked region at the offset.
	RenderWidget(
		p,
		widget,
		position + part.boundingRect().topLeft() - origin,
		part,
		QWidget::DrawChildren);
}

[[nodiscard]] bool ComposeSpecialScene(
		QPainter &p,
		not_null<QWidget*> target,
		not_null<QWidget*> stack,
		const QPointer<QWidget> &underlying,
		const std::vector<QPointer<QWidget>> &children,
		QRect capture) {
	const auto origin = capture.topLeft();
	const auto intact = [&] {
		return underlying
			&& (underlying->parentWidget() == stack.get())
			&& !underlying->isHidden()
			&& (ShownChildren(target) == children);
	};
	for (const auto &child : children) {
		if (!intact()) {
			return false;
		} else if (child == stack.get()) {
			p.save();
			p.translate(stack->pos() - origin);
			PaintSpecialLayerBackground(
				p,
				stack->rect(),
				underlying->geometry());
			p.restore();
			RenderPart(
				p,
				underlying.data(),
				stack->pos() + underlying->pos(),
				capture.intersected(stack->geometry()),
				origin);
		} else {
			RenderPart(p, child.data(), child->pos(), capture, origin);
		}
	}
	return intact();
}

[[nodiscard]] QImage CaptureScene(
		not_null<LayerWidget*> layer,
		LayerWidget *underlying,
		bool specialOnly) {
	const auto stack = layer->parentWidget();
	const auto target = stack->parentWidget();
	SendPendingMoveResizeEvents(target);

	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		layer->size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);

	const auto shown = !stack->isHidden();
	const auto special = shown
		&& underlying
		&& (underlying->parentWidget() == stack)
		&& !underlying->isHidden();
	if (specialOnly && !special) {
		return QImage();
	}
	const auto focused = shown && InFocusChain(stack);
	const auto focus = (special && focused)
		? QPointer<QWidget>(stack->window()->focusWidget())
		: QPointer<QWidget>();
	const auto children = special
		? ShownChildren(target)
		: std::vector<QPointer<QWidget>>();
	const auto weakUnderlying = QPointer<QWidget>(
		special ? underlying : nullptr);
	if (shown) {
		if (focused) {
			target->setFocus();
		}
		stack->hide();
	}
	const auto capture = layer->geometry();
	const auto flags = special
		? QWidget::RenderFlags(QWidget::IgnoreMask)
		: (QWidget::DrawChildren | QWidget::IgnoreMask);
	auto p = QPainter(&image);
	RenderWidget(p, target, QPoint(), capture, flags);
	if (shown) {
		stack->show();
		if (focus) {
			focus->setFocus();
		} else if (focused) {
			if (layer->isHidden()) {
				stack->setFocus();
			} else {
				layer->setInnerFocus();
			}
		}
	}
	if (special
		&& !ComposeSpecialScene(
			p,
			target,
			stack,
			weakUnderlying,
			children,
			capture)) {
		return QImage();
	}
	p.end();
	return image;
}

} // namespace

QImage CaptureUnderLayer(
		not_null<LayerWidget*> layer,
		LayerWidget *underlying) {
	const auto weak = QPointer<LayerWidget>(underlying);
	auto result = CaptureScene(layer, underlying, false);
	if (result.isNull() && weak) {
		// A child hiding itself while being painted has settled by now.
		result = CaptureScene(layer, weak.data(), true);
	}
	return result;
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
