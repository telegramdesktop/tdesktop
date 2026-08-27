/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session.h"

#include "base/unixtime.h"
#include "main/main_session.h"
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_engine.h"
#include "wallet/wallet_onramp.h"
#include "wallet/wallet_rates.h"
#include "wallet/wallet_user_addresses.h"

#include "wallet_engine.hpp"

#include <QtCore/QUuid>

#include <limits>

namespace Wallet {
namespace {

namespace engine = wallet_engine;

constexpr auto kPollInterval = 5 * crl::time(1000);
constexpr auto kCollectiblesPollInterval = 60 * crl::time(1000);
// The server reads the balance from toncenter and caches it for about
// thirty seconds, so two reads inside one such window return the same
// answer. This floor is twice that window: whatever jitter the poll tick,
// a stream hint and a pushed update add between two requests, the later
// one can never land inside the cache window the earlier one filled.
constexpr auto kStateRefreshInterval = 60 * crl::time(1000);
// A wallet.getState that fails for anything but WALLET_UNAVAILABLE leaves the
// presence at Unknown, which is also what "the first request has not answered
// yet" reads as, so the cold-open gate would otherwise stay closed on an
// unlabelled indicator for as long as the server keeps failing. After this
// many consecutive failed attempts the lane stops claiming to be loading and
// settles to a stated face; the 60-second floor keeps retrying underneath and
// one applied state clears the latch again.
constexpr auto kStateFailuresBeforeStated = 2;
constexpr auto kForcedCollectiblesInterval = 10 * crl::time(1000);
constexpr auto kStreamResyncInterval = 30 * crl::time(1000);
constexpr auto kEngineProviderBase = "https://toncenter.com";
constexpr auto kEngineRequestTimeoutMs = uint64(15000);

[[nodiscard]] std::optional<int64> DecimalInt64(const std::string &value) {
	auto ok = false;
	const auto result = QString::fromStdString(value).toLongLong(&ok);
	return ok ? std::make_optional(result) : std::nullopt;
}

[[nodiscard]] std::optional<uint64> DecimalUint64(
		const std::string &value) {
	auto ok = false;
	const auto result = QString::fromStdString(value).toULongLong(&ok);
	return ok ? std::make_optional(result) : std::nullopt;
}

[[nodiscard]] bool SameHistory(
		const std::vector<TransferItem> &was,
		const std::vector<TransferItem> &now) {
	return (was == now);
}

[[nodiscard]] bool SameCollectibles(
		const std::vector<Gram::NftItem> &was,
		const std::vector<Gram::NftItem> &now) {
	return (was == now);
}

[[nodiscard]] std::string NewRecordId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

[[nodiscard]] SendError SendErrorFrom(const EngineError &error) {
	if (!error.underlying) {
		return SendError::Failed;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error::LocalSigningUnavailable &) {
		return SendError::SigningUnavailable;
	} catch (const engine::wallet_client_error::InsufficientBalance &) {
		return SendError::InsufficientBalance;
	} catch (const engine::wallet_client_error::InsufficientBalanceForFees &) {
		return SendError::InsufficientFees;
	} catch (const engine::wallet_client_error
			::PreviousSubmissionUnresolved &) {
		return SendError::PreviousUnresolved;
	} catch (const engine::wallet_client_error::WalletSeqnoNotAdvanced &) {
		return SendError::PreviousUnresolved;
	} catch (const engine::wallet_client_error::SendAlreadyInProgress &) {
		return SendError::AlreadySending;
	} catch (const engine::wallet_client_error
			::SendPreviewAlreadyInProgress &) {
		return SendError::AlreadySending;
	} catch (const engine::wallet_client_error::InvalidSendRequest &) {
		return SendError::InvalidRequest;
	} catch (...) {
	}
	return SendError::Failed;
}

[[nodiscard]] bool IsSubmissionUnknown(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error::SubmissionUnknown &) {
		return true;
	} catch (...) {
	}
	return false;
}

[[nodiscard]] bool IsPreviewKilled(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error::StateUnavailable &) {
		return true;
	} catch (...) {
	}
	return false;
}

