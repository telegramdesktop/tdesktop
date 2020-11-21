/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/feed/info_feed_channels.h"

#include "info/feed/info_feed_channels_controllers.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_values.h"
#include "info/channels/info_channels_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_feed.h"
#include "lang/lang_keys.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace Info {
namespace FeedProfile {
namespace {

constexpr auto kEnableSearchChannelsAfterCount = 20;

} // namespace

Channels::Channels(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _feed(_controller->key().feed())
, _listController(std::make_unique<ChannelsController>(_controller)) {
	setupHeader();
	setupList();
	setContent(_list.data());
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));

	_controller->searchFieldController()->queryValue(
	) | rpl::start_with_next([this](QString &&query) {
		peerListScrollToTop();
		content()->searchQueryChanged(std::move(query));
	}, lifetime());
	Profile::FeedChannelsCountValue(
		_feed
	) | rpl::start_with_next([this](int count) {
		const auto enabled = (count >= kEnableSearchChannelsAfterCount);
		_controller->setSearchEnabledByContent(enabled);
	}, lifetime());
}

int Channels::desiredHeight() const {
	auto desired = _header ? _header->height() : 0;
	desired += st::infoChannelsList.item.height
		* std::max(int(_feed->channels().size()), _list->fullRowsCount());
	return qMax(height(), desired);
}

rpl::producer<Ui::ScrollToRequest> Channels::scrollToRequests() const {
	return _scrollToRequests.events();
}

std::unique_ptr<ChannelsState> Channels::saveState() {
	auto result = std::make_unique<ChannelsState>();
	result->list = _listController->saveState();
	return result;
}

void Channels::restoreState(std::unique_ptr<ChannelsState> state) {
	if (!state) {
		return;
	}
	_listController->restoreState(std::move(state->list));
}

void Channels::setupHeader() {
	if (_controller->section().type() == Section::Type::Channels) {
		return;
	}
	_header = object_ptr<Ui::FixedHeightWidget>(
		this,
		st::infoMembersHeader);
	auto parent = _header.data();

	_openChannels = Ui::CreateChild<Profile::Button>(
		parent,
		rpl::single(QString()));

	// #feed
	//object_ptr<Profile::FloatingIcon>(
	//	parent,
	//	st::infoIconFeed,
	//	st::infoIconPosition);

	_titleWrap = Ui::CreateChild<Ui::RpWidget>(parent);
	_title = setupTitle();
	_addChannel = Ui::CreateChild<Ui::IconButton>(
		_openChannels,
		st::infoChannelsAddChannel);
	_search = Ui::CreateChild<Ui::IconButton>(
		_openChannels,
		st::infoMembersSearch);

	setupButtons();

	widthValue(
	) | rpl::start_with_next([this](int width) {
		_header->resizeToWidth(width);
	}, _header->lifetime());
}

object_ptr<Ui::FlatLabel> Channels::setupTitle() {
	auto result = object_ptr<Ui::FlatLabel>(
		_titleWrap,
		Profile::FeedChannelsCountValue(
			_feed
		) | rpl::map([](int count) {
			return lng_feed_channels(lt_count, count);
		}) | Profile::ToUpperValue(),
		st::infoBlockHeaderLabel);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	return result;
}

void Channels::setupButtons() {
	using namespace rpl::mappers;

	_openChannels->addClickHandler([this] {
		showChannelsWithSearch(false);
	});

	_addChannel->addClickHandler([this] { // TODO throttle(ripple duration)
		this->addChannel();
	});

	auto searchShown = Profile::FeedChannelsCountValue(_feed)
		| rpl::map(_1 >= kEnableSearchChannelsAfterCount)
		| rpl::distinct_until_changed()
		| rpl::start_spawning(lifetime());
	_search->showOn(rpl::duplicate(searchShown));
	_search->addClickHandler([this] { // TODO throttle(ripple duration)
		this->showChannelsWithSearch(true);
	});

	std::move(
		searchShown
	) | rpl::start_with_next([this] {
		updateHeaderControlsGeometry(width());
	}, lifetime());
}

void Channels::setupList() {
	auto topSkip = _header ? _header->height() : 0;
	_list = object_ptr<ListWidget>(
		this,
		_listController.get(),
		st::infoChannelsList);
	_list->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		auto addmin = (request.ymin < 0 || !_header)
			? 0
			: _header->height();
		auto addmax = (request.ymax < 0 || !_header)
			? 0
			: _header->height();
		_scrollToRequests.fire({
			request.ymin + addmin,
			request.ymax + addmax });
	}, _list->lifetime());
	widthValue(
	) | rpl::start_with_next([this](int newWidth) {
		_list->resizeToWidth(newWidth);
	}, _list->lifetime());
	_list->heightValue(
	) | rpl::start_with_next([=](int listHeight) {
		auto newHeight = (listHeight > st::membersMarginBottom)
			? (topSkip
				+ listHeight
				+ st::membersMarginBottom)
			: 0;
		resize(width(), newHeight);
	}, _list->lifetime());
	_list->moveToLeft(0, topSkip);
}

