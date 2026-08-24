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

#include "wallet_engine.hpp"

#include <QtCore/QUrl>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace Wallet {
namespace {

namespace engine = wallet_engine;

constexpr auto kMaxTrackedEarlyCancels = 64;
constexpr auto kSecretRequireUserPresenceFlag = quint32(1U << 0);

template <typename Kind>
struct HostErrorFor;

template <>
struct HostErrorFor<engine::HttpHostErrorKind> {
	using Failed = engine::http_host_error::Failed;
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
	return !part.empty() && (part.find('/') == std::string::npos);
}

[[nodiscard]] QString SecretStorageKey(
		const engine::ProtectedSecretRef &reference) {
	if (!GoodStorageKeyPart(reference.value)) {
		throw HostFailed(
			engine::ProtectedSecretHostErrorKind::kPolicyViolation,
			u"invalid secret reference"_q);
	}
	return u"secret/"_q + QString::fromStdString(reference.value);
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
	result << quint64(record.version) << QByteArray(
		reinterpret_cast<const char*>(record.payload.data()),
		record.payload.size());
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
		.payload = std::vector<uint8_t>(
			payload.constData(),
			payload.constData() + payload.size()),
	};
}

[[nodiscard]] QByteArray SerializeSecretRecord(
		const engine::ProtectedSecretStore &request) {
	auto result = Serialize::ByteArrayWriter();
	result
		<< quint32(request.require_user_presence
			? kSecretRequireUserPresenceFlag
			: 0)
		<< QByteArray(
			reinterpret_cast<const char*>(request.bytes.data()),
			request.bytes.size());
	return std::move(result).result();
}

struct SecretRecord {
	bool requireUserPresence = false;
	QByteArray bytes;
};

[[nodiscard]] std::optional<SecretRecord> DeserializeSecretRecord(
		const QByteArray &serialized) {
	auto stream = Serialize::ByteArrayReader(serialized);
	auto flags = quint32();
	auto bytes = QByteArray();
	stream >> flags >> bytes;
	if (!stream.ok()) {
		return std::nullopt;
	}
	return SecretRecord{
		.requireUserPresence = ((flags & kSecretRequireUserPresenceFlag)
			== kSecretRequireUserPresenceFlag),
		.bytes = bytes,
	};
}

} // namespace

// Implements the engine's HTTP callback over the main-thread MTProto proxy
// transport. execute_http() blocks the calling engine worker until the
// answer arrives, the engine-supplied timeout expires, the request is
// cancelled, or the Engine closes.
class Engine::HttpHost final : public engine::WalletHttpHost {
public:
	HttpHost(base::weak_ptr<Engine> weak, not_null<Api*> api)
	: _weak(weak)
	, _api(api) {
	}

