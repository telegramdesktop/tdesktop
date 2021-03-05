/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class PeerData;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Calls {

class ChooseJoinAsProcess final {
public:
	ChooseJoinAsProcess() = default;
	~ChooseJoinAsProcess();

	enum class Context {
		Create,
		Join,
		Switch,
	};

	void start(
		not_null<PeerData*> peer,
		Context context,
		Fn<void(not_null<PeerData*> peer, not_null<PeerData*> joinAs)> done);

private:
	struct ChannelsListRequest {
		not_null<PeerData*> peer;
		Fn<void(not_null<PeerData*>, not_null<PeerData*>)> done;
		base::has_weak_ptr guard;
		QPointer<Ui::BoxContent> box;
		rpl::lifetime lifetime;
		Context context = Context();
		mtpRequestId id = 0;
	};
	std::unique_ptr<ChannelsListRequest> _request;

};

} // namespace Calls
