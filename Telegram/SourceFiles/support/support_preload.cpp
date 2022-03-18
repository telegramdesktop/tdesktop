/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "support/support_preload.h"

#include "history/history.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Support {
namespace {

constexpr auto kPreloadMessagesCount = 50;

} // namespace

int SendPreloadRequest(not_null<History*> history, Fn<void()> retry) {
	auto offsetId = MsgId();
	auto offset = 0;
	auto loadCount = kPreloadMessagesCount;
	if (const auto around = history->loadAroundId()) {
		history->getReadyFor(ShowAtUnreadMsgId);
		offset = -loadCount / 2;
		offsetId = around;
	}
	const auto offsetDate = 0;
	const auto maxId = 0;
	const auto minId = 0;
	const auto historyHash = uint64(0);
	const auto type = Data::Histories::RequestType::History;
	auto &histories = history->owner().histories();
	return histories.sendRequest(history, type, [=](Fn<void()> finish) {
		return history->session().api().request(MTPmessages_GetHistory(
			history->peer->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(offset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_long(historyHash)
		)).done([=](const MTPmessages_Messages &result) {
			if (const auto around = history->loadAroundId()) {
				if (around != offsetId) {
					retry();
					return;
				}
				history->clear(History::ClearType::Unload);
				history->getReadyFor(ShowAtUnreadMsgId);
			} else if (offsetId) {
				retry();
				return;
			} else {
				history->clear(History::ClearType::Unload);
				history->getReadyFor(ShowAtTheEndMsgId);
			}
			result.match([](const MTPDmessages_messagesNotModified&) {
			}, [&](const auto &data) {
				history->owner().processUsers(data.vusers());
				history->owner().processChats(data.vchats());
				history->addOlderSlice(data.vmessages().v);
			});
			finish();
		}).fail([=](const MTP::Error &error) {
			finish();
		}).send();
	});
}

} // namespace Support