[[nodiscard]] engine::SendIntent IntentFromArgs(const SendArgs &args) {
	auto message = engine::SendMessage{
		.destination = FormatFriendly(
			args.destination,
			args.bounce).toStdString(),
		.amount = engine::SendAmount(engine::SendAmount::kExact{
			.nanograms = QString::number(args.amountNano).toStdString(),
		}),
		.body = (args.comment.isEmpty()
			? engine::SendMessageBody(engine::SendMessageBody::kEmpty{})
			: engine::SendMessageBody(engine::SendMessageBody::kComment{
				.text = args.comment.toStdString(),
			})),
		.bounce = args.bounce,
		.state_init = std::nullopt,
	};
	return engine::SendIntent{
		.expiration = engine::SendExpiration(
			engine::SendExpiration::kEngineDefault{}),
		.messages = { std::move(message) },
	};
}

[[nodiscard]] bool TerminalSendPhase(engine::SendPhase phase) {
	switch (phase) {
	case engine::SendPhase::kIdle:
	case engine::SendPhase::kConfirmed:
	case engine::SendPhase::kReplaced:
	case engine::SendPhase::kSequenceNumberConsumed:
	case engine::SendPhase::kExpired:
	case engine::SendPhase::kSuperseded:
	case engine::SendPhase::kFailed:
	case engine::SendPhase::kCancelled:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] std::optional<Gram::NftItem> CollectibleFromEngine(
		const engine::NftItem &item) {
	const auto address = CanonicalAddress(
		QString::fromStdString(item.address));
	if (address.isEmpty()) {
		LOG(("Wallet Error: engine nft address is not parseable."));
		return std::nullopt;
	}
	const auto addressOrEmpty = [](const std::optional<std::string> &value) {
		return value
			? CanonicalAddress(QString::fromStdString(*value))
			: QString();
	};
	const auto contentValue = [&](const std::string &key) {
		const auto i = item.content.find(key);
		return (i != item.content.end())
			? QString::fromStdString(i->second)
			: QString();
	};
	auto result = Gram::NftItem();
	result.address = address;
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

[[nodiscard]] std::optional<TransferItem> HistoryItemFromEngine(
		const engine::ActivityItem &item) {
	constexpr auto kMaxTimestamp = uint64(std::numeric_limits<TimeId>::max());
	const auto amount = DecimalInt64(item.amount_nanograms);
	const auto fee = DecimalInt64(item.transaction_fee_nanograms);
	const auto lt = DecimalUint64(item.logical_time);
	if (!amount || !fee || !lt || (item.timestamp > kMaxTimestamp)) {
		LOG(("Wallet Error: engine activity item %1 has a bad number."
			).arg(QString::fromStdString(item.id)));
		return std::nullopt;
	}
	auto result = TransferItem();
	result.kind = TransferItem::Kind::Transfer;
	result.incoming
		= (item.direction == engine::ActivityDirection::kReceived);
	if (item.counterparty) {
		result.counterparty = CanonicalAddress(
			QString::fromStdString(*item.counterparty));
	}
	result.amountNano = *amount;
	result.feeNano = *fee;
	if (item.comment) {
		result.comment = QString::fromStdString(*item.comment);
	}
	result.date = TimeId(item.timestamp);
	result.lt = *lt;
	result.traceId = QByteArray::fromBase64(
		QByteArray::fromStdString(item.transaction_hash));
	result.status = (item.status == engine::ActivityStatus::kSuccess)
		? TransferItem::Status::Success
		: TransferItem::Status::Failure;
	return result;
}

} // namespace

std::vector<TransferItem> HistoryFromEngine(
		const std::vector<engine::ActivityItem> &items) {
	auto result = std::vector<TransferItem>();
	result.reserve(items.size());
	for (const auto &item : items) {
		if (auto mapped = HistoryItemFromEngine(item)) {
			result.push_back(std::move(*mapped));
		}
	}
	return result;
}

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
, _stateApi(&session->mtp())
, _engine(std::make_unique<Engine>(session, &_api))
, _rates(std::make_unique<Rates>(session))
, _onramp(std::make_unique<Onramp>(session))
, _userAddresses(std::make_unique<UserAddresses>(session))
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

UserAddresses &Session::userAddresses() {
	return *_userAddresses;
}

Ui::SeparatePanel *Session::panel() const {
	return _panel.get();
}

void Session::setPanel(std::unique_ptr<Ui::SeparatePanel> panel) {
	_panel = std::move(panel);
	if (!_panel) {
		// _collectiblesPaged is a fact about one overview's scroll
		// position, so it ends with the panel that owned it: otherwise
		// the periodic refresh stays refused for the rest of the
		// session and a collectible received while the panel was closed
		// never appears.
		_collectiblesPaged = false;
	}
}

void Session::ensureLoaded() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	refreshState();
}

