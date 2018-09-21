/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stickers.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "boxes/stickers_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "storage/localstorage.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "mainwindow.h"
#include "ui/toast/toast.h"
#include "styles/style_chat_helpers.h"

namespace Stickers {

void ApplyArchivedResult(const MTPDmessages_stickerSetInstallResultArchive &d) {
	auto &v = d.vsets.v;
	auto &order = Auth().data().stickerSetsOrderRef();
	Order archived;
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
			auto set = FeedSet(*setData);
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
			Auth().api().scheduleStickerSetRequest(i.key(), i.value());
		}
		Auth().api().requestStickerSets();
	}
	Local::writeInstalledStickers();
	Local::writeArchivedStickers();

	Ui::Toast::Config toast;
	toast.text = lang(lng_stickers_packs_archived);
	toast.maxWidth = st::stickersToastMaxWidth;
	toast.padding = st::stickersToastPadding;
	Ui::Toast::Show(toast);
//	Ui::show(Box<StickersBox>(archived), LayerOption::KeepOther);

	Auth().data().notifyStickersUpdated();
}

// For testing: Just apply random subset or your sticker sets as archived.
bool ApplyArchivedResultFake() {
	auto sets = QVector<MTPStickerSetCovered>();
	for (auto &set : Auth().data().stickerSetsRef()) {
		if ((set.flags & MTPDstickerSet::Flag::f_installed_date)
			&& !(set.flags & MTPDstickerSet_ClientFlag::f_special)) {
			if (rand_value<uint32>() % 128 < 64) {
				auto data = MTP_stickerSet(
					MTP_flags(set.flags | MTPDstickerSet::Flag::f_archived),
					MTP_int(set.installDate),
					MTP_long(set.id),
					MTP_long(set.access),
					MTP_string(set.title),
					MTP_string(set.shortName),
					MTP_int(set.count),
					MTP_int(set.hash));
				sets.push_back(MTP_stickerSetCovered(
					data,
					MTP_documentEmpty(MTP_long(0))));
			}
		}
	}
	if (sets.size() > 3) {
		sets = sets.mid(0, 3);
	}
	auto fakeResult = MTP_messages_stickerSetInstallResultArchive(
		MTP_vector<MTPStickerSetCovered>(sets));
	ApplyArchivedResult(fakeResult.c_messages_stickerSetInstallResultArchive());
	return true;
}

void InstallLocally(uint64 setId) {
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(setId);
	if (it == sets.end()) {
		return;
	}

	auto flags = it->flags;
	it->flags &= ~(MTPDstickerSet::Flag::f_archived | MTPDstickerSet_ClientFlag::f_unread);
	it->flags |= MTPDstickerSet::Flag::f_installed_date;
	it->installDate = unixtime();
	auto changedFlags = flags ^ it->flags;

	auto &order = Auth().data().stickerSetsOrderRef();
	int insertAtIndex = 0, currentIndex = order.indexOf(setId);
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, setId);
	}

	auto custom = sets.find(CustomSetId);
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
	if (changedFlags & MTPDstickerSet_ClientFlag::f_unread) {
		Local::writeFeaturedStickers();
	}
	if (changedFlags & MTPDstickerSet::Flag::f_archived) {
		auto index = Auth().data().archivedStickerSetsOrderRef().indexOf(setId);
		if (index >= 0) {
			Auth().data().archivedStickerSetsOrderRef().removeAt(index);
			Local::writeArchivedStickers();
		}
	}
	Auth().data().notifyStickersUpdated();
}

void UndoInstallLocally(uint64 setId) {
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(setId);
	if (it == sets.end()) {
		return;
	}

	it->flags &= ~MTPDstickerSet::Flag::f_installed_date;
	it->installDate = TimeId(0);

	auto &order = Auth().data().stickerSetsOrderRef();
	int currentIndex = order.indexOf(setId);
	if (currentIndex >= 0) {
		order.removeAt(currentIndex);
	}

	Local::writeInstalledStickers();
	Auth().data().notifyStickersUpdated();

	Ui::show(
		Box<InformBox>(lang(lng_stickers_not_found)),
		LayerOption::KeepOther);
}

