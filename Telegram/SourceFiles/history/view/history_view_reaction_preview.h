/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct ReactionId;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

[[nodiscard]] bool ShowReactionPreview(
	not_null<Window::SessionController*> controller,
	FullMsgId origin,
	Data::ReactionId reactionId);

} // namespace HistoryView
