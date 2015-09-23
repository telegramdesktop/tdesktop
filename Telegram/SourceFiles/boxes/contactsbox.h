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

enum CreatingGroupType {
	CreatingGroupNone,
	CreatingGroupGroup,
	CreatingGroupChannel,
};

enum MembersFilter {
	MembersFilterRecent,
	MembersFilterAdmins,
};
typedef QMap<UserData*, bool> MembersAlreadyIn;

class ConfirmBox;
class ContactsInner : public TWidget, public RPCSender {
	Q_OBJECT

private:

	struct ContactData;

public:

	ContactsInner(CreatingGroupType creating = CreatingGroupNone);
	ContactsInner(ChannelData *channel, MembersFilter channelFilter = MembersFilterRecent, const MembersAlreadyIn &already = MembersAlreadyIn());
	ContactsInner(ChatData *chat);
	ContactsInner(UserData *bot);
	void init();

	void paintEvent(QPaintEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void resizeEvent(QResizeEvent *e);
	
	void paintDialog(Painter &p, PeerData *peer, ContactData *data, bool sel);
	void updateFilter(QString filter = QString());

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	QVector<UserData*> selected();
	QVector<MTPInputUser> selectedInputs();
	PeerData *selectedUser();

	void loadProfilePhotos(int32 yFrom);
	void chooseParticipant();
	void changeCheckState(DialogRow *row);
	void changeCheckState(ContactData *data, PeerData *peer);

	void peopleReceived(const QString &query, const QVector<MTPPeer> &people);

	void refresh();

	ChatData *chat() const;
	ChannelData *channel() const;
	MembersFilter channelFilter() const;
	UserData *bot() const;
	CreatingGroupType creating() const;

	int32 selectedCount() const;
	bool hasAlreadyMembersInChannel() const {
		return !_already.isEmpty();
	}

	~ContactsInner();

signals:

	void mustScrollTo(int ymin, int ymax);
	void selectAllQuery();
	void searchByUsername();
	void chosenChanged();
	void adminAdded();

public slots:

	void onDialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);

	void updateSel();
	void peerUpdated(PeerData *peer);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);

	void onAddBot();
	void onAddAdmin();
	void onNoAddAdminBox(QObject *obj);

private:

	void addAdminDone(const MTPBool &result, mtpRequestId req);
	bool addAdminFail(const RPCError &error, mtpRequestId req);

	ChatData *_chat;
	ChannelData *_channel;
	MembersFilter _channelFilter;
	UserData *_bot;
	CreatingGroupType _creating;
	MembersAlreadyIn _already;

	ChatData *_addToChat;
	UserData *_addAdmin;
	mtpRequestId _addAdminRequestId;
	ConfirmBox *_addAdminBox;
	
	int32 _time;

	DialogsIndexed *_contacts;
	DialogRow *_sel;
	QString _filter;
	typedef QVector<DialogRow*> FilteredDialogs;
	FilteredDialogs _filtered;
	int32 _filteredSel;
	bool _mouseSel;

	int32 _selCount;

	struct ContactData {
		Text name;
		QString online;
		bool inchat;
		bool check;
	};
	typedef QMap<PeerData*, ContactData*> ContactsData;
	ContactsData _contactsData;
	typedef QMap<PeerData*, bool> CheckedContacts;
	CheckedContacts _checkedContacts;

	ContactData *contactData(DialogRow *row);

	bool _searching;
	QString _lastQuery;
	typedef QVector<PeerData*> ByUsernameRows;
	typedef QVector<ContactData*> ByUsernameDatas;
	ByUsernameRows _byUsername, _byUsernameFiltered;
	ByUsernameDatas d_byUsername, d_byUsernameFiltered; // filtered is partly subset of d_byUsername, partly subset of _byUsernameDatas
	ByUsernameDatas _byUsernameDatas;
	int32 _byUsernameSel;

	QPoint _lastMousePos;
	LinkButton _addContactLnk;

};

class ContactsBox : public ItemListBox, public RPCSender {
	Q_OBJECT

public:

	ContactsBox();
	ContactsBox(const QString &name, const QImage &photo); // group creation
	ContactsBox(ChannelData *channel); // channel setup
	ContactsBox(ChannelData *channel, MembersFilter filter, const MembersAlreadyIn &already);
	ContactsBox(ChatData *chat);
	ContactsBox(UserData *bot);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void closePressed();

	void setInnerFocus() {
		_filter.setFocus();
	}

signals:

	void adminAdded();

public slots:

	void onFilterUpdate();
	void onScroll();

	void onAdd();
	void onInvite();
	void onCreate();

	bool onSearchByUsername(bool searchCache = false);
	void onNeedSearchByUsername();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	void init();

	ContactsInner _inner;
	FlatButton _addContact;
	FlatInput _filter;

	FlatButton _next, _cancel;
	MembersFilter _membersFilter;

	void peopleReceived(const MTPcontacts_Found &result, mtpRequestId req);
	bool peopleFailed(const RPCError &error, mtpRequestId req);

	QTimer _searchTimer;
	QString _peopleQuery;
	bool _peopleFull;
	mtpRequestId _peopleRequest;

	typedef QMap<QString, MTPcontacts_Found> PeopleCache;
	PeopleCache _peopleCache;

	typedef QMap<mtpRequestId, QString> PeopleQueries;
	PeopleQueries _peopleQueries;

	// group creation
	int32 _creationRequestId;
	QString _creationName;
	QImage _creationPhoto;

	void creationDone(const MTPUpdates &updates);
	bool creationFail(const RPCError &e);
};

