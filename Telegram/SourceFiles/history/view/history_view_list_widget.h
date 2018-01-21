/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/tooltip.h"
#include "ui/rp_widget.h"
#include "mtproto/sender.h"
#include "base/timer.h"
#include "data/data_messages.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace HistoryView {

enum class Context : char;

class ListDelegate {
public:
	virtual Context listContext() = 0;
	virtual void listScrollTo(int top) = 0;
	virtual void listCloseRequest() = 0;
	virtual rpl::producer<Data::MessagesSlice> listSource(
		Data::MessagePosition aroundId,
		int limitBefore,
		int limitAfter) = 0;

};

class ListMemento {
public:
	struct ScrollTopState {
		Data::MessagePosition item;
		int shift = 0;
	};

	explicit ListMemento(Data::MessagePosition position)
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
		not_null<Window::Controller*> controller,
		not_null<ListDelegate*> delegate);

	// Set the correct scroll position after being resized.
	void restoreScrollPosition();

	void resizeToWidth(int newWidth, int minHeight) {
		_minHeight = minHeight;
		return TWidget::resizeToWidth(newWidth);
	}

	void saveState(not_null<ListMemento*> memento);
	void restoreState(not_null<ListMemento*> memento);

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

	// ElementDelegate interface.
	Context elementContext() override;
	std::unique_ptr<Element> elementCreate(
		not_null<HistoryMessage*> message) override;
	std::unique_ptr<Element> elementCreate(
		not_null<HistoryService*> message) override;
	void elementAnimationAutoplayAsync(
		not_null<const Element*> view) override;

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
	enum class Direction {
		Up,
		Down,
	};
	enum class MouseAction {
		None,
		PrepareDrag,
		Dragging,
		Selecting,
	};
	enum class EnumItemsDirection {
		TopToBottom,
		BottomToTop,
	};
	using ScrollTopState = ListMemento::ScrollTopState;

	void refreshViewer();
	void updateAroundPositionFromRows();
	void refreshRows();
	ScrollTopState countScrollState() const;
	void saveScrollState();
	void restoreScrollState();

	Element *viewForItem(const HistoryItem *item) const;
	not_null<Element*> enforceViewForItem(not_null<HistoryItem*> item);

	void mouseActionStart(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionUpdate(const QPoint &screenPos);
	void mouseActionFinish(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionCancel();
	void updateSelected();
	void performDrag();
	int itemTop(not_null<const Element*> view) const;
	void repaintItem(const Element *view);
	void resizeItem(not_null<Element*> view);
	void refreshItem(not_null<const Element*> view);
	void itemRemoved(not_null<const HistoryItem*> item);
	QPoint mapPointToItem(QPoint point, const Element *view) const;

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);
	void savePhotoToFile(PhotoData *photo);
	void saveDocumentToFile(DocumentData *document);
	void copyContextImage(PhotoData *photo);
	void showStickerPackInfo(not_null<DocumentData*> document);
	void copyContextUrl();
	void cancelContextDownload(not_null<DocumentData*> document);
	void showContextInFolder(not_null<DocumentData*> document);
	void openContextGif(FullMsgId itemId);
	void copyContextText(FullMsgId itemId);
	void copySelectedText();
	TextWithEntities getSelectedText() const;
	void setToClipboard(
		const TextWithEntities &forClipboard,
		QClipboard::Mode mode = QClipboard::Clipboard);

	not_null<Element*> findItemByY(int y) const;
	Element *strictFindItemByY(int y) const;
	int findNearestItem(Data::MessagePosition position) const;
	void viewReplaced(not_null<const Element*> was, Element *now);

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

	static constexpr auto kMinimalIdsLimit = 24;

	not_null<ListDelegate*> _delegate;
	not_null<Window::Controller*> _controller;
	Data::MessagePosition _aroundPosition;
	Context _context;
	int _aroundIndex = -1;
	int _idsLimit = kMinimalIdsLimit;
	Data::MessagesSlice _slice;
	std::vector<not_null<Element*>> _items;
	std::map<not_null<HistoryItem*>, std::unique_ptr<Element>, std::less<>> _views;
	int _itemsTop = 0;
	int _itemsWidth = 0;
	int _itemsHeight = 0;

	int _minHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	Element *_visibleTopItem = nullptr;
	int _visibleTopFromItem = 0;
	ScrollTopState _scrollTopState;

	bool _scrollDateShown = false;
	Animation _scrollDateOpacity;
	SingleQueuedInvokation _scrollDateCheck;
	base::Timer _scrollDateHideTimer;
	Element *_scrollDateLastItem = nullptr;
	int _scrollDateLastItemTop = 0;

	MouseAction _mouseAction = MouseAction::None;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	QPoint _dragStartPosition;
	QPoint _mousePosition;
	Element *_mouseActionItem = nullptr;
	HistoryCursorState _mouseCursorState = HistoryDefaultCursorState;
	uint16 _mouseTextSymbol = 0;
	bool _pressWasInactive = false;

	Element *_selectedItem = nullptr;
	TextSelection _selectedText;
	bool _wasSelectedText = false; // was some text selected in current drag action
	Qt::CursorShape _cursor = style::cur_default;

	base::unique_qptr<Ui::PopupMenu> _menu;

	QPoint _trippleClickPoint;
	base::Timer _trippleClickTimer;

	ClickHandlerPtr _contextMenuLink;

	rpl::lifetime _viewerLifetime;

};

} // namespace HistoryView
