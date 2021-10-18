/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui::EditPeer {

constexpr auto kMaxGroupChannelTitle = 128;
constexpr auto kMaxUserFirstLastName = 64;
constexpr auto kMaxChannelDescription = 255;
constexpr auto kMinUsernameLength = 5;
constexpr auto kUsernameCheckTimeout = crl::time(200);

} // namespace Ui::EditPeer
