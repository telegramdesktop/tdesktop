/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/top_peers_strip.h"

#include "base/event_filter.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
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
	uint32 badge : 27 = 0;
	uint32 userpicFrameDirty : 1 = 0;
	uint32 subscribed : 1 = 0;
	uint32 unread : 1 = 0;
	uint32 online : 1 = 0;
	uint32 muted : 1 = 0;
};

struct TopPeersStrip::Layout {
	int single = 0;
	int inrow = 0;
	float64 fsingle = 0.;
	float64 added = 0.;
};

TopPeersStrip::TopPeersStrip(
	not_null<QWidget*> parent,
	rpl::producer<TopPeersList> content)
: RpWidget(parent)
, _header(this)
, _strip(this)
, _selection(st::topPeersRadius, st::windowBgOver) {
	setupHeader();
	setupStrip();

	std::move(content) | rpl::start_with_next([=](const TopPeersList &list) {
		apply(list);
	}, lifetime());

	rpl::combine(
		_count.value(),
		_expanded.value()
	) | rpl::start_with_next([=] {
		resizeToWidth(width());
	}, _strip.lifetime());

	resize(0, _header.height() + _strip.height());
}

void TopPeersStrip::setupHeader() {
	_header.resize(0, st::searchedBarHeight);

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		&_header,
		tr::lng_recent_frequent(),
		st::searchedBarLabel);
	const auto single = outer().width();

	rpl::combine(
		_count.value(),
		widthValue()
	) | rpl::map(
		(rpl::mappers::_1 * single) > (rpl::mappers::_2 + (single * 2) / 3)
	) | rpl::distinct_until_changed() | rpl::start_with_next([=](bool more) {
		setExpanded(false);
		if (!more) {
			const auto toggle = _toggleExpanded.current();
			_toggleExpanded = nullptr;
			delete toggle;
			return;
		} else if (_toggleExpanded.current()) {
			return;
		}
		const auto toggle = Ui::CreateChild<Ui::LinkButton>(
			&_header,
			tr::lng_channels_your_more(tr::now),
			st::searchedBarLink);
		toggle->show();
		toggle->setClickedCallback([=] {
			const auto expand = !_expanded.current();
			toggle->setText(expand
				? tr::lng_recent_frequent_collapse(tr::now)
				: tr::lng_recent_frequent_all(tr::now));
			setExpanded(expand);
		});
		rpl::combine(
			_header.sizeValue(),
			toggle->widthValue()
		) | rpl::start_with_next([=](QSize size, int width) {
			const auto x = st::searchedBarPosition.x();
			const auto y = st::searchedBarPosition.y();
			toggle->moveToRight(0, 0, size.width());
			label->resizeToWidth(size.width() - x - width);
			label->moveToLeft(x, y, size.width());
		}, toggle->lifetime());
		_toggleExpanded = toggle;
	}, _header.lifetime());

	rpl::combine(
		_header.sizeValue(),
		_toggleExpanded.value()
	) | rpl::filter(
		rpl::mappers::_2 == nullptr
	) | rpl::start_with_next([=](QSize size, const auto) {
		const auto x = st::searchedBarPosition.x();
		const auto y = st::searchedBarPosition.y();
		label->resizeToWidth(size.width() - x * 2);
		label->moveToLeft(x, y, size.width());
	}, _header.lifetime());

	_header.paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(&_header).fillRect(clip, st::searchedBarBg);
	}, _header.lifetime());
}

void TopPeersStrip::setExpanded(bool expanded) {
	if (_expanded.current() == expanded) {
		return;
	}
	const auto from = expanded ? 0. : 1.;
	const auto to = expanded ? 1. : 0.;
	_expandAnimation.start([=] {
		if (!_expandAnimation.animating()) {
			updateScrollMax();
		}
		resizeToWidth(width());
		update();
	}, from, to, st::slideDuration, anim::easeOutQuint);
	_expanded = expanded;
}

