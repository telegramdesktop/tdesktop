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
#include "base/flat_set.h"
#include "base/weak_ptr.h"

// Not used for now.
//
//class MembersAddButton : public Ui::RippleButton {
//public:
//	MembersAddButton(QWidget *parent, const style::TwoIconButton &st);
//
//protected:
//	void paintEvent(QPaintEvent *e) override;
//
//	QImage prepareRippleMask() const override;
//	QPoint prepareRippleStartPosition() const override;
//
//private:
//	const style::TwoIconButton &_st;
//
//};

class PeerListRowWithLink : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

	void setActionLink(const QString &action);

	void lazyInitialize(const style::PeerListItem &st) override;

private:
	void refreshActionLink();
	QSize actionSize() const override;
	QMargins actionMargins() const override;
	void paintAction(
		Painter &p,
		TimeMs ms,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

	QString _action;
	int _actionWidth = 0;

};

class PeerListGlobalSearchController : public PeerListSearchController, private MTP::Sender {
public:
	PeerListGlobalSearchController();

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override {
		return false;
	}

private:
	bool searchInCache();
	void searchOnServer();
	void searchDone(const MTPcontacts_Found &result, mtpRequestId requestId);

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	std::map<QString, MTPcontacts_Found> _cache;
	std::map<mtpRequestId, QString> _queries;

};

class ChatsListBoxController
	: public PeerListController
	, protected base::Subscriber {
public:
	ChatsListBoxController(
		std::unique_ptr<PeerListSearchController> searchController
			= std::make_unique<PeerListGlobalSearchController>());

	void prepare() override final;
	std::unique_ptr<PeerListRow> createSearchRow(not_null<PeerData*> peer) override final;

protected:
	class Row : public PeerListRow {
	public:
		Row(not_null<History*> history) : PeerListRow(history->peer), _history(history) {
		}
		not_null<History*> history() const {
			return _history;
		}

	private:
		not_null<History*> _history;

	};
	virtual std::unique_ptr<Row> createRow(not_null<History*> history) = 0;
	virtual void prepareViewHook() = 0;
	virtual void updateRowHook(not_null<Row*> row) {
	}
	virtual QString emptyBoxText() const;

private:
	void rebuildRows();
	void checkForEmptyRows();
	bool appendRow(not_null<History*> history);

};

class ContactsBoxController : public PeerListController, protected base::Subscriber {
public:
	ContactsBoxController(std::unique_ptr<PeerListSearchController> searchController = std::make_unique<PeerListGlobalSearchController>());

	void prepare() override final;
	std::unique_ptr<PeerListRow> createSearchRow(not_null<PeerData*> peer) override final;
	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	virtual std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user);
	virtual void prepareViewHook() {
	}
	virtual void updateRowHook(not_null<PeerListRow*> row) {
	}

private:
	void rebuildRows();
	void checkForEmptyRows();
	bool appendRow(not_null<UserData*> user);

};

class EditChatAdminsBoxController : public PeerListController, private base::Subscriber {
public:
	static void Start(not_null<ChatData*> chat);

	EditChatAdminsBoxController(not_null<ChatData*> chat);

	bool allAreAdmins() const;

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	void createAllAdminsCheckbox();
	void rebuildRows();
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user);

	not_null<ChatData*> _chat;
	int _adminsUpdatedSubscription = 0;

	class LabeledCheckbox;
	QPointer<LabeledCheckbox> _allAdmins;

};

class AddParticipantsBoxController : public ContactsBoxController {
public:
	static void Start(not_null<ChatData*> chat);
	static void Start(not_null<ChannelData*> channel);
	static void Start(
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn);

	AddParticipantsBoxController(PeerData *peer);
	AddParticipantsBoxController(
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn);

	using ContactsBoxController::ContactsBoxController;

	void rowClicked(not_null<PeerListRow*> row) override;
	void itemDeselectedHook(not_null<PeerData*> peer) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) override;

private:
	static void Start(
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn,
		bool justCreated);

	int alreadyInCount() const;
	bool isAlreadyIn(not_null<UserData*> user) const;
	int fullCount() const;
	void updateTitle();

	PeerData *_peer = nullptr;
	base::flat_set<not_null<UserData*>> _alreadyIn;

};

class AddBotToGroupBoxController : public ChatsListBoxController, public base::has_weak_ptr {
public:
	static void Start(not_null<UserData*> bot);

	AddBotToGroupBoxController(not_null<UserData*> bot);

	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void prepareViewHook() override;
	QString emptyBoxText() const override;

private:
	static bool SharingBotGame(not_null<UserData*> bot);

	bool needToCreateRow(not_null<PeerData*> peer) const;
	bool sharingBotGame() const;
	QString noResultsText() const;
	QString descriptionText() const;
	void updateLabels();

	void shareBotGame(not_null<PeerData*> chat);
	void addBotToGroup(not_null<PeerData*> chat);

	not_null<UserData*> _bot;

};

class ChooseRecipientBoxController : public ChatsListBoxController {
public:
	ChooseRecipientBoxController(
		base::lambda_once<void(not_null<PeerData*>)> callback);

	void rowClicked(not_null<PeerListRow*> row) override;

	bool respectSavedMessagesChat() const override {
		return true;
	}

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(
		not_null<History*> history) override;

private:
	base::lambda_once<void(not_null<PeerData*>)> _callback;

};
