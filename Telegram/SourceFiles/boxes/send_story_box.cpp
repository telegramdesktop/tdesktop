/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/send_story_box.h"

#include "apiwrap.h"
#include "base/random.h"
#include "boxes/send_story_row.h"
#include "boxes/story_privacy_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story.h"
#include "data/data_user.h"
#include "info/stories/info_stories_albums.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "menu/menu_checked_action.h"
#include "settings/settings_common.h"
#include "storage/storage_account.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace {

using Option = Api::UserPrivacy::Option;

struct PrivacyLists {
	std::vector<not_null<PeerData*>> excludedEveryone;
	std::vector<not_null<PeerData*>> excludedContacts;
	std::vector<not_null<PeerData*>> selected;
};

struct State {
	explicit State(not_null<PeerData*> peer)
	: peer(peer)
	, postTo(peer)
	, postToList(std::vector<not_null<PeerData*>>{ peer }) {
	}

	not_null<PeerData*> peer;
	std::optional<Api::SendStoryMedia> media;
	TextWithTags caption;
	int period = Api::kStoryDefaultPeriod;
	std::vector<Data::SuggestedReaction> reactions;
	std::vector<Data::UrlArea> urls;
	std::vector<Data::StoryLocation> locations;
	std::vector<Data::WeatherArea> weather;
	std::vector<Data::ChannelPost> channelPosts;
	Data::Story *editing = nullptr;
	Api::UserPrivacy::Rule initialPrivacy;
	PrivacyLists lists;
	rpl::variable<Option> option = Option::Everyone;
	rpl::variable<bool> pinned = true;
	rpl::variable<bool> allowForwards = true;
	rpl::variable<not_null<PeerData*>> postTo;
	rpl::variable<std::vector<not_null<PeerData*>>> postToList;
	rpl::variable<std::vector<Data::StoryAlbum>> albums;
	rpl::variable<std::vector<int>> chosenAlbums;
	rpl::variable<std::vector<not_null<PeerData*>>> blocklist;
	rpl::event_stream<> listsChanges;
	Fn<void()> posted;
	Fn<void()> uploaded;
	std::shared_ptr<Data::StorySourceSave> source;
	bool submitted = false;
};

[[nodiscard]] Api::UserPrivacy::Rule PrivacyFromStory(
		not_null<Data::Story*> story) {
	auto result = Api::UserPrivacy::Rule();
	switch (story->privacy()) {
	case Data::StoryPrivacy::Public:
		result.option = Option::Everyone;
		result.never.peers = story->privacyDisallowed();
		break;
	case Data::StoryPrivacy::Contacts:
		result.option = Option::Contacts;
		result.never.peers = story->privacyDisallowed();
		break;
	case Data::StoryPrivacy::CloseFriends:
		result.option = Option::CloseFriends;
		break;
	case Data::StoryPrivacy::SelectedContacts:
	case Data::StoryPrivacy::Other:
		result.option = Option::Nobody;
		result.always.peers = story->privacyAllowed();
		break;
	}
	return result;
}

[[nodiscard]] Api::UserPrivacy::Rule CollectPrivacy(not_null<State*> state) {
	auto result = Api::UserPrivacy::Rule();
	result.option = state->option.current();
	switch (result.option) {
	case Option::Everyone:
		result.never.peers = state->lists.excludedEveryone;
		break;
	case Option::Contacts:
		result.never.peers = state->lists.excludedContacts;
		break;
	case Option::CloseFriends:
		break;
	case Option::Nobody:
		result.always.peers = state->lists.selected;
		break;
	}
	return result;
}

[[nodiscard]] bool SamePeers(
		const std::vector<not_null<PeerData*>> &a,
		const std::vector<not_null<PeerData*>> &b) {
	return (a.size() == b.size())
		&& ranges::all_of(a, [&](not_null<PeerData*> peer) {
			return ranges::contains(b, peer);
		});
}

[[nodiscard]] bool SamePrivacy(
		const Api::UserPrivacy::Rule &a,
		const Api::UserPrivacy::Rule &b) {
	return (a.option == b.option)
		&& SamePeers(a.always.peers, b.always.peers)
		&& SamePeers(a.never.peers, b.never.peers);
}

constexpr auto kPrivacyPref = "stories.privacy";
constexpr auto kPrivacyMaxPeers = 1000;

