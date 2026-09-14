/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_key_protection.h"

#include "base/openssl_help.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_user.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/button_busy.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/passcode_strength_meter.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/passcode_strength.h"
#include "ui/text/text_utilities.h"
#include "wallet/wallet_custody.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_unlock.h"

#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_passcode_strength_meter.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

[[nodiscard]] QByteArray Utf8Copy(const SecureBytes &bytes) {
	return QByteArray(
		reinterpret_cast<const char*>(bytes.span().data()),
		bytes.size());
}

// One dependent vault held between StageVaultWrap() and
// CommitStagedVaultWrap(): the account it belongs to, the header that now
// carries both wraps at the unchanged committed generation, and the header
// that was on disk before the stage, which is what a rollback writes back.
// Accounts stay weak because the key_data writer and per-vault commit
// notifications can destroy them synchronously. Resolve each storage access
// afresh and never use an account after notifying its session.
struct StagedVault {
	base::weak_ptr<Main::Account> account;
	VaultHeader staged;
	VaultHeader previous;
};

struct VaultSnapshot {
	base::weak_ptr<Main::Account> account;
	VaultWrap committed;
};

struct DerivedVault {
	base::weak_ptr<Main::Account> account;
	VaultWrap committed;
	SecureBytes vaultKey;
	VaultPreparedWrap prepared;
};

// Rewrites the pre-stage header of every vault that was already staged. A
// write that fails here is still safe: the staged wrap lives outside the
// committed generation, so the next ReadVaultHeader() drops it and
// ReconcileVaultHeader() persists that drop, and the old passcode keeps
// opening the vault either way. Kinds and generations alone are logged.
void RollbackStagedVaults(const std::vector<StagedVault> &vaults) {
	for (const auto &vault : vaults) {
		const auto account = vault.account.get();
		if (!account) {
			continue;
		}
		if (!WriteVaultHeader(
				account->local(),
				vault.previous)) {
			LOG(("Wallet Error: could not roll a staged vault wrap back "
				"at the committed generation %1."
				).arg(vault.previous.committed));
		}
	}
}

// Step zero of the staged passcode change, on the main thread: the committed
// wrap of every dependent vault, copied before the worker derives against
// it. A header that does not read, or whose committed wrap is not a passcode
// one, abandons the change here, where nothing is staged yet.
[[nodiscard]] auto SnapshotDependentVaults(
		const std::vector<not_null<Main::Account*>> &accounts)
-> std::optional<std::vector<VaultSnapshot>> {
	auto result = std::vector<VaultSnapshot>();
	result.reserve(accounts.size());
	for (const auto &account : accounts) {
		const auto reading = ReadVaultHeader(account->local());
		const auto committed = reading.header.committedWrap();
		if (reading.state != VaultReading::State::Read
			|| !committed
			|| committed->kind != VaultKind::Passcode) {
			return std::nullopt;
		}
		result.push_back({
			.account = base::make_weak(account),
			.committed = *committed,
		});
	}
	return result;
}

// The dependents were enumerated before the worker hop, and a vault can
// become passcode-wrapped while the batch derives: a store that finishes
// under an armed Install policy writes a fresh header from the engine
// marshal, and the key-protection switch does the same behind its own gate.
// Such a vault is in neither the batch nor the change, and SetPasscode would
// move key_data to the new passcode while it stays under the old one, so the
// apply asks again here, where nothing is staged yet, and abandons on any
// difference. An account that is gone is ignored, as StageDerivedVaults
// skips it with its vault.
[[nodiscard]] bool SameDependents(const std::vector<DerivedVault> &derived) {
	const auto current = CollectVaultDependents().passcodeWrapped;
	const auto account = [](const DerivedVault &vault) {
		return vault.account.get();
	};
	const auto live = ranges::count_if(derived, [&](
			const DerivedVault &vault) {
		return account(vault) != nullptr;
	});
	return (live == int(current.size()))
		&& ranges::all_of(current, [&](not_null<Main::Account*> dependent) {
			return ranges::contains(derived, dependent.get(), account);
		});
}

// Step one of the staged passcode change, back on the main thread with the
// batch. Every dependent vault gets the new passcode's wrap written beside
// its committed one and its records re-sealed under the fresh vault key,
// beside entries under the old key. Both are proved by read-back while the
// committed generation does not move: the old passcode opens every vault, and
// key_data is untouched, so it still opens the launch lock too. A vault
// whose committed wrap is no longer the one the worker derived against - by
// generation, kind or salt; every committed change bumps the generation -
// abandons the change, and so does any write failure: both roll back only
// this batch's prior stages. The newly changed vault keeps its current header,
// never the stale snapshot. An account that is gone is skipped with its vault.
[[nodiscard]] std::optional<std::vector<StagedVault>> StageDerivedVaults(
		std::vector<DerivedVault> &derived) {
	auto result = std::vector<StagedVault>();
	result.reserve(derived.size());
	const auto abandon = [&]() -> std::optional<std::vector<StagedVault>> {
		RollbackStagedVaults(result);
		return std::nullopt;
	};
	for (auto &vault : derived) {
		const auto account = vault.account.get();
		if (!account) {
			continue;
		}
		auto reading = ReadVaultHeader(account->local());
		if (reading.state != VaultReading::State::Read) {
			return abandon();
		}
		auto header = std::move(reading.header);
		const auto previous = header;
		const auto committed = header.committedWrap();
		if (!committed
			|| committed->generation != vault.committed.generation
			|| committed->kind != vault.committed.kind
			|| committed->salt != vault.committed.salt) {
			return abandon();
		}
		const auto live = vault.account.get();
		if (!live) {
			continue;
		}
		const auto session = live->maybeSession();
		if (session && session->wallet().custodyBusy()) {
			return abandon();
		}
		const auto staged = StageVaultWrap(
			live->local(),
			header,
			vault.vaultKey,
			std::move(vault.prepared));
		if (staged != VaultTransitionResult::Done) {
			return abandon();
		}
		result.push_back({
			.account = vault.account,
			.staged = std::move(header),
			.previous = previous,
		});
	}
	return result;
}

// Step three: the committed generation advances and the old runtime key is
// cleared, then the old record entries and wrap are stripped best-effort,
// account by account in staging order. A failure is not a rollback point:
// key_data already answers to the new passcode, and a vault that did not
// commit still carries the wrap the old one opens. Report that failure and
// continue committing the remaining vaults.
[[nodiscard]] bool CommitStagedVaults(std::vector<StagedVault> &vaults) {
	auto result = true;
	for (auto &vault : vaults) {
		const auto account = vault.account.get();
		if (!account) {
			continue;
		}
		const auto session = account->maybeSession();
		if (!CommitStagedVaultWrap(
				account->local(),
				vault.staged,
				session ? &session->wallet().vault() : nullptr)) {
			result = false;
		} else if (const auto live = vault.account.get()) {
			if (const auto session = live->maybeSession()) {
				session->wallet().notifyKeyProtectionChanged();
			}
		}
	}
	return result;
}

} // namespace

// The memory-hard part of the staged change, cut out as one worker job: the
// key_data wrap for the new passcode and, per dependent vault, the old
// passcode's wrap key, the vault key it opens and the new passcode's wrap.
// It holds values only - a weak account pointer that is never dereferenced
// off the main thread, a copy of the committed wrap it derives against, the
// two typed passcodes as SecureBytes - and touches no store, so nothing is
// written before every derivation has answered. run() cleanses both typed
// passcodes on every exit; the vault keys and wrap keys it produced live in
// SecureBytes and die with the batch on the main thread, applied or not.
struct VaultPasscodeChange::Data {
	Storage::PasscodeDerivation keyData;
	std::vector<VaultSnapshot> vaults;
	SecureBytes oldPasscode;
	SecureBytes newPasscode;
	std::vector<DerivedVault> derived;
	bool failed = false;
};

VaultPasscodeChange::VaultPasscodeChange(std::unique_ptr<Data> data)
: _data(std::move(data)) {
}

VaultPasscodeChange::VaultPasscodeChange(
	VaultPasscodeChange &&other) noexcept = default;

VaultPasscodeChange &VaultPasscodeChange::operator=(
	VaultPasscodeChange &&other) noexcept = default;

VaultPasscodeChange::~VaultPasscodeChange() = default;

std::optional<VaultPasscodeChange> VaultPasscodeChange::Prepare(
		const Storage::Domain &local,
		SecureBytes oldPasscode,
		const QByteArray &newPasscode) {
	auto snapshots = SnapshotDependentVaults(
		CollectVaultDependents().passcodeWrapped);
	if (!snapshots) {
		return std::nullopt;
	}
	return VaultPasscodeChange(std::make_unique<Data>(Data{
		.keyData = local.prepareNewWrap(newPasscode),
		.vaults = std::move(*snapshots),
		.oldPasscode = std::move(oldPasscode),
		.newPasscode = SecureBytes(newPasscode),
	}));
}