bool IsFaved(not_null<const DocumentData*> document) {
	const auto it = Auth().data().stickerSets().constFind(FavedSetId);
	if (it == Auth().data().stickerSets().cend()) {
		return false;
	}
	for (const auto sticker : it->stickers) {
		if (sticker == document) {
			return true;
		}
	}
	return false;
}

void CheckFavedLimit(Set &set) {
	if (set.stickers.size() <= Global::StickersFavedLimit()) {
		return;
	}
	auto removing = set.stickers.back();
	set.stickers.pop_back();
	for (auto i = set.emoji.begin(); i != set.emoji.end();) {
		auto index = i->indexOf(removing);
		if (index >= 0) {
			i->removeAt(index);
			if (i->empty()) {
				i = set.emoji.erase(i);
				continue;
			}
		}
		++i;
	}
}

void PushFavedToFront(
		Set &set,
		not_null<DocumentData*> document,
		const std::vector<not_null<EmojiPtr>> &emojiList) {
	set.stickers.push_front(document);
	for (auto emoji : emojiList) {
		set.emoji[emoji].push_front(document);
	}
	CheckFavedLimit(set);
}

void MoveFavedToFront(Set &set, int index) {
	Expects(index > 0 && index < set.stickers.size());
	auto document = set.stickers[index];
	while (index-- != 0) {
		set.stickers[index + 1] = set.stickers[index];
	}
	set.stickers[0] = document;
	for (auto &list : set.emoji) {
		auto index = list.indexOf(document);
		if (index > 0) {
			while (index-- != 0) {
				list[index + 1] = list[index];
			}
			list[0] = document;
		}
	}
}

void RequestSetToPushFaved(not_null<DocumentData*> document);

void SetIsFaved(not_null<DocumentData*> document, std::optional<std::vector<not_null<EmojiPtr>>> emojiList = std::nullopt) {
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(FavedSetId);
	if (it == sets.end()) {
		it = sets.insert(FavedSetId, Set(
			FavedSetId,
			uint64(0),
			Lang::Hard::FavedSetTitle(),
			QString(),
			0, // count
			0, // hash
			MTPDstickerSet_ClientFlag::f_special | 0,
			TimeId(0)));
	}
	auto index = it->stickers.indexOf(document);
	if (index == 0) {
		return;
	}
	if (index > 0) {
		MoveFavedToFront(*it, index);
	} else if (emojiList) {
		PushFavedToFront(*it, document, *emojiList);
	} else if (auto list = GetEmojiListFromSet(document)) {
		PushFavedToFront(*it, document, *list);
	} else {
		RequestSetToPushFaved(document);
		return;
	}
	Local::writeFavedStickers();
	Auth().data().notifyStickersUpdated();
	Auth().api().stickerSetInstalled(FavedSetId);
}

void RequestSetToPushFaved(not_null<DocumentData*> document) {
	auto addAnyway = [document](std::vector<not_null<EmojiPtr>> list) {
		if (list.empty()) {
			if (auto sticker = document->sticker()) {
				if (auto emoji = Ui::Emoji::Find(sticker->alt)) {
					list.push_back(emoji);
				}
			}
		}
		SetIsFaved(document, std::move(list));
	};
	MTP::send(MTPmessages_GetStickerSet(document->sticker()->set), rpcDone([document, addAnyway](const MTPmessages_StickerSet &result) {
		Expects(result.type() == mtpc_messages_stickerSet);
		auto list = std::vector<not_null<EmojiPtr>>();
		auto &d = result.c_messages_stickerSet();
		list.reserve(d.vpacks.v.size());
		for_const (auto &mtpPack, d.vpacks.v) {
			auto &pack = mtpPack.c_stickerPack();
			for_const (auto &documentId, pack.vdocuments.v) {
				if (documentId.v == document->id) {
					if (auto emoji = Ui::Emoji::Find(qs(mtpPack.c_stickerPack().vemoticon))) {
						list.push_back(emoji);
					}
					break;
				}
			}
		}
		addAnyway(std::move(list));
	}), rpcFail([addAnyway](const RPCError &error) {
		if (MTP::isDefaultHandledError(error)) {
			return false;
		}
		// Perhaps this is a deleted sticker pack. Add anyway.
		addAnyway({});
		return true;
	}));
}