void TopPeersStrip::setupStrip() {
	_strip.resize(0, st::topPeers.height);

	_strip.setMouseTracking(true);

	base::install_event_filter(&_strip, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Wheel) {
			stripWheelEvent(static_cast<QWheelEvent*>(e.get()));
		} else if (type == QEvent::MouseButtonPress) {
			stripMousePressEvent(static_cast<QMouseEvent*>(e.get()));
		} else if (type == QEvent::MouseMove) {
			stripMouseMoveEvent(static_cast<QMouseEvent*>(e.get()));
		} else if (type == QEvent::MouseButtonRelease) {
			stripMouseReleaseEvent(static_cast<QMouseEvent*>(e.get()));
		} else if (type == QEvent::ContextMenu) {
			stripContextMenuEvent(static_cast<QContextMenuEvent*>(e.get()));
		} else if (type == QEvent::Leave) {
			stripLeaveEvent(e.get());
		} else {
			return base::EventFilterResult::Continue;
		}
		return base::EventFilterResult::Cancel;
	});

	_strip.paintRequest() | rpl::start_with_next([=](QRect clip) {
		paintStrip(clip);
	}, _strip.lifetime());
}

TopPeersStrip::~TopPeersStrip() {
	unsubscribeUserpics(true);
}

int TopPeersStrip::resizeGetHeight(int newWidth) {
	_header.resize(newWidth, _header.height());
	const auto single = QSize(outer().width(), st::topPeers.height);
	const auto inRow = newWidth / single.width();
	const auto rows = (inRow > 0)
		? ((std::max(_count.current(), 1) + inRow - 1) / inRow)
		: 1;
	const auto height = single.height() * rows;
	const auto value = _expandAnimation.value(_expanded.current() ? 1. : 0.);
	const auto result = anim::interpolate(single.height(), height, value);
	_strip.setGeometry(0, _header.height(), newWidth, result);
	updateScrollMax(newWidth);
	return _strip.y() + _strip.height();
}

void TopPeersStrip::stripWheelEvent(QWheelEvent *e) {
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
	} else if (_expandAnimation.animating()) {
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

void TopPeersStrip::stripLeaveEvent(QEvent *e) {
	if (!_selectionByKeyboard) {
		setSelected(-1);
	}
	if (!_dragging) {
		_lastMousePosition = std::nullopt;
	}
}

void TopPeersStrip::stripMousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePosition = e->globalPos();
	_selectionByKeyboard = false;
	updateSelected();

	_mouseDownPosition = _lastMousePosition;
	_pressed = _selected;

	if (_selected >= 0) {
		Assert(_selected < _entries.size());
		auto &entry = _entries[_selected];
		if (!entry.ripple) {
			entry.ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(
					innerRounded().size(),
					st::topPeersRadius),
				[=] { update(); });
		}
		const auto layout = currentLayout();
		const auto expanded = _expanded.current();
		const auto row = expanded ? (_selected / layout.inrow) : 0;
		const auto column = (_selected - (row * layout.inrow));
		const auto x = layout.added + column * layout.fsingle - scrollLeft();
		const auto y = row * st::topPeers.height;
		entry.ripple->add(e->pos() - QPoint(
			x + st::topPeersMargin.left(),
			y + st::topPeersMargin.top()));
	}
}

void TopPeersStrip::stripMouseMoveEvent(QMouseEvent *e) {
	if (!_lastMousePosition) {
		_lastMousePosition = e->globalPos();
		if (_selectionByKeyboard) {
			return;
		}
	} else if (_selectionByKeyboard
		&& (_lastMousePosition == e->globalPos())) {
		return;
	}
	_lastMousePosition = e->globalPos();
	_selectionByKeyboard = false;
	updateSelected();

	if (!_dragging && _mouseDownPosition) {
		if ((*_lastMousePosition - *_mouseDownPosition).manhattanLength()
			>= QApplication::startDragDistance()) {
			if (!_expandAnimation.animating()) {
				_dragging = true;
				_startDraggingLeft = _scrollLeft;
			}
		}
	}
	checkDragging();
}

