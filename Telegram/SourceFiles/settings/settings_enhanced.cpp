/*
This file is part of Kotatogram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/kotatogram/kotatogram-desktop/blob/dev/LEGAL
*/
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
#include "boxes/net_boost_box.h"
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

void SetupEnhancedNetwork(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_network());

	AddButtonWithLabel(
		container,
		tr::lng_settings_net_speed_boost(),
		rpl::single(NetBoostBox::BoostLabel(cNetSpeedBoost())),
		st::settingsButton
	)->addClickHandler([=] {
		Ui::show(Box<NetBoostBox>());
	});

	AddSkip(container);
}

void SetupEnhancedMessages(not_null<Ui::VerticalLayout*> container) {
	AddDivider(container);
	AddSkip(container);
	AddSubsectionTitle(container, tr::lng_settings_messages());

	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = wrap->entity();

	AddButton(
		inner,
		tr::lng_settings_show_message_id(),
		st::settingsButton
	)->toggleOn(
		rpl::single(cShowMessagesID())
	)->toggledChanges(
	) | rpl::filter([=](bool toggled) {
		return (toggled != cShowMessagesID());
	}) | rpl::start_with_next([=](bool toggled) {
		cSetShowMessagesID(toggled);
		EnhancedSettings::Write();
		App::restart();
	}, container->lifetime());

	AddSkip(inner);
	AddDividerText(inner, tr::lng_show_messages_id_desc());

	AddSkip(container);
}

Enhanced::Enhanced(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

void Enhanced::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupEnhancedNetwork(content);
	SetupEnhancedMessages(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings

