/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/qt/qt_compare.h"
#include "data/data_message_reaction_id.h"

class HistoryItem;
class History;
class PeerData;

namespace Data {
struct ReactionId;
} // namespace Data

namespace Api {

struct FoundMessages {
	int total = -1;
	MessageIdsList messages;
	QString nextToken;
};

class MessagesSearch final {
public:
	struct Request {
		QString query;
		PeerData *from = nullptr;
		std::vector<Data::ReactionId> tags;
		MsgId topMsgId;

		friend inline bool operator==(
			const Request &,
			const Request &) = default;
		friend inline auto operator<=>(
			const Request &,
			const Request &) = default;
	};

	explicit MessagesSearch(not_null<History*> history);
	~MessagesSearch();

	void searchMessages(Request request);
	void searchMore();

	[[nodiscard]] rpl::producer<FoundMessages> messagesFounds() const;

private:
	using TLMessages = MTPmessages_Messages;
	void searchRequest();
	void searchReceived(
		const TLMessages &result,
		mtpRequestId requestId,
		const QString &nextToken);

	const not_null<History*> _history;

	base::flat_map<QString, TLMessages> _cacheOfStartByToken;

	Request _request;
	MsgId _offsetId;

	int _searchInHistoryRequest = 0; // Not real mtpRequestId.
	mtpRequestId _requestId = 0;

	rpl::event_stream<FoundMessages> _messagesFounds;

};

} // namespace Api
