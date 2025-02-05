/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_report.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/dragging_scroll_manager.h"
#include "ui/widgets/tooltip.h"
#include "ui/widgets/scroll_area.h"
#include "history/history_view_swipe_data.h"
#include "history/view/history_view_top_bar_widget.h"

#include <QtGui/QPainterPath>

struct ClickContext;
struct ClickHandlerContext;

namespace Data {
struct Group;
} // namespace Data

namespace HistoryView {
class ElementDelegate;
class EmojiInteractions;
struct TextState;
struct SelectionModeResult;
struct StateRequest;
enum class CursorState : char;
enum class PointState : char;
class EmptyPainter;
class Element;
class TranslateTracker;
struct PinnedId;
struct SelectedQuote;
class AboutView;
} // namespace HistoryView

namespace HistoryView::Reactions {
class Manager;
struct ChosenReaction;
struct ButtonParameters;
} // namespace HistoryView::Reactions

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class ChatTheme;
class ChatStyle;
class PopupMenu;
struct ChatPaintContext;
class PathShiftGradient;
struct PeerUserpicView;
} // namespace Ui

namespace Dialogs::Ui {
using namespace ::Ui;
class VideoUserpic;
} // namespace Dialogs::Ui

class HistoryInner;
class HistoryMainElementDelegate;
class HistoryMainElementDelegateMixin {
public:
	void setCurrent(HistoryInner *widget) {
		_widget = widget;
	}

	virtual not_null<HistoryView::ElementDelegate*> delegate() = 0;
	virtual ~HistoryMainElementDelegateMixin();

private:
	friend class HistoryMainElementDelegate;

	HistoryMainElementDelegateMixin();

	HistoryInner *_widget = nullptr;

};

class HistoryWidget;
class HistoryInner
	: public Ui::RpWidget
	, public Ui::AbstractTooltipShower {
public:
	using Element = HistoryView::Element;

	HistoryInner(
		not_null<HistoryWidget*> historyWidget,
		not_null<Ui::ScrollArea*> scroll,
		not_null<Window::SessionController*> controller,
		not_null<History*> history);
	~HistoryInner();

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<Ui::ChatTheme*> theme() const {
		return _theme.get();
	}

	Ui::ChatPaintContext preparePaintContext(const QRect &clip) const;

	void messagesReceived(
		not_null<PeerData*> peer,
		const QVector<MTPMessage> &messages);
	void messagesReceivedDown(
		not_null<PeerData*> peer,
		const QVector<MTPMessage> &messages);

	[[nodiscard]] TextForMimeData getSelectedText() const;

	void touchScrollUpdated(const QPoint &screenPos);

	void setItemsRevealHeight(int revealHeight);
	void changeItemsRevealHeight(int revealHeight);
	void checkActivation();
	void recountHistoryGeometry(bool initial = false);
	void updateSize();
	void setShownPinned(HistoryItem *item);

	void repaintItem(const HistoryItem *item);
	void repaintItem(const Element *view);

	[[nodiscard]] bool canCopySelected() const;
	[[nodiscard]] bool canDeleteSelected() const;

	[[nodiscard]] auto getSelectionState() const
		-> HistoryView::TopBarWidget::SelectedState;
	void clearSelected(bool onlyTextSelection = false);
	[[nodiscard]] MessageIdsList getSelectedItems() const;
	[[nodiscard]] bool hasSelectedItems() const;
	[[nodiscard]] HistoryView::SelectionModeResult inSelectionMode() const;
	[[nodiscard]] bool elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) const;
	void elementStartStickerLoop(not_null<const Element*> view);
	void elementShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context);
	void elementOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context);
	void elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView = false);
	void elementCancelUpload(const FullMsgId &context);
	void elementShowTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback);
	bool elementAnimationsPaused();
	void elementSendBotCommand(
		const QString &command,
		const FullMsgId &context);
	void elementSearchInList(
		const QString &query,
		const FullMsgId &context);
	void elementHandleViaClick(not_null<UserData*> bot);
	bool elementIsChatWide();
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient();
	void elementReplyTo(const FullReplyTo &to);
	void elementStartInteraction(not_null<const Element*> view);
	void elementStartPremium(
		not_null<const Element*> view,
		Element *replacing);
	void elementCancelPremium(not_null<const Element*> view);
	void elementStartEffect(
		not_null<const Element*> view,
		Element *replacing);

	void startEffectOnRead(not_null<HistoryItem*> item);
	void updateBotInfo(bool recount = true);

	bool wasSelectedText() const;

	// updates history->scrollTopItem/scrollTopOffset
	void visibleAreaUpdated(int top, int bottom);

	int historyHeight() const;
	int historyScrollTop() const;
	int migratedTop() const;
	int historyTop() const;
	int historyDrawTop() const;

	void setChooseReportReason(Data::ReportInput reportInput);
	void clearChooseReportReason();

	// -1 if should not be visible, -2 if bad history()
	[[nodiscard]] int itemTop(const HistoryItem *item) const;
	[[nodiscard]] int itemTop(const Element *view) const;
	[[nodiscard]] Element *viewByItem(const HistoryItem *item) const;

	// Returns (view, offset-from-top).
	[[nodiscard]] std::pair<Element*, int> findViewForPinnedTracking(
		int top) const;

	void refreshAboutView(bool force = false);
	void notifyMigrateUpdated();

	// Ui::AbstractTooltipShower interface.
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	void onParentGeometryChanged();
	bool consumeScrollAction(QPoint delta);

	[[nodiscard]] Fn<HistoryView::ElementDelegate*()> elementDelegateFactory(
		FullMsgId itemId) const;
	[[nodiscard]] ClickHandlerContext prepareClickHandlerContext(
		FullMsgId itemId) const;
	[[nodiscard]] ClickContext prepareClickContext(
		Qt::MouseButton button,
		FullMsgId itemId) const;

	[[nodiscard]] static auto DelegateMixin()
	-> std::unique_ptr<HistoryMainElementDelegateMixin>;

