/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/type_traits.h"
#include "base/call_delayed.h"
#include "mtproto/mtproto_proxy_data.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace App {

template <typename Guard, typename Lambda>
[[nodiscard]] inline auto LambdaDelayed(int duration, Guard &&object, Lambda &&lambda) {
	auto guarded = crl::guard(
		std::forward<Guard>(object),
		std::forward<Lambda>(lambda));
	return [saved = std::move(guarded), duration] {
		auto copy = saved;
		base::call_delayed(duration, std::move(copy));
	};
}

} // namespace App

namespace Ui {

// Legacy global methods.

void showPeerHistory(not_null<const PeerData*> peer, MsgId msgId);
void showPeerHistory(not_null<const History*> history, MsgId msgId);
void showChatsList(not_null<Main::Session*> session);

bool skipPaintEvent(QWidget *widget, QPaintEvent *event);

} // namespace Ui
