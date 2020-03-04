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

DicePack::DicePack(not_null<Main::Session*> session)
: _session(session) {
}

DicePack::~DicePack() = default;

DocumentData *DicePack::lookup(int value) {
	if (!_requestId) {
		load();
	}
	if (!value) {
		ensureZeroGenerated();
		return _zero;
	}
	const auto i = _map.find(value);
	return (i != end(_map)) ? i->second.get() : nullptr;
}

void DicePack::load() {
	if (_requestId) {
		return;
	}
	_requestId = _session->api().request(MTPmessages_GetStickerSet(
		MTP_inputStickerSetDice()
	)).done([=](const MTPmessages_StickerSet &result) {
		result.match([&](const MTPDmessages_stickerSet &data) {
			applySet(data);
		});
	}).fail([=](const RPCError &error) {
		_requestId = 0;
	}).send();
}

void DicePack::applySet(const MTPDmessages_stickerSet &data) {
	auto index = 0;
	for (const auto &sticker : data.vdocuments().v) {
		const auto document = _session->data().processDocument(
			sticker);
		if (document->sticker()) {
			_map.emplace(++index, document);
		}
	}
}

void DicePack::ensureZeroGenerated() {
	if (_zero) {
		return;
	}

	const auto path = qsl(":/gui/art/dice_idle.tgs");
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

} // namespace Stickers
