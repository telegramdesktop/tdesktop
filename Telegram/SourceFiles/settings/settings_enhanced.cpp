/*
This file is part of Telegram Desktop x64,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/TDesktop-x64/tdesktop/blob/dev/LEGAL
*/
#include <base/timer_rpl.h>
#include "settings/settings_enhanced.h"

#include "settings/settings_common.h"
#include "settings/settings_chat.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "boxes/connection_box.h"
#include "boxes/enhanced_options_box.h"
#include "boxes/about_box.h"
#include "boxes/confirm_box.h"
#include "platform/platform_specific.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"
#include "core/update_checker.h"
#include "core/enhanced_settings.h"
#include "core/application.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "layout.h"
#include "facades.h"
#include "app.h"
#include "styles/style_settings.h"

namespace Settings {

    void SetupEnhancedNetwork(not_null<Ui::VerticalLayout *> container) {
        const auto wrap = container->add(
                object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
                        container,
                        object_ptr<Ui::VerticalLayout>(container)));
        const auto inner = wrap->entity();

        AddDividerText(inner, tr::lng_settings_restart_hint());
        AddSkip(container);
        AddSubsectionTitle(container, tr::lng_settings_network());

        auto boostBtn = AddButtonWithLabel(
                container,
                tr::lng_settings_net_speed_boost(),
                rpl::single(NetBoostBox::BoostLabel(cNetSpeedBoost())),
                st::settingsButton
        );
        boostBtn->setColorOverride(QColor(255, 0, 0));
        boostBtn->addClickHandler([=] {
            Ui::show(Box<NetBoostBox>());
        });

        AddSkip(container);
    }

    void SetupEnhancedMessages(not_null<Ui::VerticalLayout *> container) {
        AddDivider(container);
        AddSkip(container);
        AddSubsectionTitle(container, tr::lng_settings_messages());

        const auto wrap = container->add(
                object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
                        container,
                        object_ptr<Ui::VerticalLayout>(container)));
        const auto inner = wrap->entity();

        auto MsgIdBtn = AddButton(
                inner,
                tr::lng_settings_show_message_id(),
                st::settingsButton
        );
        MsgIdBtn->setColorOverride(QColor(255, 0, 0));
        MsgIdBtn->toggleOn(
                rpl::single(cShowMessagesID())
        )->toggledChanges(
        ) | rpl::filter([=](bool toggled) {
            return (toggled != cShowMessagesID());
        }) | rpl::start_with_next([=](bool toggled) {
            cSetShowMessagesID(toggled);
            EnhancedSettings::Write();
            App::restart();
        }, container->lifetime());

        AddButton(
                inner,
                tr::lng_settings_show_repeater_option(),
                st::settingsButton
        )->toggleOn(
                rpl::single(cShowRepeaterOption())
        )->toggledChanges(
        ) | rpl::filter([=](bool toggled) {
            return (toggled != cShowRepeaterOption());
        }) | rpl::start_with_next([=](bool toggled) {
            cSetShowRepeaterOption(toggled);
            EnhancedSettings::Write();
        }, container->lifetime());

        if (cShowRepeaterOption()) {
            AddButton(
                inner,
                tr::lng_settings_repeater_reply_to_orig_msg(),
                st::settingsButton
            )->toggleOn(
                rpl::single(cRepeaterReplyToOrigMsg())
            )->toggledChanges(
            ) | rpl::filter([=](bool toggled) {
                return (toggled != cRepeaterReplyToOrigMsg());
            }) | rpl::start_with_next([=](bool toggled) {
                cSetRepeaterReplyToOrigMsg(toggled);
                EnhancedSettings::Write();
            }, container->lifetime());
        }

        auto value = rpl::single(
                rpl::empty_value()
        ) | rpl::then(base::ObservableViewer(
                Global::RefAlwaysDeleteChanged()
        )) | rpl::map([] {
            return AlwaysDeleteBox::DeleteLabel(cAlwaysDeleteFor());
        });

        AddButtonWithLabel(
                container,
                tr::lng_settings_always_delete_for(),
                std::move(value),
                st::settingsButton
        )->addClickHandler([=] {
            Ui::show(Box<AlwaysDeleteBox>());
        });

		AddButton(
				inner,
				tr::lng_settings_disable_cloud_draft_sync(),
				st::settingsButton
		)->toggleOn(
				rpl::single(cDisableCloudDraftSync())
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != cDisableCloudDraftSync());
		}) | rpl::start_with_next([=](bool toggled) {
			cSetDisableCloudDraftSync(toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

        AddSkip(container);
    }

    void SetupEnhancedButton(not_null<Ui::VerticalLayout *> container) {
        AddDivider(container);
        AddSkip(container);
        AddSubsectionTitle(container, tr::lng_settings_button());

        const auto wrap = container->add(
                object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
                        container,
                        object_ptr<Ui::VerticalLayout>(container)));
        const auto inner = wrap->entity();

        auto EmojiBtn = AddButton(
                inner,
                tr::lng_settings_show_emoji_button_as_text(),
                st::settingsButton
        );
        EmojiBtn->setColorOverride(QColor(255, 0, 0));
        EmojiBtn->toggleOn(
                rpl::single(cShowEmojiButtonAsText())
        )->toggledChanges(
        ) | rpl::filter([=](bool toggled) {
            return (toggled != cShowEmojiButtonAsText());
        }) | rpl::start_with_next([=](bool toggled) {
            cSetShowEmojiButtonAsText(toggled);
            EnhancedSettings::Write();
            App::restart();
        }, container->lifetime());

        AddDividerText(inner, tr::lng_show_emoji_button_as_text_desc());

		AddButton(
				inner,
				tr::lng_settings_show_scheduled_button(),
				st::settingsButton
		)->toggleOn(
				rpl::single(cShowScheduledButton())
		)->toggledChanges(
		) | rpl::filter([=](bool toggled) {
			return (toggled != cShowScheduledButton());
		}) | rpl::start_with_next([=](bool toggled) {
			cSetShowScheduledButton(toggled);
			EnhancedSettings::Write();
		}, container->lifetime());

        AddSkip(container);
    }

    Enhanced::Enhanced(
            QWidget *parent,
            not_null<Window::SessionController *> controller)
            : Section(parent) {
        setupContent(controller);
    }

    void Enhanced::setupContent(not_null<Window::SessionController *> controller) {
        const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

        SetupEnhancedNetwork(content);
        SetupEnhancedMessages(content);
        SetupEnhancedButton(content);

        Ui::ResizeFitChild(this, content);
    }
} // namespace Settings

