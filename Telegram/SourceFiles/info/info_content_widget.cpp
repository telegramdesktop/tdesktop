/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_content_widget.h"

#include "window/window_session_controller.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_widget.h"
#include "info/media/info_media_widget.h"
#include "info/common_groups/info_common_groups_widget.h"
#include "info/info_layer_widget.h"
#include "info/info_section_widget.h"
#include "info/info_controller.h"
#include "boxes/peer_list_box.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "data/data_forum.h"
#include "main/main_session.h"
#include "styles/style_info.h"
#include "styles/style_profile.h"
#include "styles/style_layers.h"

#include <QtCore/QCoreApplication>

namespace Info {

ContentWidget::ContentWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _scroll(this) {
	using namespace rpl::mappers;

	setAttribute(Qt::WA_OpaquePaintEvent);
	_controller->wrapValue(
	) | rpl::start_with_next([this](Wrap value) {
		if (value != Wrap::Layer) {
			applyAdditionalScroll(0);
		}
		_bg = (value == Wrap::Layer)
			? st::boxBg
			: st::profileBg;
		update();
	}, lifetime());
	if (_controller->section().type() != Section::Type::Profile) {
		rpl::combine(
			_controller->wrapValue(),
			_controller->searchEnabledByContent(),
			(_1 == Wrap::Layer) && _2
		) | rpl::distinct_until_changed(
		) | rpl::start_with_next([this](bool shown) {
			refreshSearchField(shown);
		}, lifetime());
	}
	rpl::merge(
		_scrollTopSkip.changes(),
		_scrollBottomSkip.changes()
	) | rpl::start_with_next([this] {
		updateControlsGeometry();
	}, lifetime());
}

void ContentWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void ContentWidget::updateControlsGeometry() {
	if (!_innerWrap) {
		return;
	}
	_innerWrap->resizeToWidth(width());

	auto newScrollTop = _scroll->scrollTop() + _topDelta;
	auto scrollGeometry = rect().marginsRemoved(
		{ 0, _scrollTopSkip.current(), 0, _scrollBottomSkip.current() });
	if (_scroll->geometry() != scrollGeometry) {
		_scroll->setGeometry(scrollGeometry);
	}

	if (!_scroll->isHidden()) {
		if (_topDelta) {
			_scroll->scrollToY(newScrollTop);
		}
		auto scrollTop = _scroll->scrollTop();
		_innerWrap->setVisibleTopBottom(
			scrollTop,
			scrollTop + _scroll->height());
	}
}

std::shared_ptr<ContentMemento> ContentWidget::createMemento() {
	auto result = doCreateMemento();
	_controller->saveSearchState(result.get());
	return result;
}

void ContentWidget::setIsStackBottom(bool isStackBottom) {
	_isStackBottom = isStackBottom;
}

bool ContentWidget::isStackBottom() const {
	return _isStackBottom;
}

void ContentWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_paintPadding.isNull()) {
		p.fillRect(e->rect(), _bg);
	} else {
		const auto &r = e->rect();
		const auto padding = QMargins(
			0,
			std::min(0, (r.top() - _paintPadding.top())),
			0,
			std::min(0, (r.bottom() - _paintPadding.bottom())));
		p.fillRect(r + padding, _bg);
	}
}

void ContentWidget::setGeometryWithTopMoved(
		const QRect &newGeometry,
		int topDelta) {
	_topDelta = topDelta;
	auto willBeResized = (size() != newGeometry.size());
	if (geometry() != newGeometry) {
		setGeometry(newGeometry);
	}
	if (!willBeResized) {
		QResizeEvent fake(size(), size());
		QCoreApplication::sendEvent(this, &fake);
	}
	_topDelta = 0;
}

Ui::RpWidget *ContentWidget::doSetInnerWidget(
		object_ptr<RpWidget> inner) {
	using namespace rpl::mappers;

	_innerWrap = _scroll->setOwnedWidget(
		object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
			this,
			std::move(inner),
			_innerWrap ? _innerWrap->padding() : style::margins()));
	_innerWrap->move(0, 0);

	// MSVC BUG + REGRESSION rpl::mappers::tuple :(
	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		_innerWrap->entity()->desiredHeightValue()
	) | rpl::start_with_next([this](
			int top,
			int height,
			int desired) {
		const auto bottom = top + height;
		_innerDesiredHeight = desired;
		_innerWrap->setVisibleTopBottom(top, bottom);
		_scrollTillBottomChanges.fire_copy(std::max(desired - bottom, 0));
	}, _innerWrap->lifetime());

	return _innerWrap->entity();
}

int ContentWidget::scrollTillBottom(int forHeight) const {
	const auto scrollHeight = forHeight
		- _scrollTopSkip.current()
		- _scrollBottomSkip.current();
	const auto scrollBottom = _scroll->scrollTop() + scrollHeight;
	const auto desired = _innerDesiredHeight;
	return std::max(desired - scrollBottom, 0);
}

rpl::producer<int> ContentWidget::scrollTillBottomChanges() const {
	return _scrollTillBottomChanges.events();
}

void ContentWidget::setScrollTopSkip(int scrollTopSkip) {
	_scrollTopSkip = scrollTopSkip;
}

void ContentWidget::setScrollBottomSkip(int scrollBottomSkip) {
	_scrollBottomSkip = scrollBottomSkip;
}

