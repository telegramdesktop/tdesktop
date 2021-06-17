/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/stickers/data_stickers.h"

#include "api/api_hash.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "apiwrap.h"
#include "storage/storage_account.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "ui/toast/toast.h"
#include "ui/image/image_location_factory.h"
#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "styles/style_chat_helpers.h"

namespace Data {

namespace {

void RemoveFromSet(
		StickersSets &sets,
		not_null<DocumentData*> document,
		uint64 setId) {
	const auto it = sets.find(setId);
	if (it == sets.end()) {
		return;
	}
	const auto set = it->second.get();
	const auto index = set->stickers.indexOf(document);
	if (index < 0) {
		return;
	}
	set->stickers.removeAt(index);
	if (!set->dates.empty()) {
		set->dates.erase(set->dates.begin() + index);
	}
	for (auto i = set->emoji.begin(); i != set->emoji.end();) {
		const auto index = i->indexOf(document);
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
}

} // namespace

Stickers::Stickers(not_null<Session*> owner) : _owner(owner) {
}

Session &Stickers::owner() const {
	return *_owner;
}

Main::Session &Stickers::session() const {
	return _owner->session();
}

void Stickers::notifyUpdated() {
	_updated.fire({});
}

rpl::producer<> Stickers::updated() const {
	return _updated.events();
}

void Stickers::notifyRecentUpdated() {
	_recentUpdated.fire({});
}

rpl::producer<> Stickers::recentUpdated() const {
	return _recentUpdated.events();
}

void Stickers::notifySavedGifsUpdated() {
	_savedGifsUpdated.fire({});
}

rpl::producer<> Stickers::savedGifsUpdated() const {
	return _savedGifsUpdated.events();
}

void Stickers::incrementSticker(not_null<DocumentData*> document) {
	if (!document->sticker()
		|| document->sticker()->set.type() == mtpc_inputStickerSetEmpty) {
		return;
	}

	bool writeRecentStickers = false;
	auto &sets = setsRef();
	auto it = sets.find(Data::Stickers::CloudRecentSetId);
	if (it == sets.cend()) {
		if (it == sets.cend()) {
			it = sets.emplace(
				Data::Stickers::CloudRecentSetId,
				std::make_unique<Data::StickersSet>(
					&session().data(),
					Data::Stickers::CloudRecentSetId,
					uint64(0),
					tr::lng_recent_stickers(tr::now),
					QString(),
					0, // count
					0, // hash
					MTPDstickerSet_ClientFlag::f_special | 0,
					TimeId(0))).first;
		} else {
			it->second->title = tr::lng_recent_stickers(tr::now);
		}
	}
	const auto set = it->second.get();
	auto removedFromEmoji = std::vector<not_null<EmojiPtr>>();
	auto index = set->stickers.indexOf(document);
	if (index > 0) {
		if (set->dates.empty()) {
			session().api().requestRecentStickersForce();
		} else {
			Assert(set->dates.size() == set->stickers.size());
			set->dates.erase(set->dates.begin() + index);
		}
		set->stickers.removeAt(index);
		for (auto i = set->emoji.begin(); i != set->emoji.end();) {
			if (const auto index = i->indexOf(document); index >= 0) {
				removedFromEmoji.emplace_back(i.key());
				i->removeAt(index);
				if (i->isEmpty()) {
					i = set->emoji.erase(i);
					continue;
				}
			}
			++i;
		}
	}
	if (index) {
		if (set->dates.size() == set->stickers.size()) {
			set->dates.insert(set->dates.begin(), base::unixtime::now());
		}
		set->stickers.push_front(document);
		if (const auto emojiList = getEmojiListFromSet(document)) {
			for (const auto emoji : *emojiList) {
				set->emoji[emoji].push_front(document);
			}
		} else if (!removedFromEmoji.empty()) {
			for (const auto emoji : removedFromEmoji) {
				set->emoji[emoji].push_front(document);
			}
		} else {
			session().api().requestRecentStickersForce();
		}

		writeRecentStickers = true;
	}

	// Remove that sticker from old recent, now it is in cloud recent stickers.
	bool writeOldRecent = false;
	auto &recent = getRecentPack();
	for (auto i = recent.begin(), e = recent.end(); i != e; ++i) {
		if (i->first == document) {
			writeOldRecent = true;
			recent.erase(i);
			break;
		}
	}
	while (!recent.isEmpty()
		&& (set->stickers.size() + recent.size()
			> session().serverConfig().stickersRecentLimit)) {
		writeOldRecent = true;
		recent.pop_back();
	}

	if (writeOldRecent) {
		session().saveSettings();
	}

	// Remove that sticker from custom stickers, now it is in cloud recent stickers.
	bool writeInstalledStickers = false;
	auto customIt = sets.find(Data::Stickers::CustomSetId);
	if (customIt != sets.cend()) {
		const auto custom = customIt->second.get();
		int removeIndex = custom->stickers.indexOf(document);
		if (removeIndex >= 0) {
			custom->stickers.removeAt(removeIndex);
			if (custom->stickers.isEmpty()) {
				sets.erase(customIt);
			}
			writeInstalledStickers = true;
		}
	}

	if (writeInstalledStickers) {
		session().local().writeInstalledStickers();
	}
	if (writeRecentStickers) {
		session().local().writeRecentStickers();
	}
	notifyRecentUpdated();
}

void Stickers::addSavedGif(not_null<DocumentData*> document) {
	const auto index = _savedGifs.indexOf(document);
	if (!index) {
		return;
	}
	if (index > 0) {
		_savedGifs.remove(index);
	}
	_savedGifs.push_front(document);
	if (_savedGifs.size() > session().serverConfig().savedGifsLimit) {
		_savedGifs.pop_back();
	}
	session().local().writeSavedGifs();

	notifySavedGifsUpdated();
	setLastSavedGifsUpdate(0);
	session().api().updateStickers();
}

void Stickers::checkSavedGif(not_null<HistoryItem*> item) {
	if (item->Has<HistoryMessageForwarded>()
		|| (!item->out()
			&& item->history()->peer != session().user())) {
		return;
	}
	if (const auto media = item->media()) {
		if (const auto document = media->document()) {
			if (document->isGifv()) {
				addSavedGif(document);
			}
		}
	}
}

void Stickers::applyArchivedResult(
		const MTPDmessages_stickerSetInstallResultArchive &d) {
	auto &v = d.vsets().v;
	auto &order = setsOrderRef();
	StickersSetsOrder archived;
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
			auto set = feedSet(*setData);
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
			session().api().scheduleStickerSetRequest(i.key(), i.value());
		}
		session().api().requestStickerSets();
	}
	session().local().writeInstalledStickers();
	session().local().writeArchivedStickers();

