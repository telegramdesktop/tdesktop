/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "boxes/peer_list_box.h"
#include "mtproto/sender.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "info/profile/info_profile_members_controllers.h"

struct ChatAdminRightsInfo;
struct ChatRestrictionsInfo;

namespace Window {
class SessionNavigation;
} // namespace Window

Fn<void(
	ChatAdminRightsInfo oldRights,
	ChatAdminRightsInfo newRights,
	const QString &rank)> SaveAdminCallback(
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		Fn<void(
			ChatAdminRightsInfo newRights,
			const QString &rank)> onDone,
		Fn<void()> onFail);

Fn<void(
	ChatRestrictionsInfo oldRights,
	ChatRestrictionsInfo newRights)> SaveRestrictedCallback(
		not_null<PeerData*> peer,
		not_null<PeerData*> participant,
		Fn<void(ChatRestrictionsInfo newRights)> onDone,
		Fn<void()> onFail);

void SubscribeToMigration(
	not_null<PeerData*> peer,
	rpl::lifetime &lifetime,
	Fn<void(not_null<ChannelData*>)> migrate);

enum class ParticipantsRole {
	Profile,
	Members,
	Admins,
	Restricted,
	Kicked,
};

class ParticipantsOnlineSorter {
public:
	ParticipantsOnlineSorter(
		not_null<PeerData*> peer,
		not_null<PeerListDelegate*> delegate);

	void sort();
	rpl::producer<int> onlineCountValue() const;

private:
	void sortDelayed();
	void refreshOnlineCount();

	const not_null<PeerData*> _peer;
	const not_null<PeerListDelegate*> _delegate;
	base::Timer _sortByOnlineTimer;
	rpl::variable<int> _onlineCount = 0;
	rpl::lifetime _lifetime;

};

class ParticipantsAdditionalData {
public:
	using Role = ParticipantsRole;

	ParticipantsAdditionalData(not_null<PeerData*> peer, Role role);

	PeerData *applyParticipant(const MTPChannelParticipant &data);
	PeerData *applyParticipant(
		const MTPChannelParticipant &data,
		Role overrideRole);
	void setExternal(not_null<PeerData*> participant);
	void checkForLoaded(not_null<PeerData*> participant);
	void fillFromPeer();

	[[nodiscard]] bool infoLoaded(not_null<PeerData*> participant) const;
	[[nodiscard]] bool canEditAdmin(not_null<UserData*> user) const;
	[[nodiscard]] bool canAddOrEditAdmin(not_null<UserData*> user) const;
	[[nodiscard]] bool canRestrictParticipant(
		not_null<PeerData*> participant) const;
	[[nodiscard]] bool canRemoveParticipant(
		not_null<PeerData*> participant) const;
	[[nodiscard]] std::optional<ChatAdminRightsInfo> adminRights(
		not_null<UserData*> user) const;
	QString adminRank(not_null<UserData*> user) const;
	[[nodiscard]] std::optional<ChatRestrictionsInfo> restrictedRights(
		not_null<PeerData*> participant) const;
	[[nodiscard]] bool isCreator(not_null<UserData*> user) const;
	[[nodiscard]] bool isExternal(not_null<PeerData*> participant) const;
	[[nodiscard]] bool isKicked(not_null<PeerData*> participant) const;
	[[nodiscard]] UserData *adminPromotedBy(not_null<UserData*> user) const;
	[[nodiscard]] UserData *restrictedBy(not_null<PeerData*> participant) const;

	void migrate(not_null<ChatData*> chat, not_null<ChannelData*> channel);

private:
	UserData *applyCreator(const MTPDchannelParticipantCreator &data);
	UserData *applyAdmin(const MTPDchannelParticipantAdmin &data);
	UserData *applyRegular(MTPint userId);
	PeerData *applyBanned(const MTPDchannelParticipantBanned &data);
	void fillFromChat(not_null<ChatData*> chat);
	void fillFromChannel(not_null<ChannelData*> channel);

	not_null<PeerData*> _peer;
	Role _role = Role::Members;
	UserData *_creator = nullptr;

	// Data for chats.
	base::flat_set<not_null<UserData*>> _members;
	base::flat_set<not_null<UserData*>> _admins;

	// Data for channels.
	base::flat_map<not_null<UserData*>, ChatAdminRightsInfo> _adminRights;
	base::flat_map<not_null<UserData*>, QString> _adminRanks;
	base::flat_set<not_null<UserData*>> _adminCanEdit;
	base::flat_map<not_null<UserData*>, not_null<UserData*>> _adminPromotedBy;
	std::map<not_null<PeerData*>, ChatRestrictionsInfo> _restrictedRights;
	std::set<not_null<PeerData*>> _kicked;
	std::map<not_null<PeerData*>, not_null<UserData*>> _restrictedBy;
	std::set<not_null<PeerData*>> _external;
	std::set<not_null<PeerData*>> _infoNotLoaded;

};

// Viewing admins, banned or restricted users list with search.
class ParticipantsBoxController
	: public PeerListController
	, public base::has_weak_ptr {
public:
	using Role = ParticipantsRole;

	static void Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Role role);

	ParticipantsBoxController(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Role role);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	void peerListSearchAddRow(not_null<PeerData*> peer) override;
	std::unique_ptr<PeerListRow> createSearchRow(
		not_null<PeerData*> peer) override;
	std::unique_ptr<PeerListRow> createRestoredRow(
		not_null<PeerData*> peer) override;

	std::unique_ptr<PeerListState> saveState() const override;
	void restoreState(std::unique_ptr<PeerListState> state) override;

	rpl::producer<int> onlineCountValue() const override;

