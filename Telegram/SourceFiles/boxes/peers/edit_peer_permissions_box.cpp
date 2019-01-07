/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_permissions_box.h"

#include "lang/lang_keys.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_boxes.h"

namespace {

template <typename CheckboxesMap, typename DependenciesMap>
void ApplyDependencies(
		const CheckboxesMap &checkboxes,
		const DependenciesMap &dependencies,
		QPointer<Ui::Checkbox> changed) {
	const auto checkAndApply = [&](
			auto &&current,
			auto dependency,
			bool isChecked) {
		for (auto &&checkbox : checkboxes) {
			if ((checkbox.first & dependency)
				&& (checkbox.second->checked() == isChecked)) {
				current->setChecked(isChecked);
				return true;
			}
		}
		return false;
	};
	const auto applySomeDependency = [&] {
		auto result = false;
		for (auto &&entry : checkboxes) {
			if (entry.second == changed) {
				continue;
			}
			auto isChecked = entry.second->checked();
			for (auto &&dependency : dependencies) {
				const auto check = isChecked
					? dependency.first
					: dependency.second;
				if (entry.first & check) {
					if (checkAndApply(
							entry.second,
							(isChecked
								? dependency.second
								: dependency.first),
							!isChecked)) {
						result = true;
						break;
					}
				}
			}
		}
		return result;
	};

	while (true) {
		if (!applySomeDependency()) {
			break;
		}
	};
}

std::vector<std::pair<ChatRestrictions, LangKey>> RestrictionLabels() {
	using Flag = ChatRestriction;

	return {
		{ Flag::f_view_messages, lng_rights_chat_read },
		{ Flag::f_send_messages, lng_rights_chat_send_text },
		{ Flag::f_send_media, lng_rights_chat_send_media },
		{ Flag::f_send_stickers
		| Flag::f_send_gifs
		| Flag::f_send_games
		| Flag::f_send_inline, lng_rights_chat_send_stickers },
		{ Flag::f_embed_links, lng_rights_chat_send_links },
	};
}

std::vector<std::pair<ChatAdminRights, LangKey>> AdminRightLabels(
		bool isGroup,
		bool anyoneCanAddMembers) {
	using Flag = ChatAdminRight;

	if (isGroup) {
		return {
			{ Flag::f_change_info, lng_rights_group_info },
			{ Flag::f_delete_messages, lng_rights_group_delete },
			{ Flag::f_ban_users, lng_rights_group_ban },
			{ Flag::f_invite_users, anyoneCanAddMembers
				? lng_rights_group_invite_link
				: lng_rights_group_invite },
			{ Flag::f_pin_messages, lng_rights_group_pin },
			{ Flag::f_add_admins, lng_rights_add_admins },
		};
	} else {
		return {
			{ Flag::f_change_info, lng_rights_channel_info },
			{ Flag::f_post_messages, lng_rights_channel_post },
			{ Flag::f_edit_messages, lng_rights_channel_edit },
			{ Flag::f_delete_messages, lng_rights_channel_delete },
			{ Flag::f_invite_users, lng_rights_group_invite },
			{ Flag::f_add_admins, lng_rights_add_admins }
		};
	}
}

auto Dependencies(ChatRestrictions)
-> std::vector<std::pair<ChatRestriction, ChatRestriction>> {
	using Flag = ChatRestriction;

	return {
		// stickers <-> gifs
		{ Flag::f_send_gifs, Flag::f_send_stickers },
		{ Flag::f_send_stickers, Flag::f_send_gifs },

		// stickers <-> games
		{ Flag::f_send_games, Flag::f_send_stickers },
		{ Flag::f_send_stickers, Flag::f_send_games },

		// stickers <-> inline
		{ Flag::f_send_inline, Flag::f_send_stickers },
		{ Flag::f_send_stickers, Flag::f_send_inline },

		// stickers -> send_media
		{ Flag::f_send_stickers, Flag::f_send_media },

		// embed_links -> send_media
		{ Flag::f_embed_links, Flag::f_send_media },

		// send_media- > send_messages
		{ Flag::f_send_media, Flag::f_send_messages },

		// send_messages -> view_messages
		{ Flag::f_send_messages, Flag::f_view_messages },
	};
}

ChatRestrictions NegateRestrictions(ChatRestrictions value) {
	using Flag = ChatRestriction;

	return (~value) & (Flag(0)
		| Flag::f_change_info
		| Flag::f_embed_links
		| Flag::f_invite_users
		| Flag::f_pin_messages
		| Flag::f_send_games
		| Flag::f_send_gifs
		| Flag::f_send_inline
		| Flag::f_send_media
		| Flag::f_send_messages
		| Flag::f_send_polls
		| Flag::f_send_stickers
		| Flag::f_view_messages);
}

auto Dependencies(ChatAdminRights)
-> std::vector<std::pair<ChatAdminRight, ChatAdminRight>> {
	return {};
}

} // namespace

