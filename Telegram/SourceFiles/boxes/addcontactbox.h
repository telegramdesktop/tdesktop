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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "abstractbox.h"

class AddContactBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	AddContactBox(QString fname = QString(), QString lname = QString(), QString phone = QString());
	AddContactBox(PeerData *peer);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

public slots:

	void onSend();
	void onRetry();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	void onImportDone(const MTPcontacts_ImportedContacts &res);

	void onSaveSelfDone(const MTPUser &user);
	bool onSaveSelfFail(const RPCError &error);

	void onSaveChatDone(const MTPUpdates &updates);
	void onSaveUserDone(const MTPcontacts_ImportedContacts &res);
	bool onSaveFail(const RPCError &e);

	void initBox();

	PeerData *_peer;
	QString _boxTitle;

	FlatButton _addButton, _retryButton, _cancelButton;
	FlatInput _firstInput, _lastInput, _phoneInput;

	uint64 _contactId;

	mtpRequestId _addRequest;
	QString _sentName;
};
