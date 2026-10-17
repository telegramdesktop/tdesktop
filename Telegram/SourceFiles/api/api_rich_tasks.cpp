/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_rich_tasks.h"

#include "api/api_editing.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "iv/editor/iv_editor_state.h"
#include "iv/iv_rich_message_serializer.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kSendDelay = crl::time(1000);

} // namespace

RichTasks::RichTasks(not_null<ApiWrap*> api)
: _session(&api->session())
, _sendTimer([=] { sendAccumulated(); }) {
}

bool RichTasks::togglingAllowed(not_null<HistoryItem*> item) const {
	const auto page = item->richPage();
	return page
		&& !page->part
		&& item->allowsEdit(base::unixtime::now());
}

void RichTasks::toggle(
		not_null<HistoryItem*> item,
		const Iv::Markdown::PreparedEditListItemSource &source) {
	if (!togglingAllowed(item)) {
		return;
	}
	const auto was = item->richPage();
	auto page = std::make_shared<Iv::RichPage>(*was);
	auto state = Iv::Editor::State(page, nullptr);
	if (!state.toggleTaskState(source)) {
		return;
	}
	const auto itemId = item->fullId();
	auto &entry = _entries[itemId];
	if (!entry.original) {
		entry.original = was;
	}
	entry.dirty = true;
	entry.scheduled = crl::now();
	item->applyLocalRichPage(std::move(page));
	if (!entry.requestId && !_sendTimer.isActive()) {
		_sendTimer.callOnce(kSendDelay);
	}
}

void RichTasks::sendAccumulated() {
	const auto now = crl::now();
	auto nearest = crl::time(0);
	for (auto &[itemId, entry] : _entries) {
		if (entry.requestId || !entry.dirty) {
			continue;
		}
		const auto wait = entry.scheduled + kSendDelay - now;
		if (wait <= 0) {
			send(itemId, entry);
		} else if (!nearest || nearest > wait) {
			nearest = wait;
		}
	}
	if (nearest > 0) {
		_sendTimer.callOnce(nearest);
	}
}

void RichTasks::send(FullMsgId itemId, Accumulated &entry) {
	const auto item = _session->data().message(itemId);
	if (!item) {
		_entries.remove(itemId);
		return;
	}
	entry.dirty = false;
	const auto session = _session;
	entry.requestId = EditRichMessage(item, [=] {
		const auto current = session->data().message(itemId);
		const auto page = current ? current->richPage() : nullptr;
		if (!page) {
			return std::optional<MTPInputRichMessage>();
		}
		auto serialized = Iv::SerializeInputRichMessage(
			session,
			*page,
			Iv::SerializeInputRichMessageMode::FinalSubmit);
		return (serialized.status
			== Iv::SerializeInputRichMessageStatus::Success)
			? serialized.value
			: std::optional<MTPInputRichMessage>();
	}, SendOptions(), [=](mtpRequestId) {
		finishRequest(itemId, false);
	}, [=](const QString &error, mtpRequestId) {
		finishRequest(itemId, true);
	});
}

void RichTasks::finishRequest(FullMsgId itemId, bool failed) {
	const auto i = _entries.find(itemId);
	if (i == end(_entries)) {
		return;
	}
	i->second.requestId = 0;
	if (failed) {
		const auto original = i->second.original;
		_entries.erase(i);
		if (const auto item = _session->data().message(itemId)) {
			if (original) {
				item->applyLocalRichPage(original);
			}
		}
		return;
	} else if (!i->second.dirty) {
		_entries.erase(i);
		return;
	}
	i->second.scheduled = crl::now();
	sendAccumulated();
}

} // namespace Api
