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
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/button_busy.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_session.h"

#include "styles/style_boxes.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kRetainedToastDuration = 4 * crl::time(1000);
constexpr auto kProblemToastDuration = 8 * crl::time(1000);

[[nodiscard]] VaultAuthorization Share(VaultGrant grant) {
	return grant.valid()
		? std::make_shared<VaultGrant>(std::move(grant))
		: nullptr;
}

[[nodiscard]] bool OpensWithoutPresence(const VaultRuntime &vault) {
	const auto reading = vault.reading();
	return (reading.state == KeyringReading::State::Read)
		&& (reading.keyring.wrap.kind == VaultKind::Open);
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

void ShowRetainedToast(
		std::shared_ptr<Main::SessionShow> show,
		not_null<ProtectionProvider*> provider) {
	using WeakToast = base::weak_ptr<Ui::Toast::Instance>;
	const auto toast = std::make_shared<WeakToast>();
	const auto weakSession = base::make_weak(&show->session());
	*toast = show->showToast({
		.text = tr::lng_wallet_protection_retained(
			tr::now,
			lt_provider,
			tr::marked(CurrentValue(provider->label())),
			lt_link,
			tr::link(tr::lng_wallet_protection_retained_ask(tr::now)),
			tr::marked),
		.filter = [=](const ClickHandlerPtr &, Qt::MouseButton button) {
			if (button != Qt::LeftButton) {
				return false;
			} else if (const auto session = weakSession.get()) {
				session->wallet().vault().endRetention();
			}
			if (const auto strong = toast->get()) {
				strong->hideAnimated();
			}
			return true;
		},
		.duration = kRetainedToastDuration,
	});
}

// Retention is a VaultRuntime property the user ticks inside the passcode
// box before it asks; a hardware provider's system sheet has no checkbox, so
// this box keeps the same question beside the sheet it raises as it opens.
// Every dismissal reaches boxClosing() and reports done once through the
// latch WalletPasscodeBox keeps; an ask that got an answer reports through
// the answer instead. The provider answers many main-thread turns later,
// possibly after this box or the whole panel is gone: such an answer is
// dropped whole - no toast, no flag, no report - because the close has
// already reported, and the sheet itself is system-owned and stays up until
// the user answers it. That is also why a sheet the user dismissed leaves
// this box standing with Retry instead of closing: the box is the only part
// of the ask this side owns. Nothing on the keyring's write path runs off
// the main thread: unlockWith() and grant() run inside the marshalled answer.
void HardwareUnlockBox(
		not_null<Ui::GenericBox*> box,
		HardwareUnlockArgs args) {
	struct State {
		// True from the first paint, so the sheet the box is about to raise
		// is never announced by a spinner that starts a frame later.
		rpl::variable<bool> asking = true;
		Fn<void()> ask;
		bool reported = false;
		bool busy = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto show = args.show;
	const auto provider = args.provider;
	const auto done = args.done;
	const auto weak = base::make_weak(box);
	const auto weakSession = base::make_weak(&show->session());
	const auto retry = [=] { state->ask(); };
	SetupSystemPromptBox(box, provider, state->asking.value(), retry);
	// WHY: the Windows Hello prompt blocks the window under it, so a checkbox
	// there cannot be unticked; the 15 minutes are granted and the toast
	// after the unlock offers to take them back.
	const auto remember = (provider->kind() == VaultKind::WindowsHello)
		? nullptr
		: box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_wallet_passcode_remember(tr::now),
				true,
				st::defaultBoxCheckbox),
			st::walletPasscodeCheckboxMargin);
	Ui::AddSkip(box->verticalLayout());
	const auto report = [=](VaultAuthorization grant) {
		state->reported = true;
		box->closeBox();
		done(std::move(grant));
	};
	const auto toast = [=](tr::phrase<lngtag_provider> phrase) {
		show->showToast(
			phrase(tr::now, lt_provider, CurrentValue(provider->label())),
			kProblemToastDuration);
	};
	const auto answered = [=](quint32 epoch, ProtectionUnwrapResult result) {
		if (!weak || !weakSession || !show->valid() || state->reported) {
			return;
		}
		state->busy = false;
		state->asking = false;
		auto &session = show->session();
		auto &vault = session.wallet().vault();
		// Not current(): it refuses while unusable, which this answer clears.
		if (vault.clearEpoch() != epoch
			|| session.account().maybeSession() != &session
			|| !CurrentVaultWrap(vault, args.wrap)) {
			report(nullptr);
			return;
		}
		const auto error = (result.error == ProtectionError::None
			&& (!result.key || result.key->size() != kVaultKeySize))
			? ProtectionError::Corrupt
			: result.error;
		switch (error) {
		case ProtectionError::Cancelled:
			// A dismissed sheet states nothing, so the box says it instead,
			// by standing where it was with Retry in place of the spinner.
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
		vault.setRetention(!remember || remember->checked());
		report(Share(vault.grant(session.uniqueId())));
		if (!remember && show->valid()) {
			ShowRetainedToast(show, provider);
		}
	};
	state->ask = [=] {
		if (state->busy || !weakSession || !show->valid()) {
			return;
		}
		auto &session = show->session();
		if (!CurrentVaultWrap(session.wallet().vault(), args.wrap)) {
			report(nullptr);
			return;
		}
		// The provider answers many main-thread turns after the ask, so a
		// clear trigger can land inside its prompt. The epoch is read here,
		// before the ask, and the runtime refuses a key that was opened
		// before that clear rather than undoing it.
		const auto epoch = session.wallet().vault().clearEpoch();
		state->busy = true;
		state->asking = true;
		provider->unwrap(&session.local(), args.wrap, [=](
				ProtectionUnwrapResult result) {
			answered(epoch, std::move(result));
		});
	};
	// A box shown over this one sends showFinished() again as it closes, so
	// only the first one asks: every later ask is the user's to make.
	box->showFinishes() | rpl::take(1) | rpl::on_next([=] {
		state->ask();
	}, box->lifetime());
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
		KeyringReading reading,
		Fn<void(VaultAuthorization)> done) {
	auto &session = show->session();
	auto &vault = session.wallet().vault();
	const auto wrap = &reading.keyring.wrap;
	// A wrap that reads but does not open is the same dead end a Broken
	// keyring is, so it states the same line; a dismissed prompt states
	// nothing, because the user already knows what they answered.
	const auto unavailable = [show, done] {
		show->showToast(tr::lng_wallet_vault_unavailable(tr::now));
		done(nullptr);
	};
	if (vault.retained()) {
		done(Share(vault.grant(session.uniqueId())));
		return;
	}
	// The shared retention window is checked before either protected kind.
	// Provider failures apply to the one live authority, so every account
	// becomes read-only together. A no-live installer bypasses this path
	// and never asks a stale factor to authorize its fresh keyring.
	if (quint32(wrap->kind) >= kFirstReservedVaultKind) {
		const auto provider = ProtectionProviderFor(wrap->kind);
		if (!provider) {
			session.wallet().setVaultKeyUnusable(true);
			unavailable();
			return;
		}
		UnlockWithProvider(show, provider, *wrap, done);
	} else if (wrap->kind == VaultKind::Open) {
		if (vault.unlockOpen()) {
			done(Share(vault.grant(session.uniqueId())));
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

} // namespace

bool VaultUnlockSilent(not_null<Main::Session*> session) {
	const auto &vault = session->wallet().vault();
	return vault.retained() || OpensWithoutPresence(vault);
}

VaultAuthorization AcquireSilentVaultUnlock(
		not_null<Main::Session*> session) {
	auto &vault = session->wallet().vault();
	if (vault.retained()
		|| (OpensWithoutPresence(vault) && vault.unlockOpen())) {
		return Share(vault.grant(session->uniqueId()));
	}
	return nullptr;
}

namespace {

// The vault wrap the typed passcode has to open, read on the main thread for
// the job that derives against it. It serves the Vault check alone, which
// UnlockByKind shows only over a keyring with a passcode wrap; a
// keyring that stopped reading or moved to another kind while the box was
// open yields no wrap here, so the job opens no vault key. The derived wrap
// key dies in the job.
[[nodiscard]] std::optional<VaultWrap> CommittedPasscodeWrap(
		VaultRuntime &vault) {
	const auto reading = vault.reading();
	if (reading.state != KeyringReading::State::Read
		|| reading.keyring.wrap.kind != VaultKind::Passcode) {
		return std::nullopt;
	}
	return reading.keyring.wrap;
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

// The shared policy applies to every signed-in wallet. This read-only
// display uses the existing logout-loss model for names and backup warnings;
// it does not open factors or decide liveness. A custody store with no
// records contributes no paragraph, so absence is never a backup claim.
[[nodiscard]] QString ForgottenPasscodeAbout() {
	auto result = tr::lng_wallet_passcode_forgot_about(tr::now);
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (!account->sessionExists()) {
			continue;
		}
		const auto loss = WalletLossOnLogout(account.get());
		auto paragraph = WalletLossWarning(loss);
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

void DropForgottenPasscode(std::shared_ptr<Main::SessionShow> show) {
	if (!show->valid()) {
		return;
	}
	show->session().wallet().resetCustodyAfterForgottenPasscode([show](
			CustodyResetResult result) {
		if (result == CustodyResetResult::Refused || !show->valid()) {
			return;
		} else if (result == CustodyResetResult::Failed) {
			show->showToast(tr::lng_wallet_passcode_forgot_failed(tr::now));
			return;
		}
		show->showToast(tr::lng_wallet_passcode_forgot_done(tr::now));
	});
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
		.title = tr::lng_wallet_passcode_forgot(),
	}));
}

} // namespace

bool KeyAuthorization::valid() const {
	return (grant != nullptr) || (install != nullptr);
}

void AcquireVaultUnlock(VaultUnlockArgs args) {
	Expects(args.show != nullptr);

	using State = KeyringReading::State;
	const auto show = args.show;
	const auto mayInstall = args.mayInstall;
	const auto done = std::move(args.done);
	const auto answer = [=](KeyAuthorization result) {
		if (done) {
			done(std::move(result));
		}
	};
	if (!show->valid()) {
		answer({});
		return;
	}
	const auto weakSession = base::make_weak(&show->session());
	const auto acquired = [=](VaultAuthorization grant) {
		if (!weakSession || !show->valid() || !grant) {
			answer({});
			return;
		}
		auto result = KeyAuthorization{ .grant = std::move(grant) };
		if (mayInstall) {
			result.install = MakeCustodyInstaller(show, result.grant);
		}
		answer(std::move(result));
	};
	auto &session = show->session();
	auto &vault = session.wallet().vault();
	auto reading = vault.reading();
	if (mayInstall && (!vault.hasLiveEntries() || vault.unusable())) {
		answer(KeyAuthorization{ .install = MakeCustodyInstaller(show) });
		return;
	}
	switch (reading.state) {
	case State::Read:
		UnlockByKind(show, std::move(reading), acquired);
		return;
	case State::Absent:
		if (mayInstall) {
			answer(KeyAuthorization{
				.install = MakeCustodyInstaller(show),
			});
		} else {
			show->showToast(tr::lng_wallet_vault_locked(tr::now));
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
		std::shared_ptr<Main::SessionShow> show,
		VaultAuthorization authorization) {
	Expects(show != nullptr);

	const auto weakSession = base::make_weak(&show->session());
	return [show, weakSession, authorization](CustodyInstallRequest request) {
		struct State {
			Fn<void(CustodyInstall)> ready;
			std::optional<CustodyInstall> pending;
			bool resetting = false;
		};
		const auto state = std::make_shared<State>();
		state->ready = std::move(request.ready);
		const auto answer = [state](CustodyInstall install) {
			if (state->resetting) {
				state->pending = std::move(install);
			} else if (auto ready = base::take(state->ready)) {
				ready(std::move(install));
			}
		};
		if (!weakSession || !show->valid()) {
			answer({});
			return;
		}
		auto &vault = weakSession->wallet().vault();
		auto reading = vault.reading();
		const auto live = reading.state == KeyringReading::State::Read
			&& vault.hasLiveEntries(reading.keyring);
		const auto install = [=](
				Fn<void(std::optional<quint32>, Fn<void(bool)>)> reset) {
			ShowKeyProtectionBox(show, {
				.mode = KeyProtectionMode::Install,
				.passcodeCreated = request.passcodeCreated,
				.resetUnusableVault = std::move(reset),
				.done = [answer](KeyProtectionResult result) {
					answer({
						.grant = (!result.cancelled && !result.failed)
							? Share(std::move(result.grant))
							: nullptr,
						.passcodeCreatedFromEpoch = result.passcodeCreatedFromEpoch,
					});
				},
			});
		};
		if (!live) {
			install(nullptr);
		} else if (vault.unusable()) {
			const auto reset = request.resetUnusableVault;
			if (!reset) {
				answer({});
				return;
			}
			install([=](
					std::optional<quint32> createdFromEpoch,
					Fn<void(bool)> done) {
				state->resetting = true;
				reset(createdFromEpoch, [=](CustodyResetResult result) {
					if (result == CustodyResetResult::Failed
						&& weakSession && show->valid()) {
						show->showToast(
							tr::lng_wallet_passcode_forgot_failed(tr::now));
					}
					done(result == CustodyResetResult::Done);
					state->resetting = false;
					if (auto pending = base::take(state->pending)) {
						answer(std::move(*pending));
					}
				});
			});
		} else if (reading.keyring.wrap.kind == VaultKind::Open) {
			install(nullptr);
		} else if (authorization && authorization->valid()) {
			answer({ .grant = authorization });
		} else {
			UnlockByKind(show, std::move(reading), [answer](
					VaultAuthorization grant) {
				answer({ .grant = std::move(grant) });
			});
		}
	};
}

void DropUnusedPasscode() {
	auto &local = Core::App().domain().local();
	if (!local.hasPasscode()
		|| local.appLockEnabled()
		|| LiveKeyProtection() == VaultKind::Passcode) {
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
	const auto weakSession = base::make_weak(&args.show->session());
	box->setStyle(st::walletPillBox);
	box->setNoContentMargin(true);
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	auto icon = Settings::CreateLottieIcon(
		box->verticalLayout(),
		{
			.name = u"local_passcode_enter"_q,
			.sizeOverride = st::normalBoxLottieSize,
		},
		st::walletPasscodeLottieMargin);
	box->verticalLayout()->add(std::move(icon.widget));
	box->showFinishes() | rpl::on_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	}, box->lifetime());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_passcode_check_title(),
			st::walletPhraseTitleLabel),
		st::boxRowPadding,
		style::al_top);

	// The field, the checkbox and the button share the button's side skips.
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
		field->resize(width, field->height());
		field->moveToLeft(0, 0);
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
	// passcode". KeyData verifies a passcode choice or passcode change;
	// dropping custody from inside that gate would race its continuation.
	// Verified app-lock-off proves this local key can open without a passcode;
	// otherwise the link would offer removal of
	// a launch-lock passcode nobody had to prove anything for.
	const auto forgot = (args.check == WalletPasscodeCheck::Vault
		&& !args.show->session().domain().local().appLockEnabled())
		? box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_wallet_passcode_forgot(tr::link),
				st::defaultFlatLabel),
			st::walletPasscodeForgotMargin,
			style::al_top)
		: nullptr;
	if (forgot) {
		const auto weak = base::make_weak(box);
		forgot->overrideLinkClickHandler([show = args.show, weak] {
			ConfirmForgottenPasscode(show, [weak] {
				if (weak) {
					weak->closeBox();
				}
			});
		});
	}
	// A KeyData check proves only the typed bytes, so it offers no retention
	// and mints no grant. Opening D remains a distinct check against its
	// own wrap, even when the chooser can use those bytes without another
	// prompt. Only the Vault check offers device-wide retention.
	const auto retain = (args.check != WalletPasscodeCheck::KeyData);
	const auto remember = retain
		? box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_wallet_passcode_remember(tr::now),
				true,
				st::defaultBoxCheckbox),
			st::walletPasscodeCheckboxMargin)
		: nullptr;
	if (remember) {
		Ui::AddSkip(box->verticalLayout());
	}
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
		if (state->busy || !weakSession || !args.show->valid()) {
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
			job.vaultWrap = CommittedPasscodeWrap(vault);
		} else {
			job.keyData = session.domain().local().prepareOpen(utf8);
		}
		const auto epoch = vault.clearEpoch();
		setBusy(true);
		Storage::DeriveOnWorker(std::move(job), crl::guard(box, [=](
				GateDerivation &&job) {
			if (!weakSession || !args.show->valid() || state->reported) {
				return;
			}
			setBusy(false);
			auto &session = args.show->session();
			auto &vault = session.wallet().vault();
			auto ok = false;
			auto refused = (vault.clearEpoch() != epoch);
			switch (args.check) {
			case WalletPasscodeCheck::Vault:
				refused = refused
					|| !vault.current(session.uniqueId(), epoch)
					|| !job.vaultWrap
					|| !CurrentVaultWrap(vault, *job.vaultWrap);
				if (!refused && job.vaultKey) {
					ok = vault.unlockWith(std::move(*job.vaultKey), epoch);
					refused = !ok;
				}
				break;
			case WalletPasscodeCheck::KeyData:
				ok = !refused && job.keyData
					&& session.domain().local().checkPasscode(
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
				gate.grant = vault.grant(session.uniqueId());
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
	// Close, Escape and the layer being replaced all reach closeHook(), so
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
