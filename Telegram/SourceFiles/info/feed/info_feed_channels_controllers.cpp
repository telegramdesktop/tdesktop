/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/feed/info_feed_channels_controllers.h"

#include "data/data_feed.h"
#include "data/data_session.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "auth_session.h"
#include "history/history.h"

namespace Info {
namespace FeedProfile {

ChannelsController::ChannelsController(not_null<Controller*> controller)
: PeerListController()
, _controller(controller)
, _feed(_controller->key().feed()) {
	_controller->setSearchEnabledByContent(false);
}

std::unique_ptr<PeerListRow> ChannelsController::createRow(
		not_null<PeerData*> peer) {
	auto result = std::make_unique<PeerListRow>(peer);
	result->setCustomStatus(QString());
	return result;
}

std::unique_ptr<PeerListRow> ChannelsController::createRestoredRow(
		not_null<PeerData*> peer) {
	return createRow(peer);
}

void ChannelsController::prepare() {
	setSearchNoResultsText(lang(lng_bot_groups_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_info_feed_channels));

	rebuildRows();
	using Flag = Data::FeedUpdateFlag;
	rpl::single(
		Data::FeedUpdate{ _feed, Flag::Channels }
	) | rpl::then(
		Auth().data().feedUpdated()
	) | rpl::filter([=](const Data::FeedUpdate &update) {
		return (update.feed == _feed) && (update.flag == Flag::Channels);
	}) | rpl::filter([=] {
		return _feed->channelsLoaded();
	}) | rpl::start_with_next([=] {
		rebuildRows();
	}, lifetime());
}

void ChannelsController::rebuildRows() {
	if (!_feed->channelsLoaded()) {
		return;
	}
	const auto &channels = _feed->channels();
	auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count;) {
		const auto row = delegate()->peerListRowAt(i);
		const auto peer = row->peer();
		if (ranges::find_if(channels, [=](not_null<History*> history) {
			return (history->peer == peer);
		}) != end(channels)) {
			++i;
		} else {
			delegate()->peerListRemoveRow(row);
			--count;
		}
	}
	for (const auto history : channels) {
		if (auto row = createRow(history->peer)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}
}

std::unique_ptr<PeerListState> ChannelsController::saveState() const {
	auto result = PeerListController::saveState();
	auto my = std::make_unique<SavedState>();
	using Flag = Data::FeedUpdateFlag;
	Auth().data().feedUpdated(
	) | rpl::filter([=](const Data::FeedUpdate &update) {
		return (update.feed == _feed) && (update.flag == Flag::Channels);
	}) | rpl::start_with_next([state = result.get()] {
		state->controllerState = nullptr;
	}, my->lifetime);
	result->controllerState = std::move(my);
	return result;
}

void ChannelsController::restoreState(
		std::unique_ptr<PeerListState> state) {
	PeerListController::restoreState(std::move(state));
}

void ChannelsController::rowClicked(not_null<PeerListRow*> row) {
	_controller->parentController()->showPeerHistory(
		row->peer(),
		Window::SectionShow::Way::Forward);
}

} // namespace FeedProfile
} // namespace Info
