/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/dialogs_stories_list.h"

#include "lang/lang_keys.h"
#include "ui/widgets/popup_menu.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"

#include <QtWidgets/QApplication>

namespace Dialogs::Stories {
namespace {

constexpr auto kSmallUserpicsShown = 3;
constexpr auto kSmallReadOpacity = 0.6;
constexpr auto kSummaryExpandLeft = 1.5;
constexpr auto kPreloadPages = 2;

[[nodiscard]] int AvailableNameWidth() {
	const auto &full = st::dialogsStoriesFull;
	const auto &font = full.nameStyle.font;
	const auto skip = font->spacew;
	return full.photoLeft * 2 + full.photo - 2 * skip;
}

} // namespace

struct List::Layout {
	int itemsCount = 0;
	int shownHeight = 0;
	float64 ratio = 0.;
	float64 userpicLeft = 0.;
	float64 photoLeft = 0.;
	float64 left = 0.;
	float64 single = 0.;
	int leftFull = 0;
	int leftSmall = 0;
	int singleFull = 0;
	int singleSmall = 0;
	int startIndexSmall = 0;
	int endIndexSmall = 0;
	int startIndexFull = 0;
	int endIndexFull = 0;
};

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
	setMouseTracking(true);
	resize(0, _data.empty() ? 0 : st::dialogsStoriesFull.height);
}

void List::showContent(Content &&content) {
	if (_content == content) {
		return;
	}
	if (content.users.empty()) {
		_hidingData = base::take(_data);
		if (!_hidingData.empty()) {
			toggleAnimated(false);
		}
		return;
	}
	const auto hidden = _content.users.empty();
	_content = std::move(content);
	auto items = base::take(
		_data.items.empty() ? _hidingData.items : _data.items);
	_hidingData = {};
	_data.items.reserve(_content.users.size());
	for (const auto &user : _content.users) {
		const auto i = ranges::find(items, user.id, [](const Item &item) {
			return item.user.id;
		});
		if (i != end(items)) {
			_data.items.push_back(std::move(*i));
			auto &item = _data.items.back();
			if (item.user.userpic != user.userpic) {
				item.user.userpic = user.userpic;
				item.subscribed = false;
			}
			if (item.user.name != user.name) {
				item.user.name = user.name;
				item.nameCache = QImage();
			}
			item.user.unread = user.unread;
			item.user.hidden = user.hidden;
		} else {
			_data.items.emplace_back(Item{ .user = user });
		}
	}
	updateScrollMax();
	updateSummary(_data);
	update();
	if (hidden) {
		toggleAnimated(true);
	}
}

List::Summaries List::ComposeSummaries(Data &data) {
	const auto total = int(data.items.size());
	auto unreadInFirst = 0;
	auto unreadTotal = 0;
	for (auto i = 0; i != total; ++i) {
		if (data.items[i].user.unread) {
			++unreadTotal;
			if (i < kSmallUserpicsShown) {
				++unreadInFirst;
			}
		}
	}
	auto result = Summaries();
	result.total.string
		= tr::lng_stories_row_count(tr::now, lt_count, total);
	const auto append = [&](QString &to, int index, bool last) {
		if (to.isEmpty()) {
			to = data.items[index].user.name;
		} else {
			to = (last
				? tr::lng_stories_row_unread_and_last
				: tr::lng_stories_row_unread_and_one)(
					tr::now,
					lt_accumulated,
					to,
					lt_user,
					data.items[index].user.name);
		}
	};
	if (!total) {
		return result;
	} else if (total <= kSmallUserpicsShown) {
		for (auto i = 0; i != total; ++i) {
			append(result.allNames.string, i, i == total - 1);
		}
	}
	if (unreadInFirst > 0 && unreadInFirst == unreadTotal) {
		for (auto i = 0; i != total; ++i) {
			if (data.items[i].user.unread) {
				append(result.unreadNames.string, i, !--unreadTotal);
			}
		}
	}
	return result;
}

bool List::StringsEqual(const Summaries &a, const Summaries &b) {
	return (a.total.string == b.total.string)
		&& (a.allNames.string == b.allNames.string)
		&& (a.unreadNames.string == b.unreadNames.string);
}

void List::Populate(Summary &summary) {
	if (summary.empty()) {
		return;
	}
	summary.cache = QImage();
	summary.text = Ui::Text::String(
		st::dialogsStories.nameStyle,
		summary.string);
}

