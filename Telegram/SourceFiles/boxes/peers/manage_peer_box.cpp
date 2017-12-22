/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "boxes/peers/manage_peer_box.h"

#include <rpl/combine.h>
#include "lang/lang_keys.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "history/history_admin_log_section.h"
#include "window/window_controller.h"
#include "profile/profile_channel_controllers.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace {

base::lambda<QString()> ManagePeerTitle(
		not_null<ChannelData*> channel) {
	return langFactory(channel->isMegagroup()
		? lng_manage_group_title
		: lng_manage_channel_title);
}

auto ToPositiveNumberString() {
	return rpl::map([](int count) {
		return count ? QString::number(count) : QString();
	});
}

template <typename Callback>
Info::Profile::Button *AddButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		Callback callback,
		const style::icon &icon) {
	auto button = parent->add(
		object_ptr<Info::Profile::Button>(
			parent,
			std::move(text),
			st::managePeerButton));
	button->addClickHandler(std::forward<Callback>(callback));
	Ui::CreateChild<Info::Profile::FloatingIcon>(
		button,
		icon,
		st::managePeerButtonIconPosition);
	return button;
}

template <typename Callback>
void AddButtonWithCount(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> &&text,
		rpl::producer<QString> &&count,
		Callback callback,
		const style::icon &icon) {
	auto button = AddButton(
		parent,
		std::move(text),
		std::forward<Callback>(callback),
		icon);
	auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		std::move(count),
		st::managePeerButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		button->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([label](int outerWidth, int width) {
		label->moveToRight(
			st::managePeerButtonLabelPosition.x(),
			st::managePeerButtonLabelPosition.y(),
			outerWidth);
	}, label->lifetime());
}

bool HasRecentActions(not_null<ChannelData*> channel) {
	return channel->hasAdminRights() || channel->amCreator();
}

void ShowRecentActions(
		not_null<Window::Controller*> controller,
		not_null<ChannelData*> channel) {
	controller->showSection(AdminLog::SectionMemento(channel));
}

bool HasEditInfoBox(not_null<ChannelData*> channel) {
	if (channel->canEditInformation()) {
		return true;
	} else if (!channel->isPublic() && channel->canAddMembers()) {
		// Edit invite link.
		return true;
	}
	return false;
}

void FillManageBox(
		not_null<Window::Controller*> controller,
		not_null<ChannelData*> channel,
		not_null<Ui::VerticalLayout*> content) {
	using Profile::ParticipantsBoxController;

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
			[=] { ShowRecentActions(controller, channel); },
			st::infoIconRecentActions);
	}
	if (channel->canViewMembers()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_members),
			Info::Profile::MembersCountValue(channel)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					controller,
					channel,
					ParticipantsBoxController::Role::Members);
			},
			st::infoIconMembers);
	}
	if (channel->canViewAdmins()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_administrators),
			Info::Profile::AdminsCountValue(channel)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					controller,
					channel,
					ParticipantsBoxController::Role::Admins);
			},
			st::infoIconAdministrators);
	}
	if (channel->canViewBanned()) {
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_restricted_users),
			Info::Profile::RestrictedCountValue(channel)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					controller,
					channel,
					ParticipantsBoxController::Role::Restricted);
			},
			st::infoIconRestrictedUsers);
		AddButtonWithCount(
			content,
			Lang::Viewer(lng_manage_peer_banned_users),
			Info::Profile::KickedCountValue(channel)
				| ToPositiveNumberString(),
			[=] {
				ParticipantsBoxController::Start(
					controller,
					channel,
					ParticipantsBoxController::Role::Kicked);
			},
			st::infoIconBlacklist);
	}
}

} // namespace

ManagePeerBox::ManagePeerBox(
	QWidget*,
	not_null<ChannelData*> channel)
: _channel(channel) {
}

bool ManagePeerBox::Available(not_null<ChannelData*> channel) {
	// canViewMembers() is removed, because in supergroups you
	// see them in profile and in channels only admins can see them.

	// canViewAdmins() is removed, because in supergroups it is
	// always true and in channels it is equal to canViewBanned().

	return false
//		|| channel->canViewMembers()
//		|| channel->canViewAdmins()
		|| channel->canViewBanned()
		|| channel->canEditInformation()
		|| HasRecentActions(channel);
}

void ManagePeerBox::prepare() {
	_channel->updateFull();

	setTitle(ManagePeerTitle(_channel));
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	setupContent();
}

void ManagePeerBox::setupContent() {
	auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	FillManageBox(controller(), _channel, content);
	widthValue(
	) | rpl::start_with_next([=](int width) {
		content->resizeToWidth(width);
	}, content->lifetime());
	content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWidth, height);
	}, content->lifetime());
}
