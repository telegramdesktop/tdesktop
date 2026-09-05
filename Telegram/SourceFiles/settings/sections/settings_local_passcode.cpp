/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_local_passcode.h"

#include "base/platform/base_platform_last_input.h"
#include "base/platform/base_platform_info.h"
#include "base/debug_log.h"
#include "base/openssl_help.h"
#include "base/system_unlock.h"
#include "boxes/auto_lock_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/session/session_show.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_step.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "storage/storage_domain.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/passcode_strength_meter.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_vault.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_passcode_strength_meter.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

[[nodiscard]] Storage::SetPasscodeResult SetPasscode(
		not_null<Window::SessionController*> controller,
		const QString &pass,
		Storage::PasscodeVerification verification) {
	cSetPasscodeBadTries(0);
	const auto result = controller->session().domain().local().setPasscode(
		pass.toUtf8(),
		verification);
	if (result == Storage::SetPasscodeResult::Success) {
		Core::App().localPasscodeChanged();
	}
	return result;
}

[[nodiscard]] Storage::SetPasscodeResult SetPasscode(
		not_null<Window::SessionController*> controller,
		Storage::PasscodeDerivation derived,
		Storage::PasscodeVerification verification) {
	cSetPasscodeBadTries(0);
	const auto result = controller->session().domain().local().setPasscode(
		std::move(derived),
		verification);
	if (result == Storage::SetPasscodeResult::Success) {
		Core::App().localPasscodeChanged();
	}
	return result;
}

// The proof that the old passcode was typed is minted by the Check section
// and consumed by the Change section and by the Manage disable button, which
// are different section objects created later by the navigation stack. It
// travels in the std::any the Info controller keeps for the whole settings
// stack, the same channel CloudPassword::StepData uses, but only for the one
// navigation hop that hands it over: the receiving section moves it out of
// the std::any into a member of its own, and writes it back only when it is
// itself opening another passcode section. So the proof lives exactly as long
// as the section holding it - leaving by hand, by Back, by removeFromStack or
// by the auto-close timer destroys that section and takes the proof with it -
// instead of trailing the whole settings stack, and a Manage section reached
// straight from settings search finds nothing and asks for the passcode.
[[nodiscard]] std::optional<Storage::PasscodeVerification> TakeVerification(
		std::any *stepData) {
	if (!stepData || !stepData->has_value()) {
		return std::nullopt;
	}
	const auto my = std::any_cast<Storage::PasscodeVerification>(stepData);
	if (!my) {
		return std::nullopt;
	}
	auto result = *my;
	*stepData = std::any();
	return result;
}

void WriteVerification(
		std::any *stepData,
		const std::optional<Storage::PasscodeVerification> &verification) {
	if (stepData && verification) {
		*stepData = *verification;
	}
}

// One dependent vault held between StageVaultWrap() and
// CommitStagedVaultWrap(): the account it belongs to, the header that now
// carries both wraps at the unchanged committed generation, and the header
// that was on disk before the stage, which is what a rollback writes back.
// The account pointer is valid only for the frame that stages, rewrites
// key_data and commits, and that frame is one synchronous callback - the one
// that receives the derived batch back from the worker.
struct StagedVault {
	not_null<Main::Account*> account;
	Wallet::VaultHeader staged;
	Wallet::VaultHeader previous;
};

struct VaultSnapshot {
	base::weak_ptr<Main::Account> account;
	Wallet::VaultWrap committed;
};

struct DerivedVault {
	base::weak_ptr<Main::Account> account;
	Wallet::VaultWrap committed;
	Wallet::SecureBytes vaultKey;
	Wallet::VaultPreparedWrap prepared;
};

// The memory-hard part of the staged change, cut out as one worker job: the
// key_data wrap for the new passcode and, per dependent vault, the old
// passcode's wrap key, the vault key it opens and the new passcode's wrap.
// It holds values only - a weak account pointer that is never dereferenced
// off the main thread, a copy of the committed wrap it derives against, the
// two typed passcodes as SecureBytes - and touches no store, so nothing is
// written before every derivation has answered. run() cleanses both typed
// passcodes on every exit; the vault keys and wrap keys it produced live in
// SecureBytes and die with the batch on the main thread, applied or not.
struct ChangeDerivation {
	Storage::PasscodeDerivation keyData;
	std::vector<VaultSnapshot> vaults;
	Wallet::SecureBytes oldPasscode;
	Wallet::SecureBytes newPasscode;
	std::vector<DerivedVault> derived;
	bool failed = false;

	void run();
};

