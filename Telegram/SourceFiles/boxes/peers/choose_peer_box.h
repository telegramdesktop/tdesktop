/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

struct RequestPeerQuery;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

QPointer<Ui::BoxContent> ShowChoosePeerBox(
	not_null<Window::SessionNavigation*> navigation,
	Fn<void(not_null<PeerData*>)> &&chosen,
	RequestPeerQuery query);
