/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/special_buttons.h"

#include "styles/style_boxes.h"
#include "styles/style_history.h"
#include "dialogs/dialogs_layout.h"
#include "ui/effects/ripple_animation.h"

namespace Ui {
namespace {

constexpr int kWideScale = 5;

} // namespace

HistoryDownButton::HistoryDownButton(QWidget *parent, const style::TwoIconButton &st) : RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);

	hide();
}

QImage HistoryDownButton::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint HistoryDownButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

void HistoryDownButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto over = isOver();
	auto down = isDown();
	((over || down) ? _st.iconBelowOver : _st.iconBelow).paint(p, _st.iconPosition, width());
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms);
	((over || down) ? _st.iconAboveOver : _st.iconAbove).paint(p, _st.iconPosition, width());
	if (_unreadCount > 0) {
		auto unreadString = QString::number(_unreadCount);
		if (unreadString.size() > 4) {
			unreadString = qsl("..") + unreadString.mid(unreadString.size() - 4);
		}

		Dialogs::Layout::UnreadBadgeStyle st;
		st.align = style::al_center;
		st.font = st::historyToDownBadgeFont;
		st.size = st::historyToDownBadgeSize;
		st.sizeId = Dialogs::Layout::UnreadBadgeInHistoryToDown;
		Dialogs::Layout::paintUnreadCount(p, unreadString, width(), 0, st, nullptr);
	}
}

void HistoryDownButton::setUnreadCount(int unreadCount) {
	if (_unreadCount != unreadCount) {
		_unreadCount = unreadCount;
		update();
	}
}

EmojiButton::EmojiButton(QWidget *parent, const style::IconButton &st) : RippleButton(parent, st.ripple)
, _st(st)
, _a_loading(animation(this, &EmojiButton::step_loading)) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
}

void EmojiButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();

	p.fillRect(e->rect(), st::historyComposeAreaBg);
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms, _rippleOverride ? &(*_rippleOverride)->c : nullptr);

	auto loading = a_loading.current(ms, _loading ? 1 : 0);
	p.setOpacity(1 - loading);

	auto over = isOver();
	auto icon = _iconOverride ? _iconOverride : &(over ? _st.iconOver : _st.icon);
	icon->paint(p, _st.iconPosition, width());

	p.setOpacity(1.);
	auto pen = _colorOverride ? (*_colorOverride)->p : (over ? st::historyEmojiCircleFgOver : st::historyEmojiCircleFg)->p;
	pen.setWidth(st::historyEmojiCircleLine);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);

	PainterHighQualityEnabler hq(p);
	QRect inner(QPoint((width() - st::historyEmojiCircle.width()) / 2, st::historyEmojiCircleTop), st::historyEmojiCircle);
	if (loading > 0) {
		int32 full = FullArcLength;
		int32 start = qRound(full * float64(ms % st::historyEmojiCirclePeriod) / st::historyEmojiCirclePeriod), part = qRound(loading * full / st::historyEmojiCirclePart);
		p.drawArc(inner, start, full - part);
	} else {
		p.drawEllipse(inner);
	}
}

void EmojiButton::setLoading(bool loading) {
	if (_loading != loading) {
		_loading = loading;
		auto from = loading ? 0. : 1., to = loading ? 1. : 0.;
		a_loading.start([this] { update(); }, from, to, st::historyEmojiCircleDuration);
		if (loading) {
			_a_loading.start();
		} else {
			_a_loading.stop();
		}
	}
}

void EmojiButton::setColorOverrides(const style::icon *iconOverride, const style::color *colorOverride, const style::color *rippleOverride) {
	_iconOverride = iconOverride;
	_colorOverride = colorOverride;
	_rippleOverride = rippleOverride;
	update();
}

void EmojiButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (isOver() != wasOver) {
		update();
	}
}

QPoint EmojiButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