void List::Populate(Summaries &summaries) {
	Populate(summaries.total);
	Populate(summaries.allNames);
	Populate(summaries.unreadNames);
}

void List::updateSummary(Data &data) {
	auto summaries = ComposeSummaries(data);
	if (StringsEqual(summaries, data.summaries)) {
		return;
	}
	data.summaries = std::move(summaries);
	Populate(data.summaries);
}

void List::toggleAnimated(bool shown) {
	_shownAnimation.start(
		[=] { updateHeight(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration);
}

void List::updateHeight() {
	const auto shown = _shownAnimation.value(_data.empty() ? 0. : 1.);
	resize(
		width(),
		anim::interpolate(0, st::dialogsStoriesFull.height, shown));
	if (_data.empty() && shown == 0.) {
		_hidingData = {};
	}
}

void List::updateScrollMax() {
	const auto &full = st::dialogsStoriesFull;
	const auto singleFull = full.photoLeft * 2 + full.photo;
	const auto widthFull = full.left + int(_data.items.size()) * singleFull;
	_scrollLeftMax = std::max(widthFull - width(), 0);
	_scrollLeft = std::clamp(_scrollLeft, 0, _scrollLeftMax);
	checkLoadMore();
	update();
}

rpl::producer<uint64> List::clicks() const {
	return _clicks.events();
}

rpl::producer<uint64> List::showProfileRequests() const {
	return _showProfileRequests.events();
}

rpl::producer<ToggleShownRequest> List::toggleShown() const {
	return _toggleShown.events();
}

rpl::producer<> List::expandRequests() const {
	return _expandRequests.events();
}

rpl::producer<> List::entered() const {
	return _entered.events();
}

rpl::producer<> List::loadMoreRequests() const {
	return _loadMoreRequests.events();
}

void List::enterEventHook(QEnterEvent *e) {
	_entered.fire({});
}

void List::resizeEvent(QResizeEvent *e) {
	updateScrollMax();
}

List::Layout List::computeLayout() const {
	const auto &st = st::dialogsStories;
	const auto &full = st::dialogsStoriesFull;
	const auto shownHeight = std::max(_shownHeight(), st.height);
	const auto ratio = float64(shownHeight - st.height)
		/ (full.height - st.height);
	const auto lerp = [&](float64 a, float64 b) {
		return a + (b - a) * ratio;
	};
	auto &rendering = _data.empty() ? _hidingData : _data;
	const auto singleFull = full.photoLeft * 2 + full.photo;
	const auto itemsCount = int(rendering.items.size());
	const auto narrowWidth = st::defaultDialogRow.padding.left()
		+ st::defaultDialogRow.photoSize
		+ st::defaultDialogRow.padding.left();
	const auto narrow = (width() <= narrowWidth);
	const auto smallCount = std::min(kSmallUserpicsShown, itemsCount);
	const auto smallWidth = st.photo + (smallCount - 1) * st.shift;
	const auto leftSmall = narrow
		? ((narrowWidth - smallWidth) / 2 - st.photoLeft)
		: st.left;
	const auto leftFull = (narrow
		? ((narrowWidth - full.photo) / 2 - full.photoLeft)
		: full.left) - _scrollLeft;
	const auto startIndexFull = std::max(-leftFull, 0) / singleFull;
	const auto cellLeftFull = leftFull + (startIndexFull * singleFull);
	const auto endIndexFull = std::min(
		(width() - leftFull + singleFull - 1) / singleFull,
		itemsCount);
	const auto startIndexSmall = 0;
	const auto endIndexSmall = smallCount;
	const auto cellLeftSmall = leftSmall;
	const auto userpicLeftFull = cellLeftFull + full.photoLeft;
	const auto userpicLeftSmall = cellLeftSmall + st.photoLeft;
	const auto userpicLeft = lerp(userpicLeftSmall, userpicLeftFull);
	const auto photoLeft = lerp(st.photoLeft, full.photoLeft);
	return Layout{
		.itemsCount = itemsCount,
		.shownHeight = shownHeight,
		.ratio = ratio,
		.userpicLeft = userpicLeft,
		.photoLeft = photoLeft,
		.left = userpicLeft - photoLeft,
		.single = lerp(st.shift, singleFull),
		.leftFull = leftFull,
		.leftSmall = leftSmall,
		.singleFull = singleFull,
		.singleSmall = st.shift,
		.startIndexSmall = startIndexSmall,
		.endIndexSmall = endIndexSmall,
		.startIndexFull = startIndexFull,
		.endIndexFull = endIndexFull,
	};
}

void List::paintEvent(QPaintEvent *e) {
	const auto &st = st::dialogsStories;
	const auto &full = st::dialogsStoriesFull;
	const auto layout = computeLayout();
	const auto ratio = layout.ratio;
	const auto lerp = [&](float64 a, float64 b) {
		return a + (b - a) * ratio;
	};
	auto &rendering = _data.empty() ? _hidingData : _data;
	const auto line = lerp(st.lineTwice, full.lineTwice) / 2.;
	const auto lineRead = lerp(st.lineReadTwice, full.lineReadTwice) / 2.;
	const auto photoTopSmall = (st.height - st.photo) / 2.;
	const auto photoTop = lerp(photoTopSmall, full.photoTop);
	const auto photo = lerp(st.photo, full.photo);
	const auto summaryTop = st.nameTop
		- (st.photoTop + (st.photo / 2.))
		+ (photoTop + (photo / 2.));
	const auto nameScale = layout.shownHeight / float64(full.height);
	const auto nameTop = nameScale * full.nameTop;
	const auto nameWidth = nameScale * AvailableNameWidth();
	const auto nameHeight = nameScale * full.nameStyle.font->height;
	const auto nameLeft = layout.photoLeft + (photo - nameWidth) / 2.;
	const auto readUserpicOpacity = lerp(kSmallReadOpacity, 1.);
	const auto readUserpicAppearingOpacity = lerp(kSmallReadOpacity, 0.);

	auto p = QPainter(this);
	p.fillRect(e->rect(), st::dialogsBg);
	p.translate(0, height() - layout.shownHeight);

	const auto drawSmall = (ratio < 1.);
	const auto drawFull = (ratio > 0.);
	auto hq = PainterHighQualityEnabler(p);

	const auto count = std::max(
		layout.endIndexFull - layout.startIndexFull,
		layout.endIndexSmall - layout.startIndexSmall);

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
		const auto indexSmall = layout.startIndexSmall + index;
		const auto indexFull = layout.startIndexFull + index;
		const auto small = (drawSmall && indexSmall < layout.endIndexSmall)
			? &rendering.items[indexSmall]
			: nullptr;
		const auto full = (drawFull && indexFull < layout.endIndexFull)
			? &rendering.items[indexFull]
			: nullptr;
		const auto x = layout.left + layout.single * index;
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
		// Name.
		if (const auto full = single.itemFull) {
			p.setOpacity(ratio);
			validateName(full);
			p.drawImage(
				QRectF(single.x + nameLeft, nameTop, nameWidth, nameHeight),
				full->nameCache);
		}

		// Unread gradient.
		const auto x = single.x;
		const auto userpic = QRectF(x + layout.photoLeft, photoTop, photo, photo);
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
		}
		p.setOpacity(1.);
	}, [&](Single single) {
		Expects(single.itemSmall || single.itemFull);

		const auto x = single.x;
		const auto userpic = QRectF(x + layout.photoLeft, photoTop, photo, photo);
		const auto small = single.itemSmall;
		const auto itemFull = single.itemFull;
		const auto smallUnread = small && small->user.unread;
		const auto fullUnread = itemFull && itemFull->user.unread;

		// White circle with possible read gray line.
		const auto hasReadLine = (itemFull && !fullUnread);
		if (hasReadLine) {
			auto color = st::dialogsUnreadBgMuted->c;
			color.setAlphaF(color.alphaF() * ratio);
			auto pen = QPen(color);
			pen.setWidthF(lineRead);
			p.setPen(pen);
		} else {
			p.setPen(Qt::NoPen);
		}
		const auto add = line + (hasReadLine ? (lineRead / 2.) : 0.);
		const auto rect = userpic.marginsAdded({ add, add, add, add });
		p.setBrush(st::dialogsBg);
		p.drawEllipse(rect);

		// Userpic.
		if (itemFull == small) {
			p.setOpacity(smallUnread ? 1. : readUserpicOpacity);
			validateUserpic(itemFull);
			const auto size = full.photo;
			p.drawImage(userpic, itemFull->user.userpic->image(size));
		} else {
			if (small) {
				p.setOpacity(smallUnread
					? (itemFull ? 1. : (1. - ratio))
					: (itemFull
						? kSmallReadOpacity
						: readUserpicAppearingOpacity));
				validateUserpic(small);
				const auto size = (ratio > 0.) ? full.photo : st.photo;
				p.drawImage(userpic, small->user.userpic->image(size));
			}
			if (itemFull) {
				p.setOpacity(ratio);
				validateUserpic(itemFull);
				const auto size = full.photo;
				p.drawImage(userpic, itemFull->user.userpic->image(size));
			}
		}
		p.setOpacity(1.);
	});

	paintSummary(p, rendering, summaryTop, ratio);
}

