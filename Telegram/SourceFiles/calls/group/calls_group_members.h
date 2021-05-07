/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"

namespace Ui {
class RpWidget;
class ScrollArea;
class VerticalLayout;
class SettingsButton;
} // namespace Ui

namespace Data {
class GroupCall;
} // namespace Data

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Calls::Group {

class MembersRow;
struct VolumeRequest;
struct MuteRequest;
enum class PanelMode;

class Members final
	: public Ui::RpWidget
	, private PeerListContentDelegate {
public:
	Members(
		not_null<QWidget*> parent,
		not_null<GroupCall*> call);
	~Members();

	[[nodiscard]] int desiredHeight() const;
	[[nodiscard]] rpl::producer<int> desiredHeightValue() const override;
	[[nodiscard]] rpl::producer<int> fullCountValue() const;
	[[nodiscard]] auto toggleMuteRequests() const
		-> rpl::producer<Group::MuteRequest>;
	[[nodiscard]] auto changeVolumeRequests() const
		-> rpl::producer<Group::VolumeRequest>;
	[[nodiscard]] auto kickParticipantRequests() const
		-> rpl::producer<not_null<PeerData*>>;
	[[nodiscard]] rpl::producer<> addMembersRequests() const {
		return _addMemberRequests.events();
	}

	[[nodiscard]] MembersRow *lookupRow(not_null<PeerData*> peer) const;

	void setMode(PanelMode mode);

private:
	class Controller;
	using ListWidget = PeerListContent;

	void resizeEvent(QResizeEvent *e) override;

	// PeerListContentDelegate interface.
	void peerListSetTitle(rpl::producer<QString> title) override;
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
	void peerListSetHideEmpty(bool hide) override;
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
	int peerListSelectedRowsCount() override;
	void peerListScrollToTop() override;
	void peerListAddSelectedPeerInBunch(
		not_null<PeerData*> peer) override;
	void peerListAddSelectedRowInBunch(
		not_null<PeerListRow*> row) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) override;

	void setupAddMember(not_null<GroupCall*> call);
	void resizeToList();
	void setupList();
	void setupPinnedVideo();
	void setupFakeRoundCorners();

	void updateControlsGeometry();

	const not_null<GroupCall*> _call;
	rpl::variable<PanelMode> _mode = PanelMode();
	object_ptr<Ui::ScrollArea> _scroll;
	std::unique_ptr<Controller> _listController;
	not_null<Ui::VerticalLayout*> _layout;
	const not_null<Ui::RpWidget*> _pinnedVideo;
	rpl::variable<Ui::RpWidget*> _addMemberButton = nullptr;
	ListWidget *_list = nullptr;
	rpl::event_stream<> _addMemberRequests;

	rpl::variable<bool> _canAddMembers;

	rpl::lifetime _pinnedTrackLifetime;

};

} // namespace Calls
