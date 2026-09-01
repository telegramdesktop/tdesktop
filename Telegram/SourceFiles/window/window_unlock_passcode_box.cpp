/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_unlock_passcode_box.h"

#include "core/application.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/vertical_list.h"
#include "window/window_lock_widgets.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_window_lock_widgets.h"

namespace Window {
namespace {

// Both an unlock from another window and the box's own successful submit
// arrive here, because unlockPasscode() clears the lock synchronously and
// fires passcodeLockChanges() from the inside. So the action runs once.
void Finish(
		not_null<Ui::GenericBox*> box,
		not_null<Fn<void()>*> deferred) {
	const auto weak = base::make_weak(box);
	if (const auto onstack = base::take(*deferred)) {
		onstack();
	}
	if (const auto strong = weak.get()) {
		strong->closeBox();
	}
}

void Submit(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::PasswordInput*> field,
		not_null<Ui::FlatLabel*> error,
		Fn<void()> finish) {
	const auto fail = [&](const QString &text) {
		error->setText(text);
		error->show();
	};
	switch (TryPasscode(field->text())) {
	case PasscodeAttempt::Empty:
		field->showError();
		return;
	case PasscodeAttempt::Flood:
		field->showError();
		fail(tr::lng_flood_error(tr::now));
		return;
	case PasscodeAttempt::Wrong:
		field->selectAll();
		field->showError();
		fail(tr::lng_passcode_wrong(tr::now));
		return;
	case PasscodeAttempt::Correct:
		break;
	}
	const auto weak = base::make_weak(box);
	Core::App().unlockPasscode();
	if (weak) {
		finish();
	}
}

void UnlockPasscodeBox(
		not_null<Ui::GenericBox*> box,
		UnlockPasscodeBoxStyle st,
		Fn<void()> unlocked) {
	box->setStyle(*st.box);
	box->setWidth(st::boxWideWidth);
	box->setNoContentMargin(true);
	box->addTopButton(*st.close, [=] { box->closeBox(); });

	const auto deferred = box->lifetime().make_state<Fn<void()>>(
		std::move(unlocked));
	const auto finish = [=] { Finish(box, deferred); };
	Core::App().passcodeLockChanges(
	) | rpl::filter(!rpl::mappers::_1) | rpl::on_next([=] {
		finish();
	}, box->lifetime());

	const auto content = box->verticalLayout();
	auto icon = Settings::CreateLottieIcon(
		content,
		{
			.name = u"local_passcode_enter"_q,
			.sizeOverride = st::normalBoxLottieSize,
		},
		st::unlockPasscodeIconPadding);
	content->add(std::move(icon.widget), {}, style::al_top);
	box->setShowFinishedCallback([animate = std::move(icon.animate)] {
		animate(anim::repeat::once);
	});

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_passcode_check_title(),
			st.box->title),
		st::boxRowPadding + st::unlockPasscodeTitlePadding,
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_passcode_unlock_about(),
			*st.description),
		st::boxRowPadding,
		style::al_top
	)->setTryMakeSimilarLines(true);

	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	const auto field = Settings::CloudPassword::AddPasswordField(
		content,
		tr::lng_passcode_enter(),
		QString(),
		*st.field);
	const auto error = Settings::CloudPassword::AddError(
		content,
		field,
		*st.error,
		st::boxRowPadding + st::unlockPasscodeErrorPadding);

	const auto submit = [=] { Submit(box, field, error, finish); };
	QObject::connect(field, &Ui::MaskedInputField::submitted, submit);
	box->addButton(tr::lng_passcode_submit(), submit);
	box->setFocusCallback([=] { field->setFocusFast(); });
}

} // namespace

bool ShowUnlockPasscodeBox(
		std::shared_ptr<Ui::Show> show,
		UnlockPasscodeBoxStyle st,
		Fn<void()> unlocked) {
	if (!Core::App().passcodeLocked()) {
		return false;
	}
	show->showBox(Box(UnlockPasscodeBox, st, std::move(unlocked)));
	return true;
}

} // namespace Window