// Rewrites the pre-stage header of every vault that was already staged. A
// write that fails here is still safe: the staged wrap lives outside the
// committed generation, so the next ReadVaultHeader() drops it and
// ReconcileVaultHeader() persists that drop, and the old passcode keeps
// opening the vault either way. Kinds and generations alone are logged.
void RollbackStagedVaults(const std::vector<StagedVault> &vaults) {
	for (const auto &vault : vaults) {
		if (!Wallet::WriteVaultHeader(
				vault.account->local(),
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
		const auto reading = Wallet::ReadVaultHeader(account->local());
		const auto committed = reading.header.committedWrap();
		if (reading.state != Wallet::VaultReading::State::Read
			|| !committed
			|| committed->kind != Wallet::VaultKind::Passcode) {
			return std::nullopt;
		}
		result.push_back({
			.account = base::make_weak(account),
			.committed = *committed,
		});
	}
	return result;
}

void ChangeDerivation::run() {
	auto oldUtf8 = QByteArray(
		reinterpret_cast<const char*>(oldPasscode.span().data()),
		oldPasscode.size());
	auto newUtf8 = QByteArray(
		reinterpret_cast<const char*>(newPasscode.span().data()),
		newPasscode.size());
	const auto cleanse = gsl::finally([&] {
		if (!oldUtf8.isEmpty()) {
			OPENSSL_cleanse(oldUtf8.data(), oldUtf8.size());
		}
		if (!newUtf8.isEmpty()) {
			OPENSSL_cleanse(newUtf8.data(), newUtf8.size());
		}
		oldPasscode.clear();
		newPasscode.clear();
	});
	derived.reserve(vaults.size());
	for (auto &vault : vaults) {
		const auto wrapKey = Wallet::DeriveVaultWrapKey(
			vault.committed,
			oldUtf8);
		if (!wrapKey) {
			failed = true;
			return;
		}
		auto vaultKey = Wallet::UnwrapVaultKey(vault.committed, *wrapKey);
		if (!vaultKey) {
			failed = true;
			return;
		}
		auto prepared = Wallet::PrepareVaultPasscodeWrap(newUtf8);
		if (!prepared) {
			failed = true;
			return;
		}
		derived.push_back({
			.account = std::move(vault.account),
			.committed = std::move(vault.committed),
			.vaultKey = std::move(*vaultKey),
			.prepared = std::move(*prepared),
		});
	}
	keyData.run();
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
	const auto current = Wallet::CollectVaultDependents().passcodeWrapped;
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
// its committed one and proved by a read-back, while the committed
// generation does not move: the old passcode still opens every vault, and
// key_data is untouched, so it still opens the launch lock too. A vault
// whose committed wrap is no longer the one the worker derived against - by
// generation, kind or salt; every committed change bumps the generation -
// abandons the change, and so does any write failure: both roll back what
// this staged and answer nothing at all, which leaves the old passcode the
// passcode everywhere. An account that is gone is skipped with its vault.
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
		auto reading = Wallet::ReadVaultHeader(account->local());
		if (reading.state != Wallet::VaultReading::State::Read) {
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
		const auto session = account->maybeSession();
		if (session && session->wallet().custodyBusy()) {
			return abandon();
		}
		const auto staged = Wallet::StageVaultWrap(
			account->local(),
			header,
			vault.vaultKey,
			std::move(vault.prepared));
		if (staged != Wallet::VaultTransitionResult::Done) {
			return abandon();
		}
		result.push_back({
			.account = account,
			.staged = std::move(header),
			.previous = previous,
		});
	}
	return result;
}

// Step three: the old wrap is stripped and the committed generation bumped,
// account by account in the order they were staged. A failure here is not a
// rollback point - key_data already answers to the new passcode, and a vault
// that did not commit still carries the wrap the old one opens - so the
// caller reports it and finishes the change.
[[nodiscard]] bool CommitStagedVaults(std::vector<StagedVault> &vaults) {
	auto result = true;
	for (auto &vault : vaults) {
		const auto session = vault.account->maybeSession();
		if (!Wallet::CommitStagedVaultWrap(
				vault.account->local(),
				vault.staged,
				session ? &session->wallet().vault() : nullptr)) {
			result = false;
		} else if (session) {
			session->wallet().notifyKeyProtectionChanged();
		}
	}
	return result;
}

} // namespace

namespace details {

class LocalPasscodeEnter : public AbstractSection {
public:
	enum class EnterType {
		Create,
		Check,
		Change,
	};

	LocalPasscodeEnter(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LocalPasscodeEnter();

	void showFinished() override;
	void setInnerFocus() override;
	[[nodiscard]] rpl::producer<Type> sectionShowOther() override;
	[[nodiscard]] rpl::producer<> sectionShowBack() override;

	[[nodiscard]] rpl::producer<QString> title() override;

	void setStepDataReference(std::any &data) override;

protected:
	void setupContent();

	[[nodiscard]] virtual EnterType enterType() const = 0;

private:
	rpl::event_stream<> _showFinished;
	rpl::event_stream<> _setInnerFocus;
	rpl::event_stream<Type> _showOther;
	rpl::event_stream<> _showBack;
	std::any *_stepData = nullptr;
	std::optional<Storage::PasscodeVerification> _verification;
	bool _systemUnlockWithBiometric = false;
	bool _deriving = false;

};

LocalPasscodeEnter::LocalPasscodeEnter(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractSection(parent, controller) {
}

rpl::producer<QString> LocalPasscodeEnter::title() {
	return tr::lng_settings_passcode_title();
}

void LocalPasscodeEnter::setStepDataReference(std::any &data) {
	// TypedLocalPasscodeEnter builds the content from its constructor, so
	// this runs after setupContent(). The button handler reads _verification
	// when it is pressed, which is always later than this call.
	_stepData = &data;
	_verification = TakeVerification(_stepData);
}

void LocalPasscodeEnter::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	base::SystemUnlockStatus(
		true
	) | rpl::on_next([=](base::SystemUnlockAvailability status) {
		_systemUnlockWithBiometric = status.available
			&& status.withBiometrics;
	}, lifetime());

	const auto isCreate = (enterType() == EnterType::Create);
	const auto isCheck = (enterType() == EnterType::Check);
	const auto isChange = (enterType() == EnterType::Change);
	const auto walletDependent = isChange
		&& !Wallet::CollectVaultDependents().passcodeWrapped.empty();

	auto icon = CreateLottieIcon(
		content,
		{
			.name = u"local_passcode_enter"_q,
			.sizeOverride = st::normalBoxLottieSize,
		},
		st::settingLocalPasscodeIconPadding);
	content->add(std::move(icon.widget));
	_showFinished.events(
	) | rpl::on_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	}, content->lifetime());

	if (isChange) {
		CloudPassword::SetupAutoCloseTimer(
			content->lifetime(),
			[=] { _showBack.fire({}); },
			[] { return Core::App().lastNonIdleTime(); });
	}

	Ui::AddSkip(content);

	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			isCreate
				? tr::lng_passcode_create_title()
				: isCheck
				? tr::lng_passcode_check_title()
				: tr::lng_passcode_change_title(),
			st::changePhoneTitle),
		st::changePhoneTitlePadding,
		style::al_top);

	const auto addDescription = [&](rpl::producer<QString> &&text) {
		const auto &st = st::settingLocalPasscodeDescription;
		content->add(
			object_ptr<Ui::FlatLabel>(content, std::move(text), st),
			st::changePhoneDescriptionPadding,
			style::al_top
		)->setTryMakeSimilarLines(true);
	};

	addDescription(tr::lng_passcode_about1());
	Ui::AddSkip(content);
	addDescription(tr::lng_passcode_about2());

	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto newPasscode = CloudPassword::AddPasswordField(
		content,
		isCreate
			? tr::lng_passcode_enter_first()
			: tr::lng_passcode_enter(),
		QString());

	const auto meter = walletDependent
		? [&] {
			auto object = object_ptr<Ui::PasscodeStrengthMeter>(
				content,
				st::defaultPasscodeStrengthMeter);
			object->setNaturalWidth(st::settingLocalPasscodeInputField.width);
			return content->add(
				std::move(object),
				st::settingLocalPasscodeMeterPadding,
				style::al_top);
		}()
		: nullptr;

	const auto reenterPasscode = isCheck
		? (Ui::PasswordInput*)(nullptr)
		: CloudPassword::AddPasswordField(
			content,
			tr::lng_passcode_confirm_new(),
			QString()).get();
	const auto error = CloudPassword::AddError(
		content,
		isCheck ? newPasscode.get() : reenterPasscode);
	error->setText(tr::lng_language_name(tr::now));

	if (meter) {
		connect(newPasscode, &Ui::MaskedInputField::changed, [=] {
			meter->showCandidate(newPasscode->text());
			error->hide();
		});
	}

	const auto button = content->add(
		object_ptr<Ui::RoundButton>(
			content,
			(isCreate
				? tr::lng_passcode_create_button()
				: isCheck
				? tr::lng_passcode_check_button()
				: tr::lng_passcode_change_button()),
			st::changePhoneButton),
		st::settingLocalPasscodeButtonPadding,
		style::al_top);

	// A derivation in flight disables both fields and the button and dims the
	// button's label in its style's own secondary colour. The button is made
	// transparent to the mouse as well, but the Enter path calls clicked()
	// directly, so the click handler itself refuses while _deriving is set.
	const auto setDeriving = [=](bool deriving) {
		_deriving = deriving;
		newPasscode->setDisabled(deriving);
		if (reenterPasscode) {
			reenterPasscode->setDisabled(deriving);
		}
		button->setDisabled(deriving);
		button->setAttribute(Qt::WA_TransparentForMouseEvents, deriving);
		button->setTextFgOverride(deriving
			? std::make_optional(st::changePhoneButton.numbersTextFg->c)
			: std::nullopt);
		if (!deriving) {
			newPasscode->setFocus();
		}
	};
	const auto showFieldError = [=](const QString &text) {
		newPasscode->setFocus();
		newPasscode->showError();
		error->show();
		error->setText(text);
	};

	// The three steps of the staged change, in the only order that keeps one
	// typed string opening every store: stage the new wrap beside the
	// committed one in each dependent vault, rewrite key_data, and only then
	// strip the old wraps. Every derivation those steps need - per vault the
	// old passcode's wrap key and the new passcode's wrap, plus the new
	// key_data wrap - runs first, on a worker, as one ChangeDerivation batch,
	// so nothing is written before all of them have answered. A vault that
	// became passcode-wrapped while that batch ran, or while the no-vault leg
	// derived its wrap, is in neither the derivation nor the change, so both
	// legs re-enumerate the dependents first and abandon on any difference,
	// before anything is staged - a second Save then takes the path the new
	// set needs. Nothing may read a vault between the stage and the commit -
	// a staged header is dirty, and ReconcileVaultHeader() or
	// VaultRuntime::reading() would write the rollback back and silently undo
	// the stage - so once the batch is back all three run synchronously here,
	// with no box, no toast and no rpl hop between them.
	//
	// Between step two and step three the new passcode opens the launch lock
	// while a vault may still answer only to the old one. That window spans
	// the per-account write-B calls alone and loses nothing: the wrap that
	// still opens such a vault is the one the user typed moments earlier. The
	// mirror order, committing the vaults before key_data, only trades it for
	// a window where the launch lock wants the old passcode while a vault
	// wants the new one. A crash inside step one, or between steps one and
	// two, leaves key_data untouched and every committed wrap the old one, so
	// the old passcode opens both roles and the next header read drops the
	// staged wrap by generation; a crash inside step two is completed or
	// rolled back by setPasscode()'s own generation reconciliation at the
	// next start, with every vault still on the old wrap.
	const auto applyStagedChange = [=](
			ChangeDerivation &&batch,
			Storage::PasscodeVerification verification) {
		auto vaults = (batch.failed || !SameDependents(batch.derived))
			? std::nullopt
			: StageDerivedVaults(batch.derived);
		if (!vaults) {
			showFieldError(tr::lng_wallet_protection_error(tr::now));
			return;
		}
		const auto result = SetPasscode(
			controller(),
			std::move(batch.keyData),
			verification);
		if (result != Storage::SetPasscodeResult::Success) {
			RollbackStagedVaults(*vaults);
			if (result == Storage::SetPasscodeResult::NeedsVerification) {
				_showOther.fire(LocalPasscodeCheckId());
			} else {
				showFieldError(Lang::Hard::SecureSaveError());
			}
			return;
		}
		if (!CommitStagedVaults(*vaults)) {
			controller()->showToast(
				tr::lng_wallet_protection_error(tr::now));
		}
		_showBack.fire({});
	};
	const auto runStagedChange = [=](
			const QString &newText,
			Storage::PasscodeVerification verification,
			Wallet::SecureBytes passcode) {
		auto snapshots = SnapshotDependentVaults(
			Wallet::CollectVaultDependents().passcodeWrapped);
		if (!snapshots) {
			setDeriving(false);
			showFieldError(tr::lng_wallet_protection_error(tr::now));
			return;
		}
		auto newUtf8 = newText.toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!newUtf8.isEmpty()) {
				OPENSSL_cleanse(newUtf8.data(), newUtf8.size());
			}
		});
		const auto &local = controller()->session().domain().local();
		Storage::DeriveOnWorker(
			ChangeDerivation{
				.keyData = local.prepareNewWrap(newUtf8),
				.vaults = std::move(*snapshots),
				.oldPasscode = std::move(passcode),
				.newPasscode = Wallet::SecureBytes(newUtf8),
			},
			crl::guard(this, [=](ChangeDerivation &&batch) {
				setDeriving(false);
				applyStagedChange(std::move(batch), verification);
			}));
	};

	// The change screen holds only the new passcode, so the old one is asked
	// for through the same gate ShowKeyProtectionBox() opens for every one of
	// its modes: it checks the typed bytes against key_data and against the
	// vault wrap, retains nothing, mints no grant and leaves the token this
	// screen holds unspent. A dismissal writes nothing anywhere and only lifts
	// the busy state. The box outlives this frame and the section has an
	// auto-close timer, so both answers are guarded against the section being
	// gone.
	const auto changeWithWalletVaults = [=](
			const QString &newText,
			Storage::PasscodeVerification verification) {
		const auto weak = base::make_weak(this);
		controller()->show(Box(
			Wallet::WalletPasscodeBox,
			Wallet::WalletPasscodeBoxArgs{
				.show = Main::MakeSessionShow(
					controller()->uiShow(),
					&controller()->session()),
				.check = Wallet::WalletPasscodeCheck::KeyDataAndVault,
				.passed = [=](Wallet::WalletPasscodeGate gate) {
					if (weak) {
						runStagedChange(
							newText,
							verification,
							std::move(gate.passcode));
					}
				},
				.cancelled = crl::guard(this, [=] { setDeriving(false); }),
			}));
	};

	const auto deriveAndSave = [=](
			const QString &newText,
			Storage::PasscodeVerification verification) {
		auto utf8 = newText.toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!utf8.isEmpty()) {
				OPENSSL_cleanse(utf8.data(), utf8.size());
			}
		});
		const auto &local = controller()->session().domain().local();
		Storage::DeriveOnWorker(
			local.prepareNewWrap(utf8),
			crl::guard(this, [=](Storage::PasscodeDerivation &&derived) {
				setDeriving(false);
				if (isChange) {
					const auto dependents = Wallet::CollectVaultDependents();
					if (!dependents.passcodeWrapped.empty()) {
						showFieldError(
							tr::lng_wallet_protection_error(tr::now));
						return;
					}
				}
				const auto result = SetPasscode(
					controller(),
					std::move(derived),
					verification);
				if (result == Storage::SetPasscodeResult::NeedsVerification) {
					_showOther.fire(LocalPasscodeCheckId());
					return;
				} else if (result != Storage::SetPasscodeResult::Success) {
					showFieldError(Lang::Hard::SecureSaveError());
					return;
				}
				if (isCreate) {
					if (Platform::IsWindows() || _systemUnlockWithBiometric) {
						Core::App().settings().setSystemUnlockEnabled(true);
						Core::App().saveSettingsDelayed();
					}
					_showOther.fire(LocalPasscodeManageId());
				} else if (isChange) {
					_showBack.fire({});
				}
			}));
	};

	const auto checkAndChange = [=](const QString &newText) {
		auto utf8 = newText.toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!utf8.isEmpty()) {
				OPENSSL_cleanse(utf8.data(), utf8.size());
			}
		});
		const auto &local = controller()->session().domain().local();
		Storage::DeriveOnWorker(
			local.prepareOpen(utf8),
			crl::guard(this, [=](Storage::PasscodeDerivation &&derived) {
				const auto &domain = controller()->session().domain();
				if (domain.local().checkPasscode(std::move(derived))) {
					setDeriving(false);
					newPasscode->setFocus();
					newPasscode->showError();
					newPasscode->selectAll();
					error->show();
					error->setText(tr::lng_passcode_is_same(tr::now));
					return;
				} else if (!_verification) {
					setDeriving(false);
					_showOther.fire(LocalPasscodeCheckId());
					return;
				}
				const auto verification = *_verification;
				if (!Wallet::CollectVaultDependents().passcodeWrapped.empty()) {
					changeWithWalletVaults(newText, verification);
				} else {
					deriveAndSave(newText, verification);
				}
			}));
	};
	button->setClickedCallback([=] {
		if (_deriving) {
			return;
		}
		const auto newText = newPasscode->text();
		const auto reenterText = reenterPasscode
			? reenterPasscode->text()
			: QString();
		if (isCreate || isChange) {
			if (newText.isEmpty()) {
				newPasscode->setFocus();
				newPasscode->showError();
			} else if (reenterText.isEmpty()) {
				reenterPasscode->setFocus();
				reenterPasscode->showError();
			} else if (newText != reenterText) {
				reenterPasscode->setFocus();
				reenterPasscode->showError();
				reenterPasscode->selectAll();
				error->show();
				error->setText(tr::lng_passcode_differ(tr::now));
			} else {
				setDeriving(true);
				if (isCreate) {
					deriveAndSave(newText, {});
				} else {
					checkAndChange(newText);
				}
			}
		} else if (isCheck) {
			if (!passcodeCanTry()) {
				newPasscode->setFocus();
				newPasscode->showError();
				error->show();
				error->setText(tr::lng_flood_error(tr::now));
				return;
			}
			const auto &domain = controller()->session().domain();
			const auto verification = domain.local().verifyPasscode(
				newText.toUtf8());
			if (verification) {
				cSetPasscodeBadTries(0);
				WriteVerification(_stepData, verification);
				_showOther.fire(LocalPasscodeManageId());
			} else {
				cSetPasscodeBadTries(cPasscodeBadTries() + 1);
				cSetPasscodeLastTry(crl::now());

				newPasscode->selectAll();
				newPasscode->setFocus();
				newPasscode->showError();
				error->show();
				error->setText(tr::lng_passcode_wrong(tr::now));
			}
		}
	});

	const auto submit = [=] {
		if (!reenterPasscode || reenterPasscode->hasFocus()) {
			button->clicked({}, Qt::LeftButton);
		} else {
			reenterPasscode->setFocus();
		}
	};
	connect(newPasscode, &Ui::MaskedInputField::submitted, submit);
	if (reenterPasscode) {
		connect(reenterPasscode, &Ui::MaskedInputField::submitted, submit);
	}

	_setInnerFocus.events(
	) | rpl::on_next([=] {
		if (newPasscode->text().isEmpty()) {
			newPasscode->setFocus();
		} else if (reenterPasscode && reenterPasscode->text().isEmpty()) {
			reenterPasscode->setFocus();
		} else {
			newPasscode->setFocus();
		}
	}, content->lifetime());

	Ui::ResizeFitChild(this, content);
}

