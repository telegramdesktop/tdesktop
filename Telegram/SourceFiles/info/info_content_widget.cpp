/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_content_widget.h"

#include "api/api_who_reacted.h"
#include "boxes/peer_list_box.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_forum_topic.h"
#include "data/data_forum.h"
#include "info/profile/info_profile_widget.h"
#include "info/media/info_media_widget.h"
#include "info/common_groups/info_common_groups_widget.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "info/saved/info_saved_music_common.h"
#include "info/stories/info_stories_common.h"
#include "info/info_layer_widget.h"
#include "info/info_section_widget.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/swipe_handler.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"
#include "ui/ui_utility.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
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
, _scroll(
	this,
	(_controller->wrap() == Wrap::Search
		? st::infoSharedMediaScroll
		: st::defaultScrollArea)) {
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

	setupSwipeHandler(_innerWrap);

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

	rpl::combine(
		_scroll->heightValue(),
		_innerWrap->entity()->heightValue(),
		_controller->wrapValue()
	) | rpl::start_with_next([=](
			int scrollHeight,
			int innerHeight,
			Wrap wrap) {
		const auto added = (wrap == Wrap::Layer)
			? 0
			: std::max(scrollHeight - innerHeight, 0);
		if (_addedHeight != added) {
			_addedHeight = added;
			updateInnerPadding();
		}
	}, _innerWrap->lifetime());
	updateInnerPadding();

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
	if (_additionalScroll != additionalScroll) {
		_additionalScroll = additionalScroll;
		if (_innerWrap) {
			updateInnerPadding();
		}
	}
}

void ContentWidget::updateInnerPadding() {
	const auto addedToBottom = std::max(_additionalScroll, _addedHeight);
	_innerWrap->setPadding({ 0, 0, 0, addedToBottom });
}

void ContentWidget::applyMaxVisibleHeight(int maxVisibleHeight) {
	if (_maxVisibleHeight != maxVisibleHeight) {
		_maxVisibleHeight = maxVisibleHeight;
		update();
	}
}

rpl::producer<int> ContentWidget::desiredHeightValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_innerWrap->entity()->desiredHeightValue(),
		_scrollTopSkip.value(),
		_scrollBottomSkip.value()
	//) | rpl::map(_1 + _2 + _3);
	) | rpl::map([=](int desired, int, int) {
		return desired
			+ _scrollTopSkip.current()
			+ _scrollBottomSkip.current();
	});
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

void ContentWidget::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto peer = _controller->key().peer();
	const auto topic = _controller->key().topic();
	const auto sublist = _controller->key().sublist();
	if (!peer && !topic) {
		return;
	}

	Window::FillDialogsEntryMenu(
		_controller->parentController(),
		Dialogs::EntryState{
			.key = (topic
				? Dialogs::Key{ topic }
				: sublist
				? Dialogs::Key{ sublist }
				: Dialogs::Key{ peer->owner().history(peer) }),
			.section = Dialogs::EntryState::Section::Profile,
		},
		addAction);
}

