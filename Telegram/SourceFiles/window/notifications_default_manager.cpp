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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "window/notifications_default_manager.h"

#include "mainwindow.h"
#include "lang.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_dialogs.h"

namespace Window {
namespace Notifications {

void DefaultManager::create(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, bool showUserpic, const QString &msg, bool showReplyButton) {
}

void DefaultManager::clear(History *history, bool fast) {
}


Widget::Widget(HistoryItem *msg, int32 x, int32 y, int32 fwdCount) : TWidget(0)
, history(msg->history())
, item(msg)
, fwdCount(fwdCount)
#if defined Q_OS_WIN && !defined Q_OS_WINRT
, started(GetTickCount())
#endif // Q_OS_WIN && !Q_OS_WINRT
, close(this, st::notifyClose)
, alphaDuration(st::notifyFastAnim)
, posDuration(st::notifyFastAnim)
, hiding(false)
, _index(0)
, a_opacity(0)
, a_func(anim::linear)
, a_y(y + st::notifyHeight + st::notifyDeltaY)
, _a_appearance(animation(this, &Widget::step_appearance)) {

	updateNotifyDisplay();

	hideTimer.setSingleShot(true);
	connect(&hideTimer, SIGNAL(timeout()), this, SLOT(hideByTimer()));

	inputTimer.setSingleShot(true);
	connect(&inputTimer, SIGNAL(timeout()), this, SLOT(checkLastInput()));

	close.setClickedCallback([this] {
		unlinkHistoryAndNotify();
	});
	close.setAcceptBoth(true);
	close.move(st::notifyWidth - st::notifyClose.width - st::notifyClosePos.x(), st::notifyClosePos.y());
	close.show();

	a_y.start(y);
	setGeometry(x, a_y.current(), st::notifyWidth, st::notifyHeight);

	a_opacity.start(1);
	setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);

	show();

	setWindowOpacity(a_opacity.current());

	alphaDuration = posDuration = st::notifyFastAnim;
	_a_appearance.start();

	checkLastInput();
}

void Widget::checkLastInput() {
#if defined Q_OS_WIN && !defined Q_OS_WINRT
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);
	BOOL res = GetLastInputInfo(&lii);
	if (!res || lii.dwTime >= started) {
		hideTimer.start(st::notifyWaitLongHide);
	} else {
		inputTimer.start(300);
	}
#else // Q_OS_WIN && !Q_OS_WINRT
	// TODO
	if (true) {
		hideTimer.start(st::notifyWaitLongHide);
	} else {
		inputTimer.start(300);
	}
#endif // else for Q_OS_WIN && !Q_OS_WINRT
}

void Widget::moveTo(int32 x, int32 y, int32 index) {
	if (index >= 0) {
		_index = index;
	}
	move(x, a_y.current());
	a_y.start(y);
	a_opacity.restart();
	posDuration = st::notifyFastAnim;
	_a_appearance.start();
}

void Widget::updateNotifyDisplay() {
	if (!item) return;

	int32 w = st::notifyWidth, h = st::notifyHeight;
	QImage img(w * cIntRetinaFactor(), h * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	if (cRetina()) img.setDevicePixelRatio(cRetinaFactor());
	img.fill(st::notifyBG->c);

	{
		Painter p(&img);
		p.fillRect(0, 0, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder->b);
		p.fillRect(w - st::notifyBorderWidth, 0, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder->b);
		p.fillRect(st::notifyBorderWidth, h - st::notifyBorderWidth, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder->b);
		p.fillRect(0, st::notifyBorderWidth, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder->b);

		if (!App::passcoded() && Global::NotifyView() <= dbinvShowName) {
			history->peer->loadUserpic(true, true);
			history->peer->paintUserpicLeft(p, st::notifyPhotoSize, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), width());
		} else {
			static QPixmap icon = App::pixmapFromImageInPlace(App::wnd()->iconLarge().scaled(st::notifyPhotoSize, st::notifyPhotoSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), icon);
		}

		int32 itemWidth = w - st::notifyPhotoPos.x() - st::notifyPhotoSize - st::notifyTextLeft - st::notifyClosePos.x() - st::notifyClose.width;

		QRect rectForName(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyTextTop, itemWidth, st::msgNameFont->height);
		if (!App::passcoded() && Global::NotifyView() <= dbinvShowName) {
			if (auto chatTypeIcon = Dialogs::Layout::ChatTypeIcon(history->peer, false)) {
				chatTypeIcon->paint(p, rectForName.topLeft(), w);
				rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
			}
		}

		QDateTime now(QDateTime::currentDateTime()), lastTime(item->date);
		QDate nowDate(now.date()), lastDate(lastTime.date());
		QString dt = lastTime.toString(cTimeFormat());
		int32 dtWidth = st::dialogsTextFont->width(dt);
		rectForName.setWidth(rectForName.width() - dtWidth - st::dialogsDateSkip);
		p.setFont(st::dialogsDateFont);
		p.setPen(st::dialogsDateFg);
		p.drawText(rectForName.left() + rectForName.width() + st::dialogsDateSkip, rectForName.top() + st::dialogsTextFont->ascent, dt);

		if (!App::passcoded() && Global::NotifyView() <= dbinvShowPreview) {
			const HistoryItem *textCachedFor = 0;
			Text itemTextCache(itemWidth);
			QRect r(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height, itemWidth, 2 * st::dialogsTextFont->height);
			if (fwdCount < 2) {
				bool active = false;
				item->drawInDialog(p, r, active, textCachedFor, itemTextCache);
			} else {
				p.setFont(st::dialogsTextFont);
				if (item->hasFromName() && !item->isPost()) {
					itemTextCache.setText(st::dialogsTextFont, item->author()->name);
					p.setPen(st::dialogsTextFgService);
					itemTextCache.drawElided(p, r.left(), r.top(), r.width(), st::dialogsTextFont->height);
					r.setTop(r.top() + st::dialogsTextFont->height);
				}
				p.setPen(st::dialogsTextFg);
				p.drawText(r.left(), r.top() + st::dialogsTextFont->ascent, lng_forward_messages(lt_count, fwdCount));
			}
		} else {
			static QString notifyText = st::dialogsTextFont->elided(lang(lng_notification_preview), itemWidth);
			p.setPen(st::dialogsTextFgService);
			p.drawText(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height + st::dialogsTextFont->ascent, notifyText);
		}

		p.setPen(st::dialogsNameFg);
		if (!App::passcoded() && Global::NotifyView() <= dbinvShowName) {
			history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
		} else {
			p.setFont(st::msgNameFont->f);
			static QString notifyTitle = st::msgNameFont->elided(qsl("Telegram Desktop"), rectForName.width());
			p.drawText(rectForName.left(), rectForName.top() + st::msgNameFont->ascent, notifyTitle);
		}
	}

	pm = App::pixmapFromImageInPlace(std_::move(img));
	update();
}

