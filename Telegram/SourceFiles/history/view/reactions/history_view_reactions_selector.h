/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/expected.h"
#include "base/unique_qptr.h"
#include "data/data_message_reactions.h"
#include "history/view/reactions/history_view_reactions_strip.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/round_area_with_shadow.h"
#include "ui/rp_widget.h"

namespace Data {
struct Reaction;
struct ReactionId;
} // namespace Data

namespace ChatHelpers {
class Show;
class TabbedPanel;
class EmojiListWidget;
class StickersListWidget;
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
class FlatLabel;
} // namespace Ui

namespace HistoryView::Reactions {

class UnifiedFactoryOwner final {
public:
	using RecentFactory = Fn<std::unique_ptr<Ui::Text::CustomEmoji>(
		DocumentId,
		Fn<void()>)>;

	UnifiedFactoryOwner(
		not_null<Main::Session*> session,
		const std::vector<Data::Reaction> &reactions,
		Strip *strip = nullptr);

	[[nodiscard]] const std::vector<DocumentId> &unifiedIdsList() const {
		return _unifiedIdsList;
	}

	[[nodiscard]] Data::ReactionId lookupReactionId(
		DocumentId unifiedId) const;

	[[nodiscard]] RecentFactory factory();

private:
	const not_null<Main::Session*> _session;
	Strip *_strip = nullptr;

	std::vector<DocumentId> _unifiedIdsList;
	base::flat_map<DocumentId, Data::ReactionId> _defaultReactionIds;
	base::flat_map<DocumentId, int> _defaultReactionInStripMap;

	QPoint _defaultReactionShift;
	QPoint _stripPaintOneShift;

};

class Selector final : public Ui::RpWidget {
public:
	Selector(
		not_null<QWidget*> parent,
		const style::EmojiPan &st,
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::PossibleItemReactionsRef &reactions,
		TextWithEntities about,
		Fn<void(bool fast)> close,
		IconFactory iconFactory = nullptr,
		Fn<bool()> paused = nullptr,
		bool child = false);
#if 0 // not ready
	Selector(
		not_null<QWidget*> parent,
		const style::EmojiPan &st,
		std::shared_ptr<ChatHelpers::Show> show,
		ChatHelpers::EmojiListMode mode,
		std::vector<DocumentId> recent,
		Fn<void(bool fast)> close,
		bool child = false);
#endif
	~Selector();

	[[nodiscard]] bool useTransparency() const;

	int countWidth(int desiredWidth, int maxWidth);
	[[nodiscard]] QMargins marginsForShadow() const;
	[[nodiscard]] int extendTopForCategories() const;
	[[nodiscard]] int extendTopForCategoriesAndAbout(int width) const;
	[[nodiscard]] int opaqueExtendTopAbout(int width) const;
	[[nodiscard]] int minimalHeight() const;
	[[nodiscard]] int countAppearedWidth(float64 progress) const;
	void setSpecialExpandTopSkip(int skip);
	void initGeometry(int innerTop);
	void beforeDestroy();

	void setOpaqueHeightExpand(int expand, Fn<void(int)> apply);

	[[nodiscard]] rpl::producer<ChosenReaction> chosen() const {
		return _chosen.events();
	}
	[[nodiscard]] rpl::producer<> willExpand() const {
		return _willExpand.events();
	}
	[[nodiscard]] rpl::producer<> escapes() const;

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
		float64 expanding = 0.;
		int finalBottom = 0;
		int frame = 0;
		QRect outer;
	};

