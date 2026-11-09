/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_bot.h"

#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_request.h"

#include "wallet_engine.hpp"

#include <QtCore/QUuid>

namespace Wallet {
namespace {

namespace engine = wallet_engine;
using Phase = TonConnectRequestPhase;

constexpr auto kPressGuard = crl::time(800);
constexpr auto kWalletResolveTimeout = 20 * crl::time(1000);
constexpr auto kDeadlineMaxDelay = 24 * 3600 * crl::time(1000);
constexpr auto kWalletNetwork = "-239"_cs;

[[nodiscard]] std::optional<engine::SendRequest> SendRequestFromBot(
		const TonConnectBotTransaction &transaction,
		const std::string &operationId) {
	auto messages = std::vector<engine::SendMessage>();
	messages.reserve(transaction.messages.size());
	for (const auto &message : transaction.messages) {
		const auto parsed = ParseAddress(message.address);
		if (!parsed) {
			return std::nullopt;
		}
		auto body = message.payload
			? engine::SendMessageBody(engine::SendMessageBody::kRawPayload{
				.boc = message.payload->toStdString(),
			})
			: engine::SendMessageBody(engine::SendMessageBody::kEmpty{});
		messages.push_back(engine::SendMessage{
			.destination = message.address.toStdString(),
			.amount = engine::SendAmount(engine::SendAmount::kExact{
				.nanograms = message.amount.toStdString(),
			}),
			.body = std::move(body),
			.bounce = parsed->bounceable,
			.state_init = (message.stateInit
				? std::make_optional(message.stateInit->toStdString())
				: std::nullopt),
		});
	}
	const auto expiration = transaction.validUntil
		? engine::SendExpiration(engine::SendExpiration::kExact{
			.unix_timestamp = uint64_t(*transaction.validUntil),
		})
		: engine::SendExpiration(engine::SendExpiration::kEngineDefault{});
	return engine::SendRequest{
		.operation_id = operationId,
		.force = false,
		.intent = engine::SendIntent{
			.expiration = expiration,
			.messages = std::move(messages),
		},
	};
}

[[nodiscard]] engine::TonConnectSignDataPayload SignDataPayloadFromBot(
		const TonConnectBotSignData &data) {
	using Payload = engine::TonConnectSignDataPayload;
	switch (data.type) {
	case TonConnectSignDataType::Text:
		return Payload::kText{ .text = data.text.toStdString() };
	case TonConnectSignDataType::Binary:
		return Payload::kBinary{ .bytes = data.bytes.toStdString() };
	case TonConnectSignDataType::Cell:
		return Payload::kCell{
			.schema = data.schema.toStdString(),
			.cell = data.cell.toStdString(),
		};
	}
	Unexpected("Type in SignDataPayloadFromBot.");
}

[[nodiscard]] engine::TonConnectSignDataRequest SignDataRequestFromBot(
		const TonConnectBotSignData &data,
		const QString &address) {
	return {
		.payload = SignDataPayloadFromBot(data),
		.network = (data.network
			? std::make_optional(data.network->toStdString())
			: std::nullopt),
		// Checked equal already: the engine never parses the bot's spelling.
		.from = (data.from
			? std::make_optional(address.toStdString())
			: std::nullopt),
	};
}

} // namespace

TonConnectBotRequest::TonConnectBotRequest(TonConnectBotArgs args)
: _show(std::move(args.show))
, _session(&_show->session())
, _name(std::move(args.name))
, _domain(args.username.isEmpty() ? QString() : ('@' + args.username))
, _userpic(std::move(args.userpic))
, _transaction(std::move(args.transaction))
, _signData(std::move(args.signData))
, _signDomain(std::move(args.domain))
, _current(std::move(args.current))
, _done(std::move(args.done))
, _operationId(QUuid::createUuid().toString(
	QUuid::WithoutBraces).toStdString())
, _deadlineTimer([=] { expired(); })
, _resolveTimer([=] { fail(); }) {
	_session->wallet().tonConnect().requests().hold(_lifetime);
	_state = stateFor(Phase::Loading);
}

TonConnectBotRequest::~TonConnectBotRequest() {
	stop();
	closeBox();
}

void TonConnectBotRequest::start() {
	const auto validUntil = _transaction
		? _transaction->validUntil
		: std::nullopt;
	const auto network = _transaction
		? _transaction->network
		: _signData
		? _signData->network
		: std::nullopt;
	if ((!_transaction && !_signData)
		|| (validUntil && *validUntil <= base::unixtime::now())
		|| (network && *network != kWalletNetwork.utf16())) {
		refuse();
		return;
	}
	const auto &wallet = _session->wallet();
	if (wallet.presenceCurrent() == Presence::Ready
		&& wallet.deviceCustodyState().mode != DeviceMode::Unknown) {
		prepare();
	} else if (showBox()) {
		waitWallet();
	}
}

bool TonConnectBotRequest::cancel() {
	if (_finished || _sendStarted) {
		return false;
	}
	closeBox();
	stop();
	return true;
}

TonConnectRequestBoxState TonConnectBotRequest::stateFor(Phase phase) const {
	return {
		.phase = phase,
		.name = _name,
		.domain = _domain,
		.userpic = _userpic,
	};
}

bool TonConnectBotRequest::showBox() {
	auto box = Box(TonConnectRequestBox, TonConnectRequestBoxArgs{
		.session = _session,
		.state = _state.value(),
		.confirm = crl::guard(this, [=] { confirmPressed(); }),
		.decline = crl::guard(this, [=] { declinePressed(); }),
		.dismissed = crl::guard(this, [=] { dismissed(); }),
		.pressGuard = kPressGuard,
	});
	_box = box.data();
	_show->showBox(std::move(box));
	if (!_box) {
		fail();
		return false;
	}
	return true;
}

void TonConnectBotRequest::waitWallet() {
	auto &wallet = _session->wallet();
	_polling = true;
	wallet.startPolling();
	_resolveTimer.callOnce(kWalletResolveTimeout);
	rpl::merge(
		wallet.presenceValue() | rpl::to_empty,
		wallet.transferWalletIdentityChanges(),
		wallet.custodyUpdates()
	) | rpl::on_next([=] {
		resolve();
	}, _resolveLifetime);
}

void TonConnectBotRequest::resolve() {
	if (_finished) {
		return;
	}
	const auto &wallet = _session->wallet();
	const auto presence = wallet.presenceCurrent();
	if (presence == Presence::Unavailable
		|| presence == Presence::AddressUnreadable) {
		fail();
	} else if (presence == Presence::Ready
		&& wallet.deviceCustodyState().mode != DeviceMode::Unknown) {
		stopResolving();
		prepare();
	}
}

void TonConnectBotRequest::stopResolving() {
	_resolveTimer.cancel();
	_resolveLifetime.destroy();
	if (base::take(_polling)) {
		_session->wallet().stopPolling();
	}
}

void TonConnectBotRequest::prepare() {
	auto &wallet = _session->wallet();
	const auto address = wallet.address();
	const auto &from = _transaction ? _transaction->from : _signData->from;
	if (!address) {
		fail();
		return;
	} else if (from && CanonicalAddress(*from) != *address) {
		refuse();
		return;
	} else if (_transaction) {
		const auto request = SendRequestFromBot(*_transaction, _operationId);
		auto transfer = request
			? TonConnectTransferFromEngine(*request)
			: std::nullopt;
		if (!transfer) {
			refuse();
			return;
		}
		_transfer = std::make_shared<const TonConnectTransfer>(
			std::move(*transfer));
	} else {
		_signRequest = std::make_shared<const TonConnectSignData>(
			TonConnectSignDataFromEngine(
				nullptr,
				SignDataRequestFromBot(*_signData, *address)));
	}
	wallet.transferWalletIdentityChanges(
	) | rpl::on_next([=] {
		if (!_sendStarted) {
			fail();
		}
	}, _lifetime);
	if (_transfer) {
		preview();
	} else {
		auto state = stateFor(Phase::Confirm);
		state.signData = _signRequest;
		state.confirmable = true;
		_state = std::move(state);
	}
	if (!_finished && (_box || showBox()) && _transfer) {
		armDeadline();
	}
}

void TonConnectBotRequest::preview() {
	auto &wallet = _session->wallet();
	if (!_previewOwner) {
		_previewOwner = wallet.createPreviewOwner(_previewLifetime);
	}
	auto state = stateFor(Phase::Confirm);
	state.transfer = _transfer;
	state.feeLoading = true;
	_state = std::move(state);
	wallet.estimateTonConnect(
		_previewOwner,
		_transfer,
		crl::guard(this, [=](FeeResult result) {
			previewed(std::move(result));
		}));
}

void TonConnectBotRequest::previewed(FeeResult result) {
	const auto &state = _state.current();
	if (_finished || state.phase != Phase::Confirm || state.busy) {
		return;
	} else if (result.error == SendError::InvalidRequest
		|| result.error == SendError::DataInvalid
		|| result.error == SendError::Failed) {
		refuse();
		return;
	}
	_prepared = ApplyTonConnectPreview(
		_session,
		std::move(result),
		_state,
		_idleLifetime,
		[=] {
			if (!_finished && !_state.current().busy) {
				preview();
			}
		});
}

void TonConnectBotRequest::armDeadline() {
	if (const auto validUntil = _transaction->validUntil) {
		const auto left = std::max(
			crl::time(*validUntil) - base::unixtime::now(),
			crl::time(0));
		_deadlineTimer.callOnce(
			std::min(left * 1000, kDeadlineMaxDelay),
			Qt::PreciseTimer);
	}
}

void TonConnectBotRequest::expired() {
	const auto validUntil = _transaction->validUntil;
	if (_finished || _sendStarted || !validUntil) {
		return;
	} else if (*validUntil > base::unixtime::now()) {
		armDeadline();
		return;
	}
	closeWithToast(
		tr::lng_wallet_connect_request_expired(tr::now),
		{ .result = TonConnectBotResult::Refused });
}

void TonConnectBotRequest::confirmPressed() {
	auto state = _state.current();
	if (_finished
		|| state.phase != Phase::Confirm
		|| state.busy
		|| (!_prepared && !_signRequest)) {
		return;
	}
	state.busy = true;
	state.declining = false;
	state.error = QString();
	_state = std::move(state);
	if (!_current()) {
		fail();
		return;
	}
	_keyLifetime.destroy();
	AcquireWalletKey(
		_show,
		[weak = base::make_weak(this)] { return weak && !weak->_finished; },
		_keyLifetime,
		crl::guard(this, [=](KeyAuthorization auth) {
			keyReady(std::move(auth));
		}));
}

void TonConnectBotRequest::keyReady(KeyAuthorization auth) {
	if (_finished) {
		return;
	} else if (!auth.grant) {
		backToConfirm(QString());
		return;
	}
	auto &wallet = _session->wallet();
	const auto failed = crl::guard(this, [=](TonConnectKeyError error) {
		signFailed(error);
	});
	if (_signRequest) {
		wallet.signTonConnectData(
			std::move(auth),
			TonConnectKey(),
			QString(),
			_signRequest,
			_signDomain,
			crl::guard(this, [=](TonConnectSignature result) {
				dataSigned(std::move(result));
			}),
			failed);
		return;
	}
	const auto refusal = wallet.sendRefusal(_prepared, auth);
	if (refusal != SendError::None) {
		backToConfirm(SendErrorText(refusal, TransferMinNanos(_session)));
		return;
	}
	_auth = std::move(auth);
	wallet.signTonConnectData(
		_auth,
		TonConnectKey(),
		QString(),
		nullptr,
		QString(),
		crl::guard(this, [=](TonConnectSignature account) {
			accountReady(std::move(account));
		}),
		failed);
}

void TonConnectBotRequest::accountReady(TonConnectSignature account) {
	if (_finished) {
		return;
	} else if (!_current()) {
		fail();
		return;
	}
	_account = std::move(account);
	_sendStarted = true;
	const auto weak = base::make_weak(this);
	const auto current = _current;
	auto link = TonConnectSendLink{
		.operationId = _operationId,
		// WHY: the handoff is the last moment before the broadcast, and a
		// transfer whose message is gone could never be reported to the bot.
		.handoff = [=](const QString &) {
			const auto strong = weak.get();
			const auto allowed = strong && current();
			if (strong && !allowed) {
				strong->_handoffRefused = true;
			}
			return allowed;
		},
	};
	_session->wallet().sendTonConnect(
		base::take(_auth),
		_prepared,
		std::move(link),
		crl::guard(this, [=](TonConnectSendResult result) {
			sent(std::move(result));
		}),
		[show = _show](SendError error) {
			if (error == SendError::KeyChanged && show->valid()) {
				ShowWalletKeyChanged(show);
			}
		});
}

void TonConnectBotRequest::dataSigned(TonConnectSignature result) {
	if (_finished) {
		return;
	}
	closeWithToast(tr::lng_wallet_connect_sign_done(tr::now), {
		.result = TonConnectBotResult::Signed,
		.signature = std::move(result),
	});
}

void TonConnectBotRequest::signFailed(TonConnectKeyError error) {
	if (_finished) {
		return;
	} else if (error == TonConnectKeyError::Locked
		|| error == TonConnectKeyError::Cancelled) {
		backToConfirm(QString());
	} else {
		fail();
	}
}

void TonConnectBotRequest::sent(TonConnectSendResult result) {
	using Result = TonConnectBotResult;
	// A retired send reports its BoC with an error: it may still have landed.
	const auto sentBoc = !result.signedBoc.isEmpty();
	if (_finished) {
		return;
	} else if (_handoffRefused) {
		closeWithToast(
			tr::lng_wallet_connect_request_not_sent(tr::now),
			{ .result = Result::Failed });
	} else if (result.error == SendError::None
		&& !result.messageHash.isEmpty()) {
		closeWithToast(tr::lng_wallet_connect_request_sent(tr::now), {
			.result = Result::Sent,
			.messageHash = result.messageHash,
			.signature = std::move(_account),
		});
	} else if (sentBoc
		|| result.error == SendError::None
		|| result.error == SendError::SubmissionUnknown) {
		closeWithToast(
			(sentBoc
				? tr::lng_wallet_connect_request_sent(tr::now)
				: tr::lng_wallet_connect_request_not_sent(tr::now)),
			{ .result = Result::Failed });
	} else {
		closeWithToast(
			tr::lng_wallet_connect_request_not_sent(tr::now),
			{ .result = Result::Refused });
	}
}

void TonConnectBotRequest::declinePressed() {
	const auto &state = _state.current();
	if (_finished || state.phase != Phase::Confirm || state.busy) {
		return;
	}
	closeWithToast(QString(), { .result = TonConnectBotResult::Declined });
}

void TonConnectBotRequest::dismissed() {
	if (_closingBox || _finished) {
		return;
	}
	_box = nullptr;
	if (!_sendStarted) {
		finish({ .result = TonConnectBotResult::Declined });
	}
}

void TonConnectBotRequest::backToConfirm(const QString &error) {
	if (_finished) {
		return;
	}
	_auth = KeyAuthorization();
	auto state = _state.current();
	state.busy = false;
	state.declining = false;
	state.error = error;
	_state = std::move(state);
}

void TonConnectBotRequest::refuse() {
	closeWithToast(QString(), { .result = TonConnectBotResult::Refused });
}

void TonConnectBotRequest::fail() {
	closeWithToast(
		(_state.current().busy
			? tr::lng_wallet_connect_request_failed(tr::now)
			: QString()),
		{ .result = TonConnectBotResult::Failed });
}

void TonConnectBotRequest::closeWithToast(
		const QString &text,
		TonConnectBotOutcome outcome) {
	if (_finished) {
		return;
	}
	closeBox();
	if (!text.isEmpty() && _show->valid()) {
		_show->showToast(text);
	}
	finish(std::move(outcome));
}

void TonConnectBotRequest::closeBox() {
	_closingBox = true;
	if (const auto box = _box.get()) {
		_box = nullptr;
		if (box->hasDelegate()) {
			box->closeBox();
		}
	}
}

void TonConnectBotRequest::finish(TonConnectBotOutcome outcome) {
	if (_finished) {
		return;
	}
	stop();
	const auto done = _done;
	done(std::move(outcome));
}

void TonConnectBotRequest::stop() {
	_finished = true;
	_auth = KeyAuthorization();
	stopResolving();
	_deadlineTimer.cancel();
	_previewLifetime.destroy();
	_idleLifetime.destroy();
	_keyLifetime.destroy();
	_lifetime.destroy();
}

} // namespace Wallet
