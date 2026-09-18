/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "wallet/wallet_session.h"

struct TextWithEntities;

namespace Main {
class SessionShow;
} // namespace Main

namespace Wallet {

[[nodiscard]] TextWithEntities TransferCommentCover(const TransferItem &item);

class TransferComment final : public base::has_weak_ptr {
public:
	TransferComment(
		not_null<Main::Session*> session,
		TransferItem target,
		Fn<bool()> originCurrent);
	~TransferComment();

	void activate(std::shared_ptr<Main::SessionShow> show);
	void reset();
	[[nodiscard]] bool pending() const;
	[[nodiscard]] const std::optional<QString> &plaintext() const;
	[[nodiscard]] rpl::producer<> changes() const;

private:
	[[nodiscard]] bool originCurrent() const;
	[[nodiscard]] bool attemptCurrent(uint64 revision) const;
	[[nodiscard]] bool targetIsForeign() const;
	// The press itself, re-entered by the ladder this class drives: the
	// conflict box coming back, and the resolver once it has something to
	// try again with. Only activate() starts a new press, and only a new
	// press is entitled to a fresh wait budget.
	void activateAttempt(std::shared_ptr<Main::SessionShow> show);
	void validate();
	// Nothing outside the Wallet window asks the server for the wallet's
	// state, so a first press in a chat finds no identity at all. That is
	// not a verdict about the comment: the press asks, waits, and tries
	// again with the answer.
	void resolveWallet(std::shared_ptr<Main::SessionShow> show);
	void stopResolving();
	void finish(
		uint64 revision,
		std::shared_ptr<Main::SessionShow> show,
		CommentDecryptResult result);
	void clear();

	base::weak_ptr<Main::Session> _session;
	const TransferItem _target;
	const Fn<bool()> _originCurrent;
	std::shared_ptr<CommentScope> _scope;
	Fn<void()> _closeBusy;
	std::optional<QString> _plaintext;
	rpl::event_stream<> _changes;
	uint64 _revision = 0;
	int _resolveAttempts = 0;
	bool _pending = false;
	bool _resolving = false;
	rpl::lifetime _resolve;
	rpl::lifetime _attempt;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
