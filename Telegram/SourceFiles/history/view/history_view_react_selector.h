/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animation_value.h"
#include "ui/rp_widget.h"

namespace Data {
struct ReactionId;
} // namespace Data

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PopupMenu;
} // namespace Ui

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

struct PossibleReactions {
	std::vector<Data::ReactionId> recent;
	bool morePremiumAvailable = false;
	bool customAllowed = false;
};

class PopupSelector final : public Ui::RpWidget {
public:
	PopupSelector(
		not_null<QWidget*> parent,
		PossibleReactions reactions);

	int countWidth(int desiredWidth, int maxWidth);
	[[nodiscard]] QMargins extentsForShadow() const;
	[[nodiscard]] int extendTopForCategories() const;
	[[nodiscard]] int desiredHeight() const;

protected:
	void paintEvent(QPaintEvent *e);

private:
	int _columns = 0;

};

enum class AttachSelectorResult {
	Skipped,
	Failed,
	Attached,
};
AttachSelectorResult AttachSelectorToMenu(
	not_null<Ui::PopupMenu*> menu,
	QPoint desiredPosition,
	not_null<HistoryItem*> item);

} // namespace HistoryView::Reactions
