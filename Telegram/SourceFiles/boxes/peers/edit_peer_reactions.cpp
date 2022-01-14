/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_reactions.h"

#include "data/data_message_reactions.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_peer.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "info/profile/info_profile_icon.h"
#include "settings/settings_common.h"
#include "styles/style_settings.h"
#include "styles/style_info.h"

namespace {

using Data::Reaction;

void AddReactionIcon(
		not_null<Ui::RpWidget*> button,
		not_null<DocumentData*> document) {
	struct State {
		std::shared_ptr<Data::DocumentMedia> media;
		QImage image;
	};

	const auto size = st::editPeerReactionsPreview;
	const auto state = button->lifetime().make_state<State>(State{
		.media = document->createMediaView(),
	});
	const auto icon = Ui::CreateChild<Ui::RpWidget>(button.get());
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->resize(size, size);
	button->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		icon->moveToLeft(
			st::settingsSectionIconLeft,
			(size.height() - icon->height()) / 2,
			size.width());
	}, icon->lifetime());

	const auto setImage = [=](not_null<Image*> image) {
		state->image = Images::prepare(
			image->original(),
			size * style::DevicePixelRatio(),
			size * style::DevicePixelRatio(),
			Images::Option::Smooth | Images::Option::TransparentBackground,
			size,
			size);
		icon->update();
	};
	if (const auto image = state->media->getStickerLarge()) {
		setImage(image);
	} else {
		document->session().downloaderTaskFinished(
		) | rpl::map([=] {
			return state->media->getStickerLarge();
		}) | rpl::filter_nullptr() | rpl::take(
			1
		) | rpl::start_with_next([=](not_null<Image*> image) {
			setImage(image);
		}, button->lifetime());
	}

	icon->paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(icon);
		if (!state->image.isNull()) {
			p.drawImage(0, 0, state->image);
		}
	}, icon->lifetime());
}

} // namespace

void EditAllowedReactionsBox(
		not_null<Ui::GenericBox*> box,
		bool isGroup,
		const std::vector<Reaction> &list,
		const std::vector<Reaction> &selected,
		Fn<void(const std::vector<QString> &)> callback) {
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
	Ui::CreateChild<Info::Profile::FloatingIcon>(
		enabled.get(),
		st::infoIconReactions,
		st::manageGroupButton.iconPosition);
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
		return ranges::contains(selected, entry.emoji, &Reaction::emoji);
	};
	const auto add = [&](const Data::Reaction &entry) {
		const auto button = Settings::AddButton(
			container,
			rpl::single(entry.title),
			st::manageGroupButton.button);
		AddReactionIcon(button, entry.staticIcon);
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
	}) | ranges::to<QVector>;

	peer->session().api().request(MTPmessages_SetChatAvailableReactions(
		peer->input,
		MTP_vector<MTPstring>(ids)
	)).done([=](const MTPUpdates &result) {
		peer->session().api().applyUpdates(result);
		if (const auto chat = peer->asChat()) {
			chat->setAllowedReactions(allowed);
		} else if (const auto channel = peer->asChannel()) {
			channel->setAllowedReactions(allowed);
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
