/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "data/data_chat_participant_status.h"
#include "history/admin_log/history_admin_log_filter_value.h"

namespace style {
struct SettingsButton;
} // namespace style

namespace Ui {
class GenericBox;
class RoundButton;
class RpWidget;
class VerticalLayout;
} // namespace Ui

namespace PowerSaving {
enum Flag : uint32;
using Flags = base::flags<Flag>;
} // namespace PowerSaving

template <typename Object>
class object_ptr;

namespace Window {
class SessionController;
class SessionNavigation;
} // namespace Window

struct EditPeerPermissionsBoxResult final {
	ChatRestrictions rights;
	int slowmodeSeconds = 0;
	int boostsUnrestrict = 0;
};

void ShowEditPeerPermissionsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> channelOrGroup,
	Fn<void(EditPeerPermissionsBoxResult)> done);

[[nodiscard]] Fn<void()> AboutGigagroupCallback(
	not_null<ChannelData*> channel,
	not_null<Window::SessionController*> controller);

template <typename Flags>
struct EditFlagsLabel {
	Flags flags;
	QString label;
	const style::icon *icon = nullptr;
};

template <typename Flags>
struct EditFlagsControl {
	object_ptr<Ui::RpWidget> widget;
	Fn<Flags()> value;
	rpl::producer<Flags> changes;
};

template <typename Flags>
struct NestedEditFlagsLabels {
	std::optional<rpl::producer<QString>> nestingLabel;
	std::vector<EditFlagsLabel<Flags>> nested;
};

template <typename Flags>
struct EditFlagsDescriptor {
	std::vector<NestedEditFlagsLabels<Flags>> labels;
	base::flat_map<Flags, QString> disabledMessages;
	const style::SettingsButton *st = nullptr;
	rpl::producer<QString> forceDisabledMessage;
};

using RestrictionLabel = EditFlagsLabel<ChatRestrictions>;
[[nodiscard]] std::vector<RestrictionLabel> RestrictionLabels(
	Data::RestrictionsSetOptions options);

using AdminRightLabel = EditFlagsLabel<ChatAdminRights>;
[[nodiscard]] std::vector<AdminRightLabel> AdminRightLabels(
	Data::AdminRightsSetOptions options);

[[nodiscard]] auto CreateEditRestrictions(
	QWidget *parent,
	ChatRestrictions restrictions,
	base::flat_map<ChatRestrictions, QString> disabledMessages,
	Data::RestrictionsSetOptions options)
-> EditFlagsControl<ChatRestrictions>;

[[nodiscard]] auto CreateEditAdminRights(
	QWidget *parent,
	ChatAdminRights rights,
	base::flat_map<ChatAdminRights, QString> disabledMessages,
	Data::AdminRightsSetOptions options)
-> EditFlagsControl<ChatAdminRights>;

[[nodiscard]] ChatAdminRights DisabledByDefaultRestrictions(
	not_null<PeerData*> peer);
[[nodiscard]] ChatRestrictions FixDependentRestrictions(
	ChatRestrictions restrictions);
[[nodiscard]] ChatAdminRights AdminRightsForOwnershipTransfer(
	Data::AdminRightsSetOptions options);

[[nodiscard]] auto CreateEditPowerSaving(
	QWidget *parent,
	PowerSaving::Flags flags,
	rpl::producer<QString> forceDisabledMessage
) -> EditFlagsControl<PowerSaving::Flags>;

[[nodiscard]] auto CreateEditAdminLogFilter(
	QWidget *parent,
	AdminLog::FilterValue::Flags flags,
	bool isChannel
) -> EditFlagsControl<AdminLog::FilterValue::Flags>;
