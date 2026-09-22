/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "mtproto/sender.h"
#include "wallet/wallet_ton_connect.h"

class HistoryItem;

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Wallet {

class TonConnectRequests final : public base::has_weak_ptr {
public:
	TonConnectRequests(
		not_null<Main::Session*> session,
		not_null<TonConnect*> store);
	~TonConnectRequests();

	void open(
		not_null<Window::SessionController*> controller,
		FullMsgId itemId);
	void openPending(
		not_null<Window::SessionController*> controller,
		const QString &dappClientId);
	void stop();

private:
	class Flow;
	struct Entry {
		TonConnectSessionId sessionId = 0;
		MsgId msgId = 0;
		QString topic;
		TimeId expires = 0;
		uint64 order = 0;
		bool chosen = false;
	};

	[[nodiscard]] static std::optional<Entry> PendingEntry(
		not_null<HistoryItem*> item);
	[[nodiscard]] static bool OpensByItself(const Entry &entry);
	[[nodiscard]] Window::SessionController *autoWindow() const;
	void arrived(not_null<HistoryItem*> item);
	void edited(not_null<HistoryItem*> item);
	void showNext();
	void opened(
		Entry entry,
		not_null<Window::SessionController*> controller);
	void start(
		Entry entry,
		base::weak_ptr<Window::SessionController> controller,
		std::shared_ptr<Main::SessionShow> show);
	void startSilent(Entry entry);
	[[nodiscard]] bool silentOwns(MsgId msgId) const;
	void pendingLoaded(
		base::weak_ptr<Window::SessionController> controller,
		const MTPwallet_TonConnectPending &result);
	void flowDone(not_null<Flow*> flow, bool claimed);

	const not_null<Main::Session*> _session;
	const not_null<TonConnect*> _store;
	MTP::Sender _api;
	std::unique_ptr<Flow> _active;
	std::vector<std::unique_ptr<Flow>> _silent;
	std::vector<Entry> _waiting;
	base::flat_set<MsgId> _claimedIds;
	mtpRequestId _pendingRequestId = 0;
	uint64 _order = 0;
	bool _stopped = false;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
