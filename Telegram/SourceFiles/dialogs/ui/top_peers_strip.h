/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"

namespace Ui {
class DynamicImage;
class LinkButton;
struct ScrollToRequest;
} // namespace Ui

namespace Dialogs {

struct TopPeersEntry {
	uint64 id = 0;
	QString name;
	std::shared_ptr<Ui::DynamicImage> userpic;
	uint32 badge : 28 = 0;
	uint32 unread : 1 = 0;
	uint32 muted : 1 = 0;
	uint32 online : 1 = 0;
};

struct TopPeersList {
	std::vector<TopPeersEntry> entries;
};

struct ShowTopPeerMenuRequest {
	uint64 id = 0;
	Ui::Menu::MenuCallback callback;
};

class TopPeersStrip final : public Ui::RpWidget {
public:
	TopPeersStrip(
		not_null<QWidget*> parent,
		rpl::producer<TopPeersList> content);
	~TopPeersStrip();

	[[nodiscard]] bool empty() const;
	[[nodiscard]] rpl::producer<bool> emptyValue() const;
	[[nodiscard]] rpl::producer<uint64> clicks() const;
	[[nodiscard]] rpl::producer<uint64> pressed() const;
	[[nodiscard]] rpl::producer<> pressCancelled() const;
	[[nodiscard]] auto showMenuRequests() const
		-> rpl::producer<ShowTopPeerMenuRequest>;
	[[nodiscard]] auto scrollToRequests() const
		-> rpl::producer<Ui::ScrollToRequest>;

	void removeLocally(uint64 id = 0);

	[[nodiscard]] bool selectedByKeyboard() const;
	bool selectByKeyboard(Qt::Key direction);
	void deselectByKeyboard();
	bool chooseRow();
	void pressLeftToContextMenu(bool shown);

	uint64 updateFromParentDrag(QPoint globalPosition);
	void dragLeft();

	[[nodiscard]] auto verticalScrollEvents() const
		-> rpl::producer<not_null<QWheelEvent*>>;

private:
	struct Entry;
	struct Layout;

	int resizeGetHeight(int newWidth) override;

	void setupHeader();
	void setupStrip();

	void paintStrip(QRect clip);
	void stripWheelEvent(QWheelEvent *e);
	void stripMousePressEvent(QMouseEvent *e);
	void stripMouseMoveEvent(QMouseEvent *e);
	void stripMouseReleaseEvent(QMouseEvent *e);
	void stripContextMenuEvent(QContextMenuEvent *e);
	void stripLeaveEvent(QEvent *e);

	void updateScrollMax(int newWidth = 0);
	void updateSelected();
	void setSelected(int selected);
	void setExpanded(bool expanded);
	void scrollToSelected();
	void checkDragging();
	bool finishDragging();
	void subscribeUserpic(Entry &entry);
	void unsubscribeUserpics(bool all = false);
	void paintUserpic(Painter &p, int x, int y, int index, bool selected);
	void clearSelection();
	void selectByMouse(QPoint globalPosition);

	[[nodiscard]] QRect outer() const;
	[[nodiscard]] QRect innerRounded() const;
	[[nodiscard]] int scrollLeft() const;
	[[nodiscard]] Layout currentLayout() const;
	int clearPressed();
	void apply(const TopPeersList &list);
	void apply(Entry &entry, const TopPeersEntry &data);

	Ui::RpWidget _header;
	Ui::RpWidget _strip;

	std::vector<Entry> _entries;
	rpl::variable<int> _count = 0;
	base::flat_set<uint64> _removed;
	rpl::variable<Ui::LinkButton*> _toggleExpanded = nullptr;

	rpl::event_stream<uint64> _clicks;
	rpl::event_stream<uint64> _presses;
	rpl::event_stream<> _pressCancelled;
	rpl::event_stream<ShowTopPeerMenuRequest> _showMenuRequests;
	rpl::event_stream<not_null<QWheelEvent*>> _verticalScrollEvents;

	std::optional<QPoint> _lastMousePosition;
	std::optional<QPoint> _mouseDownPosition;
	int _startDraggingLeft = 0;
	int _scrollLeft = 0;
	int _scrollLeftMax = 0;
	bool _dragging = false;
	Qt::Orientation _scrollingLock = {};

	int _selected = -1;
	int _pressed = -1;
	int _contexted = -1;
	bool _selectionByKeyboard = false;
	bool _hiddenLocally = false;

	Ui::Animations::Simple _expandAnimation;
	rpl::variable<bool> _expanded = false;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

	Ui::RoundRect _selection;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::has_weak_ptr _menuGuard;

};

} // namespace Dialogs
