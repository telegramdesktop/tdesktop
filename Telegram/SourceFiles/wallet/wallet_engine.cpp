/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_engine.h"

#include "core/application.h"
#include "gram/api/gram_api_request.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "wallet/wallet_api.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_vault.h"

#include "wallet_engine.hpp"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>

#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace Wallet {
namespace {

namespace engine = wallet_engine;

constexpr auto kMaxTrackedEarlyCancels = 64;
constexpr auto kRoutedSendTimeout = std::chrono::milliseconds(40000);

// The open recordings of the engine call running on this thread.
thread_local EngineSecretStores *t_recordingStores = nullptr;
thread_local SecretReadWatch *t_secretReadWatch = nullptr;
thread_local std::shared_ptr<TransferSubmission> t_transferSubmission;
thread_local std::optional<QByteArray> t_transferGasless;

template <typename Kind>
struct HostErrorFor;

template <>
struct HostErrorFor<engine::StatuslessHostErrorKind> {
	using Failed = engine::statusless_host_error::Failed;
};

template <>
struct HostErrorFor<engine::ProtectedSecretHostErrorKind> {
	using Failed = engine::protected_secret_host_error::Failed;
};

template <>
struct HostErrorFor<engine::JournalHostErrorKind> {
	using Failed = engine::journal_host_error::Failed;
};

template <typename Kind>
[[nodiscard]] auto HostFailed(Kind kind, const QString &diagnostic) {
	auto result = typename HostErrorFor<Kind>::Failed(
		diagnostic.toStdString());
	result.kind = kind;
	result.diagnostic = diagnostic.toStdString();
	return result;
}

[[nodiscard]] bool GoodStorageKeyPart(const std::string &part) {
	if (part.empty()) {
		return false;
	}
	for (const auto ch : part) {
		if (ch == '/' || (uchar(ch) >= 0x80)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QByteArray ToByteArray(const std::vector<uint8_t> &bytes) {
	return QByteArray(
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size());
}

[[nodiscard]] std::vector<uint8_t> ToByteVector(const QByteArray &bytes) {
	return std::vector<uint8_t>(
		bytes.constData(),
		bytes.constData() + bytes.size());
}

[[nodiscard]] std::vector<uint8_t> ToByteVector(bytes::const_span data) {
	const auto begin = reinterpret_cast<const uint8_t*>(data.data());
	return std::vector<uint8_t>(begin, begin + data.size());
}

[[nodiscard]] bool IsSendBocRequest(const Gram::HttpRequest &gram) {
	if (!gram.post || gram.endpoint != u"/api/v2/jsonRPC"_q) {
		return false;
	}
	const auto object = QJsonDocument::fromJson(gram.payload).object();
	const auto method = object.value(u"method"_q);
	return method.isString() && (method.toString() == u"sendBoc"_q);
}

[[nodiscard]] QByteArray RoutedSendBoc(const Gram::HttpRequest &gram) {
	const auto object = QJsonDocument::fromJson(gram.payload).object();
	const auto boc = object.value(u"params"_q).toObject().value(u"boc"_q);
	if (!boc.isString()) {
		return QByteArray();
	}
	auto result = QByteArray::fromBase64Encoding(
		boc.toString().toLatin1(),
		QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
	return result ? std::move(result.decoded) : QByteArray();
}

[[nodiscard]] std::vector<uint8_t> AcceptedSendBody() {
	return ToByteVector(QJsonDocument(QJsonObject{
		{ u"ok"_q, true },
		{ u"result"_q, QJsonObject{ { u"@type"_q, u"ok"_q } } },
	}).toJson(QJsonDocument::Compact));
}

[[nodiscard]] std::vector<uint8_t> EmptyPendingListBody() {
	return ToByteVector(QJsonDocument(QJsonObject{
		{ u"transactions"_q, QJsonArray() },
	}).toJson(QJsonDocument::Compact));
}

// A definite refusal is answered as a JSON-RPC error OBJECT and never as
// {"ok":false,...}: the engine's status-less transport intercepts an
// ok:false body carrying a top-level code, and a scalar top-level error,
// before its send parser sees them, and reads a code-less one as an
// ambiguous provider failure - which would turn a definite refusal into
// SubmissionUnknown. The structured error object passes that interception
// untouched and the send parser reads it as a rejection, so this is the
// one shape both layers classify as definite, and it fabricates no HTTP
// status the proxy never carried.
[[nodiscard]] std::vector<uint8_t> RejectedSendBody(
		uint64 requestId,
		const QString &token) {
	return ToByteVector(QJsonDocument(QJsonObject{
		{ u"jsonrpc"_q, u"2.0"_q },
		{ u"id"_q, QString::number(requestId) },
		{ u"error"_q, QJsonObject{
			{ u"code"_q, -32000 },
			{ u"message"_q, token },
		} },
	}).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString SecretStorageKey(
		const engine::ProtectedSecretRef &reference) {
	if (!GoodStorageKeyPart(reference.value)) {
		throw HostFailed(
			engine::ProtectedSecretHostErrorKind::kPolicyViolation,
			u"invalid secret reference"_q);
	}
	return VaultSecretStorageKey(QString::fromStdString(reference.value));
}

[[nodiscard]] QString JournalStorageKey(const engine::JournalKey &key) {
	if (!GoodStorageKeyPart(key.record_id)
		|| !GoodStorageKeyPart(key.slot)) {
		throw HostFailed(
			engine::JournalHostErrorKind::kOther,
			u"invalid journal key"_q);
	}
	return u"journal/"_q
		+ QString::fromStdString(key.record_id)
		+ '/'
		+ QString::fromStdString(key.slot);
}

[[nodiscard]] QByteArray SerializeJournalRecord(
		const engine::JournalRecord &record) {
	auto result = Serialize::ByteArrayWriter();
	result << quint64(record.version) << ToByteArray(record.payload);
	return std::move(result).result();
}

[[nodiscard]] std::optional<engine::JournalRecord> DeserializeJournalRecord(
		const QByteArray &serialized) {
	auto stream = Serialize::ByteArrayReader(serialized);
	auto version = quint64();
	auto payload = QByteArray();
	stream >> version >> payload;
	if (!stream.ok()) {
		return std::nullopt;
	}
	return engine::JournalRecord{
		.version = version,
		.payload = ToByteVector(payload),
	};
}

struct StoreInput {
	SecureBytes secret;
	quint32 epoch = 0;
	bool requireUserPresence = false;
};

struct StoreOutcome {
	std::optional<engine::ProtectedSecretHostErrorKind> error;
	quint32 epoch = 0;
	bool written = false;
};

struct ReadOutcome {
	SecureBytes secret;
	std::optional<engine::ProtectedSecretHostErrorKind> error;
	quint32 epoch = 0;
};

[[nodiscard]] SecretReadFailure SecretReadFailureFrom(
		engine::ProtectedSecretHostErrorKind kind) {
	using Error = engine::ProtectedSecretHostErrorKind;
	switch (kind) {
	case Error::kNotFound: return SecretReadFailure::Missing;
	case Error::kAuthenticationFailed: return SecretReadFailure::Locked;
	case Error::kCancelled: return SecretReadFailure::Locked;
	default: return SecretReadFailure::Unreadable;
	}
}

[[nodiscard]] ReadOutcome ReadUnderKeyring(
		Storage::Account &local,
		VaultRuntime &vault,
		uint64 accountId,
		const QString &storageKey,
		quint32 epoch) {
	using Error = engine::ProtectedSecretHostErrorKind;
	const auto deviceKey = vault.keyForRead(accountId, epoch);
	if (!deviceKey) {
		return { .error = vault.unusable()
			? Error::kUnavailable
			: Error::kAuthenticationFailed };
	}
	const auto value = local.readWalletEngineValue(storageKey);
	if (value.state == Storage::WalletEngineValue::State::Absent) {
		return { .error = Error::kNotFound };
	} else if (value.state != Storage::WalletEngineValue::State::Read) {
		return { .error = Error::kUnavailable };
	}
	const auto keyId = ReadVaultRecordKeyId(value.bytes);
	if (!keyId) {
		return { .error = Error::kNotFound };
	}
	const auto reading = vault.reading();
	if (reading.state != KeyringReading::State::Read) {
		return { .error = Error::kUnavailable };
	}
	const auto &entries = reading.keyring.entries;
	const auto i = ranges::find_if(entries, [&](const auto &entry) {
		return entry.accountId == accountId && entry.keyId == *keyId;
	});
	const auto key = (i != end(entries))
		? OpenDeviceKeyringEntry(*deviceKey, *i)
		: std::nullopt;
	if (!key) {
		return { .error = Error::kUnavailable };
	}
	auto record = OpenVaultRecord(*key, storageKey, *keyId, value.bytes);
	if (!record) {
		return { .error = Error::kUnavailable };
	} else if (!vault.current(accountId, epoch)) {
		return { .error = Error::kAuthenticationFailed };
	}
	return { .secret = std::move(record->bytes), .epoch = epoch };
}

// All authority and liveness decisions are made in the main-thread storage
// closure. In particular, a queued store never carries D past a protection
// change, and a pending install cannot be consumed by another account. The
// target's old keyId remains live through the ring write; only a successful
// record replacement removes it from membership. A failed second write leaves
// the old record usable and the fresh entry harmlessly orphaned.
[[nodiscard]] StoreOutcome StoreUnderKeyring(
		Storage::Account &local,
		Storage::Domain &domain,
		VaultRuntime &vault,
		uint64 accountId,
		const QString &storageKey,
		const StoreInput &input) {
	using Error = engine::ProtectedSecretHostErrorKind;
	auto authority = vault.authorityForStore(accountId, input.epoch);
	if (!authority.key && !authority.policy) {
		return { .error = vault.unusable()
			? Error::kUnavailable
			: Error::kAuthenticationFailed };
	}
	const auto reading = vault.reading();
	auto keyring = (reading.state == KeyringReading::State::Read)
		? vault.liveKeyring(reading.keyring)
		: DeviceKeyring();
	auto committed = false;
	const auto finishPolicy = gsl::finally([&] {
		if (!authority.policy) {
			return;
		}
		if (committed) {
			if (reading.state == KeyringReading::State::Read) {
				if (const auto provider = ProtectionProviderFor(
						reading.keyring.wrap.kind)) {
					provider->remove(
						&local,
						reading.keyring.wrap,
						[](ProtectionError) {});
				}
			}
			vault.notifyProtectionChanged();
		} else if (const auto provider = ProtectionProviderFor(
				authority.policy->wrap.kind)) {
			provider->remove(
				&local,
				authority.policy->wrap,
				[](ProtectionError) {});
		}
	});
	if (authority.policy) {
		if ((authority.policy->wrap.kind == VaultKind::Passcode
				&& !domain.hasPasscode())
			|| (!keyring.entries.empty()
				&& keyring.wrap.kind != VaultKind::Open)) {
			return { .error = Error::kAuthenticationFailed };
		}
		if (!keyring.entries.empty() && !authority.key) {
			if (vault.unlockOpen()) {
				authority.key = vault.keyForRead(accountId, authority.epoch);
			}
			if (!authority.key) {
				return { .error = Error::kUnavailable };
			}
		}
		const auto previousKey = authority.key
			? std::move(*authority.key)
			: SecureBytes();
		auto rotation = RotateDeviceKeyring(
			keyring,
			previousKey,
			*authority.policy);
		if (!rotation) {
			return { .error = Error::kUnavailable };
		}
		keyring = std::move(rotation->keyring);
		authority.key = std::move(rotation->key);
	} else if (reading.state != KeyringReading::State::Read) {
		return { .error = Error::kUnavailable };
	}
	auto keyId = QByteArray(kVaultKeyIdSize, Qt::Uninitialized);
	bytes::set_random(bytes::make_detached_span(keyId));
	auto key = SecureBytes(kVaultKeySize);
	bytes::set_random(key.span());
	auto entry = SealDeviceKeyringEntry(
		*authority.key,
		keyId,
		accountId,
		key);
	const auto sealed = SealVaultRecord(
		key,
		storageKey,
		keyId,
		input.requireUserPresence,
		input.secret.span());
	if (!entry || sealed.isEmpty()) {
		return { .error = Error::kUnavailable };
	}
	keyring.entries.push_back(std::move(*entry));
	if (vault.clearEpoch() != authority.epoch) {
		return { .error = Error::kAuthenticationFailed };
	} else if (!WriteDeviceKeyring(domain, keyring)) {
		return { .error = Error::kUnavailable };
	}
	committed = true;
	auto epoch = authority.epoch;
	if (authority.policy) {
		const auto adopted = vault.adoptCommitted(
			std::move(*authority.key),
			epoch,
			authority.owner);
		if (!adopted) {
			return { .error = Error::kAuthenticationFailed };
		}
		epoch = *adopted;
	}
	if (!local.writeWalletEngineValue(storageKey, sealed)) {
		return { .error = Error::kUnavailable, .epoch = epoch };
	}
	vault.recordStored(accountId, storageKey, keyId);
	return { .epoch = epoch, .written = true };
}

// The MTProto toncenter proxy serializes empty JSON maps as arrays: it
// answers "metadata":[] where real toncenter emits "metadata":{}, and a
// present [] in a field the engine's toncenter NFT schema types as a map
// fails the engine's strict deserialization (its serde default covers
// only an absent key). By the owner's decision of 2026-08-25 this
// normalization explicitly overrides the bridge-adds-no-semantics rule
// for this case only: an empty [] right after one of the schema's four
// map-typed keys - metadata, content, collection_content, extra - is
// rewritten to {}, every other byte untouched. Delete this seam when the
// server proxy emits {} faithfully or the engine gains a tolerant
// deserializer, whichever lands first.
[[nodiscard]] QByteArray NormalizeNftEmptyMaps(const QByteArray &body) {
	const auto keys = std::array{
		QLatin1String("metadata"),
		QLatin1String("content"),
		QLatin1String("collection_content"),
		QLatin1String("extra"),
	};
	const auto data = body.constData();
	const auto size = qsizetype(body.size());
	const auto isSpace = [](char ch) {
		return (ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n');
	};
	const auto skipSpace = [&](qsizetype position) {
		while (position < size && isSpace(data[position])) {
			++position;
		}
		return position;
	};
	auto result = body;
	auto rewrite = (char*)nullptr;
	auto position = qsizetype();
	while (position < size) {
		if (data[position++] != '"') {
			continue;
		}
		const auto from = position;
		while (position < size && data[position] != '"') {
			position += (data[position] == '\\') ? 2 : 1;
		}
		if (position >= size) {
			break;
		}
		const auto token = QLatin1String(data + from, int(position - from));
		++position;
		auto known = false;
		for (const auto &key : keys) {
			if (token == key) {
				known = true;
				break;
			}
		}
		if (!known) {
			continue;
		}
		auto lookahead = skipSpace(position);
		if (lookahead >= size || data[lookahead] != ':') {
			continue;
		}
		lookahead = skipSpace(lookahead + 1);
		if (lookahead >= size || data[lookahead] != '[') {
			continue;
		}
		const auto open = lookahead;
		lookahead = skipSpace(lookahead + 1);
		if (lookahead >= size || data[lookahead] != ']') {
			continue;
		}
		if (!rewrite) {
			rewrite = result.data();
		}
		rewrite[open] = '{';
		rewrite[lookahead] = '}';
	}
	return result;
}

} // namespace

EngineSecretStores::Recording::Recording(
	not_null<EngineSecretStores*> stores)
: _previous(t_recordingStores) {
	t_recordingStores = stores;
}

EngineSecretStores::Recording::~Recording() {
	t_recordingStores = _previous;
}

EngineSecretStores::Recording EngineSecretStores::record() {
	return Recording(this);
}

void EngineSecretStores::Remember(const QString &storageKey) {
	if (const auto stores = t_recordingStores) {
		stores->_keys.push_back(storageKey);
	}
}

std::vector<QString> EngineSecretStores::take() {
	return base::take(_keys);
}

SecretReadWatch::SecretReadWatch() : _previous(t_secretReadWatch) {
	t_secretReadWatch = this;
}

SecretReadWatch::~SecretReadWatch() {
	t_secretReadWatch = _previous;
}

SecretReadFailure SecretReadWatch::failure() const {
	return _failure;
}

void SecretReadWatch::MarkFailed(SecretReadFailure failure) {
	// The refusal that stopped the call is the one every open watch keeps:
	// a later read under the same job cannot make an earlier verdict less
	// true, and only the first one actually ended anything.
	for (auto watch = t_secretReadWatch; watch; watch = watch->_previous) {
		if (watch->_failure == SecretReadFailure::None) {
			watch->_failure = failure;
		}
	}
}

SecretReadFailure ProtectedSecretFailure(const EngineError &error) {
	// What the watch recorded at the read outranks the typed error, which
	// the engine may already have rewritten into its own taxonomy.
	if (error.secret != SecretReadFailure::None) {
		return error.secret;
	} else if (!error.underlying) {
		return SecretReadFailure::None;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_lifecycle_error::ProtectedSecretHost &e) {
		return SecretReadFailureFrom(e.kind);
	} catch (const engine::protected_secret_host_error::Failed &e) {
		return SecretReadFailureFrom(e.kind);
	} catch (...) {
	}
	return SecretReadFailure::None;
}

TransferSubmission::Recording::Recording(
	std::shared_ptr<TransferSubmission> submission,
	std::optional<QByteArray> gasless)
: _previous(t_transferSubmission)
, _previousGasless(t_transferGasless) {
	t_transferSubmission = std::move(submission);
	t_transferGasless = std::move(gasless);
}

TransferSubmission::Recording::~Recording() {
	t_transferSubmission = std::move(_previous);
	t_transferGasless = std::move(_previousGasless);
}

TransferSubmission::TransferSubmission(Submit submit)
: _submit(std::move(submit)) {
}

TransferSubmission::Recording TransferSubmission::record(
		std::optional<QByteArray> gasless) {
	return Recording(shared_from_this(), std::move(gasless));
}

std::shared_ptr<TransferSubmission> TransferSubmission::Current() {
	return t_transferSubmission;
}

std::optional<QByteArray> TransferSubmission::CurrentGasless() {
	return t_transferGasless;
}

void TransferSubmission::submit(
		TransferSubmissionData data,
		Fn<void(TransferSubmissionAnswer)> done) {
	_submit(std::move(data), std::move(done));
}

// Implements the engine's status-less provider callback over the main-thread
// MTProto proxy transport. execute_statusless() blocks the calling engine
// worker until the provider body arrives, the applicable request timeout
// expires, the request is cancelled, or the Engine closes. It hands the
// engine the body and nothing else: the proxy carries no status code, no
// response headers and no final URL, so the bridge asserts none of them.
class Engine::StatuslessHost final : public engine::WalletStatuslessHost {
public:
	StatuslessHost(base::weak_ptr<Engine> weak, not_null<Api*> api)
	: _weak(weak)
	, _api(api) {
	}

	[[nodiscard]] std::vector<uint8_t> execute_statusless(
			const engine::HttpRequest &request) override {
		const auto id = request.id.value;
		const auto url = QString::fromStdString(request.url);
		auto base = QString();
		auto basePath = QString();
		{
			auto lock = std::lock_guard(_mutex);
			if (_closed) {
				throw HostFailed(
					engine::StatuslessHostErrorKind::kCancelled,
					u"wallet engine is closing"_q);
			} else if (_cancelledEarly.remove(id)) {
				throw HostFailed(
					engine::StatuslessHostErrorKind::kCancelled,
					u"cancelled before start"_q);
			}
			base = _baseUrl;
			basePath = _basePath;
		}
		// The MTProto proxy chooses its provider upstream server-side, so
		// the bridge can only enforce config-consistency: the caller
		// contracts (see startClient) that toncenter_base_url names the
		// provider the proxy actually serves, in canonical form. Anything
		// outside that base would be silently re-routed to the proxy's
		// upstream — reject it as a policy violation instead of executing
		// a request the engine believes reached the configured origin.
		if (base.isEmpty()
			|| !url.startsWith(base)
			|| !url.mid(base.size()).startsWith(u"/api/"_q)) {
			throw HostFailed(
				engine::StatuslessHostErrorKind::kPolicyViolation,
				u"request outside the configured provider base"_q);
		}
		const auto parsed = QUrl(url);
		const auto encodedPath = parsed.path(QUrl::FullyEncoded);
		if (encodedPath.contains('%')
			|| encodedPath.contains(u".."_q)
			|| encodedPath.contains(u"//"_q)) {
			throw HostFailed(
				engine::StatuslessHostErrorKind::kPolicyViolation,
				u"unexpected request path"_q);
		}
		const auto pending = std::make_shared<Pending>();
		{
			auto lock = std::lock_guard(_mutex);
			if (_closed) {
				throw HostFailed(
					engine::StatuslessHostErrorKind::kCancelled,
					u"wallet engine is closing"_q);
			} else if (_cancelledEarly.remove(id)) {
				throw HostFailed(
					engine::StatuslessHostErrorKind::kCancelled,
					u"cancelled before start"_q);
			}
			const auto [i, inserted] = _pending.emplace(id, pending);
			Assert(inserted);
		}
		const auto gram = Gram::HttpRequest{
			.post = (request.method == engine::HttpMethod::kPost),
			.endpoint = parsed.path().mid(basePath.size()),
			.query = parsed.query(),
			.payload = ToByteArray(request.body),
		};
		const auto normalize = (gram.endpoint == u"/api/v3/nft/items"_q);
		const auto pendingList = (gram.endpoint
			== u"/api/v3/pendingTransactions"_q);
		const auto submission = TransferSubmission::Current();
		const auto routedSend = submission && IsSendBocRequest(gram);
		if (routedSend) {
			// The engine submits only the normal delivery form. Its fee-free
			// alternative was signed beside it for the same seqno and validity
			// window and travels with the recording, so both reach the server
			// in the one wallet.sendTransfer that chooses between them.
			const auto data = TransferSubmissionData{
				.normal = RoutedSendBoc(gram),
				.gasless = TransferSubmission::CurrentGasless(),
			};
			if (data.normal.isEmpty()) {
				Complete(
					pending,
					RejectedSendBody(id, u"WALLET_TRANSFER_DATA_INVALID"_q));
			} else {
				// The routed submission never sets pending->requestId, so
				// the timeout below leaves its MTProto request in flight on
				// purpose: cancelling cannot un-send a broadcast, the late
				// Updates answer carries updateSentWalletTransaction, which
				// the session binds to the still-unresolved operation. The
				// routed wait allows the server's 30-second hold plus a
				// 10-second transport margin. After that, the engine is
				// SubmissionUnknown and blocks a replacement, so an even
				// later answer can still settle the operation's receipt.
				crl::on_main(_weak, [=] {
					{
						auto lock = std::lock_guard(pending->mutex);
						if (pending->done) {
							return;
						}
					}
					submission->submit(data, [=](
							TransferSubmissionAnswer answer) {
						switch (answer.outcome) {
						case TransferSubmissionOutcome::Accepted:
							Complete(pending, AcceptedSendBody());
							break;
						case TransferSubmissionOutcome::Rejected:
							Complete(
								pending,
								RejectedSendBody(id, answer.diagnostic));
							break;
						case TransferSubmissionOutcome::Uncertain:
							Fail(
								pending,
								engine::StatuslessHostErrorKind::kOther,
								answer.diagnostic);
							break;
						}
					});
				});
			}
		} else {
			crl::on_main(_weak, [=, api = _api] {
				{
					auto lock = std::lock_guard(pending->mutex);
					if (pending->done) {
						return;
					}
				}
				// The MTProto toncenter proxy surfaces a success body or a
				// parsed error string only: provider status codes, response
				// headers and request headers never cross it. The status-less
				// transport is the exact shape for that, so a delivered body
				// is presented as a body and nothing more. It also means the
				// bridge cannot classify provider throttling — a 429 and its
				// Retry-After never reach it — so every failure the proxy does
				// not mark as a timeout is kOther with a bounded diagnostic.
				const auto requestId = api->request(gram, [=](
						const QByteArray &bytes) {
					Complete(pending, ToByteVector(normalize
						? NormalizeNftEmptyMaps(bytes)
						: bytes));
				}, [=](const Gram::ApiError &error) {
					// WHY: toncenter says "none pending" with a 404 that the
					// proxy drops, while the engine reads only a 404 or a list
					// as that answer, so this string becomes the empty list.
					if (pendingList
						&& (error.message == u"emulated traces not found"_q)) {
						Complete(pending, EmptyPendingListBody());
						return;
					}
					const auto kind = Api::IsTimeoutError(error)
						? engine::StatuslessHostErrorKind::kTimeout
						: engine::StatuslessHostErrorKind::kOther;
					Fail(pending, kind, u"MTP %1: %2"_q
						.arg(error.code)
						.arg(error.message));
				});
				auto cancel = false;
				{
					auto lock = std::lock_guard(pending->mutex);
					if (pending->done) {
						cancel = true;
					} else {
						pending->requestId = requestId;
					}
				}
				if (cancel) {
					api->cancelRequest(requestId);
				}
			});
		}
		auto lock = std::unique_lock(pending->mutex);
		pending->ready.wait_for(
			lock,
			routedSend
				? kRoutedSendTimeout
				: std::chrono::milliseconds(request.timeout_ms),
			[&] { return pending->done; });
		if (!pending->done) {
			pending->done = true;
			pending->error = { {
				engine::StatuslessHostErrorKind::kTimeout,
				u"request timeout expired"_q,
			} };
		}
		lock.unlock();
		{
			auto outerLock = std::lock_guard(_mutex);
			const auto i = _pending.find(id);
			if (i != _pending.end() && i->second == pending) {
				_pending.erase(i);
			}
		}
		if (pending->error) {
			cancelTransport(pending);
			throw HostFailed(pending->error->first, pending->error->second);
		}
		return std::move(*pending->body);
	}

	void cancel_statusless(const engine::HttpRequestId &id) override {
		auto lock = std::lock_guard(_mutex);
		if (_closed) {
			return;
		}
		const auto i = _pending.find(id.value);
		if (i != _pending.end()) {
			Fail(
				i->second,
				engine::StatuslessHostErrorKind::kCancelled,
				u"cancelled"_q);
		} else {
			if (_cancelledEarly.size() >= kMaxTrackedEarlyCancels) {
				_cancelledEarly.erase(_cancelledEarly.begin());
			}
			_cancelledEarly.emplace(id.value);
		}
	}

	// Main thread. Arms the transport policy for one new client and drops
	// per-client cancellation state: request ids restart in a fresh client,
	// so stale entries must not leak across.
	void startClient(const QString &baseUrl) {
		auto base = baseUrl;
		while (base.endsWith('/')) {
			base.chop(1);
		}
		auto pending = base::flat_map<uint64, std::shared_ptr<Pending>>();
		{
			auto lock = std::lock_guard(_mutex);
			if (_closed) {
				return;
			}
			_baseUrl = base;
			_basePath = QUrl(base).path();
			pending = base::take(_pending);
			_cancelledEarly.clear();
		}
		for (const auto &[id, entry] : pending) {
			Fail(
				entry,
				engine::StatuslessHostErrorKind::kCancelled,
				u"client replaced"_q);
		}
	}

	// Main thread. Completes every waiting request so the worker can never
	// block on a main thread that is tearing the Engine down.
	void close() {
		auto pending = base::flat_map<uint64, std::shared_ptr<Pending>>();
		{
			auto lock = std::lock_guard(_mutex);
			_closed = true;
			pending = base::take(_pending);
			_cancelledEarly.clear();
		}
		for (const auto &[id, entry] : pending) {
			Fail(
				entry,
				engine::StatuslessHostErrorKind::kCancelled,
				u"wallet engine is closing"_q);
		}
	}

private:
	struct Pending {
		std::mutex mutex;
		std::condition_variable ready;
		std::optional<std::vector<uint8_t>> body;
		std::optional<
			std::pair<engine::StatuslessHostErrorKind, QString>> error;
		mtpRequestId requestId = 0;
		bool done = false;
	};

	static void Complete(
			const std::shared_ptr<Pending> &pending,
			std::vector<uint8_t> &&body) {
		auto lock = std::lock_guard(pending->mutex);
		if (pending->done) {
			return;
		}
		pending->done = true;
		pending->body = std::move(body);
		pending->ready.notify_all();
	}

	static void Fail(
			const std::shared_ptr<Pending> &pending,
			engine::StatuslessHostErrorKind kind,
			const QString &diagnostic) {
		auto lock = std::lock_guard(pending->mutex);
		if (pending->done) {
			return;
		}
		pending->done = true;
		pending->error = { { kind, diagnostic } };
		pending->ready.notify_all();
	}

	void cancelTransport(const std::shared_ptr<Pending> &pending) {
		crl::on_main(_weak, [pending, api = _api] {
			auto requestId = mtpRequestId();
			{
				auto lock = std::lock_guard(pending->mutex);
				requestId = base::take(pending->requestId);
			}
			if (requestId) {
				api->cancelRequest(requestId);
			}
		});
	}

	const base::weak_ptr<Engine> _weak;
	Api * const _api; // Main thread only.

	std::mutex _mutex;
	bool _closed = false;
	QString _baseUrl;
	QString _basePath;
	base::flat_map<uint64, std::shared_ptr<Pending>> _pending;
	base::flat_set<uint64> _cancelledEarly;

};

// Implements the engine's protected-secret and send-journal storage over
// Storage::Account's encrypted wallet-engine records. Every call marshals
// to the main thread and blocks the engine worker until it is served, so
// journal compare-exchange is naturally serialized.
class Engine::PlatformHost final : public engine::WalletPlatformHost {
public:
	PlatformHost(
		base::weak_ptr<Engine> weak,
		not_null<Main::Session*> session,
		std::shared_ptr<VaultRuntime> vault)
	: _weak(weak)
	, _session(session)
	, _domain(&session->domainLocal())
	, _accountId(session->uniqueId())
	, _vault(std::move(vault)) {
	}

	[[nodiscard]] std::vector<uint8_t> read_protected_secret(
			const engine::ProtectedSecretRead &request) override {
		const auto key = SecretStorageKey(request.secret_ref);
		const auto epoch = privateEpoch();
		const auto vault = _vault;
		const auto accountId = _accountId;
		auto outcome = storage([=](Storage::Account &local) {
			return ReadUnderKeyring(local, *vault, accountId, key, epoch);
		});
		if (!outcome) {
			SecretReadWatch::MarkFailed(SecretReadFailure::Unreadable);
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet engine storage is unavailable"_q);
		} else if (outcome->error) {
			SecretReadWatch::MarkFailed(SecretReadFailureFrom(*outcome->error));
			throw HostFailed(*outcome->error, u"wallet keyring read refused"_q);
		} else if (!vault->current(accountId, outcome->epoch)) {
			SecretReadWatch::MarkFailed(SecretReadFailure::Locked);
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kAuthenticationFailed,
				u"wallet authorization expired"_q);
		}
		return ToByteVector(outcome->secret.span());
	}

	void store_protected_secret(
			const engine::ProtectedSecretStore &request) override {
		const auto key = SecretStorageKey(request.secret_ref);
		const auto input = std::make_shared<StoreInput>(StoreInput{
			.secret = SecureBytes(bytes::make_span(request.bytes)),
			.epoch = privateEpoch(),
			.requireUserPresence = request.require_user_presence,
		});
		const auto vault = _vault;
		const auto accountId = _accountId;
		const auto domain = _domain;
		const auto outcome = storage([=](Storage::Account &local) {
			return StoreUnderKeyring(
				local,
				*domain,
				*vault,
				accountId,
				key,
				*input);
		});
		if (!outcome) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet engine storage is unavailable"_q);
		}
		if (outcome->written) {
			EngineSecretStores::Remember(key);
		}
		if (outcome->error) {
			throw HostFailed(*outcome->error, u"wallet keyring store refused"_q);
		} else if (!vault->current(accountId, outcome->epoch)) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kAuthenticationFailed,
				u"wallet authorization expired"_q);
		}
		if (_privateAccess) {
			_privateAccess->epoch = outcome->epoch;
		}
	}

	void delete_protected_secret(
			const engine::ProtectedSecretRef &reference) override {
		const auto key = SecretStorageKey(reference);
		const auto vault = _vault;
		const auto accountId = _accountId;
		const auto removed = storage([=](Storage::Account &local) {
			const auto result = local.removeWalletEngineValue(key);
			vault->recordRemoved(accountId, key);
			return result;
		});
		if (!removed) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet engine storage is unavailable"_q);
		} else if (!*removed) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kNotFound,
				u"secret not found"_q);
		}
	}

