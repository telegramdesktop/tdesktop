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
#include "ui/buttons/icon_button.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"

namespace Window {
namespace Notifications {
namespace Default {
namespace {

// 3 desktop notifies at the same time.
constexpr int kNotifyWindowsCount = 3;

NeverFreedPointer<Manager> ManagerInstance;

int notificationWidth() {
	static auto result = ([] {
		auto replyWidth = st::defaultBoxButton.font->width(lang(lng_notification_reply).toUpper()) - st::defaultBoxButton.width;
		auto textLeft = st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft;
		auto minWidth = textLeft + replyWidth + st::boxButtonPadding.right();
		return qMax(st::notifyMinWidth, minWidth);
	})();
	return result;
}

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
		if (widget->isUnlinked()) continue;
		--count;
	}
	if (count <= 0) {
		return;
	}

	auto r = psDesktopRect();
	auto x = r.x() + r.width() - notificationWidth() - st::notifyDeltaX;
	auto y = r.y() + r.height();
	do {
		auto queued = _queuedNotifications.front();
		_queuedNotifications.pop_front();

		auto widget = std_::make_unique<Widget>(queued.history, queued.peer, queued.author, queued.item, queued.forwardedCount, x, y);
		Platform::Notifications::defaultNotificationShown(widget.get());
		_widgets.push_back(widget.release());
		--count;
	} while (count > 0 && !_queuedNotifications.isEmpty());

	auto bottom = y - st::notifyDeltaY;
	for_const (auto widget, _widgets) {
		if (widget->isUnlinked() < 0) continue;
		widget->moveTop(y - widget->height(), y);
		y -= widget->height() + st::notifyDeltaY;
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
, _reply(this, lang(lng_notification_reply), st::defaultBoxButton)
, _alphaDuration(st::notifyFastAnim)
, a_opacity(0)
, a_func(anim::linear)
, a_top(y)
, a_bottom(y + st::notifyMinHeight)
, _a_movement(animation(this, &Widget::step_movement))
, _a_appearance(animation(this, &Widget::step_appearance)) {
	setGeometry(x, a_top.current(), notificationWidth(), a_bottom.current() - a_top.current());

	updateNotifyDisplay();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideByTimer()));

	_inputTimer.setSingleShot(true);
	connect(&_inputTimer, SIGNAL(timeout()), this, SLOT(checkLastInput()));

	_close->setClickedCallback([this] {
		unlinkHistoryAndNotify();
	});
	_close->setAcceptBoth(true);
	_close->moveToRight(st::notifyClosePos.x(), st::notifyClosePos.y());
	_close->show();

	_reply->setClickedCallback([this] {
		showReplyField();
	});
	_replyPadding = st::notifyMinHeight - st::notifyPhotoPos.y() - st::notifyPhotoSize;
	_reply->moveToRight(_replyPadding, height() - _reply->height() - _replyPadding);
	_reply->hide();

	prepareActionsCache();

	a_opacity.start(1);
	setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);

	show();

	setWindowOpacity(a_opacity.current());

	_alphaDuration = st::notifyFastAnim;
	_a_appearance.start();

