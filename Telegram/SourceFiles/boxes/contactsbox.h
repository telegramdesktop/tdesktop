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
#include "boxes/members_box.h"

namespace Dialogs {
class Row;
class IndexedList;
} // namespace Dialogs

namespace Ui {
class MultiSelect;
template <typename Widget>
class WidgetSlideWrap;
} // namespace Ui

QString cantInviteError();

inline Ui::RoundImageCheckbox::PaintRoundImage PaintUserpicCallback(PeerData *peer) {
	return [peer](Painter &p, int x, int y, int outerWidth, int size) {
		peer->paintUserpicLeft(p, size, x, y, outerWidth);
	};
}

class ContactsBox : public ItemListBox, public RPCSender {
	Q_OBJECT

public:
	ContactsBox();
	ContactsBox(const QString &name, const QImage &photo); // group creation
	ContactsBox(ChannelData *channel); // channel setup
	ContactsBox(ChannelData *channel, MembersFilter filter, const MembersAlreadyIn &already);
	ContactsBox(ChatData *chat, MembersFilter filter);
	ContactsBox(UserData *bot);

signals:
	void adminAdded();

private slots:
	void onScroll();

	void onInvite();
	void onCreate();
	void onSaveAdmins();

	void onSubmit();

	bool onSearchByUsername(bool searchCache = false);
	void onNeedSearchByUsername();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void closePressed() override;
	void showAll() override;
	void doSetInnerFocus() override;

private:
	void init();
	int getTopScrollSkip() const;
	void updateScrollSkips();
	void onFilterUpdate(const QString &filter);
	void onPeerSelectedChanged(PeerData *peer, bool checked);
	void addPeerToMultiSelect(PeerData *peer, bool skipAnimation = false);

	class Inner;
	ChildWidget<Inner> _inner;
	ChildWidget<Ui::WidgetSlideWrap<Ui::MultiSelect>> _select;

	BoxButton _next, _cancel;
	MembersFilter _membersFilter;

	ScrollableBoxShadow _topShadow;
	ScrollableBoxShadow *_bottomShadow = nullptr;

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

	mtpRequestId _saveRequestId = 0;

	// saving admins
	void saveAdminsDone(const MTPUpdates &result);
	void saveSelectedAdmins();
	void getAdminsDone(const MTPmessages_ChatFull &result);
	void setAdminDone(UserData *user, const MTPBool &result);
	void removeAdminDone(UserData *user, const MTPBool &result);
	bool saveAdminsFail(const RPCError &error);
	bool editAdminFail(const RPCError &error);

	// group creation
	QString _creationName;
	QImage _creationPhoto;

	void creationDone(const MTPUpdates &updates);
	bool creationFail(const RPCError &e);

};

// This class is hold in header because it requires Qt preprocessing.
class ContactsBox::Inner : public TWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, CreatingGroupType creating = CreatingGroupNone);
	Inner(QWidget *parent, ChannelData *channel, MembersFilter membersFilter, const MembersAlreadyIn &already);
	Inner(QWidget *parent, ChatData *chat, MembersFilter membersFilter);
	Inner(QWidget *parent, UserData *bot);

	void setPeerSelectedChangedCallback(base::lambda_unique<void(PeerData *peer, bool selected)> callback);
	void peerUnselected(PeerData *peer);

	void updateFilter(QString filter = QString());
	void updateSelection();

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	QVector<UserData*> selected();
	QVector<MTPInputUser> selectedInputs();
	bool allAdmins() const {
		return _allAdmins.checked();
	}
	void setAllAdminsChangedCallback(base::lambda_unique<void()> allAdminsChangedCallback) {
		_allAdminsChangedCallback = std_::move(allAdminsChangedCallback);
	}

	void loadProfilePhotos(int32 yFrom);
	void chooseParticipant();

	void peopleReceived(const QString &query, const QVector<MTPPeer> &people);

	void refresh();

	ChatData *chat() const;
	ChannelData *channel() const;
	MembersFilter membersFilter() const;
	UserData *bot() const;
	CreatingGroupType creating() const;

	bool sharingBotGame() const;

	int32 selectedCount() const;
	bool hasAlreadyMembersInChannel() const {
		return !_already.isEmpty();
	}

	void saving(bool flag);

	~Inner();