	[[nodiscard]] engine::HttpResponse execute_http(
			const engine::HttpRequest &request) override {
		const auto id = request.id.value;
		const auto url = QString::fromStdString(request.url);
		auto base = QString();
		auto basePath = QString();
		{
			auto lock = std::lock_guard(_mutex);
			if (_closed) {
				throw HostFailed(
					engine::HttpHostErrorKind::kCancelled,
					u"wallet engine is closing"_q);
			} else if (_cancelledEarly.remove(id)) {
				throw HostFailed(
					engine::HttpHostErrorKind::kCancelled,
					u"cancelled before start"_q);
			}
			base = _baseUrl;
			basePath = _basePath;
		}
		// The transport reaches exactly the provider this client was
		// configured with, so a URL outside that base would be silently
		// re-routed to it: reject the request instead of lying about the
		// origin in final_url. The echo below is truthful only together
		// with this check.
		if (base.isEmpty()
			|| !url.startsWith(base)
			|| !url.mid(base.size()).startsWith(u"/api/"_q)) {
			throw HostFailed(
				engine::HttpHostErrorKind::kPolicyViolation,
				u"request outside the configured provider base"_q);
		}
		const auto parsed = QUrl(url);
		const auto encodedPath = parsed.path(QUrl::FullyEncoded);
		if (encodedPath.contains('%')
			|| encodedPath.contains(u".."_q)
			|| encodedPath.contains(u"//"_q)) {
			throw HostFailed(
				engine::HttpHostErrorKind::kPolicyViolation,
				u"unexpected request path"_q);
		}
		const auto pending = std::make_shared<Pending>();
		{
			auto lock = std::lock_guard(_mutex);
			if (_closed) {
				throw HostFailed(
					engine::HttpHostErrorKind::kCancelled,
					u"wallet engine is closing"_q);
			}
			const auto [i, inserted] = _pending.emplace(id, pending);
			Assert(inserted);
		}
		const auto gram = Gram::HttpRequest{
			.post = (request.method == engine::HttpMethod::kPost),
			.endpoint = parsed.path().mid(basePath.size()),
			.query = parsed.query(),
			.payload = QByteArray(
				reinterpret_cast<const char*>(request.body.data()),
				request.body.size()),
		};
		const auto requestUrl = request.url;
		crl::on_main(_weak, [=, api = _api] {
			{
				auto lock = std::lock_guard(pending->mutex);
				if (pending->done) {
					return;
				}
			}
			// The MTProto toncenter proxy surfaces a success body or a
			// parsed error string only: provider status codes, response
			// headers and request headers never cross it. So a delivered
			// body is always the 200 success case, retry classification
			// can never see 429 / Retry-After, and every non-timeout
			// failure lands in kOther. Revisit if the proxy schema ever
			// carries status codes or headers.
			const auto requestId = api->request(gram, [=](
					const QByteArray &bytes) {
				Complete(pending, engine::HttpResponse{
					.status = 200,
					.headers = {},
					.body = std::vector<uint8_t>(
						bytes.constData(),
						bytes.constData() + bytes.size()),
					.final_url = requestUrl,
				});
			}, [=](const Gram::ApiError &error) {
				const auto kind = Api::IsTimeoutError(error)
					? engine::HttpHostErrorKind::kTimeout
					: engine::HttpHostErrorKind::kOther;
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
		auto lock = std::unique_lock(pending->mutex);
		pending->ready.wait_for(
			lock,
			std::chrono::milliseconds(request.timeout_ms),
			[&] { return pending->done; });
		if (!pending->done) {
			pending->done = true;
			pending->error = { {
				engine::HttpHostErrorKind::kTimeout,
				u"request timeout expired"_q,
			} };
		}
		lock.unlock();
		{
			auto outerLock = std::lock_guard(_mutex);
			_pending.remove(id);
		}
		if (pending->error) {
			cancelTransport(pending);
			throw HostFailed(pending->error->first, pending->error->second);
		}
		return std::move(*pending->response);
	}

	void cancel_http(const engine::HttpRequestId &id) override {
		auto lock = std::lock_guard(_mutex);
		if (_closed) {
			return;
		}
		const auto i = _pending.find(id.value);
		if (i != _pending.end()) {
			Fail(
				i->second,
				engine::HttpHostErrorKind::kCancelled,
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
				engine::HttpHostErrorKind::kCancelled,
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
				engine::HttpHostErrorKind::kCancelled,
				u"wallet engine is closing"_q);
		}
	}

private:
	struct Pending {
		std::mutex mutex;
		std::condition_variable ready;
		std::optional<engine::HttpResponse> response;
		std::optional<std::pair<engine::HttpHostErrorKind, QString>> error;
		mtpRequestId requestId = 0;
		bool done = false;
	};

	static void Complete(
			const std::shared_ptr<Pending> &pending,
			engine::HttpResponse &&response) {
		auto lock = std::lock_guard(pending->mutex);
		if (pending->done) {
			return;
		}
		pending->done = true;
		pending->response = std::move(response);
		pending->ready.notify_all();
	}

	static void Fail(
			const std::shared_ptr<Pending> &pending,
			engine::HttpHostErrorKind kind,
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
		not_null<Main::Session*> session)
	: _weak(weak)
	, _session(session) {
	}

	[[nodiscard]] std::vector<uint8_t> read_protected_secret(
			const engine::ProtectedSecretRead &request) override {
		// The wallet-passcode gate hooks here once the Wallet::Session
		// migration lands: authorize request.reason (kSignTransfer,
		// kSignTonConnectProof, kRevealRecoveryPhrase) with
		// request.prompt, honoring the stored require-user-presence
		// flag, before handing out the secret bytes.
		const auto key = SecretStorageKey(request.secret_ref);
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
		}
		const auto record = DeserializeSecretRecord(value->bytes);
		if (!record) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"stored secret is unreadable"_q);
		}
		const auto &bytes = record->bytes;
		return std::vector<uint8_t>(
			bytes.constData(),
			bytes.constData() + bytes.size());
	}

	void store_protected_secret(
			const engine::ProtectedSecretStore &request) override {
		const auto key = SecretStorageKey(request.secret_ref);
		const auto value = SerializeSecretRecord(request);
		const auto done = storage([=](Storage::Account &local) {
			local.writeWalletEngineValue(key, value);
			return true;
		});
		if (!done) {
			throw HostFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet engine storage is unavailable"_q);
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
			local.writeWalletEngineValue(storageKey, replacement);
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
, _api(api)
, _httpHost(std::make_shared<HttpHost>(base::make_weak(this), api))
, _platformHost(std::make_shared<PlatformHost>(
	base::make_weak(this),
	session)) {
}

Engine::~Engine() {
	// Complete every marshalled call so the worker can never wait on the
	// main thread again, then run a best-effort client shutdown and join.
	// The send journal is durable, so an interrupted shutdown is recovered
	// by resolve_pending() on the next launch by the engine's own design.
	_httpHost->close();
	_platformHost->close();
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
	_worker->thread.join();
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

	_httpHost->startClient(
		QString::fromStdString(config.providers.toncenter_base_url));
	_client = engine::WalletClient::init(config, _httpHost, _platformHost);
}

auto Engine::client() const
-> std::shared_ptr<wallet_engine::WalletClient> {
	return _client;
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

void Engine::Execute(
		base::weak_ptr<Engine> weak,
		FnMut<void()> job,
		Fn<void(EngineError)> fail) {
	try {
		job();
	} catch (const std::exception &e) {
		// Typed engine errors are lifted from Rust through their default
		// constructors, so what() is empty for them; fall back to the
		// dynamic type name and let consumers dispatch on `underlying`.
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

void Engine::enqueue(FnMut<void()> task) {
	if (!_worker) {
		_worker = std::make_unique<Worker>();
		_worker->thread = std::thread([=] { workerLoop(); });
	}
	{
		auto lock = std::lock_guard(_worker->mutex);
		if (_worker->stopping) {
			return;
		}
		_worker->queue.push_back(std::move(task));
	}
	_worker->wake.notify_all();
}

void Engine::workerLoop() {
	while (true) {
		auto task = FnMut<void()>();
		{
			auto lock = std::unique_lock(_worker->mutex);
			_worker->wake.wait(lock, [&] {
				return _worker->stopping || !_worker->queue.empty();
			});
			if (_worker->queue.empty()) {
				break;
			}
			task = std::move(_worker->queue.front());
			_worker->queue.pop_front();
		}
		try {
			task();
		} catch (...) {
		}
	}
}

} // namespace Wallet
