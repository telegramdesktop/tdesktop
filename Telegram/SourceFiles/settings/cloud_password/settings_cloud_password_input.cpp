/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_input.h"

#include "base/qt_signal_producer.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_hint.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
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

} // namespace

class Input : public TypedAbstractStep<Input> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

};

rpl::producer<QString> Input::title() {
	return tr::lng_settings_cloud_password_password_title();
}

void Input::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	auto currentStepData = stepData();
	const auto currentStepDataPassword = base::take(currentStepData.password);
	setStepData(currentStepData);

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
		tr::lng_settings_cloud_password_password_subtitle(),
		tr::lng_cloud_password_about());

	AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto newInput = AddPasswordField(
		content,
		tr::lng_cloud_password_enter_new(),
		currentStepDataPassword);
	const auto reenterInput = AddPasswordField(
		content,
		tr::lng_cloud_password_confirm_new(),
		currentStepDataPassword);
	const auto error = AddError(content, reenterInput);

	if (!newInput->text().isEmpty()) {
		icon.icon->jumpTo(icon.icon->framesCount() / 2, icon.update);
	}

	const auto button = AddDoneButton(content, tr::lng_continue());
	button->setClickedCallback([=] {
		const auto newText = newInput->text();
		const auto reenterText = reenterInput->text();
		if (newText.isEmpty()) {
			newInput->setFocus();
			newInput->showError();
		} else if (reenterText.isEmpty()) {
			reenterInput->setFocus();
			reenterInput->showError();
		} else if (newText != reenterText) {
			reenterInput->setFocus();
			reenterInput->showError();
			reenterInput->selectAll();
			error->show();
			error->setText(tr::lng_cloud_password_differ(tr::now));
		} else {
			auto data = stepData();
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
		if (reenterInput->hasFocus()) {
			button->clicked({}, Qt::LeftButton);
		} else {
			reenterInput->setFocus();
		}
	};
	QObject::connect(newInput, &Ui::MaskedInputField::submitted, submit);
	QObject::connect(reenterInput, &Ui::MaskedInputField::submitted, submit);

	setFocusCallback([=] {
		if (newInput->text().isEmpty()) {
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