signals:
	void mustScrollTo(int ymin, int ymax);
	void searchByUsername();
	void adminAdded();
	void addRequested();

private slots:
	void onDialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow);

	void peerUpdated(PeerData *peer);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);

	void onAddBot();
	void onAddAdmin();
	void onNoAddAdminBox(QObject *obj);

	void onAllAdminsChanged();

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct ContactData {
		ContactData() = default;
		ContactData(PeerData *peer, base::lambda_wrap<void()> updateCallback);

		std_::unique_ptr<Ui::RoundImageCheckbox> checkbox;
		Text name;
		QString statusText;
		bool statusHasOnlineColor = false;
		bool disabledChecked = false;
	};

	void init();
	void initList();

	void updateRowWithTop(int rowTop);
	int getSelectedRowTop() const;
	void updateSelectedRow();
	int getRowTopWithPeer(PeerData *peer) const;
	void updateRowWithPeer(PeerData *peer);
	void addAdminDone(const MTPUpdates &result, mtpRequestId req);
	bool addAdminFail(const RPCError &error, mtpRequestId req);

	void paintDialog(Painter &p, uint64 ms, PeerData *peer, ContactData *data, bool sel);
	void paintDisabledCheckUserpic(Painter &p, PeerData *peer, int x, int y, int outerWidth) const;

	void changeCheckState(Dialogs::Row *row);
	void changeCheckState(ContactData *data, PeerData *peer);
	enum class ChangeStateWay {
		Default,
		SkipCallback,
	};
	void changePeerCheckState(ContactData *data, PeerData *peer, bool checked, ChangeStateWay useCallback = ChangeStateWay::Default);

	template <typename FilterCallback>
	void addDialogsToList(FilterCallback callback);

	bool usingMultiSelect() const {
		return (_chat != nullptr) || (_creating != CreatingGroupNone && (!_channel || _membersFilter != MembersFilter::Admins));
	}

	base::lambda_unique<void(PeerData *peer, bool selected)> _peerSelectedChangedCallback;

	int32 _rowHeight;
	int _newItemHeight = 0;
	bool _newItemSel = false;

	ChatData *_chat = nullptr;
	ChannelData *_channel = nullptr;
	MembersFilter _membersFilter = MembersFilter::Recent;
	UserData *_bot = nullptr;
	CreatingGroupType _creating = CreatingGroupNone;
	MembersAlreadyIn _already;

	Checkbox _allAdmins;
	int32 _aboutWidth;
	Text _aboutAllAdmins, _aboutAdmins;
	base::lambda_unique<void()> _allAdminsChangedCallback;

	PeerData *_addToPeer = nullptr;
	UserData *_addAdmin = nullptr;
	mtpRequestId _addAdminRequestId = 0;
	ConfirmBox *_addAdminBox = nullptr;

	int32 _time;

	std_::unique_ptr<Dialogs::IndexedList> _customList;
	Dialogs::IndexedList *_contacts = nullptr;
	Dialogs::Row *_sel = nullptr;
	QString _filter;
	using FilteredDialogs = QVector<Dialogs::Row*>;
	FilteredDialogs _filtered;
	int _filteredSel = -1;
	bool _mouseSel = false;

	using ContactsData = QMap<PeerData*, ContactData*>;
	ContactsData _contactsData;
	using CheckedContacts = OrderedSet<PeerData*>;
	CheckedContacts _checkedContacts;

	ContactData *contactData(Dialogs::Row *row);

	bool _searching = false;
	QString _lastQuery;
	using ByUsernameRows = QVector<PeerData*>;
	using ByUsernameDatas = QVector<ContactData*>;
	ByUsernameRows _byUsername, _byUsernameFiltered;
	ByUsernameDatas d_byUsername, d_byUsernameFiltered; // filtered is partly subset of d_byUsername, partly subset of _byUsernameDatas
	ByUsernameDatas _byUsernameDatas;
	int _byUsernameSel = -1;

	QPoint _lastMousePos;
	LinkButton _addContactLnk;

	bool _saving = false;
	bool _allAdminsChecked = false;

};
