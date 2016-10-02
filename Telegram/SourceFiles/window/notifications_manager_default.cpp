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
#include "window/notifications_manager_default.h"

#include "platform/platform_notifications_manager.h"
#include "mainwindow.h"
#include "lang.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_dialogs.h"

namespace Window {
namespace Notifications {
namespace Default {
namespace {

// 3 desktop notifies at the same time.
constexpr int kNotifyWindowsCount = 3;

NeverFreedPointer<Manager> ManagerInstance;

} // namespace

void start() {
	ManagerInstance.makeIfNull();
}

Manager *manager() {
	return ManagerInstance.data();
}

void finish() {
	ManagerInstance.clear();
}

Manager::Manager() {
	subscribe(FileDownload::ImageLoaded(), [this] {
		for_const (auto widget, _widgets) {
			widget->updatePeerPhoto();
		}
	});
}

void Manager::startAllHiding() {
	for_const (auto widget, _widgets) {
		widget->startHiding();
	}
}

void Manager::stopAllHiding() {
	for_const (auto widget, _widgets) {
		widget->stopHiding();
	}
}

void Manager::showNextFromQueue() {
	if (_queuedNotifications.isEmpty()) {
		return;
	}

	int count = kNotifyWindowsCount;
	for_const (auto widget, _widgets) {
		if (widget->index() < 0) continue;
		--count;
	}
	if (count <= 0) {
		return;
	}

	auto r = psDesktopRect();
	auto x = r.x() + r.width() - st::notifyWidth - st::notifyDeltaX;
	auto y = r.y() + r.height() - st::notifyHeight - st::notifyDeltaY;
	do {
		auto queued = _queuedNotifications.front();
		_queuedNotifications.pop_front();

		auto widget = std_::make_unique<Widget>(queued.history, queued.peer, queued.author, queued.item, queued.forwardedCount, x, y);
		Platform::Notifications::defaultNotificationShown(widget.get());
		_widgets.push_back(widget.release());
		--count;
	} while (count > 0 && !_queuedNotifications.isEmpty());

	auto shown = kNotifyWindowsCount - count;
	for_const (auto widget, _widgets) {
		if (widget->index() < 0) continue;
		--shown;
		widget->moveTo(x, y - shown * (st::notifyHeight + st::notifyDeltaY));
	}
}

void Manager::removeFromShown(Widget *remove) {
	if (remove) {
		auto index = _widgets.indexOf(remove);
		if (index >= 0) {
			_widgets.removeAt(index);
		}
	}
	showNextFromQueue();
}

void Manager::doShowNotification(HistoryItem *item, int forwardedCount) {
	_queuedNotifications.push_back(QueuedNotification(item, forwardedCount));
	showNextFromQueue();
}

void Manager::doClearAll() {
	_queuedNotifications.clear();
	for_const (auto widget, _widgets) {
		widget->unlinkHistory();
	}
}

void Manager::doClearAllFast() {
	_queuedNotifications.clear();

	auto widgets = createAndSwap(_widgets);
	for_const (auto widget, widgets) {
		widget->deleteLater();
	}
}

void Manager::doClearFromHistory(History *history) {
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.cend();) {
		if (i->history == history) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}
	for_const (auto widget, _widgets) {
		widget->unlinkHistory(history);
	}
	showNextFromQueue();
}

void Manager::doClearFromItem(HistoryItem *item) {
	for_const (auto widget, _widgets) {
		widget->itemRemoved(item);
	}
}

void Manager::doUpdateAll() {
	for_const (auto widget, _widgets) {
		widget->updateNotifyDisplay();
	}
}

Manager::~Manager() {
	clearAllFast();
}

Widget::Widget(History *history, PeerData *peer, PeerData *author, HistoryItem *msg, int forwardedCount, int x, int y) : TWidget(nullptr)
, _history(history)
, _peer(peer)
, _author(author)
, _item(msg)
, _forwardedCount(forwardedCount)
#if defined Q_OS_WIN && !defined Q_OS_WINRT
, _started(GetTickCount())
#endif // Q_OS_WIN && !Q_OS_WINRT
, _close(this, st::notifyClose)
, _alphaDuration(st::notifyFastAnim)
, _posDuration(st::notifyFastAnim)
, a_opacity(0)
, a_func(anim::linear)
, a_y(y + st::notifyHeight + st::notifyDeltaY)
, _a_appearance(animation(this, &Widget::step_appearance)) {
	updateNotifyDisplay();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideByTimer()));

