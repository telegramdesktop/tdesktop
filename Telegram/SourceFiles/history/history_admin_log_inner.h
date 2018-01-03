/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_admin_log_item.h"
#include "history/history_admin_log_section.h"
#include "ui/widgets/tooltip.h"
#include "ui/rp_widget.h"
#include "mtproto/sender.h"
#include "base/timer.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace AdminLog {

class SectionMemento;

class InnerWidget final
	: public Ui::RpWidget
	, public Ui::AbstractTooltipShower
	, private MTP::Sender
	, private base::Subscriber {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<ChannelData*> channel);

	base::Observable<void> showSearchSignal;
	base::Observable<int> scrollToSignal;
	base::Observable<void> cancelledSignal;

	not_null<ChannelData*> channel() const {
		return _channel;
	}

	// Set the correct scroll position after being resized.
	void restoreScrollPosition();

	void resizeToWidth(int newWidth, int minHeight) {
		_minHeight = minHeight;
		return TWidget::resizeToWidth(newWidth);
	}

	void saveState(not_null<SectionMemento*> memento);
	void restoreState(not_null<SectionMemento*> memento);

	// Empty "flags" means all events.
	void applyFilter(FilterValue &&value);
	void applySearch(const QString &query);
	void showFilter(base::lambda<void(FilterValue &&filter)> callback);

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

	~InnerWidget();

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

	// Resizes content and counts natural widget height for the desired width.
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

	void mouseActionStart(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionUpdate(const QPoint &screenPos);
	void mouseActionFinish(const QPoint &screenPos, Qt::MouseButton button);
	void mouseActionCancel();
	void updateSelected();
	void performDrag();
	int itemTop(not_null<const HistoryItem*> item) const;
	void repaintItem(const HistoryItem *item);
	QPoint mapPointToItem(QPoint point, const HistoryItem *item) const;
	void handlePendingHistoryResize();

	void showContextMenu(QContextMenuEvent *e, bool showFromTouch = false);
	void savePhotoToFile(PhotoData *photo);
	void saveDocumentToFile(DocumentData *document);
	void copyContextImage(PhotoData *photo);
	void showStickerPackInfo();
	void copyContextUrl();
	void cancelContextDownload();
	void showContextInFolder();
	void openContextGif();
	void copyContextText();
	void copySelectedText();
	TextWithEntities getSelectedText() const;
	void setToClipboard(const TextWithEntities &forClipboard, QClipboard::Mode mode = QClipboard::Clipboard);
	void suggestRestrictUser(not_null<UserData*> user);
	void restrictUser(not_null<UserData*> user, const MTPChannelBannedRights &oldRights, const MTPChannelBannedRights &newRights);
	void restrictUserDone(not_null<UserData*> user, const MTPChannelBannedRights &rights);

	void requestAdmins();
	void checkPreloadMore();
	void updateVisibleTopItem();
	void preloadMore(Direction direction);
	void itemsAdded(Direction direction, int addedCount);
	void updateSize();
	void updateMinMaxIds();
	void updateEmptyText();
	void paintEmpty(Painter &p);
	void clearAfterFilterChange();
	void clearAndRequestLog();
	void addEvents(Direction direction, const QVector<MTPChannelAdminLogEvent> &events);

	void toggleScrollDateShown();
	void repaintScrollDateCallback();
	bool displayScrollDate() const;
	void scrollDateHide();
	void scrollDateCheck();
	void scrollDateHideByTimer();

	// This function finds all history items that are displayed and calls template method
	// for each found message (in given direction) in the passed history with passed top offset.
	//
	// Method has "bool (*Method)(HistoryItem *item, int itemtop, int itembottom)" signature
	// if it returns false the enumeration stops immidiately.
	template <EnumItemsDirection direction, typename Method>
	void enumerateItems(Method method);

	// This function finds all userpics on the left that are displayed and calls template method
	// for each found userpic (from the top to the bottom) using enumerateItems() method.
	//
	// Method has "bool (*Method)(not_null<HistoryMessage*> message, int userpicTop)" signature
	// if it returns false the enumeration stops immidiately.
	template <typename Method>
	void enumerateUserpics(Method method);

	// This function finds all date elements that are displayed and calls template method
	// for each found date element (from the bottom to the top) using enumerateItems() method.
	//
	// Method has "bool (*Method)(not_null<HistoryItem*> item, int itemtop, int dateTop)" signature
	// if it returns false the enumeration stops immidiately.
	template <typename Method>
	void enumerateDates(Method method);

	not_null<Window::Controller*> _controller;
	not_null<ChannelData*> _channel;
	not_null<History*> _history;
	std::vector<HistoryItemOwned> _items;
	std::map<uint64, HistoryItem*> _itemsByIds;
	int _itemsTop = 0;
	int _itemsHeight = 0;

	LocalIdManager _idManager;
	int _minHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	HistoryItem *_visibleTopItem = nullptr;
	int _visibleTopFromItem = 0;

	bool _scrollDateShown = false;
	Animation _scrollDateOpacity;
	SingleQueuedInvokation _scrollDateCheck;
	base::Timer _scrollDateHideTimer;
	HistoryItem *_scrollDateLastItem = nullptr;
	int _scrollDateLastItemTop = 0;

	// Up - max, Down - min.
	uint64 _maxId = 0;
	uint64 _minId = 0;
	mtpRequestId _preloadUpRequestId = 0;
	mtpRequestId _preloadDownRequestId = 0;

	// Don't load anything until the memento was read.
	bool _upLoaded = true;
	bool _downLoaded = true;
	bool _filterChanged = false;
	Text _emptyText;

	MouseAction _mouseAction = MouseAction::None;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	QPoint _dragStartPosition;
	QPoint _mousePosition;
	HistoryItem *_mouseActionItem = nullptr;
	HistoryCursorState _mouseCursorState = HistoryDefaultCursorState;
	uint16 _mouseTextSymbol = 0;
	bool _pressWasInactive = false;

	HistoryItem *_selectedItem = nullptr;
	TextSelection _selectedText;
	bool _wasSelectedText = false; // was some text selected in current drag action
	Qt::CursorShape _cursor = style::cur_default;

	// context menu
	Ui::PopupMenu *_menu = nullptr;

	QPoint _trippleClickPoint;
	base::Timer _trippleClickTimer;

	ClickHandlerPtr _contextMenuLink;

	FilterValue _filter;
	QString _searchQuery;
	std::vector<not_null<UserData*>> _admins;
	std::vector<not_null<UserData*>> _adminsCanEdit;
	base::lambda<void(FilterValue &&filter)> _showFilterCallback;

};

} // namespace AdminLog
