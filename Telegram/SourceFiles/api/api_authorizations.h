/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_authorization.h"
#include "mtproto/sender.h"

class ApiWrap;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class Authorizations final {
public:
	explicit Authorizations(not_null<ApiWrap*> api);

	struct Entry {
		uint64 hash = 0;

		bool incomplete = false;
		bool callsDisabled = false;
		int apiId = 0;
		TimeId activeTime = 0;
		QString name, active, info, ip, location, system, platform;
	};
	using List = std::vector<Entry>;

	void reload();
	void cancelCurrentRequest();
	void requestTerminate(
		Fn<void(const MTPBool &result)> &&done,
		Fn<void(const MTP::Error &error)> &&fail,
		std::optional<uint64> hash = std::nullopt);

	void apply(const MTPUpdate &update);
	void reconcileBots(
		const MTPaccount_ConnectedBots &result,
		const std::vector<Data::UnreviewedAuth> &snapshot);

	[[nodiscard]] crl::time lastReceivedTime();

	[[nodiscard]] List list() const;
	[[nodiscard]] rpl::producer<List> listValue() const;
	[[nodiscard]] int total() const;
	[[nodiscard]] rpl::producer<int> totalValue() const;

	[[nodiscard]] const std::vector<Data::UnreviewedAuth> &unreviewed();
	[[nodiscard]] rpl::producer<> unreviewedChanges() const;

	void review(
		const std::vector<Data::UnreviewedAuth> &list,
		bool confirm,
		Fn<void(std::vector<Data::UnreviewedAuth>)> done);

	[[nodiscard]] rpl::producer<bool> reviewingValue() const;

	void updateTTL(int days);
	[[nodiscard]] rpl::producer<int> ttlDays() const;

	void toggleCallsDisabledHere(bool disabled) {
		toggleCallsDisabled(0, disabled);
	}
	void toggleCallsDisabled(uint64 hash, bool disabled);
	[[nodiscard]] bool callsDisabledHere() const;
	[[nodiscard]] rpl::producer<bool> callsDisabledHereValue() const;

	[[nodiscard]] static QString ActiveDateString(TimeId active);

private:
	struct ReviewRequest {
		mtpRequestId id = 0;
		bool confirm = false;
		Data::UnreviewedAuth auth;
		std::vector<Fn<void(bool)>> callbacks;
	};

	void refreshCallsDisabledHereFromCloud();
	void removeExpiredUnreviewed();
	void reconcileAuthorizations(
		const MTPaccount_Authorizations &result,
		const std::vector<Data::UnreviewedAuth> &snapshot);
	void reviewTimedOut();
	void reviewOne(Data::UnreviewedAuth auth, bool confirm);
	void reviewBotLoaded(
		Data::UnreviewedAuth auth,
		bool confirm,
		const MTPaccount_ConnectedBots &result);
	void finishReview(
		Data::UnreviewedAuth auth,
		bool success,
		bool resolved = false);
	void refreshBotNames();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	mtpRequestId _requestId = 0;

	List _list;
	rpl::event_stream<> _listChanges;

	Fn<int()> _autoconfirmPeriod;
	std::vector<Data::UnreviewedAuth> _unreviewed;
	rpl::event_stream<> _unreviewedChanges;
	Fn<void()> _saveUnreviewed;
	base::Timer _unreviewedTimer;

	mtpRequestId _ttlRequestId = 0;
	rpl::variable<int> _ttlDays = 0;

	base::flat_map<uint64, mtpRequestId> _toggleCallsDisabledRequests;
	base::flat_map<std::pair<uint64, uint64>, ReviewRequest> _reviewRequests;
	base::flat_map<std::pair<uint64, uint64>, TimeId> _reviewed;
	rpl::variable<bool> _reviewing = false;
	base::Timer _reviewTimer;
	rpl::variable<bool> _callsDisabledHere;

	crl::time _lastReceived = 0;
	rpl::lifetime _lifetime;

};

} // namespace Api
