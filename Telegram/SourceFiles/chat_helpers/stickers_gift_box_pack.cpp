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
: _session(session) {
	_premium.dividers = { 1, 3, 6, 12, 24 };
	_ton.dividers = { 0, 10, 50 };
}

GiftBoxPack::~GiftBoxPack() = default;

rpl::producer<> GiftBoxPack::updated() const {
	return _premium.updated.events();
}

rpl::producer<> GiftBoxPack::tonUpdated() const {
	return _ton.updated.events();
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
	return lookup(_premium, months, false);
}

DocumentData *GiftBoxPack::tonLookup(int amount) const {
	return lookup(_ton, amount, true);
}

DocumentData *GiftBoxPack::lookup(
		const Pack &pack,
		int divider,
		bool exact) const {
	const auto it = ranges::lower_bound(pack.dividers, divider);
	const auto fallback = pack.documents.empty()
		? nullptr
		: pack.documents.front();
	if (it == begin(pack.dividers)) {
		return fallback;
	} else if (it == end(pack.dividers)) {
		return pack.documents.back();
	}
	const auto shift = exact
		? ((*it > divider) ? 1 : 0)
		: (std::abs(divider - (*(it - 1))) < std::abs(divider - (*it)))
		? -1
		: 0;
	const auto index = int(std::distance(begin(pack.dividers), it - shift));
	return (index >= pack.documents.size())
		? fallback
		: pack.documents[index];
}

Data::FileOrigin GiftBoxPack::origin() const {
	return Data::FileOriginStickerSet(_premium.id, _premium.accessHash);
}

Data::FileOrigin GiftBoxPack::tonOrigin() const {
	return Data::FileOriginStickerSet(_ton.id, _ton.accessHash);
}

void GiftBoxPack::load() {
	load(_premium, MTP_inputStickerSetPremiumGifts());
}

void GiftBoxPack::tonLoad() {
	load(_ton, MTP_inputStickerSetTonGifts());
}

void GiftBoxPack::load(Pack &pack, const MTPInputStickerSet &set) {
	if (pack.requestId || !pack.documents.empty()) {
		return;
	}
	pack.requestId = _session->api().request(MTPmessages_GetStickerSet(
		set,
		MTP_int(0) // Hash.
	)).done([=, &pack](const MTPmessages_StickerSet &result) {
		pack.requestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(pack, data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=, &pack] {
		pack.requestId = 0;
	}).send();
}

void GiftBoxPack::applySet(Pack &pack, const MTPDmessages_stickerSet &data) {
	pack.id = data.vset().data().vid().v;
	pack.accessHash = data.vset().data().vaccess_hash().v;
	auto documents = base::flat_map<DocumentId, not_null<DocumentData*>>();
	for (const auto &sticker : data.vdocuments().v) {
		const auto document = _session->data().processDocument(sticker);
		if (document->sticker()) {
			documents.emplace(document->id, document);
			if (pack.documents.empty()) {
				// Fallback.
				pack.documents.resize(1);
				pack.documents[0] = document;
			}
		}
	}
	for (const auto &info : data.vpacks().v) {
		const auto &data = info.data();
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
						if (index < 0 || index >= pack.dividers.size()) {
							return;
						}
						if ((index + 1) > pack.documents.size()) {
							pack.documents.resize((index + 1));
						}
						pack.documents[index] = (*document);
					}
				}
			}
		}
	}
	pack.updated.fire({});
}

} // namespace Stickers
