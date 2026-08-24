/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace wallet_engine {
struct WalletClient;
struct WalletClientConfig;
struct WalletLifecycle;
} // namespace wallet_engine

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

class Api;
class EngineHttpHost;
class EnginePlatformHost;

struct EngineError {
	QString message;
};

// Owns the wallet-engine host implementations and the single worker thread
// that runs the engine's blocking calls. The engine translates nothing by
// itself: HTTP goes through Wallet::Api on the main thread, secrets and the
// send journal go through Storage::Account on the main thread, and every
// blocking engine method runs on the worker through run().
class Engine final : public base::has_weak_ptr {
public:
	explicit Engine(not_null<Main::Session*> session);
	~Engine();

	[[nodiscard]] not_null<Main::Session*> session() const {
		return _session;
	}

	// The proxy transport this engine sends through. Main thread only.
	[[nodiscard]] Api &api() const;

	// The shared lifecycle service, created on first use. Its blocking
	// methods must be called through run(), never on the main thread.
	[[nodiscard]] auto lifecycle()
		-> std::shared_ptr<wallet_engine::WalletLifecycle>;

	// The client for one configured wallet, replacing any previous one.
	// Construction only validates the config, so it runs on the main thread.
	void startClient(const wallet_engine::WalletClientConfig &config);
	[[nodiscard]] auto client() const
		-> std::shared_ptr<wallet_engine::WalletClient>;

	// Runs the blocking client shutdown on the worker and reports on main.
	void stopClient(Fn<void()> done);

	// Runs a blocking engine call on the worker thread and delivers the
	// result or the engine error on the main thread. Both callbacks are
	// dropped when the Engine is destroyed before the job finishes.
	template <typename Job, typename Done>
	void run(Job job, Done done, Fn<void(EngineError)> fail) {
		enqueue([
			weak = base::make_weak(this),
			job = std::move(job),
			done = std::move(done),
			fail = std::move(fail)
		]() mutable {
			Execute(weak, std::move(job), std::move(done), std::move(fail));
		});
	}

	// Runs a short engine call (cancel_*) on the shared thread pool, so it
	// can preempt a worker blocked inside a long call. Only for methods
	// the engine contract documents as quick and non-blocking.
	template <typename Job, typename Done>
	void runQuick(Job job, Done done, Fn<void(EngineError)> fail) {
		crl::async([
			weak = base::make_weak(this),
			job = std::move(job),
			done = std::move(done),
			fail = std::move(fail)
		]() mutable {
			Execute(weak, std::move(job), std::move(done), std::move(fail));
		});
	}

private:
	template <typename Job, typename Done>
	static void Execute(
			base::weak_ptr<Engine> weak,
			Job &&job,
			Done &&done,
			Fn<void(EngineError)> fail) {
		try {
			if constexpr (std::is_void_v<std::invoke_result_t<Job>>) {
				job();
				crl::on_main(weak, [done = std::move(done)] {
					done();
				});
			} else {
				auto result = job();
				crl::on_main(weak, [
					done = std::move(done),
					result = std::move(result)
				]() mutable {
					done(std::move(result));
				});
			}
		} catch (const std::exception &e) {
			const auto error = EngineError{
				.message = QString::fromUtf8(e.what()),
			};
			crl::on_main(weak, [fail = std::move(fail), error] {
				fail(error);
			});
		} catch (...) {
			const auto error = EngineError{
				.message = u"unexpected wallet engine error"_q,
			};
			crl::on_main(weak, [fail = std::move(fail), error] {
				fail(error);
			});
		}
	}

	void enqueue(FnMut<void()> task);
	void workerLoop();

	const not_null<Main::Session*> _session;
	const std::unique_ptr<Api> _api;
	std::shared_ptr<EngineHttpHost> _httpHost;
	std::shared_ptr<EnginePlatformHost> _platformHost;
	std::shared_ptr<wallet_engine::WalletLifecycle> _lifecycle;
	std::shared_ptr<wallet_engine::WalletClient> _client;

	std::mutex _mutex;
	std::condition_variable _wake;
	std::deque<FnMut<void()>> _queue;
	bool _stopping = false;
	std::thread _thread;

};

} // namespace Wallet
