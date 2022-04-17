/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Main

class PeerData;

void ShowReportItemsBox(
	not_null<PeerData*> peer, MessageIdsList ids);
void ShowReportPeerBox(
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer);
