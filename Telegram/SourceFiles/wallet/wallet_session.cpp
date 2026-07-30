/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session.h"

#include "base/unixtime.h"
#include "gram/api/gram_api_send.h"
#include "gram/crypto/gram_mnemonic.h"
#include "gram/ton/gram_boc.h"
#include "gram/ton/gram_message.h"
#include "gram/wallet/gram_wallet_v5.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Wallet {
namespace {

constexpr auto kPollInterval = 5 * crl::time(1000);
constexpr auto kHistoryPageLimit = 20;
constexpr auto kSendValidUntilOffset = TimeId(300);
constexpr auto kSendRetryClockMargin = TimeId(60);

} // namespace

Session::Session(not_null<Main::Session*> session)
: _session(session)
, _api(session)
, _feeEstimator(MakeFeeEstimator(&_api))
, _pollTimer([=] { pollTick(); }) {
}

Session::~Session() = default;

void Session::ensureLoaded() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	const auto stored = _session->local().readWallet();
	if (!stored || stored->words.empty()) {
		return;
	}
	if (applyKey(
			stored->words,
			stored->mnemonicType,
			stored->walletId,
			stored->phraseViewed ? KeyState::Imported : KeyState::Created)) {
		_phraseUnviewed = !stored->phraseViewed;
	}
}

bool Session::applyKey(
		std::vector<QString> words,
		Gram::MnemonicType type,
		quint32 walletId,
		KeyState state) {
	auto key = Gram::MnemonicToKeyPair(words, type);
	if (!key) {
		return false;
	}
	_keyPair = std::move(key);
	_walletId = walletId;
	_address = Gram::WalletV5Address(_keyPair->publicKey, walletId);
	_keyState = state;
	return true;
}

KeyState Session::keyState() {
	ensureLoaded();
	return _keyState.current();
}

rpl::producer<KeyState> Session::keyStateValue() {
	ensureLoaded();
	return _keyState.value();
}

std::optional<Gram::Address> Session::address() {
	ensureLoaded();
	if (_keyState.current() == KeyState::None) {
		return std::nullopt;
	}
	return _address;
}

QString Session::addressFriendly(bool bounceable) {
	ensureLoaded();
	if (_keyState.current() == KeyState::None) {
		return QString();
	}
	return Gram::FormatFriendly(_address, bounceable);
}

bool Session::create() {
	ensureLoaded();
	if (_keyState.current() != KeyState::None) {
		return false;
	}
	auto words = Gram::GenerateMnemonic();
	const auto applied = applyKey(
		words,
		Gram::MnemonicType::Ton,
		Gram::kDefaultWalletId,
		KeyState::Created);
	if (applied) {
		_session->local().writeWallet(Storage::WalletStored{
			.words = std::move(words),
			.mnemonicType = Gram::MnemonicType::Ton,
			.contractVersion = 1,
			.walletId = Gram::kDefaultWalletId,
			.networkId = -239,
			.phraseViewed = false,
		});
		_phraseUnviewed = true;
	}
	return applied;
}

bool Session::import(std::vector<QString> words) {
	ensureLoaded();
	if (_keyState.current() != KeyState::None || words.empty()) {
		return false;
	}
	for (auto &word : words) {
		word = word.trimmed().toLower();
	}
	const auto type = Gram::DetectMnemonicType(words);
	if (!type) {
		return false;
	}
	const auto applied = applyKey(
		words,
		*type,
		Gram::kDefaultWalletId,
		KeyState::Imported);
	if (applied) {
		_session->local().writeWallet(Storage::WalletStored{
			.words = std::move(words),
			.mnemonicType = *type,
			.contractVersion = 1,
			.walletId = Gram::kDefaultWalletId,
			.networkId = -239,
			.phraseViewed = true,
		});
		_phraseUnviewed = false;
	}
	return applied;
}

void Session::remove() {
	_session->local().writeWallet(Storage::WalletStored());
	_keyPair.reset();
	_address = Gram::Address();
	_walletId = Gram::kDefaultWalletId;
	_keyState = KeyState::None;
	_phraseUnviewed = false;
	clearNetworkState();
}

