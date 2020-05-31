/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "chat_helpers/stickers_set.h"
#include "boxes/stickers_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "storage/localstorage.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "ui/toast/toast.h"
#include "ui/emoji_config.h"
#include "ui/image/image_location_factory.h"
#include "base/unixtime.h"
#include "lottie/lottie_single_player.h"
#include "lottie/lottie_multi_player.h"
#include "facades.h"
#include "app.h"
#include "styles/style_chat_helpers.h"

namespace Stickers {
namespace {

constexpr auto kDontCacheLottieAfterArea = 512 * 512;

} // namespace

void ApplyArchivedResult(const MTPDmessages_stickerSetInstallResultArchive &d) {
	auto &v = d.vsets().v;
	auto &order = Auth().data().stickerSetsOrderRef();
	Order archived;
	archived.reserve(v.size());
	QMap<uint64, uint64> setsToRequest;
	for (const auto &stickerSet : v) {
		const MTPDstickerSet *setData = nullptr;
		switch (stickerSet.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = stickerSet.c_stickerSetCovered();
			if (d.vset().type() == mtpc_stickerSet) {
				setData = &d.vset().c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = stickerSet.c_stickerSetMultiCovered();
			if (d.vset().type() == mtpc_stickerSet) {
				setData = &d.vset().c_stickerSet();
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

	Ui::Toast::Show(Ui::Toast::Config{
		.text = { tr::lng_stickers_packs_archived(tr::now) },
		.st = &st::stickersToast,
		.multiline = true,
	});
//	Ui::show(Box<StickersBox>(archived, &Auth()), Ui::LayerOption::KeepOther);

	Auth().data().notifyStickersUpdated();
}

// For testing: Just apply random subset or your sticker sets as archived.
bool ApplyArchivedResultFake() {
	auto sets = QVector<MTPStickerSetCovered>();
	for (const auto &[id, set] : Auth().data().stickerSets()) {
		const auto raw = set.get();
		if ((raw->flags & MTPDstickerSet::Flag::f_installed_date)
			&& !(raw->flags & MTPDstickerSet_ClientFlag::f_special)) {
			if (rand_value<uint32>() % 128 < 64) {
				const auto data = MTP_stickerSet(
					MTP_flags(raw->flags | MTPDstickerSet::Flag::f_archived),
					MTP_int(raw->installDate),
					MTP_long(raw->id),
					MTP_long(raw->access),
					MTP_string(raw->title),
					MTP_string(raw->shortName),
					MTP_photoSizeEmpty(MTP_string()),
					MTP_int(0),
					MTP_int(raw->count),
					MTP_int(raw->hash));
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

	const auto set = it->second.get();
	auto flags = set->flags;
	set->flags &= ~(MTPDstickerSet::Flag::f_archived | MTPDstickerSet_ClientFlag::f_unread);
	set->flags |= MTPDstickerSet::Flag::f_installed_date;
	set->installDate = base::unixtime::now();
	auto changedFlags = flags ^ set->flags;

	auto &order = Auth().data().stickerSetsOrderRef();
	int insertAtIndex = 0, currentIndex = order.indexOf(setId);
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, setId);
	}

	auto customIt = sets.find(CustomSetId);
	if (customIt != sets.cend()) {
		const auto custom = customIt->second.get();
		for (const auto sticker : std::as_const(set->stickers)) {
			int removeIndex = custom->stickers.indexOf(sticker);
			if (removeIndex >= 0) custom->stickers.removeAt(removeIndex);
		}
		if (custom->stickers.isEmpty()) {
			sets.erase(customIt);
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
	const auto &sets = Auth().data().stickerSets();
	const auto it = sets.find(setId);
	if (it == sets.end()) {
		return;
	}

	const auto set = it->second.get();
	set->flags &= ~MTPDstickerSet::Flag::f_installed_date;
	set->installDate = TimeId(0);

	auto &order = Auth().data().stickerSetsOrderRef();
	int currentIndex = order.indexOf(setId);
	if (currentIndex >= 0) {
		order.removeAt(currentIndex);
	}

	Local::writeInstalledStickers();
	Auth().data().notifyStickersUpdated();

	Ui::show(
		Box<InformBox>(tr::lng_stickers_not_found(tr::now)),
		Ui::LayerOption::KeepOther);
}

bool IsFaved(not_null<const DocumentData*> document) {
	const auto &sets = Auth().data().stickerSets();
	const auto it = sets.find(FavedSetId);
	if (it == sets.cend()) {
		return false;
	}
	for (const auto sticker : it->second->stickers) {
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

void SetIsFaved(
		not_null<DocumentData*> document,
		std::optional<std::vector<not_null<EmojiPtr>>> emojiList = std::nullopt) {
	auto &sets = document->owner().stickerSetsRef();
	auto it = sets.find(FavedSetId);
	if (it == sets.end()) {
		it = sets.emplace(FavedSetId, std::make_unique<Set>(
			&document->owner(),
			FavedSetId,
			uint64(0),
			Lang::Hard::FavedSetTitle(),
			QString(),
			0, // count
			0, // hash
			MTPDstickerSet_ClientFlag::f_special | 0,
			TimeId(0))).first;
	}
	const auto set = it->second.get();
	auto index = set->stickers.indexOf(document);
	if (index == 0) {
		return;
	}
	if (index > 0) {
		MoveFavedToFront(*set, index);
	} else if (emojiList) {
		PushFavedToFront(*set, document, *emojiList);
	} else if (auto list = GetEmojiListFromSet(document)) {
		PushFavedToFront(*set, document, *list);
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
		list.reserve(d.vpacks().v.size());
		for (const auto &mtpPack : d.vpacks().v) {
			auto &pack = mtpPack.c_stickerPack();
			for (const auto &documentId : pack.vdocuments().v) {
				if (documentId.v == document->id) {
					if (const auto emoji = Ui::Emoji::Find(qs(mtpPack.c_stickerPack().vemoticon()))) {
						list.emplace_back(emoji);
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
	const auto set = it->second.get();
	auto index = set->stickers.indexOf(document);
	if (index < 0) {
		return;
	}
	set->stickers.removeAt(index);
	for (auto i = set->emoji.begin(); i != set->emoji.end();) {
		auto index = i->indexOf(document);
		if (index >= 0) {
			i->removeAt(index);
			if (i->empty()) {
				i = set->emoji.erase(i);
				continue;
			}
		}
		++i;
	}
	if (set->stickers.empty()) {
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
	for (auto &[id, set] : sets) {
		if (!(set->flags & MTPDstickerSet::Flag::f_archived)) {
			// Mark for removing.
			set->flags &= ~MTPDstickerSet::Flag::f_installed_date;
			set->installDate = 0;
		}
	}
	for (const auto &setData : data) {
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
	for (auto it = sets.begin(); it != sets.end();) {
		const auto set = it->second.get();
		bool installed = (set->flags & MTPDstickerSet::Flag::f_installed_date);
		bool featured = (set->flags & MTPDstickerSet_ClientFlag::f_featured);
		bool special = (set->flags & MTPDstickerSet_ClientFlag::f_special);
		bool archived = (set->flags & MTPDstickerSet::Flag::f_archived);
		if (!installed) { // remove not mine sets from recent stickers
			for (auto i = recent.begin(); i != recent.cend();) {
				if (set->stickers.indexOf(i->first) >= 0) {
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
	for (const auto &mtpPack : packs) {
		Assert(mtpPack.type() == mtpc_stickerPack);
		auto &pack = mtpPack.c_stickerPack();
		if (auto emoji = Ui::Emoji::Find(qs(pack.vemoticon()))) {
			emoji = emoji->original();
			auto &stickers = pack.vdocuments().v;

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
			it = sets.emplace(setId, std::make_unique<Set>(
				&Auth().data(),
				setId,
				uint64(0),
				setTitle,
				QString(),
				0, // count
				0, // hash
				MTPDstickerSet_ClientFlag::f_special | 0,
				TimeId(0))).first;
		} else {
			it->second->title = setTitle;
		}
		const auto set = it->second.get();
		set->hash = hash;

		auto dates = std::vector<TimeId>();
		auto dateIndex = 0;
		auto datesAvailable = (items.size() == usageDates.size())
			&& (setId == CloudRecentSetId);

		auto customIt = sets.find(CustomSetId);
		auto pack = Pack();
		pack.reserve(items.size());
		for (const auto &item : items) {
			++dateIndex;
			const auto document = Auth().data().processDocument(item);
			if (!document->sticker()) {
				continue;
			}

			pack.push_back(document);
			if (datesAvailable) {
				dates.push_back(TimeId(usageDates[dateIndex - 1].v));
			}
			if (customIt != sets.cend()) {
				const auto custom = customIt->second.get();
				auto index = custom->stickers.indexOf(document);
				if (index >= 0) {
					custom->stickers.removeAt(index);
				}
			}
		}
		if (customIt != sets.cend()
			&& customIt->second->stickers.isEmpty()) {
			sets.erase(customIt);
			customIt = sets.end();
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
			sets.erase(it);
		} else {
			SetPackAndEmoji(*set, std::move(pack), std::move(dates), packs);
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

void FeaturedSetsReceived(
		const QVector<MTPStickerSetCovered> &list,
		const QVector<MTPlong> &unread,
		int32 hash) {
	auto &&unreadIds = ranges::view::all(
		unread
	) | ranges::view::transform([](const MTPlong &id) {
		return id.v;
	});
	const auto unreadMap = base::flat_set<uint64>{
		unreadIds.begin(),
		unreadIds.end()
	};

	auto &setsOrder = Auth().data().featuredStickerSetsOrderRef();
	setsOrder.clear();

	auto &sets = Auth().data().stickerSetsRef();
	auto setsToRequest = base::flat_map<uint64, uint64>();
	for (auto &[id, set] : sets) {
		// Mark for removing.
		set->flags &= ~MTPDstickerSet_ClientFlag::f_featured;
	}
	for (const auto &entry : list) {
		const auto data = entry.match([&](const auto &data) {
			return data.vset().match([&](const MTPDstickerSet &data) {
				return &data;
			});
		});
		auto it = sets.find(data->vid().v);
		const auto title = GetSetTitle(*data);
		const auto installDate = data->vinstalled_date().value_or_empty();
		const auto thumb = data->vthumb();
		const auto thumbnail = thumb
			? Images::FromPhotoSize(&Auth(), *data, *thumb)
			: ImageWithLocation();
		if (it == sets.cend()) {
			auto setClientFlags = MTPDstickerSet_ClientFlag::f_featured
				| MTPDstickerSet_ClientFlag::f_not_loaded;
			if (unreadMap.contains(data->vid().v)) {
				setClientFlags |= MTPDstickerSet_ClientFlag::f_unread;
			}
			it = sets.emplace(data->vid().v, std::make_unique<Set>(
				&Auth().data(),
				data->vid().v,
				data->vaccess_hash().v,
				title,
				qs(data->vshort_name()),
				data->vcount().v,
				data->vhash().v,
				data->vflags().v | setClientFlags,
				installDate)).first;
			it->second->setThumbnail(thumbnail);
		} else {
			const auto set = it->second.get();
			set->access = data->vaccess_hash().v;
			set->title = title;
			set->shortName = qs(data->vshort_name());
			auto clientFlags = set->flags & (MTPDstickerSet_ClientFlag::f_featured | MTPDstickerSet_ClientFlag::f_unread | MTPDstickerSet_ClientFlag::f_not_loaded | MTPDstickerSet_ClientFlag::f_special);
			set->flags = data->vflags().v | clientFlags;
			set->flags |= MTPDstickerSet_ClientFlag::f_featured;
			set->installDate = installDate;
			set->setThumbnail(thumbnail);
			if (unreadMap.contains(set->id)) {
				set->flags |= MTPDstickerSet_ClientFlag::f_unread;
			} else {
				set->flags &= ~MTPDstickerSet_ClientFlag::f_unread;
			}
			if (set->count != data->vcount().v || set->hash != data->vhash().v || set->emoji.isEmpty()) {
				set->count = data->vcount().v;
				set->hash = data->vhash().v;
				set->flags |= MTPDstickerSet_ClientFlag::f_not_loaded; // need to request this set
			}
		}
		setsOrder.push_back(data->vid().v);
		if (it->second->stickers.isEmpty()
			|| (it->second->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
			setsToRequest.emplace(data->vid().v, data->vaccess_hash().v);
		}
	}

	auto unreadCount = 0;
	for (auto it = sets.begin(); it != sets.end();) {
		const auto set = it->second.get();
		bool installed = (set->flags & MTPDstickerSet::Flag::f_installed_date);
		bool featured = (set->flags & MTPDstickerSet_ClientFlag::f_featured);
		bool special = (set->flags & MTPDstickerSet_ClientFlag::f_special);
		bool archived = (set->flags & MTPDstickerSet::Flag::f_archived);
		if (installed || featured || special || archived) {
			if (featured && (set->flags & MTPDstickerSet_ClientFlag::f_unread)) {
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

	if (!setsToRequest.empty()) {
		auto &api = Auth().api();
		for (const auto [setId, accessHash] : setsToRequest) {
			api.scheduleStickerSetRequest(setId, accessHash);
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
	for (const auto &item : items) {
		const auto document = Auth().data().processDocument(item);
		if (!document->isGifv()) {
			LOG(("API Error: "
				"bad document returned in HistoryWidget::savedGifsGot!"));
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
		not_null<Main::Session*> session,
		not_null<EmojiPtr> emoji,
		uint64 seed) {
	const auto original = emoji->original();

	struct StickerWithDate {
		not_null<DocumentData*> document;
		TimeId date = 0;
	};
	auto result = std::vector<StickerWithDate>();
	auto &sets = session->data().stickerSetsRef();
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
		if (document->sticker() && document->sticker()->animated) {
			base += kSlice;
		}
		return TimeId(base + int((document->id ^ seed) % kSlice));
	};
	const auto CreateRecentSortKey = [&](not_null<DocumentData*> document) {
		return CreateSortKey(document, kSlice * 6);
	};
	auto myCounter = 0;
	const auto CreateMySortKey = [&](not_null<DocumentData*> document) {
		auto base = kSlice * 6;
		if (!document->sticker() || !document->sticker()->animated) {
			base -= kSlice;
		}
		return (base - (++myCounter));
	};
	const auto CreateFeaturedSortKey = [&](not_null<DocumentData*> document) {
		return CreateSortKey(document, kSlice * 2);
	};
	const auto CreateOtherSortKey = [&](not_null<DocumentData*> document) {
		return CreateSortKey(document, 0);
	};
	const auto InstallDateAdjusted = [&](
			TimeId date,
			not_null<DocumentData*> document) {
		return (document->sticker() && document->sticker()->animated)
			? date
			: date / 2;
	};
	const auto InstallDate = [&](not_null<DocumentData*> document) {
		Expects(document->sticker() != nullptr);

		const auto sticker = document->sticker();
		if (sticker->set.type() == mtpc_inputStickerSetID) {
			const auto setId = sticker->set.c_inputStickerSetID().vid().v;
			const auto setIt = sets.find(setId);
			if (setIt != sets.end()) {
				return InstallDateAdjusted(setIt->second->installDate, document);
			}
		}
		return TimeId(0);
	};

	auto recentIt = sets.find(Stickers::CloudRecentSetId);
	if (recentIt != sets.cend()) {
		const auto recent = recentIt->second.get();
		auto i = recent->emoji.constFind(original);
		if (i != recent->emoji.cend()) {
			result.reserve(i->size());
			for (const auto document : *i) {
				const auto usageDate = [&] {
					if (recent->dates.empty()) {
						return TimeId(0);
					}
					const auto index = recent->stickers.indexOf(document);
					if (index < 0) {
						return TimeId(0);
					}
					Assert(index < recent->dates.size());
					return recent->dates[index];
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
			if (it == sets.cend() || (it->second->flags & skip)) {
				continue;
			}
			const auto set = it->second.get();
			if (set->emoji.isEmpty()) {
				setsToRequest.emplace(set->id, set->access);
				set->flags |= MTPDstickerSet_ClientFlag::f_not_loaded;
				continue;
			}
			auto i = set->emoji.constFind(original);
			if (i == set->emoji.cend()) {
				continue;
			}
			const auto my = (set->flags & MTPDstickerSet::Flag::f_installed_date);
			result.reserve(result.size() + i->size());
			for (const auto document : *i) {
				const auto installDate = my ? set->installDate : TimeId(0);
				const auto date = (installDate > 1)
					? InstallDateAdjusted(installDate, document)
					: my
					? CreateMySortKey(document)
					: CreateFeaturedSortKey(document);
				add(document, date);
			}
		}
	};

	addList(
		session->data().stickerSetsOrder(),
		MTPDstickerSet::Flag::f_archived);
	//addList(
	//	session->data().featuredStickerSetsOrder(),
	//	MTPDstickerSet::Flag::f_installed_date);

	if (!setsToRequest.empty()) {
		for (const auto &[setId, accessHash] : setsToRequest) {
			session->api().scheduleStickerSetRequest(setId, accessHash);
		}
		session->api().requestStickerSets();
	}

	if (session->settings().suggestStickersByEmoji()) {
		const auto others = session->api().stickersByEmoji(original);
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
		&StickerWithDate::date);

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
		const auto &sets = Auth().data().stickerSets();
		auto it = sets.find(inputSet.c_inputStickerSetID().vid().v);
		if (it == sets.cend()) {
			return std::nullopt;
		}
		const auto set = it->second.get();
		auto result = std::vector<not_null<EmojiPtr>>();
		for (auto i = set->emoji.cbegin(), e = set->emoji.cend(); i != e; ++i) {
			if (i->contains(document)) {
				result.emplace_back(i.key());
			}
		}
		if (result.empty()) {
			return std::nullopt;
		}
		return result;
	}
	return std::nullopt;
}

Set *FeedSet(const MTPDstickerSet &data) {
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(data.vid().v);
	auto title = GetSetTitle(data);
	auto flags = MTPDstickerSet::Flags(0);
	const auto thumb = data.vthumb();
	const auto thumbnail = thumb
		? Images::FromPhotoSize(&Auth(), data, *thumb)
		: ImageWithLocation();
	if (it == sets.cend()) {
		it = sets.emplace(data.vid().v, std::make_unique<Set>(
			&Auth().data(),
			data.vid().v,
			data.vaccess_hash().v,
			title,
			qs(data.vshort_name()),
			data.vcount().v,
			data.vhash().v,
			data.vflags().v | MTPDstickerSet_ClientFlag::f_not_loaded,
			data.vinstalled_date().value_or_empty())).first;
		it->second->setThumbnail(thumbnail);
	} else {
		const auto set = it->second.get();
		set->access = data.vaccess_hash().v;
		set->title = title;
		set->shortName = qs(data.vshort_name());
		flags = set->flags;
		auto clientFlags = set->flags
			& (MTPDstickerSet_ClientFlag::f_featured
				| MTPDstickerSet_ClientFlag::f_unread
				| MTPDstickerSet_ClientFlag::f_not_loaded
				| MTPDstickerSet_ClientFlag::f_special);
		set->flags = data.vflags().v | clientFlags;
		const auto installDate = data.vinstalled_date();
		set->installDate = installDate
			? (installDate->v ? installDate->v : base::unixtime::now())
			: TimeId(0);
		it->second->setThumbnail(thumbnail);
		if (set->count != data.vcount().v
			|| set->hash != data.vhash().v
			|| set->emoji.isEmpty()) {
			// Need to request this data.
			set->count = data.vcount().v;
			set->hash = data.vhash().v;
			set->flags |= MTPDstickerSet_ClientFlag::f_not_loaded;
		}
	}
	const auto set = it->second.get();
	auto changedFlags = (flags ^ set->flags);
	if (changedFlags & MTPDstickerSet::Flag::f_archived) {
		auto index = Auth().data().archivedStickerSetsOrder().indexOf(set->id);
		if (set->flags & MTPDstickerSet::Flag::f_archived) {
			if (index < 0) {
				Auth().data().archivedStickerSetsOrderRef().push_front(set->id);
			}
		} else if (index >= 0) {
			Auth().data().archivedStickerSetsOrderRef().removeAt(index);
		}
	}
	return it->second.get();
}

Set *FeedSetFull(const MTPmessages_StickerSet &data) {
	Expects(data.type() == mtpc_messages_stickerSet);
	Expects(data.c_messages_stickerSet().vset().type() == mtpc_stickerSet);

	const auto &d = data.c_messages_stickerSet();
	const auto &s = d.vset().c_stickerSet();

	auto &sets = Auth().data().stickerSetsRef();
	const auto wasArchived = [&] {
		auto it = sets.find(s.vid().v);
		return (it != sets.end())
			&& (it->second->flags & MTPDstickerSet::Flag::f_archived);
	}();

	auto set = FeedSet(s);

	set->flags &= ~MTPDstickerSet_ClientFlag::f_not_loaded;

	const auto &d_docs = d.vdocuments().v;
	auto customIt = sets.find(Stickers::CustomSetId);
	const auto inputSet = MTP_inputStickerSetID(
		MTP_long(set->id),
		MTP_long(set->access));

	auto pack = Pack();
	pack.reserve(d_docs.size());
	for (const auto &item : d_docs) {
		const auto document = Auth().data().processDocument(item);
		if (!document->sticker()) continue;

		pack.push_back(document);
		if (document->sticker()->set.type() != mtpc_inputStickerSetID) {
			document->sticker()->set = inputSet;
		}
		if (customIt != sets.cend()) {
			const auto custom = customIt->second.get();
			const auto index = custom->stickers.indexOf(document);
			if (index >= 0) {
				custom->stickers.removeAt(index);
			}
		}
	}
	if (customIt != sets.cend() && customIt->second->stickers.isEmpty()) {
		sets.erase(customIt);
		customIt = sets.end();
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
		auto &v = d.vpacks().v;
		for (auto i = 0, l = v.size(); i != l; ++i) {
			if (v[i].type() != mtpc_stickerPack) continue;

			auto &pack = v[i].c_stickerPack();
			if (auto emoji = Ui::Emoji::Find(qs(pack.vemoticon()))) {
				emoji = emoji->original();
				auto &stickers = pack.vdocuments().v;

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
		const auto isArchived = !!(set->flags & MTPDstickerSet::Flag::f_archived);
		if (set->flags & MTPDstickerSet::Flag::f_installed_date) {
			if (!isArchived) {
				Local::writeInstalledStickers();
			}
		}
		if (set->flags & MTPDstickerSet_ClientFlag::f_featured) {
			Local::writeFeaturedStickers();
		}
		if (wasArchived != isArchived) {
			Local::writeArchivedStickers();
		}
	}

	Auth().data().notifyStickersUpdated();

	return set;
}

void NewSetReceived(const MTPmessages_StickerSet &data) {
	bool writeArchived = false;
	const auto &set = data.c_messages_stickerSet();
	const auto &s = set.vset().c_stickerSet();
	if (!s.vinstalled_date()) {
		LOG(("API Error: "
			"updateNewStickerSet without install_date flag."));
		return;
	} else if (s.is_archived()) {
		LOG(("API Error: "
			"updateNewStickerSet with archived flag."));
		return;
	} else if (s.is_masks()) {
		return;
	}
	auto &order = Auth().data().stickerSetsOrderRef();
	int32 insertAtIndex = 0, currentIndex = order.indexOf(s.vid().v);
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, s.vid().v);
	}

	FeedSetFull(data);
}

QString GetSetTitle(const MTPDstickerSet &s) {
	auto title = qs(s.vtitle());
	if ((s.vflags().v & MTPDstickerSet::Flag::f_official) && !title.compare(qstr("Great Minds"), Qt::CaseInsensitive)) {
		return tr::lng_stickers_default_set(tr::now);
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

template <typename Method>
auto LottieCachedFromContent(
		Method &&method,
		Storage::Cache::Key baseKey,
		uint8 keyShift,
		not_null<Main::Session*> session,
		const QByteArray &content,
		QSize box) {
	const auto key = Storage::Cache::Key{
		baseKey.high,
		baseKey.low + keyShift
	};
	const auto get = [=](FnMut<void(QByteArray &&cached)> handler) {
		session->data().cacheBigFile().get(
			key,
			std::move(handler));
	};
	const auto weak = base::make_weak(session.get());
	const auto put = [=](QByteArray &&cached) {
		crl::on_main(weak, [=, data = std::move(cached)]() mutable {
			weak->data().cacheBigFile().put(key, std::move(data));
		});
	};
	return method(
		get,
		put,
		content,
		Lottie::FrameRequest{ box });
}

template <typename Method>
auto LottieFromDocument(
		Method &&method,
		not_null<Data::DocumentMedia*> media,
		uint8 keyShift,
		QSize box) {
	const auto document = media->owner();
	const auto data = media->bytes();
	const auto filepath = document->filepath();
	if (box.width() * box.height() > kDontCacheLottieAfterArea) {
		// Don't use frame caching for large stickers.
		return method(
			Lottie::ReadContent(data, filepath),
			Lottie::FrameRequest{ box });
	}
	if (const auto baseKey = document->bigFileBaseCacheKey()) {
		return LottieCachedFromContent(
			std::forward<Method>(method),
			baseKey,
			keyShift,
			&document->session(),
			Lottie::ReadContent(data, filepath),
			box);
	}
	return method(
		Lottie::ReadContent(data, filepath),
		Lottie::FrameRequest{ box });
}

std::unique_ptr<Lottie::SinglePlayer> LottiePlayerFromDocument(
		not_null<Data::DocumentMedia*> media,
		LottieSize sizeTag,
		QSize box,
		Lottie::Quality quality,
		std::shared_ptr<Lottie::FrameRenderer> renderer) {
	return LottiePlayerFromDocument(
		media,
		nullptr,
		sizeTag,
		box,
		quality,
		std::move(renderer));
}

std::unique_ptr<Lottie::SinglePlayer> LottiePlayerFromDocument(
		not_null<Data::DocumentMedia*> media,
		const Lottie::ColorReplacements *replacements,
		LottieSize sizeTag,
		QSize box,
		Lottie::Quality quality,
		std::shared_ptr<Lottie::FrameRenderer> renderer) {
	const auto method = [&](auto &&...args) {
		return std::make_unique<Lottie::SinglePlayer>(
			std::forward<decltype(args)>(args)...,
			quality,
			replacements,
			std::move(renderer));
	};
	const auto tag = replacements ? replacements->tag : uint8(0);
	const auto keyShift = ((tag << 4) & 0xF0) | (uint8(sizeTag) & 0x0F);
	return LottieFromDocument(method, media, uint8(keyShift), box);
}

not_null<Lottie::Animation*> LottieAnimationFromDocument(
		not_null<Lottie::MultiPlayer*> player,
		not_null<Data::DocumentMedia*> media,
		LottieSize sizeTag,
		QSize box) {
	const auto method = [&](auto &&...args) {
		return player->append(std::forward<decltype(args)>(args)...);
	};
	return LottieFromDocument(method, media, uint8(sizeTag), box);
}

bool HasLottieThumbnail(
		SetThumbnailView *thumb,
		Data::DocumentMedia *media) {
	if (thumb) {
		return !thumb->content().isEmpty();
	} else if (!media) {
		return false;
	}
	const auto document = media->owner();
	if (const auto info = document->sticker()) {
		if (!info->animated) {
			return false;
		}
		media->automaticLoad(document->stickerSetOrigin(), nullptr);
		if (!media->loaded()) {
			return false;
		}
		return document->bigFileBaseCacheKey().valid();
	}
	return false;
}

std::unique_ptr<Lottie::SinglePlayer> LottieThumbnail(
		SetThumbnailView *thumb,
		Data::DocumentMedia *media,
		LottieSize sizeTag,
		QSize box,
		std::shared_ptr<Lottie::FrameRenderer> renderer) {
	const auto baseKey = thumb
		? thumb->owner()->thumbnailLocation().file().bigFileBaseCacheKey()
		: media
		? media->owner()->bigFileBaseCacheKey()
		: Storage::Cache::Key();
	if (!baseKey) {
		return nullptr;
	}
	const auto content = thumb
		? thumb->content()
		: Lottie::ReadContent(media->bytes(), media->owner()->filepath());
	if (content.isEmpty()) {
		return nullptr;
	}
	const auto method = [](auto &&...args) {
		return std::make_unique<Lottie::SinglePlayer>(
			std::forward<decltype(args)>(args)...);
	};
	const auto session = thumb
		? &thumb->owner()->session()
		: media
		? &media->owner()->session()
		: nullptr;
	return LottieCachedFromContent(
		method,
		baseKey,
		uint8(sizeTag),
		session,
		content,
		box);
}

} // namespace Stickers
