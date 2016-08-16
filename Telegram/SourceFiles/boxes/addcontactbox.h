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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "abstractbox.h"
#include "core/lambda_wrap.h"

class FlatLabel;
class ConfirmBox;

class AddContactBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	AddContactBox(QString fname = QString(), QString lname = QString(), QString phone = QString());
	AddContactBox(UserData *user);

public slots:
	void onSubmit();
	void onSave();
	void onRetry();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void showAll() override;
	void doSetInnerFocus() override;

private:
	void onImportDone(const MTPcontacts_ImportedContacts &res);

	void onSaveUserDone(const MTPcontacts_ImportedContacts &res);
	bool onSaveUserFail(const RPCError &e);

	void initBox();

	UserData *_user = nullptr;
	QString _boxTitle;

	BoxButton _save, _cancel, _retry;
	InputField _first, _last;
	PhoneInput _phone;

	bool _invertOrder;

	uint64 _contactId = 0;

	mtpRequestId _addRequest = 0;
	QString _sentName;
};

class NewGroupBox : public AbstractBox {
	Q_OBJECT

public:
	NewGroupBox();

public slots:
	void onNext();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void showAll() override;

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

public slots:
	void onPhoto();
	void onPhotoReady(const QImage &img);

	void onNext();
	void onNameSubmit();
	void onDescriptionResized();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *e) override;

	void showAll() override;
	void doSetInnerFocus() override;

private:
	void step_photoOver(float64 ms, bool timer);

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

public slots:
	void onSave();
	void onChange();
	void onCheck();

	void onPrivacyChange();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *e) override;

	void closePressed() override;
	void showAll() override;
	void doSetInnerFocus() override;

private:
	void updateSelected(const QPoint &cursorGlobalPosition);
	void step_goodFade(float64 ms, bool timer);

	void onUpdateDone(const MTPBool &result);
	bool onUpdateFail(const RPCError &error);

	void onCheckDone(const MTPBool &result);
	bool onCheckFail(const RPCError &error);
	bool onFirstCheckFail(const RPCError &error);

	void updateMaxHeight();

	void showRevokePublicLinkBoxForEdit();

	ChannelData *_channel;
	bool _existing;

	Radiobutton _public, _private;
	int32 _aboutPublicWidth, _aboutPublicHeight;
	Text _aboutPublic, _aboutPrivate;
	UsernameInput _link;
	QRect _invitationLink;
	bool _linkOver;
	BoxButton _save, _skip;

	bool _tooMuchUsernames = false;

	mtpRequestId _saveRequestId = 0;
	mtpRequestId _checkRequestId = 0;
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

public slots:
	void onSave();
	void onSubmit();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void showAll() override;
	void doSetInnerFocus() override;

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

public slots:
	void peerUpdated(PeerData *peer);

	void onSave();
	void onDescriptionResized();
	void onPublicLink();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void showAll() override;
	void doSetInnerFocus() override;

private:
	void updateMaxHeight();

	void onSaveTitleDone(const MTPUpdates &updates);
	void onSaveDescriptionDone(const MTPBool &result);
	void onSaveSignDone(const MTPUpdates &updates);
	bool onSaveFail(const RPCError &e, mtpRequestId req);

	void saveDescription();
	void saveSign();

	ChannelData *_channel;

	BoxButton _save, _cancel;
	InputField _title;
	InputArea _description;
	Checkbox _sign;

	LinkButton _publicLink;

	mtpRequestId _saveTitleRequestId, _saveDescriptionRequestId, _saveSignRequestId;
	QString _sentTitle, _sentDescription;

};

class RevokePublicLinkBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	RevokePublicLinkBox(base::lambda_unique<void()> &&revokeCallback);

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateMaxHeight();
	void updateSelected();

	struct ChatRow {
		PeerData *peer;
		Text name, status;
	};
	void paintChat(Painter &p, const ChatRow &row, bool selected, bool pressed) const;

	void getPublicDone(const MTPmessages_Chats &result);
	bool getPublicFail(const RPCError &error);

	void revokeLinkDone(const MTPBool &result);
	bool revokeLinkFail(const RPCError &error);

	PeerData *_selected = nullptr;
	PeerData *_pressed = nullptr;

	QVector<ChatRow> _rows;

	int _rowsTop = 0;
	int _rowHeight = 0;
	int _revokeWidth = 0;

	ChildWidget<FlatLabel> _aboutRevoke;
	ChildWidget<BoxButton> _cancel;

	base::lambda_unique<void()> _revokeCallback;
	mtpRequestId _revokeRequestId = 0;
	QPointer<ConfirmBox> weakRevokeConfirmBox;

};
