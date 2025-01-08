/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_input.h"

#include "api/api_cloud_password.h"
#include "base/qt_signal_producer.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/core_cloud_password.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_hint.h"
#include "settings/cloud_password/settings_cloud_password_manage.h"
#include "settings/cloud_password/settings_cloud_password_step.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

/*
Available actions for follow states.

CreatePassword:
– Continue to CreateHint.
– Back to Start.

ChangePassword:
– Continue to ChangeHint.
– Back to Manage.

CheckPassword:
– Continue to Manage.
– Recover to EmailConfirm.
– Reset and wait (+ Cancel reset).
– Reset now and Back to Settings.
– Back to Settings.

RecreateResetPassword:
– Continue to RecreateResetHint.
– Clear password and Back to Settings.
– Back to Settings.
*/

namespace Settings {
namespace CloudPassword {
namespace {

struct Icon {
	not_null<Lottie::Icon*> icon;
	Fn<void()> update;
};

Icon CreateInteractiveLottieIcon(
		not_null<Ui::VerticalLayout*> container,
		Lottie::IconDescriptor &&descriptor,
		style::margins padding) {
	auto object = object_ptr<Ui::RpWidget>(container);
	const auto raw = object.data();

	const auto width = descriptor.sizeOverride.width();
	raw->resize(QRect(
		QPoint(),
		descriptor.sizeOverride).marginsAdded(padding).size());

	auto owned = Lottie::MakeIcon(std::move(descriptor));
	const auto icon = owned.get();

	raw->lifetime().add([kept = std::move(owned)]{});

	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		const auto left = (raw->width() - width) / 2;
		icon->paint(p, left, padding.top());
	}, raw->lifetime());

	container->add(std::move(object));
	return { .icon = icon, .update = [=] { raw->update(); } };
}

[[nodiscard]] not_null<Ui::LinkButton*> AddLinkButton(
		not_null<Ui::VerticalLayout*> content,
		not_null<Ui::PasswordInput*> input) {
	const auto button = Ui::CreateChild<Ui::LinkButton>(
		content.get(),
		QString());

	rpl::merge(
		content->geometryValue(),
		input->geometryValue()
	) | rpl::start_with_next([=] {
		const auto topLeft = input->mapTo(content, input->pos());
		button->moveToLeft(
			input->pos().x(),
			topLeft.y() + input->height() + st::passcodeTextLine);
	}, button->lifetime());
	return button;
}

} // namespace

class Input : public TypedAbstractStep<Input> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

protected:
	[[nodiscard]] rpl::producer<std::vector<Type>> removeTypes() override;

private:
	void setupRecoverButton(
		not_null<Ui::VerticalLayout*> container,
		not_null<Ui::LinkButton*> button,
		not_null<Ui::FlatLabel*> info,
		Fn<void()> recoverCallback);

	rpl::variable<std::vector<Type>> _removesFromStack;
	rpl::lifetime _requestLifetime;

};

rpl::producer<std::vector<Type>> Input::removeTypes() {
	return _removesFromStack.value();
}

rpl::producer<QString> Input::title() {
	return tr::lng_settings_cloud_password_password_title();
}