Presence Session::presence() {
	ensureLoaded();
	return _presence.current();
}

rpl::producer<Presence> Session::presenceValue() {
	ensureLoaded();
	return _presence.value();
}

std::optional<QString> Session::address() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready) {
		return std::nullopt;
	}
	return _address;
}

QString Session::addressFriendly(bool bounceable) {
	ensureLoaded();
	if (_presence.current() != Presence::Ready) {
		return QString();
	}
	return FormatFriendly(_address, bounceable);
}

QByteArray Session::publicKey() const {
	return _publicKey;
}

WalletCapabilities Session::capabilities() const {
	return _capabilities;
}

void Session::refreshState() {
	if ((_presence.current() == Presence::Unavailable) || _stateRequestId) {
		return;
	}
	// The later of the two stamps is what the floor measures from, so a
	// pushed updateWalletState postpones the next request instead of only
	// failing to trigger one: the push genuinely replaces a poll round
	// rather than riding beside it.
	const auto since = std::max(_stateRequestedAt, _stateRefreshedAt);
	if (since && (crl::now() - since < kStateRefreshInterval)) {
		return;
	}
	requestState();
}

void Session::requestState() {
	_stateRequestedAt = crl::now();
	_stateRequestId = _stateApi.request(MTPwallet_GetState(
	)).done([=](const MTPWalletState &result) {
		_stateRequestId = 0;
		applyState(result);
	}).fail([=](const MTP::Error &error) {
		_stateRequestId = 0;
		if (error.type() == u"WALLET_UNAVAILABLE"_q) {
			LOG(("Wallet Error: the server has no wallet for this account."));
			setPresence(Presence::Unavailable);
			return;
		}
		LOG(("Wallet Error: wallet.getState failed: %1").arg(error.type()));
		++_stateFailures;
		updateListsGate();
	}).send();
}

void Session::applyState(const MTPWalletState &state) {
	_stateRefreshedAt = crl::now();
	_stateFailures = 0;
	const auto clear = [&] {
		_address = QString();
		_publicKey = QByteArray();
		_balanceNano = 0;
		_capabilities = WalletCapabilities();
	};
	state.match([&](const MTPDwalletState &data) {
		const auto parsed = ParseAddress(qs(data.vaddress()));
		if (!parsed) {
			LOG(("Wallet Error: server wallet address is not parseable."));
			clear();
			setPresence(Presence::Missing);
			return;
		}
		_address = parsed->raw;
		_publicKey = data.vpublic_key().v;
		_balanceNano = int64(data.vbalance().v);
		_capabilities = WalletCapabilities{
			.backupEnabled = data.is_backup_enabled(),
			.canExportPhrase = data.is_can_export_phrase(),
		};
		setPresence(Presence::Ready);
	}, [&](const MTPDwalletStateEmpty &data) {
		clear();
		setPresence(data.is_provisioning()
			? Presence::Provisioning
			: Presence::Missing);
	});
}

