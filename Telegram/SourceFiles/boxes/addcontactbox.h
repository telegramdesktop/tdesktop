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
	AddContactBox(UserData *user);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void setInnerFocus() {
		_first.setFocus();
	}

public slots:

	void onSubmit();
	void onSave();
	void onRetry();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	void onImportDone(const MTPcontacts_ImportedContacts &res);

	void onSaveUserDone(const MTPcontacts_ImportedContacts &res);
	bool onSaveUserFail(const RPCError &e);

	void initBox();

	UserData *_user;
	QString _boxTitle;

	BoxButton _save, _cancel, _retry;
	InputField _first, _last;
	PhoneInput _phone;

	bool _invertOrder;

	uint64 _contactId;

	mtpRequestId _addRequest;
	QString _sentName;
};

class NewGroupBox : public AbstractBox {
	Q_OBJECT

public:

	NewGroupBox();
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

public slots:

	void onNext();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	Radiobutton _group, _channel;
	int32 _aboutGroupWidth, _aboutGroupHeight;
	Text _aboutGroup, _aboutChannel;
	BoxButton _next, _cancel;

};

class GroupInfoBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	GroupInfoBox(CreatingGroupType creating, bool fromTypeChoose);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);

	bool animStep_photoOver(float64 ms);

	void setInnerFocus() {
		_title.setFocus();
	}

public slots:

	void onPhoto();
	void onPhotoReady(const QImage &img);

	void onNext();
	void onNameSubmit();
	void onDescriptionResized();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	QRect photoRect() const;

	void updateMaxHeight();
	void updateSelected(const QPoint &cursorGlobalPosition);
	CreatingGroupType _creating;

	anim::fvalue a_photoOver;
	Animation _a_photoOver;
	bool _photoOver;

	InputField _title;
	InputArea _description;

	QImage _photoBig;
	QPixmap _photoSmall;
	BoxButton _next, _cancel;

	// channel creation
	int32 _creationRequestId;
	ChannelData *_createdChannel;

	void creationDone(const MTPUpdates &updates);
	bool creationFail(const RPCError &e);
	void exportDone(const MTPExportedChatInvite &result);
};

class SetupChannelBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	SetupChannelBox(ChannelData *channel, bool existing = false);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);

	void closePressed();

	void setInnerFocus() {
		if (_link.isHidden()) {
			setFocus();
		} else {
			_link.setFocus();
		}
	}

public slots:

	void onSave();
	void onChange();
	void onCheck();

	void onPrivacyChange();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	void updateSelected(const QPoint &cursorGlobalPosition);
	bool animStep_goodFade(float64 ms);

	void onUpdateDone(const MTPBool &result);
	bool onUpdateFail(const RPCError &error);

	void onCheckDone(const MTPBool &result);
	bool onCheckFail(const RPCError &error);
	bool onFirstCheckFail(const RPCError &error);

	ChannelData *_channel;
	bool _existing;

	Radiobutton _public, _private;
	Checkbox _comments;
	int32 _aboutPublicWidth, _aboutPublicHeight;
	Text _aboutPublic, _aboutPrivate, _aboutComments;
	UsernameInput _link;
	QRect _invitationLink;
	bool _linkOver;
	BoxButton _save, _skip;

	bool _tooMuchUsernames;

	mtpRequestId _saveRequestId, _checkRequestId;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	QString _goodTextLink;
	anim::fvalue a_goodOpacity;
	Animation _a_goodFade;

	QTimer _checkTimer;
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
