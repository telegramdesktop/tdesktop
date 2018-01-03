/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_manager_default.h"

#include "platform/platform_notifications_manager.h"
#include "application.h"
#include "messenger.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "dialogs/dialogs_layout.h"
#include "window/themes/window_theme.h"
#include "styles/style_dialogs.h"
#include "styles/style_boxes.h"
#include "styles/style_window.h"
#include "storage/file_download.h"
#include "auth_session.h"
#include "platform/platform_specific.h"

namespace Window {
namespace Notifications {
namespace Default {
namespace {

int notificationMaxHeight() {
	return st::notifyMinHeight + st::notifyReplyArea.heightMax + st::notifyBorderWidth;
}

QPoint notificationStartPosition() {
	auto r = psDesktopRect();
	auto isLeft = Notify::IsLeftCorner(Global::NotificationsCorner());
	auto isTop = Notify::IsTopCorner(Global::NotificationsCorner());
	auto x = (isLeft == rtl()) ? (r.x() + r.width() - st::notifyWidth - st::notifyDeltaX) : (r.x() + st::notifyDeltaX);
	auto y = isTop ? r.y() : (r.y() + r.height());
	return QPoint(x, y);
}

internal::Widget::Direction notificationShiftDirection() {
	auto isTop = Notify::IsTopCorner(Global::NotificationsCorner());
	return isTop ? internal::Widget::Direction::Down : internal::Widget::Direction::Up;
}

} // namespace

std::unique_ptr<Manager> Create(System *system) {
	return std::make_unique<Manager>(system);
}

Manager::Manager(System *system) : Notifications::Manager(system) {
	subscribe(system->authSession()->downloader().taskFinished(), [this] {
		for_const (auto &notification, _notifications) {
			notification->updatePeerPhoto();
		}
	});
	subscribe(system->settingsChanged(), [this](ChangeType change) {
		settingsChanged(change);
	});
	_inputCheckTimer.setTimeoutHandler([this] { checkLastInput(); });
}

QPixmap Manager::hiddenUserpicPlaceholder() const {
	if (_hiddenUserpicPlaceholder.isNull()) {
		_hiddenUserpicPlaceholder = App::pixmapFromImageInPlace(Messenger::Instance().logoNoMargin().scaled(st::notifyPhotoSize, st::notifyPhotoSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		_hiddenUserpicPlaceholder.setDevicePixelRatio(cRetinaFactor());
	}
	return _hiddenUserpicPlaceholder;
}

bool Manager::hasReplyingNotification() const {
	for_const (auto &notification, _notifications) {
		if (notification->isReplying()) {
			return true;
		}
	}
	return false;
}

void Manager::settingsChanged(ChangeType change) {
	if (change == ChangeType::Corner) {
		auto startPosition = notificationStartPosition();
		auto shiftDirection = notificationShiftDirection();
		for_const (auto &notification, _notifications) {
			notification->updatePosition(startPosition, shiftDirection);
		}
		if (_hideAll) {
			_hideAll->updatePosition(startPosition, shiftDirection);
		}
	} else if (change == ChangeType::MaxCount) {
		int allow = Global::NotificationsCount();
		for (int i = _notifications.size(); i != 0;) {
			auto &notification = _notifications[--i];
			if (notification->isUnlinked()) continue;
			if (--allow < 0) {
				notification->unlinkHistory();
			}
		}
		if (allow > 0) {
			for (int i = 0; i != allow; ++i) {
				showNextFromQueue();
			}
		}
	} else if (change == ChangeType::DemoIsShown) {
		auto demoIsShown = Global::NotificationsDemoIsShown();
		_demoMasterOpacity.start([this] { demoMasterOpacityCallback(); }, demoIsShown ? 1. : 0., demoIsShown ? 0. : 1., st::notifyFastAnim);
	}
}

void Manager::demoMasterOpacityCallback() {
	for_const (auto &notification, _notifications) {
		notification->updateOpacity();
	}
	if (_hideAll) {
		_hideAll->updateOpacity();
	}
}

float64 Manager::demoMasterOpacity() const {
	return _demoMasterOpacity.current(Global::NotificationsDemoIsShown() ? 0. : 1.);
}

void Manager::checkLastInput() {
	auto replying = hasReplyingNotification();
	auto waiting = false;
	for_const (auto &notification, _notifications) {
		if (!notification->checkLastInput(replying)) {
			waiting = true;
		}
	}
	if (waiting) {
		_inputCheckTimer.start(300);
	}
}

void Manager::startAllHiding() {
	if (!hasReplyingNotification()) {
		int notHidingCount = 0;
		for_const (auto &notification, _notifications) {
			if (notification->isShowing()) {
				++notHidingCount;
			} else {
				notification->startHiding();
			}
		}
		notHidingCount += _queuedNotifications.size();
		if (_hideAll && notHidingCount < 2) {
			_hideAll->startHiding();
		}
	}
}

void Manager::stopAllHiding() {
	for_const (auto &notification, _notifications) {
		notification->stopHiding();
	}
	if (_hideAll) {
		_hideAll->stopHiding();
	}
}

void Manager::showNextFromQueue() {
	auto guard = gsl::finally([this] {
		if (_positionsOutdated) {
			moveWidgets();
		}
	});
	if (_queuedNotifications.empty()) {
		return;
	}
	int count = Global::NotificationsCount();
	for_const (auto &notification, _notifications) {
		if (notification->isUnlinked()) continue;
		--count;
	}
	if (count <= 0) {
		return;
	}

	auto startPosition = notificationStartPosition();
	auto startShift = 0;
	auto shiftDirection = notificationShiftDirection();
	do {
		auto queued = _queuedNotifications.front();
		_queuedNotifications.pop_front();

		auto notification = std::make_unique<Notification>(
			this,
			queued.history,
			queued.peer,
			queued.author,
			queued.item,
			queued.forwardedCount,
			startPosition, startShift, shiftDirection);
		_notifications.push_back(std::move(notification));
		--count;
	} while (count > 0 && !_queuedNotifications.empty());

	_positionsOutdated = true;
	checkLastInput();
}

void Manager::moveWidgets() {
	auto shift = st::notifyDeltaY;
	int lastShift = 0, lastShiftCurrent = 0, count = 0;
	for (int i = _notifications.size(); i != 0;) {
		auto &notification = _notifications[--i];
		if (notification->isUnlinked()) continue;

		notification->changeShift(shift);
		shift += notification->height() + st::notifyDeltaY;

		lastShiftCurrent = notification->currentShift();
		lastShift = shift;

		++count;
	}

	if (count > 1 || !_queuedNotifications.empty()) {
		auto deltaY = st::notifyHideAllHeight + st::notifyDeltaY;
		if (!_hideAll) {
			_hideAll = std::make_unique<HideAllButton>(this, notificationStartPosition(), lastShiftCurrent, notificationShiftDirection());
		}
		_hideAll->changeShift(lastShift);
		_hideAll->stopHiding();
	} else if (_hideAll) {
		_hideAll->startHidingFast();
	}
}

void Manager::changeNotificationHeight(Notification *notification, int newHeight) {
	auto deltaHeight = newHeight - notification->height();
	if (!deltaHeight) return;

	notification->addToHeight(deltaHeight);
	auto it = std::find_if(_notifications.cbegin(), _notifications.cend(), [notification](auto &item) {
		return (item.get() == notification);
	});
	if (it != _notifications.cend()) {
		for (auto i = _notifications.cbegin(); i != it; ++i) {
			auto &notification = *i;
			if (notification->isUnlinked()) continue;

			notification->addToShift(deltaHeight);
		}
	}
	if (_hideAll) {
		_hideAll->addToShift(deltaHeight);
	}
}

void Manager::unlinkFromShown(Notification *remove) {
	if (remove) {
		if (remove->unlinkHistory()) {
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::removeWidget(internal::Widget *remove) {
	if (remove == _hideAll.get()) {
		_hideAll.reset();
	} else if (remove) {
		auto it = std::find_if(_notifications.cbegin(), _notifications.cend(), [remove](auto &item) {
			return item.get() == remove;
		});
		if (it != _notifications.cend()) {
			_notifications.erase(it);
			_positionsOutdated = true;
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
	for_const (auto &notification, _notifications) {
		notification->unlinkHistory();
	}
	showNextFromQueue();
}

void Manager::doClearAllFast() {
	_queuedNotifications.clear();
	base::take(_notifications);
	base::take(_hideAll);
}

void Manager::doClearFromHistory(History *history) {
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.cend();) {
		if (i->history == history) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}
	for_const (auto &notification, _notifications) {
		if (notification->unlinkHistory(history)) {
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::doClearFromItem(HistoryItem *item) {
	_queuedNotifications.erase(std::remove_if(_queuedNotifications.begin(), _queuedNotifications.end(), [item](auto &queued) {
		return (queued.item == item);
	}), _queuedNotifications.cend());

	auto showNext = false;
	for_const (auto &notification, _notifications) {
		if (notification->unlinkItem(item)) {
			showNext = true;
		}
	}
	if (showNext) {
		// This call invalidates _notifications iterators.
		showNextFromQueue();
	}
}

void Manager::doUpdateAll() {
	for_const (auto &notification, _notifications) {
		notification->updateNotifyDisplay();
	}
}

Manager::~Manager() {
	clearAllFast();
}

namespace internal {

Widget::Widget(Manager *manager, QPoint startPosition, int shift, Direction shiftDirection) : TWidget(nullptr)
, _manager(manager)
, _startPosition(startPosition)
, _direction(shiftDirection)
, a_shift(shift)
, _a_shift(animation(this, &Widget::step_shift)) {
	setWindowOpacity(0.);

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint | Qt::NoDropShadowWindowHint | Qt::Tool);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_OpaquePaintEvent);

	Platform::InitOnTopPanel(this);

	_a_opacity.start([this] { opacityAnimationCallback(); }, 0., 1., st::notifyFastAnim);
}

void Widget::destroyDelayed() {
	hide();
	if (_deleted) return;
	_deleted = true;

	// Ubuntu has a lag if a fully transparent widget is destroyed immediately.
	App::CallDelayed(1000, this, [this] {
		manager()->removeWidget(this);
	});
}

void Widget::opacityAnimationCallback() {
	updateOpacity();
	update();
	if (!_a_opacity.animating() && _hiding) {
		destroyDelayed();
	}
}

void Widget::step_shift(float64 ms, bool timer) {
	float64 dt = ms / float64(st::notifyFastAnim);
	if (dt >= 1) {
		a_shift.finish();
	} else {
		a_shift.update(dt, anim::linear);
	}
	moveByShift();
}

void Widget::hideSlow() {
	hideAnimated(st::notifySlowHide, anim::easeInCirc);
}

void Widget::hideFast() {
	hideAnimated(st::notifyFastAnim, anim::linear);
}

void Widget::hideStop() {
	if (_hiding) {
		_hiding = false;
		_a_opacity.start([this] { opacityAnimationCallback(); }, 0., 1., st::notifyFastAnim);
	}
}

void Widget::hideAnimated(float64 duration, const anim::transition &func) {
	_hiding = true;
	_a_opacity.start([this] { opacityAnimationCallback(); }, 1., 0., duration, func);
}

void Widget::updateOpacity() {
	setWindowOpacity(_a_opacity.current(_hiding ? 0. : 1.) * _manager->demoMasterOpacity());
}

void Widget::changeShift(int top) {
	a_shift.start(top);
	_a_shift.start();
}

void Widget::updatePosition(QPoint startPosition, Direction shiftDirection) {
	_startPosition = startPosition;
	_direction = shiftDirection;
	moveByShift();
}

void Widget::addToHeight(int add) {
	auto newHeight = height() + add;
	auto newPosition = computePosition(newHeight);
	updateGeometry(newPosition.x(), newPosition.y(), width(), newHeight);
	psUpdateOverlayed(this);
}

void Widget::updateGeometry(int x, int y, int width, int height) {
	setGeometry(x, y, width, height);
	update();
}

void Widget::addToShift(int add) {
	a_shift.add(add);
	moveByShift();
}

void Widget::moveByShift() {
	move(computePosition(height()));
}

QPoint Widget::computePosition(int height) const {
	auto realShift = qRound(a_shift.current());
	if (_direction == Direction::Up) {
		realShift = -realShift - height;
	}
	return QPoint(_startPosition.x(), _startPosition.y() + realShift);
}

Background::Background(QWidget *parent) : TWidget(parent) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void Background::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(rect(), st::notificationBg);
	p.fillRect(0, 0, st::notifyBorderWidth, height(), st::notifyBorder);
	p.fillRect(width() - st::notifyBorderWidth, 0, st::notifyBorderWidth, height(), st::notifyBorder);
	p.fillRect(st::notifyBorderWidth, height() - st::notifyBorderWidth, width() - 2 * st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder);
}

Notification::Notification(Manager *manager, History *history, PeerData *peer, PeerData *author, HistoryItem *msg, int forwardedCount, QPoint startPosition, int shift, Direction shiftDirection) : Widget(manager, startPosition, shift, shiftDirection)
, _history(history)
, _peer(peer)
, _author(author)
, _item(msg)
, _forwardedCount(forwardedCount)
#ifdef Q_OS_WIN
, _started(GetTickCount())
#endif // Q_OS_WIN
, _close(this, st::notifyClose)
, _reply(this, langFactory(lng_notification_reply), st::defaultBoxButton) {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });

	auto position = computePosition(st::notifyMinHeight);
	updateGeometry(position.x(), position.y(), st::notifyWidth, st::notifyMinHeight);

	_userpicLoaded = _peer ? _peer->userpicLoaded() : true;
	updateNotifyDisplay();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideByTimer()));

	_close->setClickedCallback([this] {
		unlinkHistoryInManager();
	});
	_close->setAcceptBoth(true);
	_close->moveToRight(st::notifyClosePos.x(), st::notifyClosePos.y());
	_close->show();

	_reply->setClickedCallback([this] {
		showReplyField();
	});
	_replyPadding = st::notifyMinHeight - st::notifyPhotoPos.y() - st::notifyPhotoSize;
	updateReplyGeometry();
	_reply->hide();

	prepareActionsCache();

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &data) {
		if (data.paletteChanged()) {
			updateNotifyDisplay();
			if (!_buttonsCache.isNull()) {
				prepareActionsCache();
			}
			update();
			if (_background) {
				_background->update();
			}
		}
	});

	show();
}

void Notification::updateReplyGeometry() {
	_reply->moveToRight(_replyPadding, height() - _reply->height() - _replyPadding);
}

void Notification::refreshLang() {
	InvokeQueued(this, [this] { updateReplyGeometry(); });
}

void Notification::prepareActionsCache() {
	auto replyCache = Ui::GrabWidget(_reply);
	auto fadeWidth = st::notifyFadeRight.width();
	auto actionsTop = st::notifyTextTop + st::msgNameFont->height;
	auto replyRight = _replyPadding - st::notifyBorderWidth;
	auto actionsCacheWidth = _reply->width() + replyRight + fadeWidth;
	auto actionsCacheHeight = height() - actionsTop - st::notifyBorderWidth;
	auto actionsCacheImg = QImage(QSize(actionsCacheWidth, actionsCacheHeight) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	actionsCacheImg.setDevicePixelRatio(cRetinaFactor());
	actionsCacheImg.fill(Qt::transparent);
	{
		Painter p(&actionsCacheImg);
		st::notifyFadeRight.fill(p, rtlrect(0, 0, fadeWidth, actionsCacheHeight, actionsCacheWidth));
		p.fillRect(rtlrect(fadeWidth, 0, actionsCacheWidth - fadeWidth, actionsCacheHeight, actionsCacheWidth), st::notificationBg);
		p.drawPixmapRight(replyRight, _reply->y() - actionsTop, actionsCacheWidth, replyCache);
	}
	_buttonsCache = App::pixmapFromImageInPlace(std::move(actionsCacheImg));
}

bool Notification::checkLastInput(bool hasReplyingNotifications) {
	if (!_waitingForInput) return true;

	auto wasUserInput = true; // TODO
#ifdef Q_OS_WIN
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);
	BOOL res = GetLastInputInfo(&lii);
	wasUserInput = (!res || lii.dwTime >= _started);
#endif // Q_OS_WIN
	if (wasUserInput) {
		_waitingForInput = false;
		if (!hasReplyingNotifications) {
			_hideTimer.start(st::notifyWaitLongHide);
		}
		return true;
	}
	return false;
}

void Notification::onReplyResize() {
	changeHeight(st::notifyMinHeight + _replyArea->height() + st::notifyBorderWidth);
}

void Notification::onReplySubmit(bool ctrlShiftEnter) {
	sendReply();
}

void Notification::onReplyCancel() {
	unlinkHistoryInManager();
}

void Notification::updateGeometry(int x, int y, int width, int height) {
	if (height > st::notifyMinHeight) {
		if (!_background) {
			_background.create(this);
		}
		_background->setGeometry(0, st::notifyMinHeight, width, height - st::notifyMinHeight);
	} else if (_background) {
		_background.destroy();
	}
	Widget::updateGeometry(x, y, width, height);
}

void Notification::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setClipRect(e->rect());
	p.drawPixmap(0, 0, _cache);

	auto buttonsLeft = st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft;
	auto buttonsTop = st::notifyTextTop + st::msgNameFont->height;
	if (a_actionsOpacity.animating(getms())) {
		p.setOpacity(a_actionsOpacity.current());
		p.drawPixmapRight(st::notifyBorderWidth, buttonsTop, width(), _buttonsCache);
	} else if (_actionsVisible) {
		p.drawPixmapRight(st::notifyBorderWidth, buttonsTop, width(), _buttonsCache);
	}
}

void Notification::actionsOpacityCallback() {
	update();
	if (!a_actionsOpacity.animating() && _actionsVisible) {
		_reply->show();
	}
}

void Notification::updateNotifyDisplay() {
	if (!_history || !_peer || (!_item && _forwardedCount < 2)) return;

	auto options = Manager::getNotificationOptions(_item);
	_hideReplyButton = options.hideReplyButton;

	int32 w = width(), h = height();
	QImage img(w * cIntRetinaFactor(), h * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	if (cRetina()) img.setDevicePixelRatio(cRetinaFactor());
	img.fill(st::notificationBg->c);

	{
		Painter p(&img);
		p.fillRect(0, 0, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder);
		p.fillRect(w - st::notifyBorderWidth, 0, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder);
		p.fillRect(st::notifyBorderWidth, h - st::notifyBorderWidth, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder);
		p.fillRect(0, st::notifyBorderWidth, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder);

		if (!options.hideNameAndPhoto) {
			_history->peer->loadUserpic(true, true);
			_history->peer->paintUserpicLeft(p, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), width(), st::notifyPhotoSize);
		} else {
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), manager()->hiddenUserpicPlaceholder());
		}

