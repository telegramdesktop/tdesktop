/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_onchain_signing.h"

#include "base/timer_rpl.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_send_preparation.h"
#include "wallet/wallet_session.h"

namespace Ui {
namespace {

constexpr auto kWalletRetryDelay = crl::time(1000);

} // namespace

using Wallet::SendError;

OnchainSigning::OnchainSigning(OnchainSigningDescriptor &&descriptor)
: _box(descriptor.box)
, _show(std::move(descriptor.show))
, _session(&_show->session())
, _weakSession(base::make_weak(_session))
, _wallet(&_session->wallet())
, _order(descriptor.order)
, _route(descriptor.route)
, _wait(descriptor.wait)
, _current(std::move(descriptor.current))
, _usable(std::move(descriptor.usable))
, _signable(std::move(descriptor.signable))
, _request(std::move(descriptor.request))
, _estimated(std::move(descriptor.estimated))
, _error(descriptor.error)
, _subject(std::move(descriptor.subject))
, _failedSubject(std::move(descriptor.failedSubject))
, _transfer(std::move(descriptor.transfer))
, _quoted(std::move(descriptor.quoted))
, _refused(std::move(descriptor.refused))
, _rejected(std::move(descriptor.rejected))
, _dismissed(std::move(descriptor.dismissed))
, _duplicate(std::move(descriptor.duplicate))
, _uncertain(std::move(descriptor.uncertain))
, _completion(std::move(descriptor.completion))
, _timedOut(std::move(descriptor.timedOut))
, _closing(std::move(descriptor.closing))
, _waitTimer([=] { waited(); }) {
}

OnchainSigning::~OnchainSigning() {
	closing();
}

uint64 OnchainSigning::owner() const {
	return _owner;
}

int OnchainSigning::retries() const {
	return _retries;
}

bool OnchainSigning::submitted() const {
	return _submitted;
}

bool OnchainSigning::busy() const {
	return !resting(_stage.current());
}

rpl::producer<bool> OnchainSigning::busyValue() const {
	return _stage.value(
	) | rpl::filter(
		rpl::mappers::_1 != Stage::Closed
	) | rpl::map([=](Stage stage) {
		return !resting(stage);
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> OnchainSigning::sendingValue() const {
	return _stage.value(
	) | rpl::map(
		rpl::mappers::_1 == Stage::Sending
	) | rpl::distinct_until_changed();
}

rpl::producer<std::optional<int64>> OnchainSigning::feeValue() const {
	return _fee.value();
}

void OnchainSigning::start() {
	_owner = _wallet->createPreviewOwner(_lifetime);
	_box->boxClosing() | rpl::on_next([=] {
		closing();
	}, _lifetime);
	if (_wait != OnchainSigningWait::Estimate) {
		return;
	}
	rpl::merge(
		_wallet->presenceValue() | rpl::skip(1) | rpl::to_empty,
		_wallet->custodyUpdates(),
		_wallet->transferWalletIdentityChanges(),
		_wallet->signingReadyValue() | rpl::skip(1) | rpl::to_empty
	) | rpl::filter([=] {
		return (_stage.current() == Stage::Waiting);
	}) | rpl::on_next([=] {
		prepare();
	}, _lifetime);
}

void OnchainSigning::sign(const Wallet::ServedOperation &operation) {
	if (_stage.current() != Stage::Idle) {
		return;
	}
	_operation = operation;
	_retries = 0;
	prepare();
}

void OnchainSigning::quote(const Wallet::ServedOperation &operation) {
	if (_stage.current() != Stage::Idle) {
		return;
	}
	_operation = operation;
	_retries = 0;
	_stage = Stage::Estimating;
	estimate();
}

void OnchainSigning::confirm() {
	if (_stage.current() != Stage::Ready) {
		return;
	}
	_retries = 0;
	reestimate();
}

void OnchainSigning::confirm(const Wallet::ServedOperation &operation) {
	if (_stage.current() == Stage::Ready) {
		_operation = operation;
		confirm();
	}
}

void OnchainSigning::resume() {
	if (_stage.current() != Stage::Waiting) {
		return;
	} else if (_wait == OnchainSigningWait::Attempt) {
		if (_wallet->signingReady() || !_authorization.valid()) {
			prepare();
		}
		return;
	}
	const auto stage = _resume;
	_stage = stage;
	if (stage == Stage::Idle) {
		if (const auto request = _request) {
			request();
		}
	} else if (stage == Stage::Signing) {
		submit();
	} else {
		estimate();
	}
}

bool OnchainSigning::resting(Stage stage) const {
	return (stage == Stage::Ready) || (stage == Stage::Idle && _request);
}

bool OnchainSigning::current() const {
	return (_stage.current() != Stage::Closed) && _current();
}

bool OnchainSigning::usable(OnchainSigningCheck check) {
	const auto callback = _usable;
	return callback(check);
}

bool OnchainSigning::walletReady() const {
	using Wallet::DeviceMode;
	using Wallet::Presence;
	if (_wallet->presenceCurrent() != Presence::Ready) {
		return false;
	} else if (_resume == Stage::Idle) {
		return true;
	} else if (!_wallet->transferWalletIdentity()) {
		return false;
	} else if (_resume != Stage::Signing) {
		return true;
	}
	return (_wallet->deviceCustodyState().mode == DeviceMode::Full)
		&& _wallet->signingReady();
}

bool OnchainSigning::extended(const Wallet::FeeResult &result) {
	const auto callback = _estimated;
	if (!callback) {
		return true;
	}
	const auto weak = base::make_weak(this);
	const auto refusal = callback(result);
	if (!weak || _stage.current() == Stage::Closed) {
		return false;
	} else if (refusal != SendError::None) {
		refused(refusal);
		return false;
	}
	return true;
}

void OnchainSigning::prepare() {
	if (!usable(OnchainSigningCheck::Estimate)) {
		return;
	} else if (_authorization.valid() && !_wallet->signingReady()) {
		awaitWallet();
		return;
	}
	if (_wait == OnchainSigningWait::Estimate) {
		_waitTimer.cancel();
	}
	_stage = Stage::Preparing;
	_prepared = nullptr;
	_wallet->estimateServed(
		_owner,
		*_operation,
		crl::guard(this, [=](Wallet::FeeResult result) {
			prepared(std::move(result));
		}));
}

void OnchainSigning::prepared(Wallet::FeeResult result) {
	if (_stage.current() != Stage::Preparing) {
		return;
	}
	const auto error = (result.error == SendError::None && !result.prepared)
		? SendError::Failed
		: result.error;
	if (error != SendError::None) {
		refused(error);
		return;
	} else if (!current()) {
		close();
		return;
	}
	_prepared = std::move(result.prepared);
	if (!extended(result)) {
		return;
	} else if (_authorization.valid()) {
		submit();
	} else {
		authorize();
	}
}

void OnchainSigning::reestimate() {
	_stage = _fee.current().has_value()
		? Stage::Confirming
		: Stage::Estimating;
	estimate();
}

void OnchainSigning::estimate() {
	if (!usable(OnchainSigningCheck::Estimate)) {
		return;
	} else if (!_owner) {
		_owner = _wallet->createPreviewOwner(_previewLifetime);
	}
	_prepared = nullptr;
	_wallet->estimateServed(
		_owner,
		*_operation,
		crl::guard(this, [=](Wallet::FeeResult result) {
			estimated(std::move(result));
		}));
}

void OnchainSigning::estimated(Wallet::FeeResult result) {
	const auto stage = _stage.current();
	if (stage != Stage::Estimating && stage != Stage::Confirming) {
		return;
	} else if (!current()) {
		close();
		return;
	}
	const auto error = (result.error == SendError::None && !result.prepared)
		? SendError::Failed
		: result.error;
	if (!extended(result)) {
		return;
	} else if (error != SendError::None) {
		refused(error);
	} else if (stage == Stage::Estimating
		|| Wallet::FeeChangeNeedsConfirmation(
			_fee.current(),
			result.feeNano)) {
		quoted(result.feeNano);
	} else {
		_prepared = std::move(result.prepared);
		if (_authorization.valid()) {
			_stage = Stage::Signing;
			submit();
		} else {
			authorize();
		}
	}
}

void OnchainSigning::quoted(int64 fee) {
	if (const auto callback = _quoted) {
		const auto weak = base::make_weak(this);
		callback();
		if (!weak || _stage.current() == Stage::Closed) {
			return;
		}
	}
	_fee = fee;
	ready();
}

bool OnchainSigning::retry() {
	if (++_retries > Wallet::kSendRefusalRetries) {
		return false;
	} else if (_order == OnchainSigningOrder::SamePress) {
		prepare();
	} else {
		reestimate();
	}
	return true;
}

void OnchainSigning::refused(SendError error) {
	if (error == SendError::QuoteExpired && retry()) {
		return;
	} else if (_route == OnchainSigningRoute::Flow) {
		abandon(error);
	} else if (error == SendError::WalletNotReady
		&& ((_wait == OnchainSigningWait::Confirmation && _waitArmed)
			|| ++_retries <= Wallet::kSendRefusalRetries)) {
		awaitWallet();
	} else if (error == SendError::KeyChanged) {
		keyChanged();
	} else if (error == SendError::Silent) {
		close();
	} else if (error == SendError::NoWallet
		|| error == SendError::SigningUnavailable
		|| error == SendError::WalletNotReady) {
		unavailable(error);
	} else {
		abandon(error);
	}
}

void OnchainSigning::unavailable(SendError error) {
	if (_route == OnchainSigningRoute::Money) {
		openMoney();
	} else {
		abandon(error);
	}
}

void OnchainSigning::abandon(SendError error) {
	const auto callback = _refused;
	if (_route != OnchainSigningRoute::InBox) {
		callback(error);
		return;
	}
	_fee = std::nullopt;
	const auto weak = base::make_weak(this);
	callback(error);
	if (weak && _stage.current() != Stage::Closed) {
		ready();
	}
}

void OnchainSigning::ready() {
	stopWaiting();
	_prepared = nullptr;
	_authorization = {};
	_stage = _operation ? Stage::Ready : Stage::Idle;
}

void OnchainSigning::authorize() {
	const auto confirming = (_order == OnchainSigningOrder::ConfirmFee);
	if (confirming) {
		if (!usable(OnchainSigningCheck::Key)) {
			return;
		}
		stopWaiting();
	}
	_keyLifetime.destroy();
	_stage = Stage::Authorizing;
	const auto weak = base::make_weak(this);
	Wallet::AcquireWalletKey(
		(confirming
			? Main::MakeSessionShow(_box->uiShow(), _session)
			: _show),
		[=] {
			return weak
				&& (weak->_stage.current() == Stage::Authorizing)
				&& weak->current()
				&& (!weak->_signable || weak->_signable());
		},
		_keyLifetime,
		crl::guard(this, [=](Wallet::KeyAuthorization auth) {
			authorized(std::move(auth));
		}));
}

void OnchainSigning::authorized(Wallet::KeyAuthorization auth) {
	const auto confirming = (_order == OnchainSigningOrder::ConfirmFee);
	if (_stage.current() != Stage::Authorizing) {
		return;
	} else if (confirming && !current()) {
		close();
	} else if (confirming && !auth.valid()) {
		ready();
	} else if (!auth.valid()) {
		stop();
		if (const auto callback = _dismissed) {
			callback();
		}
	} else if (confirming) {
		_authorization = std::move(auth);
		_stage = Stage::Signing;
		submit();
	} else {
		_authorization = std::move(auth);
		prepare();
	}
}

void OnchainSigning::awaitWallet() {
	const auto stage = _stage.current();
	if (stage != Stage::Waiting) {
		_resume = stage;
		_stage = Stage::Waiting;
	}
	switch (_wait) {
	case OnchainSigningWait::Attempt:
		if (!_waitArmed) {
			_waitArmed = true;
			_waitTimer.callOnce(Wallet::kSigningReadyTimeout);
		} else if (!_waitTimer.isActive()) {
			openMoney();
		}
		return;
	case OnchainSigningWait::Estimate:
		if (!_waitTimer.isActive()) {
			_waitTimer.callOnce(Wallet::kSigningReadyTimeout);
		}
		_wallet->refreshState();
		return;
	case OnchainSigningWait::Confirmation:
		if (!_waitArmed) {
			_waitArmed = true;
			_waitTimer.callOnce(Wallet::kSigningReadyTimeout);
			_wallet->refreshState();
		} else if (!_waitTimer.isActive()) {
			waited();
			return;
		}
		watchWallet();
		return;
	case OnchainSigningWait::Signing:
		if (!_waitTimer.isActive()) {
			_waitLifetime.destroy();
			_waitTimer.callOnce(Wallet::kSigningReadyTimeout);
			_wallet->signingReadyValue(
			) | rpl::filter(
				rpl::mappers::_1
			) | rpl::take(1) | rpl::on_next([=] {
				_waitTimer.cancel();
				resume();
			}, _waitLifetime);
		}
		return;
	}
}

void OnchainSigning::watchWallet() {
	_waitLifetime.destroy();
	rpl::merge(
		_wallet->presenceValue() | rpl::skip(1) | rpl::to_empty,
		_wallet->transferWalletIdentityChanges(),
		_wallet->custodyUpdates(),
		_wallet->signingReadyValue() | rpl::skip(1) | rpl::to_empty,
		base::timer_once(kWalletRetryDelay)
	) | rpl::filter([=] {
		return walletReady();
	}) | rpl::take(1) | rpl::on_next([=] {
		resume();
	}, _waitLifetime);
}

void OnchainSigning::waited() {
	if (_stage.current() != Stage::Waiting) {
		return;
	} else if (_wait == OnchainSigningWait::Signing) {
		const auto callback = _timedOut;
		callback();
	} else {
		unavailable(SendError::WalletNotReady);
	}
}

void OnchainSigning::stopWaiting() {
	_waitArmed = false;
	_waitTimer.cancel();
	_waitLifetime.destroy();
}

void OnchainSigning::submit() {
	if (_order == OnchainSigningOrder::SamePress) {
		if (usable(OnchainSigningCheck::Submit)) {
			send();
		}
		return;
	} else if (_stage.current() != Stage::Signing
		|| !usable(OnchainSigningCheck::Submit)) {
		return;
	} else if (!_wallet->signingReady()) {
		awaitWallet();
		return;
	}
	const auto refusal = _wallet->sendRefusal(_prepared, _authorization);
	if (refusal == SendError::SigningUnavailable && retry()) {
		return;
	} else if (refusal != SendError::None) {
		refused(refusal);
		return;
	}
	stopWaiting();
	send();
}

void OnchainSigning::send() {
	const auto flow = (_route == OnchainSigningRoute::Flow);
	_stage = Stage::Sending;
	_submitted = true;
	_complete = _completion ? _completion() : nullptr;
	const auto weak = base::make_weak(this);
	const auto complete = flow ? Fn<void()>() : _complete;
	_wallet->sendServed(
		flow ? base::take(_authorization) : _authorization,
		base::take(_prepared),
		crl::guard(_session, [=](Wallet::ServedResult result) {
			const auto strong = weak.get();
			if (strong && strong->_stage.current() == Stage::Sending) {
				strong->sent(std::move(result));
			} else if (complete
				&& result.outcome == Wallet::ServedOutcome::Accepted) {
				complete();
			}
		}));
}

void OnchainSigning::sent(Wallet::ServedResult result) {
	using Wallet::ServedOutcome;
	switch (result.outcome) {
	case ServedOutcome::Accepted:
		accepted();
		return;
	case ServedOutcome::Uncertain:
		if (const auto callback = _uncertain) {
			callback();
		} else {
			_authorization = {};
			track(result.operationId);
		}
		return;
	case ServedOutcome::Duplicate: {
		const auto callback = _duplicate;
		callback();
	} return;
	case ServedOutcome::Rejected:
		_submitted = false;
		if (_route == OnchainSigningRoute::Flow) {
			const auto callback = _rejected;
			callback(result.refusal, result.errorType);
		} else if (result.refusal == SendError::KeyChanged) {
			keyChanged();
		} else if (result.refusal == SendError::Silent) {
			close();
		} else {
			LOG(("API Error: wallet.sendTransfer for %1 failed with %2."
				).arg(_transfer, result.errorType));
			if (_route == OnchainSigningRoute::Money) {
				closeWithToast(_error(tr::now));
			} else {
				abandon(result.refusal);
			}
		}
		return;
	case ServedOutcome::Refused:
		_submitted = false;
		if (_order == OnchainSigningOrder::ConfirmFee) {
			_stage = Stage::Confirming;
		}
		if (_route == OnchainSigningRoute::Flow) {
			abandon(result.refusal);
		} else {
			refused(result.refusal);
		}
		return;
	}
}

void OnchainSigning::accepted() {
	const auto complete = base::take(_complete);
	close();
	if (complete) {
		complete();
	}
}

void OnchainSigning::track(const std::string &operationId) {
	if (operationId.empty()) {
		LOG(("Wallet Error: %1 is uncertain without an operation id."
			).arg(_subject));
		closeWithToast(_error(tr::now));
		return;
	}
	_stage = Stage::Tracking;
	_operationId = operationId;
	_wallet->historyUpdates() | rpl::on_next([=] {
		resolve();
	}, _trackLifetime);
	resolve();
}

void OnchainSigning::resolve() {
	using Status = Wallet::TransferItem::Status;
	if (_stage.current() != Stage::Tracking) {
		return;
	}
	const auto item = _wallet->trackedTransaction(_operationId);
	if (!item || item->status == Status::Pending) {
		return;
	} else if (item->status == Status::Success) {
		accepted();
	} else {
		const auto &subject = _failedSubject.isEmpty()
			? _subject
			: _failedSubject;
		LOG(("Wallet Error: %1 failed.").arg(subject));
		closeWithToast(_error(tr::now));
	}
}

void OnchainSigning::stop() {
	stopWaiting();
	_prepared = nullptr;
	_authorization = {};
	_fee = std::nullopt;
	if (_stage.current() != Stage::Closed) {
		_stage = Stage::Idle;
	}
}

void OnchainSigning::openMoney() {
	const auto session = _session;
	const auto valid = current();
	close();
	if (valid) {
		Wallet::ShowWallet(session);
	}
}

void OnchainSigning::keyChanged() {
	const auto show = _show;
	const auto valid = current();
	close();
	if (valid) {
		Wallet::ShowWalletKeyChanged(show);
	}
}

void OnchainSigning::closeWithToast(const QString &text) {
	const auto show = _show;
	const auto valid = current();
	close();
	if (valid) {
		show->showToast(text);
	}
}

void OnchainSigning::close() {
	const auto box = base::make_weak(_box);
	closing();
	if (const auto strong = box.get()) {
		strong->closeBox();
	}
}

void OnchainSigning::closing() {
	if (_stage.current() == Stage::Closed) {
		return;
	}
	_stage = Stage::Closed;
	_trackLifetime.destroy();
	_waitTimer.cancel();
	_waitLifetime.destroy();
	if (_route == OnchainSigningRoute::Flow) {
		_previewLifetime.destroy();
		_owner = 0;
	} else if (_weakSession) {
		_wallet->cancelFeeEstimate(_owner);
	}
	_operation = std::nullopt;
	_operationId.clear();
	_complete = nullptr;
	_prepared = nullptr;
	_authorization = {};
	const auto weak = base::make_weak(this);
	const auto callback = _closing;
	callback();
	if (weak) {
		_keyLifetime.destroy();
	}
}

} // namespace Ui
