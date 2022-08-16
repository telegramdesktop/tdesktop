/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animation_value.h"

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView::Reactions {

struct ChosenReaction;

class Selector final {
public:
	void show(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> widget,
		FullMsgId contextId,
		QRect around);
	void hide(anim::type animated = anim::type::normal);

	[[nodiscard]] rpl::producer<ChosenReaction> chosen() const;
	[[nodiscard]] rpl::producer<bool> shown() const;

private:
	void create(not_null<Window::SessionController*> controller);

	rpl::event_stream<bool> _shown;
	base::unique_qptr<ChatHelpers::TabbedPanel> _panel;
	rpl::event_stream<ChosenReaction> _chosen;
	FullMsgId _contextId;

};

} // namespace HistoryView::Reactions
