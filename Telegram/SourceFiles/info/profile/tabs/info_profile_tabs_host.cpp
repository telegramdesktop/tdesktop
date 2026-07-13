/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/info_profile_tabs_host.h"

#include "info/profile/tabs/info_profile_tabs_strip.h"
#include "base/options.h"
#include "ui/widgets/scroll_area.h"
#include "ui/effects/slide_animation.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_basic.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

base::options::toggle ProfileMediaTabs({
	.id = kOptionProfileMediaTabs,
	.name = "Show shared media as tabs in the profile.",
	.description = "Replace the shared media buttons in profiles with "
		"a strip of tabs holding the media lists inline. Work in progress.",
});

} // namespace

const char kOptionProfileMediaTabs[] = "profile-media-tabs";

bool UseProfileMediaTabs() {
	return ProfileMediaTabs.value();
}

TabsHost::TabsHost(not_null<QWidget*> parent, Descriptor descriptor)
: RpWidget(parent)
, _context(descriptor.context)
, _tabs(std::move(descriptor.tabs))
, _strip(Ui::CreateChild<TabsStrip>(this, st::infoProfileTabsStrip))
, _stripWeak(_strip)
, _body(Ui::CreateChild<Ui::RpWidget>(this)) {
	_strip->show();
	_body->show();
	if (_tabs.empty()) {
		return;
	}
	_stripTitles.assign(_tabs.size(), QString());
	_tabsShown.assign(_tabs.size(), false);
	wireStripTitles();
	wireTabsVisibility();
	_strip->activated(
	) | rpl::on_next([this](const QString &id) {
		_userChosenTab = true;
		_pendingRestoreId = QString();
		if (id == _activeId) {
			// The viewport filler grows after the first scroll collapses
			// the cover, so settle with a second queued pass.
			const auto fire = [this] {
				_scrollToRequests.fire({ 0, -1 });
			};
			fire();
			InvokeQueued(this, fire);
		} else {
			activateTab(id);
		}
	}, lifetime());

	_body->heightValue(
	) | rpl::on_next([this](int) {
		scheduleHeightSync();
	}, _body->lifetime());
	rpl::merge(
		_strip->heightValue() | rpl::to_empty,
		_strip->naturalWidthValue() | rpl::to_empty
	) | rpl::on_next([this] {
		scheduleHeightSync();
	}, lifetime());
}

void TabsHost::scheduleBodySync() {
	if (_bodySyncQueued) {
		return;
	}
	_bodySyncQueued = true;
	InvokeQueued(this, [=] {
		if (_bodySyncQueued) {
			syncBodyNow();
		}
	});
}

void TabsHost::syncBodyNow() {
	_bodySyncQueued = false;
	const auto active = _activeTab.current();
	if (!active) {
		return;
	}
	const auto widget = active->widget();
	if (!widget->isHidden() && _body->height() != widget->height()) {
		_body->resize(_body->width(), widget->height());
	}
}

void TabsHost::scheduleHeightSync() {
	if (_heightSyncQueued) {
		return;
	}
	_heightSyncQueued = true;
	InvokeQueued(this, [=] {
		if (_heightSyncQueued) {
			syncHeightNow();
		}
	});
}

void TabsHost::syncHeightNow() {
	_heightSyncQueued = false;
	resizeToWidth(width());
}

TabsHost::~TabsHost() {
	// Lists notify their delegates while being destroyed, so the tab
	// widgets must die before the adapters owning those delegates.
	delete base::take(_body);
}

void TabsHost::wireStripTitles() {
	for (auto i = 0; i != int(_tabs.size()); ++i) {
		rpl::duplicate(
			_tabs[i].title
		) | rpl::on_next([this, i](QString text) {
			if (_stripTitles[i] != text) {
				_stripTitles[i] = std::move(text);
				syncStripTitles();
			}
		}, lifetime());
	}
}