void LocalPasscodeEnter::showFinished() {
	_showFinished.fire({});
}

void LocalPasscodeEnter::setInnerFocus() {
	_setInnerFocus.fire({});
}

rpl::producer<Type> LocalPasscodeEnter::sectionShowOther() {
	return _showOther.events();
}

rpl::producer<> LocalPasscodeEnter::sectionShowBack() {
	return _showBack.events();
}

LocalPasscodeEnter::~LocalPasscodeEnter() = default;

} // namespace details

class LocalPasscodeCreate;
class LocalPasscodeCheck;
class LocalPasscodeChange;

template <typename SectionType>
class TypedLocalPasscodeEnter : public details::LocalPasscodeEnter {
public:
	TypedLocalPasscodeEnter(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: details::LocalPasscodeEnter(parent, controller) {
		setupContent();
	}

	[[nodiscard]] static Type Id() {
		return SectionFactory<SectionType>::Instance();
	}
	[[nodiscard]] Type id() const final override {
		return Id();
	}

protected:
	[[nodiscard]] EnterType enterType() const final override {
		if constexpr (std::is_same_v<SectionType, LocalPasscodeCreate>) {
			return EnterType::Create;
		}
		if constexpr (std::is_same_v<SectionType, LocalPasscodeCheck>) {
			return EnterType::Check;
		}
		if constexpr (std::is_same_v<SectionType, LocalPasscodeChange>) {
			return EnterType::Change;
		}
		return EnterType::Create;
	}

};

class LocalPasscodeCreate final
	: public TypedLocalPasscodeEnter<LocalPasscodeCreate> {
public:
	using TypedLocalPasscodeEnter::TypedLocalPasscodeEnter;

};

class LocalPasscodeCheck final
	: public TypedLocalPasscodeEnter<LocalPasscodeCheck> {
public:
	using TypedLocalPasscodeEnter::TypedLocalPasscodeEnter;

};

class LocalPasscodeChange final
	: public TypedLocalPasscodeEnter<LocalPasscodeChange> {
public:
	using TypedLocalPasscodeEnter::TypedLocalPasscodeEnter;

};

namespace {

enum class UnlockType {
	None,
	Default,
	Biometrics,
	Companion,
};

void BuildManageContent(SectionBuilder &builder) {
	const auto controller = builder.controller();
	if (!controller) {
		return;
	}

	const auto container = builder.container();

	struct State {
		rpl::event_stream<> autoLockBoxClosing;
		rpl::event_stream<bool> appLockToggles;
		rpl::variable<bool> appLockOn;
		rpl::variable<bool> walletDependent;
	};
	const auto state = container->lifetime().make_state<State>();
	const auto &local = controller->session().domain().local();

	// The app lock state follows the store and not the row, because the
	// removal box of the disable flow turns the lock off by itself, from an
	// object that cannot reach this state. appLockToggles carries only the
	// reverts a refused or dismissed toggle needs, and it reaches the row
	// alone: rpl::variable drops a value equal to the one it already holds,
	// and a revert is exactly the case where the store never moved.
	// Both the initial toggle and its committed-change refresh use verified
	// launch protection, so planted bytes cannot make the row say off.
	state->appLockOn = rpl::single(
		local.appLockEnabled()
	) | rpl::then(local.localPasscodeChanged() | rpl::map([] {
		return Core::App().domain().local().appLockEnabled();
	}));
	state->walletDependent = rpl::single(
		rpl::empty
	) | rpl::then(rpl::merge(
		local.localPasscodeChanged(),
		controller->session().wallet().keyProtectionUpdates())
	) | rpl::map([] {
		return !Wallet::CollectVaultDependents().passcodeWrapped.empty();
	});

	builder.addSkip();

	builder.addButton({
		.id = u"passcode/change"_q,
		.title = tr::lng_passcode_change(),
		.icon = { &st::menuIconLock },
		.onClick = [=] {
			builder.showOther()(LocalPasscodeChange::Id());
		},
		.keywords = { u"password"_q, u"code"_q },
	});

	const auto lockApp = builder.addButton({
		.id = u"passcode/lock-app"_q,
		.title = tr::lng_settings_passcode_lock_app(),
		.icon = { &st::menuIconLock },
		.toggled = rpl::merge(
			state->appLockOn.value(),
			state->appLockToggles.events()),
		.keywords = { u"lock"_q, u"launch"_q, u"startup"_q },
	});
	if (lockApp) {
		const auto weak = base::make_weak(container);

		// Storage::Domain::setAppLockEnabled() zeroes the verification nonce
		// in a gsl::finally on every exit, accepted or refused, so moving
		// this toggle spends the PasscodeVerification the check screen
		// minted. A change or a disable pressed afterwards then answers
		// NeedsVerification and routes back to that screen, which is correct
		// - but it is an outcome those flows have to report, not swallow.
		const auto apply = [=](bool enabled) {
			const auto &domain = Core::App().domain();
			const auto result = domain.local().setAppLockEnabled(enabled);
			if (result != Storage::SetPasscodeResult::Success) {
				state->appLockToggles.fire_copy(!enabled);
				controller->showToast(Lang::Hard::SecureSaveError());
				return false;
			}
			Core::App().localPasscodeChanged();
			return true;
		};
		lockApp->toggledChanges(
		) | rpl::filter([=](bool value) {
			// Compare verified locking so planted bytes cannot suppress
			// a change the user requested.
			return value != Core::App().domain().local().appLockEnabled();
		}) | rpl::on_next([=](bool value) {
			if (value) {
				apply(true);
				return;
			}
			const auto dependents = Wallet::CollectVaultDependents();
			if (!dependents.open.empty()) {
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_wallet_protection_open_warning_nolock(),
					.confirmed = [=](Fn<void()> &&close) {
						if (weak) {
							apply(false);
						}
						close();
					},
					.cancelled = [=](Fn<void()> &&close) {
						if (weak) {
							state->appLockToggles.fire_copy(true);
						}
						close();
					},
					.confirmText = (
						tr::lng_settings_passcode_lock_off_open_confirm()),
					.confirmStyle = &st::attentionBoxButton,
					.title = tr::lng_settings_passcode_lock_off_open_title(),
				}));
			} else if (!dependents.passcodeWrapped.empty()) {
				if (apply(false)) {
					controller->showToast(
						tr::lng_settings_passcode_lock_off_wallet(tr::now));
				}
			} else {
				apply(false);
			}
		}, lockApp->lifetime());
	}

	builder.scope([&] {
		auto autolockLabel = state->autoLockBoxClosing.events_starting_with(
			{}
		) | rpl::map([] {
			const auto autolock = Core::App().settings().autoLock();
			const auto hours = autolock / 3600;
			const auto minutes = (autolock - (hours * 3600)) / 60;

			return (hours && minutes)
				? tr::lng_passcode_autolock_hours_minutes(
					tr::now,
					lt_hours_count,
					QString::number(hours),
					lt_minutes_count,
					QString::number(minutes))
				: minutes
				? tr::lng_minutes(tr::now, lt_count, minutes)
				: tr::lng_hours(tr::now, lt_count, hours);
		});

		const auto autoLockButton = builder.addButton({
			.id = u"passcode/auto-lock"_q,
			.title = base::Platform::LastUserInputTimeSupported()
				? tr::lng_passcode_autolock_away()
				: tr::lng_passcode_autolock_inactive(),
			.icon = { &st::menuIconTimer },
			.label = std::move(autolockLabel),
			.keywords = { u"timeout"_q, u"lock"_q, u"time"_q },
		});
		if (autoLockButton) {
			autoLockButton->addClickHandler([=] {
				const auto box = controller->show(Box<AutoLockBox>());
				box->boxClosing(
				) | rpl::start_to_stream(
					state->autoLockBoxClosing,
					box->lifetime());
			});
		}

		builder.addSkip();

		using Divider = CloudPassword::OneEdgeBoxContentDivider;
		builder.add([](const WidgetContext &ctx) {
			const auto divider = Ui::CreateChild<Divider>(ctx.container.get());
			divider->lower();
			const auto about = ctx.container->add(
				object_ptr<Ui::PaddingWrap<>>(
					ctx.container,
					object_ptr<Ui::FlatLabel>(
						ctx.container,
						rpl::combine(
							tr::lng_passcode_about1(),
							tr::lng_passcode_about3()
						) | rpl::map([](const QString &s1, const QString &s2) {
							return s1 + "\n\n" + s2;
						}),
						st::boxDividerLabel),
				st::defaultBoxDividerLabelPadding));
			about->geometryValue(
			) | rpl::on_next([=](const QRect &r) {
				divider->setGeometry(r);
			}, divider->lifetime());
			return SectionBuilder::WidgetToAdd{};
		});

		builder.add([](const WidgetContext &ctx) {
			const auto systemUnlockWrap = ctx.container->add(
				object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
					ctx.container,
					object_ptr<Ui::VerticalLayout>(ctx.container))
			)->setDuration(0);
			const auto systemUnlockContent = systemUnlockWrap->entity();

			const auto unlockType = systemUnlockContent->lifetime().make_state<
				rpl::variable<UnlockType>
			>(base::SystemUnlockStatus(
				true
			) | rpl::map([](base::SystemUnlockAvailability status) {
				return status.withBiometrics
					? UnlockType::Biometrics
					: status.withCompanion
					? UnlockType::Companion
					: status.available
					? UnlockType::Default
					: UnlockType::None;
			}));

			const auto highlights = ctx.highlights;

			unlockType->value(
			) | rpl::on_next([=](UnlockType type) {
				while (systemUnlockContent->count()) {
					delete systemUnlockContent->widgetAt(0);
				}

				Ui::AddSkip(systemUnlockContent);

				const auto biometricsButton = AddButtonWithIcon(
					systemUnlockContent,
					(Platform::IsWindows()
						? tr::lng_settings_use_winhello()
						: (type == UnlockType::Biometrics)
						? tr::lng_settings_use_touchid()
						: (type == UnlockType::Companion)
						? tr::lng_settings_use_applewatch()
						: tr::lng_settings_use_systempwd()),
					st::settingsButton,
					{ Platform::IsWindows()
						? &st::menuIconWinHello
						: (type == UnlockType::Biometrics)
						? &st::menuIconTouchID
						: (type == UnlockType::Companion)
						? &st::menuIconAppleWatch
						: &st::menuIconSystemPwd });
				biometricsButton->toggleOn(
					rpl::single(Core::App().settings().systemUnlockEnabled())
				)->toggledChanges(
				) | rpl::filter([=](bool value) {
					return value
						!= Core::App().settings().systemUnlockEnabled();
				}) | rpl::on_next([=](bool value) {
					Core::App().settings().setSystemUnlockEnabled(value);
					Core::App().saveSettingsDelayed();
				}, systemUnlockContent->lifetime());

				if (highlights) {
					highlights->push_back({
						u"passcode/biometrics"_q,
						{ biometricsButton.get() },
					});
				}

				Ui::AddSkip(systemUnlockContent);

				Ui::AddDividerText(
					systemUnlockContent,
					(Platform::IsWindows()
						? tr::lng_settings_use_winhello_about()
						: (type == UnlockType::Biometrics)
						? tr::lng_settings_use_touchid_about()
						: (type == UnlockType::Companion)
						? tr::lng_settings_use_applewatch_about()
						: tr::lng_settings_use_systempwd_about()));

			}, systemUnlockContent->lifetime());

			systemUnlockWrap->toggleOn(unlockType->value(
			) | rpl::map(rpl::mappers::_1 != UnlockType::None));

			return SectionBuilder::WidgetToAdd{};
		}, [] {
			return SearchEntry{
				.id = u"passcode/biometrics"_q,
				.title = Platform::IsWindows()
					? tr::lng_settings_use_winhello(tr::now)
					: tr::lng_settings_use_touchid(tr::now),
				.keywords = { u"biometrics"_q, u"touchid"_q, u"faceid"_q,
					u"winhello"_q, u"fingerprint"_q },
			};
		});
	}, state->appLockOn.value());

	builder.scope([&] {
		builder.addDividerText(tr::lng_settings_passcode_wallet_about());
	}, state->walletDependent.value());

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"passcode/disable"_q,
			.title = tr::lng_settings_passcode_disable(tr::now),
			.keywords = { u"disable"_q, u"remove"_q, u"turn off"_q },
		};
	});
}