	checkLastInput();
}

void Widget::prepareActionsCache() {
	auto replyCache = myGrab(_reply);
	auto fadeWidth = st::notifyFadeRight.width();
	auto actionsTop = st::notifyTextTop + st::msgNameFont->height;
	auto actionsCacheWidth = _reply->width() + _replyPadding + fadeWidth;
	auto actionsCacheHeight = height() - actionsTop;
	auto actionsCacheImg = QImage(actionsCacheWidth * cIntRetinaFactor(), actionsCacheHeight * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	actionsCacheImg.fill(st::transparent->c);
	{
		Painter p(&actionsCacheImg);
		st::notifyFadeRight.fill(p, rtlrect(0, 0, fadeWidth, actionsCacheHeight, actionsCacheWidth));
		p.fillRect(rtlrect(fadeWidth, 0, actionsCacheWidth - fadeWidth, actionsCacheHeight, actionsCacheWidth), st::notifyBg);
		p.drawPixmapRight(_replyPadding, _reply->y() - actionsTop, actionsCacheWidth, replyCache);
	}
	_buttonsCache = App::pixmapFromImageInPlace(std_::move(actionsCacheImg));
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

void Widget::onReplyResize() {
	_a_movement.stop();
	auto newHeight = st::notifyMinHeight + _replyArea->height() + st::notifyBorderWidth;
	auto delta = height() - newHeight;
	if (delta != 0) {
		auto top = a_top.current() + delta;
		a_top = anim::ivalue(top, top);
		setGeometry(x(), top, width(), a_bottom.current() - top);
		update();
	}
}

void Widget::onReplySubmit(bool ctrlShiftEnter) {
	sendReply();
}

void Widget::onReplyCancel() {
	unlinkHistoryAndNotify();
}

void Widget::moveTop(int top, int bottom) {
	a_top.start(top);
	a_bottom.start(bottom);
	_a_movement.start();
}

void Widget::updateNotifyDisplay() {
	if (!_history || !_peer || (!_item && _forwardedCount < 2)) return;

	int32 w = width(), h = height();
	QImage img(w * cIntRetinaFactor(), h * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	if (cRetina()) img.setDevicePixelRatio(cRetinaFactor());
	img.fill(st::notifyBg->c);

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

void Widget::toggleActionButtons(bool visible) {
	if (_actionsVisible != visible) {
		_actionsVisible = visible;
		_reply->hide();
		a_actionsOpacity.start([this] { actionsOpacityCallback(); }, _actionsVisible ? 0. : 1., _actionsVisible ? 1. : 0., st::notifyActionsDuration);
	}
}

void Widget::actionsOpacityCallback() {
	update();
	if (!a_actionsOpacity.animating() && _actionsVisible) {
		_reply->show();
	}
}

void Widget::showReplyField() {
	if (_replyArea) return;

	_replyArea = new InputArea(this, st::notifyReplyArea, lang(lng_message_ph), QString());
	_replyArea->resize(width() - st::notifySendReply.width - 2 * st::notifyBorderWidth, st::notifySendReply.height);
	_replyArea->moveToLeft(st::notifyBorderWidth, st::notifyMinHeight);
	_replyArea->show();
	_replyArea->setFocus();
	_replyArea->setMaxLength(MaxMessageSize);
	_replyArea->setCtrlEnterSubmit(CtrlEnterSubmitBoth);
	connect(_replyArea, SIGNAL(resized()), this, SLOT(onReplyResize()));
	connect(_replyArea, SIGNAL(submitted(bool)), this, SLOT(onReplySubmit(bool)));
	connect(_replyArea, SIGNAL(cancelled()), this, SLOT(onReplyCancel()));

	_replySend = new Ui::IconButton(this, st::notifySendReply);
	_replySend->moveToRight(st::notifyBorderWidth, st::notifyMinHeight);
	_replySend->show();
	_replySend->setClickedCallback([this] { sendReply(); });

	toggleActionButtons(false);

	a_top.finish();
	a_bottom.finish();

	_a_movement.stop();
	auto top = a_top.current() - _replyArea->height() - st::notifyBorderWidth;
	auto bottom = a_top.current() + st::notifyMinHeight;
	a_top = anim::ivalue(top, top);
	a_bottom = anim::ivalue(bottom, bottom);
	setGeometry(x(), top, width(), bottom - top);
	update();
}

void Widget::sendReply() {
	if (!_history) return;

	if (auto manager = ManagerInstance.data()) {
		auto peerId = _history->peer->id;
		auto msgId = _item ? _item->id : ShowAtUnreadMsgId;
		manager->notificationReplied(peerId, msgId, _replyArea->getLastText());
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
	if (!_replyArea) {
		toggleActionButtons(true);
	}
}

void Widget::leaveEvent(QEvent *e) {
	if (!_history) return;
	if (auto manager = ManagerInstance.data()) {
		manager->startAllHiding();
	}
	toggleActionButtons(false);
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
	Painter p(this);
	p.drawPixmap(0, 0, _cache);

	auto buttonsLeft = st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft;
	auto buttonsTop = st::notifyTextTop + st::msgNameFont->height;
	if (a_actionsOpacity.animating(getms())) {
		p.setOpacity(a_actionsOpacity.current());
		p.drawPixmapRight(0, buttonsTop, width(), _buttonsCache);
	} else if (_actionsVisible) {
		p.drawPixmapRight(0, buttonsTop, width(), _buttonsCache);
	}

	if (height() > st::notifyMinHeight) {
		auto replyAreaHeight = height() - st::notifyMinHeight - st::notifyBorderWidth;
		if (replyAreaHeight > 0) {
			p.fillRect(st::notifyBorderWidth, st::notifyMinHeight, width() - 2 * st::notifyBorderWidth, replyAreaHeight, st::notifyBg);
		}
		p.fillRect(0, st::notifyMinHeight, st::notifyBorderWidth, height() - st::notifyMinHeight, st::notifyBorder);
		p.fillRect(width() - st::notifyBorderWidth, st::notifyMinHeight, st::notifyBorderWidth, height() - st::notifyMinHeight, st::notifyBorder);
		p.fillRect(st::notifyBorderWidth, height() - st::notifyBorderWidth, width() - 2 * st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder);
	}
}

void Widget::animHide(float64 duration, anim::transition func) {
	if (!_history) return;
	_alphaDuration = duration;
	a_func = func;
	a_opacity.start(0);
	_hiding = true;
	_a_appearance.start();
}

void Widget::stopHiding() {
	if (!_history) return;
	_alphaDuration = st::notifyFastAnim;
	a_func = anim::linear;
	a_opacity.start(1);
	_hiding = false;
	_hideTimer.stop();
	_a_appearance.start();
}

void Widget::hideByTimer() {
	if (!_history) return;
	animHide(st::notifySlowHide, st::notifySlowHideFunc);
}

void Widget::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / float64(_alphaDuration);
	if (dt >= 1) {
		a_opacity.finish();
		_a_appearance.stop();
		if (_hiding) {
			deleteLater();
		}
	} else {
		a_opacity.update(dt, a_func);
	}
	setWindowOpacity(a_opacity.current());
	update();
}

void Widget::step_movement(float64 ms, bool timer) {
	float64 dt = ms / float64(st::notifyFastAnim);
	if (dt >= 1) {
		a_top.finish();
		a_bottom.finish();
	} else {
		a_top.update(dt, anim::linear);
		a_bottom.update(dt, anim::linear);
	}
	setGeometry(x(), a_top.current(), width(), a_bottom.current() - a_top.current());
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