protected:
	bool focusNextPrevChild(bool next) override;

	bool eventHook(QEvent *e) override; // calls touchEvent when necessary
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	void onTouchSelect();
	void onTouchScrollTimer();

	[[nodiscard]] static int SelectionViewOffset(
		not_null<const HistoryInner*> inner,
		not_null<const Element*> view);

	using ChosenReaction = HistoryView::Reactions::ChosenReaction;
	using VideoUserpic = Dialogs::Ui::VideoUserpic;
	using SelectedItems = std::map<HistoryItem*, TextSelection, std::less<>>;
	enum class MouseAction {
		None,
		PrepareDrag,
		Dragging,
		PrepareSelect,
		Selecting,
	};
	enum class SelectAction {
		Select,
		Deselect,
		Invert,
	};
	enum class EnumItemsDirection {
		TopToBottom,
		BottomToTop,
	};
	using CursorState = HistoryView::CursorState;
	using PointState = HistoryView::PointState;
	using TextState = HistoryView::TextState;
	using StateRequest = HistoryView::StateRequest;

	// This function finds all history items that are displayed and calls template method
	// for each found message (in given direction) in the passed history with passed top offset.
	//
	// Method has "bool (*Method)(not_null<Element*> view, int itemtop, int itembottom)" signature
	// if it returns false the enumeration stops immidiately.
	template <bool TopToBottom, typename Method>
	void enumerateItemsInHistory(History *history, int historytop, Method method);

	template <EnumItemsDirection direction, typename Method>
	void enumerateItems(Method method) {
		constexpr auto TopToBottom = (direction == EnumItemsDirection::TopToBottom);
		if (TopToBottom && _migrated) {
			enumerateItemsInHistory<TopToBottom>(_migrated, migratedTop(), method);
		}
		enumerateItemsInHistory<TopToBottom>(_history, historyTop(), method);
		if (!TopToBottom && _migrated) {
			enumerateItemsInHistory<TopToBottom>(_migrated, migratedTop(), method);
		}
	}

	// This function finds all userpics on the left that are displayed and calls template method
	// for each found userpic (from the top to the bottom) using enumerateItems() method.
	//
	// Method has "bool (*Method)(not_null<Element*> view, int userpicTop)" signature
	// if it returns false the enumeration stops immidiately.
	template <typename Method>
	void enumerateUserpics(Method method);

	// This function finds all date elements that are displayed and calls template method
	// for each found date element (from the bottom to the top) using enumerateItems() method.
	//
	// Method has "bool (*Method)(not_null<Element*> view, int itemtop, int dateTop)" signature
	// if it returns false the enumeration stops immidiately.
	template <typename Method>
	void enumerateDates(Method method);

	void scrollDateCheck();
	void scrollDateHideByTimer();
	bool canHaveFromUserpics() const;
	void mouseActionStart(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionUpdate();
	void mouseActionUpdate(const QPoint &screenPos);
	void mouseActionFinish(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionCancel();
	std::unique_ptr<QMimeData> prepareDrag();
	void performDrag();

	void paintEmpty(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int width,
		int height);

	QPoint mapPointToItem(QPoint p, const Element *view) const;
	QPoint mapPointToItem(QPoint p, const HistoryItem *item) const;
	[[nodiscard]] HistoryView::SelectedQuote selectedQuote(
		not_null<HistoryItem*> item) const;

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);
	void cancelContextDownload(not_null<DocumentData*> document);
	void openContextGif(FullMsgId itemId);
	void saveContextGif(FullMsgId itemId);
	void copyContextText(FullMsgId itemId);
	void showContextInFolder(not_null<DocumentData*> document);
	void savePhotoToFile(not_null<PhotoData*> photo);
	void saveDocumentToFile(
		FullMsgId contextId,
		not_null<DocumentData*> document);
	void copyContextImage(not_null<PhotoData*> photo, FullMsgId itemId);
	void showStickerPackInfo(not_null<DocumentData*> document);

	void itemRemoved(not_null<const HistoryItem*> item);
	void viewRemoved(not_null<const Element*> view);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	void adjustCurrent(int32 y) const;
	void adjustCurrent(int32 y, History *history) const;
	Element *prevItem(Element *item);
	Element *nextItem(Element *item);
	void updateDragSelection(Element *dragSelFrom, Element *dragSelTo, bool dragSelecting);
	TextSelection itemRenderSelection(
		not_null<Element*> view,
		int selfromy,
		int seltoy) const;
	TextSelection computeRenderSelection(
		not_null<const SelectedItems*> selected,
		not_null<Element*> view) const;

	void toggleScrollDateShown();
	void repaintScrollDateCallback();
	bool displayScrollDate() const;
	void scrollDateHide();
	void keepScrollDateForNow();

	void applyDragSelection();
	void applyDragSelection(not_null<SelectedItems*> toItems) const;
	void addSelectionRange(
		not_null<SelectedItems*> toItems,
		not_null<History*> history,
		int fromblock,
		int fromitem,
		int toblock,
		int toitem) const;
	bool isSelected(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const;
	bool isSelectedGroup(
		not_null<SelectedItems*> toItems,
		not_null<const Data::Group*> group) const;
	bool isSelectedAsGroup(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const;
	bool goodForSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		int &totalCount) const;
	void addToSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const;
	void removeFromSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item) const;
	void changeSelection(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		SelectAction action) const;
	void changeSelectionAsGroup(
		not_null<SelectedItems*> toItems,
		not_null<HistoryItem*> item,
		SelectAction action) const;
	void forwardItem(FullMsgId itemId);
	void forwardAsGroup(FullMsgId itemId);
	void deleteItem(not_null<HistoryItem*> item);
	void deleteItem(FullMsgId itemId);
	void deleteAsGroup(FullMsgId itemId);
	void reportItem(FullMsgId itemId);
	void reportAsGroup(FullMsgId itemId);
	void blockSenderItem(FullMsgId itemId);
	void blockSenderAsGroup(FullMsgId itemId);
	void copySelectedText();

	[[nodiscard]] auto reactionButtonParameters(
		not_null<const Element*> view,
		QPoint position,
		const HistoryView::TextState &reactionState) const
	-> HistoryView::Reactions::ButtonParameters;
	void toggleFavoriteReaction(not_null<Element*> view) const;
	void reactionChosen(const ChosenReaction &reaction);

	void setupSharingDisallowed();
	void setupSwipeReply();
	[[nodiscard]] bool hasCopyRestriction(HistoryItem *item = nullptr) const;
	[[nodiscard]] bool hasCopyMediaRestriction(
		not_null<HistoryItem*> item) const;
	bool showCopyRestriction(HistoryItem *item = nullptr);
	bool showCopyMediaRestriction(not_null<HistoryItem*> item);
	[[nodiscard]] bool hasCopyRestrictionForSelected() const;
	bool showCopyRestrictionForSelected();
	[[nodiscard]] bool hasSelectRestriction() const;

	VideoUserpic *validateVideoUserpic(not_null<PeerData*> peer);

	// Does any of the shown histories has this flag set.
	bool hasPendingResizedItems() const;

	const not_null<HistoryWidget*> _widget;
	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<Window::SessionController*> _controller;
	const not_null<PeerData*> _peer;
	const not_null<History*> _history;
	const not_null<HistoryView::ElementDelegate*> _elementDelegate;
	const std::unique_ptr<HistoryView::EmojiInteractions> _emojiInteractions;
	std::shared_ptr<Ui::ChatTheme> _theme;

	History *_migrated = nullptr;
	HistoryView::ElementDelegate *_migratedElementDelegate = nullptr;
	int _contentWidth = 0;
	int _historyPaddingTop = 0;
	int _revealHeight = 0;

	// Save visible area coords for painting / pressing userpics.
	int _visibleAreaTop = 0;
	int _visibleAreaBottom = 0;

	// With migrated history we perhaps do not need to display
	// the first _history message date (just skip it by height).
	int _historySkipHeight = 0;

	std::unique_ptr<HistoryView::AboutView> _aboutView;
	std::unique_ptr<HistoryView::EmptyPainter> _emptyPainter;
	std::unique_ptr<HistoryView::TranslateTracker> _translateTracker;

	mutable History *_curHistory = nullptr;
	mutable int _curBlock = 0;
	mutable int _curItem = 0;

	style::cursor _cursor = style::cur_default;
	SelectedItems _selected;
	std::optional<Data::ReportInput> _chooseForReportReason;

	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	QPainterPath _highlightPathCache;
	bool _isChatWide = false;

	base::flat_set<not_null<const HistoryItem*>> _animatedStickersPlayed;
	base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> _userpics;
	base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> _userpicsCache;
	base::flat_map<MsgId, Ui::PeerUserpicView> _hiddenSenderUserpics;
	base::flat_map<
		not_null<PeerData*>,
		std::unique_ptr<VideoUserpic>> _videoUserpics;

	std::unique_ptr<HistoryView::Reactions::Manager> _reactionsManager;
	rpl::variable<HistoryItem*> _reactionsItem;
	HistoryItem *_pinnedItem = nullptr;

	MouseAction _mouseAction = MouseAction::None;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	QPoint _dragStartPosition;
	QPoint _mousePosition;
	HistoryItem *_mouseActionItem = nullptr;
	HistoryItem *_dragStateItem = nullptr;
	CursorState _mouseCursorState = CursorState();
	uint16 _mouseTextSymbol = 0;
	bool _mouseActive = false;
	bool _dragStateUserpic = false;
	bool _pressWasInactive = false;
	bool _recountedAfterPendingResizedItems = false;
	bool _useCornerReaction = false;
	bool _acceptsHorizontalScroll = false;
	bool _horizontalScrollLocked = false;

	QPoint _trippleClickPoint;
	base::Timer _trippleClickTimer;

	Element *_dragSelFrom = nullptr;
	Element *_dragSelTo = nullptr;
	bool _dragSelecting = false;
	bool _wasSelectedText = false; // was some text selected in current drag action

	mutable bool _lastInSelectionMode = false;
	mutable Ui::Animations::Simple _inSelectionModeAnimation;

	// scroll by touch support (at least Windows Surface tablets)
	bool _touchScroll = false;
	bool _touchSelect = false;
	bool _touchInProgress = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	rpl::variable<bool> _touchMaybeSelecting;
	base::Timer _touchSelectTimer;

	Ui::DraggingScrollManager _selectScroll;

	rpl::variable<bool> _sharingDisallowed = false;

	Ui::TouchScrollState _touchScrollState = Ui::TouchScrollState::Manual;
	bool _touchPrevPosValid = false;
	bool _touchWaitingAcceleration = false;
	QPoint _touchSpeed;
	crl::time _touchSpeedTime = 0;
	crl::time _touchAccelerationTime = 0;
	crl::time _touchTime = 0;
	base::Timer _touchScrollTimer;

	HistoryView::ChatPaintGestureHorizontalData _gestureHorizontal;

	// _menu must be destroyed before _whoReactedMenuLifetime.
	rpl::lifetime _whoReactedMenuLifetime;
	base::unique_qptr<Ui::PopupMenu> _menu;

	bool _scrollDateShown = false;
	Ui::Animations::Simple _scrollDateOpacity;
	SingleQueuedInvokation _scrollDateCheck;
	base::Timer _scrollDateHideTimer;
	Element *_scrollDateLastItem = nullptr;
	int _scrollDateLastItemTop = 0;
	ClickHandlerPtr _scrollDateLink;

};

[[nodiscard]] bool CanSendReply(not_null<const HistoryItem*> item);