rpl::producer<int> ContentWidget::scrollHeightValue() const {
	return _scroll->heightValue();
}

void ContentWidget::applyAdditionalScroll(int additionalScroll) {
	if (_innerWrap) {
		_innerWrap->setPadding({ 0, 0, 0, additionalScroll });
	}
}

rpl::producer<int> ContentWidget::desiredHeightValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_innerWrap->entity()->desiredHeightValue(),
		_scrollTopSkip.value(),
		_scrollBottomSkip.value()
	) | rpl::map(_1 + _2 + _3);
}

rpl::producer<bool> ContentWidget::desiredShadowVisibility() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_scroll->scrollTopValue(),
		_scrollTopSkip.value()
	) | rpl::map((_1 > 0) || (_2 > 0));
}

bool ContentWidget::hasTopBarShadow() const {
	return (_scroll->scrollTop() > 0);
}

void ContentWidget::setInnerFocus() {
	if (_searchField) {
		_searchField->setFocus();
	} else {
		_innerWrap->entity()->setFocus();
	}
}

int ContentWidget::scrollTopSave() const {
	return _scroll->scrollTop();
}

rpl::producer<int> ContentWidget::scrollTopValue() const {
	return _scroll->scrollTopValue();
}

void ContentWidget::scrollTopRestore(int scrollTop) {
	_scroll->scrollToY(scrollTop);
}

void ContentWidget::scrollTo(const Ui::ScrollToRequest &request) {
	_scroll->scrollTo(request);
}

bool ContentWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect ContentWidget::floatPlayerAvailableRect() const {
	return mapToGlobal(_scroll->geometry());
}

rpl::producer<SelectedItems> ContentWidget::selectedListValue() const {
	return rpl::single(SelectedItems(Storage::SharedMediaType::Photo));
}

void ContentWidget::setPaintPadding(const style::margins &padding) {
	_paintPadding = padding;
}

void ContentWidget::setViewport(
		rpl::producer<not_null<QEvent*>> &&events) const {
	std::move(
		events
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		_scroll->viewportEvent(e);
	}, _scroll->lifetime());
}

auto ContentWidget::titleStories()
-> rpl::producer<Dialogs::Stories::Content> {
	return nullptr;
}

void ContentWidget::saveChanges(FnMut<void()> done) {
	done();
}

void ContentWidget::refreshSearchField(bool shown) {
	auto search = _controller->searchFieldController();
	if (search && shown) {
		auto rowView = search->createRowView(
			this,
			st::infoLayerMediaSearch);
		_searchWrap = std::move(rowView.wrap);
		_searchField = rowView.field;

		const auto view = _searchWrap.get();
		widthValue(
		) | rpl::start_with_next([=](int newWidth) {
			view->resizeToWidth(newWidth);
			view->moveToLeft(0, 0);
		}, view->lifetime());
		view->show();
		_searchField->setFocus();
		setScrollTopSkip(view->heightNoMargins() - st::lineWidth);
	} else {
		if (Ui::InFocusChain(this)) {
			setFocus();
		}
		_searchWrap = nullptr;
		setScrollTopSkip(0);
	}
}

int ContentWidget::scrollBottomSkip() const {
	return _scrollBottomSkip.current();
}

rpl::producer<int> ContentWidget::scrollBottomSkipValue() const {
	return _scrollBottomSkip.value();
}

rpl::producer<bool> ContentWidget::desiredBottomShadowVisibility() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_scroll->scrollTopValue(),
		_scrollBottomSkip.value(),
		_scroll->heightValue()
	) | rpl::map([=](int scroll, int skip, int) {
		return ((skip > 0) && (scroll < _scroll->scrollTopMax()));
	});
}

Key ContentMemento::key() const {
	if (const auto topic = this->topic()) {
		return Key(topic);
	} else if (const auto peer = this->peer()) {
		return Key(peer);
	} else if (const auto poll = this->poll()) {
		return Key(poll, pollContextId());
	} else if (const auto self = settingsSelf()) {
		return Settings::Tag{ self };
	} else if (const auto peer = storiesPeer()) {
		return Stories::Tag{ peer, storiesTab() };
	} else if (const auto peer = statisticsPeer()) {
		return Statistics::Tag{ peer, statisticsContextId() };
	} else {
		return Downloads::Tag();
	}
}

ContentMemento::ContentMemento(
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	PeerId migratedPeerId)
: _peer(peer)
, _migratedPeerId((!topic && peer->migrateFrom())
	? peer->migrateFrom()->id
	: 0)
, _topic(topic) {
	if (_topic) {
		_peer->owner().itemIdChanged(
		) | rpl::start_with_next([=](const Data::Session::IdChange &change) {
			if (_topic->rootId() == change.oldId) {
				_topic = _topic->forum()->topicFor(change.newId.msg);
			}
		}, _lifetime);
	}
}

ContentMemento::ContentMemento(Settings::Tag settings)
: _settingsSelf(settings.self.get()) {
}

ContentMemento::ContentMemento(Downloads::Tag downloads) {
}

ContentMemento::ContentMemento(Stories::Tag stories)
: _storiesPeer(stories.peer)
, _storiesTab(stories.tab) {
}

ContentMemento::ContentMemento(Statistics::Tag statistics)
: _statisticsPeer(statistics.peer)
, _statisticsContextId(statistics.contextId) {
}

} // namespace Info
