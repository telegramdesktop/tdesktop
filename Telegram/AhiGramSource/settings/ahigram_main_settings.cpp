/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
// Author: https://github.com/DyingLay

AhiGram: Main Settings implementation
*/

#include "settings/ahigram_main_settings.h"
#include "storage/ahigram_storage.h"

#include "ahigram_lang.h"
#include "settings/settings_common_session.h"
#include "settings/settings_common.h"  
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"

namespace AhiGram {
namespace Settings {

AhiGramMainSettings::AhiGramMainSettings(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: ::Settings::Section<AhiGramMainSettings>(parent)
, _controller(controller) {
	setupContent();
}

rpl::producer<QString> AhiGramMainSettings::title() {
	return AhiGram::trReactive(u"ahigram_main_settings"_q);
}

void AhiGramMainSettings::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSubsectionTitle(content, AhiGram::trReactive(u"ahigram_main_settings"_q));
	Ui::AddSkip(content);

	const auto toggles = content->lifetime().make_state<rpl::event_stream<bool>>();
	
	auto& settings = AhiGram::Storage::Settings::Instance();
	auto initialValue = settings.get().offLogsInServer;
	
	const auto offLoggingButton = content->add(
		object_ptr<::Settings::Button>(
			content,
			AhiGram::trReactive(u"ahigram_off_logging"_q),
			st::settingsButtonNoIcon
		)
	);
	
	offLoggingButton->toggleOn(toggles->events_starting_with(std::move(initialValue)));
	
	offLoggingButton->toggledChanges(
	) | rpl::start_with_next([=, &settings](bool toggled) {  
		toggles->fire_copy(toggled);
		settings.update([toggled](AhiGram::SettingsData& data) {
			data.offLogsInServer = toggled;
		});
		
	}, content->lifetime());
    
	Ui::AddDividerText(
		content, 
		AhiGram::trReactive(u"ahigram_off_logging_description"_q)
	);

	Ui::AddSkip(content);
	Ui::ResizeFitChild(this, content);
}

::Settings::Type AhiGramMainSettingsId() {
	return AhiGramMainSettings::Id();
}

} // namespace Settings
} // namespace AhiGram

template <>
struct ::Settings::SectionFactory<AhiGram::Settings::AhiGramMainSettings> : ::Settings::AbstractSectionFactory {
	object_ptr<::Settings::AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<::Settings::Container> containerValue
	) const final override {
		return object_ptr<AhiGram::Settings::AhiGramMainSettings>(parent, controller);
	}

	[[nodiscard]] static const std::shared_ptr<::Settings::SectionFactory<AhiGram::Settings::AhiGramMainSettings>> &Instance() {
		static const auto result = std::make_shared<::Settings::SectionFactory<AhiGram::Settings::AhiGramMainSettings>>();
		return result;
	}
};