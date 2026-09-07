/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <memory>
#include <vector>

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
class VaultRuntime;

struct EngineError {
	QString message;

	// The engine's typed error when one produced this failure. Rethrow it
	// to dispatch on the generated wallet_engine error taxonomy.
	std::exception_ptr underlying;
};

// The protected-secret storage keys one engine call caused this
// application to write, so a call that fails after its own store can
// remove exactly what it wrote. The key is REMEMBERED here, never
// derived: PlatformHost::store_protected_secret computes the account
// storage key itself before it marshals the write, and the engine's
// reference format is opaque to this client.
//
// Attribution: a store belongs to a call if and only if it is committed
// on the thread that opened that call's recording, while it is open.
// Engine::run() runs jobs one at a time on one worker and runQuick()'s
// contract forbids storage, so no other operation of this Engine can
// store into an open recording; the binding to the opening thread is
// what keeps another account's Engine out of it too. A store on any
// other thread is not recorded, which leaves the record behind exactly
// as today - the safe direction, because deleting another operation's
// record is worse than the orphan.
//
// Lifetime: the handle is created before the call and owned by that
// call's continuations, so a successful call simply drops it and no
// record is ever eligible for a later drop.
class EngineSecretStores final {
public:
	class Recording final {
	public:
		explicit Recording(not_null<EngineSecretStores*> stores);
		Recording(const Recording &other) = delete;
		Recording &operator=(const Recording &other) = delete;
		~Recording();

	private:
		EngineSecretStores *_previous = nullptr;

	};

	// Worker thread, inside the job, around the one engine call whose
	// stores are being attributed.
	[[nodiscard]] Recording record();

	// Worker thread. Called by the platform host once a store reached
	// disk. Does nothing when no recording is open on this thread.
	static void Remember(const QString &storageKey);

	// Main thread. The keys recorded so far, leaving none behind.
	[[nodiscard]] std::vector<QString> take();

private:
	std::vector<QString> _keys;

};

enum class TransferSubmissionOutcome {
	Accepted,
	Rejected,
	Uncertain,
};

struct TransferSubmissionAnswer {
	TransferSubmissionOutcome outcome = TransferSubmissionOutcome::Uncertain;
	QString diagnostic;
};

// The ordinary send whose one sendBoc submission the status-less host routes
// through Telegram's wallet.sendTransfer instead of the toncenter proxy. The
// host decodes the signed BOC out of the engine's request, hands it to the
// main thread through submit(), and answers the engine with a provider-shaped
// body (accepted / definitely rejected) or a host error (uncertain), so the
// engine's journal, phases and resolution stay authoritative.
//
// Attribution: a sendBoc belongs to the send whose recording is open on the
// calling thread. Engine::run() runs jobs one at a time on one worker and
// runQuick()'s contract forbids network, so no other engine call of this
// Engine can be routed while a recording is open; the rotation's send_boc
// opens none and keeps the proxy transport. A recording wraps exactly one
// client->send call.
//
// Lifetime: Current() hands the host a copy of the thread-local, and the
// host's queued main-thread work holds that reference itself, so a job that
// times out, throws and drops its own capture cannot leave the queued work
// dangling.
class TransferSubmission final
	: public std::enable_shared_from_this<TransferSubmission> {
public:
	using Submit = Fn<void(
		QByteArray boc,
		Fn<void(TransferSubmissionAnswer)> done)>;

	explicit TransferSubmission(Submit submit);

	class Recording final {
	public:
		explicit Recording(std::shared_ptr<TransferSubmission> submission);
		Recording(const Recording &other) = delete;
		Recording &operator=(const Recording &other) = delete;
		~Recording();

	private:
		std::shared_ptr<TransferSubmission> _previous;

	};

	// Worker thread, inside the job, around the one client->send call
	// whose submission is being routed.
	[[nodiscard]] Recording record();

	// Worker thread. The submission recorded on this thread, empty when no
	// recording is open.
	[[nodiscard]] static std::shared_ptr<TransferSubmission> Current();

	// Main thread. Answers through `done` exactly once, on the main thread.
	void submit(QByteArray boc, Fn<void(TransferSubmissionAnswer)> done);

private:
	const Submit _submit;

};

// Owns the wallet-engine host implementations and the single worker thread
// that runs the engine's blocking calls. The engine translates nothing by
// itself: HTTP goes through the caller-owned Wallet::Api on the main thread,
// secrets and the send journal go through Storage::Account on the main
// thread, and every blocking engine method runs on the worker through run().
// The protected secrets are sealed under the account's vault, whose unlocked
// state (VaultRuntime) the Engine owns and the platform host consults.
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
	// only validates the config, so it runs on the main thread. The
	// config's toncenter_base_url must name the provider the MTProto
	// proxy actually serves, in canonical form (lowercase host, no
	// default port): the status-less transport reports no final URL, so
	// this contract and the host's own origin check are the only origin
	// guarantee the engine has.
	void startClient(const wallet_engine::WalletClientConfig &config);
	[[nodiscard]] auto client() const
		-> std::shared_ptr<wallet_engine::WalletClient>;

	// The account's vault runtime: unlocked and armed on the main thread by
	// the reveal flow, read on the worker by the platform host.
	[[nodiscard]] VaultRuntime &vault() const;

	// Runs the blocking client shutdown on the worker and reports on main.
	// The client stays owned (and returned by client()) until `done` runs.
	void stopClient(Fn<void()> done);

	// Removes the protected-secret records a failed engine call wrote and
	// forgets them. A call that succeeded never comes here: its handle is
	// dropped instead.
	void dropStoredSecrets(EngineSecretStores &stores);

	// Runs a blocking engine call on the worker thread and delivers the
	// result or the engine error on the main thread. Both callbacks are
	// dropped when the Engine is destroyed before the job finishes. The
	// queue does not coalesce: don't enqueue a repeatable operation, like
	// a refresh, while the previous one is still outstanding. Waiters
	// that only the client shutdown releases (wait_for_change) must not
	// go through here: the serial worker could never advance them, and
	// the destructor's queued shutdown would deadlock behind them.
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
	class StatuslessHost;
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
	std::shared_ptr<StatuslessHost> _statuslessHost;
	const std::shared_ptr<VaultRuntime> _vault;
	std::shared_ptr<PlatformHost> _platformHost;
	std::shared_ptr<wallet_engine::WalletLifecycle> _lifecycle;
	std::shared_ptr<wallet_engine::WalletClient> _client;
	std::unique_ptr<Worker> _worker;

};

} // namespace Wallet
