/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_input.h"

#include "api/api_cloud_password.h"
#include "base/qt_signal_producer.h"
#include "core/core_cloud_password.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_hint.h"
#include "settings/cloud_password/settings_cloud_password_manage.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"

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

	[[nodiscard]] rpl::producer<std::vector<Type>> removeFromStack() override;

private:
	rpl::variable<std::vector<Type>> _removesFromStack;
	rpl::lifetime _requestLifetime;

};

rpl::producer<std::vector<Type>> Input::removeFromStack() {
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
				st::changePhoneIconSize,
				st::changePhoneIconSize
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
		tr::lng_cloud_password_about());

	AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

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

		const auto recover = AddLinkButton(content, newInput);
		recover->setText(tr::lng_signin_recover(tr::now));
		recover->setClickedCallback([=] {
			if (_requestLifetime) {
				return;
			}
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
		});
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
		AddSkip(content);
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

} // namespace CloudPassword

Type CloudPasswordInputId() {
	return CloudPassword::Input::Id();
}

} // namespace Settings
