/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_controllers.h"
#include "data/data_chat_participant_status.h"

class AddBotToGroupBoxController
	: public ChatsListBoxController
	, public base::has_weak_ptr {
public:
	enum class Scope {
		None,
		GroupAdmin,
		ChannelAdmin,
		ShareGame,
		All,
	};
	static void Start(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> bot,
		Scope scope = Scope::All,
		const QString &token = QString(),
		ChatAdminRights requestedRights = {});

	AddBotToGroupBoxController(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> bot,
		Scope scope,
		const QString &token,
		ChatAdminRights requestedRights);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void prepareViewHook() override;
	QString emptyBoxText() const override;

private:
	[[nodiscard]] object_ptr<Ui::RpWidget> prepareAdminnedChats();

	[[nodiscard]] bool onlyAdminToGroup() const;
	[[nodiscard]] bool onlyAdminToChannel() const;

	bool needToCreateRow(not_null<PeerData*> peer) const;
	bool sharingBotGame() const;
	QString noResultsText() const;
	void updateLabels();

	void shareBotGame(not_null<PeerData*> chat);
	void addBotToGroup(not_null<PeerData*> chat);
	void requestExistingRights(not_null<ChannelData*> channel);

	const not_null<Window::SessionController*> _controller;
	const not_null<UserData*> _bot;
	const Scope _scope = Scope::None;
	const QString _token;
	const ChatAdminRights _requestedRights;

	ChannelData *_existingRightsChannel = nullptr;
	mtpRequestId _existingRightsRequestId = 0;
	std::optional<ChatAdminRights> _existingRights;
	QString _existingRank;

	rpl::event_stream<not_null<PeerData*>> _groups;
	rpl::event_stream<not_null<PeerData*>> _channels;

	bool _adminToGroup = false;
	bool _adminToChannel = false;
	bool _memberToGroup = false;

};

void AddBotToGroup(
	not_null<UserData*> bot,
	not_null<PeerData*> chat,
	const QString &startToken);
