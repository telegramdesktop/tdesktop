/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_engine.h"

#include "gram/api/gram_api_request.h"
#include "main/main_session.h"
#include "storage/serialize_common.h"
#include "storage/storage_account.h"
#include "wallet/wallet_api.h"
#include "wallet/wallet_vault.h"

#include "wallet_engine.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>

#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace Wallet {
namespace {

namespace engine = wallet_engine;

constexpr auto kMaxTrackedEarlyCancels = 64;

// The open recordings of the engine call running on this thread.
thread_local EngineSecretStores *t_recordingStores = nullptr;
thread_local std::shared_ptr<TransferSubmission> t_transferSubmission;

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
	VaultRuntime::StoreAuthority authority;
	SecureBytes secret;
	bool requireUserPresence = false;
};

struct StoreOutcome {
	bool written = false;
	bool unavailable = false;
	bool refused = false;
	SecureBytes created;
};

[[nodiscard]] bool WriteSealedRecord(
		Storage::Account &local,
		const SecureBytes &vaultKey,
		quint32 generation,
		const QString &storageKey,
		const StoreInput &input) {
	const auto sealed = SealVaultRecord(
		vaultKey,
		storageKey,
		generation,
		input.requireUserPresence,
		input.secret.span());
	if (sealed.isEmpty()) {
		LOG(("Wallet Error: could not seal the secret record."));
		return false;
	}
	return local.writeWalletEngineValue(storageKey, sealed);
}

// The header and the record go together: a record write that fails right
// after the header write drops the header again, so no vault without a
// record and no record without a vault survive a failed creation.
[[nodiscard]] StoreOutcome CreateVaultAndStore(
		Storage::Account &local,
		const QString &storageKey,
		StoreInput &input) {
	auto &policy = *input.authority.policy;
	auto vaultKey = SecureBytes(kVaultKeySize);
	bytes::set_random(vaultKey.span());
	policy.wrap.generation = 1;
	policy.wrap.blob = WrapVaultKey(vaultKey, policy.wrap, policy.wrapKey);
	if (policy.wrap.blob.isEmpty()) {
		LOG(("Wallet Error: could not wrap the new vault key, kind: %1."
			).arg(quint32(policy.wrap.kind)));
		return { .unavailable = true };
	}
	auto header = VaultHeader{ .committed = 1 };
	header.wraps.push_back(policy.wrap);
	if (!WriteVaultHeader(local, header)) {
		return { .unavailable = true };
	} else if (!WriteSealedRecord(
			local,
			vaultKey,
			header.committed,
			storageKey,
			input)) {
		if (!RemoveVaultHeader(local)) {
			LOG(("Wallet Error: could not drop the vault header after the "
				"failed record write."));
		}
		return { .unavailable = true };
	}
	return { .written = true, .created = std::move(vaultKey) };
}

