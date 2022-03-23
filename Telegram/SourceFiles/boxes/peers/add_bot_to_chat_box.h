/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_controllers.h"

class AddBotToGroupBoxController
	: public ChatsListBoxController
	, public base::has_weak_ptr {
public:
	static void Start(not_null<UserData*> bot);

	explicit AddBotToGroupBoxController(not_null<UserData*> bot);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void prepareViewHook() override;
	QString emptyBoxText() const override;

private:
	static bool SharingBotGame(not_null<UserData*> bot);

	object_ptr<Ui::RpWidget> prepareAdminnedChats();

	bool needToCreateRow(not_null<PeerData*> peer) const;
	bool sharingBotGame() const;
	QString noResultsText() const;
	void updateLabels();

	void shareBotGame(not_null<PeerData*> chat);
	void addBotToGroup(not_null<PeerData*> chat);

	const not_null<UserData*> _bot;
	rpl::event_stream<not_null<PeerData*>> _groups;
	rpl::event_stream<not_null<PeerData*>> _channels;
	bool _adminToGroup = false;
	bool _adminToChannel = false;

};

void AddBotToGroup(not_null<UserData*> bot, not_null<PeerData*> chat);
