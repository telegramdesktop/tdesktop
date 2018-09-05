/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_main.h"

#include "settings/settings_common.h"
#include "boxes/abstract_box.h"
#include "ui/wrap/vertical_layout.h"
#include "info/profile/info_profile_button.h"
#include "lang/lang_keys.h"
#include "styles/style_settings.h"

namespace Settings {

Main::Main(QWidget *parent, not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent();
}

void Main::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	content->add(object_ptr<BoxContentDivider>(content));

	const auto addSection = [&](LangKey label, Type type) {
		content->add(object_ptr<Button>(
			content,
			Lang::Viewer(label),
			st::settingsSectionButton)
		)->addClickHandler([=] {
			_showOther.fire_copy(type);
		});
	};
	addSection(lng_settings_section_info, Type::Information);
	addSection(lng_settings_section_notify, Type::Notifications);
	addSection(lng_settings_section_privacy, Type::PrivacySecurity);
	addSection(lng_settings_section_general, Type::General);
	addSection(lng_settings_section_chat_settings, Type::Chat);

	Ui::ResizeFitChild(this, content);
}

rpl::producer<Type> Main::sectionShowOther() {
	return _showOther.events();
}

} // namespace Settings
