/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "wallet/wallet_session.h"

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Wallet {

inline constexpr auto kSendRefusalRetries = 3;
inline constexpr auto kSigningReadyTimeout = 30 * crl::time(1000);

struct SendCallbacks {
	Fn<void(SendStarted)> started;
	Fn<void(SendError)> sent;
	Fn<void(std::string)> preparing;
};

struct SendPreparationDescriptor {
	std::shared_ptr<Main::SessionShow> show;
	Fn<bool()> current;
	Fn<std::optional<SendArgs>()> args;
	Fn<bool(SendError error, std::optional<int64> fee)> blocked;
	rpl::producer<> invalidated = rpl::never<>();
	Fn<void(SendStarted)> started;
	Fn<void(SendError)> sent;
	Fn<SendCallbacks(SendCallbacks)> attempt;
};

class SendPreparation final : public base::has_weak_ptr {
public:
	explicit SendPreparation(SendPreparationDescriptor &&descriptor);

	[[nodiscard]] std::optional<int64> fee() const;
	[[nodiscard]] rpl::producer<std::optional<int64>> feeValue() const;
	[[nodiscard]] SendError error() const;
	[[nodiscard]] rpl::producer<SendError> errorValue() const;
	[[nodiscard]] rpl::producer<bool> estimatingValue() const;
	[[nodiscard]] bool sending() const;
	[[nodiscard]] rpl::producer<bool> sendingValue() const;
	[[nodiscard]] bool submitted() const;
	[[nodiscard]] bool pressable() const;

	void start();
	void estimate();
	void press();
	void refused(SendError error);
	void fail(SendError error);
	void cancel();

private:
	void requote();
	void estimated(
		uint64 revision,
		const SendArgs &args,
		bool signing,
		FeeResult result);
	void proceed();
	void acquireKey();
	void awaitSigning();
	void submit();
	void stop();

	const std::shared_ptr<Main::SessionShow> _show;
	const not_null<Main::Session*> _session;
	const not_null<Session*> _wallet;
	const Fn<bool()> _current;
	const Fn<std::optional<SendArgs>()> _args;
	const Fn<bool(SendError, std::optional<int64>)> _blocked;
	const Fn<void(SendStarted)> _started;
	const Fn<void(SendError)> _sent;
	const Fn<SendCallbacks(SendCallbacks)> _attempt;
	rpl::producer<> _invalidated;
	std::shared_ptr<const PreparedSend> _prepared;
	KeyAuthorization _authorization;
	base::Timer _signingWait;
	rpl::variable<std::optional<int64>> _fee;
	rpl::variable<SendError> _error = SendError::None;
	rpl::variable<bool> _estimating = false;
	rpl::variable<bool> _sending = false;
	uint64 _owner = 0;
	uint64 _revision = 0;
	int _refusals = 0;
	bool _requoteQueued = false;
	bool _requoteForced = false;
	bool _unlocking = false;
	bool _submitted = false;
	rpl::lifetime _signingLifetime;
	rpl::lifetime _lifetime;
	// WHY: last, so the key ladder cancelled first finds the rest alive.
	rpl::lifetime _keyLifetime;

};

} // namespace Wallet
