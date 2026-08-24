/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_engine.h"

#include "gram/api/gram_api_request.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "wallet/wallet_api.h"

#include "wallet_engine.hpp"

#include <QtCore/QUrl>

namespace Wallet {
namespace {

namespace engine = wallet_engine;

[[nodiscard]] engine::http_host_error::Failed HttpFailed(
		engine::HttpHostErrorKind kind,
		const QString &diagnostic) {
	auto result = engine::http_host_error::Failed(diagnostic.toStdString());
	result.kind = kind;
	result.diagnostic = diagnostic.toStdString();
	return result;
}

[[nodiscard]] auto SecretFailed(
		engine::ProtectedSecretHostErrorKind kind,
		const QString &diagnostic) {
	auto result = engine::protected_secret_host_error::Failed(
		diagnostic.toStdString());
	result.kind = kind;
	result.diagnostic = diagnostic.toStdString();
	return result;
}

[[nodiscard]] auto JournalFailed(
		engine::JournalHostErrorKind kind,
		const QString &diagnostic) {
	auto result = engine::journal_host_error::Failed(
		diagnostic.toStdString());
	result.kind = kind;
	result.diagnostic = diagnostic.toStdString();
	return result;
}

[[nodiscard]] QString SecretStorageKey(const std::string &reference) {
	return u"secret/"_q + QString::fromStdString(reference);
}

[[nodiscard]] QString JournalStorageKey(const engine::JournalKey &key) {
	return u"journal/"_q
		+ QString::fromStdString(key.record_id)
		+ '/'
		+ QString::fromStdString(key.slot);
}

[[nodiscard]] QByteArray SerializeJournalRecord(
		const engine::JournalRecord &record) {
	auto result = QByteArray();
	{
		auto stream = QDataStream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << quint64(record.version) << QByteArray(
			reinterpret_cast<const char*>(record.payload.data()),
			record.payload.size());
	}
	return result;
}

[[nodiscard]] std::optional<engine::JournalRecord> DeserializeJournalRecord(
		const QByteArray &serialized) {
	auto stream = QDataStream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	auto version = quint64();
	auto payload = QByteArray();
	stream >> version >> payload;
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return engine::JournalRecord{
		.version = version,
		.payload = std::vector<uint8_t>(
			payload.constData(),
			payload.constData() + payload.size()),
	};
}

} // namespace

// Implements the engine's HTTP callback over the main-thread MTProto proxy
// transport. execute_http() blocks the calling engine worker until the
// answer arrives, the engine-supplied timeout expires, the request is
// cancelled, or the Engine closes.
class EngineHttpHost final : public engine::WalletHttpHost {
public:
	EngineHttpHost(base::weak_ptr<Engine> weak, not_null<Api*> api)
	: _weak(weak)
	, _api(api) {
	}

