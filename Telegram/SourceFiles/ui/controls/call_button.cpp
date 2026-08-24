/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/call_button.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/qt_object_factory.h"
#include "ui/ui_utility.h"
#include "styles/style_calls.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

namespace Ui {
namespace {

constexpr auto kOuterBounceDuration = crl::time(100);

} // namespace

CallButton::CallButton(
	QWidget *parent,
	const style::CallButton &stFrom,
	const style::CallButton *stTo)
: RippleButton(parent, stFrom.button.ripple)
, _stFrom(&stFrom)
, _stTo(stTo) {
	init();
}

void CallButton::init() {
	resize(_stFrom->button.width, _stFrom->button.height);

	const auto size = QSize(_stFrom->bgSize, _stFrom->bgSize);
	_bgMask = RippleAnimation::MaskByDrawer(size, false, [&](QPainter &p) {
		p.drawEllipse(0, 0, size.width(), size.height());
		if (_corner) {
			auto position = _corner->pos() - _stFrom->bgPosition;
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.setBrush(st::transparent);
			const auto border = _stFrom->cornerButtonBorder;
			p.drawEllipse(QRect(position, _corner->size()).marginsAdded(
				{ border, border, border, border }));
		}
	});
	_bgFrom = Ui::PixmapFromImage(style::colorizeImage(_bgMask, _stFrom->bg));
	if (_stTo) {
		Assert(_stFrom->button.width == _stTo->button.width);
		Assert(_stFrom->button.height == _stTo->button.height);
		Assert(_stFrom->bgPosition == _stTo->bgPosition);
		Assert(_stFrom->bgSize == _stTo->bgSize);

		_bg = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_bg.setDevicePixelRatio(style::DevicePixelRatio());
		_bgTo = Ui::PixmapFromImage(style::colorizeImage(_bgMask, _stTo->bg));
		_iconMixedMask = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixedMask.setDevicePixelRatio(style::DevicePixelRatio());
		_iconFrom = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconFrom.setDevicePixelRatio(style::DevicePixelRatio());
		_iconFrom.fill(Qt::black);
		{
			QPainter p(&_iconFrom);
			p.drawImage(
				(_stFrom->bgSize
					- _stFrom->button.icon.width()) / 2,
				(_stFrom->bgSize
					- _stFrom->button.icon.height()) / 2,
				_stFrom->button.icon.instance(Qt::white));
		}
		_iconTo = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconTo.setDevicePixelRatio(style::DevicePixelRatio());
		_iconTo.fill(Qt::black);
		{
			QPainter p(&_iconTo);
			p.drawImage(
				(_stTo->bgSize
					- _stTo->button.icon.width()) / 2,
				(_stTo->bgSize
					- _stTo->button.icon.height()) / 2,
				_stTo->button.icon.instance(Qt::white));
		}
		_iconMixed = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixed.setDevicePixelRatio(style::DevicePixelRatio());
	}
}

void CallButton::setOuterValue(float64 value) {
	if (_outerValue != value) {
		_outerAnimation.start([this] {
			if (_progress == 0. || _progress == 1.) {
				update();
			}
		}, _outerValue, value, kOuterBounceDuration);
		_outerValue = value;
	}
}

void CallButton::setText(rpl::producer<QString> text) {
	_text = std::move(text);
	refreshLabel();
}

void CallButton::setLabelShown(bool shown) {
	if (_labelShown == shown) {
		return;
	}
	_labelShown = shown;
	refreshLabelShown();
}

bool CallButton::textFits() const {
	return _textFits.current();
}

rpl::producer<bool> CallButton::textFitsValue() const {
	return _textFits.value();
}

rpl::producer<QString> CallButton::textValue() const {
	return _text.value();
}

void CallButton::refreshLabel() {
	_label.create(this, _text.value(), _stFrom->label);
	rpl::combine(
		sizeValue(),
		_label->naturalWidthValue()
	) | rpl::on_next([=](QSize my, int naturalWidth) {
		_label->resizeToWidth(std::min(my.width(), naturalWidth));
		_label->moveToLeft(
			(my.width() - _label->width()) / 2,
			my.height() - _label->height(),
			my.width());
		_textFits = (naturalWidth <= my.width());
		refreshLabelShown();
	}, _label->lifetime());
	refreshLabelShown();
}

void CallButton::refreshLabelShown() {
	if (_label) {
		_label->setVisible(_labelShown && _textFits.current());
	}
}

void CallButton::setProgress(float64 progress) {
	_progress = progress;
	if (_corner) {
		_corner->setProgress(progress);
	}
	update();
}

void CallButton::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	auto bgPosition = myrtlpoint(_stFrom->bgPosition);
	auto paintFrom = (_progress == 0.) || !_stTo;
	auto paintTo = !paintFrom && (_progress == 1.);

	auto outerValue = _outerAnimation.value(_outerValue);
	if (outerValue > 0.) {
		auto outerRadius = paintFrom ? _stFrom->outerRadius : paintTo ? _stTo->outerRadius : (_stFrom->outerRadius * (1. - _progress) + _stTo->outerRadius * _progress);
		auto outerPixels = outerValue * outerRadius;
		auto outerRect = QRectF(myrtlrect(bgPosition.x(), bgPosition.y(), _stFrom->bgSize, _stFrom->bgSize));
		outerRect = outerRect.marginsAdded(QMarginsF(outerPixels, outerPixels, outerPixels, outerPixels));

		PainterHighQualityEnabler hq(p);
		if (paintFrom) {
			p.setBrush(_stFrom->outerBg);
		} else if (paintTo) {
			p.setBrush(_stTo->outerBg);
		} else {
			p.setBrush(anim::brush(_stFrom->outerBg, _stTo->outerBg, _progress));
		}
		p.setPen(Qt::NoPen);
		p.drawEllipse(outerRect);
	}

