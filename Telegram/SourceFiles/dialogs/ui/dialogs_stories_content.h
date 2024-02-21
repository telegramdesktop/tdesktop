/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
enum class StorySourcesList : uchar;
class Story;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs::Stories {

struct Content;
struct ShowMenuRequest;

[[nodiscard]] rpl::producer<Content> ContentForSession(
	not_null<Main::Session*> session,
	Data::StorySourcesList list);

[[nodiscard]] rpl::producer<Content> LastForPeer(not_null<PeerData*> peer);

void FillSourceMenu(
	not_null<Window::SessionController*> controller,
	const ShowMenuRequest &request);

} // namespace Dialogs::Stories
