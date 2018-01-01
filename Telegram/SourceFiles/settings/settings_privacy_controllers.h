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
#include "mtproto/sender.h"

namespace Settings {

class BlockedBoxController : public PeerListController, private base::Subscriber, private MTP::Sender {
public:
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	static void BlockNewUser();

private:
	void receivedUsers(const QVector<MTPContactBlocked> &result);
	void handleBlockedEvent(UserData *user);

	bool appendRow(UserData *user);
	bool prependRow(UserData *user);
	std::unique_ptr<PeerListRow> createRow(UserData *user) const;

	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;

};

class LastSeenPrivacyController : public EditPrivacyBox::Controller, private base::Subscriber {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	MTPInputPrivacyKey key() override;

	QString title() override;
	QString description() override;
	QString warning() override;
	QString exceptionLinkText(Exception exception, int count) override;
	QString exceptionBoxTitle(Exception exception) override;
	QString exceptionsDescription() override;

	void confirmSave(bool someAreDisallowed, base::lambda_once<void()> saveCallback) override;

};

class GroupsInvitePrivacyController : public EditPrivacyBox::Controller, private base::Subscriber {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	MTPInputPrivacyKey key() override;

	QString title() override;
	bool hasOption(Option option) override;
	QString description() override;
	QString exceptionLinkText(Exception exception, int count) override;
	QString exceptionBoxTitle(Exception exception) override;
	QString exceptionsDescription() override;

};

class CallsPrivacyController : public EditPrivacyBox::Controller, private base::Subscriber {
public:
	using Option = EditPrivacyBox::Option;
	using Exception = EditPrivacyBox::Exception;

	MTPInputPrivacyKey key() override;

	QString title() override;
	QString description() override;
	QString exceptionLinkText(Exception exception, int count) override;
	QString exceptionBoxTitle(Exception exception) override;
	QString exceptionsDescription() override;

};

} // namespace Settings