	if (_bgOverride) {
		Assert(!_corner); // Didn't support this case yet.

		const auto &s = _stFrom->bgSize;
		p.setPen(Qt::NoPen);
		p.setBrush(*_bgOverride);

		PainterHighQualityEnabler hq(p);
		p.drawEllipse(QRect(_stFrom->bgPosition, QSize(s, s)));
	} else if (paintFrom) {
		p.drawPixmap(bgPosition, _bgFrom);
	} else if (paintTo) {
		p.drawPixmap(bgPosition, _bgTo);
	} else {
		style::colorizeImage(_bgMask, anim::color(_stFrom->bg, _stTo->bg, _progress), &_bg);
		p.drawImage(bgPosition, _bg);
	}

	auto rippleColorInterpolated = QColor();
	auto rippleColorOverride = &rippleColorInterpolated;
	if (_rippleOverride) {
		rippleColorOverride = &(*_rippleOverride);
	} else if (paintFrom) {
		rippleColorOverride = nullptr;
	} else if (paintTo) {
		rippleColorOverride = &_stTo->button.ripple.color->c;
	} else {
		rippleColorInterpolated = anim::color(_stFrom->button.ripple.color, _stTo->button.ripple.color, _progress);
	}
	paintRipple(p, _stFrom->button.rippleAreaPosition, rippleColorOverride);

	auto positionFrom = iconPosition(_stFrom);
	if (paintFrom) {
		const auto icon = &_stFrom->button.icon;
		icon->paint(p, positionFrom, width());
	} else {
		auto positionTo = iconPosition(_stTo);
		if (paintTo) {
			_stTo->button.icon.paint(p, positionTo, width());
		} else {
			mixIconMasks();
			style::colorizeImage(_iconMixedMask, st::callIconFg->c, &_iconMixed);
			p.drawImage(myrtlpoint(_stFrom->bgPosition), _iconMixed);
		}
	}
}

bool CallButton::inBackground(QPoint point) const {
	const auto radius = _stFrom->bgSize / 2.;
	const auto center = QPointF(myrtlpoint(_stFrom->bgPosition))
		+ QPointF(radius, radius);
	const auto delta = QPointF(point) - center;
	return (QPointF::dotProduct(delta, delta) <= radius * radius);
}

void CallButton::refreshOverState(QPoint point) {
	setOver(inBackground(point), StateChangeSource::ByHover);
}

void CallButton::enterEventHook(QEnterEvent *e) {
	refreshOverState(mapFromGlobal(QCursor::pos()));
	return RpWidget::enterEventHook(e);
}

void CallButton::mouseMoveEvent(QMouseEvent *e) {
	refreshOverState(e->pos());
}

void CallButton::mousePressEvent(QMouseEvent *e) {
	refreshOverState(e->pos());
	if (isOver()) {
		RippleButton::mousePressEvent(e);
	}
}

QPoint CallButton::iconPosition(not_null<const style::CallButton*> st) const {
	auto result = st->button.iconPosition;
	if (result.x() < 0) {
		result.setX((width() - st->button.icon.width()) / 2);
	}
	if (result.y() < 0) {
		result.setY((height() - st->button.icon.height()) / 2);
	}
	return result;
}

void CallButton::mixIconMasks() {
	_iconMixedMask.fill(Qt::black);

	Painter p(&_iconMixedMask);
	PainterHighQualityEnabler hq(p);
	auto paintIconMask = [this, &p](const QImage &mask, float64 angle) {
		auto skipFrom = _stFrom->bgSize / 2;
		p.translate(skipFrom, skipFrom);
		p.rotate(angle);
		p.translate(-skipFrom, -skipFrom);
		p.drawImage(0, 0, mask);
	};
	p.save();
	paintIconMask(_iconFrom, (_stFrom->angle - _stTo->angle) * _progress);
	p.restore();
	p.setOpacity(_progress);
	paintIconMask(_iconTo, (_stTo->angle - _stFrom->angle) * (1. - _progress));
}

void CallButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto over = isOver();
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (over != wasOver) {
		update();
	}
}

void CallButton::setColorOverrides(rpl::producer<CallButtonColors> &&colors) {
	std::move(
		colors
	) | rpl::on_next([=](const CallButtonColors &c) {
		_bgOverride = c.bg;
		_rippleOverride = c.ripple;
		update();
	}, lifetime());
}

void CallButton::setStyle(
		const style::CallButton &stFrom,
		const style::CallButton *stTo) {
	if (_stFrom == &stFrom && _stTo == stTo) {
		return;
	}
	_stFrom = &stFrom;
	_stTo = stTo;
	init();
	update();
}

not_null<CallButton*> CallButton::addCornerButton(
		const style::CallButton &stFrom,
		const style::CallButton *stTo) {
	Expects(!_corner);

	_corner = CreateChild<CallButton>(this, stFrom, stTo);
	_corner->move(_stFrom->cornerButtonPosition);
	_corner->setProgress(_progress);
	_corner->show();
	init();
	update();

	return _corner;
}

QPoint CallButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _stFrom->button.rippleAreaPosition;
}

QImage CallButton::prepareRippleMask() const {
	return _bgMask;
}

} // namespace Ui
