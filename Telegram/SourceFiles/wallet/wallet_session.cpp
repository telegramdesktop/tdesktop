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
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_rates.h"

namespace Wallet {
namespace {

constexpr auto kPollInterval = 5 * crl::time(1000);
constexpr auto kHistoryPageLimit = 20;
constexpr auto kCollectiblesPageLimit = 50;
constexpr auto kCollectiblesPollInterval = 60 * crl::time(1000);
constexpr auto kForcedCollectiblesInterval = 10 * crl::time(1000);
constexpr auto kEmptyProofFreshness = 60 * crl::time(1000);
constexpr auto kStreamResyncInterval = 30 * crl::time(1000);
constexpr auto kSendValidUntilOffset = TimeId(300);
constexpr auto kSendRetryClockMargin = TimeId(60);

[[nodiscard]] bool SameCollectibles(
		const std::vector<Gram::NftItem> &was,
		const std::vector<Gram::NftItem> &now) {
	if (was.size() != now.size()) {
		return false;
	}
	for (auto i = 0, count = int(was.size()); i != count; ++i) {
		if (was[i].address != now[i].address) {
			return false;
		}
	}
	return true;
}

} // namespace

Session::Session(not_null<Main::Session*> session)
: _session(session)
, _api(session)
, _feeEstimator(MakeFeeEstimator(&_api))
, _rates(std::make_unique<Rates>(session))
, _stream(std::make_unique<Stream>(&_api, [=](StreamRefresh wanted) {
	applyStreamRefresh(wanted);
}))
, _pollTimer([=] { pollTick(); }) {
}

Session::~Session() {
	_panel = nullptr;
}

Rates &Session::rates() {
	return *_rates;
}

Ui::SeparatePanel *Session::panel() const {
	return _panel.get();
}

void Session::setPanel(std::unique_ptr<Ui::SeparatePanel> panel) {
	_panel = std::move(panel);
}

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

bool Session::provenEmpty() const {
	const auto now = crl::now();
	const auto fresh = [&](crl::time completed) {
		return completed && (now - completed <= kEmptyProofFreshness);
	};
	return _stateKnown.current()
		&& (_balanceNano.current() == 0)
		&& _history.empty()
		&& !_historyHasNext
		&& _collectibles.empty()
		&& !_pending
		&& (_sendState.current() != SendState::Sending)
		&& fresh(_stateRefreshedAt)
		&& fresh(_historyRefreshedAt)
		&& fresh(_collectiblesCompletedAt);
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
	++_networkGeneration;
	_balanceNano = 0;
	_lastState = Gram::AccountState();
	_stateKnown = false;
	_stateRefreshedAt = 0;
	_history.clear();
	_historyUpdates.fire({});
	_historyErrorLogged = false;
	_historyHasNext = false;
	_historyLoadedOffset = 0;
	_historyRefreshedAt = 0;
	_collectibles.clear();
	_collectiblesLoading.clear();
	_collectiblesTab = false;
	_collectiblesRefreshedAt = 0;
	_collectiblesCompletedAt = 0;
	_collectiblesRequestPending = false;
#ifdef _DEBUG
	_collectiblesInjected = false;
#endif // _DEBUG
	_collectiblesUpdates.fire({});
	_pending.reset();
	_sendState = SendState::Idle;
	_pollingCount = 0;
	_pollTimer.cancel();
	_stream->stop();
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
	const auto generation = _networkGeneration;
	_api.request(
		Gram::AddressInformationRequest(addressFriendly(false)),
		[=](const QByteArray &json) {
			if (generation != _networkGeneration) {
				return;
			}
			_stateRequestPending = false;
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
			if (generation != _networkGeneration) {
				return;
			}
			_stateRequestPending = false;
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
	_stateRefreshedAt = crl::now();
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
	const auto generation = _networkGeneration;
	_api.request(
		Gram::TracesRequest(
			addressFriendly(false),
			kHistoryPageLimit,
			offset),
		[=](const QByteArray &json) {
			if (generation != _networkGeneration) {
				return;
			}
			_historyRequestPending = false;
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
			if (generation != _networkGeneration) {
				return;
			}
			_historyRequestPending = false;
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
	const auto generation = _networkGeneration;
	_api.request(
		Gram::TransactionsRequest(
			addressFriendly(false),
			kHistoryPageLimit,
			offset),
		[=](const QByteArray &json) {
			if (generation != _networkGeneration) {
				return;
			}
			_historyRequestPending = false;
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
			if (generation != _networkGeneration) {
				return;
			}
			_historyRequestPending = false;
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
	_historyRefreshedAt = crl::now();
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

void Session::refreshCollectibles(bool force) {
	ensureLoaded();
#ifdef _DEBUG
	if (_collectiblesInjected) {
		return;
	}
#endif // _DEBUG
	const auto interval = force
		? kForcedCollectiblesInterval
		: kCollectiblesPollInterval;
	if (_keyState.current() == KeyState::None
		|| _collectiblesRequestPending
		|| (_collectiblesRefreshedAt
			&& (crl::now() - _collectiblesRefreshedAt < interval))) {
		return;
	}
	_collectiblesRefreshedAt = crl::now();
	requestCollectibles(0);
}

void Session::requestCollectibles(int offset) {
	_collectiblesRequestPending = true;
	const auto generation = _networkGeneration;
	_api.request(
		Gram::NftItemsByOwnerRequest(
			addressFriendly(false),
			kCollectiblesPageLimit,
			offset),
		[=](const QByteArray &json) {
			if (generation != _networkGeneration) {
				return;
			}
			_collectiblesRequestPending = false;
			auto page = Gram::ParseNftItems(json, kCollectiblesPageLimit);
			if (!page) {
				_collectiblesLoading.clear();
				return;
			}
			applyCollectiblesPage(offset, std::move(*page));
		},
		[=](const Gram::ApiError &) {
			if (generation != _networkGeneration) {
				return;
			}
			_collectiblesRequestPending = false;
			_collectiblesLoading.clear();
		});
}

void Session::applyCollectiblesPage(int offset, Gram::NftPage &&page) {
#ifdef _DEBUG
	if (_collectiblesInjected) {
		_collectiblesLoading.clear();
		return;
	}
#endif // _DEBUG
	if (!offset) {
		_collectiblesLoading.clear();
	}
	_collectiblesLoading.insert(
		_collectiblesLoading.end(),
		std::make_move_iterator(page.list.begin()),
		std::make_move_iterator(page.list.end()));
	if (page.hasNext) {
		requestCollectibles(offset + kCollectiblesPageLimit);
		return;
	}
	_collectiblesCompletedAt = crl::now();
	auto loaded = base::take(_collectiblesLoading);
	if (SameCollectibles(_collectibles, loaded)) {
		return;
	}
	setCollectibles(std::move(loaded));
}

void Session::setCollectibles(std::vector<Gram::NftItem> &&list) {
	_collectibles = std::move(list);
	if (_collectibles.empty()) {
		_collectiblesTab = false;
	}
	for (const auto &item : _collectibles) {
		_collectibleInfo[Gram::FormatRaw(item.address)] = item;
	}
	_collectiblesUpdates.fire({});
}

void Session::resolveCollectibleInfo(
		const Gram::Address &item,
		Fn<void(const Gram::NftItem &)> done) {
	const auto key = Gram::FormatRaw(item);
	const auto i = _collectibleInfo.find(key);
	if (i != end(_collectibleInfo)) {
		done(i->second);
		return;
	}
	auto &waiters = _collectibleInfoWaiters[key];
	const auto first = waiters.empty();
	waiters.push_back(std::move(done));
	if (!first) {
		return;
	}
	const auto finish = [=](Gram::NftItem found, bool remember) {
		if (remember) {
			_collectibleInfo[key] = found;
		}
		for (const auto &callback : base::take(_collectibleInfoWaiters[key])) {
			callback(found);
		}
		_collectibleInfoWaiters.remove(key);
	};
	_api.request(
		Gram::NftItemByAddressRequest(item),
		[=](const QByteArray &json) {
			auto found = Gram::NftItem();
			if (const auto page = Gram::ParseNftItems(json, 0)) {
				for (const auto &entry : page->list) {
					if (entry.address == item) {
						found = entry;
						break;
					}
				}
			}
			finish(found, true);
		},
		[=](const Gram::ApiError &) {
			finish(Gram::NftItem(), false);
		});
}

#ifdef _DEBUG
void Session::injectDebugHistory(std::vector<Gram::TransferItem> items) {
	mergeHistory(std::move(items));
}

void Session::injectDebugCollectibles(std::vector<Gram::NftItem> items) {
	_collectiblesInjected = true;
	_collectiblesLoading.clear();
	setCollectibles(std::move(items));
}

void Session::debugRawRequest(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail) {
	_api.debugRawRequest(request, std::move(done), std::move(fail));
}

void Session::debugProductRequest(
		const Gram::HttpRequest &request,
		Fn<void(const QByteArray &)> done,
		Fn<void(const Gram::ApiError &)> fail) {
	_api.request(request, std::move(done), std::move(fail));
}

void Session::debugStallNextRequest(
		const QString &endpoint,
		Fn<void()> swallowed) {
	_api.debugStallNextRequest(endpoint, std::move(swallowed));
}

void Session::debugReleaseStalledAnswer() {
	_api.debugReleaseStalledAnswer();
}

void Session::debugClearNetworkState() {
	if (_keyState.current() == KeyState::None) {
		return;
	}
	_debugClearedPollingCount = _pollingCount;
	clearNetworkState();
}

void Session::debugRestoreNetworkState() {
	_pollingCount += base::take(_debugClearedPollingCount);
	updatePollingState();
}

void Session::debugSetRefreshAges(crl::time age) {
	const auto stamp = crl::now() - age;
	const auto set = [&](crl::time &field) {
		if (field) {
			field = stamp;
		}
	};
	set(_stateRefreshedAt);
	set(_historyRefreshedAt);
	set(_collectiblesCompletedAt);
}

int Session::debugPendingCount() const {
	return _api.debugPendingCount();
}

void Session::debugStreamUseFakeEndpoint() {
	_stream->debugUseFakeEndpoint();
}

void Session::debugStreamFailAcquires(bool fail) {
	_stream->debugFailAcquires(fail);
}

void Session::debugStreamDeliverFrame(const QByteArray &frame) {
	_stream->debugDeliverFrame(frame);
}

void Session::debugStreamDropConnection() {
	_stream->debugDropConnection();
}

void Session::debugStreamExpireNow() {
	_stream->debugExpireNow();
}

bool Session::debugStreamHealthy() const {
	return _stream->healthy();
}

int Session::debugStreamAcquireCount() const {
	return _stream->debugAcquireCount();
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
	if (wanted && (_keyState.current() != KeyState::None)) {
		_stream->start(_address);
	} else {
		_stream->stop();
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
	const auto streaming = _stream->healthy();
	const auto stale = [&](crl::time at) {
		return !at || (crl::now() - at >= kStreamResyncInterval);
	};
	if ((!streaming || stale(_stateRefreshedAt)) && !_stateRequestPending) {
		refreshState();
	}
	if ((!streaming || stale(_historyRefreshedAt))
		&& !_historyRequestPending) {
		refreshHistory();
	}
	refreshCollectibles();
	if (_pending && !_pendingCheckPending) {
		checkPendingByMessage();
	}
}

void Session::applyStreamRefresh(StreamRefresh wanted) {
	if (wanted.state) {
		refreshState();
	}
	if (wanted.history) {
		refreshHistory();
	}
	if (wanted.collectibles) {
		refreshCollectibles(true);
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

const std::vector<Gram::NftItem> &Session::collectibles() const {
	return _collectibles;
}

rpl::producer<> Session::collectiblesUpdates() const {
	return _collectiblesUpdates.events();
}

bool Session::collectiblesTab() const {
	return _collectiblesTab.current();
}

rpl::producer<bool> Session::collectiblesTabValue() const {
	return _collectiblesTab.value();
}

void Session::setCollectiblesTab(bool value) {
	_collectiblesTab = value && !_collectibles.empty();
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
	const auto keepPending = [=](const QString &reason) {
		if (_pending) {
			_sendState = SendState::Pending;
			updatePollingState();
			LOG(("Wallet: %1, seqno %2, validUntil %3."
				).arg(reason).arg(usedSeqno).arg(validUntil));
		}
		if (done) {
			done(QString());
		}
	};
	const auto generation = _networkGeneration;
	_api.request(
		Gram::SendMessageRequest(boc.toBase64()),
		[=](const QByteArray &json) {
			if (generation != _networkGeneration) {
				return;
			}
			if (const auto sent = Gram::ParseSendResult(json)) {
				if (_pending && !sent->messageHashNorm.isEmpty()) {
					_pending->messageHashNorm = sent->messageHashNorm;
				}
			}
			keepPending(u"transfer posted"_q);
		},
		[=](const Gram::ApiError &error) {
			if (generation != _networkGeneration) {
				return;
			}
			if (Api::IsTimeoutError(error)) {
				keepPending(u"transfer answer timed out, still pending"_q);
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
	const auto generation = _networkGeneration;
	_api.request(
		Gram::TransactionsByMessageRequest(_pending->messageHashNorm),
		[=](const QByteArray &json) {
			if (generation != _networkGeneration) {
				return;
			}
			_pendingCheckPending = false;
			if (!_pending) {
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
			if (generation != _networkGeneration) {
				return;
			}
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
