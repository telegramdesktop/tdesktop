/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session.h"

#include "base/unixtime.h"
#include "gram/ton/gram_message.h"
#include "gram/wallet/gram_wallet_v5.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_engine.h"
#include "wallet/wallet_onramp.h"
#include "wallet/wallet_rates.h"

#include "wallet_engine.hpp"

#include <QtCore/QUuid>

namespace Wallet {
namespace {

namespace engine = wallet_engine;

constexpr auto kPollInterval = 5 * crl::time(1000);
constexpr auto kHistoryPageLimit = 20;
constexpr auto kCollectiblesPollInterval = 60 * crl::time(1000);
constexpr auto kForcedCollectiblesInterval = 10 * crl::time(1000);
constexpr auto kEmptyProofFreshness = 60 * crl::time(1000);
constexpr auto kStreamResyncInterval = 30 * crl::time(1000);
constexpr auto kSendValidUntilOffset = TimeId(300);
constexpr auto kSendRetryClockMargin = TimeId(60);
constexpr auto kEngineProviderBase = "https://toncenter.com";
constexpr auto kEngineRequestTimeoutMs = uint64(15000);

[[nodiscard]] bool SameCollectibles(
		const std::vector<Gram::NftItem> &was,
		const std::vector<Gram::NftItem> &now) {
	if (was.size() != now.size()) {
		return false;
	}
	for (auto i = 0, count = int(was.size()); i != count; ++i) {
		if (was[i].address != now[i].address
			|| was[i].collectionName != now[i].collectionName) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] std::string NewRecordId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

[[nodiscard]] QByteArray PublicKeyBytes(
		const engine::WalletDescriptor &descriptor) {
	const auto &key = descriptor.public_key;
	return QByteArray(
		reinterpret_cast<const char*>(key.data()),
		key.size());
}

[[nodiscard]] Storage::WalletStored StoredFromDescriptor(
		const engine::WalletDescriptor &descriptor,
		bool phraseViewed) {
	return Storage::WalletStored{
		.recordId = QString::fromStdString(descriptor.record_id),
		.address = QString::fromStdString(descriptor.address),
		.publicKey = PublicKeyBytes(descriptor),
		.network = qint32(descriptor.network),
		.secretRef = QString::fromStdString(descriptor.secret_ref.value),
		.phraseViewed = phraseViewed,
	};
}

[[nodiscard]] engine::WalletDescriptor DescriptorFromStored(
		const Storage::WalletStored &stored) {
	const auto &key = stored.publicKey;
	return engine::WalletDescriptor{
		.record_id = stored.recordId.toStdString(),
		.address = stored.address.toStdString(),
		.public_key = std::vector<uint8_t>(
			key.constData(),
			key.constData() + key.size()),
		.network = engine::Network(stored.network),
		.secret_ref = engine::ProtectedSecretRef{
			.value = stored.secretRef.toStdString(),
		},
	};
}

[[nodiscard]] LifecycleError LifecycleErrorFrom(const EngineError &error) {
	if (!error.underlying) {
		return LifecycleError::Failed;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_lifecycle_error::InvalidRecoveryPhrase &) {
		return LifecycleError::InvalidPhrase;
	} catch (...) {
	}
	return LifecycleError::Failed;
}

[[nodiscard]] bool SecretAlreadyGone(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &e) {
		return e.kind == engine::ProtectedSecretHostErrorKind::kNotFound;
	} catch (...) {
	}
	return false;
}

[[nodiscard]] std::vector<QString> SplitPhrase(const std::string &phrase) {
	const auto words = QString::fromStdString(phrase).split(
		QChar(' '),
		Qt::SkipEmptyParts);
	return std::vector<QString>(words.begin(), words.end());
}

[[nodiscard]] std::optional<Gram::NftItem> CollectibleFromEngine(
		const engine::NftItem &item) {
	const auto parsed = Gram::ParseAddress(
		QString::fromStdString(item.address));
	if (!parsed) {
		LOG(("Wallet Error: engine nft address is not parseable."));
		return std::nullopt;
	}
	const auto addressOrEmpty = [](const std::optional<std::string> &value) {
		if (!value) {
			return Gram::Address();
		}
		const auto parsed = Gram::ParseAddress(
			QString::fromStdString(*value));
		return parsed ? parsed->address : Gram::Address();
	};
	const auto contentValue = [&](const std::string &key) {
		const auto i = item.content.find(key);
		return (i != item.content.end())
			? QString::fromStdString(i->second)
			: QString();
	};
	auto result = Gram::NftItem();
	result.address = parsed->address;
	result.collection = addressOrEmpty(item.collection_address);
	result.realOwner = addressOrEmpty(item.real_owner);
	result.index = QString::fromStdString(item.index);
	result.contentUri = contentValue("uri");
	result.domain = contentValue("domain");
	result.contentUriHttps = result.contentUri.startsWith(u"https://"_q);
	result.onSale = item.on_sale;
	if (item.collection && item.collection->name) {
		result.collectionName = QString::fromStdString(
			*item.collection->name);
	}
	Gram::ClassifyNftKind(result);
	return result;
}

} // namespace

std::vector<Gram::NftItem> CollectiblesFromEngine(
		const engine::NftList &list) {
	auto result = std::vector<Gram::NftItem>();
	result.reserve(list.items.size());
	for (const auto &item : list.items) {
		if (auto mapped = CollectibleFromEngine(item)) {
			result.push_back(std::move(*mapped));
		}
	}
	return result;
}

Session::Session(not_null<Main::Session*> session)
: _session(session)
, _api(session)
, _engine(std::make_unique<Engine>(session, &_api))
, _feeEstimator(MakeFeeEstimator(&_api))
, _rates(std::make_unique<Rates>(session))
, _onramp(std::make_unique<Onramp>(session))
, _stream(std::make_unique<Stream>(&_api, [=](StreamRefresh wanted) {
	applyStreamRefresh(wanted);
}))
, _pollTimer([=] { pollTick(); }) {
}

Session::~Session() {
	_panel = nullptr;
}

Onramp &Session::onramp() {
	return *_onramp;
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
	if (!stored) {
		return;
	}
	if (applyDescriptor(
			DescriptorFromStored(*stored),
			stored->phraseViewed ? KeyState::Imported : KeyState::Created)) {
		_phraseUnviewed = !stored->phraseViewed;
	}
}

bool Session::applyDescriptor(
		engine::WalletDescriptor descriptor,
		KeyState state) {
	const auto parsed = Gram::ParseAddress(
		QString::fromStdString(descriptor.address));
	if (!parsed) {
		LOG(("Wallet Error: engine descriptor address is not parseable."));
		return false;
	}
	_descriptor = std::make_unique<engine::WalletDescriptor>(
		std::move(descriptor));
	_address = parsed->address;
	_keyState = state;
	updateEngineClient();
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

void Session::create(Fn<void(LifecycleError)> done) {
	ensureLoaded();
	if (_keyState.current() != KeyState::None || _lifecyclePending) {
		if (done) {
			done(LifecycleError::Failed);
		}
		return;
	}
	_lifecyclePending = true;
	const auto lifecycle = _engine->lifecycle();
	const auto recordId = NewRecordId();
	_engine->run([lifecycle, recordId] {
		return lifecycle->create_wallet(engine::CreateWalletRequest{
			.record_id = recordId,
			.network = engine::Network::kMainnet,
		});
	}, [=, this](engine::CreatedWallet created) {
		_lifecyclePending = false;
		const auto applied = applyDescriptor(
			std::move(created.descriptor),
			KeyState::Created);
		if (!applied) {
			if (done) {
				done(LifecycleError::Failed);
			}
			return;
		}
		_session->local().writeWallet(
			StoredFromDescriptor(*_descriptor, false));
		_phraseUnviewed = true;
		pollTick();
		if (done) {
			done(LifecycleError::None);
		}
	}, [=, this](EngineError error) {
		_lifecyclePending = false;
		LOG(("Wallet Error: engine create_wallet failed: %1"
			).arg(error.message));
		if (done) {
			done(LifecycleErrorFrom(error));
		}
	});
}

void Session::import(
		std::vector<QString> words,
		Fn<void(LifecycleError)> done) {
	ensureLoaded();
	if (_keyState.current() != KeyState::None
		|| _lifecyclePending
		|| words.empty()) {
		if (done) {
			done(LifecycleError::Failed);
		}
		return;
	}
	auto recovery = std::vector<std::string>();
	recovery.reserve(words.size());
	for (const auto &word : words) {
		recovery.push_back(word.trimmed().toLower().toStdString());
	}
	_lifecyclePending = true;
	const auto lifecycle = _engine->lifecycle();
	const auto recordId = NewRecordId();
	_engine->run([lifecycle, recordId, recovery = std::move(recovery)] {
		return lifecycle->import_wallet(engine::ImportWalletRequest{
			.record_id = recordId,
			.network = engine::Network::kMainnet,
			.recovery_words = recovery,
		});
	}, [=, this](engine::WalletDescriptor descriptor) {
		_lifecyclePending = false;
		const auto applied = applyDescriptor(
			std::move(descriptor),
			KeyState::Imported);
		if (!applied) {
			if (done) {
				done(LifecycleError::Failed);
			}
			return;
		}
		_session->local().writeWallet(
			StoredFromDescriptor(*_descriptor, true));
		_phraseUnviewed = false;
		pollTick();
		if (done) {
			done(LifecycleError::None);
		}
	}, [=, this](EngineError error) {
		_lifecyclePending = false;
		LOG(("Wallet Error: engine import_wallet failed: %1"
			).arg(error.message));
		if (done) {
			done(LifecycleErrorFrom(error));
		}
	});
}

void Session::remove(Fn<void(LifecycleError)> done) {
	ensureLoaded();
	if (!_descriptor) {
		_session->local().writeWallet(Storage::WalletStored());
		_keyState = KeyState::None;
		_phraseUnviewed = false;
		clearNetworkState();
		if (done) {
			done(LifecycleError::None);
		}
		return;
	} else if (_lifecyclePending) {
		if (done) {
			done(LifecycleError::Failed);
		}
		return;
	}
	_lifecyclePending = true;
	const auto lifecycle = _engine->lifecycle();
	const auto descriptor = *_descriptor;
	const auto cleared = [=, this] {
		_lifecyclePending = false;
		_session->local().writeWallet(Storage::WalletStored());
		_descriptor = nullptr;
		_address = Gram::Address();
		_keyState = KeyState::None;
		_phraseUnviewed = false;
		clearNetworkState();
		if (done) {
			done(LifecycleError::None);
		}
	};
	_engine->run([lifecycle, descriptor] {
		lifecycle->delete_wallet(descriptor);
	}, cleared, [=, this](EngineError error) {
		if (SecretAlreadyGone(error)) {
			LOG(("Wallet Warning: engine secret already gone, "
				"dropping the wallet record."));
			cleared();
			return;
		}
		_lifecyclePending = false;
		LOG(("Wallet Error: engine delete_wallet failed: %1"
			).arg(error.message));
		if (done) {
			done(LifecycleErrorFrom(error));
		}
	});
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

void Session::revealPhrase(
		Fn<void(std::vector<QString>)> done,
		Fn<void(LifecycleError)> fail) {
	ensureLoaded();
	if (!_descriptor) {
		if (fail) {
			fail(LifecycleError::Failed);
		}
		return;
	}
	const auto lifecycle = _engine->lifecycle();
	const auto descriptor = *_descriptor;
	_engine->run([lifecycle, descriptor] {
		return lifecycle->reveal_recovery_phrase(descriptor);
	}, [=](engine::RecoveryPhrase phrase) {
		if (done) {
			done(SplitPhrase(phrase.phrase));
		}
	}, [=](EngineError error) {
		LOG(("Wallet Error: engine reveal_recovery_phrase failed: %1"
			).arg(error.message));
		if (fail) {
			fail(LifecycleErrorFrom(error));
		}
	});
}

void Session::clearNetworkState() {
	++_networkGeneration;
	_balanceNano = 0;
	_lastState = Gram::AccountState();
	_engineStatus = Gram::AccountStatus::NonExisting;
	_stateKnown = false;
	_stateRefreshedAt = 0;
	_history.clear();
	_historyUpdates.fire({});
	_historyErrorLogged = false;
	_historyHasNext = false;
	_historyLoadedOffset = 0;
	_historyRefreshedAt = 0;
	_collectibles.clear();
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
	updateEngineClient();
}

void Session::updateEngineClient() {
	const auto wanted = _descriptor
		? QString::fromStdString(_descriptor->address)
		: QString();
	if (_engineStopping) {
		return;
	}
	if (const auto client = _engine->client()) {
		if (wanted == _engineClientAddress) {
			return;
		}
		_engineStopping = true;
		_engine->runQuick([client] {
			client->cancel_refresh();
			client->cancel_refresh_nfts();
			client->cancel_load_more_nfts();
		}, [] {}, [](EngineError) {});
		_engine->stopClient([=, this] {
			_engineStopping = false;
			_engineClientAddress = QString();
			updateEngineClient();
		});
		return;
	}
	if (wanted.isEmpty()) {
		return;
	}
	const auto config = engine::WalletClientConfig{
		.record_id = _descriptor->record_id,
		.address = _descriptor->address,
		.public_key = _descriptor->public_key,
		.local_secret_ref = _descriptor->secret_ref,
		.network = _descriptor->network,
		.send_validity_seconds = 300,
		.resolution_margin_seconds = 60,
		.providers = engine::ProviderConfig{
			.toncenter_base_url = kEngineProviderBase,
			.request_timeout_ms = kEngineRequestTimeoutMs,
		},
	};
	try {
		_engine->startClient(config);
		_engineClientAddress = wanted;
	} catch (const std::exception &e) {
		const auto what = QString::fromUtf8(e.what());
		const auto message = what.isEmpty()
			? QString::fromUtf8(typeid(e).name())
			: what;
		LOG(("Wallet Error: engine client start failed: %1").arg(message));
	}
}

void Session::requestEngineRefresh() {
	if (_engineRefreshPending || _engineStopping || !_engine->client()) {
		return;
	}
	_engineRefreshPending = true;
	const auto client = _engine->client();
	const auto generation = _networkGeneration;
	_engine->run([client] {
		return client->refresh();
	}, [=, this](engine::WalletUpdate update) {
		_engineRefreshPending = false;
		if (generation != _networkGeneration) {
			return;
		}
		applyEngineUpdate(update);
	}, [=, this](EngineError error) {
		_engineRefreshPending = false;
		if (generation != _networkGeneration) {
			return;
		}
		LOG(("Wallet Error: engine refresh failed: %1, "
			"keeping last-good state.").arg(error.message));
	});
}

void Session::applyEngineUpdate(const engine::WalletUpdate &update) {
	if (update.outcome != engine::WalletOperationOutcome::kCompleted) {
		LOG(("Wallet: engine refresh outcome %1, keeping last-good state."
			).arg(int(update.outcome)));
		return;
	}
	const auto &snapshot = update.snapshot;
	if (snapshot.account_resource.phase != engine::ResourcePhase::kReady
		|| !snapshot.account) {
		return;
	}
	const auto &account = *snapshot.account;
	auto ok = false;
	const auto balance = QString::fromStdString(
		account.balance_nanograms).toLongLong(&ok);
	if (!ok) {
		LOG(("Wallet Error: engine balance parse failed: %1"
			).arg(QString::fromStdString(account.balance_nanograms)));
		return;
	}
	auto mapped = _engineStatus;
	switch (account.status) {
	case engine::AccountStatus::kNonexistent:
		mapped = Gram::AccountStatus::NonExisting;
		break;
	case engine::AccountStatus::kUninitialized:
		mapped = Gram::AccountStatus::Uninit;
		break;
	case engine::AccountStatus::kActive:
		mapped = Gram::AccountStatus::Active;
		break;
	case engine::AccountStatus::kFrozen:
		mapped = Gram::AccountStatus::Frozen;
		break;
	case engine::AccountStatus::kUnknown:
		LOG(("Wallet: engine account status unknown, keeping last-good."));
		break;
	}
	_balanceNano = balance;
	_engineStatus = mapped;
	_stateKnown = true;
	_stateRefreshedAt = crl::now();
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
			_lastState = *state;
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
	if (_engineStopping || !_engine->client()) {
		return;
	}
	_collectiblesRefreshedAt = crl::now();
	requestCollectibles(false);
}

void Session::requestCollectibles(bool more) {
	const auto client = _engine->client();
	if (_engineStopping || !client) {
		return;
	}
	_collectiblesRequestPending = true;
	const auto generation = _networkGeneration;
	_engine->run([client, more] {
		return more
			? client->load_more_nfts()
			: client->refresh_nfts();
	}, [=, this](engine::WalletUpdate update) {
		_collectiblesRequestPending = false;
		if (generation != _networkGeneration) {
			return;
		}
		applyCollectiblesUpdate(update, more);
	}, [=, this](EngineError error) {
		_collectiblesRequestPending = false;
		if (generation != _networkGeneration) {
			return;
		}
		LOG(("Wallet Error: engine nft %1 failed: %2, "
			"keeping last-good collectibles."
			).arg(more ? u"load_more"_q : u"refresh"_q
			).arg(error.message));
	});
}

void Session::applyCollectiblesUpdate(
		const engine::WalletUpdate &update,
		bool more) {
#ifdef _DEBUG
	if (_collectiblesInjected) {
		return;
	}
#endif // _DEBUG
	if (update.outcome != engine::WalletOperationOutcome::kCompleted) {
		LOG(("Wallet: engine nft outcome %1, keeping last-good collectibles."
			).arg(int(update.outcome)));
		return;
	}
	const auto &nfts = update.snapshot.nfts;
	const auto &resource = more ? nfts.pagination_resource : nfts.resource;
	if (resource.phase != engine::ResourcePhase::kReady) {
		return;
	}
	if (nfts.has_more) {
		requestCollectibles(true);
		return;
	}
	_collectiblesCompletedAt = crl::now();
	auto loaded = CollectiblesFromEngine(nfts);
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
	if ((!streaming || stale(_stateRefreshedAt)) && !_engineRefreshPending) {
		requestEngineRefresh();
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
		requestEngineRefresh();
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
	return _engineStatus;
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
		.publicKey = PublicKeyBytes(*_descriptor),
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
	LOG(("Wallet Error: legacy send refused, "
		"no signing key until the engine send lands."));
	if (done) {
		done(tr::lng_wallet_send_unavailable(tr::now));
	}
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
	requestEngineRefresh();
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
		.walletId = Gram::kDefaultWalletId,
		.validUntil = base::unixtime::now() + kSendValidUntilOffset,
	};
}

} // namespace Wallet
