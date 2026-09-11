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
#include "base/timer.h"
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
#include "wallet/wallet_unlock.h"
#include "wallet/wallet_vault.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_passcode_strength_meter.h"
#include "styles/style_settings.h"

#include <QtWidgets/QApplication>

namespace Settings {
namespace {

using namespace Builder;

constexpr auto kDisableReportCharacterTime = crl::time(60);
constexpr auto kPasscodeSectionTimeout = 60 * crl::time(1000);
constexpr auto kPasscodeCountdownTick = crl::time(1000);

// One worker job for both legs of a change: the proof derived from the
// retained bytes and the derivation the write needs - the staged vault batch
// when passcode-wrapped vaults depend on the passcode, a fresh key_data wrap
// otherwise - so that the main-thread callback verifies the proof and writes
// in one statement sequence, with no hop for another window's mint to land
// in between.
struct ChangeJob {
	std::optional<Wallet::VaultPasscodeChange> batch;
	std::optional<Storage::PasscodeDerivation> fresh;
	Storage::PasscodeDerivation proof;

	void run();
};

// The inactivity window of a section holding the passcode bytes, shown as
// m:ss in its title bar. It filters events on qApp, because a filter on the
// section alone never sees its children's events, and watched is exactly the
// widget under a mouse press or the focus widget of a key press, so the
// owner's inside() decides whether the interaction belongs to it. The owner
// pauses it while its own flow is in flight and restarts it when that flow
// ends without leaving. tick() calls expired() last: that callback may
// destroy the owner and this object with it.
class PasscodeCountdown final : public QObject {
public:
	PasscodeCountdown(Fn<bool(QWidget*)> inside, Fn<void()> expired);
	~PasscodeCountdown();

	void restart();
	void pause();
	[[nodiscard]] rpl::producer<QString> text() const;

private:
	bool eventFilter(QObject *watched, QEvent *e) override;
	void tick();

