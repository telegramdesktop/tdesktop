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
#include "core/single_timer.h"
#include "ui/effects/round_image_checkbox.h"

class ContactsBox;
class ConfirmBox;

enum class MembersFilter {
	Recent,
	Admins,
};
using MembersAlreadyIn = OrderedSet<UserData*>;

class MembersBox : public ItemListBox {
	Q_OBJECT

public:
	MembersBox(ChannelData *channel, MembersFilter filter);

public slots:
	void onScroll();

	void onAdd();
	void onAdminAdded();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner;
	ChildWidget<Inner> _inner;

	ContactsBox *_addBox = nullptr;

	SingleTimer _loadTimer;

};

// This class is hold in header because it requires Qt preprocessing.
class MembersBox::Inner : public ScrolledWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, ChannelData *channel, MembersFilter filter);

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	void loadProfilePhotos(int32 yFrom);
	void chooseParticipant();

	void refresh();

	ChannelData *channel() const;
	MembersFilter filter() const;

	bool isLoaded() const {
		return !_loading;
	}
	void clearSel();

	MembersAlreadyIn already() const;

	~Inner();

signals:
	void mustScrollTo(int ymin, int ymax);
	void addRequested();
	void loaded();

public slots:
	void load();

	void updateSel();
	void peerUpdated(PeerData *peer);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void onKickConfirm();
	void onKickBoxDestroyed(QObject *obj);

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct MemberData {
		Text name;
		QString online;
		bool onlineColor;
		bool canKick;
	};

	void updateSelectedRow();
	MemberData *data(int32 index);

	void paintDialog(Painter &p, PeerData *peer, MemberData *data, bool sel, bool kickSel, bool kickDown);

	void membersReceived(const MTPchannels_ChannelParticipants &result, mtpRequestId req);
	bool membersFailed(const RPCError &error, mtpRequestId req);

	void kickDone(const MTPUpdates &result, mtpRequestId req);
	void kickAdminDone(const MTPUpdates &result, mtpRequestId req);
	bool kickFail(const RPCError &error, mtpRequestId req);
	void removeKicked();

	void clear();

	int32 _rowHeight, _newItemHeight;
	bool _newItemSel;

	ChannelData *_channel;
	MembersFilter _filter;

	QString _kickText;
	int32 _time, _kickWidth;

	int32 _sel, _kickSel, _kickDown;
	bool _mouseSel;

	UserData *_kickConfirm;
	mtpRequestId _kickRequestId;

	ConfirmBox *_kickBox;

	enum class MemberRole {
		None,
		Self,
		Creator,
		Editor,
		Moderator,
		Kicked
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

	int32 _aboutWidth;
	Text _about;
	int32 _aboutHeight;

	QPoint _lastMousePos;

};
