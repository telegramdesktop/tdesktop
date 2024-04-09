/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/top_peers_strip.h"

#include "ui/text/text.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QApplication>

namespace Dialogs {

struct TopPeersStrip::Entry {
	uint64 id = 0;
	Ui::Text::String name;
	std::shared_ptr<Ui::DynamicImage> userpic;
	bool subscribed = false;
};

TopPeersStrip::TopPeersStrip(
	not_null<QWidget*> parent,
	rpl::producer<TopPeersList> content)
: RpWidget(parent) {
	resize(0, st::topPeers.height);

	std::move(content) | rpl::start_with_next([=](const TopPeersList &list) {
		apply(list);
	}, lifetime());

	setMouseTracking(true);
}

TopPeersStrip::~TopPeersStrip() = default;

void TopPeersStrip::resizeEvent(QResizeEvent *e) {
	updateScrollMax();
}

void TopPeersStrip::wheelEvent(QWheelEvent *e) {
	const auto phase = e->phase();
	const auto fullDelta = e->pixelDelta().isNull()
		? e->angleDelta()
		: e->pixelDelta();
	if (phase == Qt::ScrollBegin || phase == Qt::ScrollEnd) {
		_scrollingLock = Qt::Orientation();
		if (fullDelta.isNull()) {
			return;
		}
	}
	const auto vertical = qAbs(fullDelta.x()) < qAbs(fullDelta.y());
	if (_scrollingLock == Qt::Orientation() && phase != Qt::NoScrollPhase) {
		_scrollingLock = vertical ? Qt::Vertical : Qt::Horizontal;
	}
	if (_scrollingLock == Qt::Vertical || (vertical && !_scrollLeftMax)) {
		_verticalScrollEvents.fire(e);
		return;
	}
	const auto delta = vertical
		? fullDelta.y()
		: ((style::RightToLeft() ? -1 : 1) * fullDelta.x());

	const auto now = _scrollLeft;
	const auto used = now - delta;
	const auto next = std::clamp(used, 0, _scrollLeftMax);
	if (next != now) {
		_scrollLeft = next;
		updateSelected();
		update();
	}
	e->accept();
}

void TopPeersStrip::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePosition = e->globalPos();
	updateSelected();

	_mouseDownPosition = _lastMousePosition;
	_pressed = _selected;
}

void TopPeersStrip::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePosition = e->globalPos();
	updateSelected();

	if (!_dragging && _mouseDownPosition) {
		if ((_lastMousePosition - *_mouseDownPosition).manhattanLength()
			>= QApplication::startDragDistance()) {
			_dragging = true;
			_startDraggingLeft = _scrollLeft;
		}
	}
	checkDragging();
}

void TopPeersStrip::checkDragging() {
	if (_dragging) {
		const auto sign = (style::RightToLeft() ? -1 : 1);
		const auto newLeft = std::clamp(
			(sign * (_mouseDownPosition->x() - _lastMousePosition.x())
				+ _startDraggingLeft),
			0,
			_scrollLeftMax);
		if (newLeft != _scrollLeft) {
			_scrollLeft = newLeft;
			update();
		}
	}
}

void TopPeersStrip::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePosition = e->globalPos();
	const auto guard = gsl::finally([&] {
		_mouseDownPosition = std::nullopt;
	});

	const auto pressed = std::exchange(_pressed, -1);
	if (finishDragging()) {
		return;
	}
	updateSelected();
	if (_selected == pressed) {
		if (_selected < _entries.size()) {
			_clicks.fire_copy(_entries[_selected].id);
		}
	}
}

void TopPeersStrip::updateScrollMax() {
	const auto &st = st::topPeers;
	const auto single = st.photoLeft * 2 + st.photo;
	const auto widthFull = int(_entries.size()) * single;
	_scrollLeftMax = std::max(widthFull - width(), 0);
	_scrollLeft = std::clamp(_scrollLeft, 0, _scrollLeftMax);
	update();
}

bool TopPeersStrip::empty() const {
	return _empty.current();
}

rpl::producer<bool> TopPeersStrip::emptyValue() const {
	return _empty.value();
}