void Widget::updatePeerPhoto() {
	if (!peerPhoto->isNull() && peerPhoto->loaded()) {
		QImage img(pm.toImage());
		{
			QPainter p(&img);
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), peerPhoto->pix(st::notifyPhotoSize));
		}
		peerPhoto = ImagePtr();
		pm = App::pixmapFromImageInPlace(std_::move(img));
		update();
	}
}

void Widget::itemRemoved(HistoryItem *del) {
	if (item == del) {
		item = 0;
		unlinkHistoryAndNotify();
	}
}

void Widget::unlinkHistoryAndNotify() {
	unlinkHistory();
	if (auto window = App::wnd()) {
		window->notifyShowNext();
	}
}

void Widget::unlinkHistory(History *hist) {
	if (!hist || hist == history) {
		animHide(st::notifyFastAnim, anim::linear);
		history = 0;
		item = 0;
	}
}

void Widget::enterEvent(QEvent *e) {
	if (!history) return;
	if (App::wnd()) App::wnd()->notifyStopHiding();
}

void Widget::leaveEvent(QEvent *e) {
	if (!history) return;
	App::wnd()->notifyStartHiding();
}

void Widget::startHiding() {
	hideTimer.start(st::notifyWaitShortHide);
}

void Widget::mousePressEvent(QMouseEvent *e) {
	if (!history) return;

	PeerId peer = history->peer->id;
	MsgId msgId = (!history->peer->isUser() && item && item->mentionsMe() && item->id > 0) ? item->id : ShowAtUnreadMsgId;

	if (e->button() == Qt::RightButton) {
		unlinkHistoryAndNotify();
	} else {
		App::wnd()->showFromTray();
		if (App::passcoded()) {
			App::wnd()->setInnerFocus();
			App::wnd()->notifyClear();
		} else {
			Ui::showPeerHistory(peer, msgId);
		}
		e->ignore();
	}
}

void Widget::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.drawPixmap(0, 0, pm);
}

void Widget::animHide(float64 duration, anim::transition func) {
	if (!history) return;
	alphaDuration = duration;
	a_func = func;
	a_opacity.start(0);
	a_y.restart();
	hiding = true;
	_a_appearance.start();
}

void Widget::stopHiding() {
	if (!history) return;
	alphaDuration = st::notifyFastAnim;
	a_func = anim::linear;
	a_opacity.start(1);
	a_y.restart();
	hiding = false;
	hideTimer.stop();
	_a_appearance.start();
}

void Widget::hideByTimer() {
	if (!history) return;
	animHide(st::notifySlowHide, st::notifySlowHideFunc);
}

void Widget::step_appearance(float64 ms, bool timer) {
	float64 dtAlpha = ms / alphaDuration, dtPos = ms / posDuration;
	if (dtAlpha >= 1) {
		a_opacity.finish();
		if (hiding) {
			_a_appearance.stop();
			deleteLater();
		} else if (dtPos >= 1) {
			_a_appearance.stop();
		}
	} else {
		a_opacity.update(dtAlpha, a_func);
	}
	setWindowOpacity(a_opacity.current());
	if (dtPos >= 1) {
		a_y.finish();
	} else {
		a_y.update(dtPos, anim::linear);
	}
	move(x(), a_y.current());
	update();
}

Widget::~Widget() {
	if (App::wnd()) App::wnd()->notifyShowNext(this);
}

} // namespace Notifications
} // namespace Window