	[[nodiscard]] std::optional<engine::JournalRecord> load_journal(
			const engine::JournalKey &key) override {
		const auto storageKey = JournalStorageKey(key);
		const auto value = storage([=](Storage::Account &local) {
			return local.readWalletEngineValue(storageKey);
		});
		using State = Storage::WalletEngineValue::State;
		if (!value) {
			throw HostFailed(
				engine::JournalHostErrorKind::kUnavailable,
				u"wallet engine storage is unavailable"_q);
		} else if (value->state == State::Absent) {
			return std::nullopt;
		} else if (value->state == State::Broken) {
			throw HostFailed(
				engine::JournalHostErrorKind::kCorruptData,
				u"stored journal record is unreadable"_q);
		}
		const auto record = DeserializeJournalRecord(value->bytes);
		if (!record) {
			throw HostFailed(
				engine::JournalHostErrorKind::kCorruptData,
				u"stored journal record is unreadable"_q);
		}
		return record;
	}

	[[nodiscard]] auto compare_exchange_journal(
			const engine::JournalCompareExchange &mutation)
	-> engine::JournalCompareExchangeResult override {
		const auto storageKey = JournalStorageKey(mutation.key);
		const auto replacement = SerializeJournalRecord(mutation.replacement);
		const auto expected = mutation.expected_version;
		struct Outcome {
			bool applied = false;
			std::optional<QByteArray> current;
			bool corrupt = false;
			bool writeFailed = false;
		};
		const auto outcome = storage([=](Storage::Account &local) {
			using State = Storage::WalletEngineValue::State;
			const auto current = local.readWalletEngineValue(storageKey);
			if (current.state == State::Broken) {
				return Outcome{ .corrupt = true };
			} else if (current.state == State::Absent) {
				if (expected) {
					return Outcome{ .applied = false };
				}
			} else {
				const auto record = DeserializeJournalRecord(current.bytes);
				if (!record) {
					return Outcome{ .corrupt = true };
				} else if (!expected || *expected != record->version) {
					return Outcome{
						.applied = false,
						.current = current.bytes,
					};
				}
			}
			if (!local.writeWalletEngineValue(storageKey, replacement)) {
				return Outcome{ .writeFailed = true };
			}
			return Outcome{ .applied = true };
		});
		if (!outcome) {
			throw HostFailed(
				engine::JournalHostErrorKind::kUnavailable,
				u"wallet engine storage is unavailable"_q);
		} else if (outcome->corrupt) {
			throw HostFailed(
				engine::JournalHostErrorKind::kCorruptData,
				u"stored journal record is unreadable"_q);
		} else if (outcome->writeFailed) {
			throw HostFailed(
				engine::JournalHostErrorKind::kUnavailable,
				u"wallet engine storage write failed"_q);
		}
		auto result = engine::JournalCompareExchangeResult{
			.applied = outcome->applied,
			.current = std::nullopt,
		};
		if (outcome->applied) {
			result.current = mutation.replacement;
		} else if (outcome->current) {
			result.current = DeserializeJournalRecord(*outcome->current);
		}
		return result;
	}

