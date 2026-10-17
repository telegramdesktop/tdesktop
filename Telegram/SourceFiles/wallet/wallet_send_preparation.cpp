/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_send_preparation.h"

#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "wallet/wallet_content.h"

namespace Wallet {

SendPreparation::SendPreparation(SendPreparationDescriptor &&descriptor)
: _show(std::move(descriptor.show))
, _session(&_show->session())
, _wallet(&_session->wallet())
, _current(std::move(descriptor.current))
, _args(std::move(descriptor.args))
, _blocked(std::move(descriptor.blocked))
, _started(std::move(descriptor.started))
, _sent(std::move(descriptor.sent))
, _attempt(std::move(descriptor.attempt))
, _invalidated(std::move(descriptor.invalidated))
, _signingWait([=] { fail(SendError::Failed); }) {
}

std::optional<int64> SendPreparation::fee() const {
	return _fee.current();
}

rpl::producer<std::optional<int64>> SendPreparation::feeValue() const {
	return _fee.value();
}

SendError SendPreparation::error() const {
	return _error.current();
}

rpl::producer<SendError> SendPreparation::errorValue() const {
	return _error.value();
}

rpl::producer<bool> SendPreparation::estimatingValue() const {
	return _estimating.value();
}

bool SendPreparation::sending() const {
	return _sending.current();
}

rpl::producer<bool> SendPreparation::sendingValue() const {
	return _sending.value();
}

bool SendPreparation::submitted() const {
	return _submitted;
}

bool SendPreparation::pressable() const {
	return !_sending.current()
		&& !_estimating.current()
		&& !_submitted
		&& !_blocked(_error.current(), _fee.current())
		&& (_prepared || (_error.current() == SendError::SigningUnavailable));
}

void SendPreparation::start() {
	_owner = _wallet->createPreviewOwner(_lifetime);
	_wallet->signingReadyValue(
	) | rpl::skip(1) | rpl::on_next([=](bool ready) {
		if (ready) {
			_signingWait.cancel();
		}
		if (!_submitted) {
			_prepared = nullptr;
			estimate();
		}
	}, _lifetime);
	rpl::merge(
		_wallet->sendStateValue() | rpl::skip(1) | rpl::to_empty,
		_wallet->presenceValue() | rpl::skip(1) | rpl::to_empty,
		std::move(_invalidated)
	) | rpl::filter([=] {
		return !_sending.current() && !_submitted;
	}) | rpl::on_next([=] {
		requote();
	}, _lifetime);
	rpl::merge(
		_wallet->balanceNanoValue() | rpl::skip(1) | rpl::to_empty,
		_wallet->custodyUpdates()
	) | rpl::filter([=] {
		return !_sending.current()
			&& !_submitted
			&& (_estimating.current()
				|| !_prepared
				|| (_error.current() != SendError::None));
	}) | rpl::on_next([=] {
		requote();
	}, _lifetime);
}

void SendPreparation::requote() {
	_prepared = nullptr;
	_requoteForced = true;
	estimate();
}

void SendPreparation::estimate() {
	if (_estimating.current()) {
		_requoteQueued = true;
		return;
	}
	const auto args = _args();
	if (!args || _submitted) {
		return;
	} else if (!SendCommentFits(args->comment.text)) {
		_prepared = nullptr;
		_fee = std::nullopt;
		_error = SendError::CommentTooLong;
		return;
	}
	const auto revision = ++_revision;
	const auto signing = _wallet->signingReady();
	_estimating = true;
	_wallet->estimateFee(
		KeyAuthorization(),
		_owner,
		*args,
		crl::guard(this, [=](FeeResult result) {
			estimated(revision, *args, signing, std::move(result));
		}));
}

void SendPreparation::estimated(
		uint64 revision,
		const SendArgs &args,
		bool signing,
		FeeResult result) {
	if (revision != _revision) {
		return;
	}
	const auto forced = base::take(_requoteForced);
	const auto again = base::take(_requoteQueued)
		&& (forced
			|| (_args() != args)
			|| (signing != _wallet->signingReady()));
	const auto error = (result.error == SendError::None && !result.prepared)
		? SendError::Failed
		: result.error;
	if (again || error == SendError::QuoteExpired) {
		_prepared = nullptr;
		_estimating = false;
		estimate();
	} else {
		_prepared = std::move(result.prepared);
		_fee = (error == SendError::None)
			? std::make_optional(result.feeNano)
			: std::nullopt;
		_error = error;
		_estimating = false;
	}
	if (_sending.current() && !_submitted) {
		proceed();
	}
}

void SendPreparation::press() {
	if (!pressable()) {
		return;
	}
	const auto weak = base::make_weak(this);
	_sending = true;
	if (!weak) {
		return;
	}
	_refusals = 0;
	proceed();
}

void SendPreparation::proceed() {
	if (!_sending.current() || _submitted || _unlocking) {
		return;
	} else if (!_current()) {
		stop();
	} else if (_estimating.current()) {
		return;
	} else if (!_authorization.valid()) {
		acquireKey();
	} else if (!_wallet->signingReady()) {
		awaitSigning();
	} else if (!_prepared) {
		const auto error = _error.current();
		const auto retry = (error == SendError::None)
			|| (error == SendError::SigningUnavailable);
		if (retry && ++_refusals <= kSendRefusalRetries) {
			estimate();
		} else {
			fail(retry ? SendError::Failed : error);
		}
	} else if (_blocked(_error.current(), _fee.current())) {
		stop();
	} else {
		submit();
	}
}

void SendPreparation::acquireKey() {
	_keyLifetime.destroy();
	_unlocking = true;
	const auto weak = base::make_weak(this);
	AcquireWalletKey(
		_show,
		[=] { return weak && _current(); },
		_keyLifetime,
		crl::guard(this, [=](KeyAuthorization auth) {
			_unlocking = false;
			if (!_sending.current() || _submitted) {
				return;
			} else if (!auth.valid()) {
				stop();
				return;
			}
			_authorization = std::move(auth);
			proceed();
		}),
		tr::lng_wallet_restore_text());
}

void SendPreparation::awaitSigning() {
	if (_signingWait.isActive()) {
		return;
	}
	_signingLifetime.destroy();
	_wallet->signingReadyValue(
	) | rpl::filter(
		rpl::mappers::_1
	) | rpl::take(1) | rpl::on_next([=] {
		proceed();
	}, _signingLifetime);
	_signingWait.callOnce(kSigningReadyTimeout);
}

void SendPreparation::submit() {
	_submitted = true;
	_signingWait.cancel();
	_signingLifetime.destroy();
	const auto session = _session;
	const auto wallet = _wallet;
	auto authorization = _authorization;
	auto prepared = base::take(_prepared);
	auto callbacks = SendCallbacks{ .started = _started, .sent = _sent };
	if (const auto attempt = _attempt) {
		const auto weak = base::make_weak(session);
		callbacks = attempt(std::move(callbacks));
		if (!weak) {
			return;
		}
	}
	wallet->send(
		std::move(authorization),
		std::move(prepared),
		crl::guard(session, std::move(callbacks.sent)),
		crl::guard(session, std::move(callbacks.started)),
		(callbacks.preparing
			? Fn<void(std::string)>(
				crl::guard(session, std::move(callbacks.preparing)))
			: nullptr));
}

void SendPreparation::refused(SendError error) {
	_submitted = false;
	const auto retry = (error == SendError::QuoteExpired)
		|| (error == SendError::SigningUnavailable);
	if (retry && ++_refusals <= kSendRefusalRetries) {
		estimate();
	} else {
		fail((error == SendError::QuoteExpired) ? SendError::Failed : error);
	}
}

void SendPreparation::fail(SendError error) {
	const auto weak = base::make_weak(this);
	stop();
	if (weak) {
		_error = error;
	}
}

void SendPreparation::stop() {
	_authorization = {};
	_signingWait.cancel();
	_signingLifetime.destroy();
	_sending = false;
}

void SendPreparation::cancel() {
	_signingWait.cancel();
	_signingLifetime.destroy();
	_keyLifetime.destroy();
}

} // namespace Wallet
