/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;

namespace Api {

class Authorizations final {
public:
	explicit Authorizations(not_null<ApiWrap*> api);

	struct Entry {
		uint64 hash = 0;

		bool incomplete = false;
		TimeId activeTime = 0;
		QString name, active, info, ip;
	};
	using List = std::vector<Entry>;

	void reload();
	void cancelCurrentRequest();
	void requestTerminate(
		Fn<void(const MTPBool &result)> &&done,
		Fn<void(const MTP::Error &error)> &&fail,
		std::optional<uint64> hash = std::nullopt);

	[[nodiscard]] crl::time lastReceivedTime();

	[[nodiscard]] List list() const;
	[[nodiscard]] rpl::producer<List> listChanges() const;
	[[nodiscard]] int total() const;
	[[nodiscard]] rpl::producer<int> totalChanges() const;

private:
	MTP::Sender _api;
	mtpRequestId _requestId = 0;

	List _list;
	rpl::event_stream<> _listChanges;

	crl::time _lastReceived = 0;

};

} // namespace Api
