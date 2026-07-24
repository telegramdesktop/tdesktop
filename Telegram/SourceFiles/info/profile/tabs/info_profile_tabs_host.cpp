/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/info_profile_tabs_host.h"

#include "info/profile/tabs/info_profile_tabs_strip.h"
#include "info/profile/tabs/adapters/info_profile_tab_media.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/options.h"
#include "core/ui_integration.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_sublist.h"
#include "info/info_controller.h"
#include "info/media/info_media_buttons.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/effects/slide_animation.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

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
	_strip->setTextContext(Core::TextContext({
		.session = &_context.peer->session(),
		.customEmojiLoopLimit = 1,
	}));
	_strip->show();
	_body->show();
	if (_tabs.empty()) {
		return;
	}
	_stripTitles.assign(_tabs.size(), TextWithEntities());
	_tabsShown.assign(_tabs.size(), false);
	refreshOrder();
	wireStripTitles();
	wireTabsVisibility();
	wireMainTab();
	_strip->contextMenuRequests(
	) | rpl::on_next([this](const QString &id) {
		showTabMenu(id);
	}, lifetime());
	_strip->activated(
	) | rpl::on_next([this](const QString &id) {
		_userChosenTab = true;
		_pendingRestoreId = QString();
		if (id == _activeId) {
			// The scroll range changes while the cover collapses,
			// so settle the exact position with a second queued pass.
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
		) | rpl::on_next([this, i](TextWithEntities text) {
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
				refreshOrder();
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

void TabsHost::wireMainTab() {
	const auto peer = _context.peer;
	peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::MainProfileTab
	) | rpl::on_next([this] {
		if (refreshOrder()) {
			syncStripTitles();
			ensureActiveVisible();
		}
	}, lifetime());
}

bool TabsHost::refreshOrder() {
	const auto main = _context.peer->mainProfileTab();
	const auto mainTabIndex = [&] {
		if (main == Data::ProfileTab::None) {
			return -1;
		}
		auto fallback = -1;
		for (auto i = 0; i != int(_tabs.size()); ++i) {
			if (_tabs[i].profileTab != main) {
				continue;
			} else if (_tabsShown[i]) {
				return i;
			} else if (fallback < 0) {
				fallback = i;
			}
		}
		return fallback;
	}();
	if (!_order.empty() && _mainTabIndex == mainTabIndex) {
		return false;
	}
	_mainTabIndex = mainTabIndex;
	_order.resize(_tabs.size());
	for (auto index = 0; index != int(_order.size()); ++index) {
		_order[index] = index;
	}
	if (mainTabIndex > 0) {
		std::rotate(
			begin(_order),
			begin(_order) + mainTabIndex,
			begin(_order) + mainTabIndex + 1);
	}
	return true;
}

int TabsHost::displayPosition(int index) const {
	return int(ranges::find(_order, index) - begin(_order));
}

int TabsHost::firstVisibleIndex() const {
	for (const auto i : _order) {
		if (_tabsShown[i]) {
			return i;
		}
	}
	return -1;
}

bool TabsHost::canSetMainTab(Data::ProfileTab tab) const {
	if (tab == Data::ProfileTab::None || _context.topic || _context.sublist) {
		return false;
	} else if (const auto channel = _context.peer->asBroadcast()) {
		return channel->canEditInformation();
	}
	return _context.peer->isSelf()
		&& ((tab == Data::ProfileTab::Posts)
			|| (tab == Data::ProfileTab::Gifts));
}

Fn<void()> TabsHost::openInWindowFor(const MediaTabDescriptor &tab) const {
	if (!tab.sharedMediaType) {
		return nullptr;
	}
	const auto peer = _context.sublist
		? _context.sublist->sublistPeer()
		: _context.peer;
	const auto separateId = Media::SeparateId(
		peer,
		_context.topic ? _context.topic->rootId() : MsgId(),
		*tab.sharedMediaType);
	if (!separateId) {
		return nullptr;
	}
	const auto window = _context.controller->parentController();
	return [=] { window->showInNewWindow(separateId); };
}

void TabsHost::showTabMenu(const QString &id) {
	using Type = Storage::SharedMediaType;

	const auto strip = _stripWeak.get();
	const auto i = ranges::find(_tabs, id, &MediaTabDescriptor::id);
	if (!strip || i == end(_tabs)) {
		return;
	}
	const auto tab = i->profileTab;
	const auto index = int(i - begin(_tabs));
	const auto mediaType = i->sharedMediaType;
	const auto expand = (mediaType == Type::PhotoVideo);
	const auto collapse = (mediaType == Type::Photo)
		|| (mediaType == Type::Video);
	const auto setAsMain = canSetMainTab(tab)
		&& (index != firstVisibleIndex());
	const auto openInWindow = openInWindowFor(*i);
	if (!expand && !collapse && !setAsMain && !openInWindow) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		strip,
		st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
	if (expand || collapse) {
		addAction(
			(expand
				? tr::lng_context_archive_expand(tr::now)
				: tr::lng_context_archive_collapse(tr::now)),
			[=] { SetMediaTabsExpanded(expand); },
			(expand ? &st::menuIconExpand : &st::menuIconCollapse));
	}
	if (setAsMain) {
		addAction(
			tr::lng_profile_tab_set_as_main(tr::now),
			crl::guard(this, [=] { setMainTab(tab); }),
			&st::menuIconReorder);
	}
	if (openInWindow) {
		addAction(
			tr::lng_context_new_window(tr::now),
			crl::guard(this, [=] {
				base::call_delayed(
					st::popupMenuWithIcons.showDuration,
					crl::guard(this, openInWindow));
			}),
			&st::menuIconNewWindow);
	}
	_menu->popup(QCursor::pos());
}

QString TabsHost::mediaTabShownId(Storage::SharedMediaType type) const {
	const auto i = ranges::find(
		_tabs,
		std::optional<Storage::SharedMediaType>(type),
		&MediaTabDescriptor::sharedMediaType);
	return ((i != end(_tabs)) && _tabsShown[i - begin(_tabs)])
		? i->id
		: QString();
}

std::optional<Storage::SharedMediaType> TabsHost::activeMediaType() const {
	const auto i = ranges::find(_tabs, _activeId, &MediaTabDescriptor::id);
	return (i != end(_tabs))
		? i->sharedMediaType
		: std::nullopt;
}

QString TabsHost::mediaSplitCounterpart() const {
	using Type = Storage::SharedMediaType;

	const auto active = activeMediaType();
	if (!active) {
		return QString();
	} else if (*active == Type::PhotoVideo) {
		const auto photos = mediaTabShownId(Type::Photo);
		return photos.isEmpty() ? mediaTabShownId(Type::Video) : photos;
	} else if (*active == Type::Photo || *active == Type::Video) {
		return mediaTabShownId(Type::PhotoVideo);
	}
	return QString();
}

bool TabsHost::mediaSplitSwitching() const {
	using Type = Storage::SharedMediaType;

	const auto active = activeMediaType();
	if (!active) {
		return false;
	} else if (*active == Type::PhotoVideo) {
		return MediaTabsExpanded();
	} else if (*active == Type::Photo || *active == Type::Video) {
		return !MediaTabsExpanded();
	}
	return false;
}

void TabsHost::setMainTab(Data::ProfileTab tab) {
	const auto peer = _context.peer;
	const auto sending = Data::ProfileTabToMTP(tab);
	if (const auto channel = peer->asChannel()) {
		peer->session().api().request(MTPchannels_SetMainProfileTab(
			channel->inputChannel(),
			sending
		)).send();
	} else {
		peer->session().api().request(
			MTPaccount_SetMainProfileTab(sending)
		).send();
	}
	peer->setMainProfileTab(tab);
}

void TabsHost::syncStripTitles() {
	auto stripTabs = std::vector<StripTab>();
	stripTabs.reserve(_tabs.size());
	for (const auto i : _order) {
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
	const auto firstVisible = firstVisibleIndex();
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
	if (!activeVisible && (activeIndex >= 0)) {
		const auto counterpart = mediaSplitCounterpart();
		if (!counterpart.isEmpty()) {
			activateTab(counterpart);
			return;
		} else if (mediaSplitSwitching()) {
			return;
		}
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
	const auto position = displayPosition(active);
	if (position >= int(_order.size())) {
		return nullptr;
	}
	const auto delta = toNextTab ? 1 : -1;
	for (auto p = position + delta
		; p >= 0 && p < int(_order.size())
		; p += delta) {
		const auto i = _order[p];
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
		raw->heightValue(
		) | rpl::on_next([this, raw](int) {
			if (!raw->isHidden()) {
				scheduleBodySync();
			}
		}, raw->lifetime());
	}
	raw->show();
	active->resizeToWidth(_body->width());
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
				displayPosition(previousIndex)
					> displayPosition(int(it - begin(_tabs))));
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
	const auto active = _activeTab.current();
	if (!active) {
		return;
	}
	if (_body->width() < st::infoMediaTabsMinBodyWidth) {
		_viewportPushPending = true;
		return;
	}
	_viewportPushPending = false;
	active->setTopOverlay((_visibleTop >= 0) ? _stripHeight : 0);
	active->setVisibleRegion(
		_visibleTop - _stripHeight,
		_visibleBottom - _stripHeight);
}

rpl::producer<MediaTabContent*> TabsHost::activeTabValue() const {
	return _activeTab.value();
}

rpl::producer<Ui::ScrollToRequest> TabsHost::scrollToRequests() const {
	return _scrollToRequests.events();
}

rpl::producer<TabTopBarBindings> TabsHost::activeTabBindings() {
	return _activeTab.value(
	) | rpl::map([=](MediaTabContent *tab) {
		_searching = false;
		auto result = tab ? tab->topBarBindings() : TabTopBarBindings();
		if (auto apply = base::take(result.applySearchQuery)) {
			result.applySearchQuery = crl::guard(this, [=](QString query) {
				_searching = !query.isEmpty();
				apply(query);
				if (_searching) {
					scrollToBodyTop();
				}
			});
		}
		return result;
	});
}

void TabsHost::scrollToBodyTop() {
	if (_visibleTop >= 0) {
		_scrollToRequests.fire({ 0, -1 });
	}
}

not_null<Ui::RpWidget*> TabsHost::stripWidget() const {
	return _strip;
}

void TabsHost::trackVerticalScroll(rpl::producer<> scrolls) {
	_strip->trackVerticalScroll(std::move(scrolls));
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
	_visibleTop = top;
	_visibleBottom = bottom;
	pushViewportToActive();
	if (_keepMinHeight) {
		scheduleHeightSync();
	}
}

void TabsHost::setScrolledToTop(bool scrolledToTop) {
	if (_scrolledToTop == scrolledToTop) {
		return;
	}
	_scrolledToTop = scrolledToTop;
	if (_keepMinHeight && scrolledToTop) {
		scheduleHeightSync();
	}
}

int TabsHost::resizeGetHeight(int newWidth) {
	_body->resizeToWidth(std::max(newWidth, 1));
	if (const auto active = _activeTab.current()) {
		active->resizeToWidth(_body->width());
		syncBodyNow();
	}
	if (_viewportPushPending && _body->width() >= st::infoMediaTabsMinBodyWidth) {
		_viewportPushPending = false;
		InvokeQueued(this, [this] { pushViewportToActive(); });
	}
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
	_body->moveToLeft(0, bodyTop);

	const auto natural = bodyTop + _body->height();
	const auto span = _visibleBottom - _visibleTop;
	_searchContentFits = _searching && !_scrolledToTop && (natural < span);
	if (_searchContentFits) {
		_keepMinHeight = span;
	} else if (_keepMinHeight
		&& ((natural >= _keepMinHeight)
			|| (natural >= _visibleBottom)
			|| _scrolledToTop)) {
		_keepMinHeight = 0;
	}
	return std::max(natural, _keepMinHeight);
}

} // namespace Info::Profile
