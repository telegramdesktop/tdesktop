/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_local_passcode.h"

#include "base/platform/base_platform_last_input.h"
#include "base/platform/base_platform_info.h"
#include "base/openssl_help.h"
#include "base/system_unlock.h"
#include "boxes/auto_lock_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_user.h"
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
#include "ui/toast/toast.h"
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

constexpr auto kDisableReportCharacterTime = crl::time(60);

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

[[nodiscard]] Storage::SetPasscodeResult RemovePasscode(
		not_null<Window::SessionController*> controller,
		Storage::PasscodeVerification verification) {
	const auto result = SetPasscode(controller, QString(), verification);
	if (result == Storage::SetPasscodeResult::Success) {
		Core::App().settings().setSystemUnlockEnabled(false);
		Core::App().saveSettingsDelayed();
	}
	return result;
}

[[nodiscard]] QString DisableChangedReport(
		const std::vector<base::weak_ptr<Main::Account>> &changed) {
	auto names = QStringList();
	auto unnamed = 0;
	for (const auto &weak : changed) {
		const auto account = weak.get();
		const auto session = account ? account->maybeSession() : nullptr;
		auto name = session ? session->user()->name().trimmed() : QString();
		if (name.isEmpty()) {
			++unnamed;
		} else {
			names.push_back(std::move(name));
		}
	}
	if (names.empty()) {
		return tr::lng_settings_passcode_disable_changed(
			tr::now,
			lt_count,
			unnamed);
	}
	auto text = tr::lng_settings_passcode_disable_changed_named(
		tr::now,
		lt_accounts,
		names.join(u", "_q));
	if (unnamed) {
		text += u" "_q + tr::lng_settings_passcode_disable_changed_unnamed(
			tr::now,
			lt_count,
			unnamed);
	}
	return text;
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

[[nodiscard]] rpl::producer<QString> WalletPasscodeDescription() {
	const auto dependents = Wallet::CollectVaultDependents();
	if (dependents.passcodeWrapped.empty()) {
		return tr::lng_passcode_unused_about();
	}
	for (const auto &account : dependents.passcodeWrapped) {
		const auto session = account->maybeSession();
		if (!session) {
			continue;
		}
		auto name = session->user()->name().trimmed();
		if (name.isEmpty()) {
			continue;
		}
		return tr::lng_passcode_wallet_about(
			lt_account,
			rpl::single(std::move(name)));
	}
	return tr::lng_passcode_wallet_about_unnamed();
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

	if (isCreate
		|| controller()->session().domain().local().appLockEnabled()) {
		addDescription(tr::lng_passcode_about1());
		Ui::AddSkip(content);
		addDescription(tr::lng_passcode_about2());
	} else {
		addDescription(WalletPasscodeDescription());
	}

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
	const auto weak = base::make_weak(this);
	const auto weakController = base::make_weak(controller());
	const auto setDeriving = [=](bool deriving) {
		_deriving = deriving;
		newPasscode->setDisabled(deriving);
		if (!weak || !weakController) {
			return;
		}
		if (reenterPasscode) {
			reenterPasscode->setDisabled(deriving);
			if (!weak || !weakController) {
				return;
			}
		}
		button->setDisabled(deriving);
		if (!weak || !weakController) {
			return;
		}
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
		if (!weak || !weakController) {
			return;
		}
		newPasscode->showError();
		if (!weak || !weakController) {
			return;
		}
		error->show();
		if (weak && weakController) {
			error->setText(text);
		}
	};

	const auto runStagedChange = [=](
			const QString &newText,
			Storage::PasscodeVerification verification,
			Wallet::SecureBytes passcode) {
		const auto controller = weakController.get();
		if (!weak || !controller) {
			return;
		}
		auto newUtf8 = newText.toUtf8();
		const auto cleanse = gsl::finally([&] {
			if (!newUtf8.isEmpty()) {
				OPENSSL_cleanse(newUtf8.data(), newUtf8.size());
			}
		});
		auto batch = Wallet::VaultPasscodeChange::Prepare(
			controller->session().domain().local(),
			std::move(passcode),
			newUtf8);
		if (!batch) {
			setDeriving(false);
			if (weak && weakController) {
				showFieldError(tr::lng_wallet_protection_error(tr::now));
			}
			return;
		}
		Storage::DeriveOnWorker(
			std::move(*batch),
			crl::guard(this, [=](Wallet::VaultPasscodeChange &&batch) {
				setDeriving(false);
				if (!weak || !weakController) {
					return;
				}
				const auto result = batch.apply([=](
						Storage::PasscodeDerivation keyData) {
					const auto controller = weakController.get();
					return controller
						? SetPasscode(
							controller,
							std::move(keyData),
							verification)
						: Storage::SetPasscodeResult::Failed;
				});
				if (!weak || !weakController) {
					return;
				}
				using Result = Wallet::VaultPasscodeChangeResult;
				switch (result) {
				case Result::VaultFailed:
					showFieldError(tr::lng_wallet_protection_error(tr::now));
					return;
				case Result::NeedsVerification:
					weak.get()->_showOther.fire(LocalPasscodeCheckId());
					return;
				case Result::PasscodeFailed:
					showFieldError(Lang::Hard::SecureSaveError());
					return;
				case Result::CommitFailed:
					weakController.get()->showToast(
						tr::lng_wallet_protection_error(tr::now));
					if (!weak || !weakController) {
						return;
					}
					[[fallthrough]];
				case Result::Done:
					weak.get()->_showBack.fire({});
					return;
				}
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

	const auto checkAndChange = [=](
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
				}
				if (!Wallet::CollectVaultDependents().passcodeWrapped.empty()) {
					changeWithWalletVaults(newText, verification);
				} else {
					deriveAndSave(newText, verification);
				}
			}));
	};

	const auto checkAndOpenManage = [=](const QString &newText) {
		setDeriving(true);
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
				setDeriving(false);
				const auto &domain = controller()->session().domain();
				const auto verification = domain.local().verifyPasscode(
					std::move(derived));
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
			} else if (isChange && !_verification) {
				_showOther.fire(LocalPasscodeCheckId());
			} else {
				setDeriving(true);
				if (isCreate) {
					deriveAndSave(newText, {});
				} else {
					checkAndChange(newText, *_verification);
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
			checkAndOpenManage(newText);
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

void BuildManageContent(
		SectionBuilder &builder,
		Fn<std::optional<Storage::PasscodeVerification>()> takeVerification) {
	const auto controller = builder.controller();
	if (!controller) {
		return;
	}
	const auto showOther = builder.showOther();

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
		// in a gsl::finally on every exit, accepted or refused, whichever
		// way the lock moves, so any flip of this toggle kills the token the
		// Check screen minted. The section's copy is therefore taken on
		// every flip, and a Change or Disable pressed afterwards finds none
		// and re-authenticates through Check instead of carrying a dead
		// token into a write that would only answer NeedsVerification.
		const auto apply = [=](bool enabled) {
			auto verification = takeVerification
				? takeVerification()
				: std::nullopt;
			if (!enabled && !verification) {
				state->appLockToggles.fire_copy(true);
				showOther(LocalPasscodeCheckId());
				return false;
			}
			const auto &domain = Core::App().domain();
			const auto result = domain.local().setAppLockEnabled(
				enabled,
				verification.value_or(Storage::PasscodeVerification()));
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
	void showRemoval(Storage::PasscodeVerification verification);
	void confirmDisable(Storage::PasscodeVerification verification);
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
		BuildManageContent(builder, [this] {
			return base::take(_verification);
		});

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

void LocalPasscodeManage::showRemoval(
		Storage::PasscodeVerification verification) {
	// Disable re-protects, it never drops a key: the box asks for the
	// passcode, warns before losing an Open vault's launch lock and then
	// names the wallets it will walk onto the chosen kind. Accounts can
	// disappear at either prompt, so this weak seed is refreshed before
	// the chooser freezes the list its eventual walk will use.
	auto accounts = std::vector<base::weak_ptr<Main::Account>>();
	{
		const auto dependents = Wallet::CollectVaultDependents();
		accounts.reserve(dependents.passcodeWrapped.size());
		for (const auto &account : dependents.passcodeWrapped) {
			accounts.push_back(base::make_weak(account));
		}
	}
	if (accounts.empty()) {
		confirmDisable(verification);
		return;
	}
	const auto weak = base::make_weak(this);
	const auto weakController = base::make_weak(controller());
	const auto show = Main::MakeSessionShow(
		controller()->uiShow(),
		&controller()->session());
	Wallet::ShowKeyProtectionBox(show, Wallet::KeyProtectionArgs{
		.mode = Wallet::KeyProtectionMode::Removal,
		.accounts = std::move(accounts),
		.done = [=](Wallet::KeyProtectionResult result) {
			if (weak && weakController) {
				weak->disableAfterRemoval(verification, std::move(result));
			}
		},
	});
}

// The removal chooser keeps every key: its Keep row only turns off the
// launch lock, and its walk changes one committed vault at a time. Failed
// walks retain the passcode and report the transitions that did complete,
// and so does a walk the user dismissed midway - its result still cancels,
// and only the empty-changed guard below tells that case from every other
// cancellation the chooser produces, all of which carry no account at all:
// report()'s empty branch is the "nothing was written" error, which a
// dismissal must never show.
// A completed walk can remove the passcode only after a fresh scan finds no
// dependent, including any account added after the chooser froze its list.
void LocalPasscodeManage::disableAfterRemoval(
		Storage::PasscodeVerification verification,
		Wallet::KeyProtectionResult result) {
	const auto weak = base::make_weak(this);
	const auto weakController = base::make_weak(controller());
	const auto report = [&] {
		if (result.changed.empty()) {
			weakController->showToast(tr::lng_wallet_protection_error(tr::now));
			return;
		}
		const auto text = DisableChangedReport(result.changed);
		weakController->showToast(Ui::Toast::Config{
			.text = tr::marked(text),
			.maxlines = 0,
			.duration = Ui::Toast::kDefaultDuration
				+ crl::time(text.size()) * kDisableReportCharacterTime,
		});
	};
	if (result.cancelled) {
		if (!result.changed.empty()) {
			report();
		}
		return;
	}
	if (result.failed) {
		// Each transition is complete by itself, so the wallets the walk
		// did move keep their new kind while the passcode stays for the
		// ones it did not.
		report();
		return;
	} else if (result.kind == Wallet::VaultKind::Passcode) {
		weakController->showToast(
			tr::lng_settings_passcode_disable_kept(tr::now));
		return;
	}
	if (!Wallet::CollectVaultDependents().passcodeWrapped.empty()) {
		weakController->showToast(
			tr::lng_settings_passcode_disable_dependent(tr::now));
		return;
	}
	const auto set = RemovePasscode(weakController.get(), verification);
	if (!weak || !weakController) {
		return;
	}
	if (set != Storage::SetPasscodeResult::Success) {
		// The vaults have moved and the passcode has not: some nonce
		// consumer ran between Check and this write - a passcode box
		// minting a token of its own, the removal chooser's Keep row
		// turning the launch lock off - and none of them can tell this
		// object, which is still holding the dead token. (The app-lock
		// toggle no longer leaves that behind; it takes the section's copy
		// and routes through Check itself.) So say what did change and ask
		// for the passcode once more, instead of ending silently on a
		// screen that looks untouched.
		report();
		if (weak && weakController) {
			weak->showOther(LocalPasscodeCheckId());
		}
		return;
	}
	// The fire ends in showBackFromStack(), which deletes the Info widget
	// this section lives in - object_ptr::destroy() is a plain delete - so
	// this object is gone the moment it returns. The event stream copies its
	// data pointer before delivering, which is why the fire itself survives
	// and only the lines after it need the guard.
	_showBack.fire({});
	if (weak && weakController) {
		weakController->hideSpecialLayer();
	}
}

void LocalPasscodeManage::confirmDisable(
		Storage::PasscodeVerification verification) {
	const auto weak = base::make_weak(this);
	const auto weakController = base::make_weak(controller());
	// While the launch lock is on, the passcode is the only thing
	// covering an open vault's key at rest, so removing it says so -
	// the wording the app-lock toggle already shows, composed with
	// today's confirmation instead of given a key of its own.
	const auto warned = !Wallet::CollectVaultDependents().open.empty()
		&& controller()->session().domain().local().appLockEnabled();
	auto text = [&]() -> rpl::producer<QString> {
		if (!warned) {
			return tr::lng_settings_passcode_disable_sure();
		}
		return rpl::combine(
			tr::lng_settings_passcode_disable_sure(),
			tr::lng_wallet_protection_open_warning_nolock()
		) | rpl::map([](const QString &sure, const QString &warning) {
			return sure + u"\n\n"_q + warning;
		});
	}();
	controller()->show(Ui::MakeConfirmBox({
		.text = std::move(text),
		.confirmed = [=](Fn<void()> close) {
			if (!weak || !weakController) {
				close();
				return;
			}
			const auto [needsRemoval, needsWarning] = [&] {
				const auto dependents = Wallet::CollectVaultDependents();
				const auto &local = weakController->session().domain().local();
				return std::pair(
					!dependents.passcodeWrapped.empty(),
					!dependents.open.empty() && local.appLockEnabled());
			}();
			if (needsRemoval || (needsWarning && !warned)) {
				close();
				if (weak && weakController) {
					if (needsRemoval) {
						weak->showRemoval(verification);
					} else {
						weak->confirmDisable(verification);
					}
				}
				return;
			}
			const auto result = RemovePasscode(
				weakController.get(),
				verification);
			if (!weak || !weakController) {
				close();
				return;
			}
			close();
			if (!weak || !weakController) {
				return;
			}
			if (result != Storage::SetPasscodeResult::Success) {
				weak->showOther(LocalPasscodeCheckId());
				return;
			}
			weak.get()->_showBack.fire({});
			if (weak && weakController) {
				weakController->hideSpecialLayer();
			}
		},
		.confirmText = tr::lng_settings_auto_night_disable(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

base::weak_qptr<Ui::RpWidget> LocalPasscodeManage::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	auto callback = [=] {
		// BuildManageContent is registered for settings search, so this
		// section can be opened without the Check section ever running.
		if (!_verification) {
			showOther(LocalPasscodeCheckId());
			return;
		}
		showRemoval(*_verification);
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
	BuildManageContent(builder, nullptr);
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
