/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class BoxContent;
} // namespace Ui

namespace style {
struct SendStoryBoxStyle;
} // namespace style

namespace Window {
class SessionController;
} // namespace Window

struct StoryPeersChooserArgs {
	not_null<Main::Session*> session;
	rpl::producer<QString> title;
	std::vector<not_null<PeerData*>> selected;
	const style::SendStoryBoxStyle *style = nullptr;
	Fn<void(std::vector<not_null<PeerData*>>)> done;
};

[[nodiscard]] object_ptr<Ui::BoxContent> ChooseStoryPeersBox(
	StoryPeersChooserArgs &&args);

void ShowCloseFriendsBox(
	std::shared_ptr<ChatHelpers::Show> show,
	Fn<void()> done = nullptr,
	const style::SendStoryBoxStyle *style = nullptr);

[[nodiscard]] std::vector<not_null<PeerData*>> CloseFriendsList(
	not_null<Main::Session*> session);

void ShowStoryBlocklistBox(
	std::shared_ptr<ChatHelpers::Show> show,
	std::vector<not_null<PeerData*>> current,
	const style::SendStoryBoxStyle *style = nullptr);

struct StoryPostAsArgs {
	not_null<Window::SessionController*> controller;
	std::vector<not_null<PeerData*>> peers;
	not_null<PeerData*> selected;
	Fn<void(not_null<PeerData*>)> done;
};

[[nodiscard]] object_ptr<Ui::BoxContent> ChooseStoryPostAsBox(
	StoryPostAsArgs &&args);

[[nodiscard]] QString StoryPostAsStatus(not_null<PeerData*> peer);
