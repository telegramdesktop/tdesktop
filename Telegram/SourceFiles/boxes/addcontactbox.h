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

class EditChannelBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	EditChannelBox(ChannelData *channel);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);

	bool eventFilter(QObject *obj, QEvent *e);

	bool descriptionAnimStep(float64 ms);

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

	QRect descriptionRect() const;
	void updateMaxHeight();
	void updateSelected(const QPoint &cursorGlobalPosition);

	void onSaveTitleDone(const MTPUpdates &updates);
	void onSaveDescriptionDone(const MTPBool &result);
	bool onSaveFail(const RPCError &e, mtpRequestId req);

	void saveDescription();

	ChannelData *_channel;
	QString _boxTitle;

	FlatButton _saveButton, _cancelButton;
	FlatInput _title;

	bool _descriptionOver;
	anim::cvalue a_descriptionBg, a_descriptionBorder;
	Animation a_description;
	FlatTextarea _description;

	LinkButton _publicLink;

	mtpRequestId _saveTitleRequestId, _saveDescriptionRequestId;
	QString _sentTitle, _sentDescription;
};
