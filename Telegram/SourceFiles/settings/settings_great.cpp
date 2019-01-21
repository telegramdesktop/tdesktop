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
#include "application.h"
#include "boxes/typing_box.h"


namespace Settings {

void GreatSetting(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);

	AddSubsectionTitle(container, lng_telegreat_setting);

	const auto checkbox = [&](LangKey label, bool checked) {
		return object_ptr<Ui::Checkbox>(
			container,
			lang(label),
			checked,
			st::settingsCheckbox);
	};
	const auto addCheckbox = [&](LangKey label, bool checked) {
		return container->add(
			checkbox(label, checked),
			st::settingsCheckboxPadding);
	};

	const auto callbackButton = addCheckbox(
		lng_telegreat_setting_callback,
		cShowCallbackData());
	const auto usernameButton = addCheckbox(
		lng_telegreat_setting_username,
		cShowUsername());
	const auto ignoreButton = addCheckbox(
		lng_telegreat_setting_ignore,
		cIgnoreBlocked());
	const auto mentionButton = addCheckbox(
		lng_telegreat_setting_text_mention,
		cTextMention());
	const auto copyButton = addCheckbox(
		lng_telegreat_setting_auto_copy,
		cAutoCopy());

	const auto typingButton = AddButton(
		container,
		lng_telegreat_setting_typing,
		st::settingsButton);

	callbackButton->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != cShowCallbackData());
	}) | rpl::start_with_next([=](bool checked) {
		cSetShowCallbackData(checked);
		Local::writeUserSettings();
	}, callbackButton->lifetime());

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