	Ui::Toast::Show(Ui::Toast::Config{
		.text = { tr::lng_stickers_packs_archived(tr::now) },
		.st = &st::stickersToast,
		.multiline = true,
	});
	//Ui::show(
	//	Box<StickersBox>(archived, &session()),
	//	Ui::LayerOption::KeepOther);

	notifyUpdated();
}

// For testing: Just apply random subset or your sticker sets as archived.
bool Stickers::applyArchivedResultFake() {
	auto sets = QVector<MTPStickerSetCovered>();
	for (const auto &[id, set] : this->sets()) {
		const auto raw = set.get();
		if ((raw->flags & MTPDstickerSet::Flag::f_installed_date)
			&& !(raw->flags & MTPDstickerSet_ClientFlag::f_special)) {
			if (openssl::RandomValue<uint32>() % 128 < 64) {
				const auto data = MTP_stickerSet(
					MTP_flags(raw->flags | MTPDstickerSet::Flag::f_archived),
					MTP_int(raw->installDate),
					MTP_long(raw->id),
					MTP_long(raw->access),
					MTP_string(raw->title),
					MTP_string(raw->shortName),
					MTP_vector<MTPPhotoSize>(),
					MTP_int(0),
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
	auto result = MTP_messages_stickerSetInstallResultArchive(
		MTP_vector<MTPStickerSetCovered>(sets));
	applyArchivedResult(result.c_messages_stickerSetInstallResultArchive());
	return true;
}

void Stickers::installLocally(uint64 setId) {
	auto &sets = setsRef();
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

	auto &order = setsOrderRef();
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
	session().local().writeInstalledStickers();
	if (changedFlags & MTPDstickerSet_ClientFlag::f_unread) {
		session().local().writeFeaturedStickers();
	}
	if (changedFlags & MTPDstickerSet::Flag::f_archived) {
		auto index = archivedSetsOrderRef().indexOf(setId);
		if (index >= 0) {
			archivedSetsOrderRef().removeAt(index);
			session().local().writeArchivedStickers();
		}
	}
	notifyUpdated();
}

void Stickers::undoInstallLocally(uint64 setId) {
	const auto &sets = this->sets();
	const auto it = sets.find(setId);
	if (it == sets.end()) {
		return;
	}

	const auto set = it->second.get();
	set->flags &= ~MTPDstickerSet::Flag::f_installed_date;
	set->installDate = TimeId(0);

	auto &order = setsOrderRef();
	int currentIndex = order.indexOf(setId);
	if (currentIndex >= 0) {
		order.removeAt(currentIndex);
	}

	session().local().writeInstalledStickers();
	notifyUpdated();

	Ui::show(
		Box<InformBox>(tr::lng_stickers_not_found(tr::now)),
		Ui::LayerOption::KeepOther);
}

bool Stickers::isFaved(not_null<const DocumentData*> document) {
	const auto &sets = this->sets();
	const auto it = sets.find(FavedSetId);
	if (it == sets.cend()) {
		return false;
	}
	for (const auto sticker : std::as_const(it->second->stickers)) {
		if (sticker == document) {
			return true;
		}
	}
	return false;
}

void Stickers::checkFavedLimit(StickersSet &set) {
	if (set.stickers.size() <= session().serverConfig().stickersFavedLimit) {
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

void Stickers::pushFavedToFront(
		StickersSet &set,
		not_null<DocumentData*> document,
		const std::vector<not_null<EmojiPtr>> &emojiList) {
	set.stickers.push_front(document);
	for (auto emoji : emojiList) {
		set.emoji[emoji].push_front(document);
	}
	checkFavedLimit(set);
}

void Stickers::moveFavedToFront(StickersSet &set, int index) {
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

void Stickers::setIsFaved(
		not_null<DocumentData*> document,
		std::optional<std::vector<not_null<EmojiPtr>>> emojiList) {
	auto &sets = setsRef();
	auto it = sets.find(FavedSetId);
	if (it == sets.end()) {
		it = sets.emplace(FavedSetId, std::make_unique<StickersSet>(
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
		moveFavedToFront(*set, index);
	} else if (emojiList) {
		pushFavedToFront(*set, document, *emojiList);
	} else if (auto list = getEmojiListFromSet(document)) {
		pushFavedToFront(*set, document, *list);
	} else {
		requestSetToPushFaved(document);
		return;
	}
	session().local().writeFavedStickers();
	notifyUpdated();
	session().api().stickerSetInstalled(FavedSetId);
}

void Stickers::requestSetToPushFaved(not_null<DocumentData*> document) {
	auto addAnyway = [=](std::vector<not_null<EmojiPtr>> list) {
		if (list.empty()) {
			if (auto sticker = document->sticker()) {
				if (auto emoji = Ui::Emoji::Find(sticker->alt)) {
					list.push_back(emoji);
				}
			}
		}
		setIsFaved(document, std::move(list));
	};
	session().api().request(MTPmessages_GetStickerSet(
		document->sticker()->set
	)).done([=](const MTPmessages_StickerSet &result) {
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
	}).fail([=](const MTP::Error &error) {
		// Perhaps this is a deleted sticker pack. Add anyway.
		addAnyway({});
	}).send();
}

void Stickers::removeFromRecentSet(not_null<DocumentData*> document) {
	RemoveFromSet(setsRef(), document, CloudRecentSetId);
	session().local().writeRecentStickers();
	notifyRecentUpdated();
}

void Stickers::setIsNotFaved(not_null<DocumentData*> document) {
	RemoveFromSet(setsRef(), document, FavedSetId);
	session().local().writeFavedStickers();
	notifyUpdated();
}

void Stickers::setFaved(not_null<DocumentData*> document, bool faved) {
	if (faved) {
		setIsFaved(document);
	} else {
		setIsNotFaved(document);
	}
}

void Stickers::setsReceived(const QVector<MTPStickerSet> &data, int32 hash) {
	auto &setsOrder = setsOrderRef();
	setsOrder.clear();

	auto &sets = setsRef();
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
			auto set = feedSet(setData.c_stickerSet());
			if (!(set->flags & MTPDstickerSet::Flag::f_archived) || (set->flags & MTPDstickerSet::Flag::f_official)) {
				setsOrder.push_back(set->id);
				if (set->stickers.isEmpty() || (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
					setsToRequest.insert(set->id, set->access);
				}
			}
		}
	}
	auto writeRecent = false;
	auto &recent = getRecentPack();
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
		auto &api = session().api();
		for (auto i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			api.scheduleStickerSetRequest(i.key(), i.value());
		}
		api.requestStickerSets();
	}

	session().local().writeInstalledStickers();
	if (writeRecent) session().saveSettings();

	const auto counted = Api::CountStickersHash(&session());
	if (counted != hash) {
		LOG(("API Error: received stickers hash %1 while counted hash is %2"
			).arg(hash
			).arg(counted));
	}

	notifyUpdated();
}

void Stickers::setPackAndEmoji(
		StickersSet &set,
		StickersPack &&pack,
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

			auto p = StickersPack();
			p.reserve(stickers.size());
			for (auto j = 0, c = stickers.size(); j != c; ++j) {
				auto document = owner().document(stickers[j].v);
				if (!document || !document->sticker()) continue;

				p.push_back(document);
			}
			set.emoji.insert(emoji, p);
		}
	}
}

void Stickers::specialSetReceived(
		uint64 setId,
		const QString &setTitle,
		const QVector<MTPDocument> &items,
		int32 hash,
		const QVector<MTPStickerPack> &packs,
		const QVector<MTPint> &usageDates) {
	auto &sets = setsRef();
	auto it = sets.find(setId);

	if (items.isEmpty()) {
		if (it != sets.cend()) {
			sets.erase(it);
		}
	} else {
		if (it == sets.cend()) {
			it = sets.emplace(setId, std::make_unique<StickersSet>(
				&owner(),
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
		auto pack = StickersPack();
		pack.reserve(items.size());
		for (const auto &item : items) {
			++dateIndex;
			const auto document = owner().processDocument(item);
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
		auto &recent = getRecentPack();
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
			setPackAndEmoji(*set, std::move(pack), std::move(dates), packs);
		}

		if (writeRecent) {
			session().saveSettings();
		}
	}

	switch (setId) {
	case CloudRecentSetId: {
		const auto counted = Api::CountRecentStickersHash(&session());
		if (counted != hash) {
			LOG(("API Error: "
				"received recent stickers hash %1 while counted hash is %2"
				).arg(hash
				).arg(counted));
		}
		session().local().writeRecentStickers();
	} break;
	case FavedSetId: {
		const auto counted = Api::CountFavedStickersHash(&session());
		if (counted != hash) {
			LOG(("API Error: "
				"received faved stickers hash %1 while counted hash is %2"
				).arg(hash
				).arg(counted));
		}
		session().local().writeFavedStickers();
	} break;
	default: Unexpected("setId in SpecialSetReceived()");
	}

	notifyUpdated();
}

void Stickers::featuredSetsReceived(
		const QVector<MTPStickerSetCovered> &list,
		const QVector<MTPlong> &unread,
		int32 hash) {
	auto &&unreadIds = ranges::views::all(
		unread
	) | ranges::views::transform([](const MTPlong &id) {
		return id.v;
	});
	const auto unreadMap = base::flat_set<uint64>{
		unreadIds.begin(),
		unreadIds.end()
	};

	auto &setsOrder = featuredSetsOrderRef();
	setsOrder.clear();

	auto &sets = setsRef();
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
		const auto title = getSetTitle(*data);
		const auto installDate = data->vinstalled_date().value_or_empty();
		const auto thumbnail = [&] {
			if (const auto thumbs = data->vthumbs()) {
				for (const auto &thumb : thumbs->v) {
					const auto result = Images::FromPhotoSize(
						&session(),
						*data,
						thumb);
					if (result.location.valid()) {
						return result;
					}
				}
			}
			return ImageWithLocation();
		}();
		if (it == sets.cend()) {
			auto setClientFlags = MTPDstickerSet_ClientFlag::f_featured
				| MTPDstickerSet_ClientFlag::f_not_loaded;
			if (unreadMap.contains(data->vid().v)) {
				setClientFlags |= MTPDstickerSet_ClientFlag::f_unread;
			}
			it = sets.emplace(data->vid().v, std::make_unique<StickersSet>(
				&owner(),
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
	setFeaturedSetsUnreadCount(unreadCount);

	const auto counted = Api::CountFeaturedStickersHash(&session());
	if (counted != hash) {
		LOG(("API Error: "
			"received featured stickers hash %1 while counted hash is %2"
			).arg(hash
			).arg(counted));
	}

	if (!setsToRequest.empty()) {
		auto &api = session().api();
		for (const auto &[setId, accessHash] : setsToRequest) {
			api.scheduleStickerSetRequest(setId, accessHash);
		}
		api.requestStickerSets();
	}

	session().local().writeFeaturedStickers();

	notifyUpdated();
}

void Stickers::gifsReceived(const QVector<MTPDocument> &items, int32 hash) {
	auto &saved = savedGifsRef();
	saved.clear();

	saved.reserve(items.size());
	for (const auto &item : items) {
		const auto document = owner().processDocument(item);
		if (!document->isGifv()) {
			LOG(("API Error: "
				"bad document returned in Stickers::gifsReceived!"));
			continue;
		}

		saved.push_back(document);
	}
	const auto counted = Api::CountSavedGifsHash(&session());
	if (counted != hash) {
		LOG(("API Error: "
			"received saved gifs hash %1 while counted hash is %2"
			).arg(hash
			).arg(counted));
	}

	session().local().writeSavedGifs();

	notifySavedGifsUpdated();
}

std::vector<not_null<DocumentData*>> Stickers::getListByEmoji(
		not_null<EmojiPtr> emoji,
		uint64 seed) {
	const auto original = emoji->original();

	struct StickerWithDate {
		not_null<DocumentData*> document;
		TimeId date = 0;
	};
	auto result = std::vector<StickerWithDate>();
	auto &sets = setsRef();
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
	const auto addList = [&](
			const StickersSetsOrder &order,
			MTPDstickerSet::Flag skip) {
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
		setsOrder(),
		MTPDstickerSet::Flag::f_archived);
	//addList(
	//	featuredSetsOrder(),
	//	MTPDstickerSet::Flag::f_installed_date);

	if (!setsToRequest.empty()) {
		for (const auto &[setId, accessHash] : setsToRequest) {
			session().api().scheduleStickerSetRequest(setId, accessHash);
		}
		session().api().requestStickerSets();
	}

	if (Core::App().settings().suggestStickersByEmoji()) {
		const auto others = session().api().stickersByEmoji(original);
		if (!others) {
			return {};
		}
		result.reserve(result.size() + others->size());
		for (const auto document : *others) {
			add(document, CreateOtherSortKey(document));
		}
	}

	ranges::actions::sort(
		result,
		std::greater<>(),
		&StickerWithDate::date);

	return ranges::views::all(
		result
	) | ranges::views::transform([](const StickerWithDate &data) {
		return data.document;
	}) | ranges::to_vector;
}

std::optional<std::vector<not_null<EmojiPtr>>> Stickers::getEmojiListFromSet(
		not_null<DocumentData*> document) {
	if (auto sticker = document->sticker()) {
		auto &inputSet = sticker->set;
		if (inputSet.type() != mtpc_inputStickerSetID) {
			return std::nullopt;
		}
		const auto &sets = this->sets();
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

StickersSet *Stickers::feedSet(const MTPDstickerSet &data) {
	auto &sets = setsRef();
	auto it = sets.find(data.vid().v);
	auto title = getSetTitle(data);
	auto flags = MTPDstickerSet::Flags(0);
	const auto thumbnail = [&] {
		if (const auto thumbs = data.vthumbs()) {
			for (const auto &thumb : thumbs->v) {
				const auto result = Images::FromPhotoSize(
					&session(),
					data,
					thumb);
				if (result.location.valid()) {
					return result;
				}
			}
		}
		return ImageWithLocation();
	}();
	if (it == sets.cend()) {
		it = sets.emplace(data.vid().v, std::make_unique<StickersSet>(
			&owner(),
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
		auto index = archivedSetsOrder().indexOf(set->id);
		if (set->flags & MTPDstickerSet::Flag::f_archived) {
			if (index < 0) {
				archivedSetsOrderRef().push_front(set->id);
			}
		} else if (index >= 0) {
			archivedSetsOrderRef().removeAt(index);
		}
	}
	return it->second.get();
}

StickersSet *Stickers::feedSetFull(const MTPmessages_StickerSet &data) {
	Expects(data.type() == mtpc_messages_stickerSet);
	Expects(data.c_messages_stickerSet().vset().type() == mtpc_stickerSet);

	const auto &d = data.c_messages_stickerSet();
	const auto &s = d.vset().c_stickerSet();

	auto &sets = setsRef();
	const auto wasArchived = [&] {
		auto it = sets.find(s.vid().v);
		return (it != sets.end())
			&& (it->second->flags & MTPDstickerSet::Flag::f_archived);
	}();

	auto set = feedSet(s);

	set->flags &= ~MTPDstickerSet_ClientFlag::f_not_loaded;

	const auto &d_docs = d.vdocuments().v;
	auto customIt = sets.find(Stickers::CustomSetId);
	const auto inputSet = MTP_inputStickerSetID(
		MTP_long(set->id),
		MTP_long(set->access));

	auto pack = StickersPack();
	pack.reserve(d_docs.size());
	for (const auto &item : d_docs) {
		const auto document = owner().processDocument(item);
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
	auto &recent = getRecentPack();
	for (auto i = recent.begin(); i != recent.cend();) {
		if (set->stickers.indexOf(i->first) >= 0 && pack.indexOf(i->first) < 0) {
			i = recent.erase(i);
			writeRecent = true;
		} else {
			++i;
		}
	}

	if (pack.isEmpty()) {
		int removeIndex = setsOrder().indexOf(set->id);
		if (removeIndex >= 0) setsOrderRef().removeAt(removeIndex);
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

				StickersPack p;
				p.reserve(stickers.size());
				for (auto j = 0, c = stickers.size(); j != c; ++j) {
					auto doc = owner().document(stickers[j].v);
					if (!doc || !doc->sticker()) continue;

					p.push_back(doc);
				}
				set->emoji.insert(emoji, p);
			}
		}
	}

	if (writeRecent) {
		session().saveSettings();
	}

	if (set) {
		const auto isArchived = !!(set->flags & MTPDstickerSet::Flag::f_archived);
		if (set->flags & MTPDstickerSet::Flag::f_installed_date) {
			if (!isArchived) {
				session().local().writeInstalledStickers();
			}
		}
		if (set->flags & MTPDstickerSet_ClientFlag::f_featured) {
			session().local().writeFeaturedStickers();
		}
		if (wasArchived != isArchived) {
			session().local().writeArchivedStickers();
		}
	}

	notifyUpdated();

	return set;
}

void Stickers::newSetReceived(const MTPmessages_StickerSet &data) {
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
	auto &order = setsOrderRef();
	int32 insertAtIndex = 0, currentIndex = order.indexOf(s.vid().v);
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, s.vid().v);
	}

	feedSetFull(data);
}

QString Stickers::getSetTitle(const MTPDstickerSet &s) {
	auto title = qs(s.vtitle());
	if ((s.vflags().v & MTPDstickerSet::Flag::f_official) && !title.compare(qstr("Great Minds"), Qt::CaseInsensitive)) {
		return tr::lng_stickers_default_set(tr::now);
	}
	return title;
}

RecentStickerPack &Stickers::getRecentPack() const {
	if (cRecentStickers().isEmpty() && !cRecentStickersPreload().isEmpty()) {
		const auto p = cRecentStickersPreload();
		cSetRecentStickersPreload(RecentStickerPreload());

		auto &recent = cRefRecentStickers();
		recent.reserve(p.size());
		for (const auto &preloaded : p) {
			const auto document = owner().document(preloaded.first);
			if (!document || !document->sticker()) continue;

			recent.push_back(qMakePair(document, preloaded.second));
		}
	}
	return cRefRecentStickers();
}

} // namespace Stickers
