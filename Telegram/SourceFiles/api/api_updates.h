/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_pts_waiter.h"
#include "base/timer.h"

class RPCError;
class ApiWrap;
class History;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class Updates final {
public:
	explicit Updates(not_null<Main::Session*> session);

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] ApiWrap &api() const;

	void applyUpdates(
		const MTPUpdates &updates,
		uint64 sentMessageRandomId = 0);
	void applyUpdatesNoPtsCheck(const MTPUpdates &updates);
	void applyUpdateNoPtsCheck(const MTPUpdate &update);

	[[nodiscard]] int32 pts() const;

	void updateOnline();
	[[nodiscard]] bool isIdle() const;
	void checkIdleFinish();
	bool lastWasOnline() const;
	crl::time lastSetOnline() const;
	bool isQuitPrevent();

	bool updateAndApply(int32 pts, int32 ptsCount, const MTPUpdates &updates);
	bool updateAndApply(int32 pts, int32 ptsCount, const MTPUpdate &update);
	bool updateAndApply(int32 pts, int32 ptsCount);

	void checkLastUpdate(bool afterSleep);

	// ms <= 0 - stop timer
	void ptsWaiterStartTimerFor(ChannelData *channel, crl::time ms);

	void getDifference();
	void requestChannelRangeDifference(not_null<History*> history);

	void addActiveChat(rpl::producer<PeerData*> chat);

private:
	enum class ChannelDifferenceRequest {
		Unknown,
		PtsGapOrShortPoll,
		AfterFail,
	};

	struct ActiveChatTracker {
		PeerData *peer = nullptr;
		rpl::lifetime lifetime;
	};

	void channelRangeDifferenceSend(
		not_null<ChannelData*> channel,
		MsgRange range,
		int32 pts);
	void channelRangeDifferenceDone(
		not_null<ChannelData*> channel,
		MsgRange range,
		const MTPupdates_ChannelDifference &result);

	void updateOnline(bool gotOtherOffline);
	void sendPing();
	void getDifferenceByPts();
	void getDifferenceAfterFail();

	[[nodiscard]] bool requestingDifference() const {
		return _ptsWaiter.requesting();
	}
	void getChannelDifference(
		not_null<ChannelData*> channel,
		ChannelDifferenceRequest from = ChannelDifferenceRequest::Unknown);
	void differenceDone(const MTPupdates_Difference &result);
	void differenceFail(const RPCError &error);
	void feedDifference(
		const MTPVector<MTPUser> &users,
		const MTPVector<MTPChat> &chats,
		const MTPVector<MTPMessage> &msgs,
		const MTPVector<MTPUpdate> &other);
	void stateDone(const MTPupdates_State &state);
	void setState(int32 pts, int32 date, int32 qts, int32 seq);
	void channelDifferenceDone(
		not_null<ChannelData*> channel,
		const MTPupdates_ChannelDifference &diff);
	void channelDifferenceFail(
		not_null<ChannelData*> channel,
		const RPCError &error);
	void failDifferenceStartTimerFor(ChannelData *channel);
	void feedChannelDifference(const MTPDupdates_channelDifference &data);

	void mtpUpdateReceived(const MTPUpdates &updates);
	void mtpNewSessionCreated();
	void feedUpdateVector(
		const MTPVector<MTPUpdate> &updates,
		bool skipMessageIds = false);
	// Doesn't call sendHistoryChangeNotifications itself.
	void feedMessageIds(const MTPVector<MTPUpdate> &updates);
	// Doesn't call sendHistoryChangeNotifications itself.
	void feedUpdate(const MTPUpdate &update);

	bool whenGetDiffChanged(
		ChannelData *channel,
		int32 ms,
		base::flat_map<not_null<ChannelData*>, crl::time> &whenMap,
		crl::time &curTime);

	void handleSendActionUpdate(
		PeerId peerId,
		MsgId rootId,
		UserId userId,
		const MTPSendMessageAction &action);

	const not_null<Main::Session*> _session;

	int32 _updatesDate = 0;
	int32 _updatesQts = -1;
	int32 _updatesSeq = 0;
	base::Timer _noUpdatesTimer;
	base::Timer _onlineTimer;

	PtsWaiter _ptsWaiter;

	base::flat_map<not_null<ChannelData*>, crl::time> _whenGetDiffByPts;
	base::flat_map<not_null<ChannelData*>, crl::time> _whenGetDiffAfterFail;
	crl::time _getDifferenceTimeByPts = 0;
	crl::time _getDifferenceTimeAfterFail = 0;

	base::Timer _byPtsTimer;

	base::flat_map<int32, MTPUpdates> _bySeqUpdates;
	base::Timer _bySeqTimer;

	base::Timer _byMinChannelTimer;

	// growing timeout for getDifference calls, if it fails
	crl::time _failDifferenceTimeout = 1;
	// growing timeout for getChannelDifference calls, if it fails
	base::flat_map<
		not_null<ChannelData*>,
		crl::time> _channelFailDifferenceTimeout;
	base::Timer _failDifferenceTimer;

	base::flat_map<
		not_null<ChannelData*>,
		mtpRequestId> _rangeDifferenceRequests;

	crl::time _lastUpdateTime = 0;
	bool _handlingChannelDifference = false;

	base::flat_map<int, ActiveChatTracker> _activeChats;
	base::flat_map<
		not_null<PeerData*>,
		base::flat_map<UserId, crl::time>> _pendingSpeakingCallMembers;

	mtpRequestId _onlineRequest = 0;
	base::Timer _idleFinishTimer;
	crl::time _lastSetOnline = 0;
	bool _lastWasOnline = false;
	bool _isIdle = false;

	rpl::lifetime _lifetime;

};

} // namespace Api