int Channels::resizeGetHeight(int newWidth) {
	if (_header) {
		updateHeaderControlsGeometry(newWidth);
	}
	return heightNoMargins();
}

//void Channels::updateSearchEnabledByContent() {
//	_controller->setSearchEnabledByContent(
//		peerListFullRowsCount() >= kEnableSearchMembersAfterCount);
//}

void Channels::updateHeaderControlsGeometry(int newWidth) {
	_openChannels->setGeometry(0, st::infoProfileSkip, newWidth, st::infoMembersHeader - st::infoProfileSkip - st::infoMembersHeaderPaddingBottom);

	auto availableWidth = newWidth
		- st::infoMembersButtonPosition.x();

	//auto cancelLeft = availableWidth - _cancelSearch->width();
	//_cancelSearch->moveToLeft(
	//	cancelLeft,
	//	st::infoMembersButtonPosition.y());

	//auto searchShownLeft = st::infoIconPosition.x()
	//	- st::infoMembersSearch.iconPosition.x();
	//auto searchHiddenLeft = availableWidth - _search->width();
	//auto searchShown = _searchShownAnimation.value(_searchShown ? 1. : 0.);
	//auto searchCurrentLeft = anim::interpolate(
	//	searchHiddenLeft,
	//	searchShownLeft,
	//	searchShown);
	//_search->moveToLeft(
	//	searchCurrentLeft,
	//	st::infoMembersButtonPosition.y());

	//if (!_search->isHidden()) {
	//	availableWidth -= st::infoMembersSearch.width;
	//}
	_addChannel->moveToLeft(
		availableWidth - _addChannel->width(),
		st::infoMembersButtonPosition.y(),
		newWidth);
	if (!_addChannel->isHidden()) {
		availableWidth -= st::infoMembersSearch.width;
	}
	_search->moveToLeft(
		availableWidth - _search->width(),
		st::infoMembersButtonPosition.y(),
		newWidth);

	//auto fieldLeft = anim::interpolate(
	//	cancelLeft,
	//	st::infoBlockHeaderPosition.x(),
	//	searchShown);
	//_searchField->setGeometryToLeft(
	//	fieldLeft,
	//	st::infoMembersSearchTop,
	//	cancelLeft - fieldLeft,
	//	_searchField->height());

	//_titleWrap->resize(
	//	searchCurrentLeft - st::infoBlockHeaderPosition.x(),
	//	_title->height());
	_titleWrap->resize(
		availableWidth - _addChannel->width() - st::infoBlockHeaderPosition.x(),
		_title->height());
	_titleWrap->moveToLeft(
		st::infoBlockHeaderPosition.x(),
		st::infoBlockHeaderPosition.y(),
		newWidth);
	_titleWrap->setAttribute(Qt::WA_TransparentForMouseEvents);

	//_title->resizeToWidth(searchHiddenLeft);
	_title->resizeToWidth(_titleWrap->width());
	_title->moveToLeft(0, 0);
}

void Channels::addChannel() {
	EditController::Start(_feed);
}

void Channels::showChannelsWithSearch(bool withSearch) {
	auto contentMemento = std::make_unique<Info::Channels::Memento>(
		_controller);
	contentMemento->setState(saveState());
	contentMemento->setSearchStartsFocused(withSearch);
	auto mementoStack = std::vector<std::unique_ptr<ContentMemento>>();
	mementoStack.push_back(std::move(contentMemento));
	_controller->showSection(
		std::make_unique<Info::Memento>(std::move(mementoStack)));
}

void Channels::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

void Channels::peerListSetTitle(Fn<QString()> title) {
}

void Channels::peerListSetAdditionalTitle(
		Fn<QString()> title) {
}

bool Channels::peerListIsRowSelected(not_null<PeerData*> peer) {
	return false;
}

int Channels::peerListSelectedRowsCount() {
	return 0;
}

std::vector<not_null<PeerData*>> Channels::peerListCollectSelectedRows() {
	return {};
}

void Channels::peerListScrollToTop() {
	_scrollToRequests.fire({ -1, -1 });
}

void Channels::peerListAddSelectedRowInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void Channels::peerListFinishSelectedRowsBunch() {
}

void Channels::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

} // namespace FeedProfile
} // namespace Info

