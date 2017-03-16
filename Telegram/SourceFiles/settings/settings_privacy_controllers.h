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
#include "boxes/edit_privacy_box.h"

namespace Settings {

class BlockedBoxController : public QObject, public PeerListBox::Controller, private base::Subscriber {
public:
	void prepare() override;
	void rowClicked(PeerData *peer) override;
	void rowActionClicked(PeerData *peer) override;
	void preloadRows() override;

private:
	void receivedUsers(const QVector<MTPContactBlocked> &result);
	void handleBlockedEvent(UserData *user);
	void blockUser();

	bool appendRow(UserData *user);
	bool prependRow(UserData *user);
	std::unique_ptr<PeerListBox::Row> createRow(UserData *user) const;

	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;

};

} // namespace Settings
