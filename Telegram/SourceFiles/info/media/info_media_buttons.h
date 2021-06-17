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
#include "info/profile/info_profile_values.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "styles/style_info.h"

namespace Info {
namespace Media {

using Type = Storage::SharedMediaType;

inline tr::phrase<lngtag_count> MediaTextPhrase(Type type) {
	switch (type) {
	case Type::Photo: return tr::lng_profile_photos;
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

	using Button = Ui::SettingsButton;
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
		not_null<Window::SessionNavigation*> navigation,
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
			std::make_shared<Info::Memento>(peer, Section(type)));
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

} // namespace Media
} // namespace Info
