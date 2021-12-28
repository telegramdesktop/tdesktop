/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Data {

class DocumentMedia;
class Session;

struct Reaction {
	QString emoji;
	QString title;
	not_null<DocumentData*> staticIcon;
	not_null<DocumentData*> appearAnimation;
	not_null<DocumentData*> selectAnimation;
	not_null<DocumentData*> activateAnimation;
	not_null<DocumentData*> activateEffects;
	bool active = false;
};

class Reactions final {
public:
	explicit Reactions(not_null<Session*> owner);

	void refresh();

	enum class Type {
		Active,
		All,
	};
	[[nodiscard]] const std::vector<Reaction> &list(Type type) const;
	[[nodiscard]] std::vector<Reaction> list(not_null<PeerData*> peer) const;

	[[nodiscard]] static std::vector<Reaction> Filtered(
		const std::vector<Reaction> &reactions,
		const std::vector<QString> &emoji);
	[[nodiscard]] std::vector<Reaction> filtered(
		const std::vector<QString> &emoji) const;

	[[nodiscard]] static std::vector<QString> ParseAllowed(
		const MTPVector<MTPstring> *list);

	[[nodiscard]] rpl::producer<> updates() const;

	enum class ImageSize {
		BottomInfo,
		InlineList,
	};
	void preloadImageFor(const QString &emoji);
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
	};

	void request();

	[[nodiscard]] std::optional<Reaction> parse(
		const MTPAvailableReaction &entry);

	void loadImage(ImageSet &set, not_null<DocumentData*> document);
	void setImage(ImageSet &set, QImage large);
	void resolveImages();
	void downloadTaskFinished();

	void repaintCollected();
	void pollCollected();

	const not_null<Session*> _owner;

	std::vector<Reaction> _active;
	std::vector<Reaction> _available;
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

	rpl::lifetime _lifetime;

};

class MessageReactions final {
public:
	explicit MessageReactions(not_null<HistoryItem*> item);

	void add(const QString &reaction);
	void remove();
	void set(const QVector<MTPReactionCount> &list, bool ignoreChosen);
	[[nodiscard]] const base::flat_map<QString, int> &list() const;
	[[nodiscard]] QString chosen() const;
	[[nodiscard]] bool empty() const;

private:
	const not_null<HistoryItem*> _item;

	QString _chosen;
	base::flat_map<QString, int> _list;

};

} // namespace Data