[[nodiscard]] QByteArray SerializeStoryPrivacy(
		const Api::UserPrivacy::Rule &rule) {
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << qint32(static_cast<int>(rule.option));
	const auto write = [&](const std::vector<not_null<PeerData*>> &list) {
		stream << quint32(list.size());
		for (const auto &peer : list) {
			stream << quint64(peer->id.value);
		}
	};
	write(rule.always.peers);
	write(rule.never.peers);
	return bytes;
}

[[nodiscard]] std::optional<Api::UserPrivacy::Rule> DeserializeStoryPrivacy(
		not_null<Main::Session*> session,
		const QByteArray &bytes) {
	auto stream = QDataStream(bytes);
	stream.setVersion(QDataStream::Qt_5_1);
	auto value = qint32(0);
	stream >> value;
	if (stream.status() != QDataStream::Ok
		|| value < 0
		|| value > int(Option::Nobody)) {
		return {};
	}
	const auto read = [&]()
			-> std::optional<std::vector<not_null<PeerData*>>> {
		auto count = quint32(0);
		stream >> count;
		if (stream.status() != QDataStream::Ok || count > kPrivacyMaxPeers) {
			return {};
		}
		auto list = std::vector<not_null<PeerData*>>();
		list.reserve(count);
		for (auto i = 0; i != count; ++i) {
			auto raw = quint64(0);
			stream >> raw;
			if (stream.status() != QDataStream::Ok) {
				return {};
			}
			if (const auto id = PeerId(raw)) {
				list.push_back(session->data().peer(id));
			}
		}
		return list;
	};
	auto result = Api::UserPrivacy::Rule();
	result.option = static_cast<Option>(value);
	if (const auto always = read()) {
		result.always.peers = *always;
	} else {
		return {};
	}
	if (const auto never = read()) {
		result.never.peers = *never;
	} else {
		return {};
	}
	return result;
}

void SaveStoryPrivacy(
		not_null<Main::Session*> session,
		const Api::UserPrivacy::Rule &rule) {
	session->local().writePref<QByteArray>(
		kPrivacyPref,
		SerializeStoryPrivacy(rule));
}

[[nodiscard]] Api::UserPrivacy::Rule RestoreStoryPrivacy(
		not_null<Main::Session*> session) {
	if (const auto data = session->local().readPref<QByteArray>(kPrivacyPref);
			!data.isEmpty()) {
		if (const auto rule = DeserializeStoryPrivacy(session, data)) {
			return *rule;
		}
	}
	return Api::UserPrivacy::Rule();
}

[[nodiscard]] QString ExcludedText(
		const std::vector<not_null<PeerData*>> &list) {
	return list.empty()
		? tr::lng_story_privacy_exclude(tr::now)
		: (list.size() == 1)
		? tr::lng_story_privacy_except_one(
			tr::now,
			lt_user,
			list.front()->name())
		: tr::lng_story_privacy_excluded(tr::now, lt_count, list.size());
}

[[nodiscard]] QString PeopleText(
		const std::vector<not_null<PeerData*>> &list) {
	return list.empty()
		? tr::lng_story_privacy_edit_list(tr::now)
		: (list.size() == 1)
		? list.front()->name()
		: tr::lng_story_privacy_people(tr::now, lt_count, list.size());
}

[[nodiscard]] TextWithEntities BlocklistText(
		const std::vector<not_null<PeerData*>> &list) {
	const auto count = int(list.size());
	return count
		? tr::lng_story_blocklist_count(
			tr::now,
			lt_link,
			Ui::Text::Link(
				tr::lng_story_blocklist_count_link(tr::now, lt_count, count),
				u"internal:"_q),
			Ui::Text::WithEntities)
		: tr::lng_story_blocklist_empty(
			tr::now,
			lt_link,
			Ui::Text::Link(
				tr::lng_story_blocklist_empty_link(tr::now),
				u"internal:"_q),
			Ui::Text::WithEntities);
}

[[nodiscard]] QString AlbumsLabel(
		const std::vector<Data::StoryAlbum> &albums,
		const std::vector<int> &chosen) {
	auto titles = QStringList();
	for (const auto &album : albums) {
		if (ranges::contains(chosen, album.id)) {
			titles.push_back(album.title);
		}
	}
	return titles.empty()
		? tr::lng_stories_album_all(tr::now)
		: (titles.size() > 2)
		? tr::lng_story_albums_count(tr::now, lt_count, titles.size())
		: titles.join(u", "_q);
}

