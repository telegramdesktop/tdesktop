/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "mtproto/sender.h"

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
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

private:
	class Row;
	struct SavedState : SavedStateBase {
		rpl::lifetime lifetime;
	};

	void rebuildRows();
	std::unique_ptr<Row> createRow(not_null<History*> history);

	const not_null<Controller*> _controller;
	not_null<Data::Feed*> _feed;

};

class NotificationsController
	: public PeerListController
	, private MTP::Sender {
public:
	static void Start(not_null<Data::Feed*> feed);

	NotificationsController(not_null<Data::Feed*> feed);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	void loadMoreRows() override;

private:
	std::unique_ptr<PeerListRow> createRow(not_null<ChannelData*> channel);
	void applyFeedDialogs(const MTPmessages_Dialogs &result);

	not_null<Data::Feed*> _feed;
	mtpRequestId _preloadRequestId = 0;
	//TimeId _preloadOffsetDate = TimeId(0); // #feed
	//MsgId _preloadOffsetId = MsgId(0);
	//PeerData *_preloadPeer = nullptr;
	bool _allLoaded = false;

};

class EditController
	: public PeerListController
	, private MTP::Sender {
public:
	static void Start(
		not_null<Data::Feed*> feed,
		ChannelData *channel = nullptr);

	EditController(
		not_null<Data::Feed*> feed,
		ChannelData *channel);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	void loadMoreRows() override;

private:
	std::unique_ptr<PeerListRow> createRow(not_null<ChannelData*> channel);
//	void applyFeedSources(const MTPchannels_FeedSources &result); // #feed

	not_null<Data::Feed*> _feed;
//	ChannelData *_startWithChannel = nullptr; // #feed
	mtpRequestId _preloadRequestId = 0;
	bool _allLoaded = false;

};

} // namespace FeedProfile
} // namespace Info
