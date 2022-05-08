/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

template <typename Object>
class object_ptr;

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Main

class PeerData;

[[nodiscard]] object_ptr<Ui::BoxContent> ReportItemsBox(
	not_null<PeerData*> peer,
	MessageIdsList ids);
[[nodiscard]] object_ptr<Ui::BoxContent> ReportProfilePhotoBox(
	not_null<PeerData*> peer,
	not_null<PhotoData*> photo);
void ShowReportPeerBox(
	not_null<Window::SessionController*> window,
	not_null<PeerData*> peer);
