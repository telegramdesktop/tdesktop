/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/reactions/history_view_reactions_strip.h"
#include "data/data_message_reactions.h"
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

class Selector final : public Ui::RpWidget {
public:
	Selector(
		not_null<QWidget*> parent,
		Data::PossibleItemReactions &&reactions,
		IconFactory iconFactory);

	int countWidth(int desiredWidth, int maxWidth);
	[[nodiscard]] QMargins extentsForShadow() const;
	[[nodiscard]] int extendTopForCategories() const;
	[[nodiscard]] int desiredHeight() const;
	void initGeometry(int innerTop);

	[[nodiscard]] rpl::producer<ChosenReaction> chosen() const {
		return _chosen.events();
	}
	[[nodiscard]] rpl::producer<> premiumPromoChosen() const {
		return _premiumPromoChosen.events();
	}

	void updateShowState(
		float64 progress,
		float64 opacity,
		bool appearing,
		bool toggling);

private:
	static constexpr int kFramesCount = 32;

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void paintAppearing(QPainter &p);
	void paintHorizontal(QPainter &p);
	void paintBubble(QPainter &p, int innerWidth);
	void paintBackgroundToBuffer();

	[[nodiscard]] int lookupSelectedIndex(QPoint position) const;
	void setSelected(int index);

	const Data::PossibleItemReactions _reactions;
	Ui::RoundAreaWithShadow _cachedRound;
	Strip _strip;

	rpl::event_stream<ChosenReaction> _chosen;
	rpl::event_stream<> _premiumPromoChosen;

	QImage _paintBuffer;
	float64 _appearProgress = 0.;
	float64 _appearOpacity = 0.;
	QRect _inner;
	QRect _outer;
	QMargins _padding;
	int _size = 0;
	int _recentRows = 0;
	int _columns = 0;
	int _skipx = 0;
	int _skipy = 0;
	int _skipBottom = 0;
	int _pressed = -1;
	bool _appearing = false;
	bool _toggling = false;
	bool _small = false;
	bool _over = false;
	bool _low = false;

};

enum class AttachSelectorResult {
	Skipped,
	Failed,
	Attached,
};
AttachSelectorResult AttachSelectorToMenu(
	not_null<Ui::PopupMenu*> menu,
	QPoint desiredPosition,
	not_null<HistoryItem*> item,
	Fn<void(ChosenReaction)> chosen,
	Fn<void(FullMsgId)> showPremiumPromo,
	IconFactory iconFactory);

} // namespace HistoryView::Reactions
