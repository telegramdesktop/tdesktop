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

#include "profile/profile_block_widget.h"

class FlatLabel;

namespace Ui {
class LeftOutlineButton;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {

class MembersWidget : public BlockWidget {
	Q_OBJECT

public:
	enum class TitleVisibility {
		Visible,
		Hidden,
	};
	MembersWidget(QWidget *parent, PeerData *peer, TitleVisibility titleVisibility = TitleVisibility::Visible);

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;
	int onlineCount() const {
		return _onlineCount;
	}

	~MembersWidget();

protected:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

	void paintContents(Painter &p) override;

	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEvent(QEvent *e) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override {
		enterEvent(e);
	}
	void leaveEvent(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override {
		leaveEvent(e);
	}

signals:
	void onlineCountUpdated(int onlineCount);

private slots:
	void onUpdateOnlineDisplay();

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);
	void repaintCallback();

	void preloadUserPhotos();

	void refreshMembers();
	void fillChatMembers(ChatData *chat);
	void fillMegagroupMembers(ChannelData *megagroup);
	void sortMembers();
	void updateOnlineCount();
	void checkSelfAdmin(ChatData *chat);
	void checkSelfAdmin(ChannelData *megagroup);
	void refreshLimitReached();

	bool limitReachedHook(const ClickHandlerPtr &handler, Qt::MouseButton button);

	void refreshVisibility();
	void updateSelection();
	void setSelected(int selected, bool selectedKick);
	void repaintSelectedRow();
	void refreshUserOnline(UserData *user);

	int getListLeft() const;
	int getListTop() const;

	struct Member {
		Member(UserData *user) : user(user) {
		}
		UserData *user;
		Text name;
		QString onlineText;
		TimeId onlineTextTill = 0;
		TimeId onlineTill = 0;
		TimeId onlineForSort = 0;
		bool online = false;
		bool isAdmin = false;
		bool canBeKicked = false;
	};
	Member *getMember(UserData *user);
	void paintMember(Painter &p, int x, int y, Member *member, bool selected, bool selectedKick);
	void paintOutlinedRect(Painter &p, int x, int y, int w, int h) const;
	void setMemberFlags(Member *member, ChatData *chat);
	Member *addUser(ChatData *chat, UserData *user);
	void setMemberFlags(Member *member, ChannelData *megagroup);
	Member *addUser(ChannelData *megagroup, UserData *user);
	bool addUsersToEnd(ChannelData *megagroup);

	ChildWidget<FlatLabel> _limitReachedInfo = { nullptr };

	QList<Member*> _list;
	QMap<UserData*, Member*> _membersByUser;
	bool _sortByOnline = false;
	TimeId _now = 0;

	int _visibleTop = 0;
	int _visibleBottom = 0;

	int _selected = -1;
	int _pressed = -1;
	bool _selectedKick = false;
	bool _pressedKick = false;
	QPoint _mousePosition;

	int _onlineCount = 0;
	TimeId _updateOnlineAt = 0;
	QTimer _updateOnlineTimer;

	int _removeWidth = 0;

};

class ChannelMembersWidget : public BlockWidget {
	Q_OBJECT

public:
	ChannelMembersWidget(QWidget *parent, PeerData *peer);

protected:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private slots:
	void onAdmins();
	void onMembers();

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void refreshButtons();
	void refreshAdmins();
	void refreshMembers();
	void refreshVisibility();

	void addButton(const QString &text, ChildWidget<Ui::LeftOutlineButton> *button, const char *slot);

	ChildWidget<Ui::LeftOutlineButton> _admins = { nullptr };
	ChildWidget<Ui::LeftOutlineButton> _members = { nullptr };

};

} // namespace Profile
