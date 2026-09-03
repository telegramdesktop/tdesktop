/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session.h"

#include "base/unixtime.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "gram/api/gram_api_emulate.h"
#include "main/main_session.h"
#include "tde2e/tde2e_api.h"
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_engine.h"
#include "wallet/wallet_onramp.h"
#include "wallet/wallet_phrase_shares.h"
#include "wallet/wallet_rates.h"
#include "wallet/wallet_user_addresses.h"

#include "wallet_engine.hpp"

#include <QtCore/QUuid>

#include <limits>

namespace Wallet {

struct ShareFetch {
	TdE2E::TemporaryKeyPair keys;
	std::vector<QByteArray> shares;
	std::vector<mtpRequestId> requests;
	std::vector<MTP::ShiftedDcId> sessions;
	Fn<void(const QString &)> fail;
	int pending = 0;
};

struct Session::PreparedRotation {
	std::vector<QString> words;
	std::string signedBoc;
	uint32 seqno = 0;
	uint64 validUntil = 0;
	int64 quotedFeeNano = 0;
};

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
constexpr auto kForcedCollectiblesInterval = 10 * crl::time(1000);
constexpr auto kStreamResyncInterval = 30 * crl::time(1000);
constexpr auto kClientSendValiditySeconds = uint64(300);
constexpr auto kClientResolutionMarginSeconds = uint64(60);
constexpr auto kClientRequestTimeoutMs = uint64(15000);
// The throw-away rotation a quote emulates is a validly signed key-change
// message that leaves the device, and only its expiration bounds a stray
// replay of it, so it gets the shortest window that comfortably outlives one
// emulation round trip (the Wallet::Api deadline plus queueing); the fresh
// prepare keeps the engine's own send validity.
constexpr auto kRotationQuoteValiditySeconds = uint64(120);

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

struct Restored {
	engine::WalletDescriptor descriptor;
	std::vector<QString> words;
};

struct ThrowawayRotation {
	std::string signedBoc;
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
	} catch (const engine::wallet_lifecycle_error::SecretWalletMismatch &) {
		return u"SecretWalletMismatch"_q;
	} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &) {
		return u"ProtectedSecretHost"_q;
	} catch (...) {
	}
	return u"unknown"_q;
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

