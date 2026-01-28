/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_local_passcode.h"

#include "base/platform/base_platform_last_input.h"
#include "base/platform/base_platform_info.h"
#include "base/system_unlock.h"
#include "boxes/auto_lock_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_step.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "storage/storage_domain.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace Builder;

void SetPasscode(
		not_null<Window::SessionController*> controller,
		const QString &pass) {
	cSetPasscodeBadTries(0);
	controller->session().domain().local().setPasscode(pass.toUtf8());
	Core::App().localPasscodeChanged();
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

protected:
	void setupContent();

	[[nodiscard]] virtual EnterType enterType() const = 0;

private:
	rpl::event_stream<> _showFinished;
	rpl::event_stream<> _setInnerFocus;
	rpl::event_stream<Type> _showOther;
	rpl::event_stream<> _showBack;
	bool _systemUnlockWithBiometric = false;

};

LocalPasscodeEnter::LocalPasscodeEnter(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractSection(parent, controller) {
}

rpl::producer<QString> LocalPasscodeEnter::title() {
	return tr::lng_settings_passcode_title();
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
	[[maybe_unused]] const auto isChange = (enterType() == EnterType::Change);

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

	const auto addField = [&](rpl::producer<QString> &&text) {
		const auto &st = st::settingLocalPasscodeInputField;
		auto container = object_ptr<Ui::RpWidget>(content);
		container->resize(container->width(), st.heightMin);
		const auto field = Ui::CreateChild<Ui::PasswordInput>(
			container.data(),
			st,
			std::move(text));

		container->geometryValue(
		) | rpl::on_next([=](const QRect &r) {
			field->moveToLeft((r.width() - field->width()) / 2, 0);
		}, container->lifetime());

		content->add(std::move(container));
		return field;
	};

	const auto addError = [&](not_null<Ui::PasswordInput*> input) {
		const auto error = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_language_name(tr::now),
				st::settingLocalPasscodeError),
			st::changePhoneDescriptionPadding,
			style::al_top);
		error->hide();
		QObject::connect(input.get(), &Ui::MaskedInputField::changed, [=] {
			error->hide();
		});
		return error;
	};

	const auto newPasscode = addField(isCreate
		? tr::lng_passcode_enter_first()
		: tr::lng_passcode_enter());

	const auto reenterPasscode = isCheck
		? (Ui::PasswordInput*)(nullptr)
		: addField(tr::lng_passcode_confirm_new());
	const auto error = addError(isCheck ? newPasscode : reenterPasscode);

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
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	button->setClickedCallback([=] {
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
				if (isChange) {
					const auto &domain = controller()->session().domain();
					if (domain.local().checkPasscode(newText.toUtf8())) {
						newPasscode->setFocus();
						newPasscode->showError();
						newPasscode->selectAll();
						error->show();
						error->setText(tr::lng_passcode_is_same(tr::now));
						return;
					}
				}
				SetPasscode(controller(), newText);
				if (isCreate) {
					if (Platform::IsWindows() || _systemUnlockWithBiometric) {
						Core::App().settings().setSystemUnlockEnabled(true);
						Core::App().saveSettingsDelayed();
					}
					_showOther.fire(LocalPasscodeManageId());
				} else if (isChange) {
					_showBack.fire({});
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
			if (domain.local().checkPasscode(newText.toUtf8())) {
				cSetPasscodeBadTries(0);
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
	};
	const auto state = container->lifetime().make_state<State>();

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
			) | rpl::start_to_stream(state->autoLockBoxClosing, box->lifetime());
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
				return value != Core::App().settings().systemUnlockEnabled();
			}) | rpl::on_next([=](bool value) {
				Core::App().settings().setSystemUnlockEnabled(value);
				Core::App().saveSettingsDelayed();
			}, systemUnlockContent->lifetime());

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

private:
	void setupContent();

	rpl::variable<bool> _isBottomFillerShown;
	rpl::event_stream<> _showBack;

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

	const SectionBuildMethod buildMethod = [](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		const auto isPaused = Window::PausedIn(
			controller,
			Window::GifPauseReason::Layer);
		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.isPaused = isPaused,
		});
		BuildManageContent(builder);
	};

	build(content, buildMethod);

	Ui::ResizeFitChild(this, content);
}

base::weak_qptr<Ui::RpWidget> LocalPasscodeManage::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {
	const auto weak = base::make_weak(this);
	auto callback = [=] {
		controller()->show(
			Ui::MakeConfirmBox({
				.text = tr::lng_settings_passcode_disable_sure(),
				.confirmed = [=](Fn<void()> &&close) {
					SetPasscode(controller(), QString());
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

	return bottomButton.content;
}

void LocalPasscodeManage::showFinished() {
	Section<LocalPasscodeManage>::showFinished();
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
