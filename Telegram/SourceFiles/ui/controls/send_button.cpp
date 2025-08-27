/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/send_button.h"

#include "lang/lang_tag.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"

namespace Ui {
namespace {

constexpr int kWideScale = 5;

} // namespace

SendButton::SendButton(QWidget *parent, const style::SendButton &st)
: RippleButton(parent, st.inner.ripple)
, _st(st) {
	updateSize();
}

void SendButton::setState(State state) {
	if (_state == state) {
		return;
	}
	const auto hasSlowmode = (_state.slowmodeDelay > 0);
	const auto hasSlowmodeChanged = hasSlowmode != (state.slowmodeDelay > 0);
	auto withSameSlowmode = state;
	withSameSlowmode.slowmodeDelay = _state.slowmodeDelay;
	const auto animate = hasSlowmodeChanged
		|| (!hasSlowmode && withSameSlowmode != _state);
	if (animate) {
		_contentFrom = grabContent();
	}
	if (_state.slowmodeDelay != state.slowmodeDelay) {
		const auto seconds = state.slowmodeDelay;
		const auto minutes = seconds / 60;
		_slowmodeDelayText = seconds
			? u"%1:%2"_q.arg(minutes).arg(seconds % 60, 2, 10, QChar('0'))
			: QString();
	}
	if (!state.starsToSend || state.type != Type::Send) {
		_starsToSendText = Text::String();
	} else if (_starsToSendText.isEmpty()
		|| _state.starsToSend != state.starsToSend) {
		_starsToSendText.setMarkedText(
			_st.stars.style,
			Text::IconEmoji(&st::starIconEmoji).append(
				Lang::FormatCountToShort(state.starsToSend).string),
			kMarkupTextOptions);
	}
	_state = state;
	if (animate) {
		_stateChangeFromWidth = width();
		_stateChangeAnimation.stop();
		updateSize();
		_contentTo = grabContent();
		_stateChangeAnimation.start(
			[=] { updateSize(); update(); },
			0.,
			1.,
			st::universalDuration);
		setPointerCursor(_state.type != Type::Slowmode);
		updateSize();
	}
	update();
}

void SendButton::finishAnimating() {
	_stateChangeAnimation.stop();
	updateSize();
	update();
}

void SendButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	auto over = (isDown() || isOver());
	auto changed = _stateChangeAnimation.value(1.);
	if (changed < 1.) {
		PainterHighQualityEnabler hq(p);
		const auto ratio = style::DevicePixelRatio();

		p.setOpacity(1. - changed);
		const auto fromSize = _contentFrom.size() / (kWideScale * ratio);
		const auto fromShift = QPoint(
			(width() - fromSize.width()) / 2,
			(height() - fromSize.height()) / 2);
		auto fromRect = QRect(
			(1 - kWideScale) / 2 * fromSize.width(),
			(1 - kWideScale) / 2 * fromSize.height(),
			kWideScale * fromSize.width(),
			kWideScale * fromSize.height()
		).translated(fromShift);
		auto hiddenWidth = anim::interpolate(0, (1 - kWideScale) / 2 * fromSize.width(), changed);
		auto hiddenHeight = anim::interpolate(0, (1 - kWideScale) / 2 * fromSize.height(), changed);
		p.drawPixmap(
			fromRect.marginsAdded(
				{ hiddenWidth, hiddenHeight, hiddenWidth, hiddenHeight }),
			_contentFrom);

		p.setOpacity(changed);
		const auto toSize = _contentTo.size() / (kWideScale * ratio);
		const auto toShift = QPoint(
			(width() - toSize.width()) / 2,
			(height() - toSize.height()) / 2);
		auto toRect = QRect(
			(1 - kWideScale) / 2 * toSize.width(),
			(1 - kWideScale) / 2 * toSize.height(),
			kWideScale * toSize.width(),
			kWideScale * toSize.height()
		).translated(toShift);
		auto shownWidth = anim::interpolate((1 - kWideScale) / 2 * width(), 0, changed);
		auto shownHeight = anim::interpolate((1 - kWideScale) / 2 * toSize.height(), 0, changed);
		p.drawPixmap(
			toRect.marginsAdded(
				{ shownWidth, shownHeight, shownWidth, shownHeight }),
			_contentTo);
		return;
	}
	switch (_state.type) {
	case Type::Record: paintRecord(p, over); break;
	case Type::Round: paintRound(p, over); break;
	case Type::Save: paintSave(p, over); break;
	case Type::Cancel: paintCancel(p, over); break;
	case Type::Send:
		if (_starsToSendText.isEmpty()) {
			paintSend(p, over);
		} else {
			paintStarsToSend(p, over);
		}
		break;
	case Type::Schedule: paintSchedule(p, over); break;
	case Type::Slowmode: paintSlowmode(p); break;
	}
}

