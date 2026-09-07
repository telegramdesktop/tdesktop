/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_session.h"

#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "data/components/recent_money_recipients.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "gram/api/gram_api_emulate.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "tde2e/tde2e_api.h"
#include "ui/widgets/separate_panel.h"
#include "wallet/wallet_engine.h"
#include "wallet/wallet_onramp.h"
#include "wallet/wallet_phrase_shares.h"
#include "wallet/wallet_rates.h"
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
	Fn<void(const QString &)> fail;
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
	mtpRequestId id = 0;
	std::vector<Fn<void()>> done;
};

struct Session::PreparedRotation {
	std::vector<QString> words;
	std::string signedBoc;
	uint32 seqno = 0;
	uint64 validUntil = 0;
	int64 quotedFeeNano = 0;
};

struct PreparedSend {
	SendArgs args;
	std::shared_ptr<const wallet_engine::SendIntent> intent;
	int64 feeNano = 0;
	uint64 owner = 0;
	uint64 revision = 0;
	int generation = 0;
	std::optional<quint32> privateEpoch;
	QByteArray sender;
	std::shared_ptr<wallet_engine::WalletClient> client;
};

struct Session::PreviewRequest {
	uint64 owner = 0;
	uint64 revision = 0;
	int generation = 0;
	std::optional<quint32> privateEpoch;
	QByteArray sender;
	std::shared_ptr<wallet_engine::WalletClient> client;
	KeyAuthorization auth;
	SendArgs args;
	Fn<void(FeeResult)> done;
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
// The largest data_normal wallet.sendTransfer documents, inclusive.
constexpr auto kTransferDataMaxBytes = 16 * 1024;
// The lane follows a submitted id for as long as the engine can still
// see the message accepted (validity plus the resolution margin), one
// attempt per tick.
constexpr auto kSubmittedLookupAttempts = int(
	(kClientSendValiditySeconds + kClientResolutionMarginSeconds)
	* 1000
	/ uint64(kPollInterval));
// The throw-away rotation a quote emulates is a validly signed key-change
// message that leaves the device, and only its expiration bounds a stray
// replay of it, so it gets the shortest window that comfortably outlives one
// emulation round trip (the Wallet::Api deadline plus queueing); the fresh
// prepare keeps the engine's own send validity.
constexpr auto kRotationQuoteValiditySeconds = uint64(120);
constexpr auto kOwnershipProofSignatureSize = 64;

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
	} catch (const engine::wallet_lifecycle_error::TonConnectSigningFailed &) {
		return u"TonConnectSigningFailed"_q;
	} catch (const engine::wallet_lifecycle_error::SecretWalletMismatch &) {
		return u"SecretWalletMismatch"_q;
	} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &) {
		return u"ProtectedSecretHost"_q;
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
	} catch (...) {
	}
	return false;
}

[[nodiscard]] bool ReadAuthorized(
		Session &session,
		const KeyAuthorization &auth) {
	return auth.grant
		&& auth.grant->valid()
		&& session.vault().unlocked();
}

