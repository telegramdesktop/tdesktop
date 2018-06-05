/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	void confirmSave(bool someAreDisallowed, FnMut<void()> saveCallback) override;

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