void Session::applyUpdate(const MTPDupdateWalletState &data) {
	applyState(data.vstate());
}

void Session::setPresence(Presence presence) {
	if (_presence.current() == presence) {
		return;
	}
	_presence = presence;
	updateListsGate();
	updatePollingState();
}

void Session::revealPhrase(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail) {
	using Flag = MTPwallet_exportSecretPhrase::Flag;
	const auto checked = password && *password;
	_stateApi.request(MTPwallet_ExportSecretPhrase(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		checked ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=](const MTPwallet_SecretPhrase &result) {
		const auto &list = result.data().vwords().v;
		auto words = std::vector<QString>();
		words.reserve(list.size());
		for (const auto &word : list) {
			words.push_back(qs(word));
		}
		if (words.size() < 2) {
			LOG(("Wallet Error: wallet.exportSecretPhrase sent no words."));
			if (fail) {
				fail(u"PHRASE_EMPTY"_q);
			}
		} else if (done) {
			done(std::move(words));
		}
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.exportSecretPhrase failed: %1"
			).arg(error.type()));
		if (fail) {
			fail(error.type());
		}
	}).handleFloodErrors().send();
}

void Session::clearNetworkState() {
	++_networkGeneration;
	_balanceNano = 0;
	_engineStatus = AccountStatus::NonExisting;
	_stateApi.request(base::take(_stateRequestId)).cancel();
	_stateRequestedAt = 0;
	_stateRefreshedAt = 0;
	_stateFailures = 0;
	_history.clear();
	_historyUpdates.fire({});
	_historyHasNext = false;
	_historyRefreshedAt = 0;
	_collectibles.clear();
	_collectiblesTab = false;
	_collectiblesRefreshedAt = 0;
	_collectiblesCompletedAt = 0;
	_collectiblesRequestPending = false;
	_collectiblesHasMore = false;
	_collectiblesPaged = false;
	_collectiblesUpdates.fire({});
	_pending.reset();
	_sendState = SendState::Idle;
	_pollingCount = 0;
	_pollTimer.cancel();
	_stream->stop();
	_historyRequestPending = false;
	_sendUnresolved = false;
	_previewNextArgs.reset();
	_previewNextDone = nullptr;
	_historyDone.clear();
	updateListsGate();
}

void Session::requestEngineRefresh() {
	if (_engineRefreshPending) {
		return;
	}
	const auto finishHistoryWaiters = [this] {
		for (const auto &callback : base::take(_historyDone)) {
			callback();
		}
	};
	if (!_engine->client()) {
		finishHistoryWaiters();
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
			finishHistoryWaiters();
			return;
		}
		applyEngineUpdate(update);
		finishHistoryWaiters();
	}, [=, this](EngineError error) {
		_engineRefreshPending = false;
		if (generation != _networkGeneration) {
			finishHistoryWaiters();
			return;
		}
		LOG(("Wallet Error: engine refresh failed: %1, "
			"keeping last-good state.").arg(error.message));
		finishHistoryWaiters();
	});
}

