/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_experimental.h"

#include "ui/boxes/confirm_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/vertical_list.h"
#include "ui/gl/gl_detection.h"
#include "ui/chat/chat_style_radius.h"
#include "base/options.h"
#include "core/application.h"
#include "core/launcher.h"
#include "chat_helpers/tabbed_panel.h"
#include "dialogs/dialogs_widget.h"
#include "history/history_item_components.h"
#include "info/profile/info_profile_actions.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "media/player/media_player_instance.h"
#include "mtproto/session_private.h"
#include "webview/webview_embed.h"
#include "window/main_window.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/notifications_manager.h"
#include "storage/localimageloader.h"
#include "data/data_document_resolver.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"

namespace Settings {
namespace {

void AddOption(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		base::options::option<bool> &option,
		rpl::producer<> resetClicks) {
	auto &lifetime = container->lifetime();
	const auto name = option.name().isEmpty() ? option.id() : option.name();
	const auto toggles = lifetime.make_state<rpl::event_stream<bool>>();
	std::move(
		resetClicks
	) | rpl::map_to(
		option.defaultValue()
	) | rpl::start_to_stream(*toggles, lifetime);

	const auto button = container->add(object_ptr<Button>(
		container,
		rpl::single(name),
		(option.relevant()
			? st::settingsButtonNoIcon
			: st::settingsOptionDisabled)
	))->toggleOn(toggles->events_starting_with(option.value()));

	const auto restarter = (option.relevant() && option.restartRequired())
		? button->lifetime().make_state<base::Timer>()
		: nullptr;
	if (restarter) {
		restarter->setCallback([=] {
			window->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = [] { Core::Restart(); },
				.confirmText = tr::lng_settings_restart_now(),
				.cancelText = tr::lng_settings_restart_later(),
			}));
		});
	}
	button->toggledChanges(
	) | rpl::start_with_next([=, &option](bool toggled) {
		if (!option.relevant() && toggled != option.defaultValue()) {
			toggles->fire_copy(option.defaultValue());
			window->showToast(
				tr::lng_settings_experimental_irrelevant(tr::now));
			return;
		}
		option.set(toggled);
		if (restarter) {
			restarter->callOnce(st::settingsButtonNoIcon.toggle.duration);
		}
	}, container->lifetime());

	const auto &description = option.description();
	if (!description.isEmpty()) {
		Ui::AddSkip(container, st::settingsCheckboxesSkip);
		Ui::AddDividerText(container, rpl::single(description));
		Ui::AddSkip(container, st::settingsCheckboxesSkip);
	}
}

void SetupExperimental(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container) {
	Ui::AddSkip(container, st::settingsCheckboxesSkip);

	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_settings_experimental_about(),
			st::boxLabel),
		st::defaultBoxDividerLabelPadding);

	auto reset = (Button*)nullptr;
	if (base::options::changed()) {
		const auto wrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto inner = wrap->entity();
		Ui::AddDivider(inner);
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
		reset = inner->add(object_ptr<Button>(
			inner,
			tr::lng_settings_experimental_restore(),
			st::settingsButtonNoIcon));
		reset->addClickHandler([=] {
			base::options::reset();
			wrap->hide(anim::type::normal);
		});
		Ui::AddSkip(inner, st::settingsCheckboxesSkip);
	}

	Ui::AddDivider(container);
	Ui::AddSkip(container, st::settingsCheckboxesSkip);

	const auto addToggle = [&](const char name[]) {
		AddOption(
			window,
			container,
			base::options::lookup<bool>(name),
			(reset
				? (reset->clicks() | rpl::to_empty)
				: rpl::producer<>()));
	};

	addToggle(ChatHelpers::kOptionTabbedPanelShowOnClick);
	addToggle(Dialogs::kOptionForumHideChatsList);
	addToggle(Core::kOptionFractionalScalingEnabled);
	addToggle(Window::kOptionViewProfileInChatsListContextMenu);
	addToggle(Info::Profile::kOptionShowPeerIdBelowAbout);
	addToggle(Ui::kOptionUseSmallMsgBubbleRadius);
	addToggle(Media::Player::kOptionDisableAutoplayNext);
	addToggle(kOptionSendLargePhotos);
	addToggle(Webview::kOptionWebviewDebugEnabled);
	addToggle(Webview::kOptionWebviewLegacyEdge);
	addToggle(kOptionAutoScrollInactiveChat);
	addToggle(Window::Notifications::kOptionGNotification);
	addToggle(Core::kOptionFreeType);
	addToggle(Core::kOptionSkipUrlSchemeRegister);
	addToggle(Data::kOptionExternalVideoPlayer);
	addToggle(Window::kOptionNewWindowsSizeAsFirst);
	addToggle(MTP::details::kOptionPreferIPv6);
	if (base::options::lookup<bool>(kOptionFastButtonsMode).value()) {
		addToggle(kOptionFastButtonsMode);
	}
	addToggle(Window::kOptionDisableTouchbar);
}

} // namespace

Experimental::Experimental(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

rpl::producer<QString> Experimental::title() {
	return tr::lng_settings_experimental();
}

void Experimental::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupExperimental(&controller->window(), content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