bool Session::phraseUnviewed() {
	ensureLoaded();
	return _phraseUnviewed.current();
}

rpl::producer<bool> Session::phraseUnviewedValue() {
	ensureLoaded();
	return _phraseUnviewed.value();
}

void Session::markPhraseViewed() {
	ensureLoaded();
	auto stored = _session->local().readWallet();
	if (stored && !stored->phraseViewed) {
		stored->phraseViewed = true;
		_session->local().writeWallet(*stored);
	}
	_phraseUnviewed = false;
}

void Session::clearNetworkState() {
	_balanceNano = 0;
	_lastState = Gram::AccountState();
	_stateKnown = false;
	_history.clear();
	_historyUpdates.fire({});
	_historyErrorLogged = false;
	_historyHasNext = false;
	_historyLoadedOffset = 0;
	_pending.reset();
	_sendState = SendState::Idle;
	_pollingCount = 0;
	_pollTimer.cancel();
	_stateRequestPending = false;
	_historyRequestPending = false;
	_pendingCheckPending = false;
	_stateDone.clear();
	_stateFail.clear();
	_historyDone.clear();
}

void Session::refreshState(
		Fn<void(const Gram::AccountState &)> done,
		Fn<void(const Gram::ApiError &)> fail) {
	ensureLoaded();
	if (_keyState.current() == KeyState::None) {
		if (fail) {
			fail(Gram::ApiError{ .message = u"No wallet."_q });
		}
		return;
	}
	if (done) {
		_stateDone.push_back(std::move(done));
	}
	if (fail) {
		_stateFail.push_back(std::move(fail));
	}
	if (_stateRequestPending) {
		return;
	}
	_stateRequestPending = true;
	_api.request(
		Gram::AddressInformationRequest(addressFriendly(false)),
		[=](const QByteArray &json) {
			_stateRequestPending = false;
			if (_keyState.current() == KeyState::None) {
				_stateDone.clear();
				_stateFail.clear();
				return;
			}
			const auto stateDone = base::take(_stateDone);
			const auto stateFail = base::take(_stateFail);
			const auto state = Gram::ParseAccountState(json);
			if (!state) {
				LOG(("Wallet Error: Failed to parse account state."));
				const auto error = Gram::ApiError{
					.message = u"Failed to parse account state."_q,
				};
				for (const auto &callback : stateFail) {
					callback(error);
				}
				return;
			}
			applyAccountState(*state);
			checkPendingBySeqno(*state);
			for (const auto &callback : stateDone) {
				callback(*state);
			}
		},
		[=](const Gram::ApiError &error) {
			_stateRequestPending = false;
			if (_keyState.current() == KeyState::None) {
				_stateDone.clear();
				_stateFail.clear();
				return;
			}
			const auto stateFail = base::take(_stateFail);
			_stateDone.clear();
			LOG(("Wallet Error: addressInformation: %1").arg(error.message));
			for (const auto &callback : stateFail) {
				callback(error);
			}
		});
}

void Session::applyAccountState(const Gram::AccountState &state) {
	_lastState = state;
	_balanceNano = state.balanceNano;
	_stateKnown = true;
}

void Session::refreshHistory(Fn<void()> done) {
	ensureLoaded();
	if (_keyState.current() == KeyState::None) {
		if (done) {
			done();
		}
		return;
	}
	if (done) {
		_historyDone.push_back(std::move(done));
	}
	if (_historyRequestPending) {
		return;
	}
	requestHistory(0);
}