void Session::applyEngineUpdate(const engine::WalletUpdate &update) {
	// refresh() publishes its account and activity legs independently and
	// reports kPartiallyCompleted when exactly one of them failed, so the
	// outcome gate below belongs to the account write only. The activity
	// list carries its own ResourceState and is applied on that phase; a
	// failed, cancelled or superseded leg never reaches kReady.
	applyEngineActivity(update, false);
	if (update.outcome != engine::WalletOperationOutcome::kCompleted) {
		LOG(("Wallet: engine refresh outcome %1, keeping last-good state."
			).arg(int(update.outcome)));
		return;
	}
	const auto &snapshot = update.snapshot;
	const auto wasUnresolved = _sendUnresolved;
	_sendUnresolved = !TerminalSendPhase(snapshot.send.phase);
	if (_sendUnresolved && !wasUnresolved) {
		updatePollingState();
	}
	if (_pending
		&& !_sendUnresolved
		&& _sendState.current() != SendState::Sending) {
		// refresh() awaits resolve_pending() before it reads activity, so
		// the update that carries the confirmed row carries this terminal
		// phase too. Dropping the local projection right here, instead of
		// waiting for the poll tick that calls resolvePending(), keeps the
		// pending row and the confirmed row from being rendered together.
		// finishPending() is not reused: its trailing requestEngineRefresh()
		// would enqueue a redundant refresh for the update being applied.
		LOG(("Wallet: pending send resolved."));
		_pending.reset();
		_sendState = SendState::Idle;
		updatePollingState();
	}
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
		mapped = AccountStatus::NonExisting;
		break;
	case engine::AccountStatus::kUninitialized:
		mapped = AccountStatus::Uninit;
		break;
	case engine::AccountStatus::kActive:
		mapped = AccountStatus::Active;
		break;
	case engine::AccountStatus::kFrozen:
		mapped = AccountStatus::Frozen;
		break;
	case engine::AccountStatus::kUnknown:
		LOG(("Wallet: engine account status unknown, keeping last-good."));
		break;
	}
	_balanceNano = balance;
	_engineStatus = mapped;
	_stateRefreshedAt = crl::now();
}

void Session::refreshHistory(Fn<void()> done) {
	ensureLoaded();
	if (_presence.current() != Presence::Ready) {
		if (done) {
			done();
		}
		return;
	}
	if (done) {
		_historyDone.push_back(std::move(done));
	}
	requestEngineRefresh();
}

void Session::requestMoreActivity() {
	const auto client = _engine->client();
	if (!client) {
		return;
	}
	_historyRequestPending = true;
	const auto generation = _networkGeneration;
	_engine->run([client] {
		return client->load_more_activity();
	}, [=, this](engine::WalletUpdate update) {
		_historyRequestPending = false;
		if (generation != _networkGeneration) {
			return;
		}
		if (update.outcome
			!= engine::WalletOperationOutcome::kCompleted) {
			LOG(("Wallet: engine activity page outcome %1, "
				"keeping last-good history.").arg(int(update.outcome)));
			return;
		}
		applyEngineActivity(update, true);
	}, [=, this](EngineError error) {
		_historyRequestPending = false;
		if (generation != _networkGeneration) {
			return;
		}
		LOG(("Wallet Error: engine load_more_activity failed: %1, "
			"keeping last-good history.").arg(error.message));
	});
}

void Session::applyEngineActivity(
		const engine::WalletUpdate &update,
		bool more) {
	const auto &activity = update.snapshot.activity;
	const auto &resource = more
		? activity.pagination_resource
		: activity.resource;
	if (resource.phase != engine::ResourcePhase::kReady) {
		return;
	}
	_historyHasNext = activity.has_more;
	_historyRefreshedAt = crl::now();
	auto loaded = HistoryFromEngine(activity.items);
	if (SameHistory(_history, loaded)) {
		return;
	}
	setHistory(std::move(loaded));
}

void Session::setHistory(std::vector<TransferItem> &&list) {
	_history = std::move(list);
	_historyUpdates.fire({});
}

bool Session::historyHasNext() const {
	return _historyHasNext;
}

void Session::loadMoreHistory() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _historyRequestPending
		|| _engineRefreshPending
		|| !_historyHasNext) {
		return;
	}
	requestMoreActivity();
}

