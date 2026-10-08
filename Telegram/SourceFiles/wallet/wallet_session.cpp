/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session.h"

#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_info.h"
#include "base/openssl_help.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "core/version.h"
#include "data/components/recent_money_recipients.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "gram/api/gram_api_account.h"
#include "gram/api/gram_api_emulate.h"
#include "gram/gram_boc.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "storage/storage_domain.h"
#include "tde2e/tde2e_api.h"
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_engine.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_onramp.h"
#include "wallet/wallet_phrase_shares.h"
#include "wallet/wallet_rates.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_emulation.h"
#include "wallet/wallet_transfer_messages.h"
#include "wallet/wallet_unlock.h"
#include "wallet/wallet_user_addresses.h"
#include "wallet/wallet_vault.h"

#include "wallet_engine.hpp"

#include <QtCore/QUuid>

#include <atomic>
#include <cmath>
#include <deque>
#include <limits>

namespace Wallet {

struct ShareFetch {
	TdE2E::TemporaryKeyPair keys;
	std::vector<QByteArray> shares;
	std::vector<mtpRequestId> requests;
	std::vector<MTP::ShiftedDcId> sessions;
	std::vector<int> dcs;
	Fn<void(const QString &)> fail;
	mtpRequestId exportRequestId = 0;
	crl::time startedAt = 0;
	int pending = 0;
};

struct CommentScope::State {
	const Session *session = nullptr;
	TransferItem target;
	QByteArray body;
	QString sender;
	std::shared_ptr<VaultRuntime> vault;
	std::optional<CustodyRecord> record;
	Fn<void()> cancelPending;
	int generation = 0;
	quint32 epoch = 0;
	std::atomic<bool> cancelled = false;
};

struct Session::HistoryRequest {
	std::optional<TransferWalletIdentity> identity;
	uint64 identityRevision = 0;
	int generation = 0;
	// The cursor this flight spent. The answer is compared against it to
	// catch a server that hands back the offset it was given, so the value
	// has to survive the round trip with the request that sent it and not
	// be re-read from _historyNextOffset, which a landing page may already
	// have moved.
	QString offset;
	mtpRequestId id = 0;
	std::vector<Fn<void()>> done;
};

struct Session::CollectiblesRequest {
	std::optional<TransferWalletIdentity> identity;
	uint64 identityRevision = 0;
	int generation = 0;
	QString offset;
	bool more = false;
	mtpRequestId id = 0;
};

struct Session::SubmittedTransfer {
	std::string operationId;
	TransferWalletIdentity identity;
	std::weak_ptr<wallet_engine::WalletClient> client;
	std::unique_ptr<TransferItem> fallback;
	std::unique_ptr<TransferItem> item;
	QString canonicalId;
	QString collectible;
	QByteArray confirmedHash;
	std::optional<TransferReceipt> receipt;
	std::optional<wallet_engine::SendPhase> terminal;
	int generation = 0;
	int lookupAttempts = 0;
	bool lookupStopped = false;
	bool paired = false;
	bool leaving = false;
};

struct Session::SubmittedLookup {
	std::string operationId;
	TransferWalletIdentity identity;
	QByteArray messageHash;
	int generation = 0;
	mtpRequestId id = 0;
};

struct Session::PreparedRotation {
	std::vector<QString> words;
	QByteArray newPublicKey;
	std::string signedBoc;
	uint32 seqno = 0;
	uint64 validUntil = 0;
	int64 quotedFeeNano = 0;
};

struct PreparedSend {
	SendArgs args;
	TransferWalletIdentity identity;
	GaslessTerms terms;
	std::shared_ptr<const wallet_engine::SendIntent> intent;
	std::shared_ptr<const wallet_engine::NftTransferIntent> nft;
	std::string operationId;
	int64 feeNano = 0;
	uint64 owner = 0;
	uint64 revision = 0;
	int generation = 0;
	std::optional<quint32> privateEpoch;
	std::shared_ptr<wallet_engine::WalletClient> client;
	bool tonConnect = false;
};

struct Session::PreviewRequest {
	std::optional<TransferWalletIdentity> identity;
	GaslessTerms terms;
	uint64 owner = 0;
	uint64 revision = 0;
	int generation = 0;
	std::optional<quint32> privateEpoch;
	std::shared_ptr<wallet_engine::WalletClient> client;
	KeyAuthorization auth;
	SendArgs args;
	std::string operationId;
	Fn<void(FeeResult)> done;
	std::shared_ptr<const wallet_engine::SendRequest> tonConnect;
	bool feeOnly = false;
};

struct Session::PreviewState : base::has_weak_ptr {
	struct Flight {
		enum class Stage {
			Encrypting,
			Previewing,
		};

		uint64 id = 0;
		PreviewRequest request;
		std::shared_ptr<const wallet_engine::SendIntent> intent;
		std::shared_ptr<const wallet_engine::NftTransferIntent> nft;
		FeeResult result;
		Stage stage = Stage::Encrypting;
		bool finished = false;
		bool cancelIssued = false;
		bool cancelFinished = false;
	};

	base::flat_map<uint64, uint64> owners;
	std::deque<PreviewRequest> queue;
	std::optional<Flight> active;
	uint64 lastOwner = 0;
	uint64 lastFlight = 0;
	bool dispatching = false;
};

namespace {

namespace engine = wallet_engine;

struct ResetClientCompletion {
	~ResetClientCompletion();

	Fn<void()> done;
};

ResetClientCompletion::~ResetClientCompletion() {
	if (done) {
		crl::on_main(std::move(done));
	}
}

constexpr auto kPollInterval = 5 * crl::time(1000);
constexpr auto kCollectiblesPollInterval = 60 * crl::time(1000);
// The server reads the balance from toncenter and caches it for about
// thirty seconds, so two reads inside one such window return the same
// answer. This floor is twice that window: whatever jitter the poll tick,
// a stream hint and a pushed update add between two requests, the later
// one can never land inside the cache window the earlier one filled.
constexpr auto kStateRefreshInterval = 60 * crl::time(1000);
constexpr auto kShareFetchTimeout = 60 * crl::time(1000);
// A wallet.getState that fails for anything but WALLET_UNAVAILABLE leaves the
// presence at Unknown, which is also what "the first request has not answered
// yet" reads as, so the cold-open gate would otherwise stay closed on an
// unlabelled indicator for as long as the server keeps failing. After this
// many consecutive failed attempts the lane stops claiming to be loading and
// settles to a stated face; the 60-second floor keeps retrying underneath and
// one applied state clears the latch again.
constexpr auto kStateFailuresBeforeStated = 2;
// The largest limit wallet.getTransactions documents.
constexpr auto kTransactionsPerPage = 50;
// How many wallet.getTransactions pages in a row may answer with nothing
// the feed can show before the next one is refused. At the documented page
// limit that is 1000 transactions per chain. Every `more` request spends
// from it, whether the reader scrolled for it or the hidden-page
// continuation volunteered it, so the two pagers share one bound, and only
// a page carrying a visible row refills it. A head page that shows nothing
// must not: a transfer push asks for one, so refilling there would restart
// the whole chain per push on exactly the wallets this feature exists for.
// What re-arms the bound instead is a key change or a presence transition
// (both go through clearHistory()), closing the panel, coming back to the
// transactions tab, a changed threshold, a scroll the reader moved down,
// and - because a feed with nothing to show has nothing to scroll either -
// pollTick(), once the history staleness floor has passed with the walk
// quiet. So nothing the server sends restarts the chain by itself, the
// walk is paced by this client's clock, and no eligible row is walled off.
constexpr auto kMaxHiddenPagesInRow = 20;
constexpr auto kForcedCollectiblesInterval = 10 * crl::time(1000);
constexpr auto kCollectibleTransferAttachedNanos = int64(50'000'000);
constexpr auto kCollectibleTransferForwardNanos = int64(1);
constexpr auto kCollectibleFollowUpRefreshes = 6;
// The largest limit wallet.getNfts accepts.
constexpr auto kCollectiblesPerPage = 20;
constexpr auto kStreamResyncInterval = 30 * crl::time(1000);
constexpr auto kClientSendValiditySeconds = uint64(300);
constexpr auto kClientResolutionMarginSeconds = uint64(60);
constexpr auto kClientRequestTimeoutMs = uint64(15000);
// Clock error only: unixtime leads the server by under 3 s, plus rounding.
constexpr auto kSendingMatchSkew = TimeId(5);
constexpr auto kPreviewClientRecordId = "public-key-only";
constexpr auto kDecryptBusyRetries = 5;
constexpr auto kDecryptBusyRetryDelay = crl::time(500);
constexpr auto kCommentRecipientRetries = 3;
constexpr auto kCommentRecipientRetryDelay = crl::time(1000);
constexpr auto kGaslessRefreshInterval = crl::time(60 * 1000);
constexpr auto kGaslessRefreshAhead = crl::time(10 * 1000);
constexpr auto kGaslessRetryInterval = crl::time(15 * 1000);
// The largest individual data field wallet.sendTransfer allows, inclusive.
constexpr auto kTransferDataMaxBytes = 16 * 1024;
constexpr auto kTonConnectOperationIdMaxBytes = 256;
// The lane follows a submitted message for as long as the engine can
// still see the message accepted (validity plus the resolution
// margin), one attempt per tick.
constexpr auto kSubmittedLookupAttempts = int(
	(kClientSendValiditySeconds + kClientResolutionMarginSeconds)
	* 1000
	/ uint64(kPollInterval));
// The throw-away rotation a quote emulates leaves the device with random
// bytes where its signature was, so the contract can never execute what the
// emulator was shown: the emulation request asks for signature checks to be
// skipped, so the fee it reports is still the fee of the real rotation. The
// expiration is the shortest window that comfortably outlives one emulation
// round trip (the Wallet::Api deadline plus queueing); the fresh prepare
// keeps the engine's own send validity.
constexpr auto kRotationQuoteValiditySeconds = uint64(120);
constexpr auto kOwnershipProofSignatureSize = 64;
constexpr auto kTonConnectChallengeAnswerSize = 32;

[[nodiscard]] int64 MinNanosFromConfig(
		float64 configured,
		int64 fallback) {
	// The value arrives as `jsonNumber value:double`, so it is judged
	// in the double domain before any cast: NaN, an infinity or an
	// out-of-range magnitude would otherwise abort or convert with
	// undefined behaviour. The ceiling is 2^53, the largest integer a
	// double represents exactly, because `double(kMaxAmountNano)` rounds
	// up to 1e18 and would let an out-of-range value through. A served
	// integer above 2^53 is indistinguishable from its nearest
	// representable neighbour: 2^53 + 1 arrives as 2^53 and is accepted,
	// and 2^53 + 2 is the first value that falls back to the default.
	const auto valid = std::isfinite(configured)
		&& (configured >= 1.)
		&& (configured <= float64(kTransferMinNanosMax))
		&& (configured == std::floor(configured));
	return valid ? int64(configured) : fallback;
}

[[nodiscard]] int64 GaslessMinNanos(not_null<Main::Session*> session) {
	const auto configured = session->appConfig().get<float64>(
		u"wallet_gasless_min_nanos"_q,
		float64(kGaslessMinNanosDefault));
	return MinNanosFromConfig(configured, kGaslessMinNanosDefault);
}

[[nodiscard]] GaslessInfo GaslessInfoFromServer(
		const MTPDupdateWalletGaslessInfo &data) {
	const auto relayer = ParseAddress(qs(data.vrelayer_address()));
	return GaslessInfo{
		.relayer = (relayer && !relayer->testnet)
			? relayer->raw
			: QString(),
		.minAmount = int64(data.vmin_amount().v),
		.resetAt = data.vreset_at().v,
		.left = data.vleft().v,
		.available = data.is_available(),
	};
}

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

void FinishHistoryWaiters(std::vector<Fn<void()>> callbacks) {
	for (const auto &callback : callbacks) {
		callback();
	}
}

[[nodiscard]] bool SameHistory(
		const std::vector<TransferItem> &was,
		const std::vector<TransferItem> &now) {
	return (was == now);
}

[[nodiscard]] base::flat_set<QString> HistoryNamedIds(
		const std::vector<TransferItem> &list) {
	auto result = base::flat_set<QString>();
	result.reserve(list.size());
	for (const auto &item : list) {
		if (!item.id.isEmpty()) {
			result.emplace(item.id);
		}
	}
	return result;
}

struct MergedHead {
	std::vector<TransferItem> list;
	int retained = 0;
	bool namedLast = false;
};

// The merged list is the served page in server order, then the rows that page
// does not name, in the order the list already had, so it is a superset of
// `was`. How many of those rows were kept and whether the page named the row
// `was` ends with come back with it, because the caller decides the cursor
// from the second of those and both fall out of the one scan below: the module
// keeps a single row-identity rule and the caller reads no answer out of the
// output vector's layout.
[[nodiscard]] MergedHead MergedHeadHistory(
		const std::vector<TransferItem> &was,
		std::vector<TransferItem> &&head) {
	const auto ids = HistoryNamedIds(head);
	auto list = std::move(head);
	auto retained = std::vector<TransferItem>();
	retained.reserve(was.size());
	// A list with no rows has no tail for the page to reach past, so the
	// coverage question is answered vacuously here and the loop below never
	// leaves it undecided.
	auto namedLast = was.empty();
	for (const auto &item : was) {
		// The server names every transaction and that name is the only
		// key this feed has, so a row the head page covers is the head
		// page's and a row it does not reach is kept exactly where the
		// reader already has it. A row the server left unnamed cannot be
		// looked up by name at all, so it is compared by value against
		// the page instead: carried again, it would otherwise be kept
		// twice. The page is a whole vector here and grows no rows until
		// the loop is over, so that lookup never reads a retained row and
		// two equal unnamed rows the list already held both survive.
		const auto listed = item.id.isEmpty()
			? ranges::contains(list, item)
			: ids.contains(item.id);
		if (!listed) {
			retained.push_back(item);
		}
		// Only the final iteration's answer survives, and that is the one
		// the cursor decision asks for: whether the page reached the row
		// the loaded list ends with. Taking it from the same `listed`
		// keeps that question on the one identity rule stated above.
		namedLast = listed;
	}
	const auto count = int(retained.size());
	list.insert(
		end(list),
		std::make_move_iterator(begin(retained)),
		std::make_move_iterator(end(retained)));
	return {
		.list = std::move(list),
		.retained = count,
		.namedLast = namedLast,
	};
}

// The identity is the one MergedHeadHistory() uses - the server's
// transaction id - and here it is the only key that can answer this
// direction at all: a served row the server left unnamed is added, because
// TransferItem's defaulted operator== makes two genuinely distinct
// transfers equal when they share a counterparty, an amount, a fee, a
// comment and a date and carry neither an id nor a tx_hash, and dropping
// one would lose a transaction the server is delivering now that no later
// page offers again at this cursor. The head arm's value test decides the
// opposite question - whether a row the list already holds is about to be
// carried forward beside an equal copy the page already contains - where
// the same equality can only drop a loaded copy the next head page brings
// back.
[[nodiscard]] std::vector<TransferItem> UnheldHistory(
		const std::vector<TransferItem> &was,
		std::vector<TransferItem> &&page) {
	const auto ids = HistoryNamedIds(page);
	auto held = base::flat_set<QString>();
	held.reserve(ids.size());
	for (const auto &item : was) {
		if (!item.id.isEmpty() && ids.contains(item.id)) {
			held.emplace(item.id);
		}
	}
	if (held.empty()) {
		return std::move(page);
	}
	auto result = std::vector<TransferItem>();
	result.reserve(page.size());
	for (auto &item : page) {
		if (item.id.isEmpty() || !held.contains(item.id)) {
			result.push_back(std::move(item));
		}
	}
	return result;
}

[[nodiscard]] std::vector<TransferItem> ArrivedCollectibles(
		const std::vector<TransferItem> &was,
		const std::vector<TransferItem> &head) {
	auto result = std::vector<TransferItem>();
	auto seen = base::flat_set<QString>();
	for (const auto &item : head) {
		if (item.kind != TransferItem::Kind::Collectible
			|| item.collectible.isEmpty()
			|| item.status == TransferItem::Status::Failure
			|| seen.contains(item.collectible)) {
			continue;
		}
		seen.emplace(item.collectible);
		const auto held = item.id.isEmpty()
			? ranges::contains(was, item)
			: ranges::contains(was, item.id, &TransferItem::id);
		if (!held) {
			result.push_back(item);
		}
	}
	return result;
}

[[nodiscard]] std::vector<Gram::NftItem> UnheldCollectibles(
		const std::vector<Gram::NftItem> &was,
		std::vector<Gram::NftItem> &&page) {
	auto held = base::flat_set<QString>();
	held.reserve(was.size());
	for (const auto &item : was) {
		held.emplace(item.address);
	}
	auto result = std::vector<Gram::NftItem>();
	result.reserve(page.size());
	for (auto &item : page) {
		if (!held.contains(item.address)) {
			result.push_back(std::move(item));
		}
	}
	return result;
}

[[nodiscard]] bool SameCollectibles(
		const std::vector<Gram::NftItem> &was,
		const std::vector<Gram::NftItem> &now) {
	return (was == now);
}

[[nodiscard]] std::string NewRecordId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

[[nodiscard]] CustodyRecord RecordFromDescriptor(
		const engine::WalletDescriptor &descriptor) {
	return CustodyRecord{
		.recordId = QString::fromStdString(descriptor.record_id),
		.address = QString::fromStdString(descriptor.address),
		.publicKey = QByteArray(
			reinterpret_cast<const char*>(descriptor.public_key.data()),
			descriptor.public_key.size()),
		.network = int(descriptor.network),
		.secretRef = QString::fromStdString(descriptor.secret_ref.value),
	};
}

[[nodiscard]] auto EngineKey(const QByteArray &key)
-> std::optional<std::vector<uint8_t>> {
	if (key.isEmpty()) {
		return std::nullopt;
	}
	return std::vector<uint8_t>(
		key.constData(),
		key.constData() + key.size());
}

[[nodiscard]] std::vector<uint8_t> EngineBytes(const QByteArray &bytes) {
	return std::vector<uint8_t>(
		bytes.constData(),
		bytes.constData() + bytes.size());
}

[[nodiscard]] QByteArray BytesFromEngine(const std::vector<uint8_t> &bytes) {
	return QByteArray(
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size());
}

[[nodiscard]] engine::WalletDescriptor DescriptorFromRecord(
		const CustodyRecord &record) {
	return engine::WalletDescriptor{
		.record_id = record.recordId.toStdString(),
		.address = record.address.toStdString(),
		.public_key = std::vector<uint8_t>(
			record.publicKey.constData(),
			record.publicKey.constData() + record.publicKey.size()),
		.network = engine::Network(record.network),
		.secret_ref = engine::ProtectedSecretRef{
			.value = record.secretRef.toStdString(),
		},
	};
}

// A record of another wallet, or an unresolved one of the served wallet:
// what the conflict box lists, what the parked reveal and drop accept, and
// what raises the device's conflict, so the three cannot disagree.
[[nodiscard]] bool RecordParked(
		const CustodyRecord &record,
		const QString &canonicalAddress,
		const QByteArray &servedKey) {
	return (CanonicalAddress(record.address) != canonicalAddress)
		|| record.unresolved(servedKey);
}

// WHY: a parked record at the served address is an older key of this
// same account, so its balance is the one the card already shows.
[[nodiscard]] QString CheckableParkedAddress(
		const CustodyRecord &record,
		const QString &servedAddress) {
	const auto address = CanonicalAddress(record.address);
	return (record.network == int(engine::Network::kMainnet)
		&& address != servedAddress)
		? address
		: QString();
}

// Public keys and addresses name a wallet; the words never reach the log.
[[nodiscard]] QString LogKey(const QByteArray &key) {
	return key.isEmpty() ? u"(none)"_q : QString::fromLatin1(key.toHex());
}

[[nodiscard]] QString AwaitingKeyRefusal(
		const CustodyStore &store,
		const PhraseIdentity &identity,
		const QString &outdated,
		const QString &changing) {
	const auto held = store.byAnchor(identity.anchor);
	if (!held || !held->awaitingServerKey || held->signingKey.isEmpty()) {
		return QString();
	}
	LOG(("Wallet Error: the record of anchor %1 awaits the server key "
		"for signing key %2."
		).arg(LogKey(identity.anchor)
		).arg(LogKey(held->signingKey)));
	return (held->signingKey == identity.signing) ? changing : outdated;
}

[[nodiscard]] QString LogWalletState(const MTPWalletState &state) {
	return state.match([](const MTPDwalletState &data) {
		return u"address=%1 key=%2 backup_enabled=%3 "
			"can_export_phrase=%4 can_enable_backup=%5"_q
			.arg(qs(data.vaddress()))
			.arg(LogKey(data.vpublic_key().v))
			.arg(data.is_backup_enabled())
			.arg(data.is_can_export_phrase())
			.arg(data.is_can_enable_backup());
	}, [](const MTPDwalletStateEmpty &data) {
		return u"empty creating=%1"_q.arg(data.is_creating());
	});
}

[[nodiscard]] QString SendErrorName(SendError error) {
	switch (error) {
	case SendError::None: return u"None"_q;
	case SendError::InvalidRequest: return u"InvalidRequest"_q;
	case SendError::AmountTooSmall: return u"AmountTooSmall"_q;
	case SendError::CommentTooLong: return u"CommentTooLong"_q;
	case SendError::CommentEncryptionUnavailable:
		return u"CommentEncryptionUnavailable"_q;
	case SendError::InsufficientBalance: return u"InsufficientBalance"_q;
	case SendError::InsufficientFees: return u"InsufficientFees"_q;
	case SendError::PreviousUnresolved: return u"PreviousUnresolved"_q;
	case SendError::AlreadySending: return u"AlreadySending"_q;
	case SendError::SigningUnavailable: return u"SigningUnavailable"_q;
	case SendError::Locked: return u"Locked"_q;
	case SendError::Failed: return u"Failed"_q;
	case SendError::Rejected: return u"Rejected"_q;
	case SendError::DataInvalid: return u"DataInvalid"_q;
	case SendError::KeyMismatch: return u"KeyMismatch"_q;
	case SendError::KeyChanged: return u"KeyChanged"_q;
	case SendError::QuoteExpired: return u"QuoteExpired"_q;
	case SendError::LinkExpired: return u"LinkExpired"_q;
	case SendError::CollectibleUnavailable:
		return u"CollectibleUnavailable"_q;
	case SendError::CollectibleRejected: return u"CollectibleRejected"_q;
	case SendError::Silent: return u"Silent"_q;
	case SendError::SubmissionUnknown: return u"SubmissionUnknown"_q;
	}
	return u"Unknown"_q;
}

// Every custody flow names its stage and refusal code in one log.txt line.
[[nodiscard]] Fn<void(const QString &)> LoggedFail(
		const QString &stage,
		Fn<void(const QString &)> fail) {
	return [stage, fail = std::move(fail)](const QString &error) {
		LOG(("Wallet Error: %1 refused: %2").arg(stage, error));
		if (fail) {
			fail(error);
		}
	};
}

[[nodiscard]] Fn<void(FeeResult)> LoggedFeeDone(Fn<void(FeeResult)> done) {
	return [done = std::move(done)](FeeResult result) {
		if (result.error != SendError::None) {
			LOG(("Wallet Error: the fee estimate answered %1."
				).arg(SendErrorName(result.error)));
		}
		if (done) {
			done(std::move(result));
		}
	};
}

struct Restored {
	engine::WalletDescriptor descriptor;
	std::vector<QString> words;
};

struct ThrowawayRotation {
	QString signedBoc;
};

[[nodiscard]] uint64 RotationValidUntil(uint64 seconds) {
	return uint64(base::unixtime::now()) + seconds;
}

[[nodiscard]] engine::PrepareKeyRotationRequest RotationRequest(
		uint64 seconds) {
	return engine::PrepareKeyRotationRequest{
		.valid_until = RotationValidUntil(seconds),
		.message_kind = engine::KeyRotationMessageKind::kExternal,
	};
}

[[nodiscard]] std::vector<QString> SplitWords(const QString &phrase) {
	const auto list = phrase.split(QChar(' '), Qt::SkipEmptyParts);
	return std::vector<QString>(list.begin(), list.end());
}

[[nodiscard]] QString NormalizeWord(const QString &word) {
	return word.trimmed().toLower();
}

[[nodiscard]] std::optional<QByteArray> RotationMnemonicKey(
		const QStringList &words) {
	try {
		const auto key = engine::rotation_mnemonic_public_key(
			words.join(QChar(' ')).toStdString());
		if (int(key.size()) != kCustodyPublicKeySize) {
			return std::nullopt;
		}
		return QByteArray(
			reinterpret_cast<const char*>(key.data()),
			key.size());
	} catch (...) {
		return std::nullopt;
	}
}

// The engine exports no signing-key function, only
// rotation_mnemonic_public_key, which returns derive_half_key(anchor).
// derive_rotation_keys derives both halves of a Rotation mnemonic with that
// same derive_half_key, independently per half; a 12-word phrase's signing
// half is its anchor half; and each half of a 24-word phrase is an
// independently checksummed BIP-39 phrase the engine accepts as a 12-word
// Rotation mnemonic of its own. So the anchor of words 13-24 taken alone is
// the 24-word phrase's signing key byte for byte, computed entirely inside
// the engine, with no cryptography in Telegram. Runs on the engine worker.
[[nodiscard]] std::optional<PhraseIdentity> DerivePhraseIdentity(
		const QStringList &normalized) {
	const auto count = normalized.size();
	if (count != 12 && count != 24) {
		LOG(("Wallet Error: phrase validation invalid_word_count=%1.")
			.arg(count));
		return std::nullopt;
	}
	const auto anchor = RotationMnemonicKey(normalized);
	if (!anchor) {
		LOG(("Wallet Error: phrase validation anchor_derivation_failed "
			"word_count=%1.").arg(count));
		return std::nullopt;
	}
	const auto signing = (count == 24)
		? RotationMnemonicKey(normalized.mid(12))
		: anchor;
	if (!signing) {
		LOG(("Wallet Error: phrase validation signing_derivation_failed "
			"word_count=%1.").arg(count));
		return std::nullopt;
	}
	return PhraseIdentity{ .anchor = *anchor, .signing = *signing };
}

// WHY: ton_connect_account() is the engine's only secret-free address
// derivation, refusing a descriptor whose anchor derives another address;
// the ref copies the engine's own form, so only the address can differ.
[[nodiscard]] bool AnchorDerivesOtherAddress(
		const std::shared_ptr<engine::WalletLifecycle> &lifecycle,
		const QByteArray &anchor,
		const QString &address) {
	const auto recordId = std::string("phrase-check");
	try {
		lifecycle->ton_connect_account(engine::WalletDescriptor{
			.record_id = recordId,
			.address = address.toStdString(),
			.public_key = std::vector<uint8_t>(
				anchor.constData(),
				anchor.constData() + anchor.size()),
			.network = engine::Network::kMainnet,
			.secret_ref = engine::ProtectedSecretRef{
				.value = "wallet:" + recordId + ":mnemonic",
			},
		});
	} catch (const engine::wallet_lifecycle_error::InvalidRecordId &) {
		return true;
	} catch (...) {
	}
	return false;
}

[[nodiscard]] const std::vector<QString> &Wordlist() {
	static const auto result = [] {
		auto list = std::vector<QString>();
		try {
			const auto words = engine::mnemonic_wordlist();
			list.reserve(words.size());
			for (const auto &word : words) {
				list.push_back(QString::fromStdString(word));
			}
			std::sort(list.begin(), list.end());
		} catch (...) {
			LOG(("Wallet Error: cannot read the engine wordlist."));
			list.clear();
		}
		return list;
	}();
	return result;
}

[[nodiscard]] QString LifecycleErrorName(const EngineError &error) {
	if (!error.underlying) {
		return u"unknown"_q;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_lifecycle_error::InvalidRecordId &) {
		return u"InvalidRecordId"_q;
	} catch (const engine::wallet_lifecycle_error::InvalidRecoveryPhrase &) {
		return u"InvalidRecoveryPhrase"_q;
	} catch (const engine::wallet_lifecycle_error::AddressDerivationFailed &) {
		return u"AddressDerivationFailed"_q;
	} catch (const engine::wallet_lifecycle_error::TonConnectSigningFailed &) {
		return u"TonConnectSigningFailed"_q;
	} catch (const engine::wallet_lifecycle_error::SecretWalletMismatch &) {
		return u"SecretWalletMismatch"_q;
	} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &) {
		return u"ProtectedSecretHost"_q;
	} catch (const engine::wallet_lifecycle_error::InvalidTonConnectSessionInput &) {
		return u"InvalidTonConnectSessionInput"_q;
	} catch (...) {
	}
	return u"unknown"_q;
}

[[nodiscard]] bool IsVaultLocked(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &e) {
		return (e.kind
			== engine::ProtectedSecretHostErrorKind::kAuthenticationFailed);
	} catch (const engine::protected_secret_host_error::Failed &e) {
		return (e.kind
			== engine::ProtectedSecretHostErrorKind::kAuthenticationFailed);
	} catch (...) {
	}
	return false;
}

[[nodiscard]] QString LifecycleErrorDetails(const EngineError &error) {
	auto hostKind = -1;
	if (error.underlying) {
		try {
			std::rethrow_exception(error.underlying);
		} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &e) {
			hostKind = int(e.kind);
		} catch (const engine::protected_secret_host_error::Failed &e) {
			hostKind = int(e.kind);
		} catch (...) {
		}
	}
	return u"%1 secret_read=%2 protected_host_kind=%3"_q
		.arg(LifecycleErrorName(error))
		.arg(int(ProtectedSecretFailure(error)))
		.arg(hostKind);
}

[[nodiscard]] bool ReadAuthorized(
		Session &session,
		const KeyAuthorization &auth) {
	return auth.grant
		&& auth.grant->valid()
		&& session.vault().unlocked();
}

[[nodiscard]] bool ValidCommentPayloadSize(int size) {
	return size >= 64 && size <= 1024 && ((size - 48) % 16 == 0);
}

[[nodiscard]] QByteArray ServerCommentBody(const QByteArray &payload) {
	if (!ValidCommentPayloadSize(payload.size())) {
		return QByteArray();
	}
	const auto cells = 1 + (payload.size() - 35 + 126) / 127;
	const auto total = payload.size() + 4 + 3 * cells - 1;
	const auto width = (total > 255) ? 2 : 1;
	auto result = QByteArray::fromHex("b5ee9c7201");
	result.reserve(10 + width + total);
	result.append(char(width));
	result.append(char(cells));
	result.append(char(1));
	result.append(char(0));
	if (width == 2) {
		result.append(char(total >> 8));
	}
	result.append(char(total));
	result.append(char(0));
	for (auto cell = 0, offset = 0; cell != cells; ++cell) {
		const auto first = (cell == 0);
		const auto last = (cell + 1 == cells);
		const auto count = first
			? 35
			: std::min(127, int(payload.size()) - offset);
		result.append(char(last ? 0 : 1));
		result.append(char(2 * (count + (first ? 4 : 0))));
		if (first) {
			result.append(QByteArray::fromHex("2167da4b"));
		}
		result.append(payload.constData() + offset, count);
		offset += count;
		if (!last) {
			result.append(char(cell + 1));
		}
	}
	return result.toBase64();
}

// WHY: a fee counts a body's bits and cells, never what they hold, and an
// encrypted comment is 32 bytes of key mix, 16 of message key and the text
// padded by 16 to 31 bytes up to a multiple of 16: no key prices it.
[[nodiscard]] QByteArray EncryptedCommentFeeBody(const QString &text) {
	const auto bytes = int(text.toUtf8().size());
	return ServerCommentBody(QByteArray(48 + ((bytes + 31) & ~15), char(0)));
}

[[nodiscard]] bool ValidEncryptedCommentBody(const QByteArray &encoded) {
	constexpr auto kMaxCells = 1025;
	constexpr auto kMaxCellBytes = 1028 + 2 * kMaxCells + 2 * (kMaxCells - 1);
	constexpr auto kMaxBytes = 16 + kMaxCellBytes;
	if (encoded.isEmpty()
		|| encoded.size() > 4 * ((kMaxBytes + 2) / 3)
		|| encoded.size() % 4) {
		return false;
	}
	const auto decoded = QByteArray::fromBase64Encoding(
		encoded,
		QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded || decoded.decoded.toBase64() != encoded) {
		return false;
	}
	const auto &body = decoded.decoded;
	auto offset = 0;
	const auto read = [&](int width) {
		if (offset + width > body.size()) {
			return -1;
		}
		auto value = 0;
		for (auto i = 0; i != width; ++i) {
			value = (value << 8) | uchar(body[offset++]);
		}
		return value;
	};
	if (read(2) != 0xb5ee || read(2) != 0x9c72) {
		return false;
	}
	const auto refs = read(1);
	const auto offsets = read(1);
	if ((refs != 1 && refs != 2) || (offsets != 1 && offsets != 2)) {
		return false;
	}
	const auto cells = read(refs);
	if (cells < 1 || cells > kMaxCells
		|| refs != ((cells > 255) ? 2 : 1)
		|| read(refs) != 1
		|| read(refs) != 0) {
		return false;
	}
	const auto total = read(offsets);
	if (total < 0 || total > kMaxCellBytes
		|| offsets != ((total > 255) ? 2 : 1)
		|| read(refs) != 0
		|| total != body.size() - offset) {
		return false;
	}
	auto payload = 0;
	for (auto cell = 0; cell != cells; ++cell) {
		const auto last = (cell + 1 == cells);
		if (read(1) != (last ? 0 : 1)) {
			return false;
		}
		const auto bits = read(1);
		const auto count = bits / 2;
		if (bits < 0 || bits % 2
			|| count < ((cell == 0) ? 4 : 1)
			|| count > body.size() - offset) {
			return false;
		}
		if (cell == 0) {
			if (read(2) != 0x2167 || read(2) != 0xda4b) {
				return false;
			}
			offset += count - 4;
			payload += count - 4;
		} else {
			offset += count;
			payload += count;
		}
		if (!last && read(refs) != cell + 1) {
			return false;
		}
	}
	return offset == body.size() && ValidCommentPayloadSize(payload);
}

[[nodiscard]] QByteArray EncryptedCommentBody(const TransferItem &item) {
	using Source = TransferItem::Source;
	using Format = TransferItem::EncryptedFormat;
	if (item.source == Source::Server
		&& item.encryptedFormat == Format::ServerPayload) {
		return ServerCommentBody(item.encryptedPayload);
	} else if (item.source == Source::Engine
		&& item.encryptedFormat == Format::EngineBodyBoc) {
		return item.encryptedPayload;
	}
	return QByteArray();
}

[[nodiscard]] KeyAuthorization TrackCommentInstallation(
		KeyAuthorization auth,
		const std::shared_ptr<KeyAuthorization> &installed) {
	installed->grant = auth.grant;
	if (const auto install = auth.install) {
		auth.install = [=](CustodyInstallRequest request) {
			request.ready = [=, ready = std::move(request.ready)](
					CustodyInstall result) {
				installed->grant = result.grant;
				ready(std::move(result));
			};
			install(std::move(request));
		};
	}
	return auth;
}

struct DecryptedComment {
	SecureBytes text;
	CommentDecryptError error = CommentDecryptError::None;
	SecretReadFailure secret = SecretReadFailure::None;
};

[[nodiscard]] DecryptedComment DecryptCommentBody(
		const std::shared_ptr<engine::WalletClient> &client,
		const engine::DecryptCommentRequest &request) {
	using Error = CommentDecryptError;
	auto watch = SecretReadWatch();
	try {
		auto text = client->decrypt_comment(request);
		const auto wipe = gsl::finally([&] {
			OPENSSL_cleanse(text.data(), text.size());
		});
		return { .text = SecureBytes(bytes::make_span(text)) };
	} catch (const engine::wallet_client_error::EncryptedCommentUnavailable &error) {
		LOG(("Wallet Error: comment decryption failed: %1"
			).arg(QString::fromUtf8(error.what())));
		// The engine reports a read the host refused and a decryption with
		// the wrong key alike; only the watch tells which one this was.
		const auto secret = watch.failure();
		return { .error = (secret != SecretReadFailure::None)
			? Error::KeyUnreadable
			: Error::DecryptionFailed,
			.secret = secret };
	} catch (const engine::wallet_client_error::LocalSigningUnavailable &) {
		return { .error = Error::Unavailable, .secret = watch.failure() };
	} catch (const engine::wallet_client_error::InvalidProtectedSecret &) {
		// The secret this device stored is broken, which is a statement
		// about the key, not about what is available right now.
		return {
			.error = Error::KeyUnreadable,
			.secret = (watch.failure() != SecretReadFailure::None)
				? watch.failure()
				: SecretReadFailure::Unreadable,
		};
	} catch (const engine::wallet_client_error::SendAlreadyInProgress &) {
		return { .error = Error::Busy };
	} catch (const engine::wallet_client_error::StateUnavailable &) {
		return { .error = Error::Cancelled };
	} catch (...) {
		return { .error = Error::Failed };
	}
}

[[nodiscard]] QString ClientErrorName(std::exception_ptr error) {
	if (!error) {
		return u"unknown"_q;
	}
	try {
		std::rethrow_exception(error);
	} catch (const engine::wallet_client_error::WalletIdentityMismatch &) {
		return u"WalletIdentityMismatch"_q;
	} catch (const engine::wallet_client_error::InvalidWalletPublicKey &) {
		return u"InvalidWalletPublicKey"_q;
	} catch (const engine::wallet_client_error
			::InvalidLocalSecretReference &) {
		return u"InvalidLocalSecretReference"_q;
	} catch (const engine::wallet_client_error::InvalidProviderBaseUrl &) {
		return u"InvalidProviderBaseUrl"_q;
	} catch (...) {
	}
	return u"unknown"_q;
}

[[nodiscard]] engine::WalletClientConfig ClientConfigFromRecord(
		const CustodyRecord &record) {
	return engine::WalletClientConfig{
		.record_id = record.recordId.toStdString(),
		.address = record.address.toStdString(),
		.public_key = std::vector<uint8_t>(
			record.publicKey.constData(),
			record.publicKey.constData() + record.publicKey.size()),
		.local_secret_ref = engine::ProtectedSecretRef{
			.value = record.secretRef.toStdString(),
		},
		.network = engine::Network(record.network),
		.send_validity_seconds = kClientSendValiditySeconds,
		.resolution_margin_seconds = kClientResolutionMarginSeconds,
		.providers = engine::ProviderConfig{
			.toncenter_base_url = "https://toncenter.com",
			.dns_root_address = std::nullopt,
			.request_timeout_ms = kClientRequestTimeoutMs,
		},
	};
}

// A public-key-only client for the served wallet. The engine reads state
// and emulates transfers from the address and key alone, with a placeholder
// signature, and answers send() with LocalSigningUnavailable. Its record id
// names no custody record, so nothing it could journal reads back as a
// signing record's, and the session never binds _clientRecordId to it.
[[nodiscard]] engine::WalletClientConfig ClientConfigForPreview(
		const TransferWalletIdentity &identity) {
	return engine::WalletClientConfig{
		.record_id = kPreviewClientRecordId,
		.address = FormatFriendly(identity.address, false).toStdString(),
		.public_key = std::vector<uint8_t>(
			identity.publicKey.constData(),
			identity.publicKey.constData() + identity.publicKey.size()),
		.local_secret_ref = std::nullopt,
		.network = engine::Network::kMainnet,
		.send_validity_seconds = kClientSendValiditySeconds,
		.resolution_margin_seconds = kClientResolutionMarginSeconds,
		.providers = engine::ProviderConfig{
			.toncenter_base_url = "https://toncenter.com",
			.dns_root_address = std::nullopt,
			.request_timeout_ms = kClientRequestTimeoutMs,
		},
	};
}

[[nodiscard]] std::optional<std::vector<int>> ParseHolderDcs(
		const MTPDwallet_secretPhraseParts &data) {
	const auto &list = data.vdcs().v;
	if (data.vtoken().v.isEmpty() || list.isEmpty()) {
		LOG(("Wallet Error: invalid backup holders token_empty=%1 count=%2."
			).arg(data.vtoken().v.isEmpty()).arg(list.size()));
		return std::nullopt;
	}
	auto result = std::vector<int>();
	result.reserve(list.size());
	for (const auto &dc : list) {
		if (dc.v <= 0
			|| dc.v >= MTP::kDcShift
			|| ranges::contains(result, dc.v)) {
			LOG(("Wallet Error: invalid backup holder index=%1 dc=%2 "
				"duplicate=%3 count=%4."
				).arg(result.size()
				).arg(dc.v
				).arg(ranges::contains(result, dc.v)
				).arg(list.size()));
			return std::nullopt;
		}
		result.push_back(dc.v);
	}
	return result;
}

[[nodiscard]] std::optional<std::vector<QByteArray>> ParseBackupHolderKeys(
		const QVector<MTPwallet_HolderDc> &list) {
	if (list.size() < 2) {
		return std::nullopt;
	}
	auto dcs = std::vector<int>();
	auto result = std::vector<QByteArray>();
	dcs.reserve(list.size());
	result.reserve(list.size());
	for (const auto &holder : list) {
		const auto &data = holder.data();
		const auto dc = data.vdc().v;
		const auto &key = data.vpublic_key().v;
		if (dc <= 0
			|| ranges::contains(dcs, dc)
			|| key.size() != PhraseShares::kPublicKeySize) {
			return std::nullopt;
		}
		dcs.push_back(dc);
		result.push_back(key);
	}
	return result;
}

[[nodiscard]] std::optional<std::vector<QByteArray>> SealBackupParts(
		const std::vector<QByteArray> &holderKeys,
		const std::vector<QString> &words) {
	const auto seed = PhraseShares::SeedFromWords(words);
	if (seed.isEmpty()) {
		return std::nullopt;
	}
	const auto shares = PhraseShares::SplitSeed(seed, int(holderKeys.size()));
	auto result = std::vector<QByteArray>();
	result.reserve(holderKeys.size());
	for (auto i = 0, count = int(holderKeys.size()); i != count; ++i) {
		auto part = PhraseShares::EncryptShare(holderKeys[i], shares[i]);
		if (!part) {
			return std::nullopt;
		}
		result.push_back(std::move(*part));
	}
	return result;
}

void FinishShareFetch(
		MTP::Sender &api,
		base::Timer &deadline,
		const std::shared_ptr<ShareFetch> &state) {
	deadline.cancel();
	for (auto &id : state->requests) {
		api.request(base::take(id)).cancel();
	}
	for (const auto shiftedDcId : base::take(state->sessions)) {
		api.instance().killSession(shiftedDcId);
	}
}

void FailShareFetch(
		MTP::Sender &api,
		base::Timer &deadline,
		const std::shared_ptr<ShareFetch> &state,
		const QString &error) {
	LOG(("Wallet Error: share fetch failed error=%1 export_request=%2 "
		"elapsed_ms=%3 pending=%4 total=%5."
		).arg(error
		).arg(state->exportRequestId
		).arg(crl::now() - state->startedAt
		).arg(state->pending
		).arg(state->shares.size()));
	for (auto i = 0; i != state->shares.size(); ++i) {
		LOG(("Wallet Error: share fetch holder export_request=%1 "
			"index=%2 dc=%3 request=%4 share_bytes=%5."
			).arg(state->exportRequestId
			).arg(i
			).arg(state->dcs[i]
			).arg(state->requests[i]
			).arg(state->shares[i].size()));
	}
	FinishShareFetch(api, deadline, state);
	if (const auto fail = base::take(state->fail)) {
		fail(error);
	}
}

[[nodiscard]] bool OpenSharePart(
		const std::shared_ptr<ShareFetch> &state,
		int index,
		const QByteArray &data) {
	auto share = PhraseShares::DecryptShare(state->keys, data);
	if (!share) {
		LOG(("Wallet Error: share part could not be opened "
			"export_request=%1 index=%2 dc=%3 encrypted_bytes=%4."
			).arg(state->exportRequestId
			).arg(index
			).arg(state->dcs[index]
			).arg(data.size()));
		return false;
	}
	state->shares[index] = std::move(*share);
	return true;
}

[[nodiscard]] QString TransferTerminalCode(TransferTerminal terminal) {
	switch (terminal) {
	case TransferTerminal::Failed:
		return u"WALLET_TRANSFER_FAILED"_q;
	case TransferTerminal::Cancelled:
		return u"WALLET_TRANSFER_CANCELLED"_q;
	case TransferTerminal::Expired:
		return u"WALLET_TRANSFER_EXPIRED"_q;
	case TransferTerminal::Replaced:
		return u"WALLET_TRANSFER_REPLACED"_q;
	case TransferTerminal::SequenceNumberConsumed:
		return u"WALLET_TRANSFER_SEQNO_CONSUMED"_q;
	case TransferTerminal::Superseded:
		return u"WALLET_TRANSFER_SUPERSEDED"_q;
	case TransferTerminal::None:
	case TransferTerminal::Confirmed:
		return QString();
	}
	Unexpected("Terminal value in TransferTerminalCode.");
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
	} catch (const engine::wallet_client_error
			::EncryptedCommentUnavailable &) {
		return SendError::CommentEncryptionUnavailable;
	} catch (const engine::wallet_client_error::NftTransferUnavailable &) {
		return SendError::CollectibleUnavailable;
	} catch (const engine::wallet_client_error
			::NftTransferEmulationRejected &) {
		return SendError::CollectibleRejected;
	} catch (...) {
	}
	return SendError::Failed;
}

[[nodiscard]] bool IsInsufficientForFees(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error
			::InsufficientBalanceForFees &) {
		return true;
	} catch (...) {
	}
	return false;
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

// A 4xx answer is the server refusing the method without executing it,
// the same reading the engine gives its own explicit-rejection status
// list, so nothing was broadcast and a fresh signature for the next
// attempt is safe. A 5xx, a negative or a local code (a transport
// timeout, for one) may have executed the method before the answer was
// lost, so it stays uncertain and the engine's journal keeps the
// operation unresolved instead of freeing the slot.
[[nodiscard]] std::optional<SendError> DefiniteTransferRefusal(
		const MTP::Error &error) {
	const auto code = error.code();
	if (code < 400 || code >= 500) {
		return std::nullopt;
	} else if (MTP::IgnoreError(error)) {
		return SendError::Silent;
	}
	const auto &type = error.type();
	return (type == u"WALLET_TRANSFER_SEND_FAILED"_q)
		? SendError::Rejected
		: (type == u"WALLET_TRANSFER_DATA_INVALID"_q)
		? SendError::DataInvalid
		: (type == u"WALLET_KEY_MISMATCH"_q)
		? SendError::KeyMismatch
		: SendError::Failed;
}

[[nodiscard]] MTPInputUser TransferRecipientInput(
		not_null<Main::Session*> session,
		UserId id) {
	const auto user = id ? session->data().userLoaded(id) : nullptr;
	return (user && !user->isSelf() && user->accessHash())
		? MTP_inputUser(MTP_long(id.bare), MTP_long(user->accessHash()))
		: MTP_inputUserEmpty();
}

[[nodiscard]] QString RotationErrorToken(const EngineError &error) {
	if (!error.underlying) {
		return u"ROTATION_FAILED"_q;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error::KeyRotationUnavailable &) {
		return u"ROTATION_PREPARE_FAILED"_q;
	} catch (const engine::wallet_client_error::InvalidProtectedSecret &) {
		return u"ROTATION_PREPARE_FAILED"_q;
	} catch (const engine::wallet_client_error::LocalSigningUnavailable &) {
		return u"ROTATION_SIGNING_UNAVAILABLE"_q;
	} catch (const engine::wallet_client_error::SendAlreadyInProgress &) {
		return u"ROTATION_ALREADY_SENDING"_q;
	} catch (const engine::wallet_client_error
			::PreviousSubmissionUnresolved &) {
		return u"ROTATION_ALREADY_SENDING"_q;
	} catch (const engine::wallet_client_error::WalletSeqnoNotAdvanced &) {
		return u"ROTATION_ALREADY_SENDING"_q;
	} catch (const engine::wallet_client_error::InsufficientBalanceForFees &) {
		return u"ROTATION_FEES"_q;
	} catch (const engine::wallet_client_error::SendFailed &) {
		return u"ROTATION_REFUSED"_q;
	} catch (...) {
	}
	return u"ROTATION_FAILED"_q;
}

[[nodiscard]] engine::SendIntent IntentFromArgs(
		const SendArgs &args,
		engine::SendMessageBody body) {
	auto message = engine::SendMessage{
		.destination = FormatFriendly(
			args.destination,
			args.bounce).toStdString(),
		.amount = engine::SendAmount(engine::SendAmount::kExact{
			.nanograms = QString::number(args.amountNano).toStdString(),
		}),
		.body = std::move(body),
		.bounce = args.bounce,
		.state_init = std::nullopt,
	};
	return engine::SendIntent{
		.expiration = engine::SendExpiration(
			engine::SendExpiration::kEngineDefault{}),
		.messages = { std::move(message) },
	};
}

[[nodiscard]] engine::NftTransferIntent CollectibleTransferIntent(
		const SendArgs &args) {
	auto payload = args.comment.text.isEmpty()
		? engine::NftTransferPayload(engine::NftTransferPayload::kEmpty{})
		: engine::NftTransferPayload(engine::NftTransferPayload::kComment{
			.text = args.comment.text.toUtf8().toStdString(),
		});
	return engine::NftTransferIntent{
		.nft_address = FormatFriendly(args.collectible, true).toStdString(),
		.recipient = FormatFriendly(
			args.destination,
			args.bounce).toStdString(),
		.funding = engine::NftTransferFunding(
			engine::NftTransferFunding::kExact{
				.attached_nanograms = QString::number(
					args.amountNano).toStdString(),
				.forward_nanograms = QString::number(
					kCollectibleTransferForwardNanos).toStdString(),
			}),
		.payload = std::move(payload),
		.expiration = engine::SendExpiration(
			engine::SendExpiration::kEngineDefault{}),
	};
}

[[nodiscard]] std::optional<TonConnectTransfer> TonConnectTransferFromEngine(
		const engine::SendRequest &request) {
	auto result = TonConnectTransfer{
		.request = std::make_shared<const engine::SendRequest>(request),
	};
	for (const auto &message : request.intent.messages) {
		const auto &amount = message.amount.get_variant();
		const auto exact = std::get_if<engine::SendAmount::kExact>(&amount);
		const auto nano = exact
			? DecimalInt64(exact->nanograms)
			: std::optional<int64>();
		if (!nano
			|| *nano < 0
			|| *nano > std::numeric_limits<int64>::max() - result.totalNano) {
			return std::nullopt;
		}
		result.totalNano += *nano;
		auto entry = TonConnectMessage{
			.destination = QString::fromStdString(message.destination),
			.amountNano = *nano,
			.deploys = message.state_init.has_value(),
		};
		const auto &body = message.body.get_variant();
		const auto comment = std::get_if<
			engine::SendMessageBody::kComment>(&body);
		const auto raw = std::get_if<
			engine::SendMessageBody::kRawPayload>(&body);
		if (comment) {
			entry.comment = QString::fromStdString(comment->text);
		} else if (raw) {
			const auto boc = QString::fromStdString(raw->boc);
			if (const auto text = Gram::TextCommentFromBoc(boc)) {
				entry.comment = *text;
			} else {
				entry.payload = boc;
			}
		}
		result.messages.push_back(std::move(entry));
	}
	const auto &expiration = request.intent.expiration.get_variant();
	const auto until = std::get_if<
		engine::SendExpiration::kExact>(&expiration);
	if (until) {
		result.validUntil = TimeId(std::min<uint64>(
			until->unix_timestamp,
			std::numeric_limits<TimeId>::max()));
	}
	return result;
}

[[nodiscard]] std::vector<TonConnectSignDataField> DecodedCellFields(
		engine::TonConnectDerivedSession &session,
		const std::string &schema,
		const std::string &cell) {
	using Decoding = engine::TonConnectSignDataCellDecoding;
	auto result = std::vector<TonConnectSignDataField>();
	try {
		const auto decoding = session.decode_sign_data_cell(schema, cell);
		const auto decoded = std::get_if<Decoding::kDecoded>(
			&decoding.get_variant());
		if (!decoded) {
			return result;
		}
		result.reserve(decoded->fields.size());
		for (const auto &field : decoded->fields) {
			result.push_back({
				.depth = int(std::min(
					field.depth,
					uint32_t(std::numeric_limits<int>::max()))),
				.name = QString::fromStdString(field.name),
				.value = QString::fromStdString(field.value),
			});
		}
	} catch (...) {
		// Display only: a failed decode must not cost the request its answer.
		return {};
	}
	return result;
}

[[nodiscard]] TonConnectSignData TonConnectSignDataFromEngine(
		engine::TonConnectDerivedSession &session,
		const engine::TonConnectSignDataRequest &request) {
	using Payload = engine::TonConnectSignDataPayload;
	using Type = TonConnectSignDataType;
	auto result = TonConnectSignData{
		.request = std::make_shared<const engine::TonConnectSignDataRequest>(
			request),
	};
	std::visit([&](const auto &data) {
		using T = std::decay_t<decltype(data)>;
		if constexpr (std::is_same_v<T, Payload::kText>) {
			result.type = Type::Text;
			result.data = QString::fromStdString(data.text);
		} else if constexpr (std::is_same_v<T, Payload::kBinary>) {
			result.type = Type::Binary;
			result.data = QString::fromStdString(data.bytes);
		} else if constexpr (std::is_same_v<T, Payload::kCell>) {
			result.type = Type::Cell;
			result.data = QString::fromStdString(data.cell);
			result.schema = QString::fromStdString(data.schema);
			result.fields = DecodedCellFields(session, data.schema, data.cell);
		}
	}, request.payload.get_variant());
	return result;
}

[[nodiscard]] int TonConnectProtocolCode(
		engine::TonConnectRpcErrorCode code) {
	using Code = engine::TonConnectRpcErrorCode;
	switch (code) {
	case Code::kUnknown: return 0;
	case Code::kBadRequest: return 1;
	case Code::kUnknownApp: return 100;
	case Code::kUserDeclined: return 300;
	case Code::kMethodNotSupported: return 400;
	}
	return -1;
}

[[nodiscard]] TonConnectAppRequest TonConnectAppRequestFromEngine(
		engine::TonConnectDerivedSession &session,
		engine::TonConnectDerivedRequest derived) {
	using Kind = TonConnectRequestKind;
	using Incoming = engine::TonConnectIncomingRequest;
	auto result = TonConnectAppRequest();
	const auto &variant = derived.request.get_variant();
	std::visit([&](const auto &data) {
		result.id = QString::fromStdString(data.id);
		result.method = QString::fromStdString(data.method);
	}, variant);
	if (const auto send = std::get_if<Incoming::kSendTransaction>(&variant)) {
		auto transfer = TonConnectTransferFromEngine(send->request);
		result.kind = transfer ? Kind::SendTransaction : Kind::Invalid;
		if (transfer) {
			result.transfer = std::make_shared<const TonConnectTransfer>(
				std::move(*transfer));
		}
	} else if (const auto sign = std::get_if<Incoming::kSignData>(&variant)) {
		result.kind = Kind::SignData;
		result.signData = std::make_shared<const TonConnectSignData>(
			TonConnectSignDataFromEngine(session, sign->request));
	} else if (std::get_if<Incoming::kSignMessage>(&variant)) {
		result.kind = Kind::Unsupported;
		result.rejection = TonConnectError::MethodNotSupported;
	} else if (std::get_if<Incoming::kDisconnect>(&variant)) {
		result.kind = Kind::Disconnect;
	} else {
		using Code = engine::TonConnectRpcErrorCode;
		const auto bad = std::get_if<Incoming::kUnsupported>(&variant);
		if (bad) {
			LOG(("Wallet Error: TON Connect %1 request not handled, "
				"code %2: %3"
				).arg(result.method
				).arg(TonConnectProtocolCode(bad->error_code)
				).arg(QString::fromStdString(bad->error_message)));
		}
		const auto known = (result.method == u"sendTransaction"_q)
			|| (result.method == u"signData"_q);
		result.kind = known
			? Kind::Invalid
			: (result.method == u"disconnect"_q)
			? Kind::Disconnect
			: Kind::Unsupported;
		if (!known
			|| (bad && bad->error_code == Code::kMethodNotSupported)) {
			result.rejection = TonConnectError::MethodNotSupported;
		}
	}
	return result;
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

[[nodiscard]] TransferTerminal StoredTransferTerminal(
		engine::SendPhase phase) {
	switch (phase) {
	case engine::SendPhase::kConfirmed:
		return TransferTerminal::Confirmed;
	case engine::SendPhase::kReplaced:
		return TransferTerminal::Replaced;
	case engine::SendPhase::kSequenceNumberConsumed:
		return TransferTerminal::SequenceNumberConsumed;
	case engine::SendPhase::kExpired:
		return TransferTerminal::Expired;
	case engine::SendPhase::kSuperseded:
		return TransferTerminal::Superseded;
	case engine::SendPhase::kFailed:
		return TransferTerminal::Failed;
	case engine::SendPhase::kCancelled:
		return TransferTerminal::Cancelled;
	default:
		return TransferTerminal::None;
	}
}

[[nodiscard]] std::optional<engine::SendPhase> RestoredTransferTerminal(
		TransferTerminal terminal) {
	switch (terminal) {
	case TransferTerminal::Confirmed:
		return engine::SendPhase::kConfirmed;
	case TransferTerminal::Replaced:
		return engine::SendPhase::kReplaced;
	case TransferTerminal::SequenceNumberConsumed:
		return engine::SendPhase::kSequenceNumberConsumed;
	case TransferTerminal::Expired:
		return engine::SendPhase::kExpired;
	case TransferTerminal::Superseded:
		return engine::SendPhase::kSuperseded;
	case TransferTerminal::Failed:
		return engine::SendPhase::kFailed;
	case TransferTerminal::Cancelled:
		return engine::SendPhase::kCancelled;
	case TransferTerminal::None:
		return std::nullopt;
	}
	Unexpected("Invalid stored transfer terminal.");
}

[[nodiscard]] std::optional<TimeId> OldestHistoryDate(
		const std::vector<TransferItem> &history) {
	auto result = std::optional<TimeId>();
	for (const auto &item : history) {
		if (item.date && (!result || *item.date < *result)) {
			result = item.date;
		}
	}
	return result;
}

[[nodiscard]] bool StaleSubmittedRecord(
		const SubmittedTransferRecord &record,
		TimeId now) {
	constexpr auto kWindow = TimeId(kClientSendValiditySeconds
		+ kClientResolutionMarginSeconds);
	return (record.posted > 0) && (now - record.posted > kWindow);
}

[[nodiscard]] bool FailedTransferTerminal(TransferTerminal terminal) {
	switch (terminal) {
	case TransferTerminal::Replaced:
	case TransferTerminal::Expired:
	case TransferTerminal::Failed:
	case TransferTerminal::Cancelled:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] engine::SendPhase PairedSendPhase(
		engine::SendPhase phase,
		bool paired) {
	return (paired && phase == engine::SendPhase::kReplaced)
		? engine::SendPhase::kSequenceNumberConsumed
		: phase;
}

[[nodiscard]] TonConnectSendResult TonConnectSendOutcome(
		const engine::SendResult &result,
		const std::string &operationId,
		bool rpcStarted,
		const QByteArray &normal) {
	const auto unknown = [&] {
		return TonConnectSendResult{
			rpcStarted ? QString::fromLatin1(normal.toBase64()) : QString(),
			SendError::SubmissionUnknown,
		};
	};
	if (result.operation_id != operationId) {
		return unknown();
	}
	const auto boc = QString::fromStdString(result.signed_boc);
	switch (result.phase) {
	case engine::SendPhase::kSubmitted:
	case engine::SendPhase::kConfirmed:
		return { boc, SendError::None };
	case engine::SendPhase::kSubmissionUnknown:
		return { boc, SendError::SubmissionUnknown };
	case engine::SendPhase::kHandedOff:
	case engine::SendPhase::kIdle:
	case engine::SendPhase::kValidating:
	case engine::SendPhase::kAuthorizing:
	case engine::SendPhase::kPreparing:
	case engine::SendPhase::kPersisting:
	case engine::SendPhase::kReadyToSubmit:
	case engine::SendPhase::kSubmitting:
		return unknown();
	case engine::SendPhase::kFailed:
	case engine::SendPhase::kCancelled:
	case engine::SendPhase::kReplaced:
	case engine::SendPhase::kSequenceNumberConsumed:
	case engine::SendPhase::kExpired:
	case engine::SendPhase::kSuperseded:
		break;
	}
	return { QString(), SendError::Failed };
}

[[nodiscard]] SubmittedTransferProjection StoredTransferProjection(
		const TransferItem &item) {
	const auto peer = (item.kind == TransferItem::Kind::PeerTransfer);
	const auto collectible = (item.kind == TransferItem::Kind::Collectible)
		? item.collectible
		: QString();
	return SubmittedTransferProjection{
		.id = item.id,
		.counterparty = item.counterparty,
		.counterpartyName = item.counterpartyName,
		.comment = item.commentEncrypted ? QString() : item.comment,
		.collectible = collectible,
		.counterpartyPeer = ((peer || !collectible.isEmpty())
			? item.counterpartyPeer
			: 0),
		.amountNano = item.amountNano,
		.feeNano = item.feeNano,
		.date = item.date,
		.peerTransfer = peer,
		.failed = (item.status == TransferItem::Status::Failure),
		.commentEncrypted = item.commentEncrypted,
		.gasless = item.gasless,
		.counterpartyBounceable = item.counterpartyBounceable,
	};
}

// A user send's served row may name only the address it was sent to.
void KeepSubmittedRecipient(
		TransferItem &item,
		const SubmittedTransferRecord &record) {
	using Kind = TransferItem::Kind;
	if (!record.recipient
		|| item.counterpartyPeer
		|| item.incoming
		|| (item.kind != Kind::Transfer && item.kind != Kind::Collectible)
		|| item.counterparty != record.destination) {
		return;
	}
	if (item.kind == Kind::Transfer) {
		item.kind = Kind::PeerTransfer;
	}
	item.counterpartyPeer = peerFromUser(record.recipient).value;
}

[[nodiscard]] std::optional<Gram::NftWebDocument> WebDocumentFromServer(
		const tl::conditional<MTPWebDocument> &document) {
	if (!document) {
		return std::nullopt;
	}
	return document->match([](const MTPDwebDocument &web) {
		return std::make_optional(Gram::NftWebDocument{
			.url = web.vurl().v,
			.accessHash = web.vaccess_hash().v,
			.mimeType = qs(web.vmime_type()),
		});
	}, [](const MTPDwebDocumentNoProxy &) {
		// A direct fetch would reveal the user's IP to the media host.
		return std::optional<Gram::NftWebDocument>();
	});
}

[[nodiscard]] std::optional<Gram::NftItem> CollectibleFromServer(
		const MTPwallet_NftItem &item) {
	const auto &data = item.data();
	const auto address = CanonicalAddress(qs(data.vaddress()));
	if (address.isEmpty()) {
		LOG(("Wallet Error: wallet.NftItem address is not parseable."));
		return std::nullopt;
	}
	auto result = Gram::NftItem();
	result.address = address;
	if (const auto collection = data.vcollection_address()) {
		result.collection = CanonicalAddress(qs(*collection));
	}
	result.index = qs(data.vindex());
	if (const auto name = data.vname()) {
		result.name = qs(*name);
	}
	result.image = WebDocumentFromServer(data.vimage());
	result.imageSmall = WebDocumentFromServer(data.vimage_small());
	result.contentUrl = WebDocumentFromServer(data.vcontent_url());
	result.lottie = WebDocumentFromServer(data.vlottie());
	Gram::ClassifyNftKind(result);
	return result;
}

// Every surface that paints a transfer prefixes a direction character of
// its own, so the amount and the direction are decided together here and
// never left as two fields a view has to reconcile: the served key-change
// row arrives as -300000000 nanograms with `incoming` clear, and the row,
// the details header and its fiat line each sign it a second time. When
// the two disagree the sign wins, because it is the source's arithmetic
// about this wallet's balance while the flag only summarizes it, and a
// row that removed value shown with a plus is the reading a user acts on.
void SetDirectedAmount(
		TransferItem &item,
		int64 nanograms,
		bool incoming) {
	const auto smallest = std::numeric_limits<int64>::min();
	if (nanograms == smallest) {
		// Negating it is undefined and its magnitude is not an int64, so
		// the one amount the record cannot hold is stored exactly as it
		// was sent instead of clamped into an amount nobody sent.
		LOG(("Wallet Error: transaction amount magnitude does not fit."));
	}
	const auto fold = (nanograms < 0) && (nanograms != smallest);
	item.amountNano = fold ? -nanograms : nanograms;
	item.incoming = fold ? false : incoming;
}

[[nodiscard]] std::optional<TransferItem> HistoryItemFromEngine(
		const engine::ActivityItem &item,
		const std::optional<TransferWalletIdentity> &identity) {
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
	result.source = TransferItem::Source::Engine;
	result.id = QString::fromStdString(item.id);
	result.walletIdentity = identity;
	result.kind = TransferItem::Kind::Transfer;
	if (item.counterparty) {
		result.counterparty = CanonicalAddress(
			QString::fromStdString(*item.counterparty));
	}
	SetDirectedAmount(
		result,
		*amount,
		(item.direction == engine::ActivityDirection::kReceived));
	result.feeNano = *fee;
	if (item.encrypted_comment) {
		result.commentEncrypted = true;
		result.encryptedPayload = QByteArray::fromStdString(
			*item.encrypted_comment);
		if (!result.encryptedPayload.isEmpty()) {
			result.encryptedFormat = TransferItem::EncryptedFormat::EngineBodyBoc;
		}
	} else if (item.comment) {
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

[[nodiscard]] TransferItem HistoryItemFromServer(
		const MTPWalletTransaction &item,
		const std::optional<TransferWalletIdentity> &identity) {
	const auto &data = item.data();
	auto result = TransferItem();
	result.source = TransferItem::Source::Server;
	result.id = qs(data.vid());
	result.walletIdentity = identity;
	SetDirectedAmount(result, data.vamount().v, data.is_incoming());
	result.feeNano = data.vfee().v;
	result.gasless = data.is_gasless();
	result.date = data.vdate().v;
	result.status = data.is_failed()
		? TransferItem::Status::Failure
		: TransferItem::Status::Success;
	if (data.is_key_change()) {
		// The peer is not read on purpose: a key change names no
		// counterparty, and whatever the server puts there (the wallet's
		// own address, for one) would render the row as a transfer to or
		// from someone, which is exactly the reading this kind exists to
		// prevent. The direction is decided here, once: a key change is
		// the wallet's own outgoing transaction whatever the server's
		// incoming bit says.
		result.kind = TransferItem::Kind::KeyChange;
		result.incoming = false;
	} else {
		const auto setCounterparty = [&](const auto &data) {
			const auto parsed = ParseAddress(qs(data.vaddress()));
			result.counterparty = parsed ? parsed->raw : QString();
			result.counterpartyBounceable = parsed
				&& parsed->friendly
				&& parsed->bounceable;
			if (const auto domain = data.vdomain()) {
				result.counterpartyName = qs(*domain);
			}
			if (result.counterparty.isEmpty()) {
				LOG(("Wallet Error: wallet.getTransactions sent an unusable "
					"counterparty address."));
			}
		};
		data.vpeer().match([&](const MTPDwalletTransactionPeerUser &data) {
			result.kind = TransferItem::Kind::PeerTransfer;
			result.counterpartyPeer = peerFromUser(data.vuser_id()).value;
			setCounterparty(data);
		}, [&](const MTPDwalletTransactionPeerAddress &data) {
			setCounterparty(data);
		}, [&](const MTPDwalletTransactionPeerOnramp &data) {
			setCounterparty(data);
			// Without a provider or an address it reads as the address peer.
			const auto provider = qs(data.vprovider_name()).trimmed();
			if (!provider.isEmpty() && !result.counterparty.isEmpty()) {
				result.kind = TransferItem::Kind::Onramp;
				result.provider = provider;
			}
		}, [](const MTPDwalletTransactionPeerUnsupported &) {
			// Nothing is written, because the defaults are the row: a
			// Kind::Transfer with no counterparty renders through
			// RowContentFromItem's fall-through as a Deposit or a
			// Withdrawal by direction, with the date and the amount. That
			// is exactly the graceful degradation this constructor exists
			// for, so no lang key is invented for it.
		});
		if (const auto nft = data.vnft()) {
			if (auto record = CollectibleFromServer(*nft)) {
				result.kind = TransferItem::Kind::Collectible;
				result.collectible = record->address;
				result.collectibleRecord = std::move(*record);
				result.provider = QString();
			}
		}
	}
	result.commentEncrypted = data.is_comment_encrypted();
	if (const auto comment = data.vcomment()) {
		if (result.commentEncrypted) {
			result.encryptedPayload = DecodeServerEncryptedComment(
				qs(*comment));
			if (!result.encryptedPayload.isEmpty()) {
				result.encryptedFormat
					= TransferItem::EncryptedFormat::ServerPayload;
			}
		} else {
			result.comment = qs(*comment);
		}
	}
	if (const auto hash = data.vtx_hash()) {
		result.traceId = TransactionHashFromServer(qs(*hash));
	}
	return result;
}

[[nodiscard]] std::optional<TransferReceipt> ReceiptFromServer(
		const MTPDupdateSentWalletTransaction &data) {
	// The contract names msg_hash a string and fixes no encoding for
	// it, so the only rule this client may impose is that a receipt
	// addresses a message at all: the bytes are kept exactly as they
	// arrived and echoed unchanged into the lookup. Reading them as
	// text would be a guess, and a guess that refused a token the
	// server accepts would leave an accepted payment unresolvable and
	// refuse the next send for the whole resolution window.
	const auto &hash = data.vmsg_hash().v;
	if (hash.isEmpty()) {
		return std::nullopt;
	}
	return TransferReceipt{
		.messageHash = hash,
		.gasless = data.is_gasless(),
	};
}

[[nodiscard]] const MTPDupdateSentWalletTransaction *SentUpdateFromServer(
		const MTPUpdates &updates) {
	auto result = static_cast<const MTPUpdate*>(nullptr);
	auto conflicting = false;
	const auto inspect = [&](const MTPUpdate &update) {
		if (update.type() != mtpc_updateSentWalletTransaction || conflicting) {
			return;
		} else if (!result) {
			result = &update;
			return;
		}
		auto previous = mtpBuffer();
		auto next = mtpBuffer();
		result->write(previous);
		update.write(next);
		conflicting = (previous != next);
	};
	const auto inspectVector = [&](const MTPVector<MTPUpdate> &list) {
		for (const auto &update : list.v) {
			inspect(update);
		}
	};
	updates.match([&](const MTPDupdates &data) {
		inspectVector(data.vupdates());
	}, [&](const MTPDupdatesCombined &data) {
		inspectVector(data.vupdates());
	}, [&](const MTPDupdateShort &data) {
		inspect(data.vupdate());
	}, [](const auto &) {
	});
	return (result && !conflicting)
		? &result->c_updateSentWalletTransaction()
		: nullptr;
}

} // namespace

// The TL declares tx_hash as an unqualified string and states no encoding,
// while ExplorerTransactionUrl() hexes whatever is stored here straight into
// a URL path with no validation and no escaping. The shape is therefore
// decided here, and a string of neither recognized shape stores nothing: an
// empty traceId hides the explorer entry instead of pointing it at a hash
// this client cannot be sure of. Base64 of 32 bytes is 43 or 44 characters
// and can therefore never also be 64 hex digits, so the two tests cannot
// collide and their order is a cheapness choice, not a correctness one.
QByteArray TransactionHashFromServer(const QString &value) {
	constexpr auto kHashBytes = 32;
	const auto latin = value.toLatin1();
	const auto hex = (latin.size() == 2 * kHashBytes)
		&& ranges::all_of(latin, [](char ch) {
			return (ch >= '0' && ch <= '9')
				|| (ch >= 'a' && ch <= 'f')
				|| (ch >= 'A' && ch <= 'F');
		});
	if (hex) {
		return QByteArray::fromHex(latin);
	}
	const auto decode = [&](QByteArray::Base64Option encoding) {
		return QByteArray::fromBase64Encoding(
			latin,
			encoding | QByteArray::AbortOnBase64DecodingErrors);
	};
	auto standard = decode(QByteArray::Base64Encoding);
	if (standard && (standard.decoded.size() == kHashBytes)) {
		return std::move(standard.decoded);
	}
	auto url = decode(QByteArray::Base64UrlEncoding);
	if (url && (url.decoded.size() == kHashBytes)) {
		return std::move(url.decoded);
	}
	LOG(("Wallet Error: Unusable transaction hash: %1").arg(value));
	return QByteArray();
}

bool EncryptedCommentPending(const TransferItem &item) {
	return item.commentEncrypted
		&& item.id.isEmpty()
		&& item.encryptedPayload.isEmpty();
}

bool EncryptedCommentUnusable(const TransferItem &item) {
	if (!item.commentEncrypted || EncryptedCommentPending(item)) {
		return false;
	} else if (item.id.isEmpty()
		|| (item.incoming && CanonicalAddress(item.counterparty).isEmpty())) {
		return true;
	}
	return !ValidEncryptedCommentBody(EncryptedCommentBody(item));
}

bool EncryptedCommentRevealable(const TransferItem &item) {
	return item.commentEncrypted
		&& !EncryptedCommentPending(item)
		&& !EncryptedCommentUnusable(item);
}

QByteArray DecodeServerEncryptedComment(const QString &encoded) {
	constexpr auto kMinBytes = 64;
	constexpr auto kMaxBytes = 1024;
	constexpr auto kEncodedLimit = 4 * ((kMaxBytes + 2) / 3);
	constexpr auto kHeaderBytes = 48;
	constexpr auto kBlockBytes = 16;
	if (encoded.isEmpty()
		|| encoded.size() > kEncodedLimit
		|| (encoded.size() % 4)) {
		return QByteArray();
	}
	const auto latin = encoded.toLatin1();
	auto decoded = QByteArray::fromBase64Encoding(
		latin,
		QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded
		|| decoded.decoded.size() < kMinBytes
		|| decoded.decoded.size() > kMaxBytes
		|| ((decoded.decoded.size() - kHeaderBytes) % kBlockBytes)
		|| decoded.decoded.toBase64() != latin) {
		return QByteArray();
	}
	return std::move(decoded.decoded);
}

std::vector<TransferItem> HistoryFromEngine(
		const std::vector<engine::ActivityItem> &items,
		std::optional<TransferWalletIdentity> identity) {
	auto result = std::vector<TransferItem>();
	result.reserve(items.size());
	for (const auto &item : items) {
		if (auto mapped = HistoryItemFromEngine(item, identity)) {
			result.push_back(std::move(*mapped));
		}
	}
	return result;
}

std::vector<TransferItem> HistoryFromServer(
		const QVector<MTPWalletTransaction> &list,
		std::optional<TransferWalletIdentity> identity) {
	auto result = std::vector<TransferItem>();
	result.reserve(list.size());
	for (const auto &item : list) {
		result.push_back(HistoryItemFromServer(item, identity));
	}
	return result;
}

std::vector<Gram::NftItem> CollectiblesFromServer(
		const QVector<MTPwallet_NftItem> &list) {
	auto result = std::vector<Gram::NftItem>();
	result.reserve(list.size());
	auto seen = base::flat_set<QString>();
	for (const auto &item : list) {
		if (auto mapped = CollectibleFromServer(item)) {
			if (seen.emplace(mapped->address).second) {
				result.push_back(std::move(*mapped));
			}
		}
	}
	return result;
}

bool IsWordlistWord(const QString &word) {
	const auto &list = Wordlist();
	const auto normalized = NormalizeWord(word);
	return std::binary_search(list.begin(), list.end(), normalized);
}

std::vector<QString> WordlistSuggestions(
		const QString &prefix,
		int limit) {
	auto result = std::vector<QString>();
	const auto normalized = NormalizeWord(prefix);
	if (normalized.isEmpty() || limit <= 0) {
		return result;
	}
	const auto &list = Wordlist();
	auto i = std::lower_bound(list.begin(), list.end(), normalized);
	while (i != list.end()
		&& int(result.size()) != limit
		&& i->startsWith(normalized)) {
		result.push_back(*i);
		++i;
	}
	return result;
}

PhraseMatch DetectPhraseMatch(const std::vector<QString> &words) {
	auto engineWords = std::vector<std::string>();
	engineWords.reserve(words.size());
	for (const auto &word : words) {
		engineWords.push_back(NormalizeWord(word).toStdString());
	}
	try {
		const auto schemes = engine::detect_mnemonic_schemes(engineWords);
		auto rotation = false;
		auto foreign = false;
		for (const auto scheme : schemes) {
			if (scheme == engine::MnemonicScheme::kRotation) {
				rotation = true;
			} else if (scheme == engine::MnemonicScheme::kTon
				|| scheme == engine::MnemonicScheme::kBip39) {
				foreign = true;
			}
		}
		if (rotation) {
			return PhraseMatch::Rotation;
		} else if (foreign) {
			return PhraseMatch::Foreign;
		}
		return PhraseMatch::None;
	} catch (...) {
		return PhraseMatch::Rotation;
	}
}

void WalletLoss::add(const WalletLoss &other) {
	unbacked += other.unbacked;
	parked += other.parked;
	rotating += other.rotating;
	holdsRecords = other.holdsRecords || holdsRecords;
	unknown = other.unknown || unknown;
}

WalletLoss WalletLossOnLogout(not_null<Main::Account*> account) {
	auto result = WalletLoss();
	const auto store = ReadCustodyStore(account->local());
	if (!store) {
		// Broken, or written by a newer format: what this device holds
		// cannot be read, and Account::reset() destroys it either way.
		result.unknown = true;
		return result;
	}
	result.holdsRecords = !store->records.empty();
	if (store->pendingRotation) {
		++result.rotating;
	}
	const auto session = account->maybeSession();
	const auto identity = session
		? session->wallet().transferWalletIdentity()
		: std::nullopt;
	const auto known = session
		&& session->wallet().presenceCurrent() == Presence::Ready
		&& identity;
	const auto backed = known
		&& session->wallet().capabilities().backupEnabled;
	for (const auto &record : store->records) {
		const auto sameWallet = known
			&& CanonicalAddress(record.address) == identity->address;
		const auto active = known
			? (sameWallet && record.signsWith(identity->publicKey))
			: record.active;
		const auto obsolete = sameWallet
			&& !active
			&& !record.signingKey.isEmpty();
		if (known ? !sameWallet : !active) {
			++result.parked;
		} else if (!known) {
			result.unknown = true;
		} else if (!obsolete && (!backed || record.rotatedSinceBackup)) {
			++result.unbacked;
		}
	}
	return result;
}

QString WalletLossWarning(WalletLoss loss) {
	auto result = QString();
	const auto append = [&](const QString &line) {
		if (!result.isEmpty()) {
			result += u"\n\n"_q;
		}
		result += line;
	};
	if (loss.unbacked > 0) {
		append(tr::lng_sure_logout_wallet_local(
			tr::now,
			lt_count,
			loss.unbacked));
	}
	if (loss.parked > 0) {
		append(tr::lng_sure_logout_wallet_parked(
			tr::now,
			lt_count,
			loss.parked));
	}
	if (loss.rotating > 0) {
		append(tr::lng_sure_logout_wallet_rotating(
			tr::now,
			lt_count,
			loss.rotating));
	}
	if (loss.unknown) {
		append(tr::lng_sure_logout_wallet_unknown(tr::now));
	}
	return result;
}

CommentScope::CommentScope(std::shared_ptr<State> state)
: _state(std::move(state)) {
}

void CommentScope::cancel() {
	if (!_state->cancelled.exchange(true)) {
		if (auto cancel = base::take(_state->cancelPending)) {
			crl::on_main(std::move(cancel));
		}
		_cancelledChanges.fire({});
	}
}

bool CommentScope::cancelled() const {
	return _state->cancelled;
}

rpl::producer<> CommentScope::cancelledChanges() const {
	return _cancelledChanges.events();
}

Session::Session(not_null<Main::Session*> session)
: _session(session)
, _api(session)
, _stateApi(&session->mtp())
, _engine(std::make_unique<Engine>(session, &_api))
, _rates(std::make_unique<Rates>(session))
, _onramp(std::make_unique<Onramp>(session))
, _userAddresses(std::make_unique<UserAddresses>(session))
, _transferMessages(std::make_unique<TransferMessages>(session))
, _tonConnect(std::make_unique<TonConnect>(session))
, _stream(std::make_unique<Stream>(&_api, [=](StreamRefresh wanted) {
	applyStreamRefresh(wanted);
}))
, _pollTimer([=] { pollTick(); })
, _gaslessTimer([=] { refreshGaslessInfo(); })
, _decryptRetryTimer([=] { settleDeferredDecrypts(); })
, _walletAvailable(session->appConfig().walletAvailable())
, _transferMinNanos(TransferMinNanos(session)) {
	vault().protectionChanges() | rpl::on_next([=] {
		updateDeviceCustodyState(true);
	}, _lifetime);
	rpl::merge(
		_transferWalletIdentityChanges.events(),
		_custodyUpdates.events()
	) | rpl::on_next([=] {
		validateCommentScopes();
	}, _commentLifetime);
	_sendState.changes() | rpl::on_next([=](SendState state) {
		if (state != SendState::Idle) {
			retireCommentScopes();
		}
	}, _commentLifetime);
	transferWalletIdentityChanges() | rpl::on_next([=] {
		_lastReceipt.reset();
		const auto hadSubmission = _submission
			|| _pending
			|| (_sendState.current() != SendState::Idle);
		retireSubmission();
		_pending.reset();
		_sendUnresolved = _sendUnresolved || hadSubmission;
		_sendState = SendState::Idle;
		resetGaslessInfo();
		refreshGaslessInfo();
		if (hadSubmission) {
			updatePollingState();
			const auto weak = base::make_weak(_engine.get());
			syncEngineClient();
			if (weak) {
				requestEngineRefresh();
			}
		}
	}, _lifetime);
	transferWalletIdentityChanges() | rpl::on_next([=] {
		_tonConnect->walletChanged();
	}, _lifetime);
	rpl::merge(
		vault().granted(),
		vault().protectionChanges()
	) | rpl::on_next([=] {
		_tonConnect->vaultChanged();
	}, _lifetime);
	session->appConfig().refreshed() | rpl::on_next([=, this] {
		applyWalletAvailable();
		applyTransferMinNanos();
		refreshGaslessInfo();
	}, _lifetime);
	resetGaslessInfo();
}

Session::~Session() {
	_tonConnect->stop();
	retireCommentScopes();
	_commentLifetime.destroy();
	if (const auto state = _shareFetch.lock()) {
		FinishShareFetch(_stateApi, _shareFetchTimer, state);
	}
	_panel = nullptr;
	retireGaslessRequest();
}

Onramp &Session::onramp() {
	return *_onramp;
}

Rates &Session::rates() {
	return *_rates;
}

TransferMessages &Session::transferMessages() {
	return *_transferMessages;
}

UserAddresses &Session::userAddresses() {
	return *_userAddresses;
}

TonConnect &Session::tonConnect() {
	return *_tonConnect;
}

Ui::SeparatePanel *Session::panel() const {
	return _panel.get();
}

void Session::setPanel(std::unique_ptr<Ui::SeparatePanel> panel) {
	_panel = std::move(panel);
	if (!_panel) {
		// _historyPaged and _collectiblesPaged are facts about one
		// overview's scroll position, so they end with the panel that
		// owned them: otherwise the periodic refresh stays refused for
		// the rest of the session and a transaction or a collectible
		// received while the panel was closed never appears. The pages
		// spent looking for a row the feed can show are such a fact too,
		// so the next reader to open the panel gets the whole bound. The
		// Walt answer is one as well, so its request is cancelled and both
		// its flag and its value go with the panel, leaving the next
		// opening to ask exactly once on its own.
		_historyPaged = false;
		_collectiblesPaged = false;
		resetHiddenHistoryPages();
		_stateApi.request(base::take(_waltBalanceRequestId)).cancel();
		_waltBalanceRequested = false;
		_existingWaltBalanceUrl = QString();
		_windowSend.clear();
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

Presence Session::presenceCurrent() const {
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

WalletCapabilities Session::capabilities() const {
	return _capabilities.current();
}

rpl::producer<WalletCapabilities> Session::capabilitiesValue() const {
	return _capabilities.value();
}

QByteArray Session::publicKey() const {
	return _publicKey;
}

auto Session::transferWalletIdentity() const
-> std::optional<TransferWalletIdentity> {
	if (_presence.current() == Presence::Unknown && _custody
		&& !_custodyReadFailed && !_custody->pendingRotation
		&& _custody->records.size() == 1) {
		const auto &record = _custody->records.front();
		const auto address = CanonicalAddress(record.address);
		if (record.active
			&& record.network == int(engine::Network::kMainnet)
			&& _custody->lastSeenServerKey.size() == kCustodyPublicKeySize
			&& record.signsWith(_custody->lastSeenServerKey)
			&& !record.recordId.isEmpty()
			&& !record.secretRef.isEmpty()
			&& !address.isEmpty()) {
			return TransferWalletIdentity{
				.address = address,
				.publicKey = _custody->lastSeenServerKey,
				.revision = _walletIdentityRevision,
			};
		}
	}
	if (_presence.current() != Presence::Ready
		|| _address.isEmpty()
		|| _publicKey.size() != kCustodyPublicKeySize) {
		return std::nullopt;
	}
	return TransferWalletIdentity{
		.address = _address,
		.publicKey = _publicKey,
		.revision = _walletIdentityRevision,
	};
}

bool Session::transferWalletIdentityCurrent(
		const TransferWalletIdentity &identity) const {
	const auto current = transferWalletIdentity();
	return current && (*current == identity);
}

rpl::producer<> Session::transferWalletIdentityChanges() const {
	return _transferWalletIdentityChanges.events();
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

void Session::requestState(
		Fn<void(const MTPWalletState &)> done,
		Fn<void()> fail) {
	if (done) {
		_stateApi.request(base::take(_stateRequestId)).cancel();
	}
	const auto startedAt = _stateRequestedAt = crl::now();
	const auto revision = _walletIdentityRevision;
	auto request = _stateApi.request(MTPwallet_GetState());
	auto &policy = done ? request.handleAllErrors() : request;
	_stateRequestId = policy.done([=](
			const MTPWalletState &result,
			mtpRequestId requestId) {
		LOG(("Wallet Info: wallet.getState request=%1 elapsed_ms=%2 "
			"requested_revision=%3 current_revision=%4; %5."
			).arg(requestId
			).arg(crl::now() - startedAt
			).arg(revision
			).arg(_walletIdentityRevision
			).arg(LogWalletState(result)));
		_stateRequestId = 0;
		if (done) {
			done(result);
		} else {
			applyState(result, false);
		}
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		LOG(("Wallet Error: wallet.getState request=%1 elapsed_ms=%2 "
			"failed: %3"
			).arg(requestId).arg(crl::now() - startedAt).arg(error.type()));
		_stateRequestId = 0;
		if (fail) {
			fail();
			return;
		}
		if (error.type() == u"WALLET_UNAVAILABLE"_q) {
			LOG(("Wallet Error: the server has no wallet for this account."));
			setPresence(Presence::Unavailable);
			return;
		}
		++_stateFailures;
		updateListsGate();
	}).send();
	LOG(("Wallet Info: wallet.getState sent request=%1 revision=%2 "
		"state_age_ms=%3."
		).arg(_stateRequestId
		).arg(revision
		).arg(_stateRefreshedAt ? (startedAt - _stateRefreshedAt) : -1));
}

bool GaslessTerms::eligible(int64 amountNano) const {
	return usable && (amountNano > 0) && (amountNano >= effectiveMinNanos);
}

// The relayer does not sponsor a transfer back to the wallet that signed it:
// the server refuses the whole pair, and the mandatory normal variant beside
// it is not executed instead, so an amount that an ordinary paid send moves
// would fail as soon as the offer was accepted. The offer therefore stops at
// the destination, and such a send stays on its authorized normal fee.
bool GaslessTerms::eligible(
		int64 amountNano,
		const QString &destination) const {
	return eligible(amountNano)
		&& identity
		&& !destination.isEmpty()
		&& (CanonicalAddress(destination) != identity->address);
}

GaslessTerms Session::gaslessTerms() {
	const auto weak = base::make_weak(_engine.get());
	refreshGaslessInfo();
	return weak ? _gaslessTerms.current() : GaslessTerms();
}

rpl::producer<GaslessTerms> Session::gaslessTermsValue() {
	const auto weak = base::make_weak(_engine.get());
	refreshGaslessInfo();
	if (!weak) {
		return rpl::single(GaslessTerms());
	}
	return _gaslessTerms.value();
}

void Session::refreshGaslessInfo(bool force) {
	if (_gaslessRefreshing) {
		return;
	}
	const auto weak = base::make_weak(_engine.get());
	_gaslessRefreshing = true;
	const auto guard = gsl::finally([=, this] {
		if (weak) {
			_gaslessRefreshing = false;
		}
	});
	const auto terms = _gaslessTerms.current();
	const auto identity = transferWalletIdentity();
	if (terms.identity != identity) {
		resetGaslessInfo();
		if (!weak) {
			return;
		}
	} else if (terms.transferMinNanos != TransferMinNanos(_session)
		|| terms.configuredMinNanos != GaslessMinNanos(_session)) {
		retireGaslessRequest();
		_gaslessExpiresAt = 0;
	}
	applyGaslessTerms(_gaslessTerms.current());
	if (!weak) {
		return;
	}
	_gaslessRefreshWanted = _gaslessRefreshWanted || force;
	if (!_preview || _preview->owners.empty() || !identity) {
		retireGaslessRequest();
		return;
	}
	const auto now = crl::now();
	if (_gaslessRequestId
		&& (now - _gaslessRequestedAt >= crl::time(kClientRequestTimeoutMs))) {
		retireGaslessRequest();
		_gaslessExpiresAt = 0;
		applyGaslessTerms(_gaslessTerms.current());
		if (!weak) {
			return;
		}
	}
	// Every prepared transfer is bound to the exact terms it was estimated
	// with, so letting the info lapse and then fetching it again would flip
	// the terms to stale and back once a minute and throw away every
	// prepared transfer with them, even when the server answers with the
	// same info. The info is fetched ahead of its expiry instead, and an
	// unchanged answer then leaves the terms, and the transfers, as they are.
	const auto expiring = _gaslessTerms.current().fresh
		&& (now >= _gaslessExpiresAt - kGaslessRefreshAhead);
	if (!_gaslessRequestId
		&& (!_gaslessTerms.current().fresh
			|| expiring
			|| _gaslessRefreshWanted)
		&& (!_gaslessRequestedAt
			|| (now - _gaslessRequestedAt >= kGaslessRetryInterval))) {
		requestGaslessInfo();
	}
	if (!weak || !_preview || _preview->owners.empty()
		|| !transferWalletIdentityCurrent(*identity)) {
		return;
	}
	const auto &current = _gaslessTerms.current();
	auto deadline = _gaslessRequestId
		? _gaslessRequestedAt + crl::time(kClientRequestTimeoutMs)
		: (!current.fresh || expiring || _gaslessRefreshWanted)
		? _gaslessRequestedAt + kGaslessRetryInterval
		: (_gaslessExpiresAt - kGaslessRefreshAhead);
	if (current.fresh) {
		deadline = std::min(deadline, _gaslessExpiresAt);
		if (current.info->resetAt > 0) {
			const auto resetIn = crl::time(current.info->resetAt)
				- crl::time(base::unixtime::now());
			deadline = std::min(deadline, now + resetIn * 1000);
		}
	}
	_gaslessTimer.callOnce(std::max(crl::time(1), deadline - crl::now()));
}

void Session::requestGaslessInfo() {
	const auto identity = transferWalletIdentity();
	if (_gaslessRequestId
		|| !_preview
		|| _preview->owners.empty()
		|| !identity) {
		return;
	}
	const auto serial = ++_gaslessRequestSerial;
	const auto generation = _networkGeneration;
	const auto weak = base::make_weak(_engine.get());
	const auto weakSession = base::make_weak(_session);
	_gaslessRequestedAt = crl::now();
	_gaslessRefreshWanted = false;
	const auto ownsRequest = [=, this] {
		return weak
			&& (serial == _gaslessRequestSerial)
			&& (generation == _networkGeneration);
	};
	const auto current = [=, this] {
		return ownsRequest()
			&& transferWalletIdentityCurrent(*identity)
			&& _preview
			&& !_preview->owners.empty();
	};
	_gaslessRequestId = _stateApi.request(
		MTPwallet_GetGaslessInfo()
	).done([=, this](const MTPUpdates &result) {
		if (ownsRequest()) {
			_gaslessRequestId = 0;
		}
		if (weakSession) {
			weakSession->api().applyUpdates(result);
		}
		if (weak) {
			refreshGaslessInfo();
		}
	}).fail([=](const MTP::Error &error) {
		if (!current()) {
			return;
		}
		LOG(("Wallet Error: the gasless request failed: %1"
			).arg(error.type()));
		_gaslessRequestId = 0;
		_gaslessExpiresAt = 0;
		refreshGaslessInfo();
	}).handleAllErrors().send();
}

void Session::retireGaslessRequest() {
	++_gaslessRequestSerial;
	_stateApi.request(base::take(_gaslessRequestId)).cancel();
	_gaslessTimer.cancel();
	_gaslessRefreshWanted = false;
}

void Session::resetGaslessInfo() {
	const auto weak = base::make_weak(_engine.get());
	const auto refreshing = std::exchange(_gaslessRefreshing, true);
	const auto guard = gsl::finally([=, this] {
		if (weak) {
			_gaslessRefreshing = refreshing;
		}
	});
	retireGaslessRequest();
	_gaslessRequestedAt = 0;
	_gaslessExpiresAt = 0;
	applyGaslessTerms(GaslessTerms());
}

void Session::applyGaslessInfo(GaslessInfo info, bool refreshed) {
	if (refreshed) {
		_gaslessExpiresAt = crl::now() + kGaslessRefreshInterval;
	}
	auto terms = _gaslessTerms.current();
	terms.info = std::move(info);
	applyGaslessTerms(std::move(terms));
}

void Session::applyGaslessTerms(GaslessTerms terms) {
	const auto &previous = _gaslessTerms.current();
	terms.identity = transferWalletIdentity();
	terms.transferMinNanos = TransferMinNanos(_session);
	terms.configuredMinNanos = GaslessMinNanos(_session);
	terms.effectiveMinNanos = std::max(
		terms.transferMinNanos,
		terms.configuredMinNanos);
	const auto &info = terms.info;
	if (info && (info->minAmount > 0)) {
		terms.effectiveMinNanos = std::max(
			terms.effectiveMinNanos,
			info->minAmount);
	}
	const auto now = base::unixtime::now();
	if ((_gaslessExpiresAt <= crl::now())
		|| (info && (info->resetAt > 0) && (info->resetAt <= now))) {
		_gaslessExpiresAt = 0;
	}
	terms.fresh = terms.identity && info && (_gaslessExpiresAt > 0);
	terms.usable = terms.fresh
		&& info->available
		&& (info->left > 0)
		&& (info->resetAt >= 0)
		&& (info->minAmount > 0)
		&& !info->relayer.isEmpty();
	terms.revision = previous.revision;
	if (terms != previous) {
		++terms.revision;
		_gaslessTerms = std::move(terms);
	}
}

QString Session::existingWaltBalanceUrl() const {
	return _existingWaltBalanceUrl.current();
}

rpl::producer<QString> Session::existingWaltBalanceUrlValue() {
	requestExistingWaltBalance();
	return _existingWaltBalanceUrl.value();
}

rpl::producer<std::optional<int64>> Session::parkedBalanceNanoValue() {
	requestParkedChecks(true);
	return _parkedBalanceNano.value();
}

void Session::syncParkedChecks(
		const CustodyStore &store,
		const QString &servedAddress) {
	auto addresses = std::vector<QString>();
	for (const auto &record : store.records) {
		const auto address = CheckableParkedAddress(record, servedAddress);
		if (!address.isEmpty() && !ranges::contains(addresses, address)) {
			addresses.push_back(address);
		}
	}
	for (auto i = begin(_parkedChecks); i != end(_parkedChecks);) {
		if (ranges::contains(addresses, i->first)) {
			++i;
			continue;
		} else if (i->second.requestId) {
			_api.cancelRequest(i->second.requestId);
		}
		i = _parkedChecks.erase(i);
	}
	for (const auto &address : addresses) {
		_parkedChecks.emplace(address, ParkedCheck());
	}
}

void Session::requestParkedFunds(const QString &address) {
	const auto i = _parkedChecks.find(address);
	if (i == end(_parkedChecks)
		|| i->second.requestId
		|| _presence.current() != Presence::Ready) {
		return;
	}
	const auto revision = i->second.revision = ++_parkedCheckRevision;
	const auto requestId = _api.request(
		Gram::AddressInformationRequest(FormatFriendly(address, false)),
		[=](const QByteArray &json) {
			const auto funds = Gram::ParseAddressFunds(json);
			if (!funds) {
				LOG(("Wallet Error: parked balance parse failed."));
			}
			finishParkedCheck(address, revision, funds);
		},
		[=](const Gram::ApiError &error) {
			LOG(("Wallet Error: parked balance request failed: %1"
				).arg(error.message));
			finishParkedCheck(address, revision, std::nullopt);
		});
	const auto j = _parkedChecks.find(address);
	if (j != end(_parkedChecks) && j->second.revision == revision) {
		j->second.requestId = requestId;
	}
}

void Session::finishParkedCheck(
		const QString &address,
		uint64 revision,
		std::optional<Gram::AddressFunds> funds) {
	const auto i = _parkedChecks.find(address);
	if (i == end(_parkedChecks) || i->second.revision != revision) {
		return;
	}
	auto &check = i->second;
	check.requestId = 0;
	if (!funds) {
		check.funds = ParkedFunds::Unknown;
	} else if (!funds->balanceNano && funds->neverUsed) {
		check.funds = ParkedFunds::Empty;
	} else {
		check.funds = ParkedFunds::Funded;
		check.nano = funds->balanceNano;
	}
	updateDeviceCustodyState();
}

void Session::requestParkedChecks(bool refresh) {
	auto addresses = std::vector<QString>();
	for (const auto &[address, check] : _parkedChecks) {
		if ((check.funds == ParkedFunds::Checking)
			|| (refresh
				&& (check.funds == ParkedFunds::Funded
					|| check.funds == ParkedFunds::Unknown))) {
			addresses.push_back(address);
		}
	}
	for (const auto &address : addresses) {
		requestParkedFunds(address);
	}
}

void Session::dropEmptyParked() {
	if (_parkedDropping
		|| custodyBusy()
		|| _presence.current() != Presence::Ready) {
		return;
	}
	for (const auto &record : custody().records) {
		const auto address = CheckableParkedAddress(record, _address);
		const auto i = _parkedChecks.find(address);
		if (i == end(_parkedChecks) || i->second.funds != ParkedFunds::Empty) {
			continue;
		}
		const auto key = record.publicKey;
		_parkedDropping = true;
		LOG(("Wallet Info: dropping an empty unused parked wallet."));
		dropParked(key, [=] {
			_parkedDropping = false;
			dropEmptyParked();
		}, [=](const QString &error) {
			_parkedDropping = false;
			const auto j = _parkedChecks.find(address);
			if (j != end(_parkedChecks)
				&& j->second.funds == ParkedFunds::Empty) {
				j->second.funds = ParkedFunds::Unknown;
			}
			updateDeviceCustodyState();
		});
		return;
	}
}

void Session::publishParkedBalance() {
	auto sum = int64(0);
	for (const auto &[address, check] : _parkedChecks) {
		if (check.funds == ParkedFunds::Unknown) {
			_parkedBalanceNano = std::nullopt;
			return;
		} else if (check.funds == ParkedFunds::Funded) {
			sum += check.nano;
		}
	}
	_parkedBalanceNano = sum;
}

bool Session::parkedHidden(
		const CustodyRecord &record,
		const QString &servedAddress) const {
	const auto i = _parkedChecks.find(
		CheckableParkedAddress(record, servedAddress));
	return (i != end(_parkedChecks))
		&& (i->second.funds == ParkedFunds::Checking
			|| i->second.funds == ParkedFunds::Empty);
}

void Session::requestExistingWaltBalance() {
	// One opening of the wallet panel asks once. setPanel() cancels and
	// clears both of these together when the panel is dropped, so this one
	// flag is the whole "already asked this opening" state, and a failed or
	// cancelled request gets exactly one retry - on the next opening.
	if (_waltBalanceRequested) {
		return;
	}
	_waltBalanceRequested = true;
	const auto generation = _networkGeneration;
	_waltBalanceRequestId = _stateApi.request(
		MTPwallet_GetExistingWaltBalance()
	).done([=](const MTPwallet_ExistingBalance &result) {
		_waltBalanceRequestId = 0;
		if (generation == _networkGeneration) {
			const auto &data = result.data();
			_existingWaltBalanceUrl = data.is_has_balance()
				? qs(data.vurl())
				: QString();
		}
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.getExistingWaltBalance failed: %1"
			).arg(error.type()));
		_waltBalanceRequestId = 0;
	}).handleAllErrors().send();
}

void Session::applyState(const MTPWalletState &state, bool pushed) {
	LOG(("Wallet Info: applying state source=%1 previous_address=%2 "
		"previous_key=%3 previous_revision=%4; %5."
		).arg(pushed ? u"push"_q : u"response"_q
		).arg(_address
		).arg(LogKey(_publicKey)
		).arg(_walletIdentityRevision
		).arg(LogWalletState(state)));
	_stateRefreshedAt = crl::now();
	_stateFailures = 0;
	const auto clear = [&] {
		if (!_address.isEmpty() || !_publicKey.isEmpty()) {
			retireCommentScopes();
			++_walletIdentityRevision;
		}
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
			setPresence(Presence::AddressUnreadable);
			return;
		}
		const auto wasReady = (_presence.current() == Presence::Ready);
		const auto addressChanged = (_address != parsed->raw);
		const auto keyChanged = (_publicKey != data.vpublic_key().v);
		const auto identityChanged = addressChanged || keyChanged;
		if (identityChanged) {
			retireCommentScopes();
			++_walletIdentityRevision;
		}
		_address = parsed->raw;
		_publicKey = data.vpublic_key().v;
		if (wasReady && addressChanged) {
			const auto weak = base::make_weak(_engine.get());
			const auto revision = _walletIdentityRevision;
			clearHistory();
			if (!weak || revision != _walletIdentityRevision) {
				return;
			}
			clearCollectibles();
			if (!weak || revision != _walletIdentityRevision) {
				return;
			}
			_engineStatus = AccountStatus::NonExisting;
		}
		_balanceNano = int64(data.vbalance().v);
		_capabilities = WalletCapabilities{
			.backupEnabled = data.is_backup_enabled(),
			.canExportPhrase = data.is_can_export_phrase(),
			.canEnableBackup = data.is_can_enable_backup(),
		};
		setPresence(Presence::Ready);
		// A presence that was not Ready has already had its drain and its
		// first page from setPresence(); the case that write structurally
		// cannot see is a presence that stayed Ready while the served
		// address changed. That is a different wallet, so both lanes leave
		// with the transfer submission in flight, the engine status returns
		// to its unknown value and the new wallet's head page is asked for at
		// once. It runs before reconcileCustody() because that reconciliation
		// may stop and restart the engine client, and a restart must find an
		// already-drained collectibles lane rather than have its first
		// delivery wiped afterwards.
		// A same-address key change is the same wallet under a rotated key:
		// its lists and engine status stay, the history lane is marked stale
		// because the rotation is one new row in it, and reconcileCustody()
		// settles which record still signs for it. A pushed state on the
		// same wallet is the transfer notification the server sends as a
		// transfer progresses, so it too is news about the history lane
		// alone and invalidates only that one. The arms are ordered so that
		// a push which also changed the address takes the first one and gets
		// exactly one head page from the drain, never a second one from the
		// marker.
		if (wasReady && addressChanged) {
			refreshHistory();
			refreshCollectibles();
		} else if (wasReady && (pushed || keyChanged)) {
			_historyStale = true;
			refreshStaleHistory();
		}
		reconcileCustody();
		if (wasReady && identityChanged) {
			_transferWalletIdentityChanges.fire({});
		}
	}, [&](const MTPDwalletStateEmpty &data) {
		clear();
		setPresence(data.is_creating()
			? Presence::Provisioning
			: Presence::Missing);
	});
}

void Session::applyUpdate(const MTPDupdateWalletState &data) {
	applyState(data.vstate(), true);
}

void Session::applyUpdate(const MTPDupdateSentWalletTransaction &data) {
	const auto &hash = data.vmsg_hash().v;
	if (hash.isEmpty()) {
		return;
	}
	auto operationId = std::string();
	auto ambiguous = false;
	const auto match = [&](const std::string &id) {
		if (operationId.empty()) {
			operationId = id;
		} else if (operationId != id) {
			ambiguous = true;
		}
	};
	for (const auto &entry : _submitted) {
		if (entry.generation == _networkGeneration
			&& transferWalletIdentityCurrent(entry.identity)
			&& entry.receipt
			&& entry.receipt->messageHash == hash) {
			match(entry.operationId);
		}
	}
	if (_submission
		&& submissionCurrent(_submission->operationId, _submission->prepared)
		&& _submission->receipt
		&& _submission->receipt->messageHash == hash) {
		match(_submission->operationId);
	}
	if (!operationId.empty() && !ambiguous) {
		if (!applySubmittedUpdate(operationId, data)) {
			LOG(("Wallet Error: conflicting pushed transfer receipt."));
		}
	}
}

void Session::applyUpdate(const MTPDupdateWalletGaslessInfo &data) {
	const auto weak = base::make_weak(_engine.get());
	applyGaslessInfo(GaslessInfoFromServer(data), true);
	if (weak) {
		refreshGaslessInfo();
	}
}

void Session::applyUpdate(const MTPDupdateWalletTonConnectSession &data) {
	_tonConnect->apply(data.vsession());
}

void Session::applyUpdate(
		const MTPDupdateWalletTonConnectPendingDisconnect &data) {
	_tonConnect->applyPendingDisconnect(data.vsession_ids().v);
}

void Session::setPresence(Presence presence) {
	if (_presence.current() == presence) {
		return;
	}
	retireCommentScopes();
	if (_presence.current() == Presence::Ready) {
		++_walletIdentityRevision;
		clearSubmittedTransfers();
	}
	const auto weak = base::make_weak(_engine.get());
	const auto revision = _walletIdentityRevision;
	const auto current = [=] {
		return weak
			&& revision == _walletIdentityRevision
			&& _presence.current() == presence;
	};
	_presence = presence;
	if (!current()) {
		return;
	}
	// The gate is recomputed before the lanes are drained, because both
	// drains publish into the same derived faces the gate does: a face
	// evaluated between them reads emptied lists under the gate this
	// wallet held while it was Ready, which was never true of it. Doing it
	// first costs nothing — for a presence that is not Ready every term
	// in updateListsGate() that reads the history lane is conjoined with
	// `ready`, so it writes the same two values before the drain as after.
	updateListsGate();
	if (!current()) {
		return;
	}
	if (presence != Presence::Ready) {
		clearHistory();
		if (!current()) {
			return;
		}
		clearCollectibles();
		if (!current()) {
			return;
		}
	}
	updatePollingState();
	if (!current()) {
		return;
	}
	if (presence == Presence::Ready) {
		refreshHistory();
		if (current()) {
			refreshCollectibles();
		}
	}
	if (current()) {
		_transferWalletIdentityChanges.fire({});
	}
}

bool Session::revealsLocally() {
	ensureLoaded();
	const auto record = currentRecord();
	return (_presence.current() == Presence::Ready)
		&& (_publicKey.size() == kCustodyPublicKeySize)
		&& (record != nullptr)
		&& !vaultKeyUnusable()
		&& !secretUnreadable(record->recordId);
}

std::optional<BackupDisableApproval> Session::backupDisableApproval() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize
		|| vaultKeyUnusable()) {
		return std::nullopt;
	}
	const auto record = currentRecord();
	if (!record
		|| record->recordId.isEmpty()
		|| secretUnreadable(record->recordId)) {
		return std::nullopt;
	}
	return BackupDisableApproval{
		.address = _address,
		.recordId = record->recordId,
		.networkGeneration = _networkGeneration,
	};
}

VaultRuntime &Session::vault() const {
	return _engine->vault();
}

bool Session::custodyBusy() const {
	return _phraseRevealing || _replacing || _backupChanging || _rotating
		|| _custodyResetting;
}

std::shared_ptr<CommentScope> Session::createCommentScope(
		TransferItem target,
		rpl::lifetime &lifetime) {
	const auto &store = custody();
	if (!target.walletIdentity) {
		target.walletIdentity = transferWalletIdentity();
	}
	if (!target.walletIdentity
		|| !transferWalletIdentityCurrent(*target.walletIdentity)
		|| !EncryptedCommentRevealable(target)
		|| !commentAccessAvailable()
		|| custodyBusy()) {
		return nullptr;
	}
	const auto sender = CanonicalAddress(target.incoming
		? target.counterparty
		: target.walletIdentity->address);
	if (sender.isEmpty()) {
		return nullptr;
	}
	auto body = EncryptedCommentBody(target);
	if (!ValidEncryptedCommentBody(body)) {
		return nullptr;
	}
	auto state = std::make_shared<CommentScope::State>();
	state->session = this;
	state->target = std::move(target);
	state->body = std::move(body);
	state->sender = sender;
	state->vault = vault().shared_from_this();
	state->generation = _networkGeneration;
	state->epoch = vault().clearEpoch();
	const auto record = store.current(
		state->target.walletIdentity->address,
		state->target.walletIdentity->publicKey);
	if (record) {
		state->record = *record;
	}
	updateDeviceCustodyState();
	// A signing client bound to another record refuses the scope. A record
	// whose signing client is not up yet does not: decryptComment() starts
	// that client, or waits for the swap that brings it up.
	if (signingClient()
		&& (!state->record || _clientRecordId != state->record->recordId)) {
		return nullptr;
	}
	const auto scope = std::shared_ptr<CommentScope>(new CommentScope(state));
	if (!commentScopeCurrent(scope)) {
		return nullptr;
	}
	validateCommentScopes();
	_commentScopes.push_back(scope);
	lifetime.add([weak = std::weak_ptr(scope)] {
		if (const auto scope = weak.lock()) {
			scope->cancel();
		}
	});
	return scope;
}

bool Session::secretUnreadable(const QString &recordId) const {
	return !recordId.isEmpty() && (recordId == _unreadableRecordId);
}

void Session::validateUnreadableRecord() {
	if (_unreadableRecordId.isEmpty() || !_custody) {
		return;
	}
	const auto &records = _custody->records;
	const auto i = ranges::find(
		records,
		_unreadableRecordId,
		&CustodyRecord::recordId);
	if (i == end(records)) {
		_unreadableRecordId = QString();
	}
}

void Session::noteSecretReadFailure(
		SecretReadFailure failure,
		const QString &recordId) {
	// A verdict names the record whose secret was read. Without one it
	// judges nothing, and demoting the served wallet on evidence that is
	// not about it is exactly what this subject exists to prevent.
	if (recordId.isEmpty()) {
		return;
	} else if (failure == SecretReadFailure::Missing) {
		LOG(("Wallet Error: the protected secret of a held record is "
			"gone, dropping the record."));
		removeCustodyRecord(recordId);
	} else if (failure == SecretReadFailure::Unreadable
		&& _unreadableRecordId != recordId) {
		LOG(("Wallet Error: the protected secret of a held record could "
			"not be read; while that record serves this wallet the device "
			"serves read-only, until the next launch."));
		_unreadableRecordId = recordId;
		updateDeviceCustodyState();
	}
}

bool Session::commentAccessReady() const {
	return commentAccessAvailable() && !custodyBusy();
}

bool Session::commentAccessAvailable() const {
	const auto identity = transferWalletIdentity();
	return identity.has_value()
		&& _custody.has_value()
		&& !_custodyReadFailed
		&& _custody->records.size() <= 1
		&& !_custody->pendingRotation
		&& !(_clientStopping && !_clientRecordId.isEmpty())
		&& !_pending
		&& !_sendUnresolved
		&& _sendState.current() == SendState::Idle
		&& !ranges::any_of(_custody->records, [&](const CustodyRecord &record) {
			return &record != _custody->current(
				identity->address,
				identity->publicKey);
		});
}

bool Session::commentScopeCurrent(
		const std::shared_ptr<CommentScope> &scope) const {
	if (!scope || scope->cancelled()) {
		return false;
	}
	const auto &state = *scope->_state;
	if (state.session != this
		|| state.generation != _networkGeneration
		|| state.epoch != state.vault->clearEpoch()
		|| !transferWalletIdentityCurrent(*state.target.walletIdentity)
		|| !commentAccessAvailable()) {
		scope->cancel();
		return false;
	}
	const auto current = _custody->current(
		state.target.walletIdentity->address,
		state.target.walletIdentity->publicKey);
	if ((current != nullptr) != state.record.has_value()
		|| (current && (*current != *state.record
			|| !current->active
			|| current->recordId.isEmpty()
			|| current->secretRef.isEmpty()
			|| current->network != int(engine::Network::kMainnet)
			|| CanonicalAddress(current->address)
				!= state.target.walletIdentity->address))) {
		scope->cancel();
		return false;
	}
	return true;
}

void Session::validateCommentScopes() {
	const auto scopes = _commentScopes;
	for (const auto &weak : scopes) {
		if (const auto scope = weak.lock()) {
			if (commentScopeCurrent(scope)) {
				continue;
			}
		}
		_commentScopes.erase(ranges::remove_if(
			_commentScopes,
			[](const std::weak_ptr<CommentScope> &weak) {
				const auto scope = weak.lock();
				return !scope || scope->cancelled();
			}), end(_commentScopes));
	}
}

void Session::retireCommentScopes(
		const std::shared_ptr<CommentScope> &except) {
	const auto scopes = _commentScopes;
	for (const auto &weak : scopes) {
		if (const auto scope = weak.lock()) {
			if (scope != except) {
				scope->cancel();
			}
		}
	}
	validateCommentScopes();
}

void Session::decryptComment(
		KeyAuthorization auth,
		std::shared_ptr<CommentScope> scope,
		Fn<void(CommentDecryptResult)> done) {
	decryptComment({
		.auth = std::move(auth),
		.scope = std::move(scope),
		.done = std::move(done),
	});
}

void Session::decryptComment(DeferredDecrypt request) {
	using Error = CommentDecryptError;
	auto &auth = request.auth;
	const auto &scope = request.scope;
	const auto &done = request.done;
	const auto finish = [=, this](CommentDecryptResult result) {
		if (!commentScopeCurrent(scope)) {
			result = { .error = Error::Cancelled };
		}
		if (done) {
			done(std::move(result));
		}
	};
	// WHY: every refusal below is a state that settles on its own - a
	// custody operation in flight, a client still being swapped - so the
	// decryption waits for it instead of telling the user that a comment
	// which is perfectly readable cannot be read.
	const auto wait = [&] {
		if (request.attempts++ >= kDecryptBusyRetries) {
			return false;
		}
		_deferredDecrypts.push_back(std::move(request));
		_decryptRetryTimer.callOnce(kDecryptBusyRetryDelay);
		return true;
	};
	if (!commentScopeCurrent(scope)) {
		finish({ .error = Error::Cancelled });
		return;
	} else if (!scope->_state->record) {
		finish({ .error = Error::Unavailable });
		return;
	} else if (custodyBusy()) {
		if (!wait()) {
			finish({ .error = Error::Unavailable });
		}
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		finish({ .error = Error::Locked });
		return;
	}
	// The record a restore under this scope has just stored gets its signing
	// client asynchronously: the public-key-only client that served the
	// previews stops first and the signing one starts from that stop's
	// callback. The decryption waits that swap out, and only a client that
	// settled on another record, or on none, refuses it.
	const auto recordId = scope->_state->record->recordId;
	if (!_clientStopping
		&& (!_engine->client() || _clientRecordId != recordId)) {
		syncEngineClient();
	}
	if (_clientStopping) {
		_deferredDecrypts.push_back(std::move(request));
		return;
	} else if (!_engine->client() || _clientRecordId != recordId) {
		if (!wait()) {
			finish({ .error = Error::Unavailable });
		}
		return;
	}
	const auto state = scope->_state;
	const auto client = _engine->client();
	const auto body = engine::DecryptCommentRequest{
		.sender = state->sender.toStdString(),
		.body = state->body.toStdString(),
	};
	// The retry keeps its own handle of the grant: the job takes the
	// original with it and drops it when the call ends.
	auto retry = request;
	++retry.attempts;
	_engine->runLocal([
		state,
		client,
		request = body,
		grant = std::move(auth.grant)
	]() mutable {
		const auto authorization = base::take(grant);
		if (state->cancelled || state->epoch != state->vault->clearEpoch()) {
			return DecryptedComment{ .error = Error::Cancelled };
		} else if (!authorization->valid() || !state->vault->unlocked()) {
			return DecryptedComment{ .error = Error::Locked };
		}
		auto result = DecryptCommentBody(client, request);
		if (state->cancelled || state->epoch != state->vault->clearEpoch()) {
			return DecryptedComment{ .error = Error::Cancelled };
		}
		return result;
	}, [=, this](DecryptedComment result) {
		// A secret this device holds and could not read is news about the
		// key, not about this comment, so it settles after the comment has
		// been answered with whatever it could be answered with.
		const auto settle = gsl::finally([this, recordId, secret = result.secret] {
			noteSecretReadFailure(secret, recordId);
		});
		if (!commentScopeCurrent(scope) || client != _engine->client()) {
			finish({ .error = Error::Cancelled });
		} else if (result.error == Error::Busy) {
			// The engine keeps one resolution slot, which the journal
			// recovery following a client start, a transfer preparation or
			// a name lookup may hold: the decryption tries again a bounded
			// number of times before it is stated unavailable.
			if (retry.attempts <= kDecryptBusyRetries) {
				_deferredDecrypts.push_back(retry);
				_decryptRetryTimer.callOnce(kDecryptBusyRetryDelay);
			} else {
				finish({ .error = Error::Unavailable });
			}
		} else if (result.error != Error::None) {
			finish({ .error = result.error });
		} else {
			const auto text = result.text.span();
			finish({ .text = QString::fromUtf8(
				reinterpret_cast<const char*>(text.data()),
				text.size()) });
		}
	}, [=](EngineError) {
		finish({ .error = Error::Failed });
	});
}

QString Session::phraseDiagnosticState() const {
	const auto now = crl::now();
	const auto held = _custody ? _custody->forAddress(_address) : nullptr;
	const auto capabilities = _capabilities.current();
	return u"address=%1 key=%2 revision=%3 presence=%4 device_mode=%5 "
		"conflict=%6 state_age_ms=%7 engine_age_ms=%8 state_request=%9 "
		"state_failures=%10 backup_enabled=%11 can_export=%12 "
		"can_enable_backup=%13; custody_loaded=%14 custody_read_failed=%15 "
		"held_anchor=%16 held_signing=%17 held_unreadable=%18 "
		"awaiting_server_key=%19 pending_rotation=%20; "
		"busy_reveal=%21 busy_replace=%22 busy_backup=%23 busy_rotate=%24 "
		"busy_reset=%25 vault_unlocked=%26 vault_unusable=%27"_q
		.arg(_address)
		.arg(LogKey(_publicKey))
		.arg(_walletIdentityRevision)
		.arg(int(_presence.current()))
		.arg(int(_deviceCustody.current().mode))
		.arg(_deviceCustody.current().conflict)
		.arg(_stateRefreshedAt ? now - _stateRefreshedAt : -1)
		.arg(_engineRefreshedAt ? now - _engineRefreshedAt : -1)
		.arg(_stateRequestId)
		.arg(_stateFailures)
		.arg(capabilities.backupEnabled)
		.arg(capabilities.canExportPhrase)
		.arg(capabilities.canEnableBackup)
		.arg(_custody.has_value())
		.arg(_custodyReadFailed)
		.arg(LogKey(held ? held->publicKey : QByteArray()))
		.arg(LogKey(held ? held->signingKey : QByteArray()))
		.arg(held && secretUnreadable(held->recordId))
		.arg(held && held->awaitingServerKey)
		.arg(_custody && _custody->pendingRotation.has_value())
		.arg(_phraseRevealing)
		.arg(_replacing)
		.arg(_backupChanging)
		.arg(_rotating)
		.arg(_custodyResetting)
		.arg(vault().unlocked())
		.arg(vault().unusable());
}

Fn<void(const QString &)> Session::loggedPhraseFail(
		const QString &stage,
		Fn<void(const QString &)> fail) {
	const auto initial = phraseDiagnosticState();
	const auto startedAt = crl::now();
	return [=, this, fail = std::move(fail)](const QString &error) {
		LOG(("Wallet Error: %1 refused: %2 elapsed_ms=%3; initial: %4; "
			"current: %5."
			).arg(stage
			).arg(error
			).arg(crl::now() - startedAt
			).arg(initial
			).arg(phraseDiagnosticState()));
		if (fail) {
			fail(error);
		}
	};
}

void Session::revealPhrase(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &error)> fail,
		Fn<void()> authorized) {
	ensureLoaded();
	fail = loggedPhraseFail(u"phrase reveal"_q, std::move(fail));
	if (custodyBusy() || custody().pendingRotation) {
		LOG(("Wallet Error: reveal requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: reveal requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	retireCommentScopes();
	_phraseRevealing = true;
	// Every path below ends in exactly one of these two calls, which is
	// what clears the guard, so none of them is fenced by _networkGeneration:
	// a reveal owns no network-derived state, and dropping its callback
	// would either orphan a just-stored engine secret or leave the guard
	// set for the rest of the session.
	done = [this, done = std::move(done)](
			std::vector<QString> words,
			CustodyOutcome outcome) {
		_phraseRevealing = false;
		if (done) {
			done(std::move(words), outcome);
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_phraseRevealing = false;
		if (fail) {
			fail(error);
		}
	};
	const auto held = vaultKeyUnusable() ? nullptr : currentRecord();
	const auto record = (held && secretUnreadable(held->recordId))
		? nullptr
		: held;
	if (record) {
		if (!ReadAuthorized(*this, auth)) {
			fail(u"PHRASE_VAULT_LOCKED"_q);
			return;
		}
		revealLocally(std::move(auth), *record, [=](
				std::vector<QString> words) {
			done(std::move(words), CustodyOutcome::Installed);
		}, fail);
	} else {
		revealFromShares(
			std::move(auth),
			std::move(password),
			done,
			fail,
			nullptr,
			std::move(authorized));
	}
}

void Session::revealLocally(
		KeyAuthorization auth,
		const CustodyRecord &record,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail) {
	if (!ReadAuthorized(*this, auth)) {
		fail(u"PHRASE_VAULT_LOCKED"_q);
		return;
	}
	const auto initiatingRecordId = record.recordId;
	LOG(("Wallet Info: local phrase read address=%1 anchor=%2 signing=%3 "
		"network=%4 active=%5."
		).arg(record.address
		).arg(LogKey(record.publicKey)
		).arg(LogKey(record.signingKey)
		).arg(record.network
		).arg(record.active));
	const auto lifecycle = _engine->lifecycle();
	const auto descriptor = DescriptorFromRecord(record);
	_engine->runLocal([lifecycle, descriptor] {
		return lifecycle->reveal_recovery_phrase(descriptor);
	}, [=, grant = auth.grant](engine::RecoveryPhrase phrase) {
		auto words = SplitWords(QString::fromStdString(phrase.phrase));
		if (words.size() < 2) {
			LOG(("Wallet Error: local phrase reveal too short word_count=%1."
				).arg(words.size()));
			fail(u"PHRASE_EMPTY"_q);
			return;
		}
		done(std::move(words));
	}, [=, this, grant = auth.grant](EngineError error) {
		noteSecretReadFailure(
			ProtectedSecretFailure(error),
			initiatingRecordId);
		LOG(("Wallet Error: local phrase reveal failed: %1"
			).arg(LifecycleErrorDetails(error)));
		fail(IsVaultLocked(error)
			? u"PHRASE_VAULT_LOCKED"_q
			: u"PHRASE_LOCAL_FAILED"_q);
	});
}

void Session::revealFromShares(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope,
		Fn<void()> authorized) {
	if (scope && !commentScopeCurrent(scope)) {
		fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		return;
	}
	if (scope && !_capabilities.current().canExportPhrase) {
		fail(u"PHRASE_STATE_UNKNOWN"_q);
		return;
	}
	// Sent without a password first even when the account has one: the
	// server decides whether this export needs it, and answers
	// PASSWORD_MISSING when it does, which the caller turns into a repeat
	// that carries the password.
	using Flag = MTPwallet_exportSecretPhrase::Flag;
	const auto checked = password && *password;
	const auto revision = _walletIdentityRevision;
	const auto pending = std::make_shared<bool>(true);
	const auto request = _stateApi.request(MTPwallet_ExportSecretPhrase(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		checked ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=, this](
			const MTPwallet_SecretPhraseParts &result,
			mtpRequestId requestId) {
		if (!base::take(*pending)) {
			return;
		}
		if (scope) {
			scope->_state->cancelPending = nullptr;
		}
		if (scope && !commentScopeCurrent(scope)) {
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			return;
		}
		const auto &data = result.data();
		const auto dcs = ParseHolderDcs(data);
		if (!dcs) {
			LOG(("Wallet Error: wallet.exportSecretPhrase invalid holders "
				"request=%1 count=%2."
				).arg(requestId).arg(data.vdcs().v.size()));
			fail(u"PHRASE_PARTS_INVALID"_q);
			return;
		}
		if (authorized) {
			authorized();
		}
		LOG(("Wallet Info: wallet.exportSecretPhrase request=%1 named "
			"%2 holder(s); requested_revision=%3 current_revision=%4."
			).arg(requestId
			).arg(int(dcs->size())
			).arg(revision
			).arg(_walletIdentityRevision));
		fetchShareParts(
			auth,
			qs(data.vtoken()),
			*dcs,
			done,
			fail,
			scope,
			requestId);
	}).fail([=, this](const MTP::Error &error, mtpRequestId requestId) {
		if (!base::take(*pending)) {
			return;
		}
		if (scope) {
			scope->_state->cancelPending = nullptr;
		}
		if (scope && !commentScopeCurrent(scope)) {
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			return;
		}
		LOG(("Wallet Error: wallet.exportSecretPhrase failed: %1 "
			"code=%2 request=%3 password_supplied=%4 "
			"requested_revision=%5 current_revision=%6."
			).arg(error.type()
			).arg(error.code()
			).arg(requestId
			).arg(checked
			).arg(revision
			).arg(_walletIdentityRevision));
		fail((scope && MTP::IgnoreError(error))
			? u"PHRASE_SILENT_ERROR"_q
			: error.type());
	}).handleFloodErrors().send();
	LOG(("Wallet Info: wallet.exportSecretPhrase sent request=%1 "
		"address=%2 key=%3 revision=%4 state_age_ms=%5."
		).arg(request
		).arg(_address
		).arg(LogKey(_publicKey)
		).arg(revision
		).arg(_stateRefreshedAt ? (crl::now() - _stateRefreshedAt) : -1));
	if (scope) {
		scope->_state->cancelPending = crl::guard(_engine.get(), [=, this] {
			if (!base::take(*pending)) {
				return;
			}
			_stateApi.request(request).cancel();
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		});
	}
}

void Session::fetchShareParts(
		KeyAuthorization auth,
		const QString &token,
		std::vector<int> dcs,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope,
		mtpRequestId exportRequestId) {
	if (scope && !commentScopeCurrent(scope)) {
		fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		return;
	}
	auto keys = TdE2E::TemporaryKeyPair::Generate();
	if (!keys) {
		LOG(("Wallet Error: could not generate an ephemeral key "
			"export_request=%1 holders=%2."
			).arg(exportRequestId).arg(dcs.size()));
		fail(u"PHRASE_PARTS_INVALID"_q);
		return;
	}
	const auto count = int(dcs.size());
	const auto state = std::make_shared<ShareFetch>(ShareFetch{
		.keys = std::move(*keys),
		.shares = std::vector<QByteArray>(count),
		.requests = std::vector<mtpRequestId>(count),
		.sessions = std::vector<MTP::ShiftedDcId>(count),
		.dcs = dcs,
		.fail = [=](const QString &error) {
			if (scope) {
				scope->_state->cancelPending = nullptr;
			}
			fail(error);
		},
		.exportRequestId = exportRequestId,
		.startedAt = crl::now(),
		.pending = count,
	});
	const auto publicKey = state->keys.publicKey();
	for (auto i = 0; i != count; ++i) {
		state->sessions[i] = MTP::ShiftDcId(dcs[i], MTP::kWalletShareDcShift);
		state->requests[i] = _stateApi.request(
			MTPwallet_FetchEncryptedSecretPhrasePart(
				MTP_string(token),
				MTP_bytes(publicKey))
		).done([=, this](
				const MTPwallet_EncryptedSecretPhrasePart &result,
				mtpRequestId requestId) {
			if (!state->fail) {
				return;
			}
			state->requests[i] = 0;
			LOG(("Wallet Info: share fetch response export_request=%1 "
				"request=%2 index=%3 dc=%4 encrypted_bytes=%5 elapsed_ms=%6."
				).arg(exportRequestId
				).arg(requestId
				).arg(i
				).arg(dcs[i]
				).arg(result.data().vdata().v.size()
				).arg(crl::now() - state->startedAt));
			if (scope && !commentScopeCurrent(scope)) {
				FailShareFetch(
					_stateApi,
					_shareFetchTimer,
					state,
					u"PHRASE_ORIGIN_EXPIRED"_q);
				return;
			}
			if (!OpenSharePart(state, i, result.data().vdata().v)) {
				LOG(("Wallet Error: share part %1 of %2 did not open."
					).arg(i + 1).arg(count));
				FailShareFetch(
					_stateApi,
					_shareFetchTimer,
					state,
					u"PHRASE_PART_INVALID"_q);
				return;
			} else if (--state->pending) {
				return;
			}
			FinishShareFetch(_stateApi, _shareFetchTimer, state);
			const auto seed = PhraseShares::CombineShares(state->shares);
			if (!seed) {
				LOG(("Wallet Error: %1 share parts do not combine."
					).arg(state->shares.size()));
				FailShareFetch(
					_stateApi,
					_shareFetchTimer,
					state,
					u"PHRASE_PART_INVALID"_q);
				return;
			}
			if (scope) {
				scope->_state->cancelPending = nullptr;
			}
			restoreFromWords(
				auth,
				SplitWords(QString::fromUtf8(*seed)),
				done,
				base::take(state->fail),
				scope,
				exportRequestId);
		}).fail([=, this](const MTP::Error &error) {
			if (!state->fail) {
				return;
			}
			LOG(("Wallet Error: wallet.fetchEncryptedSecretPhrasePart "
				"failed: %1 code=%2 export_request=%3 request=%4 "
				"index=%5 dc=%6."
				).arg(error.type()
				).arg(error.code()
				).arg(exportRequestId
				).arg(state->requests[i]
				).arg(i
				).arg(dcs[i]));
			state->requests[i] = 0;
			FailShareFetch(
				_stateApi,
				_shareFetchTimer,
				state,
				(scope && !commentScopeCurrent(scope))
					? u"PHRASE_ORIGIN_EXPIRED"_q
					: (scope && MTP::IgnoreError(error))
					? u"PHRASE_SILENT_ERROR"_q
					: error.type());
		}).handleFloodErrors().toDC(state->sessions[i]).send();
		LOG(("Wallet Info: share fetch sent export_request=%1 request=%2 "
			"index=%3 dc=%4 total=%5."
			).arg(exportRequestId
			).arg(state->requests[i]
			).arg(i
			).arg(dcs[i]
			).arg(count));
	}
	if (scope) {
		scope->_state->cancelPending = crl::guard(_engine.get(), [=, this] {
			if (state->fail) {
				FailShareFetch(
					_stateApi,
					_shareFetchTimer,
					state,
					u"PHRASE_ORIGIN_EXPIRED"_q);
			}
		});
	}
	_shareFetch = state;
	_shareFetchTimer.setCallback([this] {
		const auto state = _shareFetch.lock();
		if (!state || !state->fail) {
			return;
		}
		LOG(("Wallet Error: share fetch timed out with %1 part(s) pending."
			).arg(state->pending));
		FailShareFetch(_stateApi, _shareFetchTimer, state, u"PHRASE_TIMEOUT"_q);
	});
	_shareFetchTimer.callOnce(kShareFetchTimeout);
}

void Session::validatePhraseIdentity(
		const std::vector<QString> &words,
		Fn<void(std::optional<PhraseIdentity>)> done) {
	auto normalized = QStringList();
	for (const auto &word : words) {
		normalized.push_back(NormalizeWord(word));
	}
	_engine->runLocal([normalized] {
		return DerivePhraseIdentity(normalized);
	}, done, [done, count = words.size()](EngineError error) {
		LOG(("Wallet Error: phrase validation worker failed word_count=%1: %2"
			).arg(count).arg(LifecycleErrorDetails(error)));
		done(std::nullopt);
	});
}

void Session::restoreFromWords(
		KeyAuthorization auth,
		std::vector<QString> words,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope,
		mtpRequestId exportRequestId) {
	if (scope && !commentScopeCurrent(scope)) {
		fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		return;
	} else if (words.size() < 2) {
		LOG(("Wallet Error: reconstructed phrase too short "
			"word_count=%1 export_request=%2."
			).arg(words.size()).arg(exportRequestId));
		fail(u"PHRASE_EMPTY"_q);
		return;
	}
	// The wallet this restore is for, named once here. A scope carries it
	// from the transaction it was opened over; every other flow takes the
	// served one, and both are held to it for the rest of the ladder, so a
	// wallet replaced from another device while a prompt is open cannot end
	// with the old phrase parked on this one.
	const auto targetIdentity = scope
		? scope->_state->target.walletIdentity
		: transferWalletIdentity();
	if (!targetIdentity) {
		LOG(("Wallet Error: restore requested with no served wallet."));
		fail(u"PHRASE_STATE_UNKNOWN"_q);
		return;
	}
	const auto lifecycle = _engine->lifecycle();
	const auto expectedKey = targetIdentity->publicKey;
	const auto targetAddress = targetIdentity->address;
	const auto heldNow = custody().forAddress(targetAddress);
	LOG(("Wallet Info: restoring %1 word(s) for %2; served key %3, "
		"held anchor %4, held signing key %5; export_request=%6 "
		"revision=%7 state_age_ms=%8."
		).arg(int(words.size())
		).arg(targetAddress
		).arg(LogKey(expectedKey)
		).arg(LogKey(heldNow ? heldNow->publicKey : QByteArray())
		).arg(LogKey(heldNow ? heldNow->signingKey : QByteArray())
		).arg(exportRequestId
		).arg(targetIdentity->revision
		).arg(_stateRefreshedAt ? (crl::now() - _stateRefreshedAt) : -1));
	const auto crossed = std::make_shared<bool>(false);
	const auto weakSession = base::make_weak(_session);
	done = [weakSession, crossed, done = std::move(done)](
			std::vector<QString> phrase,
			CustodyOutcome outcome) {
		if (weakSession) {
			weakSession->wallet().settleVaultReset(
				crossed,
				outcome == CustodyOutcome::Installed);
			if (weakSession) {
				done(std::move(phrase), outcome);
			}
		}
	};
	fail = [weakSession, crossed, fail = std::move(fail)](
			const QString &error) {
		if (weakSession) {
			weakSession->wallet().settleVaultReset(crossed, false);
			if (weakSession) {
				fail(error);
			}
		}
	};
	// WHY: a scope binds the comment attempt, never the install. Once the
	// protection was chosen the key belongs on this device, so from here on
	// only a served wallet that is no longer the target undoes the import -
	// that is the one case that would park a foreign record here. An attempt
	// that merely expired loses its reveal, and the next press finds the key
	// held instead of restoring it all over again.
	const auto targetServed = [=, this] {
		const auto current = transferWalletIdentityCurrent(*targetIdentity);
		if (!current) {
			LOG(("Wallet Error: phrase target expired export_request=%1 "
				"target_address=%2 target_key=%3 target_revision=%4; %5."
				).arg(exportRequestId
				).arg(targetAddress
				).arg(LogKey(expectedKey)
				).arg(targetIdentity->revision
				).arg(phraseDiagnosticState()));
		}
		return current;
	};
	const auto keyChangeRefusal = [=, this](const PhraseIdentity &identity) {
		return AwaitingKeyRefusal(
			custody(),
			identity,
			u"PHRASE_OUTDATED"_q,
			u"PHRASE_KEY_CHANGING"_q);
	};
	// The resolved install travels into both continuations, which is what
	// holds the grant across the worker call: the runtime cleanses the key
	// as soon as the last handle goes, and the store runs on the worker.
	const auto store = [=, this](
			PhraseIdentity identity,
			CustodyInstall install,
			std::vector<QString> phrase) {
		// A scope still carrying a record is the no-record invariant the
		// confirmed reset re-establishes before an install - except when the
		// record is exactly the one whose secret could not be read, which no
		// reset touches: that record is what this restore replaces, and
		// persistCustody() writes over its anchor rather than beside it.
		if (!targetServed()
			|| (scope
				&& scope->_state->record
				&& !secretUnreadable(scope->_state->record->recordId))) {
			LOG(("Wallet Error: phrase install eligibility changed "
				"export_request=%1 scope_has_record=%2."
				).arg(exportRequestId
				).arg(scope && scope->_state->record.has_value()));
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			return;
		} else if (!install.grant || !install.grant->valid()) {
			fail(u"PHRASE_VAULT_LOCKED"_q);
			return;
		}
		auto recoveryWords = std::vector<std::string>();
		recoveryWords.reserve(phrase.size());
		for (const auto &word : phrase) {
			recoveryWords.push_back(word.toStdString());
		}
		auto request = engine::ImportWalletRequest{
			.record_id = NewRecordId(),
			.network = engine::Network::kMainnet,
			.recovery_words = std::move(recoveryWords),
		};
		const auto stores = std::make_shared<EngineSecretStores>();
		_engine->run([
			lifecycle,
			request = std::move(request),
			stores,
			grant = install.grant,
			words = std::move(phrase)
		]() mutable -> std::optional<Restored> {
			// The grant is the vault term: a clear wipes every grant, so a
			// grant that is still valid is a vault no clear intervened on.
			// The scope's epoch is the comment attempt's term and says
			// nothing about whether this key may be stored.
			if (!grant->valid()) {
				return std::nullopt;
			}
			const auto recording = stores->record();
			auto descriptor = lifecycle->import_wallet(request);
			return Restored{ std::move(descriptor), std::move(words) };
		}, [=, this](std::optional<Restored> result) {
			if (!result) {
				LOG(("Wallet Error: phrase import grant expired before worker "
					"export_request=%1.").arg(exportRequestId));
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				return;
			}
			if (scope && !scope->cancelled()
				&& install.grant && install.grant->valid()) {
				scope->_state->epoch = vault().clearEpoch();
			}
			auto restored = std::move(*result);
			const auto descriptor = restored.descriptor;
			auto record = RecordFromDescriptor(descriptor);
			const auto rollback = [=, this](Fn<void()> finished) {
				const auto cleanup = [=, this] {
					_engine->dropStoredSecrets(*stores);
					finished();
				};
				_engine->run([lifecycle, descriptor] {
					lifecycle->delete_wallet(descriptor);
				}, cleanup, [=](EngineError error) {
					LOG(("Wallet Error: phrase import rollback failed "
						"export_request=%1: %2"
						).arg(exportRequestId).arg(LifecycleErrorDetails(error)));
					cleanup();
				});
			};
			if (record.network != int(engine::Network::kMainnet)
				|| CanonicalAddress(record.address) != targetAddress) {
				LOG(("Wallet Error: the import made %1 on network %2, "
					"not %3."
					).arg(CanonicalAddress(record.address)
					).arg(record.network
					).arg(targetAddress));
				rollback([=] { fail(u"PHRASE_OTHER_WALLET"_q); });
				return;
			} else if (!targetServed()) {
				rollback([=] { fail(u"PHRASE_ORIGIN_EXPIRED"_q); });
				return;
			}
			const auto refusal = keyChangeRefusal(identity);
			if (!refusal.isEmpty()) {
				rollback([=] { fail(refusal); });
				return;
			}
			record.signingKey = identity.signing;
			if (scope) {
				record.active = true;
				scope->_state->record = record;
			}
			// The write swaps the signing client from inside, and that swap
			// retires every scope the outgoing record served. This one is
			// what the swap was for, so it is named across the write and
			// nowhere else: every other retire still cancels everything.
			_installingScope = scope;
			const auto persisted = persistCustody(record);
			_installingScope = nullptr;
			if (!persisted) {
				LOG(("Wallet Error: phrase custody write failed "
					"export_request=%1 address=%2 anchor=%3 signing=%4."
					).arg(exportRequestId
					).arg(record.address
					).arg(LogKey(record.publicKey)
					).arg(LogKey(record.signingKey)));
				if (scope) {
					scope->_state->record = std::nullopt;
				}
				rollback([=, words = std::move(restored.words)]() mutable {
					done(std::move(words), CustodyOutcome::WriteFailed);
				});
				return;
			} else if (!targetServed()) {
				const auto stored = custody().byAnchor(record.publicKey);
				if (stored && stored->recordId == record.recordId) {
					removeCustodyRecord(record.recordId);
				}
				rollback([=] { fail(u"PHRASE_ORIGIN_EXPIRED"_q); });
				return;
			}
			done(std::move(restored.words), CustodyOutcome::Installed);
		}, [=, this](EngineError error) {
			// Only stores recorded by this import are removed. The shared
			// ring may already protect other accounts, and its latest factor
			// stays committed even when this import failed after that write.
			// A new orphan entry is swept by the next necessary ring write.
			_engine->dropStoredSecrets(*stores);
			LOG(("Wallet Error: import_wallet failed export_request=%1 "
				"address=%2 anchor=%3 signing=%4: %5"
				).arg(exportRequestId
				).arg(targetAddress
				).arg(LogKey(identity.anchor)
				).arg(LogKey(identity.signing)
				).arg(LifecycleErrorDetails(error)));
			if (!targetServed()) {
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				return;
			}
			const auto name = LifecycleErrorName(error);
			fail(IsVaultLocked(error)
				? u"PHRASE_VAULT_LOCKED"_q
				: (name == u"InvalidRecoveryPhrase"_q)
				? u"PHRASE_INVALID_PHRASE"_q
				: u"PHRASE_IMPORT_FAILED"_q);
		});
	};
	const auto continueInstall = [=, this](
			PhraseIdentity identity,
			CustodyInstall answer,
			std::vector<QString> phrase) {
		if (scope && !scope->cancelled()
			&& answer.passcodeCreatedFromEpoch
			&& scope->_state->epoch == *answer.passcodeCreatedFromEpoch
			&& answer.grant && answer.grant->valid()) {
			scope->_state->epoch = vault().clearEpoch();
		}
		if (!targetServed()) {
			fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		} else if (!answer.grant) {
			done(std::move(phrase), CustodyOutcome::Cancelled);
		} else {
			store(identity, std::move(answer), std::move(phrase));
		}
	};
	// The phrase's keys are derived on the engine worker and compared before
	// the install ladder opens anything: the signing key with the served
	// key, and the anchor with the anchor of the record this device holds
	// of the target wallet, when it holds one. An invalid phrase, an
	// obsolete phrase of this wallet or one belonging to another wallet is
	// refused here while the keyring and the custody store are still
	// untouched, so no chooser, no store and no replacement of a vault this
	// process cannot open is ever reached by such a phrase. The anchor
	// check exists because each half of a 24-word phrase is checksummed on
	// its own: a phrase whose signing half is this wallet's and whose anchor
	// half is a valid phrase of another wallet derives another address, and
	// the signing check alone would let it cross the confirmed reset and
	// fail only at the import, with the vault it replaced already gone.
	const auto verified = [=, this](
			PhraseIdentity identity,
			std::vector<QString> words) {
		const auto held = custody().forAddress(targetAddress);
		LOG(("Wallet Info: the phrase derives anchor %1, signing key %2."
			).arg(LogKey(identity.anchor), LogKey(identity.signing)));
		if (held && held->publicKey != identity.anchor) {
			LOG(("Wallet Error: that anchor is not the held anchor %1."
				).arg(LogKey(held->publicKey)));
			fail(u"PHRASE_OTHER_WALLET"_q);
			return;
		}
		const auto refusal = keyChangeRefusal(identity);
		if (!refusal.isEmpty()) {
			fail(refusal);
			return;
		} else if (identity.signing != expectedKey) {
			LOG(("Wallet Error: that signing key is not the served key %1."
				).arg(LogKey(expectedKey)));
			fail(u"PHRASE_OUTDATED"_q);
			return;
		}
		// The installer resolves live policy immediately before storing.
		// It reuses this flow's valid secured grant or the shared retention
		// window, while Open and an empty ring still require a choice.
		// Preparing that choice does not write until the host's first store.
		if (const auto install = auth.install) {
			install(resettableInstallRequest(
				expectedKey,
				scope,
				crossed,
				[=, words = std::move(words)](CustodyInstall answer) mutable {
					continueInstall(
						identity,
						std::move(answer),
						std::move(words));
				}));
		} else if (auth.grant && auth.grant->valid()) {
			store(
				identity,
				CustodyInstall{ .grant = auth.grant },
				std::move(words));
		} else {
			fail(u"PHRASE_VAULT_LOCKED"_q);
		}
	};
	validatePhraseIdentity(words, [=, this](
			std::optional<PhraseIdentity> identity) mutable {
		if (!identity) {
			LOG(("Wallet Error: no identity derives from %1 word(s)."
				).arg(int(words.size())));
			fail(u"PHRASE_INVALID_PHRASE"_q);
			return;
		}
		_engine->runLocal([=, anchor = identity->anchor] {
			return AnchorDerivesOtherAddress(lifecycle, anchor, targetAddress);
		}, [=, words = std::move(words)](bool other) mutable {
			if (other) {
				LOG(("Wallet Error: anchor %1 derives an address "
					"other than %2."
					).arg(LogKey(identity->anchor), targetAddress));
				fail(u"PHRASE_OTHER_WALLET"_q);
			} else {
				verified(*identity, std::move(words));
			}
		}, [=](EngineError error) {
			LOG(("Wallet Error: the anchor address check failed "
				"export_request=%1 address=%2 anchor=%3: %4"
				).arg(exportRequestId
				).arg(targetAddress
				).arg(LogKey(identity->anchor)
				).arg(LifecycleErrorDetails(error)));
			fail(u"PHRASE_IMPORT_FAILED"_q);
		});
	});
}

void Session::restoreFromPhrase(
		KeyAuthorization auth,
		std::vector<QString> words,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	restoreFromPhrase(
		std::move(auth),
		std::move(words),
		nullptr,
		[done = std::move(done)](KeyAuthorization) {
			if (done) {
				done();
			}
		},
		std::move(fail));
}

void Session::restoreFromPhrase(
		KeyAuthorization auth,
		std::vector<QString> words,
		std::shared_ptr<CommentScope> scope,
		Fn<void(KeyAuthorization)> done,
		Fn<void(const QString &error)> fail) {
	fail = loggedPhraseFail(u"phrase restore"_q, std::move(fail));
	if (scope) {
		// A scope over a vault this process cannot open carries that vault's
		// record, which the confirmed reset drops before the install. The
		// vault can become usable before the install runs, and then no reset
		// comes, so restoreFromWords() re-establishes the no-record invariant
		// before it stores, refusing a scope that still carries a record.
		if (!commentScopeCurrent(scope)
			|| (scope->_state->record
				&& !vaultKeyUnusable()
				&& !secretUnreadable(scope->_state->record->recordId))) {
			if (fail) {
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			}
			return;
		}
	} else {
		ensureLoaded();
	}
	if (custodyBusy()) {
		LOG(("Wallet Error: restore requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: restore requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto match = DetectPhraseMatch(words);
	if (match != PhraseMatch::Rotation) {
		if (fail) {
			fail((match == PhraseMatch::Foreign)
				? u"PHRASE_FOREIGN_PHRASE"_q
				: u"PHRASE_INVALID_PHRASE"_q);
		}
		return;
	}
	const auto installed = std::make_shared<KeyAuthorization>();
	auth = TrackCommentInstallation(std::move(auth), installed);
	retireCommentScopes(scope);
	_phraseRevealing = true;
	// A store the install ladder was cancelled out of persists nothing, and
	// this flow has no words of its own to show, so it is a failure here.
	// The guard is cleared once, by whichever wrapped callback runs, and the
	// outer fail is called directly: routing through the wrapped one would
	// clear the guard a second time. The two not-installed outcomes are told
	// apart, because a cancelled chooser states nothing while a custody write
	// that failed must be stated.
	auto refused = [this, fail, installed](const QString &error) {
		_phraseRevealing = false;
		*installed = KeyAuthorization();
		if (fail) {
			fail(error);
		}
	};
	restoreFromWords(
		std::move(auth),
		std::move(words),
		[this, scope, installed, done = std::move(done), fail = std::move(fail)](
				std::vector<QString>,
				CustodyOutcome outcome) {
			_phraseRevealing = false;
			if (scope && !commentScopeCurrent(scope)) {
				*installed = KeyAuthorization();
				if (fail) {
					fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				}
			} else if (outcome == CustodyOutcome::Installed) {
				if (done) {
					done(base::take(*installed));
				}
			} else if (fail) {
				fail((outcome == CustodyOutcome::WriteFailed)
					? u"PHRASE_INSTALL_FAILED"_q
					: u"PHRASE_INSTALL_CANCELLED"_q);
			}
		},
		std::move(refused),
		scope);
}

void Session::restoreFromBackup(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	restoreFromBackup(
		std::move(auth),
		std::move(password),
		nullptr,
		[done = std::move(done)](KeyAuthorization) {
			if (done) {
				done();
			}
		},
		std::move(fail));
}

void Session::restoreFromBackup(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		std::shared_ptr<CommentScope> scope,
		Fn<void(KeyAuthorization)> done,
		Fn<void(const QString &error)> fail) {
	fail = loggedPhraseFail(u"backup restore"_q, std::move(fail));
	if (scope) {
		// A scope over a vault this process cannot open carries that vault's
		// record, which the confirmed reset drops before the install. The
		// vault can become usable before the install runs, and then no reset
		// comes, so restoreFromWords() re-establishes the no-record invariant
		// before it stores, refusing a scope that still carries a record.
		if (!commentScopeCurrent(scope)
			|| (scope->_state->record
				&& !vaultKeyUnusable()
				&& !secretUnreadable(scope->_state->record->recordId))) {
			if (fail) {
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			}
			return;
		}
	} else {
		ensureLoaded();
	}
	if (custodyBusy()) {
		LOG(("Wallet Error: restore requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: restore requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto installed = std::make_shared<KeyAuthorization>();
	auth = TrackCommentInstallation(std::move(auth), installed);
	retireCommentScopes(scope);
	_phraseRevealing = true;
	// Same one-terminal-call shape as restoreFromPhrase: an install ladder
	// that stored nothing makes this flow fail, because a restore that stored
	// nothing restored nothing. The two not-installed outcomes are told apart,
	// because a cancelled chooser states nothing while a custody write that
	// failed must be stated.
	auto refused = [this, fail, installed](const QString &error) {
		_phraseRevealing = false;
		*installed = KeyAuthorization();
		if (fail) {
			fail(error);
		}
	};
	revealFromShares(
		std::move(auth),
		std::move(password),
		[this, scope, installed, done = std::move(done), fail = std::move(fail)](
				std::vector<QString>,
				CustodyOutcome outcome) {
			_phraseRevealing = false;
			if (scope && !commentScopeCurrent(scope)) {
				*installed = KeyAuthorization();
				if (fail) {
					fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				}
			} else if (outcome == CustodyOutcome::Installed) {
				if (done) {
					done(base::take(*installed));
				}
			} else if (fail) {
				fail((outcome == CustodyOutcome::WriteFailed)
					? u"PHRASE_INSTALL_FAILED"_q
					: u"PHRASE_INSTALL_CANCELLED"_q);
			}
		},
		std::move(refused),
		scope);
}

void Session::revealParked(
		KeyAuthorization auth,
		const QByteArray &publicKey,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = loggedPhraseFail(u"parked reveal"_q, std::move(fail));
	LOG(("Wallet Info: parked reveal requested anchor=%1.")
		.arg(LogKey(publicKey)));
	if (custodyBusy()) {
		LOG(("Wallet Error: reveal requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: reveal requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto record = custody().byAnchor(publicKey);
	if (!record || !parked(*record)) {
		LOG(("Wallet Error: parked reveal requested for a non-parked key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	if (!ReadAuthorized(*this, auth)) {
		if (fail) {
			fail(u"PHRASE_VAULT_LOCKED"_q);
		}
		return;
	}
	retireCommentScopes();
	_phraseRevealing = true;
	done = [this, done = std::move(done)](
			std::vector<QString> words,
			CustodyOutcome outcome) {
		_phraseRevealing = false;
		if (done) {
			done(std::move(words), outcome);
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_phraseRevealing = false;
		if (fail) {
			fail(error);
		}
	};
	// An unresolved record of the served wallet is parked only until its
	// phrase is read, and this reveal has to read it anyway: the identity
	// of the revealed words establishes its signing key under the same
	// latch, before the words go out, so a record that turns out to sign
	// with the served key is current by the time the box that listed it
	// rebuilds, and one that does not is settled as obsolete.
	const auto unresolved = (CanonicalAddress(record->address) == _address);
	const auto recordId = record->recordId;
	revealLocally(std::move(auth), *record, [=, this](
			std::vector<QString> words) {
		if (!unresolved) {
			done(std::move(words), CustodyOutcome::Installed);
			return;
		}
		validatePhraseIdentity(words, [=, this](
				std::optional<PhraseIdentity> identity) mutable {
			if (identity) {
				establishSigningKey(recordId, *identity);
			}
			done(std::move(words), CustodyOutcome::Installed);
		});
	}, fail);
}

void Session::dropParked(
		const QByteArray &publicKey,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = loggedPhraseFail(u"parked drop"_q, std::move(fail));
	LOG(("Wallet Info: parked drop requested anchor=%1.")
		.arg(LogKey(publicKey)));
	if (custodyBusy()) {
		LOG(("Wallet Error: drop requested while another is in flight."));
		if (fail) {
			fail(u"PHRASE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: drop requested without a settled wallet key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto record = custody().byAnchor(publicKey);
	if (!record || !parked(*record)) {
		LOG(("Wallet Error: drop requested for a non-parked key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	retireCommentScopes();
	_replacing = true;
	done = [this, done = std::move(done)] {
		_replacing = false;
		if (done) {
			done();
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_replacing = false;
		if (fail) {
			fail(error);
		}
	};
	const auto lifecycle = _engine->lifecycle();
	const auto recordId = record->recordId;
	_engine->run([lifecycle, descriptor = DescriptorFromRecord(*record)] {
		lifecycle->delete_wallet(descriptor);
	}, [=, this] {
		removeCustodyRecord(recordId);
		done();
	}, [=](EngineError error) {
		LOG(("Wallet Error: parked delete_wallet failed: %1"
			).arg(LifecycleErrorDetails(error)));
		fail(u"PHRASE_LOCAL_FAILED"_q);
	});
}

void Session::enableBackup(
		KeyAuthorization auth,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"backup enable"_q, std::move(fail));
	if (custodyBusy() || custody().pendingRotation) {
		LOG(("Wallet Error: backup requested while another is in flight."));
		if (fail) {
			fail(u"BACKUP_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup requested without a settled wallet key."));
		if (fail) {
			fail(u"BACKUP_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto matching = vaultKeyUnusable() ? nullptr : currentRecord();
	const auto proofKey = !matching
		? QByteArray()
		: matching->signingKey.isEmpty()
		? matching->publicKey
		: matching->signingKey;
	if (proofKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup requested without local custody."));
		if (fail) {
			fail(u"BACKUP_NO_CUSTODY"_q);
		}
		return;
	}
	const auto record = *matching;
	if (!ReadAuthorized(*this, auth)) {
		if (fail) {
			fail(u"BACKUP_VAULT_LOCKED"_q);
		}
		return;
	}
	const auto address = _address;
	const auto generation = _networkGeneration;
	retireCommentScopes();
	_backupChanging = true;
	done = [this, done = std::move(done)] {
		_backupChanging = false;
		if (done) {
			done();
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_backupChanging = false;
		if (fail) {
			fail(error);
		}
	};
	const auto current = [=, this] {
		const auto now = vaultKeyUnusable() ? nullptr : currentRecord();
		return (generation == _networkGeneration)
			&& (address == _address)
			&& now
			&& (now->recordId == record.recordId);
	};
	const auto proofFailed = [=](OwnershipProofError error) {
		fail(!current()
			? u"BACKUP_WALLET_CHANGED"_q
			: (error == OwnershipProofError::VaultLocked)
			? u"BACKUP_VAULT_LOCKED"_q
			: u"BACKUP_PROOF_FAILED"_q);
	};
	const auto send = [=, this](const MTPVector<MTPbytes> &parts) {
		if (!current()) {
			fail(u"BACKUP_WALLET_CHANGED"_q);
			return;
		}
		const auto proofReady = [=, this](OwnershipProof proof) {
			if (!current()) {
				fail(u"BACKUP_WALLET_CHANGED"_q);
				return;
			}
			using Flag = MTPwallet_enableBackup::Flag;
			// One-shot proof: a refused or lost answer is settled, not resent.
			_stateApi.request(MTPwallet_EnableBackup(
				MTP_flags(Flag::f_new_public_key | Flag::f_proof),
				parts,
				MTP_bytes(proofKey),
				MTP_walletOwnershipProof(
					MTP_int(proof.timestamp),
					MTP_bytes(bytes::make_span(proof.signature)))
			)).done([=, this](const MTPWalletState &result) {
				clearRotatedSinceBackup();
				applyState(result, false);
				done();
			}).fail([=, this](const MTP::Error &error) {
				LOG(("Wallet Error: wallet.enableBackup with a proof failed: %1"
					).arg(error.type()));
				settleRefusedBackupChange(
					address,
					proofKey,
					true,
					error.type(),
					done,
					[=, this](const QString &refused) {
						fail(backupEnableRefusal(
							address,
							record.recordId,
							proofKey,
							refused));
					});
			}).handleAllErrors().send();
		};
		requestOwnershipProof(
			DescriptorFromRecord(record),
			proofKey,
			auth.grant,
			proofReady,
			proofFailed);
	};
	_stateApi.request(MTPwallet_GetBackupHolderDcs(
	)).done([=, this](const MTPVector<MTPwallet_HolderDc> &result) {
		const auto keys = ParseBackupHolderKeys(result.v);
		if (!keys) {
			LOG(("Wallet Error: wallet.getBackupHolderDcs answered "
				"%1 holder(s).").arg(result.v.size()));
			fail(u"BACKUP_HOLDERS_INVALID"_q);
			return;
		}
		if (!current()) {
			fail(u"BACKUP_WALLET_CHANGED"_q);
			return;
		}
		revealLocally(auth, record, [=, this](std::vector<QString> words) {
			const auto parts = SealBackupParts(*keys, words);
			if (!parts) {
				LOG(("Wallet Error: backup parts could not be sealed."));
				fail(u"BACKUP_ENCRYPT_FAILED"_q);
				return;
			}
			auto list = QVector<MTPbytes>();
			list.reserve(int(parts->size()));
			for (const auto &part : *parts) {
				list.push_back(MTP_bytes(part));
			}
			const auto sealed = MTP_vector<MTPbytes>(std::move(list));
			validatePhraseIdentity(words, [=](
					std::optional<PhraseIdentity> identity) {
				if (!identity
					|| (identity->anchor != record.publicKey)
					|| (identity->signing != proofKey)) {
					LOG(("Wallet Error: the revealed phrase does not derive "
						"the key its proof names."));
					fail(u"BACKUP_KEY_MISMATCH"_q);
					return;
				}
				send(sealed);
			});
		}, [=](const QString &error) {
			// revealLocally is the phrase flow's helper and refuses in its
			// own family; a vault cleared between the holder-DC answer and
			// this read is this flow's refusal, so it leaves in this flow's
			// token.
			fail((error == u"PHRASE_VAULT_LOCKED"_q)
				? u"BACKUP_VAULT_LOCKED"_q
				: error);
		});
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.getBackupHolderDcs failed: %1"
			).arg(error.type()));
		fail(error.type());
	}).send();
}

QString Session::backupEnableRefusal(
		const QString &address,
		const QString &recordId,
		const QByteArray &proofKey,
		const QString &error) {
	const auto keyRefused = (error == u"WALLET_ROTATION_NOT_FOUND"_q)
		|| (error == u"WALLET_PROOF_INVALID"_q);
	if (keyRefused && (_address == address) && (_publicKey != proofKey)) {
		const auto now = custody().current(_address, _publicKey);
		if (!now || now->recordId != recordId) {
			return u"BACKUP_PHRASE_OUTDATED"_q;
		} else if (error == u"WALLET_ROTATION_NOT_FOUND"_q) {
			return u"BACKUP_KEY_UNCONFIRMED"_q;
		}
	}
	return (error == u"WALLET_PROOF_INVALID"_q)
		? u"BACKUP_NOT_VERIFIED"_q
		: error;
}

void Session::disableBackup(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"backup disable"_q, std::move(fail));
	if (custodyBusy() || custody().pendingRotation) {
		LOG(("Wallet Error: backup disable requested "
			"while another is in flight."));
		if (fail) {
			fail(u"BACKUP_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup disable requested "
			"without a settled wallet key."));
		if (fail) {
			fail(u"BACKUP_STATE_UNKNOWN"_q);
		}
		return;
	}
	if (!currentRecord()) {
		LOG(("Wallet Error: backup disable requested without local custody."));
		if (fail) {
			fail(u"BACKUP_NO_CUSTODY"_q);
		}
		return;
	}
	retireCommentScopes();
	_backupChanging = true;
	done = [this, done = std::move(done)] {
		_backupChanging = false;
		if (done) {
			done();
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_backupChanging = false;
		if (fail) {
			fail(error);
		}
	};
	using Flag = MTPwallet_disableBackup::Flag;
	const auto checked = password && *password;
	_stateApi.request(MTPwallet_DisableBackup(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		checked ? password->result : MTP_inputCheckPasswordEmpty(),
		MTP_bytes(), // new_public_key
		MTPWalletOwnershipProof() // proof
	)).done([=, this](const MTPWalletState &result) {
		clearRotatedSinceBackup();
		applyState(result, false);
		done();
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.disableBackup failed: %1"
			).arg(error.type()));
		fail(error.type());
	}).handleFloodErrors().send();
}

void Session::disableBackupWithProof(
		KeyAuthorization auth,
		BackupDisableApproval approved,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"backup disable"_q, std::move(fail));
	if (custodyBusy() || custody().pendingRotation) {
		LOG(("Wallet Error: backup disable requested "
			"while another is in flight."));
		if (fail) {
			fail(u"BACKUP_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup disable requested "
			"without a settled wallet key."));
		if (fail) {
			fail(u"BACKUP_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto record = vaultKeyUnusable() ? nullptr : currentRecord();
	const auto proofKey = !record
		? QByteArray()
		: record->signingKey.isEmpty()
		? record->publicKey
		: record->signingKey;
	if (proofKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup disable requested without local custody."));
		if (fail) {
			fail(u"BACKUP_NO_CUSTODY"_q);
		}
		return;
	}
	if (approved.networkGeneration != _networkGeneration
		|| approved.address != _address
		|| approved.recordId.isEmpty()
		|| record->recordId != approved.recordId) {
		LOG(("Wallet Error: backup disable approved "
			"for another wallet state."));
		if (fail) {
			fail(u"BACKUP_WALLET_CHANGED"_q);
		}
		return;
	}
	if (!ReadAuthorized(*this, auth)) {
		if (fail) {
			fail(u"BACKUP_VAULT_LOCKED"_q);
		}
		return;
	}
	const auto address = _address;
	const auto recordId = record->recordId;
	const auto generation = _networkGeneration;
	auto descriptor = DescriptorFromRecord(*record);
	retireCommentScopes();
	_backupChanging = true;
	done = [this, done = std::move(done)] {
		_backupChanging = false;
		if (done) {
			done();
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_backupChanging = false;
		if (fail) {
			fail(error);
		}
	};
	const auto current = [=, this] {
		const auto now = vaultKeyUnusable() ? nullptr : currentRecord();
		return (generation == _networkGeneration)
			&& (address == _address)
			&& now
			&& (now->recordId == recordId);
	};
	const auto proofReady = [=, this](OwnershipProof proof) {
		if (!current()) {
			fail(u"BACKUP_WALLET_CHANGED"_q);
			return;
		}
		using Flag = MTPwallet_disableBackup::Flag;
		// The proof is one-shot, so a negative or 500-class answer reaches
		// .fail() instead of the transport resending the identical body.
		// Every refusal is settled against the served state: a disable the
		// scanner already made, or one the server applied before its answer
		// was lost, is still the outcome the user asked for.
		_stateApi.request(MTPwallet_DisableBackup(
			MTP_flags(Flag::f_new_public_key | Flag::f_proof),
			MTP_inputCheckPasswordEmpty(), // password
			MTP_bytes(proofKey), // new_public_key
			MTP_walletOwnershipProof(
				MTP_int(proof.timestamp),
				MTP_bytes(bytes::make_span(proof.signature))) // proof
		)).done([=, this](const MTPWalletState &result) {
			clearRotatedSinceBackup();
			applyState(result, false);
			done();
		}).fail([=, this](const MTP::Error &error) {
			LOG(("Wallet Error: wallet.disableBackup with a proof failed: %1"
				).arg(error.type()));
			settleRefusedBackupChange(
				address,
				proofKey,
				false,
				error.type(),
				done,
				fail);
		}).handleAllErrors().send();
	};
	const auto proofFailed = [=](OwnershipProofError error) {
		fail(!current()
			? u"BACKUP_WALLET_CHANGED"_q
			: (error == OwnershipProofError::VaultLocked)
			? u"BACKUP_VAULT_LOCKED"_q
			: u"BACKUP_PROOF_FAILED"_q);
	};
	requestOwnershipProof(
		std::move(descriptor),
		proofKey,
		auth.grant,
		proofReady,
		proofFailed);
}

void Session::settleRefusedBackupChange(
		QString address,
		QByteArray proofKey,
		bool backupEnabled,
		QString error,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	// WHY: only a served backup in the asked state under the proof key is
	// this action's outcome; the read bypasses requestState, where another
	// settle would cancel it and leave _backupChanging set until relaunch.
	const auto startedAt = crl::now();
	const auto generation = _networkGeneration;
	_stateApi.request(MTPwallet_GetState(
	)).done([=, this](const MTPWalletState &state, mtpRequestId requestId) {
		LOG(("Wallet Info: backup settle wallet.getState request=%1 "
			"elapsed_ms=%2; %3."
			).arg(requestId
			).arg(crl::now() - startedAt
			).arg(LogWalletState(state)));
		if (generation != _networkGeneration) {
			fail(error);
			return;
		}
		applyState(state, false);
		const auto settled = (state.type() == mtpc_walletState)
			&& (state.c_walletState().is_backup_enabled() == backupEnabled)
			&& (state.c_walletState().vpublic_key().v == proofKey)
			&& (_address == address);
		if (settled) {
			clearRotatedSinceBackup();
			done();
		} else {
			fail(error);
		}
	}).fail([=](const MTP::Error &refused, mtpRequestId requestId) {
		LOG(("Wallet Error: backup settle wallet.getState request=%1 "
			"elapsed_ms=%2 failed: %3"
			).arg(requestId).arg(crl::now() - startedAt).arg(refused.type()));
		fail(error);
	}).handleAllErrors().send();
}

bool Session::rotationOffered() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		return false;
	}
	const auto matching = currentRecord();
	return (matching != nullptr)
		&& !matching->rotatedSinceBackup
		&& !custody().pendingRotation
		&& (signingClient() != nullptr)
		&& !_clientStopping;
}

void Session::quoteRotationFee(
		KeyAuthorization auth,
		Fn<void(FeeResult)> done) {
	ensureLoaded();
	if (!rotationOffered()) {
		if (done) {
			done(FeeResult{ .error = SendError::SigningUnavailable });
		}
		return;
	} else if (custodyBusy()
		|| _previewPending
		|| _sendState.current() != SendState::Idle
		|| _pending
		|| _sendUnresolved) {
		if (done) {
			done(FeeResult{ .error = SendError::AlreadySending });
		}
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		if (done) {
			done(FeeResult{ .error = SendError::Locked });
		}
		return;
	}
	retireCommentScopes();
	_rotating = true;
	const auto client = signingClient();
	// The record this client signs with, named now: a swap can rebind the
	// session's own id before the answer comes back.
	const auto signingRecordId = _clientRecordId;
	const auto generation = _networkGeneration;
	const auto finish = [=, this](FeeResult result) {
		_rotating = false;
		if (done) {
			done(result);
		}
	};
	const auto failed = [=](const QString &log) {
		LOG(("Wallet Error: key rotation quote failed: %1").arg(log));
		finish(FeeResult{ .error = SendError::Failed });
	};
	_engine->run([
		client,
		request = RotationRequest(kRotationQuoteValiditySeconds)
	] {
		auto prepared = client->prepare_key_rotation(request);
		return ThrowawayRotation{ Gram::BreakRotationSignature(
			QString::fromStdString(prepared.signed_boc)) };
	}, [=, this, grant = auth.grant](ThrowawayRotation throwaway) {
		if (throwaway.signedBoc.isEmpty()) {
			failed(u"unexpected rotation message shape"_q);
			return;
		} else if (generation != _networkGeneration) {
			failed(u"stale generation"_q);
			return;
		}
		_api.request(Gram::EmulateTraceRequest(
			throwaway.signedBoc
		), [=, this](const QByteArray &json) {
			const auto trace = Gram::ParseEmulatedTrace(json);
			if (generation != _networkGeneration) {
				failed(u"stale generation"_q);
			} else if (!trace
				|| CanonicalAddress(trace->account) != _address) {
				failed(u"unusable emulation trace"_q);
			} else {
				const auto fee = trace->feeNano;
				finish(FeeResult{
					.feeNano = fee,
					.error = (_balanceNano.current() < fee)
						? SendError::InsufficientFees
						: SendError::None,
				});
			}
		}, [=](const Gram::ApiError &error) {
			failed(u"MTP %1: %2"_q.arg(error.code).arg(error.message));
		});
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: engine prepare_key_rotation (quote) failed: %1"
			).arg(error.message));
		noteSecretReadFailure(ProtectedSecretFailure(error), signingRecordId);
		finish(FeeResult{ .error = (generation != _networkGeneration)
			? SendError::Failed
			: SendErrorFrom(error) });
	});
}

void Session::prepareRotation(
		KeyAuthorization auth,
		int64 quotedFeeNano,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"rotation prepare"_q, std::move(fail));
	if (custodyBusy()) {
		LOG(("Wallet Error: rotation requested while another is in flight."));
		if (fail) {
			fail(u"ROTATION_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: rotation requested "
			"without a settled wallet key."));
		if (fail) {
			fail(u"ROTATION_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto matching = currentRecord();
	if (!matching) {
		LOG(("Wallet Error: rotation requested without local custody."));
		if (fail) {
			fail(u"ROTATION_NO_CUSTODY"_q);
		}
		return;
	}
	if (matching->rotatedSinceBackup || custody().pendingRotation) {
		LOG(("Wallet Error: rotation requested "
			"with one already applied or pending."));
		if (fail) {
			fail(u"ROTATION_BUSY"_q);
		}
		return;
	}
	const auto client = signingClient();
	if (!client || _clientStopping) {
		LOG(("Wallet Error: rotation requested without a signing client."));
		if (fail) {
			fail(u"ROTATION_SIGNING_UNAVAILABLE"_q);
		}
		return;
	}
	if (_sendState.current() != SendState::Idle
		|| _pending
		|| _sendUnresolved) {
		LOG(("Wallet Error: rotation requested while a send is in flight."));
		if (fail) {
			fail(u"ROTATION_ALREADY_SENDING"_q);
		}
		return;
	}
	if (!ReadAuthorized(*this, auth)) {
		if (fail) {
			fail(u"ROTATION_VAULT_LOCKED"_q);
		}
		return;
	}
	retireCommentScopes();
	_rotating = true;
	const auto signingRecordId = _clientRecordId;
	fail = [this, fail = std::move(fail)](const QString &error) {
		_rotating = false;
		if (fail) {
			fail(error);
		}
	};
	_engine->run([
		client,
		request = RotationRequest(kClientSendValiditySeconds)
	] {
		return client->prepare_key_rotation(request);
	}, [=, this, grant = auth.grant](engine::PreparedKeyRotation prepared) {
		auto words = SplitWords(QString::fromStdString(
			prepared.replacement_recovery_phrase.phrase));
		auto newPublicKey = QByteArray(
			reinterpret_cast<const char*>(prepared.new_public_key.data()),
			prepared.new_public_key.size());
		if (words.size() < 2
			|| newPublicKey.size() != kCustodyPublicKeySize) {
			LOG(("Wallet Error: key rotation prepare produced "
				"no words or no key."));
			fail(u"ROTATION_PREPARE_FAILED"_q);
			return;
		}
		_preparedRotation = std::make_unique<PreparedRotation>(
			PreparedRotation{
				.words = words,
				.newPublicKey = std::move(newPublicKey),
				.signedBoc = std::move(prepared.signed_boc),
				.seqno = prepared.seqno,
				.validUntil = prepared.valid_until,
				.quotedFeeNano = quotedFeeNano,
			});
		if (done) {
			done(std::move(words));
		}
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: engine prepare_key_rotation failed: %1"
			).arg(error.message));
		noteSecretReadFailure(ProtectedSecretFailure(error), signingRecordId);
		fail(RotationErrorToken(error));
	});
}

void Session::abandonRotation() {
	if (_rotationConfirmed || custody().pendingRotation) {
		return;
	}
	_preparedRotation = nullptr;
	_rotating = false;
}

void Session::submitRotation(
		KeyAuthorization auth,
		Fn<void()> confirmed,
		Fn<void(const QString &error)> fail) {
	fail = LoggedFail(u"rotation submit"_q, std::move(fail));
	if (_rotationConfirmed) {
		LOG(("Wallet Error: rotation submitted while another is in flight."));
		if (fail) {
			fail(u"ROTATION_BUSY"_q);
		}
		return;
	}
	if (!_preparedRotation) {
		LOG(("Wallet Error: rotation submitted without a prepared one."));
		if (fail) {
			fail(u"ROTATION_NOT_PREPARED"_q);
		}
		return;
	}
	const auto refuse = [&](const QString &error) {
		abandonRotation();
		if (fail) {
			fail(error);
		}
	};
	if (uint64(base::unixtime::now()) >= _preparedRotation->validUntil) {
		LOG(("Wallet Error: rotation submitted after its validity window."));
		refuse(u"ROTATION_EXPIRED"_q);
		return;
	}
	if (_balanceNano.current() < _preparedRotation->quotedFeeNano) {
		LOG(("Wallet Error: rotation submitted with a balance below the fee."));
		refuse(u"ROTATION_FEES"_q);
		return;
	}
	const auto client = signingClient();
	if (!client || _clientStopping) {
		LOG(("Wallet Error: rotation submitted without a signing client."));
		refuse(u"ROTATION_SIGNING_UNAVAILABLE"_q);
		return;
	}
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	if (!identity || !_sendRecoveryReady) {
		refuse(u"ROTATION_STATE_UNKNOWN"_q);
		return;
	} else if (_sendState.current() != SendState::Idle
		|| _pending
		|| _sendUnresolved) {
		refuse(u"ROTATION_ALREADY_SENDING"_q);
		return;
	} else if (!persistSubmittedTransfers()) {
		refuse(u"ROTATION_STORE_FAILED"_q);
		return;
	}
	// abandonRotation() reads a set _rotationConfirmed as "a submit is in
	// flight", so the latch is armed with a callable whatever was passed.
	_rotationConfirmed = [confirmed = std::move(confirmed)] {
		if (confirmed) {
			confirmed();
		}
	};
	_rotationFailed = std::move(fail);
	storePendingRotation(std::move(auth), [=, this] {
		if (!transferOperationCurrent(*identity, generation, client)
			|| !_sendRecoveryReady) {
			discardPendingRotation();
			finishRotation(u"ROTATION_STATE_UNKNOWN"_q);
			return;
		} else if (_sendState.current() != SendState::Idle
			|| _pending
			|| _sendUnresolved) {
			discardPendingRotation();
			finishRotation(u"ROTATION_ALREADY_SENDING"_q);
			return;
		} else if (!persistSubmittedTransfers()) {
			discardPendingRotation();
			finishRotation(u"ROTATION_STORE_FAILED"_q);
			return;
		}
		const auto awaitResolution = [=, this] {
			_preparedRotation = nullptr;
			updatePollingState();
			requestEngineRefresh();
		};
		const auto &pending = *custody().pendingRotation;
		auto request = engine::SendBocRequest{
			.operation_id = pending.operationId.toStdString(),
			.force = false,
			.signed_boc = std::move(_preparedRotation->signedBoc),
			.seqno = _preparedRotation->seqno,
			.valid_until = _preparedRotation->validUntil,
		};
		_engine->run([client, request = std::move(request)] {
			return client->send_boc(request);
		}, [=, this](engine::SendResult result) {
			if (TerminalSendPhase(result.phase)) {
				applyRotationSnapshot(engine::SendSnapshot{
					.operation_id = std::move(result.operation_id),
					.phase = result.phase,
				}, false);
			} else {
				awaitResolution();
			}
		}, [=, this](EngineError error) {
			LOG(("Wallet Error: engine send_boc failed: %1"
				).arg(error.message));
			if (IsSubmissionUnknown(error)) {
				awaitResolution();
				return;
			}
			discardPendingRotation();
			finishRotation(RotationErrorToken(error));
		});
	}, [=, this](const QString &error) {
		finishRotation(error);
	});
}

std::vector<CustodyRecord> Session::parkedRecords() {
	auto result = std::vector<CustodyRecord>();
	for (const auto &record : custody().records) {
		if (parked(record) && !parkedHidden(record, _address)) {
			result.push_back(record);
		}
	}
	ranges::reverse(result);
	return result;
}

bool Session::parked(const CustodyRecord &record) const {
	return RecordParked(record, _address, _publicKey);
}

DeviceCustodyState Session::deviceCustodyState() const {
	return _deviceCustody.current();
}

auto Session::deviceCustodyStateValue() const
-> rpl::producer<DeviceCustodyState> {
	return _deviceCustody.value();
}

rpl::producer<> Session::custodyUpdates() const {
	return _custodyUpdates.events();
}

rpl::producer<> Session::keyProtectionUpdates() const {
	return vault().protectionChanges();
}

void Session::notifyKeyProtectionChanged(bool vaultKeyStillUnusable) {
	vault().notifyProtectionChanged(vaultKeyStillUnusable);
}

bool Session::vaultKeyUnusable() const {
	return vault().unusable();
}

void Session::setVaultKeyUnusable(bool unusable) {
	vault().setUnusable(unusable);
}

void Session::resetCustodyAfterForgottenPasscode(
		Fn<void(CustodyResetResult)> done) {
	const auto weak = base::make_weak(_session);
	const auto current = [weak] {
		return weak && !weak->domain().local().appLockEnabled()
			&& LiveKeyProtection() == VaultKind::Passcode;
	};
	if (custodyBusy() || !current()) {
		done(CustodyResetResult::Refused);
		return;
	}
	resetDeviceCustody(nullptr, current, [done](CustodyResetResult result) {
		DropUnusedPasscode();
		done(result);
	});
}

// The confirmation belongs to one still-current custody flow, but destroys
// the device authority. Check that flow before starting and after every
// client has stopped. A deliberate clear restamps only its surviving scope;
// any later clear, identity change or cancellation still refuses the install.
void Session::resetUnusableVault(
		const QByteArray &expected,
		const std::shared_ptr<CommentScope> &scope,
		Fn<void(CustodyResetResult)> done) {
	const auto weak = base::make_weak(_session);
	const auto current = [=] {
		if (!weak || weak->account().maybeSession() != weak.get()) {
			return false;
		}
		const auto &wallet = weak->wallet();
		return (wallet._phraseRevealing || wallet._replacing
				|| wallet._backupChanging || wallet._rotating)
			&& (scope
				? wallet.commentScopeCurrent(scope)
				: (wallet._presence.current() == Presence::Ready
					&& wallet._publicKey == expected))
			&& wallet.vault().reading().state == KeyringReading::State::Read
			&& wallet.vaultKeyUnusable();
	};
	if (!current()) {
		done(CustodyResetResult::Refused);
		return;
	}
	resetDeviceCustody(scope, current, std::move(done));
}

void Session::resetDeviceCustody(
		const std::shared_ptr<CommentScope> &scope,
		Fn<bool()> current,
		Fn<void(CustodyResetResult)> done) {
	struct State {
		std::vector<base::weak_ptr<Main::Session>> sessions;
		int pending = 0;
	};
	const auto state = std::make_shared<State>();
	const auto domain = base::make_weak(&_session->domain());
	const auto runtime = vault().shared_from_this();
	for (const auto &[index, account] : domain->accounts()) {
		if (const auto session = account->maybeSession()) {
			const auto &wallet = session->wallet();
			if (wallet._custodyResetting || wallet._clientStopping
				|| (&wallet != this && wallet.custodyBusy())) {
				done(CustodyResetResult::Refused);
				return;
			}
			state->sessions.push_back(base::make_weak(session));
		}
	}
	state->pending = int(state->sessions.size());
	for (const auto &weak : state->sessions) {
		weak->wallet()._custodyResetting = true;
	}
	runtime->clear();
	const auto epoch = runtime->clearEpoch();
	if (scope) {
		scope->_state->epoch = epoch;
	}
	const auto finish = [=] {
		if (--state->pending) {
			return;
		}
		auto unchanged = bool(domain);
		auto signedIn = 0;
		if (domain) {
			for (const auto &[index, account] : domain->accounts()) {
				if (const auto session = account->maybeSession()) {
					++signedIn;
					unchanged = unchanged && !session->wallet()._engine->client()
						&& ranges::any_of(state->sessions, [=](const auto &weak) {
							return weak.get() == session;
						});
				}
			}
		}
		auto result = CustodyResetResult::Refused;
		if (unchanged && signedIn == int(state->sessions.size())
			&& runtime->clearEpoch() == epoch && current()) {
			for (const auto &weak : state->sessions) {
				if (weak) {
					auto &wallet = weak->wallet();
					wallet._custody = CustodyStore();
					wallet._custodyReadFailed = false;
					wallet._unreadableRecordId = QString();
					wallet._preparedRotation.reset();
					wallet.retireSubmission();
					wallet._pending.reset();
					++wallet._sendRevision;
				}
			}
			if (scope) {
				scope->_state->record.reset();
			}
			result = ResetVaultAndCustody(*domain)
				? CustodyResetResult::Done
				: CustodyResetResult::Failed;
			runtime->setUnusable(false);
		}
		for (const auto &weak : state->sessions) {
			if (weak) {
				weak->wallet()._custodyResetting = false;
			}
		}
		if (result != CustodyResetResult::Refused) {
			for (const auto &weak : state->sessions) {
				if (weak) {
					weak->wallet()._sendState = SendState::Idle;
				}
			}
			runtime->notifyProtectionChanged();
		} else {
			for (const auto &weak : state->sessions) {
				if (!weak) {
					continue;
				}
				auto &wallet = weak->wallet();
				const auto interrupted = wallet._submission
					|| (wallet._sendState.current() != SendState::Idle);
				if (interrupted) {
					// WHY: the stopped client can't answer its send, so retire it
					// here; a broadcast that may have landed stays unresolved.
					const auto &submission = wallet._submission;
					if (submission && submission->rpcStarted) {
						wallet._sendUnresolved = true;
						wallet._unresolvedOperationId = submission->operationId;
					}
					wallet.retireSubmission();
					wallet._pending.reset();
					++wallet._sendRevision;
				}
				wallet.syncEngineClient();
				if (interrupted && weak) {
					auto &restarted = weak->wallet();
					const auto entry = restarted._sendUnresolved
						? restarted.submittedTransfer(
							restarted._unresolvedOperationId)
						: nullptr;
					const auto record = entry
						? restarted.submittedTransferRecord(
							entry->operationId,
							entry->identity)
						: nullptr;
					if (record
						&& record->recordId == restarted._clientRecordId) {
						entry->client = restarted.signingClient();
					}
					restarted._sendState = SendState::Idle;
				}
			}
		}
		done(result);
	};
	for (const auto &weak : state->sessions) {
		if (!weak) {
			finish();
			continue;
		}
		auto &wallet = weak->wallet();
		wallet.retireCommentScopes((&wallet == this) ? scope : nullptr);
		if (weak) {
			weak->wallet().stopEngineClientForReset(finish);
		} else {
			finish();
		}
	}
}

CustodyInstallRequest Session::resettableInstallRequest(
		QByteArray expected,
		std::shared_ptr<CommentScope> scope,
		std::shared_ptr<bool> crossed,
		Fn<void(CustodyInstall)> proceed) {
	return {
		.ready = crl::guard(_session, std::move(proceed)),
		.passcodeCreated = [scope, weak = base::make_weak(_session)](
				quint32 previousEpoch,
				quint32 epoch) {
			if (!weak || weak->account().maybeSession() != weak.get()
				|| epoch != quint32(previousEpoch + 1)
				|| weak->wallet().vault().clearEpoch() != epoch) {
				return false;
			}
			// The handoff is what the scope needs, not what the install
			// needs: an attempt that already lapsed loses only its reveal,
			// so a scope that cannot be restamped is left to die and the
			// key still lands under the protection the user just chose.
			if (scope
				&& !scope->cancelled()
				&& scope->_state->epoch == previousEpoch) {
				scope->_state->epoch = epoch;
			}
			return true;
		},
		.resetUnusableVault = [=, weak = base::make_weak(_session)](
				std::optional<quint32> createdFromEpoch,
				Fn<void(CustodyResetResult)> done) {
			if (!weak) {
				done(CustodyResetResult::Refused);
				return;
			}
			if (scope && !scope->cancelled() && createdFromEpoch
				&& scope->_state->epoch == *createdFromEpoch) {
				scope->_state->epoch = weak->wallet().vault().clearEpoch();
			}
			weak->wallet().resetUnusableVault(expected, scope, [=](
					CustodyResetResult result) {
				if (result != CustodyResetResult::Refused) {
					*crossed = true;
				}
				done(result);
			});
		},
	};
}

void Session::settleVaultReset(
		const std::shared_ptr<bool> &crossed,
		bool installed) {
	const auto runtime = vault().shared_from_this();
	if (base::take(*crossed)) {
		if (!installed) {
			updateDeviceCustodyState();
		}
		runtime->notifyProtectionChanged(runtime->unusable());
	}
	DropUnusedPasscode();
}

const CustodyStore &Session::custody() {
	if (!_custody) {
		_custody = ReadCustodyStore(_session->local());
		_custodyReadFailed = !_custody.has_value();
		if (!_custody) {
			LOG(("Wallet Error: custody store unreadable, treating as empty."));
			_custody = CustodyStore();
		} else {
			DropPreVaultCustody(_session->local(), *_custody);
		}
	}
	return *_custody;
}

const CustodyRecord *Session::currentRecord() {
	return custody().current(_address, _publicKey);
}

bool Session::persistCustody(const CustodyRecord &record) {
	auto store = custody();
	auto superseded = std::vector<CustodyRecord>();
	for (const auto &existing : store.records) {
		if (existing.publicKey == record.publicKey
			&& existing.secretRef != record.secretRef) {
			superseded.push_back(existing);
		}
	}
	store.records.erase(
		ranges::remove(
			store.records,
			record.publicKey,
			&CustodyRecord::publicKey),
		end(store.records));
	for (auto &existing : store.records) {
		existing.active = false;
	}
	store.records.push_back(record);
	store.records.back().active = true;
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: custody record write failed."));
		return false;
	}
	_custodyReadFailed = false;
	_custody = std::move(store);
	validateUnreadableRecord();
	// The superseded secrets go only after the write landed, so a failed
	// write leaves the old record and its secret exactly as before. The
	// pending rotation is not a record and its secretRef never equals a
	// record's, so it is never in this set; the new record's own secret is
	// kept out by the secretRef comparison.
	const auto lifecycle = _engine->lifecycle();
	// WHY: callers install a record signing with the served key, and never
	// while a record of its anchor awaits the server key, so what this
	// supersedes is the same phrase or an obsolete one, never the chain's key.
	for (const auto &each : superseded) {
		_engine->run([lifecycle, descriptor = DescriptorFromRecord(each)] {
			lifecycle->delete_wallet(descriptor);
		}, [] {}, [](EngineError) {
			LOG(("Wallet Error: delete_wallet of a superseded record "
				"failed."));
		});
	}
	updateDeviceCustodyState();
	return true;
}

void Session::replaceWithNew(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"wallet replace"_q, std::move(fail));
	// A replace and a reveal must never overlap: a shares-restore that
	// finished after a replace landed would write an active custody record
	// for the replaced key and show stale words. Both flows write the same
	// custody store, so each one's busy check refuses the other.
	if (custodyBusy()) {
		LOG(("Wallet Error: replace requested while another is in flight."));
		if (fail) {
			fail(u"REPLACE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: replace requested without a settled wallet key."));
		if (fail) {
			fail(u"REPLACE_STATE_UNKNOWN"_q);
		}
		return;
	}
	retireCommentScopes();
	_replacing = true;
	done = [this, done = std::move(done)](CustodyOutcome outcome) {
		_replacing = false;
		if (done) {
			done(outcome);
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_replacing = false;
		if (fail) {
			fail(error);
		}
	};
	const auto oldAddress = _address;
	sendReplaceWallet(
		MTP_inputWalletNew(),
		std::move(password),
		[=, this](const MTPWalletState &state) {
			finishConfirmedReplace(oldAddress, std::nullopt, state, done, fail);
		},
		[=](const MTP::Error &error) {
			fail(error.type());
		});
}

void Session::replaceWithImported(
		KeyAuthorization auth,
		std::vector<QString> words,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	fail = LoggedFail(u"wallet replace"_q, std::move(fail));
	if (custodyBusy()) {
		LOG(("Wallet Error: replace requested while another is in flight."));
		if (fail) {
			fail(u"REPLACE_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: replace requested without a settled wallet key."));
		if (fail) {
			fail(u"REPLACE_STATE_UNKNOWN"_q);
		}
		return;
	}
	const auto match = DetectPhraseMatch(words);
	if (match != PhraseMatch::Rotation) {
		if (fail) {
			fail((match == PhraseMatch::Foreign)
				? u"REPLACE_FOREIGN_PHRASE"_q
				: u"REPLACE_INVALID_PHRASE"_q);
		}
		return;
	}
	retireCommentScopes();
	_replacing = true;
	const auto expected = _publicKey;
	const auto crossed = std::make_shared<bool>(false);
	const auto weakSession = base::make_weak(_session);
	done = [weakSession, crossed, done = std::move(done)](CustodyOutcome outcome) {
		if (weakSession) {
			weakSession->wallet()._replacing = false;
			weakSession->wallet().settleVaultReset(
				crossed,
				outcome == CustodyOutcome::Installed);
			if (weakSession && done) {
				done(outcome);
			}
		}
	};
	fail = [weakSession, crossed, fail = std::move(fail)](
			const QString &error) {
		if (weakSession) {
			weakSession->wallet()._replacing = false;
			weakSession->wallet().settleVaultReset(crossed, false);
			if (weakSession && fail) {
				fail(error);
			}
		}
	};
	const auto oldAddress = _address;
	const auto lifecycle = _engine->lifecycle();
	const auto keyChangeRefusal = [=, this](const PhraseIdentity &identity) {
		return AwaitingKeyRefusal(
			custody(),
			identity,
			u"REPLACE_OUTDATED_PHRASE"_q,
			u"REPLACE_KEY_CHANGING"_q);
	};
	// The store authority is resolved the way restoreFromWords resolves it,
	// and for the same reason the install ladder runs first: a cancelled
	// chooser has to abort before wallet.replaceWallet is sent, so nothing
	// on the server can name a key this device never stored.
	const auto store = [=, this](
			PhraseIdentity identity,
			CustodyInstall install,
			std::vector<QString> phrase) {
		const auto refusal = keyChangeRefusal(identity);
		if (!refusal.isEmpty()) {
			fail(refusal);
			return;
		}
		auto recoveryWords = std::vector<std::string>();
		recoveryWords.reserve(phrase.size());
		for (const auto &word : phrase) {
			recoveryWords.push_back(word.toStdString());
		}
		auto request = engine::ImportWalletRequest{
			.record_id = NewRecordId(),
			.network = engine::Network::kMainnet,
			.recovery_words = std::move(recoveryWords),
		};
		const auto stores = std::make_shared<EngineSecretStores>();
		_engine->run([
			lifecycle,
			request = std::move(request),
			stores
		]() mutable {
			const auto recording = stores->record();
			return lifecycle->import_wallet(request);
		}, [=, this](engine::WalletDescriptor descriptor) {
			auto record = RecordFromDescriptor(descriptor);
			record.signingKey = identity.signing;
			const auto address = CanonicalAddress(record.address);
			const auto abandon = [=, this](const QString &error) {
				const auto cleanup = [=, this] {
					_engine->dropStoredSecrets(*stores);
					fail(error);
				};
				_engine->run([lifecycle, descriptor] {
					lifecycle->delete_wallet(descriptor);
				}, cleanup, [=](EngineError) {
					LOG(("Wallet Error: delete_wallet after an abandoned "
						"import failed."));
					cleanup();
				});
			};
			const auto applied = [=, this](const MTPWalletState &state) {
				const auto answered = (state.type() == mtpc_walletState)
					? ParseAddress(qs(state.c_walletState().vaddress()))
					: std::optional<ParsedAddress>();
				if (!answered || answered->raw != address) {
					LOG(("Wallet Error: wallet.replaceWallet answered "
						"another address."));
					abandon(u"REPLACE_KEY_MISMATCH"_q);
					return;
				}
				const auto refusal = keyChangeRefusal(identity);
				if (!refusal.isEmpty()) {
					abandon(refusal);
					return;
				}
				finishConfirmedReplace(
					oldAddress,
					record,
					state,
					done,
					fail);
			};
			const auto send = [=, this](
					TimeId timestamp,
					const std::vector<uint8_t> &signature) {
				using Flag = MTPDinputWalletImported::Flag;
				const auto rotated = (record.publicKey != record.signingKey);
				const auto anchor = rotated ? record.publicKey : QByteArray();
				sendReplaceWallet(
					MTP_inputWalletImported(
						MTP_flags(rotated
							? Flag::f_anchor_public_key
							: Flag(0)),
						MTP_bytes(record.signingKey),
						MTP_bytes(anchor), // anchor_public_key
						MTP_walletOwnershipProof(
							MTP_int(timestamp),
							MTP_bytes(bytes::make_span(signature)))),
					password,
					applied,
					[=, this](const MTP::Error &error) {
						if (!MTP::IsTemporaryError(error)
							|| MTP::IsFloodError(error)) {
							abandon(error.type());
							return;
						}
						recoverImportedReplace(
							address,
							record.signingKey,
							applied,
							abandon);
					});
			};
			// The challenge lives 300 seconds and admits one attempt, so it
			// is fetched only here - after the install ladder answered and
			// the engine stored the words - and signed at once. A cancelled
			// chooser, a refused phrase or a failed import never reaches
			// this continuation and issues no challenge.
			requestOwnershipProof(
				descriptor,
				record.signingKey,
				install.grant,
				[=](OwnershipProof proof) {
					send(proof.timestamp, proof.signature);
				},
				[=](OwnershipProofError error) {
					abandon((error == OwnershipProofError::VaultLocked)
						? u"REPLACE_VAULT_LOCKED"_q
						: u"REPLACE_PROOF_FAILED"_q);
				});
		}, [=, this](EngineError error) {
			_engine->dropStoredSecrets(*stores);
			const auto name = LifecycleErrorName(error);
			LOG(("Wallet Error: import_wallet failed: %1").arg(name));
			fail(IsVaultLocked(error)
				? u"REPLACE_VAULT_LOCKED"_q
				: (name == u"InvalidRecoveryPhrase"_q)
				? u"REPLACE_INVALID_PHRASE"_q
				: u"REPLACE_IMPORT_FAILED"_q);
		});
	};
	const auto continueInstall = [=](
			PhraseIdentity identity,
			CustodyInstall answer,
			std::vector<QString> phrase) {
		if (!answer.grant) {
			fail(u"REPLACE_INSTALL_CANCELLED"_q);
		} else {
			store(identity, std::move(answer), std::move(phrase));
		}
	};
	// The phrase's keys are derived on the engine worker before the install
	// ladder opens anything, as restoreFromWords does, so an invalid phrase
	// reaches no chooser and no store; they are deliberately not compared
	// with the served key - importing another wallet's phrase is what
	// this replace is for. The address the server's wallet.replaceWallet
	// answer serves is what confirms the wallet it accepted; the key it
	// serves classifies the record through reconciliation.
	validatePhraseIdentity(words, [=, this](
			std::optional<PhraseIdentity> identity) mutable {
		if (!identity) {
			fail(u"REPLACE_INVALID_PHRASE"_q);
			return;
		}
		const auto refusal = keyChangeRefusal(*identity);
		if (!refusal.isEmpty()) {
			fail(refusal);
			return;
		}
		if (const auto install = auth.install) {
			install(resettableInstallRequest(
				expected,
				nullptr,
				crossed,
				[=, words = std::move(words)](CustodyInstall answer) mutable {
					continueInstall(
						*identity,
						std::move(answer),
						std::move(words));
				}));
		} else if (auth.grant && auth.grant->valid()) {
			store(
				*identity,
				CustodyInstall{ .grant = auth.grant },
				std::move(words));
		} else {
			fail(u"REPLACE_VAULT_LOCKED"_q);
		}
	});
}

void Session::requestOwnershipProof(
		engine::WalletDescriptor descriptor,
		QByteArray signingKey,
		VaultAuthorization grant,
		Fn<void(OwnershipProof)> done,
		Fn<void(OwnershipProofError)> fail) {
	const auto lifecycle = _engine->lifecycle();
	// The challenge lives 300 seconds and admits one attempt, so each caller
	// asks for it only once nothing but the signature stands between it and
	// the send, and it is signed at once. A resend of this request mints a
	// new challenge server-side and only the final answer is used, so it
	// keeps the ordinary flood policy; the send that spends the proof does
	// not. The engine signs with the stored phrase's current signing key
	// and names that key back; a proof under any key but the one the
	// caller is about to send is dropped here, so a record whose signing
	// key disagrees with its phrase never reaches the server.
	_stateApi.request(MTPwallet_GetProofChallenge(
	)).done([=, this](const MTPwallet_ProofChallenge &result) {
		const auto &challenge = result.data();
		_tonConnectOwnershipDomain = qs(challenge.vdomain());
		const auto timestamp = base::unixtime::now();
		if (timestamp <= 0) {
			LOG(("Wallet Error: no usable timestamp for the ownership "
				"proof."));
			fail(OwnershipProofError::Failed);
			return;
		}
		auto request = engine::TonConnectProofSignRequest{
			.descriptor = descriptor,
			.domain = challenge.vdomain().v.toStdString(),
			.timestamp = uint64_t(timestamp),
			.payload = challenge.vpayload().v.toStdString(),
		};
		_engine->runLocal([lifecycle, request = std::move(request)] {
			return lifecycle->sign_ton_connect_proof(request);
		}, [=, grant = grant](engine::TonConnectProofSignature proof) {
			const auto size = int(proof.signature.size());
			if (size != kOwnershipProofSignatureSize) {
				LOG(("Wallet Error: the ownership proof signature has "
					"%1 bytes.").arg(size));
				fail(OwnershipProofError::Failed);
				return;
			}
			const auto signer = QByteArray(
				reinterpret_cast<const char*>(proof.public_key.data()),
				proof.public_key.size());
			if (signer != signingKey) {
				LOG(("Wallet Error: the ownership proof was signed "
					"with another key."));
				fail(OwnershipProofError::Failed);
				return;
			}
			done(OwnershipProof{
				.timestamp = timestamp,
				.signature = std::move(proof.signature),
			});
		}, [=, this, grant = grant](EngineError error) {
			LOG(("Wallet Error: ownership proof signing failed: %1"
				).arg(LifecycleErrorName(error)));
			noteSecretReadFailure(
				ProtectedSecretFailure(error),
				QString::fromStdString(descriptor.record_id));
			fail(IsVaultLocked(error)
				? OwnershipProofError::VaultLocked
				: OwnershipProofError::Failed);
		});
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.getProofChallenge failed: %1"
			).arg(error.type()));
		fail(OwnershipProofError::Failed);
	}).handleFloodErrors().send();
}

TonConnectAccess Session::tonConnectAccess() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		return TonConnectAccess::WalletNotReady;
	} else if (_rotating
		|| custody().pendingRotation
		|| custody().anyAwaitingServerKey()) {
		return TonConnectAccess::KeyChanging;
	}
	const auto record = vaultKeyUnusable() ? nullptr : currentRecord();
	if (!record || secretUnreadable(record->recordId)) {
		return TonConnectAccess::NoCurrentKey;
	}
	const auto &signingKey = record->signingKey.isEmpty()
		? record->publicKey
		: record->signingKey;
	if (signingKey != _publicKey) {
		return TonConnectAccess::KeyChanging;
	} else if (custodyBusy()) {
		return TonConnectAccess::Busy;
	}
	return TonConnectAccess::Allowed;
}

bool Session::tonConnectProofDomainAllowed(const QString &domain) const {
	return TonConnectProofDomainAllowed(domain, _tonConnectOwnershipDomain);
}

void Session::deriveTonConnectSession(
		KeyAuthorization auth,
		const QString &dappClientId,
		const QByteArray &nonce,
		Fn<void(TonConnectKey)> done,
		Fn<void(TonConnectKeyError)> fail) {
	const auto record = (tonConnectAccess() == TonConnectAccess::Allowed)
		? currentRecord()
		: nullptr;
	if (!record) {
		fail(TonConnectKeyError::Blocked);
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		fail(TonConnectKeyError::Locked);
		return;
	}
	struct Derived {
		std::shared_ptr<engine::TonConnectDerivedSession> session;
		std::string clientId;
		std::vector<uint8_t> signingKey;
	};
	const auto address = _address;
	const auto served = _publicKey;
	const auto generation = _networkGeneration;
	const auto recordId = record->recordId;
	const auto lifecycle = _engine->lifecycle();
	auto request = engine::TonConnectDerivedSessionRequest{
		.descriptor = DescriptorFromRecord(*record),
		.dapp_client_id = dappClientId.toLower().toStdString(),
		.nonce = EngineBytes(nonce),
	};
	_engine->runLocal([lifecycle, request = std::move(request)] {
		auto session = lifecycle->derive_ton_connect_session(request);
		auto clientId = session->public_key_hex();
		auto signingKey = session->signing_public_key();
		return Derived{
			.session = std::move(session),
			.clientId = std::move(clientId),
			.signingKey = std::move(signingKey),
		};
	}, [=, this, grant = auth.grant](Derived derived) {
		const auto now = currentRecord();
		auto signingKey = BytesFromEngine(derived.signingKey);
		if (generation != _networkGeneration
			|| address != _address
			|| served != _publicKey
			|| !now
			|| now->recordId != recordId
			|| signingKey != served) {
			LOG(("Wallet Error: TON Connect key derived "
				"for another wallet state."));
			fail(TonConnectKeyError::Blocked);
			return;
		}
		done(TonConnectKey{
			.session = std::move(derived.session),
			.clientId = QString::fromStdString(derived.clientId),
			.signingKey = std::move(signingKey),
		});
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: TON Connect key derivation failed: %1"
			).arg(LifecycleErrorName(error)));
		noteSecretReadFailure(ProtectedSecretFailure(error), recordId);
		fail(IsVaultLocked(error)
			? TonConnectKeyError::Locked
			: TonConnectKeyError::Failed);
	});
}

void Session::prepareTonConnectEvent(
		KeyAuthorization auth,
		TonConnectKey key,
		TonConnectEventRequest request,
		Fn<void(TonConnectReply)> done,
		Fn<void(TonConnectKeyError)> fail) {
	if (request.proofPayload
		&& !tonConnectProofDomainAllowed(request.proofDomain)) {
		LOG(("Wallet Error: TON Connect proof refused for a reserved "
			"domain."));
		fail(TonConnectKeyError::Failed);
		return;
	}
	const auto record = (tonConnectAccess() == TonConnectAccess::Allowed)
		? currentRecord()
		: nullptr;
	if (!record
		|| request.address != _address
		|| key.signingKey != _publicKey) {
		fail(TonConnectKeyError::Blocked);
		return;
	} else if (request.proofPayload && !ReadAuthorized(*this, auth)) {
		fail(TonConnectKeyError::Locked);
		return;
	}
	const auto timestamp = base::unixtime::now();
	if (!key
		|| request.eventId > uint64(std::numeric_limits<int64>::max())
		|| (request.proofPayload && timestamp <= 0)) {
		LOG(("Wallet Error: TON Connect event requested "
			"with unusable input."));
		fail(TonConnectKeyError::Failed);
		return;
	}
	struct Prepared {
		TonConnectReply reply;
		QString address;
		QByteArray publicKey;
	};
	const auto address = _address;
	const auto served = _publicKey;
	const auto generation = _networkGeneration;
	const auto recordId = record->recordId;
	const auto lifecycle = _engine->lifecycle();
	const auto session = key.session;
	const auto eventId = request.eventId;
	const auto payload = request.proofPayload
		? std::make_optional(request.proofPayload->toStdString())
		: std::nullopt;
	auto descriptor = DescriptorFromRecord(*record);
	auto domain = request.proofDomain.toStdString();
	auto challenge = EngineBytes(request.challenge);
	auto signingKey = EngineBytes(key.signingKey);
	auto device = engine::TonConnectDevice{
		.platform = Platform::IsWindows()
			? engine::TonConnectDevicePlatform::kWindows
			: Platform::IsMac()
			? engine::TonConnectDevicePlatform::kMac
			: engine::TonConnectDevicePlatform::kLinux,
		.app_name = "telegram",
		.app_version = AppVersionStr,
	};
	_engine->runLocal([
		=,
		descriptor = std::move(descriptor),
		domain = std::move(domain),
		challenge = std::move(challenge),
		signingKey = std::move(signingKey),
		device = std::move(device)
	] {
		auto account = lifecycle->ton_connect_account(descriptor);
		auto proof = std::optional<engine::TonConnectProofReply>();
		if (payload) {
			auto signature = lifecycle->sign_ton_connect_proof({
				.descriptor = descriptor,
				.domain = domain,
				.timestamp = uint64_t(timestamp),
				.payload = *payload,
			});
			account.public_key = std::move(signature.public_key);
			proof = engine::TonConnectProofReply{
				.timestamp = uint64_t(timestamp),
				.domain = domain,
				.payload = *payload,
				.signature = std::move(signature.signature),
			};
		} else {
			account.public_key = signingKey;
		}
		const auto answer = session->open_challenge(challenge);
		const auto body = session->encrypt_connect_event(
			eventId,
			account,
			std::move(proof),
			device);
		return Prepared{
			.reply = {
				.challengeAnswer = BytesFromEngine(answer),
				.body = BytesFromEngine(body),
			},
			.address = QString::fromStdString(account.address),
			.publicKey = BytesFromEngine(account.public_key),
		};
	}, [=, this, grant = auth.grant](Prepared prepared) {
		const auto now = currentRecord();
		if (generation != _networkGeneration
			|| address != _address
			|| served != _publicKey
			|| !now
			|| now->recordId != recordId) {
			LOG(("Wallet Error: TON Connect event prepared "
				"for another wallet state."));
			fail(TonConnectKeyError::Blocked);
			return;
		}
		const auto answerSize = prepared.reply.challengeAnswer.size();
		if (CanonicalAddress(prepared.address) != _address
			|| prepared.publicKey != _publicKey
			|| answerSize != kTonConnectChallengeAnswerSize) {
			LOG(("Wallet Error: TON Connect event prepared "
				"with an unexpected account."));
			fail(TonConnectKeyError::Failed);
			return;
		}
		done(std::move(prepared.reply));
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: TON Connect event preparation failed: %1"
			).arg(LifecycleErrorName(error)));
		noteSecretReadFailure(ProtectedSecretFailure(error), recordId);
		fail(IsVaultLocked(error)
			? TonConnectKeyError::Locked
			: TonConnectKeyError::Failed);
	});
}

void Session::prepareTonConnectError(
		TonConnectKey key,
		QByteArray challenge,
		uint64 eventId,
		int code,
		Fn<void(TonConnectReply)> done,
		Fn<void()> fail) {
	if (!key || eventId > uint64(std::numeric_limits<int64>::max())) {
		LOG(("Wallet Error: TON Connect rejection requested "
			"with unusable input."));
		fail();
		return;
	}
	using Code = engine::TonConnectConnectErrorCode;
	struct Reason {
		Code code = Code::kUserDeclined;
		std::string message;
	};
	constexpr auto kManifestNotFound = 2;
	constexpr auto kManifestContent = 3;
	auto reason = (code == kManifestNotFound)
		? Reason{ Code::kManifestNotFound, "Manifest not found" }
		: (code == kManifestContent)
		? Reason{ Code::kManifestContent, "Manifest content error" }
		: Reason{ Code::kUserDeclined, "User declined the connection" };
	_engine->runLocal([
		=,
		session = key.session,
		challenge = EngineBytes(challenge),
		reason = std::move(reason)
	] {
		const auto answer = session->open_challenge(challenge);
		const auto body = session->encrypt_connect_error(
			eventId,
			reason.code,
			reason.message);
		return TonConnectReply{
			.challengeAnswer = BytesFromEngine(answer),
			.body = BytesFromEngine(body),
		};
	}, [=](TonConnectReply reply) {
		if (reply.challengeAnswer.size() != kTonConnectChallengeAnswerSize) {
			LOG(("Wallet Error: TON Connect rejection prepared "
				"with an unexpected answer."));
			fail();
			return;
		}
		done(std::move(reply));
	}, [=](EngineError error) {
		LOG(("Wallet Error: TON Connect rejection preparation failed: %1"
			).arg(LifecycleErrorName(error)));
		fail();
	});
}

void Session::prepareTonConnectDisconnect(
		TonConnectKey key,
		uint64 eventId,
		Fn<void(QByteArray)> done,
		Fn<void()> fail) {
	if (!key || eventId > uint64(std::numeric_limits<int64>::max())) {
		LOG(("Wallet Error: TON Connect disconnect requested "
			"with unusable input."));
		fail();
		return;
	}
	_engine->runLocal([session = key.session, eventId] {
		return BytesFromEngine(session->encrypt_disconnect_event(eventId));
	}, [=](QByteArray body) {
		if (body.isEmpty()) {
			LOG(("Wallet Error: TON Connect disconnect prepared empty."));
			fail();
			return;
		}
		done(std::move(body));
	}, [=](EngineError error) {
		LOG(("Wallet Error: TON Connect disconnect preparation failed: %1"
			).arg(LifecycleErrorName(error)));
		fail();
	});
}

void Session::decryptTonConnectRequest(
		TonConnectKey key,
		QByteArray body,
		Fn<void(TonConnectAppRequest)> done,
		Fn<void()> fail) {
	const auto now = base::unixtime::now();
	if (!key || now <= 0) {
		LOG(("Wallet Error: TON Connect request decryption requested "
			"with unusable input."));
		fail();
		return;
	}
	_engine->runLocal([
		session = key.session,
		body = EngineBytes(body),
		now = uint64(now)
	] {
		auto derived = session->decrypt_request(body, now);
		return TonConnectAppRequestFromEngine(*session, std::move(derived));
	}, std::move(done), [=](EngineError error) {
		LOG(("Wallet Error: TON Connect request could not be decrypted: %1"
			).arg(error.message));
		fail();
	});
}

void Session::answerTonConnectChallenge(
		TonConnectKey key,
		QByteArray challenge,
		Fn<void(QByteArray)> done,
		Fn<void()> fail) {
	if (!key) {
		LOG(("Wallet Error: TON Connect challenge answer requested "
			"with unusable input."));
		fail();
		return;
	}
	_engine->runLocal([
		session = key.session,
		challenge = EngineBytes(challenge)
	] {
		return BytesFromEngine(session->open_challenge(challenge));
	}, [=](QByteArray answer) {
		if (answer.size() != kTonConnectChallengeAnswerSize) {
			LOG(("Wallet Error: TON Connect challenge answered "
				"with an unexpected size: %1.").arg(answer.size()));
			fail();
			return;
		}
		done(std::move(answer));
	}, [=](EngineError error) {
		LOG(("Wallet Error: TON Connect challenge could not be answered: %1"
			).arg(error.message));
		fail();
	});
}

void Session::encryptTonConnectResponse(
		TonConnectKey key,
		QString requestId,
		TonConnectResponse response,
		Fn<void(QByteArray)> done,
		Fn<void()> fail) {
	if (!key) {
		LOG(("Wallet Error: TON Connect response requested "
			"with unusable input."));
		fail();
		return;
	}
	using Code = engine::TonConnectRpcErrorCode;
	struct Reason {
		Code code = Code::kUnknown;
		std::string message;
	};
	auto reason = (response.error == TonConnectError::BadRequest)
		? Reason{ Code::kBadRequest, "Bad request" }
		: (response.error == TonConnectError::UserDeclined)
		? Reason{ Code::kUserDeclined, "User declined the transaction" }
		: (response.error == TonConnectError::UnknownApp)
		? Reason{ Code::kUnknownApp, "Unknown app" }
		: (response.error == TonConnectError::MethodNotSupported)
		? Reason{ Code::kMethodNotSupported, "Method not supported" }
		: Reason{ Code::kUnknown, "Transaction was not sent" };
	_engine->runLocal([
		session = key.session,
		id = requestId.toStdString(),
		boc = response.signedBoc.toStdString(),
		reason = std::move(reason),
		disconnected = response.disconnected
	] {
		return BytesFromEngine(disconnected
			? session->encrypt_disconnect_success(id)
			: boc.empty()
			? session->encrypt_error(id, reason.code, reason.message)
			: session->encrypt_send_success(id, boc));
	}, [=](QByteArray body) {
		if (body.isEmpty()) {
			LOG(("Wallet Error: TON Connect response encrypted "
				"to an empty body."));
			fail();
			return;
		}
		done(std::move(body));
	}, [=](EngineError error) {
		LOG(("Wallet Error: TON Connect response could not be encrypted: %1"
			).arg(error.message));
		fail();
	});
}

void Session::signTonConnectData(
		KeyAuthorization auth,
		TonConnectKey key,
		QString requestId,
		std::shared_ptr<const TonConnectSignData> data,
		QString domain,
		Fn<void(QByteArray)> done,
		Fn<void(TonConnectKeyError)> fail) {
	const auto record = (tonConnectAccess() == TonConnectAccess::Allowed)
		? currentRecord()
		: nullptr;
	if (!record || !key || key.signingKey != _publicKey) {
		fail(TonConnectKeyError::Blocked);
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		fail(TonConnectKeyError::Locked);
		return;
	}
	const auto timestamp = base::unixtime::now();
	if (!data
		|| !data->request
		|| domain.isEmpty()
		|| !TonConnectRequestIdValid(requestId)
		|| timestamp <= 0) {
		LOG(("Wallet Error: TON Connect data signing requested "
			"with unusable input."));
		fail(TonConnectKeyError::Failed);
		return;
	}
	struct Signed {
		QByteArray body;
		QByteArray publicKey;
	};
	const auto address = _address;
	const auto served = _publicKey;
	const auto generation = _networkGeneration;
	const auto recordId = record->recordId;
	const auto lifecycle = _engine->lifecycle();
	const auto session = key.session;
	auto request = engine::TonConnectSignDataSignRequest{
		.descriptor = DescriptorFromRecord(*record),
		.request = *data->request,
		.domain = domain.toStdString(),
		.timestamp = uint64_t(timestamp),
	};
	_engine->runLocal([
		=,
		request = std::move(request),
		id = requestId.toStdString()
	] {
		const auto result = lifecycle->sign_ton_connect_data(request);
		return Signed{
			.body = BytesFromEngine(
				session->encrypt_sign_data_success(id, result)),
			.publicKey = BytesFromEngine(result.public_key),
		};
	}, [=, this, grant = auth.grant](Signed result) {
		const auto now = currentRecord();
		if (generation != _networkGeneration
			|| address != _address
			|| served != _publicKey
			|| !now
			|| now->recordId != recordId) {
			LOG(("Wallet Error: TON Connect data signed "
				"for another wallet state."));
			fail(TonConnectKeyError::Blocked);
			return;
		} else if (result.publicKey != served || result.body.isEmpty()) {
			LOG(("Wallet Error: TON Connect data signed "
				"with an unexpected key."));
			fail(TonConnectKeyError::Failed);
			return;
		}
		done(std::move(result.body));
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: TON Connect data signing failed: %1"
			).arg(LifecycleErrorName(error)));
		noteSecretReadFailure(ProtectedSecretFailure(error), recordId);
		fail(IsVaultLocked(error)
			? TonConnectKeyError::Locked
			: TonConnectKeyError::Failed);
	});
}

void Session::sendReplaceWallet(
		const MTPInputWalletReplacement &wallet,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(const MTPWalletState &)> applied,
		Fn<void(const MTP::Error &)> fail) {
	using Flag = MTPwallet_replaceWallet::Flag;
	const auto checked = password && *password;
	auto request = _stateApi.request(MTPwallet_ReplaceWallet(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		wallet,
		checked ? password->result : MTP_inputCheckPasswordEmpty()));
	// An imported replacement carries a one-shot ownership proof: the
	// server admits one attempt per challenge, and the MTP instance's
	// automatic resend of a request answered with a negative or 500-class
	// code repeats the identical body, so a resent proof could only be
	// refused as spent or spend the challenge behind the flow's back. Such
	// an answer therefore reaches .fail() here and the import checks the
	// served state with a read, without repeating the proof. A new wallet
	// carries nothing one-shot and keeps the transport's resend.
	auto &policy = (wallet.type() == mtpc_inputWalletImported)
		? request.handleAllErrors()
		: request.handleFloodErrors();
	policy.done([=](const MTPWalletState &result) {
		applied(result);
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.replaceWallet failed: %1"
			).arg(error.type()));
		fail(error);
	}).send();
}

void Session::recoverImportedReplace(
		QString canonicalAddress,
		QByteArray signingKey,
		Fn<void(const MTPWalletState &)> applied,
		Fn<void(const QString &)> abandon) {
	const auto unconfirmed = [=] {
		abandon(u"REPLACE_STATE_UNCONFIRMED"_q);
	};
	requestState([=, this](const MTPWalletState &state) {
		if (state.type() != mtpc_walletState) {
			unconfirmed();
			return;
		}
		const auto &data = state.c_walletState();
		const auto parsed = ParseAddress(qs(data.vaddress()));
		if (data.vpublic_key().v.size() != kCustodyPublicKeySize || !parsed) {
			unconfirmed();
			return;
		}
		if (parsed->raw != canonicalAddress) {
			applyState(state, false);
			abandon(u"REPLACE_KEY_MISMATCH"_q);
			return;
		} else if (data.vpublic_key().v != signingKey) {
			LOG(("Wallet Error: the recovered wallet state does not serve "
				"the imported signing key %1.").arg(LogKey(signingKey)));
			applyState(state, false);
			abandon(u"REPLACE_OUTDATED_PHRASE"_q);
			return;
		}
		applied(state);
	}, unconfirmed);
}

void Session::finishConfirmedReplace(
		QString oldAddress,
		std::optional<CustodyRecord> newActive,
		const MTPWalletState &state,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &)> fail) {
	applyState(state, false);
	const auto lifecycle = _engine->lifecycle();
	if (newActive) {
		if (!persistCustody(*newActive)) {
			_engine->run([
				lifecycle,
				descriptor = DescriptorFromRecord(*newActive)
			] {
				lifecycle->delete_wallet(descriptor);
			}, [] {}, [](EngineError) {});
			// wallet.replaceWallet has already succeeded and applyState() has
			// already run, so the replacement is real and cannot be taken
			// back: this answers done, not fail. The delete_wallet above is
			// local cleanup of a secret nothing points at, not a retraction,
			// and only the custody install on this device did not happen.
			done(CustodyOutcome::WriteFailed);
			return;
		}
	}
	if (oldAddress != _address) {
		const auto records = custody().records;
		for (const auto &record : records) {
			if (CanonicalAddress(record.address) != oldAddress) {
				continue;
			}
			removeCustodyRecord(record.recordId);
			_engine->run([
				lifecycle,
				descriptor = DescriptorFromRecord(record)
			] {
				lifecycle->delete_wallet(descriptor);
			}, [] {}, [](EngineError) {
				LOG(("Wallet Error: delete_wallet of the replaced wallet "
					"failed."));
			});
		}
	}
	done(CustodyOutcome::Installed);
}

void Session::reconcileCustody() {
	if (_publicKey.size() != kCustodyPublicKeySize) {
		return;
	}
	// The served key is this device's own replacement key, so the chain
	// confirmed the rotation before the journal did and the promotion runs
	// here. The in-flight send_boc, if any, precedes the queued client stop
	// on the serial worker, and its later verdict finds no pending and
	// returns. A third served key never matches: it leaves the pending to
	// the journal, or to a restore of the current phrase whose client's
	// empty journal then discards it.
	const auto &pending = custody().pendingRotation;
	const auto confirmedByServer = pending
		&& !pending->newPublicKey.isEmpty()
		&& pending->newPublicKey == _publicKey
		&& custody().forAddress(_address) != nullptr;
	if (confirmedByServer) {
		promotePendingRotation();
	}
	auto store = custody();
	const auto previousServed = store.lastSeenServerKey;
	auto changed = false;
	for (auto &record : store.records) {
		const auto sameWallet = (CanonicalAddress(record.address) == _address);
		if (sameWallet
			&& record.signingKey.isEmpty()
			&& record.publicKey == _publicKey) {
			record.signingKey = record.publicKey;
			changed = true;
		}
		if (record.awaitingServerKey
			&& (!sameWallet
				|| record.signingKey == _publicKey
				|| _publicKey != previousServed)) {
			record.awaitingServerKey = false;
			changed = true;
		}
		const auto active = sameWallet && record.signsWith(_publicKey);
		if (record.active != active) {
			record.active = active;
			changed = true;
		}
	}
	if (store.lastSeenServerKey != _publicKey) {
		if (!store.lastSeenServerKey.isEmpty()) {
			LOG(("Wallet Info: server wallet key changed."));
		}
		store.lastSeenServerKey = _publicKey;
		changed = true;
	}
	if (changed) {
		if (WriteCustodyStore(_session->local(), store)) {
			_custody = std::move(store);
		} else {
			LOG(("Wallet Error: custody parking write failed."));
		}
	}
	updateDeviceCustodyState();
	if (custody().pendingRotation || custody().anyAwaitingServerKey()) {
		updatePollingState();
	}
	if (confirmedByServer && !custody().pendingRotation) {
		finishRotation(QString());
	}
}

void Session::updateDeviceCustodyState(bool cachedOnly) {
	if (cachedOnly && !_custody) {
		return;
	}
	const auto &store = cachedOnly ? *_custody : custody();
	const auto identity = transferWalletIdentity();
	if (!identity) {
		return;
	}
	syncParkedChecks(store, identity->address);
	publishParkedBalance();
	const auto conflict = ranges::any_of(
		store.records,
		[&](const CustodyRecord &record) {
			return RecordParked(
				record,
				identity->address,
				identity->publicKey)
				&& !parkedHidden(record, identity->address);
		});
	const auto current = store.current(
		identity->address,
		identity->publicKey);
	const auto mode = (current
		&& !vaultKeyUnusable()
		&& !secretUnreadable(current->recordId))
		? DeviceMode::Full
		: _capabilities.current().canExportPhrase
		? DeviceMode::ReadOnlyRestorable
		: DeviceMode::ReadOnlyNotRestorable;
	const auto state = DeviceCustodyState{
		.mode = mode,
		.conflict = conflict,
	};
	if (cachedOnly && _deviceCustody.current() == state) {
		return;
	}
	_deviceCustody = state;
	_custodyUpdates.fire({});
	if (!cachedOnly) {
		syncEngineClient();
	}
	requestParkedChecks(false);
	dropEmptyParked();
}

void Session::syncEngineClient() {
	const auto identity = transferWalletIdentity();
	const auto wanted = identity
		? custody().current(identity->address, identity->publicKey)
		: nullptr;
	// Without a record that signs for the served wallet the client runs
	// public-key-only: fees are still emulated and state still read from
	// the address and key alone, while send() and every secret-reading path
	// stay refused until a restore or import lands a record and this swap
	// runs again to replace it with the signing client.
	const auto previewOnly = !wanted
		&& identity
		&& (_presence.current() == Presence::Ready);
	const auto previewMatches = previewOnly
		&& _clientRecordId.isEmpty()
		&& _clientPreviewIdentity
		&& (_clientPreviewIdentity->address == identity->address)
		&& (_clientPreviewIdentity->publicKey == identity->publicKey);
	auto started = false;
	if (_clientStopping || _custodyResetting) {
		return;
	} else if (_engine->client()) {
		const auto matches = wanted
			? (_clientRecordId == wanted->recordId)
			: previewMatches;
		if (!matches) {
			if (_submission && submissionCurrent(
					_submission->operationId,
					_submission->prepared)) {
				retirePreviews(SendError::SigningUnavailable);
				return;
			}
			_sendRecoveryReady = false;
			_clientStopping = true;
			updateSigningReady();
			// WHY: the record goes before the retire, not after it and not
			// in the stop's callback. From here nothing may sign with this
			// client, and the retire validates every scope it keeps - so a
			// record still named here would make comment access read as
			// gone and cancel the very scope the exemption below spares.
			const auto bound = !base::take(_clientRecordId).isEmpty();
			// A scope opened under the public-key-only client holds no
			// record yet, and the record it restores is what this swap
			// binds: it waits for the signing client instead of dying with
			// the client that could not have served it anyway. The install
			// running right now is the same case one step later - its scope
			// held the record this install just replaced - so it is the one
			// scope a record-bound swap keeps.
			if (bound) {
				retireCommentScopes(_installingScope);
			}
			_engine->stopClient([this] {
				_clientStopping = false;
				_clientRecordId = QString();
				_clientPreviewIdentity.reset();
				syncEngineClient();
			});
			retirePreviews(SendError::SigningUnavailable);
			return;
		}
	} else if (!wanted && !previewOnly) {
		settleDeferredDecrypts();
		return;
	} else {
		try {
			_engine->startClient(wanted
				? ClientConfigFromRecord(*wanted)
				: ClientConfigForPreview(*identity));
			_clientRecordId = wanted ? wanted->recordId : QString();
			_clientPreviewIdentity = wanted
				? std::optional<TransferWalletIdentity>()
				: identity;
			_sendRecoveryReady = false;
			updateSigningReady();
			started = true;
		} catch (...) {
			LOG(("Wallet Error: engine client start refused: %1"
				).arg(ClientErrorName(std::current_exception())));
			settleDeferredDecrypts();
			return;
		}
	}
	settleDeferredDecrypts();
	const auto weak = base::make_weak(_engine.get());
	const auto generation = _networkGeneration;
	const auto client = _engine->client();
	const auto current = [=] {
		return weak && identity && transferOperationCurrent(
			*identity,
			generation,
			client);
	};
	restoreSubmittedTransfers();
	if (!current()) {
		return;
	}
	if (sendRecoveryNeeded()) {
		resolvePending();
		updatePollingState();
	}
	if (current() && started && _presence.current() == Presence::Ready) {
		requestEngineRefresh();
	}
}

// Completion outlives a session which logs out during the stop. Engine
// shutdown then finishes in its destructor, and the last callback owner
// posts completion to main after that destructor has returned. This keeps
// the domain reset moving without allowing deletion ahead of a live client.
void Session::stopEngineClientForReset(Fn<void()> done) {
	const auto completion = std::make_shared<ResetClientCompletion>();
	completion->done = std::move(done);
	const auto weak = base::make_weak(_session);
	const auto finish = [=] {
		if (weak) {
			weak->wallet()._clientStopping = false;
			weak->wallet()._clientRecordId = QString();
			weak->wallet()._clientPreviewIdentity.reset();
			weak->wallet().settleDeferredDecrypts();
		}
		if (auto done = base::take(completion->done)) {
			done();
		}
	};
	_sendRecoveryReady = false;
	_clientStopping = true;
	updateSigningReady();
	retirePreviews(SendError::SigningUnavailable);
	if (weak) {
		_engine->stopClient(finish);
	}
}

void Session::removeCustodyRecord(const QString &recordId) {
	auto store = custody();
	store.records.erase(
		ranges::remove(
			store.records,
			recordId,
			&CustodyRecord::recordId),
		end(store.records));
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: custody record removal write failed."));
		return;
	}
	_custody = std::move(store);
	validateUnreadableRecord();
	updateDeviceCustodyState();
}

// The one write that establishes a pre-v4 record's signing key from its
// own phrase. The anchor the words derive must be the record's: the engine
// derived the record's address from that anchor at the import, so words
// that derive another anchor are not this record's phrase, and the record
// stays unresolved rather than being settled by a foreign identity.
void Session::establishSigningKey(
		const QString &recordId,
		const PhraseIdentity &identity) {
	auto store = custody();
	const auto i = ranges::find(
		store.records,
		recordId,
		&CustodyRecord::recordId);
	if (i == end(store.records) || !i->signingKey.isEmpty()) {
		return;
	} else if (i->publicKey != identity.anchor) {
		LOG(("Wallet Error: revealed phrase does not derive its record's "
			"anchor, the record stays unresolved."));
		return;
	}
	i->signingKey = identity.signing;
	i->awaitingServerKey = false;
	i->active = (CanonicalAddress(i->address) == _address)
		&& i->signsWith(_publicKey);
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: signing key establishment write failed."));
		return;
	}
	_custody = std::move(store);
	updateDeviceCustodyState();
}

void Session::clearNetworkState() {
	retireCommentScopes();
	++_networkGeneration;
	++_walletIdentityRevision;
	const auto weak = base::make_weak(_engine.get());
	const auto generation = _networkGeneration;
	const auto revision = _walletIdentityRevision;
	const auto current = [=] {
		return weak
			&& generation == _networkGeneration
			&& revision == _walletIdentityRevision;
	};
	_engineStatus = AccountStatus::NonExisting;
	_stateApi.request(base::take(_stateRequestId)).cancel();
	_stateRequestedAt = 0;
	_stateRefreshedAt = 0;
	_engineRefreshedAt = 0;
	_stateFailures = 0;
	if (const auto request = base::take(_collectiblesRequest)) {
		_stateApi.request(request->id).cancel();
	}
	_pollingCount = 0;
	_pollTimer.cancel();
	_stream->stop();
	clearHistory();
	if (!current()) {
		return;
	}
	_balanceNano = 0;
	resetGaslessInfo();
	if (!current()) {
		return;
	}
	clearCollectibles();
	if (!current()) {
		return;
	}
	updateListsGate();
	if (!current()) {
		return;
	}
	_transferWalletIdentityChanges.fire({});
	if (!current()) {
		return;
	}
	retirePreviews(SendError::Failed);
}

void Session::requestEngineRefresh() {
	const auto identity = transferWalletIdentity();
	const auto client = _engine->client();
	const auto generation = _networkGeneration;
	if (_engineRefreshPending
		|| !identity
		|| !transferOperationCurrent(*identity, generation, client)) {
		return;
	}
	_engineRefreshPending = true;
	const auto sendRevision = _sendRevision;
	_engine->run([client] {
		return client->refresh();
	}, [=, this](engine::WalletUpdate update) {
		_engineRefreshPending = false;
		if (!transferOperationCurrent(*identity, generation, client)) {
			requestEngineRefresh();
			return;
		}
		applyEngineUpdate(update, sendRevision);
	}, [=, this](EngineError error) {
		_engineRefreshPending = false;
		if (!transferOperationCurrent(*identity, generation, client)) {
			requestEngineRefresh();
			return;
		}
		LOG(("Wallet Error: engine refresh failed: %1, "
			"keeping last-good state.").arg(error.message));
	});
}

void Session::applyEngineUpdate(
		const engine::WalletUpdate &update,
		uint64 sendRevision) {
	if (update.outcome != engine::WalletOperationOutcome::kCompleted) {
		LOG(("Wallet: engine refresh outcome %1, keeping last-good state."
			).arg(int(update.outcome)));
		return;
	}
	const auto weak = base::make_weak(_engine.get());
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	const auto client = _engine->client();
	const auto current = [=] {
		return weak && identity && transferOperationCurrent(
			*identity,
			generation,
			client);
	};
	if (signingClient()) {
		applySendSnapshot(update.snapshot.send, false, sendRevision);
		if (!current()) {
			return;
		}
		applyRotationSnapshot(update.snapshot.send, false);
		if (!current()) {
			return;
		}
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
	_engineStatus = mapped;
	_engineRefreshedAt = crl::now();
	_balanceNano = balance;
}

void Session::refreshHistory(Fn<void()> done) {
	ensureLoaded();
	if ((_presence.current() != Presence::Ready) || _historyPaged) {
		if (done) {
			done();
		}
		return;
	}
	requestTransactions(false, std::move(done));
}

void Session::refreshStaleHistory() {
	// _historyPaged is deliberately not consulted — a push is the server
	// stating that this wallet's history moved, which a poll and a stream
	// hint are not — and the applied head page clears that term itself.
	if (!_historyStale
		|| (_presence.current() != Presence::Ready)
		|| !pollingRequested()
		|| collectiblesTab()
		|| _historyRequest) {
		return;
	}
	requestTransactions(false);
}

void Session::applyTransferMinNanos() {
	const auto now = TransferMinNanos(_session);
	if (_transferMinNanos == now) {
		return;
	}
	_transferMinNanos = now;
	// The canonical list did not move, so nothing is reloaded and no
	// identity changes: what moved is which of its rows the feed may show,
	// which is exactly what an applied page publishes. _historyUpdates is
	// the one stream every consumer of the projection already rides -
	// rebuildList, HistoryShownValue and listsEmptyStateValue all merge it -
	// so the gate itself is not recomputed here: none of its terms moved.
	// The paging bound is re-armed rather than inherited, because a moved
	// threshold changes what the reader can see at all: pages that were
	// spent looking for rows under the old one say nothing about the new.
	_historyUpdates.fire({});
	resetHiddenHistoryPages();
	continueHiddenHistory(!historyVisibleEmpty());
}

void Session::applyWalletAvailable() {
	const auto now = _session->appConfig().walletAvailable();
	if (_walletAvailable == now) {
		return;
	}
	_walletAvailable = now;
	if (!now) {
		return;
	}
	// WHY: WALLET_UNAVAILABLE answers latched before the server made the
	// wallet available describe the old state, so both lanes ask again.
	_userAddresses->resetUnavailable();
	if (_presence.current() != Presence::Unavailable) {
		return;
	}
	_stateFailures = 0;
	_stateRequestedAt = 0;
	_stateRefreshedAt = 0;
	setPresence(Presence::Unknown);
	if (_loaded) {
		refreshState();
	}
}

void Session::setHistory(std::vector<TransferItem> &&list) {
	_history = std::move(list);
	dropSubmittedIfListed();
	_listedBoundary = historyCanPage()
		? OldestHistoryDate(_history)
		: std::nullopt;
	_historyUpdates.fire({});
}

void Session::requestTransactions(bool more, Fn<void()> done) {
	if (_historyRequest) {
		if (done) {
			_historyRequest->done.push_back(std::move(done));
		}
		return;
	}
	const auto request = std::make_shared<HistoryRequest>(HistoryRequest{
		.identity = transferWalletIdentity(),
		.identityRevision = _walletIdentityRevision,
		.generation = _networkGeneration,
		.offset = more ? _historyNextOffset : QString(),
	});
	if (done) {
		request->done.push_back(std::move(done));
	}
	_historyRequest = request;
	// A head page issued after an invalidation is what the marker asked for,
	// whoever issued it, so it is spent here at the issue and not at the
	// success: a failed forced page therefore retries nothing by itself, and
	// a push landing during the flight re-arms the marker so that flight's
	// older answer can never satisfy it. A more page never spends it,
	// because it does not refresh the head.
	if (!more) {
		_historyStale = false;
	}
	_historyRequestedAt = crl::now();
	// The inbound and outbound flags stay unset on purpose: the overview
	// shows one undivided feed and offers no direction filter, so asking the
	// server for half of the list would invent a UI this task does not add.
	// They stay available for a filter that is actually designed.
	request->id = _stateApi.request(MTPwallet_GetTransactions(
		MTP_flags(0),
		MTP_string(request->offset),
		MTP_int(kTransactionsPerPage)
	)).done([=](const MTPwallet_Transactions &result) {
		const auto current = (_historyRequest == request)
			&& historyRequestCurrent(*request);
		if (_historyRequest == request) {
			_historyRequest = nullptr;
		}
		const auto weak = base::make_weak(_engine.get());
		if (current) {
			applyTransactions(result, more, *request);
		}
		FinishHistoryWaiters(base::take(request->done));
		if (weak && current && historyRequestCurrent(*request)) {
			refreshStaleHistory();
		}
	}).fail([=](const MTP::Error &error) {
		const auto current = (_historyRequest == request)
			&& historyRequestCurrent(*request);
		if (_historyRequest == request) {
			_historyRequest = nullptr;
		}
		const auto weak = base::make_weak(_engine.get());
		if (current) {
			LOG(("Wallet Error: wallet.getTransactions failed: %1"
				).arg(error.type()));
			if (!more) {
				_historyUnreachable = true;
			}
			_historySettled = true;
			updateListsGate();
			releaseDeferredRows();
		}
		FinishHistoryWaiters(base::take(request->done));
		if (weak && current && historyRequestCurrent(*request)) {
			refreshStaleHistory();
		}
	}).handleAllErrors().send();
}

bool Session::listRequestCurrent(
		const std::optional<TransferWalletIdentity> &identity,
		uint64 identityRevision,
		int generation) const {
	return generation == _networkGeneration
		&& identityRevision == _walletIdentityRevision
		&& _presence.current() == Presence::Ready
		&& identity == transferWalletIdentity();
}

bool Session::historyRequestCurrent(const HistoryRequest &request) const {
	return listRequestCurrent(
		request.identity,
		request.identityRevision,
		request.generation);
}

void Session::resolveTransaction(
		const QString &id,
		Fn<void(ResolvedTransaction)> done) {
	if (id.isEmpty()) {
		if (done) {
			done({ .failed = true });
		}
		return;
	}
	const auto identity = transferWalletIdentity();
	_stateApi.request(MTPwallet_GetTransactionsByIDs(
		MTP_vector<MTPstring>(1, MTP_string(id.toStdString()))
	)).done([=](const MTPwallet_Transactions &result) {
		const auto &data = result.data();
		// The peers come first, for the same reason the feed stores them
		// first: a transaction whose user is missing from Data::Session
		// falls back to its address instead of the Telegram identity.
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		auto list = HistoryFromServer(data.vtransactions().v, identity);
		rememberCollectibles(list);
		// WHY: the id a message carries is the transfer's trace id, while
		// the served row is named by its own lt:hash, so an answer to one
		// id is its only row, and anything else names no transaction.
		if (done) {
			done({ .item = (list.size() == 1)
				? std::make_optional(std::move(list.front()))
				: std::nullopt });
		}
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.getTransactionsByIDs failed: %1"
			).arg(error.type()));
		if (done) {
			done({ .failed = true });
		}
	}).send();
}

void Session::applyTransactions(
		const MTPwallet_Transactions &result,
		bool more,
		const HistoryRequest &request) {
	if (!historyRequestCurrent(request)) {
		return;
	}
	const auto weak = base::make_weak(_engine.get());
	const auto &data = result.data();
	// The peers are stored before anything resolves one, because a row whose
	// user is missing from Data::Session falls through to the address
	// and domain presentation. That is also legitimate for an address-only
	// peer, so a dropped users vector would look exactly like a working
	// client while losing the Telegram identity.
	_session->data().processUsers(data.vusers());
	if (!weak || !historyRequestCurrent(request)) {
		return;
	}
	_session->data().processChats(data.vchats());
	if (!weak || !historyRequestCurrent(request)) {
		return;
	}
	const auto wasNextOffset = _historyNextOffset;
	const auto wasHasNext = _historyHasNext;
	const auto next = data.vnext_offset();
	// An empty next_offset is byte-identical to a first-page request, so
	// paging on it would read the same rows forever. It ends the list exactly
	// as an absent one does; the value itself is opaque and never parsed.
	_historyNextOffset = next ? qs(*next) : QString();
	_historyHasNext = !_historyNextOffset.isEmpty();
	if (_historyHasNext && (_historyNextOffset == request.offset)) {
		// A cursor that comes back byte-identical to the one just spent
		// is the server making no progress: the next request would read
		// the same rows and append them a second time, which is a
		// duplicate-row defect as much as a request loop. It ends the
		// list exactly as an absent cursor does. A head request sends an
		// empty offset, which _historyHasNext already excludes, so this
		// can only ever fire for a `more` page.
		LOG(("Wallet Error: wallet.getTransactions repeated its offset."));
		_historyNextOffset = QString();
		_historyHasNext = false;
	}
	const auto opened = (_historyRefreshedAt != 0);
	_historyRefreshedAt = crl::now();
	_historyUnreachable = false;
	// checkLoadMore() pages only the transactions tab, but the answer is a
	// round trip late, so a page landing after the reader moved to
	// Collectibles would re-arm the term setCollectiblesTab() just released
	// and refuse the head refresh for a list nobody is looking at.
	// collectiblesTab() is the effective selection, never the raw request.
	_historyPaged = more
		&& (_panel != nullptr)
		&& !collectiblesTab();
	_historySettled = true;
	auto loaded = HistoryFromServer(data.vtransactions().v, request.identity);
	rememberCollectibles(loaded);
	const auto shown = ranges::any_of(loaded, [&](const TransferItem &i) {
		return !historyItemHidden(i);
	});
	if (shown) {
		_historyHiddenPages = 0;
	}
	auto arrived = std::vector<TransferItem>();
	if (more) {
		auto fresh = UnheldHistory(_history, std::move(loaded));
		if (!fresh.empty()) {
			auto list = _history;
			list.insert(
				end(list),
				std::make_move_iterator(begin(fresh)),
				std::make_move_iterator(end(fresh)));
			setHistory(std::move(list));
		}
	} else {
		const auto served = int(loaded.size());
		const auto full = (served == kTransactionsPerPage);
		// WHY: the list asked for when a wallet opens covers its first page,
		// and a row the history already held was followed when it arrived,
		// so only a row this history never held asks for the list again.
		if (opened) {
			arrived = ArrivedCollectibles(_history, loaded);
		}
		auto merged = MergedHeadHistory(_history, std::move(loaded));
		if ((merged.retained > 0)
			&& full
			&& (merged.retained == int(_history.size()))) {
			// A full page naming none of the loaded rows cannot be shown
			// to touch them: a page's worth of transfers has arrived
			// since the reader last paged, so keeping both halves would
			// leave a hole between them that no cursor reaches. The
			// served window is the truthful list there, which is what a
			// head page has always been, and its own cursor stands.
			merged.list.resize(served);
		} else if (!merged.namedLast) {
			// The page did not name the row the loaded list ends with, so
			// it did not reach past that tail and the cursor it carries
			// points back inside rows the merged list still holds:
			// spending it would re-read them, the merge would drop them
			// again as duplicates, and the older pages behind them would
			// never be reached. The cursor the list already had points
			// past its whole tail, so that is the one paging must keep,
			// and an exhausted list stays exhausted for the same reason.
			// A page that did reach the tail is past every merged row, so
			// there its own cursor stands.
			_historyNextOffset = wasNextOffset;
			_historyHasNext = wasHasNext;
		}
		if (!SameHistory(_history, merged.list)) {
			setHistory(std::move(merged.list));
		}
	}
	if (weak && historyRequestCurrent(request)) {
		updateListsGate();
		if (weak && historyRequestCurrent(request)) {
			continueHiddenHistory(shown);
		}
		if (weak && historyRequestCurrent(request)) {
			followCollectibles(arrived);
		}
		if (weak && historyRequestCurrent(request)) {
			updatePollingState();
		}
		if (weak && historyRequestCurrent(request)) {
			releaseDeferredRows();
		}
	}
}

void Session::continueHiddenHistory(bool progressed) {
	if (progressed) {
		resetHiddenHistoryPages();
		return;
	}
	if ((_panel == nullptr) || collectiblesTab()) {
		return;
	}
	loadMoreHistory();
}

void Session::clearHistory() {
	const auto request = base::take(_historyRequest);
	if (request) {
		_stateApi.request(request->id).cancel();
	}
	clearSubmittedTransfers();
	_history.clear();
	_historyHasNext = false;
	_historyNextOffset = QString();
	_historyRefreshedAt = 0;
	_historyRequestedAt = 0;
	_historySettled = false;
	_historyUnreachable = false;
	_historyPaged = false;
	_historyStale = false;
	resetHiddenHistoryPages();
	// _historySettled and _historyUnreachable, cleared just above, are the
	// gate's two history terms, so this drain is the only point at which an
	// emptied list and the gate the previous page settled could be read
	// together. Recomputing here keeps this lane's own publication from
	// ever being evaluated against the gate of the wallet whose rows just
	// left: an open gate over two empty lists is listsConfirmedEmpty().
	const auto weak = base::make_weak(_engine.get());
	const auto sendRevision = _sendRevision;
	updateListsGate();
	if (weak && sendRevision == _sendRevision) {
		_sendState = SendState::Idle;
	}
	if (weak && sendRevision == _sendRevision) {
		_historyUpdates.fire({});
	}
	if (request) {
		FinishHistoryWaiters(base::take(request->done));
	}
}

void Session::clearCollectibles() {
	if (const auto request = base::take(_collectiblesRequest)) {
		_stateApi.request(request->id).cancel();
	}
	_collectibles.clear();
	_collectibleFollowUps.clear();
	_collectiblesRefreshedAt = 0;
	_collectiblesCompletedAt = 0;
	_collectiblesForced = false;
	_collectiblesHasMore = false;
	_collectiblesNextOffset = QString();
	_collectiblesPaged = false;
	_collectiblesTab = false;
	_collectiblesUpdates.fire({});
}

bool Session::historyHasNext() const {
	return _historyHasNext;
}

bool Session::historyCanPage() const {
	return _historyHasNext && (_historyHiddenPages < kMaxHiddenPagesInRow);
}

void Session::releaseDeferredRows() {
	if (historyCanPage() || !_listedBoundary) {
		return;
	}
	_listedBoundary = std::nullopt;
	_historyUpdates.fire({});
}

bool Session::historyLoadingMore() const {
	// The feed has nothing it can show and the server says more exists, so
	// the walk that looks for a row worth a line is either running or owed.
	// An empty _history answers true as well, which is the same statement:
	// a page that carried a cursor and no row at all is still being looked
	// past. The two streams below carry every move of either term -
	// setHistory() fires _historyUpdates, and applyTransactions() ends in
	// updateListsGate(), which fires _listsStateUpdates even for the head
	// page that matched what was already loaded and wrote no rows.
	return _historyHasNext && historyVisibleEmpty();
}

rpl::producer<bool> Session::historyLoadingMoreValue() const {
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		historyUpdates(),
		_listsStateUpdates.events()
	)) | rpl::map([=, this] {
		return historyLoadingMore();
	}) | rpl::distinct_until_changed();
}

void Session::loadMoreHistory() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _historyRequest
		|| !_historyHasNext) {
		return;
	}
	// The bound is spent here, where the page is actually asked for, and
	// not at either pager's own entry: a hidden row adds no height, so the
	// list can keep believing it is short of content and ask again through
	// checkLoadMore(), while continueHiddenHistory() asks for the same lane
	// from the answer side. Counting requests is what makes both finite.
	if (_historyHiddenPages >= kMaxHiddenPagesInRow) {
		return;
	}
	++_historyHiddenPages;
	if (_historyHiddenPages == kMaxHiddenPagesInRow) {
		LOG(("Wallet: transaction paging bound of %1 pages spent."
			).arg(kMaxHiddenPagesInRow));
	}
	requestTransactions(true);
}

void Session::resetHiddenHistoryPages() {
	_historyHiddenPages = 0;
	releaseDeferredRows();
}

void Session::refreshCollectibles(bool force) {
	ensureLoaded();
	if (_presence.current() != Presence::Ready) {
		return;
	}
	// A forced ask refused below is owed until a head request goes out.
	_collectiblesForced = _collectiblesForced || force;
	const auto forced = _collectiblesForced
		|| !_collectibleFollowUps.empty();
	const auto interval = forced
		? kForcedCollectiblesInterval
		: kCollectiblesPollInterval;
	if (_collectiblesRequest
		|| _collectiblesPaged
		|| (_collectiblesRefreshedAt
			&& (crl::now() - _collectiblesRefreshedAt < interval))) {
		return;
	}
	_collectiblesForced = false;
	_collectiblesRefreshedAt = crl::now();
	requestCollectibles(false);
}

bool Session::collectiblesHasNext() const {
	return _collectiblesHasMore;
}

void Session::loadMoreCollectibles() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _collectiblesRequest
		|| !_collectiblesHasMore) {
		return;
	}
	requestCollectibles(true);
}

void Session::requestCollectibles(bool more) {
	if (_collectiblesRequest
		|| (more && _collectiblesNextOffset.isEmpty())) {
		return;
	}
	const auto request = std::make_shared<CollectiblesRequest>(
		CollectiblesRequest{
			.identity = transferWalletIdentity(),
			.identityRevision = _walletIdentityRevision,
			.generation = _networkGeneration,
			.offset = more ? _collectiblesNextOffset : QString(),
			.more = more,
		});
	_collectiblesRequest = request;
	request->id = _stateApi.request(MTPwallet_GetNfts(
		MTP_string(request->offset),
		MTP_int(kCollectiblesPerPage)
	)).done([=](const MTPwallet_NftItems &result) {
		const auto current = (_collectiblesRequest == request)
			&& listRequestCurrent(
				request->identity,
				request->identityRevision,
				request->generation);
		if (_collectiblesRequest == request) {
			_collectiblesRequest = nullptr;
		}
		if (current) {
			applyCollectibles(result, *request);
		}
	}).fail([=](const MTP::Error &error) {
		const auto current = (_collectiblesRequest == request)
			&& listRequestCurrent(
				request->identity,
				request->identityRevision,
				request->generation);
		if (_collectiblesRequest == request) {
			_collectiblesRequest = nullptr;
		}
		if (current) {
			LOG(("Wallet Error: wallet.getNfts failed: %1, "
				"keeping last-good collectibles."
				).arg(error.type()));
			if (!request->more) {
				spendCollectibleFollowUps(nullptr);
			}
		}
	}).handleAllErrors().send();
}

void Session::applyCollectibles(
		const MTPwallet_NftItems &result,
		const CollectiblesRequest &request) {
	const auto &data = result.data();
	const auto served = data.vnext_offset();
	auto next = served ? qs(*served) : QString();
	if (!next.isEmpty() && (next == request.offset)) {
		LOG(("Wallet Error: wallet.getNfts repeated its offset."));
		next = QString();
	}
	auto loaded = CollectiblesFromServer(data.vitems().v);
	_collectiblesNextOffset = next;
	_collectiblesHasMore = !next.isEmpty();
	_collectiblesCompletedAt = crl::now();
	_collectiblesPaged = request.more
		&& (_panel != nullptr)
		&& collectiblesTab();
	auto list = std::vector<Gram::NftItem>();
	if (request.more) {
		auto fresh = UnheldCollectibles(_collectibles, std::move(loaded));
		list = _collectibles;
		list.insert(
			end(list),
			std::make_move_iterator(begin(fresh)),
			std::make_move_iterator(end(fresh)));
	} else {
		list = std::move(loaded);
		spendCollectibleFollowUps(&list);
	}
	if (!SameCollectibles(_collectibles, list)) {
		setCollectibles(std::move(list));
	}
}

void Session::setCollectibles(std::vector<Gram::NftItem> &&list) {
	_collectibles = std::move(list);
	if (_collectibles.empty()) {
		_collectiblesTab = false;
		refreshStaleHistory();
	}
	for (const auto &item : _collectibles) {
		_collectibleInfo[item.address] = item;
	}
	_collectiblesUpdates.fire({});
}

void Session::rememberCollectibles(const std::vector<TransferItem> &items) {
	for (const auto &item : items) {
		if (const auto &record = item.collectibleRecord) {
			_collectibleInfo.emplace(record->address, *record);
		}
	}
}

void Session::followCollectibles(const std::vector<TransferItem> &arrived) {
	auto followed = false;
	for (const auto &row : arrived) {
		auto sent = false;
		if (!row.incoming) {
			for (auto &entry : _submitted) {
				if (entry.collectible != row.collectible
					|| entry.generation != _networkGeneration
					|| !transferWalletIdentityCurrent(entry.identity)
					|| (!entry.canonicalId.isEmpty()
						&& entry.canonicalId != row.id)) {
					continue;
				}
				sent = sent || entry.leaving;
				entry.leaving = true;
			}
		}
		// This client's own send already armed that transfer's follow-up.
		if (!sent) {
			followCollectible(row.collectible, row.incoming);
			followed = true;
		}
	}
	if (followed) {
		refreshCollectibles(true);
	}
}

void Session::followCollectible(const QString &address, bool incoming) {
	auto &followUp = _collectibleFollowUps[address];
	if (!followUp.left) {
		followUp.left = kCollectibleFollowUpRefreshes;
	}
	followUp.incoming = incoming;
}

void Session::spendCollectibleFollowUps(
		const std::vector<Gram::NftItem> *list) {
	auto &followUps = _collectibleFollowUps;
	for (auto i = begin(followUps); i != end(followUps);) {
		const auto listed = list
			&& ranges::contains(*list, i->first, &Gram::NftItem::address);
		const auto reflected = list && (listed == i->second.incoming);
		if (!reflected && (--i->second.left > 0)) {
			++i;
		} else {
			i = followUps.erase(i);
		}
	}
}

void Session::updateListsGate() {
	const auto weak = base::make_weak(_engine.get());
	const auto revision = _walletIdentityRevision;
	const auto presence = _presence.current();
	const auto unknown = (presence == Presence::Unknown);
	const auto ready = (presence == Presence::Ready);
	_stateUnreachable = (unknown
		&& (_stateFailures >= kStateFailuresBeforeStated))
		|| (ready && _historyUnreachable)
		|| (presence == Presence::AddressUnreadable);
	_listsGated = (unknown && !_stateUnreachable)
		|| (presence == Presence::Provisioning)
		|| (ready && !_historySettled && submittedTransactions().empty());
	if (weak && revision == _walletIdentityRevision) {
		_listsStateUpdates.fire({});
	}
}

bool Session::listsConfirmedEmpty() const {
	return !_listsGated.current()
		&& submittedTransactions().empty()
		&& historyVisibleEmpty()
		&& !_historyHasNext
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
		const auto i = _collectibleInfo.find(item);
		if (i != end(_collectibleInfo)) {
			found = i->second;
		} else if (remember) {
			_collectibleInfo.emplace(item, found);
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
	refreshStaleHistory();
}

void Session::stopPolling() {
	if (_pollingCount > 0) {
		--_pollingCount;
	}
	updatePollingState();
}

void Session::updatePollingState() {
	const auto wanted = (_pollingCount > 0)
		|| _pending
		|| _sendUnresolved
		|| sendRecoveryNeeded()
		|| submittedLookupNeeded()
		|| custody().pendingRotation
		|| custody().anyAwaitingServerKey();
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
	expireStaleSubmittedTransfers();
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
		|| stale(std::max(_stateRefreshedAt, _engineRefreshedAt))) {
		requestEngineRefresh();
	}
	// The history leg deliberately carries no !streaming disjunct. Every
	// refresh that disjunct reaches above self-floors, but refreshHistory()
	// does not: a stream hint means a transaction touched this address and
	// flooring it would delay a just-received transfer. With the disjunct the
	// lane would send a real wallet.getTransactions on every tick for the
	// whole time the stream is not live, which is every cold open, acquire,
	// reconnect and backoff. stale() is true for a zero stamp, so the first
	// tick still requests at once and the lane then settles to one request
	// per resync interval, plus the unfloored stream hints.
	const auto historyStale = stale(
		std::max(_historyRequestedAt, _historyRefreshedAt));
	const auto historyIdle = !_historyRequest;
	if (historyStale) {
		refreshHistory();
	}
	// A feed whose loaded pages are all hidden gives the reader nothing to
	// scroll, so there is no gesture it could ask for more with. The walk
	// that looks for a row it can show is resumed here instead, on the same
	// floor the head refresh uses, so it is paced by this client's clock and
	// never by how often a sender pushes. Idleness is read before that
	// refresh, because the term asks whether a walk is already running: a
	// head page issued by this very tick is not one, and the answer it
	// brings starts the walk itself through continueHiddenHistory().
	if (historyStale
		&& historyIdle
		&& (_panel != nullptr)
		&& !collectiblesTab()
		&& historyLoadingMore()) {
		resetHiddenHistoryPages();
		loadMoreHistory();
	}
	refreshCollectibles();
	if ((_pending
			|| _sendUnresolved
			|| sendRecoveryNeeded()
			|| custody().pendingRotation)
		&& !_resolveRequestPending) {
		resolvePending();
	}
	lookupSubmittedTransaction();
}

void Session::applyStreamRefresh(StreamRefresh wanted) {
	if (wanted.state) {
		refreshState();
	}
	if (wanted.history) {
		if (_historyRequest) {
			// The flight was sent before this hint, so it may miss its rows.
			const auto generation = _networkGeneration;
			const auto revision = _walletIdentityRevision;
			_historyRequest->done.push_back([=] {
				if (generation == _networkGeneration
					&& revision == _walletIdentityRevision) {
					refreshHistory();
				}
			});
		} else {
			refreshHistory();
		}
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

int64 Session::transferMinNanos() const {
	return _transferMinNanos;
}

bool Session::historyItemHidden(const TransferItem &item) const {
	return HistoryTransferHidden(item, _transferMinNanos);
}

bool Session::historyVisibleEmpty() const {
	return ranges::all_of(_history, [&](const TransferItem &item) {
		return historyItemHidden(item);
	});
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

rpl::producer<ListsEmptyState> Session::listsEmptyStateValue() const {
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		historyUpdates(),
		collectiblesUpdates(),
		_listsStateUpdates.events()
	)) | rpl::map([=, this] {
		return ListsEmptyState{
			.confirmedEmpty = listsConfirmedEmpty(),
			.unreachable = _stateUnreachable,
		};
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
	// Each lane's paged term is a fact about one visible list and ends with
	// that view: the tab strip scrolls the returning list back to its top,
	// so the head refresh a switch releases truncates nothing the reader
	// can still see. The clear follows tab, the effective selection, so
	// asking for a tab that cannot be shown deselects and clears nothing.
	// applyTransactions() reads that same effective selection before it
	// arms the history term, so a page landing later cannot undo this clear.
	if (tab) {
		_historyPaged = false;
	} else {
		_collectiblesPaged = false;
		resetHiddenHistoryPages();
	}
	_collectiblesTab = tab;
	refreshStaleHistory();
}

void Session::setWindowSend(std::string operationId) {
	_windowSend = std::move(operationId);
	_historyUpdates.fire({});
}

const std::string &Session::windowSend() const {
	return _windowSend;
}

SendState Session::sendState() const {
	return _sendState.current();
}

rpl::producer<SendState> Session::sendStateValue() const {
	return _sendState.value();
}

std::optional<PendingSendInfo> Session::pendingSend() const {
	return (_pending
		&& transferWalletIdentityCurrent(_pending->walletIdentity))
		? _pending
		: std::nullopt;
}

auto Session::lastTransferReceipt() const
-> const std::optional<TransferReceipt> & {
	return _lastReceipt;
}

const TransferItem *Session::submittedShown(
		const SubmittedTransfer &entry) const {
	if (entry.generation != _networkGeneration
		|| !transferWalletIdentityCurrent(entry.identity)
		|| (!entry.canonicalId.isEmpty()
			&& ranges::contains(
				_history,
				entry.canonicalId,
				&TransferItem::id))) {
		return nullptr;
	}
	return entry.item ? entry.item.get() : entry.fallback.get();
}

std::vector<TransferItem> Session::submittedTransactions() const {
	auto result = std::vector<TransferItem>();
	for (const auto &entry : _submitted) {
		if (const auto item = submittedShown(entry)) {
			result.push_back(*item);
		}
	}
	return result;
}

auto Session::listedSubmittedTransactions() const
-> std::vector<ListedSubmittedTransfer> {
	auto result = std::vector<ListedSubmittedTransfer>();
	for (const auto &entry : _submitted) {
		const auto item = submittedShown(entry);
		if (!item
			|| (_listedBoundary
				&& item->date
				&& (*item->date < *_listedBoundary))) {
			continue;
		}
		result.push_back({ entry.operationId, *item });
	}
	return result;
}

TonConnectSendFate Session::tonConnectSendFate(
		const std::string &operationId) {
	const auto identity = transferWalletIdentity();
	if (!_sendRecoveryReady || _clientStopping || !identity) {
		return TonConnectSendFate::Unknown;
	} else if (_submission && _submission->operationId == operationId) {
		return TonConnectSendFate::Sending;
	} else if ((_pending && _pending->operationId == operationId)
		|| (_sendUnresolved
			&& (_unresolvedOperationId.empty()
				|| _unresolvedOperationId == operationId))) {
		return TonConnectSendFate::Unresolved;
	}
	const auto record = submittedTransferRecord(operationId, *identity);
	if (!record) {
		return TonConnectSendFate::Absent;
	} else if (record->terminal == TransferTerminal::None) {
		return TonConnectSendFate::Unresolved;
	}
	return FailedTransferTerminal(record->terminal)
		? TonConnectSendFate::NotExecuted
		: TonConnectSendFate::Settled;
}

std::optional<TransferItem> Session::submittedTransaction(
		const std::string &operationId) const {
	const auto entry = ranges::find(
		_submitted,
		operationId,
		&SubmittedTransfer::operationId);
	if (entry == end(_submitted)
		|| entry->generation != _networkGeneration
		|| !transferWalletIdentityCurrent(entry->identity)) {
		return std::nullopt;
	}
	if (!entry->canonicalId.isEmpty()) {
		const auto item = ranges::find(
			_history,
			entry->canonicalId,
			&TransferItem::id);
		if (item != end(_history)) {
			return *item;
		}
	}
	return entry->item ? std::make_optional(*entry->item) : std::nullopt;
}

std::optional<TransferItem> Session::trackedTransaction(
		const std::string &operationId) const {
	if (auto result = submittedTransaction(operationId)) {
		return result;
	}
	const auto entry = ranges::find(
		_submitted,
		operationId,
		&SubmittedTransfer::operationId);
	const auto shown = (entry != end(_submitted))
		? submittedShown(*entry)
		: nullptr;
	return shown ? std::make_optional(*shown) : std::nullopt;
}

std::optional<TransferItem> Session::sendingTransaction(
		const std::string &operationId) const {
	if (operationId.empty()
		|| _sendState.current() != SendState::Sending
		|| !_submission
		|| _submission->operationId != operationId
		|| !_submission->rpcStarted
		|| !transferWalletIdentityCurrent(_submission->prepared->identity)) {
		return std::nullopt;
	}
	const auto &args = _submission->prepared->args;
	auto result = ItemFromPending({
		.walletIdentity = _submission->prepared->identity,
		.posted = _submission->posted,
		.amountNano = args.amountNano,
		.destination = CanonicalAddress(args.destination),
		.collectible = args.collectible,
		.comment = (args.comment.isPublic ? args.comment.text : QString()),
		.recipient = args.userId,
		.bounce = args.bounce,
	});
	const auto entry = ranges::find(
		_submitted,
		operationId,
		&SubmittedTransfer::operationId);
	if (entry != end(_submitted)
		&& entry->generation == _networkGeneration
		&& transferWalletIdentityCurrent(entry->identity)) {
		result.id = entry->canonicalId;
	}
	if (!result.id.isEmpty() || result.counterparty.isEmpty()) {
		return result;
	}
	const auto same = [&](const TransferItem &item) {
		return result.collectible.isEmpty()
			? (item.counterparty == result.counterparty
				&& item.amountNano == result.amountNano)
			: (item.kind == TransferItem::Kind::Collectible
				&& item.collectible == result.collectible);
	};
	const auto ambiguous = ranges::any_of(_submitted, [&](const auto &other) {
		return other.operationId != operationId
			&& other.generation == _networkGeneration
			&& other.canonicalId.isEmpty()
			&& (other.item || other.fallback)
			&& same(other.item ? *other.item : *other.fallback);
	});
	if (ambiguous) {
		return result;
	}
	const auto from = _submission->posted - kSendingMatchSkew;
	const auto candidate = [&](const TransferItem &item) {
		return !item.incoming
			&& item.kind != TransferItem::Kind::KeyChange
			&& !item.id.isEmpty()
			&& item.walletIdentity == result.walletIdentity
			&& same(item)
			&& item.date
			&& *item.date >= from
			&& !ranges::contains(
				_submitted,
				item.id,
				&SubmittedTransfer::canonicalId);
	};
	const auto found = ranges::find_if(_history, candidate);
	if (found == end(_history)) {
		return result;
	}
	// Identical transfers cannot be told apart, so none of them is hidden.
	const auto single = ranges::none_of(_history, [&](const auto &item) {
		return candidate(item) && (item.id != found->id);
	});
	if (single) {
		result.id = found->id;
	}
	return result;
}

TransferItem ItemFromPending(const PendingSendInfo &pending) {
	return TransferItem{
		.walletIdentity = pending.walletIdentity,
		.kind = (!pending.collectible.isEmpty()
			? TransferItem::Kind::Collectible
			: pending.recipient
			? TransferItem::Kind::PeerTransfer
			: TransferItem::Kind::Transfer),
		.incoming = false,
		.counterparty = pending.destination,
		.counterpartyBounceable = pending.bounce,
		.counterpartyPeer = (pending.recipient
			? peerFromUser(pending.recipient).value
			: quint64()),
		.collectible = pending.collectible,
		.amountNano = pending.amountNano,
		.comment = pending.comment,
		.date = pending.posted,
		.status = TransferItem::Status::Pending,
	};
}

int SendCommentBytes(const QString &text) {
	return text.toUtf8().size();
}

bool SendCommentFits(const QString &text) {
	return SendCommentBytes(text) <= kSendCommentMaxBytes;
}

int64 CollectibleTransferAttachedNanos() {
	return kCollectibleTransferAttachedNanos;
}

int64 TransferMinNanosFromConfig(float64 configured) {
	return MinNanosFromConfig(configured, kTransferMinNanosDefault);
}

int64 TransferMinNanos(not_null<Main::Session*> session) {
	return TransferMinNanosFromConfig(session->appConfig().get<float64>(
		u"wallet_transfer_min_nanos"_q,
		float64(kTransferMinNanosDefault)));
}

bool TransferMagnitudeBelowMinimum(int64 amountNano, int64 minNanos) {
	// Negating the smallest int64 is undefined and its magnitude does not
	// fit the type, so the one amount that cannot be measured is answered
	// from what is known about it instead: its magnitude is 2^63, which is
	// larger than every minimum an int64 can hold, so it is never below
	// one whatever this policy is configured with.
	constexpr auto smallest = std::numeric_limits<int64>::min();
	if (amountNano == smallest) {
		return false;
	}
	const auto magnitude = (amountNano < 0) ? -amountNano : amountNano;
	return (magnitude < minNanos);
}

bool TransferAmountBelowMinimum(int64 amountNano, int64 minNanos) {
	return (amountNano > 0)
		&& TransferMagnitudeBelowMinimum(amountNano, minNanos);
}

bool HistoryTransferHidden(const TransferItem &item, int64 minNanos) {
	using Kind = TransferItem::Kind;
	// Only an ordinary monetary transfer is judged. A key change, a
	// collectible, an on-ramp and a contract interaction are activity
	// the feed states for reasons of their own, and the amount threshold
	// says nothing about whether they are worth a row.
	const auto transfer = (item.kind == Kind::Transfer)
		|| (item.kind == Kind::PeerTransfer);
	return transfer
		&& TransferMagnitudeBelowMinimum(item.amountNano, minNanos);
}

uint64 Session::createPreviewOwner(rpl::lifetime &lifetime) {
	if (!_preview) {
		_preview = std::make_unique<PreviewState>();
	}
	const auto owner = ++_preview->lastOwner;
	_preview->owners.emplace(owner, 0);
	lifetime.add(crl::guard(_preview.get(), [=, this] {
		retirePreviewOwner(owner);
	}));
	refreshGaslessInfo(true);
	return owner;
}

void Session::estimateFee(
		KeyAuthorization auth,
		uint64 owner,
		const SendArgs &args,
		Fn<void(FeeResult)> done) {
	if (!_preview || !_preview->owners.contains(owner)) {
		return;
	}
	done = LoggedFeeDone(std::move(done));
	const auto transfersCollectible = !args.collectible.isEmpty();
	const auto collectible = transfersCollectible
		? CanonicalAddress(args.collectible)
		: QString();
	const auto recipient = transfersCollectible
		? CanonicalAddress(args.destination)
		: QString();
	const auto collectibleInvalid = transfersCollectible
		&& (collectible.isEmpty()
			|| recipient.isEmpty()
			|| (recipient == _address)
			|| (recipient == collectible)
			|| (!args.comment.text.isEmpty() && !args.comment.isPublic));
	const auto inputError = !SendCommentFits(args.comment.text)
		? SendError::CommentTooLong
		: collectibleInvalid
		? SendError::InvalidRequest
		: transfersCollectible
		? SendError::None
		: (args.amountNano <= 0
			|| FormatFriendly(args.destination, args.bounce).isEmpty())
		? SendError::InvalidRequest
		: TransferAmountBelowMinimum(
			args.amountNano,
			TransferMinNanos(_session))
		? SendError::AmountTooSmall
		: SendError::None;
	if (inputError != SendError::None) {
		cancelFeeEstimate(owner);
		if (done) {
			done(FeeResult{ .error = inputError });
		}
		return;
	}
	const auto isPrivate = !args.comment.text.isEmpty()
		&& !args.comment.isPublic;
	const auto keyed = isPrivate && auth.valid();
	const auto privateEpoch = keyed
		? std::make_optional(vault().clearEpoch())
		: std::nullopt;
	if (keyed && !ReadAuthorized(*this, auth)) {
		cancelFeeEstimate(owner);
		if (done) {
			done(FeeResult{ .error = SendError::Locked });
		}
		return;
	}
	ensureLoaded();
	const auto terms = gaslessTerms();
	const auto i = _preview->owners.find(owner);
	if (i == end(_preview->owners)) {
		return;
	}
	auto request = PreviewRequest{
		.identity = transferWalletIdentity(),
		.terms = terms,
		.owner = owner,
		.revision = ++i->second,
		.generation = _networkGeneration,
		.privateEpoch = privateEpoch,
		.client = _engine->client(),
		.auth = keyed ? std::move(auth) : KeyAuthorization(),
		.args = args,
		.operationId = transfersCollectible ? NewRecordId() : std::string(),
		.done = std::move(done),
		.feeOnly = isPrivate && !keyed,
	};
	if (transfersCollectible) {
		request.args.collectible = collectible;
		request.args.amountNano = kCollectibleTransferAttachedNanos;
	}
	enqueuePreview(std::move(request));
}

void Session::enqueuePreview(PreviewRequest request) {
	const auto owner = request.owner;
	const auto queued = ranges::find(
		_preview->queue,
		owner,
		&PreviewRequest::owner);
	if (queued != end(_preview->queue)) {
		*queued = std::move(request);
	} else {
		_preview->queue.push_back(std::move(request));
	}
	_previewPending = true;
	if (_preview->active && _preview->active->request.owner == owner) {
		_preview->active->request.done = nullptr;
		cancelPreview();
	}
	startPreview();
}

void Session::estimateTonConnect(
		uint64 owner,
		std::shared_ptr<const TonConnectTransfer> transfer,
		Fn<void(FeeResult)> done) {
	if (!_preview || !_preview->owners.contains(owner)) {
		return;
	}
	done = LoggedFeeDone(std::move(done));
	if (!transfer || !transfer->request || transfer->messages.empty()) {
		cancelFeeEstimate(owner);
		done(FeeResult{ .error = SendError::InvalidRequest });
		return;
	}
	ensureLoaded();
	const auto terms = gaslessTerms();
	const auto i = _preview->owners.find(owner);
	if (i == end(_preview->owners)) {
		return;
	}
	const auto &first = transfer->messages.front();
	const auto parsed = ParseAddress(first.destination);
	auto request = PreviewRequest{
		.identity = transferWalletIdentity(),
		.terms = terms,
		.owner = owner,
		.revision = ++i->second,
		.generation = _networkGeneration,
		.client = _engine->client(),
		.args = SendArgs{
			.destination = CanonicalAddress(first.destination),
			.amountNano = transfer->totalNano,
			.bounce = parsed ? parsed->bounceable : true,
		},
		.done = std::move(done),
		.tonConnect = transfer->request,
	};
	enqueuePreview(std::move(request));
}

void Session::cancelFeeEstimate(uint64 owner) {
	if (!_preview) {
		return;
	}
	const auto i = _preview->owners.find(owner);
	if (i == end(_preview->owners)) {
		return;
	}
	++i->second;
	const auto queued = ranges::find(
		_preview->queue,
		owner,
		&PreviewRequest::owner);
	if (queued != end(_preview->queue)) {
		_preview->queue.erase(queued);
	}
	if (_preview->active && _preview->active->request.owner == owner) {
		_preview->active->request.done = nullptr;
		cancelPreview();
	}
	_previewPending = _preview->active.has_value() || !_preview->queue.empty();
}

void Session::resolveCommentRecipient(
		const QString &destination,
		bool bounce,
		const QByteArray &recipientPublicKey,
		Fn<void(CommentRecipient)> done) {
	resolveCommentRecipientAttempt(
		destination,
		bounce,
		recipientPublicKey,
		std::move(done),
		0);
}

void Session::resolveCommentRecipientAttempt(
		const QString &destination,
		bool bounce,
		const QByteArray &recipientPublicKey,
		Fn<void(CommentRecipient)> done,
		int attempt) {
	// WHY: only the recipient's own answer may turn a private comment into a
	// public one, so a busy engine slot, a client swap or a provider that did
	// not answer is asked again a few times and then stated as Unknown.
	const auto retry = [=, this] {
		if (attempt >= kCommentRecipientRetries) {
			done(CommentRecipient::Unknown);
			return;
		}
		base::call_delayed(kCommentRecipientRetryDelay, _session, [=, this] {
			resolveCommentRecipientAttempt(
				destination,
				bounce,
				recipientPublicKey,
				done,
				attempt + 1);
		});
	};
	const auto recipient = FormatFriendly(destination, bounce);
	const auto client = _engine->client();
	if (recipient.isEmpty()) {
		done(CommentRecipient::Unknown);
		return;
	} else if (!client || _clientStopping) {
		retry();
		return;
	}
	auto request = engine::EncryptedCommentRecipientRequest{
		.recipient = recipient.toStdString(),
		.recipient_public_key = EngineKey(recipientPublicKey),
	};
	_engine->run([client, request = std::move(request)] {
		return client->resolve_encrypted_comment_recipient(request);
	}, [=](std::vector<uint8_t>) {
		done(CommentRecipient::Encryptable);
	}, [=](EngineError error) {
		if (SendErrorFrom(error) == SendError::CommentEncryptionUnavailable) {
			done(CommentRecipient::PlainOnly);
		} else {
			retry();
		}
	});
}

void Session::resolveDnsName(
		const QString &name,
		Fn<void(std::optional<QString>)> done,
		Fn<void(DnsLookupError)> fail) {
	const auto client = _engine->client();
	if (!client || _clientStopping) {
		fail(DnsLookupError::Failed);
		return;
	}
	_engine->run([client, name = name.toStdString()] {
		return client->resolve_dns(name);
	}, [=](std::optional<std::string> address) {
		done(address
			? std::make_optional(QString::fromStdString(*address))
			: std::nullopt);
	}, [=, this](EngineError error) {
		LOG(("Wallet Error: dns lookup failed: %1").arg(error.message));
		// A comment decryption may hold the slot; mid-transfer it is final.
		const auto busy = (SendErrorFrom(error) == SendError::AlreadySending)
			&& (_sendState.current() != SendState::Sending);
		fail(busy ? DnsLookupError::Busy : DnsLookupError::Failed);
	});
}

void Session::retirePreviewOwner(uint64 owner) {
	cancelFeeEstimate(owner);
	_preview->owners.remove(owner);
	if (_preview->owners.empty()) {
		retireGaslessRequest();
	}
}

bool Session::previewCurrent(const PreviewRequest &request) const {
	const auto i = _preview->owners.find(request.owner);
	return i != end(_preview->owners) && i->second == request.revision;
}

bool Session::transferClientMatches(
		const TransferWalletIdentity &identity,
		const std::shared_ptr<engine::WalletClient> &client) const {
	if (!client || client != _engine->client() || !_custody) {
		return false;
	}
	const auto record = _custody->current(
		identity.address,
		identity.publicKey);
	return record
		&& record->recordId == _clientRecordId
		&& record->network == int(engine::Network::kMainnet);
}

bool Session::previewClientMatches(
		const TransferWalletIdentity &identity,
		const std::shared_ptr<engine::WalletClient> &client) const {
	return transferClientMatches(identity, client)
		|| (client
			&& client == _engine->client()
			&& _clientRecordId.isEmpty()
			&& _clientPreviewIdentity
			&& _clientPreviewIdentity->address == identity.address
			&& _clientPreviewIdentity->publicKey == identity.publicKey);
}

auto Session::signingClient() const
-> std::shared_ptr<engine::WalletClient> {
	return _clientRecordId.isEmpty() ? nullptr : _engine->client();
}

bool Session::signingReady() const {
	return _signingReady.current();
}

rpl::producer<bool> Session::signingReadyValue() const {
	return _signingReady.value();
}

void Session::updateSigningReady() {
	_signingReady = (signingClient() != nullptr)
		&& !_clientStopping
		&& _sendRecoveryReady;
}

void Session::settleDeferredDecrypts() {
	if (_clientStopping || _deferredDecrypts.empty()) {
		return;
	}
	for (auto &deferred : base::take(_deferredDecrypts)) {
		decryptComment(std::move(deferred));
	}
}

SendError Session::previewError(const PreviewRequest &request) {
	const auto terms = gaslessTerms();
	const auto ordinary = !request.tonConnect
		&& request.args.collectible.isEmpty();
	if (!previewCurrent(request)) {
		return SendError::QuoteExpired;
	} else if (request.privateEpoch
		&& (*request.privateEpoch != vault().clearEpoch()
			|| !ReadAuthorized(*this, request.auth))) {
		return SendError::Locked;
	} else if (request.generation != _networkGeneration
		|| !request.identity
		|| !transferWalletIdentityCurrent(*request.identity)) {
		return SendError::Failed;
	} else if (_presence.current() != Presence::Ready
		|| request.identity->publicKey.size() != kCustodyPublicKeySize
		|| (!request.tonConnect && request.args.amountNano <= 0)
		|| request.args.destination.isEmpty()) {
		return SendError::InvalidRequest;
	} else if (ordinary
		&& TransferAmountBelowMinimum(
			request.args.amountNano,
			TransferMinNanos(_session))) {
		return SendError::AmountTooSmall;
	} else if (ordinary
		&& (request.terms != terms || terms.identity != request.identity)) {
		return SendError::QuoteExpired;
	} else if (_clientStopping
		|| !previewClientMatches(*request.identity, request.client)) {
		return SendError::SigningUnavailable;
	} else if (_sendState.current() == SendState::Sending
		|| _rotating
		|| custody().pendingRotation) {
		return SendError::AlreadySending;
	} else if (_pending || _sendUnresolved) {
		return SendError::PreviousUnresolved;
	}
	return SendError::None;
}

void Session::startPreview() {
	if (_preview->dispatching || _preview->active) {
		return;
	}
	_preview->dispatching = true;
	const auto weak = base::make_weak(_preview.get());
	while (!_preview->queue.empty()) {
		auto next = std::move(_preview->queue.front());
		_preview->queue.pop_front();
		_previewPending = !_preview->queue.empty();
		if (!previewCurrent(next)) {
			continue;
		}
		const auto error = previewError(next);
		if (!weak) {
			return;
		} else if (!previewCurrent(next)) {
			continue;
		} else if (error != SendError::None) {
			if (const auto done = base::take(next.done)) {
				done(FeeResult{ .error = error });
				if (!weak) {
					return;
				}
			}
			continue;
		}
		const auto flight = ++_preview->lastFlight;
		_preview->active = PreviewState::Flight{
			.id = flight,
			.request = std::move(next),
		};
		_previewPending = true;
		const auto &request = _preview->active->request;
		if (!request.args.collectible.isEmpty()) {
			previewCollectible(flight);
		} else if (request.tonConnect || request.args.comment.text.isEmpty()) {
			previewPrepared(flight, engine::SendMessageBody::kEmpty{});
		} else if (request.args.comment.isPublic) {
			previewPrepared(flight, engine::SendMessageBody::kComment{
				.text = request.args.comment.text.toUtf8().toStdString(),
			});
		} else if (request.feeOnly) {
			previewPrepared(flight, engine::SendMessageBody::kRawPayload{
				.boc = EncryptedCommentFeeBody(
					request.args.comment.text).toStdString(),
			});
		} else {
			const auto client = request.client;
			const auto signingRecordId = _clientRecordId;
			auto encrypt = engine::CreateEncryptedCommentRequest{
				.recipient = FormatFriendly(
					request.args.destination,
					request.args.bounce).toStdString(),
				.comment = request.args.comment.text.toUtf8().toStdString(),
				.recipient_public_key = EngineKey(
					request.args.recipientPublicKey),
			};
			_engine->run([client, encrypt = std::move(encrypt)] {
				return client->create_encrypted_comment(encrypt);
			}, [=, this](engine::Boc body) {
				previewPrepared(flight, engine::SendMessageBody::kRawPayload{
					.boc = std::move(body),
				});
			}, [=, this](EngineError error) {
				// Encrypting for the recipient reads this wallet's own key,
				// so a refused read here says the same thing about it as a
				// refused signature does.
				noteSecretReadFailure(
					ProtectedSecretFailure(error),
					signingRecordId);
				finishPreview(flight, FeeResult{
					.error = SendErrorFrom(error),
				});
			});
		}
		if (!weak) {
			return;
		} else if (_preview->active) {
			break;
		}
	}
	_preview->dispatching = false;
}

void Session::previewPrepared(uint64 flight, engine::SendMessageBody body) {
	if (!_preview->active || _preview->active->id != flight) {
		return;
	}
	auto &active = *_preview->active;
	if (!active.request.done || !previewCurrent(active.request)) {
		finishPreview(flight, FeeResult{ .error = SendError::Failed });
		return;
	}
	const auto error = previewError(active.request);
	if (error != SendError::None) {
		finishPreview(flight, FeeResult{ .error = error });
		return;
	}
	const auto tonConnect = active.request.tonConnect;
	active.intent = tonConnect
		? std::make_shared<const engine::SendIntent>(tonConnect->intent)
		: std::make_shared<const engine::SendIntent>(
			IntentFromArgs(active.request.args, std::move(body)));
	active.stage = PreviewState::Flight::Stage::Previewing;
	const auto client = active.request.client;
	const auto paired = !tonConnect && active.request.terms.eligible(
		active.request.args.amountNano,
		active.request.args.destination);
	const auto own = active.request.identity
		? active.request.identity->address
		: QString();
	const auto total = active.request.args.amountNano;
	auto request = engine::SendPreviewRequest{ .intent = *active.intent };
	_engine->run([client, tonConnect, request = std::move(request)] {
		return tonConnect
			? client->preview_ton_connect(*tonConnect)
			: client->preview_send(request);
	}, [=, this](engine::SendPreview preview) {
		const auto fee = DecimalInt64(preview.emulation.wallet_fees_nanograms);
		auto result = (fee && *fee >= 0)
			? FeeResult{ .feeNano = *fee }
			: FeeResult{ .error = SendError::Failed };
		if (tonConnect && result.error == SendError::None) {
			result.emulation = std::make_shared<const TonConnectEmulation>(
				ParseTonConnectEmulation(preview.emulation, own, total));
		}
		finishPreview(flight, std::move(result));
	}, [=, this](EngineError error) {
		// The preview emulates the ordinary form of the transfer, so it
		// refuses an amount that would leave the wallet without its fee.
		// A fee-free transfer is not paid for by the wallet, and the engine
		// asks nothing but the amount of it when it signs the pair, so the
		// refusal is the fee reserve alone and this transfer carries none.
		if (paired && IsInsufficientForFees(error)) {
			finishPreview(flight, FeeResult{ .feeNano = 0 });
			return;
		}
		finishPreview(flight, FeeResult{ .error = SendErrorFrom(error) });
	});
}

void Session::previewCollectible(uint64 flight) {
	if (!_preview->active || _preview->active->id != flight) {
		return;
	}
	auto &active = *_preview->active;
	if (!active.request.done || !previewCurrent(active.request)) {
		finishPreview(flight, FeeResult{ .error = SendError::Failed });
		return;
	}
	const auto error = previewError(active.request);
	if (error != SendError::None) {
		finishPreview(flight, FeeResult{ .error = error });
		return;
	}
	active.nft = std::make_shared<const engine::NftTransferIntent>(
		CollectibleTransferIntent(active.request.args));
	active.stage = PreviewState::Flight::Stage::Previewing;
	const auto client = active.request.client;
	auto request = engine::NftTransferPreviewRequest{
		.operation_id = active.request.operationId,
		.intent = *active.nft,
	};
	_engine->run([client, request = std::move(request)] {
		return client->preview_nft_transfer(request);
	}, [=, this](engine::SendPreview preview) {
		const auto fee = DecimalInt64(
			preview.emulation.trace_fees_nanograms);
		finishPreview(flight, (fee && *fee >= 0)
			? FeeResult{ .feeNano = *fee }
			: FeeResult{ .error = SendError::Failed });
	}, [=, this](EngineError error) {
		finishPreview(flight, FeeResult{ .error = SendErrorFrom(error) });
	});
}

void Session::finishPreview(uint64 flight, FeeResult result) {
	if (!_preview->active || _preview->active->id != flight) {
		return;
	}
	_preview->active->finished = true;
	_preview->active->result = result;
	settlePreview();
}

void Session::cancelPreview() {
	if (!_preview->active
		|| _preview->active->stage != PreviewState::Flight::Stage::Previewing
		|| _preview->active->cancelIssued) {
		return;
	}
	_preview->active->cancelIssued = true;
	const auto flight = _preview->active->id;
	const auto client = _preview->active->request.client;
	_engine->runQuick([client] {
		client->cancel_send_preview();
	}, [=, this] {
		finishPreviewCancel(flight);
	}, [=, this](EngineError) {
		finishPreviewCancel(flight);
	});
}

void Session::finishPreviewCancel(uint64 flight) {
	if (!_preview->active || _preview->active->id != flight) {
		return;
	}
	_preview->active->cancelFinished = true;
	settlePreview();
}

void Session::settlePreview() {
	if (!_preview->active
		|| !_preview->active->finished
		|| (_preview->active->cancelIssued
			&& !_preview->active->cancelFinished)) {
		return;
	}
	auto flight = *base::take(_preview->active);
	_previewPending = !_preview->queue.empty();
	const auto weak = base::make_weak(_preview.get());
	if (flight.request.done && previewCurrent(flight.request)) {
		const auto error = previewError(flight.request);
		if (!weak) {
			return;
		} else if (!previewCurrent(flight.request)) {
			startPreview();
			return;
		} else if (error != SendError::None) {
			flight.result = FeeResult{ .error = error };
		} else if (flight.result.error == SendError::None
			&& flight.intent
			&& !flight.cancelIssued
			&& flight.request.feeOnly) {
			flight.intent = nullptr;
		} else if (flight.result.error == SendError::None
			&& (flight.intent || flight.nft)
			&& !flight.cancelIssued) {
			flight.result.prepared = std::make_shared<const PreparedSend>(
				PreparedSend{
					.args = flight.request.args,
					.identity = *flight.request.identity,
					.terms = flight.request.terms,
					.intent = std::move(flight.intent),
					.nft = std::move(flight.nft),
					.operationId = flight.request.operationId,
					.feeNano = flight.result.feeNano,
					.owner = flight.request.owner,
					.revision = flight.request.revision,
					.generation = flight.request.generation,
					.privateEpoch = flight.request.privateEpoch,
					.client = flight.request.client,
					.tonConnect = (flight.request.tonConnect != nullptr),
				});
		} else if (flight.result.error == SendError::None) {
			flight.result = FeeResult{ .error = SendError::Failed };
		}
		if (const auto done = base::take(flight.request.done)) {
			done(std::move(flight.result));
			if (!weak) {
				return;
			}
		}
	}
	startPreview();
}

void Session::retirePreviews(SendError error) {
	if (!_preview) {
		return;
	}
	auto retired = base::take(_preview->queue);
	if (_preview->active) {
		auto request = _preview->active->request;
		request.done = base::take(_preview->active->request.done);
		retired.push_front(std::move(request));
		cancelPreview();
	}
	_previewPending = _preview->active.has_value();
	const auto weak = base::make_weak(_preview.get());
	for (auto &request : retired) {
		if (previewCurrent(request)) {
			if (const auto done = base::take(request.done)) {
				done(FeeResult{ .error = error });
				if (!weak) {
					return;
				}
			}
		}
	}
}

SendError Session::sendRefusal(
		const std::shared_ptr<const PreparedSend> &prepared,
		const KeyAuthorization &auth) {
	if (!prepared || (!prepared->intent && !prepared->nft) || !_preview) {
		return SendError::InvalidRequest;
	}
	const auto terms = gaslessTerms();
	const auto owner = _preview->owners.find(prepared->owner);
	if (owner == end(_preview->owners)
		|| owner->second != prepared->revision) {
		return SendError::QuoteExpired;
	}
	const auto &args = prepared->args;
	const auto tonConnect = prepared->tonConnect;
	const auto ordinary = !tonConnect && !prepared->nft;
	if (!SendCommentFits(args.comment.text)) {
		return SendError::CommentTooLong;
	}
	if ((!tonConnect
			&& (args.amountNano <= 0
				|| FormatFriendly(args.destination, args.bounce).isEmpty()))
		|| prepared->feeNano < 0
		|| (!args.comment.text.isEmpty()
			&& !args.comment.isPublic
			&& !prepared->privateEpoch)
		|| (prepared->nft && prepared->operationId.empty())) {
		return SendError::InvalidRequest;
	}
	if (ordinary
		&& TransferAmountBelowMinimum(
			args.amountNano,
			TransferMinNanos(_session))) {
		return SendError::AmountTooSmall;
	}
	if (prepared->privateEpoch
		&& *prepared->privateEpoch != vault().clearEpoch()) {
		return SendError::Locked;
	}
	if (_presence.current() != Presence::Ready
		|| prepared->generation != _networkGeneration
		|| !transferWalletIdentityCurrent(prepared->identity)) {
		return SendError::Failed;
	}
	if (ordinary
		&& (prepared->terms != terms
			|| terms.identity != prepared->identity)) {
		return SendError::QuoteExpired;
	}
	if (_clientStopping
		|| !transferClientMatches(prepared->identity, prepared->client)) {
		return SendError::SigningUnavailable;
	}
	if (_sendState.current() != SendState::Idle
		|| _rotating
		|| custody().pendingRotation) {
		return SendError::AlreadySending;
	}
	if (_pending || _sendUnresolved) {
		return SendError::PreviousUnresolved;
	}
	if (!_sendRecoveryReady) {
		return SendError::Failed;
	}
	if (!ReadAuthorized(*this, auth)) {
		return SendError::Locked;
	}
	const auto paired = ordinary
		&& terms.eligible(args.amountNano, args.destination);
	const auto balance = _balanceNano.current();
	if (args.amountNano > balance) {
		return SendError::InsufficientBalance;
	}
	if (!paired && prepared->feeNano > balance - args.amountNano) {
		// The relayer pays a fee-free transfer's fee, so nothing of the
		// balance is kept back for it and the whole of it can be sent.
		return SendError::InsufficientFees;
	}
	return SendError::None;
}

void Session::send(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		Fn<void(SendError)> done,
		Fn<void(SendStarted)> started) {
	startSend(
		std::move(auth),
		std::move(prepared),
		std::move(done),
		std::move(started),
		nullptr,
		{});
}

void Session::sendTonConnect(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		TonConnectSendLink link,
		Fn<void(TonConnectSendResult)> done,
		Fn<void(SendError)> settled) {
	if (!prepared
		|| !prepared->tonConnect
		|| link.operationId.empty()
		|| link.operationId.size() > kTonConnectOperationIdMaxBytes
		|| !link.handoff) {
		if (done) {
			done({ .error = SendError::InvalidRequest });
		}
		return;
	}
	startSend(
		std::move(auth),
		std::move(prepared),
		std::move(settled),
		nullptr,
		std::move(done),
		std::move(link));
}

void Session::startSend(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		Fn<void(SendError)> done,
		Fn<void(SendStarted)> started,
		Fn<void(TonConnectSendResult)> tonConnect,
		TonConnectSendLink tonConnectLink) {
	done = [done = std::move(done)](SendError error) {
		if (error != SendError::None) {
			LOG(("Wallet Error: the send answered %1."
				).arg(SendErrorName(error)));
		}
		if (done) {
			done(error);
		}
	};
	const auto fail = [&](SendError error) {
		if (done) {
			done(error);
		}
		if (tonConnect) {
			tonConnect({ .error = error });
		}
	};
	const auto refusal = sendRefusal(prepared, auth);
	if (refusal != SendError::None) {
		fail(refusal);
		return;
	}
	const auto terms = gaslessTerms();
	const auto &args = prepared->args;
	const auto ordinary = !prepared->tonConnect && !prepared->nft;
	const auto paired = ordinary
		&& terms.eligible(args.amountNano, args.destination);
	const auto linked = !tonConnectLink.operationId.empty();
	const auto preset = linked || !prepared->operationId.empty();
	const auto operationId = linked
		? tonConnectLink.operationId
		: preset
		? prepared->operationId
		: NewRecordId();
	const auto client = prepared->client;
	const auto generation = prepared->generation;
	const auto identity = prepared->identity;
	const auto custodyRecord = custody().current(
		identity.address,
		identity.publicKey);
	if (!custodyRecord
		|| custodyRecord->recordId != _clientRecordId
		|| (preset && submittedTransferRecord(operationId, identity))) {
		fail(SendError::Failed);
		return;
	}
	// The record this send signs with, named now: a swap can rebind the
	// session's own id before the engine answers.
	const auto signingRecordId = custodyRecord->recordId;
	const auto signingKey = custodyRecord->signingKey.isEmpty()
		? custodyRecord->publicKey
		: custodyRecord->signingKey;
	submittedTransferStore().records.push_back(SubmittedTransferRecord{
		.recordId = custodyRecord->recordId,
		.address = identity.address,
		.publicKey = identity.publicKey,
		.operationId = operationId,
		.destination = CanonicalAddress(args.destination),
		.comment = args.comment.isPublic ? args.comment.text : QString(),
		.collectible = args.collectible,
		.amountNano = args.amountNano,
		.recipient = args.userId,
		.posted = base::unixtime::now(),
		.network = custodyRecord->network,
		.paired = paired,
		.bounce = args.bounce,
	});
	_submittedTransfersDirty = true;
	if (!persistSubmittedTransfers()) {
		retireSubmittedTransferRecord(operationId, identity);
		LOG(("Wallet Error: transfer preparation could not be stored."));
		fail(SendError::Failed);
		return;
	}
	const auto stored = submittedTransferRecord(operationId, identity);
	if (!stored) {
		fail(SendError::Failed);
		return;
	}
	const auto pending = PendingSendInfo{
		.operationId = operationId,
		.walletIdentity = identity,
		.posted = stored->posted,
		.amountNano = stored->amountNano,
		.destination = stored->destination,
		.collectible = stored->collectible,
		.comment = stored->comment,
		.recipient = stored->recipient,
		.bounce = stored->bounce,
	};
	const auto owner = _preview->owners.find(prepared->owner);
	if (owner != end(_preview->owners)) {
		++owner->second;
	}
	++_sendRevision;
	_lastReceipt.reset();
	const auto userId = args.userId;
	const auto weak = base::make_weak(_engine.get());
	const auto current = [=, this] {
		return weak && submissionCurrent(operationId, prepared);
	};
	const auto recordPending = [=, this](SendError answer) {
		if (!current() || _sendState.current() != SendState::Sending) {
			return;
		}
		const auto record = submittedTransferRecord(operationId, identity);
		const auto held = submittedTransfer(operationId);
		if (!record
			&& (!held
				|| held->client.lock() != client
				|| held->canonicalId.isEmpty())) {
			return;
		}
		if (record && record->handoff != TransferHandoff::Possible) {
			record->handoff = TransferHandoff::Possible;
			_submittedTransfersDirty = true;
		}
		_pending = pending;
		const auto entry = held ? held : upsertSubmittedTransfer(
			operationId,
			identity,
			generation,
			client);
		if (entry
			&& !entry->receipt
			&& entry->canonicalId.isEmpty()
			&& _submission->receipt) {
			entry->receipt = _submission->receipt;
		}
		if (!persistSubmittedTransfers()) {
			LOG(("Wallet Error: submitted transfer facts remain dirty."));
		}
		_sendUnresolved = true;
		_unresolvedOperationId = operationId;
		dropSubmittedIfListed();
		_sendState = SendState::Pending;
		if (!current()) {
			return;
		}
		_historyUpdates.fire({});
		if (!current()) {
			return;
		}
		updateListsGate();
		if (!current()) {
			return;
		}
		startSubmittedLookup();
		if (!current()) {
			return;
		}
		requestEngineRefresh();
		if (current() && done) {
			done(answer);
		}
	};
	const auto recordUnknown = [=] {
		recordPending(SendError::SubmissionUnknown);
	};
	auto request = std::optional<engine::SendRequest>();
	if (prepared->intent) {
		request = engine::SendRequest{
			.operation_id = operationId,
			.force = false,
			.intent = *prepared->intent,
		};
	}
	const auto route = std::make_shared<TransferSubmission>([=, this](
			TransferSubmissionData data,
			Fn<void(TransferSubmissionAnswer)> answer) {
		if (!weak) {
			answer({ TransferSubmissionOutcome::Rejected });
			return;
		}
		submitTransfer(
			operationId,
			prepared,
			std::move(data),
			std::move(answer));
	});
	_submission = TransferSubmissionState{
		.operationId = operationId,
		.prepared = prepared,
		.started = std::move(started),
		.tonConnect = std::move(tonConnect),
		.tonConnectHandoff = std::move(tonConnectLink.handoff),
		.posted = stored->posted,
		.paired = paired,
		.normalFeeAuthorized = true,
	};
	_engine->run([
		client,
		request = std::move(request),
		nft = prepared->nft,
		operationId,
		route,
		paired
	] {
		if (nft) {
			const auto recording = route->record();
			return client->send_nft_transfer(engine::NftTransferRequest{
				.operation_id = operationId,
				.force = false,
				.intent = *nft,
			});
		} else if (!paired) {
			const auto recording = route->record();
			return client->send(*request);
		}
		// A fee-free offer needs both delivery forms of the same transfer.
		// prepare_transfer() reads the account once, so the pair it signs
		// covers one sequence number and one validity window and the two
		// forms stay mutually exclusive; it submits and journals neither.
		// The external form then goes through the ordinary durable send_boc
		// workflow, which keeps the journal, phases and resolution exactly
		// as an ordinary send has them, while the relayer alternative rides
		// the recording to the routed host so that one wallet.sendTransfer
		// carries both and the server picks the form it will execute.
		const auto pair = client->prepare_transfer(
			engine::PrepareTransferRequest{
				.operation_id = request->operation_id,
				.intent = request->intent,
			});
		// A Boc crosses the engine boundary as its standard padded Base64
		// text: send_boc() takes the external form back as it came, while
		// wallet.sendTransfer carries raw bytes. An undecodable alternative
		// is recorded empty, which submitTransfer() refuses.
		auto gasless = QByteArray::fromBase64Encoding(
			QByteArray::fromStdString(pair.internal_boc),
			QByteArray::Base64Encoding
				| QByteArray::AbortOnBase64DecodingErrors);
		const auto recording = route->record(gasless
			? std::move(gasless.decoded)
			: QByteArray());
		return client->send_boc(engine::SendBocRequest{
			.operation_id = pair.operation_id,
			.force = request->force,
			.signed_boc = pair.external_boc,
			.seqno = pair.seqno,
			.valid_until = pair.valid_until,
		});
	}, [=, this, grant = auth.grant](engine::SendResult result) {
		if (!current() || _sendState.current() != SendState::Sending) {
			return;
		}
		if (auto report = base::take(_submission->tonConnect)) {
			report(TonConnectSendOutcome(
				result,
				operationId,
				_submission->rpcStarted,
				_submission->normal));
			if (!current()) {
				return;
			}
		}
		if (result.operation_id != operationId) {
			LOG(("Wallet Error: engine send result names another operation."));
			recordUnknown();
			return;
		}
		switch (result.phase) {
		case engine::SendPhase::kSubmitted:
			if (userId) {
				const auto user = _session->data().userLoaded(userId);
				if (user && !user->isSelf()) {
					_session->recentMoneyRecipients().bump(user);
					if (!current()) {
						return;
					}
				}
			}
			recordPending(SendError::None);
			return;
		case engine::SendPhase::kSubmissionUnknown:
		case engine::SendPhase::kHandedOff:
		case engine::SendPhase::kIdle:
		case engine::SendPhase::kValidating:
		case engine::SendPhase::kAuthorizing:
		case engine::SendPhase::kPreparing:
		case engine::SendPhase::kPersisting:
		case engine::SendPhase::kReadyToSubmit:
		case engine::SendPhase::kSubmitting:
			recordUnknown();
			return;
		case engine::SendPhase::kFailed:
		case engine::SendPhase::kCancelled:
		case engine::SendPhase::kConfirmed:
		case engine::SendPhase::kReplaced:
		case engine::SendPhase::kSequenceNumberConsumed:
		case engine::SendPhase::kExpired:
		case engine::SendPhase::kSuperseded: {
			const auto record = submittedTransferRecord(operationId, identity);
			if (record && record->handoff == TransferHandoff::Possible) {
				const auto entry = upsertSubmittedTransfer(
					operationId,
					identity,
					generation,
					client);
				if (entry) {
					entry->terminal = result.phase;
					if (entry->fallback && FailedTransferTerminal(
							StoredTransferTerminal(result.phase))) {
						entry->fallback->status = TransferItem::Status::Failure;
					}
				}
			} else {
				retireSubmittedTransferRecord(operationId, identity);
			}
			if (!persistSubmittedTransfers()) {
				LOG(("Wallet Error: terminal transfer facts remain dirty."));
			}
			const auto submission = base::take(_submission);
			if (submission
				&& submission->rpcStarted
				&& FailedTransferTerminal(StoredTransferTerminal(
					PairedSendPhase(result.phase, paired)))) {
				_transferMessages->failSending(
					submission->draft,
					TransferTerminalCode(StoredTransferTerminal(
						PairedSendPhase(result.phase, paired))));
			}
			// A pair refused before its broadcast started names an offer
			// that expired under the confirmed operation, not the fee the
			// user authorized for the normal variant.
			const auto refusal = submission
				? submission->refusal.value_or(
					(paired && !submission->rpcStarted)
						? SendError::QuoteExpired
						: SendError::Failed)
				: SendError::Failed;
			_sendState = SendState::Idle;
			LOG(("Wallet Error: engine send ended in phase %1 (%2)."
				).arg(int(result.phase)).arg(int(refusal)));
			if (!weak) {
				return;
			}
			syncEngineClient();
			if (weak) {
				_historyUpdates.fire({});
			}
			if (!weak) {
				return;
			} else if (refusal == SendError::KeyMismatch) {
				settleKeyMismatch(identity, signingKey, done);
			} else if (done) {
				done(refusal);
			}
		} return;
		}
	}, [=, this, grant = auth.grant](EngineError error) {
		// The signing read happened before any of the bookkeeping below, so
		// what it says about the key is recorded whatever this send becomes.
		noteSecretReadFailure(ProtectedSecretFailure(error), signingRecordId);
		if (!current() || _sendState.current() != SendState::Sending) {
			return;
		} else if (IsSubmissionUnknown(error)) {
			SettleTonConnect(*_submission, SendError::SubmissionUnknown);
			recordUnknown();
			return;
		}
		if (auto report = base::take(_submission->tonConnect)) {
			report({
				.error = _submission->refusal.value_or(SendErrorFrom(error)),
			});
		}
		auto possible = false;
		if (const auto record = submittedTransferRecord(operationId, identity)) {
			possible = (record->handoff == TransferHandoff::Possible);
			if (!possible) {
				retireSubmittedTransferRecord(operationId, identity);
				if (!persistSubmittedTransfers()) {
					LOG(("Wallet Error: unused transfer preparation remains dirty."));
				}
			}
		}
		auto listed = false;
		if (_submission && _submission->rpcStarted && possible) {
			const auto entry = upsertSubmittedTransfer(
				operationId,
				identity,
				generation,
				client);
			if (entry) {
				if (entry->fallback) {
					entry->fallback->status = TransferItem::Status::Failure;
				}
				listed = true;
			}
			if (!persistSubmittedTransfers()) {
				LOG(("Wallet Error: failed transfer facts remain dirty."));
			}
		}
		const auto submission = base::take(_submission);
		if (submission && submission->rpcStarted) {
			_transferMessages->failSending(
				submission->draft,
				error.message);
		}
		auto failed = SendErrorFrom(error);
		if (submission && submission->refusal) {
			failed = *submission->refusal;
		} else if (paired
			&& submission
			&& !submission->rpcStarted
			&& (failed == SendError::Failed
				|| failed == SendError::InvalidRequest
				|| failed == SendError::DataInvalid)) {
			failed = SendError::QuoteExpired;
		}
		LOG(("Wallet Error: engine send failed (%1).").arg(int(failed)));
		_sendState = SendState::Idle;
		if (!weak) {
			return;
		}
		syncEngineClient();
		if (weak && listed) {
			_historyUpdates.fire({});
		}
		if (!weak) {
			return;
		} else if (failed == SendError::KeyMismatch) {
			settleKeyMismatch(identity, signingKey, done);
		} else if (done) {
			done(failed);
		}
	});
	_sendState = SendState::Sending;
}

bool Session::submissionCurrent(
		const std::string &operationId,
		const std::shared_ptr<const PreparedSend> &prepared) const {
	return _submission
		&& prepared
		&& (_submission->operationId == operationId)
		&& (_submission->prepared == prepared)
		&& transferOperationCurrent(
			prepared->identity,
			prepared->generation,
			prepared->client);
}

void Session::submitTransfer(
		std::string operationId,
		std::shared_ptr<const PreparedSend> prepared,
		TransferSubmissionData data,
		Fn<void(TransferSubmissionAnswer)> done) {
	const auto weak = base::make_weak(_engine.get());
	const auto current = [=, this] {
		return weak && submissionCurrent(operationId, prepared);
	};
	const auto refuse = [=, this](SendError error, const QString &diagnostic) {
		if (current()) {
			_submission->refusal = error;
		}
		done({ TransferSubmissionOutcome::Rejected, diagnostic });
	};
	if (!current()) {
		LOG(("Wallet Error: transfer submission refused for a stale "
			"operation or wallet."));
		done({
			TransferSubmissionOutcome::Rejected,
			u"WALLET_TRANSFER_STALE"_q,
		});
		return;
	} else if (_submission->rpcStarted) {
		done({
			TransferSubmissionOutcome::Uncertain,
			u"WALLET_TRANSFER_ALREADY_SUBMITTED"_q,
		});
		return;
	} else if (_clientStopping
		|| !transferClientMatches(prepared->identity, prepared->client)) {
		refuse(
			SendError::SigningUnavailable,
			u"WALLET_TRANSFER_SIGNING_UNAVAILABLE"_q);
		return;
	}
	const auto terms = gaslessTerms();
	if (!current()) {
		refuse(SendError::QuoteExpired, u"WALLET_TRANSFER_STALE"_q);
		return;
	} else if (_clientStopping
		|| !transferClientMatches(prepared->identity, prepared->client)) {
		refuse(
			SendError::SigningUnavailable,
			u"WALLET_TRANSFER_SIGNING_UNAVAILABLE"_q);
		return;
	}
	const auto amount = prepared->args.amountNano;
	const auto ordinary = !prepared->tonConnect && !prepared->nft;
	if (ordinary
		&& TransferAmountBelowMinimum(amount, TransferMinNanos(_session))) {
		refuse(SendError::AmountTooSmall, u"WALLET_TRANSFER_AMOUNT_TOO_SMALL"_q);
		return;
	} else if ((ordinary
			&& (prepared->terms != terms
				|| terms.identity != prepared->identity
				|| _submission->paired != terms.eligible(
					amount,
					prepared->args.destination)))
		|| !_submission->normalFeeAuthorized
		|| _clientStopping) {
		refuse(SendError::QuoteExpired, u"WALLET_TRANSFER_QUOTE_EXPIRED"_q);
		return;
	} else if (data.gasless.has_value() != _submission->paired
		|| data.normal.isEmpty()
		|| data.normal.size() > kTransferDataMaxBytes
		|| (data.gasless
			&& (data.gasless->isEmpty()
				|| data.gasless->size() > kTransferDataMaxBytes))) {
		refuse(
			_submission->paired
				? SendError::QuoteExpired
				: SendError::DataInvalid,
			u"WALLET_TRANSFER_DATA_INVALID"_q);
		return;
	}
	const auto balance = _balanceNano.current();
	if (amount > balance) {
		refuse(SendError::InsufficientBalance, u"WALLET_TRANSFER_BALANCE_LOW"_q);
		return;
	} else if (!_submission->paired
		&& prepared->feeNano > balance - amount) {
		// A paired transfer's fee is the relayer's, so the balance is not
		// asked to cover it.
		refuse(SendError::InsufficientFees, u"WALLET_TRANSFER_FEES_LOW"_q);
		return;
	}
	const auto identity = prepared->identity;
	const auto record = submittedTransferRecord(operationId, identity);
	if (!record) {
		refuse(SendError::Failed, u"WALLET_TRANSFER_STORAGE_FAILED"_q);
		return;
	}
	const auto was = record->handoff;
	record->handoff = TransferHandoff::Possible;
	_submittedTransfersDirty = _submittedTransfersDirty
		|| (was != TransferHandoff::Possible);
	if (!persistSubmittedTransfers()) {
		if (const auto retained = submittedTransferRecord(operationId, identity)) {
			retained->handoff = was;
		}
		LOG(("Wallet Error: transfer handoff could not be stored."));
		refuse(SendError::Failed, u"WALLET_TRANSFER_STORAGE_FAILED"_q);
		return;
	} else if (const auto handoff = _submission->tonConnectHandoff) {
		if (!handoff(QString::fromLatin1(data.normal.toBase64()))) {
			LOG(("Wallet Error: TON Connect transfer could not be stored."));
			refuse(SendError::Failed, u"WALLET_TRANSFER_STORAGE_FAILED"_q);
			return;
		}
	}
	// The request id is not remembered on purpose. The broadcast must
	// reach the server, and the transport's automatic resend of a request
	// answered with a negative or 500-class code repeats the identical
	// body: a resent broadcast is at best redundant and at worst refused
	// for a message the first copy delivered, so every error reaches the
	// fail arm here instead. The answer must bind however late it lands,
	// because it is the only source of the receipt, so the host's timeout
	// never cancels it either; the sender's destructor is the one cancel,
	// and a request still queued at that moment is recovered by the
	// engine journal on the next launch.
	_submission->normal = data.normal;
	_submission->rpcStarted = true;
	const auto weakSession = base::make_weak(_session);
	auto randomId = base::RandomValue<uint64>();
	while (!randomId) {
		randomId = base::RandomValue<uint64>();
	}
	const auto messageId = prepared->nft
		? FullMsgId()
		: _transferMessages->create(prepared->args, randomId);
	_submission->draft = messageId;
	if (const auto report = _submission->started) {
		const auto started = SendStarted{
			.operationId = operationId,
			.message = messageId,
		};
		crl::on_main(_session, [=] { report(started); });
	}
	DEBUG_LOG(("Wallet Info: wallet.sendTransfer data_normal: %1"
		).arg(QString::fromLatin1(data.normal.toBase64())));
	if (data.gasless) {
		DEBUG_LOG(("Wallet Info: wallet.sendTransfer data_gasless: %1"
			).arg(QString::fromLatin1(data.gasless->toBase64())));
	}
	using Flag = MTPwallet_SendTransfer::Flag;
	_stateApi.request(MTPwallet_SendTransfer(
		MTP_flags(data.gasless ? Flag::f_data_gasless : Flag(0)),
		MTP_bytes(data.normal),
		data.gasless ? MTP_bytes(*data.gasless) : MTPbytes(),
		TransferRecipientInput(_session, prepared->args.userId),
		MTP_long(randomId)
	)).done([=, this](const MTPUpdates &result) {
		const auto account = weakSession;
		const auto finish = done;
		const auto sent = SentUpdateFromServer(result);
		const auto receipt = sent ? ReceiptFromServer(*sent) : std::nullopt;
		if (receipt) {
			DEBUG_LOG(("Wallet Info: wallet.sendTransfer accepted, gasless: %1, "
				"msg_hash: %2").arg(Logs::b(receipt->gasless)).arg(
					QString::fromLatin1(receipt->messageHash.toBase64())));
		}
		auto accepted = receipt.has_value();
		if (accepted && weak) {
			accepted = bindTransferReceipt(operationId, prepared, *sent);
		}
		if (account) {
			account->api().applyUpdates(result);
		}
		if (!accepted) {
			LOG(("Wallet Error: wallet.sendTransfer receipt unusable."));
			finish({
				TransferSubmissionOutcome::Uncertain,
				u"WALLET_TRANSFER_RECEIPT_INVALID"_q,
			});
			return;
		}
		finish({ TransferSubmissionOutcome::Accepted });
	}).fail([=, this](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.sendTransfer failed: %1"
			).arg(error.type()));
		const auto refusal = DefiniteTransferRefusal(error);
		if (!refusal) {
			done({ TransferSubmissionOutcome::Uncertain, error.type() });
			return;
		} else if (current()) {
			_submission->refusal = *refusal;
		}
		_transferMessages->failSending(messageId, error.type());
		done({ TransferSubmissionOutcome::Rejected, error.type() });
	}).handleAllErrors().send();
}

void Session::settleKeyMismatch(
		TransferWalletIdentity identity,
		QByteArray signingKey,
		Fn<void(SendError)> done) {
	requestState([=, this](const MTPWalletState &state) {
		applyState(state, false);
		const auto changed = (_address == identity.address)
			&& (_publicKey != signingKey)
			&& (deviceCustodyState().mode != DeviceMode::Full)
			&& !custody().current(_address, _publicKey);
		done(changed ? SendError::KeyChanged : SendError::KeyMismatch);
	}, [=] {
		done(SendError::KeyMismatch);
	});
}

bool Session::bindTransferReceipt(
		const std::string &operationId,
		const std::shared_ptr<const PreparedSend> &prepared,
		const MTPDupdateSentWalletTransaction &data) {
	if (!prepared || !transferOperationCurrent(
			prepared->identity,
			prepared->generation,
			prepared->client)) {
		return true;
	}
	const auto entry = submittedTransfer(operationId);
	if (!submissionCurrent(operationId, prepared)
		&& (!entry || entry->client.lock() != prepared->client)) {
		return true;
	}
	return applySubmittedUpdate(operationId, data);
}

bool Session::applySubmittedUpdate(
		const std::string &operationId,
		const MTPDupdateSentWalletTransaction &data) {
	const auto receipt = ReceiptFromServer(data);
	if (!receipt) {
		return false;
	}
	const auto weak = base::make_weak(_engine.get());
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	if (!identity) {
		return false;
	}
	auto changed = false;
	{
		const auto active = _submission
			&& submissionCurrent(operationId, _submission->prepared);
		auto entry = submittedTransfer(operationId);
		const auto record = submittedTransferRecord(operationId, *identity);
		const auto conflicts = [&](const QByteArray &hash) {
			return !hash.isEmpty() && hash != receipt->messageHash;
		};
		if ((active && entry
				&& entry->client.lock() != _submission->prepared->client)
			|| (record && record->handoff != TransferHandoff::Possible)
			|| (entry && entry->receipt
				&& conflicts(entry->receipt->messageHash))
			|| (active && _submission->receipt
				&& conflicts(_submission->receipt->messageHash))
			|| (record && record->messageHash
				&& conflicts(*record->messageHash))) {
			return false;
		}
		if (ranges::any_of(_submitted, [&](const auto &other) {
				return other.operationId != operationId
					&& other.generation == generation
					&& other.identity == *identity
					&& other.receipt
					&& other.receipt->messageHash == receipt->messageHash;
			})) {
			return false;
		}
		if (!entry && active) {
			entry = upsertSubmittedTransfer(
				operationId,
				*identity,
				generation,
				_submission->prepared->client);
			changed = (entry != nullptr);
		}
		if (!entry) {
			return false;
		}
		if (active
			&& (!_submission->receipt
				|| _submission->receipt->messageHash.isEmpty())) {
			_submission->receipt = receipt;
			_lastReceipt = receipt;
			changed = true;
		}
		if (record && (!record->messageHash || record->messageHash->isEmpty())) {
			record->messageHash = receipt->messageHash;
			_submittedTransfersDirty = true;
			changed = true;
		}
		if ((!entry->receipt || entry->receipt->messageHash.isEmpty())
			&& (entry->canonicalId.isEmpty() || entry->item)) {
			entry->receipt = receipt;
			changed = true;
		}
		if (const auto transaction = data.vtransaction()) {
			changed = !entry->lookupStopped || changed;
			entry->lookupStopped = true;
			auto items = std::vector{
				HistoryItemFromServer(*transaction, identity),
			};
			rememberCollectibles(items);
			changed = adoptSubmittedTransaction(
				operationId,
				std::move(items.front())) || changed;
		}
	}
	if (!persistSubmittedTransfers()) {
		LOG(("Wallet Error: received transfer facts remain dirty."));
	}
	dropSubmittedIfListed();
	if (!changed) {
		return true;
	}
	_historyUpdates.fire({});
	if (!weak
		|| generation != _networkGeneration
		|| !transferWalletIdentityCurrent(*identity)) {
		return true;
	}
	updateListsGate();
	if (weak
		&& generation == _networkGeneration
		&& transferWalletIdentityCurrent(*identity)) {
		startSubmittedLookup();
	}
	return true;
}

bool Session::transferOperationCurrent(
		const TransferWalletIdentity &identity,
		int generation,
		const std::shared_ptr<engine::WalletClient> &client) const {
	return generation == _networkGeneration
		&& transferWalletIdentityCurrent(identity)
		&& client
		&& client == _engine->client()
		&& !_clientStopping;
}

Session::SubmittedTransfer *Session::submittedTransfer(
		const std::string &operationId) {
	if (operationId.empty()) {
		return nullptr;
	}
	const auto i = ranges::find(
		_submitted,
		operationId,
		&SubmittedTransfer::operationId);
	return (i != end(_submitted)
		&& i->generation == _networkGeneration
		&& transferWalletIdentityCurrent(i->identity))
		? &*i
		: nullptr;
}

SubmittedTransferStore &Session::submittedTransferStore() {
	if (!_submittedTransferStore) {
		_submittedTransferStore = ReadSubmittedTransferStore(_session->local());
		if (!_submittedTransferStore) {
			LOG(("Wallet Error: submitted transfer store unreadable."));
			_submittedTransferStore = SubmittedTransferStore();
		}
	}
	return *_submittedTransferStore;
}

SubmittedTransferRecord *Session::submittedTransferRecord(
		const std::string &operationId,
		const TransferWalletIdentity &identity) {
	const auto custodyRecord = custody().current(
		identity.address,
		identity.publicKey);
	if (!custodyRecord) {
		return nullptr;
	}
	auto &records = submittedTransferStore().records;
	const auto found = ranges::find_if(records, [&](const auto &record) {
		return record.network == custodyRecord->network
			&& record.address == identity.address
			&& record.publicKey == identity.publicKey
			&& record.operationId == operationId;
	});
	return (found != end(records)) ? &*found : nullptr;
}

bool Session::persistSubmittedTransfers() {
	auto &store = submittedTransferStore();
	for (const auto &entry : _submitted) {
		if (entry.generation != _networkGeneration
			|| !transferWalletIdentityCurrent(entry.identity)) {
			continue;
		}
		const auto record = submittedTransferRecord(
			entry.operationId,
			entry.identity);
		if (!record) {
			continue;
		} else if (!entry.canonicalId.isEmpty() && !entry.item) {
			retireSubmittedTransferRecord(entry.operationId, entry.identity);
			continue;
		}
		const auto was = *record;
		if (entry.receipt && !record->messageHash) {
			record->messageHash = entry.receipt->messageHash;
		}
		if (entry.terminal) {
			record->terminal = StoredTransferTerminal(*entry.terminal);
		}
		record->confirmedHash = entry.confirmedHash;
		record->lookupAttempts = entry.lookupAttempts;
		record->lookupStopped = entry.lookupStopped;
		if (entry.item) {
			record->served = StoredTransferProjection(*entry.item);
		}
		_submittedTransfersDirty = _submittedTransfersDirty || (*record != was);
	}
	if (!_submittedTransfersDirty) {
		return true;
	}
	auto pruned = store;
	auto size = SubmittedTransferStoreSize(pruned);
	if (!size) {
		return false;
	}
	const auto now = base::unixtime::now();
	while (pruned.records.size() > kSubmittedTransferMaxRecords
		|| *size > kSubmittedTransferMaxBytes) {
		auto oldest = end(pruned.records);
		for (auto i = begin(pruned.records); i != end(pruned.records); ++i) {
			const auto live = submittedTransferRecord(
				i->operationId,
				TransferWalletIdentity{
					.address = i->address,
					.publicKey = i->publicKey,
				});
			const auto current = live
				&& live->recordId == i->recordId
				&& live->network == i->network
				&& ((_sendUnresolved
						&& _unresolvedOperationId == i->operationId)
					|| (_submission
						&& _submission->operationId == i->operationId));
			if (current
				|| (!i->served
					&& i->terminal == TransferTerminal::None
					&& !StaleSubmittedRecord(*i, now))) {
				continue;
			}
			if (oldest == end(pruned.records) || i->posted < oldest->posted) {
				oldest = i;
			}
		}
		if (oldest == end(pruned.records)) {
			return false;
		}
		pruned.records.erase(oldest);
		size = SubmittedTransferStoreSize(pruned);
		if (!size) {
			return false;
		}
	}
	if (!WriteSubmittedTransferStore(_session->local(), pruned)) {
		return false;
	}
	store = std::move(pruned);
	auto hidden = false;
	for (auto &entry : _submitted) {
		if (submittedTransferRecord(entry.operationId, entry.identity)) {
			continue;
		}
		entry.lookupStopped = true;
		if (!entry.terminal && !entry.item && entry.canonicalId.isEmpty()) {
			entry.fallback.reset();
			hidden = true;
		}
	}
	_submittedTransfersDirty = false;
	if (hidden) {
		_historyUpdates.fire({});
	}
	return true;
}

void Session::retireSubmittedTransferRecord(
		const std::string &operationId,
		const TransferWalletIdentity &identity) {
	if (const auto record = submittedTransferRecord(operationId, identity)) {
		auto &records = submittedTransferStore().records;
		records.erase(begin(records) + (record - records.data()));
		_submittedTransfersDirty = true;
	}
}

Session::SubmittedTransfer *Session::upsertSubmittedTransfer(
		const std::string &operationId,
		const TransferWalletIdentity &identity,
		int generation,
		const std::shared_ptr<engine::WalletClient> &client) {
	if (const auto entry = submittedTransfer(operationId)) {
		return (entry->client.lock() == client) ? entry : nullptr;
	}
	const auto record = submittedTransferRecord(operationId, identity);
	if (!record || record->handoff != TransferHandoff::Possible) {
		return nullptr;
	}
	if (_submitted.size() >= kSubmittedTransferMaxRecords) {
		_submitted.erase(ranges::remove_if(_submitted, [&](const auto &entry) {
			const auto active = (_submission
					&& _submission->operationId == entry.operationId)
				|| (_sendUnresolved
					&& _unresolvedOperationId == entry.operationId);
			return !active
				&& ((!entry.fallback && !entry.item)
					|| ((entry.terminal || entry.item)
						&& !submittedTransferRecord(
							entry.operationId,
							entry.identity)));
		}), end(_submitted));
	}
	if (_submitted.size() >= kSubmittedTransferMaxRecords) {
		return nullptr;
	}
	auto fallback = std::make_unique<TransferItem>(ItemFromPending({
		.operationId = operationId,
		.walletIdentity = identity,
		.posted = record->posted,
		.amountNano = record->amountNano,
		.destination = record->destination,
		.collectible = record->collectible,
		.comment = record->comment,
		.recipient = record->recipient,
		.bounce = record->bounce,
	}));
	if (FailedTransferTerminal(record->terminal)) {
		fallback->status = TransferItem::Status::Failure;
	}
	auto item = std::unique_ptr<TransferItem>();
	if (record->served) {
		const auto &stored = *record->served;
		item = std::make_unique<TransferItem>();
		item->source = TransferItem::Source::Server;
		item->id = stored.id;
		item->walletIdentity = identity;
		item->kind = !stored.collectible.isEmpty()
			? TransferItem::Kind::Collectible
			: stored.peerTransfer
			? TransferItem::Kind::PeerTransfer
			: TransferItem::Kind::Transfer;
		item->counterparty = stored.counterparty;
		item->counterpartyBounceable = stored.counterpartyBounceable;
		item->counterpartyName = stored.counterpartyName;
		item->counterpartyPeer = stored.counterpartyPeer;
		item->collectible = stored.collectible;
		item->amountNano = stored.amountNano;
		item->feeNano = stored.feeNano;
		item->gasless = stored.gasless;
		item->comment = stored.comment;
		item->commentEncrypted = stored.commentEncrypted;
		item->date = stored.date;
		item->status = stored.failed
			? TransferItem::Status::Failure
			: TransferItem::Status::Success;
		fallback.reset();
	}
	auto receipt = std::optional<TransferReceipt>();
	if (record->messageHash) {
		receipt = TransferReceipt{ .messageHash = *record->messageHash };
	}
	const auto canonicalId = item ? item->id : QString();
	_submitted.push_back(SubmittedTransfer{
		.operationId = operationId,
		.identity = identity,
		.client = (record->recordId == _clientRecordId) ? client : nullptr,
		.fallback = std::move(fallback),
		.item = std::move(item),
		.canonicalId = canonicalId,
		.collectible = record->collectible,
		.confirmedHash = record->confirmedHash,
		.receipt = std::move(receipt),
		.terminal = RestoredTransferTerminal(record->terminal),
		.generation = generation,
		.lookupAttempts = record->lookupAttempts,
		.lookupStopped = record->lookupStopped,
		.paired = record->paired,
	});
	return &_submitted.back();
}

bool Session::submittedLookupNeeded() const {
	return _lookup || ranges::any_of(_submitted, [&](const auto &entry) {
		return entry.receipt
			&& !entry.lookupStopped
			&& entry.lookupAttempts < kSubmittedLookupAttempts
			&& entry.canonicalId.isEmpty()
			&& entry.generation == _networkGeneration
			&& transferWalletIdentityCurrent(entry.identity);
	});
}

bool Session::submittedLookupCurrent(
		const std::shared_ptr<SubmittedLookup> &request) const {
	if (_lookup != request
		|| request->generation != _networkGeneration
		|| !transferWalletIdentityCurrent(request->identity)) {
		return false;
	}
	const auto i = ranges::find(
		_submitted,
		request->operationId,
		&SubmittedTransfer::operationId);
	return i != end(_submitted)
		&& i->identity == request->identity
		&& i->generation == request->generation
		&& i->canonicalId.isEmpty()
		&& i->receipt
		&& i->receipt->messageHash == request->messageHash;
}

void Session::startSubmittedLookup() {
	updatePollingState();
}

void Session::lookupSubmittedTransaction() {
	if (_lookup || _submitted.empty()) {
		return;
	}
	const auto last = ranges::find(
		_submitted,
		_lastLookupOperationId,
		&SubmittedTransfer::operationId);
	const auto start = (last == end(_submitted))
		? size_t(0)
		: size_t(last - begin(_submitted) + 1);
	for (auto i = size_t(0); i != _submitted.size(); ++i) {
		auto &entry = _submitted[(start + i) % _submitted.size()];
		if (!entry.receipt
			|| entry.lookupStopped
			|| !entry.canonicalId.isEmpty()
			|| entry.lookupAttempts >= kSubmittedLookupAttempts
			|| entry.generation != _networkGeneration
			|| !transferWalletIdentityCurrent(entry.identity)) {
			continue;
		}
		if (!submittedTransferRecord(entry.operationId, entry.identity)) {
			continue;
		}
		++entry.lookupAttempts;
		entry.lookupStopped = (entry.lookupAttempts >= kSubmittedLookupAttempts);
		const auto request = std::make_shared<SubmittedLookup>(SubmittedLookup{
			.operationId = entry.operationId,
			.identity = entry.identity,
			.messageHash = entry.receipt->messageHash,
			.generation = entry.generation,
		});
		_lastLookupOperationId = entry.operationId;
		if (!persistSubmittedTransfers()) {
			--entry.lookupAttempts;
			entry.lookupStopped = false;
			if (const auto record = submittedTransferRecord(
					entry.operationId,
					entry.identity)) {
				record->lookupAttempts = entry.lookupAttempts;
				record->lookupStopped = entry.lookupStopped;
			}
			LOG(("Wallet Error: transfer lookup budget could not be stored."));
			return;
		} else if (!submittedTransferRecord(
				request->operationId,
				request->identity)) {
			return;
		}
		_lookup = request;
		// The token is the server's own opaque message hash, echoed exactly
		// as it arrived: MTP_string(const std::string &) copies the bytes
		// verbatim, so no encoding is imposed on a value whose contract
		// states none. MTP_string(const QString &) would re-encode through
		// QString::toUtf8(), which is why the QByteArray overload is deleted;
		// neither is used here.
		request->id = _stateApi.request(MTPwallet_GetTransactionsByMsgHash(
			MTP_vector<MTPstring>(
				1,
				MTP_string(request->messageHash.toStdString()))
		)).done([=](const MTPwallet_Transactions &result) {
			if (_lookup != request) {
				return;
			}
			const auto weak = base::make_weak(_engine.get());
			if (submittedLookupCurrent(request)) {
				applySubmittedLookup(result, request);
			}
			if (!weak || _lookup != request) {
				return;
			}
			_lookup = nullptr;
			updatePollingState();
		}).fail([=](const MTP::Error &error) {
			if (_lookup != request) {
				return;
			}
			const auto current = submittedLookupCurrent(request);
			_lookup = nullptr;
			if (current) {
				LOG(("Wallet Error: wallet.getTransactionsByMsgHash failed: %1"
					).arg(error.type()));
			}
			updatePollingState();
		}).handleAllErrors().send();
		return;
	}
}

void Session::applySubmittedLookup(
		const MTPwallet_Transactions &result,
		const std::shared_ptr<SubmittedLookup> &request) {
	const auto weak = base::make_weak(_engine.get());
	const auto &data = result.data();
	_session->data().processUsers(data.vusers());
	if (!weak || !submittedLookupCurrent(request)) {
		return;
	}
	_session->data().processChats(data.vchats());
	if (!weak || !submittedLookupCurrent(request)) {
		return;
	}
	// The answer's balance and next_offset are read by neither this lane
	// nor applyTransactions(): the state lane and the engine refresh are
	// the balance authority, and a by-message answer is not the paged
	// feed, so its offset would page a list that nobody renders.
	auto loaded = HistoryFromServer(data.vtransactions().v, request->identity);
	rememberCollectibles(loaded);
	// wallet.transactions echoes neither the requested message hash nor
	// any per-row link to it, so the attribution is made by the request:
	// one hash per lookup, and the whole answer belongs to it. Within the
	// answer only a record this wallet signed as an ordinary transfer can
	// be the submitted operation - a self-transfer also returns the
	// incoming half, and HistoryItemFromServer marks every key change
	// outgoing whatever the server's bit says - and only a record that
	// names itself, because the identity this lane needs is the server's
	// own transaction id and an empty string is not one. Two candidates
	// naming different transactions cannot be told apart, and a message's
	// records only grow, so no later answer would resolve it: the lane
	// stops with nothing attached, the head page still brings the real
	// row in, no payment is declared failed and no second send is freed.
	const auto candidate = [](const TransferItem &item) {
		return !item.incoming
			&& !item.id.isEmpty()
			&& (item.kind != TransferItem::Kind::KeyChange);
	};
	const auto found = ranges::find_if(loaded, candidate);
	if (found == end(loaded)) {
		return;
	}
	const auto id = found->id;
	const auto ambiguous = ranges::any_of(loaded, [&](const auto &item) {
		return candidate(item) && item.id != id;
	});
	if (ambiguous) {
		if (const auto entry = submittedTransfer(request->operationId)) {
			entry->lookupStopped = true;
		}
		LOG(("Wallet Error: wallet.getTransactionsByMsgHash sent "
			"ambiguous transaction identity."));
		if (!persistSubmittedTransfers()) {
			LOG(("Wallet Error: stopped transfer lookup remains dirty."));
		}
		return;
	}
	const auto changed = adoptSubmittedTransaction(
		request->operationId,
		std::move(*found));
	dropSubmittedIfListed();
	if (!changed) {
		return;
	}
	_historyUpdates.fire({});
	if (!weak
		|| request->generation != _networkGeneration
		|| !transferWalletIdentityCurrent(request->identity)) {
		return;
	}
	updateListsGate();
	if (weak
		&& request->generation == _networkGeneration
		&& transferWalletIdentityCurrent(request->identity)) {
		updatePollingState();
	}
}

bool Session::adoptSubmittedTransaction(
		const std::string &operationId,
		TransferItem item) {
	const auto entry = submittedTransfer(operationId);
	if (!entry || item.walletIdentity != entry->identity) {
		return false;
	}
	auto changed = !entry->lookupStopped;
	entry->lookupStopped = true;
	if (_lookup
		&& _lookup->operationId == operationId
		&& _lookup->identity == entry->identity
		&& _lookup->generation == entry->generation) {
		dropSubmittedLookup();
		changed = true;
	}
	const auto conflict = item.id.isEmpty()
		|| (!entry->canonicalId.isEmpty() && entry->canonicalId != item.id)
		|| ranges::any_of(_submitted, [&](const auto &other) {
			return other.operationId != operationId
				&& other.generation == entry->generation
				&& other.identity == entry->identity
				&& other.canonicalId == item.id;
		});
	if (conflict) {
		LOG(("Wallet Error: sent transfer has unusable or conflicting "
			"transaction identity."));
	} else if (entry->canonicalId.isEmpty()) {
		if (const auto record = submittedTransferRecord(
				operationId,
				entry->identity)) {
			KeepSubmittedRecipient(item, *record);
		}
		entry->canonicalId = item.id;
		entry->item = std::make_unique<TransferItem>(std::move(item));
		entry->fallback.reset();
		entry->confirmedHash.clear();
		changed = true;
	}
	if (!persistSubmittedTransfers()) {
		LOG(("Wallet Error: canonical transfer facts remain dirty."));
	}
	return changed;
}

void Session::dropSubmittedIfListed() {
	auto changed = false;
	auto leaving = false;
	for (auto &entry : _submitted) {
		if (entry.generation != _networkGeneration
			|| !transferWalletIdentityCurrent(entry.identity)
			|| (!entry.fallback && !entry.item)) {
			continue;
		}
		// WHY: the engine confirms only the wallet's own message; the item
		// changes owner in a later transaction and wallet.getNfts follows the
		// chain with a lag, so the forced cadence runs until the item is gone.
		if (!entry.leaving
			&& !entry.collectible.isEmpty()
			&& ((entry.terminal == engine::SendPhase::kConfirmed)
				|| (entry.item
					&& !entry.item->incoming
					&& (entry.item->status
						== TransferItem::Status::Success)))) {
			entry.leaving = true;
			followCollectible(entry.collectible, false);
			leaving = true;
		}
		if (entry.canonicalId.isEmpty() && !entry.confirmedHash.isEmpty()) {
			const auto candidate = [&](const TransferItem &item) {
				return !item.incoming
					&& item.kind != TransferItem::Kind::KeyChange
					&& !item.id.isEmpty()
					&& item.walletIdentity == entry.identity
					&& item.traceId == entry.confirmedHash;
			};
			const auto found = ranges::find_if(_history, candidate);
			if (found != end(_history)) {
				const auto ambiguous = ranges::any_of(
					_history,
					[&](const auto &item) {
						return candidate(item) && item.id != found->id;
					}) || ranges::any_of(_submitted, [&](const auto &other) {
						return other.operationId != entry.operationId
							&& (other.confirmedHash == entry.confirmedHash
								|| other.canonicalId == found->id);
					});
				if (!ambiguous) {
					entry.canonicalId = found->id;
				}
			}
		}
		if (entry.canonicalId.isEmpty()
			|| !ranges::contains(
				_history,
				entry.canonicalId,
				&TransferItem::id)) {
			continue;
		}
		changed = true;
		entry.fallback.reset();
		entry.item.reset();
		entry.receipt.reset();
		entry.confirmedHash.clear();
		entry.lookupStopped = true;
		if (_lookup && _lookup->operationId == entry.operationId) {
			dropSubmittedLookup();
		}
	}
	if ((changed || _submittedTransfersDirty) && !persistSubmittedTransfers()) {
		LOG(("Wallet Error: reconciled transfer facts remain dirty."));
	}
	if (leaving) {
		refreshCollectibles(true);
	}
}

void Session::dropSubmittedLookup() {
	if (const auto request = base::take(_lookup)) {
		_stateApi.request(request->id).cancel();
	}
}

void Session::SettleTonConnect(
		TransferSubmissionState &submission,
		SendError error) {
	if (auto report = base::take(submission.tonConnect)) {
		report({
			submission.rpcStarted
				? QString::fromLatin1(submission.normal.toBase64())
				: QString(),
			error,
		});
	}
}

void Session::retireSubmission() {
	auto retired = base::take(_submission);
	if (!retired) {
		return;
	}
	SettleTonConnect(*retired, SendError::Failed);
	if (retired->rpcStarted) {
		_transferMessages->dropSending(retired->draft);
	}
}

void Session::clearSubmittedTransfers() {
	dropSubmittedLookup();
	_submitted.clear();
	_pending.reset();
	retireSubmission();
	_lastReceipt.reset();
	_lastLookupOperationId.clear();
	_unresolvedOperationId.clear();
	_windowSend.clear();
	_sendUnresolved = false;
	_sendRecoveryReady = false;
	++_sendRevision;
	updateSigningReady();
}

bool Session::sendRecoveryNeeded() const {
	return !_sendRecoveryReady
		&& !_clientStopping
		&& signingClient()
		&& transferWalletIdentity().has_value();
}

// WHY: past valid_until the contract refuses the signed message, so a row
// still pending then can never execute. The verdict is inferred, so it
// waits for the journal: a guess here would outrank the real answer.
void Session::expireStaleSubmittedTransfers() {
	const auto identity = transferWalletIdentity();
	if (!_sendRecoveryReady || !identity) {
		return;
	}
	const auto now = base::unixtime::now();
	auto changed = false;
	for (auto &record : submittedTransferStore().records) {
		if (record.terminal != TransferTerminal::None
			|| record.served
			|| record.handoff != TransferHandoff::Possible
			|| record.recordId != _clientRecordId
			|| submittedTransferRecord(record.operationId, *identity) != &record
			|| !StaleSubmittedRecord(record, now)
			|| record.operationId == _unresolvedOperationId
			|| (_submission
				&& _submission->operationId == record.operationId)) {
			continue;
		}
		record.terminal = TransferTerminal::Expired;
		changed = true;
		if (const auto entry = submittedTransfer(record.operationId)) {
			if (!entry->terminal) {
				entry->terminal = engine::SendPhase::kExpired;
			}
			if (entry->fallback) {
				entry->fallback->status = TransferItem::Status::Failure;
			}
		}
	}
	if (!changed) {
		return;
	}
	_submittedTransfersDirty = true;
	if (!persistSubmittedTransfers()) {
		LOG(("Wallet Error: expired transfer facts remain dirty."));
	}
	_historyUpdates.fire({});
}

void Session::dropForeignSubmittedTransfers(
		const TransferWalletIdentity &identity) {
	const auto custodyRecord = custody().current(
		identity.address,
		identity.publicKey);
	if (!custodyRecord || _clientRecordId.isEmpty()) {
		return;
	}
	const auto foreign = [&](const SubmittedTransferRecord &record) {
		return record.recordId != _clientRecordId
			&& record.network == custodyRecord->network
			&& record.address == identity.address
			&& record.publicKey == identity.publicKey;
	};
	auto &records = submittedTransferStore().records;
	auto dropped = std::vector<std::string>();
	for (const auto &record : records) {
		if (foreign(record)) {
			dropped.push_back(record.operationId);
		}
	}
	if (dropped.empty()) {
		return;
	}
	records.erase(ranges::remove_if(records, foreign), end(records));
	_submitted.erase(ranges::remove_if(_submitted, [&](const auto &entry) {
		return entry.identity.address == identity.address
			&& entry.identity.publicKey == identity.publicKey
			&& ranges::contains(dropped, entry.operationId);
	}), end(_submitted));
	_submittedTransfersDirty = true;
	if (!persistSubmittedTransfers()) {
		LOG(("Wallet Error: dropped foreign transfer facts remain dirty."));
	}
	_historyUpdates.fire({});
	updateListsGate();
}

void Session::restoreSubmittedTransfers() {
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	const auto client = signingClient();
	if (!identity || !transferOperationCurrent(*identity, generation, client)) {
		return;
	}
	dropForeignSubmittedTransfers(*identity);
	auto changed = false;
	for (const auto &record : submittedTransferStore().records) {
		if (record.handoff != TransferHandoff::Possible
			|| (_submission && _submission->operationId == record.operationId)
			|| submittedTransfer(record.operationId)
			|| submittedTransferRecord(
				record.operationId,
				*identity) != &record) {
			continue;
		}
		if (upsertSubmittedTransfer(
				record.operationId,
				*identity,
				generation,
				client)) {
			changed = true;
		}
	}
	dropSubmittedIfListed();
	if (!changed) {
		return;
	}
	const auto weak = base::make_weak(_engine.get());
	_historyUpdates.fire({});
	if (weak && transferOperationCurrent(*identity, generation, client)) {
		updateListsGate();
	}
}

void Session::resolvePending() {
	const auto identity = transferWalletIdentity();
	const auto client = signingClient();
	const auto generation = _networkGeneration;
	if (_resolveRequestPending
		|| !identity
		|| !transferOperationCurrent(*identity, generation, client)) {
		return;
	}
	_resolveRequestPending = true;
	const auto sendRevision = _sendRevision;
	_engine->run([client] {
		return client->resolve_pending();
	}, [=, this](engine::SendSnapshot snapshot) {
		const auto weak = base::make_weak(_engine.get());
		const auto current = [=] {
			return weak && transferOperationCurrent(
				*identity,
				generation,
				client);
		};
		if (!current()) {
			_resolveRequestPending = false;
			return;
		}
		const auto hadPending = bool(_pending);
		restoreSubmittedTransfers();
		if (!current()) {
			if (weak) {
				_resolveRequestPending = false;
			}
			return;
		}
		applySendSnapshot(snapshot, true, sendRevision);
		if (!current()) {
			if (weak) {
				_resolveRequestPending = false;
			}
			return;
		}
		applyRotationSnapshot(snapshot, true);
		if (!weak) {
			return;
		}
		_resolveRequestPending = false;
		if (!current()) {
			return;
		}
		if (sendRevision == _sendRevision) {
			_sendRecoveryReady = true;
			updateSigningReady();
			expireStaleSubmittedTransfers();
		}
		if (hadPending && !_pending) {
			requestEngineRefresh();
		}
		updatePollingState();
	}, [=, this](EngineError error) {
		_resolveRequestPending = false;
		if (!transferOperationCurrent(*identity, generation, client)) {
			return;
		}
		LOG(("Wallet Error: engine resolve_pending failed: %1, "
			"keeping last-good state.").arg(error.message));
	});
}

void Session::finishPending() {
	LOG(("Wallet: pending send resolved."));
	_pending.reset();
	_submission.reset();
	_sendUnresolved = false;
	_unresolvedOperationId.clear();
	const auto weak = base::make_weak(_engine.get());
	// Waiters follow this even when an unresolved send settles while Idle.
	_sendState.force_assign(SendState::Idle);
	if (!weak) {
		return;
	}
	syncEngineClient();
}

void Session::applySendSnapshot(
		const engine::SendSnapshot &snapshot,
		bool journalAuthoritative,
		uint64 sendRevision) {
	const auto weak = base::make_weak(_engine.get());
	const auto identity = transferWalletIdentity();
	const auto generation = _networkGeneration;
	const auto client = signingClient();
	const auto current = [=] {
		return weak && identity && transferOperationCurrent(
			*identity,
			generation,
			client);
	};
	if (!current()) {
		return;
	}
	const auto operationId = snapshot.operation_id.value_or(std::string());
	auto changed = false;
	if (journalAuthoritative
		&& sendRevision == _sendRevision
		&& !_submission
		&& _sendState.current() != SendState::Sending) {
		const auto record = submittedTransferRecord(operationId, *identity);
		if (record
			&& record->recordId == _clientRecordId
			&& record->handoff == TransferHandoff::Preparation
			&& snapshot.phase != engine::SendPhase::kIdle) {
			record->handoff = TransferHandoff::Possible;
			_submittedTransfersDirty = true;
			if (upsertSubmittedTransfer(
					operationId,
					*identity,
					generation,
					client)) {
				changed = true;
			}
		}
		const auto custodyRecord = custody().current(
			identity->address,
			identity->publicKey);
		if (custodyRecord && custodyRecord->recordId == _clientRecordId) {
			auto &records = submittedTransferStore().records;
			const auto size = records.size();
			records.erase(ranges::remove_if(records, [&](const auto &record) {
				return record.recordId == custodyRecord->recordId
					&& record.network == custodyRecord->network
					&& record.address == identity->address
					&& record.publicKey == identity->publicKey
					&& record.handoff == TransferHandoff::Preparation
					&& (record.operationId != operationId
						|| snapshot.phase == engine::SendPhase::kIdle);
			}), end(records));
			_submittedTransfersDirty = _submittedTransfersDirty
				|| records.size() != size;
		}
	}
	const auto found = submittedTransfer(operationId);
	const auto entry = (found && found->client.lock() == client)
		? found
		: nullptr;
	const auto terminal = snapshot.phase != engine::SendPhase::kIdle
		&& TerminalSendPhase(snapshot.phase);
	// Only the clock writes kExpired ahead of the journal, which outranks it.
	const auto phase = entry
		? PairedSendPhase(snapshot.phase, entry->paired)
		: snapshot.phase;
	const auto inferred = entry
		&& (entry->terminal == engine::SendPhase::kExpired)
		&& (phase != engine::SendPhase::kExpired);
	if (entry && terminal && (!entry->terminal || inferred)) {
		// The journal holds the normal delivery form of a paired send, so
		// the server executing the fee-free alternative instead advances the
		// sequence number without that exact message ever landing. The
		// engine reads its own message as replaced; for a send that offered
		// the alternative this is the ordinary outcome of the offer, and the
		// receipt's message hash names the transaction that did execute.
		entry->terminal = phase;
		changed = true;
		switch (phase) {
		case engine::SendPhase::kConfirmed:
		case engine::SendPhase::kSequenceNumberConsumed:
		case engine::SendPhase::kSuperseded:
			if (inferred && entry->fallback) {
				entry->fallback->status = TransferItem::Status::Pending;
			}
			break;
		default:
			if (entry->fallback) {
				entry->fallback->status = TransferItem::Status::Failure;
			}
			break;
		}
	}
	if (entry
		&& entry->terminal == engine::SendPhase::kConfirmed
		&& snapshot.phase == engine::SendPhase::kConfirmed
		&& entry->canonicalId.isEmpty()
		&& entry->confirmedHash.isEmpty()
		&& snapshot.resolution
		&& snapshot.resolution->transaction_hash) {
		const auto encoded = QByteArray::fromStdString(
			*snapshot.resolution->transaction_hash);
		auto decoded = QByteArray::fromBase64Encoding(
			encoded,
			QByteArray::AbortOnBase64DecodingErrors);
		if (decoded
			&& decoded.decoded.size() == 32
			&& decoded.decoded.toBase64() == encoded) {
			entry->confirmedHash = std::move(decoded.decoded);
			changed = true;
		}
	}
	const auto local = _pending
		&& _submission
		&& _pending->operationId == operationId
		&& _pending->walletIdentity == *identity
		&& submissionCurrent(operationId, _submission->prepared);
	auto settled = local && terminal;
	if (!_pending
		&& !_submission
		&& _sendState.current() == SendState::Idle
		&& sendRevision == _sendRevision) {
		if (!TerminalSendPhase(snapshot.phase)) {
			if (!operationId.empty()
				&& (!_sendUnresolved
					|| _unresolvedOperationId.empty()
					|| _unresolvedOperationId == operationId)) {
				_sendUnresolved = true;
				_unresolvedOperationId = operationId;
			}
		} else if (_sendUnresolved) {
			settled = (terminal
				&& !operationId.empty()
				&& _unresolvedOperationId == operationId)
				|| journalAuthoritative;
		}
	}
	_submittedTransfersDirty = _submittedTransfersDirty || changed;
	dropSubmittedIfListed();
	if (settled) {
		if (_submission
			&& _submission->rpcStarted
			&& FailedTransferTerminal(StoredTransferTerminal(
				PairedSendPhase(snapshot.phase, _submission->paired)))) {
			_transferMessages->failSending(
				_submission->draft,
				TransferTerminalCode(StoredTransferTerminal(
					PairedSendPhase(snapshot.phase, _submission->paired))));
		}
		finishPending();
	} else if (_sendUnresolved) {
		retireCommentScopes();
	}
	if (!current()) {
		return;
	}
	if (changed) {
		_historyUpdates.fire({});
		if (!current()) {
			return;
		}
		updateListsGate();
		if (!current()) {
			return;
		}
	}
	updatePollingState();
}

void Session::applyRotationSnapshot(
		const engine::SendSnapshot &snapshot,
		bool journalAuthoritative) {
	if (!custody().pendingRotation) {
		return;
	}
	const auto operationId = snapshot.operation_id
		? QString::fromStdString(*snapshot.operation_id)
		: QString();
	if (operationId != custody().pendingRotation->operationId) {
		// A journal naming nothing for the pending is conclusive only from
		// a successful standalone resolve_pending(): refresh() swallows the
		// failure of its own embedded resolve (refresh.rs) and still
		// reports kCompleted with the client's fresh, empty send snapshot,
		// so an update's kIdle is not journal-derived, and the result of
		// send_boc speaks for its own submission only. Even the standalone
		// answer waits for no submit to be in flight: a resolve_pending
		// queued on the serial worker before the store write answers after
		// it, while the send_boc behind it is still queued, so with
		// _rotating set the result of send_boc is the authority. Without it
		// (after a restart) the journal is, and it re-reports even a
		// terminal record with its operation id, so an empty or foreign one
		// means the broadcast never reached it.
		if (journalAuthoritative && !_rotating) {
			LOG(("Wallet Error: pending rotation has no journal record."));
			discardPendingRotation();
			finishRotation(u"ROTATION_FAILED"_q);
		}
		return;
	}
	switch (snapshot.phase) {
	case engine::SendPhase::kConfirmed:
		promotePendingRotation();
		finishRotation(QString());
		return;
	case engine::SendPhase::kReplaced:
		discardPendingRotation();
		finishRotation(u"ROTATION_REPLACED"_q);
		return;
	case engine::SendPhase::kExpired:
		discardPendingRotation();
		finishRotation(u"ROTATION_EXPIRED"_q);
		return;
	case engine::SendPhase::kFailed:
	case engine::SendPhase::kCancelled:
	case engine::SendPhase::kSuperseded:
	case engine::SendPhase::kSequenceNumberConsumed:
		LOG(("Wallet Error: rotation ended in send phase %1."
			).arg(int(snapshot.phase)));
		discardPendingRotation();
		finishRotation(u"ROTATION_FAILED"_q);
		return;
	case engine::SendPhase::kIdle:
	case engine::SendPhase::kValidating:
	case engine::SendPhase::kAuthorizing:
	case engine::SendPhase::kPreparing:
	case engine::SendPhase::kPersisting:
	case engine::SendPhase::kReadyToSubmit:
	case engine::SendPhase::kSubmitting:
	case engine::SendPhase::kSubmissionUnknown:
	case engine::SendPhase::kSubmitted:
	case engine::SendPhase::kHandedOff:
		return;
	}
}

void Session::storePendingRotation(
		KeyAuthorization auth,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	const auto active = currentRecord();
	if (!active) {
		LOG(("Wallet Error: rotation stored without local custody."));
		fail(u"ROTATION_NO_CUSTODY"_q);
		return;
	}
	// A store, so the seam requires a live grant and the retention window
	// alone is never enough; it also carries no install ladder, because the
	// rotation replaces the secret of a wallet whose vault already exists.
	if (!auth.grant || !auth.grant->valid()) {
		LOG(("Wallet Error: rotation stored without an unlocked vault."));
		fail(u"ROTATION_VAULT_LOCKED"_q);
		return;
	}
	const auto lifecycle = _engine->lifecycle();
	const auto expectedAnchor = active->publicKey;
	const auto address = CanonicalAddress(active->address);
	auto recoveryWords = std::vector<std::string>();
	recoveryWords.reserve(_preparedRotation->words.size());
	for (const auto &word : _preparedRotation->words) {
		recoveryWords.push_back(word.toStdString());
	}
	auto request = engine::ImportWalletRequest{
		.record_id = NewRecordId(),
		.network = engine::Network::kMainnet,
		.recovery_words = std::move(recoveryWords),
	};
	_engine->run([lifecycle, request = std::move(request)]() mutable {
		return lifecycle->import_wallet(request);
	}, [=, this, grant = auth.grant](engine::WalletDescriptor descriptor) {
		const auto record = RecordFromDescriptor(descriptor);
		const auto rollBack = [=, this](const QString &error) {
			_engine->run([lifecycle, descriptor] {
				lifecycle->delete_wallet(descriptor);
			}, [=] {
				fail(error);
			}, [=](EngineError) {
				LOG(("Wallet Error: delete_wallet after a refused rotation "
					"store failed."));
				fail(error);
			});
		};
		if (record.publicKey != expectedAnchor
			|| CanonicalAddress(record.address) != address) {
			LOG(("Wallet Error: prepared rotation phrase derives "
				"another wallet."));
			rollBack(u"ROTATION_KEY_MISMATCH"_q);
			return;
		}
		auto store = custody();
		store.pendingRotation = PendingRotation{
			.recordId = record.recordId,
			.secretRef = record.secretRef,
			.operationId = QString::fromStdString(NewRecordId()),
			.newPublicKey = _preparedRotation->newPublicKey,
		};
		if (!WriteCustodyStore(_session->local(), store)) {
			LOG(("Wallet Error: pending rotation write failed."));
			rollBack(u"ROTATION_STORE_FAILED"_q);
			return;
		}
		_custody = std::move(store);
		done();
	}, [=, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: import_wallet for a rotation failed: %1"
			).arg(LifecycleErrorName(error)));
		// The grant this store ran under can lapse between the confirmation
		// and the store, and the host then refuses it typed. That arm, not
		// the seam check above, is what states a vault emptied mid-flow.
		fail(IsVaultLocked(error)
			? u"ROTATION_VAULT_LOCKED"_q
			: u"ROTATION_STORE_FAILED"_q);
	});
}

void Session::discardPendingRotation() {
	auto store = custody();
	const auto pending = base::take(store.pendingRotation);
	if (!pending) {
		return;
	}
	// delete_wallet re-derives the address from the descriptor's anchor key
	// and refuses a record that disagrees, and the pending shares both with
	// the record of the served wallet by the import-time check, whether or
	// not that record still signs for it, so its descriptor is that
	// record's identity under the pending's handle.
	if (const auto held = store.forAddress(_address)) {
		const auto lifecycle = _engine->lifecycle();
		_engine->run([
			lifecycle,
			descriptor = DescriptorFromRecord(CustodyRecord{
				.recordId = pending->recordId,
				.address = held->address,
				.publicKey = held->publicKey,
				.network = held->network,
				.secretRef = pending->secretRef,
			})
		] {
			lifecycle->delete_wallet(descriptor);
		}, [] {}, [](EngineError error) {
			LOG(("Wallet Error: delete_wallet of a discarded rotation "
				"failed: %1").arg(LifecycleErrorName(error)));
		});
	} else {
		LOG(("Wallet Error: discarded rotation has no custody record, "
			"its secret is left in place."));
	}
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: pending rotation removal write failed."));
		return;
	}
	_custody = std::move(store);
	updatePollingState();
}

void Session::promotePendingRotation() {
	auto store = custody();
	const auto pending = base::take(store.pendingRotation);
	if (!pending) {
		return;
	}
	const auto i = ranges::find_if(store.records, [&](const auto &record) {
		return (CanonicalAddress(record.address) == _address);
	});
	if (i == end(store.records)) {
		LOG(("Wallet Error: confirmed rotation has no custody record."));
		discardPendingRotation();
		return;
	}
	// The record's identity survives and only its engine handle changes,
	// so the handle to delete is read before the swap, and the swap and
	// the pending's removal go down in one write: with two, a crash between
	// them would leave a promoted record beside a stale pending whose
	// recordId now IS the live one, and the next start's discard would
	// delete the live secret. The signing key and the awaiting mark go down
	// in that same write: the mark says the chain already holds the new key
	// while the server still serves the old one, and reconcileCustody()
	// clears it once the served key catches up or moves elsewhere. A pre-v4
	// pending names no key, so it promotes to an unknown signing key and
	// the record reads obsolete until its phrase is restored.
	const auto lifecycle = _engine->lifecycle();
	const auto superseded = DescriptorFromRecord(*i);
	i->recordId = pending->recordId;
	i->secretRef = pending->secretRef;
	i->signingKey = pending->newPublicKey;
	i->awaitingServerKey = !pending->newPublicKey.isEmpty()
		&& pending->newPublicKey != _publicKey;
	i->rotatedSinceBackup = true;
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: rotation promotion write failed, "
			"retrying on the next snapshot."));
		return;
	}
	_custody = std::move(store);
	_engine->run([lifecycle, superseded] {
		lifecycle->delete_wallet(superseded);
	}, [] {}, [](EngineError error) {
		LOG(("Wallet Error: delete_wallet of the rotated-out record "
			"failed: %1").arg(LifecycleErrorName(error)));
	});
	updateDeviceCustodyState();
	updatePollingState();
}

void Session::finishRotation(const QString &error) {
	_rotating = false;
	_preparedRotation = nullptr;
	const auto confirmed = base::take(_rotationConfirmed);
	const auto failed = base::take(_rotationFailed);
	if (!error.isEmpty()) {
		if (failed) {
			failed(error);
		}
	} else if (confirmed) {
		confirmed();
	}
}

void Session::clearRotatedSinceBackup() {
	auto store = custody();
	const auto i = ranges::find_if(store.records, [&](const auto &record) {
		return (CanonicalAddress(record.address) == _address);
	});
	if (i == end(store.records) || !i->rotatedSinceBackup) {
		return;
	}
	i->rotatedSinceBackup = false;
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: rotation guard reset write failed."));
		return;
	}
	_custody = std::move(store);
}

} // namespace Wallet