	// Main thread. Wakes every waiting storage call with "closing".
	void close() {
		auto waiting = std::vector<std::shared_ptr<Waiting>>();
		{
			auto lock = std::lock_guard(_mutex);
			_closed = true;
			waiting = base::take(_waiting);
		}
		for (const auto &entry : waiting) {
			auto lock = std::lock_guard(entry->mutex);
			entry->done = true;
			entry->ready.notify_all();
		}
	}

private:
	[[nodiscard]] quint32 privateEpoch() const {
		if (_privateAccess) {
			_privateAccess->used = true;
			return _privateAccess->epoch;
		}
		return _vault->clearEpoch();
	}

	struct Waiting {
		std::mutex mutex;
		std::condition_variable ready;
		bool done = false;
	};

	// Runs `task` with the account storage on the main thread and blocks
	// the engine worker until it finishes. Returns nullopt after close()
	// or when the task failed with an exception.
	template <typename Task>
	[[nodiscard]] auto storage(Task task)
	-> std::optional<decltype(task(std::declval<Storage::Account&>()))> {
		using Result = decltype(task(std::declval<Storage::Account&>()));
		struct State : Waiting {
			std::optional<Result> result;
		};
		const auto state = std::make_shared<State>();
		{
			auto lock = std::lock_guard(_mutex);
			if (_closed) {
				return std::nullopt;
			}
			_waiting.push_back(state);
		}
		crl::on_main(_weak, [state, task, session = _session] {
			auto lock = std::lock_guard(state->mutex);
			if (state->done) {
				return;
			}
			try {
				if (session->account().maybeSession() == session.get()) {
					state->result = task(session->local());
				}
			} catch (...) {
			}
			state->done = true;
			state->ready.notify_all();
		});
		{
			auto lock = std::unique_lock(state->mutex);
			state->ready.wait(lock, [&] { return state->done; });
		}
		{
			auto lock = std::lock_guard(_mutex);
			_waiting.erase(
				ranges::remove(_waiting, state),
				_waiting.end());
		}
		return state->result
			? std::optional<Result>(std::move(*state->result))
			: std::nullopt;
	}