void Session::requestHistory(int offset) {
	_historyRequestPending = true;
	_api.request(
		Gram::TracesRequest(
			addressFriendly(false),
			kHistoryPageLimit,
			offset),
		[=](const QByteArray &json) {
			_historyRequestPending = false;
			if (_keyState.current() == KeyState::None) {
				_historyDone.clear();
				return;
			}
			auto page = Gram::ParseTraces(json, _address, kHistoryPageLimit);
			if (!page) {
				if (!_historyErrorLogged) {
					_historyErrorLogged = true;
					LOG(("Wallet: traces unavailable (parse failed), "
						"using transactions fallback."));
				}
				requestHistoryFallback(offset);
				return;
			}
			_historyErrorLogged = false;
			applyHistoryPage(offset, std::move(*page));
			for (const auto &callback : base::take(_historyDone)) {
				callback();
			}
		},
		[=](const Gram::ApiError &error) {
			_historyRequestPending = false;
			if (_keyState.current() == KeyState::None) {
				_historyDone.clear();
				return;
			}
			if (!_historyErrorLogged) {
				_historyErrorLogged = true;
				LOG(("Wallet: traces unavailable (%1), "
					"using transactions fallback.").arg(error.message));
			}
			requestHistoryFallback(offset);
		});
}

void Session::requestHistoryFallback(int offset) {
	_historyRequestPending = true;
	_api.request(
		Gram::TransactionsRequest(
			addressFriendly(false),
			kHistoryPageLimit,
			offset),
		[=](const QByteArray &json) {
			_historyRequestPending = false;
			if (_keyState.current() == KeyState::None) {
				_historyDone.clear();
				return;
			}
			if (auto page = Gram::ParseTransactions(
					json,
					_address,
					kHistoryPageLimit)) {
				applyHistoryPage(offset, std::move(*page));
			}
			for (const auto &callback : base::take(_historyDone)) {
				callback();
			}
		},
		[=](const Gram::ApiError &) {
			_historyRequestPending = false;
			if (_keyState.current() == KeyState::None) {
				_historyDone.clear();
				return;
			}
			for (const auto &callback : base::take(_historyDone)) {
				callback();
			}
		});
}

void Session::mergeHistory(std::vector<Gram::TransferItem> &&items) {
	auto changed = false;
	for (auto &item : items) {
		const auto matches = [&](const Gram::TransferItem &existing) {
			return item.traceId.isEmpty()
				? (existing.traceId.isEmpty() && (existing.lt == item.lt))
				: (existing.traceId == item.traceId);
		};
		const auto i = ranges::find_if(_history, matches);
		if (i == _history.end()) {
			_history.push_back(std::move(item));
			changed = true;
		} else {
			const auto differs = (i->status != item.status)
				|| (i->date != item.date)
				|| (i->lt != item.lt)
				|| (i->amountNano != item.amountNano)
				|| (i->comment != item.comment);
			if (differs) {
				*i = std::move(item);
				changed = true;
			}
		}
	}
	if (changed) {
		ranges::sort(_history, [](
				const Gram::TransferItem &a,
				const Gram::TransferItem &b) {
			return (a.date != b.date) ? (a.date > b.date) : (a.lt > b.lt);
		});
		_historyUpdates.fire({});
	}
}

void Session::applyHistoryPage(int offset, Gram::HistoryPage &&page) {
	// The offset ladder counts SERVER rows (traces or transactions), not
	// mapped items. Only a page that extends the current tail (request
	// offset == _historyLoadedOffset) may update _historyHasNext or advance
	// the ladder; the offset-0 poll therefore stops mattering as soon as a
	// deeper page was consumed, and can never clobber a deeper "no more".
	if (offset == _historyLoadedOffset) {
		_historyHasNext = page.hasNext;
		if (page.hasNext) {
			_historyLoadedOffset += kHistoryPageLimit;
		}
	}
	mergeHistory(std::move(page.list));
}

bool Session::historyHasNext() const {
	return _historyHasNext;
}

void Session::loadMoreHistory() {
	ensureLoaded();
	if (_keyState.current() == KeyState::None
		|| _historyRequestPending
		|| !_historyHasNext) {
		return;
	}
	requestHistory(_historyLoadedOffset);
}

#ifdef _DEBUG
void Session::injectDebugHistory(std::vector<Gram::TransferItem> items) {
	mergeHistory(std::move(items));
}
#endif

void Session::startPolling() {
	++_pollingCount;
	updatePollingState();
}

void Session::stopPolling() {
	if (_pollingCount > 0) {
		--_pollingCount;
	}
	updatePollingState();
}

