/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <memory>

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

struct EngineError {
	QString message;

	// The engine's typed error when one produced this failure. Rethrow it
	// to dispatch on the generated wallet_engine error taxonomy.
	std::exception_ptr underlying;
};

// Owns the wallet-engine host implementations and the single worker thread
// that runs the engine's blocking calls. The engine translates nothing by
// itself: HTTP goes through the caller-owned Wallet::Api on the main thread,
// secrets and the send journal go through Storage::Account on the main
// thread, and every blocking engine method runs on the worker through run().
// Every Engine method must be called on the main thread.
class Engine final : public base::has_weak_ptr {
public:
	Engine(not_null<Main::Session*> session, not_null<Api*> api);
	~Engine();

	[[nodiscard]] not_null<Main::Session*> session() const {
		return _session;
	}

	// The shared lifecycle service, created on first use. Its blocking
	// methods must be called through run(), never invoked directly.
	[[nodiscard]] auto lifecycle()
		-> std::shared_ptr<wallet_engine::WalletLifecycle>;

	// The client for one configured wallet. A live client must first be
	// stopped through stopClient() and its callback awaited. Construction
	// only validates the config, so it runs on the main thread.
	void startClient(const wallet_engine::WalletClientConfig &config);
	[[nodiscard]] auto client() const
		-> std::shared_ptr<wallet_engine::WalletClient>;

	// Runs the blocking client shutdown on the worker and reports on main.
	// The client stays owned (and returned by client()) until `done` runs.
	void stopClient(Fn<void()> done);

	// Runs a blocking engine call on the worker thread and delivers the
	// result or the engine error on the main thread. Both callbacks are
	// dropped when the Engine is destroyed before the job finishes. The
	// queue does not coalesce: don't enqueue a repeatable operation, like
	// a refresh, while the previous one is still outstanding.
	template <typename Job, typename Done>
	void run(Job job, Done done, Fn<void(EngineError)> fail) {
		enqueue(Package(
			base::make_weak(this),
			std::move(job),
			std::move(done),
			std::move(fail)));
	}

	// Runs a short engine call (cancel_*) on the shared thread pool, so it
	// can preempt a worker blocked inside a long call. Only for methods
	// the engine contract documents as quick and non-blocking. The job
	// must capture its client / lifecycle shared_ptr here, on the main
	// thread, and must not touch storage, network or Engine members.
	template <typename Job, typename Done>
	void runQuick(Job job, Done done, Fn<void(EngineError)> fail) {
		crl::async(Package(
			base::make_weak(this),
			std::move(job),
			std::move(done),
			std::move(fail)));
	}

private:
	class HttpHost;
	class PlatformHost;
	struct Worker;

	static void Execute(
		base::weak_ptr<Engine> weak,
		FnMut<void()> job,
		Fn<void(EngineError)> fail);

	template <typename Job, typename Done>
	[[nodiscard]] static FnMut<void()> Package(
			base::weak_ptr<Engine> weak,
			Job job,
			Done done,
			Fn<void(EngineError)> fail) {
		auto wrapped = [
			weak,
			job = std::move(job),
			done = std::move(done)
		]() mutable {
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
		};
		return [
			weak,
			wrapped = std::move(wrapped),
			fail = std::move(fail)
		]() mutable {
			Execute(weak, std::move(wrapped), std::move(fail));
		};
	}

	void enqueue(FnMut<void()> task);
	void workerLoop();

	const not_null<Main::Session*> _session;
	const not_null<Api*> _api;
	std::shared_ptr<HttpHost> _httpHost;
	std::shared_ptr<PlatformHost> _platformHost;
	std::shared_ptr<wallet_engine::WalletLifecycle> _lifecycle;
	std::shared_ptr<wallet_engine::WalletClient> _client;
	std::unique_ptr<Worker> _worker;

};

} // namespace Wallet
