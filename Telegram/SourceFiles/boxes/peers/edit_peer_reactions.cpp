/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_reactions.h"

#include "boxes/reactions_settings_box.h" // AddReactionAnimatedIcon
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
#include "ui/widgets/checkbox.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_info.h"

void EditAllowedReactionsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		bool isGroup,
		const std::vector<Data::Reaction> &list,
		const Data::AllowedReactions &allowed,
		Fn<void(const Data::AllowedReactions &)> callback) {
	using namespace Data;
	using namespace rpl::mappers;

	const auto iconHeight = st::editPeerReactionsPreview;
	box->setTitle(tr::lng_manage_peer_reactions());

	enum class Option {
		All,
		Some,
		None,
	};
	struct State {
		base::flat_map<ReactionId, not_null<Ui::SettingsButton*>> toggles;
		rpl::variable<Option> option; // For groups.
		rpl::variable<bool> anyToggled; // For channels.
		rpl::event_stream<bool> forceToggleAll; // For channels.
	};
	const auto optionInitial = (allowed.type != AllowedReactionsType::Some)
		? Option::All
		: allowed.some.empty()
		? Option::None
		: Option::Some;
	const auto state = box->lifetime().make_state<State>(State{
		.option = optionInitial,
		.anyToggled = (optionInitial != Option::None),
	});

	const auto collect = [=] {
		auto result = AllowedReactions();
		if (!isGroup || state->option.current() == Option::Some) {
			result.some.reserve(state->toggles.size());
			for (const auto &[id, button] : state->toggles) {
				if (button->toggled()) {
					result.some.push_back(id);
				}
			}
		}
		result.type = isGroup
			? (state->option.current() != Option::All
				? AllowedReactionsType::Some
				: AllowedReactionsType::All)
			: (result.some.size() == state->toggles.size())
			? AllowedReactionsType::Default
			: AllowedReactionsType::Some;
		return result;
	};

	const auto container = box->verticalLayout();

	const auto enabled = isGroup
		? nullptr
		: container->add(object_ptr<Ui::SettingsButton>(
			container,
			tr::lng_manage_peer_reactions_enable(),
			st::manageGroupButton.button));
	if (enabled && !list.empty()) {
		AddReactionAnimatedIcon(
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
	if (enabled) {
		enabled->toggleOn(state->anyToggled.value());
		enabled->toggledChanges(
		) | rpl::filter([=](bool value) {
			return (value != state->anyToggled.current());
		}) | rpl::start_to_stream(state->forceToggleAll, enabled->lifetime());
	}
	const auto group = std::make_shared<Ui::RadioenumGroup<Option>>(
		state->option.current());
	group->setChangedCallback([=](Option value) {
		state->option = value;
	});
	const auto addOption = [&](Option option, const QString &text) {
		if (!isGroup) {
			return;
		}
		container->add(
			object_ptr<Ui::Radioenum<Option>>(
				container,
				group,
				option,
				text,
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	addOption(Option::All, tr::lng_manage_peer_reactions_all(tr::now));
	addOption(Option::Some, tr::lng_manage_peer_reactions_some(tr::now));
	addOption(Option::None, tr::lng_manage_peer_reactions_none(tr::now));

	const auto about = [](Option option) {
		switch (option) {
		case Option::All: return tr::lng_manage_peer_reactions_all_about();
		case Option::Some: return tr::lng_manage_peer_reactions_some_about();
		case Option::None: return tr::lng_manage_peer_reactions_none_about();
		}
		Unexpected("Option value in EditAllowedReactionsBox.");
	};
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		(isGroup
			? (state->option.value()
				| rpl::map(about)
				| rpl::flatten_latest())
			: tr::lng_manage_peer_reactions_about_channel()));

	const auto wrap = enabled ? nullptr : container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	if (wrap) {
		wrap->toggleOn(state->option.value() | rpl::map(_1 == Option::Some));
		wrap->finishAnimating();
	}
	const auto reactions = wrap ? wrap->entity() : container.get();

	Ui::AddSkip(reactions);
	Ui::AddSubsectionTitle(
		reactions,
		(enabled
			? tr::lng_manage_peer_reactions_available()
			: tr::lng_manage_peer_reactions_some_title()));

	const auto like = QString::fromUtf8("\xf0\x9f\x91\x8d");
	const auto dislike = QString::fromUtf8("\xf0\x9f\x91\x8e");
	const auto activeOnStart = [&](const ReactionId &id) {
		const auto inSome = ranges::contains(allowed.some, id);
		if (!isGroup) {
			return inSome || (allowed.type != AllowedReactionsType::Some);
		}
		const auto emoji = id.emoji();
		const auto isDefault = (emoji == like) || (emoji == dislike);
		return (allowed.type != AllowedReactionsType::Some)
			? isDefault
			: (inSome || (isDefault && allowed.some.empty()));
	};
	const auto add = [&](const Reaction &entry) {
		const auto button = reactions->add(object_ptr<Ui::SettingsButton>(
			reactions,
			rpl::single(entry.title),
			st::manageGroupButton.button));
		AddReactionAnimatedIcon(
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
		state->toggles.emplace(entry.id, button);
		button->toggleOn(rpl::single(
			activeOnStart(entry.id)
		) | rpl::then(enabled
			? (state->forceToggleAll.events() | rpl::type_erased())
			: rpl::never<bool>()
		));
		if (enabled) {
			button->toggledChanges(
			) | rpl::start_with_next([=](bool toggled) {
				if (toggled) {
					state->anyToggled = true;
				} else if (collect().some.empty()) {
					state->anyToggled = false;
				}
			}, button->lifetime());
		}
	};
	for (const auto &entry : list) {
		add(entry);
	}
	for (const auto &id : allowed.some) {
		if (const auto customId = id.custom()) {
			// Some possible forward compatibility.
			const auto button = reactions->add(object_ptr<Ui::SettingsButton>(
				reactions,
				rpl::single(u"Custom reaction"_q),
				st::manageGroupButton.button));
			AddReactionCustomIcon(
				button,
				button->sizeValue(
				) | rpl::map([=](const QSize &size) {
					return QPoint(
						st::editPeerReactionsIconLeft,
						(size.height() - iconHeight) / 2);
				}),
				iconHeight,
				navigation->parentController(),
				customId,
				rpl::never<>(),
				&button->lifetime());
			state->toggles.emplace(id, button);
			button->toggleOn(rpl::single(true));
		}
	}

	box->addButton(tr::lng_settings_save(), [=] {
		const auto result = collect();
		box->closeBox();
		callback(result);
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void SaveAllowedReactions(
		not_null<PeerData*> peer,
		const Data::AllowedReactions &allowed) {
	auto ids = allowed.some | ranges::views::transform(
		Data::ReactionToMTP
	) | ranges::to<QVector<MTPReaction>>;

	using Type = Data::AllowedReactionsType;
	const auto updated = (allowed.type != Type::Some)
		? MTP_chatReactionsAll(MTP_flags((allowed.type == Type::Default)
			? MTPDchatReactionsAll::Flag(0)
			: MTPDchatReactionsAll::Flag::f_allow_custom))
		: allowed.some.empty()
		? MTP_chatReactionsNone()
		: MTP_chatReactionsSome(MTP_vector<MTPReaction>(ids));
	peer->session().api().request(MTPmessages_SetChatAvailableReactions(
		peer->input,
		updated
	)).done([=](const MTPUpdates &result) {
		peer->session().api().applyUpdates(result);
		if (const auto chat = peer->asChat()) {
			chat->setAllowedReactions(Data::Parse(updated));
		} else if (const auto channel = peer->asChannel()) {
			channel->setAllowedReactions(Data::Parse(updated));
		} else {
			Unexpected("Invalid peer type in SaveAllowedReactions.");
		}
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"REACTION_INVALID"_q) {
			peer->updateFullForced();
			peer->owner().reactions().refreshDefault();
		}
	}).send();
}