[[nodiscard]] std::optional<std::vector<int>> ParseHolderDcs(
		const MTPDwallet_secretPhraseParts &data) {
	const auto &list = data.vdcs().v;
	if (data.vtoken().v.isEmpty() || list.isEmpty()) {
		return std::nullopt;
	}
	auto result = std::vector<int>();
	result.reserve(list.size());
	for (const auto &dc : list) {
		if (dc.v <= 0
			|| dc.v >= MTP::kDcShift
			|| ranges::contains(result, dc.v)) {
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
		LOG(("Wallet Error: share part %1 could not be opened.").arg(index));
		return false;
	}
	state->shares[index] = std::move(*share);
	return true;
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

// The TL declares tx_hash as an unqualified string and states no encoding,
// while ExplorerTransactionUrl() hexes whatever is stored here straight into
// a URL path with no validation and no escaping. The shape is therefore
// decided here, and a string of neither recognized shape stores nothing: an
// empty traceId hides the explorer entry instead of pointing it at a hash
// this client cannot be sure of. Base64 of 32 bytes is 43 or 44 characters
// and can therefore never also be 64 hex digits, so the two tests cannot
// collide and their order is a cheapness choice, not a correctness one.
[[nodiscard]] QByteArray TransactionHashFromServer(const QString &value) {
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
	LOG(("Wallet Error: wallet.getTransactions sent an unusable tx_hash."));
	return QByteArray();
}

[[nodiscard]] TransferItem HistoryItemFromServer(
		const MTPWalletTransaction &item) {
	const auto &data = item.data();
	auto result = TransferItem();
	result.incoming = data.is_incoming();
	result.amountNano = data.vamount().v;
	result.feeNano = data.vfee().v;
	result.date = data.vdate().v;
	// A failure is a settled outcome and it wins over pending: a transaction
	// the server has marked failed will never confirm, so telling the reader
	// to keep waiting for it would be the worst of the readings available.
	// Both flags at once can only mean the server has not yet dropped the row
	// from its pending set, which is bookkeeping rather than a state to act
	// on; the row then shows its fee, which a failed transaction did pay.
	result.status = data.is_failed()
		? TransferItem::Status::Failure
		: data.is_pending()
		? TransferItem::Status::Pending
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
		data.vpeer().match([&](const MTPDwalletTransactionPeerUser &data) {
			// counterparty stays empty on purpose: the server sends no
			// address for a user counterparty and the client must not
			// synthesize one. The details sheet builds its sender /
			// recipient row from the peer instead and takes the address
			// half from the per-user address store, which answers only for
			// a user the chain has named.
			result.kind = TransferItem::Kind::PeerTransfer;
			result.counterpartyPeer = peerFromUser(data.vuser_id()).value;
		}, [&](const MTPDwalletTransactionPeerAddress &data) {
			result.counterparty = CanonicalAddress(qs(data.vaddress()));
			if (result.counterparty.isEmpty()) {
				LOG(("Wallet Error: wallet.getTransactions sent an unusable "
					"counterparty address."));
			}
		}, [](const MTPDwalletTransactionPeerUnsupported &) {
			// Nothing is written, because the defaults are the row: a
			// Kind::Transfer with no counterparty renders through
			// RowContentFromItem's fall-through as a Deposit or a
			// Withdrawal by direction, with the date and the amount. That
			// is exactly the graceful degradation this constructor exists
			// for, so no lang key is invented for it.
		});
	}
	if (const auto comment = data.vcomment()) {
		result.comment = qs(*comment);
	}
	if (const auto hash = data.vtx_hash()) {
		result.traceId = TransactionHashFromServer(qs(*hash));
	}
	// id, lt and every collectible / provider / encrypted-comment member are
	// left at their defaults: the record has no id field, the server sends
	// nothing for the rest, and lt's only reader is the wallethistory Debug
	// log line, which prints the honest zero instead of a synthesized time.
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

std::vector<TransferItem> HistoryFromServer(
		const QVector<MTPWalletTransaction> &list) {
	auto result = std::vector<TransferItem>();
	result.reserve(list.size());
	for (const auto &item : list) {
		result.push_back(HistoryItemFromServer(item));
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
	if (const auto state = _shareFetch.lock()) {
		FinishShareFetch(_stateApi, _shareFetchTimer, state);
	}
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
		// _historyPaged and _collectiblesPaged are facts about one
		// overview's scroll position, so they end with the panel that
		// owned them: otherwise the periodic refresh stays refused for
		// the rest of the session and a transaction or a collectible
		// received while the panel was closed never appears.
		_historyPaged = false;
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

WalletCapabilities Session::capabilities() const {
	return _capabilities.current();
}

rpl::producer<WalletCapabilities> Session::capabilitiesValue() const {
	return _capabilities.value();
}

QByteArray Session::publicKey() const {
	return _publicKey;
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
			setPresence(Presence::AddressUnreadable);
			return;
		}
		_address = parsed->raw;
		_publicKey = data.vpublic_key().v;
		_balanceNano = int64(data.vbalance().v);
		_capabilities = WalletCapabilities{
			.backupEnabled = data.is_backup_enabled(),
			.canExportPhrase = data.is_can_export_phrase(),
			.canEnableBackup = data.is_can_enable_backup(),
		};
		setPresence(Presence::Ready);
		reconcileCustody();
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
	// The gate is recomputed before the lanes are drained, because both
	// drains publish into the same derived faces the gate does: a face
	// evaluated between them reads emptied lists under the gate this
	// wallet held while it was Ready, which was never true of it. Doing it
	// first costs nothing — for a presence that is not Ready every term
	// in updateListsGate() that reads the history lane is conjoined with
	// `ready`, so it writes the same two values before the drain as after.
	updateListsGate();
	if (presence != Presence::Ready) {
		// clearHistory() cancels the request in flight, so a refreshHistory()
		// issued while the wallet was Ready would never run its done and its
		// caller would wait forever. Draining here is exactly what
		// refreshHistory() itself does for a presence that is not Ready.
		clearHistory();
		finishHistoryWaiters();
		clearCollectibles();
	}
	updatePollingState();
	if (presence == Presence::Ready) {
		refreshHistory();
	}
}

bool Session::revealsLocally() {
	ensureLoaded();
	return (_presence.current() == Presence::Ready)
		&& (_publicKey.size() == kCustodyPublicKeySize)
		&& (custody().matching(_publicKey) != nullptr);
}

bool Session::custodyBusy() const {
	return _phraseRevealing || _replacing || _backupChanging || _rotating;
}

void Session::revealPhrase(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	_phraseRevealing = true;
	// Every path below ends in exactly one of these two calls, which is
	// what clears the guard, so none of them is fenced by _networkGeneration:
	// a reveal owns no network-derived state, and dropping its callback
	// would either orphan a just-stored engine secret or leave the guard
	// set for the rest of the session.
	done = [this, done = std::move(done)](std::vector<QString> words) {
		_phraseRevealing = false;
		if (done) {
			done(std::move(words));
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_phraseRevealing = false;
		if (fail) {
			fail(error);
		}
	};
	if (const auto record = custody().matching(_publicKey)) {
		revealLocally(*record, done, fail);
	} else {
		revealFromShares(std::move(password), done, fail);
	}
}

void Session::revealLocally(
		const CustodyRecord &record,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail) {
	const auto lifecycle = _engine->lifecycle();
	const auto descriptor = DescriptorFromRecord(record);
	_engine->run([lifecycle, descriptor] {
		return lifecycle->reveal_recovery_phrase(descriptor);
	}, [=](engine::RecoveryPhrase phrase) {
		auto words = SplitWords(QString::fromStdString(phrase.phrase));
		if (words.size() < 2) {
			LOG(("Wallet Error: local phrase reveal produced no words."));
			fail(u"PHRASE_EMPTY"_q);
			return;
		}
		done(std::move(words));
	}, [=](EngineError error) {
		LOG(("Wallet Error: local phrase reveal failed: %1"
			).arg(LifecycleErrorName(error)));
		fail(u"PHRASE_LOCAL_FAILED"_q);
	});
}

void Session::revealFromShares(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail) {
	using Flag = MTPwallet_exportSecretPhrase::Flag;
	const auto checked = password && *password;
	_stateApi.request(MTPwallet_ExportSecretPhrase(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		checked ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=, this](const MTPwallet_SecretPhraseParts &result) {
		const auto &data = result.data();
		const auto dcs = ParseHolderDcs(data);
		if (!dcs) {
			LOG(("Wallet Error: wallet.exportSecretPhrase answered "
				"%1 holder(s).").arg(data.vdcs().v.size()));
			fail(u"PHRASE_PARTS_INVALID"_q);
			return;
		}
		fetchShareParts(qs(data.vtoken()), *dcs, done, fail);
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.exportSecretPhrase failed: %1"
			).arg(error.type()));
		fail(error.type());
	}).handleFloodErrors().send();
}

void Session::fetchShareParts(
		const QString &token,
		std::vector<int> dcs,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail) {
	auto keys = TdE2E::TemporaryKeyPair::Generate();
	if (!keys) {
		LOG(("Wallet Error: could not generate an ephemeral key."));
		fail(u"PHRASE_PARTS_INVALID"_q);
		return;
	}
	const auto count = int(dcs.size());
	const auto state = std::make_shared<ShareFetch>(ShareFetch{
		.keys = std::move(*keys),
		.shares = std::vector<QByteArray>(count),
		.requests = std::vector<mtpRequestId>(count),
		.sessions = std::vector<MTP::ShiftedDcId>(count),
		.fail = fail,
		.pending = count,
	});
	const auto publicKey = state->keys.publicKey();
	for (auto i = 0; i != count; ++i) {
		state->sessions[i] = MTP::ShiftDcId(dcs[i], MTP::kWalletShareDcShift);
		state->requests[i] = _stateApi.request(
			MTPwallet_FetchEncryptedSecretPhrasePart(
				MTP_string(token),
				MTP_bytes(publicKey))
		).done([=, this](const MTPwallet_EncryptedSecretPhrasePart &result) {
			if (!state->fail) {
				return;
			}
			state->requests[i] = 0;
			if (!OpenSharePart(state, i, result.data().vdata().v)) {
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
			restoreFromWords(
				SplitWords(QString::fromUtf8(*seed)),
				done,
				base::take(state->fail));
		}).fail([=, this](const MTP::Error &error) {
			if (!state->fail) {
				return;
			}
			state->requests[i] = 0;
			LOG(("Wallet Error: wallet.fetchEncryptedSecretPhrasePart "
				"failed: %1").arg(error.type()));
			FailShareFetch(_stateApi, _shareFetchTimer, state, error.type());
		}).handleFloodErrors().toDC(state->sessions[i]).send();
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

void Session::restoreFromWords(
		std::vector<QString> words,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &)> fail) {
	if (words.size() < 2) {
		LOG(("Wallet Error: reconstructed phrase has no words."));
		fail(u"PHRASE_EMPTY"_q);
		return;
	}
	const auto lifecycle = _engine->lifecycle();
	const auto expected = _publicKey;
	auto recoveryWords = std::vector<std::string>();
	recoveryWords.reserve(words.size());
	for (const auto &word : words) {
		recoveryWords.push_back(word.toStdString());
	}
	auto request = engine::ImportWalletRequest{
		.record_id = NewRecordId(),
		.network = engine::Network::kMainnet,
		.recovery_words = std::move(recoveryWords),
	};
	_engine->run([
		lifecycle,
		request = std::move(request),
		words = std::move(words)
	]() mutable {
		auto descriptor = lifecycle->import_wallet(request);
		return Restored{ std::move(descriptor), std::move(words) };
	}, [=, this](Restored restored) {
		const auto record = RecordFromDescriptor(restored.descriptor);
		if (record.publicKey != expected) {
			LOG(("Wallet Error: restored phrase derives another key."));
			_engine->run([lifecycle, descriptor = restored.descriptor] {
				lifecycle->delete_wallet(descriptor);
			}, [=] {
				fail(u"PHRASE_KEY_MISMATCH"_q);
			}, [=](EngineError) {
				LOG(("Wallet Error: delete_wallet after a key mismatch "
					"failed."));
				fail(u"PHRASE_KEY_MISMATCH"_q);
			});
			return;
		}
		if (!persistCustody(record)) {
			_engine->run([lifecycle, descriptor = restored.descriptor] {
				lifecycle->delete_wallet(descriptor);
			}, [] {}, [](EngineError) {});
		}
		done(std::move(restored.words));
	}, [=](EngineError error) {
		const auto name = LifecycleErrorName(error);
		LOG(("Wallet Error: import_wallet failed: %1").arg(name));
		fail((name == u"InvalidRecoveryPhrase"_q)
			? u"PHRASE_INVALID_PHRASE"_q
			: u"PHRASE_IMPORT_FAILED"_q);
	});
}

void Session::restoreFromPhrase(
		std::vector<QString> words,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	_phraseRevealing = true;
	restoreFromWords(std::move(words), [this, done = std::move(done)](
			std::vector<QString>) {
		_phraseRevealing = false;
		if (done) {
			done();
		}
	}, [this, fail = std::move(fail)](const QString &error) {
		_phraseRevealing = false;
		if (fail) {
			fail(error);
		}
	});
}

void Session::restoreFromBackup(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	_phraseRevealing = true;
	revealFromShares(std::move(password), [this, done = std::move(done)](
			std::vector<QString>) {
		_phraseRevealing = false;
		if (done) {
			done();
		}
	}, [this, fail = std::move(fail)](const QString &error) {
		_phraseRevealing = false;
		if (fail) {
			fail(error);
		}
	});
}

void Session::revealParked(
		const QByteArray &publicKey,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	const auto record = custody().matching(publicKey);
	if (!record || publicKey == _publicKey) {
		LOG(("Wallet Error: parked reveal requested for a non-parked key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
	_phraseRevealing = true;
	done = [this, done = std::move(done)](std::vector<QString> words) {
		_phraseRevealing = false;
		if (done) {
			done(std::move(words));
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_phraseRevealing = false;
		if (fail) {
			fail(error);
		}
	};
	revealLocally(*record, done, fail);
}

void Session::dropParked(
		const QByteArray &publicKey,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	const auto record = custody().matching(publicKey);
	if (!record || publicKey == _publicKey) {
		LOG(("Wallet Error: drop requested for a non-parked key."));
		if (fail) {
			fail(u"PHRASE_STATE_UNKNOWN"_q);
		}
		return;
	}
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
	_engine->run([lifecycle, descriptor = DescriptorFromRecord(*record)] {
		lifecycle->delete_wallet(descriptor);
	}, [=, this] {
		removeCustodyRecord(publicKey);
		done();
	}, [=](EngineError error) {
		LOG(("Wallet Error: parked delete_wallet failed: %1"
			).arg(LifecycleErrorName(error)));
		fail(u"PHRASE_LOCAL_FAILED"_q);
	});
}

void Session::prepareBackupParts(
		Fn<void(std::vector<QByteArray>)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	if (custodyBusy()) {
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
	const auto matching = custody().matching(_publicKey);
	if (!matching) {
		LOG(("Wallet Error: backup requested without local custody."));
		if (fail) {
			fail(u"BACKUP_NO_CUSTODY"_q);
		}
		return;
	}
	const auto record = *matching;
	_backupChanging = true;
	// Every path below ends in exactly one of these two calls, which is
	// what clears the guard, so none of them is fenced by _networkGeneration:
	// a reveal owns no network-derived state, and dropping its callback
	// would either orphan a just-stored engine secret or leave the guard
	// set for the rest of the session.
	done = [this, done = std::move(done)](std::vector<QByteArray> parts) {
		_backupChanging = false;
		if (done) {
			done(std::move(parts));
		}
	};
	fail = [this, fail = std::move(fail)](const QString &error) {
		_backupChanging = false;
		if (fail) {
			fail(error);
		}
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
		revealLocally(record, [=](std::vector<QString> words) {
			auto parts = SealBackupParts(*keys, words);
			if (!parts) {
				LOG(("Wallet Error: backup parts could not be sealed."));
				fail(u"BACKUP_ENCRYPT_FAILED"_q);
				return;
			}
			done(std::move(*parts));
		}, fail);
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.getBackupHolderDcs failed: %1"
			).arg(error.type()));
		fail(error.type());
	}).send();
}

void Session::disableBackup(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	if (custodyBusy()) {
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
	if (!custody().matching(_publicKey)) {
		LOG(("Wallet Error: backup disable requested without local custody."));
		if (fail) {
			fail(u"BACKUP_NO_CUSTODY"_q);
		}
		return;
	}
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
		checked ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=, this](const MTPWalletState &result) {
		clearRotatedSinceBackup();
		applyState(result);
		done();
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.disableBackup failed: %1"
			).arg(error.type()));
		fail(error.type());
	}).handleFloodErrors().send();
}

void Session::enableBackup(
		std::vector<QByteArray> parts,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	if (custodyBusy()) {
		LOG(("Wallet Error: backup enable requested "
			"while another is in flight."));
		if (fail) {
			fail(u"BACKUP_BUSY"_q);
		}
		return;
	}
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		LOG(("Wallet Error: backup enable requested "
			"without a settled wallet key."));
		if (fail) {
			fail(u"BACKUP_STATE_UNKNOWN"_q);
		}
		return;
	}
	if (!custody().matching(_publicKey)) {
		LOG(("Wallet Error: backup enable requested without local custody."));
		if (fail) {
			fail(u"BACKUP_NO_CUSTODY"_q);
		}
		return;
	}
	if (parts.empty()) {
		LOG(("Wallet Error: backup enable requested without parts."));
		if (fail) {
			fail(u"BACKUP_PARTS_EMPTY"_q);
		}
		return;
	}
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
	auto list = QVector<MTPbytes>();
	list.reserve(parts.size());
	for (auto &part : parts) {
		list.push_back(MTP_bytes(std::move(part)));
	}
	using Flag = MTPwallet_enableBackup::Flag;
	const auto checked = password && *password;
	_stateApi.request(MTPwallet_EnableBackup(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		MTP_vector<MTPbytes>(std::move(list)),
		checked ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=, this](const MTPWalletState &result) {
		clearRotatedSinceBackup();
		applyState(result);
		done();
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.enableBackup failed: %1"
			).arg(error.type()));
		fail(error.type());
	}).handleFloodErrors().send();
}

