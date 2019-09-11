/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

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
