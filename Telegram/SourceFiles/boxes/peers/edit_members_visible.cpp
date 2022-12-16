/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_members_visible.h"

#include "boxes/peers/edit_peer_info_box.h"
#include "data/data_channel.h"
#include "ui/rp_widget.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "settings/settings_common.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"

[[nodiscard]] object_ptr<Ui::RpWidget> CreateMembersVisibleButton(
		not_null<ChannelData*> megagroup) {
	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();

	struct State {
		rpl::event_stream<bool> toggled;
	};
	Settings::AddSkip(container);
	const auto state = container->lifetime().make_state<State>();
	const auto button = container->add(
		EditPeerInfoBox::CreateButton(
			container,
			tr::lng_profile_hide_participants(),
			rpl::single(QString()),
			[] {},
			st::manageGroupTopicsButton,
			{ &st::infoRoundedIconAntiSpam, Settings::kIconPurple }
	))->toggleOn(rpl::single(
		(megagroup->flags() & ChannelDataFlag::ParticipantsHidden) != 0
	) | rpl::then(state->toggled.events()));
	Settings::AddSkip(container);
	Settings::AddDividerText(
		container,
		tr::lng_profile_hide_participants_about());

	button->toggledValue(
	) | rpl::start_with_next([=](bool toggled) {
		megagroup->session().api().request(
			MTPchannels_ToggleParticipantsHidden(
				megagroup->inputChannel,
				MTP_bool(toggled)
			)
		).done([=](const MTPUpdates &result) {
			megagroup->session().api().applyUpdates(result);
		}).send();
	}, button->lifetime());

	return result;
}
