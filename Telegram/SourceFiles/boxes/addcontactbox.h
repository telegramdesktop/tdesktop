/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include "layerwidget.h"

class AddContactBox : public LayeredWidget, public RPCSender {
	Q_OBJECT

public:

	AddContactBox(QString fname = QString(), QString lname = QString(), QString phone = QString());
	AddContactBox(PeerData *peer);
	void parentResized();
	void animStep(float64 dt);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void startHide();
	~AddContactBox();

public slots:

	void onSend();
	void onRetry();
	void onCancel();

private:

	void hideAll();
	void showAll();

	void onImportDone(const MTPcontacts_ImportedContacts &res);

	void onSaveSelfDone(const MTPUser &user);
	bool onSaveSelfFail(const RPCError &error);

	void onSaveChatDone(const MTPmessages_StatedMessage &result);
	void onSaveUserDone(const MTPcontacts_ImportedContacts &res);
	bool onSaveFail(const RPCError &e);

	void initBox();

	PeerData *_peer;
	QString _boxTitle;

	int32 _width, _height, _thumbw, _thumbh;
	FlatButton _addButton, _retryButton, _cancelButton;
	FlatInput _firstInput, _lastInput, _phoneInput;

	uint64 _contactId;

	QPixmap _cache;

	mtpRequestId _addRequest;
	QString _sentName;

	anim::fvalue a_opacity;
	bool _hiding;
};
