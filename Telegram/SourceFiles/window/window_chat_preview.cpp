/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_chat_preview.h"

#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/view/history_view_chat_preview.h"
#include "mainwidget.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace Window {
namespace {

constexpr auto kChatPreviewDelay = crl::time(1000);

} // namespace

ChatPreviewManager::ChatPreviewManager(
	not_null<SessionController*> controller)
: _controller(controller)
, _timer([=] { showScheduled(); }) {
}

bool ChatPreviewManager::show(
		Dialogs::RowDescriptor row,
		Fn<void(bool shown)> callback,
		QPointer<QWidget> parentOverride,
		std::optional<QPoint> positionOverride) {
	cancelScheduled();
	_topicLifetime.destroy();
	if (const auto topic = row.key.topic()) {
		_topicLifetime = topic->destroyed() | rpl::start_with_next([=] {
			_menu = nullptr;
		});
	} else if (!row.key) {
		return false;
	}

	const auto parent = parentOverride
		? parentOverride
		: _controller->content();
	auto preview = HistoryView::MakeChatPreview(parent, row.key.entry());
	if (!preview.menu) {
		return false;
	}
	_menu = std::move(preview.menu);
	const auto weakMenu = Ui::MakeWeak(_menu.get());
	const auto weakThread = base::make_weak(row.key.entry()->asThread());
	const auto weakController = base::make_weak(_controller);
	std::move(
		preview.actions
	) | rpl::start_with_next([=](HistoryView::ChatPreviewAction action) {
		if (const auto controller = weakController.get()) {
			if (const auto thread = weakThread.get()) {
				const auto itemId = action.openItemId;
				const auto owner = &thread->owner();
				if (action.markRead) {
					MarkAsReadThread(thread);
				} else if (action.markUnread) {
					if (const auto history = thread->asHistory()) {
						history->owner().histories().changeDialogUnreadMark(
							history,
							true);
					}
				} else if (action.openInfo) {
					controller->showPeerInfo(thread);
				} else if (const auto item = owner->message(itemId)) {
					controller->showMessage(item);
				} else {
					controller->showThread(thread);
				}
			}
		}
		if (const auto strong = weakMenu.data()) {
			strong->hideMenu();
		}
	}, _menu->lifetime());
	QObject::connect(_menu.get(), &QObject::destroyed, [=] {
		_topicLifetime.destroy();
		if (callback) {
			callback(false);
		}
	});

	if (callback) {
		callback(true);
	}
	_menu->popup(positionOverride.value_or(QCursor::pos()));

	return true;
}

bool ChatPreviewManager::schedule(
		Dialogs::RowDescriptor row,
		Fn<void(bool shown)> callback,
		QPointer<QWidget> parentOverride,
		std::optional<QPoint> positionOverride) {
	cancelScheduled();
	_topicLifetime.destroy();
	if (const auto topic = row.key.topic()) {
		_topicLifetime = topic->destroyed() | rpl::start_with_next([=] {
			cancelScheduled();
			_menu = nullptr;
		});
	} else if (!row.key.history()) {
		return false;
	}
	_scheduled = std::move(row);
	_scheduledCallback = std::move(callback);
	_scheduledParentOverride = std::move(parentOverride);
	_scheduledPositionOverride = positionOverride;
	_timer.callOnce(kChatPreviewDelay);
	return true;
}

void ChatPreviewManager::showScheduled() {
	show(
		base::take(_scheduled),
		base::take(_scheduledCallback),
		nullptr,
		base::take(_scheduledPositionOverride));
}

void ChatPreviewManager::cancelScheduled() {
	_scheduled = {};
	_scheduledCallback = nullptr;
	_scheduledPositionOverride = {};
	_timer.cancel();
}

} // namespace Window