bool Session::rotationOffered() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		return false;
	}
	const auto matching = custody().matching(_publicKey);
	return (matching != nullptr)
		&& !matching->rotatedSinceBackup
		&& !custody().pendingRotation
		&& (_engine->client() != nullptr)
		&& !_clientStopping;
}

void Session::quoteRotationFee(Fn<void(FeeResult)> done) {
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
	}
	_rotating = true;
	const auto client = _engine->client();
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
		return ThrowawayRotation{ std::move(prepared.signed_boc) };
	}, [=, this](ThrowawayRotation throwaway) {
		if (generation != _networkGeneration) {
			failed(u"stale generation"_q);
			return;
		}
		_api.request(Gram::EmulateTraceRequest(
			QString::fromStdString(throwaway.signedBoc)
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
	}, [=, this](EngineError error) {
		LOG(("Wallet Error: engine prepare_key_rotation (quote) failed: %1"
			).arg(error.message));
		finish(FeeResult{ .error = (generation != _networkGeneration)
			? SendError::Failed
			: SendErrorFrom(error) });
	});
}

void Session::prepareRotation(
		int64 quotedFeeNano,
		Fn<void(std::vector<QString>)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	const auto matching = custody().matching(_publicKey);
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
	const auto client = _engine->client();
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
	_rotating = true;
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
	}, [=, this](engine::PreparedKeyRotation prepared) {
		auto words = SplitWords(QString::fromStdString(
			prepared.replacement_recovery_phrase.phrase));
		if (words.size() < 2) {
			LOG(("Wallet Error: key rotation prepare produced no words."));
			fail(u"ROTATION_PREPARE_FAILED"_q);
			return;
		}
		_preparedRotation = std::make_unique<PreparedRotation>(
			PreparedRotation{
				.words = words,
				.signedBoc = std::move(prepared.signed_boc),
				.seqno = prepared.seqno,
				.validUntil = prepared.valid_until,
				.quotedFeeNano = quotedFeeNano,
			});
		if (done) {
			done(std::move(words));
		}
	}, [=](EngineError error) {
		LOG(("Wallet Error: engine prepare_key_rotation failed: %1"
			).arg(error.message));
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
		Fn<void()> confirmed,
		Fn<void(const QString &error)> fail) {
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
	const auto client = _engine->client();
	if (!client || _clientStopping) {
		LOG(("Wallet Error: rotation submitted without a signing client."));
		refuse(u"ROTATION_SIGNING_UNAVAILABLE"_q);
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
	storePendingRotation([=, this] {
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
		if (record.publicKey != _publicKey) {
			result.push_back(record);
		}
	}
	ranges::reverse(result);
	return result;
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

const CustodyStore &Session::custody() {
	if (!_custody) {
		_custody = ReadCustodyStore(_session->local());
		if (!_custody) {
			LOG(("Wallet Error: custody store unreadable, treating as empty."));
			_custody = CustodyStore();
		}
	}
	return *_custody;
}

bool Session::persistCustody(const CustodyRecord &record) {
	auto store = custody();
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
	_custody = std::move(store);
	updateDeviceCustodyState();
	return true;
}

void Session::replaceWithNew(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	auto oldRecord = std::optional<CustodyRecord>();
	if (const auto record = custody().matching(_publicKey)) {
		oldRecord = *record;
	}
	sendReplaceWallet(
		MTP_inputWalletNew(),
		std::move(password),
		[=, this](const MTPWalletState &state) {
			finishConfirmedReplace(oldRecord, std::nullopt, state, done, fail);
		},
		fail);
}

void Session::replaceWithImported(
		std::vector<QString> words,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	auto oldRecord = std::optional<CustodyRecord>();
	if (const auto record = custody().matching(_publicKey)) {
		oldRecord = *record;
	}
	const auto lifecycle = _engine->lifecycle();
	auto recoveryWords = std::vector<std::string>();
	recoveryWords.reserve(words.size());
	for (const auto &word : words) {
		recoveryWords.push_back(word.toStdString());
	}
	auto request = engine::ImportWalletRequest{
		.record_id = NewRecordId(),
		.network = engine::Network::kMainnet,
		.recovery_words = std::move(recoveryWords),
	};
	_engine->run([lifecycle, request = std::move(request)]() mutable {
		return lifecycle->import_wallet(request);
	}, [=, this](engine::WalletDescriptor descriptor) {
		const auto record = RecordFromDescriptor(descriptor);
		sendReplaceWallet(
			MTP_inputWalletImported(MTP_bytes(record.publicKey)),
			password,
			[=, this](const MTPWalletState &state) {
				const auto answered = (state.type() == mtpc_walletState)
					? state.c_walletState().vpublic_key().v
					: QByteArray();
				if (answered != record.publicKey) {
					LOG(("Wallet Error: wallet.replaceWallet answered "
						"another key."));
					_engine->run([lifecycle, descriptor] {
						lifecycle->delete_wallet(descriptor);
					}, [=] {
						fail(u"REPLACE_KEY_MISMATCH"_q);
					}, [=](EngineError) {
						LOG(("Wallet Error: delete_wallet after a key "
							"mismatch failed."));
						fail(u"REPLACE_KEY_MISMATCH"_q);
					});
					return;
				}
				finishConfirmedReplace(oldRecord, record, state, done, fail);
			},
			[=, this](const QString &error) {
				_engine->run([lifecycle, descriptor] {
					lifecycle->delete_wallet(descriptor);
				}, [=] {
					fail(error);
				}, [=](EngineError) {
					LOG(("Wallet Error: delete_wallet after a failed "
						"replace failed."));
					fail(error);
				});
			});
	}, [=](EngineError error) {
		const auto name = LifecycleErrorName(error);
		LOG(("Wallet Error: import_wallet failed: %1").arg(name));
		fail((name == u"InvalidRecoveryPhrase"_q)
			? u"REPLACE_INVALID_PHRASE"_q
			: u"REPLACE_IMPORT_FAILED"_q);
	});
}

void Session::sendReplaceWallet(
		const MTPInputWalletReplacement &wallet,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(const MTPWalletState &)> applied,
		Fn<void(const QString &)> fail) {
	using Flag = MTPwallet_replaceWallet::Flag;
	const auto checked = password && *password;
	_stateApi.request(MTPwallet_ReplaceWallet(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		wallet,
		checked ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=](const MTPWalletState &result) {
		applied(result);
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.replaceWallet failed: %1"
			).arg(error.type()));
		fail(error.type());
	}).handleFloodErrors().send();
}

void Session::finishConfirmedReplace(
		std::optional<CustodyRecord> oldRecord,
		std::optional<CustodyRecord> newActive,
		const MTPWalletState &state,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	applyState(state);
	const auto lifecycle = _engine->lifecycle();
	if (newActive) {
		auto sameKeyRow = std::optional<CustodyRecord>();
		const auto row = custody().matching(newActive->publicKey);
		if (row && row->recordId != newActive->recordId) {
			sameKeyRow = *row;
		}
		if (!persistCustody(*newActive)) {
			_engine->run([
				lifecycle,
				descriptor = DescriptorFromRecord(*newActive)
			] {
				lifecycle->delete_wallet(descriptor);
			}, [] {}, [](EngineError) {});
			done();
			return;
		}
		if (sameKeyRow) {
			_engine->run([
				lifecycle,
				descriptor = DescriptorFromRecord(*sameKeyRow)
			] {
				lifecycle->delete_wallet(descriptor);
			}, [] {}, [](EngineError) {
				LOG(("Wallet Error: delete_wallet of a superseded record "
					"failed."));
			});
		}
	}
	if (oldRecord && (oldRecord->publicKey != _publicKey)) {
		removeCustodyRecord(oldRecord->publicKey);
		_engine->run([
			lifecycle,
			descriptor = DescriptorFromRecord(*oldRecord)
		] {
			lifecycle->delete_wallet(descriptor);
		}, [] {}, [](EngineError) {
			LOG(("Wallet Error: delete_wallet of the replaced wallet "
				"failed."));
		});
	}
	done();
}

void Session::reconcileCustody() {
	if (_publicKey.size() != kCustodyPublicKeySize) {
		return;
	}
	auto store = custody();
	auto changed = false;
	for (auto &record : store.records) {
		if (record.publicKey != _publicKey) {
			if (record.active) {
				record.active = false;
				changed = true;
			}
		} else if (!record.active) {
			record.active = true;
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
	if (custody().pendingRotation) {
		updatePollingState();
	}
}

void Session::updateDeviceCustodyState() {
	if (_presence.current() != Presence::Ready
		|| _publicKey.size() != kCustodyPublicKeySize) {
		return;
	}
	const auto &store = custody();
	const auto conflict = ranges::any_of(
		store.records,
		[&](const CustodyRecord &record) {
			return record.publicKey != _publicKey;
		});
	const auto mode = store.matching(_publicKey)
		? DeviceMode::Full
		: _capabilities.current().canExportPhrase
		? DeviceMode::ReadOnlyRestorable
		: DeviceMode::ReadOnlyNotRestorable;
	_deviceCustody = DeviceCustodyState{
		.mode = mode,
		.conflict = conflict,
	};
	_custodyUpdates.fire({});
	syncEngineClient();
}

void Session::syncEngineClient() {
	const auto ready = (_presence.current() == Presence::Ready)
		&& (_publicKey.size() == kCustodyPublicKeySize);
	const auto wanted = ready ? custody().matching(_publicKey) : nullptr;
	if (_engine->client()) {
		if ((wanted && _clientRecordId == wanted->recordId)
			|| _clientStopping) {
			return;
		}
		_clientStopping = true;
		_engine->stopClient([this] {
			_clientStopping = false;
			_clientRecordId = QString();
			syncEngineClient();
		});
		return;
	} else if (!wanted || _clientStopping) {
		return;
	}
	try {
		_engine->startClient(ClientConfigFromRecord(*wanted));
		_clientRecordId = wanted->recordId;
		requestEngineRefresh();
	} catch (...) {
		LOG(("Wallet Error: engine client start refused: %1"
			).arg(ClientErrorName(std::current_exception())));
	}
}

void Session::removeCustodyRecord(const QByteArray &publicKey) {
	auto store = custody();
	store.records.erase(
		ranges::remove(
			store.records,
			publicKey,
			&CustodyRecord::publicKey),
		end(store.records));
	if (!WriteCustodyStore(_session->local(), store)) {
		LOG(("Wallet Error: custody record removal write failed."));
		return;
	}
	_custody = std::move(store);
	updateDeviceCustodyState();
}

void Session::clearNetworkState() {
	++_networkGeneration;
	_balanceNano = 0;
	_engineStatus = AccountStatus::NonExisting;
	_stateApi.request(base::take(_stateRequestId)).cancel();
	_stateRequestedAt = 0;
	_stateRefreshedAt = 0;
	_stateFailures = 0;
	clearHistory();
	_collectiblesRequestPending = false;
	clearCollectibles();
	_pending.reset();
	_sendState = SendState::Idle;
	_pollingCount = 0;
	_pollTimer.cancel();
	_stream->stop();
	_sendUnresolved = false;
	_previewNextArgs.reset();
	_previewNextDone = nullptr;
	_historyDone.clear();
	updateListsGate();
}

void Session::requestEngineRefresh() {
	if (_engineRefreshPending || !_engine->client()) {
		return;
	}
	_engineRefreshPending = true;
	const auto client = _engine->client();
	const auto generation = _networkGeneration;
	_engine->run([client] {
		return client->refresh();
	}, [=, this](engine::WalletUpdate update) {
		_engineRefreshPending = false;
		if (generation != _networkGeneration || _clientStopping) {
			return;
		}
		applyEngineUpdate(update);
	}, [=, this](EngineError error) {
		_engineRefreshPending = false;
		if (generation != _networkGeneration || _clientStopping) {
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
	applyRotationSnapshot(update.snapshot.send, false);
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
	if ((_presence.current() != Presence::Ready) || _historyPaged) {
		if (done) {
			done();
		}
		return;
	}
	if (done) {
		_historyDone.push_back(std::move(done));
	}
	requestTransactions(false);
}

void Session::setHistory(std::vector<TransferItem> &&list) {
	_history = std::move(list);
	_historyUpdates.fire({});
}

void Session::requestTransactions(bool more) {
	if (_historyRequestId) {
		return;
	}
	_historyRequestedAt = crl::now();
	// The inbound and outbound flags stay unset on purpose: the overview
	// shows one undivided feed and offers no direction filter, so asking the
	// server for half of the list would invent a UI this task does not add.
	// They stay available for a filter that is actually designed.
	_historyRequestId = _stateApi.request(MTPwallet_GetTransactions(
		MTP_flags(0),
		MTP_string(more ? _historyNextOffset : QString()),
		MTP_int(kTransactionsPerPage)
	)).done([=](const MTPwallet_Transactions &result) {
		_historyRequestId = 0;
		applyTransactions(result, more);
		finishHistoryWaiters();
	}).fail([=](const MTP::Error &error) {
		_historyRequestId = 0;
		LOG(("Wallet Error: wallet.getTransactions failed: %1"
			).arg(error.type()));
		if (!more) {
			_historyUnreachable = true;
		}
		_historySettled = true;
		updateListsGate();
		finishHistoryWaiters();
	}).handleAllErrors().send();
}

void Session::applyTransactions(
		const MTPwallet_Transactions &result,
		bool more) {
	const auto &data = result.data();
	// The peers are stored before anything resolves one, because a row whose
	// user is missing from Data::Session falls through to the address
	// presentation and paints a plain Deposit or Withdrawal row. That is
	// also the legitimate row for two other server inputs, so a dropped
	// users vector would look exactly like a working client.
	_session->data().processUsers(data.vusers());
	_session->data().processChats(data.vchats());
	const auto next = data.vnext_offset();
	// An empty next_offset is byte-identical to a first-page request, so
	// paging on it would read the same rows forever. It ends the list exactly
	// as an absent one does; the value itself is opaque and never parsed.
	_historyNextOffset = next ? qs(*next) : QString();
	_historyHasNext = !_historyNextOffset.isEmpty();
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
	auto loaded = HistoryFromServer(data.vtransactions().v);
	if (more) {
		if (!loaded.empty()) {
			auto list = _history;
			list.insert(
				end(list),
				std::make_move_iterator(begin(loaded)),
				std::make_move_iterator(end(loaded)));
			setHistory(std::move(list));
		}
	} else if (!SameHistory(_history, loaded)) {
		setHistory(std::move(loaded));
	}
	_historySettled = true;
	updateListsGate();
}

void Session::finishHistoryWaiters() {
	for (const auto &callback : base::take(_historyDone)) {
		callback();
	}
}

void Session::clearHistory() {
	_stateApi.request(base::take(_historyRequestId)).cancel();
	_history.clear();
	_historyHasNext = false;
	_historyNextOffset = QString();
	_historyRefreshedAt = 0;
	_historyRequestedAt = 0;
	_historySettled = false;
	_historyUnreachable = false;
	_historyPaged = false;
	_historyUpdates.fire({});
}

void Session::clearCollectibles() {
	_collectibles.clear();
	_collectiblesRefreshedAt = 0;
	_collectiblesCompletedAt = 0;
	_collectiblesHasMore = false;
	_collectiblesPaged = false;
	_collectiblesTab = false;
	_collectiblesUpdates.fire({});
}

bool Session::historyHasNext() const {
	return _historyHasNext;
}

void Session::loadMoreHistory() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _historyRequestId
		|| !_historyHasNext) {
		return;
	}
	requestTransactions(true);
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
		if (generation != _networkGeneration || _clientStopping) {
			return;
		}
		applyCollectiblesUpdate(update, more);
	}, [=, this](EngineError error) {
		_collectiblesRequestPending = false;
		if (generation != _networkGeneration || _clientStopping) {
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
	if (_presence.current() != Presence::Ready) {
		return;
	}
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
	const auto ready = (presence == Presence::Ready);
	_stateUnreachable = (unknown
		&& (_stateFailures >= kStateFailuresBeforeStated))
		|| (ready && _historyUnreachable)
		|| (presence == Presence::AddressUnreadable);
	_listsGated = (unknown && !_stateUnreachable)
		|| (presence == Presence::Provisioning)
		|| (ready && !_historySettled);
	_listsStateUpdates.fire({});
}

bool Session::listsConfirmedEmpty() const {
	return !_listsGated.current()
		&& _history.empty()
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
	const auto wanted = (_pollingCount > 0)
		|| _pending
		|| _sendUnresolved
		|| custody().pendingRotation;
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
	if (!streaming || stale(_stateRefreshedAt)) {
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
	if (stale(std::max(_historyRequestedAt, _historyRefreshedAt))) {
		refreshHistory();
	}
	refreshCollectibles();
	if ((_pending || _sendUnresolved || custody().pendingRotation)
		&& !_resolveRequestPending) {
		resolvePending();
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
	if (_rotating || custody().pendingRotation) {
		if (done) {
			done(FeeResult{ .error = SendError::AlreadySending });
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
	if (_sendState.current() != SendState::Idle
		|| _rotating
		|| custody().pendingRotation) {
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
		applyRotationSnapshot(snapshot, true);
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
		// failure of its own embedded resolve (refresh.rs:28) and still
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
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	const auto active = custody().matching(_publicKey);
	if (!active) {
		LOG(("Wallet Error: rotation stored without local custody."));
		fail(u"ROTATION_NO_CUSTODY"_q);
		return;
	}
	const auto lifecycle = _engine->lifecycle();
	const auto expected = _publicKey;
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
	}, [=, this](engine::WalletDescriptor descriptor) {
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
		if (record.publicKey != expected
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
		};
		if (!WriteCustodyStore(_session->local(), store)) {
			LOG(("Wallet Error: pending rotation write failed."));
			rollBack(u"ROTATION_STORE_FAILED"_q);
			return;
		}
		_custody = std::move(store);
		done();
	}, [=](EngineError error) {
		LOG(("Wallet Error: import_wallet for a rotation failed: %1"
			).arg(LifecycleErrorName(error)));
		fail(u"ROTATION_STORE_FAILED"_q);
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
	// the active record by the import-time check, so its descriptor is the
	// active record's identity under the pending's handle.
	if (const auto active = store.matching(_publicKey)) {
		const auto lifecycle = _engine->lifecycle();
		_engine->run([
			lifecycle,
			descriptor = DescriptorFromRecord(CustodyRecord{
				.recordId = pending->recordId,
				.address = active->address,
				.publicKey = active->publicKey,
				.network = active->network,
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
	const auto i = ranges::find(
		store.records,
		_publicKey,
		&CustodyRecord::publicKey);
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
	// delete the live secret.
	const auto lifecycle = _engine->lifecycle();
	const auto superseded = DescriptorFromRecord(*i);
	i->recordId = pending->recordId;
	i->secretRef = pending->secretRef;
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
	const auto i = ranges::find(
		store.records,
		_publicKey,
		&CustodyRecord::publicKey);
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
