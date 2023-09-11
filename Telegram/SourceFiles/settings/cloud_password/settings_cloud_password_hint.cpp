/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_hint.h"

#include "api/api_cloud_password.h"
#include "lang/lang_keys.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_email.h"
#include "settings/cloud_password/settings_cloud_password_manage.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

/*
Available actions for follow states.

CreateHint:
– Continue to Email.
– Skip to Email.
– Back to CreatePassword.

ChangeHint:
– Continue to Email.
– Skip to Email.
– Back to ChangePassword.

RecreateResetHint:
– Continue to Manage.
– Skip to Manage.
– Back to RecreateResetPassword.
*/

namespace Settings {
namespace CloudPassword {

class Hint : public TypedAbstractStep<Hint> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

private:
	rpl::lifetime _requestLifetime;

};

rpl::producer<QString> Hint::title() {
	return tr::lng_settings_cloud_password_hint_title();
}

void Hint::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto currentStepData = stepData();
	const auto currentStepDataHint = base::take(currentStepData.hint);
	setStepData(currentStepData);

	SetupHeader(
		content,
		u"cloud_password/hint"_q,
		showFinishes(),
		tr::lng_settings_cloud_password_hint_subtitle(),
		tr::lng_settings_cloud_password_hint_about());

	AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto wrap = AddWrappedField(
		content,
		tr::lng_cloud_password_hint(),
		currentStepDataHint);
	const auto newInput = wrap->entity();
	const auto error = AddError(content, nullptr);
	newInput->changes(
	) | rpl::start_with_next([=] {
		error->hide();
	}, newInput->lifetime());
	AddSkipInsteadOfField(content);

	const auto save = [=](const QString &hint) {
		if (currentStepData.processRecover.setNewPassword) {
			if (_requestLifetime) {
				return;
			}
			_requestLifetime = cloudPassword().recoverPassword(
				currentStepData.processRecover.checkedCode,
				currentStepData.password,
				hint
			) | rpl::start_with_error_done([=](const QString &type) {
				_requestLifetime.destroy();

				error->show();
				if (MTP::IsFloodError(type)) {
					error->setText(tr::lng_flood_error(tr::now));
				} else {
					error->setText(Lang::Hard::ServerError());
				}
			}, [=] {
				_requestLifetime.destroy();

				auto empty = StepData();
				empty.currentPassword = stepData().password;
				setStepData(std::move(empty));
				showOther(CloudPasswordManageId());
			});
		} else {
			auto data = stepData();
			data.hint = hint;
			setStepData(std::move(data));
			showOther(CloudPasswordEmailId());
		}
	};

	AddLinkButton(
		wrap,
		tr::lng_settings_cloud_password_skip_hint()
	)->setClickedCallback([=] {
		save(QString());
	});

	const auto button = AddDoneButton(content, tr::lng_continue());
	button->setClickedCallback([=] {
		const auto newText = newInput->getLastText();
		if (newText.isEmpty()) {
			newInput->setFocus();
			newInput->showError();
		} else if (newText == stepData().password) {
			error->show();
			error->setText(tr::lng_cloud_password_bad(tr::now));
			newInput->setFocus();
			newInput->showError();
		} else {
			save(newText);
		}
	});

	const auto submit = [=] { button->clicked({}, Qt::LeftButton); };
	newInput->submits() | rpl::start_with_next(submit, newInput->lifetime());

	setFocusCallback([=] { newInput->setFocus(); });

	Ui::ResizeFitChild(this, content);
}

} // namespace CloudPassword

Type CloudPasswordHintId() {
	return CloudPassword::Hint::Id();
}

} // namespace Settings
