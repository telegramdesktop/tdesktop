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

DocumentData *GiftBoxPack::lookup(int months, Type type) const {
	const auto it = _setsData.find(type);
	if (it == _setsData.end() || it->second.documents.empty()) {
		return nullptr;
	}

	const auto& documents = it->second.documents;
	const auto itMonths = ranges::lower_bound(_localMonths, months);
	const auto fallback = documents[0];

	if (itMonths == begin(_localMonths)) {
		return fallback;
	} else if (itMonths == end(_localMonths)) {
		return documents.back();
	}

	const auto left = *(itMonths - 1);
	const auto right = *itMonths;
	const auto shift = (std::abs(months - left) < std::abs(months - right))
		? -1
		: 0;
	const auto index = int(
		std::distance(begin(_localMonths), itMonths - shift));
	return (index >= documents.size()) ? fallback : documents[index];
}

Data::FileOrigin GiftBoxPack::origin(Type type) const {
	const auto it = _setsData.find(type);
	if (it == _setsData.end()) {
		return Data::FileOrigin();
	}
	return Data::FileOriginStickerSet(
		it->second.setId,
		it->second.accessHash);
}

void GiftBoxPack::load(Type type) {
	if (_requestId) {
		return;
	}

	const auto it = _setsData.find(type);
	if (it != _setsData.end() && !it->second.documents.empty()) {
		return;
	}

	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		type == Type::Currency
			? MTP_inputStickerSetTonGifts()
			: MTP_inputStickerSetPremiumGifts(),
		MTP_int(0) // Hash.
	)).done([=](const MTPmessages_StickerSet &result) {
		_requestId = 0;
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data, type);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

void GiftBoxPack::applySet(const MTPDmessages_stickerSet &data, Type type) {
	auto setData = SetData();
	setData.setId = data.vset().data().vid().v;
	setData.accessHash = data.vset().data().vaccess_hash().v;

	auto documents = base::flat_map<DocumentId, not_null<DocumentData*>>();
	for (const auto &sticker : data.vdocuments().v) {
		const auto document = _session->data().processDocument(sticker);
		if (document->sticker()) {
			documents.emplace(document->id, document);
			if (setData.documents.empty()) {
				// Fallback.
				setData.documents.resize(1);
				setData.documents[0] = document;
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
							if ((index + 1) > setData.documents.size()) {
								setData.documents.resize((index + 1));
							}
							setData.documents[index] = (*document);
						}
					}
				}
			}
		});
	}

	_setsData[type] = std::move(setData);
	_updated.fire({});
}

} // namespace Stickers