		int32 itemWidth = w - st::notifyPhotoPos.x() - st::notifyPhotoSize - st::notifyTextLeft - st::notifyClosePos.x() - st::notifyClose.width;

		QRect rectForName(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyTextTop, itemWidth, st::msgNameFont->height);
		if (!options.hideNameAndPhoto) {
			if (auto chatTypeIcon = Dialogs::Layout::ChatTypeIcon(_history->peer, false, false)) {
				chatTypeIcon->paint(p, rectForName.topLeft(), w);
				rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
			}
		}

		if (!options.hideMessageText) {
			const HistoryItem *textCachedFor = 0;
			Text itemTextCache(itemWidth);
			QRect r(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyItemTop + st::msgNameFont->height, itemWidth, 2 * st::dialogsTextFont->height);
			if (_item) {
				auto active = false, selected = false;
				_item->drawInDialog(
					p,
					r,
					active,
					selected,
					HistoryItem::DrawInDialog::Normal,
					textCachedFor,
					itemTextCache);
			} else if (_forwardedCount > 1) {
				p.setFont(st::dialogsTextFont);
				if (_author) {
					itemTextCache.setText(st::dialogsTextStyle, _author->name);
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
		if (!options.hideNameAndPhoto) {
			_history->peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
		} else {
			p.setFont(st::msgNameFont);
			static QString notifyTitle = st::msgNameFont->elided(qsl("Telegram Desktop"), rectForName.width());
			p.drawText(rectForName.left(), rectForName.top() + st::msgNameFont->ascent, notifyTitle);
		}
	}

	_cache = App::pixmapFromImageInPlace(std::move(img));
	if (!canReply()) {
		toggleActionButtons(false);
	}
	update();
}

void Notification::updatePeerPhoto() {
	if (_userpicLoaded || !_peer || !_peer->userpicLoaded()) {
		return;
	}
	_userpicLoaded = true;

	auto img = _cache.toImage();
	{
		Painter p(&img);
		_peer->paintUserpicLeft(p, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), width(), st::notifyPhotoSize);
	}
	_cache = App::pixmapFromImageInPlace(std::move(img));
	update();
}

bool Notification::unlinkItem(HistoryItem *deleted) {
	auto unlink = (_item && _item == deleted);
	if (unlink) {
		_item = nullptr;
		unlinkHistory();
	}
	return unlink;
}

bool Notification::canReply() const {
	return !_hideReplyButton && (_item != nullptr) && !App::passcoded() && (Global::NotifyView() <= dbinvShowPreview);
}

void Notification::unlinkHistoryInManager() {
	manager()->unlinkFromShown(this);
}

void Notification::toggleActionButtons(bool visible) {
	if (_actionsVisible != visible) {
		_actionsVisible = visible;
		a_actionsOpacity.start([this] { actionsOpacityCallback(); }, _actionsVisible ? 0. : 1., _actionsVisible ? 1. : 0., st::notifyActionsDuration);
		_reply->clearState();
		_reply->hide();
	}
}

void Notification::showReplyField() {
	activateWindow();

	if (_replyArea) {
		_replyArea->setFocus();
		return;
	}
	stopHiding();

	_background.create(this);
	_background->setGeometry(0, st::notifyMinHeight, width(), st::notifySendReply.height + st::notifyBorderWidth);
	_background->show();

	_replyArea.create(this, st::notifyReplyArea, langFactory(lng_message_ph), QString());
	_replyArea->resize(width() - st::notifySendReply.width - 2 * st::notifyBorderWidth, st::notifySendReply.height);
	_replyArea->moveToLeft(st::notifyBorderWidth, st::notifyMinHeight);
	_replyArea->show();
	_replyArea->setFocus();
	_replyArea->setMaxLength(MaxMessageSize);
	_replyArea->setCtrlEnterSubmit(Ui::CtrlEnterSubmit::Both);

	// Catch mouse press event to activate the window.
	QCoreApplication::instance()->installEventFilter(this);
	connect(_replyArea, SIGNAL(resized()), this, SLOT(onReplyResize()));
	connect(_replyArea, SIGNAL(submitted(bool)), this, SLOT(onReplySubmit(bool)));
	connect(_replyArea, SIGNAL(cancelled()), this, SLOT(onReplyCancel()));

	_replySend.create(this, st::notifySendReply);
	_replySend->moveToRight(st::notifyBorderWidth, st::notifyMinHeight);
	_replySend->show();
	_replySend->setClickedCallback([this] { sendReply(); });

	toggleActionButtons(false);

	onReplyResize();
	update();
}

void Notification::sendReply() {
	if (!_history) return;

	auto peerId = _history->peer->id;
	auto msgId = _item ? _item->id : ShowAtUnreadMsgId;
	manager()->notificationReplied(peerId, msgId, _replyArea->getLastText());

	manager()->startAllHiding();
}

void Notification::changeHeight(int newHeight) {
	manager()->changeNotificationHeight(this, newHeight);
}

bool Notification::unlinkHistory(History *history) {
	auto unlink = _history && (history == _history || !history);
	if (unlink) {
		hideFast();
		_history = nullptr;
		_item = nullptr;
	}
	return unlink;
}

void Notification::enterEventHook(QEvent *e) {
	if (!_history) return;
	manager()->stopAllHiding();
	if (!_replyArea && canReply()) {
		toggleActionButtons(true);
	}
}

void Notification::leaveEventHook(QEvent *e) {
	if (!_history) return;
	manager()->startAllHiding();
	toggleActionButtons(false);
}

void Notification::startHiding() {
	if (!_history) return;
	hideSlow();
}

void Notification::mousePressEvent(QMouseEvent *e) {
	if (!_history) return;

	if (e->button() == Qt::RightButton) {
		unlinkHistoryInManager();
	} else {
		e->ignore();
		auto peerId = _history->peer->id;
		auto msgId = _item ? _item->id : ShowAtUnreadMsgId;
		manager()->notificationActivated(peerId, msgId);
	}
}

bool Notification::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::MouseButtonPress) {
		if (auto receiver = qobject_cast<QWidget*>(o)) {
			if (isAncestorOf(receiver)) {
				activateWindow();
			}
		}
	}
	return false;
}

void Notification::stopHiding() {
	if (!_history) return;
	_hideTimer.stop();
	Widget::hideStop();
}

void Notification::onHideByTimer() {
	startHiding();
}

HideAllButton::HideAllButton(Manager *manager, QPoint startPosition, int shift, Direction shiftDirection) : Widget(manager, startPosition, shift, shiftDirection) {
	setCursor(style::cur_pointer);

	auto position = computePosition(st::notifyHideAllHeight);
	updateGeometry(position.x(), position.y(), st::notifyWidth, st::notifyHideAllHeight);
	hide();
	createWinId();

	subscribe(Window::Theme::Background(), [this](const Window::Theme::BackgroundUpdate &data) {
		if (data.paletteChanged()) {
			update();
		}
	});

	show();
}

void HideAllButton::startHiding() {
	hideSlow();
}

void HideAllButton::startHidingFast() {
	hideFast();
}

void HideAllButton::stopHiding() {
	hideStop();
}

void HideAllButton::enterEventHook(QEvent *e) {
	_mouseOver = true;
	update();
}

void HideAllButton::leaveEventHook(QEvent *e) {
	_mouseOver = false;
	update();
}

void HideAllButton::mousePressEvent(QMouseEvent *e) {
	_mouseDown = true;
}

void HideAllButton::mouseReleaseEvent(QMouseEvent *e) {
	auto mouseDown = base::take(_mouseDown);
	if (mouseDown && _mouseOver) {
		manager()->clearAll();
	}
}

void HideAllButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setClipRect(e->rect());

	p.fillRect(rect(), _mouseOver ? st::lightButtonBgOver : st::lightButtonBg);
	p.fillRect(0, 0, width(), st::notifyBorderWidth, st::notifyBorder);
	p.fillRect(0, height() - st::notifyBorderWidth, width(), st::notifyBorderWidth, st::notifyBorder);
	p.fillRect(0, st::notifyBorderWidth, st::notifyBorderWidth, height() - 2 * st::notifyBorderWidth, st::notifyBorder);
	p.fillRect(width() - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorderWidth, height() - 2 * st::notifyBorderWidth, st::notifyBorder);

	p.setFont(st::defaultLinkButton.font);
	p.setPen(_mouseOver ? st::lightButtonFgOver : st::lightButtonFg);
	p.drawText(rect(), lang(lng_notification_hide_all), style::al_center);
}

} // namespace internal
} // namespace Default
} // namespace Notifications
} // namespace Window
