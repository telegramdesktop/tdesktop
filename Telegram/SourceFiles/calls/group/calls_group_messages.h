/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Data {
class GroupCall;
} // namespace Data

namespace MTP {
class Sender;
struct Response;
} // namespace MTP

namespace Calls::Group {

struct Message {
	uint64 randomId = 0;
	TimeId date = 0;
	not_null<PeerData*> peer;
	TextWithEntities text;
	bool failed = false;
};

class Messages final {
public:
	Messages(not_null<GroupCall*> call, not_null<MTP::Sender*> api);

	void send(TextWithTags text);

	void received(const MTPDupdateGroupCallMessage &data);
	void received(const MTPDupdateGroupCallEncryptedMessage &data);

	[[nodiscard]] rpl::producer<std::vector<Message>> listValue() const;

private:
	[[nodiscard]] bool ready() const;
	void sendPending();
	void pushChanges();
	void checkDestroying(bool afterChanges = false);

	void received(
		uint64 randomId,
		const MTPPeer &from,
		const MTPTextWithEntities &message,
		bool checkCustomEmoji = false);
	void sent(uint64 randomId, const MTP::Response &response);
	void failed(uint64 randomId, const MTP::Response &response);

	const not_null<GroupCall*> _call;
	const not_null<MTP::Sender*> _api;

	Data::GroupCall *_real = nullptr;

	std::vector<TextWithTags> _pending;

	base::Timer _destroyTimer;
	std::vector<Message> _messages;
	rpl::event_stream<std::vector<Message>> _changes;

	TimeId _ttl = 0;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
