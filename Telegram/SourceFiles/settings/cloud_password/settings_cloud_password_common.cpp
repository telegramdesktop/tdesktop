/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_common.h"

#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings::CloudPassword {

void SetupHeader(
		not_null<Ui::VerticalLayout*> content,
		const QString &lottie,
		rpl::producer<> &&showFinished,
		rpl::producer<QString> &&subtitle,
		rpl::producer<QString> &&about) {
	if (!lottie.isEmpty()) {
		const auto &size = st::settingsCloudPasswordIconSize;
		auto icon = CreateLottieIcon(
			content,
			{ .name = lottie, .sizeOverride = { size, size } },
			st::settingLocalPasscodeIconPadding);
		content->add(std::move(icon.widget));
		std::move(
			showFinished
		) | rpl::start_with_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, content->lifetime());

		AddSkip(content);
	}

	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				std::move(subtitle),
				st::changePhoneTitle)),
		st::changePhoneTitlePadding);

	{
		const auto &st = st::settingLocalPasscodeDescription;
		content->add(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::FlatLabel>(content, std::move(about), st)),
			st::changePhoneDescriptionPadding);
	}
}

not_null<Ui::PasswordInput*> AddPasswordField(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> &&placeholder,
		const QString &text) {
	const auto &st = st::settingLocalPasscodeInputField;
	auto container = object_ptr<Ui::RpWidget>(content);
	container->resize(container->width(), st.heightMin);
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		container.data(),
		st,
		std::move(placeholder),
		text);

	container->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		field->moveToLeft((r.width() - field->width()) / 2, 0);
	}, container->lifetime());

	content->add(std::move(container));
	return field;
}

not_null<Ui::FlatLabel*> AddError(
		not_null<Ui::VerticalLayout*> content,
		Ui::PasswordInput *input) {
	const auto error = content->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				// Set any text to resize.
				tr::lng_language_name(tr::now),
				st::settingLocalPasscodeError)),
		st::changePhoneDescriptionPadding)->entity();
	error->hide();
	if (input) {
		QObject::connect(input, &Ui::MaskedInputField::changed, [=] {
			error->hide();
		});
	}
	return error;
};

not_null<Ui::RoundButton*> AddDoneButton(
		not_null<Ui::VerticalLayout*> content,
		rpl::producer<QString> &&text) {
	const auto button = content->add(
		object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
			content,
			object_ptr<Ui::RoundButton>(
				content,
				std::move(text),
				st::changePhoneButton)),
		st::settingLocalPasscodeButtonPadding)->entity();
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	return button;
}

void AddSkipInsteadOfField(not_null<Ui::VerticalLayout*> content) {
	AddSkip(content, st::settingLocalPasscodeInputField.heightMin);
}

void AddSkipInsteadOfError(not_null<Ui::VerticalLayout*> content) {
	auto dummy = base::make_unique_q<Ui::FlatLabel>(
		content,
		tr::lng_language_name(tr::now),
		st::settingLocalPasscodeError);
	AddSkip(content, dummy->height());
	dummy = nullptr;
}

AbstractStep::AbstractStep(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractSection(parent)
, _controller(controller) {
}

not_null<Window::SessionController*> AbstractStep::controller() const {
	return _controller;
}

void AbstractStep::showBack() {
	_showBack.fire({});
}

void AbstractStep::showOther(Type type) {
	_showOther.fire_copy(type);
}

void AbstractStep::setFocusCallback(Fn<void()> callback) {
	_setInnerFocusCallback = callback;
}

rpl::producer<> AbstractStep::showFinishes() const {
	return _showFinished.events();
}

void AbstractStep::showFinished() {
	_showFinished.fire({});
}

void AbstractStep::setInnerFocus() {
	if (_setInnerFocusCallback) {
		_setInnerFocusCallback();
	}
}

rpl::producer<Type> AbstractStep::sectionShowOther() {
	return _showOther.events();
}

rpl::producer<> AbstractStep::sectionShowBack() {
	return _showBack.events();
}

AbstractStep::~AbstractStep() = default;

} // namespace Settings::CloudPassword
