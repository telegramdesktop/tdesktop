/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/notifications_manager_default.h"

#include "platform/platform_notifications_manager.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/text/text_options.h"
#include "ui/emoji_config.h"
#include "ui/empty_userpic.h"
#include "ui/ui_utility.h"
#include "dialogs/dialogs_layout.h"
#include "window/themes/window_theme.h"
#include "storage/file_download.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "history/history.h"
#include "history/history_item.h"
#include "base/platform/base_platform_last_input.h"
#include "base/call_delayed.h"
#include "facades.h"
#include "app.h"
#include "mainwindow.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

#include <QtCore/QCoreApplication>

namespace Window {
namespace Notifications {
namespace Default {
namespace {

int notificationMaxHeight() {
	return st::notifyMinHeight + st::notifyReplyArea.heightMax + st::notifyBorderWidth;
}

QPoint notificationStartPosition() {
	const auto corner = Core::App().settings().notificationsCorner();
	const auto r = psDesktopRect();
	const auto isLeft = Core::Settings::IsLeftCorner(corner);
	const auto isTop = Core::Settings::IsTopCorner(corner);
	const auto x = (isLeft == rtl()) ? (r.x() + r.width() - st::notifyWidth - st::notifyDeltaX) : (r.x() + st::notifyDeltaX);
	const auto y = isTop ? r.y() : (r.y() + r.height());
	return QPoint(x, y);
}

internal::Widget::Direction notificationShiftDirection() {
	auto isTop = Core::Settings::IsTopCorner(Core::App().settings().notificationsCorner());
	return isTop ? internal::Widget::Direction::Down : internal::Widget::Direction::Up;
}

} // namespace

std::unique_ptr<Manager> Create(System *system) {
	return std::make_unique<Manager>(system);
}

Manager::Manager(System *system)
: Notifications::Manager(system)
, _inputCheckTimer([=] { checkLastInput(); }) {
	subscribe(system->settingsChanged(), [this](ChangeType change) {
		settingsChanged(change);
	});
}

Manager::QueuedNotification::QueuedNotification(
	not_null<HistoryItem*> item,
	int forwardedCount)
: history(item->history())
, peer(history->peer)
, author(item->notificationHeader())
, item((forwardedCount < 2) ? item.get() : nullptr)
, forwardedCount(forwardedCount)
, fromScheduled((item->out() || peer->isSelf()) && item->isFromScheduled()) {
}

QPixmap Manager::hiddenUserpicPlaceholder() const {
	if (_hiddenUserpicPlaceholder.isNull()) {
		_hiddenUserpicPlaceholder = App::pixmapFromImageInPlace(Core::App().logoNoMargin().scaled(st::notifyPhotoSize, st::notifyPhotoSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
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
		int allow = Core::App().settings().notificationsCount();
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
	return _demoMasterOpacity.value(Global::NotificationsDemoIsShown() ? 0. : 1.);
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
		_inputCheckTimer.callOnce(300);
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
	int count = Core::App().settings().notificationsCount();
	for (const auto &notification : _notifications) {
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

		subscribeToSession(&queued.history->session());
		_notifications.push_back(std::make_unique<Notification>(
			this,
			queued.history,
			queued.peer,
			queued.author,
			queued.item,
			queued.forwardedCount,
			queued.fromScheduled,
			startPosition,
			startShift,
			shiftDirection));
		--count;
	} while (count > 0 && !_queuedNotifications.empty());

	_positionsOutdated = true;
	checkLastInput();
}

void Manager::subscribeToSession(not_null<Main::Session*> session) {
	auto i = _subscriptions.find(session);
	if (i == _subscriptions.end()) {
		i = _subscriptions.emplace(session).first;
		session->account().sessionChanges(
		) | rpl::start_with_next([=] {
			_subscriptions.remove(session);
		}, i->second.lifetime);
	} else if (i->second.subscription) {
		return;
	}
	session->downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		auto found = false;
		for (const auto &notification : _notifications) {
			if (const auto history = notification->maybeHistory()) {
				if (&history->session() == session) {
					notification->updatePeerPhoto();
					found = true;
				}
			}
		}
		if (!found) {
			_subscriptions[session].subscription.destroy();
		}
	}, i->second.subscription);
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
		const auto it = ranges::find(
			_notifications,
			remove,
			&std::unique_ptr<Notification>::get);
		if (it != end(_notifications)) {
			_notifications.erase(it);
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::doShowNotification(
		not_null<HistoryItem*> item,
		int forwardedCount) {
	_queuedNotifications.emplace_back(item, forwardedCount);
	showNextFromQueue();
}

void Manager::doClearAll() {
	_queuedNotifications.clear();
	for (const auto &notification : _notifications) {
		notification->unlinkHistory();
	}
	showNextFromQueue();
}

void Manager::doClearAllFast() {
	_queuedNotifications.clear();
	base::take(_notifications);
	base::take(_hideAll);
}

void Manager::doClearFromHistory(not_null<History*> history) {
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.cend();) {
		if (i->history == history) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}
	for (const auto &notification : _notifications) {
		if (notification->unlinkHistory(history)) {
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::doClearFromSession(not_null<Main::Session*> session) {
	for (auto i = _queuedNotifications.begin(); i != _queuedNotifications.cend();) {
		if (&i->history->session() == session) {
			i = _queuedNotifications.erase(i);
		} else {
			++i;
		}
	}
	for (const auto &notification : _notifications) {
		if (notification->unlinkSession(session)) {
			_positionsOutdated = true;
		}
	}
	showNextFromQueue();
}

void Manager::doClearFromItem(not_null<HistoryItem*> item) {
	_queuedNotifications.erase(std::remove_if(_queuedNotifications.begin(), _queuedNotifications.end(), [&](auto &queued) {
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

Widget::Widget(
	not_null<Manager*> manager,
	QPoint startPosition,
	int shift,
	Direction shiftDirection)
: RpWidget(Core::App().getModalParent())
, _manager(manager)
, _startPosition(startPosition)
, _direction(shiftDirection)
, _shift(shift)
, _shiftAnimation([=](crl::time now) {
	return shiftAnimationCallback(now);
}) {
	setWindowOpacity(0.);

	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint)
		| Qt::WindowStaysOnTopHint
		| Qt::BypassWindowManagerHint
		| Qt::NoDropShadowWindowHint
		| Qt::Tool);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_OpaquePaintEvent);

	Ui::Platform::InitOnTopPanel(this);

	_a_opacity.start([this] { opacityAnimationCallback(); }, 0., 1., st::notifyFastAnim);
}

void Widget::destroyDelayed() {
	hide();
	if (_deleted) return;
	_deleted = true;

	// Ubuntu has a lag if a fully transparent widget is destroyed immediately.
	base::call_delayed(1000, this, [this] {
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

bool Widget::shiftAnimationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::notifyFastAnim;
	}
	const auto dt = (now - _shiftAnimation.started())
		/ float64(st::notifyFastAnim);
	if (dt >= 1.) {
		_shift.finish();
	} else {
		_shift.update(dt, anim::linear);
	}
	moveByShift();
	return (dt < 1.);
}

void Widget::hideSlow() {
	if (anim::Disabled()) {
		_hiding = true;
		base::call_delayed(
			st::notifySlowHide,
			this,
			[=, guard = _hidingDelayed.make_guard()] {
				if (guard && _hiding) {
					hideFast();
				}
			});
	} else {
		hideAnimated(st::notifySlowHide, anim::easeInCirc);
	}
}

void Widget::hideFast() {
	hideAnimated(st::notifyFastAnim, anim::linear);
}

void Widget::hideStop() {
	if (_hiding) {
		_hiding = false;
		_hidingDelayed = {};
		_a_opacity.start([this] { opacityAnimationCallback(); }, 0., 1., st::notifyFastAnim);
	}
}

void Widget::hideAnimated(float64 duration, const anim::transition &func) {
	_hiding = true;
	_a_opacity.start([this] { opacityAnimationCallback(); }, 1., 0., duration, func);
}

void Widget::updateOpacity() {
	setWindowOpacity(_a_opacity.value(_hiding ? 0. : 1.) * _manager->demoMasterOpacity());
}

void Widget::changeShift(int top) {
	_shift.start(top);
	_shiftAnimation.start();
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
	Ui::Platform::UpdateOverlayed(this);
}

void Widget::updateGeometry(int x, int y, int width, int height) {
	setGeometry(x, y, width, height);
	setMinimumSize(QSize(width, height));
	setMaximumSize(QSize(width, height));
	update();
}

void Widget::addToShift(int add) {
	_shift.add(add);
	moveByShift();
}

void Widget::moveByShift() {
	move(computePosition(height()));
}

QPoint Widget::computePosition(int height) const {
	auto realShift = qRound(_shift.current());
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

Notification::Notification(
	not_null<Manager*> manager,
	not_null<History*> history,
	not_null<PeerData*> peer,
	const QString &author,
	HistoryItem *item,
	int forwardedCount,
	bool fromScheduled,
	QPoint startPosition,
	int shift,
	Direction shiftDirection)
: Widget(manager, startPosition, shift, shiftDirection)
, _peer(peer)
, _started(crl::now())
, _history(history)
, _userpicView(_peer->createUserpicView())
, _author(author)
, _item(item)
, _forwardedCount(forwardedCount)
, _fromScheduled(fromScheduled)
, _close(this, st::notifyClose)
, _reply(this, tr::lng_notification_reply(), st::defaultBoxButton) {
	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	auto position = computePosition(st::notifyMinHeight);
	updateGeometry(position.x(), position.y(), st::notifyWidth, st::notifyMinHeight);

	_userpicLoaded = !_userpicView || (_userpicView->image() != nullptr);
	updateNotifyDisplay();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, &QTimer::timeout, [=] { startHiding(); });

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
		st::notifyFadeRight.fill(p, style::rtlrect(0, 0, fadeWidth, actionsCacheHeight, actionsCacheWidth));
		p.fillRect(style::rtlrect(fadeWidth, 0, actionsCacheWidth - fadeWidth, actionsCacheHeight, actionsCacheWidth), st::notificationBg);
		p.drawPixmapRight(replyRight, _reply->y() - actionsTop, actionsCacheWidth, replyCache);
	}
	_buttonsCache = App::pixmapFromImageInPlace(std::move(actionsCacheImg));
}

bool Notification::checkLastInput(bool hasReplyingNotifications) {
	if (!_waitingForInput) return true;

	const auto waitForUserInput = base::Platform::LastUserInputTimeSupported()
		? (Core::App().lastNonIdleTime() <= _started)
		: false;
	if (!waitForUserInput) {
		_waitingForInput = false;
		if (!hasReplyingNotifications) {
			_hideTimer.start(st::notifyWaitLongHide);
		}
		return true;
	}
	return false;
}

void Notification::replyResized() {
	changeHeight(st::notifyMinHeight + _replyArea->height() + st::notifyBorderWidth);
}

void Notification::replyCancel() {
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
	if (a_actionsOpacity.animating()) {
		p.setOpacity(a_actionsOpacity.value(1.));
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
	if (!_history || (!_item && _forwardedCount < 2)) return;

	const auto options = Manager::GetNotificationOptions(_item);
	_hideReplyButton = options.hideReplyButton;

	int32 w = width(), h = height();
	QImage img(w * cIntRetinaFactor(), h * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	img.setDevicePixelRatio(cRetinaFactor());
	img.fill(st::notificationBg->c);

	{
		Painter p(&img);
		p.fillRect(0, 0, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder);
		p.fillRect(w - st::notifyBorderWidth, 0, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder);
		p.fillRect(st::notifyBorderWidth, h - st::notifyBorderWidth, w - st::notifyBorderWidth, st::notifyBorderWidth, st::notifyBorder);
		p.fillRect(0, st::notifyBorderWidth, st::notifyBorderWidth, h - st::notifyBorderWidth, st::notifyBorder);

		if (!options.hideNameAndPhoto) {
			if (_fromScheduled && _history->peer->isSelf()) {
				Ui::EmptyUserpic::PaintSavedMessages(p, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), width(), st::notifyPhotoSize);
				_userpicLoaded = true;
			} else if (_history->peer->isRepliesChat()) {
				Ui::EmptyUserpic::PaintRepliesMessages(p, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), width(), st::notifyPhotoSize);
				_userpicLoaded = true;
			} else {
				_userpicView = _history->peer->createUserpicView();
				_history->peer->loadUserpic();
				_history->peer->paintUserpicLeft(p, _userpicView, st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), width(), st::notifyPhotoSize);
			}
		} else {
			p.drawPixmap(st::notifyPhotoPos.x(), st::notifyPhotoPos.y(), manager()->hiddenUserpicPlaceholder());
			_userpicLoaded = true;
		}

		int32 itemWidth = w - st::notifyPhotoPos.x() - st::notifyPhotoSize - st::notifyTextLeft - st::notifyClosePos.x() - st::notifyClose.width;

		QRect rectForName(st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft, st::notifyTextTop, itemWidth, st::msgNameFont->height);
		const auto reminder = _fromScheduled && _history->peer->isSelf();
		if (!options.hideNameAndPhoto) {
			if (_fromScheduled) {
				static const auto emoji = Ui::Emoji::Find(QString::fromUtf8("\xF0\x9F\x93\x85"));
				const auto size = Ui::Emoji::GetSizeNormal() / cIntRetinaFactor();
				const auto top = rectForName.top() + (st::msgNameFont->height - size) / 2;
				Ui::Emoji::Draw(p, emoji, Ui::Emoji::GetSizeNormal(), rectForName.left(), top);
				rectForName.setLeft(rectForName.left() + size + st::msgNameFont->spacew);
			}
			if (const auto chatTypeIcon = Dialogs::Layout::ChatTypeIcon(_history->peer, false, false)) {
				chatTypeIcon->paint(p, rectForName.topLeft(), w);
				rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
			}
		}

		if (!options.hideMessageText) {
			auto itemTextCache = Ui::Text::String(itemWidth);
			auto r = QRect(
				st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft,
				st::notifyItemTop + st::msgNameFont->height,
				itemWidth,
				2 * st::dialogsTextFont->height);
			p.setTextPalette(st::dialogsTextPalette);
			p.setPen(st::dialogsTextFg);
			p.setFont(st::dialogsTextFont);
			const auto text = _item
				? _item->inDialogsText(reminder
					? HistoryItem::DrawInDialog::WithoutSender
					: HistoryItem::DrawInDialog::Normal)
				: ((!_author.isEmpty()
					? textcmdLink(1, _author)
					: QString())
					+ (_forwardedCount > 1
						? ('\n' + tr::lng_forward_messages(
							tr::now,
							lt_count,
							_forwardedCount))
						: QString()));
			const auto Options = TextParseOptions{
				TextParseRichText
				| (_forwardedCount > 1 ? TextParseMultiline : 0),
				0,
				0,
				Qt::LayoutDirectionAuto,
			};
			itemTextCache.setText(st::dialogsTextStyle, text, Options);
			itemTextCache.drawElided(
				p,
				r.left(),
				r.top(),
				r.width(),
				r.height() / st::dialogsTextFont->height);
			p.restoreTextPalette();
		} else {
			p.setFont(st::dialogsTextFont);
			p.setPen(st::dialogsTextFgService);
			p.drawText(
				st::notifyPhotoPos.x() + st::notifyPhotoSize + st::notifyTextLeft,
				st::notifyItemTop + st::msgNameFont->height + st::dialogsTextFont->ascent,
				st::dialogsTextFont->elided(
					tr::lng_notification_preview(tr::now),
					itemWidth));
		}

		p.setPen(st::dialogsNameFg);
		Ui::Text::String titleText;
		const auto title = options.hideNameAndPhoto
			? qsl("Telegram Desktop")
			: reminder
			? tr::lng_notification_reminder(tr::now)
			: _history->peer->nameText().toString();
		const auto fullTitle = manager()->addTargetAccountName(
			title,
			&_history->session());
		titleText.setText(st::msgNameStyle, fullTitle, Ui::NameTextOptions());
		titleText.drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());
	}

	_cache = App::pixmapFromImageInPlace(std::move(img));
	if (!canReply()) {
		toggleActionButtons(false);
	}
	update();
}

void Notification::updatePeerPhoto() {
	if (_userpicLoaded) {
		return;
	}
	_userpicView = _peer->createUserpicView();
	if (_userpicView && !_userpicView->image()) {
		return;
	}
	_userpicLoaded = true;

	auto img = _cache.toImage();
	{
		Painter p(&img);
		_peer->paintUserpicLeft(
			p,
			_userpicView,
			st::notifyPhotoPos.x(),
			st::notifyPhotoPos.y(),
			width(),
			st::notifyPhotoSize);
	}
	_cache = App::pixmapFromImageInPlace(std::move(img));
	_userpicView = nullptr;
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
	return !_hideReplyButton
		&& (_item != nullptr)
		&& !Core::App().passcodeLocked()
		&& (Core::App().settings().notifyView() <= dbinvShowPreview);
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
	if (!_item) {
		return;
	}
	activateWindow();

	if (_replyArea) {
		_replyArea->setFocus();
		return;
	}
	stopHiding();

	_background.create(this);
	_background->setGeometry(0, st::notifyMinHeight, width(), st::notifySendReply.height + st::notifyBorderWidth);
	_background->show();

	_replyArea.create(
		this,
		st::notifyReplyArea,
		Ui::InputField::Mode::MultiLine,
		tr::lng_message_ph());
	_replyArea->resize(width() - st::notifySendReply.width - 2 * st::notifyBorderWidth, st::notifySendReply.height);
	_replyArea->moveToLeft(st::notifyBorderWidth, st::notifyMinHeight);
	_replyArea->show();
	_replyArea->setFocus();
	_replyArea->setMaxLength(MaxMessageSize);
	_replyArea->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	_replyArea->setInstantReplaces(Ui::InstantReplaces::Default());
	_replyArea->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	_replyArea->setMarkdownReplacesEnabled(rpl::single(true));

	// Catch mouse press event to activate the window.
	QCoreApplication::instance()->installEventFilter(this);
	connect(_replyArea, &Ui::InputField::resized, [=] { replyResized(); });
	connect(_replyArea, &Ui::InputField::submitted, [=] { sendReply(); });
	connect(_replyArea, &Ui::InputField::cancelled, [=] { replyCancel(); });

	_replySend.create(this, st::notifySendReply);
	_replySend->moveToRight(st::notifyBorderWidth, st::notifyMinHeight);
	_replySend->show();
	_replySend->setClickedCallback([this] { sendReply(); });

	toggleActionButtons(false);

	replyResized();
	update();
}

void Notification::sendReply() {
	if (!_history) return;

	manager()->notificationReplied(
		myId(),
		_replyArea->getTextWithAppliedMarkdown());

	manager()->startAllHiding();
}

Notifications::Manager::NotificationId Notification::myId() const {
	if (!_history) {
		return {};
	}
	return { .full = {
		.sessionId = _history->session().uniqueId(),
		.peerId = _history->peer->id
	}, .msgId = _item ? _item->id : ShowAtUnreadMsgId };
}

void Notification::changeHeight(int newHeight) {
	manager()->changeNotificationHeight(this, newHeight);
}

bool Notification::unlinkHistory(History *history) {
	const auto unlink = _history && (history == _history || !history);
	if (unlink) {
		hideFast();
		_history = nullptr;
		_item = nullptr;
	}
	return unlink;
}

bool Notification::unlinkSession(not_null<Main::Session*> session) {
	const auto unlink = _history && (&_history->session() == session);
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
		manager()->notificationActivated(myId());
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

HideAllButton::HideAllButton(
	not_null<Manager*> manager,
	QPoint startPosition,
	int shift,
	Direction shiftDirection)
: Widget(manager, startPosition, shift, shiftDirection) {
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
	p.drawText(rect(), tr::lng_notification_hide_all(tr::now), style::al_center);
}

} // namespace internal
} // namespace Default
} // namespace Notifications
} // namespace Window