void VaultPasscodeChange::run() {
	Expects(_data != nullptr);

	auto &data = *_data;
	auto oldUtf8 = Utf8Copy(data.oldPasscode);
	auto newUtf8 = Utf8Copy(data.newPasscode);
	const auto cleanse = gsl::finally([&] {
		if (!oldUtf8.isEmpty()) {
			OPENSSL_cleanse(oldUtf8.data(), oldUtf8.size());
		}
		if (!newUtf8.isEmpty()) {
			OPENSSL_cleanse(newUtf8.data(), newUtf8.size());
		}
		data.oldPasscode.clear();
		data.newPasscode.clear();
	});
	data.derived.reserve(data.vaults.size());
	for (auto &vault : data.vaults) {
		const auto wrapKey = DeriveVaultWrapKey(
			vault.committed,
			oldUtf8);
		if (!wrapKey) {
			data.failed = true;
			return;
		}
		auto vaultKey = UnwrapVaultKey(vault.committed, *wrapKey);
		if (!vaultKey) {
			data.failed = true;
			return;
		}
		auto prepared = PrepareVaultPasscodeWrap(newUtf8);
		if (!prepared) {
			data.failed = true;
			return;
		}
		data.derived.push_back({
			.account = std::move(vault.account),
			.committed = std::move(vault.committed),
			.vaultKey = std::move(*vaultKey),
			.prepared = std::move(*prepared),
		});
	}
	data.keyData.run();
}

// Stage every dependent, write key_data, then commit every surviving vault:
// all derivations precede this synchronous sequence. Before key_data commits,
// the old passcode opens launch and every dependent; a failed stage or either
// writer refusal rolls back prior stages. After key_data's checked commit,
// even before the writer returns, launch takes the new passcode while an
// uncommitted vault still takes the old one. A failed vault commit is never a
// rollback point, and later commits must still be attempted. Restart settles
// each store by its own committed generation; it does not finish this batch.
//
// Synchronous does not mean callback-free. Storage::Domain fires
// _passcodeKeyChanged, exposed as localPasscodeChanged(), inside the writer;
// its subscribers update passcode labels, lock/minimized controls and wallet
// metadata while headers are dirty. Settings' SetPasscode also calls
// Application::localPasscodeChanged(), whose synchronous auto-lock fan-out
// can tear down UI and clear runtimes.
// Per-vault notifyKeyProtectionChanged() also runs while later headers remain
// staged. The current CollectVaultDependents metadata reader uses
// non-writing ReadVaultHeader(), which filters stages only in its copy.
// A subscriber reaching ReconcileVaultHeader() or VaultRuntime::reading()
// would persist that removal, strip record entries and write away a stage
// between these steps.
// The stack-owned batch and weak accounts survive UI/account destruction
// across those callbacks without an event-loop hop or retained writer.
VaultPasscodeChangeResult VaultPasscodeChange::apply(
		Fn<Storage::SetPasscodeResult(Storage::PasscodeDerivation)> writer) {
	const auto data = base::take(_data);
	Expects(data != nullptr);

	using Result = VaultPasscodeChangeResult;
	auto vaults = (data->failed || !SameDependents(data->derived))
		? std::nullopt
		: StageDerivedVaults(data->derived);
	if (!vaults) {
		return Result::VaultFailed;
	}
	const auto result = writer(std::move(data->keyData));
	if (result != Storage::SetPasscodeResult::Success) {
		RollbackStagedVaults(*vaults);
		return (result == Storage::SetPasscodeResult::NeedsVerification)
			? Result::NeedsVerification
			: Result::PasscodeFailed;
	}
	return CommitStagedVaults(*vaults)
		? Result::Done
		: Result::CommitFailed;
}

namespace {

// Both legs of a change in one worker job: the proof derived from the current
// bytes and the derivation the write needs - the staged vault batch when
// passcode-wrapped vaults depend on the passcode, a fresh key_data wrap
// otherwise.
struct LocalPasscodeChangeJob {
	std::optional<VaultPasscodeChange> batch;
	std::optional<Storage::PasscodeDerivation> fresh;
	Storage::PasscodeDerivation proof;

	void run() {
		if (batch) {
			batch->run();
		}
		if (fresh) {
			fresh->run();
		}
		proof.run();
	}
};

} // namespace

void ChangeLocalPasscode(
		not_null<QObject*> guard,
		const SecureBytes &current,
		const QByteArray &updated,
		Fn<void(LocalPasscodeChangeResult)> done) {
	using Result = LocalPasscodeChangeResult;
	const auto &local = Core::App().domain().local();
	auto utf8 = Utf8Copy(current);
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
	});
	auto job = LocalPasscodeChangeJob{ .proof = local.prepareOpen(utf8) };
	if (!CollectVaultDependents().passcodeWrapped.empty()) {
		job.batch = VaultPasscodeChange::Prepare(
			local,
			current.copy(),
			updated);
		if (!job.batch) {
			done(Result::VaultFailed);
			return;
		}
	} else {
		job.fresh = local.prepareNewWrap(updated);
	}
	const auto weak = QPointer<QObject>(guard.get());
	Storage::DeriveOnWorker(
		std::move(job),
		crl::guard(guard.get(), [=](LocalPasscodeChangeJob &&job) {
			auto &local = Core::App().domain().local();
			// A vault that became passcode-wrapped during the derivation is in
			// no batch, and a fresh key_data wrap would strand it under the
			// passcode this replaces.
			if (!job.batch
				&& !CollectVaultDependents().passcodeWrapped.empty()) {
				done(Result::VaultFailed);
				return;
			}
			const auto verification = local.verifyPasscode(
				std::move(job.proof));
			if (!verification) {
				done(Result::Stale);
				return;
			}
			const auto write = [&](Storage::PasscodeDerivation keyData) {
				cSetPasscodeBadTries(0);
				const auto result = local.setPasscode(
					std::move(keyData),
					*verification);
				if (result == Storage::SetPasscodeResult::Success) {
					Core::App().localPasscodeChanged();
				}
				return result;
			};
			const auto result = job.batch
				? job.batch->apply(write)
				: [&] {
					switch (write(std::move(*job.fresh))) {
					case Storage::SetPasscodeResult::Success:
						return VaultPasscodeChangeResult::Done;
					case Storage::SetPasscodeResult::NeedsVerification:
						return VaultPasscodeChangeResult::NeedsVerification;
					case Storage::SetPasscodeResult::Failed:
						return VaultPasscodeChangeResult::PasscodeFailed;
					}
					Unexpected("SetPasscodeResult in ChangeLocalPasscode.");
				}();
			if (!weak) {
				return;
			}
			switch (result) {
			case VaultPasscodeChangeResult::Done:
				done(Result::Done);
				return;
			case VaultPasscodeChangeResult::CommitFailed:
				done(Result::CommitFailed);
				return;
			case VaultPasscodeChangeResult::VaultFailed:
				done(Result::VaultFailed);
				return;
			case VaultPasscodeChangeResult::PasscodeFailed:
				done(Result::PasscodeFailed);
				return;
			case VaultPasscodeChangeResult::NeedsVerification:
				LOG(("App Error: key_data refused a passcode verification "
					"minted in the same callback."));
				done(Result::Stale);
				return;
			}
			Unexpected("Result in ChangeLocalPasscode.");
		}));
}

namespace {

// A function-local static, so a provider registered from another translation
// unit's initializer cannot observe an unconstructed vector.
[[nodiscard]] std::vector<std::unique_ptr<ProtectionProvider>> &Providers() {
	static auto result = std::vector<std::unique_ptr<ProtectionProvider>>();
	return result;
}

// task.md asks for availability to be re-evaluated when the box opens, not
// while it is open: a row appearing or vanishing under the user's finger
// would move the selection. The local lifetime unsubscribes before the
// answer is used, so a provider that answers asynchronously reads as
// unavailable, which is the fail-closed verdict.
[[nodiscard]] bool AvailableNow(const ProtectionProvider &provider) {
	auto result = false;
	auto lifetime = rpl::lifetime();
	provider.available(
	) | rpl::take(1) | rpl::on_next([&](bool value) {
		result = value;
	}, lifetime);
	return result;
}

[[nodiscard]] Ui::PasscodeStrengthBand PasscodeBand(
		const SecureBytes &passcode) {
	auto text = QString::fromUtf8(
		reinterpret_cast<const char*>(passcode.span().data()),
		passcode.size());
	const auto cleanse = gsl::finally([&] {
		if (!text.isEmpty()) {
			OPENSSL_cleanse(text.data(), text.size() * sizeof(QChar));
		}
	});
	return Ui::EstimatePasscodeStrength(text).band;
}

[[nodiscard]] QString RemovalWalletNames(
		const std::vector<base::weak_ptr<Main::Account>> &accounts) {
	auto names = QStringList();
	for (const auto &weak : accounts) {
		const auto account = weak.get();
		if (!account) {
			continue;
		} else if (const auto session = account->maybeSession()) {
			names.push_back(session->user()->name());
		}
	}
	return names.join(u", "_q);
}

// Whether the committed wrap on disk is still the one a caller derived
// against, asked again on a worker hop's answer: by generation, kind and
// salt, as StageDerivedVaults compares, because every committed change bumps
// the generation. StageVaultWrap writes the caller's header copy as it is,
// without comparing it to the disk, so a copy that a concurrent transition
// outdated under the hop would clobber that transition. A header that does
// not read answers false as well.
[[nodiscard]] bool WrapStillCommitted(
		Storage::Account &local,
		const VaultWrap &wrap) {
	const auto reading = ReadVaultHeader(local);
	if (reading.state != VaultReading::State::Read) {
		return false;
	}
	const auto committed = reading.header.committedWrap();
	return committed
		&& (committed->generation == wrap.generation)
		&& (committed->kind == wrap.kind)
		&& (committed->salt == wrap.salt);
}

// The non-provider half of AcquireVaultKey as one worker job: a copy of the
// wrap it derives against, the typed bytes as SecureBytes and the vault key
// that wrap seals. It holds values only and touches no store. run() cleanses
// the typed bytes on every exit; the key lives in SecureBytes and dies with
// the job on the main thread, used or not.
struct VaultKeyAcquisition {
	VaultWrap wrap;
	SecureBytes passcode;
	std::optional<SecureBytes> key;