void List::validateUserpic(not_null<Item*> item) {
	if (!item->subscribed) {
		item->subscribed = true;
		//const auto id = item.user.id;
		item->user.userpic->subscribeToUpdates([=] {
			update();
		});
	}
}

void List::validateName(not_null<Item*> item) {
	const auto &color = st::dialogsNameFg;
	if (!item->nameCache.isNull() && item->nameCacheColor == color->c) {
		return;
	}
	const auto &full = st::dialogsStoriesFull;
	const auto &font = full.nameStyle.font;
	const auto available = AvailableNameWidth();
	const auto text = Ui::Text::String(full.nameStyle, item->user.name);
	const auto ratio = style::DevicePixelRatio();
	item->nameCacheColor = color->c;
	item->nameCache = QImage(
		QSize(available, font->height) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	item->nameCache.setDevicePixelRatio(ratio);
	item->nameCache.fill(Qt::transparent);
	auto p = Painter(&item->nameCache);
	p.setPen(color);
	text.drawElided(p, 0, 0, available, 1, style::al_top);
}

List::Summary &List::ChooseSummary(
		Summaries &summaries,
		int totalItems,
		int fullWidth) {
	const auto &st = st::dialogsStories;
	const auto used = std::min(totalItems, kSmallUserpicsShown);
	const auto taken = st.left
		+ st.photoLeft
		+ st.photo
		+ (used - 1) * st.shift
		+ st.nameLeft
		+ st.nameRight;
	const auto available = fullWidth - taken;
	const auto prepare = [&](Summary &summary) {
		if (!summary.empty() && (summary.text.maxWidth() <= available)) {
			summary.available = available;
			return true;
		}
		return false;
	};
	if (prepare(summaries.unreadNames)) {
		return summaries.unreadNames;
	} else if (prepare(summaries.allNames)) {
		return summaries.allNames;
	}
	prepare(summaries.total);
	return summaries.total;
}

void List::PrerenderSummary(Summary &summary) {
	if (!summary.cache.isNull()
		&& summary.cacheForWidth == summary.available
		&& summary.cacheColor == st::dialogsNameFg->c) {
		return;
	}
	const auto &st = st::dialogsStories;
	const auto use = std::min(summary.text.maxWidth(), summary.available);
	const auto ratio = style::DevicePixelRatio();
	summary.cache = QImage(
		QSize(use, st.nameStyle.font->height) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	summary.cache.setDevicePixelRatio(ratio);
	summary.cache.fill(Qt::transparent);
	auto p = Painter(&summary.cache);
	p.setPen(st::dialogsNameFg);
	summary.text.drawElided(p, 0, 0, summary.available);
}

void List::paintSummary(
		QPainter &p,
		Data &data,
		float64 summaryTop,
		float64 hidden) {
	const auto total = int(data.items.size());
	auto &summary = ChooseSummary(data.summaries, total, width());
	PrerenderSummary(summary);
	const auto lerp = [&](float64 from, float64 to) {
		return from + (to - from) * hidden;
	};
	const auto &st = st::dialogsStories;
	const auto &full = st::dialogsStoriesFull;
	const auto used = std::min(total, kSmallUserpicsShown);
	const auto fullLeft = st.left
		+ st.photoLeft
		+ st.photo
		+ (used - 1) * st.shift
		+ st.nameLeft;
	const auto leftFinal = std::min(
		full.left + (full.photoLeft * 2 + full.photo) * total,
		width()) * kSummaryExpandLeft;
	const auto left = lerp(fullLeft, leftFinal);
	const auto ratio = summary.cache.devicePixelRatio();
	const auto summaryWidth = lerp(summary.cache.width() / ratio, 0.);
	const auto summaryHeight = lerp(summary.cache.height() / ratio, 0.);
	summaryTop += ((summary.cache.height() / ratio) - summaryHeight) / 2.;
	p.setOpacity(1. - hidden);
	p.drawImage(
		QRectF(left, summaryTop, summaryWidth, summaryHeight),
		summary.cache);
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
		updateSelected();
		checkLoadMore();
		update();
	}
	e->accept();
}

void List::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePosition = e->globalPos();
	updateSelected();

	_mouseDownPosition = _lastMousePosition;
	_pressed = _selected;
}

