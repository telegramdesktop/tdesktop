/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

class Session;

struct Reaction {
	QString emoji;
	QString title;
	not_null<DocumentData*> staticIcon;
	not_null<DocumentData*> selectAnimation;
	not_null<DocumentData*> activateAnimation;
	not_null<DocumentData*> activateEffects;
};

class Reactions final {
public:
	explicit Reactions(not_null<Session*> owner);

	[[nodiscard]] const std::vector<Reaction> &list() const;
	[[nodiscard]] std::vector<Reaction> list(not_null<PeerData*> peer) const;

	[[nodiscard]] static std::vector<Reaction> Filtered(
		const std::vector<Reaction> &reactions,
		const std::vector<QString> &emoji);
	[[nodiscard]] std::vector<Reaction> filtered(
		const std::vector<QString> &emoji) const;

	[[nodiscard]] static std::vector<QString> ParseAllowed(
		const MTPVector<MTPstring> *list);

	[[nodiscard]] rpl::producer<> updates() const;

private:
	void request();

	[[nodiscard]] std::optional<Reaction> parse(
		const MTPAvailableReaction &entry);

	const not_null<Session*> _owner;

	std::vector<Reaction> _available;
	rpl::event_stream<> _updated;

	mtpRequestId _requestId = 0;
	int32 _hash = 0;

	rpl::lifetime _lifetime;

};

class MessageReactions final {
public:
	static std::vector<QString> SuggestList();

	explicit MessageReactions(not_null<HistoryItem*> item);

	void add(const QString &reaction);
	void set(const QVector<MTPReactionCount> &list, bool ignoreChosen);
	[[nodiscard]] const base::flat_map<QString, int> &list() const;
	[[nodiscard]] QString chosen() const;

private:
	void sendRequest();

	const not_null<HistoryItem*> _item;

	QString _chosen;
	base::flat_map<QString, int> _list;
	mtpRequestId _requestId = 0;

};

} // namespace Data