	const base::weak_ptr<Engine> _weak;
	const not_null<Main::Session*> _session; // Main thread only.
	const not_null<Storage::Domain*> _domain; // Main thread only.
	const uint64 _accountId;
	const std::shared_ptr<VaultRuntime> _vault;

	std::mutex _mutex;
	bool _closed = false;
	std::vector<std::shared_ptr<Waiting>> _waiting;

};

struct Engine::Worker {
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<FnMut<void()>> queue;
	bool stopping = false;
	std::thread thread;
};

Engine::Engine(not_null<Main::Session*> session, not_null<Api*> api)
: _session(session)
, _accountId(session->uniqueId())
, _statuslessHost(std::make_shared<StatuslessHost>(base::make_weak(this), api))
, _vault(session->domain().walletKeyring().shared_from_this())
, _platformHost(std::make_shared<PlatformHost>(
	base::make_weak(this),
	session,
	_vault)) {
	_vault->registerAccount(*session);
	session->account().sessionChanges(
	) | rpl::filter([](Main::Session *current) {
		return current == nullptr;
	}) | rpl::on_next([=] {
		_vault->unregisterAccount(_accountId);
	}, _lifetime);
}

Engine::~Engine() {
	// Complete every marshalled call so the worker can never wait on the
	// main thread again, then run a best-effort client shutdown and join.
	// The send journal is durable, so an interrupted shutdown is recovered
	// by resolve_pending() on the next launch by the engine's own design.
	_statuslessHost->close();
	_platformHost->close();
	_lifetime.destroy();
	_vault->unregisterAccount(_accountId);
	if (_localWorker) {
		{
			auto lock = std::lock_guard(_localWorker->mutex);
			_localWorker->queue.clear();
			_localWorker->stopping = true;
		}
		_localWorker->wake.notify_all();
		if (_localWorker->thread.joinable()) {
			_localWorker->thread.join();
		}
	}
	const auto client = base::take(_client);
	if (!_worker) {
		if (client) {
			try {
				client->shutdown();
			} catch (...) {
			}
		}
		return;
	}
	{
		auto lock = std::lock_guard(_worker->mutex);
		_worker->queue.clear();
		if (client) {
			_worker->queue.push_back([client] {
				try {
					client->shutdown();
				} catch (...) {
				}
			});
		}
		_worker->stopping = true;
	}
	_worker->wake.notify_all();
	if (_worker->thread.joinable()) {
		_worker->thread.join();
	}
}

