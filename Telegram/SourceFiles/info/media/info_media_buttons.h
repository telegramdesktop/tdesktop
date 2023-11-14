/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/mappers.h>
#include <rpl/map.h>
#include "lang/lang_keys.h"
#include "data/data_stories_ids.h"
#include "storage/storage_shared_media.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "info/stories/info_stories_widget.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"
#include "data/data_user.h"
#include "styles/style_info.h"

namespace Info::Media {

using Type = Storage::SharedMediaType;

inline tr::phrase<lngtag_count> MediaTextPhrase(Type type) {
	switch (type) {
	case Type::Photo: return tr::lng_profile_photos;
	case Type::GIF: return tr::lng_profile_gifs;
	case Type::Video: return tr::lng_profile_videos;
	case Type::File: return tr::lng_profile_files;
	case Type::MusicFile: return tr::lng_profile_songs;
	case Type::Link: return tr::lng_profile_shared_links;
	case Type::RoundVoiceFile: return tr::lng_profile_audios;
	}
	Unexpected("Type in MediaTextPhrase()");
};

inline auto MediaText(Type type) {
	return [phrase = MediaTextPhrase(type)](int count) {
		return phrase(tr::now, lt_count, count);
	};
}

template <typename Count, typename Text>
inline auto AddCountedButton(
		Ui::VerticalLayout *parent,
		Count &&count,
		Text &&textFromCount,
		Ui::MultiSlideTracker &tracker) {
	using namespace rpl::mappers;

	using namespace ::Settings;
	auto forked = std::move(count)
		| start_spawning(parent->lifetime());
	auto text = rpl::duplicate(
		forked
	) | rpl::map([textFromCount](int count) {
		return (count > 0)
			? textFromCount(count)
			: QString();
	});
	auto button = parent->add(object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
		parent,
		object_ptr<Ui::SettingsButton>(
			parent,
			std::move(text),
			st::infoSharedMediaButton))
	)->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		rpl::duplicate(forked) | rpl::map(_1 > 0)
	);
	tracker.track(button);
	return button;
};

inline auto AddButton(
		Ui::VerticalLayout *parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		MsgId topicRootId,
		PeerData *migrated,
		Type type,
		Ui::MultiSlideTracker &tracker) {
	auto result = AddCountedButton(
		parent,
		Profile::SharedMediaCountValue(peer, topicRootId, migrated, type),
		MediaText(type),
		tracker)->entity();
	result->addClickHandler([=] {
		const auto topic = topicRootId
			? peer->forumTopicFor(topicRootId)
			: nullptr;
		if (topicRootId && !topic) {
			return;
		}
		navigation->showSection(topicRootId
			? std::make_shared<Info::Memento>(topic, Section(type))
			: std::make_shared<Info::Memento>(peer, Section(type)));
	});
	return result;
};

inline auto AddCommonGroupsButton(
		Ui::VerticalLayout *parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> user,
		Ui::MultiSlideTracker &tracker) {
	auto result = AddCountedButton(
		parent,
		Profile::CommonGroupsCountValue(user),
		[](int count) {
			return tr::lng_profile_common_groups(tr::now, lt_count, count);
		},
		tracker)->entity();
	result->addClickHandler([=] {
		navigation->showSection(
			std::make_shared<Info::Memento>(user, Section::Type::CommonGroups));
	});
	return result;
};

inline auto AddStoriesButton(
		Ui::VerticalLayout *parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Ui::MultiSlideTracker &tracker) {
	auto count = rpl::single(0) | rpl::then(Data::SavedStoriesIds(
		peer,
		ServerMaxStoryId - 1,
		0
	) | rpl::map([](const Data::StoriesIdsSlice &slice) {
		return slice.fullCount().value_or(0);
	}));
	const auto phrase = peer->isChannel() ? (+[](int count) {
		return tr::lng_profile_posts(tr::now, lt_count, count);
	}) : (+[](int count) {
		return tr::lng_profile_saved_stories(tr::now, lt_count, count);
	});
	auto result = AddCountedButton(
		parent,
		std::move(count),
		phrase,
		tracker)->entity();
	result->addClickHandler([=] {
		navigation->showSection(Info::Stories::Make(peer));
	});
	return result;
};

} // namespace Info::Media
