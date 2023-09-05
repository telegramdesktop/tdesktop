/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

struct RequestPeerQuery;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

void ShowChoosePeerBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<UserData*> bot,
	RequestPeerQuery query,
	Fn<void(not_null<PeerData*>)> chosen);
