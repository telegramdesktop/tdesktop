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
#include "storage/storage_shared_media.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_values.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "styles/style_info.h"

namespace Info {
namespace Media {

using Type = Storage::SharedMediaType;

inline auto MediaTextPhrase(Type type) {
	switch (type) {
	case Type::Photo: return lng_profile_photos;
	case Type::Video: return lng_profile_videos;
	case Type::File: return lng_profile_files;
	case Type::MusicFile: return lng_profile_songs;
	case Type::Link: return lng_profile_shared_links;
	case Type::VoiceFile: return lng_profile_audios;
//	case Type::RoundFile: return lng_profile_rounds;
	}
	Unexpected("Type in MediaTextPhrase()");
};

inline auto MediaText(Type type) {
	return [phrase = MediaTextPhrase(type)](int count) {
		return phrase(lt_count, count);
	};
}

template <typename Count, typename Text>
inline auto AddCountedButton(
		Ui::VerticalLayout *parent,
		Count &&count,
		Text &&textFromCount,
		Ui::MultiSlideTracker &tracker) {
	using namespace rpl::mappers;

	using Button = Profile::Button;
	auto forked = std::move(count)
		| start_spawning(parent->lifetime());
	auto text = rpl::duplicate(
		forked
	) | rpl::map([textFromCount](int count) {
		return (count > 0)
			? textFromCount(count)
			: QString();
	});
	auto button = parent->add(object_ptr<Ui::SlideWrap<Button>>(
		parent,
		object_ptr<Button>(
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
		not_null<Window::Navigation*> navigation,
		not_null<PeerData*> peer,
		PeerData *migrated,
		Type type,
		Ui::MultiSlideTracker &tracker) {
	auto result = AddCountedButton(
		parent,
		Profile::SharedMediaCountValue(peer, migrated, type),
		MediaText(type),
		tracker)->entity();
	result->addClickHandler([=] {
		navigation->showSection(
			Info::Memento(peer->id, Section(type)));
	});
	return std::move(result);
};

inline auto AddCommonGroupsButton(
		Ui::VerticalLayout *parent,
		not_null<Window::Navigation*> navigation,
		not_null<UserData*> user,
		Ui::MultiSlideTracker &tracker) {
	auto result = AddCountedButton(
		parent,
		Profile::CommonGroupsCountValue(user),
		[](int count) {
			return lng_profile_common_groups(lt_count, count);
		},
		tracker)->entity();
	result->addClickHandler([=] {
		navigation->showSection(
			Info::Memento(user->id, Section::Type::CommonGroups));
	});
	return std::move(result);
};

} // namespace Media
} // namespace Info