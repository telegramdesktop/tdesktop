/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_manage.h"

#include "api/api_cloud_password.h"
#include "core/core_cloud_password.h"
#include "lang/lang_keys.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_email.h"
#include "settings/cloud_password/settings_cloud_password_hint.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/cloud_password/settings_cloud_password_start.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

/*
Available actions for follow states.

From CreateEmail
From CreateEmailConfirm
From ChangeEmail
From ChangeEmailConfirm
From CheckPassword
From RecreateResetHint:
– Continue to ChangePassword.
– Continue to ChangeEmail.
– DisablePassword and Back to Settings.
– Back to Settings.
*/

namespace Settings {
namespace CloudPassword {

class Manage : public TypedAbstractStep<Manage> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

	[[nodiscard]] QPointer<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

protected:
	[[nodiscard]] rpl::producer<std::vector<Type>> removeTypes() override;

private:
	rpl::variable<bool> _isBottomFillerShown;

	QString _currentPassword;

	rpl::lifetime _requestLifetime;

};

rpl::producer<QString> Manage::title() {
	return tr::lng_settings_cloud_password_start_title();
}

rpl::producer<std::vector<Type>> Manage::removeTypes() {
	return rpl::single(std::vector<Type>{
		CloudPasswordStartId(),
		CloudPasswordInputId(),
		CloudPasswordHintId(),
		CloudPasswordEmailId(),
		CloudPasswordEmailConfirmId(),
		CloudPasswordManageId(),
	});
}

void Manage::setupContent() {
	setFocusPolicy(Qt::StrongFocus);
	setFocus();

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto currentStepData = stepData();
	_currentPassword = base::take(currentStepData.currentPassword);
	// If we go back from Password Manage to Privacy Settings
	// we should forget the current password.
	setStepData(std::move(currentStepData));

	const auto quit = [=] {
		setStepData(StepData());
		showBack();
	};

	SetupAutoCloseTimer(content->lifetime(), quit);

	const auto state = cloudPassword().stateCurrent();
	if (!state) {
		quit();
		return;
	}
	cloudPassword().state(
	) | rpl::start_with_next([=](const Core::CloudPasswordState &state) {
		if (!_requestLifetime && !state.hasPassword) {
			quit();
		}
	}, lifetime());

	const auto showOtherAndRememberPassword = [=](Type type) {
		// Remember the current password to have ability
		// to return from Change Password to Password Manage.
		auto data = stepData();
		data.currentPassword = _currentPassword;
		setStepData(std::move(data));

		showOther(type);
	};

	AddDividerTextWithLottie(content, {
		.lottie = u"cloud_password/intro"_q,
		.showFinished = showFinishes(),
		.about = tr::lng_settings_cloud_password_manage_about1(
			TextWithEntities::Simple),
	});

	Ui::AddSkip(content);
	AddButtonWithIcon(
		content,
		tr::lng_settings_cloud_password_manage_password_change(),
		st::settingsButton,
		{ &st::menuIconPermissions }
	)->setClickedCallback([=] {
		showOtherAndRememberPassword(CloudPasswordInputId());
	});
	AddButtonWithIcon(
		content,
		state->hasRecovery
			? tr::lng_settings_cloud_password_manage_email_change()
			: tr::lng_settings_cloud_password_manage_email_new(),
		st::settingsButton,
		{ &st::menuIconRecoveryEmail }
	)->setClickedCallback([=] {
		auto data = stepData();
		data.setOnlyRecoveryEmail = true;
		setStepData(std::move(data));

		showOtherAndRememberPassword(CloudPasswordEmailId());
	});
	Ui::AddSkip(content);

	using Divider = CloudPassword::OneEdgeBoxContentDivider;
	const auto divider = Ui::CreateChild<Divider>(this);
	divider->lower();
	const auto about = content->add(
		object_ptr<Ui::PaddingWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_settings_cloud_password_manage_about2(),
				st::boxDividerLabel),
		st::defaultBoxDividerLabelPadding));
	rpl::combine(
		about->geometryValue(),
		content->widthValue()
	) | rpl::start_with_next([=](QRect r, int w) {
		r.setWidth(w);
		divider->setGeometry(r);
	}, divider->lifetime());
	_isBottomFillerShown.value(
	) | rpl::start_with_next([=](bool shown) {
		divider->skipEdge(Qt::BottomEdge, shown);
	}, divider->lifetime());

	Ui::ResizeFitChild(this, content);
}

QPointer<Ui::RpWidget> Manage::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {

	const auto disable = [=](Fn<void()> close) {
		if (_requestLifetime) {
			return;
		}
		_requestLifetime = cloudPassword().set(
			_currentPassword,
			QString(),
			QString(),
			false,
			QString()
		) | rpl::start_with_error_done([=](const QString &type) {
			AbstractStep::isPasswordInvalidError(type);
		}, [=] {
			setStepData(StepData());
			close();
			showBack();
		});
	};

	auto callback = [=] {
		controller()->show(
			Ui::MakeConfirmBox({
				.text = tr::lng_settings_cloud_password_manage_disable_sure(),
				.confirmed = disable,
				.confirmText = tr::lng_settings_auto_night_disable(),
				.confirmStyle = &st::attentionBoxButton,
			}));
	};
	auto bottomButton = CloudPassword::CreateBottomDisableButton(
		parent,
		geometryValue(),
		tr::lng_settings_password_disable(),
		std::move(callback));

	_isBottomFillerShown = base::take(bottomButton.isBottomFillerShown);

	return bottomButton.content;
}

} // namespace CloudPassword

Type CloudPasswordManageId() {
	return CloudPassword::Manage::Id();
}

} // namespace Settings