void List::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePosition = e->globalPos();
	updateSelected();

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
			checkLoadMore();
			update();
		}
	}
}

void List::checkLoadMore() {
	if (_scrollLeftMax - _scrollLeft < width() * kPreloadPages) {
		_loadMoreRequests.fire({});
	}
}

void List::mouseReleaseEvent(QMouseEvent *e) {
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
		if (_selected < 0) {
			_expandRequests.fire({});
		} else if (_selected < _data.items.size()) {
			_clicks.fire_copy(_data.items[_selected].user.id);
		}
	}
}

void List::contextMenuEvent(QContextMenuEvent *e) {
	_menu = nullptr;

	if (e->reason() == QContextMenuEvent::Mouse) {
		_lastMousePosition = e->globalPos();
		updateSelected();
	}
	if (_selected < 0 || _data.empty()) {
		return;
	}

	auto &item = _data.items[_selected];
	_menu = base::make_unique_q<Ui::PopupMenu>(this);

	const auto id = item.user.id;
	const auto hidden = item.user.hidden;
	_menu->addAction(tr::lng_context_view_profile(tr::now), [=] {
		_showProfileRequests.fire_copy(id);
	});
	if (!_content.full || hidden) {
		_menu->addAction(hidden
			? tr::lng_stories_show_in_chats(tr::now)
			: tr::lng_stories_hide_to_contacts(tr::now),
			[=] { _toggleShown.fire({ .id = id, .shown = hidden }); });
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
	if (_menu->empty()) {
		_menu = nullptr;
	} else {
		_menu->popup(e->globalPos());
		e->accept();
	}
}

bool List::finishDragging() {
	if (!_dragging) {
		return false;
	}
	checkDragging();
	_dragging = false;
	updateSelected();
	return true;
}

void List::updateSelected() {
	if (_pressed >= 0) {
		return;
	}
	const auto &st = st::dialogsStories;
	const auto &full = st::dialogsStoriesFull;
	const auto p = mapFromGlobal(_lastMousePosition);
	const auto layout = computeLayout();
	const auto firstRightFull = layout.leftFull
		+ (layout.startIndexFull + 1) * layout.singleFull;
	const auto firstRightSmall = layout.leftSmall
		+ st.photoLeft
		+ st.photo;
	const auto lastRightAddFull = 0;
	const auto lastRightAddSmall = st.photoLeft;
	const auto lerp = [&](float64 a, float64 b) {
		return a + (b - a) * layout.ratio;
	};
	const auto firstRight = lerp(firstRightSmall, firstRightFull);
	const auto lastRightAdd = lerp(lastRightAddSmall, lastRightAddFull);
	const auto activateFull = (layout.ratio >= 0.5);
	const auto startIndex = activateFull
		? layout.startIndexFull
		: layout.startIndexSmall;
	const auto endIndex = activateFull
		? layout.endIndexFull
		: layout.endIndexSmall;
	const auto x = p.x();
	const auto infiniteIndex = (x < firstRight)
		? 0
		: int(std::floor(((x - firstRight) / layout.single) + 1));
	const auto index = (endIndex == startIndex)
		? -1
		: (infiniteIndex == endIndex - startIndex
			&& x < firstRight
				+ (endIndex - startIndex - 1) * layout.single
				+ lastRightAdd)
		? (infiniteIndex - 1) // Last small part should still be clickable.
		: (startIndex + infiniteIndex >= endIndex)
		? -1
		: infiniteIndex;
	const auto selected = (index < 0
		|| startIndex + index >= layout.itemsCount)
		? -1
		: (startIndex + index);
	if (_selected != selected) {
		const auto over = (selected >= 0);
		if (over != (_selected >= 0)) {
			setCursor(over ? style::cur_pointer : style::cur_default);
		}
		_selected = selected;
	}
}

} // namespace Dialogs::Stories