	void run();
};

void VaultKeyAcquisition::run() {
	auto utf8 = Utf8Copy(passcode);
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
		passcode.clear();
	});
	if (const auto wrapKey = DeriveVaultWrapKey(wrap, utf8)) {
		key = UnwrapVaultKey(wrap, *wrapKey);
	}
}

// Opens the vault key that the account's committed wrap seals: Passcode
// derives its wrap key from the typed bytes and Open from the wrap's own
// secret, while a kind at or above kFirstReservedVaultKind belongs to a
// registered provider and is opened by it. The answer arrives through a
// callback for either branch: a provider's hand-off is asynchronous by
// contract, and the derivation runs on a worker. The job copies the typed
// bytes before this returns, so no caller needs them past this frame.
//
// The error says why no key came: the provider branch forwards the
// provider's own answer, with Unavailable for a kind no provider is
// registered for - a header from another platform's tdata - and the worker
// branch answers AuthenticationFailed, a wrap the typed bytes do not open.
// Cancelled is the one a caller tells apart: a dismissed system sheet is not
// a failure to state.
void AcquireVaultKey(
		not_null<Storage::Account*> local,
		const VaultWrap &wrap,
		const SecureBytes &passcode,
		Fn<void(std::optional<SecureBytes>, ProtectionError)> done) {
	if (quint32(wrap.kind) >= kFirstReservedVaultKind) {
		const auto provider = ProtectionProviderFor(wrap.kind);
		if (!provider) {
			done(std::nullopt, ProtectionError::Unavailable);
			return;
		}
		provider->unwrap(local, wrap, [done](ProtectionUnwrapResult result) {
			if (result.error != ProtectionError::None) {
				done(std::nullopt, result.error);
			} else {
				done(std::move(result.key), ProtectionError::None);
			}
		});
		return;
	}
	Storage::DeriveOnWorker(
		VaultKeyAcquisition{ .wrap = wrap, .passcode = passcode.copy() },
		[done](VaultKeyAcquisition &&job) {
			const auto error = job.key
				? ProtectionError::None
				: ProtectionError::AuthenticationFailed;
			done(std::move(job.key), error);
		});
}

// The passcode row's wrap as one worker job: the typed bytes as SecureBytes
// and the fresh wrap with its wrap key. run() cleanses the typed bytes on
// every exit; the wrap key lives in SecureBytes and dies with the job on the
// main thread, applied or not.
struct PasscodeWrapPreparation {
	SecureBytes passcode;
	std::optional<VaultPreparedWrap> prepared;

	void run();
};

void PasscodeWrapPreparation::run() {
	auto utf8 = Utf8Copy(passcode);
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
		passcode.clear();
	});
	prepared = PrepareVaultPasscodeWrap(utf8);
}

// The passcode row's wrap, derived on the worker. The job takes its copy of
// the typed bytes before this returns, so a caller may pass bytes it does
// not own past this frame, and cleanses that copy on the worker: the only
// lasting copy of the typed bytes stays the box's.
//
// The wrap is worth sealing only while key_data still holds the passcode
// these bytes are. A change that lands before the wrap is armed - while the
// chooser is open, during this derivation, or while the confirmation of an
// unusable vault's reset holds the wrap - is refused by the chooser's
// passcodeChanged latch, asked on this call's answer and again on that
// confirmation. One that lands after the arm reaches VaultRuntime's
// synchronous clear() on localPasscodeChanged(), which drops the armed
// policy and moves the epoch the seal compares.
void PreparePasscodeWrap(
		const SecureBytes &passcode,
		Fn<void(std::optional<VaultPreparedWrap>)> done) {
	Storage::DeriveOnWorker(
		PasscodeWrapPreparation{ .passcode = passcode.copy() },
		[done](PasscodeWrapPreparation &&job) {
			done(std::move(job.prepared));
		});
}

// One removal's whole walk, behind a shared_ptr because the key material in
// it is move-only and every continuation below has to fit in a copyable Fn.
// Both account lists are weak: Main::Domain owns the accounts and one can be
// logged out and dropped while the box that started the walk is open, or
// after the walk has already moved its vault.
//
// Every listed vault is transitioned onto a copy of the one prepared wrap
// rather than onto a freshly prepared one of its own: VaultPreparedWrap is
// move-only, and preparing one per account would put PrepareVaultOpenWrap()
// behind call sites the warning box does not guard.
//
// alive() is the walk's owner probe. The walk outlives the box that started
// it by contract, but it may not keep writing once that box has answered:
// the box stays dismissible while an account's key is derived on a worker,
// and Cancel, Escape or a layer teardown reports the default cancellation to
// the caller there and then. Every account not transitioned by that moment
// has to keep its passcode wrap, so the loop and every acquisition ask the
// probe and stop through FinishVaultRemoval() - the accounts already moved
// stay moved, and the answer it gives is dropped by the guarded done or
// lands on a box that has already reported.
struct VaultRemovalWalk {
	std::vector<base::weak_ptr<Main::Account>> accounts;
	std::vector<base::weak_ptr<Main::Account>> changed;
	SecureBytes passcode;
	VaultPreparedWrap prepared;
	VaultHeader header;
	VaultKind kind = VaultKind::Passcode;
	Fn<bool()> alive;
	Fn<void(KeyProtectionResult)> done;
	int index = 0;
};

// The one answer the walk gives, after the last account or at the first
// account it could not finish. Each vault key went out of scope with its own
// transition; the typed passcode and the prepared wrap key go here, before
// the caller hears anything.
void FinishVaultRemoval(
		const std::shared_ptr<VaultRemovalWalk> &walk,
		bool failed) {
	auto result = KeyProtectionResult{
		.cancelled = false,
		.failed = failed,
		.kind = walk->kind,
		.changed = std::move(walk->changed),
	};
	walk->passcode.clear();
	walk->prepared.wrapKey.clear();
	walk->done(std::move(result));
}

// One account at a time, with account N + 1 chained from account N's
// completion: acquiring a vault key is asynchronous by contract, so the list
// cannot be walked in a loop. The loop here skips accounts that are gone and
// stops before acquiring one once the owner probe answers no.
void WalkVaultRemoval(std::shared_ptr<VaultRemovalWalk> walk) {
	Expects(walk != nullptr);
	Expects(walk->alive != nullptr);

	while (walk->index < int(walk->accounts.size())) {
		if (!walk->alive()) {
			FinishVaultRemoval(walk, true);
			return;
		}
		const auto weak = walk->accounts[walk->index];
		++walk->index;
		const auto account = weak.get();
		if (!account) {
			// Logged out and dropped while the box was open: Main::Domain
			// no longer owns it and there is no vault left to transition.
			continue;
		}
		auto reading = ReadVaultHeader(account->local());
		if (reading.state != VaultReading::State::Read) {
			FinishVaultRemoval(walk, true);
			return;
		}
		walk->header = std::move(reading.header);
		const auto committed = walk->header.committedWrap();
		Assert(committed != nullptr);
		const auto retired = *committed;
		const auto acquired = [=](
				std::optional<SecureBytes> key,
				ProtectionError) {
			const auto live = weak.get();
			if (!live) {
				WalkVaultRemoval(walk);
				return;
			} else if (!walk->alive()) {
				FinishVaultRemoval(walk, true);
				return;
			}
			const auto session = live->maybeSession();
			if (!key
				|| !WrapStillCommitted(live->local(), retired)
				|| (session && session->wallet().custodyBusy())) {
				FinishVaultRemoval(walk, true);
				return;
			}
			const auto result = TransitionVaultWrap(
				live->local(),
				walk->header,
				*key,
				VaultPreparedWrap{
					.wrap = walk->prepared.wrap,
					.wrapKey = walk->prepared.wrapKey.copy(),
				},
				session ? &session->wallet().vault() : nullptr);
			key.reset();
			if (result != VaultTransitionResult::Done) {
				FinishVaultRemoval(walk, true);
				return;
			}
			walk->changed.push_back(weak);
			if (session) {
				session->wallet().notifyKeyProtectionChanged();
			}
			WalkVaultRemoval(walk);
		};
		AcquireVaultKey(
			&account->local(),
			*committed,
			walk->passcode,
			acquired);
		return;
	}
	FinishVaultRemoval(walk, false);
}

// What the wallet-only passcode create box answers with. An empty passcode
// is the box closed without creating anything.
struct WalletPasscodeCreated {
	SecureBytes passcode;
};

// What the passcode change box answers with. An empty passcode is the box
// closed without a change; stale says the bytes it was opened with no longer
// open key_data, or that a vault still opens with them, so the chooser that
// holds those bytes has to close as well.
struct WalletPasscodeChanged {
	SecureBytes passcode;
	bool stale = false;
};

