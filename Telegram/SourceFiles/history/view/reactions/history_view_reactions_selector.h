/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/round_area_with_shadow.h"
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
	void initGeometry(int innerTop);

	void updateShowState(
		float64 progress,
		float64 opacity,
		bool appearing,
		bool toggling);

private:
	static constexpr int kFramesCount = 32;

	void paintEvent(QPaintEvent *e);

	void paintAppearing(QPainter &p);
	void paintBg(QPainter &p);

	PossibleReactions _reactions;
	QImage _appearBuffer;
	Ui::RoundAreaWithShadow _cachedRound;
	float64 _appearProgress = 0.;
	float64 _appearOpacity = 0.;
	QRect _inner;
	QMargins _padding;
	int _size = 0;
	int _recentRows = 0;
	int _columns = 0;
	int _skipx = 0;
	int _skipy = 0;
	int _skipBottom = 0;
	bool _appearing = false;
	bool _toggling = false;
	bool _small = false;

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
