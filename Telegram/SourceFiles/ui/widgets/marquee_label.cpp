/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/marquee_label.h"

#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_options.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_marquee_label.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr auto kMarqueeDelay = crl::time(500);
constexpr auto kFrameDelta = crl::time(16);

} // namespace

MarqueeLabel::MarqueeLabel(
	QWidget *parent,
	rpl::producer<QString> text,
	const style::FlatLabel &st)
: RpWidget(parent)
, _st(st)
, _marquee([=](crl::time now) { return marqueeStep(now); }) {
	std::move(
		text
	) | rpl::on_next([=](const QString &value) {
		setText(value);
	}, lifetime());

	anim::Disables(
	) | rpl::on_next([=](bool) {
		refreshMarqueeState();
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		invalidateCache();
		update();
	}, lifetime());
}

void MarqueeLabel::setTextColorOverride(std::optional<QColor> color) {
	if (_textColorOverride == color) {
		return;
	}
	_textColorOverride = color;
	invalidateCache();
	update();
}

void MarqueeLabel::setContextCopyText(const QString &copyText) {
	_contextCopyText = copyText;
}

QAccessible::Role MarqueeLabel::accessibilityRole() {
	return QAccessible::Role::StaticText;
}

QString MarqueeLabel::accessibilityName() {
	return _text.toString();
}

void MarqueeLabel::setText(const QString &text) {
	_text.setText(_st.style, text, NameTextOptions());
	accessibilityNameChanged();
	invalidateCache();
	_offset = 0.;
	_delay = kMarqueeDelay;
	_lastUpdate = crl::now();
	setNaturalWidth(_text.maxWidth());
	refreshMarqueeState();
	update();
}

int MarqueeLabel::resizeGetHeight(int newWidth) {
	_availableTextWidth = newWidth;
	refreshMarqueeState();
	const auto full = _text.isEmpty()
		? _st.style.font->height
		: _text.countHeight(_text.maxWidth());
	return _st.maxHeight ? std::min(full, _st.maxHeight) : full;
}

void MarqueeLabel::showEvent(QShowEvent *e) {
	refreshMarqueeState();
}

void MarqueeLabel::hideEvent(QHideEvent *e) {
	refreshMarqueeState();
}

void MarqueeLabel::refreshMarqueeState() {
	const auto was = _overflown;
	_overflown = (_availableTextWidth > 0)
		&& (_text.maxWidth() > _availableTextWidth);
	const auto scroll = _overflown && isVisible() && !anim::Disabled();
	if (scroll && !_marquee.animating()) {
		_offset = 0.;
		_delay = kMarqueeDelay;
		_lastUpdate = crl::now();
		_lastFrame = 0;
		_marquee.start();
	} else if (!scroll && _marquee.animating()) {
		_marquee.stop();
		_offset = 0.;
		update();
	}
	if (was != _overflown) {
		update();
	}
}

bool MarqueeLabel::marqueeStep(crl::time now) {
	const auto dt = std::min(now - _lastUpdate, kFrameDelta);
	_lastUpdate = now;
	if (_delay > 0) {
		_delay -= dt;
		return true;
	}
	const auto total = float64(_text.maxWidth() + st::marqueeLabelGap);
	const auto slowdown = float64(st::marqueeLabelSlowdown);
	const auto fast = float64(st::marqueeLabelSpeed);
	const auto slow = float64(st::marqueeLabelSpeedSlow);
	auto speed = fast;
	if (_offset < slowdown) {
		speed = slow + (fast - slow) * (_offset / slowdown);
	} else if (_offset >= total - slowdown) {
		const auto dist = _offset - (total - slowdown);
		speed = fast - (fast - slow) * (dist / slowdown);
	}
	_offset += (dt / 1000.) * speed;
	if (_offset > total) {
		_offset = 0.;
		_delay = kMarqueeDelay;
	}
	if (now - _lastFrame >= kFrameDelta) {
		_lastFrame = now;
		update();
	}
	return true;
}