// Runs on the main thread inside the marshal so the decision is made under
// the live header: an existing vault accepts only the unlocked key (a
// creation policy never applies to it), an absent one only the policy, and
// a Broken or Unsupported header is never overwritten nor read as absence.
// The key was copied on the worker, so it is accepted only while the
// runtime's epoch is still the one it was copied under: a wrap transition
// that committed in between cleared the runtime and retired that key, and a
// record sealed under it would open under no wrap the header holds.
[[nodiscard]] StoreOutcome StoreUnderVault(
		Storage::Account &local,
		const VaultRuntime &vault,
		const QString &storageKey,
		StoreInput &input) {
	using State = VaultReading::State;
	const auto reading = ReconcileVaultHeader(local);
	if (reading.state == State::Broken
		|| reading.state == State::Unsupported) {
		return { .unavailable = true };
	} else if (reading.state == State::Read) {
		if (!input.authority.key
			|| vault.clearEpoch() != input.authority.epoch) {
			return { .refused = true };
		}
		const auto written = WriteSealedRecord(
			local,
			*input.authority.key,
			reading.header.committed,
			storageKey,
			input);
		return written
			? StoreOutcome{ .written = true }
			: StoreOutcome{ .unavailable = true };
	} else if (!input.authority.policy) {
		return { .refused = true };
	}
	return CreateVaultAndStore(local, storageKey, input);
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

TransferSubmission::Recording::Recording(
	std::shared_ptr<TransferSubmission> submission)
: _previous(t_transferSubmission) {
	t_transferSubmission = std::move(submission);
}

TransferSubmission::Recording::~Recording() {
	t_transferSubmission = std::move(_previous);
}

TransferSubmission::TransferSubmission(Submit submit)
: _submit(std::move(submit)) {
}

TransferSubmission::Recording TransferSubmission::record() {
	return Recording(shared_from_this());
}

std::shared_ptr<TransferSubmission> TransferSubmission::Current() {
	return t_transferSubmission;
}

void TransferSubmission::submit(
		QByteArray boc,
		Fn<void(TransferSubmissionAnswer)> done) {
	_submit(std::move(boc), std::move(done));
}

// Implements the engine's status-less provider callback over the main-thread
// MTProto proxy transport. execute_statusless() blocks the calling engine
// worker until the provider body arrives, the engine-supplied timeout
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
		const auto submission = TransferSubmission::Current();
		if (submission && IsSendBocRequest(gram)) {
			const auto boc = RoutedSendBoc(gram);
			if (boc.isEmpty()) {
				Complete(
					pending,
					RejectedSendBody(id, u"WALLET_TRANSFER_DATA_INVALID"_q));
			} else {
				// The routed submission never sets pending->requestId, so
				// the timeout below leaves its MTProto request in flight on
				// purpose: cancelling cannot un-send a broadcast, the late
				// wallet.sentTransfer is the only source of the receipt and
				// the session binds it to the still-unresolved operation,
				// and the engine is already SubmissionUnknown by then and
				// blocks a replacement, so the late answer is pure gain.
				crl::on_main(_weak, [=] {
					{
						auto lock = std::lock_guard(pending->mutex);
						if (pending->done) {
							return;
						}
					}
					submission->submit(boc, [=](
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
			std::chrono::milliseconds(request.timeout_ms),
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
	, _vault(std::move(vault)) {
	}

	[[nodiscard]] std::vector<uint8_t> read_protected_secret(
			const engine::ProtectedSecretRead &request) override {
		const auto key = SecretStorageKey(request.secret_ref);
		// Decided here on the worker, before the marshal: storage() turns
		// every throw inside it into kUnavailable, never a typed refusal.
		const auto vaultKey = _vault->keyForRead();
		if (!vaultKey) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kAuthenticationFailed,
				u"wallet vault is locked"_q);
		}
		const auto value = storage([=](Storage::Account &local) {
			return local.readWalletEngineValue(key);
		});
		using State = Storage::WalletEngineValue::State;
		if (!value) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet engine storage is unavailable"_q);
		} else if (value->state == State::Absent) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kNotFound,
				u"secret not found"_q);
		} else if (value->state == State::Broken) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"stored secret is unreadable"_q);
		} else if (!IsVaultRecord(value->bytes)) {
			LOG(("Wallet Warning: a pre-vault secret record reads as absent."));
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kNotFound,
				u"pre-vault secret"_q);
		}
		const auto record = OpenVaultRecord(*vaultKey, key, value->bytes);
		if (!record) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"stored secret is unreadable"_q);
		}
		return ToByteVector(record->bytes.span());
	}

	// Overwrites an existing record even when it is unreadable: an
	// explicit store of a fresh secret is the recovery path for a
	// Broken record, unlike the journal CAS which refuses to touch one.
	void store_protected_secret(
			const engine::ProtectedSecretStore &request) override {
		const auto key = SecretStorageKey(request.secret_ref);
		// storage() copies its task into the main-thread call, so the
		// move-only authority and the secret travel behind one pointer.
		const auto input = std::make_shared<StoreInput>(StoreInput{
			.authority = _vault->authorityForStore(),
			.secret = SecureBytes(bytes::make_span(request.bytes)),
			.requireUserPresence = request.require_user_presence,
		});
		if (!input->authority.key && !input->authority.policy) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kAuthenticationFailed,
				u"wallet vault is locked"_q);
		}
		const auto vault = _vault;
		auto outcome = storage([=](Storage::Account &local) {
			return StoreUnderVault(local, *vault, key, *input);
		});
		if (!outcome) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet engine storage is unavailable"_q);
		} else if (outcome->refused) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kAuthenticationFailed,
				u"wallet vault is locked"_q);
		} else if (outcome->unavailable) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet vault is unavailable"_q);
		}
		EngineSecretStores::Remember(key);
		if (!outcome->created.empty()) {
			_vault->adoptCreated(
				std::move(outcome->created),
				input->authority.epoch);
		}
	}

	void delete_protected_secret(
			const engine::ProtectedSecretRef &reference) override {
		const auto key = SecretStorageKey(reference);
		const auto removed = storage([=](Storage::Account &local) {
			return local.removeWalletEngineValue(key);
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
				state->result = task(session->local());
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
, _statuslessHost(std::make_shared<StatuslessHost>(base::make_weak(this), api))
, _vault(std::make_shared<VaultRuntime>())
, _platformHost(std::make_shared<PlatformHost>(
	base::make_weak(this),
	session,
	_vault)) {
}

Engine::~Engine() {
	// Complete every marshalled call so the worker can never wait on the
	// main thread again, then run a best-effort client shutdown and join.
	// The send journal is durable, so an interrupted shutdown is recovered
	// by resolve_pending() on the next launch by the engine's own design.
	_statuslessHost->close();
	_platformHost->close();
	_vault->clear();
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
	}
	// removeWalletEngineValue answers false only for a key that is not
	// in the map, so this counts records that were really there and
	// never removals that failed: at that API there is no
	// removal-failed answer, exactly as dropCreatedVault reads its own.
	if (dropped) {
		LOG(("Wallet Info: dropped %1 secret record(s) a failed engine "
			"call stored.").arg(dropped));
	}
}

void Engine::Execute(
		base::weak_ptr<Engine> weak,
		FnMut<void()> job,
		Fn<void(EngineError)> fail) {
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
		};
		crl::on_main(weak, [fail = std::move(fail), error] {
			fail(error);
		});
	} catch (...) {
		const auto error = EngineError{
			.message = u"unexpected wallet engine error"_q,
			.underlying = std::current_exception(),
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