void Session::updatePollingState() {
	const auto wanted = (_pollingCount > 0) || (_pending && !pendingExpired());
	if (!wanted) {
		_pollTimer.cancel();
	} else if (!_pollTimer.isActive()) {
		_pollTimer.callEach(kPollInterval);
		pollTick();
	}
}

bool Session::pollingRequested() const {
	return _pollingCount > 0;
}

void Session::pollTick() {
	ensureLoaded();
	if (_keyState.current() == KeyState::None) {
		return;
	}
	updatePollingState();
	if (!_pollTimer.isActive()) {
		return;
	}
	if (!_stateRequestPending) {
		refreshState();
	}
	if (!_historyRequestPending) {
		refreshHistory();
	}
	if (_pending && !_pendingCheckPending) {
		checkPendingByMessage();
	}
}

int64 Session::balanceNano() const {
	return _balanceNano.current();
}

rpl::producer<int64> Session::balanceNanoValue() const {
	return _balanceNano.value();
}

rpl::producer<bool> Session::stateKnownValue() const {
	return _stateKnown.value();
}

Gram::AccountStatus Session::status() const {
	return _lastState.status;
}

const std::vector<Gram::TransferItem> &Session::history() const {
	return _history;
}

rpl::producer<> Session::historyUpdates() const {
	return _historyUpdates.events();
}

SendState Session::sendState() const {
	return _sendState.current();
}

rpl::producer<SendState> Session::sendStateValue() const {
	return _sendState.value();
}

const std::optional<PendingSend> &Session::pendingSend() const {
	return _pending;
}

void Session::estimateFee(const SendArgs &args, Fn<void(FeeResult)> done) {
	ensureLoaded();
	if (_keyState.current() == KeyState::None) {
		if (done) {
			done(FeeResult{ .error = u"No wallet."_q });
		}
		return;
	}
	const auto knownSeqno = Gram::SeqnoFromStateData(
		_lastState.dataBoc).value_or(0);
	auto request = FeeRequest{
		.publicKey = _keyPair->publicKey,
		.transfer = buildTransferRequest(args, knownSeqno),
		.attachStateInit = (_lastState.status != Gram::AccountStatus::Active),
	};
	_feeEstimator->estimate(request, std::move(done));
}

void Session::send(SendArgs args, Fn<void(QString)> done) {
	ensureLoaded();
	if (_keyState.current() == KeyState::None) {
		if (done) {
			done(u"No wallet."_q);
		}
		return;
	}
	if (args.amountNano <= 0 || args.destination.hash.isEmpty()) {
		if (done) {
			done(u"Invalid send parameters."_q);
		}
		return;
	}
	if (_sendState.current() == SendState::Sending) {
		if (done) {
			done(u"Send already in progress."_q);
		}
		return;
	}
	if (_pending) {
		if (!pendingExpired()) {
			if (done) {
				done(u"Previous transfer is still pending until %1."_q.arg(
					_pending->validUntil + kSendRetryClockMargin));
			}
			return;
		}
		LOG(("Wallet: pending transfer expired, allowing fresh send."));
		_pending.reset();
		updatePollingState();
	}
	_sendState = SendState::Sending;
	refreshState([=](const Gram::AccountState &state) {
		sendWithState(args, state, done);
	}, [=](const Gram::ApiError &error) {
		_sendState = SendState::Idle;
		if (done) {
			done(u"Failed to read wallet state: %1"_q.arg(error.message));
		}
	});
}

