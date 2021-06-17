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
namespace GL {
enum class Backend;
} // namespace GL
} // namespace Ui

namespace Data {
class GroupCall;
} // namespace Data

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Calls::Group {

class Viewport;
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
		not_null<GroupCall*> call,
		PanelMode mode,
		Ui::GL::Backend backend);
	~Members();

	[[nodiscard]] not_null<Viewport*> viewport() const;
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
	[[nodiscard]] QRect getInnerGeometry() const;

private:
	class Controller;
	struct VideoTile;
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
	void setupFakeRoundCorners();

	void trackViewportGeometry();
	void updateControlsGeometry();

	const not_null<GroupCall*> _call;
	rpl::variable<PanelMode> _mode = PanelMode();
	object_ptr<Ui::ScrollArea> _scroll;
	std::unique_ptr<Controller> _listController;
	not_null<Ui::VerticalLayout*> _layout;
	const not_null<Ui::RpWidget*> _videoWrap;
	const std::unique_ptr<Ui::RpWidget> _videoPlaceholder;
	std::unique_ptr<Viewport> _viewport;
	rpl::variable<Ui::RpWidget*> _addMemberButton = nullptr;
	RpWidget *_topSkip = nullptr;
	RpWidget *_bottomSkip = nullptr;
	ListWidget *_list = nullptr;
	rpl::event_stream<> _addMemberRequests;

	rpl::variable<bool> _canAddMembers;

};

} // namespace Calls