	Selector(
		not_null<QWidget*> parent,
		const style::EmojiPan &st,
		std::shared_ptr<ChatHelpers::Show> show,
		const Data::PossibleItemReactionsRef &reactions,
		ChatHelpers::EmojiListMode mode,
		std::vector<DocumentId> recent,
		TextWithEntities about,
		IconFactory iconFactory,
		Fn<bool()> paused,
		Fn<void(bool fast)> close,
		bool child);

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void paintAppearing(QPainter &p);
	void paintCollapsed(QPainter &p);
	void paintExpanding(Painter &p, float64 progress);
	void paintExpandingBg(QPainter &p, const ExpandingRects &rects);
	void paintFadingExpandIcon(QPainter &p, float64 progress);
	void paintExpanded(QPainter &p);
	void paintNonTransparentExpandRect(QPainter &p, const QRect &) const;
	void paintBubble(QPainter &p, int innerWidth);
	void paintBackgroundToBuffer();

	ExpandingRects updateExpandingRects(float64 progress);

	[[nodiscard]] int recentCount() const;
	[[nodiscard]] int countSkipLeft() const;
	[[nodiscard]] int lookupSelectedIndex(QPoint position) const;
	void setSelected(int index);

	void expand();
	void cacheExpandIcon();
	void createList();
	void finishExpand();
	ChosenReaction lookupChosen(const Data::ReactionId &id) const;
	void preloadAllRecentsAnimations();

	const style::EmojiPan &_st;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const Data::PossibleItemReactions _reactions;
	const std::vector<DocumentId> _recent;
	const ChatHelpers::EmojiListMode _listMode;
	const Fn<bool()> _paused;
	Fn<void()> _jumpedToPremium;
	Ui::RoundAreaWithShadow _cachedRound;
	std::unique_ptr<Strip> _strip;
	std::unique_ptr<Ui::FlatLabel> _about;
	mutable int _aboutExtend = 0;

	rpl::event_stream<ChosenReaction> _chosen;
	rpl::event_stream<> _willExpand;
	rpl::event_stream<> _escapes;

	Ui::ScrollArea *_scroll = nullptr;
	ChatHelpers::EmojiListWidget *_list = nullptr;
	ChatHelpers::StickersListWidget *_stickers = nullptr;
	ChatHelpers::StickersListFooter *_footer = nullptr;
	std::unique_ptr<UnifiedFactoryOwner> _unifiedFactoryOwner;
	Ui::PlainShadow *_shadow = nullptr;
	rpl::variable<int> _shadowTop = 0;
	rpl::variable<int> _shadowSkip = 0;
	bool _showEmptySearch = false;

	QImage _paintBuffer;
	Ui::Animations::Simple _expanding;
	float64 _appearProgress = 0.;
	float64 _appearOpacity = 0.;
	QRect _inner;
	QRect _outer;
	QRect _outerWithBubble;
	QImage _expandIconCache;
	QImage _aboutCache;
	QMargins _padding;
	int _specialExpandTopSkip = 0;
	int _collapsedTopSkip = 0;
	int _topAddOnExpand = 0;

	int _opaqueHeightExpand = 0;
	Fn<void(int)> _opaqueApplyHeightExpand;

	const int _size = 0;
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

#if 0 // not ready
AttachSelectorResult MakeJustSelectorMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	QPoint desiredPosition,
	ChatHelpers::EmojiListMode mode,
	std::vector<DocumentId> recent,
	Fn<void(ChosenReaction)> chosen);
#endif

AttachSelectorResult AttachSelectorToMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	QPoint desiredPosition,
	not_null<HistoryItem*> item,
	Fn<void(ChosenReaction)> chosen,
	TextWithEntities about,
	IconFactory iconFactory = nullptr);

[[nodiscard]] auto AttachSelectorToMenu(
	not_null<Ui::PopupMenu*> menu,
	QPoint desiredPosition,
	const style::EmojiPan &st,
	std::shared_ptr<ChatHelpers::Show> show,
	const Data::PossibleItemReactionsRef &reactions,
	TextWithEntities about,
	IconFactory iconFactory = nullptr,
	Fn<bool()> paused = nullptr
) -> base::expected<not_null<Selector*>, AttachSelectorResult>;

[[nodiscard]] TextWithEntities ItemReactionsAbout(
	not_null<HistoryItem*> item);

} // namespace HistoryView::Reactions
