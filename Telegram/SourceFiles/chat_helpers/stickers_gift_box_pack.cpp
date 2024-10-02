/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_gift_box_pack.h"

#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Stickers {

GiftBoxPack::GiftBoxPack(not_null<Main::Session*> session)
: _session(session)
, _localMonths({ 1, 3, 6, 12, 24 }) {
}

GiftBoxPack::~GiftBoxPack() = default;

rpl::producer<> GiftBoxPack::updated() const {
	return _updated.events();
}

int GiftBoxPack::monthsForStars(int stars) const {
	if (stars <= 1000) {
		return 3;
	} else if (stars < 2500) {
		return 6;
	} else {
		return 12;
	}
}

DocumentData *GiftBoxPack::lookup(int months) const {
	const auto it = ranges::lower_bound(_localMonths, months);
	const auto fallback = _documents.empty() ? nullptr : _documents[0];
	if (it == begin(_localMonths)) {
		return fallback;
	} else if (it == end(_localMonths)) {
		return _documents.back();
	}
	const auto left = *(it - 1);
	const auto right = *it;
	const auto shift = (std::abs(months - left) < std::abs(months - right))
		? -1
		: 0;
	const auto index = int(std::distance(begin(_localMonths), it - shift));
	return (index >= _documents.size()) ? fallback : _documents[index];
}

Data::FileOrigin GiftBoxPack::origin() const {
	return Data::FileOriginStickerSet(_setId, _accessHash);
}

void GiftBoxPack::load() {
	if (_requestId || !_documents.empty()) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetPremiumGifts(),
		MTP_int(0) // Hash.
	)).done([=](const MTPmessages_StickerSet &result) {
		_requestId = 0;
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
	_accessHash = data.vset().data().vaccess_hash().v;
	auto documents = base::flat_map<DocumentId, not_null<DocumentData*>>();
	for (const auto &sticker : data.vdocuments().v) {
		const auto document = _session->data().processDocument(sticker);
		if (document->sticker()) {
			documents.emplace(document->id, document);
			if (_documents.empty()) {
				// Fallback.
				_documents.resize(1);
				_documents[0] = document;
			}
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
					if (const auto sticker = (*document)->sticker()) {
						if (!sticker->alt.isEmpty()) {
							const auto ch = int(sticker->alt[0].unicode());
							const auto index = (ch - '1'); // [0, 4];
							if (index < 0 || index >= _localMonths.size()) {
								return;
							}
							if ((index + 1) > _documents.size()) {
								_documents.resize((index + 1));
							}
							_documents[index] = (*document);
						}
					}
				}
			}
		});
	}
	_updated.fire({});
}

} // namespace Stickers
