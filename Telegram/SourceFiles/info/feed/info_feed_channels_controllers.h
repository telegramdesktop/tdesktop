/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"

namespace Data {
class Feed;
} // namespace Data

namespace Info {

class Controller;

namespace FeedProfile {

class ChannelsController
	: public PeerListController
	, private base::Subscriber {
public:
	ChannelsController(not_null<Controller*> controller);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;

	std::unique_ptr<PeerListRow> createRestoredRow(
		not_null<PeerData*> peer) override;

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

private:
	class Row;
	struct SavedState : SavedStateBase {
		rpl::lifetime lifetime;
	};

	void rebuildRows();
	std::unique_ptr<Row> createRow(not_null<ChannelData*> channel);

	const not_null<Controller*> _controller;
	not_null<Data::Feed*> _feed;

};

} // namespace FeedProfile
} // namespace Info
