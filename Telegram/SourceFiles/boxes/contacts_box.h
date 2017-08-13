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

namespace Dialogs {
class Row;
class IndexedList;
} // namespace Dialogs

namespace Ui {
class RippleAnimation;
class LinkButton;
class Checkbox;
class MultiSelect;
template <typename Widget>
class WidgetSlideWrap;
} // namespace Ui

enum class PeerFloodType {
	Send,
	InviteGroup,
	InviteChannel,
};
QString PeerFloodErrorText(PeerFloodType type);

inline Ui::RoundImageCheckbox::PaintRoundImage PaintUserpicCallback(PeerData *peer) {
	return [peer](Painter &p, int x, int y, int outerWidth, int size) {
		peer->paintUserpicLeft(p, x, y, outerWidth, size);
	};
}

class ContactsBox : public BoxContent, public RPCSender {
	Q_OBJECT

public:
	ContactsBox(QWidget*);
	ContactsBox(QWidget*, const QString &name, const QImage &photo); // group creation
	ContactsBox(QWidget*, ChannelData *channel); // channel setup
	ContactsBox(QWidget*, ChannelData *channel, MembersFilter filter, const MembersAlreadyIn &already);
	ContactsBox(QWidget*, ChatData *chat, MembersFilter filter);
	ContactsBox(QWidget*, UserData *bot);

signals:
	void adminAdded();

private slots:
	void onSubmit();

	bool onSearchByUsername(bool searchCache = false);
	void onNeedSearchByUsername();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>> createMultiSelect();

	void updateTitle();
	int getTopScrollSkip() const;
	void updateScrollSkips();
	void onFilterUpdate(const QString &filter);
	void onPeerSelectedChanged(PeerData *peer, bool checked);
	void addPeerToMultiSelect(PeerData *peer, bool skipAnimation = false);

	void saveChatAdmins();
	void inviteParticipants();
	void createGroup();

	// global search
	void peopleReceived(const MTPcontacts_Found &result, mtpRequestId req);
	bool peopleFailed(const RPCError &error, mtpRequestId req);

	// saving admins
	void saveAdminsDone(const MTPUpdates &result);
	void saveSelectedAdmins();
	void getAdminsDone(const MTPmessages_ChatFull &result);
	void setAdminDone(gsl::not_null<UserData*> user, const MTPBool &result);
	void removeAdminDone(gsl::not_null<UserData*> user, const MTPBool &result);
	bool saveAdminsFail(const RPCError &error);
	bool editAdminFail(const RPCError &error);

	// group creation
	void creationDone(const MTPUpdates &updates);
	bool creationFail(const RPCError &e);

	ChatData *_chat = nullptr;
	ChannelData *_channel = nullptr;
	MembersFilter _membersFilter = MembersFilter::Recent;
	UserData *_bot = nullptr;
	CreatingGroupType _creating = CreatingGroupNone;
	MembersAlreadyIn _alreadyIn;

	object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>> _select;

	class Inner;
	QPointer<Inner> _inner;

	object_ptr<QTimer> _searchTimer;
	QString _peopleQuery;
	bool _peopleFull;
	mtpRequestId _peopleRequest;

	typedef QMap<QString, MTPcontacts_Found> PeopleCache;
	PeopleCache _peopleCache;

	typedef QMap<mtpRequestId, QString> PeopleQueries;
	PeopleQueries _peopleQueries;

	mtpRequestId _saveRequestId = 0;

	QString _creationName;
	QImage _creationPhoto;

};

