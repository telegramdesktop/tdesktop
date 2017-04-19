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
#include "calls/calls_panel.h"

#include "calls/calls_call.h"
#include "styles/style_calls.h"
#include "styles/style_history.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/shadow.h"
#include "messenger.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "platform/platform_specific.h"

namespace Calls {

class Panel::Button : public Ui::RippleButton {
public:
	Button(QWidget *parent, const style::CallButton &st);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	const style::CallButton &_st;
	QPixmap _bg;

};

Panel::Button::Button(QWidget *parent, const style::CallButton &st) : Ui::RippleButton(parent, st.button.ripple)
, _st(st) {
	resize(_st.button.width, _st.button.height);
	_bg = App::pixmapFromImageInPlace(style::colorizeImage(prepareRippleMask(), _st.bg));
}

void Panel::Button::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.drawPixmap(myrtlpoint(_st.button.rippleAreaPosition), _bg);

	auto ms = getms();

	paintRipple(p, _st.button.rippleAreaPosition.x(), _st.button.rippleAreaPosition.y(), ms);

	auto down = isDown();
	auto position = _st.button.iconPosition;
	_st.button.icon.paint(p, position, width());
}

void Panel::Button::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto over = isOver();
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (over != wasOver) {
		update();
	}
}

QPoint Panel::Button::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.button.rippleAreaPosition;
}

QImage Panel::Button::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(_st.button.rippleAreaSize, _st.button.rippleAreaSize));
}

Panel::Panel(gsl::not_null<Call*> call)
: _call(call)
, _user(call->user())
, _hangup(this, st::callHangup)
, _mute(this, st::callMuteToggle)
, _name(this)
, _status(this) {
	initControls();
	initLayout();
	show();
}

void Panel::initControls() {
	subscribe(_call->stateChanged(), [this](Call::State state) {
		if (state == Call::State::Failed || state == Call::State::Ended) {
			callDestroyed();
		}
	});
	_hangup->setClickedCallback([this] {
		if (_call) {
			_call->hangup();
		}
	});
	if (_call->type() == Call::Type::Incoming) {
		_answer.create(this, st::callAnswer);
		_answer->setClickedCallback([this] {
			if (_call) {
				_call->answer();
			}
		});
	}
}

void Panel::initLayout() {
	hide();

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | /*Qt::WindowStaysOnTopHint | */Qt::BypassWindowManagerHint | Qt::NoDropShadowWindowHint | Qt::Tool);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);

	initGeometry();

	processUserPhoto();
	subscribe(AuthSession::Current().api().fullPeerUpdated(), [this](PeerData *peer) {
		if (peer == _user) {
			processUserPhoto();
		}
	});
	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] {
		refreshUserPhoto();
	});
	createDefaultCacheImage();
}

void Panel::processUserPhoto() {
	if (!_user->userpicLoaded()) {
		_user->loadUserpic(true);
	}
	auto photo = (_user->photoId && _user->photoId != UnknownPeerPhotoId) ? App::photo(_user->photoId) : nullptr;
	if (isGoodUserPhoto(photo)) {
		photo->full->load(true);
	} else {
		if ((_user->photoId == UnknownPeerPhotoId) || (_user->photoId && (!photo || !photo->date))) {
			App::api()->requestFullPeer(_user);
		}
	}
	refreshUserPhoto();
}

void Panel::refreshUserPhoto() {
	auto photo = (_user->photoId && _user->photoId != UnknownPeerPhotoId) ? App::photo(_user->photoId) : nullptr;
	if (isGoodUserPhoto(photo) && photo->full->loaded() && (photo->id != _userPhotoId || !_userPhotoFull)) {
		_userPhotoId = photo->id;
		_userPhotoFull = true;
		createUserpicCache(photo->full);
	} else if (_userPhoto.isNull()) {
		if (auto userpic = _user->currentUserpic()) {
			createUserpicCache(userpic);
		}
	}
}

void Panel::createUserpicCache(ImagePtr image) {
	auto size = st::callWidth * cIntRetinaFactor();
	auto options = _useTransparency ? (Images::Option::RoundedLarge | Images::Option::RoundedTopLeft | Images::Option::RoundedTopRight | Images::Option::Smooth) : 0;
	auto width = image->width();
	auto height = image->height();
	if (width > height) {
		width = qMax((width * size) / height, 1);
		height = size;
	} else {
		height = qMax((height * size) / width, 1);
		width = size;
	}
	_userPhoto = image->pixNoCache(width, height, options, size, size);
	if (cRetina()) _userPhoto.setDevicePixelRatio(cRetinaFactor());

	refreshCacheImageUserPhoto();

	update();
}