// The fields both passcode boxes share: the passcode with its strength meter,
// its confirmation and the error line under them. validate() answers the
// typed passcode, or nothing once it has shown why it cannot be used. Exactly
// two refusals, an empty passcode and a mismatch: no length rule and no band
// gates these boxes, the meter only advises.
struct PasscodeFields {
	not_null<Ui::PasswordInput*> first;
	not_null<Ui::PasswordInput*> second;
	Fn<void(const QString &)> showError;
	Fn<std::optional<QString>()> validate;
};

[[nodiscard]] PasscodeFields AddPasscodeFields(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> enter,
		rpl::producer<QString> confirm) {
	const auto &fieldSt = st::settingLocalPasscodeInputField;
	const auto addField = [&](rpl::producer<QString> placeholder) {
		const auto wrap = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::walletProtectionCreateFieldMargin);
		wrap->resize(wrap->width(), fieldSt.heightMin);
		const auto field = Ui::CreateChild<Ui::PasswordInput>(
			wrap,
			fieldSt,
			std::move(placeholder));
		wrap->widthValue(
		) | rpl::on_next([=](int width) {
			field->moveToLeft((width - field->width()) / 2, 0);
		}, wrap->lifetime());
		return not_null(field);
	};
	const auto first = addField(std::move(enter));
	const auto meter = [&] {
		auto object = object_ptr<Ui::PasscodeStrengthMeter>(
			box,
			st::defaultPasscodeStrengthMeter);
		object->setNaturalWidth(fieldSt.width);
		return box->addRow(
			std::move(object),
			st::walletProtectionMeterMargin,
			style::al_top);
	}();
	const auto second = addField(std::move(confirm));
	const auto error = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			QString(),
			st::settingLocalPasscodeError),
		st::walletPasscodeErrorMargin,
		style::al_top);
	error->hide();
	const auto showError = [=](const QString &text) {
		error->show();
		error->setText(text);
	};
	QObject::connect(first, &Ui::MaskedInputField::changed, [=] {
		meter->showCandidate(first->text());
		error->hide();
	});
	QObject::connect(second, &Ui::MaskedInputField::changed, [=] {
		error->hide();
	});
	box->setFocusCallback([=] {
		first->setFocusFast();
	});
	const auto validate = [=]() -> std::optional<QString> {
		const auto typed = first->text();
		if (typed.isEmpty()) {
			first->setFocus();
			first->showError();
			return std::nullopt;
		} else if (typed != second->text()) {
			second->setFocus();
			second->showError();
			second->selectAll();
			showError(tr::lng_passcode_differ(tr::now));
			return std::nullopt;
		}
		return typed;
	};
	return {
		.first = first,
		.second = second,
		.showError = showError,
		.validate = validate,
	};
}

// Enter in the first field moves to the confirmation, which saves.
void SubmitPasscodeFields(const PasscodeFields &fields, Fn<void()> save) {
	const auto first = fields.first;
	const auto second = fields.second;
	const auto submit = [=] {
		if (second->hasFocus() || first->text().isEmpty()) {
			save();
		} else {
			second->setFocus();
		}
	};
	QObject::connect(first, &Ui::MaskedInputField::submitted, submit);
	QObject::connect(second, &Ui::MaskedInputField::submitted, submit);
}

// The passcode this box creates is created straight in the wallet-only role,
// through Storage::Domain::createPasscodeWithoutAppLock(): the app lock never
// turns on, and neither the auto-lock nor the system unlock setting is
// touched.
void WalletPasscodeCreateBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(WalletPasscodeCreated)> done) {
	struct State {
		WalletPasscodeCreated result;
		SecureBytes typed;
		QPointer<Ui::RoundButton> save;
		bool finished = false;
		bool busy = false;
		bool reported = false;
	};
	const auto state = box->lifetime().make_state<State>();

	box->setTitle(tr::lng_wallet_protection_create_title());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_protection_create_about(),
			st::boxLabel),
		st::walletProtectionIntroMargin);

	const auto fields = AddPasscodeFields(
		box,
		tr::lng_wallet_protection_create_enter(),
		tr::lng_wallet_protection_create_confirm());
	const auto first = fields.first;
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		first->setDisabled(busy);
		fields.second->setDisabled(busy);
		Ui::SetButtonBusy(state->save.data(), busy);
		if (!busy) {
			first->setFocus();
		}
	};

	const auto save = [=] {
		if (state->finished || state->busy) {
			return;
		}
		const auto typed = fields.validate();
		if (!typed) {
			return;
		}
		auto utf8 = typed->toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!utf8.isEmpty()) {
				OPENSSL_cleanse(utf8.data(), utf8.size());
			}
		});
		const auto &local = show->session().domain().local();
		state->typed = SecureBytes(utf8);
		cSetPasscodeBadTries(0);
		setBusy(true);
		// One derivation on the worker, the new passcode's key_data wrap, and
		// one logical write in the wallet-only role:
		// createPasscodeWithoutAppLock() keeps the verified open wrap, so the
		// launch lock never turns on and nothing is left half-created. That
		// entry checks again at the write itself that no passcode exists
		// yet: a passcode another window created while the derivation ran
		// is refused with NeedsVerification, and this box closes with nothing
		// created. That window's write has already set the chooser's
		// passcodeChanged latch, which turns the empty answer into a close.
		Storage::DeriveOnWorker(
			local.prepareNewWrap(utf8),
			crl::guard(box, [=](Storage::PasscodeDerivation &&derived) {
				auto &local = show->session().domain().local();
				const auto set = local.createPasscodeWithoutAppLock(
					std::move(derived));
				if (set == Storage::SetPasscodeResult::NeedsVerification) {
					state->typed.clear();
					box->closeBox();
					return;
				} else if (set != Storage::SetPasscodeResult::Success) {
					setBusy(false);
					state->typed.clear();
					first->setFocus();
					first->showError();
					fields.showError(Lang::Hard::SecureSaveError());
					return;
				}
				state->finished = true;
				Core::App().localPasscodeChanged();
				state->result = { .passcode = std::move(state->typed) };
				box->closeBox();
			}));
	};
	SubmitPasscodeFields(fields, save);
	state->save = box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	// One answer on the way out, whatever closed the box: the created
	// passcode or nothing at all.
	box->boxClosing() | rpl::on_next([=] {
		if (state->reported) {
			return;
		}
		state->reported = true;
		if (done) {
			done(std::move(state->result));
		}
	}, box->lifetime());
}

// Changes the passcode the chooser was opened with. current are the bytes the
// chooser's gate accepted; a change made elsewhere makes them stale, which an
// idle box answers by closing and a busy one learns from its own proof.
void WalletPasscodeChangeBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		SecureBytes current,
		Fn<void(WalletPasscodeChanged)> done) {
	struct State {
		SecureBytes current;
		SecureBytes typed;
		WalletPasscodeChanged result;
		QPointer<Ui::RoundButton> save;
		bool busy = false;
		bool reported = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->current = std::move(current);

	box->setTitle(tr::lng_wallet_protection_change_title());
	box->addSkip(st::walletProtectionRowSkip);
	const auto fields = AddPasscodeFields(
		box,
		tr::lng_passcode_enter_new(),
		tr::lng_passcode_confirm_new());
	const auto first = fields.first;
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		first->setDisabled(busy);
		fields.second->setDisabled(busy);
		Ui::SetButtonBusy(state->save.data(), busy);
		if (!busy) {
			first->setFocus();
		}
	};
	// The box's own write fires this too, while busy.
	show->session().domain().local().localPasscodeChanged(
	) | rpl::filter([=] {
		return !state->busy;
	}) | rpl::on_next([=] {
		state->result.stale = true;
		crl::on_main(box, [=] { box->closeBox(); });
	}, box->lifetime());

	const auto save = [=] {
		if (state->busy) {
			return;
		}
		const auto typed = fields.validate();
		if (!typed) {
			return;
		}
		auto utf8 = typed->toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!utf8.isEmpty()) {
				OPENSSL_cleanse(utf8.data(), utf8.size());
			}
		});
		if (!bytes::compare(state->current.span(), bytes::make_span(utf8))) {
			first->setFocus();
			first->showError();
			first->selectAll();
			fields.showError(tr::lng_passcode_is_same(tr::now));
			return;
		}
		state->typed = SecureBytes(utf8);
		setBusy(true);
		using Result = LocalPasscodeChangeResult;
		ChangeLocalPasscode(box, state->current, utf8, [=](Result result) {
			switch (result) {
			case Result::Done:
				state->result = { .passcode = std::move(state->typed) };
				box->closeBox();
				return;
			case Result::CommitFailed:
				show->showToast(tr::lng_wallet_protection_error(tr::now));
				[[fallthrough]];
			case Result::Stale:
				state->typed.clear();
				state->result.stale = true;
				box->closeBox();
				return;
			case Result::VaultFailed:
			case Result::PasscodeFailed:
				state->typed.clear();
				setBusy(false);
				first->showError();
				fields.showError((result == Result::VaultFailed)
					? tr::lng_wallet_protection_error(tr::now)
					: Lang::Hard::SecureSaveError());
				return;
			}
			Unexpected("Result in WalletPasscodeChangeBox.");
		});
	};
	SubmitPasscodeFields(fields, save);
	state->save = box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->boxClosing() | rpl::on_next([=] {
		if (state->reported) {
			return;
		}
		state->reported = true;
		state->current.clear();
		if (done) {
			done(std::move(state->result));
		}
	}, box->lifetime());
}