// This class is hold in header because it requires Qt preprocessing.
class ContactsBox::Inner : public TWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, CreatingGroupType creating = CreatingGroupNone);
	Inner(QWidget *parent, ChannelData *channel, MembersFilter membersFilter, const MembersAlreadyIn &already);
	Inner(QWidget *parent, ChatData *chat, MembersFilter membersFilter);
	Inner(QWidget *parent, UserData *bot);

	void setPeerSelectedChangedCallback(base::lambda<void(PeerData *peer, bool selected)> callback);
	void peerUnselected(PeerData *peer);

	void updateFilter(QString filter = QString());
	void updateSelection();

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	std::vector<gsl::not_null<UserData*>> selected();
	QVector<MTPInputUser> selectedInputs();
	bool allAdmins() const;
	void setAllAdminsChangedCallback(base::lambda<void()> allAdminsChangedCallback) {
		_allAdminsChangedCallback = std::move(allAdminsChangedCallback);
	}

	void chooseParticipant();

	void peopleReceived(const QString &query, const QVector<MTPPeer> &people);

	void refresh();

	ChatData *chat() const;
	ChannelData *channel() const;
	MembersFilter membersFilter() const;
	UserData *bot() const;
	CreatingGroupType creating() const;

	bool sharingBotGame() const;

	int selectedCount() const;
	bool hasAlreadyMembersInChannel() const {
		return !_already.isEmpty();
	}

	void saving(bool flag);

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	~Inner();

signals:
	void mustScrollTo(int ymin, int ymax);
	void searchByUsername();
	void adminAdded();

private slots:
	void onDialogRowReplaced(Dialogs::Row *oldRow, Dialogs::Row *newRow);

	void peerUpdated(PeerData *peer);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);

	void onAllAdminsChanged();

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	struct ContactData {
		ContactData();
		ContactData(PeerData *peer, base::lambda<void()> updateCallback);
		~ContactData();

		std::unique_ptr<Ui::RoundImageCheckbox> checkbox;
		std::unique_ptr<Ui::RippleAnimation> ripple;
		int rippleRowTop = 0;
		Text name;
		QString statusText;
		bool statusHasOnlineColor = false;
		bool disabledChecked = false;
	};
	void addRipple(PeerData *peer, ContactData *data);
	void stopLastRipple(ContactData *data);
	void setPressed(Dialogs::Row *pressed);
	void setFilteredPressed(int pressed);
	void setSearchedPressed(int pressed);
	void clearSearchedContactDatas();

	bool isRowDisabled(PeerData *peer, ContactData *data) const;
	void loadProfilePhotos();
	void addBot();

	void init();
	void initList();
	void invalidateCache();

	void updateRowWithTop(int rowTop);
	int getSelectedRowTop() const;
	void updateSelectedRow();
	int getRowTopWithPeer(PeerData *peer) const;
	void updateRowWithPeer(PeerData *peer);

	void paintDialog(Painter &p, TimeMs ms, PeerData *peer, ContactData *data, bool sel);
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

	PeerData *selectedPeer() const;
	bool usingMultiSelect() const {
		return (_chat != nullptr) || (_creating != CreatingGroupNone && (!_channel || _membersFilter != MembersFilter::Admins));
	}
	void changeMultiSelectCheckState();
	void shareBotGameToSelected();
	void addBotToSelectedGroup();

	base::lambda<void(PeerData *peer, bool selected)> _peerSelectedChangedCallback;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _rowHeight = 0;
	int _rowsTop = 0;
	int _aboutHeight = 0;

	ChatData *_chat = nullptr;
	ChannelData *_channel = nullptr;
	MembersFilter _membersFilter = MembersFilter::Recent;
	UserData *_bot = nullptr;
	CreatingGroupType _creating = CreatingGroupNone;
	MembersAlreadyIn _already;

	object_ptr<Ui::Checkbox> _allAdmins;
	int32 _aboutWidth;
	Text _aboutAllAdmins, _aboutAdmins;
	base::lambda<void()> _allAdminsChangedCallback;

	PeerData *_addToPeer = nullptr;

	int32 _time;

	std::unique_ptr<Dialogs::IndexedList> _customList;
	Dialogs::IndexedList *_contacts = nullptr;
	Dialogs::Row *_selected = nullptr;
	Dialogs::Row *_pressed = nullptr;
	QString _filter;
	using FilteredDialogs = QVector<Dialogs::Row*>;
	FilteredDialogs _filtered;
	int _filteredSelected = -1;
	int _filteredPressed = -1;
	bool _mouseSelection = false;

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
	int _searchedSelected = -1;
	int _searchedPressed = -1;

	QPoint _lastMousePos;
	object_ptr<Ui::LinkButton> _addContactLnk;

	bool _saving = false;
	bool _allAdminsChecked = false;

};