void Session::sendWithState(
		SendArgs args,
		const Gram::AccountState &state,
		Fn<void(QString)> done) {
	const auto abort = [&](const QString &error) {
		_sendState = SendState::Idle;
		if (done) {
			done(error);
		}
	};
	auto usedSeqno = quint32(0);
	if (state.status == Gram::AccountStatus::Active) {
		const auto seqno = Gram::SeqnoFromStateData(state.dataBoc);
		if (!seqno) {
			LOG(("Wallet Error: Seqno unavailable for active wallet, "
				"send aborted."));
			abort(u"Wallet state has no seqno, send aborted."_q);
			return;
		}
		usedSeqno = *seqno;
	} else if (state.status == Gram::AccountStatus::Frozen) {
		abort(u"Wallet is frozen."_q);
		return;
	}
	if (args.simulateStaleSeqno && usedSeqno > 0) {
		usedSeqno = usedSeqno - 1;
	}
	const auto request = buildTransferRequest(args, usedSeqno);
	const auto attachStateInit
		= (state.status != Gram::AccountStatus::Active);
	const auto boc = Gram::BuildSignedTransfer(
		*_keyPair,
		request,
		attachStateInit);
	const auto root = Gram::DeserializeBoc(boc);
	if (!root) {
		LOG(("Wallet Error: Failed to deserialize signed transfer."));
		abort(u"Internal error building transfer."_q);
		return;
	}
	const auto hashNorm = Gram::NormalizedExternalHash(*root);
	_pending = PendingSend{
		.messageHashNorm = hashNorm,
		.signedSeqno = usedSeqno,
		.validUntil = request.validUntil,
		.posted = base::unixtime::now(),
		.amountNano = args.amountNano,
		.destination = args.destination,
		.comment = args.comment,
	};
	const auto validUntil = request.validUntil;
	_api.request(
		Gram::SendMessageRequest(boc.toBase64()),
		[=](const QByteArray &json) {
			if (_keyState.current() == KeyState::None) {
				return;
			}
			if (const auto sent = Gram::ParseSendResult(json)) {
				if (_pending && !sent->messageHashNorm.isEmpty()) {
					_pending->messageHashNorm = sent->messageHashNorm;
				}
			}
			if (_pending) {
				_sendState = SendState::Pending;
				updatePollingState();
				LOG(("Wallet: transfer posted, seqno %1, validUntil %2."
					).arg(usedSeqno).arg(validUntil));
			}
			if (done) {
				done(QString());
			}
		},
		[=](const Gram::ApiError &error) {
			if (_keyState.current() == KeyState::None) {
				return;
			}
			_pending.reset();
			_sendState = SendState::Failed;
			updatePollingState();
			LOG(("Wallet Error: sendMessage failed: %1").arg(error.message));
			if (done) {
				done(u"Send failed: %1"_q.arg(error.message));
			}
		});
}

void Session::checkPendingByMessage() {
	if (!_pending || _pendingCheckPending) {
		return;
	}
	_pendingCheckPending = true;
	_api.request(
		Gram::TransactionsByMessageRequest(_pending->messageHashNorm),
		[=](const QByteArray &json) {
			_pendingCheckPending = false;
			if (!_pending || _keyState.current() == KeyState::None) {
				return;
			}
			if (const auto found = Gram::ParseTransactionsByMessageFound(
					json)) {
				if (*found) {
					finishPending();
				}
			}
		},
		[=](const Gram::ApiError &) {
			_pendingCheckPending = false;
		});
}

void Session::checkPendingBySeqno(const Gram::AccountState &state) {
	if (!_pending) {
		return;
	}
	const auto seqno = Gram::SeqnoFromStateData(state.dataBoc);
	if (seqno && (*seqno > _pending->signedSeqno)) {
		finishPending();
	}
}

void Session::finishPending() {
	const auto seqno = _pending ? _pending->signedSeqno : quint32(0);
	LOG(("Wallet: transfer landed, seqno %1.").arg(seqno));
	_pending.reset();
	_sendState = SendState::Idle;
	updatePollingState();
	refreshState();
	refreshHistory();
}

bool Session::pendingExpired() const {
	return _pending
		&& (base::unixtime::now()
			> _pending->validUntil + kSendRetryClockMargin);
}

Gram::TransferRequest Session::buildTransferRequest(
		const SendArgs &args,
		quint32 seqno) const {
	auto message = Gram::TransferMessage{
		.destination = args.destination,
		.bounce = args.bounce,
		.amountNano = args.amountNano,
		.body = (args.comment.isEmpty()
			? std::nullopt
			: std::optional(Gram::BuildCommentBody(args.comment))),
	};
	return Gram::TransferRequest{
		.messages = { std::move(message) },
		.seqno = seqno,
		.walletId = _walletId,
		.validUntil = base::unixtime::now() + kSendValidUntilOffset,
	};
}

} // namespace Wallet
