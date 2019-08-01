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

	session->data().itemRemoved(
	) | rpl::filter([](not_null<const HistoryItem*> item) {
		return item->isSingleEmoji();
	}) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		remove(item);
	}, _lifetime);
}

bool EmojiPack::add(not_null<HistoryItem*> item, const QString &text) {
	auto length = 0;
	const auto trimmed = text.trimmed();
	if (const auto emoji = Ui::Emoji::Find(trimmed, &length)) {
		if (length == trimmed.size()) {
			_items[emoji].emplace(item);
			return true;
		}
	}
	return false;
}

bool EmojiPack::remove(not_null<const HistoryItem*> item) {
	if (!item->isSingleEmoji()) {
		return false;
	}
	auto length = 0;
	const auto trimmed = item->originalString().trimmed();
	const auto emoji = Ui::Emoji::Find(trimmed, &length);
	Assert(emoji != nullptr);
	Assert(length == trimmed.size());
	const auto i = _items.find(emoji);
	Assert(i != end(_items));
	const auto j = i->second.find(item);
	Assert(j != end(i->second));
	i->second.erase(j);
	if (i->second.empty()) {
		_items.erase(i);
	}
	return true;
}

DocumentData *EmojiPack::stickerForEmoji(not_null<HistoryItem*> item) {
	if (!item->isSingleEmoji()) {
		return nullptr;
	}
	auto length = 0;
	const auto trimmed = item->originalString().trimmed();
	const auto emoji = Ui::Emoji::Find(trimmed, &length);
	Assert(emoji != nullptr);
	Assert(length == trimmed.size());
	const auto i = _map.find(emoji);
	return (i != end(_map)) ? i->second.get() : nullptr;
}

void EmojiPack::refresh() {
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetAnimatedEmoji()
	)).done([=](const MTPmessages_StickerSet &result) {
		_requestId = 0;
		refreshDelayed();
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data);
		});
	}).fail([=](const RPCError &error) {
		_requestId = 0;
		refreshDelayed();
	}).send();
}

void EmojiPack::applySet(const MTPDmessages_stickerSet &data) {
	const auto stickers = collectStickers(data.vdocuments().v);
	auto was = base::take(_map);

	for (const auto &pack : data.vpacks().v) {
		pack.match([&](const MTPDstickerPack &data) {
			applyPack(data, stickers);
		});
	}

	for (const auto &[emoji, document] : _map) {
		const auto i = was.find(emoji);
		if (i == end(was)) {
			refreshItems(emoji);
		} else {
			if (i->second != document) {
				refreshItems(i->first);
			}
			was.erase(i);
		}
	}
	for (const auto &[emoji, Document] : was) {
		refreshItems(emoji);
	}
}

void EmojiPack::refreshItems(EmojiPtr emoji) {
	const auto i = _items.find(emoji);
	if (i == end(_items)) {
		return;
	}
	for (const auto &item : i->second) {
		_session->data().requestItemViewRefresh(item);
	}
}

void EmojiPack::applyPack(
		const MTPDstickerPack &data,
		const base::flat_map<uint64, not_null<DocumentData*>> &map) {
	const auto emoji = [&] {
		return Ui::Emoji::Find(qs(data.vemoticon()));
	}();
	const auto document = [&]() -> DocumentData * {
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
}

base::flat_map<uint64, not_null<DocumentData*>> EmojiPack::collectStickers(
		const QVector<MTPDocument> &list) const {
	auto result = base::flat_map<uint64, not_null<DocumentData*>>();
	for (const auto &sticker : list) {
		const auto document = _session->data().processDocument(
			sticker);
		if (document->sticker()) {
			result.emplace(document->id, document);
		}
	}
	return result;
}

void EmojiPack::refreshDelayed() {
	App::CallDelayed(kRefreshTimeout, _session, [=] {
		refresh();
	});
}

} // namespace Stickers
