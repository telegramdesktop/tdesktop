/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
enum class StorySourcesList : uchar;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Dialogs::Stories {

struct Content;

[[nodiscard]] rpl::producer<Content> ContentForSession(
	not_null<Main::Session*> session,
	Data::StorySourcesList list);

[[nodiscard]] rpl::producer<Content> LastForPeer(not_null<PeerData*> peer);

} // namespace Dialogs::Stories