void SetIsNotFaved(not_null<DocumentData*> document) {
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(FavedSetId);
	if (it == sets.end()) {
		return;
	}
	auto index = it->stickers.indexOf(document);
	if (index < 0) {
		return;
	}
	it->stickers.removeAt(index);
	for (auto i = it->emoji.begin(); i != it->emoji.end();) {
		auto index = i->indexOf(document);
		if (index >= 0) {
			i->removeAt(index);
			if (i->empty()) {
				i = it->emoji.erase(i);
				continue;
			}
		}
		++i;
	}
	if (it->stickers.empty()) {
		sets.erase(it);
	}
	Local::writeFavedStickers();
	Auth().data().notifyStickersUpdated();
}

void SetFaved(not_null<DocumentData*> document, bool faved) {
	if (faved) {
		SetIsFaved(document);
	} else {
		SetIsNotFaved(document);
	}
}

void SetsReceived(const QVector<MTPStickerSet> &data, int32 hash) {
	auto &setsOrder = Auth().data().stickerSetsOrderRef();
	setsOrder.clear();

	auto &sets = Auth().data().stickerSetsRef();
	QMap<uint64, uint64> setsToRequest;
	for (auto &set : sets) {
		if (!(set.flags & MTPDstickerSet::Flag::f_archived)) {
			// Mark for removing.
			set.flags &= ~MTPDstickerSet::Flag::f_installed_date;
			set.installDate = 0;
		}
	}
	for_const (auto &setData, data) {
		if (setData.type() == mtpc_stickerSet) {
			auto set = FeedSet(setData.c_stickerSet());
			if (!(set->flags & MTPDstickerSet::Flag::f_archived) || (set->flags & MTPDstickerSet::Flag::f_official)) {
				setsOrder.push_back(set->id);
				if (set->stickers.isEmpty() || (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
					setsToRequest.insert(set->id, set->access);
				}
			}
		}
	}
	auto writeRecent = false;
	auto &recent = GetRecentPack();
	for (auto it = sets.begin(), e = sets.end(); it != e;) {
		bool installed = (it->flags & MTPDstickerSet::Flag::f_installed_date);
		bool featured = (it->flags & MTPDstickerSet_ClientFlag::f_featured);
		bool special = (it->flags & MTPDstickerSet_ClientFlag::f_special);
		bool archived = (it->flags & MTPDstickerSet::Flag::f_archived);
		if (!installed) { // remove not mine sets from recent stickers
			for (auto i = recent.begin(); i != recent.cend();) {
				if (it->stickers.indexOf(i->first) >= 0) {
					i = recent.erase(i);
					writeRecent = true;
				} else {
					++i;
				}
			}
		}
		if (installed || featured || special || archived) {
			++it;
		} else {
			it = sets.erase(it);
		}
	}

	if (!setsToRequest.isEmpty()) {
		auto &api = Auth().api();
		for (auto i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			api.scheduleStickerSetRequest(i.key(), i.value());
		}
		api.requestStickerSets();
	}

	Local::writeInstalledStickers();
	if (writeRecent) Local::writeUserSettings();

	if (Local::countStickersHash() != hash) {
		LOG(("API Error: received stickers hash %1 while counted hash is %2").arg(hash).arg(Local::countStickersHash()));
	}

	Auth().data().notifyStickersUpdated();
}

void SetPackAndEmoji(
		Set &set,
		Pack &&pack,
		const std::vector<TimeId> &&dates,
		const QVector<MTPStickerPack> &packs) {
	set.stickers = std::move(pack);
	set.dates = std::move(dates);
	set.emoji.clear();
	for_const (auto &mtpPack, packs) {
		Assert(mtpPack.type() == mtpc_stickerPack);
		auto &pack = mtpPack.c_stickerPack();
		if (auto emoji = Ui::Emoji::Find(qs(pack.vemoticon))) {
			emoji = emoji->original();
			auto &stickers = pack.vdocuments.v;

			auto p = Pack();
			p.reserve(stickers.size());
			for (auto j = 0, c = stickers.size(); j != c; ++j) {
				auto document = Auth().data().document(stickers[j].v);
				if (!document || !document->sticker()) continue;

				p.push_back(document);
			}
			set.emoji.insert(emoji, p);
		}
	}
}

void SpecialSetReceived(
		uint64 setId,
		const QString &setTitle,
		const QVector<MTPDocument> &items,
		int32 hash,
		const QVector<MTPStickerPack> &packs,
		const QVector<MTPint> &usageDates) {
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(setId);

	if (items.isEmpty()) {
		if (it != sets.cend()) {
			sets.erase(it);
		}
	} else {
		if (it == sets.cend()) {
			it = sets.insert(setId, Set(
				setId,
				uint64(0),
				setTitle,
				QString(),
				0, // count
				0, // hash
				MTPDstickerSet_ClientFlag::f_special | 0,
				TimeId(0)));
		} else {
			it->title = setTitle;
		}
		it->hash = hash;

		auto dates = std::vector<TimeId>();
		auto dateIndex = 0;
		auto datesAvailable = (items.size() == usageDates.size())
			&& (setId == CloudRecentSetId);

		auto custom = sets.find(CustomSetId);
		auto pack = Pack();
		pack.reserve(items.size());
		for_const (auto &mtpDocument, items) {
			++dateIndex;
			auto document = Auth().data().document(mtpDocument);
			if (!document->sticker()) {
				continue;
			}

			pack.push_back(document);
			if (datesAvailable) {
				dates.push_back(TimeId(usageDates[dateIndex - 1].v));
			}
			if (custom != sets.cend()) {
				auto index = custom->stickers.indexOf(document);
				if (index >= 0) {
					custom->stickers.removeAt(index);
				}
			}
		}
		if (custom != sets.cend() && custom->stickers.isEmpty()) {
			sets.erase(custom);
			custom = sets.end();
		}

		auto writeRecent = false;
		auto &recent = GetRecentPack();
		for (auto i = recent.begin(); i != recent.cend();) {
			if (it->stickers.indexOf(i->first) >= 0 && pack.indexOf(i->first) < 0) {
				i = recent.erase(i);
				writeRecent = true;
			} else {
				++i;
			}
		}

		if (pack.isEmpty()) {
			sets.erase(it);
		} else {
			SetPackAndEmoji(*it, std::move(pack), std::move(dates), packs);
		}

		if (writeRecent) {
			Local::writeUserSettings();
		}
	}

	switch (setId) {
	case CloudRecentSetId: {
		if (Local::countRecentStickersHash() != hash) {
			LOG(("API Error: received recent stickers hash %1 while counted hash is %2").arg(hash).arg(Local::countRecentStickersHash()));
		}
		Local::writeRecentStickers();
	} break;
	case FavedSetId: {
		if (Local::countFavedStickersHash() != hash) {
			LOG(("API Error: received faved stickers hash %1 while counted hash is %2").arg(hash).arg(Local::countFavedStickersHash()));
		}
		Local::writeFavedStickers();
	} break;
	default: Unexpected("setId in SpecialSetReceived()");
	}

	Auth().data().notifyStickersUpdated();
}

void FeaturedSetsReceived(const QVector<MTPStickerSetCovered> &data, const QVector<MTPlong> &unread, int32 hash) {
	OrderedSet<uint64> unreadMap;
	for_const (auto &unreadSetId, unread) {
		unreadMap.insert(unreadSetId.v);
	}

	auto &setsOrder = Auth().data().featuredStickerSetsOrderRef();
	setsOrder.clear();

	auto &sets = Auth().data().stickerSetsRef();
	QMap<uint64, uint64> setsToRequest;
	for (auto &set : sets) {
		set.flags &= ~MTPDstickerSet_ClientFlag::f_featured; // mark for removing
	}
	for (int i = 0, l = data.size(); i != l; ++i) {
		auto &setData = data[i];
		const MTPDstickerSet *set = nullptr;
		switch (setData.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = setData.c_stickerSetCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				set = &d.vset.c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = setData.c_stickerSetMultiCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				set = &d.vset.c_stickerSet();
			}
		} break;
		}

		if (set) {
			auto it = sets.find(set->vid.v);
			const auto title = GetSetTitle(*set);
			const auto installDate = set->has_installed_date()
				? set->vinstalled_date.v
				: TimeId(0);
			if (it == sets.cend()) {
				auto setClientFlags = MTPDstickerSet_ClientFlag::f_featured
					| MTPDstickerSet_ClientFlag::f_not_loaded;
				if (unreadMap.contains(set->vid.v)) {
					setClientFlags |= MTPDstickerSet_ClientFlag::f_unread;
				}
				it = sets.insert(set->vid.v, Set(
					set->vid.v,
					set->vaccess_hash.v,
					title,
					qs(set->vshort_name),
					set->vcount.v,
					set->vhash.v,
					set->vflags.v | setClientFlags,
					installDate));
			} else {
				it->access = set->vaccess_hash.v;
				it->title = title;
				it->shortName = qs(set->vshort_name);
				auto clientFlags = it->flags & (MTPDstickerSet_ClientFlag::f_featured | MTPDstickerSet_ClientFlag::f_unread | MTPDstickerSet_ClientFlag::f_not_loaded | MTPDstickerSet_ClientFlag::f_special);
				it->flags = set->vflags.v | clientFlags;
				it->flags |= MTPDstickerSet_ClientFlag::f_featured;
				it->installDate = installDate;
				if (unreadMap.contains(it->id)) {
					it->flags |= MTPDstickerSet_ClientFlag::f_unread;
				} else {
					it->flags &= ~MTPDstickerSet_ClientFlag::f_unread;
				}
				if (it->count != set->vcount.v || it->hash != set->vhash.v || it->emoji.isEmpty()) {
					it->count = set->vcount.v;
					it->hash = set->vhash.v;
					it->flags |= MTPDstickerSet_ClientFlag::f_not_loaded; // need to request this set
				}
			}
			setsOrder.push_back(set->vid.v);
			if (it->stickers.isEmpty() || (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
				setsToRequest.insert(set->vid.v, set->vaccess_hash.v);
			}
		}
	}

	auto unreadCount = 0;
	for (auto it = sets.begin(), e = sets.end(); it != e;) {
		bool installed = (it->flags & MTPDstickerSet::Flag::f_installed_date);
		bool featured = (it->flags & MTPDstickerSet_ClientFlag::f_featured);
		bool special = (it->flags & MTPDstickerSet_ClientFlag::f_special);
		bool archived = (it->flags & MTPDstickerSet::Flag::f_archived);
		if (installed || featured || special || archived) {
			if (featured && (it->flags & MTPDstickerSet_ClientFlag::f_unread)) {
				++unreadCount;
			}
			++it;
		} else {
			it = sets.erase(it);
		}
	}
	Auth().data().setFeaturedStickerSetsUnreadCount(unreadCount);

	if (Local::countFeaturedStickersHash() != hash) {
		LOG(("API Error: received featured stickers hash %1 while counted hash is %2").arg(hash).arg(Local::countFeaturedStickersHash()));
	}

	if (!setsToRequest.isEmpty()) {
		auto &api = Auth().api();
		for (auto i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			api.scheduleStickerSetRequest(i.key(), i.value());
		}
		api.requestStickerSets();
	}

	Local::writeFeaturedStickers();

	Auth().data().notifyStickersUpdated();
}

