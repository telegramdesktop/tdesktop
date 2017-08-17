/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "boxes/peer_list_box.h"

namespace Calls {

class BoxController : public PeerListController, private base::Subscriber, private MTP::Sender {
public:
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

private:
	void receivedCalls(const QVector<MTPMessage> &result);
	void refreshAbout();

	class Row;
	Row *rowForItem(HistoryItem *item);

	enum class InsertWay {
		Append,
		Prepend,
	};
	bool insertRow(HistoryItem *item, InsertWay way);
	std::unique_ptr<PeerListRow> createRow(HistoryItem *item) const;

	MsgId _offsetId = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;

};

} // namespace Calls
