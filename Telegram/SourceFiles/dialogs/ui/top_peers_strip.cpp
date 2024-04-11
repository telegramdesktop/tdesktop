/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/top_peers_strip.h"

#include "ui/effects/ripple_animation.h"
#include "ui/text/text.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/dynamic_image.h"
#include "ui/painter.h"
#include "ui/unread_badge_paint.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QApplication>

namespace Dialogs {

struct TopPeersStrip::Entry {
	uint64 id = 0;
	Ui::Text::String name;
	std::shared_ptr<Ui::DynamicImage> userpic;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	Ui::Animations::Simple onlineShown;
	QImage userpicFrame;
	float64 userpicFrameOnline = 0.;
	QString badgeString;
	uint32 badge : 28 = 0;
	uint32 userpicFrameDirty : 1 = 0;
	uint32 subscribed : 1 = 0;
	uint32 online : 1 = 0;
	uint32 muted : 1 = 0;
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

TopPeersStrip::~TopPeersStrip() {
	unsubscribeUserpics(true);
}

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
		unsubscribeUserpics();
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
			unsubscribeUserpics();
			update();
		}
	}
}

void TopPeersStrip::unsubscribeUserpics(bool all) {
	const auto &st = st::topPeers;
	const auto single = st.photoLeft * 2 + st.photo;
	auto x = -_scrollLeft;
	for (auto &entry : _entries) {
		if (all || x + single <= 0 || x >= width()) {
			if (entry.subscribed) {
				entry.userpic->subscribeToUpdates(nullptr);
				entry.subscribed = false;
			}
			entry.userpicFrame = QImage();
			entry.onlineShown.stop();
			entry.ripple = nullptr;
		}
		x += single;
	}
}

void TopPeersStrip::subscribeUserpic(Entry &entry) {
	const auto raw = entry.userpic.get();
	entry.userpic->subscribeToUpdates([=] {
		const auto i = ranges::find(
			_entries,
			raw,
			[&](const Entry &entry) { return entry.userpic.get(); });
		if (i != end(_entries)) {
			i->userpicFrameDirty = 1;
		}
		update();
	});
	entry.subscribed = true;
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
	unsubscribeUserpics();
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
			entry.subscribed = false;
		}
	}
	_entries = std::move(now);
	unsubscribeUserpics();
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
		}
		entry.userpic = data.userpic;
		if (entry.subscribed) {
			subscribeUserpic(entry);
		}
	}
	if (entry.online != data.online) {
		entry.online = data.online;
		if (!entry.subscribed) {
			entry.onlineShown.stop();
		} else {
			entry.onlineShown.start(
				[=] { update(); },
				entry.online ? 0. : 1.,
				entry.online ? 1. : 0.,
				st::dialogsOnlineBadgeDuration);
		}
	}
	if (entry.badge != data.badge) {
		entry.badge = data.badge;
		entry.badgeString = QString();
		entry.userpicFrameDirty = 1;
	}
	if (entry.muted != data.muted) {
		entry.muted = data.muted;
		if (entry.badge) {
			entry.userpicFrameDirty = 1;
		}
	}
}

void TopPeersStrip::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto &st = st::topPeers;
	const auto line = st.lineTwice / 2;
	const auto single = st.photoLeft * 2 + st.photo;

	const auto from = std::min(_scrollLeft / single, int(_entries.size()));
	const auto till = std::max(
		(_scrollLeft + width() + single - 1) / single + 1,
		from);

	auto x = -_scrollLeft + from * single;
	for (auto i = from; i != till; ++i) {
		auto &entry = _entries[i];
		if (!entry.subscribed) {
			subscribeUserpic(entry);
		}
		paintUserpic(p, i, x);

		const auto nameLeft = x + st.nameLeft;
		const auto nameWidth = single - 2 * st.nameLeft;
		entry.name.drawElided(
			p,
			nameLeft,
			st.nameTop,
			nameWidth,
			1,
			style::al_top);
		x += single;
	}
}

void TopPeersStrip::paintUserpic(Painter &p, int index, int x) {
	Expects(index >= 0 && index < _entries.size());

	auto &entry = _entries[index];
	const auto &st = st::topPeers;
	const auto size = st.photo;
	const auto rect = QRect(x + st.photoLeft, st.photoTop, size, size);

	const auto online = entry.onlineShown.value(entry.online ? 1. : 0.);
	const auto useFrame = !entry.userpicFrame.isNull()
		&& !entry.userpicFrameDirty
		&& (entry.userpicFrameOnline == online);
	if (useFrame) {
		p.drawImage(rect, entry.userpicFrame);
		return;
	}
	const auto simple = entry.userpic->image(size);
	const auto ratio = style::DevicePixelRatio();
	const auto renderFrame = (online > 0) || entry.badge;
	if (!renderFrame) {
		entry.userpicFrame = QImage();
		p.drawImage(rect, simple);
		return;
	} else if (entry.userpicFrame.size() != QSize(size, size) * ratio) {
		entry.userpicFrame = QImage(
			QSize(size, size) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		entry.userpicFrame.setDevicePixelRatio(ratio);
	}
	entry.userpicFrame.fill(Qt::transparent);
	entry.userpicFrameDirty = 0;
	entry.userpicFrameOnline = online;

	auto q = QPainter(&entry.userpicFrame);
	const auto inner = QRect(0, 0, size, size);
	q.drawImage(inner, simple);

	auto hq = PainterHighQualityEnabler(p);

	if (online > 0) {
		q.setCompositionMode(QPainter::CompositionMode_Source);
		const auto onlineSize = st::dialogsOnlineBadgeSize;
		const auto stroke = st::dialogsOnlineBadgeStroke;
		const auto skip = st::dialogsOnlineBadgeSkip;
		const auto shrink = (onlineSize / 2) * (1. - online);

		auto pen = QPen(Qt::transparent);
		pen.setWidthF(stroke * online);
		q.setPen(pen);
		q.setBrush(st::dialogsOnlineBadgeFg);
		q.drawEllipse(QRectF(
			size - skip.x() - onlineSize,
			size - skip.y() - onlineSize,
			onlineSize,
			onlineSize
		).marginsRemoved({ shrink, shrink, shrink, shrink }));
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
	}

	if (entry.badge) {
		if (entry.badgeString.isEmpty()) {
			entry.badgeString = (entry.badge < 1000)
				? QString::number(entry.badge)
				: (QString::number(entry.badge / 1000) + 'K');
		}
		auto st = Ui::UnreadBadgeStyle();
		st.selected = (_selected == index);
		st.muted = entry.muted;
		const auto &counter = entry.badgeString;
		const auto badge = PaintUnreadBadge(q, counter, size, 0, st);
	}

	q.end();

	p.drawImage(rect, entry.userpicFrame);
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
	const auto index = (_scrollLeft + x) / single;
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
