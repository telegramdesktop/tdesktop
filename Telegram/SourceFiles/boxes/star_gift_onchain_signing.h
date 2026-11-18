/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "lang/lang_keys.h"
#include "wallet/wallet_session_served.h"

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {

class GenericBox;

enum class OnchainSigningOrder : uchar {
	SamePress,
	ConfirmFee,
};

enum class OnchainSigningRoute : uchar {
	Money,
	InBox,
	Flow,
};

enum class OnchainSigningWait : uchar {
	Attempt,
	Estimate,
	Confirmation,
	Signing,
};

enum class OnchainSigningCheck : uchar {
	Estimate,
	Key,
	Submit,
};

struct OnchainSigningDescriptor {
	not_null<GenericBox*> box;
	std::shared_ptr<Main::SessionShow> show;
	OnchainSigningOrder order = OnchainSigningOrder::SamePress;
	OnchainSigningRoute route = OnchainSigningRoute::Money;
	OnchainSigningWait wait = OnchainSigningWait::Estimate;
	Fn<bool()> current;
	Fn<bool(OnchainSigningCheck)> usable;
	Fn<bool()> signable;
	Fn<void()> request;
	Fn<Wallet::SendError(const Wallet::FeeResult &)> estimated;
	tr::phrase<> error;
	QString subject;
	QString failedSubject;
	QString transfer;
	Fn<void()> quoted;
	Fn<void(Wallet::SendError)> refused;
	Fn<void(Wallet::SendError, const QString &)> rejected;
	Fn<void()> dismissed;
	Fn<void()> duplicate;
	Fn<void()> uncertain;
	Fn<Fn<void()>()> completion;
	Fn<void()> timedOut;
	Fn<void()> closing;
};

class OnchainSigning final : public base::has_weak_ptr {
public:
	explicit OnchainSigning(OnchainSigningDescriptor &&descriptor);
	~OnchainSigning();

	[[nodiscard]] uint64 owner() const;
	[[nodiscard]] int retries() const;
	[[nodiscard]] bool submitted() const;
	[[nodiscard]] bool busy() const;
	[[nodiscard]] rpl::producer<bool> busyValue() const;
	[[nodiscard]] rpl::producer<bool> sendingValue() const;
	[[nodiscard]] rpl::producer<std::optional<int64>> feeValue() const;

	void start();
	void sign(const Wallet::ServedOperation &operation);
	void quote(const Wallet::ServedOperation &operation);
	void confirm();
	void confirm(const Wallet::ServedOperation &operation);
	void awaitWallet();
	void resume();
	void refused(Wallet::SendError error);
	void stop();
	void openMoney();
	void keyChanged();
	void closeWithToast(const QString &text);
	void close();
	void closing();

private:
	enum class Stage : uchar {
		Idle,
		Preparing,
		Estimating,
		Ready,
		Confirming,
		Authorizing,
		Waiting,
		Signing,
		Sending,
		Tracking,
		Closed,
	};

	[[nodiscard]] bool resting(Stage stage) const;
	[[nodiscard]] bool current() const;
	[[nodiscard]] bool usable(OnchainSigningCheck check);
	[[nodiscard]] bool walletReady() const;
	[[nodiscard]] bool extended(const Wallet::FeeResult &result);
	void prepare();
	void prepared(Wallet::FeeResult result);
	void reestimate();
	void estimate();
	void estimated(Wallet::FeeResult result);
	void quoted(int64 fee);
	[[nodiscard]] bool retry();
	void unavailable(Wallet::SendError error);
	void abandon(Wallet::SendError error);
	void ready();
	void authorize();
	void authorized(Wallet::KeyAuthorization auth);
	void watchWallet();
	void waited();
	void stopWaiting();
	void submit();
	void send();
	void sent(Wallet::ServedResult result);
	void accepted();
	void track(const std::string &operationId);
	void resolve();

	const not_null<GenericBox*> _box;
	const std::shared_ptr<Main::SessionShow> _show;
	const not_null<Main::Session*> _session;
	const base::weak_ptr<Main::Session> _weakSession;
	const not_null<Wallet::Session*> _wallet;
	const OnchainSigningOrder _order = OnchainSigningOrder::SamePress;
	const OnchainSigningRoute _route = OnchainSigningRoute::Money;
	const OnchainSigningWait _wait = OnchainSigningWait::Estimate;
	const Fn<bool()> _current;
	const Fn<bool(OnchainSigningCheck)> _usable;
	const Fn<bool()> _signable;
	const Fn<void()> _request;
	const Fn<Wallet::SendError(const Wallet::FeeResult &)> _estimated;
	const tr::phrase<> _error;
	const QString _subject;
	const QString _failedSubject;
	const QString _transfer;
	const Fn<void()> _quoted;
	const Fn<void(Wallet::SendError)> _refused;
	const Fn<void(Wallet::SendError, const QString &)> _rejected;
	const Fn<void()> _dismissed;
	const Fn<void()> _duplicate;
	const Fn<void()> _uncertain;
	const Fn<Fn<void()>()> _completion;
	const Fn<void()> _timedOut;
	const Fn<void()> _closing;
	base::Timer _waitTimer;
	std::optional<Wallet::ServedOperation> _operation;
	std::shared_ptr<const Wallet::PreparedSend> _prepared;
	Wallet::KeyAuthorization _authorization;
	Fn<void()> _complete;
	std::string _operationId;
	rpl::variable<Stage> _stage = Stage::Idle;
	rpl::variable<std::optional<int64>> _fee;
	uint64 _owner = 0;
	int _retries = 0;
	Stage _resume = Stage::Idle;
	bool _waitArmed = false;
	bool _submitted = false;
	rpl::lifetime _waitLifetime;
	rpl::lifetime _trackLifetime;
	rpl::lifetime _previewLifetime;
	rpl::lifetime _lifetime;
	// WHY: last, so the key ladder cancelled first finds the rest alive.
	rpl::lifetime _keyLifetime;

};

} // namespace Ui