void GifsReceived(const QVector<MTPDocument> &items, int32 hash) {
	auto &saved = Auth().data().savedGifsRef();
	saved.clear();

	saved.reserve(items.size());
	for_const (auto &gif, items) {
		auto document = Auth().data().document(gif);
		if (!document->isGifv()) {
			LOG(("API Error: bad document returned in HistoryWidget::savedGifsGot!"));
			continue;
		}

		saved.push_back(document);
	}
	if (Local::countSavedGifsHash() != hash) {
		LOG(("API Error: received saved gifs hash %1 while counted hash is %2").arg(hash).arg(Local::countSavedGifsHash()));
	}

	Local::writeSavedGifs();

	Auth().data().notifySavedGifsUpdated();
}

std::vector<not_null<DocumentData*>> GetListByEmoji(
		not_null<EmojiPtr> emoji,
		uint64 seed) {
	const auto original = emoji->original();

	struct StickerWithDate {
		not_null<DocumentData*> document;
		TimeId date = 0;
	};
	auto result = std::vector<StickerWithDate>();
	auto &sets = Auth().data().stickerSetsRef();
	auto setsToRequest = base::flat_map<uint64, uint64>();

	const auto add = [&](not_null<DocumentData*> document, TimeId date) {
		if (ranges::find(result, document, [](const StickerWithDate &data) {
			return data.document;
		}) == result.end()) {
			result.push_back({ document, date });
		}
	};

	constexpr auto kSlice = 65536;
	const auto CreateSortKey = [&](
			not_null<DocumentData*> document,
			int base) {
		return TimeId(base + int((document->id ^ seed) % kSlice));
	};
	const auto CreateRecentSortKey = [&](not_null<DocumentData*> document) {
		return CreateSortKey(document, kSlice * 4);
	};
	auto myCounter = 0;
	const auto CreateMySortKey = [&] {
		return (kSlice * 4 - (++myCounter));
	};
	const auto CreateFeaturedSortKey = [&](not_null<DocumentData*> document) {
		return CreateSortKey(document, kSlice * 2);
	};
	const auto CreateOtherSortKey = [&](not_null<DocumentData*> document) {
		return CreateSortKey(document, kSlice);
	};
	const auto InstallDate = [&](not_null<DocumentData*> document) {
		Expects(document->sticker() != nullptr);

		const auto sticker = document->sticker();
		if (sticker->set.type() == mtpc_inputStickerSetID) {
			const auto setId = sticker->set.c_inputStickerSetID().vid.v;
			const auto setIt = sets.find(setId);
			if (setIt != sets.end()) {
				return setIt->installDate;
			}
		}
		return TimeId(0);
	};

	auto recentIt = sets.find(Stickers::CloudRecentSetId);
	if (recentIt != sets.cend()) {
		auto i = recentIt->emoji.constFind(original);
		if (i != recentIt->emoji.cend()) {
			result.reserve(i->size());
			for (const auto document : *i) {
				const auto usageDate = [&] {
					if (recentIt->dates.empty()) {
						return TimeId(0);
					}
					const auto index = recentIt->stickers.indexOf(document);
					if (index < 0) {
						return TimeId(0);
					}
					Assert(index < recentIt->dates.size());
					return recentIt->dates[index];
				}();
				const auto date = usageDate
					? usageDate
					: InstallDate(document);
				result.push_back({
					document,
					date ? date : CreateRecentSortKey(document) });
			}
		}
	}
	const auto addList = [&](const Order &order, MTPDstickerSet::Flag skip) {
		for (const auto setId : order) {
			auto it = sets.find(setId);
			if (it == sets.cend() || (it->flags & skip)) {
				continue;
			}
			if (it->emoji.isEmpty()) {
				setsToRequest.emplace(it->id, it->access);
				it->flags |= MTPDstickerSet_ClientFlag::f_not_loaded;
				continue;
			}
			auto i = it->emoji.constFind(original);
			if (i == it->emoji.cend()) {
				continue;
			}
			const auto my = (it->flags & MTPDstickerSet::Flag::f_installed_date);
			result.reserve(result.size() + i->size());
			for (const auto document : *i) {
				const auto installDate = my ? it->installDate : TimeId(0);
				const auto date = (installDate > 1)
					? installDate
					: my
					? CreateMySortKey()
					: CreateFeaturedSortKey(document);
				add(document, date);
			}
		}
	};

	addList(
		Auth().data().stickerSetsOrder(),
		MTPDstickerSet::Flag::f_archived);
	//addList(
	//	Auth().data().featuredStickerSetsOrder(),
	//	MTPDstickerSet::Flag::f_installed_date);

	if (!setsToRequest.empty()) {
		for (const auto [setId, accessHash] : setsToRequest) {
			Auth().api().scheduleStickerSetRequest(setId, accessHash);
		}
		Auth().api().requestStickerSets();
	}

	if (Global::SuggestStickersByEmoji()) {
		const auto others = Auth().api().stickersByEmoji(original);
		if (!others) {
			return {};
		}
		result.reserve(result.size() + others->size());
		for (const auto document : *others) {
			add(document, CreateOtherSortKey(document));
		}
	}

	ranges::action::sort(
		result,
		std::greater<>(),
		[](const StickerWithDate &data) { return data.date; });

	return ranges::view::all(
		result
	) | ranges::view::transform([](const StickerWithDate &data) {
		return data.document;
	}) | ranges::to_vector;
}