QImage EmojiButton::prepareRippleMask() const {
	return RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

SendButton::SendButton(QWidget *parent) : RippleButton(parent, st::historyReplyCancel.ripple) {
	resize(st::historySendSize);
}

void SendButton::setType(Type type) {
	if (_type != type) {
		_contentFrom = grabContent();
		_type = type;
		_a_typeChanged.finish();
		_contentTo = grabContent();
		_a_typeChanged.start([this] { update(); }, 0., 1., st::historyRecordVoiceDuration);
		update();
	}
	if (_type != Type::Record) {
		_recordActive = false;
		_a_recordActive.finish();
	}
}

void SendButton::setRecordActive(bool recordActive) {
	if (_recordActive != recordActive) {
		_recordActive = recordActive;
		_a_recordActive.start([this] { recordAnimationCallback(); }, _recordActive ? 0. : 1., _recordActive ? 1. : 0, st::historyRecordVoiceDuration);
		update();
	}
}

void SendButton::finishAnimation() {
	_a_typeChanged.finish();
	_a_recordActive.finish();
	update();
}

void SendButton::mouseMoveEvent(QMouseEvent *e) {
	AbstractButton::mouseMoveEvent(e);
	if (_recording) {
		if (_recordUpdateCallback) {
			_recordUpdateCallback(e->globalPos());
		}
	}
}

void SendButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto over = (isDown() || isOver());
	auto changed = _a_typeChanged.current(ms, 1.);
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
	} else if (_type == Type::Record) {
		auto recordActive = recordActiveRatio();
		auto rippleColor = anim::color(st::historyAttachEmoji.ripple.color, st::historyRecordVoiceRippleBgActive, recordActive);
		paintRipple(p, (width() - st::historyAttachEmoji.rippleAreaSize) / 2, st::historyAttachEmoji.rippleAreaPosition.y(), ms, &rippleColor);

		auto fastIcon = [recordActive, over, this] {
			if (recordActive == 1.) {
				return &st::historyRecordVoiceActive;
			} else if (over) {
				return &st::historyRecordVoiceOver;
			}
			return &st::historyRecordVoice;
		};
		fastIcon()->paintInCenter(p, rect());
		if (recordActive > 0. && recordActive < 1.) {
			p.setOpacity(recordActive);
			st::historyRecordVoiceActive.paintInCenter(p, rect());
			p.setOpacity(1.);
		}
	} else if (_type == Type::Save) {
		auto &saveIcon = over ? st::historyEditSaveIconOver : st::historyEditSaveIcon;
		saveIcon.paint(p, st::historySendIconPosition, width());
	} else if (_type == Type::Cancel) {
		paintRipple(p, (width() - st::historyAttachEmoji.rippleAreaSize) / 2, st::historyAttachEmoji.rippleAreaPosition.y(), ms);

		auto &cancelIcon = over ? st::historyReplyCancelIconOver : st::historyReplyCancelIcon;
		cancelIcon.paintInCenter(p, rect());
	} else {
		auto &sendIcon = over ? st::historySendIconOver : st::historySendIcon;
		sendIcon.paint(p, st::historySendIconPosition, width());
	}
}

void SendButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto down = (state() & StateFlag::Down);
	if ((was & StateFlag::Down) != down) {
		if (down) {
			if (_type == Type::Record) {
				_recording = true;
				if (_recordStartCallback) {
					_recordStartCallback();
				}
			}
		} else if (_recording) {
			_recording = false;
			if (_recordStopCallback) {
				_recordStopCallback(_recordActive);
			}
		}
	}
}

QPixmap SendButton::grabContent() {
	auto result = QImage(kWideScale * size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(Qt::transparent);
	{
		Painter p(&result);
		p.drawPixmap((kWideScale - 1) / 2 * width(), (kWideScale - 1) / 2 * height(), myGrab(this));
	}
	return App::pixmapFromImageInPlace(std::move(result));
}

QImage SendButton::prepareRippleMask() const {
	auto size = (_type == Type::Record) ? st::historyAttachEmoji.rippleAreaSize : st::historyReplyCancel.rippleAreaSize;
	return Ui::RippleAnimation::ellipseMask(QSize(size, size));
}

QPoint SendButton::prepareRippleStartPosition() const {
	auto real = mapFromGlobal(QCursor::pos());
	auto size = (_type == Type::Record) ? st::historyAttachEmoji.rippleAreaSize : st::historyReplyCancel.rippleAreaSize;
	auto y = (_type == Type::Record) ? st::historyAttachEmoji.rippleAreaPosition.y() : (height() - st::historyReplyCancel.rippleAreaSize) / 2;
	return real - QPoint((width() - size) / 2, y);
}

void SendButton::recordAnimationCallback() {
	update();
	if (_recordAnimationCallback) {
		_recordAnimationCallback();
	}
}

PeerAvatarButton::PeerAvatarButton(QWidget *parent, PeerData *peer, const style::PeerAvatarButton &st) : AbstractButton(parent)
, _peer(peer)
, _st(st) {
	resize(_st.size, _st.size);
}

void PeerAvatarButton::paintEvent(QPaintEvent *e) {
	if (_peer) {
		Painter p(this);
		_peer->paintUserpic(p, (_st.size - _st.photoSize) / 2, (_st.size - _st.photoSize) / 2, _st.photoSize);
	}
}

NewAvatarButton::NewAvatarButton(QWidget *parent, int size, QPoint position) : RippleButton(parent, st::defaultActiveButton.ripple)
, _position(position) {
	resize(size, size);
}

void NewAvatarButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_image.isNull()) {
		p.drawPixmap(0, 0, _image);
		return;
	}

	p.setPen(Qt::NoPen);
	p.setBrush(isOver() ? st::defaultActiveButton.textBgOver : st::defaultActiveButton.textBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(rect());
	}

	paintRipple(p, 0, 0, getms());

	st::newGroupPhotoIcon.paint(p, _position, width());
}

void NewAvatarButton::setImage(const QImage &image) {
	auto small = image.scaled(size() * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	Images::prepareCircle(small);
	_image = App::pixmapFromImageInPlace(std::move(small));
	_image.setDevicePixelRatio(cRetinaFactor());
	update();
}

QImage NewAvatarButton::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(size());
}

} // namespace Ui
