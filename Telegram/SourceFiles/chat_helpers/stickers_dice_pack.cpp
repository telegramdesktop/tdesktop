/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_dice_pack.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "storage/localimageloader.h"
#include "base/unixtime.h"
#include "apiwrap.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Stickers {
namespace {

constexpr auto kZeroDiceDocumentId = 0xa3b83c9f84fa9e83ULL;

} // namespace

DicePack::DicePack(not_null<Main::Session*> session, const QString &emoji)
: _session(session)
, _emoji(emoji) {
}

DicePack::~DicePack() = default;

DocumentData *DicePack::lookup(int value) {
	if (!_requestId) {
		load();
	}
	const auto i = _map.find(value);
	//if (!value) {
	//	ensureZeroGenerated();
	//	return _zero;
	//}
	return (i != end(_map)) ? i->second.get() : nullptr;
}

void DicePack::load() {
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetDice(MTP_string(_emoji))
	)).done([=](const MTPmessages_StickerSet &result) {
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data);
		});
	}).fail([=](const RPCError &error) {
		_requestId = 0;
	}).send();
}

void DicePack::applySet(const MTPDmessages_stickerSet &data) {
	_map.clear();
	auto documents = base::flat_map<DocumentId, not_null<DocumentData*>>();
	for (const auto &sticker : data.vdocuments().v) {
		const auto document = _session->data().processDocument(
			sticker);
		if (document->sticker()) {
			documents.emplace(document->id, document);
		}
	}
	for (const auto pack : data.vpacks().v) {
		pack.match([&](const MTPDstickerPack &data) {
			const auto emoji = qs(data.vemoticon());
			if (emoji.isEmpty()) {
				return;
			}
			const auto ch = int(emoji[0].unicode());
			const auto index = (ch == '#') ? 0 : (ch + 1 - '1');
			if (index < 0 || index > 6) {
				return;
			}
			for (const auto id : data.vdocuments().v) {
				if (const auto document = documents.take(id.v)) {
					_map.emplace(index, *document);
				}
			}
		});
	}
}

void DicePack::ensureZeroGenerated() {
	if (_zero) {
		return;
	}

	static const auto kDiceString = QString::fromUtf8("\xF0\x9F\x8E\xB2");
	static const auto kDartString = QString::fromUtf8("\xF0\x9F\x8E\xAF");
	const auto path = QString((_emoji == kDiceString)
		? ":/gui/art/dice_idle.tgs"
		: ":/gui/art/dart_idle.tgs");
	auto task = FileLoadTask(
		path,
		QByteArray(),
		nullptr,
		SendMediaType::File,
		FileLoadTo(0, {}, 0),
		{});
	task.process();
	const auto result = task.peekResult();
	Assert(result != nullptr);
	_zero = _session->data().processDocument(
		result->document,
		std::move(result->thumb));
	_zero->setLocation(FileLocation(path));

	Ensures(_zero->sticker());
	Ensures(_zero->sticker()->animated);
}

DicePacks::DicePacks(not_null<Main::Session*> session) : _session(session) {
}

DocumentData *DicePacks::lookup(const QString &emoji, int value) {
	const auto i = _packs.find(emoji);
	if (i != end(_packs)) {
		return i->second->lookup(value);
	}
	return _packs.emplace(
		emoji,
		std::make_unique<DicePack>(_session, emoji)
	).first->second->lookup(value);
}

} // namespace Stickers
