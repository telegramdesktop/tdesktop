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
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "wallet/wallet_custody.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_session.h"

#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

// The one durable trace the forgot path leaves between destroying the
// vaults and removing the passcode. It is an app-level preference and not
// a key_data field, because key_data is the file whose write can fail here
// and a flag written into it would fail for the same reason and at the
// same moment; the settings file is a different one, and Local::start()
// has already read it by the time any domain starts.
constexpr auto kForgottenPasscodeClearKey = std::string_view(
	"wallet.forgotten_passcode_clear");

[[nodiscard]] VaultAuthorization Share(VaultGrant grant) {
	return grant.valid()
		? std::make_shared<VaultGrant>(std::move(grant))
		: nullptr;
}

// The runtime releases a key only through a grant's destructor, while
// unlockOpen(), unlockWithPasscode() and unlockWith() install one without
// minting a grant. So every successful unlock here is followed immediately
// by grant(): the handle that answers is what keeps the key alive, and its
// last copy going away is what cleanses it.
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
	// This branch cannot run in this build: no provider is registered on any
	// platform, and ParseVaultHeader maps every kind at or above
	// kFirstReservedVaultKind to Unsupported, so no hardware wrap is ever
	// read back as one. It is written for 2026/08/31/passcode-touch-id-unlock
	// and 2026/08/31/passcode-windows-hello-unlock, each of which teaches
	// wallet_vault.cpp's reader its own kind in the commit that registers its
	// provider. One implementation serves two entries: AcquireVaultUnlock's
	// Read case and MakeCustodyInstaller's, which routes through here rather
	// than repeating the ladder.
	if (quint32(wrap->kind) >= kFirstReservedVaultKind) {
		const auto provider = ProtectionProviderFor(wrap->kind);
		if (!provider) {
			unavailable();
			return;
		}
		// The provider answers many main-thread turns after the ask, so a
		// clear trigger can land inside its prompt. The epoch is read here,
		// before the ask, and the runtime refuses a key that was opened
		// before that clear rather than undoing it.
		const auto epoch = vault.clearEpoch();
		provider->unwrap(&local, *wrap, [show, done, unavailable, epoch](
				ProtectionUnwrapResult result) {
			if (result.error == ProtectionError::Cancelled) {
				done(nullptr);
				return;
			} else if (result.error != ProtectionError::None || !result.key) {
				unavailable();
				return;
			}
			auto &runtime = show->session().wallet().vault();
			if (!runtime.unlockWith(std::move(*result.key), epoch)) {
				unavailable();
				return;
			}
			done(Share(runtime.grant()));
		});
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
// the job that derives against it, for either check: a header that is not
// Read means the vault has no passcode wrap to prove, so the check that must
// prove key_data and the vault at once lets key_data alone decide. Absent is
// the install case with a passcode already set; Broken and Unsupported never
// reach here, the caller refuses before the gate is shown. The derived wrap
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
// check: the vault wrap key the typed passcode must open and the vault key
// behind it, and for KeyDataAndVault the key_data derivation beside them. It
// holds values only - a copy of the committed passcode wrap, the typed bytes
// as SecureBytes, the key_data job - and never dereferences a runtime, a
// session or the box, so nothing is installed, compared or counted before
// every derivation has answered. Both legs run whatever the other answered,
// so a wrong passcode takes the same time a right one does. run() cleanses
// its own copy of the typed bytes on every exit; the typed bytes themselves
// survive to the answer only for the KeyDataAndVault hand-off, where the
// gate carries them to the caller, and are cleared by run() for Vault, where
// the vault key it opened is what the answer installs. Every key it produced
// lives in SecureBytes and dies with the job on the main thread, installed
// or not.
struct GateDerivation {
	WalletPasscodeCheck check = WalletPasscodeCheck::Vault;
	std::optional<Storage::PasscodeDerivation> keyData;
	std::optional<VaultWrap> vaultWrap;
	SecureBytes passcode;
	std::optional<SecureBytes> vaultKey;
	bool vaultOpens = false;

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
		auto key = std::optional<SecureBytes>();
		if (const auto wrapKey = DeriveVaultWrapKey(*vaultWrap, utf8)) {
			key = UnwrapVaultKey(*vaultWrap, *wrapKey);
		}
		vaultOpens = key.has_value();
		if (check == WalletPasscodeCheck::Vault) {
			vaultKey = std::move(key);
		}
	} else {
		vaultOpens = (check == WalletPasscodeCheck::KeyDataAndVault);
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

// The one function in the product that destroys a wallet vault, and what it
// destroys is not recoverable from anywhere. The live session's cached key
// goes first, so nothing can seal a new record under a key that is about to
// stop existing; then every secret the custody store names, then the store
// itself, then the header. A store that cannot be read still loses its
// header - a vault must never survive as a file nothing can open again. The
// answer is about the cleanup alone: a false says leftovers remain, not that
// the vault survived, and the caller decides the passcode's fate by looking
// at the vaults themselves. Only counts reach the log.
[[nodiscard]] bool DropVaultAndCustody(not_null<Main::Account*> account) {
	auto &local = account->local();
	const auto session = account->maybeSession();
	if (session) {
		session->wallet().vault().clear();
	}
	auto ok = true;
	auto store = ReadCustodyStore(local);
	if (!store) {
		LOG(("Wallet Error: custody store unreadable while dropping a vault "
			"after a forgotten passcode."));
		ok = false;
	} else {
		auto removed = 0;
		ForEachCustodySecretRef(*store, [&](const QString &secretRef) {
			if (local.removeWalletEngineValue(
					VaultSecretStorageKey(secretRef))) {
				++removed;
			}
			return false;
		});
		const auto emptied = CustodyStore{
			.lastSeenServerKey = store->lastSeenServerKey,
		};
		if (!WriteCustodyStore(local, emptied)) {
			LOG(("Wallet Error: could not empty the custody store after a "
				"forgotten passcode, %1 sealed values were removed."
				).arg(removed));
			ok = false;
		}
	}
	if (!RemoveVaultHeader(local)) {
		LOG(("Wallet Error: could not remove the vault header after a "
			"forgotten passcode."));
		ok = false;
	}
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
// which is a second enumeration and not the cleanup's answer, because a
// custody store that could not be emptied is not a header that survived, and
// treating the two as one verdict would leave a passcode nobody remembers
// standing over nothing it can open. The mirror order is the one that strands
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
	auto &settings = Core::App().settings();
	// Every vault this passcode could still be asked for is gone by here, so
	// its removal is owed from this point on whatever the write does. The flag
	// is recorded before the write is attempted and is dropped only once
	// hasPasscode() has answered, so what it covers is a checked write that
	// did not reach the disk and a crash inside that write. A crash between
	// the last DropVaultAndCustody() and this line is not covered: the record
	// sits below the second enumeration so that the surviving-vault refusal
	// above never writes the flag at all, and that placement is what leaves
	// the enumeration's own window outside it. saveSettings() rather than the
	// delayed timer because it queues the bytes now instead of a second from
	// now; it cannot prove they reached the disk, and a settings write lost as
	// well leaves exactly today's outcome.
	settings.writePref<bool>(kForgottenPasscodeClearKey, true);
	Core::App().saveSettings();
	local.clearPasscodeAfterReset();
	if (local.hasPasscode()) {
		show->showToast(tr::lng_wallet_passcode_forgot_later(tr::now));
		return;
	}
	settings.clearPref(kForgottenPasscodeClearKey);
	settings.setSystemUnlockEnabled(false);
	Core::App().saveSettingsDelayed();
	Core::App().localPasscodeChanged();
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

	// Only show and the caller's own callback are captured, so the ladder is
	// safe to invoke long after the box that acquired it is gone. The header
	// is re-read here because ShowKeyProtectionBox refuses Install over a
	// vault that already reads, and because the state may have moved between
	// the acquisition and the store.
	return [show](Fn<void(CustodyInstall)> ready) {
		using State = VaultReading::State;
		const auto answer = [ready](CustodyInstall install) {
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
		switch (reading.state) {
		case State::Absent:
			ShowKeyProtectionBox(show, {
				.mode = KeyProtectionMode::Install,
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
			return;
		case State::Read:
			UnlockByKind(
				show,
				std::move(reading),
				true,
				[answer](VaultAuthorization grant) {
					if (!grant) {
						answer({});
					} else {
						answer({ .grant = std::move(grant) });
					}
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

void FinishForgottenPasscodeClear(bool openedWithoutPasscode) {
	auto &settings = Core::App().settings();
	if (!settings.readPref<bool>(kForgottenPasscodeClearKey, false)) {
		return;
	}
	auto &local = Core::App().domain().local();
	// The flag records an owed removal, never permission to perform it.
	// !appLockEnabled() means the current committed open wrap was proved
	// to recover this local key. openedWithoutPasscode is a separate
	// fact: this process actually started with the empty passcode, captured
	// by Main::Domain::startWith(). Turning the lock off after a typed start
	// cannot supply that proof. Recheck verified locking and fresh vault
	// dependencies because either can have changed since the flag was set.
	// A failed proof, a missing passcode, a lock turned on or a surviving
	// dependent clears the flag; keeping it until those conditions change
	// would make it a standing permission.
	if (!openedWithoutPasscode
		|| !local.hasPasscode()
		|| local.appLockEnabled()
		|| !CollectVaultDependents().passcodeWrapped.empty()) {
		settings.clearPref(kForgottenPasscodeClearKey);
		Core::App().saveSettingsDelayed();
		return;
	}
	local.clearPasscodeAfterReset();
	if (local.hasPasscode()) {
		// The write failed again. The flag stays, and the start after this
		// one tries once more; nothing else in the state has moved.
		return;
	}
	settings.clearPref(kForgottenPasscodeClearKey);
	settings.setSystemUnlockEnabled(false);
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
	// passcode". KeyDataAndVault gates disable and change flows the user can
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
	const auto retain = (args.check != WalletPasscodeCheck::KeyDataAndVault);
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
	// A derivation in flight disables the field and the Submit button and
	// dims the button's label. defaultBoxButton paints no disabled state of
	// its own, so the label is its text colour at half alpha, the box-footer
	// idiom. The button is transparent to the mouse as well, but Enter in
	// the field and a key release on a focused button still reach the
	// handler, so submit itself refuses while busy.
	const auto setBusy = [=](bool busy) {
		state->busy = busy;
		field->setDisabled(busy);
		if (const auto button = state->submit.data()) {
			button->setDisabled(busy);
			button->setAttribute(Qt::WA_TransparentForMouseEvents, busy);
			button->setTextFgOverride(busy
				? std::make_optional(
					anim::with_alpha(button->st().textFg->c, 0.5))
				: std::nullopt);
		}
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
		job.vaultWrap = CommittedPasscodeWrap(session.local(), vault);
		if (args.check == WalletPasscodeCheck::KeyDataAndVault) {
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
			case WalletPasscodeCheck::KeyDataAndVault:
				ok = session.domain().local().checkPasscode(
					std::move(*job.keyData))
					&& job.vaultOpens;
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