void TabsHost::wireTabsVisibility() {
	for (auto i = 0; i != int(_tabs.size()); ++i) {
		rpl::duplicate(
			_tabs[i].shown
		) | rpl::on_next([this, i](bool shown) {
			if (_tabsShown[i] != shown) {
				_tabsShown[i] = shown;
				syncStripTitles();
				if (shown
					&& !_pendingRestoreId.isEmpty()
					&& (_tabs[i].id == _pendingRestoreId)) {
					restoreActiveTab(base::take(_pendingRestoreId));
				}
				ensureActiveVisible();
				scheduleHeightSync();
			}
		}, lifetime());
	}
}

void TabsHost::syncStripTitles() {
	auto stripTabs = std::vector<StripTab>();
	stripTabs.reserve(_tabs.size());
	for (auto i = 0; i != int(_tabs.size()); ++i) {
		if (!_tabsShown[i]) {
			continue;
		}
		stripTabs.push_back({
			.id = _tabs[i].id,
			.text = _stripTitles[i],
		});
	}
	_strip->setTabs(std::move(stripTabs));
	if (!_activeId.isEmpty()) {
		const auto it = ranges::find(
			_tabs,
			_activeId,
			&MediaTabDescriptor::id);
		if (it != end(_tabs) && _tabsShown[it - begin(_tabs)]) {
			_strip->setActiveTab(_activeId);
		}
	}
}

void TabsHost::ensureActiveVisible() {
	const auto firstVisible = [&] {
		for (auto i = 0; i != int(_tabs.size()); ++i) {
			if (_tabsShown[i]) {
				return i;
			}
		}
		return -1;
	}();
	const auto activeIndex = _activeId.isEmpty()
		? -1
		: int(ranges::find(_tabs, _activeId, &MediaTabDescriptor::id)
			- begin(_tabs));
	const auto activeVisible = (activeIndex >= 0)
		&& (activeIndex < int(_tabs.size()))
		&& _tabsShown[activeIndex];
	if (activeVisible
		&& (_userChosenTab || activeIndex == firstVisible)) {
		return;
	}
	if (firstVisible >= 0) {
		if (activeIndex != firstVisible) {
			activateTab(_tabs[firstVisible].id);
		}
		return;
	}
	if (!_activeId.isEmpty()) {
		if (const auto previous = _contents[_activeId].get()) {
			previous->deactivated();
			previous->widget()->hide();
		}
		_activeId = QString();
		_activeTab = nullptr;
	}
}

void TabsHost::restoreActiveTab(const QString &id) {
	const auto it = ranges::find(_tabs, id, &MediaTabDescriptor::id);
	if (it == end(_tabs)) {
		return;
	} else if (_tabsShown[it - begin(_tabs)]) {
		_userChosenTab = true;
		activateTab(id, false);
	} else {
		_pendingRestoreId = id;
	}
}

QRect TabsHost::bodyGeometry() const {
	return _body->geometry();
}

Fn<void()> TabsHost::prepareSwitch(bool toNextTab) {
	if (_activeId.isEmpty()) {
		return nullptr;
	}
	const auto active = int(ranges::find(
		_tabs,
		_activeId,
		&MediaTabDescriptor::id) - begin(_tabs));
	if (active >= int(_tabs.size())) {
		return nullptr;
	}
	const auto delta = toNextTab ? 1 : -1;
	for (auto i = active + delta
		; i >= 0 && i < int(_tabs.size())
		; i += delta) {
		if (!_tabsShown[i]) {
			continue;
		}
		return crl::guard(this, [=, id = _tabs[i].id] {
			_userChosenTab = true;
			_pendingRestoreId = QString();
			activateTab(id);
		});
	}
	return nullptr;
}

