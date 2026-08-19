/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

struct PollData;

namespace Ui {
class ChatStyle;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView {

class Element;
class ElementOverlayHost;

void ShowAddPollOptionOverlay(
	ElementOverlayHost &host,
	not_null<QWidget*> parent,
	not_null<Element*> view,
	not_null<PollData*> poll,
	FullMsgId context,
	not_null<Window::SessionController*> controller,
	not_null<const Ui::ChatStyle*> st);

} // namespace HistoryView