void ContentWidget::checkBeforeCloseByEscape(Fn<void()> close) {
	if (_searchField) {
		if (!_searchField->empty()) {
			_searchField->setText({});
		} else {
			close();
		}
	} else {
		close();
	}
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

rpl::producer<bool> ContentWidget::desiredBottomShadowVisibility() {
	using namespace rpl::mappers;
	return rpl::combine(
		_scroll->scrollTopValue(),
		_scrollBottomSkip.value(),
		_scroll->heightValue()
	) | rpl::map([=](int scroll, int skip, int) {
		return ((skip > 0) && (scroll < _scroll->scrollTopMax()));
	});
}

not_null<Ui::ScrollArea*> ContentWidget::scroll() const {
	return _scroll.data();
}

void ContentWidget::replaceSwipeHandler(
		Ui::Controls::SwipeHandlerArgs *incompleteArgs) {
	_swipeHandlerLifetime.destroy();
	auto args = std::move(*incompleteArgs);
	args.widget = _innerWrap;
	args.scroll = _scroll.data();
	args.onLifetime = &_swipeHandlerLifetime;
	Ui::Controls::SetupSwipeHandler(std::move(args));
}

void ContentWidget::setupSwipeHandler(not_null<Ui::RpWidget*> widget) {
	_swipeHandlerLifetime.destroy();

	auto update = [=](Ui::Controls::SwipeContextData data) {
		if (data.translation > 0) {
			if (!_swipeBackData.callback) {
				_swipeBackData = Ui::Controls::SetupSwipeBack(
					this,
					[]() -> std::pair<QColor, QColor> {
						return {
							st::historyForwardChooseBg->c,
							st::historyForwardChooseFg->c,
						};
					});
			}
			_swipeBackData.callback(data);
			return;
		} else if (_swipeBackData.lifetime) {
			_swipeBackData = {};
		}
	};

	auto init = [=](int, Qt::LayoutDirection direction) {
		return (direction == Qt::RightToLeft && _controller->hasBackButton())
			? Ui::Controls::DefaultSwipeBackHandlerFinishData([=] {
				checkBeforeClose(crl::guard(this, [=] {
					_controller->parentController()->hideLayer();
					_controller->showBackFromStack();
				}));
			})
			: Ui::Controls::SwipeHandlerFinishData();
	};

	Ui::Controls::SetupSwipeHandler({
		.widget = widget,
		.scroll = _scroll.data(),
		.update = std::move(update),
		.init = std::move(init),
		.onLifetime = &_swipeHandlerLifetime,
	});
}

Key ContentMemento::key() const {
	if (const auto topic = this->topic()) {
		return Key(topic);
	} else if (const auto sublist = this->sublist()) {
		return Key(sublist);
	} else if (const auto peer = this->peer()) {
		return Key(peer);
	} else if (const auto poll = this->poll()) {
		return Key(poll, pollContextId());
	} else if (const auto self = settingsSelf()) {
		return Settings::Tag{ self };
	} else if (const auto gifts = giftsPeer()) {
		return PeerGifts::Tag{
			gifts,
			giftsCollectionId(),
		};
	} else if (const auto stories = storiesPeer()) {
		return Stories::Tag{
			stories,
			storiesAlbumId(),
			storiesAddToAlbumId(),
		};
	} else if (const auto music = musicPeer()) {
		return Saved::MusicTag{ music };
	} else if (statisticsTag().peer) {
		return statisticsTag();
	} else if (const auto starref = starrefPeer()) {
		return BotStarRef::Tag(starref, starrefType());
	} else if (const auto who = reactionsWhoReadIds()) {
		return Key(who, _reactionsSelected, _pollReactionsContextId);
	} else if (const auto another = globalMediaSelf()) {
		return GlobalMedia::Tag{ another };
	} else {
		return Downloads::Tag();
	}
}

ContentMemento::ContentMemento(
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist,
	PeerId migratedPeerId)
: _peer(peer)
, _migratedPeerId((!topic && !sublist && peer->migrateFrom())
	? peer->migrateFrom()->id
	: 0)
, _topic(topic)
, _sublist(sublist) {
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
, _storiesAlbumId(stories.albumId)
, _storiesAddToAlbumId(stories.addingToAlbumId) {
}

ContentMemento::ContentMemento(Saved::MusicTag music)
: _musicPeer(music.peer) {
}

ContentMemento::ContentMemento(PeerGifts::Tag gifts)
: _giftsPeer(gifts.peer)
, _giftsCollectionId(gifts.collectionId) {
}

ContentMemento::ContentMemento(Statistics::Tag statistics)
: _statisticsTag(statistics) {
}

ContentMemento::ContentMemento(BotStarRef::Tag starref)
: _starrefPeer(starref.peer)
, _starrefType(starref.type) {
}

ContentMemento::ContentMemento(GlobalMedia::Tag global)
: _globalMediaSelf(global.self) {
}

ContentMemento::ContentMemento(
	std::shared_ptr<Api::WhoReadList> whoReadIds,
	FullMsgId contextId,
	Data::ReactionId selected)
: _reactionsWhoReadIds(whoReadIds
	? whoReadIds
	: std::make_shared<Api::WhoReadList>())
, _reactionsSelected(selected)
, _pollReactionsContextId(contextId) {
}

} // namespace Info