[[nodiscard]] bool SameCommentRecord(
		const CustodyRecord &a,
		const CustodyRecord &b) {
	return a.recordId == b.recordId
		&& a.address == b.address
		&& a.publicKey == b.publicKey
		&& a.network == b.network
		&& a.secretRef == b.secretRef
		&& a.active == b.active
		&& a.rotatedSinceBackup == b.rotatedSinceBackup;
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
		const auto count = first ? 35 : std::min(127, payload.size() - offset);
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

[[nodiscard]] KeyAuthorization TrackCommentInstallation(
		KeyAuthorization auth,
		const std::shared_ptr<KeyAuthorization> &installed) {
	installed->grant = auth.grant;
	if (const auto install = auth.install) {
		auth.install = [=](Fn<void(CustodyInstall)> ready) {
			install([=](CustodyInstall result) {
				installed->grant = result.grant;
				ready(std::move(result));
			});
		};
	}
	return auth;
}

struct DecryptedComment {
	SecureBytes text;
	CommentDecryptError error = CommentDecryptError::None;
};

[[nodiscard]] DecryptedComment DecryptCommentBody(
		const std::shared_ptr<engine::WalletClient> &client,
		const engine::DecryptCommentRequest &request) {
	using Error = CommentDecryptError;
	try {
		auto text = client->decrypt_comment(request);
		const auto wipe = gsl::finally([&] {
			OPENSSL_cleanse(text.data(), text.size());
		});
		return { .text = SecureBytes(bytes::make_span(text)) };
	} catch (const engine::wallet_client_error::EncryptedCommentUnavailable &) {
		return { .error = Error::DecryptionFailed };
	} catch (const engine::wallet_client_error::LocalSigningUnavailable &) {
		return { .error = Error::Unavailable };
	} catch (const engine::wallet_client_error::InvalidProtectedSecret &) {
		return { .error = Error::Unavailable };
	} catch (const engine::wallet_client_error::SendAlreadyInProgress &) {
		return { .error = Error::Unavailable };
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
	} catch (const engine::wallet_client_error
			::EncryptedCommentUnavailable &) {
		return SendError::CommentEncryptionUnavailable;
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
		: SendError::Failed;
}

[[nodiscard]] bool IsProtectedSecretNotFound(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_lifecycle_error
			::ProtectedSecretHost &hostError) {
		return hostError.kind == engine::ProtectedSecretHostErrorKind::kNotFound;
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
		const MTPWalletTransaction &item,
		const std::optional<TransferWalletIdentity> &identity) {
	const auto &data = item.data();
	auto result = TransferItem();
	result.source = TransferItem::Source::Server;
	result.id = qs(data.vid());
	result.walletIdentity = identity;
	SetDirectedAmount(result, data.vamount().v, data.is_incoming());
	result.feeNano = data.vfee().v;
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
			result.counterparty = CanonicalAddress(qs(data.vaddress()));
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
		}, [](const MTPDwalletTransactionPeerUnsupported &) {
			// Nothing is written, because the defaults are the row: a
			// Kind::Transfer with no counterparty renders through
			// RowContentFromItem's fall-through as a Deposit or a
			// Withdrawal by direction, with the date and the amount. That
			// is exactly the graceful degradation this constructor exists
			// for, so no lang key is invented for it.
		});
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
		const MTPDwallet_sentTransfer &data) {
	constexpr auto kMessageHashBytes = 32;
	const auto &hash = data.vmsg_hash().v;
	if (data.vtransaction_id().v.isEmpty()
		|| hash.size() != kMessageHashBytes) {
		return std::nullopt;
	}
	return TransferReceipt{
		.transactionId = qs(data.vtransaction_id()),
		.messageHash = hash,
		.gasless = data.is_gasless(),
		.gaslessLeft = data.vgasless_left().v,
		.gaslessResetAt = data.vgasless_reset_at().v,
	};
}

} // namespace

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
	const auto served = session
		? session->wallet().publicKey()
		: QByteArray();
	const auto known = !served.isEmpty();
	const auto backed = known
		&& session->wallet().capabilities().backupEnabled;
	for (const auto &record : store->records) {
		const auto active = known
			? (record.publicKey == served)
			: record.active;
		if (!active) {
			++result.parked;
		} else if (!known) {
			result.unknown = true;
		} else if (!backed || record.rotatedSinceBackup) {
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

// The forgot-passcode twin of WalletLossWarning(), and it lives here beside
// it on purpose. The model is shared and correct - both actions destroy the
// same keys - but the statements are not: the logout renderer's strings say
// that logging out is what destroys them, and the forgot box has just
// promised the reader they will not be logged out. So the two say the same
// facts about the same WalletLoss in their own words, in the same order, and
// a term added to one is missing from the other unless they are read
// together - which is why they are written together.
QString ForgottenPasscodeLoss(WalletLoss loss) {
	auto result = QString();
	const auto append = [&](const QString &line) {
		if (!result.isEmpty()) {
			result += u"\n\n"_q;
		}
		result += line;
	};
	if (loss.unbacked > 0) {
		append(tr::lng_wallet_passcode_forgot_local(
			tr::now,
			lt_count,
			loss.unbacked));
	}
	if (loss.parked > 0) {
		append(tr::lng_wallet_passcode_forgot_parked(
			tr::now,
			lt_count,
			loss.parked));
	}
	if (loss.rotating > 0) {
		append(tr::lng_wallet_passcode_forgot_rotating(
			tr::now,
			lt_count,
			loss.rotating));
	}
	if (loss.unknown) {
		append(tr::lng_wallet_passcode_forgot_unknown(tr::now));
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
, _stream(std::make_unique<Stream>(&_api, [=](StreamRefresh wanted) {
	applyStreamRefresh(wanted);
}))
, _pollTimer([=] { pollTick(); }) {
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
}

Session::~Session() {
	retireCommentScopes();
	_commentLifetime.destroy();
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
	_stateRequestedAt = crl::now();
	auto request = _stateApi.request(MTPwallet_GetState());
	auto &policy = done ? request.handleAllErrors() : request;
	_stateRequestId = policy.done([=](const MTPWalletState &result) {
		_stateRequestId = 0;
		if (done) {
			done(result);
		} else {
			applyState(result, false);
		}
	}).fail([=](const MTP::Error &error) {
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
		LOG(("Wallet Error: wallet.getState failed: %1").arg(error.type()));
		++_stateFailures;
		updateListsGate();
	}).send();
}

void Session::applyState(const MTPWalletState &state, bool pushed) {
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
		const auto identityChanged = (_address != parsed->raw)
			|| (_publicKey != data.vpublic_key().v);
		if (identityChanged) {
			retireCommentScopes();
			++_walletIdentityRevision;
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
		// A presence that was not Ready has already had its drain and its
		// first page from setPresence(); the case that write structurally
		// cannot see is a presence that stayed Ready while the served
		// identity changed. That is a different wallet, so both lanes leave
		// with the transfer submission in flight, the engine status returns
		// to its unknown value and the new wallet's head page is asked for at
		// once. It runs before reconcileCustody() because that reconciliation
		// may stop and restart the engine client, and a restart must find an
		// already-drained collectibles lane rather than have its first
		// delivery wiped afterwards.
		// A pushed state on the same wallet is the transfer notification the
		// server sends as a transfer progresses, so it is news about the
		// history lane alone and invalidates only that one. The arms are
		// ordered so that a push which also changed the identity takes the
		// first one and gets exactly one head page from the drain, never a second
		// one from the marker.
		if (wasReady && identityChanged) {
			clearHistory();
			clearCollectibles();
			_submission.reset();
			_engineStatus = AccountStatus::NonExisting;
			refreshHistory();
		} else if (wasReady && pushed) {
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

void Session::setPresence(Presence presence) {
	if (_presence.current() == presence) {
		return;
	}
	retireCommentScopes();
	if (_presence.current() == Presence::Ready) {
		++_walletIdentityRevision;
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
		clearHistory();
		clearCollectibles();
	}
	updatePollingState();
	if (presence == Presence::Ready) {
		refreshHistory();
	}
	_transferWalletIdentityChanges.fire({});
}

bool Session::revealsLocally() {
	ensureLoaded();
	return (_presence.current() == Presence::Ready)
		&& (_publicKey.size() == kCustodyPublicKeySize)
		&& (custody().matching(_publicKey) != nullptr);
}

VaultRuntime &Session::vault() {
	return _engine->vault();
}

bool Session::custodyBusy() const {
	return _phraseRevealing || _replacing || _backupChanging || _rotating;
}

std::shared_ptr<CommentScope> Session::createCommentScope(
		TransferItem target,
		rpl::lifetime &lifetime) {
	if (!target.walletIdentity
		|| !transferWalletIdentityCurrent(*target.walletIdentity)
		|| target.id.isEmpty()
		|| !target.commentEncrypted
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
	auto body = QByteArray();
	if (target.source == TransferItem::Source::Server
		&& target.encryptedFormat == TransferItem::EncryptedFormat::ServerPayload) {
		body = ServerCommentBody(target.encryptedPayload);
	} else if (target.source == TransferItem::Source::Engine
		&& target.encryptedFormat == TransferItem::EncryptedFormat::EngineBodyBoc) {
		body = target.encryptedPayload;
	}
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
	if (const auto record = _custody->matching(_publicKey)) {
		if (!_engine->client() || _clientRecordId != record->recordId) {
			return nullptr;
		}
		state->record = *record;
	} else if (_engine->client()) {
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

bool Session::commentAccessAvailable() const {
	return _custody.has_value()
		&& !_custodyReadFailed
		&& _custody->records.size() <= 1
		&& !_custody->pendingRotation
		&& !_clientStopping
		&& !_pending
		&& !_sendUnresolved
		&& _sendState.current() == SendState::Idle
		&& !ranges::any_of(_custody->records, [&](const CustodyRecord &record) {
			return record.publicKey != _publicKey;
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
	const auto current = _custody->matching(_publicKey);
	if ((current != nullptr) != state.record.has_value()
		|| (current && (!SameCommentRecord(*current, *state.record)
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
	using Error = CommentDecryptError;
	const auto finish = [=, this](CommentDecryptResult result) {
		if (!commentScopeCurrent(scope)) {
			result = { .error = Error::Cancelled };
		}
		if (done) {
			done(std::move(result));
		}
	};
	if (!commentScopeCurrent(scope)) {
		finish({ .error = Error::Cancelled });
		return;
	} else if (custodyBusy()
		|| !scope->_state->record
		|| !_engine->client()
		|| _clientRecordId != scope->_state->record->recordId) {
		finish({ .error = Error::Unavailable });
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		finish({ .error = Error::Locked });
		return;
	}
	const auto state = scope->_state;
	const auto client = _engine->client();
	const auto request = engine::DecryptCommentRequest{
		.sender = state->sender.toStdString(),
		.body = state->body.toStdString(),
	};
	_engine->run([
		state,
		client,
		request,
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
		if (!commentScopeCurrent(scope) || client != _engine->client()) {
			finish({ .error = Error::Cancelled });
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

void Session::revealPhrase(
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	if (const auto record = custody().matching(_publicKey)) {
		if (!ReadAuthorized(*this, auth)) {
			fail(u"PHRASE_VAULT_LOCKED"_q);
			return;
		}
		revealLocally(std::move(auth), *record, [=](
				std::vector<QString> words) {
			done(std::move(words), CustodyOutcome::Installed);
		}, fail);
	} else {
		revealFromShares(std::move(auth), std::move(password), done, fail);
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
	const auto initiatingPublicKey = record.publicKey;
	const auto lifecycle = _engine->lifecycle();
	const auto descriptor = DescriptorFromRecord(record);
	_engine->run([lifecycle, descriptor] {
		return lifecycle->reveal_recovery_phrase(descriptor);
	}, [=, grant = auth.grant](engine::RecoveryPhrase phrase) {
		auto words = SplitWords(QString::fromStdString(phrase.phrase));
		if (words.size() < 2) {
			LOG(("Wallet Error: local phrase reveal produced no words."));
			fail(u"PHRASE_EMPTY"_q);
			return;
		}
		done(std::move(words));
	}, [=, grant = auth.grant](EngineError error) {
		if (IsProtectedSecretNotFound(error)) {
			removeCustodyRecord(initiatingPublicKey);
		}
		LOG(("Wallet Error: local phrase reveal failed: %1"
			).arg(LifecycleErrorName(error)));
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
		std::shared_ptr<CommentScope> scope) {
	if (scope && !commentScopeCurrent(scope)) {
		fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		return;
	}
	if (scope && !_capabilities.current().canExportPhrase) {
		fail(u"PHRASE_STATE_UNKNOWN"_q);
		return;
	}
	using Flag = MTPwallet_exportSecretPhrase::Flag;
	const auto checked = password && *password;
	const auto pending = std::make_shared<bool>(true);
	const auto request = _stateApi.request(MTPwallet_ExportSecretPhrase(
		MTP_flags(checked ? Flag::f_password : Flag(0)),
		checked ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=, this](const MTPwallet_SecretPhraseParts &result) {
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
			LOG(("Wallet Error: wallet.exportSecretPhrase answered "
				"%1 holder(s).").arg(data.vdcs().v.size()));
			fail(u"PHRASE_PARTS_INVALID"_q);
			return;
		}
		fetchShareParts(auth, qs(data.vtoken()), *dcs, done, fail, scope);
	}).fail([=, this](const MTP::Error &error) {
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
		LOG(("Wallet Error: wallet.exportSecretPhrase failed: %1"
			).arg(error.type()));
		fail((scope && MTP::IgnoreError(error))
			? u"PHRASE_SILENT_ERROR"_q
			: error.type());
	}).handleFloodErrors().send();
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
		std::shared_ptr<CommentScope> scope) {
	if (scope && !commentScopeCurrent(scope)) {
		fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		return;
	}
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
		.fail = [=](const QString &error) {
			if (scope) {
				scope->_state->cancelPending = nullptr;
			}
			fail(error);
		},
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
			if (scope && !commentScopeCurrent(scope)) {
				FailShareFetch(
					_stateApi,
					_shareFetchTimer,
					state,
					u"PHRASE_ORIGIN_EXPIRED"_q);
				return;
			}
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
			if (scope) {
				scope->_state->cancelPending = nullptr;
			}
			restoreFromWords(
				auth,
				SplitWords(QString::fromUtf8(*seed)),
				done,
				base::take(state->fail),
				scope);
		}).fail([=, this](const MTP::Error &error) {
			if (!state->fail) {
				return;
			}
			state->requests[i] = 0;
			LOG(("Wallet Error: wallet.fetchEncryptedSecretPhrasePart "
				"failed: %1").arg(error.type()));
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

void Session::restoreFromWords(
		KeyAuthorization auth,
		std::vector<QString> words,
		Fn<void(std::vector<QString>, CustodyOutcome outcome)> done,
		Fn<void(const QString &)> fail,
		std::shared_ptr<CommentScope> scope) {
	if (scope && !commentScopeCurrent(scope)) {
		fail(u"PHRASE_ORIGIN_EXPIRED"_q);
		return;
	} else if (words.size() < 2) {
		LOG(("Wallet Error: reconstructed phrase has no words."));
		fail(u"PHRASE_EMPTY"_q);
		return;
	}
	const auto lifecycle = _engine->lifecycle();
	const auto expected = scope
		? scope->_state->target.walletIdentity->publicKey
		: _publicKey;
	// The resolved install travels into both continuations, which is what
	// holds the grant across the worker call: the runtime cleanses the key
	// as soon as the last handle goes, and the store runs on the worker.
	// Its `created` term is what tells a failure arm whether the header it
	// has to drop is one this store wrote.
	const auto store = [=, this](
			CustodyInstall install,
			std::vector<QString> phrase) {
		if (scope && !commentScopeCurrent(scope)) {
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
		const auto state = scope ? scope->_state : nullptr;
		_engine->run([
			lifecycle,
			request = std::move(request),
			stores,
			state,
			grant = install.grant,
			words = std::move(phrase)
		]() mutable -> std::optional<Restored> {
			if (state && (state->cancelled
				|| state->epoch != state->vault->clearEpoch()
				|| !grant->valid())) {
				return std::nullopt;
			}
			const auto recording = stores->record();
			auto descriptor = lifecycle->import_wallet(request);
			return Restored{ std::move(descriptor), std::move(words) };
		}, [=, this](std::optional<Restored> result) {
			if (!result) {
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				return;
			}
			auto restored = std::move(*result);
			const auto descriptor = restored.descriptor;
			auto record = RecordFromDescriptor(descriptor);
			const auto rollback = [=, this](Fn<void()> finished) {
				const auto cleanup = [=, this] {
					_engine->dropStoredSecrets(*stores);
					if (install.created) {
						dropCreatedVault();
					}
					finished();
				};
				_engine->run([lifecycle, descriptor] {
					lifecycle->delete_wallet(descriptor);
				}, cleanup, [=](EngineError) {
					cleanup();
				});
			};
			if (record.publicKey != expected
				|| (scope && (record.network != int(engine::Network::kMainnet)
					|| CanonicalAddress(record.address)
						!= scope->_state->target.walletIdentity->address))) {
				rollback([=] { fail(u"PHRASE_KEY_MISMATCH"_q); });
				return;
			} else if (scope && !commentScopeCurrent(scope)) {
				rollback([=] { fail(u"PHRASE_ORIGIN_EXPIRED"_q); });
				return;
			}
			if (scope) {
				record.active = true;
				scope->_state->record = record;
			}
			if (!persistCustody(record)) {
				if (scope) {
					scope->_state->record = std::nullopt;
				}
				rollback([=, words = std::move(restored.words)]() mutable {
					done(std::move(words), CustodyOutcome::WriteFailed);
				});
				return;
			} else if (scope && !commentScopeCurrent(scope)) {
				const auto current = custody().matching(record.publicKey);
				if (current && current->recordId == record.recordId) {
					removeCustodyRecord(record.publicKey);
				}
				rollback([=] { fail(u"PHRASE_ORIGIN_EXPIRED"_q); });
				return;
			}
			done(std::move(restored.words), CustodyOutcome::Installed);
		}, [=, this](EngineError error) {
			// The record goes before the header: a store creates the vault
			// header only together with the record it seals, so removing
			// the record first keeps that invariant true at every instant.
			// An import that failed before its own store recorded nothing,
			// so this removes nothing on the ordinary refusal.
			_engine->dropStoredSecrets(*stores);
			if (install.created) {
				dropCreatedVault();
			}
			if (scope && !commentScopeCurrent(scope)) {
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
				return;
			}
			const auto name = LifecycleErrorName(error);
			LOG(("Wallet Error: import_wallet failed: %1").arg(name));
			fail(IsVaultLocked(error)
				? u"PHRASE_VAULT_LOCKED"_q
				: (name == u"InvalidRecoveryPhrase"_q)
				? u"PHRASE_INVALID_PHRASE"_q
				: u"PHRASE_IMPORT_FAILED"_q);
		});
	};
	// The install ladder goes first whenever the flow carries one: a read
	// grant handed out by an open retention window must never carry a
	// silent store into a vault whose passcode the user has not just typed.
	if (const auto install = auth.install) {
		install(crl::guard(_session, [=, words = std::move(words)](
				CustodyInstall answer) mutable {
			if (scope && !commentScopeCurrent(scope)) {
				fail(u"PHRASE_ORIGIN_EXPIRED"_q);
			} else if (!answer.grant) {
				done(std::move(words), CustodyOutcome::Cancelled);
			} else {
				store(std::move(answer), std::move(words));
			}
		}));
	} else if (auth.grant && auth.grant->valid()) {
		store(CustodyInstall{ .grant = auth.grant }, std::move(words));
	} else {
		fail(u"PHRASE_VAULT_LOCKED"_q);
	}
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
	if (scope) {
		if (!commentScopeCurrent(scope) || scope->_state->record) {
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
	if (scope) {
		if (!commentScopeCurrent(scope) || scope->_state->record) {
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
	revealLocally(std::move(auth), *record, [=](
			std::vector<QString> words) {
		done(std::move(words), CustodyOutcome::Installed);
	}, fail);
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
		KeyAuthorization auth,
		Fn<void(std::vector<QByteArray>)> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	const auto matching = custody().matching(_publicKey);
	if (!matching) {
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
	retireCommentScopes();
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
		revealLocally(auth, record, [=](std::vector<QString> words) {
			auto parts = SealBackupParts(*keys, words);
			if (!parts) {
				LOG(("Wallet Error: backup parts could not be sealed."));
				fail(u"BACKUP_ENCRYPT_FAILED"_q);
				return;
			}
			done(std::move(*parts));
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

void Session::disableBackup(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
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
	if (!custody().matching(_publicKey)) {
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
		checked ? password->result : MTP_inputCheckPasswordEmpty()
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

void Session::enableBackup(
		std::vector<QByteArray> parts,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done,
		Fn<void(const QString &error)> fail) {
	ensureLoaded();
	if (custodyBusy() || custody().pendingRotation) {
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
		applyState(result, false);
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
	}, [=, this, grant = auth.grant](ThrowawayRotation throwaway) {
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
	}, [=, this, grant = auth.grant](EngineError error) {
		LOG(("Wallet Error: engine prepare_key_rotation (quote) failed: %1"
			).arg(error.message));
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
	if (!ReadAuthorized(*this, auth)) {
		if (fail) {
			fail(u"ROTATION_VAULT_LOCKED"_q);
		}
		return;
	}
	retireCommentScopes();
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
	}, [=, this, grant = auth.grant](engine::PreparedKeyRotation prepared) {
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
	}, [=, grant = auth.grant](EngineError error) {
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
		KeyAuthorization auth,
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
	storePendingRotation(std::move(auth), [=, this] {
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

rpl::producer<> Session::keyProtectionUpdates() const {
	return _keyProtectionUpdates.events();
}

void Session::notifyKeyProtectionChanged() {
	_keyProtectionUpdates.fire({});
}

void Session::dropCustodyAfterForgottenPasscode() {
	vault().clear();
	_custody = std::nullopt;
	updateDeviceCustodyState();
	notifyKeyProtectionChanged();
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
	_custodyReadFailed = false;
	_custody = std::move(store);
	updateDeviceCustodyState();
	return true;
}

// Undoes the vault the store that just failed had created. Only that store
// carries a created install, and nothing else writes the header while it
// runs, so the header this drops can only be the one it wrote: no epoch and
// no ownership check is needed. It runs beside the delete_wallet the same
// failure fires, not after it, so a delete that fails leaves no vault.
void Session::dropCreatedVault() {
	// removeWalletEngineValue answers false only for a key that is not in
	// the map, so this boolean is "there was a header to drop" and never
	// "the removal failed": at that API there is no removal-failed answer.
	// An engine failure before its own store - an invalid recovery phrase
	// is the common one - leaves no header, which is the ordinary case
	// here and states nothing.
	if (RemoveVaultHeader(_session->local())) {
		LOG(("Wallet Info: dropped the vault header a failed store "
			"created."));
	}
}

void Session::replaceWithNew(
		std::optional<Core::CloudPasswordResult> password,
		Fn<void(CustodyOutcome)> done,
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
	auto oldRecord = std::optional<CustodyRecord>();
	if (const auto record = custody().matching(_publicKey)) {
		oldRecord = *record;
	}
	const auto lifecycle = _engine->lifecycle();
	// The store authority is resolved the way restoreFromWords resolves it,
	// and for the same reason the install ladder runs first: a cancelled
	// chooser has to abort before wallet.replaceWallet is sent, so nothing
	// on the server can name a key this device never stored.
	const auto store = [=, this](
			CustodyInstall install,
			std::vector<QString> phrase) {
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
			const auto record = RecordFromDescriptor(descriptor);
			const auto created = install.created;
			const auto abandon = [=, this](const QString &error) {
				if (created) {
					dropCreatedVault();
				}
				_engine->run([lifecycle, descriptor] {
					lifecycle->delete_wallet(descriptor);
				}, [=] {
					fail(error);
				}, [=](EngineError) {
					LOG(("Wallet Error: delete_wallet after an abandoned "
						"import failed."));
					fail(error);
				});
			};
			const auto applied = [=, this](const MTPWalletState &state) {
				const auto answered = (state.type() == mtpc_walletState)
					? state.c_walletState().vpublic_key().v
					: QByteArray();
				if (answered != record.publicKey) {
					LOG(("Wallet Error: wallet.replaceWallet answered "
						"another key."));
					if (created) {
						dropCreatedVault();
					}
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
				finishConfirmedReplace(
					oldRecord,
					record,
					state,
					done,
					fail);
			};
			const auto send = [=, this](
					TimeId timestamp,
					const std::vector<uint8_t> &signature) {
				sendReplaceWallet(
					MTP_inputWalletImported(
						MTP_bytes(record.publicKey),
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
						recoverImportedReplace(record.publicKey, applied, abandon);
					});
			};
			const auto sign = [=, this](
					const MTPDwallet_proofChallenge &challenge) {
				const auto timestamp = base::unixtime::now();
				if (timestamp <= 0) {
					LOG(("Wallet Error: no usable timestamp for the ownership "
						"proof."));
					abandon(u"REPLACE_PROOF_FAILED"_q);
					return;
				}
				auto request = engine::TonConnectProofSignRequest{
					.descriptor = descriptor,
					.domain = challenge.vdomain().v.toStdString(),
					.timestamp = uint64_t(timestamp),
					.payload = challenge.vpayload().v.toStdString(),
				};
				_engine->run([lifecycle, request = std::move(request)] {
					return lifecycle->sign_ton_connect_proof(request);
				}, [=, grant = install.grant](engine::TonConnectProofSignature proof) {
					const auto size = int(proof.signature.size());
					if (size != kOwnershipProofSignatureSize) {
						LOG(("Wallet Error: the ownership proof signature has "
							"%1 bytes.").arg(size));
						abandon(u"REPLACE_PROOF_FAILED"_q);
						return;
					}
					send(timestamp, proof.signature);
				}, [=, grant = install.grant](EngineError error) {
					LOG(("Wallet Error: sign_ton_connect_proof failed: %1"
						).arg(LifecycleErrorName(error)));
					abandon(IsVaultLocked(error)
						? u"REPLACE_VAULT_LOCKED"_q
						: u"REPLACE_PROOF_FAILED"_q);
				});
			};
			// The challenge lives 300 seconds and admits one attempt, so it
			// is fetched only here - after the install ladder answered and
			// the engine stored the words - and signed at once. A cancelled
			// chooser, a refused phrase or a failed import never reaches
			// this continuation and issues no challenge. A resend of this
			// request mints a new challenge server-side and only the final
			// answer is used, so it keeps the ordinary flood policy; the
			// send that spends the proof does not, see sendReplaceWallet.
			_stateApi.request(MTPwallet_GetProofChallenge(
			)).done([=](const MTPwallet_ProofChallenge &result) {
				sign(result.data());
			}).fail([=](const MTP::Error &error) {
				LOG(("Wallet Error: wallet.getProofChallenge failed: %1"
					).arg(error.type()));
				abandon(u"REPLACE_PROOF_FAILED"_q);
			}).handleFloodErrors().send();
		}, [=, this](EngineError error) {
			_engine->dropStoredSecrets(*stores);
			if (install.created) {
				dropCreatedVault();
			}
			const auto name = LifecycleErrorName(error);
			LOG(("Wallet Error: import_wallet failed: %1").arg(name));
			fail(IsVaultLocked(error)
				? u"REPLACE_VAULT_LOCKED"_q
				: (name == u"InvalidRecoveryPhrase"_q)
				? u"REPLACE_INVALID_PHRASE"_q
				: u"REPLACE_IMPORT_FAILED"_q);
		});
	};
	if (const auto install = auth.install) {
		install([=, words = std::move(words)](
				CustodyInstall answer) mutable {
			if (!answer.grant) {
				fail(u"REPLACE_INSTALL_CANCELLED"_q);
			} else {
				store(std::move(answer), std::move(words));
			}
		});
	} else if (auth.grant && auth.grant->valid()) {
		store(CustodyInstall{ .grant = auth.grant }, std::move(words));
	} else {
		fail(u"REPLACE_VAULT_LOCKED"_q);
	}
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
		QByteArray publicKey,
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
		if (data.vpublic_key().v.size() != kCustodyPublicKeySize
			|| !ParseAddress(qs(data.vaddress()))) {
			unconfirmed();
			return;
		}
		if (data.vpublic_key().v != publicKey) {
			applyState(state, false);
			abandon(u"REPLACE_KEY_MISMATCH"_q);
			return;
		}
		applied(state);
	}, unconfirmed);
}

void Session::finishConfirmedReplace(
		std::optional<CustodyRecord> oldRecord,
		std::optional<CustodyRecord> newActive,
		const MTPWalletState &state,
		Fn<void(CustodyOutcome)> done,
		Fn<void(const QString &)> fail) {
	applyState(state, false);
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
			// wallet.replaceWallet has already succeeded and applyState() has
			// already run, so the replacement is real and cannot be taken
			// back: this answers done, not fail. The delete_wallet above is
			// local cleanup of a secret nothing points at, not a retraction,
			// and only the custody install on this device did not happen.
			done(CustodyOutcome::WriteFailed);
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
	done(CustodyOutcome::Installed);
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
		retireCommentScopes();
		_clientStopping = true;
		_engine->stopClient([this] {
			_clientStopping = false;
			_clientRecordId = QString();
			syncEngineClient();
		});
		retirePreviews(SendError::SigningUnavailable);
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
	retireCommentScopes();
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
	_submission.reset();
	_sendState = SendState::Idle;
	_pollingCount = 0;
	_pollTimer.cancel();
	_stream->stop();
	_sendUnresolved = false;
	updateListsGate();
	retirePreviews(SendError::Failed);
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
	if (!TerminalSendPhase(snapshot.send.phase)) {
		retireCommentScopes();
	}
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
		_submission.reset();
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

void Session::setHistory(std::vector<TransferItem> &&list) {
	_history = std::move(list);
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
		MTP_string(more ? _historyNextOffset : QString()),
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
		}
		FinishHistoryWaiters(base::take(request->done));
		if (weak && current && historyRequestCurrent(*request)) {
			refreshStaleHistory();
		}
	}).handleAllErrors().send();
}

bool Session::historyRequestCurrent(const HistoryRequest &request) const {
	return request.generation == _networkGeneration
		&& request.identityRevision == _walletIdentityRevision
		&& _presence.current() == Presence::Ready
		&& request.identity == transferWalletIdentity();
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
	_historySettled = true;
	auto loaded = HistoryFromServer(data.vtransactions().v, request.identity);
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
	if (weak && historyRequestCurrent(request)) {
		dropSubmittedIfListed();
		updateListsGate();
	}
}

void Session::clearHistory() {
	const auto request = base::take(_historyRequest);
	if (request) {
		_stateApi.request(request->id).cancel();
	}
	_stateApi.request(base::take(_lookupRequestId)).cancel();
	_history.clear();
	_historyHasNext = false;
	_historyNextOffset = QString();
	_historyRefreshedAt = 0;
	_historyRequestedAt = 0;
	_historySettled = false;
	_historyUnreachable = false;
	_historyPaged = false;
	_historyStale = false;
	_lookup.reset();
	_submitted.reset();
	// _historySettled and _historyUnreachable, cleared just above, are the
	// gate's two history terms, so this drain is the only point at which an
	// emptied list and the gate the previous page settled could be read
	// together. Recomputing here keeps this lane's own publication from
	// ever being evaluated against the gate of the wallet whose rows just
	// left: an open gate over two empty lists is listsConfirmedEmpty().
	updateListsGate();
	_historyUpdates.fire({});
	if (request) {
		FinishHistoryWaiters(base::take(request->done));
	}
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
		|| _historyRequest
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
		refreshStaleHistory();
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
		|| _lookup
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
	lookupSubmittedTransaction();
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
	refreshStaleHistory();
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

auto Session::lastTransferReceipt() const
-> const std::optional<TransferReceipt> & {
	return _lastReceipt;
}

auto Session::submittedTransaction() const
-> const std::optional<TransferItem> & {
	return _submitted;
}

int SendCommentBytes(const QString &text) {
	return text.toUtf8().size();
}

bool SendCommentFits(const QString &text) {
	return SendCommentBytes(text) <= kSendCommentMaxBytes;
}

int64 TransferMinNanosFromConfig(float64 configured) {
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
	return valid ? int64(configured) : kTransferMinNanosDefault;
}

int64 TransferMinNanos(not_null<Main::Session*> session) {
	return TransferMinNanosFromConfig(session->appConfig().get<float64>(
		u"wallet_transfer_min_nanos"_q,
		float64(kTransferMinNanosDefault)));
}

bool TransferAmountBelowMinimum(int64 amountNano, int64 minNanos) {
	return (amountNano > 0) && (amountNano < minNanos);
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
	const auto inputError = !SendCommentFits(args.comment.text)
		? SendError::CommentTooLong
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
	const auto privateEpoch = isPrivate
		? std::make_optional(vault().clearEpoch())
		: std::nullopt;
	if (isPrivate && !ReadAuthorized(*this, auth)) {
		cancelFeeEstimate(owner);
		if (done) {
			done(FeeResult{ .error = SendError::Locked });
		}
		return;
	}
	ensureLoaded();
	const auto i = _preview->owners.find(owner);
	if (i == end(_preview->owners)) {
		return;
	}
	auto request = PreviewRequest{
		.owner = owner,
		.revision = ++i->second,
		.generation = _networkGeneration,
		.privateEpoch = privateEpoch,
		.sender = _publicKey,
		.client = _engine->client(),
		.auth = isPrivate ? std::move(auth) : KeyAuthorization(),
		.args = args,
		.done = std::move(done),
	};
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

void Session::retirePreviewOwner(uint64 owner) {
	cancelFeeEstimate(owner);
	_preview->owners.remove(owner);
}

bool Session::previewCurrent(const PreviewRequest &request) const {
	const auto i = _preview->owners.find(request.owner);
	return i != end(_preview->owners) && i->second == request.revision;
}

SendError Session::previewError(const PreviewRequest &request) {
	if (request.privateEpoch
		&& (*request.privateEpoch != vault().clearEpoch()
			|| !ReadAuthorized(*this, request.auth))) {
		return SendError::Locked;
	} else if (request.generation != _networkGeneration
		|| request.sender != _publicKey) {
		return SendError::Failed;
	} else if (_presence.current() != Presence::Ready
		|| request.sender.size() != kCustodyPublicKeySize
		|| request.args.amountNano <= 0
		|| request.args.destination.isEmpty()) {
		return SendError::InvalidRequest;
	} else if (TransferAmountBelowMinimum(
			request.args.amountNano,
			TransferMinNanos(_session))) {
		return SendError::AmountTooSmall;
	} else if (_clientStopping
		|| !request.client
		|| request.client != _engine->client()) {
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
		if (error != SendError::None) {
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
		if (request.args.comment.text.isEmpty()) {
			previewPrepared(flight, engine::SendMessageBody::kEmpty{});
		} else if (request.args.comment.isPublic) {
			previewPrepared(flight, engine::SendMessageBody::kComment{
				.text = request.args.comment.text.toUtf8().toStdString(),
			});
		} else {
			const auto client = request.client;
			auto encrypt = engine::CreateEncryptedCommentRequest{
				.recipient = FormatFriendly(
					request.args.destination,
					request.args.bounce).toStdString(),
				.comment = request.args.comment.text.toUtf8().toStdString(),
			};
			_engine->run([client, encrypt = std::move(encrypt)] {
				return client->create_encrypted_comment(encrypt);
			}, [=, this](engine::Boc body) {
				previewPrepared(flight, engine::SendMessageBody::kRawPayload{
					.boc = std::move(body),
				});
			}, [=, this](EngineError error) {
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
	active.intent = std::make_shared<const engine::SendIntent>(
		IntentFromArgs(active.request.args, std::move(body)));
	active.stage = PreviewState::Flight::Stage::Previewing;
	const auto client = active.request.client;
	auto request = engine::SendPreviewRequest{ .intent = *active.intent };
	_engine->run([client, request = std::move(request)] {
		return client->preview_send(request);
	}, [=, this](engine::SendPreview preview) {
		const auto fee = DecimalInt64(preview.emulation.wallet_fees_nanograms);
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
		if (error != SendError::None) {
			flight.result = FeeResult{ .error = error };
		} else if (flight.result.error == SendError::None
			&& flight.intent
			&& !flight.cancelIssued) {
			flight.result.prepared = std::make_shared<const PreparedSend>(
				PreparedSend{
					.args = flight.request.args,
					.intent = std::move(flight.intent),
					.feeNano = flight.result.feeNano,
					.owner = flight.request.owner,
					.revision = flight.request.revision,
					.generation = flight.request.generation,
					.privateEpoch = flight.request.privateEpoch,
					.sender = flight.request.sender,
					.client = flight.request.client,
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

void Session::send(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		Fn<void(SendError)> done) {
	const auto fail = [&](SendError error) {
		if (done) {
			done(error);
		}
	};
	if (!prepared || !prepared->intent || !_preview) {
		fail(SendError::InvalidRequest);
		return;
	}
	const auto owner = _preview->owners.find(prepared->owner);
	if (owner == end(_preview->owners)
		|| owner->second != prepared->revision) {
		fail(SendError::InvalidRequest);
		return;
	}
	const auto &args = prepared->args;
	if (!SendCommentFits(args.comment.text)) {
		fail(SendError::CommentTooLong);
		return;
	} else if (args.amountNano <= 0
		|| FormatFriendly(args.destination, args.bounce).isEmpty()
		|| prepared->feeNano < 0) {
		fail(SendError::InvalidRequest);
		return;
	} else if (TransferAmountBelowMinimum(
			args.amountNano,
			TransferMinNanos(_session))) {
		fail(SendError::AmountTooSmall);
		return;
	}
	if (prepared->privateEpoch
		&& *prepared->privateEpoch != vault().clearEpoch()) {
		fail(SendError::Locked);
		return;
	} else if (_presence.current() != Presence::Ready
		|| prepared->generation != _networkGeneration
		|| prepared->sender != _publicKey
		|| prepared->sender.size() != kCustodyPublicKeySize) {
		fail(SendError::Failed);
		return;
	} else if (_clientStopping
		|| !prepared->client
		|| prepared->client != _engine->client()) {
		fail(SendError::SigningUnavailable);
		return;
	} else if (_sendState.current() != SendState::Idle
		|| _rotating
		|| custody().pendingRotation) {
		fail(SendError::AlreadySending);
		return;
	} else if (_pending || _sendUnresolved) {
		fail(SendError::PreviousUnresolved);
		return;
	} else if (!ReadAuthorized(*this, auth)) {
		fail(SendError::Locked);
		return;
	}
	const auto balance = _balanceNano.current();
	if (args.amountNano > balance) {
		fail(SendError::InsufficientBalance);
		return;
	} else if (prepared->feeNano > balance - args.amountNano) {
		fail(SendError::InsufficientFees);
		return;
	}
	++owner->second;
	const auto operationId = NewRecordId();
	const auto client = prepared->client;
	const auto generation = prepared->generation;
	const auto destination = args.destination;
	const auto amountNano = args.amountNano;
	const auto userId = args.userId;
	const auto comment = args.comment.isPublic
		? args.comment.text
		: QString();
	const auto weak = base::make_weak(_engine.get());
	const auto recordPending = [=, this](SendError answer) {
		_pending = PendingSendInfo{
			.posted = base::unixtime::now(),
			.amountNano = amountNano,
			.destination = destination,
			.comment = comment,
		};
		_sendState = SendState::Pending;
		if (!weak) {
			return;
		}
		updatePollingState();
		requestEngineRefresh();
		if (done) {
			done(answer);
		}
	};
	const auto recordUnknown = [=, this] {
		if (_submission) {
			_submission->hostAnswered = true;
		}
		dropSubmittedLookup();
		startSubmittedLookup();
		recordPending(SendError::SubmissionUnknown);
	};
	auto request = engine::SendRequest{
		.operation_id = operationId,
		.force = false,
		.intent = *prepared->intent,
	};
	const auto route = std::make_shared<TransferSubmission>([=, this](
			QByteArray boc,
			Fn<void(TransferSubmissionAnswer)> answer) {
		submitTransfer(
			operationId,
			generation,
			std::move(boc),
			std::move(answer));
	});
	_submission = TransferSubmissionState{
		.operationId = operationId,
		.sender = prepared->sender,
	};
	_engine->run([client, request = std::move(request), route] {
		const auto recording = route->record();
		return client->send(request);
	}, [=, this, grant = auth.grant](engine::SendResult result) {
		if (generation != _networkGeneration) {
			return;
		}
		if (result.operation_id != operationId) {
			LOG(("Wallet Error: engine send result names another operation."));
		}
		switch (result.phase) {
		case engine::SendPhase::kSubmitted:
			if (userId) {
				const auto user = _session->data().userLoaded(userId);
				if (user && !user->isSelf()) {
					_session->recentMoneyRecipients().bump(user);
					if (!weak) {
						return;
					}
				}
			}
			dropSubmittedLookup();
			startSubmittedLookup();
			_submission.reset();
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
			const auto submission = base::take(_submission);
			const auto refusal = submission
				? submission->refusal.value_or(SendError::Failed)
				: SendError::Failed;
			_sendState = SendState::Idle;
			LOG(("Wallet Error: engine send ended in phase %1 (%2)."
				).arg(int(result.phase)).arg(int(refusal)));
			if (weak && done) {
				done(refusal);
			}
		} return;
		}
	}, [=, this, grant = auth.grant](EngineError error) {
		if (generation != _networkGeneration) {
			return;
		}
		if (IsSubmissionUnknown(error)) {
			recordUnknown();
			return;
		}
		_submission.reset();
		const auto failed = SendErrorFrom(error);
		LOG(("Wallet Error: engine send failed (%1).").arg(int(failed)));
		_sendState = SendState::Idle;
		if (weak && done) {
			done(failed);
		}
	});
	_sendState = SendState::Sending;
}

void Session::submitTransfer(
		std::string operationId,
		int generation,
		QByteArray boc,
		Fn<void(TransferSubmissionAnswer)> done) {
	if (generation != _networkGeneration
		|| !_submission
		|| _submission->operationId != operationId
		|| _submission->sender != _publicKey) {
		LOG(("Wallet Error: transfer submission refused for a stale "
			"operation or wallet."));
		done({
			TransferSubmissionOutcome::Rejected,
			u"WALLET_TRANSFER_STALE"_q,
		});
		return;
	} else if (boc.isEmpty() || boc.size() > kTransferDataMaxBytes) {
		_submission->refusal = SendError::DataInvalid;
		LOG(("Wallet Error: transfer data of %1 bytes refused before the "
			"RPC.").arg(boc.size()));
		done({
			TransferSubmissionOutcome::Rejected,
			u"WALLET_TRANSFER_DATA_INVALID"_q,
		});
		return;
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
	_stateApi.request(MTPwallet_SendTransfer(
		MTP_flags(0),
		MTP_bytes(boc),
		MTPbytes()
	)).done([=](const MTPwallet_SentTransfer &result) {
		const auto receipt = ReceiptFromServer(result.data());
		if (!receipt) {
			LOG(("Wallet Error: wallet.sentTransfer receipt unusable."));
			done({
				TransferSubmissionOutcome::Uncertain,
				u"WALLET_TRANSFER_RECEIPT_INVALID"_q,
			});
			return;
		}
		bindTransferReceipt(operationId, generation, *receipt);
		done({ TransferSubmissionOutcome::Accepted });
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.sendTransfer failed: %1"
			).arg(error.type()));
		const auto refusal = DefiniteTransferRefusal(error);
		if (!refusal) {
			done({ TransferSubmissionOutcome::Uncertain, error.type() });
			return;
		} else if (_submission
			&& _submission->operationId == operationId
			&& generation == _networkGeneration) {
			_submission->refusal = *refusal;
		}
		done({ TransferSubmissionOutcome::Rejected, error.type() });
	}).handleAllErrors().send();
}

void Session::bindTransferReceipt(
		const std::string &operationId,
		int generation,
		TransferReceipt receipt) {
	if (!_submission
		|| _submission->operationId != operationId
		|| _submission->sender != _publicKey
		|| generation != _networkGeneration) {
		return;
	}
	_submission->receipt = receipt;
	_lastReceipt = std::move(receipt);
	// A receipt landing after the engine already answered the send as
	// unknown belongs to that still-unresolved operation, so the lookup
	// starts for it right here and never for a new operation; a receipt
	// that lands first waits for the engine's own kSubmitted answer,
	// which starts the lookup once the operation is recorded as pending.
	if (_submission->hostAnswered && (_pending || _sendUnresolved)) {
		startSubmittedLookup();
	}
}

void Session::startSubmittedLookup() {
	if (_lookup
		|| _submitted
		|| !_submission
		|| !_submission->receipt
		|| _submission->sender != _publicKey) {
		return;
	}
	_lookup = SubmittedLookup{
		.sender = _submission->sender,
		.transactionId = _submission->receipt->transactionId,
	};
	updatePollingState();
}

void Session::lookupSubmittedTransaction() {
	if (!_lookup
		|| _lookupRequestId
		|| (_presence.current() != Presence::Ready)) {
		return;
	} else if (_lookup->sender != _publicKey
		|| _lookup->attempts >= kSubmittedLookupAttempts) {
		LOG(("Wallet: submitted transaction lookup stopped after %1 of %2 "
			"attempts."
			).arg(_lookup->attempts).arg(kSubmittedLookupAttempts));
		_lookup.reset();
		updatePollingState();
		return;
	}
	++_lookup->attempts;
	const auto generation = _networkGeneration;
	const auto transactionId = _lookup->transactionId;
	const auto identity = transferWalletIdentity();
	_lookupRequestId = _stateApi.request(MTPwallet_GetTransactionsByIDs(
		MTP_vector<MTPstring>(1, MTP_string(transactionId))
	)).done([=](const MTPwallet_Transactions &result) {
		_lookupRequestId = 0;
		if (generation != _networkGeneration
			|| !_lookup
			|| _lookup->transactionId != transactionId) {
			return;
		}
		applySubmittedLookup(result, identity);
	}).fail([=](const MTP::Error &error) {
		_lookupRequestId = 0;
		LOG(("Wallet Error: wallet.getTransactionsByIDs failed: %1"
			).arg(error.type()));
	}).handleAllErrors().send();
}

void Session::applySubmittedLookup(
		const MTPwallet_Transactions &result,
		std::optional<TransferWalletIdentity> identity) {
	const auto &data = result.data();
	_session->data().processUsers(data.vusers());
	_session->data().processChats(data.vchats());
	// The answer's balance and next_offset are read by neither this lane
	// nor applyTransactions(): the state lane and the engine refresh are
	// the balance authority, and a by-id answer is not the paged feed, so
	// its offset would page a list that nobody renders.
	auto loaded = HistoryFromServer(data.vtransactions().v, identity);
	const auto found = ranges::find(
		loaded,
		_lookup->transactionId,
		&TransferItem::id);
	if (found == end(loaded)) {
		return;
	}
	_lookup.reset();
	if (!ranges::contains(_history, found->id, &TransferItem::id)) {
		_submitted = std::move(*found);
		_historyUpdates.fire({});
	}
	updatePollingState();
}

void Session::dropSubmittedIfListed() {
	if (_submitted
		&& ranges::contains(_history, _submitted->id, &TransferItem::id)) {
		_submitted.reset();
		_historyUpdates.fire({});
	}
}

void Session::dropSubmittedLookup() {
	// A newly recorded operation supersedes the lane and the projection of
	// the previous transfer. That transfer has already resolved, because a
	// send is refused while one is unresolved, so nothing is lost when they
	// leave; kept, the served row would hide the new pending row and hold
	// the lane's single slot, so the new id would never be followed. The
	// resolved transfer's row returns through the head page, exactly as it
	// does after a restart. clearHistory() drains the same members together
	// with the list they describe.
	_stateApi.request(base::take(_lookupRequestId)).cancel();
	_lookup.reset();
	if (base::take(_submitted)) {
		_historyUpdates.fire({});
	}
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
	_submission.reset();
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
		KeyAuthorization auth,
		Fn<void()> done,
		Fn<void(const QString &)> fail) {
	const auto active = custody().matching(_publicKey);
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