	_inputTimer.setSingleShot(true);
	connect(&_inputTimer, SIGNAL(timeout()), this, SLOT(checkLastInput()));

	_close.setClickedCallback([this] {
		unlinkHistoryAndNotify();
	});
	_close.setAcceptBoth(true);
	_close.move(st::notifyWidth - st::notifyClose.width - st::notifyClosePos.x(), st::notifyClosePos.y());
	_close.show();

	a_y.start(y);
	setGeometry(x, a_y.current(), st::notifyWidth, st::notifyHeight);

	a_opacity.start(1);
	setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);

	show();

	setWindowOpacity(a_opacity.current());

	_alphaDuration = _posDuration = st::notifyFastAnim;
	_a_appearance.start();

	checkLastInput();
}

void Widget::checkLastInput() {
#if defined Q_OS_WIN && !defined Q_OS_WINRT
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);
	BOOL res = GetLastInputInfo(&lii);
	if (!res || lii.dwTime >= _started) {
		_hideTimer.start(st::notifyWaitLongHide);
	} else {
		_inputTimer.start(300);
	}
#else // Q_OS_WIN && !Q_OS_WINRT
	// TODO
	if (true) {
		_hideTimer.start(st::notifyWaitLongHide);
	} else {
		_inputTimer.start(300);
	}
#endif // else for Q_OS_WIN && !Q_OS_WINRT
}

void Widget::moveTo(int x, int y, int index) {
	if (index >= 0) {
		_index = index;
	}
	move(x, a_y.current());
	a_y.start(y);
	a_opacity.restart();
	_posDuration = st::notifyFastAnim;
	_a_appearance.start();
}

void Widget::updateNotifyDisplay() {
	if (!_history || !_peer || (!_item && _forwardedCount < 2)) return;

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
			_history->peer->loadUserpic(true, true);
			_history->peer->paintUserpicLeft(p, st::notifyPhotoSize, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), width());
		} else {
			static QPixmap icon = App::pixmapFromImageInPlace(App::wnd()->iconLarge().scaled(st::notifyPhotoSize, st::notifyPhotoSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), icon);
		}

		int32 itemWidth = w - st::notifyPhotoPos.x() - st::notifyPhotoSize - st::notifyTextLeft - st::notifyClosePos.x() - st::notifyClose.width;

		QRect rectForName(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyTextTop, itemWidth, st::msgNameFont->height);
		if (!App::passcoded() && Global::NotifyView() <= dbinvShowName) {
			if (auto chatTypeIcon = Dialogs::Layout::ChatTypeIcon(_history->peer, false)) {
				chatTypeIcon->paint(p, rectForName.topLeft(), w);
				rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
			}
		}

		if (!App::passcoded() && Global::NotifyView() <= dbinvShowPreview) {
			const HistoryItem *textCachedFor = 0;
			Text itemTextCache(itemWidth);
			QRect r(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height, itemWidth, 2 * st::dialogsTextFont->height);
			if (_item) {
				bool active = false;
				_item->drawInDialog(p, r, active, textCachedFor, itemTextCache);
			} else if (_forwardedCount > 1) {
				p.setFont(st::dialogsTextFont);
				if (_author) {
					itemTextCache.setText(st::dialogsTextFont, _author->name);
					p.setPen(st::dialogsTextFgService);
					itemTextCache.drawElided(p, r.left(), r.top(), r.width(), st::dialogsTextFont->height);
					r.setTop(r.top() + st::dialogsTextFont->height);
				}
				p.setPen(st::dialogsTextFg);
				p.drawText(r.left(), r.top() + st::dialogsTextFont->ascent, lng_forward_messages(lt_count, _forwardedCount));
			}
		} else {
			static QString notifyText = st::dialogsTextFont->elided(lang(lng_notification_preview), itemWidth);
			p.setFont(st::dialogsTextFont);
			p.setPen(st::dialogsTextFgService);
			p.drawText(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height + st::dialogsTextFont->ascent, notifyText);
		}

		p.setPen(st::dialogsNameFg);
		if (!App::passcoded() && Global::NotifyView() <= dbinvShowName) {
			_history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
		} else {
			p.setFont(st::msgNameFont);
			static QString notifyTitle = st::msgNameFont->elided(qsl("Telegram Desktop"), rectForName.width());
			p.drawText(rectForName.left(), rectForName.top() + st::msgNameFont->ascent, notifyTitle);
		}
	}

	_cache = App::pixmapFromImageInPlace(std_::move(img));
	update();
}