	engine::HttpResponse execute_http(
			const engine::HttpRequest &request) override {
		const auto id = request.id.value;
		const auto url = QUrl(QString::fromStdString(request.url));
		if (!url.path().startsWith(u"/api/"_q)) {
			throw HttpFailed(
				engine::HttpHostErrorKind::kPolicyViolation,
				u"unexpected request path"_q);
		}
		const auto pending = std::make_shared<Pending>();
		{
			auto lock = std::lock_guard(_mutex);
			if (_closed) {
				throw HttpFailed(
					engine::HttpHostErrorKind::kCancelled,
					u"wallet engine is closing"_q);
			} else if (_cancelledEarly.remove(id)) {
				throw HttpFailed(
					engine::HttpHostErrorKind::kCancelled,
					u"cancelled before start"_q);
			}
			_pending.emplace(id, pending);
		}
		const auto gram = Gram::HttpRequest{
			.post = (request.method == engine::HttpMethod::kPost),
			.endpoint = url.path(),
			.query = url.query(),
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
			api->request(gram, [=](const QByteArray &bytes) {
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
			throw HttpFailed(pending->error->first, pending->error->second);
		}
		return *pending->response;
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
			_cancelledEarly.emplace(id.value);
		}
	}

	// Main thread. Cancels state left by a replaced client: request ids
	// restart in a fresh client, so stale entries must not leak across.
	void resetForNewClient() {
		auto pending = base::flat_map<uint64, std::shared_ptr<Pending>>();
		{
			auto lock = std::lock_guard(_mutex);
			if (_closed) {
				return;
			}
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

	const base::weak_ptr<Engine> _weak;
	Api * const _api; // Main thread only.

	std::mutex _mutex;
	bool _closed = false;
	base::flat_map<uint64, std::shared_ptr<Pending>> _pending;
	base::flat_set<uint64> _cancelledEarly;

};

// Implements the engine's protected-secret and send-journal storage over
// Storage::Account's encrypted wallet-engine records. Every call marshals
// to the main thread and blocks the engine worker until it is served, so
// journal compare-exchange is naturally serialized.
class EnginePlatformHost final : public engine::WalletPlatformHost {
public:
	EnginePlatformHost(
		base::weak_ptr<Engine> weak,
		not_null<Main::Session*> session)
	: _weak(weak)
	, _session(session) {
	}

	std::vector<uint8_t> read_protected_secret(
			const engine::ProtectedSecretRead &request) override {
		// The storage cache keeps the encrypted-at-rest bytes in memory,
		// matching the legacy wallet that held the derived key in memory.
		const auto key = SecretStorageKey(request.secret_ref.value);
		auto value = storage([=](Storage::Account &local) {
			return local.walletEngineValue(key);
		});
		if (!value) {
			throw SecretFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet engine is closing"_q);
		} else if (!*value) {
			throw SecretFailed(
				engine::ProtectedSecretHostErrorKind::kNotFound,
				u"secret not found"_q);
		}
		const auto &bytes = **value;
		return std::vector<uint8_t>(
			bytes.constData(),
			bytes.constData() + bytes.size());
	}

	void store_protected_secret(
			const engine::ProtectedSecretStore &request) override {
		const auto key = SecretStorageKey(request.secret_ref.value);
		const auto value = QByteArray(
			reinterpret_cast<const char*>(request.bytes.data()),
			request.bytes.size());
		const auto done = storage([=](Storage::Account &local) {
			local.setWalletEngineValue(key, value);
			return true;
		});
		if (!done) {
			throw SecretFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet engine is closing"_q);
		}
	}

	void delete_protected_secret(
			const engine::ProtectedSecretRef &reference) override {
		const auto key = SecretStorageKey(reference.value);
		const auto done = storage([=](Storage::Account &local) {
			local.setWalletEngineValue(key, std::nullopt);
			return true;
		});
		if (!done) {
			throw SecretFailed(
				engine::ProtectedSecretHostErrorKind::kUnavailable,
				u"wallet engine is closing"_q);
		}
	}

	std::optional<engine::JournalRecord> load_journal(
			const engine::JournalKey &key) override {
		const auto storageKey = JournalStorageKey(key);
		const auto value = storage([=](Storage::Account &local) {
			return local.walletEngineValue(storageKey);
		});
		if (!value) {
			throw JournalFailed(
				engine::JournalHostErrorKind::kUnavailable,
				u"wallet engine is closing"_q);
		} else if (!*value) {
			return std::nullopt;
		}
		const auto record = DeserializeJournalRecord(**value);
		if (!record) {
			throw JournalFailed(
				engine::JournalHostErrorKind::kCorruptData,
				u"stored journal record is unreadable"_q);
		}
		return record;
	}

	engine::JournalCompareExchangeResult compare_exchange_journal(
			const engine::JournalCompareExchange &mutation) override {
		const auto storageKey = JournalStorageKey(mutation.key);
		const auto replacement = SerializeJournalRecord(mutation.replacement);
		const auto expected = mutation.expected_version;
		struct Outcome {
			bool applied = false;
			std::optional<QByteArray> current;
			bool corrupt = false;
		};
		const auto outcome = storage([=](Storage::Account &local) {
			const auto current = local.walletEngineValue(storageKey);
			if (!current) {
				if (expected) {
					return Outcome{ .applied = false };
				}
			} else {
				const auto record = DeserializeJournalRecord(*current);
				if (!record) {
					return Outcome{ .corrupt = true };
				} else if (!expected || *expected != record->version) {
					return Outcome{
						.applied = false,
						.current = current,
					};
				}
			}
			local.setWalletEngineValue(storageKey, replacement);
			return Outcome{ .applied = true, .current = replacement };
		});
		if (!outcome) {
			throw JournalFailed(
				engine::JournalHostErrorKind::kUnavailable,
				u"wallet engine is closing"_q);
		} else if (outcome->corrupt) {
			throw JournalFailed(
				engine::JournalHostErrorKind::kCorruptData,
				u"stored journal record is unreadable"_q);
		}
		auto result = engine::JournalCompareExchangeResult{
			.applied = outcome->applied,
			.current = std::nullopt,
		};
		if (outcome->current) {
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
	// the engine worker until it finishes. Returns nullopt after close().
	template <typename Task>
	auto storage(Task task)
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
			state->result = task(session->local());
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

Engine::Engine(not_null<Main::Session*> session)
: _session(session)
, _api(std::make_unique<Api>(session))
, _httpHost(std::make_shared<EngineHttpHost>(
	base::make_weak(this),
	_api.get()))
, _platformHost(std::make_shared<EnginePlatformHost>(
	base::make_weak(this),
	session))
, _thread([=] { workerLoop(); }) {
}

Engine::~Engine() {
	// Complete every marshalled call so the worker can never wait on the
	// main thread again, then run a best-effort client shutdown and join.
	// The send journal is durable, so an interrupted shutdown is recovered
	// by resolve_pending() on the next launch by the engine's own design.
	_httpHost->close();
	_platformHost->close();
	{
		auto lock = std::lock_guard(_mutex);
		_queue.clear();
		if (const auto client = base::take(_client)) {
			_queue.push_back([client] {
				try {
					client->shutdown();
				} catch (...) {
				}
			});
		}
		_stopping = true;
	}
	_wake.notify_all();
	_thread.join();
}

Api &Engine::api() const {
	return *_api;
}

auto Engine::lifecycle()
-> std::shared_ptr<wallet_engine::WalletLifecycle> {
	if (!_lifecycle) {
		_lifecycle = engine::WalletLifecycle::init(_platformHost);
	}
	return _lifecycle;
}

void Engine::startClient(const engine::WalletClientConfig &config) {
	_httpHost->resetForNewClient();
	_client = engine::WalletClient::init(config, _httpHost, _platformHost);
}

auto Engine::client() const
-> std::shared_ptr<wallet_engine::WalletClient> {
	return _client;
}

void Engine::stopClient(Fn<void()> done) {
	const auto client = base::take(_client);
	if (!client) {
		done();
		return;
	}
	run([client] {
		client->shutdown();
	}, std::move(done), [](EngineError) {});
}

void Engine::enqueue(FnMut<void()> task) {
	{
		auto lock = std::lock_guard(_mutex);
		if (_stopping) {
			return;
		}
		_queue.push_back(std::move(task));
	}
	_wake.notify_all();
}

void Engine::workerLoop() {
	while (true) {
		auto task = FnMut<void()>();
		{
			auto lock = std::unique_lock(_mutex);
			_wake.wait(lock, [&] {
				return _stopping || !_queue.empty();
			});
			if (_queue.empty()) {
				break;
			}
			task = std::move(_queue.front());
			_queue.pop_front();
		}
		task();
	}
}

} // namespace Wallet
