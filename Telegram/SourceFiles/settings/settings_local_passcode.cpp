/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_local_passcode.h"

#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

namespace Settings {
namespace details {

LocalPasscodeEnter::LocalPasscodeEnter(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractSection(parent)
, _controller(controller) {
}

rpl::producer<QString> LocalPasscodeEnter::title() {
	return tr::lng_settings_passcode_title();
}

void LocalPasscodeEnter::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto isCreate = (enterType() == EnterType::Create);
	const auto isCheck = (enterType() == EnterType::Check);

	auto icon = CreateLottieIcon(
		content,
		{
			.name = u"local_passcode_enter"_q,
			.sizeOverride = {
				st::changePhoneIconSize,
				st::changePhoneIconSize,
			},
		},
		st::settingLocalPasscodeIconPadding);
	content->add(std::move(icon.widget));
	_showFinished.events(
	) | rpl::start_with_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	}, content->lifetime());

	AddSkip(content);

	content->add(
		object_ptr<Ui::CenterWrap<>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				isCreate
					? tr::lng_passcode_create_title()
					: isCheck
					? tr::lng_passcode_check_title()
					: tr::lng_passcode_change_title(),
				st::changePhoneTitle)),
		st::changePhoneTitlePadding);

	const auto addDescription = [&](rpl::producer<QString> &&text) {
		const auto &st = st::settingLocalPasscodeDescription;
		content->add(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::FlatLabel>(content, std::move(text), st)),
			st::changePhoneDescriptionPadding);
	};

	addDescription(tr::lng_passcode_about1());
	AddSkip(content);
	addDescription(tr::lng_passcode_about2());

	AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto addField = [&](rpl::producer<QString> &&text) {
		const auto &st = st::settingLocalPasscodeInputField;
		auto container = object_ptr<Ui::RpWidget>(content);
		container->resize(container->width(), st.heightMin);
		const auto field = Ui::CreateChild<Ui::PasswordInput>(
			container.data(),
			st,
			std::move(text));

		container->geometryValue(
		) | rpl::start_with_next([=](const QRect &r) {
			field->moveToLeft((r.width() - field->width()) / 2, 0);
		}, container->lifetime());

		content->add(std::move(container));
		return field;
	};

	const auto addError = [&](not_null<Ui::PasswordInput*> input) {
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
		QObject::connect(input.get(), &Ui::MaskedInputField::changed, [=] {
			error->hide();
		});
		return error;
	};

	const auto newPasscode = addField(tr::lng_passcode_enter_first());

	const auto reenterPasscode = isCheck
		? (Ui::PasswordInput*)(nullptr)
		: addField(tr::lng_passcode_confirm_new());
	const auto reenterError = isCheck
		? (Ui::FlatLabel*)(nullptr)
		: addError(reenterPasscode);

	const auto button = content->add(
		object_ptr<Ui::CenterWrap<Ui::RoundButton>>(
			content,
			object_ptr<Ui::RoundButton>(
				content,
				isCreate
					? tr::lng_passcode_create_button()
					: isCheck
					? tr::lng_passcode_check_button()
					: tr::lng_passcode_change_button(),
				st::changePhoneButton)),
		st::settingLocalPasscodeButtonPadding)->entity();
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	button->setClickedCallback([=] {
		const auto newText = newPasscode->text();
		const auto reenterText = reenterPasscode
			? reenterPasscode->text()
			: QString();
		if (isCreate) {
			if (newText.isEmpty()) {
				newPasscode->setFocus();
				newPasscode->showError();
			} else if (reenterText.isEmpty()) {
				reenterPasscode->setFocus();
				reenterPasscode->showError();
			} else if (newText != reenterText) {
				reenterPasscode->setFocus();
				reenterPasscode->showError();
				reenterPasscode->selectAll();
				reenterError->show();
				reenterError->setText(tr::lng_passcode_differ(tr::now));
			} else {
				// showOther
			}
		}
	});

	_setInnerFocus.events(
	) | rpl::start_with_next([=] {
		if (newPasscode->text().isEmpty()) {
			newPasscode->setFocus();
		} else if (reenterPasscode && reenterPasscode->text().isEmpty()) {
			reenterPasscode->setFocus();
		} else {
			newPasscode->setFocus();
		}
	}, content->lifetime());

	Ui::ResizeFitChild(this, content);
}

void LocalPasscodeEnter::showFinished() {
	_showFinished.fire({});
}

void LocalPasscodeEnter::setInnerFocus() {
	_setInnerFocus.fire({});
}

LocalPasscodeEnter::~LocalPasscodeEnter() = default;

} // namespace details
} // namespace Settings
