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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
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

	void setInnerFocus() {
		_firstInput.setFocus();
	}

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

	bool _invertOrder;

	uint64 _contactId;

	mtpRequestId _addRequest;
	QString _sentName;
};

class EditNameTitleBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	EditNameTitleBox(PeerData *peer);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void setInnerFocus() {
		_first.setFocus();
	}

public slots:

	void onSave();
	void onSubmit();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	void onSaveSelfDone(const MTPUser &user);
	bool onSaveSelfFail(const RPCError &error);

	void onSaveChatDone(const MTPUpdates &updates);
	bool onSaveChatFail(const RPCError &e);

	PeerData *_peer;
	QString _boxTitle;

	BoxButton _save, _cancel;
	InputField _first, _last;

	bool _invertOrder;

	mtpRequestId _requestId;
	QString _sentName;
};

class EditChannelBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	EditChannelBox(ChannelData *channel);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void setInnerFocus() {
		if (!_description.hasFocus()) {
			_title.setFocus();
		}
	}

public slots:

	void peerUpdated(PeerData *peer);

	void onSave();
	void onDescriptionResized();
	void onPublicLink();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	void updateMaxHeight();

	void onSaveTitleDone(const MTPUpdates &updates);
	void onSaveDescriptionDone(const MTPBool &result);
	bool onSaveFail(const RPCError &e, mtpRequestId req);

	void saveDescription();

	ChannelData *_channel;

	BoxButton _save, _cancel;
	InputField _title;
	InputArea _description;

	LinkButton _publicLink;

	mtpRequestId _saveTitleRequestId, _saveDescriptionRequestId;
	QString _sentTitle, _sentDescription;
};
