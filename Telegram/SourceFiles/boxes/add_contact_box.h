/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "styles/style_widgets.h"

class ConfirmBox;
class PeerListBox;

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
class UserpicButton;
} // namespace Ui

enum class PeerFloodType {
	Send,
	InviteGroup,
	InviteChannel,
};
QString PeerFloodErrorText(PeerFloodType type);

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

class GroupInfoBox : public BoxContent, private MTP::Sender {
	Q_OBJECT

public:
	GroupInfoBox(QWidget*, CreatingGroupType creating, bool fromTypeChoose);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onNext();
	void onNameSubmit();
	void onDescriptionResized();
	void onClose() {
		closeBox();
	}

private:
	void createChannel(const QString &title, const QString &description);
	void createGroup(not_null<PeerListBox*> selectUsersBox, const QString &title, const std::vector<not_null<PeerData*>> &users);

	void updateMaxHeight();
	void updateSelected(const QPoint &cursorGlobalPosition);

	CreatingGroupType _creating;
	bool _fromTypeChoose = false;

	object_ptr<Ui::UserpicButton> _photo = { nullptr };
	object_ptr<Ui::InputField> _title = { nullptr };
	object_ptr<Ui::InputArea> _description = { nullptr };

	// group / channel creation
	mtpRequestId _creationRequestId = 0;
	ChannelData *_createdChannel = nullptr;

};

class SetupChannelBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	SetupChannelBox(QWidget*, ChannelData *channel, bool existing = false);

	void setInnerFocus() override;

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
	void showAddContactsToChannelBox() const;

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

class EditNameBox : public BoxContent, public RPCSender {
public:
	EditNameBox(QWidget*, not_null<UserData*> user);

protected:
	void setInnerFocus() override;
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void submit();
	void save();
	void saveSelfDone(const MTPUser &user);
	bool saveSelfFail(const RPCError &error);

	not_null<UserData*> _user;

	object_ptr<Ui::InputField> _first;
	object_ptr<Ui::InputField> _last;

	bool _invertOrder = false;

	mtpRequestId _requestId = 0;
	QString _sentName;

};

class EditBioBox : public BoxContent, private MTP::Sender {
public:
	EditBioBox(QWidget*, not_null<UserData*> self);

protected:
	void setInnerFocus() override;
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void updateMaxHeight();
	void handleBioUpdated();
	void save();

	style::InputField _dynamicFieldStyle;
	not_null<UserData*> _self;

	object_ptr<Ui::InputArea> _bio;
	object_ptr<Ui::FlatLabel> _countdown;
	object_ptr<Ui::FlatLabel> _about;
	mtpRequestId _requestId = 0;
	QString _sentBio;

};

class EditChannelBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	EditChannelBox(QWidget*, not_null<ChannelData*> channel);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private slots:
	void onSave();
	void onDescriptionResized();
	void onPublicLink();
	void onClose() {
		closeBox();
	}

private:
	void updateMaxHeight();
	bool canEditSignatures() const;
	bool canEditInvites() const;
	void handleChannelNameChange();

	void onSaveTitleDone(const MTPUpdates &result);
	void onSaveDescriptionDone(const MTPBool &result);
	void onSaveSignDone(const MTPUpdates &result);
	void onSaveInvitesDone(const MTPUpdates &result);
	bool onSaveFail(const RPCError &error, mtpRequestId req);

	void saveDescription();
	void saveSign();
	void saveInvites();

	not_null<ChannelData*> _channel;

	object_ptr<Ui::InputField> _title;
	object_ptr<Ui::InputArea> _description;
	object_ptr<Ui::Checkbox> _sign;

	enum class Invites {
		Everybody,
		OnlyAdmins,
	};
	std::shared_ptr<Ui::RadioenumGroup<Invites>> _inviteGroup;
	object_ptr<Ui::Radioenum<Invites>> _inviteEverybody;
	object_ptr<Ui::Radioenum<Invites>> _inviteOnlyAdmins;

	object_ptr<Ui::LinkButton> _publicLink;

	mtpRequestId _saveTitleRequestId = 0;
	mtpRequestId _saveDescriptionRequestId = 0;
	mtpRequestId _saveSignRequestId = 0;
	mtpRequestId _saveInvitesRequestId = 0;

	QString _sentTitle, _sentDescription;

};

class RevokePublicLinkBox : public BoxContent, public RPCSender {
public:
	RevokePublicLinkBox(QWidget*, base::lambda<void()> revokeCallback);

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<Ui::FlatLabel> _aboutRevoke;

	class Inner;
	QPointer<Inner> _inner;

	int _innerTop = 0;
	base::lambda<void()> _revokeCallback;

};
