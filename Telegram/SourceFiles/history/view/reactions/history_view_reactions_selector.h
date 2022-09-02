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
class EmojiListWidget;
class StickersListFooter;
enum class EmojiListMode;
} // namespace ChatHelpers

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PopupMenu;
class ScrollArea;
class PlainShadow;
} // namespace Ui

namespace HistoryView::Reactions {

class Selector final : public Ui::RpWidget {
public:
	Selector(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> parentController,
		const Data::PossibleItemReactionsRef &reactions,
		IconFactory iconFactory,
		Fn<void(bool fast)> close);
	Selector(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> parentController,
		ChatHelpers::EmojiListMode mode,
		std::vector<DocumentId> recent,
		Fn<void(bool fast)> close);

	[[nodiscard]] bool useTransparency() const;

	int countWidth(int desiredWidth, int maxWidth);
	[[nodiscard]] QMargins extentsForShadow() const;
	[[nodiscard]] int extendTopForCategories() const;
	[[nodiscard]] int minimalHeight() const;
	void setSpecialExpandTopSkip(int skip);
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

	struct ExpandingRects {
		QRect categories;
		QRect list;
		float64 radius = 0.;
		int finalBottom = 0;
	};

	Selector(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> parentController,
		const Data::PossibleItemReactionsRef &reactions,
		ChatHelpers::EmojiListMode mode,
		std::vector<DocumentId> recent,
		IconFactory iconFactory,
		Fn<void(bool fast)> close);

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void paintAppearing(QPainter &p);
	void paintCollapsed(QPainter &p);
	void paintExpanding(Painter &p, float64 progress);
	ExpandingRects paintExpandingBg(QPainter &p, float64 progress);
	void paintFadingExpandIcon(QPainter &p, float64 progress);
	void paintExpanded(QPainter &p);
	void paintBubble(QPainter &p, int innerWidth);
	void paintBackgroundToBuffer();

	[[nodiscard]] int recentCount() const;
	[[nodiscard]] int countSkipLeft() const;
	[[nodiscard]] int lookupSelectedIndex(QPoint position) const;
	void setSelected(int index);

	void expand();
	void cacheExpandIcon();
	void createList(not_null<Window::SessionController*> controller);
	void finishExpand();

	const base::weak_ptr<Window::SessionController> _parentController;
	const Data::PossibleItemReactions _reactions;
	const std::vector<DocumentId> _recent;
	const ChatHelpers::EmojiListMode _listMode;
	Fn<void()> _jumpedToPremium;
	base::flat_map<DocumentId, int> _defaultReactionInStripMap;
	Ui::RoundAreaWithShadow _cachedRound;
	QPoint _defaultReactionShift;
	QPoint _stripPaintOneShift;
	std::unique_ptr<Strip> _strip;

	rpl::event_stream<ChosenReaction> _chosen;
	rpl::event_stream<> _premiumPromoChosen;

	Ui::ScrollArea *_scroll = nullptr;
	ChatHelpers::EmojiListWidget *_list = nullptr;
	ChatHelpers::StickersListFooter *_footer = nullptr;
	Ui::PlainShadow *_shadow = nullptr;
	rpl::variable<int> _shadowTop = 0;
	rpl::variable<int> _shadowSkip = 0;

	QImage _paintBuffer;
	Ui::Animations::Simple _expanding;
	float64 _appearProgress = 0.;
	float64 _appearOpacity = 0.;
	QRect _inner;
	QRect _outer;
	QRect _outerWithBubble;
	QImage _expandIconCache;
	QMargins _padding;
	int _specialExpandTopSkip = 0;
	int _collapsedTopSkip = 0;
	int _size = 0;
	int _recentRows = 0;
	int _columns = 0;
	int _skipx = 0;
	int _skipy = 0;
	int _pressed = -1;
	bool _useTransparency = false;
	bool _appearing = false;
	bool _toggling = false;
	bool _expanded = false;
	bool _expandScheduled = false;
	bool _expandFinished = false;
	bool _small = false;
	bool _over = false;
	bool _low = false;

};

enum class AttachSelectorResult {
	Skipped,
	Failed,
	Attached,
};

AttachSelectorResult MakeJustSelectorMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	QPoint desiredPosition,
	ChatHelpers::EmojiListMode mode,
	std::vector<DocumentId> recent,
	Fn<void(ChosenReaction)> chosen);

AttachSelectorResult AttachSelectorToMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	QPoint desiredPosition,
	not_null<HistoryItem*> item,
	Fn<void(ChosenReaction)> chosen,
	Fn<void(FullMsgId)> showPremiumPromo,
	IconFactory iconFactory);

} // namespace HistoryView::Reactions