void Input::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto currentStepData = stepData();
	const auto currentStepDataPassword = base::take(currentStepData.password);
	const auto currentStepProcessRecover = base::take(
		currentStepData.processRecover);
	setStepData(currentStepData);

	const auto currentState = cloudPassword().stateCurrent();
	const auto hasPassword = !currentStepProcessRecover.setNewPassword
		&& (currentState ? currentState->hasPassword : false);
	const auto isCheck = currentStepData.currentPassword.isEmpty()
		&& hasPassword
		&& !currentStepProcessRecover.setNewPassword;

	if (currentStepProcessRecover.setNewPassword) {
		_removesFromStack = std::vector<Type>{
			CloudPasswordEmailConfirmId()
		};
	}

	const auto icon = CreateInteractiveLottieIcon(
		content,
		{
			.name = u"cloud_password/password_input"_q,
			.sizeOverride = {
				st::settingsCloudPasswordIconSize,
				st::settingsCloudPasswordIconSize
			},
		},
		st::settingLocalPasscodeIconPadding);

	SetupHeader(
		content,
		QString(),
		rpl::never<>(),
		isCheck
			? tr::lng_settings_cloud_password_check_subtitle()
			: hasPassword
			? tr::lng_settings_cloud_password_manage_password_change()
			: tr::lng_settings_cloud_password_password_subtitle(),
		isCheck
			? tr::lng_settings_cloud_password_manage_about1()
			: tr::lng_cloud_password_about());

	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto newInput = AddPasswordField(
		content,
		isCheck
			? tr::lng_cloud_password_enter_old()
			: tr::lng_cloud_password_enter_new(),
			currentStepDataPassword);
	const auto reenterInput = isCheck
		? (Ui::PasswordInput*)(nullptr)
		: AddPasswordField(
			content,
			tr::lng_cloud_password_confirm_new(),
			currentStepDataPassword).get();
	const auto error = AddError(content, newInput);
	if (reenterInput) {
		QObject::connect(reenterInput, &Ui::MaskedInputField::changed, [=] {
			error->hide();
		});
	}

	if (isCheck) {
		AddSkipInsteadOfField(content);

		const auto hint = currentState ? currentState->hint : QString();
		const auto hintInfo = Ui::CreateChild<Ui::FlatLabel>(
			error->parentWidget(),
			tr::lng_signin_hint(tr::now, lt_password_hint, hint),
			st::defaultFlatLabel);
		hintInfo->setVisible(!hint.isEmpty());
		error->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			hintInfo->setGeometry(r);
		}, hintInfo->lifetime());
		error->shownValue(
		) | rpl::start_with_next([=](bool shown) {
			if (shown) {
				hintInfo->hide();
			} else {
				hintInfo->setVisible(!hint.isEmpty());
			}
		}, hintInfo->lifetime());

		auto recoverCallback = [=] {
			if (_requestLifetime) {
				return;
			}
			const auto state = cloudPassword().stateCurrent();
			if (!state) {
				return;
			}
			if (state->hasRecovery) {
				_requestLifetime = cloudPassword().requestPasswordRecovery(
				) | rpl::start_with_next_error([=](const QString &pattern) {
					_requestLifetime.destroy();

					auto data = stepData();
					data.processRecover = currentStepProcessRecover;
					data.processRecover.emailPattern = pattern;
					setStepData(std::move(data));
					showOther(CloudPasswordEmailConfirmId());
				}, [=](const QString &type) {
					_requestLifetime.destroy();

					error->show();
					if (MTP::IsFloodError(type)) {
						error->setText(tr::lng_flood_error(tr::now));
					} else {
						error->setText(Lang::Hard::ServerError());
					}
				});
			} else {
				const auto callback = [=](Fn<void()> close) {
					if (_requestLifetime) {
						return;
					}
					close();
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
					});
				};
				controller()->show(Ui::MakeConfirmBox({
					.text = tr::lng_cloud_password_reset_no_email(),
					.confirmed = callback,
					.confirmText = tr::lng_cloud_password_reset_ok(),
					.cancelText = tr::lng_cancel(),
					.confirmStyle = &st::attentionBoxButton,
				}));
			}
		};

		const auto recover = AddLinkButton(content, newInput);
		const auto resetInfo = Ui::CreateChild<Ui::FlatLabel>(
			content,
			QString(),
			st::boxDividerLabel);
		recover->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			resetInfo->moveToLeft(r.x(), r.y() + st::passcodeTextLine);
		}, resetInfo->lifetime());

		setupRecoverButton(
			content,
			recover,
			resetInfo,
			std::move(recoverCallback));
	} else if (currentStepProcessRecover.setNewPassword && reenterInput) {
		const auto skip = AddLinkButton(content, reenterInput);
		skip->setText(tr::lng_settings_auto_night_disable(tr::now));
		skip->setClickedCallback([=] {
			if (_requestLifetime) {
				return;
			}
			_requestLifetime = cloudPassword().recoverPassword(
				currentStepProcessRecover.checkedCode,
				QString(),
				QString()
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

				controller()->show(
					Ui::MakeInformBox(tr::lng_cloud_password_removed()));
				setStepData(StepData());
				showBack();
			});
		});
		Ui::AddSkip(content);
	}

	if (!newInput->text().isEmpty()) {
		icon.icon->jumpTo(icon.icon->framesCount() / 2, icon.update);
	}

	const auto checkPassword = [=](const QString &pass) {
		if (_requestLifetime) {
			return;
		}
		_requestLifetime = cloudPassword().check(
			pass
		) | rpl::start_with_error_done([=](const QString &type) {
			_requestLifetime.destroy();

			newInput->setFocus();
			newInput->showError();
			newInput->selectAll();
			error->show();
			if (MTP::IsFloodError(type)) {
				error->setText(tr::lng_flood_error(tr::now));
			} else if (type == u"PASSWORD_HASH_INVALID"_q
				|| type == u"SRP_PASSWORD_CHANGED"_q) {
				error->setText(tr::lng_cloud_password_wrong(tr::now));
			} else {
				error->setText(Lang::Hard::ServerError());
			}
		}, [=] {
			_requestLifetime.destroy();

			if (const auto state = cloudPassword().stateCurrent()) {
				if (state->pendingResetDate > 0) {
					auto lifetime = rpl::lifetime();
					lifetime = cloudPassword().cancelResetPassword(
					) | rpl::start_with_next([] {});
				}
			}

			auto data = stepData();
			data.currentPassword = pass;
			setStepData(std::move(data));
			showOther(CloudPasswordManageId());
		});
	};

	const auto button = AddDoneButton(
		content,
		isCheck ? tr::lng_passcode_check_button() : tr::lng_continue());
	button->setClickedCallback([=] {
		const auto newText = newInput->text();
		const auto reenterText = isCheck ? QString() : reenterInput->text();
		if (newText.isEmpty()) {
			newInput->setFocus();
			newInput->showError();
		} else if (reenterInput && reenterText.isEmpty()) {
			reenterInput->setFocus();
			reenterInput->showError();
		} else if (reenterInput && (newText != reenterText)) {
			reenterInput->setFocus();
			reenterInput->showError();
			reenterInput->selectAll();
			error->show();
			error->setText(tr::lng_cloud_password_differ(tr::now));
		} else if (isCheck) {
			checkPassword(newText);
		} else {
			auto data = stepData();
			data.processRecover = currentStepProcessRecover;
			data.password = newText;
			setStepData(std::move(data));
			showOther(CloudPasswordHintId());
		}
	});

	base::qt_signal_producer(
		newInput.get(),
		&QLineEdit::textChanged // Covers Undo.
	) | rpl::map([=] {
		return newInput->text().isEmpty();
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool empty) {
		const auto from = icon.icon->frameIndex();
		const auto to = empty ? 0 : (icon.icon->framesCount() / 2 - 1);
		icon.icon->animate(icon.update, from, to);
	}, content->lifetime());

	const auto submit = [=] {
		if (!reenterInput || reenterInput->hasFocus()) {
			button->clicked({}, Qt::LeftButton);
		} else {
			reenterInput->setFocus();
		}
	};
	QObject::connect(newInput, &Ui::MaskedInputField::submitted, submit);
	if (reenterInput) {
		using namespace Ui;
		QObject::connect(reenterInput, &MaskedInputField::submitted, submit);
	}

	setFocusCallback([=] {
		if (isCheck || newInput->text().isEmpty()) {
			newInput->setFocus();
		} else if (reenterInput->text().isEmpty()) {
			reenterInput->setFocus();
		} else {
			newInput->setFocus();
		}
	});

	Ui::ResizeFitChild(this, content);
}

