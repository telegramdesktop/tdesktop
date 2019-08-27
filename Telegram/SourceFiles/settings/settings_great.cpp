/*
This file is part of Telegreat,
modified by Sean.

For license and copyright information please follow this link:
https://git.io/TD
*/
#include "settings/settings_great.h"

#include "styles/style_settings.h"
#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/wrap.h"
#include "ui/widgets/checkbox.h"
#include "info/profile/info_profile_button.h"
#include "storage/localstorage.h"
#include "mainwindow.h"
#include "core/application.h"
#include "boxes/typing_box.h"


namespace Settings {

void GreatSetting(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, tr::lng_telegreat_setting());

	const auto checkbox = [&](const QString &label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			label,
			checked,
			st::settingsCheckbox);
	};
	const auto addCheckbox = [&](const QString &label, bool checked) {
		return container->add(
			checkbox(label, checked),
			st::settingsCheckboxPadding);
	};

	const auto usernameButton = addCheckbox(
		tr::lng_telegreat_setting_username(tr::now),
		cShowUsername());
	const auto ignoreButton = addCheckbox(
		tr::lng_telegreat_setting_ignore(tr::now),
		cIgnoreBlocked());
	const auto callbackButton = addCheckbox(
		tr::lng_telegreat_setting_callback(tr::now),
		cShowCallbackData());
	const auto mentionButton = addCheckbox(
		tr::lng_telegreat_setting_text_mention(tr::now),
		cTextMention());
	const auto copyButton = addCheckbox(
		tr::lng_telegreat_setting_auto_copy(tr::now),
		cAutoCopy());

	const auto typingButton = AddButton(
		container,
		tr::lng_telegreat_setting_typing(),
		st::settingsButton);

	usernameButton->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != cShowUsername());
	}) | rpl::start_with_next([=](bool checked) {
		cSetShowUsername(checked);
		Local::writeUserSettings();
	}, usernameButton->lifetime());

	ignoreButton->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != cIgnoreBlocked());
	}) | rpl::start_with_next([=](bool checked) {
		cSetIgnoreBlocked(checked);
		Local::writeUserSettings();
	}, ignoreButton->lifetime());

	callbackButton->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != cShowCallbackData());
	}) | rpl::start_with_next([=](bool checked) {
		cSetShowCallbackData(checked);
		Local::writeUserSettings();
	}, callbackButton->lifetime());

	mentionButton->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != cTextMention());
	}) | rpl::start_with_next([=](bool checked) {
		cSetTextMention(checked);
		Local::writeUserSettings();
	}, mentionButton->lifetime());

	copyButton->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != cAutoCopy());
	}) | rpl::start_with_next([=](bool checked) {
		cSetAutoCopy(checked);
		Local::writeUserSettings();
	}, copyButton->lifetime());
	
	typingButton->addClickHandler([=] {
		Ui::show(Box<TypingBox>());
	});

	AddSkip(container, st::settingsCheckboxesSkip);
}
} // namespace Settings