void Widget::updatePeerPhoto() {
	if (!peerPhoto->isNull() && peerPhoto->loaded()) {
		auto img = _cache.toImage();
		{
			Painter p(&img);
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), peerPhoto->pix(st::notifyPhotoSize));
		}
		peerPhoto = ImagePtr();
		_cache = App::pixmapFromImageInPlace(std_::move(img));
		update();
	}
}

void Widget::itemRemoved(HistoryItem *deleted) {
	if (_item && _item == deleted) {
		_item = nullptr;
		unlinkHistoryAndNotify();
	}
}

void Widget::unlinkHistoryAndNotify() {
	unlinkHistory();
	if (auto manager = ManagerInstance.data()) {
		manager->showNextFromQueue();
	}
}

void Widget::unlinkHistory(History *history) {
	if (!history || history == _history) {
		animHide(st::notifyFastAnim, anim::linear);
		_history = nullptr;
		_item = nullptr;
	}
}

void Widget::enterEvent(QEvent *e) {
	if (!_history) return;
	if (auto manager = ManagerInstance.data()) {
		manager->stopAllHiding();
	}
}

void Widget::leaveEvent(QEvent *e) {
	if (!_history) return;
	if (auto manager = ManagerInstance.data()) {
		manager->startAllHiding();
	}
}

void Widget::startHiding() {
	_hideTimer.start(st::notifyWaitShortHide);
}

void Widget::mousePressEvent(QMouseEvent *e) {
	if (!_history) return;

	if (e->button() == Qt::RightButton) {
		unlinkHistoryAndNotify();
	} else {
		e->ignore();
		if (auto manager = ManagerInstance.data()) {
			auto peerId = _history->peer->id;
			auto msgId = _item ? _item->id : ShowAtUnreadMsgId;
			manager->notificationActivated(peerId, msgId);
		}
	}
}

void Widget::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.drawPixmap(0, 0, _cache);
}

void Widget::animHide(float64 duration, anim::transition func) {
	if (!_history) return;
	_alphaDuration = duration;
	a_func = func;
	a_opacity.start(0);
	a_y.restart();
	_hiding = true;
	_a_appearance.start();
}

void Widget::stopHiding() {
	if (!_history) return;
	_alphaDuration = st::notifyFastAnim;
	a_func = anim::linear;
	a_opacity.start(1);
	a_y.restart();
	_hiding = false;
	_hideTimer.stop();
	_a_appearance.start();
}

void Widget::hideByTimer() {
	if (!_history) return;
	animHide(st::notifySlowHide, st::notifySlowHideFunc);
}

void Widget::step_appearance(float64 ms, bool timer) {
	float64 dtAlpha = ms / _alphaDuration, dtPos = ms / _posDuration;
	if (dtAlpha >= 1) {
		a_opacity.finish();
		if (_hiding) {
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
	if (auto manager = ManagerInstance.data()) {
		manager->removeFromShown(this);
	}
}

} // namespace Default
} // namespace Notifications
} // namespace Window