void Session::refreshCollectibles(bool force) {
	ensureLoaded();
	const auto interval = force
		? kForcedCollectiblesInterval
		: kCollectiblesPollInterval;
	if (_presence.current() != Presence::Ready
		|| _collectiblesRequestPending
		|| _collectiblesPaged
		|| (_collectiblesRefreshedAt
			&& (crl::now() - _collectiblesRefreshedAt < interval))) {
		return;
	}
	if (!_engine->client()) {
		return;
	}
	_collectiblesRefreshedAt = crl::now();
	requestCollectibles(false);
}

bool Session::collectiblesHasNext() const {
	return _collectiblesHasMore;
}

void Session::loadMoreCollectibles() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _collectiblesRequestPending
		|| !_collectiblesHasMore) {
		return;
	}
	requestCollectibles(true);
}

void Session::requestCollectibles(bool more) {
	const auto client = _engine->client();
	if (!client) {
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
	const auto &nfts = update.snapshot.nfts;
	if (update.outcome == engine::WalletOperationOutcome::kSkipped) {
		_collectiblesHasMore = nfts.has_more;
		return;
	}
	const auto &resource = more ? nfts.pagination_resource : nfts.resource;
	if ((update.outcome != engine::WalletOperationOutcome::kCompleted)
		|| (resource.phase != engine::ResourcePhase::kReady)) {
		LOG(("Wallet: engine nft outcome %1, keeping last-good collectibles."
			).arg(int(update.outcome)));
		return;
	}
	_collectiblesHasMore = nfts.has_more;
	_collectiblesPaged = more && (_panel != nullptr);
	_collectiblesCompletedAt = crl::now();
	auto loaded = CollectiblesFromEngine(nfts);
	if (!SameCollectibles(_collectibles, loaded)) {
		setCollectibles(std::move(loaded));
	}
}

void Session::setCollectibles(std::vector<Gram::NftItem> &&list) {
	_collectibles = std::move(list);
	if (_collectibles.empty()) {
		_collectiblesTab = false;
	}
	for (const auto &item : _collectibles) {
		_collectibleInfo[item.address] = item;
	}
	_collectiblesUpdates.fire({});
}

void Session::updateListsGate() {
	const auto presence = _presence.current();
	const auto unknown = (presence == Presence::Unknown);
	_stateUnreachable = unknown
		&& (_stateFailures >= kStateFailuresBeforeStated);
	_listsGated = (unknown && !_stateUnreachable)
		|| (presence == Presence::Provisioning);
	_listsStateUpdates.fire({});
}

bool Session::listsConfirmedEmpty() const {
	return !_listsGated.current()
		&& _history.empty()
		&& _collectibles.empty();
}

void Session::resolveCollectibleInfo(
		const QString &item,
		Fn<void(const Gram::NftItem &)> done) {
	const auto i = _collectibleInfo.find(item);
	if (i != end(_collectibleInfo)) {
		done(i->second);
		return;
	}
	auto &waiters = _collectibleInfoWaiters[item];
	const auto first = waiters.empty();
	waiters.push_back(std::move(done));
	if (!first) {
		return;
	}
	const auto finish = [=](Gram::NftItem found, bool remember) {
		if (remember) {
			_collectibleInfo[item] = found;
		}
		auto &waiting = _collectibleInfoWaiters[item];
		for (const auto &callback : base::take(waiting)) {
			callback(found);
		}
		_collectibleInfoWaiters.remove(item);
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
void Session::debugClearNetworkState() {
	if (_presence.current() != Presence::Ready) {
		return;
	}
	_debugClearedPollingCount = _pollingCount;
	clearNetworkState();
}

void Session::debugRestoreNetworkState() {
	_pollingCount += base::take(_debugClearedPollingCount);
	updatePollingState();
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
	const auto wanted = (_pollingCount > 0) || _pending || _sendUnresolved;
	if (!wanted) {
		_pollTimer.cancel();
	} else if (!_pollTimer.isActive()) {
		_pollTimer.callEach(kPollInterval);
		pollTick();
	}
	if (wanted && (_presence.current() == Presence::Ready)) {
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
	updatePollingState();
	if (!_pollTimer.isActive()) {
		return;
	}
	refreshState();
	if (_presence.current() != Presence::Ready) {
		return;
	}
	const auto streaming = _stream->healthy();
	const auto stale = [&](crl::time at) {
		return !at || (crl::now() - at >= kStreamResyncInterval);
	};
	if (!streaming
		|| stale(_stateRefreshedAt)
		|| stale(_historyRefreshedAt)) {
		requestEngineRefresh();
	}
	refreshCollectibles();
	if ((_pending || _sendUnresolved) && !_resolveRequestPending) {
		resolvePending();
	}
}

void Session::applyStreamRefresh(StreamRefresh wanted) {
	if (wanted.state) {
		refreshState();
	}
	if (wanted.history) {
		requestEngineRefresh();
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
	return _presence.value() | rpl::map([](Presence presence) {
		return (presence == Presence::Ready);
	});
}

AccountStatus Session::status() const {
	return _engineStatus;
}

const std::vector<TransferItem> &Session::history() const {
	return _history;
}

rpl::producer<> Session::historyUpdates() const {
	return _historyUpdates.events();
}

bool Session::listsGated() const {
	return _listsGated.current();
}

rpl::producer<bool> Session::listsGatedValue() const {
	return _listsGated.value();
}

rpl::producer<bool> Session::stateUnreachableValue() const {
	return rpl::single(rpl::empty) | rpl::then(
		_listsStateUpdates.events()
	) | rpl::map([=, this] {
		return _stateUnreachable;
	}) | rpl::distinct_until_changed();
}

rpl::producer<bool> Session::listsConfirmedEmptyValue() const {
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		historyUpdates(),
		collectiblesUpdates(),
		_listsStateUpdates.events()
	)) | rpl::map([=, this] {
		return listsConfirmedEmpty();
	}) | rpl::distinct_until_changed();
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
	const auto tab = value && !_collectibles.empty();
	if (!tab) {
		_collectiblesPaged = false;
	}
	_collectiblesTab = tab;
}

SendState Session::sendState() const {
	return _sendState.current();
}

rpl::producer<SendState> Session::sendStateValue() const {
	return _sendState.value();
}

const std::optional<PendingSendInfo> &Session::pendingSend() const {
	return _pending;
}

void Session::estimateFee(const SendArgs &args, Fn<void(FeeResult)> done) {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| args.amountNano <= 0
		|| args.destination.isEmpty()) {
		if (done) {
			done(FeeResult{ .error = SendError::InvalidRequest });
		}
		return;
	}
	if (!_engine->client()) {
		if (done) {
			done(FeeResult{ .error = SendError::SigningUnavailable });
		}
		return;
	}
	if (_previewPending) {
		_previewNextArgs = args;
		_previewNextDone = std::move(done);
		const auto client = _engine->client();
		_engine->runQuick([client] {
			client->cancel_send_preview();
		}, [] {}, [](EngineError) {});
		return;
	}
	startPreview(args, std::move(done));
}

void Session::startPreview(
		SendArgs args,
		Fn<void(FeeResult)> done,
		bool retried) {
	_previewPending = true;
	const auto client = _engine->client();
	const auto generation = _networkGeneration;
	auto request = engine::SendPreviewRequest{
		.intent = IntentFromArgs(args),
	};
	_engine->run([client, request = std::move(request)] {
		return client->preview_send(request);
	}, [=, this](engine::SendPreview preview) {
		_previewPending = false;
		if (_previewNextArgs) {
			startPreview(
				*base::take(_previewNextArgs),
				base::take(_previewNextDone));
			return;
		}
		if (generation != _networkGeneration) {
			return;
		}
		if (done) {
			done(FeeResult{
				.feeNano = QString::fromStdString(
					preview.emulation.wallet_fees_nanograms).toLongLong(),
			});
		}
	}, [=, this](EngineError error) {
		_previewPending = false;
		if (_previewNextArgs) {
			startPreview(
				*base::take(_previewNextArgs),
				base::take(_previewNextDone));
			return;
		}
		if (generation != _networkGeneration) {
			return;
		}
		// The engine's cancel_send_preview is momentary: it kills the
		// currently active preview, so a cancel that outlived its target
		// kills the latest request. Every legitimate cancellation stashes
		// a next pair first or bumps the generation, so this failure shape
		// with an empty stash and a fresh generation is a stale kill.
		// Retry once.
		if (!retried
			&& IsPreviewKilled(error)
			&& _engine->client()) {
			startPreview(args, done, true);
			return;
		}
		LOG(("Wallet Error: engine preview_send failed: %1"
			).arg(error.message));
		if (done) {
			done(FeeResult{ .error = SendErrorFrom(error) });
		}
	});
}

void Session::send(SendArgs args, Fn<void(SendError)> done) {
	ensureLoaded();
	if (_presence.current() != Presence::Ready) {
		if (done) {
			done(SendError::Failed);
		}
		return;
	}
	if (args.amountNano <= 0 || args.destination.isEmpty()) {
		if (done) {
			done(SendError::InvalidRequest);
		}
		return;
	}
	if (_sendState.current() != SendState::Idle) {
		if (done) {
			done(SendError::AlreadySending);
		}
		return;
	}
	if (!_engine->client()) {
		if (done) {
			done(SendError::SigningUnavailable);
		}
		return;
	}
	_sendState = SendState::Sending;
	const auto client = _engine->client();
	const auto generation = _networkGeneration;
	const auto destination = args.destination;
	const auto amountNano = args.amountNano;
	const auto comment = args.comment.trimmed();
	const auto recordPending = [=, this] {
		_pending = PendingSendInfo{
			.posted = base::unixtime::now(),
			.amountNano = amountNano,
			.destination = destination,
			.comment = comment,
		};
		_sendState = SendState::Pending;
		updatePollingState();
		requestEngineRefresh();
		if (done) {
			done(SendError::None);
		}
	};
	auto request = engine::SendRequest{
		.operation_id = NewRecordId(),
		.force = false,
		.intent = IntentFromArgs(args),
	};
	_engine->run([client, request = std::move(request)] {
		return client->send(request);
	}, [=, this](engine::SendResult) {
		if (generation != _networkGeneration) {
			return;
		}
		recordPending();
	}, [=, this](EngineError error) {
		if (generation != _networkGeneration) {
			return;
		}
		LOG(("Wallet Error: engine send failed: %1").arg(error.message));
		if (IsSubmissionUnknown(error)) {
			recordPending();
			return;
		}
		_sendState = SendState::Idle;
		if (done) {
			done(SendErrorFrom(error));
		}
	});
}

void Session::resolvePending() {
	if (_resolveRequestPending || !_engine->client()) {
		return;
	}
	_resolveRequestPending = true;
	const auto client = _engine->client();
	const auto generation = _networkGeneration;
	_engine->run([client] {
		return client->resolve_pending();
	}, [=, this](engine::SendSnapshot snapshot) {
		_resolveRequestPending = false;
		if (generation != _networkGeneration) {
			return;
		}
		if (TerminalSendPhase(snapshot.phase)
			&& _sendState.current() != SendState::Sending) {
			finishPending();
		}
	}, [=, this](EngineError error) {
		_resolveRequestPending = false;
		if (generation != _networkGeneration) {
			return;
		}
		LOG(("Wallet Error: engine resolve_pending failed: %1, "
			"keeping last-good state.").arg(error.message));
	});
}

void Session::finishPending() {
	LOG(("Wallet: pending send resolved."));
	_pending.reset();
	_sendUnresolved = false;
	_sendState = SendState::Idle;
	updatePollingState();
	requestEngineRefresh();
}

} // namespace Wallet
