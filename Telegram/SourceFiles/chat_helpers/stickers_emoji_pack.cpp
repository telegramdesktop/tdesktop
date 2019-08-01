/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_emoji_pack.h"

#include "history/history_item.h"
#include "ui/emoji_config.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "apiwrap.h"

namespace Stickers {
namespace {

constexpr auto kRefreshTimeout = TimeId(7200);

} // namespace

EmojiPack::EmojiPack(not_null<Main::Session*> session) : _session(session) {
	refresh();
}

DocumentData *EmojiPack::stickerForEmoji(not_null<HistoryItem*> item) {
	const auto text = item->originalText().text.trimmed();
	auto length = 0;
	const auto emoji = Ui::Emoji::Find(text, &length);
	if (!emoji || length != text.size()) {
		return nullptr;
	}
	const auto i = _map.find(emoji);
	if (i != end(_map)) {
		return i->second;
	}
	return nullptr;
}

void EmojiPack::refresh() {
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetAnimatedEmoji()
	)).done([=](const MTPmessages_StickerSet &result) {
		refreshDelayed();
		result.match([&](const MTPDmessages_stickerSet &data) {
			auto map = base::flat_map<uint64, not_null<DocumentData*>>();
			for (const auto &sticker : data.vdocuments().v) {
				const auto document = _session->data().processDocument(
					sticker);
				if (document->sticker()) {
					map.emplace(document->id, document);
				}
			}
			for (const auto &pack : data.vpacks().v) {
				pack.match([&](const MTPDstickerPack &data) {
					const auto emoji = [&] {
						return Ui::Emoji::Find(qs(data.vemoticon()));
					}();
					const auto document = [&]() -> DocumentData* {
						for (const auto &id : data.vdocuments().v) {
							const auto i = map.find(id.v);
							if (i != end(map)) {
								return i->second.get();
							}
						}
						return nullptr;
					}();
					if (emoji && document) {
						_map.emplace_or_assign(emoji, document);
					}
				});
			}
		});
		int a = 0;
	}).fail([=](const RPCError &error) {
		refreshDelayed();
	}).send();
}

void EmojiPack::refreshDelayed() {
	App::CallDelayed(kRefreshTimeout, _session, [=] {
		refresh();
	});
}

} // namespace Stickers