class MembersInner : public TWidget, public RPCSender {
	Q_OBJECT

private:

	struct MemberData;

public:

	MembersInner(ChannelData *channel, MembersFilter filter);

	void paintEvent(QPaintEvent *e);
	void enterEvent(QEvent *e);
	void leaveEvent(QEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);

	void paintDialog(Painter &p, PeerData *peer, MemberData *data, bool sel, bool kickSel, bool kickDown);

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	void loadProfilePhotos(int32 yFrom);
	void chooseParticipant();

	void refresh();

	ChannelData *channel() const;
	MembersFilter filter() const;

	void load();
	bool isLoaded() const {
		return !_loading;
	}

	QMap<UserData*, bool> already() const;

	~MembersInner();

signals:

	void mustScrollTo(int ymin, int ymax);

	void loaded();

public slots:

	void updateSel();
	void peerUpdated(PeerData *peer);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void onKickConfirm();
	void onKickBoxDestroyed(QObject *obj);

private:

	void clearSel();
	MemberData *data(int32 index);

	void membersReceived(const MTPchannels_ChannelParticipants &result, mtpRequestId req);
	bool membersFailed(const RPCError &error, mtpRequestId req);

	void kickDone(const MTPUpdates &result, mtpRequestId req);
	void kickAdminDone(const MTPBool &result, mtpRequestId req);
	bool kickFail(const RPCError &error, mtpRequestId req);
	void removeKicked();

	void clear();

	ChannelData *_channel;
	MembersFilter _filter;

	QString _kickText;
	int32 _time, _kickWidth;

	int32 _sel, _kickSel, _kickDown;
	bool _mouseSel;

	UserData *_kickConfirm;
	mtpRequestId _kickRequestId;

	ConfirmBox *_kickBox;

	enum MemberRole {
		MemberRoleNone,
		MemberRoleSelf,
		MemberRoleCreator,
		MemberRoleEditor,
		MemberRoleModerator,
		MemberRoleKicked
	};

	struct MemberData {
		Text name;
		QString online;
		bool canKick;
	};

	bool _loading;
	mtpRequestId _loadingRequestId;
	typedef QVector<UserData*> MemberRows;
	typedef QVector<QDateTime> MemberDates;
	typedef QVector<MemberRole> MemberRoles;
	typedef QVector<MemberData*> MemberDatas;
	MemberRows _rows;
	MemberDates _dates;
	MemberRoles _roles;
	MemberDatas _datas;

	QPoint _lastMousePos;

};

class MembersBox : public ItemListBox {
	Q_OBJECT

public:

	MembersBox(ChannelData *channel, MembersFilter filter);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);

	void setInnerFocus() {
		setFocus();
	}

public slots:

	void onLoaded();
	void onScroll();

	void onAdd();
	void onAdminAdded();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	MembersInner _inner;
	FlatButton _add, _done;

	ContactsBox *_addBox;
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

	FlatRadiobutton _group, _channel;
	int32 _aboutGroupWidth, _aboutGroupHeight;
	Text _aboutGroup, _aboutChannel;
	FlatButton _next, _cancel;

};

class GroupInfoBox : public AbstractBox, public RPCSender {
	Q_OBJECT

public:

	GroupInfoBox(CreatingGroupType creating, bool fromTypeChoose);
	void keyPressEvent(QKeyEvent *e);
	void paintEvent(QPaintEvent *e);
	void resizeEvent(QResizeEvent *e);
	void mouseMoveEvent(QMouseEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void leaveEvent(QEvent *e);

	bool eventFilter(QObject *obj, QEvent *e);

	bool descriptionAnimStep(float64 ms);
	bool photoAnimStep(float64 ms);

	void setInnerFocus() {
		_name.setFocus();
	}

public slots:

	void onPhoto();
	void onPhotoReady(const QImage &img);

	void onNext();
	void onDescriptionResized();

protected:

	void hideAll();
	void showAll();
	void showDone();

private:

	QRect descriptionRect() const;
	QRect photoRect() const;

	void updateMaxHeight();
	void updateSelected(const QPoint &cursorGlobalPosition);
	CreatingGroupType _creating;

	anim::fvalue a_photoOver;
	Animation a_photo;
	bool _photoOver, _descriptionOver;

	anim::cvalue a_descriptionBg, a_descriptionBorder;
	Animation a_description;

	FlatInput _name;
	FlatButton _photo;
	FlatTextarea _description;
	QImage _photoBig;
	QPixmap _photoSmall;
	FlatButton _next, _cancel;

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
	bool goodAnimStep(float64 ms);

	ChannelData *_channel;
	bool _existing;

	FlatRadiobutton _public, _private;
	FlatCheckbox _comments;
	int32 _aboutPublicWidth, _aboutPublicHeight;
	Text _aboutPublic, _aboutPrivate, _aboutComments;
	QString _linkPlaceholder;
	UsernameInput _link;
	QRect _invitationLink;
	bool _linkOver;
	FlatButton _save, _skip;

	void onUpdateDone(const MTPBool &result);
	bool onUpdateFail(const RPCError &error);

	void onCheckDone(const MTPBool &result);
	bool onCheckFail(const RPCError &error);
	bool onFirstCheckFail(const RPCError &error);

	bool _tooMuchUsernames;

	mtpRequestId _saveRequestId, _checkRequestId;
	QString _sentUsername, _checkUsername, _errorText, _goodText;

	QString _goodTextLink;
	anim::fvalue a_goodOpacity;
	Animation a_good;

	QTimer _checkTimer;
};
