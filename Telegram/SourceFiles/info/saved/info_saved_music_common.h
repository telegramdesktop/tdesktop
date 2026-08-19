/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info {
class Controller;
} // namespace Info

namespace Info::Saved {

struct MusicTag {
	explicit MusicTag(not_null<PeerData*> peer)
	: peer(peer) {
	}

	not_null<PeerData*> peer;
};

void SetupSavedMusic(
	not_null<Ui::VerticalLayout*> container,
	not_null<Info::Controller*> controller,
	not_null<PeerData*> peer,
	rpl::producer<std::optional<QColor>> topBarColor);

} // namespace Info::Saved
