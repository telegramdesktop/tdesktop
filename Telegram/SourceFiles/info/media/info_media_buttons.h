/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang/lang_keys.h"
#include "storage/storage_shared_media.h"

namespace Ui {
class AbstractButton;
class MultiSlideTracker;
class SettingsButton;
class VerticalLayout;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

namespace Info::Media {

using Type = Storage::SharedMediaType;

[[nodiscard]] tr::phrase<lngtag_count> MediaTextPhrase(Type type);

[[nodiscard]] Fn<QString(int)> MediaText(Type type);

[[nodiscard]] not_null<Ui::SlideWrap<Ui::SettingsButton>*> AddCountedButton(
	Ui::VerticalLayout *parent,
	rpl::producer<int> &&count,
	Fn<QString(int)> &&textFromCount,
	Ui::MultiSlideTracker &tracker);

[[nodiscard]] not_null<Ui::SettingsButton*> AddButton(
	Ui::VerticalLayout *parent,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	MsgId topicRootId,
	PeerData *migrated,
	Type type,
	Ui::MultiSlideTracker &tracker);

[[nodiscard]] not_null<Ui::SettingsButton*> AddCommonGroupsButton(
	Ui::VerticalLayout *parent,
	not_null<Window::SessionNavigation*> navigation,
	not_null<UserData*> user,
	Ui::MultiSlideTracker &tracker);

[[nodiscard]] not_null<Ui::SettingsButton*> AddSimilarPeersButton(
	Ui::VerticalLayout *parent,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	Ui::MultiSlideTracker &tracker);

[[nodiscard]] not_null<Ui::SettingsButton*> AddStoriesButton(
	Ui::VerticalLayout *parent,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	Ui::MultiSlideTracker &tracker);

[[nodiscard]] not_null<Ui::SettingsButton*> AddSavedSublistButton(
	Ui::VerticalLayout *parent,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	Ui::MultiSlideTracker &tracker);

[[nodiscard]] not_null<Ui::SettingsButton*> AddPeerGiftsButton(
	Ui::VerticalLayout *parent,
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	Ui::MultiSlideTracker &tracker);

} // namespace Info::Media