// The commit clears the runtime, because the key it held is the one the
// switch retired, and the fresh vault key never leaves the transition. The
// new committed wrap in the caller header seals that key under the wrap key
// the chooser prepared, so a copy of that wrap key opens it again here and
// the store that asked for this switch gets its grant without a second ask.
[[nodiscard]] VaultGrant GrantAfterSwitch(
		const VaultHeader &header,
		const SecureBytes &wrapKey,
		VaultRuntime &vault) {
	const auto committed = header.committedWrap();
	if (!committed) {
		return VaultGrant();
	}
	const auto epoch = vault.clearEpoch();
	auto key = UnwrapVaultKey(*committed, wrapKey);
	if (!key || !vault.unlockWith(std::move(*key), epoch)) {
		return VaultGrant();
	}
	return vault.grant();
}

// header carries the account's committed vault header in Switch and is empty
// in Install (there is no vault yet) and in Removal (there is one per listed
// account). passcode is taken by value and moved into the box's own state,
// so the box owns the only live copy of the typed bytes and they are
// cleansed with it on every close path.
void KeyProtectionBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		KeyProtectionArgs args,
		std::optional<VaultHeader> header,
		SecureBytes passcode) {
	// busy is the re-entry guard: a provider call, a sub-box and a worker
	// derivation are all asynchronous, so a second Save while one is in
	// flight would start a second write of the same vault. header is the
	// box's own copy of the Switch header, which TransitionVaultWrap rewrites
	// in place and which must therefore outlive an asynchronous key
	// acquisition. walk is the removal walk this box started, kept because
	// the walk outlives the box by contract while its own answer is dropped
	// by the guarded done on exactly the dismissal that matters, so the
	// transitions it had already completed are readable only from here, at
	// the one moment boxClosing() reports. This field is the walk's first
	// owner and the only one left once the walk has answered, but the
	// chained continuations own it for as long as it can still transition,
	// so it outlives nothing the walk does not, and it closes no cycle back
	// to the box, so it dies with the box. It exposes nothing new either:
	// the typed bytes in it are the copy this box already owns in passcode,
	// FinishVaultRemoval() cleanses those and the prepared wrap key before
	// it answers, and what stays reachable is the Open wrap's own secret -
	// key material, but the same bytes every account the walk moved now
	// carries in its own committed vault header.
	struct State {
		SecureBytes passcode;
		std::optional<VaultHeader> header;
		KeyProtectionResult result;
		std::vector<not_null<Ui::Radioenum<VaultKind>*>> radios;
		QPointer<Ui::RoundButton> save;
		std::shared_ptr<VaultRemovalWalk> walk;
		std::optional<VaultPreparedWrap> pendingInstall;
		bool busy = false;
		bool passcodeChanged = false;
		bool reported = false;
	};
	const auto state = box->lifetime().make_state<State>();
	// State lives in the box's lifetime, so every continuation a provider
	// answers on - each of them asynchronous by contract - is guarded with the
	// box. A layer teardown while a system prompt is up (setupPasscodeLock()
	// hides every layer instantly) would otherwise leave them dereferencing
	// freed memory.
	const auto weak = base::make_weak(box);
	state->passcode = std::move(passcode);
	state->header = std::move(header);
	// A save that finished stashes its verdict and closes; boxClosing() is
	// the one place that answers the caller, so every dismissal - Cancel,
	// Escape, the layer being replaced - still reports the default
	// { .cancelled = true } and the caller persists nothing.
	const auto closeWith = [=](KeyProtectionResult result) {
		state->result = std::move(result);
		box->closeBox();
	};
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		for (const auto &radio : state->radios) {
			radio->setDisabled(busy);
		}
		Ui::SetButtonBusy(state->save.data(), busy);
		if (!busy && state->passcodeChanged) {
			crl::on_main(box, [=] { box->closeBox(); });
		}
	};
	// Nothing was written, so the box stays open on the user's own choice.
	const auto refuse = [=] {
		setBusy(false);
		show->showToast(tr::lng_wallet_protection_error(tr::now));
	};
	// A write was attempted and could not be finished: the box closes and the
	// caller hears failed, so nothing downstream assumes the new kind.
	const auto fail = [=] {
		show->showToast(tr::lng_wallet_protection_error(tr::now));
		closeWith({ .cancelled = false, .failed = true });
	};
	// The gate's bytes stop being the app passcode once another window
	// changes or removes it, and a Passcode row saved with them would wrap
	// this vault under a passcode the app no longer asks for. An idle box
	// just closes. A busy one may be waiting on a sub-box, a system prompt
	// or a worker derivation while another window writes, so the change is
	// remembered instead: the box closes as soon as it is idle again, and a
	// Passcode wrap is applied only while nothing changed. The create step
	// clears it, because its own write installs the bytes it hands back.
	// The close is deferred out of the writer.
	show->session().domain().local().localPasscodeChanged(
	) | rpl::on_next([=] {
		state->passcodeChanged = true;
		if (!state->busy) {
			crl::on_main(box, [=] { box->closeBox(); });
		}
	}, box->lifetime());
	const auto mode = args.mode;
	const auto grantForStore = args.grantForStore;
	const auto resetUnusableVault = args.resetUnusableVault;
	const auto removal = (mode == KeyProtectionMode::Removal);

	box->setTitle(tr::lng_wallet_protection_title());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			(removal
				? tr::lng_wallet_protection_removal_about
				: tr::lng_wallet_protection_about)(),
			st::boxLabel),
		st::walletProtectionIntroMargin);
	if (removal && args.accounts.size() > 1) {
		auto names = RemovalWalletNames(args.accounts);
		if (!names.isEmpty()) {
			box->addRow(
				object_ptr<Ui::FlatLabel>(
					box,
					tr::lng_wallet_protection_removal_wallets(
						lt_accounts,
						rpl::single(std::move(names))),
					st::walletProtectionAboutLabel),
				st::walletProtectionIntroMargin);
		}
	}

	auto hardware = std::vector<not_null<ProtectionProvider*>>();
	for (const auto &provider : ProtectionProviders()) {
		if (AvailableNow(*provider)) {
			hardware.push_back(provider.get());
		}
	}
	const auto preselected = [&] {
		switch (mode) {
		case KeyProtectionMode::Install:
			return hardware.empty()
				? VaultKind::Passcode
				: hardware.front()->kind();
		case KeyProtectionMode::Switch: {
			const auto committed = state->header
				? state->header->committedWrap()
				: nullptr;
			return committed ? committed->kind : VaultKind::Passcode;
		}
		case KeyProtectionMode::Removal:
			return VaultKind::Passcode;
		}
		Unexpected("Mode in KeyProtectionBox.");
	}();
	const auto group = std::make_shared<Ui::RadioenumGroup<VaultKind>>(
		preselected);

	const auto addRow = [&](
			VaultKind kind,
			rpl::producer<QString> title,
			rpl::producer<QString> about,
			const style::Checkbox &checkboxSt) {
		box->addSkip(st::walletProtectionRowSkip);
		const auto row = box->addRow(object_ptr<Ui::VerticalLayout>(box));
		const auto radio = row->add(object_ptr<Ui::Radioenum<VaultKind>>(
			box,
			group,
			kind,
			QString(),
			checkboxSt));
		std::move(title) | rpl::on_next([=](const QString &text) {
			radio->setText(text);
		}, radio->lifetime());
		state->radios.push_back(radio);
		row->add(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(about),
				st::walletProtectionAboutLabel),
			st::walletProtectionAboutMargin);
		return row;
	};
	// The click helper covers the whole row, so it is created after the row's
	// last label: it must stay on top of them to receive their clicks.
	const auto makeClickable = [&](
			not_null<Ui::VerticalLayout*> row,
			VaultKind kind) {
		const auto button = Ui::CreateChild<Ui::AbstractButton>(row.get());
		row->sizeValue(
		) | rpl::on_next([=](QSize size) {
			button->resize(size);
		}, button->lifetime());
		button->setClickedCallback([=] {
			if (!state->busy) {
				group->setValue(kind);
			}
		});
	};

	for (const auto &provider : hardware) {
		const auto kind = provider->kind();
		const auto row = addRow(
			kind,
			provider->title(),
			provider->description(),
			st::defaultBoxCheckbox);
		makeClickable(row, kind);
	}

	const auto passcodeRow = addRow(
		VaultKind::Passcode,
		(removal
			? tr::lng_wallet_protection_passcode_keep
			: tr::lng_wallet_protection_passcode)(),
		(removal
			? tr::lng_wallet_protection_passcode_keep_about
			: tr::lng_wallet_protection_passcode_about)(),
		st::defaultBoxCheckbox);
	// The band lives in the box's lifetime: nothing derived from the typed
	// passcode reaches a member of anything that outlives the box. It moves
	// when the passcode is changed from here.
	using Band = Ui::PasscodeStrengthBand;
	const auto band = state->passcode.empty()
		? nullptr
		: box->lifetime().make_state<rpl::variable<Band>>(
			PasscodeBand(state->passcode));
	if (band) {
		const auto strength = passcodeRow->add(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_wallet_protection_passcode_current(
					lt_band,
					band->value(
					) | rpl::map(
						Ui::PasscodeStrengthBandName
					) | rpl::flatten_latest()),
				st::walletProtectionAboutLabel),
			st::walletProtectionStrengthMargin);
		rpl::combine(
			band->value(),
			rpl::single(rpl::empty) | rpl::then(style::PaletteChanged())
		) | rpl::on_next([=](Band value, rpl::empty_value) {
			strength->setTextColorOverride(
				Ui::PasscodeStrengthBandColor(value)->c);
		}, strength->lifetime());
	}
	makeClickable(passcodeRow, VaultKind::Passcode);
	// Only the chooser opened for the vault as it is offers the change, and
	// only while its passcode row is chosen, because the change saves that
	// choice: an install or a store waiting on this box holds the vault's
	// custody, and a removal is about to drop the passcode. The link goes after
	// the row's click helper, so it stays above it, and slides with the choice.
	const auto changeLink = (band
		&& mode == KeyProtectionMode::Switch
		&& !grantForStore)
		? passcodeRow->add(object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			passcodeRow,
			object_ptr<Ui::FlatLabel>(
				passcodeRow,
				tr::lng_wallet_protection_passcode_change(
					lt_arrow,
					rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
					tr::link),
				st::walletProtectionAboutLabel),
			st::walletProtectionChangeMargin))
		: nullptr;
	if (changeLink) {
		changeLink->toggleOn(group->value() | rpl::map([](VaultKind kind) {
			return (kind == VaultKind::Passcode);
		}));
		changeLink->finishAnimating();
	}

	const auto openRow = addRow(
		VaultKind::Open,
		tr::lng_wallet_protection_open(),
		tr::lng_wallet_protection_open_about(),
		st::walletProtectionAttentionCheckbox);
	makeClickable(openRow, VaultKind::Open);

	// Weak by contract, and for the same reason the walk keeps them so:
	// Main::Domain owns the accounts and one can be logged out and dropped
	// between this box opening and its save finishing.
	const auto accounts = args.accounts;

	const auto discardPendingInstall = [=, local = &show->session().local()] {
		const auto prepared = base::take(state->pendingInstall);
		const auto provider = prepared
			? ProtectionProviderFor(prepared->wrap.kind)
			: nullptr;
		if (provider) {
			provider->remove(local, prepared->wrap, [](ProtectionError) {});
		}
	};

	// Where the three save paths converge: Install arms the prepared wrap as
	// the account's creation policy, Switch transitions this account's vault
	// onto it and Removal walks the caller's list of them.
	const auto apply = [=](VaultKind kind, VaultPreparedWrap prepared) {
		auto &session = show->session();
		switch (mode) {
		case KeyProtectionMode::Install: {
			if (!resetUnusableVault) {
				// Nothing reaches the disk here: the caller's store writes
				// the header when it seals its first record under this
				// policy.
				auto &vault = session.wallet().vault();
				vault.arm(std::move(prepared));
				closeWith({
					.cancelled = false,
					.kind = kind,
					.grant = vault.grant(),
				});
				return;
			}
			// The account still carries a vault this process cannot open, so
			// the prepared wrap waits here until the user confirms deleting
			// the stored key, and only that confirmation resets the vault -
			// before arm(), because the reset clears the runtime and
			// VaultRuntime::clear() drops an armed policy. Cancel closes the
			// chooser instead of returning to it: a passcode the create box
			// has just made went to withPasscode() without reaching
			// state->passcode, so a second Save would try to create one over
			// it. A Passcode wrap is not armed once another window changed
			// the passcode while the confirmation waited, as withPasscode()
			// refuses before it gets here. Every exit that arms nothing - a
			// cancel, that change, a refused or failed reset, a box that has
			// already answered or is torn down under the confirmation - gives
			// the prepared wrap back to its provider, as the Switch arm does.
			state->pendingInstall = std::move(prepared);
			if (state->reported) {
				discardPendingInstall();
				return;
			}
			show->showBox(Ui::MakeConfirmBox({
				.text = tr::lng_wallet_vault_reset_about(tr::now),
				.confirmed = crl::guard(weak, [=](Fn<void()> close) {
					close();
					if (!state->pendingInstall) {
						return;
					}
					const auto stale = (kind == VaultKind::Passcode)
						&& state->passcodeChanged;
					const auto reset = !stale && resetUnusableVault();
					// A reset refused for a comment scope that went stale
					// cancels that scope, which can destroy this box right
					// here; its boxClosing() has then given the prepared wrap
					// back and answered the caller, so nothing is left to do.
					if (!weak) {
						return;
					} else if (!reset) {
						discardPendingInstall();
						box->closeBox();
						return;
					}
					auto next = base::take(state->pendingInstall);
					auto &vault = show->session().wallet().vault();
					vault.arm(std::move(*next));
					closeWith({
						.cancelled = false,
						.kind = kind,
						.grant = vault.grant(),
					});
				}),
				.cancelled = crl::guard(weak, [=](Fn<void()> close) {
					close();
					discardPendingInstall();
					box->closeBox();
				}),
				.confirmText = tr::lng_wallet_vault_reset_confirm(),
				.confirmStyle = &st::attentionBoxButton,
				.title = tr::lng_wallet_vault_reset_title(),
			}));
		} return;
		case KeyProtectionMode::Switch: {
			Assert(state->header.has_value());
			const auto committed = state->header->committedWrap();
			Assert(committed != nullptr);
			const auto retired = *committed;
			const auto next = std::make_shared<VaultPreparedWrap>(
				std::move(prepared));
			const auto local = &session.local();
			const auto wallet = &session.wallet();
			// The enrolled wrap goes back to its provider on every exit that
			// does not commit it: a hardware enroll has already created a
			// credential for it that no header will ever name otherwise, and
			// the next Save enrolls a fresh one. Passcode and Open have no
			// provider, so for them this is nothing. The transition below
			// spends only the wrap key and keeps the wrap for this.
			const auto discardPrepared = [=] {
				const auto provider = ProtectionProviderFor(next->wrap.kind);
				if (provider) {
					provider->remove(local, next->wrap, [](ProtectionError) {});
				}
			};
			const auto acquired = [=](
					std::optional<SecureBytes> key,
					ProtectionError error) {
				if (error == ProtectionError::Cancelled) {
					// A dismissed system sheet on the retiring factor changes
					// nothing and leaves the user in the box on the row they
					// picked, as a dismissed enroll does below.
					discardPrepared();
					setBusy(false);
					return;
				} else if (!key || (!grantForStore && wallet->custodyBusy())) {
					discardPrepared();
					refuse();
					return;
				} else if (!WrapStillCommitted(*local, retired)
					|| (next->wrap.kind == VaultKind::Passcode
						&& !Core::App().domain().local().hasPasscode())) {
					// The vault moved under the hop, so the header this box
					// holds is stale and a retry against it could only
					// mismatch again: nothing is written and the box closes.
					// The passcode row's wrap is likewise only meaningful while
					// key_data holds that passcode: another account's
					// protection change or a logout can have reconciled it
					// away during the hop, and wrapping this vault under it
					// would leave one only the forgot path can free. Both are
					// asked here, on the hop's answer.
					discardPrepared();
					fail();
					return;
				} else if (kind == VaultKind::Passcode
					&& state->passcodeChanged) {
					box->closeBox();
					return;
				}
				const auto grantKey = grantForStore
					? std::make_shared<SecureBytes>(next->wrapKey.copy())
					: std::shared_ptr<SecureBytes>();
				const auto result = TransitionVaultWrap(
					*local,
					*state->header,
					*key,
					VaultPreparedWrap{
						.wrap = next->wrap,
						.wrapKey = std::move(next->wrapKey),
					},
					&wallet->vault());
				key.reset();
				if (result != VaultTransitionResult::Done) {
					// Failure leaves the old wrap committed. Staging rolls back
					// records and header best-effort; a failed commit can retain
					// both wraps in the caller header and on disk. Reconciliation
					// strips records and wraps outside the committed generation.
					discardPrepared();
					fail();
					return;
				}
				wallet->notifyKeyProtectionChanged();
				// The retiring provider is told only once the new wrap is
				// committed, so no failure above can retire the wrap that
				// is still the one opening this vault. Only a hardware kind
				// has a provider: the registry refuses Passcode and Open,
				// the kinds opened without one.
				const auto old = ProtectionProviderFor(retired.kind);
				if (old) {
					old->remove(local, retired, [](ProtectionError) {});
				}
				auto outcome = KeyProtectionResult{
					.cancelled = false,
					.kind = kind,
				};
				if (grantKey) {
					outcome.grant = GrantAfterSwitch(
						*state->header,
						*grantKey,
						wallet->vault());
				}
				closeWith(std::move(outcome));
			};
			AcquireVaultKey(
				local,
				retired,
				state->passcode,
				crl::guard(weak, acquired));
		} return;
		case KeyProtectionMode::Removal:
			// The walk copies the typed bytes because it outlives this
			// frame by contract; it cleanses that copy before it answers.
			// Its owner probe answers no as soon as this box has answered
			// the caller - boxClosing() reports the default cancellation on
			// every dismissal, and the box itself goes with the layer - so a
			// walk that outlives that answer stops instead of writing under
			// it. state is read only while the box is alive to hold it.
			state->walk = std::make_shared<VaultRemovalWalk>(
				VaultRemovalWalk{
					.accounts = accounts,
					.passcode = state->passcode.copy(),
					.prepared = std::move(prepared),
					.kind = kind,
					.alive = [=] { return weak && !state->reported; },
					.done = crl::guard(weak, closeWith),
				});
			WalkVaultRemoval(state->walk);
			return;
		}
		Unexpected("Mode in KeyProtectionBox.");
	};
	// The bytes the passcode row protects the vault with: the ones typed at
	// the gate, or the ones the wallet-only create box has just made. Both
	// helpers below take their own copy of bytes before returning, so
	// created may pass the local it was handed.
	const auto withPasscode = [=](const SecureBytes &bytes) {
		if (removal) {
			// Removal's row keeps the passcode the caller came to drop, in
			// the wallet-only role: the launch prompt goes off, no vault is
			// rewritten at all and every dependent wrap keeps opening. The
			// proof that write asks for is minted on the worker from the
			// bytes the gate accepted and spent as soon as it answers.
			MintVerificationOnWorker(box, bytes, [=](
					std::optional<Storage::PasscodeVerification> verification) {
				auto &local = show->session().domain().local();
				if (!verification
					|| (local.setAppLockEnabled(false, *verification)
						!= Storage::SetPasscodeResult::Success)) {
					closeWith({ .cancelled = false, .failed = true });
					return;
				}
				Core::App().localPasscodeChanged();
				closeWith({ .cancelled = false, .kind = VaultKind::Passcode });
			});
			return;
		}
		PreparePasscodeWrap(bytes, crl::guard(weak, [=](
				std::optional<VaultPreparedWrap> prepared) {
			if (state->passcodeChanged) {
				box->closeBox();
				return;
			} else if (!prepared) {
				refuse();
				return;
			}
			apply(VaultKind::Passcode, std::move(*prepared));
		}));
	};
	const auto savePasscode = [=] {
		if (!state->passcode.empty()) {
			withPasscode(state->passcode);
			return;
		}
		const auto created = [=](WalletPasscodeCreated result) {
			if (result.passcode.empty()) {
				setBusy(false);
			} else {
				state->passcodeChanged = false;
				withPasscode(result.passcode);
			}
		};
		// The create box answers from its own boxClosing(), which a layer
		// teardown can fire after this box is already gone, and every branch
		// above reads state, which lives in this box's lifetime. A dropped
		// answer undoes nothing: the create box has already installed the
		// passcode in the wallet-only role, and this box is what would have
		// written a vault under it - so what stays is a wallet-only passcode
		// over a vault still on the kind it had, which the next
		// DropUnusedPasscode() site removes unless something has come to
		// depend on it.
		show->showBox(Box(
			WalletPasscodeCreateBox,
			show,
			crl::guard(weak, created)));
	};
	// The product's only call site of PrepareVaultOpenWrap(): this box is the
	// one surface that can put a vault under VaultKind::Open, and it prepares
	// that wrap only past the confirmation. Cancel returns to the chooser
	// with nothing prepared and nothing written.
	//
	// The warning uses verified protection after this operation. Removal
	// drops the passcode, so its wording describes no launch lock.
	const auto appLockAfter = !removal
		&& show->session().domain().local().appLockEnabled();
	const auto saveOpen = [=] {
		show->showBox(Ui::MakeConfirmBox({
			.text = (appLockAfter
				? tr::lng_wallet_protection_open_warning_lock
				: tr::lng_wallet_protection_open_warning_nolock)(tr::now),
			.confirmed = [=](Fn<void()> close) {
				close();
				auto prepared = PrepareVaultOpenWrap();
				if (!prepared) {
					refuse();
					return;
				}
				apply(VaultKind::Open, std::move(*prepared));
			},
			.cancelled = [=](Fn<void()> close) {
				setBusy(false);
				close();
			},
			.confirmText = tr::lng_wallet_protection_open_confirm(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_wallet_protection_open_title(),
		}));
	};
	// The registered provider enrolls a wrap, and that wrap is what the outcome
	// above writes. No vault key is handed in: only the enrolled wrap key is
	// contractual, because every consumer re-seals the vault key under it and
	// overwrites the wrap's own blob. A row reaches here only for a provider
	// that reported available when the box opened - on macOS the Touch ID one
	// Platform::start() registers; the reader rule a new provider's kind must
	// follow is with RegisterProtectionProvider in the header.
	const auto saveHardware = [=](VaultKind kind) {
		const auto provider = ProtectionProviderFor(kind);
		if (!provider) {
			refuse();
			return;
		}
		const auto local = &show->session().local();
		const auto enrolled = [=](ProtectionEnrollResult result) {
			if (result.error == ProtectionError::Cancelled) {
				// A dismissed system prompt changes nothing and leaves the
				// user in the box on the row they picked.
				setBusy(false);
			} else if (result.error != ProtectionError::None
				|| !result.wrap) {
				refuse();
			} else {
				apply(kind, std::move(*result.wrap));
			}
		};
		provider->enroll(local, crl::guard(weak, enrolled));
	};
	// Keeping the kind the vault already carries writes nothing, but a store
	// waiting on this box still needs the vault open: the committed wrap is
	// opened once here with what the gate accepted, or by its provider, so
	// the user confirms the choice and unlocks it in the same step.
	const auto keepWithGrant = [=](VaultKind kind) {
		const auto committed = state->header
			? state->header->committedWrap()
			: nullptr;
		if (!committed) {
			fail();
			return;
		}
		setBusy(true);
		const auto wrap = *committed;
		const auto vault = &show->session().wallet().vault();
		const auto epoch = vault->clearEpoch();
		AcquireVaultKey(
			&show->session().local(),
			wrap,
			state->passcode,
			crl::guard(weak, [=](
					std::optional<SecureBytes> key,
					ProtectionError error) {
				if (error == ProtectionError::Cancelled) {
					setBusy(false);
				} else if (!key
					|| !vault->unlockWith(std::move(*key), epoch)) {
					refuse();
				} else {
					closeWith({
						.cancelled = false,
						.kind = kind,
						.grant = vault->grant(),
					});
				}
			}));
	};
	const auto save = [=] {
		if (state->busy) {
			return;
		}
		const auto kind = group->current();
		// In Switch the preselection is the kind the vault already carries,
		// so saving it writes nothing: what the caller wants is already
		// true. Removal's passcode row is not that case - it keeps the
		// vault's kind but still has the app lock to turn off.
		if (mode == KeyProtectionMode::Switch && kind == preselected) {
			if (grantForStore) {
				keepWithGrant(kind);
			} else {
				closeWith({ .cancelled = false, .kind = kind });
			}
			return;
		}
		setBusy(true);
		if (kind == VaultKind::Passcode) {
			savePasscode();
		} else if (kind == VaultKind::Open) {
			saveOpen();
		} else {
			saveHardware(kind);
		}
	};
	if (changeLink) {
		changeLink->entity()->overrideLinkClickHandler([=] {
			if (state->busy) {
				return;
			}
			// Busy until the change box answers, so its write reaches the
			// passcodeChanged latch without closing this box, and the passcode
			// row stays chosen while the bytes and the header are replaced.
			setBusy(true);
			const auto changed = [=](WalletPasscodeChanged result) {
				if (result.stale) {
					box->closeBox();
					return;
				} else if (result.passcode.empty()) {
					setBusy(false);
					return;
				}
				// A passcode-wrapped vault was rewrapped by the change, so the
				// header this box keeps for the switch is read again.
				auto reading = ReadVaultHeader(show->session().local());
				if (reading.state != VaultReading::State::Read
					|| !reading.header.committedWrap()) {
					box->closeBox();
					return;
				}
				state->header = std::move(reading.header);
				state->passcode = std::move(result.passcode);
				*band = PasscodeBand(state->passcode);
				state->passcodeChanged = false;
				setBusy(false);
				// The change saves the choice it was made from: nothing more
				// is written when the vault already asks for the passcode, and
				// another kind switches onto the new one.
				save();
			};
			show->showBox(Box(
				WalletPasscodeChangeBox,
				show,
				state->passcode.copy(),
				crl::guard(weak, changed)));
		});
	}
	state->save = box->addButton(tr::lng_settings_save(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	// A dismissal that landed after the removal walk had already moved a
	// wallet is not "nothing happened", so the report carries those wallets:
	// cancelled keeps meaning that the user dismissed this box, and a
	// dismissal with nothing moved stays as silent as every other
	// cancellation this file produces. The walk's own answer is still
	// dropped by the guarded done, which is why the list is read here, and
	// nothing can be added to it after reported is set one statement
	// earlier - that flag is what the walk's alive() probe answers no from.
	box->boxClosing(
	) | rpl::on_next([state, discardPendingInstall, done = args.done] {
		if (state->reported) {
			return;
		}
		state->reported = true;
		discardPendingInstall();
		if (state->result.cancelled && state->walk) {
			state->result.kind = state->walk->kind;
			state->result.changed = base::take(state->walk->changed);
		}
		if (done) {
			done(std::move(state->result));
		}
	}, box->lifetime());
}

struct RemovalConfirmation {
	~RemovalConfirmation();

	KeyProtectionArgs args;
	SecureBytes passcode;
	bool completed = false;
};

RemovalConfirmation::~RemovalConfirmation() {
	passcode.clear();
	if (!completed) {
		completed = true;
		if (auto done = base::take(args.done)) {
			done({});
		}
	}
}

void ShowRemovalProtectionBox(
		std::shared_ptr<Main::SessionShow> show,
		base::weak_ptr<Main::Session> weakSession,
		KeyProtectionArgs args,
		SecureBytes passcode) {
	if (!weakSession || !show->valid()) {
		passcode.clear();
		if (auto done = base::take(args.done)) {
			done({});
		}
		return;
	}
	const auto warn = [&] {
		const auto dependents = CollectVaultDependents();
		args.accounts.clear();
		args.accounts.reserve(dependents.passcodeWrapped.size());
		for (const auto &account : dependents.passcodeWrapped) {
			args.accounts.push_back(base::make_weak(account));
		}
		return AnyVaultHoldsKey(dependents.open)
			&& weakSession->domain().local().appLockEnabled();
	}();
	if (!warn) {
		show->showBox(Box(
			KeyProtectionBox,
			show,
			std::move(args),
			std::optional<VaultHeader>(),
			std::move(passcode)));
		return;
	}
	const auto state = std::make_shared<RemovalConfirmation>();
	state->args = std::move(args);
	state->passcode = std::move(passcode);
	show->showBox(Ui::MakeConfirmBox({
		.text = rpl::combine(
			tr::lng_settings_passcode_disable_sure(),
			tr::lng_wallet_protection_open_warning_nolock()
		) | rpl::map([](const QString &sure, const QString &warning) {
			return sure + u"\n\n"_q + warning;
		}),
		.confirmed = [=](Fn<void()> close) {
			if (state->completed) {
				close();
				return;
			}
			state->completed = true;
			auto args = base::take(state->args);
			auto passcode = base::take(state->passcode);
			const auto liveShow = show;
			const auto liveSession = weakSession;
			close();
			if (!liveSession || !liveShow->valid()) {
				passcode.clear();
				if (auto done = base::take(args.done)) {
					done({});
				}
				return;
			}
			{
				const auto dependents = CollectVaultDependents();
				args.accounts.clear();
				args.accounts.reserve(dependents.passcodeWrapped.size());
				for (const auto &account : dependents.passcodeWrapped) {
					args.accounts.push_back(base::make_weak(account));
				}
			}
			liveShow->showBox(Box(
				KeyProtectionBox,
				liveShow,
				std::move(args),
				std::optional<VaultHeader>(),
				std::move(passcode)));
		},
		.cancelled = [state](Fn<void()> close) {
			if (state->completed) {
				close();
				return;
			}
			state->completed = true;
			state->passcode.clear();
			auto done = base::take(state->args.done);
			close();
			if (done) {
				done({});
			}
		},
		.confirmText = tr::lng_settings_auto_night_disable(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

} // namespace

rpl::producer<QString> ProtectionProvider::label() const {
	return title();
}

void RegisterProtectionProvider(
		std::unique_ptr<ProtectionProvider> provider) {
	Expects(provider != nullptr);

	const auto kind = provider->kind();
	if (quint32(kind) < kFirstReservedVaultKind) {
		LOG(("Wallet Error: a protection provider claims vault kind %1, "
			"which this build opens without a provider.").arg(quint32(kind)));
		return;
	} else if (ProtectionProviderFor(kind)) {
		LOG(("Wallet Error: a second protection provider claims vault "
			"kind %1.").arg(quint32(kind)));
		return;
	}
	Providers().push_back(std::move(provider));
}

auto ProtectionProviders()
-> const std::vector<std::unique_ptr<ProtectionProvider>> & {
	return Providers();
}

ProtectionProvider *ProtectionProviderFor(VaultKind kind) {
	const auto &list = Providers();
	const auto i = ranges::find_if(
		list,
		[&](const std::unique_ptr<ProtectionProvider> &provider) {
			return (provider->kind() == kind);
		});
	return (i != end(list)) ? i->get() : nullptr;
}

void MintVerificationOnWorker(
		not_null<QObject*> guard,
		const SecureBytes &passcode,
		Fn<void(std::optional<Storage::PasscodeVerification>)> done) {
	auto utf8 = Utf8Copy(passcode);
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
	});
	const auto &local = Core::App().domain().local();
	Storage::DeriveOnWorker(local.prepareOpen(utf8), crl::guard(guard, [done](
			Storage::PasscodeDerivation &&derived) {
		auto &local = Core::App().domain().local();
		done(local.verifyPasscode(std::move(derived)));
	}));
}

void ShowKeyProtectionBox(
		std::shared_ptr<Main::SessionShow> show,
		KeyProtectionArgs args,
		SecureBytes verified) {
	auto &session = show->session();
	const auto weakSession = base::make_weak(&session);
	const auto report = [done = args.done](KeyProtectionResult result) {
		if (done) {
			done(std::move(result));
		}
	};
	auto header = std::optional<VaultHeader>();
	switch (args.mode) {
	case KeyProtectionMode::Install:
		// Install is asked for an account that has no vault yet, or one whose
		// vault this process cannot open and the caller can reset once the
		// user confirms it; a header that reads in any other case is the
		// caller's contract broken, not a user error.
		if (!args.resetUnusableVault
			&& ReadVaultHeader(session.local()).state
			== VaultReading::State::Read) {
			LOG(("Wallet Error: key protection asked to install over a vault "
				"this account already carries."));
			report({ .failed = true });
			return;
		}
		break;
	case KeyProtectionMode::Switch: {
		auto reading = ReadVaultHeader(session.local());
		if (reading.state != VaultReading::State::Read) {
			show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
			report({});
			return;
		}
		header = std::move(reading.header);
	} break;
	case KeyProtectionMode::Removal:
		for (const auto &weak : args.accounts) {
			const auto account = weak.get();
			if (account
				&& (ReadVaultHeader(account->local()).state
					!= VaultReading::State::Read)) {
				show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
				report({});
				return;
			}
		}
		break;
	}
	auto chooser = [=](SecureBytes passcode) mutable {
		if (args.mode == KeyProtectionMode::Removal) {
			ShowRemovalProtectionBox(
				show,
				weakSession,
				std::move(args),
				std::move(passcode));
			return;
		}
		show->showBox(Box(
			KeyProtectionBox,
			show,
			std::move(args),
			std::move(header),
			std::move(passcode)));
	};
	// The passcode comes first in every mode and whatever the vault's
	// retention window says: no branch here consults vault.retained(). Only
	// bytes the caller has already proved against key_data on the worker,
	// the gate's own check, skip the gate.
	if (!session.domain().local().hasPasscode()) {
		chooser(SecureBytes());
		return;
	} else if (!verified.empty()) {
		chooser(std::move(verified));
		return;
	}
	show->showBox(Box(WalletPasscodeBox, WalletPasscodeBoxArgs{
		.show = show,
		.check = WalletPasscodeCheck::KeyData,
		.passed = [chooser](WalletPasscodeGate gate) mutable {
			chooser(std::move(gate.passcode));
		},
		.cancelled = [report] { report({}); },
	}));
}

VaultDependents CollectVaultDependents() {
	auto result = VaultDependents();
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		const auto reading = ReadVaultHeader(account->local());
		if (reading.state != VaultReading::State::Read) {
			continue;
		}
		const auto wrap = reading.header.committedWrap();
		if (!wrap) {
			continue;
		} else if (wrap->kind == VaultKind::Passcode) {
			result.passcodeWrapped.push_back(account.get());
		} else if (wrap->kind == VaultKind::Open) {
			result.open.push_back(account.get());
		}
	}
	return result;
}

bool VaultHoldsKey(not_null<Main::Account*> account) {
	const auto session = account->maybeSession();
	if (session && session->wallet().custodyBusy()) {
		return true;
	}
	const auto store = ReadCustodyStore(account->local());
	return !store
		|| !store->records.empty()
		|| (store->pendingRotation
			&& !store->pendingRotation->secretRef.isEmpty());
}

bool AnyVaultHoldsKey(const std::vector<not_null<Main::Account*>> &accounts) {
	return ranges::any_of(accounts, [](not_null<Main::Account*> account) {
		return VaultHoldsKey(account);
	});
}

void DropKeylessPasscodeVaults() {
	auto notify = std::vector<base::weak_ptr<Main::Session>>();
	for (const auto &account : CollectVaultDependents().passcodeWrapped) {
		if (VaultHoldsKey(account)) {
			continue;
		}
		const auto session = account->maybeSession();
		if (session) {
			session->wallet().vault().clear();
		}
		if (RemoveVaultHeader(account->local())) {
			LOG(("Wallet Info: dropped a passcode vault that holds no key."));
		}
		if (session) {
			notify.push_back(base::make_weak(session));
		}
	}
	for (const auto &weak : notify) {
		if (const auto session = weak.get()) {
			session->wallet().notifyKeyProtectionChanged();
		}
	}
}

int CountVaultWrapDependents(const VaultWrap &wrap) {
	auto result = 0;
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		const auto reading = ReadVaultHeader(account->local());
		if (reading.state == VaultReading::State::Absent) {
			continue;
		} else if (reading.state != VaultReading::State::Read) {
			++result;
			continue;
		}
		const auto committed = reading.header.committedWrap();
		if (committed
			&& (committed->kind == wrap.kind)
			&& (committed->openSecret == wrap.openSecret)) {
			++result;
		}
	}
	return result;
}

} // namespace Wallet
