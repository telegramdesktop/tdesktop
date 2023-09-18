/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#include <ui/boxes/single_choice_box.h>

#include "rabbit/rabbit_settings.h"
#include "rabbit/rabbit_lang.h"
#include "rabbit/rabbit_settings_menu.h"
#include "lang_auto.h"
#include "mainwindow.h"
#include "settings/settings_common.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "boxes/connection_box.h"
#include "platform/platform_specific.h"
#include "window/window_session_controller.h"
#include "lang/lang_instance.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"
#include "apiwrap.h"
#include "api/api_blocked_peers.h"
#include "ui/widgets/continuous_sliders.h"

#define SettingsMenuJsonSwitch(LangKey, Option) AddButton( \
	container, \
	rktr(#LangKey), \
	st::settingsButtonNoIcon \
)->toggleOn( \
	rpl::single(::RabbitSettings::JsonSettings::GetBool(#Option)) \
)->toggledValue( \
) | rpl::filter([](bool enabled) { \
	return (enabled != ::RabbitSettings::JsonSettings::GetBool(#Option)); \
}) | rpl::start_with_next([](bool enabled) { \
	::RabbitSettings::JsonSettings::Set(#Option, enabled); \
	::RabbitSettings::JsonSettings::Write(); \
}, container->lifetime());

namespace Settings {

    rpl::producer<QString> Rabbit::title() {
        return rktr("rtg_settings_rabbit");
    }

    Rabbit::Rabbit(
            QWidget *parent,
            not_null<Window::SessionController *> controller)
            : Section(parent) {
        setupContent(controller);
    }

	void Rabbit::SetupGeneral(not_null<Ui::VerticalLayout *> container)
    {
	    AddSubsectionTitle(container, rktr("rtg_settings_general"));

    	SettingsMenuJsonSwitch(rtg_settings_show_phone_number, show_phone_in_settings);
    	SettingsMenuJsonSwitch(rtg_settings_chat_id, show_ids);
    }

	void Rabbit::SetupAppearance(not_null<Ui::VerticalLayout *> container) {
	    AddSubsectionTitle(container, rktr("rtg_settings_appearance"));

    	const auto userpicRoundnessLabel = container->add(
			object_ptr<Ui::LabelSimple>(
				container,
				st::settingsAudioVolumeLabel),
			st::settingsAudioVolumeLabelPadding);
    	const auto userpicRoundnessSlider = container->add(
			object_ptr<Ui::MediaSlider>(
				container,
				st::settingsAudioVolumeSlider),
			st::settingsAudioVolumeSliderPadding);
    	const auto updateUserpicRoundnessLabel = [=](int value) {
    		const auto radius = QString::number(value);
    		userpicRoundnessLabel->setText(ktr("rtg_settings_userpic_rounding", { "radius", radius }));
    	};
    	const auto updateUserpicRoundness = [=](int value) {
    		updateUserpicRoundnessLabel(value);
    		::RabbitSettings::JsonSettings::Set("userpic_roundness", value);
    		::RabbitSettings::JsonSettings::Write();
    	};
    	userpicRoundnessSlider->resize(st::settingsAudioVolumeSlider.seekSize);
    	userpicRoundnessSlider->setPseudoDiscrete(
			51,
			[](int val) { return val; },
			::RabbitSettings::JsonSettings::GetInt("userpic_roundness"),
			updateUserpicRoundness);
    	updateUserpicRoundnessLabel(::RabbitSettings::JsonSettings::GetInt("userpic_roundness"));

		AddSubsectionTitle(container, rktr("rtg_side_menu_elements"));

		AddButton(
			container,
			tr::lng_create_group_title(),
			st::settingsButton,
			IconDescriptor{ &st::menuIconGroups }
		)->toggleOn(
			rpl::single(::RabbitSettings::JsonSettings::GetBool("side_menu_create_group"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != ::RabbitSettings::JsonSettings::GetBool("side_menu_create_group"));
		}) | rpl::start_with_next([](bool enabled) {
			::RabbitSettings::JsonSettings::Set("side_menu_create_group", enabled);
			::RabbitSettings::JsonSettings::Write();
		}, container->lifetime());

		AddButton(
			container,
			tr::lng_create_channel_title(),
			st::settingsButton,
			IconDescriptor{ &st::menuIconChannel }
		)->toggleOn(
			rpl::single(::RabbitSettings::JsonSettings::GetBool("side_menu_create_channel"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != ::RabbitSettings::JsonSettings::GetBool("side_menu_create_channel"));
		}) | rpl::start_with_next([](bool enabled) {
			::RabbitSettings::JsonSettings::Set("side_menu_create_channel", enabled);
			::RabbitSettings::JsonSettings::Write();
		}, container->lifetime());

		AddButton(
			container,
			tr::lng_menu_my_stories(),
			st::settingsButton,
			IconDescriptor{ &st::menuIconStoriesSavedSection }
		)->toggleOn(
			rpl::single(::RabbitSettings::JsonSettings::GetBool("side_menu_my_stories"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != ::RabbitSettings::JsonSettings::GetBool("side_menu_my_stories"));
		}) | rpl::start_with_next([](bool enabled) {
			::RabbitSettings::JsonSettings::Set("side_menu_my_stories", enabled);
			::RabbitSettings::JsonSettings::Write();
		}, container->lifetime());

		AddButton(
			container,
			tr::lng_menu_contacts(),
			st::settingsButton,
			IconDescriptor{ &st::menuIconProfile }
		)->toggleOn(
			rpl::single(::RabbitSettings::JsonSettings::GetBool("side_menu_contacts"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != ::RabbitSettings::JsonSettings::GetBool("side_menu_contacts"));
		}) | rpl::start_with_next([](bool enabled) {
			::RabbitSettings::JsonSettings::Set("side_menu_contacts", enabled);
			::RabbitSettings::JsonSettings::Write();
		}, container->lifetime());

		AddButton(
			container,
			tr::lng_menu_calls(),
			st::settingsButton,
			IconDescriptor{ &st::menuIconPhone }
		)->toggleOn(
			rpl::single(::RabbitSettings::JsonSettings::GetBool("side_menu_calls"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != ::RabbitSettings::JsonSettings::GetBool("side_menu_calls"));
		}) | rpl::start_with_next([](bool enabled) {
			::RabbitSettings::JsonSettings::Set("side_menu_calls", enabled);
			::RabbitSettings::JsonSettings::Write();
		}, container->lifetime());

		AddButton(
			container,
			tr::lng_saved_messages(),
			st::settingsButton,
			IconDescriptor{ &st::menuIconSavedMessages }
		)->toggleOn(
			rpl::single(::RabbitSettings::JsonSettings::GetBool("side_menu_saved_messages"))
		)->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled != ::RabbitSettings::JsonSettings::GetBool("side_menu_saved_messages"));
		}) | rpl::start_with_next([](bool enabled) {
			::RabbitSettings::JsonSettings::Set("side_menu_saved_messages", enabled);
			::RabbitSettings::JsonSettings::Write();
		}, container->lifetime());
    }

    void Rabbit::SetupChats(not_null<Ui::VerticalLayout *> container) {
        AddSubsectionTitle(container, rktr("rtg_settings_chats"));
    	
        const auto stickerHeightLabel = container->add(
		    object_ptr<Ui::LabelSimple>(
			    container,
			    st::settingsAudioVolumeLabel),
		    st::settingsAudioVolumeLabelPadding);
	    const auto stickerHeightSlider = container->add(
		    object_ptr<Ui::MediaSlider>(
			    container,
			    st::settingsAudioVolumeSlider),
		    st::settingsAudioVolumeSliderPadding);
	    const auto updateStickerHeightLabel = [=](int value) {
		    const auto pixels = QString::number(value);
		    stickerHeightLabel->setText(ktr("rtg_settings_sticker_height", { "pixels", pixels }));
	    };
	    const auto updateStickerHeight = [=](int value) {
		    updateStickerHeightLabel(value);
		    ::RabbitSettings::JsonSettings::Set("sticker_height", value);
		    ::RabbitSettings::JsonSettings::Write();
	    };
	    stickerHeightSlider->resize(st::settingsAudioVolumeSlider.seekSize);
	    stickerHeightSlider->setPseudoDiscrete(
		    193,
		    [](int val) { return val + 64; },
		    ::RabbitSettings::JsonSettings::GetInt("sticker_height"),
		    updateStickerHeight);
	    updateStickerHeightLabel(::RabbitSettings::JsonSettings::GetInt("sticker_height"));
    }

    void Rabbit::SetupRabbitSettings(not_null<Ui::VerticalLayout *> container, not_null<Window::SessionController *> controller) {
		AddSkip(container);
    	SetupGeneral(container);

    	AddSkip(container);
    	SetupAppearance(container);

    	AddSkip(container);
        SetupChats(container);
    }

    void Rabbit::setupContent(not_null<Window::SessionController *> controller) {
        const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

        SetupRabbitSettings(content, controller);

        Ui::ResizeFitChild(this, content);
    }
} // namespace Settings
