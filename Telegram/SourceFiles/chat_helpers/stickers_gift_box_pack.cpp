/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_gift_box_pack.h"

#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Stickers {

GiftBoxPack::GiftBoxPack(not_null<Main::Session*> session)
: _session(session)
, _localMonths({ 3, 6, 12 }) {
}

GiftBoxPack::~GiftBoxPack() = default;

DocumentData *GiftBoxPack::lookup(int months) const {
	const auto it = ranges::lower_bound(_localMonths, months);
	if (it == begin(_localMonths)) {
		return _documents.empty() ? nullptr : _documents[0];
	}
	const auto left = *(it - 1);
	const auto right = *it;
	const auto shift = (std::abs(months - left) < std::abs(months - right))
		? -1
		: 0;
	const auto index = int(std::distance(begin(_localMonths), it - shift));
	return (index >= _documents.size()) ? nullptr : _documents[index];
}

void GiftBoxPack::load() {
	if (_requestId || !_documents.empty()) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetPremiumGifts(),
		MTP_int(0) // Hash.
	)).done([=](const MTPmessages_StickerSet &result) {
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

void GiftBoxPack::applySet(const MTPDmessages_stickerSet &data) {
	_setId = data.vset().data().vid().v;
	auto documents = base::flat_map<DocumentId, not_null<DocumentData*>>();
	for (const auto &sticker : data.vdocuments().v) {
		const auto document = _session->data().processDocument(sticker);
		if (document->sticker()) {
			documents.emplace(document->id, document);
		}
	}
	for (const auto &pack : data.vpacks().v) {
		pack.match([&](const MTPDstickerPack &data) {
			const auto emoji = qs(data.vemoticon());
			if (emoji.isEmpty()) {
				return;
			}
			for (const auto &id : data.vdocuments().v) {
				if (const auto document = documents.take(id.v)) {
					_documents.push_back(*document);
				}
			}
		});
	}
}

bool GiftBoxPack::isGiftSticker(not_null<DocumentData*> document) const {
	return document->sticker()
		? (document->sticker()->set.id == _setId)
		: false;
}

} // namespace Stickers
