/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/stickers/data_stickers.h"

#include "api/api_hash.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "lang/lang_keys.h"
#include "data/data_premium_limits.h"
#include "boxes/premium_limits_box.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "apiwrap.h"
#include "storage/storage_account.h"
#include "settings/settings_premium.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_session.h"
#include "main/main_app_config.h"
#include "mtproto/mtproto_config.h"
#include "ui/toast/toast.h"
#include "ui/image/image_location_factory.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"
#include "base/unixtime.h"
#include "boxes/abstract_box.h" // Ui::show().
#include "styles/style_chat_helpers.h"

namespace Data {
namespace {

constexpr auto kPremiumToastDuration = 5 * crl::time(1000);

using SetFlag = StickersSetFlag;

[[nodiscard]] TextWithEntities SavedGifsToast(
		const Data::PremiumLimits &limits) {
	const auto defaultLimit = limits.gifsDefault();
	const auto premiumLimit = limits.gifsPremium();
	return Ui::Text::Bold(
		tr::lng_saved_gif_limit_title(tr::now, lt_count, defaultLimit)
	).append('\n').append(
		tr::lng_saved_gif_limit_more(
			tr::now,
			lt_count,
			premiumLimit,
			lt_link,
			Ui::Text::Link(tr::lng_saved_gif_limit_link(tr::now)),
			Ui::Text::WithEntities));
}

[[nodiscard]] TextWithEntities FaveStickersToast(
		const Data::PremiumLimits &limits) {
	const auto defaultLimit = limits.stickersFavedDefault();
	const auto premiumLimit = limits.stickersFavedPremium();
	return Ui::Text::Bold(
		tr::lng_fave_sticker_limit_title(tr::now, lt_count, defaultLimit)
	).append('\n').append(
		tr::lng_fave_sticker_limit_more(
			tr::now,
			lt_count,
			premiumLimit,
			lt_link,
			Ui::Text::Link(tr::lng_fave_sticker_limit_link(tr::now)),
			Ui::Text::WithEntities));
}

void MaybeShowPremiumToast(
		std::shared_ptr<ChatHelpers::Show> show,
		TextWithEntities text,
		const QString &ref) {
	if (!show) {
		return;
	}
	const auto session = &show->session();
	if (session->user()->isPremium()) {
		return;
	}
	const auto filter = [=](const auto ...) {
		if (const auto controller = show->resolveWindow()) {
			Settings::ShowPremium(controller, ref);
		}
		return false;
	};
	show->showToast({
		.text = std::move(text),
		.filter = filter,
		.duration = kPremiumToastDuration,
	});
}

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
		const auto index = i->second.indexOf(document);
		if (index >= 0) {
			i->second.removeAt(index);
			if (i->second.empty()) {
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

void Stickers::notifyUpdated(StickersType type) {
	_updated.fire_copy(type);
}

rpl::producer<StickersType> Stickers::updated() const {
	return _updated.events();
}

rpl::producer<> Stickers::updated(StickersType type) const {
	using namespace rpl::mappers;
	return updated() | rpl::filter(_1 == type) | rpl::to_empty;
}

void Stickers::notifyRecentUpdated(StickersType type) {
	_recentUpdated.fire_copy(type);
}

rpl::producer<StickersType> Stickers::recentUpdated() const {
	return _recentUpdated.events();
}

rpl::producer<> Stickers::recentUpdated(StickersType type) const {
	using namespace rpl::mappers;
	return recentUpdated() | rpl::filter(_1 == type) | rpl::to_empty;
}

void Stickers::notifySavedGifsUpdated() {
	_savedGifsUpdated.fire({});
}

rpl::producer<> Stickers::savedGifsUpdated() const {
	return _savedGifsUpdated.events();
}

void Stickers::notifyStickerSetInstalled(uint64 setId) {
	_stickerSetInstalled.fire(std::move(setId));
}

rpl::producer<uint64> Stickers::stickerSetInstalled() const {
	return _stickerSetInstalled.events();
}

void Stickers::notifyEmojiSetInstalled(uint64 setId) {
	_emojiSetInstalled.fire(std::move(setId));
}

rpl::producer<uint64> Stickers::emojiSetInstalled() const {
	return _emojiSetInstalled.events();
}

void Stickers::incrementSticker(not_null<DocumentData*> document) {
	if (!document->sticker() || !document->sticker()->set) {
		return;
	}

	bool writeRecentStickers = false;
	auto &sets = setsRef();
	auto it = sets.find(Data::Stickers::CloudRecentSetId);
	if (it == sets.cend()) {
		it = sets.emplace(
			Data::Stickers::CloudRecentSetId,
			std::make_unique<Data::StickersSet>(
				&session().data(),
				Data::Stickers::CloudRecentSetId,
				uint64(0), // accessHash
				uint64(0), // hash
				tr::lng_recent_stickers(tr::now),
				QString(),
				0, // count
				SetFlag::Special,
				TimeId(0))).first;
	} else {
		it->second->title = tr::lng_recent_stickers(tr::now);
	}
	const auto set = it->second.get();
	auto removedFromEmoji = std::vector<not_null<EmojiPtr>>();
	auto index = set->stickers.indexOf(document);
	if (index > 0) {
		if (set->dates.empty()) {
			session().api().requestSpecialStickersForce(false, true, false);
		} else {
			Assert(set->dates.size() == set->stickers.size());
			set->dates.erase(set->dates.begin() + index);
		}
		set->stickers.removeAt(index);
		for (auto i = set->emoji.begin(); i != set->emoji.end();) {
			if (const auto index = i->second.indexOf(document); index >= 0) {
				removedFromEmoji.emplace_back(i->first);
				i->second.removeAt(index);
				if (i->second.empty()) {
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
			for (const auto &emoji : *emojiList) {
				set->emoji[emoji].push_front(document);
			}
		} else if (!removedFromEmoji.empty()) {
			for (const auto emoji : removedFromEmoji) {
				set->emoji[emoji].push_front(document);
			}
		} else {
			session().api().requestSpecialStickersForce(false, true, false);
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
	notifyRecentUpdated(StickersType::Stickers);
}

void Stickers::addSavedGif(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	const auto index = _savedGifs.indexOf(document);
	if (!index) {
		return;
	}
	if (index > 0) {
		_savedGifs.remove(index);
	}
	_savedGifs.push_front(document);
	const auto session = &document->session();
	const auto limits = Data::PremiumLimits(session);
	if (_savedGifs.size() > limits.gifsCurrent()) {
		_savedGifs.pop_back();
		MaybeShowPremiumToast(
			show,
			SavedGifsToast(limits),
			LimitsPremiumRef("saved_gifs"));
	}
	session->local().writeSavedGifs();

	notifySavedGifsUpdated();
	setLastSavedGifsUpdate(0);
	session->api().updateSavedGifs();
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
				addSavedGif(nullptr, document);
			}
		}
	}
}

void Stickers::applyArchivedResult(
		const MTPDmessages_stickerSetInstallResultArchive &d) {
	auto &v = d.vsets().v;
	StickersSetsOrder archived;
	archived.reserve(v.size());
	QMap<uint64, uint64> setsToRequest;

	auto masksCount = 0;
	auto stickersCount = 0;
	for (const auto &data : v) {
		const auto set = feedSet(data);
		if (set->flags & SetFlag::NotLoaded) {
			setsToRequest.insert(set->id, set->accessHash);
		}
		if (set->type() == StickersType::Emoji) {
			continue;
		}
		const auto isMasks = (set->type() == StickersType::Masks);
		(isMasks ? masksCount : stickersCount)++;
		auto &order = isMasks ? maskSetsOrderRef() : setsOrderRef();
		const auto index = order.indexOf(set->id);
		if (index >= 0) {
			order.removeAt(index);
		}
		archived.push_back(set->id);
	}
	if (!setsToRequest.isEmpty()) {
		for (auto i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			session().api().scheduleStickerSetRequest(i.key(), i.value());
		}
		session().api().requestStickerSets();
	}
	if (stickersCount) {
		session().local().writeInstalledStickers();
		session().local().writeArchivedStickers();
	}
	if (masksCount) {
		session().local().writeInstalledMasks();
		session().local().writeArchivedMasks();
	}

	// TODO async toast.
	Ui::Toast::Show(Ui::Toast::Config{
		.text = { tr::lng_stickers_packs_archived(tr::now) },
		.st = &st::stickersToast,
	});
	//Ui::show(
	//	Box<StickersBox>(archived, &session()),
	//	Ui::LayerOption::KeepOther);
	if (stickersCount) {
		notifyUpdated(StickersType::Stickers);
	}
	if (masksCount) {
		notifyUpdated(StickersType::Masks);
	}
}

void Stickers::installLocally(uint64 setId) {
	auto &sets = setsRef();
	auto it = sets.find(setId);
	if (it == sets.end()) {
		return;
	}

	const auto set = it->second.get();
	const auto flags = set->flags;
	set->flags &= ~(SetFlag::Archived | SetFlag::Unread);
	set->flags |= SetFlag::Installed;
	set->installDate = base::unixtime::now();
	auto changedFlags = flags ^ set->flags;

	const auto isMasks = (set->type() == StickersType::Masks);
	const auto isEmoji = (set->type() == StickersType::Emoji);
	auto &order = isEmoji
		? emojiSetsOrderRef()
		: isMasks
		? maskSetsOrderRef()
		: setsOrderRef();
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
	if (!isMasks && (changedFlags & SetFlag::Unread)) {
		if (isEmoji) {
			session().local().writeFeaturedCustomEmoji();
		} else {
			session().local().writeFeaturedStickers();
		}
	}
	if (!isEmoji && (changedFlags & SetFlag::Archived)) {
		auto &archivedOrder = isMasks
			? archivedMaskSetsOrderRef()
			: archivedSetsOrderRef();
		const auto index = archivedOrder.indexOf(setId);
		if (index >= 0) {
			archivedOrder.removeAt(index);
			if (isMasks) {
				session().local().writeArchivedMasks();
			} else {
				session().local().writeArchivedStickers();
			}
		}
	}
	notifyUpdated(set->type());
}

void Stickers::undoInstallLocally(uint64 setId) {
	const auto &sets = this->sets();
	const auto it = sets.find(setId);
	if (it == sets.end()) {
		return;
	}

	const auto set = it->second.get();
	set->flags &= ~SetFlag::Installed;
	set->installDate = TimeId(0);

	auto &order = setsOrderRef();
	int currentIndex = order.indexOf(setId);
	if (currentIndex >= 0) {
		order.removeAt(currentIndex);
	}

	session().local().writeInstalledStickers();
	notifyUpdated(set->type());

	Ui::show(
		Ui::MakeInformBox(tr::lng_stickers_not_found()),
		Ui::LayerOption::KeepOther);
}

bool Stickers::isFaved(not_null<const DocumentData*> document) const {
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

void Stickers::checkFavedLimit(
		StickersSet &set,
		std::shared_ptr<ChatHelpers::Show> show) {
	const auto session = &_owner->session();
	const auto limits = Data::PremiumLimits(session);
	if (set.stickers.size() <= limits.stickersFavedCurrent()) {
		return;
	}
	auto removing = set.stickers.back();
	set.stickers.pop_back();
	for (auto i = set.emoji.begin(); i != set.emoji.end();) {
		auto index = i->second.indexOf(removing);
		if (index >= 0) {
			i->second.removeAt(index);
			if (i->second.empty()) {
				i = set.emoji.erase(i);
				continue;
			}
		}
		++i;
	}
	MaybeShowPremiumToast(
		std::move(show),
		FaveStickersToast(limits),
		LimitsPremiumRef("stickers_faved"));
}

void Stickers::pushFavedToFront(
		StickersSet &set,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		const std::vector<not_null<EmojiPtr>> &emojiList) {
	set.stickers.push_front(document);
	for (auto emoji : emojiList) {
		set.emoji[emoji].push_front(document);
	}
	checkFavedLimit(set, std::move(show));
}

void Stickers::moveFavedToFront(StickersSet &set, int index) {
	Expects(index > 0 && index < set.stickers.size());

	auto document = set.stickers[index];
	while (index-- != 0) {
		set.stickers[index + 1] = set.stickers[index];
	}
	set.stickers[0] = document;
	for (auto &[emoji, list] : set.emoji) {
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
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		std::optional<std::vector<not_null<EmojiPtr>>> emojiList) {
	auto &sets = setsRef();
	auto it = sets.find(FavedSetId);
	if (it == sets.end()) {
		it = sets.emplace(FavedSetId, std::make_unique<StickersSet>(
			&document->owner(),
			FavedSetId,
			uint64(0), // accessHash
			uint64(0), // hash
			Lang::Hard::FavedSetTitle(),
			QString(),
			0, // count
			SetFlag::Special,
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
		pushFavedToFront(*set, show, document, *emojiList);
	} else if (auto list = getEmojiListFromSet(document)) {
		pushFavedToFront(*set, show, document, *list);
	} else {
		requestSetToPushFaved(show, document);
		return;
	}
	session().local().writeFavedStickers();
	notifyUpdated(StickersType::Stickers);
	notifyStickerSetInstalled(FavedSetId);
}

void Stickers::requestSetToPushFaved(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	auto addAnyway = [=](std::vector<not_null<EmojiPtr>> list) {
		if (list.empty()) {
			if (auto sticker = document->sticker()) {
				if (auto emoji = Ui::Emoji::Find(sticker->alt)) {
					list.push_back(emoji);
				}
			}
		}
		setIsFaved(nullptr, document, std::move(list));
	};
	session().api().request(MTPmessages_GetStickerSet(
		Data::InputStickerSet(document->sticker()->set),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		result.match([&](const MTPDmessages_stickerSet &data) {
			auto list = std::vector<not_null<EmojiPtr>>();
			list.reserve(data.vpacks().v.size());
			for (const auto &mtpPack : data.vpacks().v) {
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
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).fail([=] {
		// Perhaps this is a deleted sticker pack. Add anyway.
		addAnyway({});
	}).send();
}

void Stickers::removeFromRecentSet(not_null<DocumentData*> document) {
	RemoveFromSet(setsRef(), document, CloudRecentSetId);
	session().local().writeRecentStickers();
	notifyRecentUpdated(StickersType::Stickers);
}

void Stickers::setIsNotFaved(not_null<DocumentData*> document) {
	RemoveFromSet(setsRef(), document, FavedSetId);
	session().local().writeFavedStickers();
	notifyUpdated(StickersType::Stickers);
}

void Stickers::setFaved(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		bool faved) {
	if (faved) {
		setIsFaved(std::move(show), document);
	} else {
		setIsNotFaved(document);
	}
}

void Stickers::setsReceived(
		const QVector<MTPStickerSet> &data,
		uint64 hash) {
	somethingReceived(data, hash, StickersType::Stickers);
}

void Stickers::masksReceived(
		const QVector<MTPStickerSet> &data,
		uint64 hash) {
	somethingReceived(data, hash, StickersType::Masks);
}

void Stickers::emojiReceived(
		const QVector<MTPStickerSet> &data,
		uint64 hash) {
	somethingReceived(data, hash, StickersType::Emoji);
}

void Stickers::somethingReceived(
		const QVector<MTPStickerSet> &list,
		uint64 hash,
		StickersType type) {
	auto &setsOrder = (type == StickersType::Emoji)
		? emojiSetsOrderRef()
		: (type == StickersType::Masks)
		? maskSetsOrderRef()
		: setsOrderRef();
	setsOrder.clear();

	auto &sets = setsRef();
	QMap<uint64, uint64> setsToRequest;
	for (auto &[id, set] : sets) {
		const auto archived = !!(set->flags & SetFlag::Archived);
		if (!archived && (type == set->type())) {
			// Mark for removing.
			set->flags &= ~SetFlag::Installed;
			set->installDate = 0;
		}
	}
	for (const auto &info : list) {
		const auto set = feedSet(info);
		if (!(set->flags & SetFlag::Archived)
			|| (set->flags & SetFlag::Official)) {
			setsOrder.push_back(set->id);
			if (set->stickers.isEmpty()
				|| (set->flags & SetFlag::NotLoaded)) {
				setsToRequest.insert(set->id, set->accessHash);
			}
		}
	}
	auto writeRecent = false;
	auto &recent = getRecentPack();
	for (auto it = sets.begin(); it != sets.end();) {
		const auto set = it->second.get();
		const auto installed = !!(set->flags & SetFlag::Installed);
		const auto featured = !!(set->flags & SetFlag::Featured);
		const auto special = !!(set->flags & SetFlag::Special);
		const auto archived = !!(set->flags & SetFlag::Archived);
		const auto emoji = !!(set->flags & SetFlag::Emoji);
		const auto locked = (set->locked > 0);
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
		if (installed || featured || special || archived || emoji || locked) {
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

	if (type == StickersType::Emoji) {
		session().local().writeInstalledCustomEmoji();
	} else if (type == StickersType::Masks) {
		session().local().writeInstalledMasks();
	} else {
		session().local().writeInstalledStickers();
	}
	if (writeRecent) {
		session().saveSettings();
	}

	const auto counted = (type == StickersType::Emoji)
		? Api::CountCustomEmojiHash(&session())
		: (type == StickersType::Masks)
		? Api::CountMasksHash(&session())
		: Api::CountStickersHash(&session());
	if (counted != hash) {
		LOG(("API Error: received %1 hash %2 while counted hash is %3"
			).arg((type == StickersType::Emoji)
				? "custom-emoji"
				: (type == StickersType::Masks)
				? "masks"
				: "stickers"
			).arg(hash
			).arg(counted));
	}

	notifyUpdated(type);
}

void Stickers::setPackAndEmoji(
		StickersSet &set,
		StickersPack &&pack,
		std::vector<TimeId> &&dates,
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
			for (auto j = 0, c = int(stickers.size()); j != c; ++j) {
				auto document = owner().document(stickers[j].v);
				if (!document || !document->sticker()) continue;

				p.push_back(document);
			}
			set.emoji[emoji] = std::move(p);
		}
	}
}

not_null<StickersSet*> Stickers::collectibleSet() {
	const auto setId = CollectibleSetId;
	auto &sets = setsRef();
	auto it = sets.find(setId);
	if (it == sets.cend()) {
		it = sets.emplace(setId, std::make_unique<StickersSet>(
				&owner(),
				setId,
				uint64(0), // accessHash
				uint64(0), // hash
				tr::lng_collectible_emoji(tr::now),
				QString(),
				0, // count
				SetFlag::Special,
				TimeId(0))).first;
	}
	return it->second.get();
}

void Stickers::specialSetReceived(
		uint64 setId,
		const QString &setTitle,
		const QVector<MTPDocument> &items,
		uint64 hash,
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
				uint64(0), // accessHash
				uint64(0), // hash
				setTitle,
				QString(),
				0, // count
				SetFlag::Special,
				TimeId(0))).first;
		} else {
			it->second->title = setTitle;
		}
		const auto set = it->second.get();
		set->hash = hash;

		auto dates = std::vector<TimeId>();
		auto dateIndex = 0;
		auto datesAvailable = (items.size() == usageDates.size())
			&& ((setId == CloudRecentSetId)
				|| (setId == CloudRecentAttachedSetId));

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
	case CloudRecentAttachedSetId: {
		const auto counted = Api::CountRecentStickersHash(&session(), true);
		if (counted != hash) {
			LOG(("API Error: "
				"received recent attached stickers hash %1 "
				"while counted hash is %2"
				).arg(hash
				).arg(counted));
		}
		session().local().writeRecentMasks();
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

	notifyUpdated((setId == CloudRecentAttachedSetId)
		? StickersType::Masks
		: StickersType::Stickers);
}

void Stickers::featuredSetsReceived(
		const MTPmessages_FeaturedStickers &result) {
	setLastFeaturedUpdate(crl::now());
	result.match([](const MTPDmessages_featuredStickersNotModified &) {
	}, [&](const MTPDmessages_featuredStickers &data) {
		featuredReceived(data, StickersType::Stickers);
	});
}

void Stickers::featuredEmojiSetsReceived(
		const MTPmessages_FeaturedStickers &result) {
	setLastFeaturedEmojiUpdate(crl::now());
	result.match([](const MTPDmessages_featuredStickersNotModified &) {
	}, [&](const MTPDmessages_featuredStickers &data) {
		featuredReceived(data, StickersType::Emoji);
	});
}

void Stickers::featuredReceived(
		const MTPDmessages_featuredStickers &data,
		StickersType type) {
	const auto &list = data.vsets().v;
	const auto &unread = data.vunread().v;
	const auto hash = data.vhash().v;

	auto &&unreadIds = ranges::views::all(
		unread
	) | ranges::views::transform(&MTPlong::v);
	const auto unreadMap = base::flat_set<uint64>{
		unreadIds.begin(),
		unreadIds.end()
	};

	const auto isEmoji = (type == StickersType::Emoji);
	auto &featuredOrder = isEmoji
		? featuredEmojiSetsOrderRef()
		: featuredSetsOrderRef();
	featuredOrder.clear();

	auto &sets = setsRef();
	auto setsToRequest = base::flat_map<uint64, uint64>();
	for (auto &[id, set] : sets) {
		// Mark for removing.
		if (set->type() == type) {
			set->flags &= ~SetFlag::Featured;
		}
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
		auto thumbnailType = StickerType::Webp;
		const auto thumbnail = [&] {
			if (const auto thumbs = data->vthumbs()) {
				for (const auto &thumb : thumbs->v) {
					const auto result = Images::FromPhotoSize(
						&session(),
						*data,
						thumb);
					if (result.location.valid()) {
						thumbnailType = ThumbnailTypeFromPhotoSize(thumb);
						return result;
					}
				}
			}
			return ImageWithLocation();
		}();
		const auto setId = data->vid().v;
		const auto flags = SetFlag::Featured
			| (unreadMap.contains(setId) ? SetFlag::Unread : SetFlag())
			| ParseStickersSetFlags(*data);
		if (it == sets.cend()) {
			it = sets.emplace(data->vid().v, std::make_unique<StickersSet>(
				&owner(),
				setId,
				data->vaccess_hash().v,
				data->vhash().v,
				title,
				qs(data->vshort_name()),
				data->vcount().v,
				flags | SetFlag::NotLoaded,
				installDate)).first;
		} else {
			const auto set = it->second.get();
			set->accessHash = data->vaccess_hash().v;
			set->title = title;
			set->shortName = qs(data->vshort_name());
			set->flags = flags
				| (set->flags & (SetFlag::NotLoaded | SetFlag::Special));
			set->installDate = installDate;
			if (set->count != data->vcount().v || set->hash != data->vhash().v || set->emoji.empty()) {
				set->count = data->vcount().v;
				set->hash = data->vhash().v;
				set->flags |= SetFlag::NotLoaded; // need to request this set
			}
		}
		it->second->setThumbnail(thumbnail, thumbnailType);
		it->second->thumbnailDocumentId = data->vthumb_document_id().value_or_empty();
		featuredOrder.push_back(data->vid().v);
		if (it->second->stickers.isEmpty()
			|| (it->second->flags & SetFlag::NotLoaded)) {
			setsToRequest.emplace(data->vid().v, data->vaccess_hash().v);
		}
	}

	auto unreadCount = 0;
	for (auto it = sets.begin(); it != sets.end();) {
		const auto set = it->second.get();
		const auto installed = (set->flags & SetFlag::Installed);
		const auto featured = (set->flags & SetFlag::Featured);
		const auto special = (set->flags & SetFlag::Special);
		const auto archived = (set->flags & SetFlag::Archived);
		const auto emoji = !!(set->flags & SetFlag::Emoji);
		const auto locked = (set->locked > 0);
		if (installed || featured || special || archived || emoji || locked) {
			if (featured && (set->flags & SetFlag::Unread)) {
				if (!(set->flags & SetFlag::Emoji)) {
					++unreadCount;
				}
			}
			++it;
		} else {
			it = sets.erase(it);
		}
	}
	setFeaturedSetsUnreadCount(unreadCount);

	const auto counted = isEmoji
		? Api::CountFeaturedEmojiHash(&session())
		: Api::CountFeaturedStickersHash(&session());
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
	if (isEmoji) {
		session().local().writeFeaturedCustomEmoji();
	} else {
		session().local().writeFeaturedStickers();
	}

	notifyUpdated(type);
}

void Stickers::gifsReceived(const QVector<MTPDocument> &items, uint64 hash) {
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

std::vector<not_null<DocumentData*>> Stickers::getPremiumList(uint64 seed) {
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
		if (document->sticker() && document->sticker()->isAnimated()) {
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
		if (!document->sticker() || !document->sticker()->isAnimated()) {
			base -= kSlice;
		}
		return (base - (++myCounter));
	};
	const auto CreateFeaturedSortKey = [&](not_null<DocumentData*> document) {
		return CreateSortKey(document, kSlice * 2);
	};
	const auto InstallDateAdjusted = [&](
			TimeId date,
			not_null<DocumentData*> document) {
		return (document->sticker() && document->sticker()->isAnimated())
			? date
			: date / 2;
	};
	const auto RecentInstallDate = [&](not_null<DocumentData*> document) {
		Expects(document->sticker() != nullptr);

		const auto sticker = document->sticker();
		if (sticker->set.id) {
			const auto setIt = sets.find(sticker->set.id);
			if (setIt != sets.end()) {
				return InstallDateAdjusted(setIt->second->installDate, document);
			}
		}
		return TimeId(0);
	};

	auto recentIt = sets.find(Stickers::CloudRecentSetId);
	if (recentIt != sets.cend()) {
		const auto recent = recentIt->second.get();
		const auto count = int(recent->stickers.size());
		result.reserve(count);
		for (auto i = 0; i != count; ++i) {
			const auto document = recent->stickers[i];
			auto index = i;
			if (!document->isPremiumSticker()) {
				continue;
			} else {
				index = recent->stickers.indexOf(document);
			}
			const auto usageDate = (recent->dates.empty() || index < 0)
				? 0
				: recent->dates[index];
			const auto date = usageDate
				? usageDate
				: RecentInstallDate(document);
			result.push_back({
				document,
				date ? date : CreateRecentSortKey(document) });
		}
	}
	const auto addList = [&](
			const StickersSetsOrder &order,
			SetFlag skip) {
		for (const auto setId : order) {
			auto it = sets.find(setId);
			if (it == sets.cend() || (it->second->flags & skip)) {
				continue;
			}
			const auto set = it->second.get();
			if (set->emoji.empty()) {
				setsToRequest.emplace(set->id, set->accessHash);
				set->flags |= SetFlag::NotLoaded;
				continue;
			}
			const auto my = (set->flags & SetFlag::Installed);
			result.reserve(result.size() + set->stickers.size());
			for (const auto document : set->stickers) {
				if (!document->isPremiumSticker()) {
					continue;
				}
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

	addList(setsOrder(), SetFlag::Archived);
	addList(featuredSetsOrder(), SetFlag::Installed);

	if (!setsToRequest.empty()) {
		for (const auto &[setId, accessHash] : setsToRequest) {
			session().api().scheduleStickerSetRequest(setId, accessHash);
		}
		session().api().requestStickerSets();
	}

	ranges::sort(result, std::greater<>(), &StickerWithDate::date);

	return result
		| ranges::views::transform(&StickerWithDate::document)
		| ranges::to_vector;
}

std::vector<not_null<DocumentData*>> Stickers::getListByEmoji(
		std::vector<EmojiPtr> emoji,
		uint64 seed,
		bool forceAllResults) {
	auto all = base::flat_set<EmojiPtr>();
	for (const auto &one : emoji) {
		all.emplace(one->original());
	}
	const auto single = (all.size() == 1) ? all.front() : nullptr;

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
		if (document->sticker() && document->sticker()->isAnimated()) {
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
		if (!document->sticker() || !document->sticker()->isAnimated()) {
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
		return (document->sticker() && document->sticker()->isAnimated())
			? date
			: date / 2;
	};
	const auto RecentInstallDate = [&](not_null<DocumentData*> document) {
		Expects(document->sticker() != nullptr);

		const auto sticker = document->sticker();
		if (sticker->set.id) {
			const auto setIt = sets.find(sticker->set.id);
			if (setIt != sets.end()) {
				return InstallDateAdjusted(setIt->second->installDate, document);
			}
		}
		return TimeId(0);
	};

	auto recentIt = sets.find(Stickers::CloudRecentSetId);
	if (recentIt != sets.cend()) {
		const auto recent = recentIt->second.get();
		const auto i = single
			? recent->emoji.find(single)
			: recent->emoji.end();
		const auto list = (i != recent->emoji.end())
			? &i->second
			: !single
			? &recent->stickers
			: nullptr;
		if (list) {
			const auto count = int(list->size());
			result.reserve(count);
			for (auto i = 0; i != count; ++i) {
				const auto document = (*list)[i];
				const auto sticker = document->sticker();
				auto index = i;
				if (!sticker) {
					continue;
				} else if (!single) {
					const auto main = Ui::Emoji::Find(sticker->alt);
					if (!main || !all.contains(main)) {
						continue;
					}
				} else {
					index = recent->stickers.indexOf(document);
				}
				const auto usageDate = (recent->dates.empty() || index < 0)
					? 0
					: recent->dates[index];
				const auto date = usageDate
					? usageDate
					: RecentInstallDate(document);
				result.push_back({
					document,
					date ? date : CreateRecentSortKey(document) });
			}
		}
	}
	const auto addList = [&](
			const StickersSetsOrder &order,
			SetFlag skip) {
		for (const auto setId : order) {
			auto it = sets.find(setId);
			if (it == sets.cend() || (it->second->flags & skip)) {
				continue;
			}
			const auto set = it->second.get();
			if (set->emoji.empty()) {
				setsToRequest.emplace(set->id, set->accessHash);
				set->flags |= SetFlag::NotLoaded;
				continue;
			}
			const auto my = (set->flags & SetFlag::Installed);
			const auto i = single
				? set->emoji.find(single)
				: set->emoji.end();
			const auto list = (i != set->emoji.end())
				? &i->second
				: !single
				? &set->stickers
				: nullptr;
			if (list) {
				result.reserve(result.size() + list->size());
				for (const auto document : *list) {
					const auto sticker = document->sticker();
					if (!sticker) {
						continue;
					} else if (!single) {
						const auto main = Ui::Emoji::Find(sticker->alt);
						if (!main || !all.contains(main)) {
							continue;
						}
					}
					const auto installDate = my ? set->installDate : TimeId(0);
					const auto date = (installDate > 1)
						? InstallDateAdjusted(installDate, document)
						: my
						? CreateMySortKey(document)
						: CreateFeaturedSortKey(document);
					add(document, date);
				}
			}
		}
	};

	addList(setsOrder(), SetFlag::Archived);
	//addList(featuredSetsOrder(), SetFlag::Installed);

	if (!setsToRequest.empty()) {
		for (const auto &[setId, accessHash] : setsToRequest) {
			session().api().scheduleStickerSetRequest(setId, accessHash);
		}
		session().api().requestStickerSets();
	}

	if (forceAllResults || Core::App().settings().suggestStickersByEmoji()) {
		const auto key = ranges::accumulate(
			all,
			QString(),
			ranges::plus(),
			&Ui::Emoji::One::text);
		const auto others = session().api().stickersByEmoji(key);
		if (others) {
			result.reserve(result.size() + others->size());
			for (const auto document : *others) {
				add(document, CreateOtherSortKey(document));
			}
		} else if (!forceAllResults) {
			return {};
		}
	}

	ranges::sort(result, std::greater<>(), &StickerWithDate::date);

	const auto appConfig = &session().appConfig();
	auto mixed = std::vector<not_null<DocumentData*>>();
	mixed.reserve(result.size());
	auto premiumIndex = 0, nonPremiumIndex = 0;
	const auto skipToNext = [&](bool premium) {
		auto &index = premium ? premiumIndex : nonPremiumIndex;
		while (index < result.size()
			&& result[index].document->isPremiumSticker() != premium) {
			++index;
		}
	};
	const auto done = [&](bool premium) {
		skipToNext(premium);
		const auto &index = premium ? premiumIndex : nonPremiumIndex;
		return (index == result.size());
	};
	const auto take = [&](bool premium) {
		if (done(premium)) {
			return false;
		}
		auto &index = premium ? premiumIndex : nonPremiumIndex;
		mixed.push_back(result[index++].document);
		return true;
	};

	if (session().premium()) {
		const auto normalsPerPremium = appConfig->get<int>(
			u"stickers_normal_by_emoji_per_premium_num"_q,
			2);
		do {
			// Add "stickers_normal_by_emoji_per_premium_num" non-premium.
			for (auto i = 0; i < normalsPerPremium; ++i) {
				if (!take(false)) {
					break;
				}
			}
			// Then one premium.
		} while (!done(false) && take(true));

		// Add what's left.
		while (take(false)) {
		}
		while (take(true)) {
		}
	} else {
		// All non-premium.
		while (take(false)) {
		}

		// In the end add "stickers_premium_by_emoji_num" premium.
		const auto premiumsToEnd = appConfig->get<int>(
			u"stickers_premium_by_emoji_num"_q,
			0);
		for (auto i = 0; i < premiumsToEnd; ++i) {
			if (!take(true)) {
				break;
			}
		}
	}

	return mixed;
}

std::optional<std::vector<not_null<EmojiPtr>>> Stickers::getEmojiListFromSet(
		not_null<DocumentData*> document) {
	if (auto sticker = document->sticker()) {
		auto &inputSet = sticker->set;
		if (!inputSet.id) {
			return std::nullopt;
		}
		const auto &sets = this->sets();
		auto it = sets.find(inputSet.id);
		if (it == sets.cend()) {
			return std::nullopt;
		}
		const auto set = it->second.get();
		auto result = std::vector<not_null<EmojiPtr>>();
		for (auto i = set->emoji.cbegin(), e = set->emoji.cend(); i != e; ++i) {
			if (i->second.contains(document)) {
				result.emplace_back(i->first);
			}
		}
		if (result.empty()) {
			return std::nullopt;
		}
		return result;
	}
	return std::nullopt;
}

not_null<StickersSet*> Stickers::feedSet(const MTPStickerSet &info) {
	auto &sets = setsRef();
	const auto &data = info.data();
	auto it = sets.find(data.vid().v);
	auto title = getSetTitle(data);
	auto oldFlags = StickersSetFlags(0);
	auto thumbnailType = StickerType::Webp;
	const auto thumbnail = [&] {
		if (const auto thumbs = data.vthumbs()) {
			for (const auto &thumb : thumbs->v) {
				const auto result = Images::FromPhotoSize(
					&session(),
					data,
					thumb);
				if (result.location.valid()) {
					thumbnailType = Data::ThumbnailTypeFromPhotoSize(thumb);
					return result;
				}
			}
		}
		return ImageWithLocation();
	}();
	const auto flags = ParseStickersSetFlags(data);
	if (it == sets.cend()) {
		it = sets.emplace(data.vid().v, std::make_unique<StickersSet>(
			&owner(),
			data.vid().v,
			data.vaccess_hash().v,
			data.vhash().v,
			title,
			qs(data.vshort_name()),
			data.vcount().v,
			flags | SetFlag::NotLoaded,
			data.vinstalled_date().value_or_empty())).first;
	} else {
		const auto set = it->second.get();
		set->accessHash = data.vaccess_hash().v;
		set->title = title;
		set->shortName = qs(data.vshort_name());
		oldFlags = set->flags;
		const auto clientFlags = set->flags
			& (SetFlag::Featured
				| SetFlag::Unread
				| SetFlag::NotLoaded
				| SetFlag::Special);
		set->flags = flags | clientFlags;
		const auto installDate = data.vinstalled_date();
		set->installDate = installDate
			? (installDate->v ? installDate->v : base::unixtime::now())
			: TimeId(0);
		if (set->count != data.vcount().v
			|| set->hash != data.vhash().v
			|| set->emoji.empty()) {
			// Need to request this data.
			set->count = data.vcount().v;
			set->hash = data.vhash().v;
			set->flags |= SetFlag::NotLoaded;
		}
	}
	const auto set = it->second.get();
	set->setThumbnail(thumbnail, thumbnailType);
	set->thumbnailDocumentId = data.vthumb_document_id().value_or_empty();
	auto changedFlags = (oldFlags ^ set->flags);
	if (changedFlags & SetFlag::Archived) {
		const auto isMasks = (set->type() == StickersType::Masks);
		auto &archivedOrder = isMasks
			? archivedMaskSetsOrderRef()
			: archivedSetsOrderRef();
		const auto index = archivedOrder.indexOf(set->id);
		if (set->flags & SetFlag::Archived) {
			if (index < 0) {
				archivedOrder.push_front(set->id);
			}
		} else if (index >= 0) {
			archivedOrder.removeAt(index);
		}
	}
	return it->second.get();
}

not_null<StickersSet*> Stickers::feedSetFull(
		const MTPDmessages_stickerSet &data) {
	const auto set = feedSet(data.vset());
	feedSetStickers(set, data.vdocuments().v, data.vpacks().v);
	return set;
}

not_null<StickersSet*> Stickers::feedSet(
		const MTPStickerSetCovered &data) {
	const auto set = data.match([&](const auto &data) {
		return feedSet(data.vset());
	});
	data.match([](const MTPDstickerSetCovered &data) {
	}, [&](const MTPDstickerSetNoCovered &data) {
	}, [&](const MTPDstickerSetMultiCovered &data) {
		feedSetCovers(set, data.vcovers().v);
	}, [&](const MTPDstickerSetFullCovered &data) {
		feedSetStickers(set, data.vdocuments().v, data.vpacks().v);
	});
	return set;
}

void Stickers::feedSetStickers(
		not_null<StickersSet*> set,
		const QVector<MTPDocument> &documents,
		const QVector<MTPStickerPack> &packs) {
	set->flags &= ~SetFlag::NotLoaded;

	auto &sets = setsRef();
	const auto wasArchived = [&] {
		const auto it = sets.find(set->id);
		return (it != sets.end())
			&& (it->second->flags & SetFlag::Archived);
	}();

	auto customIt = sets.find(Stickers::CustomSetId);
	const auto inputSet = set->identifier();

	auto pack = StickersPack();
	pack.reserve(documents.size());
	for (const auto &item : documents) {
		const auto document = owner().processDocument(item);
		if (!document->sticker()) {
			continue;
		}

		pack.push_back(document);
		if (!document->sticker()->set.id) {
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
		if (set->stickers.indexOf(i->first) >= 0
			&& pack.indexOf(i->first) < 0) {
			i = recent.erase(i);
			writeRecent = true;
		} else {
			++i;
		}
	}

	const auto isEmoji = (set->type() == StickersType::Emoji);
	const auto isMasks = (set->type() == StickersType::Masks);
	set->stickers = pack;
	set->emoji.clear();
	for (auto i = 0, l = int(packs.size()); i != l; ++i) {
		const auto &pack = packs[i].data();
		if (auto emoji = Ui::Emoji::Find(qs(pack.vemoticon()))) {
			emoji = emoji->original();
			auto &stickers = pack.vdocuments().v;

			auto p = StickersPack();
			p.reserve(stickers.size());
			for (auto j = 0, c = int(stickers.size()); j != c; ++j) {
				const auto document = owner().document(stickers[j].v);
				if (!document->sticker()) {
					continue;
				}
				p.push_back(document);
			}
			set->emoji[emoji] = std::move(p);
		}
	}

	if (writeRecent) {
		session().saveSettings();
	}

	const auto isArchived = !!(set->flags & SetFlag::Archived);
	if ((set->flags & SetFlag::Installed) && !isArchived) {
		if (isEmoji) {
			session().local().writeInstalledCustomEmoji();
		} else if (isMasks) {
			session().local().writeInstalledMasks();
		} else {
			session().local().writeInstalledStickers();
		}
	}
	if (set->flags & SetFlag::Featured) {
		if (isEmoji) {
			session().local().writeFeaturedCustomEmoji();
		} else if (isMasks) {
		} else {
			session().local().writeFeaturedStickers();
		}
	}
	if (wasArchived != isArchived) {
		if (isEmoji) {
		} else if (isMasks) {
			session().local().writeArchivedMasks();
		} else {
			session().local().writeArchivedStickers();
		}
	}
	notifyUpdated(set->type());
}

void Stickers::feedSetCovers(
		not_null<StickersSet*> set,
		const QVector<MTPDocument> &documents) {
	set->covers = StickersPack();
	for (const auto &cover : documents) {
		const auto document = session().data().processDocument(cover);
		if (document->sticker()) {
			set->covers.push_back(document);
		}
	}
}

void Stickers::newSetReceived(const MTPDmessages_stickerSet &set) {
	const auto &s = set.vset().c_stickerSet();
	if (!s.vinstalled_date()) {
		LOG(("API Error: "
			"updateNewStickerSet without install_date flag."));
		return;
	} else if (s.is_archived()) {
		LOG(("API Error: "
			"updateNewStickerSet with archived flag."));
		return;
	}
	auto &order = s.is_emojis()
		? emojiSetsOrderRef()
		: s.is_masks()
		? maskSetsOrderRef()
		: setsOrderRef();
	int32 insertAtIndex = 0, currentIndex = order.indexOf(s.vid().v);
	if (currentIndex != insertAtIndex) {
		if (currentIndex > 0) {
			order.removeAt(currentIndex);
		}
		order.insert(insertAtIndex, s.vid().v);
	}

	feedSetFull(set);
}

QString Stickers::getSetTitle(const MTPDstickerSet &s) {
	auto title = qs(s.vtitle());
	if ((s.vflags().v & MTPDstickerSet::Flag::f_official)
		&& !title.compare(u"Great Minds"_q, Qt::CaseInsensitive)) {
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

StickerType ThumbnailTypeFromPhotoSize(const MTPPhotoSize &size) {
	const auto &type = size.match([&](const auto &data) {
		return data.vtype().v;
	});
	const auto ch = type.isEmpty() ? char() : type[0];
	switch (ch) {
	case 's': return StickerType::Webp;
	case 'a': return StickerType::Tgs;
	case 'v': return StickerType::Webm;
	}
	return StickerType::Webp;
}

} // namespace Stickers
