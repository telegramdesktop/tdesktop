/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"

#include "api/api_cloud_password.h"
#include "base/unixtime.h"
#include "core/core_cloud_password.h"
#include "lang/lang_keys.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_email.h"
#include "settings/cloud_password/settings_cloud_password_hint.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/cloud_password/settings_cloud_password_manage.h"
#include "settings/cloud_password/settings_cloud_password_start.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/sent_code_field.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

/*
Available actions for follow states.

CreateEmailConfirm from CreateEmail:
– Continue to Manage.
– Abort to Settings.
– Back to Settings.

ChangeEmailConfirm from ChangeEmail:
– Continue to Manage.
– Abort to Settings.
– Back to Settings.

Recover from CreatePassword:
– Continue to RecreateResetPassword.
– Back to Settings.
*/

namespace Settings {
namespace CloudPassword {

class EmailConfirm : public TypedAbstractStep<EmailConfirm> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

protected:
	[[nodiscard]] rpl::producer<std::vector<Type>> removeTypes() override;

private:
	rpl::lifetime _requestLifetime;

};

rpl::producer<QString> EmailConfirm::title() {
	return tr::lng_settings_cloud_password_email_title();
}

rpl::producer<std::vector<Type>> EmailConfirm::removeTypes() {
	return rpl::single(std::vector<Type>{
		CloudPasswordStartId(),
		CloudPasswordInputId(),
		CloudPasswordHintId(),
		CloudPasswordEmailId(),
		CloudPasswordEmailConfirmId(),
		CloudPasswordManageId(),
	});
}

void EmailConfirm::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto currentStepData = stepData();
	const auto currentStepDataCodeLength = base::take(
		currentStepData.unconfirmedEmailLengthCode);
	// If we go back from Email Confirm to Privacy Settings
	// we should forget the current password.
	const auto currentPassword = base::take(currentStepData.currentPassword);
	const auto typedPassword = base::take(currentStepData.password);
	const auto recoverEmailPattern = base::take(
		currentStepData.processRecover.emailPattern);
	setStepData(currentStepData);

	const auto state = cloudPassword().stateCurrent();
	if (!state) {
		setStepData(StepData());
		showBack();
		return;
	}
	cloudPassword().state(
	) | rpl::start_with_next([=](const Core::CloudPasswordState &state) {
		if (!_requestLifetime
			&& state.unconfirmedPattern.isEmpty()
			&& recoverEmailPattern.isEmpty()) {
			setStepData(StepData());
			showBack();
		}
	}, lifetime());

	SetupHeader(
		content,
		u"cloud_password/email"_q,
		showFinishes(),
		state->unconfirmedPattern.isEmpty()
			? tr::lng_settings_cloud_password_email_recovery_subtitle()
			: tr::lng_cloud_password_confirm(),
		rpl::single(
			tr::lng_cloud_password_waiting_code(
				tr::now,
				lt_email,
				state->unconfirmedPattern.isEmpty()
					? recoverEmailPattern
					: state->unconfirmedPattern)));

	AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	auto objectInput = object_ptr<Ui::SentCodeField>(
		content,
		st::settingLocalPasscodeInputField,
		tr::lng_change_phone_code_title());
	const auto newInput = objectInput.data();
	const auto wrap = content->add(
		object_ptr<Ui::CenterWrap<Ui::InputField>>(
			content,
			std::move(objectInput)));

	const auto error = AddError(content, nullptr);
	newInput->changes(
	) | rpl::start_with_next([=] {
		error->hide();
	}, newInput->lifetime());
	AddSkipInsteadOfField(content);