void TopPeersStrip::checkDragging() {
	if (_dragging && !_expandAnimation.animating()) {
		const auto sign = (style::RightToLeft() ? -1 : 1);
		const auto newLeft = std::clamp(
			(sign * (_mouseDownPosition->x() - _lastMousePosition->x())
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
	if (!all && (_expandAnimation.animating() || _expanded.current())) {
		return;
	}
	const auto single = outer().width();
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

void TopPeersStrip::stripMouseReleaseEvent(QMouseEvent *e) {
	_lastMousePosition = e->globalPos();
	const auto guard = gsl::finally([&] {
		_mouseDownPosition = std::nullopt;
	});

	const auto pressed = std::exchange(_pressed, -1);
	if (pressed >= 0) {
		Assert(pressed < _entries.size());
		auto &entry = _entries[pressed];
		if (entry.ripple) {
			entry.ripple->lastStop();
		}
	}
	if (finishDragging()) {
		return;
	}
	_selectionByKeyboard = false;
	updateSelected();
	if (_selected >= 0 && _selected == pressed) {
		Assert(_selected < _entries.size());
		_clicks.fire_copy(_entries[_selected].id);
	}
}

void TopPeersStrip::updateScrollMax(int newWidth) {
	if (_expandAnimation.animating()) {
		return;
	} else if (!newWidth) {
		newWidth = width();
	}
	if (_expanded.current()) {
		_scrollLeft = 0;
		_scrollLeftMax = 0;
	} else {
		const auto single = outer().width();
		const auto widthFull = int(_entries.size()) * single;
		_scrollLeftMax = std::max(widthFull - newWidth, 0);
		_scrollLeft = std::clamp(_scrollLeft, 0, _scrollLeftMax);
	}
	unsubscribeUserpics();
	update();
}

bool TopPeersStrip::empty() const {
	return !_count.current();
}

rpl::producer<bool> TopPeersStrip::emptyValue() const {
	return _count.value()
		| rpl::map(!rpl::mappers::_1)
		| rpl::distinct_until_changed();
}

rpl::producer<uint64> TopPeersStrip::clicks() const {
	return _clicks.events();
}

auto TopPeersStrip::showMenuRequests() const
-> rpl::producer<ShowTopPeerMenuRequest> {
	return _showMenuRequests.events();
}

auto TopPeersStrip::scrollToRequests() const
-> rpl::producer<Ui::ScrollToRequest> {
	return _scrollToRequests.events();
}

void TopPeersStrip::removeLocally(uint64 id) {
	if (!id) {
		unsubscribeUserpics(true);
		setSelected(-1);
		_pressed = -1;
		_entries.clear();
		_hiddenLocally = true;
		_count = 0;
		return;
	}
	_removed.emplace(id);
	const auto i = ranges::find(_entries, id, &Entry::id);
	if (i == end(_entries)) {
		return;
	} else if (i->subscribed) {
		i->userpic->subscribeToUpdates(nullptr);
	}
	const auto index = int(i - begin(_entries));
	_entries.erase(i);
	if (_selected > index) {
		--_selected;
	}
	if (_pressed > index) {
		--_pressed;
	}
	updateScrollMax();
	_count = int(_entries.size());
	update();
}

bool TopPeersStrip::selectedByKeyboard() const {
	return _selectionByKeyboard && _selected >= 0;
}

bool TopPeersStrip::selectByKeyboard(Qt::Key direction) {
	if (_entries.empty()) {
		return false;
	} else if (direction == Qt::Key()) {
		_selectionByKeyboard = true;
		if (_selected < 0) {
			setSelected(0);
			scrollToSelected();
			return true;
		}
	} else if (direction == Qt::Key_Left) {
		if (_selected > 0) {
			_selectionByKeyboard = true;
			setSelected(_selected - 1);
			scrollToSelected();
			return true;
		}
	} else if (direction == Qt::Key_Right) {
		if (_selected + 1 < _entries.size()) {
			_selectionByKeyboard = true;
			setSelected(_selected + 1);
			scrollToSelected();
			return true;
		}
	} else if (direction == Qt::Key_Up) {
		const auto layout = currentLayout();
		if (_selected < 0) {
			_selectionByKeyboard = true;
			const auto rows = _expanded.current()
				? ((int(_entries.size()) + layout.inrow - 1) / layout.inrow)
				: 1;
			setSelected((rows - 1) * layout.inrow);
			scrollToSelected();
			return true;
		} else if (!_expanded.current()) {
			deselectByKeyboard();
		} else if (_selected >= 0) {
			const auto row = _selected / layout.inrow;
			if (row > 0) {
				_selectionByKeyboard = true;
				setSelected(_selected - layout.inrow);
				scrollToSelected();
				return true;
			} else {
				deselectByKeyboard();
			}
		}
	} else if (direction == Qt::Key_Down) {
		if (_selected >= 0 && _expanded.current()) {
			const auto layout = currentLayout();
			const auto row = _selected / layout.inrow;
			const auto rows = (int(_entries.size()) + layout.inrow - 1)
				/ layout.inrow;
			if (row + 1 < rows) {
				_selectionByKeyboard = true;
				setSelected(std::min(
					_selected + layout.inrow,
					int(_entries.size()) - 1));
				scrollToSelected();
				return true;
			} else {
				deselectByKeyboard();
			}
		}
	}
	return false;
}

void TopPeersStrip::deselectByKeyboard() {
	if (_selectionByKeyboard) {
		setSelected(-1);
	}
}

bool TopPeersStrip::chooseRow() {
	if (_selected >= 0) {
		Assert(_selected < _entries.size());
		_clicks.fire_copy(_entries[_selected].id);
		return true;
	}
	return false;
}

void TopPeersStrip::apply(const TopPeersList &list) {
	if (_hiddenLocally) {
		return;
	}
	auto now = std::vector<Entry>();

	auto selectedId = (_selected >= 0) ? _entries[_selected].id : 0;
	auto pressedId = (_pressed >= 0) ? _entries[_pressed].id : 0;
	for (const auto &entry : list.entries) {
		if (_removed.contains(entry.id)) {
			continue;
		}
		const auto i = ranges::find(_entries, entry.id, &Entry::id);
		if (i != end(_entries)) {
			now.push_back(base::take(*i));
		} else {
			now.push_back({ .id = entry.id });
		}
		apply(now.back(), entry);
	}
	if (now.empty()) {
		_count = 0;
	}
	for (auto &entry : _entries) {
		if (entry.subscribed) {
			entry.userpic->subscribeToUpdates(nullptr);
			entry.subscribed = false;
		}
	}
	_entries = std::move(now);
	if (selectedId) {
		const auto i = ranges::find(_entries, selectedId, &Entry::id);
		if (i != end(_entries)) {
			_selected = int(i - begin(_entries));
		}
	}
	if (pressedId) {
		const auto i = ranges::find(_entries, pressedId, &Entry::id);
		if (i != end(_entries)) {
			_pressed = int(i - begin(_entries));
		}
	}
	updateScrollMax();
	unsubscribeUserpics();
	_count = int(_entries.size());
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
	if (entry.unread != data.unread) {
		entry.unread = data.unread;
		if (!entry.badge) {
			entry.userpicFrameDirty = 1;
		}
	}
	if (entry.muted != data.muted) {
		entry.muted = data.muted;
		if (entry.badge || entry.unread) {
			entry.userpicFrameDirty = 1;
		}
	}
}

QRect TopPeersStrip::outer() const {
	const auto &st = st::topPeers;
	const auto single = st.photoLeft * 2 + st.photo;
	return QRect(0, 0, single, st::topPeers.height);
}

QRect TopPeersStrip::innerRounded() const {
	return outer().marginsRemoved(st::topPeersMargin);
}

int TopPeersStrip::scrollLeft() const {
	const auto value = _expandAnimation.value(_expanded.current() ? 1. : 0.);
	return anim::interpolate(_scrollLeft, 0, value);
}

void TopPeersStrip::paintStrip(QRect clip) {
	auto p = Painter(&_strip);

	const auto &st = st::topPeers;
	const auto scroll = scrollLeft();

	const auto rows = (height() + st.height - 1) / st.height;
	const auto fromrow = std::min(clip.y() / st.height, rows);
	const auto tillrow = std::min(
		(clip.y() + clip.height() + st.height - 1) / st.height,
		rows);
	const auto layout = currentLayout();
	const auto fsingle = layout.fsingle;
	const auto added = layout.added;

	for (auto row = fromrow; row != tillrow; ++row) {
		const auto shift = scroll + row * layout.inrow * fsingle;
		const auto from = std::min(
			int(std::floor((shift + clip.x()) / fsingle)),
			int(_entries.size()));
		const auto till = std::clamp(
			int(std::ceil(
				(shift + clip.x() + clip.width() + fsingle - 1) / fsingle + 1
			)),
			from,
			int(_entries.size()));

		auto x = int(base::SafeRound(-shift + from * fsingle + added));
		auto y = row * st.height;
		const auto highlighted = (_pressed >= 0) ? _pressed : _selected;
		for (auto i = from; i != till; ++i) {
			auto &entry = _entries[i];
			const auto selected = (i == highlighted);
			if (selected) {
				_selection.paint(p, innerRounded().translated(x, y));
			}
			if (entry.ripple) {
				entry.ripple->paint(
					p,
					x + st::topPeersMargin.left(),
					y + st::topPeersMargin.top(),
					width());
				if (entry.ripple->empty()) {
					entry.ripple = nullptr;
				}
			}

			if (!entry.subscribed) {
				subscribeUserpic(entry);
			}
			paintUserpic(p, x, y, i, selected);

			p.setPen(st::dialogsNameFg);
			entry.name.drawElided(
				p,
				x + st.nameLeft,
				y + st.nameTop,
				layout.single - 2 * st.nameLeft,
				1,
				style::al_top);
			x += fsingle;
		}
	}
}

void TopPeersStrip::paintUserpic(
		Painter &p,
		int x,
		int y,
		int index,
		bool selected) {
	Expects(index >= 0 && index < _entries.size());

	auto &entry = _entries[index];
	const auto &st = st::topPeers;
	const auto size = st.photo;
	const auto rect = QRect(x + st.photoLeft, y + st.photoTop, size, size);

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
	const auto renderFrame = (online > 0) || entry.badge || entry.unread;
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

	auto hq = PainterHighQualityEnabler(q);

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

	if (entry.badge || entry.unread) {
		if (entry.badgeString.isEmpty()) {
			entry.badgeString = !entry.badge
				? u" "_q
				: (entry.badge < 1000)
				? QString::number(entry.badge)
				: (QString::number(entry.badge / 1000) + 'K');
		}
		auto st = Ui::UnreadBadgeStyle();
		st.selected = selected;
		st.muted = entry.muted;
		const auto &counter = entry.badgeString;
		const auto badge = PaintUnreadBadge(q, counter, size, 0, st);

		const auto width = style::ConvertScaleExact(2.);
		const auto add = (width - style::ConvertScaleExact(1.)) / 2.;
		auto pen = QPen(Qt::transparent);
		pen.setWidthF(width);
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.setPen(pen);
		q.setBrush(Qt::NoBrush);
		q.drawEllipse(QRectF(badge).marginsAdded({ add, add, add, add }));
	}

	q.end();

	p.drawImage(rect, entry.userpicFrame);
}

void TopPeersStrip::stripContextMenuEvent(QContextMenuEvent *e) {
	_menu = nullptr;

	if (e->reason() == QContextMenuEvent::Mouse) {
		_lastMousePosition = e->globalPos();
		_selectionByKeyboard = false;
		updateSelected();
	}
	if (_selected < 0 || _entries.empty()) {
		return;
	}
	Assert(_selected < _entries.size());
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
			_selectionByKeyboard = false;
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
	_selectionByKeyboard = false;
	updateSelected();
	return true;
}

TopPeersStrip::Layout TopPeersStrip::currentLayout() const {
	const auto single = outer().width();
	const auto inrow = std::max(width() / single, 1);
	const auto value = _expandAnimation.value(_expanded.current() ? 1. : 0.);
	const auto esingle = (width() / float64(inrow));
	const auto fsingle = single + (esingle - single) * value;

	return {
		.single = single,
		.inrow = inrow,
		.fsingle = fsingle,
		.added = (fsingle - single) / 2.,
	};
}

void TopPeersStrip::updateSelected() {
	if (_pressed >= 0 || !_lastMousePosition || _selectionByKeyboard) {
		return;
	}
	const auto p = _strip.mapFromGlobal(*_lastMousePosition);
	const auto expanded = _expanded.current();
	const auto row = expanded ? (p.y() / st::topPeers.height) : 0;
	const auto layout = currentLayout();
	const auto column = (_scrollLeft + p.x()) / layout.fsingle;
	const auto index = row * layout.inrow + int(std::floor(column));
	setSelected((index < 0 || index >= _entries.size()) ? -1 : index);
}

void TopPeersStrip::setSelected(int selected) {
	if (_selected != selected) {
		const auto over = (selected >= 0);
		if (over != (_selected >= 0)) {
			setCursor(over ? style::cur_pointer : style::cur_default);
		}
		_selected = selected;
		update();
	}
}

void TopPeersStrip::scrollToSelected() {
	if (_selected < 0) {
		return;
	} else if (_expanded.current()) {
		const auto layout = currentLayout();
		const auto row = _selected / layout.inrow;
		const auto header = _header.height();
		const auto top = header + row * st::topPeers.height;
		const auto bottom = top + st::topPeers.height;
		_scrollToRequests.fire({ top - (row ? 0 : header), bottom});
	} else {
		const auto single = outer().width();
		const auto left = _selected * single;
		const auto right = left + single;
		if (_scrollLeft > left) {
			_scrollLeft = std::clamp(left, 0, _scrollLeftMax);
		} else if (_scrollLeft + width() < right) {
			_scrollLeft = std::clamp(right - width(), 0, _scrollLeftMax);
		}
		const auto height = _header.height() + st::topPeers.height;
		_scrollToRequests.fire({ 0, height });
	}
}

} // namespace Dialogs
