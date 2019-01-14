/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/manage_peer_box.h"

#include <rpl/combine.h>
#include "lang/lang_keys.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/peers/edit_peer_permissions_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "history/admin_log/history_admin_log_section.h"
#include "window/window_controller.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

Fn<QString()> ManagePeerTitle(not_null<PeerData*> peer) {
	return langFactory((peer->isChat() || peer->isMegagroup())
		? lng_manage_group_title
		: lng_manage_channel_title);
}

auto ToPositiveNumberString() {
	return rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});
}

Info::Profile::Button *AddButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		Fn<void()> callback,
		const style::icon &icon) {
	return ManagePeerBox::CreateButton(
		parent,
		std::move(text),
		rpl::single(QString()),
		std::move(callback),
		st::managePeerButton,
		&icon);
}

void AddButtonWithCount(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::icon &icon) {
	ManagePeerBox::CreateButton(
		parent,
		std::move(text),
		std::move(count),
		std::move(callback),
		st::managePeerButton,
		&icon);
}

bool HasRecentActions(not_null<ChannelData*> channel) {
	return channel->hasAdminRights() || channel->amCreator();
}

void ShowRecentActions(
		not_null<Window::Navigation*> navigation,
		not_null<ChannelData*> channel) {
	navigation->showSection(AdminLog::SectionMemento(channel));
}

bool HasEditInfoBox(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		if (chat->canEditInformation()) {
			return true;
		}
	} else if (const auto channel = peer->asChannel()) {
		if (channel->canEditInformation()) {
			return true;
		} else if (!channel->isPublic() && channel->canAddMembers()) {
			// Edit invite link.
			return true;
		}
	}
	return false;
}

void ShowEditPermissions(not_null<PeerData*> peer) {
	const auto box = Ui::show(
		Box<EditPeerPermissionsBox>(peer),
		LayerOption::KeepOther);
	box->saveEvents(
	) | rpl::start_with_next([=](MTPDchatBannedRights::Flags restrictions) {
		const auto callback = crl::guard(box, [=](bool success) {
			if (success) {
				box->closeBox();
			}
		});
		peer->session().api().saveDefaultRestrictions(
			peer->migrateToOrMe(),
			MTP_chatBannedRights(MTP_flags(restrictions), MTP_int(0)),
			callback);
	}, box->lifetime());
}

void FillManageChatBox(
		not_null<Window::Navigation*> navigation,
		not_null<ChatData*> chat,
		not_null<Ui::VerticalLayout*> content) {
	if (HasEditInfoBox(chat)) {
		AddButton(
			content,
			Lang::Viewer(lng_manage_group_info),
			[=] { Ui::show(Box<EditPeerInfoBox>(chat)); },
			st::infoIconInformation);
	}
	if (chat->canEditPermissions()) {
		AddButton(
			content,
			Lang::Viewer(lng_manage_peer_permissions),
			[=] { ShowEditPermissions(chat); },
			st::infoIconPermissions);
	}
	if (chat->amIn()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_administrators),
			Info::Profile::AdminsCountValue(chat)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					chat,
					ParticipantsBoxController::Role::Admins);
			},
			st::infoIconAdministrators);
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_members),
			Info::Profile::MembersCountValue(chat)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					chat,
					ParticipantsBoxController::Role::Members);
			},
			st::infoIconMembers);
	}
}

void FillManageChannelBox(
		not_null<Window::Navigation*> navigation,
		not_null<ChannelData*> channel,
		not_null<Ui::VerticalLayout*> content) {
	auto isGroup = channel->isMegagroup();
	if (HasEditInfoBox(channel)) {
		AddButton(
			content,
			Lang::Viewer(isGroup
				? lng_manage_group_info
				: lng_manage_channel_info),
			[=] { Ui::show(Box<EditPeerInfoBox>(channel)); },
			st::infoIconInformation);
	}
	if (HasRecentActions(channel)) {
		AddButton(
			content,
			Lang::Viewer(lng_manage_peer_recent_actions),
			[=] { ShowRecentActions(navigation, channel); },
			st::infoIconRecentActions);
	}
	if (channel->canEditPermissions()) {
		AddButton(
			content,
			Lang::Viewer(lng_manage_peer_permissions),
			[=] { ShowEditPermissions(channel); },
			st::infoIconPermissions);
	}
	if (channel->canViewAdmins()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_administrators),
			Info::Profile::AdminsCountValue(channel)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					channel,
					ParticipantsBoxController::Role::Admins);
			},
			st::infoIconAdministrators);
	}
	if (channel->canViewMembers()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_members),
			Info::Profile::MembersCountValue(channel)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					channel,
					ParticipantsBoxController::Role::Members);
			},
			st::infoIconMembers);
	}
	if (!channel->isMegagroup()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_removed_users),
			Info::Profile::KickedCountValue(channel)
			| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					navigation,
					channel,
					ParticipantsBoxController::Role::Kicked);
			},
			st::infoIconBlacklist);
	}
}

} // namespace

ManagePeerBox::ManagePeerBox(
	QWidget*,
	not_null<PeerData*> peer)
: _peer(peer) {
}

bool ManagePeerBox::Available(not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		return false
			|| chat->canEditInformation()
			|| chat->canEditPermissions();
	} else if (const auto channel = peer->asChannel()) {
		// canViewMembers() is removed, because in supergroups you
		// see them in profile and in channels only admins can see them.

		// canViewAdmins() is removed, because in supergroups it is
		// always true and in channels it is equal to canViewBanned().

		return false
			//|| channel->canViewMembers()
			//|| channel->canViewAdmins()
			|| channel->canViewBanned()
			|| channel->canEditInformation()
			|| channel->canEditPermissions()
			|| HasRecentActions(channel);
	} else {
		return false;
	}
}

Info::Profile::Button *ManagePeerBox::CreateButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Fn<void()> callback,
		const style::InfoProfileCountButton &st,
		const style::icon *icon) {
	const auto button = parent->add(
		object_ptr<Info::Profile::Button>(
			parent,
			std::move(text),
			st.button));
	button->addClickHandler(callback);
	if (icon) {
		Ui::CreateChild<Info::Profile::FloatingIcon>(
			button,
			*icon,
			st.iconPosition);
	}
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		std::move(count),
		st.label);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	rpl::combine(
		button->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=, &st](int outerWidth, int width) {
		label->moveToRight(
			st.labelPosition.x(),
			st.labelPosition.y(),
			outerWidth);
	}, label->lifetime());

	return button;
}

void ManagePeerBox::prepare() {
	_peer->updateFull();

	setTitle(ManagePeerTitle(_peer));
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	setupContent();
}

void ManagePeerBox::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	if (const auto chat = _peer->asChat()) {
		FillManageChatBox(App::wnd()->controller(), chat, content);
	} else if (const auto channel = _peer->asChannel()) {
		FillManageChannelBox(App::wnd()->controller(), channel, content);
	}
	setDimensionsToContent(st::boxWidth, content);
}
