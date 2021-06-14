/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "info/media/info_media_widget.h"
#include "data/data_shared_media.h"
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

using BaseLayout = Overview::Layout::ItemBase;
using UniversalMsgId = int32;

class ListWidget final
	: public Ui::RpWidget
	, public Overview::Layout::Delegate {
public:
	ListWidget(
		QWidget *parent,
		not_null<AbstractController*> controller);
	~ListWidget();

	Main::Session &session() const;

	void restart();

	rpl::producer<int> scrollToRequests() const;
	rpl::producer<SelectedItems> selectedListValue() const;
	void cancelSelection() {
		clearSelected();
	}

	QRect getCurrentSongGeometry();
	rpl::producer<> checkForHide() const {
		return _checkForHide.events();
	}
	bool preventAutoHide() const;

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	void registerHeavyItem(not_null<const BaseLayout*> item) override;
	void unregisterHeavyItem(not_null<const BaseLayout*> item) override;

	void openPhoto(not_null<PhotoData*> photo, FullMsgId id) override;
	void openDocument(
		not_null<DocumentData*> document,
		FullMsgId id) override;

private:
	struct Context;
	class Section;
	using CursorState = HistoryView::CursorState;
	using TextState = HistoryView::TextState;
	using StateRequest = HistoryView::StateRequest;
	enum class MouseAction {
		None,
		PrepareDrag,
		Dragging,
		PrepareSelect,
		Selecting,
	};
	struct CachedItem {
		CachedItem(std::unique_ptr<BaseLayout> item);
		CachedItem(CachedItem &&other);
		CachedItem &operator=(CachedItem &&other);
		~CachedItem();

		std::unique_ptr<BaseLayout> item;
		bool stale = false;
	};
	struct FoundItem {
		not_null<BaseLayout*> layout;
		QRect geometry;
		bool exact = false;
	};
	struct SelectionData {
		explicit SelectionData(TextSelection text) : text(text) {
		}

		TextSelection text;
		bool canDelete = false;
		bool canForward = false;
	};
	using SelectedMap = base::flat_map<
		UniversalMsgId,
		SelectionData,
		std::less<>>;
	enum class DragSelectAction {
		None,
		Selecting,
		Deselecting,
	};
	struct MouseState {
		UniversalMsgId itemId = 0;
		QSize size;
		QPoint cursor;
		bool inside = false;

		inline bool operator==(const MouseState &other) const {
			return (itemId == other.itemId)
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
	struct ScrollTopState {
		UniversalMsgId item = 0;
		int shift = 0;
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
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void start();
	int recountHeight();
	void refreshHeight();

	QMargins padding() const;
	bool isMyItem(not_null<const HistoryItem*> item) const;
	bool isItemLayout(
		not_null<const HistoryItem*> item,
		BaseLayout *layout) const;
	bool isPossiblyMyId(FullMsgId fullId) const;
	void repaintItem(const HistoryItem *item);
	void repaintItem(UniversalMsgId msgId);
	void repaintItem(const BaseLayout *item);
	void repaintItem(QRect itemGeometry);
	void itemRemoved(not_null<const HistoryItem*> item);
	void itemLayoutChanged(not_null<const HistoryItem*> item);

	void refreshViewer();
	void invalidatePaletteCache();
	void refreshRows();
	SparseIdsMergedSlice::Key sliceKey(
		UniversalMsgId universalId) const;
	BaseLayout *getLayout(UniversalMsgId universalId);
	BaseLayout *getExistingLayout(UniversalMsgId universalId) const;
	std::unique_ptr<BaseLayout> createLayout(
		UniversalMsgId universalId,
		Type type);

	SelectedItems collectSelectedItems() const;
	MessageIdsList collectSelectedIds() const;
	void pushSelectedItems();
	FullMsgId computeFullId(UniversalMsgId universalId) const;
	bool hasSelected() const;
	bool isSelectedItem(
		const SelectedMap::const_iterator &i) const;
	void removeItemSelection(
		const SelectedMap::const_iterator &i);
	bool hasSelectedText() const;
	bool hasSelectedItems() const;
	void clearSelected();
	void forwardSelected();
	void forwardItem(UniversalMsgId universalId);
	void forwardItems(MessageIdsList &&items);
	void deleteSelected();
	void deleteItem(UniversalMsgId universalId);
	DeleteMessagesBox *deleteItems(MessageIdsList &&items);
	void applyItemSelection(
		UniversalMsgId universalId,
		TextSelection selection);
	void toggleItemSelection(
		UniversalMsgId universalId);
	SelectedMap::iterator itemUnderPressSelection();
	SelectedMap::const_iterator itemUnderPressSelection() const;
	bool isItemUnderPressSelected() const;
	bool requiredToStartDragging(not_null<BaseLayout*> layout) const;
	bool isPressInSelectedText(TextState state) const;
	void applyDragSelection();
	void applyDragSelection(SelectedMap &applyTo) const;
	bool changeItemSelection(
		SelectedMap &selected,
		UniversalMsgId universalId,
		TextSelection selection) const;

	static bool IsAfter(
		const MouseState &a,
		const MouseState &b);
	static bool SkipSelectFromItem(const MouseState &state);
	static bool SkipSelectTillItem(const MouseState &state);

	void markLayoutsStale();
	void clearStaleLayouts();
	std::vector<Section>::iterator findSectionByItem(
		UniversalMsgId universalId);
	std::vector<Section>::iterator findSectionAfterTop(int top);
	std::vector<Section>::const_iterator findSectionAfterTop(
		int top) const;
	std::vector<Section>::const_iterator findSectionAfterBottom(
		std::vector<Section>::const_iterator from,
		int bottom) const;
	FoundItem findItemByPoint(QPoint point) const;
	std::optional<FoundItem> findItemById(UniversalMsgId universalId);
	FoundItem findItemDetails(not_null<BaseLayout*> item);
	FoundItem foundItemInSection(
		const FoundItem &item,
		const Section &section) const;

	ScrollTopState countScrollState() const;
	void saveScrollState();
	void restoreScrollState();

	QPoint clampMousePosition(QPoint position) const;
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
	style::cursor computeMouseCursor() const;
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

	void setActionBoxWeak(QPointer<Ui::RpWidget> box);

	const not_null<AbstractController*> _controller;
	const not_null<PeerData*> _peer;
	PeerData * const _migrated = nullptr;
	const Type _type = Type::Photo;

	static constexpr auto kMinimalIdsLimit = 16;
	static constexpr auto kDefaultAroundId = (ServerMaxMsgId - 1);
	UniversalMsgId _universalAroundId = kDefaultAroundId;
	int _idsLimit = kMinimalIdsLimit;
	SparseIdsMergedSlice _slice;

	std::unordered_map<UniversalMsgId, CachedItem> _layouts;
	base::flat_set<not_null<const BaseLayout*>> _heavyLayouts;
	bool _heavyLayoutsInvalidated = false;
	std::vector<Section> _sections;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	ScrollTopState _scrollTopState;
	rpl::event_stream<int> _scrollToRequests;

	MouseAction _mouseAction = MouseAction::None;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	QPoint _mousePosition;
	MouseState _overState;
	MouseState _pressState;
	BaseLayout *_overLayout = nullptr;
	UniversalMsgId _contextUniversalId = 0;
	CursorState _mouseCursorState = CursorState();
	uint16 _mouseTextSymbol = 0;
	bool _pressWasInactive = false;
	SelectedMap _selected;
	SelectedMap _dragSelected;
	rpl::event_stream<SelectedItems> _selectedListStream;
	style::cursor _cursor = style::cur_default;
	DragSelectAction _dragSelectAction = DragSelectAction::None;
	bool _wasSelectedText = false; // was some text selected in current drag action

	struct DateBadge {
		SingleQueuedInvokation check;
		base::Timer hideTimer;
		Ui::Animations::Simple opacity;
		bool goodType = false;
		bool shown = false;
		QString text;
		QRect rect;
	} _dateBadge;

	base::unique_qptr<Ui::PopupMenu> _contextMenu;
	rpl::event_stream<> _checkForHide;
	QPointer<Ui::RpWidget> _actionBoxWeak;
	rpl::lifetime _actionBoxWeakLifetime;

	QPoint _trippleClickPoint;
	crl::time _trippleClickStartTime = 0;

	rpl::lifetime _viewerLifetime;

};

} // namespace Media
} // namespace Info
