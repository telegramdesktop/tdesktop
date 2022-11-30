/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_antispam_validator.h"

#include "apiwrap.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/text/text_utilities.h"
#include "ui/toasts/common_toasts.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"

namespace AntiSpamMenu {
namespace {

[[nodiscard]] int EnableAntiSpamMinMembers(not_null<ChannelData*> channel) {
	return channel->session().account().appConfig().get<int>(
		u"telegram_antispam_group_size_min"_q,
		100);
}

} // namespace

AntiSpamValidator::AntiSpamValidator(
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel)
: _channel(channel)
, _controller(controller) {
}

object_ptr<Ui::RpWidget> AntiSpamValidator::createButton() const {
	const auto channel = _channel;
	auto container = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		(QWidget*)nullptr,
		object_ptr<Ui::VerticalLayout>((QWidget*)nullptr));
	struct State {
		rpl::variable<bool> locked;
		rpl::event_stream<bool> toggled;
	};
	Settings::AddSkip(container->entity());
	const auto state = container->lifetime().make_state<State>();
	const auto button = container->entity()->add(
		EditPeerInfoBox::CreateButton(
			container->entity(),
			tr::lng_manage_peer_antispam(),
			rpl::single(QString()),
			[] {},
			st::manageGroupTopicsButton,
			{ &st::infoRoundedIconAdministrators, Settings::kIconPurple }
	))->toggleOn(rpl::single(
		_channel->antiSpamMode()
	) | rpl::then(state->toggled.events()));
	container->show(anim::type::instant);
	Settings::AddDividerText(
		container->entity(),
		tr::lng_manage_peer_antispam_about());

	const auto updateLocked = [=] {
		const auto &config = channel->session().account().appConfig();
		const auto min = EnableAntiSpamMinMembers(channel);
		const auto locked = (channel->membersCount() <= min);
		state->locked = locked;
		button->setToggleLocked(locked);
	};
	using UpdateFlag = Data::PeerUpdate::Flag;
	_channel->session().changes().peerUpdates(
		_channel,
		UpdateFlag::Members | UpdateFlag::Admins
	) | rpl::start_with_next(updateLocked, button->lifetime());
	updateLocked();
	button->toggledValue(
	) | rpl::start_with_next([=, controller = _controller](bool toggled) {
		if (state->locked.current() && toggled) {
			state->toggled.fire(false);
			Ui::ShowMultilineToast({
				.parentOverride = Window::Show(controller).toastParent(),
				.text = tr::lng_manage_peer_antispam_not_enough(
					tr::now,
					lt_count,
					EnableAntiSpamMinMembers(channel),
					Ui::Text::RichLangValue),
			});
		} else {
			channel->session().api().request(MTPchannels_ToggleAntiSpam(
				channel->inputChannel,
				MTP_bool(toggled)
			)).done([=](const MTPUpdates &result) {
				channel->session().api().applyUpdates(result);
			}).send();
		}
	}, button->lifetime());

	return container;
}

} // namespace AntiSpamMenu
