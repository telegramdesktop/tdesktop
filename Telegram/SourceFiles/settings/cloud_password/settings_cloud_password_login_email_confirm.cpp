/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_login_email_confirm.h"

#include "api/api_cloud_password.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/core_cloud_password.h"
#include "intro/intro_code_input.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_login_email.h"
#include "settings/cloud_password/settings_cloud_password_step.h"
#include "ui/boxes/boost_box.h" // Ui::StartFireworks.
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/sent_code_field.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

/*
Available actions for follow states.

LoginEmailConfirm from LoginEmail:
– Continue to Settings.
– Back to LoginEmail.
*/

namespace Settings {
namespace CloudPassword {

class LoginEmailConfirm : public TypedAbstractStep<LoginEmailConfirm> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;

	void setupContent();

protected:
	[[nodiscard]] rpl::producer<std::vector<Type>> removeTypes() override;

private:
	QString _collectedCode;
	std::optional<MTP::Sender> _api;

	rpl::event_stream<> _processFinishes;

};

rpl::producer<std::vector<Type>> LoginEmailConfirm::removeTypes() {
	return _processFinishes.events() | rpl::map([] {
		return std::vector<Type>{ CloudLoginEmailId() };
	});
}

rpl::producer<QString> LoginEmailConfirm::title() {
	return tr::lng_settings_cloud_login_email_section_title();
}

void LoginEmailConfirm::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto currentStepData = stepData();
	const auto currentStepDataCodeLength = base::take(
		currentStepData.unconfirmedEmailLengthCode);
	const auto newEmail = currentStepData.email;
	setStepData(currentStepData);

	if (!currentStepDataCodeLength) {
		setStepData(StepData());
		showBack();
		return;
	}
	cloudPassword().state(
	) | rpl::start_with_next([=](const Core::CloudPasswordState &state) {
		if (state.loginEmailPattern.isEmpty()) {
			setStepData(StepData());
			showBack();
		}
	}, lifetime());

	SetupHeader(
		content,
		u"cloud_password/email"_q,
		showFinishes(),
		tr::lng_settings_cloud_login_email_code_title(),
		tr::lng_settings_cloud_login_email_code_about(
			lt_email,
			rpl::single(Ui::Text::WrapEmailPattern(newEmail)),
			TextWithEntities::Simple));

	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto newInput = content->add(
		object_ptr<Ui::CodeInput>(content),
		style::al_top);
	newInput->setDigitsCountMax(currentStepDataCodeLength);

	Ui::AddSkip(content);
	const auto error = AddError(content, nullptr);
	AddSkipInsteadOfField(content);

	const auto submit = [=] {
		_api.emplace(&controller()->session().mtp());
		const auto newText = _collectedCode;
		if (newText.isEmpty()) {
			newInput->setFocus();
			newInput->showError();
		} else {
			const auto weak = base::make_weak(controller()->content());
			const auto done = [=] {
				_api.reset();
				_processFinishes.fire({});
				cloudPassword().reload();
				setStepData(StepData());
				showBack();
				if (const auto strong = weak.get()) {
					Ui::StartFireworks(strong);
				}
			};
			const auto fail = [=](const QString &type) {
				_api.reset();

				newInput->setFocus();
				newInput->showError();
				error->show();

				if (MTP::IsFloodError(type)) {
					error->setText(tr::lng_flood_error(tr::now));
				} else if (type == u"EMAIL_NOT_ALLOWED"_q) {
					error->setText(
						tr::lng_settings_error_email_not_alowed(tr::now));
				} else if (type == u"CODE_INVALID"_q) {
					error->setText(tr::lng_signin_wrong_code(tr::now));
				} else if (type == u"EMAIL_HASH_EXPIRED"_q) {
					// Show box?
					error->setText(Lang::Hard::EmailConfirmationExpired());
				} else {
					error->setText(Lang::Hard::ServerError());
				}
			};
			Api::VerifyLoginEmail(*_api, newText, done, fail);
		}
	};

	newInput->codeCollected(
	) | rpl::start_with_next([=](const QString &code) {
		_collectedCode = code;
		error->hide();
		submit();
	}, lifetime());

	setFocusCallback([=] { newInput->setFocus(); });

	Ui::ResizeFitChild(this, content);
}

} // namespace CloudPassword

Type CloudLoginEmailConfirmId() {
	return CloudPassword::LoginEmailConfirm::Id();
}

} // namespace Settings
