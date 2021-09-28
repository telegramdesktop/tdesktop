/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/timer.h"

class ApiWrap;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class ViewsManager final {
public:
	explicit ViewsManager(not_null<ApiWrap*> api);

	void scheduleIncrement(not_null<HistoryItem*> item);
	void removeIncremented(not_null<PeerData*> peer);

private:
	void viewsIncrement();

	void done(
		QVector<MTPint> ids,
		const MTPmessages_MessageViews &result,
		mtpRequestId requestId);
	void fail(const MTP::Error &error, mtpRequestId requestId);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	base::flat_map<not_null<PeerData*>, base::flat_set<MsgId>> _incremented;
	base::flat_map<not_null<PeerData*>, base::flat_set<MsgId>> _toIncrement;
	base::flat_map<not_null<PeerData*>, mtpRequestId> _incrementRequests;
	base::flat_map<mtpRequestId, not_null<PeerData*>> _incrementByRequest;
	base::Timer _incrementTimer;

};

} // namespace Api
