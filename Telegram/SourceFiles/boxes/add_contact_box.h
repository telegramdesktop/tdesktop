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

#include "boxes/abstractbox.h"

class ConfirmBox;

namespace Ui {
class FlatLabel;
class InputField;
class PhoneInput;
class InputArea;
class UsernameInput;
class Checkbox;
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
class LinkButton;
class NewAvatarButton;
} // namespace Ui

class AddContactBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	AddContactBox(QWidget*, QString fname = QString(), QString lname = QString(), QString phone = QString());
	AddContactBox(QWidget*, UserData *user);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setInnerFocus() override;

private slots:
	void onSubmit();
	void onSave();
	void onRetry();

private:
	void updateButtons();
	void onImportDone(const MTPcontacts_ImportedContacts &res);

	void onSaveUserDone(const MTPcontacts_ImportedContacts &res);
	bool onSaveUserFail(const RPCError &e);

	UserData *_user = nullptr;

	object_ptr<Ui::InputField> _first;
	object_ptr<Ui::InputField> _last;
	object_ptr<Ui::PhoneInput> _phone;

	bool _retrying = false;
	bool _invertOrder = false;

	uint64 _contactId = 0;

	mtpRequestId _addRequest = 0;
	QString _sentName;

};

class GroupInfoBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	GroupInfoBox(QWidget*, CreatingGroupType creating, bool fromTypeChoose);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onPhotoReady(const QImage &img);

	void onNext();
	void onNameSubmit();
	void onDescriptionResized();
	void onClose() {
		closeBox();
	}

private:
	void setupPhotoButton();

	void creationDone(const MTPUpdates &updates);
	bool creationFail(const RPCError &e);
	void exportDone(const MTPExportedChatInvite &result);

	void updateMaxHeight();
	void updateSelected(const QPoint &cursorGlobalPosition);

	CreatingGroupType _creating;
	bool _fromTypeChoose = false;

	object_ptr<Ui::NewAvatarButton> _photo;
	object_ptr<Ui::InputField> _title;
	object_ptr<Ui::InputArea> _description = { nullptr };

	QImage _photoImage;

	// channel creation
	mtpRequestId _creationRequestId = 0;
	ChannelData *_createdChannel = nullptr;

};

class SetupChannelBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	SetupChannelBox(QWidget*, ChannelData *channel, bool existing = false);

	void setInnerFocus() override;
	void closeHook() override;

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private slots:
	void onSave();
	void onChange();
	void onCheck();

private:
	enum class Privacy {
		Public,
		Private,
	};
	void privacyChanged(Privacy value);
	void updateSelected(const QPoint &cursorGlobalPosition);

	void onUpdateDone(const MTPBool &result);
	bool onUpdateFail(const RPCError &error);

	void onCheckDone(const MTPBool &result);
	bool onCheckFail(const RPCError &error);
	bool onFirstCheckFail(const RPCError &error);

	void updateMaxHeight();

	void showRevokePublicLinkBoxForEdit();

	ChannelData *_channel = nullptr;
	bool _existing = false;

	std::shared_ptr<Ui::RadioenumGroup<Privacy>> _privacyGroup;
	object_ptr<Ui::Radioenum<Privacy>> _public;
	object_ptr<Ui::Radioenum<Privacy>> _private;
	int32 _aboutPublicWidth, _aboutPublicHeight;
	Text _aboutPublic, _aboutPrivate;

	object_ptr<Ui::UsernameInput> _link;

	QRect _invitationLink;
	bool _linkOver = false;
	bool _tooMuchUsernames = false;

	mtpRequestId _saveRequestId = 0;
	mtpRequestId _checkRequestId = 0;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	QTimer _checkTimer;

};

class EditNameTitleBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	EditNameTitleBox(QWidget*, PeerData *peer);

protected:
	void setInnerFocus() override;
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onSave();
	void onSubmit();

private:
	void onSaveSelfDone(const MTPUser &user);
	bool onSaveSelfFail(const RPCError &error);

	void onSaveChatDone(const MTPUpdates &updates);
	bool onSaveChatFail(const RPCError &e);

	PeerData *_peer;

	object_ptr<Ui::InputField> _first;
	object_ptr<Ui::InputField> _last;

	bool _invertOrder = false;

	mtpRequestId _requestId = 0;
	QString _sentName;

};

class EditChannelBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	EditChannelBox(QWidget*, ChannelData *channel);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void peerUpdated(PeerData *peer);

	void onSave();
	void onDescriptionResized();
	void onPublicLink();
	void onClose() {
		closeBox();
	}

private:
	void updateMaxHeight();

	void onSaveTitleDone(const MTPUpdates &updates);
	void onSaveDescriptionDone(const MTPBool &result);
	void onSaveSignDone(const MTPUpdates &updates);
	bool onSaveFail(const RPCError &e, mtpRequestId req);

	void saveDescription();
	void saveSign();

	ChannelData *_channel;

	object_ptr<Ui::InputField> _title;
	object_ptr<Ui::InputArea> _description;
	object_ptr<Ui::Checkbox> _sign;

	object_ptr<Ui::LinkButton> _publicLink;

	mtpRequestId _saveTitleRequestId = 0;
	mtpRequestId _saveDescriptionRequestId = 0;
	mtpRequestId _saveSignRequestId = 0;

	QString _sentTitle, _sentDescription;

};

class RevokePublicLinkBox : public BoxContent, public RPCSender {
public:
	RevokePublicLinkBox(QWidget*, base::lambda<void()> revokeCallback);

protected:
	void prepare() override;

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
	void paintChat(Painter &p, const ChatRow &row, bool selected) const;

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

	object_ptr<Ui::FlatLabel> _aboutRevoke;

	base::lambda<void()> _revokeCallback;
	mtpRequestId _revokeRequestId = 0;
	QPointer<ConfirmBox> _weakRevokeConfirmBox;

};
