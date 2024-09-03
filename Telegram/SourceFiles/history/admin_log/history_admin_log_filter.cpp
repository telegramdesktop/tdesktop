/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/admin_log/history_admin_log_filter.h"

#include "boxes/peers/edit_peer_permissions_box.h"
#include "history/admin_log/history_admin_log_filter_value.h"
#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"

namespace AdminLog {

EditFlagsDescriptor<FilterValue::Flags> FilterValueLabels(bool isChannel) {
	using Label = EditFlagsLabel<FilterValue::Flags>;
	using Flag = FilterValue::Flag;

	const auto adminRights = Flag::Promote | Flag::Demote;
	const auto restrictions = Flag::Ban
		| Flag::Unban
		| Flag::Kick
		| Flag::Unkick;
	const auto membersNew = Flag::Join | Flag::Invite;
	const auto membersRemoved = Flag::Leave;

	auto members = std::vector<Label>{
		{ adminRights, tr::lng_admin_log_filter_admins_new(tr::now) },
		{ restrictions, tr::lng_admin_log_filter_restrictions(tr::now) },
		{ membersNew, tr::lng_admin_log_filter_members_new(tr::now) },
		{ membersRemoved, tr::lng_admin_log_filter_members_removed(tr::now) },
	};

	const auto info = Flag::Info | Flag::Settings;
	const auto invites = Flag::Invites;
	const auto calls = Flag::GroupCall;
	auto settings = std::vector<Label>{
		{
			info,
			((!isChannel)
				? tr::lng_admin_log_filter_info_group
				: tr::lng_admin_log_filter_info_channel)(tr::now),
		},
		{ invites, tr::lng_admin_log_filter_invite_links(tr::now) },
		{
			calls,
			((!isChannel)
				? tr::lng_admin_log_filter_voice_chats
				: tr::lng_admin_log_filter_voice_chats_channel)(tr::now),
		},
		{
			Flag::SubExtend,
			tr::lng_admin_log_filter_sub_extend(tr::now),
		},
	};
	if (!isChannel) {
		settings.push_back({
			Flag::Topics,
			tr::lng_admin_log_filter_topics(tr::now),
		});
	}
	const auto deleted = Flag::Delete;
	const auto edited = Flag::Edit;
	const auto pinned = Flag::Pinned;
	auto messages = std::vector<Label>{
		{ deleted, tr::lng_admin_log_filter_messages_deleted(tr::now) },
		{ edited, tr::lng_admin_log_filter_messages_edited(tr::now) },
	};
	if (!isChannel) {
		messages.push_back({
			pinned,
			tr::lng_admin_log_filter_messages_pinned(tr::now),
		});
	}
	return { .labels = {
		{
			tr::lng_admin_log_filter_actions_member_section(),
			std::move(members),
		},
		{
			tr::lng_admin_log_filter_actions_settings_section(),
			std::move(settings),
		},
		{
			tr::lng_admin_log_filter_actions_messages_section(),
			std::move(messages),
		},
	}, .st = nullptr };
}

Fn<FilterValue::Flags()> FillFilterValueList(
		not_null<Ui::VerticalLayout*> container,
		bool isChannel,
		const FilterValue &filter) {
	auto [checkboxes, getResult, changes] = CreateEditAdminLogFilter(
		container,
		filter.flags ? (*filter.flags) : ~FilterValue::Flags(0),
		isChannel);
	container->add(std::move(checkboxes));
	return getResult;
}

} // namespace AdminLog
