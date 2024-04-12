/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"

namespace Ui {
class DynamicImage;
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
	[[nodiscard]] auto showMenuRequests() const
		-> rpl::producer<ShowTopPeerMenuRequest>;

	void removeLocally(uint64 id = 0);

	[[nodiscard]] bool selectedByKeyboard() const;
	void selectByKeyboard(Qt::Key direction);
	void deselectByKeyboard();
	bool chooseRow();

private:
	struct Entry;

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void updateScrollMax();
	void updateSelected();
	void setSelected(int selected);
	void scrollToSelected();
	void checkDragging();
	bool finishDragging();
	void subscribeUserpic(Entry &entry);
	void unsubscribeUserpics(bool all = false);
	void paintUserpic(Painter &p, int x, int index, bool selected);

	[[nodiscard]] QRect outer() const;
	[[nodiscard]] QRect innerRounded() const;
	void apply(const TopPeersList &list);
	void apply(Entry &entry, const TopPeersEntry &data);

	std::vector<Entry> _entries;
	rpl::variable<bool> _empty = true;
	base::flat_set<uint64> _removed;

	rpl::event_stream<uint64> _clicks;
	rpl::event_stream<ShowTopPeerMenuRequest> _showMenuRequests;
	rpl::event_stream<not_null<QWheelEvent*>> _verticalScrollEvents;

	QPoint _lastMousePosition;
	std::optional<QPoint> _mouseDownPosition;
	int _startDraggingLeft = 0;
	int _scrollLeft = 0;
	int _scrollLeftMax = 0;
	bool _dragging = false;
	Qt::Orientation _scrollingLock = {};

	int _selected = -1;
	int _pressed = -1;
	bool _selectionByKeyboard = false;
	bool _hiddenLocally = false;

	Ui::RoundRect _selection;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::has_weak_ptr _menuGuard;

};

} // namespace Dialogs