void Input::setupRecoverButton(
		not_null<Ui::VerticalLayout*> container,
		not_null<Ui::LinkButton*> button,
		not_null<Ui::FlatLabel*> info,
		Fn<void()> recoverCallback) {

	struct Status {
		enum class SuggestAction {
			Recover,
			Reset,
			CancelReset,
		};
		SuggestAction suggest = SuggestAction::Recover;
		TimeId left = 0;
	};

	struct State {
		base::Timer timer;
		rpl::variable<Status> status;
	};

	const auto state = container->lifetime().make_state<State>();

	const auto updateStatus = [=] {
		const auto passwordState = cloudPassword().stateCurrent();
		const auto date = passwordState ? passwordState->pendingResetDate : 0;
		const auto left = (date - base::unixtime::now());
		state->status = Status{
			.suggest = ((left > 0)
				? Status::SuggestAction::CancelReset
				: date
				? Status::SuggestAction::Reset
				: Status::SuggestAction::Recover),
			.left = left,
		};
	};
	state->timer.setCallback(updateStatus);
	updateStatus();

	state->status.value(
	) | rpl::start_with_next([=](const Status &status) {
		switch (status.suggest) {
		case Status::SuggestAction::Recover: {
			info->setText(QString());
			button->setText(tr::lng_signin_recover(tr::now));
		} break;
		case Status::SuggestAction::Reset: {
			info->setText(QString());
			button->setText(tr::lng_cloud_password_reset_ready(tr::now));
		} break;
		case Status::SuggestAction::CancelReset: {
			info->setText(
				tr::lng_settings_cloud_password_reset_in(
					tr::now,
					lt_duration,
					Ui::FormatResetCloudPasswordIn(status.left)));
			button->setText(
				tr::lng_cloud_password_reset_cancel_title(tr::now));
		} break;
		}
	}, container->lifetime());

	cloudPassword().state(
	) | rpl::start_with_next([=](const Core::CloudPasswordState &passState) {
		updateStatus();
		state->timer.cancel();
		if (passState.pendingResetDate) {
			state->timer.callEach(999);
		}
	}, container->lifetime());

	button->setClickedCallback([=] {
		const auto passState = cloudPassword().stateCurrent();
		if (_requestLifetime || !passState) {
			return;
		}
		updateStatus();
		const auto suggest = state->status.current().suggest;
		if (suggest == Status::SuggestAction::Recover) {
			recoverCallback();
		} else if (suggest == Status::SuggestAction::CancelReset) {
			const auto cancel = [=](Fn<void()> close) {
				if (_requestLifetime) {
					return;
				}
				close();
				_requestLifetime = cloudPassword().cancelResetPassword(
				) | rpl::start_with_error_done([=](const QString &error) {
					_requestLifetime.destroy();
				}, [=] {
					_requestLifetime.destroy();
				});
			};
			controller()->show(Ui::MakeConfirmBox({
				.text = tr::lng_cloud_password_reset_cancel_sure(),
				.confirmed = cancel,
				.confirmText = tr::lng_box_yes(),
				.cancelText = tr::lng_box_no(),
			}));
		} else if (suggest == Status::SuggestAction::Reset) {
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
					return !s.hasPassword;
				}) | rpl::take(
					1
				) | rpl::start_with_next([=](const PasswordState &s) {
					_requestLifetime.destroy();
					controller()->show(Ui::MakeInformBox(
						tr::lng_cloud_password_removed()));
					setStepData(StepData());
					showBack();
				});
			});
		}
	});
}

} // namespace CloudPassword

Type CloudPasswordInputId() {
	return CloudPassword::Input::Id();
}

} // namespace Settings