void TabsHost::activateTab(const QString &id, bool animated) {
	if (_activeId == id) {
		_strip->setActiveTab(id);
		return;
	}
	const auto it = ranges::find(_tabs, id, &MediaTabDescriptor::id);
	if (it == end(_tabs) || !_tabsShown[it - begin(_tabs)]) {
		return;
	}
	_strip->setActiveTab(id);

	auto &cached = _contents[id];
	const auto created = !cached;
	if (created) {
		auto context = _context;
		context.parent = _body;
		context.scrollToRequest = [weak = base::make_weak(this), id](
				int ymin,
				int ymax) {
			const auto that = weak.get();
			if (!that) {
				return;
			}
			const auto i = that->_contents.find(id);
			if (i == end(that->_contents)) {
				return;
			}
			// Flush the queued height coalescers, otherwise the exact
			// scroll below clamps to a stale scroll range.
			that->syncBodyNow();
			that->syncHeightNow();
			const auto widget = i->second->widget();
			const auto top = Ui::MapFrom(
				that,
				widget.get(),
				QPoint(0, ymin)).y();
			that->_scrollToRequests.fire({
				top,
				(ymax < 0) ? -1 : (top + (ymax - ymin)),
			});
		};
		cached = it->factory(std::move(context));
	}
	const auto previousId = _activeId;
	const auto previousIndex = previousId.isEmpty()
		? -1
		: int(ranges::find(_tabs, previousId, &MediaTabDescriptor::id)
			- begin(_tabs));
	_activeId = id;

	const auto previous = previousId.isEmpty()
		? nullptr
		: _contents[previousId].get();
	if (previous) {
		if (_visibleTop >= 0) {
			_tabScrollTops[previousId] = _visibleTop;
		} else {
			_tabScrollTops.remove(previousId);
		}
		_keepMinHeight = std::max(_visibleBottom, 0);
		previous->deactivated();
	}

	const auto active = cached.get();
	const auto widget = active->widget();
	const auto raw = widget.get();
	if (created) {
		if (raw->parentWidget() != _body) {
			raw->setParent(_body);
		}
		_body->widthValue(
		) | rpl::on_next([raw](int newWidth) {
			if (!raw->isHidden()) {
				raw->resizeToWidth(newWidth);
			}
		}, raw->lifetime());
		raw->heightValue(
		) | rpl::on_next([this, raw](int) {
			if (!raw->isHidden()) {
				scheduleBodySync();
			}
		}, raw->lifetime());
	}
	raw->show();
	raw->resizeToWidth(_body->width());
	_body->resize(_body->width(), raw->height());

	_activeTab = active;
	pushViewportToActive();

	if (previous) {
		auto wasCache = QPixmap();
		if (animated
			&& _visibleBottom > _visibleTop
			&& _body->width() > 0) {
			const auto part = bodyVisibleRect().intersected(
				previous->widget()->rect());
			if (!part.isEmpty()) {
				wasCache = Ui::GrabWidget(previous->widget(), part);
			}
		}
		if (_visibleTop >= 0) {
			const auto i = _tabScrollTops.find(id);
			const auto restore = (i != end(_tabScrollTops))
				? i->second
				: 0;
			if (restore != _visibleTop) {
				const auto fire = [this, restore] {
					_scrollToRequests.fire({ restore, -1 });
				};
				fire();
				InvokeQueued(this, fire);
			}
		}
		if (animated) {
			startSlideAnimation(
				std::move(wasCache),
				active,
				previousIndex > int(it - begin(_tabs)));
		}
		previous->widget()->hide();
	}
}

QRect TabsHost::bodyVisibleRect() const {
	const auto bodyTop = _stripHeight;
	const auto top = std::max(_visibleTop - bodyTop, 0);
	const auto bottom = std::min(
		_visibleBottom - bodyTop,
		std::max(_body->height(), height() - bodyTop));
	return QRect(
		0,
		top,
		_body->width(),
		std::max(bottom - top, 1));
}

