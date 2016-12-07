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

#include "boxes/abstractbox.h"
#include "ui/filedialog.h"

class ConfirmBox;

namespace Ui {
class FlatLabel;
class InputField;
class PhoneInput;
class InputArea;
class UsernameInput;
class Checkbox;
class Radiobutton;
class LinkButton;
class RoundButton;
class NewAvatarButton;
} // namespace Ui

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

	void doSetInnerFocus() override;

private:
	void onImportDone(const MTPcontacts_ImportedContacts &res);

	void onSaveUserDone(const MTPcontacts_ImportedContacts &res);
	bool onSaveUserFail(const RPCError &e);

	void initBox();

	UserData *_user = nullptr;

	ChildWidget<Ui::InputField> _first;
	ChildWidget<Ui::InputField> _last;
	ChildWidget<Ui::PhoneInput> _phone;

	ChildWidget<Ui::RoundButton> _save;
	ChildWidget<Ui::RoundButton> _cancel;
	ChildWidget<Ui::RoundButton> _retry;

	bool _invertOrder;

	uint64 _contactId = 0;

	mtpRequestId _addRequest = 0;
	QString _sentName;
};

class GroupInfoBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	GroupInfoBox(CreatingGroupType creating, bool fromTypeChoose);

public slots:
	void onPhotoReady(const QImage &img);

	void onNext();
	void onNameSubmit();
	void onDescriptionResized();

protected:
	void resizeEvent(QResizeEvent *e) override;

	void doSetInnerFocus() override;

private:
	void notifyFileQueryUpdated(const FileDialog::QueryUpdate &update);

	void updateMaxHeight();
	void updateSelected(const QPoint &cursorGlobalPosition);
	CreatingGroupType _creating;

	ChildWidget<Ui::NewAvatarButton> _photo;
	ChildWidget<Ui::InputField> _title;
	ChildWidget<Ui::InputArea> _description;

	QImage _photoImage;

	ChildWidget<Ui::RoundButton> _next;
	ChildWidget<Ui::RoundButton> _cancel;

	// channel creation
	mtpRequestId _creationRequestId = 0;
	ChannelData *_createdChannel = nullptr;

	FileDialog::QueryId _setPhotoFileQueryId = 0;

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
	void doSetInnerFocus() override;

private:
	void updateSelected(const QPoint &cursorGlobalPosition);

	void onUpdateDone(const MTPBool &result);
	bool onUpdateFail(const RPCError &error);

	void onCheckDone(const MTPBool &result);
	bool onCheckFail(const RPCError &error);
	bool onFirstCheckFail(const RPCError &error);

	void updateMaxHeight();

	void showRevokePublicLinkBoxForEdit();

	ChannelData *_channel;
	bool _existing;

	ChildWidget<Ui::Radiobutton> _public;
	ChildWidget<Ui::Radiobutton> _private;
	int32 _aboutPublicWidth, _aboutPublicHeight;
	Text _aboutPublic, _aboutPrivate;

	ChildWidget<Ui::UsernameInput> _link;

	QRect _invitationLink;
	bool _linkOver;

	ChildWidget<Ui::RoundButton> _save;
	ChildWidget<Ui::RoundButton> _skip;

	bool _tooMuchUsernames = false;

	mtpRequestId _saveRequestId = 0;
	mtpRequestId _checkRequestId = 0;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	QString _goodTextLink;
	Animation _a_goodOpacity;

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
	void resizeEvent(QResizeEvent *e) override;

	void doSetInnerFocus() override;

private:
	void onSaveSelfDone(const MTPUser &user);
	bool onSaveSelfFail(const RPCError &error);

	void onSaveChatDone(const MTPUpdates &updates);
	bool onSaveChatFail(const RPCError &e);

	PeerData *_peer;

	ChildWidget<Ui::InputField> _first;
	ChildWidget<Ui::InputField> _last;

	ChildWidget<Ui::RoundButton> _save;
	ChildWidget<Ui::RoundButton> _cancel;

	bool _invertOrder = false;

	mtpRequestId _requestId = 0;
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
	void resizeEvent(QResizeEvent *e) override;

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

	ChildWidget<Ui::InputField> _title;
	ChildWidget<Ui::InputArea> _description;
	ChildWidget<Ui::Checkbox> _sign;

	ChildWidget<Ui::LinkButton> _publicLink;

	ChildWidget<Ui::RoundButton> _save;
	ChildWidget<Ui::RoundButton> _cancel;

	mtpRequestId _saveTitleRequestId = 0;
	mtpRequestId _saveDescriptionRequestId = 0;
	mtpRequestId _saveSignRequestId = 0;

	QString _sentTitle, _sentDescription;

};

class RevokePublicLinkBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:
	RevokePublicLinkBox(base::lambda<void()> &&revokeCallback);

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

	ChildWidget<Ui::FlatLabel> _aboutRevoke;
	ChildWidget<Ui::RoundButton> _cancel;

	base::lambda<void()> _revokeCallback;
	mtpRequestId _revokeRequestId = 0;
	QPointer<ConfirmBox> weakRevokeConfirmBox;

};