std::optional<std::vector<not_null<EmojiPtr>>> GetEmojiListFromSet(
		not_null<DocumentData*> document) {
	if (auto sticker = document->sticker()) {
		auto &inputSet = sticker->set;
		if (inputSet.type() != mtpc_inputStickerSetID) {
			return std::nullopt;
		}
		auto &sets = Auth().data().stickerSets();
		auto it = sets.constFind(inputSet.c_inputStickerSetID().vid.v);
		if (it == sets.cend()) {
			return std::nullopt;
		}
		auto result = std::vector<not_null<EmojiPtr>>();
		for (auto i = it->emoji.cbegin(), e = it->emoji.cend(); i != e; ++i) {
			if (i->contains(document)) {
				result.push_back(i.key());
			}
		}
		if (result.empty()) {
			return std::nullopt;
		}
		return std::move(result);
	}
	return std::nullopt;
}

Set *FeedSet(const MTPDstickerSet &set) {
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(set.vid.v);
	auto title = GetSetTitle(set);
	auto flags = MTPDstickerSet::Flags(0);
	if (it == sets.cend()) {
		it = sets.insert(set.vid.v, Stickers::Set(
			set.vid.v,
			set.vaccess_hash.v,
			title,
			qs(set.vshort_name),
			set.vcount.v,
			set.vhash.v,
			set.vflags.v | MTPDstickerSet_ClientFlag::f_not_loaded,
			set.has_installed_date() ? set.vinstalled_date.v : TimeId(0)));
	} else {
		it->access = set.vaccess_hash.v;
		it->title = title;
		it->shortName = qs(set.vshort_name);
		flags = it->flags;
		auto clientFlags = it->flags
			& (MTPDstickerSet_ClientFlag::f_featured
				| MTPDstickerSet_ClientFlag::f_unread
				| MTPDstickerSet_ClientFlag::f_not_loaded
				| MTPDstickerSet_ClientFlag::f_special);
		it->flags = set.vflags.v | clientFlags;
		it->installDate = set.has_installed_date()
			? set.vinstalled_date.v
			: TimeId(0);
		if (it->count != set.vcount.v
			|| it->hash != set.vhash.v
			|| it->emoji.isEmpty()) {
			it->count = set.vcount.v;
			it->hash = set.vhash.v;
			it->flags |= MTPDstickerSet_ClientFlag::f_not_loaded; // need to request this set
		}
	}
	auto changedFlags = (flags ^ it->flags);
	if (changedFlags & MTPDstickerSet::Flag::f_archived) {
		auto index = Auth().data().archivedStickerSetsOrder().indexOf(it->id);
		if (it->flags & MTPDstickerSet::Flag::f_archived) {
			if (index < 0) {
				Auth().data().archivedStickerSetsOrderRef().push_front(it->id);
			}
		} else if (index >= 0) {
			Auth().data().archivedStickerSetsOrderRef().removeAt(index);
		}
	}
	return &it.value();
}