	const auto resendInfo = Ui::CreateChild<Ui::FlatLabel>(
		error->parentWidget(),
		tr::lng_cloud_password_resent(tr::now),
		st::changePhoneLabel);
	resendInfo->hide();
	error->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		resendInfo->setGeometry(r);
	}, resendInfo->lifetime());
	error->shownValue(
	) | rpl::start_with_next([=](bool shown) {
		if (shown) {
			resendInfo->hide();
		}
	}, resendInfo->lifetime());

	const auto resend = AddLinkButton(wrap, tr::lng_cloud_password_resend());
	resend->setClickedCallback([=] {
		if (_requestLifetime) {
			return;
		}
		_requestLifetime = cloudPassword().resendEmailCode(
		) | rpl::start_with_error_done([=](const QString &type) {
			_requestLifetime.destroy();

			error->show();
			error->setText(Lang::Hard::ServerError());
		}, [=] {
			_requestLifetime.destroy();

			error->hide();
			resendInfo->show();
			newInput->hideError();
		});
	});

	if (!recoverEmailPattern.isEmpty()) {
		resend->setText(tr::lng_signin_try_password(tr::now));

		resend->setClickedCallback([=] {
			const auto reset = [=](Fn<void()> close) {
				if (_requestLifetime) {
					return;
				}
				_requestLifetime = cloudPassword().resetPassword(
				) | rpl::start_with_next_error_done([=](
						Api::CloudPassword::ResetRetryDate retryDate) {
					_requestLifetime.destroy();
					const auto left = std::max(
						retryDate - base::unixtime::now(),
						60);
					controller()->show(Ui::MakeInformBox(
						tr::lng_cloud_password_reset_later(
							tr::now,
							lt_duration,
							Ui::FormatResetCloudPasswordIn(left))));
				}, [=](const QString &type) {
					_requestLifetime.destroy();
				}, [=] {
					_requestLifetime.destroy();

					cloudPassword().reload();
					using PasswordState = Core::CloudPasswordState;
					_requestLifetime = cloudPassword().state(
					) | rpl::filter([=](const PasswordState &s) {
						return s.pendingResetDate != 0;
					}) | rpl::take(
						1
					) | rpl::start_with_next([=](const PasswordState &s) {
						const auto left = (s.pendingResetDate
							- base::unixtime::now());
						if (left > 0) {
							_requestLifetime.destroy();
							controller()->show(Ui::MakeInformBox(
								tr::lng_settings_cloud_password_reset_in(
									tr::now,
									lt_duration,
									Ui::FormatResetCloudPasswordIn(left))));
							setStepData(StepData());
							showBack();
						}
					});
				});
				_requestLifetime.add(close);
			};

			controller()->show(Ui::MakeConfirmBox({
				.text = tr::lng_cloud_password_reset_with_email(),
				.confirmed = reset,
				.confirmText = tr::lng_cloud_password_reset_ok(),
				.confirmStyle = &st::attentionBoxButton,
			}));
		});
	}

	const auto button = AddDoneButton(
		content,
		recoverEmailPattern.isEmpty()
			? tr::lng_settings_cloud_password_email_confirm()
			: tr::lng_passcode_check_button());
	button->setClickedCallback([=] {
		const auto newText = newInput->getDigitsOnly();
		if (newText.isEmpty()) {
			newInput->setFocus();
			newInput->showError();
		} else if (!_requestLifetime && recoverEmailPattern.isEmpty()) {
			_requestLifetime = cloudPassword().confirmEmail(
				newText
			) | rpl::start_with_error_done([=](const QString &type) {
				_requestLifetime.destroy();

				newInput->setFocus();
				newInput->showError();
				error->show();

				if (MTP::IsFloodError(type)) {
					error->setText(tr::lng_flood_error(tr::now));
				} else if (type == u"CODE_INVALID"_q) {
					error->setText(tr::lng_signin_wrong_code(tr::now));
				} else if (type == u"EMAIL_HASH_EXPIRED"_q) {
					// Show box?
					error->setText(Lang::Hard::EmailConfirmationExpired());
				} else {
					error->setText(Lang::Hard::ServerError());
				}
			}, [=] {
				_requestLifetime.destroy();

				auto empty = StepData();
				const auto anyPassword = currentPassword.isEmpty()
					? typedPassword
					: currentPassword;
				empty.currentPassword = anyPassword;
				setStepData(std::move(empty));
				// If we don't have the current password
				// Then we should go to Privacy Settings.
				if (anyPassword.isEmpty()) {
					showBack();
				} else {
					showOther(CloudPasswordManageId());
				}
			});
		} else if (!_requestLifetime) {
			_requestLifetime = cloudPassword().checkRecoveryEmailAddressCode(
				newText
			) | rpl::start_with_error_done([=](const QString &type) {
				_requestLifetime.destroy();

				newInput->setFocus();
				newInput->showError();
				error->show();

				if (MTP::IsFloodError(type)) {
					error->setText(tr::lng_flood_error(tr::now));
					return;
				}

				if (type == u"PASSWORD_RECOVERY_NA"_q) {
					setStepData(StepData());
					showBack();
				} else if (type == u"PASSWORD_RECOVERY_EXPIRED"_q) {
					setStepData(StepData());
					showBack();
				} else if (type == u"CODE_INVALID"_q) {
					error->setText(tr::lng_signin_wrong_code(tr::now));
				} else {
					error->setText(Logs::DebugEnabled()
						// internal server error
						? type
						: Lang::Hard::ServerError());
				}
			}, [=] {
				_requestLifetime.destroy();

				auto empty = StepData();
				empty.processRecover.checkedCode = newText;
				empty.processRecover.setNewPassword = true;
				setStepData(std::move(empty));
				showOther(CloudPasswordInputId());
			});
		}
	});

	const auto submit = [=] { button->clicked({}, Qt::LeftButton); };
	newInput->setAutoSubmit(currentStepDataCodeLength, submit);
	newInput->submits() | rpl::start_with_next(submit, newInput->lifetime());

	setFocusCallback([=] { newInput->setFocus(); });

	Ui::ResizeFitChild(this, content);
}

} // namespace CloudPassword

Type CloudPasswordEmailConfirmId() {
	return CloudPassword::EmailConfirm::Id();
}

} // namespace Settings
