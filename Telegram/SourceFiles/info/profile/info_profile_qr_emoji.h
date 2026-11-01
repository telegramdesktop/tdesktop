/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/peer_qr_emoji_field.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
struct PeerQrColors;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Info::Profile {

struct QrEmojiPicker {
	not_null<Ui::RpWidget*> widget;
	rpl::producer<QString> chosen;
	Fn<void(const std::vector<QString>&)> setPicks;
	Fn<void(bool)> setShown;
	Fn<void(const Ui::PeerQrColors&)> setColors;
};

[[nodiscard]] QrEmojiPicker CreateQrEmojiPicker(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	const std::vector<QString> &picks);

[[nodiscard]] rpl::producer<Ui::PeerQrEmojiSource> QrEmojiSourceValue(
	not_null<Main::Session*> session);

} // namespace Info::Profile
