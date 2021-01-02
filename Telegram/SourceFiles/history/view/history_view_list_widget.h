/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "ui/widgets/tooltip.h"
#include "mtproto/sender.h"
#include "base/timer.h"
#include "data/data_messages.h"
#include "history/view/history_view_element.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
struct Group;
class CloudImageView;
} // namespace Data

namespace HistoryView {

struct TextState;
struct StateRequest;
enum class CursorState : char;
enum class PointState : char;
enum class Context : char;

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
	virtual void listScrollTo(int top) = 0;
	virtual void listCancelRequest() = 0;
	virtual void listDeleteRequest() = 0;
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
	virtual void listVisibleItemsChanged(HistoryItemsList &&items) = 0;
	virtual MessagesBarData listMessagesBar(
		const std::vector<not_null<Element*>> &elements) = 0;
	virtual void listContentRefreshed() = 0;
	virtual ClickHandlerPtr listDateLink(not_null<Element*> view) = 0;
	virtual bool listElementHideReply(not_null<const Element*> view) = 0;
	virtual bool listElementShownUnread(not_null<const Element*> view) = 0;
	virtual bool listIsGoodForAroundPosition(
		not_null<const Element*> view) = 0;
	virtual void listSendBotCommand(
		const QString &command,
		const FullMsgId &context) = 0;
	virtual void listHandleViaClick(not_null<UserData*> bot) = 0;

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
	, public Ui::AbstractTooltipShower
	, private base::Subscriber {
public:
	ListWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<ListDelegate*> delegate);

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
	[[nodiscard]] bool animatedScrolling() const;
	bool isAbovePosition(Data::MessagePosition position) const;
	bool isBelowPosition(Data::MessagePosition position) const;
	void highlightMessage(FullMsgId itemId);
	void showAroundPosition(
		Data::MessagePosition position,
		Fn<bool()> overrideInitialScroll);

	[[nodiscard]] TextForMimeData getSelectedText() const;
	[[nodiscard]] MessageIdsList getSelectedIds() const;
	[[nodiscard]] SelectedItems getSelectedItems() const;
	void cancelSelection();
	void selectItem(not_null<HistoryItem*> item);
	void selectItemAsGroup(not_null<HistoryItem*> item);

	bool loadedAtTopKnown() const;
	bool loadedAtTop() const;
	bool loadedAtBottomKnown() const;
	bool loadedAtBottom() const;
	bool isEmpty() const;

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	[[nodiscard]] rpl::producer<FullMsgId> editMessageRequested() const;
	void editMessageRequestNotify(FullMsgId item);
	[[nodiscard]] rpl::producer<FullMsgId> replyToMessageRequested() const;
	void replyToMessageRequestNotify(FullMsgId item);
	[[nodiscard]] rpl::producer<FullMsgId> readMessageRequested() const;

	// ElementDelegate interface.
	Context elementContext() override;
	std::unique_ptr<Element> elementCreate(
		not_null<HistoryMessage*> message,
		Element *replacing = nullptr) override;
	std::unique_ptr<Element> elementCreate(
		not_null<HistoryService*> message,
		Element *replacing = nullptr) override;
	bool elementUnderCursor(not_null<const Element*> view) override;
	crl::time elementHighlightTime(
		not_null<const HistoryItem*> item) override;
	bool elementInSelectionMode() override;
	bool elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) override;
	void elementStartStickerLoop(not_null<const Element*> view) override;
	void elementShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) override;
	void elementShowTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) override;
	bool elementIsGifPaused() override;
	bool elementHideReply(not_null<const Element*> view) override;
	bool elementShownUnread(not_null<const Element*> view) override;
	void elementSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void elementHandleViaClick(not_null<UserData*> bot) override;

	~ListWidget();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	// Resize content and count natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
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
	using ScrollTopState = ListMemento::ScrollTopState;
	using PointState = HistoryView::PointState;
	using CursorState = HistoryView::CursorState;

	void refreshViewer();
	void updateAroundPositionFromNearest(int nearestIndex);
	void refreshRows();
	ScrollTopState countScrollState() const;
	void saveScrollState();
	void restoreScrollState();

	Element *viewForItem(FullMsgId itemId) const;
	Element *viewForItem(const HistoryItem *item) const;
	not_null<Element*> enforceViewForItem(not_null<HistoryItem*> item);

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
	void repaintHighlightedItem(not_null<const Element*> view);
	void resizeItem(not_null<Element*> view);
	void refreshItem(not_null<const Element*> view);
	void itemRemoved(not_null<const HistoryItem*> item);
	QPoint mapPointToItem(QPoint point, const Element *view) const;

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);

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

	void updateHighlightedMessage();

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

	ClickHandlerPtr hiddenUserpicLink(FullMsgId id);

	static constexpr auto kMinimalIdsLimit = 24;

	const not_null<ListDelegate*> _delegate;
	const not_null<Window::SessionController*> _controller;
	Data::MessagePosition _aroundPosition;
	Data::MessagePosition _shownAtPosition;
	Context _context;
	int _aroundIndex = -1;
	int _idsLimit = kMinimalIdsLimit;
	Data::MessagesSlice _slice;
	std::vector<not_null<Element*>> _items;
	std::map<
		not_null<HistoryItem*>,
		std::unique_ptr<Element>,
		std::less<>> _views;
	int _itemsTop = 0;
	int _itemsWidth = 0;
	int _itemsHeight = 0;
	int _itemAverageHeight = 0;
	base::flat_set<FullMsgId> _animatedStickersPlayed;
	base::flat_map<
		not_null<PeerData*>,
		std::shared_ptr<Data::CloudImageView>> _userpics, _userpicsCache;

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

	base::unique_qptr<Ui::PopupMenu> _menu;

	QPoint _trippleClickPoint;
	crl::time _trippleClickStartTime = 0;

	crl::time _highlightStart = 0;
	FullMsgId _highlightedMessageId;
	base::Timer _highlightTimer;

	rpl::event_stream<FullMsgId> _requestedToEditMessage;
	rpl::event_stream<FullMsgId> _requestedToReplyToMessage;
	rpl::event_stream<FullMsgId> _requestedToReadMessage;

	rpl::lifetime _viewerLifetime;

};

void ConfirmDeleteSelectedItems(not_null<ListWidget*> widget);
void ConfirmForwardSelectedItems(not_null<ListWidget*> widget);
void ConfirmSendNowSelectedItems(not_null<ListWidget*> widget);

[[nodiscard]] QString WrapBotCommandInChat(
	not_null<PeerData*> peer,
	const QString &command,
	const FullMsgId &context);
[[nodiscard]] QString WrapBotCommandInChat(
	not_null<PeerData*> peer,
	const QString &command,
	not_null<UserData*> bot);

} // namespace HistoryView
