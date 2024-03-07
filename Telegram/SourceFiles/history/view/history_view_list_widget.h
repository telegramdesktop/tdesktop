/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/dragging_scroll_manager.h"
#include "ui/widgets/tooltip.h"
#include "mtproto/sender.h"
#include "data/data_messages.h"
#include "history/view/history_view_element.h"
#include "history/history_view_highlight_manager.h"
#include "history/history_view_top_toast.h"

struct ClickHandlerContext;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
class ChatTheme;
struct ChatPaintContext;
enum class TouchScrollState;
struct PeerUserpicView;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
struct Group;
struct Reaction;
struct AllowedReactions;
} // namespace Data

namespace HistoryView::Reactions {
class Manager;
struct ChosenReaction;
struct ButtonParameters;
} // namespace HistoryView::Reactions

namespace Window {
struct SectionShow;
} // namespace Window

namespace HistoryView {

struct TextState;
struct StateRequest;
class EmojiInteractions;
class TranslateTracker;
enum class CursorState : char;
enum class PointState : char;
enum class Context : char;

enum class CopyRestrictionType : char {
	None,
	Group,
	Channel,
};

struct SelectedItem {
	explicit SelectedItem(FullMsgId msgId) : msgId(msgId) {
	}

	FullMsgId msgId;
	bool canDelete = false;
	bool canForward = false;
	bool canSendNow = false;
};

struct MessagesBar {
	Element *element = nullptr;
	bool hidden = false;
	bool focus = false;
};

struct MessagesBarData {
	MessagesBar bar;
	rpl::producer<QString> text;
};

using SelectedItems = std::vector<SelectedItem>;

class ListDelegate {
public:
	virtual Context listContext() = 0;
	virtual bool listScrollTo(int top, bool syntetic = true) = 0;
	virtual void listCancelRequest() = 0;
	virtual void listDeleteRequest() = 0;
	virtual void listTryProcessKeyInput(not_null<QKeyEvent*> e) = 0;
	virtual rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) = 0;
	virtual bool listAllowsMultiSelect() = 0;
	virtual bool listIsItemGoodForSelection(not_null<HistoryItem*> item) = 0;
	virtual bool listIsLessInOrder(
		not_null<HistoryItem*> first,
		not_null<HistoryItem*> second) = 0;
	virtual void listSelectionChanged(SelectedItems &&items) = 0;
	virtual void listMarkReadTill(not_null<HistoryItem*> item) = 0;
	virtual void listMarkContentsRead(
		const base::flat_set<not_null<HistoryItem*>> &items) = 0;
	virtual MessagesBarData listMessagesBar(
		const std::vector<not_null<Element*>> &elements) = 0;
	virtual void listContentRefreshed() = 0;
	virtual void listUpdateDateLink(
		ClickHandlerPtr &link,
		not_null<Element*> view) = 0;
	virtual bool listElementHideReply(not_null<const Element*> view) = 0;
	virtual bool listElementShownUnread(not_null<const Element*> view) = 0;
	virtual bool listIsGoodForAroundPosition(
		not_null<const Element*> view) = 0;
	virtual void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) = 0;
	virtual void listSearch(
		const QString &query,
		const FullMsgId &context) = 0;
	virtual void listHandleViaClick(not_null<UserData*> bot) = 0;
	virtual not_null<Ui::ChatTheme*> listChatTheme() = 0;
	virtual CopyRestrictionType listCopyRestrictionType(
		HistoryItem *item) = 0;
	CopyRestrictionType listCopyRestrictionType() {
		return listCopyRestrictionType(nullptr);
	}
	virtual CopyRestrictionType listCopyMediaRestrictionType(
		not_null<HistoryItem*> item) = 0;
	virtual CopyRestrictionType listSelectRestrictionType() = 0;
	virtual auto listAllowedReactionsValue()
		-> rpl::producer<Data::AllowedReactions> = 0;
	virtual void listShowPremiumToast(not_null<DocumentData*> document) = 0;
	virtual void listOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) = 0;
	virtual void listOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView) = 0;
	virtual void listPaintEmpty(
		Painter &p,
		const Ui::ChatPaintContext &context) = 0;
	virtual QString listElementAuthorRank(not_null<const Element*> view) = 0;
	virtual History *listTranslateHistory() = 0;
	virtual void listAddTranslatedItems(
		not_null<TranslateTracker*> tracker) = 0;
};