[[nodiscard]] bool HasFormatting(const TextWithTags &text) {
	return ranges::any_of(text.tags, [](const TextWithTags::Tag &tag) {
		return !tag.id.isEmpty();
	});
}

void SetupPostAs(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<State*> state) {
	const auto content = box->verticalLayout();
	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto inner = wrap->entity();
	Ui::AddSubsectionTitle(inner, tr::lng_story_post_as());
	const auto peer = state->postTo.current();
	const auto row = inner->add(object_ptr<SendStoryRow>(
		inner,
		SendStoryRowContent{
			.peer = peer,
			.title = peer->name(),
			.subtitle = StoryPostAsStatus(peer),
			.chevron = true,
		}));
	state->postTo.changes() | rpl::on_next([=](not_null<PeerData*> peer) {
		row->setPeer(peer, StoryPostAsStatus(peer));
	}, row->lifetime());
	row->setClickedCallback([=] {
		controller->show(ChooseStoryPostAsBox({
			.controller = controller,
			.peers = state->postToList.current(),
			.selected = state->postTo.current(),
			.done = [=](not_null<PeerData*> peer) {
				state->postTo = peer;
			},
		}));
	});
	Ui::AddSkip(inner);
	Ui::AddDivider(inner);
	Ui::AddSkip(inner);

	wrap->toggleOn(state->postToList.value(
	) | rpl::map([](const std::vector<not_null<PeerData*>> &list) {
		return (list.size() > 1);
	}));
	wrap->finishAnimating();
}

