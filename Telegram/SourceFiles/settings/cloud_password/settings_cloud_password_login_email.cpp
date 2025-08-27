/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_login_email.h"

#include "api/api_cloud_password.h"
#include "core/core_cloud_password.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_login_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_manage.h"
#include "settings/cloud_password/settings_cloud_password_step.h"
#include "ui/boxes/confirm_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace CloudPassword {

class LoginEmail : public TypedAbstractStep<LoginEmail> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

private:
	rpl::lifetime _requestLifetime;
	std::optional<MTP::Sender> _api;
	rpl::variable<bool> _confirmButtonBusy = false;

};

rpl::producer<QString> LoginEmail::title() {
	return tr::lng_settings_cloud_login_email_section_title();
}

void LoginEmail::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	auto currentStepData = stepData();
	const auto newEmail = base::take(currentStepData.email);
	setStepData(currentStepData);

	SetupHeader(
		content,
		u"cloud_password/email"_q,
		showFinishes(),
		tr::lng_settings_cloud_login_email_title(),
		tr::lng_settings_cloud_login_email_about());

	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto newInput = AddWrappedField(
		content,
		tr::lng_settings_cloud_login_email_placeholder(),
		QString());
	const auto error = AddError(content, nullptr);
	newInput->changes() | rpl::start_with_next([=] {
		error->hide();
	}, newInput->lifetime());
	newInput->setText(newEmail);
	if (newInput->hasText()) {
		newInput->selectAll();
	}
	AddSkipInsteadOfField(content);

	const auto send = [=] {
		Expects(_api == std::nullopt);

		_confirmButtonBusy = true;
		_api.emplace(&controller()->session().mtp());

		const auto data = stepData();

		const auto done = [=](int length, const QString &pattern) {
			_api.reset();
			_confirmButtonBusy = false;
			auto data = stepData();
			data.unconfirmedEmailLengthCode = length;
			setStepData(std::move(data));
			showOther(CloudLoginEmailConfirmId());
		};
		const auto fail = [=](const QString &type) {
			_api.reset();
			_confirmButtonBusy = false;

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
		};

		Api::RequestLoginEmailCode(*_api, data.email, done, fail);
	};

	const auto confirm = [=](const QString &email) {
		if (_confirmButtonBusy.current()) {
			return;
		}

		auto data = stepData();
		data.email = email;
		setStepData(std::move(data));

		if (!email.isEmpty()) {
			send();
			return;
		}
	};

	const auto button = AddDoneButton(
		content,
		rpl::conditional(
			_confirmButtonBusy.value(),
			rpl::single(QString()),
			tr::lng_settings_cloud_login_email_confirm()));
	button->setClickedCallback([=] {
		const auto newText = newInput->getLastText();
		if (newText.isEmpty()) {
			newInput->setFocus();
			newInput->showError();
		} else {
			confirm(newText);
		}
	});
	{
		using namespace Info::Statistics;
		const auto loadingAnimation = InfiniteRadialAnimationWidget(
			button,
			st::giveawayGiftCodeStartButton.height / 2);
		AddChildToWidgetCenter(button, loadingAnimation);
		loadingAnimation->showOn(_confirmButtonBusy.value());
	}

	const auto submit = [=] { button->clicked({}, Qt::LeftButton); };
	newInput->submits() | rpl::start_with_next(submit, newInput->lifetime());

	setFocusCallback([=] { newInput->setFocus(); });

	Ui::ResizeFitChild(this, content);
}

} // namespace CloudPassword

Type CloudLoginEmailId() {
	return CloudPassword::LoginEmail::Id();
}

} // namespace Settings
