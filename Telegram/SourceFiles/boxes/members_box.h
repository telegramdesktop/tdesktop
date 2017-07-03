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

#include "boxes/abstract_box.h"
#include "core/single_timer.h"
#include "ui/effects/round_checkbox.h"
#include "ui/widgets/buttons.h"

class ContactsBox;
class ConfirmBox;

enum class MembersFilter {
	Recent,
	Admins,
};
using MembersAlreadyIn = OrderedSet<UserData*>;

// Not used for now.
//
//class MembersAddButton : public Ui::RippleButton {
//public:
//	MembersAddButton(QWidget *parent, const style::TwoIconButton &st);
//
//protected:
//	void paintEvent(QPaintEvent *e) override;
//
//	QImage prepareRippleMask() const override;
//	QPoint prepareRippleStartPosition() const override;
//
//private:
//	const style::TwoIconButton &_st;
//
//};

class MembersBox : public BoxContent {
	Q_OBJECT

public:
	MembersBox(QWidget*, ChannelData *channel, MembersFilter filter);

public slots:
	void onAdminAdded();

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void onAdd();
	void refreshButtons();

	ChannelData *_channel = nullptr;
	MembersFilter _filter = MembersFilter::Recent;

	class Inner;
	QPointer<Inner> _inner;

	QPointer<ContactsBox> _addBox;

	object_ptr<SingleTimer> _loadTimer = { nullptr };

};

// This class is hold in header because it requires Qt preprocessing.
class MembersBox::Inner : public TWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, gsl::not_null<ChannelData*> channel, MembersFilter filter);

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	void refresh();

	ChannelData *channel() const;
	MembersFilter filter() const;

	bool isLoaded() const {
		return !_loading;
	}
	void clearSel();

	MembersAlreadyIn already() const;
	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	~Inner();

signals:
	void mustScrollTo(int ymin, int ymax);
	void loaded();

public slots:
	void load();

	void peerUpdated(PeerData *peer);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct RowData;
	enum class MemberRole {
		None,
		Self,
		Creator,
		Admin,
		Restricted,
		Kicked,
	};
	struct Member {
		Member(gsl::not_null<UserData*> user);
		Member(Member &&other);
		Member &operator=(Member &&other);
		~Member();

		gsl::not_null<UserData*> user;
		QDateTime date;
		MemberRole role = MemberRole::None;
		bool adminCanEdit = false;
		MTPChannelAdminRights adminRights;
		MTPChannelBannedRights restrictedRights;
		std::unique_ptr<RowData> data;
	};
	void addRipple(gsl::not_null<RowData*> data);
	void stopLastRipple(gsl::not_null<RowData*> data);
	void setPressed(int pressed);
	void chooseParticipant();
	void actionPressed(Member &row);

	void updateSelection();
	void loadProfilePhotos();

	void updateRowWithTop(int rowTop);
	int getSelectedRowTop() const;
	void updateSelectedRow();
	void ensureData(Member &row);

	void paintDialog(Painter &p, TimeMs ms, Member &row, bool selected, bool kickSelected);

	void membersReceived(const MTPchannels_ChannelParticipants &result, mtpRequestId req);
	bool membersFailed(const RPCError &error, mtpRequestId req);

	bool kickFail(const RPCError &error);
	void removeKicked(UserData *kicked);

	void clear();

	int _rowHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	gsl::not_null<ChannelData*> _channel;
	MembersFilter _filter;

	QString _kickText;
	TimeId _time = 0;
	int _kickWidth = 0;

	int _selected = -1;
	int _pressed = -1;
	int _kickSelected = -1;
	int _kickPressed = -1;
	bool _mouseSelection = false;

	QPointer<BoxContent> _kickBox;

	bool _loading = true;
	mtpRequestId _loadingRequestId = 0;
	std::vector<Member> _rows;

	int _aboutWidth = 0;
	Text _about;
	int _aboutHeight = 0;

	QPoint _lastMousePos;

};