Set *FeedSetFull(const MTPmessages_StickerSet &data) {
	Expects(data.type() == mtpc_messages_stickerSet);
	Expects(data.c_messages_stickerSet().vset.type() == mtpc_stickerSet);
	auto &d = data.c_messages_stickerSet();
	auto set = FeedSet(d.vset.c_stickerSet());

	set->flags &= ~MTPDstickerSet_ClientFlag::f_not_loaded;

	auto &sets = Auth().data().stickerSetsRef();
	auto &d_docs = d.vdocuments.v;
	auto custom = sets.find(Stickers::CustomSetId);

	auto pack = Pack();
	pack.reserve(d_docs.size());
	for (auto i = 0, l = d_docs.size(); i != l; ++i) {
		auto doc = Auth().data().document(d_docs.at(i));
		if (!doc->sticker()) continue;

		pack.push_back(doc);
		if (custom != sets.cend()) {
			auto index = custom->stickers.indexOf(doc);
			if (index >= 0) {
				custom->stickers.removeAt(index);
			}
		}
	}
	if (custom != sets.cend() && custom->stickers.isEmpty()) {
		sets.erase(custom);
		custom = sets.end();
	}

	auto writeRecent = false;
	auto &recent = GetRecentPack();
	for (auto i = recent.begin(); i != recent.cend();) {
		if (set->stickers.indexOf(i->first) >= 0 && pack.indexOf(i->first) < 0) {
			i = recent.erase(i);
			writeRecent = true;
		} else {
			++i;
		}
	}

	if (pack.isEmpty()) {
		int removeIndex = Auth().data().stickerSetsOrder().indexOf(set->id);
		if (removeIndex >= 0) Auth().data().stickerSetsOrderRef().removeAt(removeIndex);
		sets.remove(set->id);
		set = nullptr;
	} else {
		set->stickers = pack;
		set->emoji.clear();
		auto &v = d.vpacks.v;
		for (auto i = 0, l = v.size(); i != l; ++i) {
			if (v[i].type() != mtpc_stickerPack) continue;

			auto &pack = v[i].c_stickerPack();
			if (auto emoji = Ui::Emoji::Find(qs(pack.vemoticon))) {
				emoji = emoji->original();
				auto &stickers = pack.vdocuments.v;

				Pack p;
				p.reserve(stickers.size());
				for (auto j = 0, c = stickers.size(); j != c; ++j) {
					auto doc = Auth().data().document(stickers[j].v);
					if (!doc || !doc->sticker()) continue;

					p.push_back(doc);
				}
				set->emoji.insert(emoji, p);
			}
		}
	}

	if (writeRecent) {
		Local::writeUserSettings();
	}

	if (set) {
		if (set->flags & MTPDstickerSet::Flag::f_installed_date) {
			if (!(set->flags & MTPDstickerSet::Flag::f_archived)) {
				Local::writeInstalledStickers();
			}
		}
		if (set->flags & MTPDstickerSet_ClientFlag::f_featured) {
			Local::writeFeaturedStickers();
		}
	}

	Auth().data().notifyStickersUpdated();

	return set;
}

QString GetSetTitle(const MTPDstickerSet &s) {
	auto title = qs(s.vtitle);
	if ((s.vflags.v & MTPDstickerSet::Flag::f_official) && !title.compare(qstr("Great Minds"), Qt::CaseInsensitive)) {
		return lang(lng_stickers_default_set);
	}
	return title;
}

RecentStickerPack &GetRecentPack() {
	if (cRecentStickers().isEmpty() && !cRecentStickersPreload().isEmpty()) {
		const auto p = cRecentStickersPreload();
		cSetRecentStickersPreload(RecentStickerPreload());

		auto &recent = cRefRecentStickers();
		recent.reserve(p.size());
		for (const auto &preloaded : p) {
			const auto document = Auth().data().document(preloaded.first);
			if (!document || !document->sticker()) continue;

			recent.push_back(qMakePair(document, preloaded.second));
		}
	}
	return cRefRecentStickers();
}

} // namespace Stickers