void SetupPrivacy(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		std::shared_ptr<ChatHelpers::Show> show,
		const style::SendStoryBoxStyle *style,
		not_null<State*> state) {
	const auto content = box->verticalLayout();
	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto inner = wrap->entity();
	Ui::AddSubsectionTitle(
		inner,
		tr::lng_story_privacy_header(),
		{},
		style ? &style->subsectionTitle : nullptr);

	const auto choose = [=](
			rpl::producer<QString> title,
			std::vector<not_null<PeerData*>> selected,
			Fn<void(std::vector<not_null<PeerData*>>)> done) {
		show->showBox(ChooseStoryPeersBox({
			.session = session,
			.title = std::move(title),
			.selected = std::move(selected),
			.style = style,
			.done = [=](std::vector<not_null<PeerData*>> peers) {
				done(std::move(peers));
				SaveStoryPrivacy(session, CollectPrivacy(state));
				state->listsChanges.fire({});
			},
		}));
	};
	const auto addRow = [&](
			Option option,
			const QString &title,
			SendStoryRowIcon icon,
			Fn<QString()> subtitle,
			Fn<void()> openList,
			Fn<bool()> openOnSelect) {
		const auto row = inner->add(object_ptr<SendStoryRow>(
			inner,
			SendStoryRowContent{
				.icon = icon,
				.title = title,
				.subtitle = subtitle(),
				.subtitleLink = true,
				.radio = true,
				.divider = true,
				.style = style ? &style->row : nullptr,
			}));
		row->setChecked(
			(state->option.current() == option),
			anim::type::instant);
		state->option.value() | rpl::on_next([=](Option value) {
			row->setChecked(value == option);
		}, row->lifetime());
		state->listsChanges.events() | rpl::on_next([=] {
			row->setSubtitle(subtitle());
		}, row->lifetime());
		row->setClickedCallback([=] {
			const auto chosen = (state->option.current() == option);
			state->option = option;
			if (chosen || openOnSelect()) {
				openList();
			}
		});
		return row;
	};

	addRow(
		Option::Everyone,
		tr::lng_story_privacy_everyone(tr::now),
		SendStoryRowIcon{
			.icon = &st::storyPrivacyIconEveryone,
			.bg1 = &st::historyPeer4UserpicBg,
			.bg2 = &st::historyPeer4UserpicBg2,
		},
		[=] { return ExcludedText(state->lists.excludedEveryone); },
		[=] {
			choose(
				tr::lng_story_privacy_exclude_title(),
				state->lists.excludedEveryone,
				[=](std::vector<not_null<PeerData*>> peers) {
					state->lists.excludedEveryone = std::move(peers);
				});
		},
		[] { return false; });

	addRow(
		Option::Contacts,
		tr::lng_story_privacy_contacts(tr::now),
		SendStoryRowIcon{
			.icon = &st::storyPrivacyIconContacts,
			.bg1 = &st::historyPeer5UserpicBg,
			.bg2 = &st::historyPeer5UserpicBg2,
		},
		[=] { return ExcludedText(state->lists.excludedContacts); },
		[=] {
			choose(
				tr::lng_story_privacy_exclude_title(),
				state->lists.excludedContacts,
				[=](std::vector<not_null<PeerData*>> peers) {
					state->lists.excludedContacts = std::move(peers);
				});
		},
		[] { return false; });

	addRow(
		Option::CloseFriends,
		tr::lng_story_privacy_close_friends(tr::now),
		SendStoryRowIcon{
			.icon = &st::storyPrivacyIconCloseFriends,
			.bg1 = &st::historyPeer2UserpicBg,
			.bg2 = &st::historyPeer2UserpicBg2,
		},
		[=] { return PeopleText(CloseFriendsList(session)); },
		[=] {
			ShowCloseFriendsBox(show, crl::guard(box, [=] {
				state->listsChanges.fire({});
			}), style);
		},
		[=] { return CloseFriendsList(session).empty(); });

	const auto last = addRow(
		Option::Nobody,
		tr::lng_story_privacy_selected(tr::now),
		SendStoryRowIcon{
			.icon = &st::storyPrivacyIconSelected,
			.bg1 = &st::historyPeer8UserpicBg,
			.bg2 = &st::historyPeer8UserpicBg2,
		},
		[=] { return PeopleText(state->lists.selected); },
		[=] {
			choose(
				tr::lng_story_privacy_select_title(),
				state->lists.selected,
				[=](std::vector<not_null<PeerData*>> peers) {
					state->lists.selected = std::move(peers);
				});
		},
		[=] { return state->lists.selected.empty(); });
	last->setDivider(false);
	Ui::AddSkip(inner);

	const auto blocklist = Ui::AddDividerText(
		inner,
		state->blocklist.value() | rpl::map(BlocklistText),
		st::defaultBoxDividerLabelPadding,
		style ? style->dividerText : st::defaultDividerLabel);
	blocklist->setClickHandlerFilter([=](const auto &...) {
		ShowStoryBlocklistBox(
			show,
			state->blocklist.current(),
			style);
		return false;
	});
	session->api().stories().blocklistValue(
	) | rpl::on_next([=](std::vector<not_null<PeerData*>> &&list) {
		state->blocklist = std::move(list);
	}, inner->lifetime());

	wrap->toggleOn(state->postTo.value(
	) | rpl::map([](not_null<PeerData*> peer) {
		return peer->isSelf();
	}));
	wrap->finishAnimating();
}

void SetupAlbums(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<State*> state) {
	const auto content = box->verticalLayout();
	const auto stories = &controller->session().data().stories();
	const auto wrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			content,
			object_ptr<Ui::VerticalLayout>(content)));
	const auto inner = wrap->entity();
	const auto button = Settings::AddButtonWithLabel(
		inner,
		tr::lng_story_albums(),
		rpl::combine(
			state->albums.value(),
			state->chosenAlbums.value()
		) | rpl::map(AlbumsLabel),
		st::settingsButtonNoIcon);
	const auto menu = button->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	const auto toggleAlbum = [=](int id) {
		auto list = state->chosenAlbums.current();
		if (ranges::contains(list, id)) {
			list.erase(ranges::remove(list, id), end(list));
		} else {
			list.push_back(id);
		}
		state->chosenAlbums = std::move(list);
	};
	button->setClickedCallback([=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		(*menu)->addAction(
			tr::lng_stories_album_new_button(tr::now),
			[=] {
				controller->show(Box(
					Info::Stories::NewAlbumBox,
					controller,
					state->postTo.current(),
					StoryId(),
					crl::guard(box, [=](Data::StoryAlbum album) {
						toggleAlbum(album.id);
					})));
			},
			&st::menuIconAddToFolder);
		(*menu)->addSeparator();
		for (const auto &album : state->albums.current()) {
			const auto id = album.id;
			Menu::AddCheckedAction(
				menu->get(),
				album.title,
				[=] { toggleAlbum(id); },
				nullptr,
				ranges::contains(state->chosenAlbums.current(), id));
		}
		(*menu)->popup(QCursor::pos());
	});
	state->postTo.value(
	) | rpl::map([=](not_null<PeerData*> peer) {
		return stories->albumsListValue(peer->id);
	}) | rpl::flatten_latest(
	) | rpl::on_next([=](std::vector<Data::StoryAlbum> &&list) {
		state->albums = std::move(list);
	}, wrap->lifetime());
	state->postTo.changes() | rpl::on_next([=] {
		state->chosenAlbums = std::vector<int>();
	}, wrap->lifetime());
	wrap->toggleOn(rpl::combine(
		state->pinned.value(),
		state->albums.value()
	) | rpl::map([](bool pinned, const std::vector<Data::StoryAlbum> &list) {
		return pinned && !list.empty();
	}));
	wrap->finishAnimating();
}