void MarqueeLabel::paintEvent(QPaintEvent *e) {
	if (_text.isEmpty()) {
		return;
	}
	Painter p(this);
	if (_overflown) {
		paintMarquee(p);
		return;
	}
	if (_textColorOverride) {
		p.setPen(*_textColorOverride);
	} else {
		p.setPen(_st.textFg);
	}
	p.setTextPalette(_st.palette);
	_text.draw(p, {
		.position = { 0, 0 },
		.availableWidth = width(),
		.align = _st.align,
		.clip = e->rect(),
		.palette = &_st.palette,
		.now = crl::now(),
	});
}

void MarqueeLabel::invalidateCache() {
	_tape = QImage();
	_frameOffset = std::nullopt;
}

void MarqueeLabel::validateTape() {
	const auto ratio = style::DevicePixelRatio();
	const auto natural = _text.maxWidth();
	const auto size = QSize(natural + st::marqueeLabelGap, height());
	if (_tape.size() == size * ratio) {
		return;
	}
	_frameOffset = std::nullopt;
	_tape = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	_tape.setDevicePixelRatio(ratio);
	_tape.fill(Qt::transparent);
	auto q = Painter(&_tape);
	if (_textColorOverride) {
		q.setPen(*_textColorOverride);
	} else {
		q.setPen(_st.textFg);
	}
	q.setTextPalette(_st.palette);
	_text.draw(q, {
		.position = { 0, 0 },
		.availableWidth = natural,
		.palette = &_st.palette,
		.now = crl::now(),
	});
}

void MarqueeLabel::validateFades() {
	const auto ratio = style::DevicePixelRatio();
	const auto fade = st::marqueeLabelFade;
	const auto size = QSize(fade, height());
	if (_fadeRight.size() == size * ratio) {
		return;
	}
	_frameOffset = std::nullopt;
	const auto prepare = [&](int fromAlpha, int tillAlpha) {
		auto result = QImage(
			size * ratio,
			QImage::Format_ARGB32_Premultiplied);
		result.setDevicePixelRatio(ratio);
		result.fill(Qt::transparent);
		auto q = QPainter(&result);
		auto gradient = QLinearGradient(0, 0, fade, 0);
		gradient.setColorAt(0., QColor(255, 255, 255, fromAlpha));
		gradient.setColorAt(1., QColor(255, 255, 255, tillAlpha));
		q.fillRect(QRect(QPoint(), size), gradient);
		return result;
	};
	_fadeLeft = prepare(255, 0);
	_fadeRight = prepare(0, 255);
}

void MarqueeLabel::validateFrame(float64 offset) {
	const auto ratio = style::DevicePixelRatio();
	const auto full = size() * ratio;
	if (_frame.size() != full) {
		_frame = QImage(full, QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
		_frameOffset = std::nullopt;
	}
	validateTape();
	validateFades();
	if (_frameOffset == offset) {
		return;
	}
	_frameOffset = offset;

	const auto fade = st::marqueeLabelFade;
	const auto total = float64(_text.maxWidth() + st::marqueeLabelGap);
	_frame.fill(Qt::transparent);
	auto q = QPainter(&_frame);
	q.drawImage(QPointF(-offset, 0), _tape);
	if (offset + width() > total) {
		q.drawImage(QPointF(total - offset, 0), _tape);
	}
	q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	const auto ramp = float64(st::marqueeLabelFadeRamp);
	const auto leftOpacity = (offset < ramp)
		? (offset / ramp)
		: (offset > total - ramp)
		? (1. - (offset - (total - ramp)) / ramp)
		: 1.;
	if (leftOpacity > 0.) {
		q.setOpacity(leftOpacity);
		q.drawImage(QPointF(0, 0), _fadeLeft);
		q.setOpacity(1.);
	}
	q.drawImage(QPointF(width() - fade, 0), _fadeRight);
}

void MarqueeLabel::paintMarquee(Painter &p) {
	const auto ratio = style::DevicePixelRatio();
	validateFrame(std::round(_offset * ratio) / ratio);
	p.drawImage(0, 0, _frame);
}

void MarqueeLabel::contextMenuEvent(QContextMenuEvent *e) {
	if (_contextCopyText.isEmpty() || _text.isEmpty()) {
		return;
	}
	_menu = base::make_unique_q<PopupMenu>(this);
	_menu->addAction(_contextCopyText, [text = _text.toString()] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(text));
	});
	_menu->popup(e->globalPos());
	e->accept();
}

} // namespace Ui
