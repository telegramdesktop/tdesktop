/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"
#include "mtproto/sender.h"
#include "rpl/variable.h"

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

enum class UserAddressState : uchar {
	Unknown,
	Absent,
	Known,
};

struct UserAddress {
	UserAddressState state = UserAddressState::Unknown;
	QString address;

	friend bool operator==(const UserAddress &, const UserAddress &) = default;
};

struct ForceResolveError {
	QString type;
	bool silent = false;
};

class UserAddresses final {
public:
	explicit UserAddresses(not_null<Main::Session*> session);

	// Asks Telegram for the addresses of |ids| this session has no answer
	// for yet, in calls of at most 100, and runs |done| once when the last
	// of them settles. |done| runs exactly once for any input, including an
	// empty one and one every id of which is already answered or cannot be
	// turned into an InputUser — and in those cases it runs before
	// resolve() returns, so a caller must tolerate a synchronous
	// completion. It does not run at all if the session is destroyed first,
	// which retires the requests with it. Read the answer back with
	// known(); |done| carries no payload because the store is the answer.
	void resolve(std::vector<UserId> ids, Fn<void()> done);

	// Explicitly asks Telegram for one eligible user's address, even when
	// known() already has an answer. The sender must be Ready with positive
	// balance. Local refusal is synchronous: WALLET_UNAVAILABLE,
	// WALLET_USER_INVALID, WALLET_USER_INELIGIBLE, WALLET_NOT_READY or
	// WALLET_BALANCE_EMPTY. Invalid replies fail with WALLET_ADDRESS_INVALID;
	// RPC failures retain their type and silent disposition and are terminal.
	// Only a validated nonempty canonical address reaches |done|, after it
	// is published through known(). Earlier passive lookups and full-user
	// requests cannot overwrite it; later full-user requests can update it.
	// Failure preserves the store and carries no destination.
	// Destruction retires pending requests without running either callback.
	void forceResolve(
		UserId id,
		Fn<void(QString)> done,
		Fn<void(ForceResolveError)> fail);
	[[nodiscard]] QString forceResolveError(UserId id) const;

	// Unknown for an id no source has answered for, including one
	// Data::Session cannot hand back, which is never sent. A chunk already
	// in flight never overwrites an answer that landed after it was sent.
	[[nodiscard]] UserAddress known(UserId id) const;

	// True once the API refused this account. That is a fact about the
	// account, not an answer about any user, so no id is recorded Absent
	// because of it and nothing is asked again this session.
	[[nodiscard]] bool unavailable() const;
	[[nodiscard]] rpl::producer<bool> unavailableValue() const;

private:
	struct Job {
		Fn<void()> done;
		int chunks = 0;
	};

	void sendChunk(const std::shared_ptr<Job> &job, std::vector<UserId> ids);
	void applyChunk(
		const std::vector<UserId> &asked,
		const QVector<MTPWalletUserAddress> &reply);
	void finishChunk(const std::shared_ptr<Job> &job);
	void finish(const std::shared_ptr<Job> &job);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	rpl::variable<bool> _unavailable = false;

};

} // namespace Wallet
