/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_unlock.h"

#include "base/openssl_help.h"
#include "base/weak_qptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/button_busy.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_session.h"

#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

[[nodiscard]] VaultAuthorization Share(VaultGrant grant) {
	return grant.valid()
		? std::make_shared<VaultGrant>(std::move(grant))
		: nullptr;
}

[[nodiscard]] QString CurrentValue(rpl::producer<QString> producer) {
	auto result = QString();
	auto lifetime = rpl::lifetime();
	std::move(producer) | rpl::take(1) | rpl::on_next([&](QString &&value) {
		result = std::move(value);
	}, lifetime);
	return result;
}

struct HardwareUnlockArgs {
	std::shared_ptr<Main::SessionShow> show;
	not_null<ProtectionProvider*> provider;
	VaultWrap wrap;
	Fn<void(VaultAuthorization)> done;
};

// Retention is a VaultRuntime property the user ticks inside the passcode
// box before it asks; a hardware provider's system sheet has no checkbox,
// so this box asks the same question first and Continue is what runs the
// sheet. Every dismissal reaches boxClosing() and reports done once through
// the latch WalletPasscodeBox keeps; a Continue that got an answer reports
// through the answer instead. The provider answers many main-thread turns
// later, possibly after this box or the whole panel is gone: such an answer
// is dropped whole - no toast, no flag, no report - because the close has
// already reported, and the sheet itself is system-owned and stays up until
// the user answers it. Nothing on the header's write path runs off the main
// thread: unlockWith() and grant() run inside the marshalled answer.
void HardwareUnlockBox(
		not_null<Ui::GenericBox*> box,
		HardwareUnlockArgs args) {
	struct State {
		bool reported = false;
		bool busy = false;
		QPointer<Ui::RoundButton> submit;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto show = args.show;
	const auto provider = args.provider;
	const auto done = args.done;
	const auto weak = base::make_weak(box);
	box->setTitle(provider->title());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_protection_hardware_prompt(
				lt_provider,
				provider->label()),
			st::boxLabel),
		st::walletProtectionIntroMargin);
	const auto remember = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_wallet_passcode_remember(tr::now),
			false,
			st::defaultBoxCheckbox),
		st::walletPasscodeCheckboxMargin);
	const auto report = [=](VaultAuthorization grant) {
		state->reported = true;
		box->closeBox();
		done(std::move(grant));
	};
	const auto toast = [=](tr::phrase<lngtag_provider> phrase) {
		show->showToast(phrase(
			tr::now,
			lt_provider,
			CurrentValue(provider->label())));
	};
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		Ui::SetButtonBusy(state->submit.data(), busy);
	};
	const auto answered = [=](quint32 epoch, ProtectionUnwrapResult result) {
		if (!show->valid() || !weak || state->reported) {
			return;
		}
		setBusy(false);
		auto &session = show->session();
		auto &vault = session.wallet().vault();
		const auto error = (result.error == ProtectionError::None
			&& !result.key)
			? ProtectionError::Corrupt
			: result.error;
		switch (error) {
		case ProtectionError::Cancelled:
			report(nullptr);
			return;
		case ProtectionError::AuthenticationFailed:
			toast(tr::lng_wallet_protection_hardware_failed);
			return;
		case ProtectionError::Unavailable:
		case ProtectionError::Corrupt:
			session.wallet().setVaultKeyUnusable(true);
			toast(tr::lng_wallet_protection_hardware_unavailable);
			report(nullptr);
			return;
		case ProtectionError::Absent:
			session.wallet().setVaultKeyUnusable(true);
			toast(tr::lng_wallet_protection_hardware_absent);
			report(nullptr);
			return;
		case ProtectionError::None:
			break;
		}
		if (!vault.unlockWith(std::move(*result.key), epoch)) {
			show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
			report(nullptr);
			return;
		}
		session.wallet().setVaultKeyUnusable(false);
		vault.setRetention(remember->checked());
		report(Share(vault.grant()));
	};
	const auto submit = [=] {
		if (state->busy) {
			return;
		}
		auto &session = show->session();
		// The provider answers many main-thread turns after the ask, so a
		// clear trigger can land inside its prompt. The epoch is read here,
		// before the ask, and the runtime refuses a key that was opened
		// before that clear rather than undoing it.
		const auto epoch = session.wallet().vault().clearEpoch();
		setBusy(true);
		provider->unwrap(&session.local(), args.wrap, [=](
				ProtectionUnwrapResult result) {
			answered(epoch, std::move(result));
		});
	};
	state->submit = box->addButton(tr::lng_continue(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->boxClosing() | rpl::on_next([=] {
		if (!state->reported) {
			state->reported = true;
			done(nullptr);
		}
	}, box->lifetime());
}

// A destroyed panel drops the box silently and a box never shown never
// emits boxClosing(), so this fails closed before showing anything, as the
// passcode branch of UnlockByKind does.
void UnlockWithProvider(
		std::shared_ptr<Main::SessionShow> show,
		not_null<ProtectionProvider*> provider,
		VaultWrap wrap,
		Fn<void(VaultAuthorization)> done) {
	if (!show->valid()) {
		done(nullptr);
		return;
	}
	show->showBox(Box(HardwareUnlockBox, HardwareUnlockArgs{
		.show = show,
		.provider = provider,
		.wrap = std::move(wrap),
		.done = std::move(done),
	}));
}

// The runtime releases a key only through a grant's destructor, while
// unlockOpen() and unlockWith() install one without minting a grant. So
// every successful unlock here is followed immediately by grant(): the
// handle that answers is what keeps the key alive, and its last copy
// going away is what cleanses it.
void UnlockByKind(
		std::shared_ptr<Main::SessionShow> show,
		VaultReading reading,
		bool ignoreRetention,
		Fn<void(VaultAuthorization)> done) {
	auto &session = show->session();
	auto &vault = session.wallet().vault();
	auto &local = session.local();
	const auto wrap = reading.header.committedWrap();
	Assert(wrap != nullptr);
	// A wrap that reads but does not open is the same dead end a Broken
	// header is, so it states the same line; a dismissed prompt states
	// nothing, because the user already knows what they answered.
	const auto unavailable = [show, done] {
		show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
		done(nullptr);
	};
	if (!ignoreRetention && vault.retained()) {
		done(Share(vault.grant()));
		return;
	}
	// A hardware kind is opened by the provider that registered it, through
	// the retention box above its system sheet. One implementation serves two
	// entries, AcquireVaultUnlock's Read case and MakeCustodyInstaller's, and
	// the installer's ignoreRetention skips only the retained window above:
	// the box offers its checkbox on that path too, as the passcode box does.
	// A kind no provider claims - a header copied to a platform without one -
	// cannot open in this process either, so it flags the session the way a
	// provider's failed answer does and the restore can replace it.
	if (quint32(wrap->kind) >= kFirstReservedVaultKind) {
		const auto provider = ProtectionProviderFor(wrap->kind);
		if (!provider) {
			session.wallet().setVaultKeyUnusable(true);
			unavailable();
			return;
		}
		UnlockWithProvider(show, provider, *wrap, done);
	} else if (wrap->kind == VaultKind::Open) {
		if (vault.unlockOpen(local)) {
			done(Share(vault.grant()));
		} else {
			unavailable();
		}
	} else {
		// A destroyed panel drops the box silently, and a box that was never
		// shown never emits boxClosing(), so neither passed nor cancelled
		// would ever run and the flow's own latch would stay set for the life
		// of the session. Fail closed instead of showing nothing.
		if (!show->valid()) {
			done(nullptr);
			return;
		}
		show->showBox(Box(WalletPasscodeBox, WalletPasscodeBoxArgs{
			.show = show,
			.check = WalletPasscodeCheck::Vault,
			.passed = [done](WalletPasscodeGate gate) {
				done(Share(std::move(gate.grant)));
			},
			.cancelled = [done] { done(nullptr); },
		}));
	}
}

// The vault wrap the typed passcode has to open, read on the main thread for
// the job that derives against it. It serves the Vault check alone, which
// UnlockByKind shows only over a header committed to a passcode wrap; a
// header that stopped reading or moved to another kind while the box was
// open yields no wrap here, so the job opens no vault key. The derived wrap
// key dies in the job.
[[nodiscard]] std::optional<VaultWrap> CommittedPasscodeWrap(
		Storage::Account &local,
		VaultRuntime &vault) {
	const auto reading = vault.reading(local);
	if (reading.state != VaultReading::State::Read) {
		return std::nullopt;
	}
	const auto wrap = reading.header.committedWrap();
	if (!wrap || wrap->kind != VaultKind::Passcode) {
		return std::nullopt;
	}
	return *wrap;
}

// The memory-hard part of the gate, cut out as one worker job for either
// check: for Vault the vault wrap key the typed passcode must open and the
// vault key behind it, for KeyData the key_data derivation alone. It holds
// values only - a copy of the committed passcode wrap, the typed bytes as
// SecureBytes, the key_data job - and never dereferences a runtime, a
// session or the box, so nothing is installed, compared or counted before
// the derivation has answered. run() cleanses its own copy of the typed
// bytes on every exit; the typed bytes themselves survive to the answer
// only for the KeyData hand-off, where the gate carries them to the caller,
// and are cleared by run() for Vault, where the vault key it opened is what
// the answer installs. Every key it produced lives in SecureBytes and dies
// with the job on the main thread, installed or not.
struct GateDerivation {
	WalletPasscodeCheck check = WalletPasscodeCheck::Vault;
	std::optional<Storage::PasscodeDerivation> keyData;
	std::optional<VaultWrap> vaultWrap;
	SecureBytes passcode;
	std::optional<SecureBytes> vaultKey;

	void run();
};

void GateDerivation::run() {
	auto utf8 = QByteArray(
		reinterpret_cast<const char*>(passcode.span().data()),
		passcode.size());
	const auto cleanse = gsl::finally([&] {
		if (!utf8.isEmpty()) {
			OPENSSL_cleanse(utf8.data(), utf8.size());
		}
		if (check == WalletPasscodeCheck::Vault) {
			passcode.clear();
		}
	});
	if (vaultWrap) {
		if (const auto wrapKey = DeriveVaultWrapKey(*vaultWrap, utf8)) {
			vaultKey = UnwrapVaultKey(*vaultWrap, *wrapKey);
		}
	}
	if (keyData) {
		keyData->run();
	}
}

// What the forgot-passcode confirmation says about every wallet this device
// would lose. The restorability of one is the model the logout confirmation
// already states: WalletLossOnLogout() reads the custody store, the served
// public key and the backup capability, unlocking nothing and writing
// nothing. An empty loss is two different things - every record is backed, or
// there is no record at all - so the backup claim is made only against a
// store that holds records, which is what the model's holdsRecords carries,
// and an account holding none contributes nothing. That fact travels on the
// loss instead of being read again, so the store is read exactly once per
// dependent account and the model, not a second read, is what tells the two
// empty losses apart. A wallet is named only when its account has a live
// session to name it from - an unauthorized account in
// Main::Domain::accounts() has none, and inventing a name for it would be a
// guess about which key is about to go.
[[nodiscard]] QString ForgottenPasscodeAbout() {
	auto result = tr::lng_wallet_passcode_forgot_about(tr::now);
	const auto dependents = CollectVaultDependents();
	for (const auto &account : dependents.passcodeWrapped) {
		const auto loss = WalletLossOnLogout(account);
		auto paragraph = ForgottenPasscodeLoss(loss);
		if (paragraph.isEmpty()) {
			if (!loss.holdsRecords) {
				continue;
			}
			paragraph = tr::lng_wallet_passcode_forgot_backed(tr::now);
		}
		const auto session = account->maybeSession();
		result += u"\n\n"_q
			+ (session
				? (session->user()->name() + u": "_q + paragraph)
				: paragraph);
	}
	return result;
}

// The forgot-passcode path's use of the shared reset. The live session's
// cached key goes first, so nothing can seal a new record under a key that
// is about to stop existing; ResetVaultAndCustody() then destroys what it
// can reach of the vault on disk, which is not always the header - a store
// it could not empty keeps it; and the session is made to agree with the
// disk at once - this path installs nothing afterwards, so its notify
// funnel runs right here. The answer is the reset's: a false says
// leftovers remain without saying whether the header is one of them, so
// the caller decides the passcode's fate by looking at the vaults
// themselves.
[[nodiscard]] bool DropVaultAndCustody(not_null<Main::Account*> account) {
	auto &local = account->local();
	const auto session = account->maybeSession();
	if (session) {
		session->wallet().vault().clear();
	}
	const auto ok = ResetVaultAndCustody(local);
	if (session) {
		session->wallet().dropCustodyAfterForgottenPasscode();
	}
	return ok;
}

// The accounts come from CollectVaultDependents().passcodeWrapped and from
// nothing else, re-enumerated here rather than carried over from the
// confirmation: an Open vault, a hardware kind and a header that does not
// read are absent from that list by construction, and an account listed
// while the box was open can have been logged out since.
//
// The passcode goes last, and only when every one of those vaults is gone -
// which is a second enumeration and not the cleanup's answer, because that
// answer does not say whether a header survived: a store that could not be
// read loses its header anyway, while one that was read but could not be
// written emptied keeps it, and both report false. Re-reading the headers
// is the only thing that tells those two disks apart, and treating the
// boolean as the verdict would leave a passcode nobody remembers standing
// over nothing it can open. The mirror order is the one that strands
// a key: a header that could not be removed would stay sealed under that
// passcode, and with the passcode already gone there would be no way left to
// remove the header either. clearPasscodeAfterReset() writes without asking
// for the passcode, safe here for the reason it is safe after a reset - the
// entry point requires the app lock to be off, so once these stores are gone
// the passcode guards nothing that could still be asked for - and its write
// can still fail, so the state it leaves behind is read back before anything
// claims it succeeded. No account is logged out.
void DropForgottenPasscode(std::shared_ptr<Main::SessionShow> show) {
	auto cleaned = true;
	const auto dependents = CollectVaultDependents();
	for (const auto &account : dependents.passcodeWrapped) {
		if (!DropVaultAndCustody(account)) {
			cleaned = false;
		}
	}
	if (!CollectVaultDependents().passcodeWrapped.empty()) {
		show->showToast(tr::lng_wallet_passcode_forgot_failed(tr::now));
		return;
	}
	auto &local = Core::App().domain().local();
	// The passcode may already be gone: the last dependent's
	// DropVaultAndCustody() reaches the notify funnel through
	// dropCustodyAfterForgottenPasscode(), whose reconciliation drops it.
	// A dependent without a session fires no notify, so the checked write
	// below is still this function's own; one that fails is finished by the
	// next start's reconciliation.
	if (local.hasPasscode()) {
		local.clearPasscodeAfterReset();
		if (local.hasPasscode()) {
			show->showToast(tr::lng_wallet_passcode_forgot_later(tr::now));
			return;
		}
		Core::App().settings().setSystemUnlockEnabled(false);
		Core::App().saveSettingsDelayed();
		Core::App().localPasscodeChanged();
	}
	show->showToast(cleaned
		? tr::lng_wallet_passcode_forgot_done(tr::now)
		: tr::lng_wallet_passcode_forgot_leftovers(tr::now));
}

void ConfirmForgottenPasscode(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> closeGate) {
	show->showBox(Ui::MakeConfirmBox({
		.text = ForgottenPasscodeAbout(),
		.confirmed = [=](Fn<void()> &&close) {
			close();
			// The link was offered only while the verified app lock was off,
			// which is what lets DropForgottenPasscode() clear the passcode
			// without asking for it. This confirmation can outlive that
			// reading - another window may have turned the lock on while it
			// was open - so the lock is read again at the destructive step,
			// and a stale confirmation removes nothing and leaves the gate
			// open for the passcode that now locks the launch.
			if (Core::App().domain().local().appLockEnabled()) {
				LOG(("Wallet Warning: a forgotten-passcode reset was "
					"confirmed after the app lock was enabled; nothing "
					"removed."));
				return;
			}
			DropForgottenPasscode(show);
			closeGate();
		},
		.confirmText = tr::lng_wallet_passcode_forgot_confirm(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_wallet_passcode_forgot_title(),
	}));
}

} // namespace

bool KeyAuthorization::valid() const {
	return (grant != nullptr) || (install != nullptr);
}

void AcquireVaultUnlock(VaultUnlockArgs args) {
	Expects(args.show != nullptr);

	using State = VaultReading::State;
	const auto show = args.show;
	const auto mayInstall = args.mayInstall;
	const auto done = std::move(args.done);
	const auto answer = [=](KeyAuthorization result) {
		if (done) {
			done(std::move(result));
		}
	};
	// The installer travels beside the grant for every header state a read
	// was acquired for, not only for the one that has no vault: a store
	// resolved through it re-routes by kind at store time, which is what
	// keeps a passcode vault asked at install even when a retained window
	// had already handed this flow a prompt-free read grant.
	const auto acquired = [=](VaultAuthorization grant) {
		if (!grant) {
			answer({});
			return;
		}
		auto result = KeyAuthorization{ .grant = std::move(grant) };
		if (mayInstall) {
			result.install = MakeCustodyInstaller(show);
		}
		answer(std::move(result));
	};
	auto &session = show->session();
	auto reading = session.wallet().vault().reading(session.local());
	switch (reading.state) {
	case State::Read:
		// A key that could not be opened in this process is not asked again:
		// the flow carries the installer alone, as it does over an absent
		// header, and the consented restore that reaches it resets the vault
		// only after the user explicitly confirms deleting the stored key; a
		// cancellation before that leaves the old ciphertext in place. A
		// relaunch asks the provider afresh.
		if (mayInstall && session.wallet().vaultKeyUnusable()) {
			answer(KeyAuthorization{
				.install = MakeCustodyInstaller(show),
			});
			return;
		}
		UnlockByKind(show, std::move(reading), false, acquired);
		return;
	case State::Absent:
		if (mayInstall) {
			answer(KeyAuthorization{
				.install = MakeCustodyInstaller(show),
			});
		} else {
			show->showToast(VaultLockedText(&session));
			answer({});
		}
		return;
	case State::Broken:
	case State::Unsupported:
		show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
		answer({});
		return;
	}
	Unexpected("Vault reading state in AcquireVaultUnlock.");
}

CustodyInstaller MakeCustodyInstaller(
		std::shared_ptr<Main::SessionShow> show) {
	Expects(show != nullptr);

	// Only show and the caller's own callbacks are captured, so the ladder is
	// safe to invoke long after the box that acquired it is gone. The header
	// is re-read here because ShowKeyProtectionBox refuses Install over a
	// vault that already reads, and because the state may have moved between
	// the acquisition and the store.
	return [show](CustodyInstallRequest request) {
		using State = VaultReading::State;
		const auto answer = [ready = std::move(request.ready)](
				CustodyInstall install) {
			if (ready) {
				ready(std::move(install));
			}
		};
		// Every arm below ends in a box or a toast, and a destroyed panel
		// drops both silently, so an installer invoked after the wallet
		// window is gone would never answer and would strand the flow's
		// latch. Fail closed before anything is read.
		if (!show->valid()) {
			answer({});
			return;
		}
		auto &session = show->session();
		auto reading = session.wallet().vault().reading(session.local());
		const auto install = [=](Fn<bool()> reset) {
			ShowKeyProtectionBox(show, {
				.mode = KeyProtectionMode::Install,
				.resetUnusableVault = std::move(reset),
				.done = [answer](KeyProtectionResult result) {
					auto grant = (!result.cancelled && !result.failed)
						? Share(std::move(result.grant))
						: nullptr;
					if (!grant) {
						answer({});
					} else {
						answer({ .grant = std::move(grant), .created = true });
					}
				},
			});
		};
		switch (reading.state) {
		case State::Absent:
			install(nullptr);
			return;
		case State::Read:
			// A hardware wrap that could not be opened in this process only
			// flags the session, and nothing is dropped before the consented
			// restore or import that reaches this installer confirms it: the
			// chooser prepares the new protection first and then asks the
			// user to confirm deleting the stored key, and only on that
			// confirmation does the session's reset remove the old vault,
			// after checking again that the target and the flag still hold.
			// A cancelled chooser, Open warning, provider prompt or
			// confirmation leaves the old ciphertext openable on a later run
			// where the factor works again; past the confirmation it is gone
			// for good, and a failed store afterwards is retried through the
			// backup or the recovery phrase. Without a reset the request
			// cannot install here at all.
			if (session.wallet().vaultKeyUnusable()) {
				const auto reset = request.resetUnusableVault;
				if (!reset) {
					LOG(("Wallet Error: the custody installer was asked to "
						"install over a vault this process cannot open, "
						"without a reset."));
					answer({});
					return;
				}
				install([=] {
					switch (reset()) {
					case CustodyResetResult::Done:
						return true;
					case CustodyResetResult::Refused:
						return false;
					case CustodyResetResult::Failed:
						if (show->valid()) {
							show->showToast(
								tr::lng_wallet_passcode_forgot_failed(tr::now));
						}
						return false;
					}
					Unexpected("Reset result in the custody installer.");
				});
				return;
			}
			// A restored or imported key is about to be written, so the user
			// confirms how it is protected here even over a vault that already
			// reads: keeping the current kind unlocks it, and another kind
			// switches the whole vault before the store. The box answers with
			// the grant either way, so nothing is asked a second time.
			ShowKeyProtectionBox(show, {
				.mode = KeyProtectionMode::Switch,
				.grantForStore = true,
				.done = [answer](KeyProtectionResult result) {
					auto grant = (!result.cancelled
						&& !result.failed
						&& result.grant.valid())
						? Share(std::move(result.grant))
						: nullptr;
					if (!grant) {
						answer({});
					} else {
						answer({ .grant = std::move(grant) });
					}
				},
			});
			return;
		case State::Broken:
		case State::Unsupported:
			show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
			answer({});
			return;
		}
		Unexpected("Vault reading state in the custody installer.");
	};
}

QString VaultLockedText(not_null<Main::Session*> session) {
	return session->domain().local().hasPasscode()
		? tr::lng_wallet_vault_locked(tr::now)
		: tr::lng_wallet_vault_no_passcode(tr::now);
}

void DropUnusedPasscode() {
	auto &local = Core::App().domain().local();
	if (!local.hasPasscode()
		|| local.appLockEnabled()
		|| !CollectVaultDependents().passcodeWrapped.empty()) {
		return;
	}
	local.clearPasscodeAfterReset();
	if (local.hasPasscode()) {
		LOG(("Wallet Error: could not remove the passcode after its last "
			"dependent went; the next reconciliation tries again."));
		return;
	}
	Core::App().settings().setSystemUnlockEnabled(false);
	Core::App().saveSettingsDelayed();
	Core::App().localPasscodeChanged();
}

// The verdict the typed passcode must pass is the caller's; both of them
// are stated with WalletPasscodeCheck in wallet_unlock.h.
void WalletPasscodeBox(
		not_null<Ui::GenericBox*> box,
		WalletPasscodeBoxArgs args) {
	Expects(args.passed != nullptr);

	struct State {
		bool reported = false;
		bool busy = false;
		QPointer<Ui::RoundButton> submit;
	};
	const auto state = box->lifetime().make_state<State>();
	box->setTitle(tr::lng_passcode_check_title());
	const auto &fieldSt = st::settingLocalPasscodeInputField;
	const auto wrap = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::walletPasscodeFieldMargin);
	wrap->resize(wrap->width(), fieldSt.heightMin);
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		wrap,
		fieldSt,
		tr::lng_passcode_enter());
	wrap->widthValue(
	) | rpl::on_next([=](int width) {
		field->moveToLeft((width - field->width()) / 2, 0);
	}, wrap->lifetime());
	const auto error = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			QString(),
			st::settingLocalPasscodeError),
		st::walletPasscodeErrorMargin,
		style::al_top);
	error->hide();
	// The link is bound to the Vault check alone, because that is the one
	// check meaning "this device holds a key it cannot open without the
	// passcode". KeyData gates the protection chooser, which the user can
	// simply cancel, where dropping a vault from inside the box would race
	// the caller's continuation. Verified app-lock-off proves this local key
	// can open without a passcode; otherwise the link would offer removal of
	// a launch-lock passcode nobody had to prove anything for.
	const auto forgot = (args.check == WalletPasscodeCheck::Vault
		&& !args.show->session().domain().local().appLockEnabled())
		? box->addRow(
			object_ptr<Ui::LinkButton>(
				box,
				tr::lng_wallet_passcode_forgot(tr::now),
				st::boxLinkButton),
			st::walletPasscodeForgotMargin)
		: nullptr;
	if (forgot) {
		const auto weak = base::make_weak(box);
		forgot->setClickedCallback([show = args.show, weak] {
			ConfirmForgottenPasscode(show, [weak] {
				if (weak) {
					weak->closeBox();
				}
			});
		});
	}
	// Opening the protection chooser must never leave a vault unlocked or
	// retained behind it, so that check offers no retention and mints no
	// grant: it answers with the typed passcode alone.
	const auto retain = (args.check != WalletPasscodeCheck::KeyData);
	const auto remember = retain
		? box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_wallet_passcode_remember(tr::now),
				false,
				st::defaultBoxCheckbox),
			st::walletPasscodeCheckboxMargin)
		: nullptr;
	QObject::connect(field, &Ui::MaskedInputField::changed, [=] {
		error->hide();
	});
	box->setFocusCallback([=] {
		field->setFocusFast();
	});
	const auto showError = [=](const QString &text) {
		field->setFocus();
		field->showError();
		error->show();
		error->setText(text);
	};
	// A derivation in flight disables the field as well as the Submit
	// button. The button stops taking the mouse, but Enter in the field
	// and a key release on a focused button still reach the handler, so
	// submit itself refuses while state->busy is set.
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		field->setDisabled(busy);
		Ui::SetButtonBusy(state->submit.data(), busy);
		if (!busy) {
			field->setFocus();
		}
	};
	const auto submit = [=] {
		if (state->busy) {
			return;
		} else if (!passcodeCanTry()) {
			showError(tr::lng_flood_error(tr::now));
			return;
		}
		auto &session = args.show->session();
		auto &vault = session.wallet().vault();
		auto utf8 = field->text().toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!utf8.isEmpty()) {
				OPENSSL_cleanse(utf8.data(), utf8.size());
			}
		});
		auto job = GateDerivation{
			.check = args.check,
			.passcode = SecureBytes(utf8),
		};
		if (args.check == WalletPasscodeCheck::Vault) {
			job.vaultWrap = CommittedPasscodeWrap(session.local(), vault);
		} else {
			job.keyData = session.domain().local().prepareOpen(utf8);
		}
		const auto epoch = vault.clearEpoch();
		setBusy(true);
		Storage::DeriveOnWorker(std::move(job), crl::guard(box, [=](
				GateDerivation &&job) {
			setBusy(false);
			auto &session = args.show->session();
			auto &vault = session.wallet().vault();
			auto ok = false;
			auto refused = false;
			switch (args.check) {
			case WalletPasscodeCheck::Vault:
				refused = job.vaultKey
					&& !vault.unlockWith(std::move(*job.vaultKey), epoch);
				ok = job.vaultKey && !refused;
				break;
			case WalletPasscodeCheck::KeyData:
				ok = session.domain().local().checkPasscode(
					std::move(*job.keyData));
				break;
			}
			if (refused) {
				// The clear epoch moved under the derivation - a screen
				// lock, a system sleep or a committed wrap change - so the
				// key is refused exactly as UnlockByKind refuses a late
				// provider answer. The passcode was not wrong, so no bad
				// try is counted and the box stays open for another try.
				showError(tr::lng_wallet_vault_unavailable(tr::now));
				return;
			} else if (!ok) {
				cSetPasscodeBadTries(cPasscodeBadTries() + 1);
				cSetPasscodeLastTry(crl::now());
				field->selectAll();
				showError(tr::lng_passcode_wrong(tr::now));
				return;
			}
			cSetPasscodeBadTries(0);
			auto gate = WalletPasscodeGate();
			if (retain) {
				vault.setRetention(remember->checked());
				gate.grant = vault.grant();
			} else {
				gate.passcode = std::move(job.passcode);
			}
			state->reported = true;
			box->closeBox();
			args.passed(std::move(gate));
		}));
	};
	QObject::connect(field, &Ui::MaskedInputField::submitted, submit);
	state->submit = box->addButton(tr::lng_passcode_submit(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	// Cancel, Escape and the layer being replaced all reach closeHook(), so
	// this one handler tells a caller that must persist nothing about every
	// dismissal; the flag keeps a successful submit and the close it starts
	// from reporting twice.
	box->boxClosing() | rpl::on_next([=] {
		if (!state->reported) {
			state->reported = true;
			if (args.cancelled) {
				args.cancelled();
			}
		}
	}, box->lifetime());
}

} // namespace Wallet
