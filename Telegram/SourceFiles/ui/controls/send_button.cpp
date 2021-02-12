/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/send_button.h"

#include "ui/effects/ripple_animation.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

constexpr int kWideScale = 5;

} // namespace

SendButton::SendButton(QWidget *parent)
: RippleButton(parent, st::historyReplyCancel.ripple) {
	resize(st::historySendSize);
}

void SendButton::setType(Type type) {
	Expects(isSlowmode() || type != Type::Slowmode);

	if (isSlowmode() && type != Type::Slowmode) {
		_afterSlowmodeType = type;
		return;
	}
	if (_type != type) {
		_contentFrom = grabContent();
		_type = type;
		_a_typeChanged.stop();
		_contentTo = grabContent();
		_a_typeChanged.start(
			[=] { update(); },
			0.,
			1.,
			st::historyRecordVoiceDuration);
		setPointerCursor(_type != Type::Slowmode);
		update();
	}
}

void SendButton::setSlowmodeDelay(int seconds) {
	Expects(seconds >= 0 && seconds < kSlowmodeDelayLimit);

	if (_slowmodeDelay == seconds) {
		return;
	}
	_slowmodeDelay = seconds;
	_slowmodeDelayText = isSlowmode()
		? u"%1:%2"_q.arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'))
		: QString();
	setType(isSlowmode() ? Type::Slowmode : _afterSlowmodeType);
	update();
}

void SendButton::finishAnimating() {
	_a_typeChanged.stop();
	update();
}

void SendButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto over = (isDown() || isOver());
	auto changed = _a_typeChanged.value(1.);
	if (changed < 1.) {
		PainterHighQualityEnabler hq(p);
		p.setOpacity(1. - changed);
		auto targetRect = QRect((1 - kWideScale) / 2 * width(), (1 - kWideScale) / 2 * height(), kWideScale * width(), kWideScale * height());
		auto hiddenWidth = anim::interpolate(0, (1 - kWideScale) / 2 * width(), changed);
		auto hiddenHeight = anim::interpolate(0, (1 - kWideScale) / 2 * height(), changed);
		p.drawPixmap(targetRect.marginsAdded(QMargins(hiddenWidth, hiddenHeight, hiddenWidth, hiddenHeight)), _contentFrom);
		p.setOpacity(changed);
		auto shownWidth = anim::interpolate((1 - kWideScale) / 2 * width(), 0, changed);
		auto shownHeight = anim::interpolate((1 - kWideScale) / 2 * height(), 0, changed);
		p.drawPixmap(targetRect.marginsAdded(QMargins(shownWidth, shownHeight, shownWidth, shownHeight)), _contentTo);
		return;
	}
	switch (_type) {
	case Type::Record: paintRecord(p, over); break;
	case Type::Save: paintSave(p, over); break;
	case Type::Cancel: paintCancel(p, over); break;
	case Type::Send: paintSend(p, over); break;
	case Type::Schedule: paintSchedule(p, over); break;
	case Type::Slowmode: paintSlowmode(p); break;
	}
}

void SendButton::paintRecord(Painter &p, bool over) {
	const auto recordActive = 0.;
	if (!isDisabled()) {
		auto rippleColor = anim::color(
			st::historyAttachEmoji.ripple.color,
			st::historyRecordVoiceRippleBgActive,
			recordActive);
		paintRipple(
			p,
			(width() - st::historyAttachEmoji.rippleAreaSize) / 2,
			st::historyAttachEmoji.rippleAreaPosition.y(),
			&rippleColor);
	}

	auto fastIcon = [&] {
		if (isDisabled()) {
			return &st::historyRecordVoice;
		} else if (recordActive == 1.) {
			return &st::historyRecordVoiceActive;
		} else if (over) {
			return &st::historyRecordVoiceOver;
		}
		return &st::historyRecordVoice;
	};
	fastIcon()->paintInCenter(p, rect());
	if (!isDisabled() && recordActive > 0. && recordActive < 1.) {
		p.setOpacity(recordActive);
		st::historyRecordVoiceActive.paintInCenter(p, rect());
		p.setOpacity(1.);
	}
}

void SendButton::paintSave(Painter &p, bool over) {
	const auto &saveIcon = over
		? st::historyEditSaveIconOver
		: st::historyEditSaveIcon;
	saveIcon.paintInCenter(p, rect());
}

void SendButton::paintCancel(Painter &p, bool over) {
	paintRipple(p, (width() - st::historyAttachEmoji.rippleAreaSize) / 2, st::historyAttachEmoji.rippleAreaPosition.y());

	const auto &cancelIcon = over
		? st::historyReplyCancelIconOver
		: st::historyReplyCancelIcon;
	cancelIcon.paintInCenter(p, rect());
}

void SendButton::paintSend(Painter &p, bool over) {
	const auto &sendIcon = over
		? st::historySendIconOver
		: st::historySendIcon;
	if (isDisabled()) {
		const auto color = st::historyRecordVoiceFg->c;
		sendIcon.paint(p, st::historySendIconPosition, width(), color);
	} else {
		sendIcon.paint(p, st::historySendIconPosition, width());
	}
}

void SendButton::paintSchedule(Painter &p, bool over) {
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

void SendButton::paintSlowmode(Painter &p) {
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		rect().marginsRemoved(st::historySlowmodeCounterMargins),
		_slowmodeDelayText,
		style::al_center);
}

bool SendButton::isSlowmode() const {
	return (_slowmodeDelay > 0);
}

QPixmap SendButton::grabContent() {
	auto result = QImage(
		kWideScale * size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(style::DevicePixelRatio());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		p.drawPixmap(
			(kWideScale - 1) / 2 * width(),
			(kWideScale - 1) / 2 * height(),
			GrabWidget(this));
	}
	return Ui::PixmapFromImage(std::move(result));
}

QImage SendButton::prepareRippleMask() const {
	auto size = (_type == Type::Record)
		? st::historyAttachEmoji.rippleAreaSize
		: st::historyReplyCancel.rippleAreaSize;
	return RippleAnimation::ellipseMask(QSize(size, size));
}

QPoint SendButton::prepareRippleStartPosition() const {
	auto real = mapFromGlobal(QCursor::pos());
	auto size = (_type == Type::Record)
		? st::historyAttachEmoji.rippleAreaSize
		: st::historyReplyCancel.rippleAreaSize;
	auto y = (_type == Type::Record)
		? st::historyAttachEmoji.rippleAreaPosition.y()
		: (height() - st::historyReplyCancel.rippleAreaSize) / 2;
	return real - QPoint((width() - size) / 2, y);
}

} // namespace Ui
