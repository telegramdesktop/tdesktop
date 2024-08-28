/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_layer_widget.h"

#include "ui/painter.h"
#include "ui/ui_utility.h"

#include <QtGui/QGuiApplication>

namespace Editor {
namespace {

constexpr auto kCacheBackgroundFastTimeout = crl::time(200);
constexpr auto kCacheBackgroundFullTimeout = crl::time(1000);
constexpr auto kFadeBackgroundDuration = crl::time(200);

// Thread: Main.
[[nodiscard]] bool IsNightMode() {
	return (st::windowBg->c.lightnessF() < 0.5);
}

[[nodiscard]] QColor BlurOverlayColor(bool night) {
	return QColor(16, 16, 16, night ? 128 : 192);
}

[[nodiscard]] QImage ProcessBackground(QImage image, bool night) {
	const auto size = image.size();
	auto p = QPainter(&image);
	p.fillRect(
		QRect(QPoint(), image.size() / image.devicePixelRatio()),
		BlurOverlayColor(night));
	p.end();
	return Images::DitherImage(
		Images::BlurLargeImage(
			std::move(image).scaled(
				size / style::ConvertScale(4),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation),
			24).scaled(
				size,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation));
}

} // namespace

LayerWidget::LayerWidget(
	not_null<QWidget*> parent,
	base::unique_qptr<Ui::RpWidget> content)
: Ui::LayerWidget(parent)
, _content(std::move(content))
, _backgroundTimer([=] { checkCacheBackground(); }) {
	_content->setParent(this);
	_content->show();

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		auto p = QPainter(this);
		const auto faded = _backgroundFade.value(1.);
		if (faded < 1.) {
			p.drawImage(rect(), _backgroundBack);
			if (faded > 0.) {
				p.setOpacity(faded);
				p.drawImage(rect(), _background);
			}
		} else {
			p.drawImage(rect(), _background);
		}
	}, lifetime());
}

bool LayerWidget::eventHook(QEvent *e) {
	return RpWidget::eventHook(e);
}

void LayerWidget::start() {
	_backgroundNight = IsNightMode();
	_background = ProcessBackground(renderBackground(), _backgroundNight);

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		checkBackgroundStale();
		_content->resize(size);
	}, lifetime());

	style::PaletteChanged() | rpl::start_with_next([=] {
		checkBackgroundStale();
	}, lifetime());
}

void LayerWidget::checkBackgroundStale() {
	const auto ratio = style::DevicePixelRatio();
	const auto &ready = _backgroundNext.isNull()
		? _background
		: _backgroundNext;
	if (ready.size() == size() * ratio
		&& _backgroundNight == IsNightMode()) {
		_backgroundTimer.cancel();
	} else if (!_backgroundCaching && !_backgroundTimer.isActive()) {
		_lastAreaChangeTime = crl::now();
		_backgroundTimer.callOnce(kCacheBackgroundFastTimeout);
	}
}

QImage LayerWidget::renderBackground() {
	const auto parent = parentWidget();
	const auto target = parent->parentWidget();
	Ui::SendPendingMoveResizeEvents(target);

	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);

	const auto shown = !parent->isHidden();
	const auto focused = shown && Ui::InFocusChain(parent);
	if (shown) {
		if (focused) {
			target->setFocus();
		}
		parent->hide();
	}
	auto p = QPainter(&image);
	Ui::RenderWidget(p, target, QPoint(), geometry());
	p.end();

	if (shown) {
		parent->show();
		if (focused) {
			if (isHidden()) {
				parent->setFocus();
			} else {
				setInnerFocus();
			}
		}
	}

	return image;
}

void LayerWidget::checkCacheBackground() {
	if (_backgroundCaching || _backgroundTimer.isActive()) {
		return;
	}
	const auto now = crl::now();
	if (now - _lastAreaChangeTime < kCacheBackgroundFullTimeout
		&& QGuiApplication::mouseButtons() != 0) {
		_backgroundTimer.callOnce(kCacheBackgroundFastTimeout);
		return;
	}
	cacheBackground();
}

void LayerWidget::cacheBackground() {
	_backgroundCaching = true;
	const auto weak = Ui::MakeWeak(this);
	const auto night = IsNightMode();
	crl::async([weak, night, image = renderBackground()]() mutable {
		auto result = ProcessBackground(image, night);
		crl::on_main([weak, night, result = std::move(result)]() mutable {
			if (const auto strong = weak.data()) {
				strong->backgroundReady(std::move(result), night);
			}
		});
	});
}

void LayerWidget::backgroundReady(QImage background, bool night) {
	_backgroundCaching = false;

	const auto required = size() * style::DevicePixelRatio();
	if (background.size() == required && night == IsNightMode()) {
		_backgroundNext = std::move(background);
		_backgroundNight = night;
		if (!_backgroundFade.animating()) {
			startBackgroundFade();
		}
		update();
	} else if (_background.size() != required) {
		_backgroundTimer.callOnce(kCacheBackgroundFastTimeout);
	}
}

void LayerWidget::startBackgroundFade() {
	if (_backgroundNext.isNull()) {
		return;
	}
	_backgroundBack = std::move(_background);
	_background = base::take(_backgroundNext);
	_backgroundFade.start([=] {
		update();
		if (!_backgroundFade.animating()) {
			_backgroundBack = QImage();
			startBackgroundFade();
		}
	}, 0., 1., kFadeBackgroundDuration);
}

void LayerWidget::parentResized() {
	resizeToWidth(parentWidget()->width());
	if (_background.isNull()) {
		start();
	}
}

void LayerWidget::keyPressEvent(QKeyEvent *e) {
	QGuiApplication::sendEvent(_content.get(), e);
}

int LayerWidget::resizeGetHeight(int newWidth) {
	return parentWidget()->height();
}

bool LayerWidget::closeByOutsideClick() const {
	return false;
}

} // namespace Editor
