/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"

namespace Calls {

class BoxController
	: public PeerListController
	, private base::Subscriber
	, private MTP::Sender {
public:
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

private:
	void receivedCalls(const QVector<MTPMessage> &result);
	void refreshAbout();

	class Row;
	Row *rowForItem(not_null<const HistoryItem*> item);

	enum class InsertWay {
		Append,
		Prepend,
	};
	bool insertRow(not_null<HistoryItem*> item, InsertWay way);
	std::unique_ptr<PeerListRow> createRow(
		not_null<HistoryItem*> item) const;

	MsgId _offsetId = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;

};

} // namespace Calls