	base::Timer _timer;
	crl::time _expiresAt = 0;
	bool _paused = true;
	rpl::variable<QString> _text;
	Fn<bool(QWidget*)> _inside;
	Fn<void()> _expired;

};

[[nodiscard]] QString FormatCountdown(crl::time seconds) {
	return u"%1:%2"_q
		.arg(seconds / 60)
		.arg(seconds % 60, 2, 10, QChar('0'));
}

void ChangeJob::run() {
	if (batch) {
		batch->run();
	}
	if (fresh) {
		fresh->run();
	}
	proof.run();
}

PasscodeCountdown::PasscodeCountdown(
	Fn<bool(QWidget*)> inside,
	Fn<void()> expired)
: _timer([=] { tick(); })
, _inside(std::move(inside))
, _expired(std::move(expired)) {
	qApp->installEventFilter(this);
}

PasscodeCountdown::~PasscodeCountdown() {
	qApp->removeEventFilter(this);
}

void PasscodeCountdown::restart() {
	_paused = false;
	_expiresAt = crl::now() + kPasscodeSectionTimeout;
	_text = FormatCountdown(kPasscodeSectionTimeout / 1000);
	_timer.callEach(kPasscodeCountdownTick);
}

void PasscodeCountdown::pause() {
	_paused = true;
	_timer.cancel();
}

rpl::producer<QString> PasscodeCountdown::text() const {
	return _text.value();
}

bool PasscodeCountdown::eventFilter(QObject *watched, QEvent *e) {
	const auto type = e->type();
	if (!_paused
		&& (type == QEvent::MouseButtonPress || type == QEvent::KeyPress)
		&& _inside(qobject_cast<QWidget*>(watched))) {
		restart();
	}
	return false;
}

void PasscodeCountdown::tick() {
	const auto remaining = _expiresAt - crl::now();
	if (remaining > 0) {
		_text = FormatCountdown((remaining + 999) / 1000);
		return;
	}
	_text = FormatCountdown(0);
	pause();
	_expired();
}

// A section's own writes - the change, the lock toggle, the removal and the
// ones the wallet boxes it opens make - fire this from inside the writer
// while busy() is set, so the filter keeps it to changes made elsewhere.
// Those make the retained bytes stale: a changed passcode is asked for again,
// a removed one leaves the passcode area, because a Check for a passcode that
// no longer exists is wrong. The section's forgetAndCheck() tells the two
// apart, for this signal and for every mint that fails on stale bytes.
// Deferred out of the writer, the way KeyProtectionBox closes on the same
// signal.
void SubscribeToExternalPasscodeChanges(
		not_null<Ui::RpWidget*> section,
		not_null<Window::SessionController*> controller,
		Fn<bool()> busy,
		Fn<void()> stale) {
	controller->session().domain().local().localPasscodeChanged(
	) | rpl::filter([=] {
		return !busy();
	}) | rpl::on_next([=] {
		crl::on_main(section, stale);
	}, section->lifetime());
}

// A section that survives its own Back is the bottom of its settings layer -
// for example after the passcode deep link opened Check in a fresh layer and
// Manage dropped it - where Back has nowhere to go, so leaving the passcode
// area means closing the layer. Otherwise the fire ends in
// showBackFromStack(), which deletes the Info widget the section lives in -
// object_ptr::destroy() is a plain delete - so the section is gone the
// moment it returns. The event stream copies its data pointer before
// delivering, which is why the fire itself survives and only the lines after
// it need the guard. Closing the layer destroys the section as well, so the
// caller touches nothing of it after this.
void LeavePasscodeArea(
		not_null<Ui::RpWidget*> section,
		not_null<Window::SessionController*> controller,
		Fn<void()> showBack) {
	const auto weak = base::make_weak(section);
	const auto weakController = base::make_weak(controller);
	showBack();
	if (weak && weakController) {
		weakController->hideSpecialLayer();
	}
}

[[nodiscard]] Wallet::VaultPasscodeChangeResult MapChangeResult(
		Storage::SetPasscodeResult result) {
	using Result = Wallet::VaultPasscodeChangeResult;
	switch (result) {
	case Storage::SetPasscodeResult::Success:
		return Result::Done;
	case Storage::SetPasscodeResult::NeedsVerification:
		return Result::NeedsVerification;
	case Storage::SetPasscodeResult::Failed:
		return Result::PasscodeFailed;
	}
	Unexpected("SetPasscodeResult in MapChangeResult.");
}

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
	// The reconciliation that runs on the removal walk's last transition
	// may already have dropped the passcode; that is the outcome this write
	// exists to produce, and a redundant checked rewrite of key_data could
	// only mis-report a disk error over a passcode that is already gone.
	if (!Core::App().domain().local().hasPasscode()) {
		return Storage::SetPasscodeResult::Success;
	}
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

// The passcode bytes the Check section accepted are consumed by the Manage
// and Change sections, which are different section objects created later by
// the navigation stack, and each keeps its own copy for its lifetime. They
// travel in the std::any the Info controller keeps for the whole settings
// stack, the same channel CloudPassword::StepData uses, but only for the one
// synchronous navigation hop that hands them over, in either direction: the
// sender writes the carrier immediately before the hop, the receiving section
// moves the bytes out of the std::any into a member of its own and resets the
// any, so nothing lingers there, and a section that closes instead of hopping
// takes its bytes straight back. std::any needs a copyable payload and
// SecureBytes is move-only, so the carrier is a shared_ptr whose buffer the
// move has already cleansed by the time the reset destroys it. So the bytes
// live exactly as long as the section holding them - leaving by hand, by
// removeFromStack or by the countdown destroys that section and cleanses
// them - and a Manage section reached straight from settings search finds
// nothing and asks for the passcode.
struct PasscodeHandOver {
	std::shared_ptr<Wallet::SecureBytes> bytes;
};

[[nodiscard]] Wallet::SecureBytes TakePasscode(std::any *stepData) {
	if (!stepData || !stepData->has_value()) {
		return {};
	}
	const auto carrier = std::any_cast<PasscodeHandOver>(stepData);
	if (!carrier || !carrier->bytes) {
		return {};
	}
	auto result = std::move(*carrier->bytes);
	*stepData = std::any();
	return result;
}

void WritePasscode(std::any *stepData, Wallet::SecureBytes bytes) {
	if (stepData) {
		*stepData = PasscodeHandOver{
			std::make_shared<Wallet::SecureBytes>(std::move(bytes)),
		};
	}
}

[[nodiscard]] QByteArray Utf8Copy(const Wallet::SecureBytes &bytes) {
	return QByteArray(
		reinterpret_cast<const char*>(bytes.span().data()),
		bytes.size());
}

[[nodiscard]] rpl::producer<QString> WalletPasscodeDescription() {
	const auto dependents = Wallet::CollectVaultDependents();
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
	[[nodiscard]] rpl::producer<std::vector<Type>> removeFromStack() override;
	void checkBeforeClose(Fn<void()> close) override;

	[[nodiscard]] rpl::producer<QString> title() override;
	[[nodiscard]] rpl::producer<QString> titleBadge() override;

	void setStepDataReference(std::any &data) override;

protected:
	void setupContent();

	[[nodiscard]] virtual EnterType enterType() const = 0;

private:
	void forgetAndCheck();
	void leaveArea();

	rpl::event_stream<> _showFinished;
	rpl::event_stream<> _setInnerFocus;
	rpl::event_stream<Type> _showOther;
	rpl::event_stream<> _showBack;
	rpl::event_stream<std::vector<Type>> _removeFromStack;
	std::any *_stepData = nullptr;
	Wallet::SecureBytes _passcode;
	std::unique_ptr<PasscodeCountdown> _countdown;
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

rpl::producer<QString> LocalPasscodeEnter::titleBadge() {
	if (!_countdown) {
		return nullptr;
	}
	return _countdown->text();
}

void LocalPasscodeEnter::setStepDataReference(std::any &data) {
	// TypedLocalPasscodeEnter builds the content from its constructor, so
	// this runs after setupContent(), still inside the navigation that
	// creates the section, where a nested navigation is unsafe: a Change
	// section that received no bytes goes to Check one turn later. Check
	// and Create discard whatever carrier arrived.
	_stepData = &data;
	auto bytes = TakePasscode(_stepData);
	if (enterType() != EnterType::Change) {
		return;
	}
	_passcode = std::move(bytes);
	if (_passcode.empty()) {
		crl::on_main(this, [=] {
			_showOther.fire(LocalPasscodeCheckId());
		});
	} else {
		_countdown->restart();
	}
}

void LocalPasscodeEnter::forgetAndCheck() {
	if (!controller()->session().domain().local().hasPasscode()) {
		leaveArea();
		return;
	}
	_passcode.clear();
	if (_countdown) {
		_countdown->pause();
	}
	_showOther.fire(LocalPasscodeCheckId());
}

void LocalPasscodeEnter::leaveArea() {
	_passcode.clear();
	if (_countdown) {
		_countdown->pause();
	}
	_removeFromStack.fire({ LocalPasscodeManageId() });
	LeavePasscodeArea(this, controller(), [=] { _showBack.fire({}); });
}

void LocalPasscodeEnter::checkBeforeClose(Fn<void()> close) {
	if (enterType() != EnterType::Change
		|| _passcode.empty()
		|| !_stepData) {
		close();
		return;
	}
	// Back rebuilds Manage from its memento through the same step-data hop
	// that brought the bytes here, so the rebuilt Manage stays instead of
	// asking for the passcode again. The same hook serves Close, where the
	// bytes go nowhere: a Close that leaves this section alive takes them
	// straight back out of the any, an instant one destroys the controller
	// and the carrier together.
	WritePasscode(_stepData, std::move(_passcode));
	const auto weak = base::make_weak(this);
	close();
	if (weak) {
		_passcode = TakePasscode(_stepData);
	}
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
		_countdown = std::make_unique<PasscodeCountdown>(
			[=](QWidget *widget) {
				return widget && isAncestorOf(widget);
			},
			[=] { leaveArea(); });

		SubscribeToExternalPasscodeChanges(
			this,
			controller(),
			[=] { return _deriving; },
			[=] { forgetAndCheck(); });
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
	// _deriving also guards the localPasscodeChanged() subscription above,
	// so it stays set through the section's own write and is lifted only on
	// the branches that stay on this section; the countdown pauses with it.
	const auto weak = base::make_weak(this);
	const auto weakController = base::make_weak(controller());
	const auto setDeriving = [=](bool deriving) {
		_deriving = deriving;
		if (_countdown) {
			if (deriving) {
				_countdown->pause();
			} else {
				_countdown->restart();
			}
		}
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

	// The write of a change. vaultBytes are what the vault leg of the staged
	// batch derives from - the retained bytes when they open this session's
	// vault, the gate's when they do not - and empty when no passcode-wrapped
	// vault depends on the passcode, where a fresh key_data wrap is written
	// instead. The key_data proof is always minted from the retained bytes,
	// in the same callback that spends it.
	const auto applyChange = [=](
			const QString &newText,
			Wallet::SecureBytes vaultBytes) {
		const auto controller = weakController.get();
		if (!weak || !controller) {
			return;
		}
		auto newUtf8 = newText.toUtf8();
		auto oldUtf8 = Utf8Copy(_passcode);
		const auto cleanse = gsl::finally([&] {
			if (!newUtf8.isEmpty()) {
				OPENSSL_cleanse(newUtf8.data(), newUtf8.size());
			}
			if (!oldUtf8.isEmpty()) {
				OPENSSL_cleanse(oldUtf8.data(), oldUtf8.size());
			}
		});
		const auto &local = controller->session().domain().local();
		auto job = ChangeJob{ .proof = local.prepareOpen(oldUtf8) };
		if (!vaultBytes.empty()) {
			job.batch = Wallet::VaultPasscodeChange::Prepare(
				local,
				std::move(vaultBytes),
				newUtf8);
			if (!job.batch) {
				setDeriving(false);
				if (weak && weakController) {
					showFieldError(tr::lng_wallet_protection_error(tr::now));
				}
				return;
			}
		} else {
			job.fresh = local.prepareNewWrap(newUtf8);
		}
		Storage::DeriveOnWorker(
			std::move(job),
			crl::guard(this, [=](ChangeJob &&job) {
				const auto controller = weakController.get();
				if (!controller) {
					return;
				}
				auto &local = controller->session().domain().local();
				if (!job.batch) {
					const auto dependents = Wallet::CollectVaultDependents();
					if (!dependents.passcodeWrapped.empty()) {
						setDeriving(false);
						if (weak && weakController) {
							showFieldError(
								tr::lng_wallet_protection_error(tr::now));
						}
						return;
					}
				}
				const auto verification = local.verifyPasscode(
					std::move(job.proof));
				if (!verification) {
					forgetAndCheck();
					return;
				}
				const auto result = job.batch
					? job.batch->apply([&](
							Storage::PasscodeDerivation keyData) {
						return SetPasscode(
							controller,
							std::move(keyData),
							*verification);
					})
					: MapChangeResult(SetPasscode(
						controller,
						std::move(*job.fresh),
						*verification));
				if (!weak || !weakController) {
					return;
				}
				using Result = Wallet::VaultPasscodeChangeResult;
				switch (result) {
				case Result::VaultFailed:
					setDeriving(false);
					showFieldError(tr::lng_wallet_protection_error(tr::now));
					return;
				case Result::NeedsVerification:
					LOG(("App Error: key_data refused a passcode verification "
						"minted in the same callback."));
					forgetAndCheck();
					return;
				case Result::PasscodeFailed:
					setDeriving(false);
					showFieldError(Lang::Hard::SecureSaveError());
					return;
				case Result::CommitFailed:
					controller->showToast(
						tr::lng_wallet_protection_error(tr::now));
					if (!weak || !weakController) {
						return;
					}
					[[fallthrough]];
				case Result::Done: {
					auto changed = newText.toUtf8();
					const auto cleanse = gsl::finally([&] {
						if (!changed.isEmpty()) {
							OPENSSL_cleanse(changed.data(), changed.size());
						}
					});
					WritePasscode(_stepData, Wallet::SecureBytes(changed));
					_showBack.fire({});
					return;
				}
				}
			}));
	};

	// The retained bytes open key_data but not this session's vault, so the
	// vault's passcode is asked for through the same gate
	// ShowKeyProtectionBox() opens for every one of its modes: it checks the
	// typed bytes against key_data and against the vault wrap, retains
	// nothing and mints no grant, and what it answers with serves the vault
	// leg alone. A dismissal writes nothing anywhere and only lifts the busy
	// state. The box outlives this frame, so both answers are guarded against
	// the section being gone.
	const auto changeWithWalletVaults = [=](const QString &newText) {
		controller()->show(Box(
			Wallet::WalletPasscodeBox,
			Wallet::WalletPasscodeBoxArgs{
				.show = Main::MakeSessionShow(
					controller()->uiShow(),
					&controller()->session()),
				.check = Wallet::WalletPasscodeCheck::KeyDataAndVault,
				.passed = [=](Wallet::WalletPasscodeGate gate) {
					if (weak) {
						applyChange(newText, std::move(gate.passcode));
					}
				},
				.cancelled = crl::guard(this, [=] { setDeriving(false); }),
			}));
	};

	const auto deriveAndSave = [=](const QString &newText) {
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
				const auto result = SetPasscode(
					controller(),
					std::move(derived),
					Storage::PasscodeVerification());
				if (!weak || !weakController) {
					return;
				} else if (result != Storage::SetPasscodeResult::Success) {
					setDeriving(false);
					showFieldError(Lang::Hard::SecureSaveError());
					return;
				}
				if (Platform::IsWindows() || _systemUnlockWithBiometric) {
					Core::App().settings().setSystemUnlockEnabled(true);
					Core::App().saveSettingsDelayed();
				}
				auto created = newText.toUtf8();
				const auto cleanse = gsl::finally([&] {
					if (!created.isEmpty()) {
						OPENSSL_cleanse(created.data(), created.size());
					}
				});
				WritePasscode(_stepData, Wallet::SecureBytes(created));
				_showOther.fire(LocalPasscodeManageId());
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
				}
				if (Wallet::CollectVaultDependents().passcodeWrapped.empty()) {
					applyChange(newText, {});
					return;
				}
				Wallet::CheckWalletPasscode(
					&controller()->session(),
					_passcode,
					crl::guard(this, [=](
							Wallet::WalletPasscodeVerdict verdict) {
						if (!verdict.keyData) {
							forgetAndCheck();
						} else if (!verdict.vault) {
							changeWithWalletVaults(newText);
						} else {
							applyChange(newText, _passcode.copy());
						}
					}));
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
				const auto &local = controller()->session().domain().local();
				if (!local.checkPasscode(std::move(derived))) {
					setDeriving(false);
					cSetPasscodeBadTries(cPasscodeBadTries() + 1);
					cSetPasscodeLastTry(crl::now());

					newPasscode->selectAll();
					newPasscode->setFocus();
					newPasscode->showError();
					error->show();
					error->setText(tr::lng_passcode_wrong(tr::now));
					return;
				}
				cSetPasscodeBadTries(0);
				auto accepted = newText.toUtf8();
				const auto cleanse = gsl::finally([&] {
					if (!accepted.isEmpty()) {
						OPENSSL_cleanse(accepted.data(), accepted.size());
					}
				});
				WritePasscode(_stepData, Wallet::SecureBytes(accepted));
				_showOther.fire(LocalPasscodeManageId());
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
					deriveAndSave(newText);
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

namespace details {

// Check drops the stale passcode sections beneath it, so that Back from it
// lands on what was below the passcode area and never on a Manage or Change
// rebuilt without bytes, which would only send it back to Check again.
rpl::producer<std::vector<Type>> LocalPasscodeEnter::removeFromStack() {
	switch (enterType()) {
	case EnterType::Check:
		return rpl::single(std::vector<Type>{
			LocalPasscodeManageId(),
			LocalPasscodeChange::Id(),
			LocalPasscodeCreateId(),
		});
	case EnterType::Change:
		return _removeFromStack.events();
	case EnterType::Create:
		return nullptr;
	}
	Unexpected("Enter type in LocalPasscodeEnter::removeFromStack.");
}

} // namespace details

namespace {

enum class UnlockType {
	None,
	Default,
	Biometrics,
	Companion,
};

class LocalPasscodeManage;

void BuildManageContent(
	SectionBuilder &builder,
	LocalPasscodeManage *section);

class LocalPasscodeManage : public Section<LocalPasscodeManage> {
public:
	LocalPasscodeManage(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~LocalPasscodeManage();

	[[nodiscard]] rpl::producer<QString> title() override;
	[[nodiscard]] rpl::producer<QString> titleBadge() override;

	void showFinished() override;
	[[nodiscard]] rpl::producer<> sectionShowBack() override;

	[[nodiscard]] rpl::producer<std::vector<Type>> removeFromStack() override;

	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

	void setStepDataReference(std::any &data) override;

	void setBusy(bool busy);
	[[nodiscard]] rpl::producer<bool> busyValue() const;
	void touched();
	void mintVerification(Fn<void(Storage::PasscodeVerification)> done);
	void writeRefused();

private:
	void setupContent();
	void forgetAndCheck();
	void leaveArea();
	void disable();
	void checkVaultAndRemove();
	void showRemoval(Wallet::SecureBytes verified);
	void confirmDisable();
	void disableAfterRemoval(Wallet::KeyProtectionResult result);
	void removeVerifiedAndLeave(Fn<void()> beforeFailureToast);

	rpl::variable<bool> _isBottomFillerShown;
	rpl::event_stream<> _showBack;
	QPointer<Ui::RpWidget> _disableButton;
	QPointer<Ui::RpWidget> _pinnedBottom;
	std::any *_stepData = nullptr;
	Wallet::SecureBytes _passcode;
	std::unique_ptr<PasscodeCountdown> _countdown;
	rpl::variable<bool> _busy = false;

};

LocalPasscodeManage::LocalPasscodeManage(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	// The pinned bottom content with the Disable button is parented to the
	// Info widget, not to this section, so it is asked for by itself.
	_countdown = std::make_unique<PasscodeCountdown>(
		[=](QWidget *widget) {
			return widget
				&& (isAncestorOf(widget)
					|| (_pinnedBottom && _pinnedBottom->isAncestorOf(widget)));
		},
		[=] { leaveArea(); });
	setupContent();
}

rpl::producer<QString> LocalPasscodeManage::title() {
	return tr::lng_settings_passcode_title();
}

rpl::producer<QString> LocalPasscodeManage::titleBadge() {
	return _countdown->text();
}

void LocalPasscodeManage::setStepDataReference(std::any &data) {
	// createPinnedToBottom() runs before this, so the disable button reads
	// _passcode when it is pressed, which is always later than this call.
	// A Manage without bytes - reached from settings search, or rebuilt from
	// a memento the bytes did not follow - asks for the passcode instead of
	// showing buttons that bounce, and offers to create one when there is no
	// passcode to ask for. That happens one turn later, because this still
	// runs inside the navigation that creates the section.
	_stepData = &data;
	_passcode = TakePasscode(_stepData);
	if (!_passcode.empty()) {
		_countdown->restart();
		return;
	}
	crl::on_main(this, [=] {
		const auto &local = controller()->session().domain().local();
		showOther(local.hasPasscode()
			? LocalPasscodeCheckId()
			: LocalPasscodeCreateId());
	});
}

rpl::producer<std::vector<Type>> LocalPasscodeManage::removeFromStack() {
	return rpl::single(std::vector<Type>{
		LocalPasscodeManage::Id(),
		LocalPasscodeCreate::Id(),
		LocalPasscodeCheck::Id(),
		LocalPasscodeChange::Id(),
	});
}

void LocalPasscodeManage::setBusy(bool busy) {
	_busy = busy;
	if (busy) {
		_countdown->pause();
	} else {
		_countdown->restart();
	}
	if (_disableButton) {
		_disableButton->setDisabled(busy);
	}
}

rpl::producer<bool> LocalPasscodeManage::busyValue() const {
	return _busy.value();
}

void LocalPasscodeManage::touched() {
	if (!_busy.current()) {
		_countdown->restart();
	}
}

void LocalPasscodeManage::mintVerification(
		Fn<void(Storage::PasscodeVerification)> done) {
	Wallet::MintVerificationOnWorker(this, _passcode, [=](
			std::optional<Storage::PasscodeVerification> verification) {
		if (!verification) {
			forgetAndCheck();
		} else {
			done(*verification);
		}
	});
}

void LocalPasscodeManage::writeRefused() {
	LOG(("App Error: key_data refused a passcode verification minted in the "
		"same callback."));
	forgetAndCheck();
}

void LocalPasscodeManage::forgetAndCheck() {
	if (!controller()->session().domain().local().hasPasscode()) {
		leaveArea();
		return;
	}
	_passcode.clear();
	_countdown->pause();
	showOther(LocalPasscodeCheckId());
}

void LocalPasscodeManage::leaveArea() {
	_passcode.clear();
	_countdown->pause();
	LeavePasscodeArea(this, controller(), [=] { _showBack.fire({}); });
}

void LocalPasscodeManage::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SubscribeToExternalPasscodeChanges(
		this,
		controller(),
		[=] { return _busy.current(); },
		[=] { forgetAndCheck(); });

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
		// The hop destroys this section, so Change receives the bytes
		// themselves and hands them back the same way to the Manage rebuilt
		// from the memento, which takes them in setStepDataReference().
		auto passOther = crl::guard(this, [=, this](Type type) {
			if (_busy.current()) {
				return;
			} else if (type == LocalPasscodeChange::Id()) {
				WritePasscode(_stepData, std::move(_passcode));
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
		BuildManageContent(builder, this);

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

void LocalPasscodeManage::disable() {
	setBusy(true);
	if (Wallet::CollectVaultDependents().passcodeWrapped.empty()) {
		confirmDisable();
	} else {
		checkVaultAndRemove();
	}
}

// The retained bytes are checked against key_data and against this session's
// vault before any walk re-keys the vaults: bytes gone stale go to Check
// here, not after a removal whose final write could no longer be proved.
// Bytes that open both skip the removal box's gate; bytes that open key_data
// alone leave the gate to ask for the vault's passcode, as it does for every
// other caller.
void LocalPasscodeManage::checkVaultAndRemove() {
	Wallet::CheckWalletPasscode(
		&controller()->session(),
		_passcode,
		crl::guard(this, [=](Wallet::WalletPasscodeVerdict verdict) {
			if (!verdict.keyData) {
				forgetAndCheck();
			} else {
				showRemoval(verdict.vault
					? _passcode.copy()
					: Wallet::SecureBytes());
			}
		}));
}

void LocalPasscodeManage::showRemoval(Wallet::SecureBytes verified) {
	// Disable re-protects, it never drops a key: the box asks for the
	// passcode unless verified already carries it, warns before losing an
	// Open vault's launch lock and then names the wallets it will walk onto
	// the chosen kind. Accounts can disappear at either prompt, so this weak
	// seed is refreshed before the chooser freezes the list its eventual
	// walk will use.
	auto accounts = std::vector<base::weak_ptr<Main::Account>>();
	{
		const auto dependents = Wallet::CollectVaultDependents();
		accounts.reserve(dependents.passcodeWrapped.size());
		for (const auto &account : dependents.passcodeWrapped) {
			accounts.push_back(base::make_weak(account));
		}
	}
	if (accounts.empty()) {
		confirmDisable();
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
				weak->disableAfterRemoval(std::move(result));
			}
		},
	}, std::move(verified));
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
		Wallet::KeyProtectionResult result) {
	const auto weakController = base::make_weak(controller());
	const auto report = [=, changed = result.changed] {
		if (changed.empty()) {
			weakController->showToast(tr::lng_wallet_protection_error(tr::now));
			return;
		}
		const auto text = DisableChangedReport(changed);
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
		setBusy(false);
		return;
	}
	if (result.failed) {
		// Each transition is complete by itself, so the wallets the walk
		// did move keep their new kind while the passcode stays for the
		// ones it did not.
		report();
		setBusy(false);
		return;
	} else if (result.kind == Wallet::VaultKind::Passcode) {
		weakController->showToast(
			tr::lng_settings_passcode_disable_kept(tr::now));
		setBusy(false);
		return;
	}
	if (!Wallet::CollectVaultDependents().passcodeWrapped.empty()) {
		weakController->showToast(
			tr::lng_settings_passcode_disable_dependent(tr::now));
		setBusy(false);
		return;
	}
	removeVerifiedAndLeave(report);
}

void LocalPasscodeManage::removeVerifiedAndLeave(
		Fn<void()> beforeFailureToast) {
	// The reconciliation that runs on a removal walk's last transition, or a
	// removal made elsewhere while the confirmation was open, may already have
	// dropped the passcode - RemovePasscode() then answers Success with no
	// write - and there is nothing left to prove for it.
	if (!controller()->session().domain().local().hasPasscode()) {
		leaveArea();
		return;
	}
	const auto weak = base::make_weak(this);
	const auto weakController = base::make_weak(controller());
	mintVerification([=](Storage::PasscodeVerification verification) {
		const auto result = RemovePasscode(controller(), verification);
		if (!weak || !weakController) {
			return;
		}
		if (result == Storage::SetPasscodeResult::NeedsVerification) {
			writeRefused();
			return;
		} else if (result != Storage::SetPasscodeResult::Success) {
			// After a removal walk the vaults have moved and the passcode has
			// not: say what did change before reporting the write that failed,
			// instead of ending silently on a screen that looks untouched.
			if (beforeFailureToast) {
				beforeFailureToast();
			}
			weakController->showToast(Lang::Hard::SecureSaveError());
			setBusy(false);
			return;
		}
		leaveArea();
	});
}

void LocalPasscodeManage::confirmDisable() {
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
			close();
			if (!weak || !weakController) {
				return;
			} else if (needsRemoval) {
				weak->checkVaultAndRemove();
				return;
			} else if (needsWarning && !warned) {
				weak->confirmDisable();
				return;
			}
			weak->removeVerifiedAndLeave(nullptr);
		},
		.cancelled = [=](Fn<void()> close) {
			close();
			if (weak) {
				weak->setBusy(false);
			}
		},
		.confirmText = tr::lng_settings_auto_night_disable(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

base::weak_qptr<Ui::RpWidget> LocalPasscodeManage::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	auto bottomButton = CloudPassword::CreateBottomDisableButton(
		parent,
		geometryValue(),
		tr::lng_settings_passcode_disable(),
		[=] {
			if (!_busy.current()) {
				disable();
			}
		});

	_isBottomFillerShown = base::take(bottomButton.isBottomFillerShown);
	_disableButton = bottomButton.button.get();
	_pinnedBottom = bottomButton.content.get();

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

void BuildManageContent(
		SectionBuilder &builder,
		LocalPasscodeManage *section) {
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
		.icon = { &st::menuIconPermissions },
		.toggled = rpl::merge(
			state->appLockOn.value(),
			state->appLockToggles.events()),
		.keywords = { u"lock"_q, u"launch"_q, u"startup"_q },
		.shown = state->walletDependent.value(),
	});
	if (lockApp) {
		const auto weak = base::make_weak(container);

		section->busyValue(
		) | rpl::on_next([=](bool busy) {
			lockApp->setDisabled(busy);
		}, lockApp->lifetime());

		// Turning the lock on installs nothing weaker and needs no proof, so
		// it is one synchronous write. Turning it off installs an open wrap,
		// which key_data lets through only with a proof minted from the
		// retained bytes, in the same callback as the write. Both run with
		// the section busy: the write fires localPasscodeChanged() and the
		// section's subscription must see it as its own. A refused or failed
		// flip reverts the row and lifts the busy state; a refused proof
		// means the bytes went stale and asks for the passcode again.
		const auto lockOn = [=] {
			section->setBusy(true);
			const auto result = Core::App().domain().local().setAppLockEnabled(
				true,
				Storage::PasscodeVerification());
			if (result != Storage::SetPasscodeResult::Success) {
				state->appLockToggles.fire_copy(false);
				controller->showToast(Lang::Hard::SecureSaveError());
				section->setBusy(false);
				return;
			}
			Core::App().localPasscodeChanged();
			if (weak) {
				section->setBusy(false);
			}
		};
		const auto lockOff = [=](bool walletDependent) {
			section->setBusy(true);
			section->mintVerification([=](
					Storage::PasscodeVerification verification) {
				const auto &domain = Core::App().domain();
				const auto result = domain.local().setAppLockEnabled(
					false,
					verification);
				if (result == Storage::SetPasscodeResult::NeedsVerification) {
					section->writeRefused();
					return;
				} else if (result != Storage::SetPasscodeResult::Success) {
					state->appLockToggles.fire_copy(true);
					controller->showToast(Lang::Hard::SecureSaveError());
					section->setBusy(false);
					return;
				}
				Core::App().localPasscodeChanged();
				if (walletDependent) {
					controller->showToast(
						tr::lng_settings_passcode_lock_off_wallet(tr::now));
				}
				if (weak) {
					section->setBusy(false);
				}
			});
		};
		lockApp->toggledChanges(
		) | rpl::filter([=](bool value) {
			// Compare verified locking so planted bytes cannot suppress
			// a change the user requested.
			return value != Core::App().domain().local().appLockEnabled();
		}) | rpl::on_next([=](bool value) {
			if (value) {
				lockOn();
				return;
			}
			const auto dependents = Wallet::CollectVaultDependents();
			if (dependents.open.empty()) {
				lockOff(!dependents.passcodeWrapped.empty());
				return;
			}
			section->setBusy(true);
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_wallet_protection_open_warning_nolock(),
				.confirmed = [=](Fn<void()> &&close) {
					if (weak) {
						lockOff(false);
					}
					close();
				},
				.cancelled = [=](Fn<void()> &&close) {
					if (weak) {
						state->appLockToggles.fire_copy(true);
						section->setBusy(false);
					}
					close();
				},
				.confirmText = (
					tr::lng_settings_passcode_lock_off_open_confirm()),
				.confirmStyle = &st::attentionBoxButton,
				.title = tr::lng_settings_passcode_lock_off_open_title(),
			}));
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
			const auto weak = base::make_weak(container);
			autoLockButton->addClickHandler([=] {
				const auto box = controller->show(Box<AutoLockBox>());
				box->boxClosing(
				) | rpl::on_next([=] {
					if (weak) {
						state->autoLockBoxClosing.fire({});
						section->touched();
					}
				}, box->lifetime());
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

		builder.add([=](const WidgetContext &ctx) {
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
					section->touched();
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