template <typename Flags, typename FlagLabelPairs>
EditFlagsControl<Flags> CreateEditFlags(
		QWidget *parent,
		LangKey header,
		Flags checked,
		Flags disabled,
		const FlagLabelPairs &flagLabelPairs) {
	auto widget = object_ptr<Ui::VerticalLayout>(parent);
	const auto container = widget.data();

	const auto checkboxes = container->lifetime(
	).make_state<std::map<Flags, QPointer<Ui::Checkbox>>>();

	const auto value = [=] {
		auto result = Flags(0);
		for (const auto &[flags, checkbox] : *checkboxes) {
			if (checkbox->checked()) {
				result |= flags;
			} else {
				result &= ~flags;
			}
		}
		return result;
	};

	const auto changes = container->lifetime(
	).make_state<rpl::event_stream<>>();

	const auto applyDependencies = [=](Ui::Checkbox *control) {
		static const auto dependencies = Dependencies(Flags());
		ApplyDependencies(*checkboxes, dependencies, control);
	};

	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			Lang::Viewer(header),
			st::rightsHeaderLabel),
		st::rightsHeaderMargin);

	auto addCheckbox = [&](Flags flags, const QString &text) {
		const auto control = container->add(
			object_ptr<Ui::Checkbox>(
				container,
				text,
				(checked & flags) != 0,
				st::rightsCheckbox,
				st::rightsToggle),
			st::rightsToggleMargin);
		control->checkedChanges(
		) | rpl::start_with_next([=](bool checked) {
			InvokeQueued(control, [=] {
				applyDependencies(control);
				changes->fire({});
			});
		}, control->lifetime());
		if ((disabled & flags) != 0) {
			control->setDisabled(true);
		}
		checkboxes->emplace(flags, control);
	};
	for (const auto &[flags, label] : flagLabelPairs) {
		addCheckbox(flags, lang(label));
	}

	applyDependencies(nullptr);
	for (const auto &[flags, checkbox] : *checkboxes) {
		checkbox->finishAnimating();
	}

	return {
		std::move(widget),
		value,
		changes->events() | rpl::map(value)
	};
}

EditFlagsControl<MTPDchatBannedRights::Flags> CreateEditRestrictions(
		QWidget *parent,
		LangKey header,
		MTPDchatBannedRights::Flags restrictions,
		MTPDchatBannedRights::Flags disabled) {
	auto result = CreateEditFlags(
		parent,
		header,
		NegateRestrictions(restrictions),
		disabled,
		RestrictionLabels());
	result.value = [original = std::move(result.value)]{
		return NegateRestrictions(original());
	};
	result.changes = std::move(
		result.changes
	) | rpl::map(NegateRestrictions);

	return result;
}

EditFlagsControl<MTPDchatAdminRights::Flags> CreateEditAdminRights(
		QWidget *parent,
		LangKey header,
		MTPDchatAdminRights::Flags rights,
		MTPDchatAdminRights::Flags disabled,
		bool isGroup,
		bool anyoneCanAddMembers) {
	return CreateEditFlags(
		parent,
		header,
		rights,
		disabled,
		AdminRightLabels(isGroup, anyoneCanAddMembers));
}