auto Engine::lifecycle()
-> std::shared_ptr<wallet_engine::WalletLifecycle> {
	if (!_lifecycle) {
		_lifecycle = engine::WalletLifecycle::init(_platformHost);
	}
	return _lifecycle;
}

void Engine::startClient(const engine::WalletClientConfig &config) {
	Expects(!_client);

	_statuslessHost->startClient(
		QString::fromStdString(config.providers.toncenter_base_url));
	_client = engine::WalletClient::new_statusless(
		config,
		_statuslessHost,
		_platformHost);
}

auto Engine::client() const
-> std::shared_ptr<wallet_engine::WalletClient> {
	return _client;
}

VaultRuntime &Engine::vault() const {
	return *_vault;
}

void Engine::stopClient(Fn<void()> done) {
	if (!_client) {
		done();
		return;
	}
	const auto client = _client;
	const auto finish = [=, this] {
		if (_client == client) {
			_client = nullptr;
		}
		done();
	};
	run([client] {
		client->shutdown();
	}, finish, [finish](EngineError) { finish(); });
}

void Engine::dropStoredSecrets(EngineSecretStores &stores) {
	auto dropped = 0;
	for (const auto &key : stores.take()) {
		if (_session->local().removeWalletEngineValue(key)) {
			++dropped;
		}
		_vault->recordRemoved(_accountId, key);
	}
	if (dropped) {
		LOG(("Wallet Info: dropped %1 secret record(s) a failed engine "
			"call stored.").arg(dropped));
	}
}

