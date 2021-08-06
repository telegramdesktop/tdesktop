/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "data/stickers/data_stickers_set.h"
#include "settings.h"

class HistoryItem;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
class DocumentMedia;

class Stickers final {
public:
	explicit Stickers(not_null<Session*> owner);

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;

	// for backward compatibility
	static constexpr auto DefaultSetId = 0;
	static constexpr auto CustomSetId = 0xFFFFFFFFFFFFFFFFULL;

	// For stickers panel, should not appear in Sets.
	static constexpr auto RecentSetId = 0xFFFFFFFFFFFFFFFEULL;
	static constexpr auto NoneSetId = 0xFFFFFFFFFFFFFFFDULL;
	static constexpr auto FeaturedSetId = 0xFFFFFFFFFFFFFFFBULL;

	// For cloud-stored recent stickers.
	static constexpr auto CloudRecentSetId = 0xFFFFFFFFFFFFFFFCULL;
	static constexpr auto CloudRecentAttachedSetId = 0xFFFFFFFFFFFFFFF9ULL;

	// For cloud-stored faved stickers.
	static constexpr auto FavedSetId = 0xFFFFFFFFFFFFFFFAULL;

	// For setting up megagroup sticker set.
	static constexpr auto MegagroupSetId = 0xFFFFFFFFFFFFFFEFULL;

	enum Recent {
		Regular,
		Attached,
	};

	void notifyUpdated();
	[[nodiscard]] rpl::producer<> updated() const;
	void notifyRecentUpdated(Recent recent = Recent::Regular);
	[[nodiscard]] rpl::producer<Recent> recentUpdated() const;
	void notifySavedGifsUpdated();
	[[nodiscard]] rpl::producer<> savedGifsUpdated() const;
	void notifyStickerSetInstalled(uint64 setId);
	[[nodiscard]] rpl::producer<uint64> stickerSetInstalled() const;

	void incrementSticker(not_null<DocumentData*> document);

	bool updateNeeded(crl::time now) const {
		return updateNeeded(_lastUpdate, now);
	}
	void setLastUpdate(crl::time update) {
		_lastUpdate = update;
	}
	bool recentUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastRecentUpdate, now);
	}
	void setLastRecentUpdate(crl::time update) {
		if (update) {
			notifyRecentUpdated();
		}
		_lastRecentUpdate = update;
	}
	bool masksUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastMasksUpdate, now);
	}
	void setLastMasksUpdate(crl::time update) {
		_lastMasksUpdate = update;
	}
	bool recentAttachedUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastRecentAttachedUpdate, now);
	}
	void setLastRecentAttachedUpdate(crl::time update) {
		if (update) {
			notifyRecentUpdated(Recent::Attached);
		}
		_lastRecentAttachedUpdate = update;
	}
	bool favedUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastFavedUpdate, now);
	}
	void setLastFavedUpdate(crl::time update) {
		_lastFavedUpdate = update;
	}
	bool featuredUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastFeaturedUpdate, now);
	}
	void setLastFeaturedUpdate(crl::time update) {
		_lastFeaturedUpdate = update;
	}
	bool savedGifsUpdateNeeded(crl::time now) const {
		return updateNeeded(_lastSavedGifsUpdate, now);
	}
	void setLastSavedGifsUpdate(crl::time update) {
		_lastSavedGifsUpdate = update;
	}
	int featuredSetsUnreadCount() const {
		return _featuredSetsUnreadCount.current();
	}
	void setFeaturedSetsUnreadCount(int count) {
		_featuredSetsUnreadCount = count;
	}
	[[nodiscard]] rpl::producer<int> featuredSetsUnreadCountValue() const {
		return _featuredSetsUnreadCount.value();
	}
	const StickersSets &sets() const {
		return _sets;
	}
	StickersSets &setsRef() {
		return _sets;
	}
	const StickersSetsOrder &setsOrder() const {
		return _setsOrder;
	}
	StickersSetsOrder &setsOrderRef() {
		return _setsOrder;
	}
	const StickersSetsOrder &maskSetsOrder() const {
		return _maskSetsOrder;
	}
	StickersSetsOrder &maskSetsOrderRef() {
		return _maskSetsOrder;
	}
	const StickersSetsOrder &featuredSetsOrder() const {
		return _featuredSetsOrder;
	}
	StickersSetsOrder &featuredSetsOrderRef() {
		return _featuredSetsOrder;
	}
	const StickersSetsOrder &archivedSetsOrder() const {
		return _archivedSetsOrder;
	}
	StickersSetsOrder &archivedSetsOrderRef() {
		return _archivedSetsOrder;
	}
	const StickersSetsOrder &archivedMaskSetsOrder() const {
		return _archivedMaskSetsOrder;
	}
	StickersSetsOrder &archivedMaskSetsOrderRef() {
		return _archivedMaskSetsOrder;
	}
	const SavedGifs &savedGifs() const {
		return _savedGifs;
	}
	SavedGifs &savedGifsRef() {
		return _savedGifs;
	}
	void removeFromRecentSet(not_null<DocumentData*> document);

	void addSavedGif(not_null<DocumentData*> document);
	void checkSavedGif(not_null<HistoryItem*> item);

	void applyArchivedResult(
		const MTPDmessages_stickerSetInstallResultArchive &d);
	bool applyArchivedResultFake(); // For testing.
	void installLocally(uint64 setId);
	void undoInstallLocally(uint64 setId);
	bool isFaved(not_null<const DocumentData*> document);
	void setFaved(not_null<DocumentData*> document, bool faved);

	void setsReceived(const QVector<MTPStickerSet> &data, int32 hash);
	void specialSetReceived(
		uint64 setId,
		const QString &setTitle,
		const QVector<MTPDocument> &items,
		int32 hash,
		const QVector<MTPStickerPack> &packs = QVector<MTPStickerPack>(),
		const QVector<MTPint> &usageDates = QVector<MTPint>());
	void featuredSetsReceived(
		const QVector<MTPStickerSetCovered> &list,
		const QVector<MTPlong> &unread,
		int32 hash);
	void gifsReceived(const QVector<MTPDocument> &items, int32 hash);

	std::vector<not_null<DocumentData*>> getListByEmoji(
		not_null<EmojiPtr> emoji,
		uint64 seed);
	std::optional<std::vector<not_null<EmojiPtr>>> getEmojiListFromSet(
		not_null<DocumentData*> document);

	StickersSet *feedSet(const MTPDstickerSet &data);
	StickersSet *feedSetFull(const MTPmessages_StickerSet &data);
	void newSetReceived(const MTPmessages_StickerSet &data);

	QString getSetTitle(const MTPDstickerSet &s);

	RecentStickerPack &getRecentPack() const;

