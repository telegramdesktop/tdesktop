/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <memory>
#include <optional>
#include <vector>

namespace wallet_engine {
struct SendResult;
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

// Why the platform host refused a protected-secret read. Missing is the one
// answer a caller may act on irreversibly - the value is not in storage at
// all - while Unreadable covers every shape of "it is there and this read
// did not produce it", which a locked keyring, a corrupt record and a
// momentary host error all reach alike.
enum class SecretReadFailure : uchar {
	None,
	Locked,
	Missing,
	Unreadable,
};

struct EngineError {
	QString message;

	// The engine's typed error when one produced this failure. Rethrow it
	// to dispatch on the generated wallet_engine error taxonomy.
	std::exception_ptr underlying;

	// What the host answered to a protected-secret read this job made, as
	// the watch around the job recorded it. The engine rewrites such a
	// refusal into its own client taxonomy - a send answers SendFailed -
	// so by the time `underlying` is inspected the read's own verdict is
	// no longer in it, and this field is what survives the rewrite.
	SecretReadFailure secret = SecretReadFailure::None;
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

// Whether the platform host refused a protected-secret read during the
// engine call running on this thread. The engine folds such a refusal into
// the same typed failure as a decryption with the wrong key, so a caller
// that must tell the two apart opens a watch around its call and reads it
// afterwards. Watches nest: a refusal is recorded by every watch open on
// the thread, so the one Engine::Execute holds around each job sees what an
// inner watch saw. Same thread contract as the store recording above.
class SecretReadWatch final {
public:
	SecretReadWatch();
	SecretReadWatch(const SecretReadWatch &other) = delete;
	SecretReadWatch &operator=(const SecretReadWatch &other) = delete;
	~SecretReadWatch();

	[[nodiscard]] SecretReadFailure failure() const;

	// Worker thread. Called by the platform host when a read is refused.
	// Does nothing when no watch is open on this thread.
	static void MarkFailed(SecretReadFailure failure);

private:
	SecretReadWatch *_previous = nullptr;
	SecretReadFailure _failure = SecretReadFailure::None;

};

// The same verdict the watch records, for a caller that holds the engine's
// typed error instead. Answers None for every failure that is not the host
// refusing a protected secret.
[[nodiscard]] SecretReadFailure ProtectedSecretFailure(
	const EngineError &error);

enum class TransferSubmissionOutcome {
	Accepted,
	Rejected,
	Uncertain,
};

struct TransferSubmissionAnswer {
	TransferSubmissionOutcome outcome = TransferSubmissionOutcome::Uncertain;
	QString diagnostic;
};

struct TransferSubmissionData {
	QByteArray normal;
	std::optional<QByteArray> gasless;
};

// The send whose one sendBoc submission the status-less host routes through
// Telegram's wallet.sendTransfer instead of the toncenter proxy. The host
// decodes the signed BOC out of the engine's request, pairs it with the
// relayer alternative the recording carries, hands both to the main thread
// through submit(), and answers the engine with a provider-shaped body
// (accepted / definitely rejected) or a host error (uncertain), so the
// engine's journal, phases and resolution stay authoritative.
//
// A gasless-eligible transfer signs both delivery forms in one engine
// prepare_transfer() and submits the external one through send_boc(), so the
// alternative the server may pick instead never travels inside the engine's
// own request. The recording carries it beside the routed call, which is why
// only the normal BOC is decoded out of the request here.
//
// Attribution: a submission belongs to the send whose recording is open on the
// calling thread. Engine::run() runs jobs one at a time on one worker and
// runQuick()'s contract forbids network, so no other engine call of this
// Engine can be routed while a recording is open; the rotation's send_boc
// opens none and keeps the proxy transport. A recording wraps exactly one
// client->send, client->send_boc or client->send_nft_transfer call.
//
// Lifetime: Current() hands the host a copy of the thread-local, and the
// host's queued main-thread work holds that reference itself, so a job that
// times out, throws and drops its own capture cannot leave the queued work
// dangling.
class TransferSubmission final
	: public std::enable_shared_from_this<TransferSubmission> {
public:
	using Submit = Fn<void(
		TransferSubmissionData data,
		Fn<void(TransferSubmissionAnswer)> done)>;

	explicit TransferSubmission(Submit submit);

	class Recording final {
	public:
		Recording(
			std::shared_ptr<TransferSubmission> submission,
			std::optional<QByteArray> gasless);
		Recording(const Recording &other) = delete;
		Recording &operator=(const Recording &other) = delete;
		~Recording();

	private:
		std::shared_ptr<TransferSubmission> _previous;
		std::optional<QByteArray> _previousGasless;

	};

	// Worker thread, inside the job, around the one send call whose
	// submission is being routed. `gasless` is the relayer alternative
	// prepared for the same seqno and validity window, absent for a send
	// with no fee-free offer.
	[[nodiscard]] Recording record(
		std::optional<QByteArray> gasless = std::nullopt);

	// Worker thread. The submission recorded on this thread, empty when no
	// recording is open.
	[[nodiscard]] static std::shared_ptr<TransferSubmission> Current();

	// Worker thread. The relayer alternative of the open recording.
	[[nodiscard]] static std::optional<QByteArray> CurrentGasless();

	// Main thread. Answers through `done` exactly once, on the main thread.
	void submit(
		TransferSubmissionData data,
		Fn<void(TransferSubmissionAnswer)> done);

private:
	const Submit _submit;

};

// Owns the wallet-engine host implementations and the workers running
// blocking engine calls. Local secret reads use their own serial worker so
// background network requests cannot delay them. The engine translates
// nothing by itself: HTTP goes through the caller-owned Wallet::Api on the main thread,
// secrets and the send journal go through Storage::Account on the main
// thread. Network operations and storage writes run through run(); only
// local read operations may run through runLocal().
// Each secret has its own K in the domain keyring. Engines borrow the shared
// VaultRuntime, which owns D, grants and retention for every signed-in account.
// Every Engine method must be called on the main thread.
class Engine final : public base::has_weak_ptr {
public:
	Engine(not_null<Main::Session*> session, not_null<Api*> api);
	~Engine();