struct SelectionData {
	bool canDelete = false;
	bool canForward = false;
	bool canSendNow = false;
};

using SelectedMap = base::flat_map<
	FullMsgId,
	SelectionData,
	std::less<>>;

class ListMemento {
public:
	struct ScrollTopState {
		Data::MessagePosition item;
		int shift = 0;
	};

	explicit ListMemento(
		Data::MessagePosition position = Data::UnreadMessagePosition)
	: _aroundPosition(position) {
	}
	void setAroundPosition(Data::MessagePosition position) {
		_aroundPosition = position;
	}
	Data::MessagePosition aroundPosition() const {
		return _aroundPosition;
	}
	void setIdsLimit(int limit) {
		_idsLimit = limit;
	}
	int idsLimit() const {
		return _idsLimit;
	}
	void setScrollTopState(ScrollTopState state) {
		_scrollTopState = state;
	}
	ScrollTopState scrollTopState() const {
		return _scrollTopState;
	}

private:
	Data::MessagePosition _aroundPosition;
	ScrollTopState _scrollTopState;
	int _idsLimit = 0;

};

class ListWidget final
	: public Ui::RpWidget
	, public ElementDelegate
	, public Ui::AbstractTooltipShower {
public:
	ListWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<ListDelegate*> delegate);

	static const crl::time kItemRevealDuration;

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<Window::SessionController*> controller() const;
	[[nodiscard]] not_null<ListDelegate*> delegate() const;

	// Set the correct scroll position after being resized.
	void restoreScrollPosition();

	void resizeToWidth(int newWidth, int minHeight);

	void saveState(not_null<ListMemento*> memento);
	void restoreState(not_null<ListMemento*> memento);
	std::optional<int> scrollTopForPosition(
		Data::MessagePosition position) const;
	Element *viewByPosition(Data::MessagePosition position) const;
	std::optional<int> scrollTopForView(not_null<Element*> view) const;
	[[nodiscard]] bool animatedScrolling() const;
	bool isAbovePosition(Data::MessagePosition position) const;
	bool isBelowPosition(Data::MessagePosition position) const;
	void highlightMessage(
		FullMsgId itemId,
		const TextWithEntities &part,
		int partOffsetHint);

	void showAtPosition(
		Data::MessagePosition position,
		const Window::SectionShow &params,
		Fn<void(bool found)> done = nullptr);
	void refreshViewer();

	[[nodiscard]] TextForMimeData getSelectedText() const;
	[[nodiscard]] MessageIdsList getSelectedIds() const;
	[[nodiscard]] SelectedItems getSelectedItems() const;
	void cancelSelection();
	void selectItem(not_null<HistoryItem*> item);
	void selectItemAsGroup(not_null<HistoryItem*> item);

	void touchScrollUpdated(const QPoint &screenPos);

	[[nodiscard]] bool loadedAtTopKnown() const;
	[[nodiscard]] bool loadedAtTop() const;
	[[nodiscard]] bool loadedAtBottomKnown() const;
	[[nodiscard]] bool loadedAtBottom() const;
	[[nodiscard]] bool isEmpty() const;

	[[nodiscard]] bool markingContentsRead() const;
	[[nodiscard]] bool markingMessagesRead() const;
	void showFinished();
	void checkActivation();

	[[nodiscard]] bool hasCopyRestriction(HistoryItem *item = nullptr) const;
	[[nodiscard]] bool hasCopyMediaRestriction(
		not_null<HistoryItem*> item) const;
	[[nodiscard]] bool showCopyRestriction(HistoryItem *item = nullptr);
	[[nodiscard]] bool showCopyMediaRestriction(not_null<HistoryItem*> item);
	[[nodiscard]] bool hasCopyRestrictionForSelected() const;
	[[nodiscard]] bool showCopyRestrictionForSelected();
	[[nodiscard]] bool hasSelectRestriction() const;

	[[nodiscard]] std::pair<Element*, int> findViewForPinnedTracking(
		int top) const;
	[[nodiscard]] ClickHandlerContext prepareClickHandlerContext(
		FullMsgId id);

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	[[nodiscard]] rpl::producer<FullMsgId> editMessageRequested() const;
	void editMessageRequestNotify(FullMsgId item) const;
	[[nodiscard]] bool lastMessageEditRequestNotify() const;
	[[nodiscard]] rpl::producer<FullReplyTo> replyToMessageRequested() const;
	void replyToMessageRequestNotify(FullReplyTo id);
	[[nodiscard]] rpl::producer<FullMsgId> readMessageRequested() const;
	[[nodiscard]] rpl::producer<FullMsgId> showMessageRequested() const;
	void replyNextMessage(FullMsgId fullId, bool next = true);

	[[nodiscard]] Reactions::ButtonParameters reactionButtonParameters(
		not_null<const Element*> view,
		QPoint position,
		const TextState &reactionState) const;
	void toggleFavoriteReaction(not_null<Element*> view) const;

	// ElementDelegate interface.
	Context elementContext() override;
	bool elementUnderCursor(not_null<const Element*> view) override;
	bool elementInSelectionMode() override;
	bool elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) override;
	void elementStartStickerLoop(not_null<const Element*> view) override;
	void elementShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) override;
	void elementOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) override;
	void elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView = false) override;
	void elementCancelUpload(const FullMsgId &context) override;
	void elementShowTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) override;
	bool elementAnimationsPaused() override;
	bool elementHideReply(not_null<const Element*> view) override;
	bool elementShownUnread(not_null<const Element*> view) override;
	void elementSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void elementSearchInList(
		const QString &query,
		const FullMsgId &context) override;
	void elementHandleViaClick(not_null<UserData*> bot) override;
	bool elementIsChatWide() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	void elementReplyTo(const FullReplyTo &to) override;
	void elementStartInteraction(not_null<const Element*> view) override;
	void elementStartPremium(
		not_null<const Element*> view,
		Element *replacing) override;
	void elementCancelPremium(not_null<const Element*> view) override;
	QString elementAuthorRank(not_null<const Element*> view) override;

	void setEmptyInfoWidget(base::unique_qptr<Ui::RpWidget> &&w);
	void overrideIsChatWide(bool isWide);

	~ListWidget();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	bool eventHook(QEvent *e) override; // calls touchEvent when necessary
	void touchEvent(QTouchEvent *e);
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	// Resize content and count natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
	using ScrollTopState = ListMemento::ScrollTopState;
	using PointState = HistoryView::PointState;
	using CursorState = HistoryView::CursorState;
	using ChosenReaction = HistoryView::Reactions::ChosenReaction;
	using ViewsMap = base::flat_map<
		not_null<HistoryItem*>,
		std::unique_ptr<Element>>;

	struct MouseState {
		MouseState();
		MouseState(
			FullMsgId itemId,
			int height,
			QPoint point,
			PointState pointState);

		FullMsgId itemId;
		int height = 0;
		QPoint point;
		PointState pointState;

		inline bool operator==(const MouseState &other) const {
			return (itemId == other.itemId)
				&& (point == other.point);
		}
		inline bool operator!=(const MouseState &other) const {
			return !(*this == other);
		}
	};
	struct ItemRevealAnimation {
		Ui::Animations::Simple animation;
		int startHeight = 0;
	};
	enum class Direction {
		Up,
		Down,
	};
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
	enum class DragSelectAction {
		None,
		Selecting,
		Deselecting,
	};

	void onTouchSelect();
	void onTouchScrollTimer();

	void updateAroundPositionFromNearest(int nearestIndex);
	void refreshRows(const Data::MessagesSlice &old);
	ScrollTopState countScrollState() const;
	void saveScrollState();
	void restoreScrollState();

	[[nodiscard]] bool jumpToBottomInsteadOfUnread() const;
	void showAroundPosition(
		Data::MessagePosition position,
		Fn<bool()> overrideInitialScroll);
	bool showAtPositionNow(
		Data::MessagePosition position,
		const Window::SectionShow &params,
		Fn<void(bool found)> done);

	Ui::ChatPaintContext preparePaintContext(const QRect &clip) const;

	Element *viewForItem(FullMsgId itemId) const;
	Element *viewForItem(const HistoryItem *item) const;
	not_null<Element*> enforceViewForItem(
		not_null<HistoryItem*> item,
		ViewsMap &old);

	void mouseActionStart(
		const QPoint &globalPosition,
		Qt::MouseButton button);
	void mouseActionUpdate(const QPoint &globalPosition);
	void mouseActionUpdate();
	void mouseActionFinish(
		const QPoint &globalPosition,
		Qt::MouseButton button);
	void mouseActionCancel();
	std::unique_ptr<QMimeData> prepareDrag();
	void performDrag();
	style::cursor computeMouseCursor() const;
	int itemTop(not_null<const Element*> view) const;
	void repaintItem(FullMsgId itemId);
	void repaintItem(const Element *view);
	void resizeItem(not_null<Element*> view);
	void refreshItem(not_null<const Element*> view);
	void itemRemoved(not_null<const HistoryItem*> item);
	QPoint mapPointToItem(QPoint point, const Element *view) const;

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);
	void reactionChosen(ChosenReaction reaction);

	void touchResetSpeed();
	void touchUpdateSpeed();
	void touchDeaccelerate(int32 elapsed);

	[[nodiscard]] int findItemIndexByY(int y) const;
	[[nodiscard]] not_null<Element*> findItemByY(int y) const;
	[[nodiscard]] Element *strictFindItemByY(int y) const;
	[[nodiscard]] int findNearestItem(Data::MessagePosition position) const;
	void viewReplaced(not_null<const Element*> was, Element *now);
	[[nodiscard]] HistoryItemsList collectVisibleItems() const;

	void checkMoveToOtherViewer();
	void updateVisibleTopItem();
	void updateItemsGeometry();
	void updateSize();
	void refreshAttachmentsFromTill(int from, int till);
	void refreshAttachmentsAtIndex(int index);

	void toggleScrollDateShown();
	void repaintScrollDateCallback();
	bool displayScrollDate() const;
	void scrollDateHide();
	void scrollDateCheck();
	void scrollDateHideByTimer();
	void keepScrollDateForNow();

	void computeScrollTo(
		int to,
		Data::MessagePosition position,
		anim::type animated);
	enum class AnimatedScroll {
		Full,
		Part,
		None,
	};
	void scrollTo(
		int scrollTop,
		Data::MessagePosition attachPosition,
		int delta,
		AnimatedScroll type);

	void trySwitchToWordSelection();
	void switchToWordSelection();
	void validateTrippleClickStartTime();
	SelectedItems collectSelectedItems() const;
	MessageIdsList collectSelectedIds() const;
	void pushSelectedItems();
	void removeItemSelection(
		const SelectedMap::const_iterator &i);
	bool hasSelectedText() const;
	bool hasSelectedItems() const;
	bool inSelectionMode() const;
	bool overSelectedItems() const;
	void clearTextSelection();
	void clearSelected();
	void setTextSelection(
		not_null<Element*> view,
		TextSelection selection);
	int itemMinimalHeight() const;

	bool isGoodForSelection(
		SelectedMap &applyTo,
		not_null<HistoryItem*> item,
		int &totalCount) const;
	bool addToSelection(
		SelectedMap &applyTo,
		not_null<HistoryItem*> item) const;
	bool removeFromSelection(
		SelectedMap &applyTo,
		FullMsgId itemId) const;
	void changeSelection(
		SelectedMap &applyTo,
		not_null<HistoryItem*> item,
		SelectAction action) const;
	bool isSelectedGroup(
		const SelectedMap &applyTo,
		not_null<const Data::Group*> group) const;
	bool isSelectedAsGroup(
		const SelectedMap &applyTo,
		not_null<HistoryItem*> item) const;
	void changeSelectionAsGroup(
		SelectedMap &applyTo,
		not_null<HistoryItem*> item,
		SelectAction action) const;

	SelectedMap::iterator itemUnderPressSelection();
	SelectedMap::const_iterator itemUnderPressSelection() const;
	bool isItemUnderPressSelected() const;
	bool isInsideSelection(
		not_null<const Element*> view,
		not_null<HistoryItem*> exactItem,
		const MouseState &state) const;
	bool requiredToStartDragging(not_null<Element*> view) const;
	bool isPressInSelectedText(TextState state) const;
	void updateDragSelection();
	void updateDragSelection(
		const Element *fromView,
		const MouseState &fromState,
		const Element *tillView,
		const MouseState &tillState);
	void updateDragSelection(
		std::vector<not_null<Element*>>::const_iterator from,
		std::vector<not_null<Element*>>::const_iterator till);
	void ensureDragSelectAction(
		std::vector<not_null<Element*>>::const_iterator from,
		std::vector<not_null<Element*>>::const_iterator till);
	void clearDragSelection();
	void applyDragSelection();
	void applyDragSelection(SelectedMap &applyTo) const;
	TextSelection itemRenderSelection(
		not_null<const Element*> view) const;
	TextSelection computeRenderSelection(
		not_null<const SelectedMap*> selected,
		not_null<const Element*> view) const;
	void checkUnreadBarCreation();
	void applyUpdatedScrollState();
	void scrollToAnimationCallback(FullMsgId attachToId, int relativeTo);
	void startItemRevealAnimations();
	void revealItemsCallback();
	void maybeMarkReactionsRead(not_null<HistoryItem*> item);

	void startMessageSendingAnimation(not_null<HistoryItem*> item);
	void showPremiumStickerTooltip(
		not_null<const HistoryView::Element*> view);

	void paintUserpics(
		Painter &p,
		const Ui::ChatPaintContext &context,
		QRect clip);
	void paintDates(
		Painter &p,
		const Ui::ChatPaintContext &context,
		QRect clip);

	// This function finds all history items that are displayed and calls template method
	// for each found message (in given direction) in the passed history with passed top offset.
	//
	// Method has "bool (*Method)(not_null<Element*> view, int itemtop, int itembottom)" signature
	// if it returns false the enumeration stops immediately.
	template <EnumItemsDirection direction, typename Method>
	void enumerateItems(Method method);

	// This function finds all userpics on the left that are displayed and calls template method
	// for each found userpic (from the top to the bottom) using enumerateItems() method.
	//
	// Method has "bool (*Method)(not_null<Element*> view, int userpicTop)" signature
	// if it returns false the enumeration stops immediately.
	template <typename Method>
	void enumerateUserpics(Method method);

	// This function finds all date elements that are displayed and calls template method
	// for each found date element (from the bottom to the top) using enumerateItems() method.
	//
	// Method has "bool (*Method)(not_null<HistoryItem*> item, int itemtop, int dateTop)" signature
	// if it returns false the enumeration stops immediately.
	template <typename Method>
	void enumerateDates(Method method);

	void setGeometryCrashAnnotations(not_null<Element*> view);

	static constexpr auto kMinimalIdsLimit = 24;

	const not_null<ListDelegate*> _delegate;
	const not_null<Window::SessionController*> _controller;
	const std::unique_ptr<EmojiInteractions> _emojiInteractions;
	const Context _context;

	Data::MessagePosition _aroundPosition;
	Data::MessagePosition _shownAtPosition;
	Data::MessagePosition _initialAroundPosition;
	int _aroundIndex = -1;
	int _idsLimit = kMinimalIdsLimit;
	Data::MessagesSlice _slice;
	std::vector<not_null<Element*>> _items;
	ViewsMap _views, _viewsCapacity;
	int _itemsTop = 0;
	int _itemsWidth = 0;
	int _itemsHeight = 0;
	int _itemAverageHeight = 0;
	base::flat_set<not_null<Element*>> _itemRevealPending;
	base::flat_map<
		not_null<Element*>,
		ItemRevealAnimation> _itemRevealAnimations;
	int _itemsRevealHeight = 0;
	base::flat_set<FullMsgId> _animatedStickersPlayed;
	base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> _userpics;
	base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> _userpicsCache;
	base::flat_map<MsgId, Ui::PeerUserpicView> _hiddenSenderUserpics;

	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	QPainterPath _highlightPathCache;

	base::unique_qptr<Ui::RpWidget> _emptyInfo = nullptr;

	std::unique_ptr<HistoryView::Reactions::Manager> _reactionsManager;
	rpl::variable<HistoryItem*> _reactionsItem;
	bool _useCornerReaction = false;

	std::unique_ptr<TranslateTracker> _translateTracker;

	int _minHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	Element *_visibleTopItem = nullptr;
	int _visibleTopFromItem = 0;
	ScrollTopState _scrollTopState;
	Ui::Animations::Simple _scrollToAnimation;
	Fn<bool()> _overrideInitialScroll;

	bool _scrollInited = false;
	bool _scrollDateShown = false;
	Ui::Animations::Simple _scrollDateOpacity;
	SingleQueuedInvokation _scrollDateCheck;
	base::Timer _scrollDateHideTimer;
	Element *_scrollDateLastItem = nullptr;
	int _scrollDateLastItemTop = 0;
	ClickHandlerPtr _scrollDateLink;
	SingleQueuedInvokation _applyUpdatedScrollState;

	MessagesBar _bar;
	rpl::variable<QString> _barText;

	MouseAction _mouseAction = MouseAction::None;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	QPoint _mousePosition;
	MouseState _overState;
	MouseState _pressState;
	Element *_overElement = nullptr;
	HistoryItem *_overItemExact = nullptr;
	HistoryItem *_pressItemExact = nullptr;
	CursorState _mouseCursorState = CursorState();
	uint16 _mouseTextSymbol = 0;
	bool _pressWasInactive = false;

	bool _selectEnabled = false;
	HistoryItem *_selectedTextItem = nullptr;
	TextSelection _selectedTextRange;
	TextForMimeData _selectedText;
	SelectedMap _selected;
	base::flat_set<FullMsgId> _dragSelected;
	DragSelectAction _dragSelectAction = DragSelectAction::None;
	bool _dragSelectDirectionUp = false;
	// Was some text selected in current drag action.
	bool _wasSelectedText = false;
	Qt::CursorShape _cursor = style::cur_default;

	bool _isChatWide = false;
	bool _refreshingViewer = false;
	bool _showFinished = false;
	bool _resizePending = false;
	std::optional<bool> _overrideIsChatWide;

	// _menu must be destroyed before _whoReactedMenuLifetime.
	rpl::lifetime _whoReactedMenuLifetime;
	base::unique_qptr<Ui::PopupMenu> _menu;

	QPoint _trippleClickPoint;
	crl::time _trippleClickStartTime = 0;

	ElementHighlighter _highlighter;

	// scroll by touch support (at least Windows Surface tablets)
	bool _touchScroll = false;
	bool _touchSelect = false;
	bool _touchInProgress = false;
	QPoint _touchStart, _touchPrevPos, _touchPos;
	base::Timer _touchSelectTimer;

	Ui::DraggingScrollManager _selectScroll;

	InfoTooltip _topToast;

	Ui::TouchScrollState _touchScrollState = Ui::TouchScrollState();
	bool _touchPrevPosValid = false;
	bool _touchWaitingAcceleration = false;
	QPoint _touchSpeed;
	crl::time _touchSpeedTime = 0;
	crl::time _touchAccelerationTime = 0;
	crl::time _touchTime = 0;
	base::Timer _touchScrollTimer;

	rpl::event_stream<FullMsgId> _requestedToEditMessage;
	rpl::event_stream<FullReplyTo> _requestedToReplyToMessage;
	rpl::event_stream<FullMsgId> _requestedToReadMessage;
	rpl::event_stream<FullMsgId> _requestedToShowMessage;

	rpl::lifetime _viewerLifetime;

};

void ConfirmDeleteSelectedItems(not_null<ListWidget*> widget);
void ConfirmForwardSelectedItems(not_null<ListWidget*> widget);
void ConfirmSendNowSelectedItems(not_null<ListWidget*> widget);

[[nodiscard]] CopyRestrictionType CopyRestrictionTypeFor(
	not_null<PeerData*> peer,
	HistoryItem *item = nullptr);
[[nodiscard]] CopyRestrictionType CopyMediaRestrictionTypeFor(
	not_null<PeerData*> peer,
	not_null<HistoryItem*> item);
[[nodiscard]] CopyRestrictionType SelectRestrictionTypeFor(
	not_null<PeerData*> peer);

} // namespace HistoryView
