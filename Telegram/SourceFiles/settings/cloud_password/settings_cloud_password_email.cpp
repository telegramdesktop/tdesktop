/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_email.h"

#include "api/api_cloud_password.h"
#include "core/core_cloud_password.h"
#include "lang/lang_keys.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_manage.h"
#include "settings/cloud_password/settings_cloud_password_step.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

/*
Available actions for follow states.

CreateEmail:
– Continue to EmailConfirm.
+ Continue to Manage if Email is confirmed already.
– Warn and Skip to Manage.
– Back to CreateHint.

ChangeEmail from ChangePassword:
– Continue to EmailConfirm.
+ Continue to Manage if Email is confirmed already.
– Warn and Skip to Manage.
– Back to ChangeHint.

ChangeEmail from Manage:
– Continue to EmailConfirm.
+ Continue to Manage if Email is confirmed already.
– Back to Manage.
*/

namespace Settings {
namespace CloudPassword {

class Email : public TypedAbstractStep<Email> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

private:
	rpl::lifetime _requestLifetime;

};

rpl::producer<QString> Email::title() {
	return tr::lng_settings_cloud_password_email_title();
}

void Email::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto currentStepData = stepData();
	const auto currentStepDataEmail = base::take(currentStepData.email);
	const auto setOnly = base::take(currentStepData.setOnlyRecoveryEmail);
	setStepData(currentStepData);

	const auto state = cloudPassword().stateCurrent();
	const auto hasRecovery = state && state->hasRecovery;

	SetupHeader(
		content,
		u"cloud_password/email"_q,
		showFinishes(),
		hasRecovery
			? tr::lng_settings_cloud_password_manage_email_change()
			: tr::lng_settings_cloud_password_email_subtitle(),
		tr::lng_settings_cloud_password_email_about());

	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto wrap = AddWrappedField(
		content,
		tr::lng_cloud_password_email(),
		currentStepDataEmail);
	const auto newInput = wrap->entity();
	const auto error = AddError(content, nullptr);
	newInput->changes(
	) | rpl::start_with_next([=] {
		error->hide();
	}, newInput->lifetime());
	AddSkipInsteadOfField(content);

	const auto send = [=](Fn<void()> close) {
		Expects(!_requestLifetime);

		const auto data = stepData();

		_requestLifetime = (setOnly
			? cloudPassword().setEmail(data.currentPassword, data.email)
			: cloudPassword().set(
				data.currentPassword,
				data.password,
				data.hint,
				!data.email.isEmpty(),
				data.email)
		) | rpl::start_with_next_error_done([=](Api::CloudPassword::SetOk d) {
			_requestLifetime.destroy();

			auto data = stepData();
			data.unconfirmedEmailLengthCode = d.unconfirmedEmailLengthCode;
			setStepData(std::move(data));
			showOther(CloudPasswordEmailConfirmId());
		}, [=](const QString &type) {
			_requestLifetime.destroy();

			if (MTP::IsFloodError(type)) {
				error->show();
				error->setText(tr::lng_flood_error(tr::now));
			} else if (AbstractStep::isPasswordInvalidError(type)) {
			} else if (type == u"EMAIL_INVALID"_q) {
				error->show();
				error->setText(tr::lng_cloud_password_bad_email(tr::now));
				newInput->setFocus();
				newInput->showError();
				newInput->selectAll();
			}
		}, [=] {
			_requestLifetime.destroy();

			auto empty = StepData();
			empty.currentPassword = stepData().password.isEmpty()
				? stepData().currentPassword
				: stepData().password;
			setStepData(std::move(empty));
			showOther(CloudPasswordManageId());
		});

		if (close) {
			_requestLifetime.add(close);
		}
	};

	const auto confirm = [=](const QString &email) {
		if (_requestLifetime) {
			return;
		}

		auto data = stepData();
		data.email = email;
		setStepData(std::move(data));

		if (!email.isEmpty()) {
			send(nullptr);
			return;
		}

		controller()->show(Ui::MakeConfirmBox({
			.text = { tr::lng_cloud_password_about_recover() },
			.confirmed = crl::guard(this, send),
			.confirmText = tr::lng_cloud_password_skip_email(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};

	const auto skip = AddLinkButton(
		wrap,
		tr::lng_cloud_password_skip_email());
	skip->setClickedCallback([=] {
		confirm(QString());
	});
	skip->setVisible(!setOnly);

	const auto button = AddDoneButton(
		content,
		tr::lng_settings_cloud_password_save());
	button->setClickedCallback([=] {
		const auto newText = newInput->getLastText();
		if (newText.isEmpty()) {
			newInput->setFocus();
			newInput->showError();
		} else {
			confirm(newText);
		}
	});

	const auto submit = [=] { button->clicked({}, Qt::LeftButton); };
	newInput->submits() | rpl::start_with_next(submit, newInput->lifetime());

	setFocusCallback([=] { newInput->setFocus(); });

	Ui::ResizeFitChild(this, content);
}

} // namespace CloudPassword

Type CloudPasswordEmailId() {
	return CloudPassword::Email::Id();
}

} // namespace Settings