class LocalPasscodeManage : public Section<LocalPasscodeManage> {
public:
	LocalPasscodeManage(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LocalPasscodeManage();

	[[nodiscard]] rpl::producer<QString> title() override;

	void showFinished() override;
	[[nodiscard]] rpl::producer<> sectionShowBack() override;

	[[nodiscard]] rpl::producer<std::vector<Type>> removeFromStack() override;

	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

	void setStepDataReference(std::any &data) override;

private:
	void setupContent();
	void disableAfterRemoval(
		Storage::PasscodeVerification verification,
		Wallet::KeyProtectionResult result);

	rpl::variable<bool> _isBottomFillerShown;
	rpl::event_stream<> _showBack;
	QPointer<Ui::RpWidget> _disableButton;
	std::any *_stepData = nullptr;
	std::optional<Storage::PasscodeVerification> _verification;

};

LocalPasscodeManage::LocalPasscodeManage(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> LocalPasscodeManage::title() {
	return tr::lng_settings_passcode_title();
}

void LocalPasscodeManage::setStepDataReference(std::any &data) {
	// createPinnedToBottom() runs before this, so the disable button reads
	// _verification when it is pressed, which is always later than this call.
	_stepData = &data;
	_verification = TakeVerification(_stepData);
}

rpl::producer<std::vector<Type>> LocalPasscodeManage::removeFromStack() {
	return rpl::single(std::vector<Type>{
		LocalPasscodeManage::Id(),
		LocalPasscodeCreate::Id(),
		LocalPasscodeCheck::Id(),
		LocalPasscodeChange::Id(),
	});
}

void LocalPasscodeManage::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	CloudPassword::SetupAutoCloseTimer(
		content->lifetime(),
		[=] { _showBack.fire({}); },
		[] { return Core::App().lastNonIdleTime(); });

	const SectionBuildMethod buildMethod = [this](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		auto &lifetime = container->lifetime();
		const auto highlights = lifetime.make_state<HighlightRegistry>();

		const auto isPaused = Window::PausedIn(
			controller,
			Window::GifPauseReason::Layer);
		auto passOther = crl::guard(this, [=, this](Type type) {
			if (type == LocalPasscodeChange::Id()) {
				WriteVerification(_stepData, _verification);
			}
			showOther(type);
		});
		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(passOther),
			.isPaused = isPaused,
			.highlights = highlights,
		});
		BuildManageContent(builder);