thread_local Engine::PrivateAccess *Engine::_privateAccess = nullptr;

std::shared_ptr<Engine::PrivateAccess> Engine::privateAccess() const {
	return std::make_shared<PrivateAccess>(PrivateAccess{
		.epoch = _vault->clearEpoch(),
	});
}

bool Engine::PrivateResultCurrent(
		const VaultRuntime &vault,
		uint64 accountId,
		const PrivateAccess &access) {
	return !access.used || vault.current(accountId, access.epoch);
}

EngineError Engine::PrivateAccessError() {
	return {
		.message = u"wallet authorization expired"_q,
		.underlying = std::make_exception_ptr(HostFailed(
			engine::ProtectedSecretHostErrorKind::kAuthenticationFailed,
			u"wallet authorization expired"_q)),
	};
}

void Engine::Execute(
		base::weak_ptr<Engine> weak,
		const std::shared_ptr<PrivateAccess> &access,
		FnMut<void()> job,
		Fn<void(EngineError)> fail) {
	const auto previous = std::exchange(_privateAccess, access.get());
	const auto restore = gsl::finally([=] { _privateAccess = previous; });
	// WHY: every protected-secret read happens under this job, on this
	// thread, and the engine rewrites a refused read into whatever its own
	// taxonomy calls the operation that failed - a send becomes SendFailed.
	// The watch records the host's answer where it is still the truth, and
	// the error carries it to main, so a caller no longer has to recognize
	// a rewritten error to learn that the key could not be read.
	auto watch = SecretReadWatch();
	try {
		job();
	} catch (const std::exception &e) {
		// Engine errors lifted from Rust always carry a non-empty
		// what(): the Rust Display text, or the qualified variant
		// name for a family that doesn't export Display. The typeid
		// fallback guards a foreign exception with an empty what().
		// Either way the message is log-only and consumers dispatch
		// by rethrowing `underlying`.
		const auto what = QString::fromUtf8(e.what());
		const auto error = EngineError{
			.message = what.isEmpty()
				? QString::fromUtf8(typeid(e).name())
				: what,
			.underlying = std::current_exception(),
			.secret = watch.failure(),
		};
		crl::on_main(weak, [fail = std::move(fail), error] {
			fail(error);
		});
	} catch (...) {
		const auto error = EngineError{
			.message = u"unexpected wallet engine error"_q,
			.underlying = std::current_exception(),
			.secret = watch.failure(),
		};
		crl::on_main(weak, [fail = std::move(fail), error] {
			fail(error);
		});
	}
}

void Engine::Enqueue(
		std::unique_ptr<Worker> &worker,
		FnMut<void()> task) {
	if (!worker) {
		worker = std::make_unique<Worker>();
		worker->thread = std::thread([pointer = worker.get()] {
			WorkerLoop(pointer);
		});
	}
	{
		auto lock = std::lock_guard(worker->mutex);
		if (worker->stopping) {
			return;
		}
		worker->queue.push_back(std::move(task));
	}
	worker->wake.notify_all();
}

void Engine::WorkerLoop(not_null<Worker*> worker) {
	while (true) {
		auto task = FnMut<void()>();
		{
			auto lock = std::unique_lock(worker->mutex);
			worker->wake.wait(lock, [&] {
				return worker->stopping || !worker->queue.empty();
			});
			if (worker->queue.empty()) {
				break;
			}
			task = std::move(worker->queue.front());
			worker->queue.pop_front();
		}
		try {
			task();
		} catch (...) {
		}
	}
}

} // namespace Wallet
