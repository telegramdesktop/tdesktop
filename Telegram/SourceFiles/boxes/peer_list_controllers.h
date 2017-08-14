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

#include "peer_list_box.h"

class PeerListRowWithLink : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

	void setActionLink(const QString &action);

	void lazyInitialize() override;

private:
	void refreshActionLink();
	QSize actionSize() const override;
	QMargins actionMargins() const override;
	void paintAction(Painter &p, TimeMs ms, int x, int y, int outerWidth, bool actionSelected) override;

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

class ChatsListBoxController : public PeerListController, protected base::Subscriber {
public:
	ChatsListBoxController(std::unique_ptr<PeerListSearchController> searchController = std::make_unique<PeerListGlobalSearchController>());

	void prepare() override final;
	std::unique_ptr<PeerListRow> createSearchRow(gsl::not_null<PeerData*> peer) override final;

protected:
	class Row : public PeerListRow {
	public:
		Row(gsl::not_null<History*> history) : PeerListRow(history->peer), _history(history) {
		}
		gsl::not_null<History*> history() const {
			return _history;
		}

	private:
		gsl::not_null<History*> _history;

	};
	virtual std::unique_ptr<Row> createRow(gsl::not_null<History*> history) = 0;
	virtual void prepareViewHook() = 0;
	virtual void updateRowHook(gsl::not_null<Row*> row) {
	}

private:
	void rebuildRows();
	void checkForEmptyRows();
	bool appendRow(gsl::not_null<History*> history);

};

class ContactsBoxController : public PeerListController, protected base::Subscriber {
public:
	ContactsBoxController(std::unique_ptr<PeerListSearchController> searchController = std::make_unique<PeerListGlobalSearchController>());

	void prepare() override final;
	std::unique_ptr<PeerListRow> createSearchRow(gsl::not_null<PeerData*> peer) override final;
	void rowClicked(gsl::not_null<PeerListRow*> row) override;

protected:
	virtual std::unique_ptr<PeerListRow> createRow(gsl::not_null<UserData*> user);
	virtual void prepareViewHook() {
	}
	virtual void updateRowHook(gsl::not_null<PeerListRow*> row) {
	}

private:
	void rebuildRows();
	void checkForEmptyRows();
	bool appendRow(gsl::not_null<UserData*> user);

};