		std::move(showFinished) | rpl::on_next([=] {
			for (const auto &[id, entry] : *highlights) {
				if (entry.widget) {
					controller->checkHighlightControl(
						id,
						entry.widget,
						base::duplicate(entry.args));
				}
			}
		}, lifetime);
	};

	build(content, buildMethod);

	Ui::ResizeFitChild(this, content);
}

// What the removal box answers with, and the only place Disable lets the
// passcode go. Nothing here touches the app lock: the box's own "keep the
// passcode for the wallet only" row has already called setAppLockEnabled()
// and Core::App().localPasscodeChanged() itself, and the section's toggle
// follows that signal, so it shows a change this object did not make. A walk
// that failed or was kept leaves the passcode in place, which is what keeps
// Disable from ever costing a wallet key.
void LocalPasscodeManage::disableAfterRemoval(
		Storage::PasscodeVerification verification,
		Wallet::KeyProtectionResult result) {
	if (result.cancelled) {
		return;
	}
	const auto changed = int(result.changed.size());
	const auto report = [&] {
		controller()->showToast((changed > 0)
			? tr::lng_settings_passcode_disable_changed(
				tr::now,
				lt_count,
				changed)
			: tr::lng_wallet_protection_error(tr::now));
	};
	if (result.failed) {
		// Each transition is complete by itself, so the wallets the walk
		// did move keep their new kind while the passcode stays for the
		// ones it did not.
		report();
		return;
	} else if (result.kind == Wallet::VaultKind::Passcode) {
		controller()->showToast(
			tr::lng_settings_passcode_disable_kept(tr::now));
		return;
	}
	const auto set = SetPasscode(controller(), QString(), verification);
	if (set != Storage::SetPasscodeResult::Success) {
		// The vaults have moved and the passcode has not. That is what the
		// app-lock toggle leaves behind: setAppLockEnabled() zeroes the
		// verification nonce on every exit and cannot tell this object,
		// which is still holding the spent token. So say what did change
		// and ask for the passcode once more, instead of ending silently
		// on a screen that looks untouched.
		report();
		showOther(LocalPasscodeCheckId());
		return;
	}
	Core::App().settings().setSystemUnlockEnabled(false);
	Core::App().saveSettingsDelayed();
	// The fire ends in showBackFromStack(), which deletes the Info widget
	// this section lives in - object_ptr::destroy() is a plain delete - so
	// this object is gone the moment it returns. The event stream copies its
	// data pointer before delivering, which is why the fire itself survives
	// and only the lines after it need the guard.
	const auto weak = base::make_weak(this);
	_showBack.fire({});
	if (weak) {
		controller()->hideSpecialLayer();
	}
}

