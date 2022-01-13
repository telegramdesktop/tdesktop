/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Data {

class DocumentMedia;
class Session;

struct Reaction {
	QString emoji;
	QString title;
	not_null<DocumentData*> staticIcon;
	not_null<DocumentData*> appearAnimation;
	not_null<DocumentData*> selectAnimation;
	//not_null<DocumentData*> activateAnimation;
	//not_null<DocumentData*> activateEffects;
	DocumentData *centerIcon = nullptr;
	DocumentData *aroundAnimation = nullptr;
	bool active = false;
};

class Reactions final {
public:
	explicit Reactions(not_null<Session*> owner);
	~Reactions();

	void refresh();

	enum class Type {
		Active,
		All,
	};
	[[nodiscard]] const std::vector<Reaction> &list(Type type) const;
	[[nodiscard]] QString favorite() const;
	void setFavorite(const QString &emoji);

	[[nodiscard]] static base::flat_set<QString> ParseAllowed(
		const MTPVector<MTPstring> *list);

	[[nodiscard]] rpl::producer<> updates() const;

	enum class ImageSize {
		BottomInfo,
		InlineList,
	};
	void preloadImageFor(const QString &emoji);
	void preloadAnimationsFor(const QString &emoji);
	[[nodiscard]] QImage resolveImageFor(
		const QString &emoji,
		ImageSize size);

	void send(not_null<HistoryItem*> item, const QString &chosen);
	[[nodiscard]] bool sending(not_null<HistoryItem*> item) const;

	void poll(not_null<HistoryItem*> item, crl::time now);

	void updateAllInHistory(not_null<PeerData*> peer, bool enabled);

private:
	struct ImageSet {
		QImage bottomInfo;
		QImage inlineList;
		std::shared_ptr<DocumentMedia> media;
		std::unique_ptr<Lottie::Icon> icon;
		bool fromAppearAnimation = false;
	};

	void request();

	[[nodiscard]] std::optional<Reaction> parse(
		const MTPAvailableReaction &entry);

	void loadImage(
		ImageSet &set,
		not_null<DocumentData*> document,
		bool fromAppearAnimation);
	void setLottie(ImageSet &set);
	void resolveImages();
	void downloadTaskFinished();

	void repaintCollected();
	void pollCollected();

	const not_null<Session*> _owner;

	std::vector<Reaction> _active;
	std::vector<Reaction> _available;
	QString _favorite;
	base::flat_map<
		not_null<DocumentData*>,
		std::shared_ptr<Data::DocumentMedia>> _iconsCache;
	rpl::event_stream<> _updated;

	mtpRequestId _requestId = 0;
	int32 _hash = 0;

	base::flat_map<QString, ImageSet> _images;
	rpl::lifetime _imagesLoadLifetime;
	bool _waitingForList = false;

	base::flat_map<FullMsgId, mtpRequestId> _sentRequests;

	base::flat_map<not_null<HistoryItem*>, crl::time> _repaintItems;
	base::Timer _repaintTimer;
	base::flat_set<not_null<HistoryItem*>> _pollItems;
	base::flat_set<not_null<HistoryItem*>> _pollingItems;
	mtpRequestId _pollRequestId = 0;

	mtpRequestId _saveFaveRequestId = 0;

	rpl::lifetime _lifetime;

};

class MessageReactions final {
public:
	explicit MessageReactions(not_null<HistoryItem*> item);

	void add(const QString &reaction);
	void remove();
	void set(
		const QVector<MTPReactionCount> &list,
		const QVector<MTPMessageUserReaction> &recent,
		bool ignoreChosen);
	[[nodiscard]] const base::flat_map<QString, int> &list() const;
	[[nodiscard]] auto recent() const
		-> const base::flat_map<QString, std::vector<not_null<UserData*>>> &;
	[[nodiscard]] QString chosen() const;
	[[nodiscard]] bool empty() const;

private:
	const not_null<HistoryItem*> _item;

	QString _chosen;
	base::flat_map<QString, int> _list;
	base::flat_map<QString, std::vector<not_null<UserData*>>> _recent;

};

} // namespace Data