void SetupOptions(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<State*> state) {
	const auto content = box->verticalLayout();

	Ui::AddSkip(content);
	const auto screenshots = Settings::AddButtonWithIcon(
		content,
		tr::lng_story_screenshots(),
		st::settingsButtonNoIcon);
	screenshots->toggleOn(state->allowForwards.value());
	screenshots->toggledChanges() | rpl::on_next([=](bool value) {
		state->allowForwards = value;
	}, screenshots->lifetime());

	auto keepText = state->postTo.value(
	) | rpl::map([](not_null<PeerData*> peer) {
		return peer->isSelf()
			? tr::lng_story_keep()
			: peer->isMegagroup()
			? tr::lng_story_keep_group()
			: tr::lng_story_keep_channel();
	}) | rpl::flatten_latest();
	const auto keep = Settings::AddButtonWithIcon(
		content,
		std::move(keepText),
		st::settingsButtonNoIcon);
	keep->toggleOn(state->pinned.value());
	keep->toggledChanges() | rpl::on_next([=](bool value) {
		state->pinned = value;
	}, keep->lifetime());

	SetupAlbums(box, controller, state);
	Ui::AddSkip(content);

	const auto hours = float64(state->period / 3600);
	auto keepAbout = state->postTo.value(
	) | rpl::map([=](not_null<PeerData*> peer) {
		return peer->isSelf()
			? tr::lng_story_keep_about(lt_count, rpl::single(hours))
			: peer->isMegagroup()
			? tr::lng_story_keep_group_about(lt_count, rpl::single(hours))
			: tr::lng_story_keep_channel_about(lt_count, rpl::single(hours));
	}) | rpl::flatten_latest();
	Ui::AddDividerText(content, std::move(keepAbout));
}

void ApplyPrivacy(
		not_null<State*> state,
		const Api::UserPrivacy::Rule &rule) {
	state->initialPrivacy = rule;
	state->option = rule.option;
	switch (rule.option) {
	case Option::Everyone:
		state->lists.excludedEveryone = rule.never.peers;
		break;
	case Option::Contacts:
		state->lists.excludedContacts = rule.never.peers;
		break;
	case Option::Nobody:
		state->lists.selected = rule.always.peers;
		break;
	case Option::CloseFriends:
		break;
	}
}

[[nodiscard]] Data::StorySourceSend ToSourceSend(not_null<State*> state) {
	const auto peers = [](const auto &list) {
		auto result = std::vector<PeerId>();
		for (const auto &peer : list) {
			result.push_back(peer->id);
		}
		return result;
	};
	auto albums = std::vector<int>();
	for (const auto &album : state->chosenAlbums.current()) {
		albums.push_back(album);
	}
	return {
		.option = int(state->option.current()),
		.excludedEveryone = peers(state->lists.excludedEveryone),
		.excludedContacts = peers(state->lists.excludedContacts),
		.selected = peers(state->lists.selected),
		.pinned = state->pinned.current(),
		.allowForwards = state->allowForwards.current(),
		.albums = std::move(albums),
		.postTo = state->postTo.current()->id,
	};
}