void SendButton::paintRecord(QPainter &p, bool over) {
	if (!isDisabled()) {
		paintRipple(
			p,
			(width() - _st.inner.rippleAreaSize) / 2,
			_st.inner.rippleAreaPosition.y());
	}

	const auto &icon = (isDisabled() || !over)
		? _st.record
		: _st.recordOver;
	icon.paintInCenter(p, rect());
}

void SendButton::paintRound(QPainter &p, bool over) {
	if (!isDisabled()) {
		paintRipple(
			p,
			(width() - _st.inner.rippleAreaSize) / 2,
			_st.inner.rippleAreaPosition.y());
	}

	const auto &icon = (isDisabled() || !over)
		? _st.round
		: _st.roundOver;
	icon.paintInCenter(p, rect());
}

void SendButton::paintSave(QPainter &p, bool over) {
	const auto &saveIcon = over
		? st::historyEditSaveIconOver
		: st::historyEditSaveIcon;
	saveIcon.paintInCenter(p, rect());
}

void SendButton::paintCancel(QPainter &p, bool over) {
	paintRipple(
		p,
		(width() - _st.inner.rippleAreaSize) / 2,
		_st.inner.rippleAreaPosition.y());

	const auto &cancelIcon = over
		? st::historyReplyCancelIconOver
		: st::historyReplyCancelIcon;
	cancelIcon.paintInCenter(p, rect());
}

void SendButton::paintSend(QPainter &p, bool over) {
	const auto &sendIcon = over ? _st.inner.iconOver : _st.inner.icon;
	if (isDisabled()) {
		const auto color = st::historyRecordVoiceFg->c;
		sendIcon.paint(p, st::historySendIconPosition, width(), color);
	} else {
		sendIcon.paint(p, st::historySendIconPosition, width());
	}
}

void SendButton::paintStarsToSend(QPainter &p, bool over) {
	const auto geometry = starsGeometry();
	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(over ? _st.stars.textBgOver : _st.stars.textBg);
		const auto radius = geometry.rounded.height() / 2;
		p.drawRoundedRect(geometry.rounded, radius, radius);
	}
	p.setPen(over ? _st.stars.textFgOver : _st.stars.textFg);
	_starsToSendText.draw(p, {
		.position = geometry.inner.topLeft(),
		.outerWidth = width(),
		.availableWidth = geometry.inner.width(),
	});
}

void SendButton::paintSchedule(QPainter &p, bool over) {
	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(over ? st::historySendIconFgOver : st::historySendIconFg);
		p.drawEllipse(
			st::historyScheduleIconPosition.x(),
			st::historyScheduleIconPosition.y(),
			st::historyScheduleIcon.width(),
			st::historyScheduleIcon.height());
	}
	st::historyScheduleIcon.paint(
		p,
		st::historyScheduleIconPosition,
		width());
}

void SendButton::paintSlowmode(QPainter &p) {
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		rect().marginsRemoved(st::historySlowmodeCounterMargins),
		_slowmodeDelayText,
		style::al_center);
}

SendButton::StarsGeometry SendButton::starsGeometry() const {
	const auto &st = _st.stars;
	const auto inner = QRect(
		0,
		0,
		_starsToSendText.maxWidth(),
		st.style.font->height);
	const auto rounded = inner.marginsAdded(QMargins(
		st.padding.left() - st.width / 2,
		st.padding.top() + st.textTop,
		st.padding.right() - st.width / 2,
		st.height - st.padding.top() - st.textTop - st.style.font->height));
	const auto add = (_st.inner.height - rounded.height()) / 2;
	const auto outer = rounded.marginsAdded(QMargins(
		add,
		add,
		add,
		_st.inner.height - add - rounded.height()));
	const auto shift = -outer.topLeft();
	return {
		.inner = inner.translated(shift),
		.rounded = rounded.translated(shift),
		.outer = outer.translated(shift),
	};
}

void SendButton::updateSize() {
	const auto finalWidth = _starsToSendText.isEmpty()
		? _st.inner.width
		: starsGeometry().outer.width();
	const auto progress = _stateChangeAnimation.value(1.);
	resize(
		anim::interpolate(_stateChangeFromWidth, finalWidth, progress),
		_st.inner.height);
}

QPixmap SendButton::grabContent() {
	auto result = QImage(
		kWideScale * size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		p.drawPixmap(
			(kWideScale - 1) / 2 * width(),
			(kWideScale - 1) / 2 * height(),
			GrabWidget(this));
	}
	return PixmapFromImage(std::move(result));
}

QImage SendButton::prepareRippleMask() const {
	const auto size = _st.inner.rippleAreaSize;
	return RippleAnimation::EllipseMask(QSize(size, size));
}

QPoint SendButton::prepareRippleStartPosition() const {
	const auto real = mapFromGlobal(QCursor::pos());
	const auto size = _st.inner.rippleAreaSize;
	const auto y = (height() - _st.inner.rippleAreaSize) / 2;
	return real - QPoint((width() - size) / 2, y);
}

} // namespace Ui
