/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_chat_participant_status.h"

namespace Ui {
class GenericBox;
class RoundButton;
class RpWidget;
class VerticalLayout;
} // namespace Ui

template <typename Object>
class object_ptr;

namespace Window {
class SessionController;
class SessionNavigation;
} // namespace Window

struct EditPeerPermissionsBoxResult final {
	ChatRestrictions rights;
	int slowmodeSeconds = 0;
};

void ShowEditPeerPermissionsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> channelOrGroup,
	Fn<void(EditPeerPermissionsBoxResult)> done);

[[nodiscard]] Fn<void()> AboutGigagroupCallback(
	not_null<ChannelData*> channel,
	not_null<Window::SessionController*> controller);

struct RestrictionLabel {
	ChatRestrictions flags;
	QString label;
};
[[nodiscard]] std::vector<RestrictionLabel> RestrictionLabels(
	Data::RestrictionsSetOptions options);

struct AdminRightLabel {
	ChatAdminRights flags;
	QString label;
};
[[nodiscard]] std::vector<AdminRightLabel> AdminRightLabels(
	Data::AdminRightsSetOptions options);

template <typename Flags, typename Widget>
struct EditFlagsControl {
	object_ptr<Widget> widget;
	Fn<Flags()> value;
	rpl::producer<Flags> changes;
};

[[nodiscard]] auto CreateEditRestrictions(
	QWidget *parent,
	rpl::producer<QString> header,
	ChatRestrictions restrictions,
	std::map<ChatRestrictions, QString> disabledMessages,
	Data::RestrictionsSetOptions options)
-> EditFlagsControl<ChatRestrictions, Ui::RpWidget>;

[[nodiscard]] auto CreateEditAdminRights(
	QWidget *parent,
	rpl::producer<QString> header,
	ChatAdminRights rights,
	std::map<ChatAdminRights, QString> disabledMessages,
	Data::AdminRightsSetOptions options)
-> EditFlagsControl<ChatAdminRights, Ui::RpWidget>;

[[nodiscard]] ChatAdminRights DisabledByDefaultRestrictions(
	not_null<PeerData*> peer);
[[nodiscard]] ChatRestrictions FixDependentRestrictions(
	ChatRestrictions restrictions);
[[nodiscard]] ChatAdminRights AdminRightsForOwnershipTransfer(
	Data::AdminRightsSetOptions options);
