/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_todo_lists.h"

#include "api/api_editing.h"
#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/random.h"
#include "data/business/data_shortcut_messages.h" // ShortcutIdToMTP
#include "data/data_changes.h"
#include "data/data_histories.h"
#include "data/data_todo_list.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kSendTogglesDelay = 3 * crl::time(1000);

} // namespace

TodoLists::TodoLists(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance())
, _sendTimer([=] { sendAccumulatedToggles(false); }) {
}

void TodoLists::create(
		const TodoListData &data,
		SendAction action,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	auto starsPaid = 0;
	const auto sendFlags = PrepareSendMedia(_session, action, starsPaid);
	const auto history = action.history;
	const auto peer = history->peer;
	const auto topicRootId = action.replyTo.messageId
		? action.replyTo.topicRootId
		: 0;
	const auto monoforumPeerId = action.replyTo.monoforumPeerId;
	const auto clearCloudDraft = action.clearDraft;
	const auto sendAs = action.options.sendAs;
	auto &histories = history->owner().histories();
	const auto randomId = base::RandomValue<uint64>();
	histories.sendPreparedMessage(
		history,
		action.replyTo,
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
			MTP_flags(sendFlags),
			peer->input(),
			Data::Histories::ReplyToPlaceholder(),
			TodoListDataToInputMedia(&data),
			MTP_string(),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(),
			MTP_int(action.options.scheduled),
			MTP_int(action.options.scheduleRepeatPeriod),
			(sendAs ? sendAs->input() : MTP_inputPeerEmpty()),
			Data::ShortcutIdToMTP(_session, action.options.shortcutId),
			MTP_long(action.options.effectId),
			MTP_long(starsPaid),
			SuggestToMTP(action.options.suggest)
		), [=](const MTPUpdates &result, const MTP::Response &response) {
		if (clearCloudDraft) {
			history->finishSavingCloudDraft(
				topicRootId,
				monoforumPeerId,
				UnixtimeFromMsgId(response.outerMsgId));
		}
		_session->changes().historyUpdated(
			history,
			(action.options.scheduled
				? Data::HistoryUpdate::Flag::ScheduledSent
				: Data::HistoryUpdate::Flag::MessageSent));
		if (const auto onstack = done) {
			onstack();
		}
	}, [=](const MTP::Error &error, const MTP::Response &response) {
		if (clearCloudDraft) {
			history->finishSavingCloudDraft(
				topicRootId,
				monoforumPeerId,
				UnixtimeFromMsgId(response.outerMsgId));
		}
		if (const auto onstack = fail) {
			onstack(error.type());
		}
	});
}

void TodoLists::edit(
		not_null<HistoryItem*> item,
		const TodoListData &data,
		SendOptions options,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	EditTodoList(item, data, options, [=](mtpRequestId) {
		if (const auto onstack = done) {
			onstack();
		}
	}, [=](const QString &error, mtpRequestId) {
		if (const auto onstack = fail) {
			onstack(error);
		}
	});
}

void TodoLists::add(
		not_null<HistoryItem*> item,
		const std::vector<TodoListItem> &items,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	if (items.empty()) {
		return;
	}
	const auto session = _session;
	_session->api().request(MTPmessages_AppendTodoList(
		item->history()->peer->input(),
		MTP_int(item->id.bare),
		TodoListItemsToMTP(&item->history()->session(), items)
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		if (const auto onstack = done) {
			onstack();
		}
	}).fail([=](const MTP::Error &error) {
		if (const auto onstack = fail) {
			onstack(error.type());
		}
	}).send();
}

void TodoLists::toggleCompletion(FullMsgId itemId, int id, bool completed) {
	auto &entry = _toggles[itemId];
	if (completed) {
		const auto changed1 = entry.completed.emplace(id).second;
		const auto changed2 = entry.incompleted.remove(id);
		if (!changed1 && !changed2) {
			return;
		}
	} else {
		const auto changed1 = entry.incompleted.emplace(id).second;
		const auto changed2 = entry.completed.remove(id);
		if (!changed1 && !changed2) {
			return;
		}
	}
	entry.scheduled = crl::now();
	if (!entry.requestId && !_sendTimer.isActive()) {
		_sendTimer.callOnce(kSendTogglesDelay);
	}
}

void TodoLists::sendAccumulatedToggles(bool force) {
	const auto now = crl::now();
	auto nearest = crl::time(0);
	for (auto &[itemId, entry] : _toggles) {
		if (entry.requestId) {
			continue;
		}
		const auto wait = entry.scheduled + kSendTogglesDelay - now;
		if (wait <= 0) {
			entry.scheduled = 0;
			send(itemId, entry);
		} else if (!nearest || nearest > wait) {
			nearest = wait;
		}
	}
	if (nearest > 0) {
		_sendTimer.callOnce(nearest);
	}
}

void TodoLists::send(FullMsgId itemId, Accumulated &entry) {
	const auto item = _session->data().message(itemId);
	if (!item) {
		return;
	}
	auto completed = entry.completed
		| ranges::views::transform([](int id) { return MTP_int(id); });
	auto incompleted = entry.incompleted
		| ranges::views::transform([](int id) { return MTP_int(id); });
	entry.requestId = _api.request(MTPmessages_ToggleTodoCompleted(
		item->history()->peer->input(),
		MTP_int(item->id),
		MTP_vector_from_range(completed),
		MTP_vector_from_range(incompleted)
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
		finishRequest(itemId);
	}).fail([=](const MTP::Error &error) {
		finishRequest(itemId);
	}).send();
	entry.completed.clear();
	entry.incompleted.clear();
}

void TodoLists::finishRequest(FullMsgId itemId) {
	auto &entry = _toggles[itemId];
	entry.requestId = 0;
	if (entry.completed.empty() && entry.incompleted.empty()) {
		_toggles.remove(itemId);
	} else {
		sendAccumulatedToggles(false);
	}
}

} // namespace Api