private:
	bool updateNeeded(crl::time lastUpdate, crl::time now) const {
		constexpr auto kUpdateTimeout = crl::time(3600'000);
		return (lastUpdate == 0)
			|| (now >= lastUpdate + kUpdateTimeout);
	}
	void checkFavedLimit(StickersSet &set);
	void setIsFaved(
		not_null<DocumentData*> document,
		std::optional<std::vector<not_null<EmojiPtr>>> emojiList
			= std::nullopt);
	void setIsNotFaved(not_null<DocumentData*> document);
	void pushFavedToFront(
		StickersSet &set,
		not_null<DocumentData*> document,
		const std::vector<not_null<EmojiPtr>> &emojiList);
	void moveFavedToFront(StickersSet &set, int index);
	void requestSetToPushFaved(not_null<DocumentData*> document);
	void setPackAndEmoji(
		StickersSet &set,
		StickersPack &&pack,
		const std::vector<TimeId> &&dates,
		const QVector<MTPStickerPack> &packs);

	const not_null<Session*> _owner;
	rpl::event_stream<> _updated;
	rpl::event_stream<Recent> _recentUpdated;
	rpl::event_stream<> _savedGifsUpdated;
	rpl::event_stream<uint64> _stickerSetInstalled;
	crl::time _lastUpdate = 0;
	crl::time _lastRecentUpdate = 0;
	crl::time _lastFavedUpdate = 0;
	crl::time _lastFeaturedUpdate = 0;
	crl::time _lastSavedGifsUpdate = 0;
	crl::time _lastMasksUpdate = 0;
	crl::time _lastRecentAttachedUpdate = 0;
	rpl::variable<int> _featuredSetsUnreadCount = 0;
	StickersSets _sets;
	StickersSetsOrder _setsOrder;
	StickersSetsOrder _maskSetsOrder;
	StickersSetsOrder _featuredSetsOrder;
	StickersSetsOrder _archivedSetsOrder;
	StickersSetsOrder _archivedMaskSetsOrder;
	SavedGifs _savedGifs;

};

} // namespace Data
