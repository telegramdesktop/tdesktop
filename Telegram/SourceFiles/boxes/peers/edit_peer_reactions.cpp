/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_reactions.h"

#include "boxes/reactions_settings_box.h" // AddReactionLottieIcon
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "settings/settings_common.h"
#include "styles/style_settings.h"
#include "styles/style_info.h"

void EditAllowedReactionsBox(
		not_null<Ui::GenericBox*> box,
		bool isGroup,
		const std::vector<Data::Reaction> &list,
		const base::flat_set<QString> &selected,
		Fn<void(const std::vector<QString> &)> callback) {
	const auto iconHeight = st::editPeerReactionsPreview;
	box->setTitle(tr::lng_manage_peer_reactions());

	struct State {
		base::flat_map<QString, not_null<Ui::SettingsButton*>> toggles;
		rpl::variable<bool> anyToggled;
		rpl::event_stream<bool> forceToggleAll;
	};
	const auto state = box->lifetime().make_state<State>(State{
		.anyToggled = !selected.empty(),
	});

	const auto collect = [=] {
		auto result = std::vector<QString>();
		result.reserve(state->toggles.size());
		for (const auto &[emoji, button] : state->toggles) {
			if (button->toggled()) {
				result.push_back(emoji);
			}
		}
		return result;
	};

	const auto container = box->verticalLayout();

	const auto enabled = Settings::AddButton(
		container,
		tr::lng_manage_peer_reactions_enable(),
		st::manageGroupButton.button);
	if (!list.empty()) {
		AddReactionLottieIcon(
			enabled,
			enabled->sizeValue(
			) | rpl::map([=](const QSize &size) {
				return QPoint(
					st::manageGroupButton.iconPosition.x(),
					(size.height() - iconHeight) / 2);
			}),
			iconHeight,
			list.front(),
			rpl::never<>(),
			rpl::never<>(),
			&enabled->lifetime());
	}
	enabled->toggleOn(state->anyToggled.value());
	enabled->toggledChanges(
	) | rpl::filter([=](bool value) {
		return (value != state->anyToggled.current());
	}) | rpl::start_to_stream(state->forceToggleAll, enabled->lifetime());

	Settings::AddSkip(container);
	Settings::AddDividerText(
		container,
		(isGroup
			? tr::lng_manage_peer_reactions_about
			: tr::lng_manage_peer_reactions_about_channel)());

	Settings::AddSkip(container);
	Settings::AddSubsectionTitle(
		container,
		tr::lng_manage_peer_reactions_available());

	const auto active = [&](const Data::Reaction &entry) {
		return selected.contains(entry.emoji);
	};
	const auto add = [&](const Data::Reaction &entry) {
		const auto button = Settings::AddButton(
			container,
			rpl::single(entry.title),
			st::manageGroupButton.button);
		AddReactionLottieIcon(
			button,
			button->sizeValue(
			) | rpl::map([=](const QSize &size) {
				return QPoint(
					st::editPeerReactionsIconLeft,
					(size.height() - iconHeight) / 2);
			}),
			iconHeight,
			entry,
			button->events(
			) | rpl::filter([=](not_null<QEvent*> event) {
				return event->type() == QEvent::Enter;
			}) | rpl::to_empty,
			rpl::never<>(),
			&button->lifetime());
		state->toggles.emplace(entry.emoji, button);
		button->toggleOn(rpl::single(
			active(entry)
		) | rpl::then(
			state->forceToggleAll.events()
		));
		button->toggledChanges(
		) | rpl::start_with_next([=](bool toggled) {
			if (toggled) {
				state->anyToggled = true;
			} else if (collect().empty()) {
				state->anyToggled = false;
			}
		}, button->lifetime());
	};
	for (const auto &entry : list) {
		add(entry);
	}

	box->addButton(tr::lng_settings_save(), [=] {
		const auto ids = collect();
		box->closeBox();
		callback(ids);
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void SaveAllowedReactions(
		not_null<PeerData*> peer,
		const std::vector<QString> &allowed) {
	auto ids = allowed | ranges::views::transform([=](QString value) {
		return MTP_string(value);
	}) | ranges::to<QVector<MTPstring>>;

	peer->session().api().request(MTPmessages_SetChatAvailableReactions(
		peer->input,
		MTP_vector<MTPstring>(ids)
	)).done([=](const MTPUpdates &result) {
		peer->session().api().applyUpdates(result);
		if (const auto chat = peer->asChat()) {
			chat->setAllowedReactions({ begin(allowed), end(allowed) });
		} else if (const auto channel = peer->asChannel()) {
			channel->setAllowedReactions({ begin(allowed), end(allowed) });
		} else {
			Unexpected("Invalid peer type in SaveAllowedReactions.");
		}
	}).fail([=](const MTP::Error &error) {
		if (error.type() == qstr("REACTION_INVALID")) {
			peer->updateFullForced();
			peer->owner().reactions().refresh();
		}
	}).send();
}