void Submit(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<State*> state) {
	if (state->submitted) {
		return;
	}
	auto privacy = CollectPrivacy(state);
	state->submitted = true;
	if (const auto editing = state->editing) {
		if (!SamePrivacy(privacy, state->initialPrivacy)) {
			session->api().stories().send({
				.peer = editing->peer(),
				.privacy = std::move(privacy),
				.editId = editing->id(),
				.editPrivacy = true,
			});
			session->data().stories().editSources().updateSendState(
				editing->peer()->id,
				editing->id(),
				ToSourceSend(state));
		}
		box->closeBox();
		return;
	}
	auto caption = state->caption;
	if (HasFormatting(caption)
		&& session->appConfig().storiesEntitiesPremiumOnly()
		&& !session->premium()) {
		show->showToast(tr::lng_story_entities_premium(tr::now));
		state->submitted = false;
		return;
	}
	const auto pendingId = state->source
		? base::RandomValue<uint64>()
		: uint64(0);
	if (pendingId) {
		session->data().stories().editSources().savePending(
			pendingId,
			*state->source,
			ToSourceSend(state));
	}
	const auto uploaded = state->uploaded;
	session->api().stories().send({
		.peer = state->postTo.current(),
		.media = base::take(state->media),
		.caption = std::move(caption),
		.privacy = std::move(privacy),
		.albums = (state->pinned.current()
			? state->chosenAlbums.current()
			: std::vector<int>()),
		.reactions = std::move(state->reactions),
		.urls = std::move(state->urls),
		.locations = std::move(state->locations),
		.weather = std::move(state->weather),
		.channelPosts = std::move(state->channelPosts),
		.period = state->period,
		.pinned = state->pinned.current(),
		.noForwards = !state->allowForwards.current(),
		.sent = (pendingId || uploaded)
			? Fn<void(StoryId, TimeId)>([=](
					StoryId id,
					TimeId expiresAt) {
				if (pendingId) {
					session->data().stories().editSources().finalize(
						pendingId,
						id,
						expiresAt);
				}
				if (uploaded) {
					uploaded();
				}
			})
			: nullptr,
		.failed = pendingId
			? Fn<void(const QString &)>([=](const QString &) {
				session->data().stories().editSources().removePending(
					pendingId);
			})
			: nullptr,
	});
	const auto posted = state->posted;
	if (posted) {
		posted();
	}
	box->closeBox();
}

} // namespace

void SendStoryBox(not_null<Ui::GenericBox*> box, SendStoryBoxArgs &&args) {
	const auto controller = args.controller;
	const auto editing = args.editing;
	const auto show = args.show ? args.show : controller->uiShow();
	const auto session = &show->session();
	const auto state = box->lifetime().make_state<State>(args.peer);
	state->media = std::move(args.media);
	state->caption = std::move(args.caption);
	state->period = args.period;
	state->reactions = std::move(args.reactions);
	state->urls = std::move(args.urls);
	state->locations = std::move(args.locations);
	state->weather = std::move(args.weather);
	state->channelPosts = std::move(args.channelPosts);
	state->editing = editing;
	state->posted = std::move(args.posted);
	state->uploaded = std::move(args.uploaded);
	state->source = std::move(args.source);
	if (editing) {
		ApplyPrivacy(state, PrivacyFromStory(editing));
	} else {
		ApplyPrivacy(state, RestoreStoryPrivacy(session));
	}

	box->setTitle(editing
		? tr::lng_story_edit_privacy_title()
		: tr::lng_story_post_title());
	box->setWidth(st::boxWideWidth);
	box->setStyle(args.style ? args.style->box : st::sendStoryBox);
	box->addTopButton(
		args.style ? args.style->close : st::boxTitleClose,
		[=] { box->closeBox(); });

	if (!editing) {
		SetupPostAs(box, controller, state);
	}
	SetupPrivacy(box, session, show, args.style, state);
	if (!editing) {
		SetupOptions(box, controller, state);
	}

	box->addButton(
		editing ? tr::lng_story_save_settings() : tr::lng_story_post_button(),
		[=] { Submit(box, session, show, state); });

	if (state->peer->isSelf() && !editing) {
		const auto self = state->peer;
		session->api().stories().requestChatsToSend(crl::guard(box, [=](
				std::vector<not_null<PeerData*>> chats) {
			auto list = std::vector<not_null<PeerData*>>{ self };
			for (const auto &chat : chats) {
				if (chat != self) {
					list.push_back(chat);
				}
			}
			state->postToList = std::move(list);
		}));
	}
}