protected:
	// Allow child controllers not providing navigation.
	// This is their responsibility to override all methods that use it.
	struct CreateTag {
	};
	ParticipantsBoxController(
		CreateTag,
		Window::SessionNavigation *navigation,
		not_null<PeerData*> peer,
		Role role);

	virtual std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> participant) const;

private:
	using Row = Info::Profile::MemberListRow;
	using Type = Row::Type;
	using Rights = Row::Rights;
	struct SavedState : SavedStateBase {
		explicit SavedState(const ParticipantsAdditionalData &additional);

		using SearchStateBase = PeerListSearchController::SavedStateBase;
		std::unique_ptr<SearchStateBase> searchState;
		int offset = 0;
		bool allLoaded = false;
		bool wasLoading = false;
		ParticipantsAdditionalData additional;
		rpl::lifetime lifetime;
	};

	static std::unique_ptr<PeerListSearchController> CreateSearchController(
		not_null<PeerData*> peer,
		Role role,
		not_null<ParticipantsAdditionalData*> additional);

	void prepareChatRows(not_null<ChatData*> chat);
	void rebuildChatRows(not_null<ChatData*> chat);
	void rebuildChatParticipants(not_null<ChatData*> chat);
	void rebuildChatAdmins(not_null<ChatData*> chat);
	void chatListReady();
	void rebuildRowTypes();

	void addNewItem();
	void addNewParticipants();

	void refreshDescription();
	void setupListChangeViewers();
	void showAdmin(not_null<UserData*> user);
	void editAdminDone(
		not_null<UserData*> user,
		ChatAdminRightsInfo rights,
		const QString &rank);
	void showRestricted(not_null<UserData*> user);
	void editRestrictedDone(
		not_null<PeerData*> participant,
		ChatRestrictionsInfo rights);
	void removeKicked(
		not_null<PeerListRow*> row,
		not_null<PeerData*> participant);
	void removeKickedWithRow(not_null<PeerData*> participant);
	void removeKicked(not_null<PeerData*> participant);
	void kickParticipant(not_null<PeerData*> participant);
	void kickParticipantSure(not_null<PeerData*> participant);
	void unkickParticipant(not_null<UserData*> user);
	void removeAdmin(not_null<UserData*> user);
	void removeAdminSure(not_null<UserData*> user);
	bool appendRow(not_null<PeerData*> participant);
	bool prependRow(not_null<PeerData*> participant);
	bool removeRow(not_null<PeerData*> participant);
	void refreshCustomStatus(not_null<PeerListRow*> row) const;
	bool feedMegagroupLastParticipants();
	Type computeType(not_null<PeerData*> participant) const;
	void recomputeTypeFor(not_null<PeerData*> participant);

	void subscribeToMigration();
	void migrate(not_null<ChatData*> chat, not_null<ChannelData*> channel);
	void subscribeToCreatorChange(not_null<ChannelData*> channel);
	void fullListRefresh();

	// It may be nullptr in subclasses of this controller.
	Window::SessionNavigation *_navigation = nullptr;

	not_null<PeerData*> _peer;
	MTP::Sender _api;
	Role _role = Role::Admins;
	int _offset = 0;
	mtpRequestId _loadRequestId = 0;
	bool _allLoaded = false;
	ParticipantsAdditionalData _additional;
	std::unique_ptr<ParticipantsOnlineSorter> _onlineSorter;
	Ui::BoxPointer _editBox;
	Ui::BoxPointer _addBox;
	QPointer<Ui::BoxContent> _editParticipantBox;

};

// Members, banned and restricted users server side search.
class ParticipantsBoxSearchController : public PeerListSearchController {
public:
	using Role = ParticipantsBoxController::Role;

	ParticipantsBoxSearchController(
		not_null<ChannelData*> channel,
		Role role,
		not_null<ParticipantsAdditionalData*> additional);

	void searchQuery(const QString &query) override;
	bool isLoading() override;
	bool loadMoreRows() override;

	std::unique_ptr<SavedStateBase> saveState() const override;
	void restoreState(std::unique_ptr<SavedStateBase> state) override;

private:
	struct SavedState : SavedStateBase {
		QString query;
		int offset = 0;
		bool allLoaded = false;
		bool wasLoading = false;
	};
	struct CacheEntry {
		MTPchannels_ChannelParticipants result;
		int requestedCount = 0;
	};
	struct Query {
		QString text;
		int offset = 0;
	};

	void searchOnServer();
	bool searchInCache();
	void searchDone(
		mtpRequestId requestId,
		const MTPchannels_ChannelParticipants &result,
		int requestedCount);

	not_null<ChannelData*> _channel;
	Role _role = Role::Restricted;
	not_null<ParticipantsAdditionalData*> _additional;
	MTP::Sender _api;

	base::Timer _timer;
	QString _query;
	mtpRequestId _requestId = 0;
	int _offset = 0;
	bool _allLoaded = false;
	std::map<QString, CacheEntry> _cache;
	std::map<mtpRequestId, Query> _queries;

};