	[[nodiscard]] not_null<Main::Session*> session() const {
		return _session;
	}

	// The shared lifecycle service, created on first use. Its blocking
	// methods must be called through run() or runLocal(), never directly.
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

	// The domain's runtime, shared with every other signed-in account.
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
		Enqueue(_worker, Package(
			base::make_weak(this),
			_vault,
			_accountId,
			privateAccess(),
			std::move(job),
			std::move(done),
			std::move(fail)));
	}

	template <typename Job, typename Done>
	void runLocal(Job job, Done done, Fn<void(EngineError)> fail) {
		Enqueue(_localWorker, Package(
			base::make_weak(this),
			_vault,
			_accountId,
			privateAccess(),
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
			_vault,
			_accountId,
			privateAccess(),
			std::move(job),
			std::move(done),
			std::move(fail)));
	}

private:
	class StatuslessHost;
	class PlatformHost;
	struct Worker;
	struct PrivateAccess {
		quint32 epoch = 0;
		bool used = false;
	};

	[[nodiscard]] std::shared_ptr<PrivateAccess> privateAccess() const;
	[[nodiscard]] static bool PrivateResultCurrent(
		const VaultRuntime &vault,
		uint64 accountId,
		const PrivateAccess &access);
	[[nodiscard]] static EngineError PrivateAccessError();
	static thread_local PrivateAccess *_privateAccess;

	static void Execute(
		base::weak_ptr<Engine> weak,
		const std::shared_ptr<PrivateAccess> &access,
		FnMut<void()> job,
		Fn<void(EngineError)> fail);

	// A job starts under one clear epoch even while it waits in the queue.
	// Only a protected host call marks it private, so ordinary refreshes keep
	// their normal completion behavior. A private result is checked again on
	// main before delivery; plaintext already produced on the worker cannot
	// escape through a delayed success callback after lock or rotation.
	// SendResult reports the completed submission and its already journaled
	// signed message, not fresh private access. Preserve that fact so a clear
	// while awaiting the provider cannot turn an accepted send into failure.
	// This exception never bypasses the protected host's own access checks or
	// authorizes delivery of a phrase, decrypted comment or prepared signature.
	template <typename Job, typename Done>
	[[nodiscard]] static FnMut<void()> Package(
			base::weak_ptr<Engine> weak,
			std::shared_ptr<VaultRuntime> vault,
			uint64 accountId,
			std::shared_ptr<PrivateAccess> access,
			Job job,
			Done done,
			Fn<void(EngineError)> fail) {
		auto wrapped = [
			weak,
			vault,
			accountId,
			access,
			fail,
			job = std::move(job),
			done = std::move(done)
		]() mutable {
			if constexpr (std::is_void_v<std::invoke_result_t<Job>>) {
				job();
				crl::on_main(weak, [
					vault,
					accountId,
					access,
					fail,
					done = std::move(done)
				] {
					if (PrivateResultCurrent(*vault, accountId, *access)) {
						done();
					} else {
						fail(PrivateAccessError());
					}
				});
			} else {
				auto result = job();
				crl::on_main(weak, [
					vault,
					accountId,
					access,
					fail,
					done = std::move(done),
					result = std::move(result)
				]() mutable {
					if (std::is_same_v<decltype(result), wallet_engine::SendResult>
						|| PrivateResultCurrent(*vault, accountId, *access)) {
						done(std::move(result));
					} else {
						fail(PrivateAccessError());
					}
				});
			}
		};
		return [
			weak,
			access,
			wrapped = std::move(wrapped),
			fail = std::move(fail)
		]() mutable {
			Execute(weak, access, std::move(wrapped), std::move(fail));
		};
	}

	static void Enqueue(
		std::unique_ptr<Worker> &worker,
		FnMut<void()> task);
	static void WorkerLoop(not_null<Worker*> worker);

	const not_null<Main::Session*> _session;
	const uint64 _accountId;
	std::shared_ptr<StatuslessHost> _statuslessHost;
	const std::shared_ptr<VaultRuntime> _vault;
	std::shared_ptr<PlatformHost> _platformHost;
	std::shared_ptr<wallet_engine::WalletLifecycle> _lifecycle;
	std::shared_ptr<wallet_engine::WalletClient> _client;
	std::unique_ptr<Worker> _worker;
	std::unique_ptr<Worker> _localWorker;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