base::weak_qptr<Ui::RpWidget> LocalPasscodeManage::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	const auto weak = base::make_weak(this);
	auto callback = [=] {
		// BuildManageContent is registered for settings search, so this
		// section can be opened without the Check section ever running.
		if (!_verification) {
			showOther(LocalPasscodeCheckId());
			return;
		}
		const auto verification = *_verification;
		auto dependents = Wallet::CollectVaultDependents();
		if (!dependents.passcodeWrapped.empty()) {
			// Disable re-protects, it never drops a key: the box asks for
			// the passcode itself, names the wallets, warns about the open
			// kind behind its own row and walks every listed vault onto the
			// chosen one. The box waits for the passcode before it does any
			// of that, and an account can be logged out and dropped while it
			// waits, so this frame's enumeration goes over weakly and the box
			// skips whatever is gone by the time it acts.
			auto accounts = std::vector<base::weak_ptr<Main::Account>>();
			accounts.reserve(dependents.passcodeWrapped.size());
			for (const auto &account : dependents.passcodeWrapped) {
				accounts.push_back(base::make_weak(account));
			}
			const auto show = Main::MakeSessionShow(
				controller()->uiShow(),
				&controller()->session());
			Wallet::ShowKeyProtectionBox(show, Wallet::KeyProtectionArgs{
				.mode = Wallet::KeyProtectionMode::Removal,
				.accounts = std::move(accounts),
				.done = [=](Wallet::KeyProtectionResult result) {
					if (weak) {
						disableAfterRemoval(
							verification,
							std::move(result));
					}
				},
			});
			return;
		}
		// While the launch lock is on, the passcode is the only thing
		// covering an open vault's key at rest, so removing it says so -
		// the wording the app-lock toggle already shows, composed with
		// today's confirmation instead of given a key of its own.
		auto text = [&]() -> rpl::producer<QString> {
			const auto &local = controller()->session().domain().local();
			// Warn if removal loses an Open vault's verified launch lock.
			if (dependents.open.empty() || !local.appLockEnabled()) {
				return tr::lng_settings_passcode_disable_sure();
			}
			return rpl::combine(
				tr::lng_settings_passcode_disable_sure(),
				tr::lng_wallet_protection_open_warning_nolock()
			) | rpl::map([](const QString &sure, const QString &warning) {
				return sure + u"\n\n"_q + warning;
			});
		}();
		controller()->show(
			Ui::MakeConfirmBox({
				.text = std::move(text),
				.confirmed = [=](Fn<void()> &&close) {
					if (!weak) {
						close();
						return;
					}
					const auto result = SetPasscode(
						controller(),
						QString(),
						verification);
					if (result != Storage::SetPasscodeResult::Success) {
						close();
						if (weak) {
							showOther(LocalPasscodeCheckId());
						}
						return;
					}
					Core::App().settings().setSystemUnlockEnabled(false);
					Core::App().saveSettingsDelayed();

					close();
					if (weak) {
						_showBack.fire({});
					}
					if (weak) {
						controller()->hideSpecialLayer();
					}
				},
				.confirmText = tr::lng_settings_auto_night_disable(),
				.confirmStyle = &st::attentionBoxButton,
			}));
	};
	auto bottomButton = CloudPassword::CreateBottomDisableButton(
		parent,
		geometryValue(),
		tr::lng_settings_passcode_disable(),
		std::move(callback));

	_isBottomFillerShown = base::take(bottomButton.isBottomFillerShown);
	_disableButton = bottomButton.button.get();

	return bottomButton.content;
}

void LocalPasscodeManage::showFinished() {
	Section<LocalPasscodeManage>::showFinished();
	controller()->checkHighlightControl(
		u"passcode/disable"_q,
		_disableButton);
}

rpl::producer<> LocalPasscodeManage::sectionShowBack() {
	return _showBack.events();
}

LocalPasscodeManage::~LocalPasscodeManage() = default;

const auto kMeta = BuildHelper({
	.id = LocalPasscodeManage::Id(),
	.parentId = nullptr,
	.title = &tr::lng_settings_passcode_title,
	.icon = &st::menuIconLock,
}, [](SectionBuilder &builder) {
	BuildManageContent(builder);
});

} // namespace

Type LocalPasscodeCreateId() {
	return LocalPasscodeCreate::Id();
}

Type LocalPasscodeCheckId() {
	return LocalPasscodeCheck::Id();
}

Type LocalPasscodeManageId() {
	return LocalPasscodeManage::Id();
}

} // namespace Settings
