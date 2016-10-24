/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "stickers.h"

#include "boxes/stickers_box.h"
#include "boxes/confirmbox.h"
#include "lang.h"
#include "apiwrap.h"
#include "localstorage.h"
#include "mainwidget.h"

namespace Stickers {
namespace {

constexpr int kReadFeaturedSetsTimeoutMs = 1000;
internal::FeaturedReader *FeaturedReaderInstance = nullptr;

} // namespace

void applyArchivedResult(const MTPDmessages_stickerSetInstallResultArchive &d) {
	auto &v = d.vsets.c_vector().v;
	auto &order = Global::RefStickerSetsOrder();
	Stickers::Order archived;
	archived.reserve(v.size());
	QMap<uint64, uint64> setsToRequest;
	for_const (auto &stickerSet, v) {
		const MTPDstickerSet *setData = nullptr;
		switch (stickerSet.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = stickerSet.c_stickerSetCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = stickerSet.c_stickerSetMultiCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
		} break;
		}
		if (setData) {
			auto set = Stickers::feedSet(*setData);
			if (set->stickers.isEmpty()) {
				setsToRequest.insert(set->id, set->access);
			}
			auto index = order.indexOf(set->id);
			if (index >= 0) {
				order.removeAt(index);
			}
			archived.push_back(set->id);
		}
	}
	if (!setsToRequest.isEmpty()) {
		for (auto i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			App::api()->scheduleStickerSetRequest(i.key(), i.value());
		}
		App::api()->requestStickerSets();
	}
	Local::writeInstalledStickers();
	Local::writeArchivedStickers();
	Ui::showLayer(new StickersBox(archived), KeepOtherLayers);

	emit App::main()->stickersUpdated();
}

void installLocally(uint64 setId) {
	auto &sets = Global::RefStickerSets();
	auto it = sets.find(setId);
	if (it == sets.end()) {
		return;
	}

	auto flags = it->flags;
	it->flags &= ~(MTPDstickerSet::Flag::f_archived | MTPDstickerSet_ClientFlag::f_unread);
	it->flags |= MTPDstickerSet::Flag::f_installed;
	auto changedFlags = flags ^ it->flags;

	auto &order = Global::RefStickerSetsOrder();
	int insertAtIndex = 0, currentIndex = order.indexOf(setId);
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, setId);
	}

	auto custom = sets.find(Stickers::CustomSetId);
	if (custom != sets.cend()) {
		for_const (auto sticker, it->stickers) {
			int removeIndex = custom->stickers.indexOf(sticker);
			if (removeIndex >= 0) custom->stickers.removeAt(removeIndex);
		}
		if (custom->stickers.isEmpty()) {
			sets.erase(custom);
		}
	}
	Local::writeInstalledStickers();
	if (changedFlags & MTPDstickerSet_ClientFlag::f_unread) Local::writeFeaturedStickers();
	if (changedFlags & MTPDstickerSet::Flag::f_archived) {
		auto index = Global::RefArchivedStickerSetsOrder().indexOf(setId);
		if (index >= 0) {
			Global::RefArchivedStickerSetsOrder().removeAt(index);
			Local::writeArchivedStickers();
		}
	}
	emit App::main()->stickersUpdated();
}

void undoInstallLocally(uint64 setId) {
	auto &sets = Global::RefStickerSets();
	auto it = sets.find(setId);
	if (it == sets.end()) {
		return;
	}

	it->flags &= ~MTPDstickerSet::Flag::f_installed;

	auto &order = Global::RefStickerSetsOrder();
	int currentIndex = order.indexOf(setId);
	if (currentIndex >= 0) {
		order.removeAt(currentIndex);
	}

	Local::writeInstalledStickers();
	emit App::main()->stickersUpdated();

	Ui::showLayer(new InformBox(lang(lng_stickers_not_found)), KeepOtherLayers);
}

void markFeaturedAsRead(uint64 setId) {
	if (!FeaturedReaderInstance) {
		if (auto main = App::main()) {
			FeaturedReaderInstance = new internal::FeaturedReader(main);
		} else {
			return;
		}
	}
	FeaturedReaderInstance->scheduleRead(setId);
}

namespace internal {

void readFeaturedDone() {
	Local::writeFeaturedStickers();
	if (App::main()) {
		emit App::main()->stickersUpdated();
	}
}

FeaturedReader::FeaturedReader(QObject *parent) : QObject(parent)
, _timer(new QTimer(this)) {
	_timer->setSingleShot(true);
	connect(_timer, SIGNAL(timeout()), this, SLOT(onReadSets()));
}

void FeaturedReader::scheduleRead(uint64 setId) {
	if (!_setIds.contains(setId)) {
		_setIds.insert(setId);
		_timer->start(kReadFeaturedSetsTimeoutMs);
	}
}

void FeaturedReader::onReadSets() {
	auto &sets = Global::RefStickerSets();
	auto count = Global::FeaturedStickerSetsUnreadCount();
	QVector<MTPlong> wrappedIds;
	wrappedIds.reserve(_setIds.size());
	for_const (auto setId, _setIds) {
		auto it = sets.find(setId);
		if (it != sets.cend()) {
			it->flags &= ~MTPDstickerSet_ClientFlag::f_unread;
			wrappedIds.append(MTP_long(setId));
			if (count) {
				--count;
			}
		}
	}
	_setIds.clear();

	if (!wrappedIds.empty()) {
		MTP::send(MTPmessages_ReadFeaturedStickers(MTP_vector<MTPlong>(wrappedIds)), rpcDone(&readFeaturedDone));
		Global::SetFeaturedStickerSetsUnreadCount(count);
	}
}

} // namespace internal
} // namespace Stickers
