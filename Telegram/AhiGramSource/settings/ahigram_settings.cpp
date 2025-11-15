/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// Use of the code is permitted as long as links to the original source are maintained.
// Author: https://github.com/DyingLay

AhiGram: Settings implementation
*/

#include "settings/ahigram_settings.h"
#include "settings/ahigram_main_settings.h"

#include "ahigram_lang.h"
#include "settings/settings_common_session.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"

#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"
#include "ui/basic_click_handlers.h"

namespace AhiGram {
namespace Settings {
namespace {

struct LinkButtonData {
	QString textKey;
	QString rightText;
	QString url;
	const style::icon *icon;
};

const auto kLinkButtons = std::array{
    LinkButtonData{ u"ahigram_telegram_info_channel"_q, u"@AhiGram"_q, u"https://t.me/AhiGram"_q, &st::menuIconChannel },
    LinkButtonData{ u"ahigram_telegram_release_info_channel"_q, u"@AhiGramReleases"_q, u"https://t.me/AhiGramReleases"_q, &st::menuIconChannel },
    LinkButtonData{ u"ahigram_telegram_github_channel"_q, u"GitHub"_q, u"https://github.com/AhiGram"_q, &st::menuIconLinks }
};

} // namespace

AhiGramSettings::AhiGramSettings(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: ::Settings::Section<AhiGramSettings>(parent)
, _controller(controller) {
	setupContent();
}

rpl::producer<QString> AhiGramSettings::title() {
	return AhiGram::trReactive(u"ahigram_settings_title"_q);
}

void AhiGramSettings::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	//Ui::AddSubsectionTitle(content, AhiGram::trReactive(u"ahigram_settings_title"_q));
	//Ui::AddSkip(content);

	::Settings::AddButtonWithIcon(
		content,
		AhiGram::trReactive(u"ahigram_main_settings"_q),
		st::settingsButton,
		{ &st::menuIconAhiGramHome }
	)->addClickHandler([=] {
		showOther(AhiGramMainSettingsId());
	});
	//Ui::AddDividerText(content, AhiGram::trReactive(u"ahigram_main_settings_description"_q));

	Ui::AddSkip(content);
	Ui::AddDivider(content);
	Ui::AddSkip(content);

	Ui::AddSubsectionTitle(content, AhiGram::trReactive(u"ahigram_advanced_options"_q));
	Ui::AddSkip(content);

	const auto &st = st::settingsButtonRightLabelSpoiler;
    
	for (const auto &buttonData : kLinkButtons) {
		const auto buttonText = AhiGram::trReactive(buttonData.textKey);
		const auto rightText = rpl::single(buttonData.rightText);
		const auto url = buttonData.url;
		
		const auto linkButton = ::Settings::AddButtonWithIcon(
			content,
			rpl::duplicate(buttonText),
			st,
			{ buttonData.icon }
		);
		::Settings::CreateRightLabel(linkButton, rightText, st, std::move(buttonText));

		linkButton->addClickHandler([=] {
			UrlClickHandler::Open(url);
		});

		Ui::AddSkip(content);
	}

	Ui::AddSkip(content);
	Ui::ResizeFitChild(this, content);
}

::Settings::Type AhiGramSettingsId() {
	return AhiGramSettings::Id();
}

} // namespace Settings
} // namespace AhiGram

template <>
struct ::Settings::SectionFactory<AhiGram::Settings::AhiGramSettings> : ::Settings::AbstractSectionFactory {
	object_ptr<::Settings::AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<::Settings::Container> containerValue
	) const final override {
		return object_ptr<AhiGram::Settings::AhiGramSettings>(parent, controller);
	}

	[[nodiscard]] static const std::shared_ptr<::Settings::SectionFactory<AhiGram::Settings::AhiGramSettings>> &Instance() {
		static const auto result = std::make_shared<::Settings::SectionFactory<AhiGram::Settings::AhiGramSettings>>();
		return result;
	}
};