bool Panel::isGoodUserPhoto(PhotoData *photo) {
	if (!photo || !photo->date) {
		return false;
	}
	auto badAspect = [](int a, int b) {
		return a > 10 * b;
	};
	auto width = photo->full->width();
	auto height = photo->full->height();
	return !badAspect(width, height) && !badAspect(height, width);
}

void Panel::initGeometry() {
	auto center = Messenger::Instance().getPointForCallPanelCenter();
	_useTransparency = Platform::TransparentWindowsSupported(center);
	_padding = _useTransparency ? st::callShadow.extend : style::margins();
	_contentTop = _padding.top() + st::callWidth;
	auto screen = QApplication::desktop()->screenGeometry(center);
	auto rect = QRect(0, 0, st::callWidth, st::callHeight);
	setGeometry(rect.translated(center - rect.center()).marginsAdded(_padding));
	createBottomImage();
}

void Panel::createBottomImage() {
	if (!_useTransparency) {
		return;
	}
	auto bottomWidth = width();
	auto bottomHeight = height() - _padding.top() - st::callWidth;
	auto image = QImage(QSize(bottomWidth, bottomHeight) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		Ui::Shadow::paint(p, QRect(_padding.left(), 0, st::callWidth, bottomHeight - _padding.bottom()), width(), st::callShadow, Ui::Shadow::Side::Left | Ui::Shadow::Side::Right | Ui::Shadow::Side::Bottom);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::callBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(myrtlrect(_padding.left(), -st::historyMessageRadius, st::callWidth, bottomHeight - _padding.bottom() + st::historyMessageRadius), st::historyMessageRadius, st::historyMessageRadius);
	}
	_bottomCache = App::pixmapFromImageInPlace(std::move(image));
}

void Panel::createDefaultCacheImage() {
	if (!_useTransparency || !_cache.isNull()) {
		return;
	}
	auto cache = QImage(size(), QImage::Format_ARGB32_Premultiplied);
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		auto inner = rect().marginsRemoved(_padding);
		Ui::Shadow::paint(p, inner, width(), st::callShadow);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::callBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(myrtlrect(inner), st::historyMessageRadius, st::historyMessageRadius);
	}
	_cache = App::pixmapFromImageInPlace(std::move(cache));
}

void Panel::refreshCacheImageUserPhoto() {
	auto cache = QImage(size(), QImage::Format_ARGB32_Premultiplied);
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		Ui::Shadow::paint(p, QRect(_padding.left(), _padding.top(), st::callWidth, st::callWidth), width(), st::callShadow, Ui::Shadow::Side::Top | Ui::Shadow::Side::Left | Ui::Shadow::Side::Right);
		p.drawPixmapLeft(_padding.left(), _padding.top(), width(), _userPhoto);
		p.drawPixmapLeft(0, _padding.top() + st::callWidth, width(), _bottomCache);
	}
	_cache = App::pixmapFromImageInPlace(std::move(cache));
}

void Panel::resizeEvent(QResizeEvent *e) {
	auto controlsTop = _contentTop + st::callControlsTop;
	if (_answer) {
		auto bothWidth = _answer->width() + st::callControlsSkip + _hangup->width();
		_hangup->moveToLeft((width() - bothWidth) / 2, controlsTop);
		_answer->moveToRight((width() - bothWidth) / 2, controlsTop);
	} else {
		_hangup->moveToLeft((width() - _hangup->width()) / 2, controlsTop);
	}
	_mute->moveToRight(_padding.right() + st::callMuteRight, controlsTop);
}

void Panel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (_useTransparency) {
		p.drawPixmapLeft(0, 0, width(), _cache);
	} else {
		p.drawPixmapLeft(0, 0, width(), _userPhoto);
		p.fillRect(myrtlrect(0, st::callWidth, width(), height() - st::callWidth), st::callBg);
	}
}

void Panel::mousePressEvent(QMouseEvent *e) {
	auto dragArea = myrtlrect(_padding.left(), _padding.top(), st::callWidth, st::callWidth);
	if (e->button() == Qt::LeftButton && dragArea.contains(e->pos())) {
		_dragging = true;
		_dragStartMousePosition = e->globalPos();
		_dragStartMyPosition = QPoint(x(), y());
	}
}

void Panel::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		if (!(e->buttons() & Qt::LeftButton)) {
			_dragging = false;
		} else {
			move(_dragStartMyPosition + (e->globalPos() - _dragStartMousePosition));
		}
	}
}

void Panel::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_dragging = false;
	}
}

void Panel::callDestroyed() {
}

} // namespace Calls
