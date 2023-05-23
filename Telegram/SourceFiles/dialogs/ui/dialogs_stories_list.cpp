/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_stories_list.h"

#include "ui/painter.h"
#include "styles/style_dialogs.h"

#include <QtWidgets/QApplication>

namespace Dialogs::Stories {
namespace {

constexpr auto kSmallUserpicsShown = 3;
constexpr auto kSmallReadOpacity = 0.6;

} // namespace

List::List(
	not_null<QWidget*> parent,
	rpl::producer<Content> content,
	Fn<int()> shownHeight)
: RpWidget(parent)
, _shownHeight(shownHeight) {
	setCursor(style::cur_default);

	std::move(content) | rpl::start_with_next([=](Content &&content) {
		showContent(std::move(content));
	}, lifetime());

	_shownAnimation.stop();
	resize(0, _items.empty() ? 0 : st::dialogsStoriesFull.height);
}

void List::showContent(Content &&content) {
	if (_content == content) {
		return;
	}
	if (content.users.empty()) {
		_hidingItems = base::take(_items);
		if (!_hidingItems.empty()) {
			toggleAnimated(false);
		}
		return;
	}
	const auto hidden = _content.users.empty();
	_content = std::move(content);
	auto items = base::take(_items.empty() ? _hidingItems : _items);
	_hidingItems.clear();
	_items.reserve(_content.users.size());
	for (const auto &user : _content.users) {
		const auto i = ranges::find(items, user.id, [](const Item &item) {
			return item.user.id;
		});
		if (i != end(items)) {
			_items.push_back(std::move(*i));
			auto &item = _items.back();
			if (item.user.userpic != user.userpic) {
				item.user.userpic = user.userpic;
				item.subscribed = false;
			}
			if (item.user.name != user.name) {
				item.user.name = user.name;
				item.nameCache = QImage();
			}
			item.user.unread = user.unread;
		} else {
			_items.emplace_back(Item{ .user = user });
		}
	}
	updateScrollMax();
	update();
	if (hidden) {
		toggleAnimated(true);
	}
}

void List::toggleAnimated(bool shown) {
	_shownAnimation.start(
		[=] { updateHeight(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration);
}

void List::updateHeight() {
	const auto shown = _shownAnimation.value(_items.empty() ? 0. : 1.);
	resize(
		width(),
		anim::interpolate(0, st::dialogsStoriesFull.height, shown));
}

void List::updateScrollMax() {
	const auto &full = st::dialogsStoriesFull;
	const auto singleFull = full.photoLeft * 2 + full.photo;
	const auto widthFull = full.left + int(_items.size()) * singleFull;
	_scrollLeftMax = std::max(widthFull - width(), 0);
	_scrollLeft = std::clamp(_scrollLeft, 0, _scrollLeftMax);
	update();
}

rpl::producer<uint64> List::clicks() const {
	return _clicks.events();
}

rpl::producer<> List::expandRequests() const {
	return _expandRequests.events();
}

rpl::producer<> List::entered() const {
	return _entered.events();
}

void List::enterEventHook(QEnterEvent *e) {
	_entered.fire({});
}

void List::resizeEvent(QResizeEvent *e) {
	updateScrollMax();
}

void List::paintEvent(QPaintEvent *e) {
	const auto &st = st::dialogsStories;
	const auto &full = st::dialogsStoriesFull;
	const auto shownHeight = std::max(_shownHeight(), st.height);
	const auto ratio = float64(shownHeight - st.height)
		/ (full.height - st.height);
	const auto lerp = [=](float64 a, float64 b) {
		return a + (b - a) * ratio;
	};
	const auto photo = lerp(st.photo, full.photo);
	const auto photoTopSmall = (st.height - st.photo) / 2.;
	const auto photoTop = lerp(photoTopSmall, full.photoTop);
	const auto line = lerp(st.lineTwice, full.lineTwice) / 2.;
	const auto lineRead = lerp(st.lineReadTwice, full.lineReadTwice) / 2.;
	const auto nameTop = (photoTop + photo)
		* (full.nameTop / float64(full.photoTop + full.photo));
	const auto infoTop = st.nameTop
		- (st.photoTop + (st.photo / 2.))
		+ (photoTop + (photo / 2.));
	const auto singleSmall = st.shift;
	const auto singleFull = full.photoLeft * 2 + full.photo;
	const auto single = lerp(singleSmall, singleFull);
	const auto itemsCount = int(_items.size());
	const auto leftSmall = st.left;
	const auto leftFull = full.left - _scrollLeft;
	const auto startIndexFull = std::max(-leftFull, 0) / singleFull;
	const auto cellLeftFull = leftFull + (startIndexFull * singleFull);
	const auto endIndexFull = std::min(
		(width() - leftFull + singleFull - 1) / singleFull,
		itemsCount);
	const auto startIndexSmall = 0;
	const auto endIndexSmall = std::min(kSmallUserpicsShown, itemsCount);
	const auto cellLeftSmall = leftSmall;
	const auto userpicLeftFull = cellLeftFull + full.photoLeft;
	const auto userpicLeftSmall = cellLeftSmall + st.photoLeft;
	const auto userpicLeft = lerp(userpicLeftSmall, userpicLeftFull);
	const auto photoLeft = lerp(st.photoLeft, full.photoLeft);
	const auto left = userpicLeft - photoLeft;
	const auto readUserpicOpacity = lerp(kSmallReadOpacity, 1.);
	const auto readUserpicAppearingOpacity = lerp(kSmallReadOpacity, 0.);

	auto p = QPainter(this);
	p.fillRect(e->rect(), st::dialogsBg);
	p.translate(0, height() - shownHeight);

	const auto drawSmall = (ratio < 1.);
	const auto drawFull = (ratio > 0.);
	auto hq = PainterHighQualityEnabler(p);

	const auto subscribe = [&](not_null<Item*> item) {
		if (!item->subscribed) {
			item->subscribed = true;
			//const auto id = item.user.id;
			item->user.userpic->subscribeToUpdates([=] {
				update();
			});
		}
	};
	const auto count = std::max(
		endIndexFull - startIndexFull,
		endIndexSmall - startIndexSmall);

	struct Single {
		float64 x = 0.;
		int indexSmall = 0;
		Item *itemSmall = nullptr;
		int indexFull = 0;
		Item *itemFull = nullptr;

		explicit operator bool() const {
			return itemSmall || itemFull;
		}
	};
	const auto lookup = [&](int index) {
		const auto indexSmall = startIndexSmall + index;
		const auto indexFull = startIndexFull + index;
		const auto small = (drawSmall && indexSmall < endIndexSmall)
			? &_items[indexSmall]
			: nullptr;
		const auto full = (drawFull && indexFull < endIndexFull)
			? &_items[indexFull]
			: nullptr;
		const auto x = left + single * index;
		return Single{ x, indexSmall, small, indexFull, full };
	};
	const auto hasUnread = [&](const Single &single) {
		return (single.itemSmall && single.itemSmall->user.unread)
			|| (single.itemFull && single.itemFull->user.unread);
	};
	const auto enumerate = [&](auto &&paintGradient, auto &&paintOther) {
		auto nextGradientPainted = false;
		for (auto i = count; i != 0;) {
			--i;
			const auto gradientPainted = nextGradientPainted;
			nextGradientPainted = false;
			if (const auto current = lookup(i)) {
				if (!gradientPainted) {
					paintGradient(current);
				}
				if (i > 0 && hasUnread(current)) {
					if (const auto next = lookup(i - 1)) {
						if (current.itemSmall || !next.itemSmall) {
							nextGradientPainted = true;
							paintGradient(next);
						}
					}
				}
				paintOther(current);
			}
		}
	};
	enumerate([&](Single single) {
		// Unread gradient.
		const auto x = single.x;
		const auto userpic = QRectF(x + photoLeft, photoTop, photo, photo);
		const auto small = single.itemSmall;
		const auto itemFull = single.itemFull;
		const auto smallUnread = small && small->user.unread;
		const auto fullUnread = itemFull && itemFull->user.unread;
		const auto unreadOpacity = (smallUnread && fullUnread)
			? 1.
			: smallUnread
			? (1. - ratio)
			: fullUnread
			? ratio
			: 0.;
		if (unreadOpacity > 0.) {
			p.setOpacity(unreadOpacity);
			const auto outerAdd = 2 * line;
			const auto outer = userpic.marginsAdded(
				{ outerAdd, outerAdd, outerAdd, outerAdd });
			p.setPen(Qt::NoPen);
			auto gradient = QLinearGradient(
				userpic.topRight(),
				userpic.bottomLeft());
			gradient.setStops({
				{ 0., st::groupCallLive1->c },
				{ 1., st::groupCallMuted1->c },
			});
			p.setBrush(gradient);
			p.drawEllipse(outer);
			p.setOpacity(1.);
		}
	}, [&](Single single) {
		Expects(single.itemSmall || single.itemFull);

		const auto x = single.x;
		const auto userpic = QRectF(x + photoLeft, photoTop, photo, photo);
		const auto small = single.itemSmall;
		const auto itemFull = single.itemFull;
		const auto smallUnread = small && small->user.unread;
		const auto fullUnread = itemFull && itemFull->user.unread;

		// White circle with possible read gray line.
		if (itemFull && !fullUnread) {
			auto color = st::dialogsUnreadBgMuted->c;
			color.setAlphaF(color.alphaF() * ratio);
			auto pen = QPen(color);
			pen.setWidthF(lineRead);
			p.setPen(pen);
		} else {
			p.setPen(Qt::NoPen);
		}
		const auto add = line + (itemFull ? (lineRead / 2.) : 0.);
		const auto rect = userpic.marginsAdded({ add, add, add, add });
		p.setBrush(st::dialogsBg);
		p.drawEllipse(rect);

		// Userpic.
		if (itemFull == small) {
			p.setOpacity(smallUnread ? 1. : readUserpicOpacity);
			subscribe(itemFull);
			const auto size = full.photo;
			p.drawImage(userpic, itemFull->user.userpic->image(size));
		} else {
			if (small) {
				p.setOpacity(smallUnread
					? (itemFull ? 1. : (1. - ratio))
					: (itemFull
						? kSmallReadOpacity
						: readUserpicAppearingOpacity));
				subscribe(small);
				const auto size = (ratio > 0.) ? full.photo : st.photo;
				p.drawImage(userpic, small->user.userpic->image(size));
			}
			if (itemFull) {
				p.setOpacity(ratio);
				subscribe(itemFull);
				const auto size = full.photo;
				p.drawImage(userpic, itemFull->user.userpic->image(size));
			}
		}
		p.setOpacity(1.);
	});
}

void List::wheelEvent(QWheelEvent *e) {
	const auto horizontal = (e->angleDelta().x() != 0);
	if (!horizontal) {
		e->ignore();
		return;
	}
	auto delta = horizontal
		? ((style::RightToLeft() ? -1 : 1) * (e->pixelDelta().x()
			? e->pixelDelta().x()
			: e->angleDelta().x()))
		: (e->pixelDelta().y()
			? e->pixelDelta().y()
			: e->angleDelta().y());

	const auto now = _scrollLeft;
	const auto used = now - delta;
	const auto next = std::clamp(used, 0, _scrollLeftMax);
	if (next != now) {
		_expandRequests.fire({});
		_scrollLeft = next;
		//updateSelected();
		update();
	}
	e->accept();
}

void List::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_mouseDownPosition = _lastMousePosition = e->globalPos();
	//updateSelected();
}

void List::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePosition = e->globalPos();
	//updateSelected();

	if (!_dragging && _mouseDownPosition) {
		if ((_lastMousePosition - *_mouseDownPosition).manhattanLength()
			>= QApplication::startDragDistance()) {
			if (_shownHeight() < st::dialogsStoriesFull.height) {
				_expandRequests.fire({});
			}
			_dragging = true;
			_startDraggingLeft = _scrollLeft;
		}
	}
	checkDragging();
}

void List::checkDragging() {
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

void List::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePosition = e->globalPos();
	const auto guard = gsl::finally([&] {
		_mouseDownPosition = std::nullopt;
	});

	//const auto wasDown = std::exchange(_pressed, SpecialOver::None);
	if (finishDragging()) {
		return;
	}
	//updateSelected();
}

bool List::finishDragging() {
	if (!_dragging) {
		return false;
	}
	checkDragging();
	_dragging = false;
	//updateSelected();
	return true;
}

} // namespace Dialogs::Stories