void TabsHost::startSlideAnimation(
		QPixmap wasCache,
		not_null<MediaTabContent*> now,
		bool slideLeft) {
	if (_visibleBottom <= _visibleTop || _body->width() <= 0) {
		return;
	}
	const auto rect = bodyVisibleRect();
	const auto part = rect.intersected(now->widget()->rect());
	auto nowCache = part.isEmpty()
		? QPixmap()
		: Ui::GrabWidget(now->widget(), part);
	if (wasCache.isNull() || nowCache.isNull()) {
		return;
	}
	_slideRect = rect.translated(0, _stripHeight);
	_slideAnimation = std::make_unique<Ui::SlideAnimation>();
	_slideAnimation->setSnapshots(
		std::move(wasCache),
		std::move(nowCache));
	_slideAnimation->start(slideLeft, [=] {
		if (_slideAnimation && !_slideAnimation->animating()) {
			_slideAnimation = nullptr;
			_body->show();
		}
		update();
	}, st::slideDuration);
	_body->hide();
	update();
}

void TabsHost::paintEvent(QPaintEvent *e) {
	if (!_slideAnimation) {
		return;
	}
	auto p = QPainter(this);
	p.fillRect(_slideRect, st::windowBg);
	_slideAnimation->paintFrame(
		p,
		_slideRect.x(),
		_slideRect.y(),
		width());
}

void TabsHost::pushViewportToActive() {
	if (const auto active = _activeTab.current()) {
		active->setTopOverlay((_visibleTop >= 0) ? _stripHeight : 0);
		active->setVisibleRegion(
			_visibleTop - _stripHeight,
			_visibleBottom - _stripHeight);
	}
}

rpl::producer<MediaTabContent*> TabsHost::activeTabValue() const {
	return _activeTab.value();
}

rpl::producer<Ui::ScrollToRequest> TabsHost::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<TabTopBarBindings> TabsHost::activeTabBindings() const {
	return _activeTab.value(
	) | rpl::map([](MediaTabContent *tab) {
		return tab ? tab->topBarBindings() : TabTopBarBindings();
	});
}

not_null<Ui::RpWidget*> TabsHost::stripWidget() const {
	return _strip;
}

void TabsHost::returnStrip() {
	const auto strip = _stripWeak.get();
	if (!strip) {
		return;
	}
	if (strip->parentWidget() != this) {
		strip->setParent(this);
		strip->show();
	}
	resizeToWidth(width());
}

void TabsHost::setVisibleRegion(int top, int bottom) {
	if (_visibleTop == top && _visibleBottom == bottom) {
		return;
	}
	const auto heightChanged = ((_visibleBottom - _visibleTop)
		!= (bottom - top));
	_visibleTop = top;
	_visibleBottom = bottom;
	pushViewportToActive();
	if (heightChanged) {
		const auto want = std::max(
			_stripHeight + _body->height(),
			bottom - top);
		if (want != height()) {
			scheduleHeightSync();
		}
	}
}

int TabsHost::resizeGetHeight(int newWidth) {
	if (!ranges::contains(_tabsShown, true)) {
		return 0;
	}
	if (const auto strip = _stripWeak.get()
		; strip && strip->parentWidget() == this) {
		const auto stripWidth = std::min(strip->naturalWidth(), newWidth);
		strip->resizeToWidth(stripWidth);
		strip->moveToLeft((newWidth - stripWidth) / 2, 0);
		_stripHeight = strip->height();
	}
	const auto bodyTop = _stripHeight;
	_body->resizeToWidth(std::max(newWidth, 1));
	_body->moveToLeft(0, bodyTop);

	const auto natural = bodyTop + _body->height();
	const auto viewport = _visibleBottom - _visibleTop;
	if (_keepMinHeight
		&& ((natural >= _keepMinHeight)
			|| (_visibleBottom <= std::max(natural, viewport)))) {
		_keepMinHeight = 0;
	}
	return std::max({ natural, viewport, _keepMinHeight });
}

} // namespace Info::Profile
