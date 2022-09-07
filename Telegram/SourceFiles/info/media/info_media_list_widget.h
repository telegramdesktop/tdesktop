/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/widgets/tooltip.h"
#include "info/media/info_media_widget.h"
#include "info/media/info_media_common.h"
#include "overview/overview_layout_delegate.h"

class DeleteMessagesBox;

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {
struct TextState;
struct StateRequest;
enum class CursorState : char;
enum class PointState : char;
} // namespace HistoryView

namespace Ui {
class PopupMenu;
class BoxContent;
} // namespace Ui

namespace Overview {
namespace Layout {
class ItemBase;
} // namespace Layout
} // namespace Overview

namespace Window {
class SessionController;
} // namespace Window

namespace Info {

class AbstractController;

namespace Media {

struct ListFoundItem;
struct ListContext;
class ListSection;
class ListProvider;

class ListWidget final
	: public Ui::RpWidget
	, public Overview::Layout::Delegate
	, public Ui::AbstractTooltipShower {
public:
	ListWidget(
		QWidget *parent,
		not_null<AbstractController*> controller);
	~ListWidget();

	Main::Session &session() const;

	void restart();

	rpl::producer<int> scrollToRequests() const;
	rpl::producer<SelectedItems> selectedListValue() const;
	void selectionAction(SelectionAction action);

	QRect getCurrentSongGeometry();
	rpl::producer<> checkForHide() const {
		return _checkForHide.events();
	}
	bool preventAutoHide() const;

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	// Overview::Layout::Delegate
	void registerHeavyItem(not_null<const BaseLayout*> item) override;
	void unregisterHeavyItem(not_null<const BaseLayout*> item) override;
	void repaintItem(not_null<const BaseLayout*> item) override;
	bool itemVisible(not_null<const BaseLayout*> item) override;

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;
	bool tooltipWindowActive() const override;

	void openPhoto(not_null<PhotoData*> photo, FullMsgId id) override;
	void openDocument(
		not_null<DocumentData*> document,
		FullMsgId id,
		bool showInMediaView = false) override;

private:
	struct DateBadge;
	using Section = ListSection;
	using FoundItem = ListFoundItem;
	using CursorState = HistoryView::CursorState;
	using TextState = HistoryView::TextState;
	using StateRequest = HistoryView::StateRequest;
	using SelectionData = ListItemSelectionData;
	using SelectedMap = ListSelectedMap;
	using DragSelectAction = ListDragSelectAction;
	enum class MouseAction {
		None,
		PrepareDrag,
		Dragging,
		PrepareSelect,
		Selecting,
	};
	struct MouseState {
		HistoryItem *item = nullptr;
		QSize size;
		QPoint cursor;
		bool inside = false;

		inline bool operator==(const MouseState &other) const {
			return (item == other.item)
				&& (cursor == other.cursor);
		}
		inline bool operator!=(const MouseState &other) const {
			return !(*this == other);
		}

	};
	enum class ContextMenuSource {
		Mouse,
		Touch,
		Other,
	};

	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void start();
	int recountHeight();
	void refreshHeight();
	void subscribeToSession(
		not_null<Main::Session*> session,
		rpl::lifetime &lifetime);

	void setupSelectRestriction();

	QMargins padding() const;
	bool isItemLayout(
		not_null<const HistoryItem*> item,
		BaseLayout *layout) const;
	void repaintItem(const HistoryItem *item);
	void repaintItem(const BaseLayout *item);
	void repaintItem(QRect itemGeometry);
	void itemRemoved(not_null<const HistoryItem*> item);
	void itemLayoutChanged(not_null<const HistoryItem*> item);

	void refreshViewer();
	void refreshRows();
	void trackSession(not_null<Main::Session*> session);

	[[nodiscard]] SelectedItems collectSelectedItems() const;
	[[nodiscard]] MessageIdsList collectSelectedIds() const;
	[[nodiscard]] MessageIdsList collectSelectedIds(
		const SelectedItems &items) const;
	void pushSelectedItems();
	[[nodiscard]] bool hasSelected() const;
	[[nodiscard]] bool isSelectedItem(
		const SelectedMap::const_iterator &i) const;
	void removeItemSelection(
		const SelectedMap::const_iterator &i);
	[[nodiscard]] bool hasSelectedText() const;
	[[nodiscard]] bool hasSelectedItems() const;
	void clearSelected();
	void forwardSelected();
	void forwardItem(GlobalMsgId globalId);
	void forwardItems(MessageIdsList &&items);
	void deleteSelected();
	void deleteItem(GlobalMsgId globalId);
	void deleteItems(SelectedItems &&items, Fn<void()> confirmed = nullptr);
	void applyItemSelection(
		HistoryItem *item,
		TextSelection selection);
	void toggleItemSelection(not_null<HistoryItem*> item);
	[[nodiscard]] SelectedMap::iterator itemUnderPressSelection();
	[[nodiscard]] auto itemUnderPressSelection() const
		-> SelectedMap::const_iterator;
	bool isItemUnderPressSelected() const;
	[[nodiscard]] bool requiredToStartDragging(
		not_null<BaseLayout*> layout) const;
	[[nodiscard]] bool isPressInSelectedText(TextState state) const;
	void applyDragSelection();
	void applyDragSelection(SelectedMap &applyTo) const;

	[[nodiscard]] bool isAfter(
		const MouseState &a,
		const MouseState &b) const;
	[[nodiscard]] static bool SkipSelectFromItem(const MouseState &state);
	[[nodiscard]] static bool SkipSelectTillItem(const MouseState &state);

	[[nodiscard]] std::vector<Section>::iterator findSectionByItem(
		not_null<const HistoryItem*> item);
	[[nodiscard]] std::vector<Section>::iterator findSectionAfterTop(
		int top);
	[[nodiscard]] std::vector<Section>::const_iterator findSectionAfterTop(
		int top) const;
	[[nodiscard]] auto findSectionAfterBottom(
		std::vector<Section>::const_iterator from,
		int bottom) const -> std::vector<Section>::const_iterator;
	[[nodiscard]] FoundItem findItemByPoint(QPoint point) const;
	[[nodiscard]] std::optional<FoundItem> findItemByItem(
		const HistoryItem *item);
	[[nodiscard]] FoundItem findItemDetails(not_null<BaseLayout*> item);
	[[nodiscard]] FoundItem foundItemInSection(
		const FoundItem &item,
		const Section &section) const;

	[[nodiscard]] ListScrollTopState countScrollState() const;
	void saveScrollState();
	void restoreScrollState();

	[[nodiscard]] QPoint clampMousePosition(QPoint position) const;
	void mouseActionStart(
		const QPoint &globalPosition,
		Qt::MouseButton button);
	void mouseActionUpdate(const QPoint &globalPosition);
	void mouseActionUpdate();
	void mouseActionFinish(
		const QPoint &globalPosition,
		Qt::MouseButton button);
	void mouseActionCancel();
	void performDrag();
	[[nodiscard]] style::cursor computeMouseCursor() const;
	void showContextMenu(
		QContextMenuEvent *e,
		ContextMenuSource source);

	void updateDragSelection();
	void clearDragSelection();

	void updateDateBadgeFor(int top);
	void scrollDateCheck();
	void scrollDateHide();
	void toggleScrollDateShown();

	void trySwitchToWordSelection();
	void switchToWordSelection();
	void validateTrippleClickStartTime();
	void checkMoveToOtherViewer();
	void clearHeavyItems();

	void setActionBoxWeak(QPointer<Ui::BoxContent> box);

	const not_null<AbstractController*> _controller;
	const std::unique_ptr<ListProvider> _provider;

	base::flat_set<not_null<const BaseLayout*>> _heavyLayouts;
	bool _heavyLayoutsInvalidated = false;
	std::vector<Section> _sections;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	ListScrollTopState _scrollTopState;
	rpl::event_stream<int> _scrollToRequests;

	MouseAction _mouseAction = MouseAction::None;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	QPoint _mousePosition;
	MouseState _overState;
	MouseState _pressState;
	BaseLayout *_overLayout = nullptr;
	HistoryItem *_contextItem = nullptr;
	CursorState _mouseCursorState = CursorState();
	uint16 _mouseTextSymbol = 0;
	bool _pressWasInactive = false;
	SelectedMap _selected;
	SelectedMap _dragSelected;
	rpl::event_stream<SelectedItems> _selectedListStream;
	style::cursor _cursor = style::cur_default;
	DragSelectAction _dragSelectAction = DragSelectAction::None;
	bool _wasSelectedText = false; // was some text selected in current drag action

	const std::unique_ptr<DateBadge> _dateBadge;
	base::flat_map<not_null<Main::Session*>, rpl::lifetime> _trackedSessions;

	base::unique_qptr<Ui::PopupMenu> _contextMenu;
	rpl::event_stream<> _checkForHide;
	QPointer<Ui::BoxContent> _actionBoxWeak;
	rpl::lifetime _actionBoxWeakLifetime;

	QPoint _trippleClickPoint;
	crl::time _trippleClickStartTime = 0;

};

} // namespace Media
} // namespace Info