rpl::producer<uint64> TopPeersStrip::clicks() const {
	return _clicks.events();
}

auto TopPeersStrip::showMenuRequests() const
-> rpl::producer<ShowTopPeerMenuRequest> {
	return _showMenuRequests.events();
}

void TopPeersStrip::apply(const TopPeersList &list) {
	auto now = std::vector<Entry>();

	if (list.entries.empty()) {
		_empty = true;
	}
	for (const auto &entry : list.entries) {
		const auto i = ranges::find(_entries, entry.id, &Entry::id);
		if (i != end(_entries)) {
			now.push_back(base::take(*i));
		} else {
			now.push_back({ .id = entry.id });
		}
		apply(now.back(), entry);
	}
	for (auto &entry : _entries) {
		if (entry.subscribed) {
			entry.userpic->subscribeToUpdates(nullptr);
			entry.subscribed = 0;
		}
	}
	_entries = std::move(now);
	if (!_entries.empty()) {
		_empty = false;
	}
	update();
}

void TopPeersStrip::apply(Entry &entry, const TopPeersEntry &data) {
	Expects(entry.id == data.id);
	Expects(data.userpic != nullptr);

	if (entry.name.toString() != data.name) {
		entry.name.setText(st::topPeers.nameStyle, data.name);
	}
	if (entry.userpic.get() != data.userpic.get()) {
		if (entry.subscribed) {
			entry.userpic->subscribeToUpdates(nullptr);
			entry.subscribed = 0;
		}
		entry.userpic = data.userpic;
	}
}

void TopPeersStrip::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto x = -_scrollLeft;
	const auto &st = st::topPeers;
	const auto line = st.lineTwice / 2;
	const auto single = st.photoLeft * 2 + st.photo;
	for (auto &entry : _entries) {
		if (!entry.subscribed) {
			entry.userpic->subscribeToUpdates([=] {
				update();
			});
			entry.subscribed = 1;
		}
		const auto image = entry.userpic->image(st.photo);
		p.drawImage(
			QRect(x + st.photoLeft, st.photoTop, st.photo, st.photo),
			image);

		const auto nameLeft = x + st.nameLeft;
		entry.name.drawElided(p, nameLeft, st.nameTop, single, 1, style::al_top);

		x += single;
	}
}

void TopPeersStrip::contextMenuEvent(QContextMenuEvent *e) {
	_menu = nullptr;

	if (e->reason() == QContextMenuEvent::Mouse) {
		_lastMousePosition = e->globalPos();
		updateSelected();
	}
	if (_selected < 0 || _entries.empty()) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	_showMenuRequests.fire({
		_entries[_selected].id,
		Ui::Menu::CreateAddActionCallback(_menu),
	});
	if (_menu->empty()) {
		_menu = nullptr;
		return;
	}
	const auto updateAfterMenuDestroyed = [=] {
		const auto globalPosition = QCursor::pos();
		if (rect().contains(mapFromGlobal(globalPosition))) {
			_lastMousePosition = globalPosition;
			updateSelected();
		}
	};
	QObject::connect(
		_menu.get(),
		&QObject::destroyed,
		crl::guard(&_menuGuard, updateAfterMenuDestroyed));
	_menu->popup(e->globalPos());
	e->accept();
}

bool TopPeersStrip::finishDragging() {
	if (!_dragging) {
		return false;
	}
	checkDragging();
	_dragging = false;
	updateSelected();
	return true;
}

void TopPeersStrip::updateSelected() {
	if (_pressed >= 0) {
		return;
	}
	const auto &st = st::topPeers;
	const auto p = mapFromGlobal(_lastMousePosition);
	const auto x = p.x();
	const auto single = st.photoLeft * 2 + st.photo;
	const auto index = (x - _scrollLeft) / single;
	const auto selected = (index < 0 || index >= _entries.size())
		? -1
		: index;
	if (_selected != selected) {
		const auto over = (selected >= 0);
		if (over != (_selected >= 0)) {
			setCursor(over ? style::cur_pointer : style::cur_default);
		}
		_selected = selected;
	}
}

} // namespace Dialogs
