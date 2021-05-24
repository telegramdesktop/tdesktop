/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_members.h"

#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_menu.h"
#include "calls/group/calls_volume_item.h"
#include "calls/group/calls_group_members_row.h"
#include "calls/group/calls_group_viewport.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_peer.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "data/data_peer_values.h" // Data::CanWriteValue.
#include "data/data_session.h" // Data::Session::invitedToCallUsers.
#include "settings/settings_common.h" // Settings::CreateButton.
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/cross_line.h"
#include "core/application.h" // Core::App().domain, .activeWindow.
#include "main/main_domain.h" // Core::App().domain().activate.
#include "main/main_session.h"
#include "boxes/peers/edit_participants_box.h" // SubscribeToMigration.
#include "window/window_controller.h" // Controller::sessionController.
#include "window/window_session_controller.h"
#include "media/view/media_view_pip.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_calls.h"

namespace Calls::Group {
namespace {

constexpr auto kKeepRaisedHandStatusDuration = 3 * crl::time(1000);
constexpr auto kShadowMaxAlpha = 74;

using Row = MembersRow;

} // namespace

class Members::Controller final
	: public PeerListController
	, public MembersRowDelegate
	, public base::has_weak_ptr {
public:
	Controller(
		not_null<GroupCall*> call,
		not_null<QWidget*> menuParent,
		PanelMode mode);
	~Controller();

	using MuteRequest = Group::MuteRequest;
	using VolumeRequest = Group::VolumeRequest;

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	[[nodiscard]] rpl::producer<int> fullCountValue() const {
		return _fullCount.value();
	}
	[[nodiscard]] rpl::producer<MuteRequest> toggleMuteRequests() const;
	[[nodiscard]] rpl::producer<VolumeRequest> changeVolumeRequests() const;
	[[nodiscard]] auto kickParticipantRequests() const
		-> rpl::producer<not_null<PeerData*>>;

	Row *findRow(not_null<PeerData*> participantPeer) const;
	void setMode(PanelMode mode);

	bool rowIsMe(not_null<PeerData*> participantPeer) override;
	bool rowCanMuteMembers() override;
	void rowUpdateRow(not_null<Row*> row) override;
	void rowScheduleRaisedHandStatusRemove(not_null<Row*> row) override;
	void rowPaintIcon(
		Painter &p,
		QRect rect,
		const IconState &state) override;
	int rowPaintStatusIcon(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		not_null<MembersRow*> row,
		const IconState &state) override;
	bool rowIsNarrow() override;
	//void rowPaintNarrowBackground(
	//	Painter &p,
	//	int x,
	//	int y,
	//	bool selected) override;
	//void rowPaintNarrowBorder(
	//	Painter &p,
	//	int x,
	//	int y,
	//	not_null<Row*> row) override;
	//void rowPaintNarrowShadow(
	//	Painter &p,
	//	int x,
	//	int y,
	//	int sizew,
	//	int sizeh) override;

	//int customRowHeight() override;
	//void customRowPaint(
	//	Painter &p,
	//	crl::time now,
	//	not_null<PeerListRow*> row,
	//	bool selected) override;
	//bool customRowSelectionPoint(
	//	not_null<PeerListRow*> row,
	//	int x,
	//	int y) override;
	//Fn<QImage()> customRowRippleMaskGenerator() override;

private:
	[[nodiscard]] std::unique_ptr<Row> createRowForMe();
	[[nodiscard]] std::unique_ptr<Row> createRow(
		const Data::GroupCallParticipant &participant);
	[[nodiscard]] std::unique_ptr<Row> createInvitedRow(
		not_null<PeerData*> participantPeer);

	[[nodiscard]] bool isMe(not_null<PeerData*> participantPeer) const;
	void prepareRows(not_null<Data::GroupCall*> real);
	//void repaintByTimer();

	[[nodiscard]] base::unique_qptr<Ui::PopupMenu> createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row);
	void addMuteActionsToContextMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> participantPeer,
		bool participantIsCallAdmin,
		not_null<Row*> row);
	void setupListChangeViewers();
	void subscribeToChanges(not_null<Data::GroupCall*> real);
	void updateRow(
		const std::optional<Data::GroupCallParticipant> &was,
		const Data::GroupCallParticipant &now);
	void updateRow(
		not_null<Row*> row,
		const Data::GroupCallParticipant *participant);
	void removeRow(not_null<Row*> row);
	void updateRowLevel(not_null<Row*> row, float level);
	void checkRowPosition(not_null<Row*> row);
	[[nodiscard]] bool needToReorder(not_null<Row*> row) const;
	[[nodiscard]] bool allRowsAboveAreSpeaking(not_null<Row*> row) const;
	[[nodiscard]] bool allRowsAboveMoreImportantThanHand(
		not_null<Row*> row,
		uint64 raiseHandRating) const;
	const Data::GroupCallParticipant *findParticipant(
		const std::string &endpoint) const;
	const std::string &computeScreenEndpoint(
		not_null<const Data::GroupCallParticipant*> participant) const;
	const std::string &computeCameraEndpoint(
		not_null<const Data::GroupCallParticipant*> participant) const;
	//void setRowVideoEndpoint(
	//	not_null<Row*> row,
	//	const std::string &endpoint);
	bool toggleRowVideo(not_null<PeerListRow*> row);
	void showRowMenu(not_null<PeerListRow*> row);

	void toggleVideoEndpointActive(
		const VideoEndpoint &endpoint,
		bool active);

	void appendInvitedUsers();
	void scheduleRaisedHandStatusRemove();

	const not_null<GroupCall*> _call;
	not_null<PeerData*> _peer;
	std::string _largeEndpoint;
	bool _prepared = false;

	rpl::event_stream<MuteRequest> _toggleMuteRequests;
	rpl::event_stream<VolumeRequest> _changeVolumeRequests;
	rpl::event_stream<not_null<PeerData*>> _kickParticipantRequests;
	rpl::variable<int> _fullCount = 1;

	not_null<QWidget*> _menuParent;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::flat_set<not_null<PeerData*>> _menuCheckRowsAfterHidden;

	base::flat_map<PeerListRowId, crl::time> _raisedHandStatusRemoveAt;
	base::Timer _raisedHandStatusRemoveTimer;

	base::flat_map<uint32, not_null<Row*>> _soundingRowBySsrc;
	//base::flat_map<std::string, not_null<Row*>> _videoEndpoints;
	base::flat_set<not_null<PeerData*>> _cameraActive;
	base::flat_set<not_null<PeerData*>> _screenActive;
	Ui::Animations::Basic _soundingAnimation;

	crl::time _soundingAnimationHideLastTime = 0;
	bool _skipRowLevelUpdate = false;

	PanelMode _mode = PanelMode::Default;
	Ui::CrossLineAnimation _inactiveCrossLine;
	Ui::CrossLineAnimation _coloredCrossLine;
	Ui::CrossLineAnimation _inactiveNarrowCrossLine;
	Ui::CrossLineAnimation _coloredNarrowCrossLine;
	//Ui::CrossLineAnimation _videoNarrowCrossLine;
	Ui::CrossLineAnimation _videoLargeCrossLine;
	Ui::RoundRect _narrowRoundRectSelected;
	Ui::RoundRect _narrowRoundRect;
	QImage _narrowShadow;

	rpl::lifetime _lifetime;

};

Members::Controller::Controller(
	not_null<GroupCall*> call,
	not_null<QWidget*> menuParent,
	PanelMode mode)
: _call(call)
, _peer(call->peer())
, _menuParent(menuParent)
, _raisedHandStatusRemoveTimer([=] { scheduleRaisedHandStatusRemove(); })
, _mode(mode)
, _inactiveCrossLine(st::groupCallMemberInactiveCrossLine)
, _coloredCrossLine(st::groupCallMemberColoredCrossLine)
, _inactiveNarrowCrossLine(st::groupCallNarrowInactiveCrossLine)
, _coloredNarrowCrossLine(st::groupCallNarrowColoredCrossLine)
//, _videoNarrowCrossLine(st::groupCallVideoCrossLine)
, _videoLargeCrossLine(st::groupCallLargeVideoCrossLine)
, _narrowRoundRectSelected(
	ImageRoundRadius::Large,
	st::groupCallMembersBgOver)
, _narrowRoundRect(ImageRoundRadius::Large, st::groupCallMembersBg) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_inactiveCrossLine.invalidate();
		_coloredCrossLine.invalidate();
		_inactiveNarrowCrossLine.invalidate();
		_coloredNarrowCrossLine.invalidate();
		//_videoNarrowCrossLine.invalidate();
	}, _lifetime);

	rpl::combine(
		rpl::single(anim::Disabled()) | rpl::then(anim::Disables()),
		Core::App().appDeactivatedValue()
	) | rpl::start_with_next([=](bool animDisabled, bool deactivated) {
		const auto hide = !(!animDisabled && !deactivated);

		if (!(hide && _soundingAnimationHideLastTime)) {
			_soundingAnimationHideLastTime = hide ? crl::now() : 0;
		}
		for (const auto &[_, row] : _soundingRowBySsrc) {
			if (hide) {
				updateRowLevel(row, 0.);
			}
			row->setSkipLevelUpdate(hide);
		}
		if (!hide && !_soundingAnimation.animating()) {
			_soundingAnimation.start();
		}
		_skipRowLevelUpdate = hide;
	}, _lifetime);

	_soundingAnimation.init([=](crl::time now) {
		if (const auto &last = _soundingAnimationHideLastTime; (last > 0)
			&& (now - last >= kBlobsEnterDuration)) {
			_soundingAnimation.stop();
			return false;
		}
		for (const auto &[ssrc, row] : _soundingRowBySsrc) {
			row->updateBlobAnimation(now);
			delegate()->peerListUpdateRow(row);
		}
		return true;
	});

	_peer->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::About
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (const auto row = findRow(update.peer)) {
			row->setAbout(update.peer->about());
		}
	}, _lifetime);
}

Members::Controller::~Controller() {
	base::take(_menu);
}

//void Members::Controller::setRowVideoEndpoint(
//		not_null<Row*> row,
//		const std::string &endpoint) {
//	const auto was = row->videoTrackEndpoint();
//	if (was != endpoint) {
//		if (!was.empty()) {
//			_videoEndpoints.remove(was);
//		}
//		if (!endpoint.empty()) {
//			_videoEndpoints.emplace(endpoint, row);
//		}
//	}
//	if (endpoint.empty()) {
//		row->clearVideoTrack();
//	} else {
//		_call->addVideoOutput(endpoint, row->createVideoTrack(endpoint));
//	}
//}

void Members::Controller::setupListChangeViewers() {
	_call->real(
	) | rpl::start_with_next([=](not_null<Data::GroupCall*> real) {
		subscribeToChanges(real);
	}, _lifetime);

	_call->levelUpdates(
	) | rpl::start_with_next([=](const LevelUpdate &update) {
		const auto i = _soundingRowBySsrc.find(update.ssrc);
		if (i != end(_soundingRowBySsrc)) {
			updateRowLevel(i->second, update.value);
		}
	}, _lifetime);

	//_call->videoEndpointLargeValue(
	//) | rpl::filter([=](const VideoEndpoint &largeEndpoint) {
	//	return (_largeEndpoint != largeEndpoint.endpoint);
	//}) | rpl::start_with_next([=](const VideoEndpoint &largeEndpoint) {
	//	if (_call->streamsVideo(_largeEndpoint)) {
	//		if (const auto participant = findParticipant(_largeEndpoint)) {
	//			if (const auto row = findRow(participant->peer)) {
	//				const auto current = row->videoTrackEndpoint();
	//				if (current.empty()
	//					|| (computeScreenEndpoint(participant) == _largeEndpoint
	//						&& computeCameraEndpoint(participant) == current)) {
	//					setRowVideoEndpoint(row, _largeEndpoint);
	//				}
	//			}
	//		}
	//	}
	//	_largeEndpoint = largeEndpoint.endpoint;
	//	if (const auto participant = findParticipant(_largeEndpoint)) {
	//		if (const auto row = findRow(participant->peer)) {
	//			if (row->videoTrackEndpoint() == _largeEndpoint) {
	//				const auto &camera = computeCameraEndpoint(participant);
	//				const auto &screen = computeScreenEndpoint(participant);
	//				if (_largeEndpoint == camera
	//					&& _call->streamsVideo(screen)) {
	//					setRowVideoEndpoint(row, screen);
	//				} else if (_largeEndpoint == screen
	//					&& _call->streamsVideo(camera)) {
	//					setRowVideoEndpoint(row, camera);
	//				} else {
	//					setRowVideoEndpoint(row, std::string());
	//				}
	//			}
	//		}
	//	}
	//}, _lifetime);

	//_call->streamsVideoUpdates(
	//) | rpl::start_with_next([=](StreamsVideoUpdate update) {
	//	Assert(update.endpoint != _largeEndpoint);
	//	if (update.streams) {
	//		if (const auto participant = findParticipant(update.endpoint)) {
	//			if (const auto row = findRow(participant->peer)) {
	//				const auto &camera = computeCameraEndpoint(participant);
	//				const auto &screen = computeScreenEndpoint(participant);
	//				if (update.endpoint == camera
	//					&& (!_call->streamsVideo(screen)
	//						|| _largeEndpoint == screen)) {
	//					setRowVideoEndpoint(row, camera);
	//				} else if (update.endpoint == screen
	//					&& (_largeEndpoint != screen)) {
	//					setRowVideoEndpoint(row, screen);
	//				}
	//			}
	//		}
	//	} else {
	//		const auto i = _videoEndpoints.find(update.endpoint);
	//		if (i != end(_videoEndpoints)) {
	//			const auto row = i->second;
	//			const auto real = _call->lookupReal();
	//			Assert(real != nullptr);
	//			const auto participant = real->participantByPeer(
	//				row->peer());
	//			if (!participant) {
	//				setRowVideoEndpoint(row, std::string());
	//			} else {
	//				const auto &camera = computeCameraEndpoint(participant);
	//				const auto &screen = computeScreenEndpoint(participant);
	//				if (update.endpoint == camera
	//					&& (_largeEndpoint != screen)
	//					&& _call->streamsVideo(screen)) {
	//					setRowVideoEndpoint(row, screen);
	//				} else if (update.endpoint == screen
	//					&& (_largeEndpoint != camera)
	//					&& _call->streamsVideo(camera)) {
	//					setRowVideoEndpoint(row, camera);
	//				} else {
	//					setRowVideoEndpoint(row, std::string());
	//				}
	//			}
	//		}
	//	}
	//}, _lifetime);

	_call->rejoinEvents(
	) | rpl::start_with_next([=](const Group::RejoinEvent &event) {
		const auto guard = gsl::finally([&] {
			delegate()->peerListRefreshRows();
		});
		if (const auto row = findRow(event.wasJoinAs)) {
			removeRow(row);
		}
		if (findRow(event.nowJoinAs)) {
			return;
		} else if (auto row = createRowForMe()) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}, _lifetime);
}

void Members::Controller::subscribeToChanges(not_null<Data::GroupCall*> real) {
	_fullCount = real->fullCountValue();

	real->participantsReloaded(
	) | rpl::start_with_next([=] {
		prepareRows(real);
	}, _lifetime);

	using Update = Data::GroupCall::ParticipantUpdate;
	real->participantUpdated(
	) | rpl::start_with_next([=](const Update &update) {
		Expects(update.was.has_value() || update.now.has_value());

		const auto participantPeer = update.was
			? update.was->peer
			: update.now->peer;
		if (!update.now) {
			if (const auto row = findRow(participantPeer)) {
				const auto owner = &participantPeer->owner();
				if (isMe(participantPeer)) {
					updateRow(row, nullptr);
				} else {
					removeRow(row);
					delegate()->peerListRefreshRows();
				}
			}
		} else {
			updateRow(update.was, *update.now);
		}
	}, _lifetime);

	for (const auto &[endpoint, track] : _call->activeVideoTracks()) {
		toggleVideoEndpointActive(endpoint, true);
	}
	_call->videoStreamActiveUpdates(
	) | rpl::start_with_next([=](const VideoEndpoint &endpoint) {
		const auto active = _call->activeVideoTracks().contains(endpoint);
		toggleVideoEndpointActive(endpoint, active);
	}, _lifetime);

	if (_prepared) {
		appendInvitedUsers();
	}
}

void Members::Controller::toggleVideoEndpointActive(
		const VideoEndpoint &endpoint,
		bool active) {
	const auto toggleOne = [=](
			base::flat_set<not_null<PeerData*>> &set,
			not_null<PeerData*> participantPeer,
			bool active) {
		if ((active && set.emplace(participantPeer).second)
			|| (!active && set.remove(participantPeer))) {
			if (_mode == PanelMode::Wide) {
				if (const auto row = findRow(participantPeer)) {
					delegate()->peerListUpdateRow(row);
				}
			}
		}
	};
	const auto &id = endpoint.id;
	const auto participantPeer = endpoint.peer;
	const auto real = _call->lookupReal();
	if (active) {
		if (const auto participant = findParticipant(id)) {
			if (computeCameraEndpoint(participant) == id) {
				toggleOne(_cameraActive, participantPeer, true);
			} else if (computeScreenEndpoint(participant) == id) {
				toggleOne(_screenActive, participantPeer, true);
			}
		}
	} else if (const auto participant = real->participantByPeer(
			participantPeer)) {
		const auto &camera = computeCameraEndpoint(participant);
		const auto &screen = computeScreenEndpoint(participant);
		if (camera == id || camera.empty()) {
			toggleOne(_cameraActive, participantPeer, false);
		}
		if (screen == id || screen.empty()) {
			toggleOne(_screenActive, participantPeer, false);
		}
	} else {
		toggleOne(_cameraActive, participantPeer, false);
		toggleOne(_screenActive, participantPeer, false);
	}

}

void Members::Controller::appendInvitedUsers() {
	if (const auto id = _call->id()) {
		for (const auto user : _peer->owner().invitedToCallUsers(id)) {
			if (auto row = createInvitedRow(user)) {
				delegate()->peerListAppendRow(std::move(row));
			}
		}
		delegate()->peerListRefreshRows();
	}

	using Invite = Data::Session::InviteToCall;
	_peer->owner().invitesToCalls(
	) | rpl::filter([=](const Invite &invite) {
		return (invite.id == _call->id());
	}) | rpl::start_with_next([=](const Invite &invite) {
		if (auto row = createInvitedRow(invite.user)) {
			delegate()->peerListAppendRow(std::move(row));
			delegate()->peerListRefreshRows();
		}
	}, _lifetime);
}

void Members::Controller::updateRow(
		const std::optional<Data::GroupCallParticipant> &was,
		const Data::GroupCallParticipant &now) {
	auto reorderIfInvitedBefore = 0;
	auto checkPosition = (Row*)nullptr;
	auto addedToBottom = (Row*)nullptr;
	if (const auto row = findRow(now.peer)) {
		if (row->state() == Row::State::Invited) {
			reorderIfInvitedBefore = row->absoluteIndex();
		}
		updateRow(row, &now);
		if ((now.speaking && (!was || !was->speaking))
			|| (now.raisedHandRating != (was ? was->raisedHandRating : 0))
			|| (!now.canSelfUnmute && was && was->canSelfUnmute)) {
			checkPosition = row;
		}
	} else if (auto row = createRow(now)) {
		if (row->speaking()) {
			delegate()->peerListPrependRow(std::move(row));
		} else {
			reorderIfInvitedBefore = delegate()->peerListFullRowsCount();
			if (now.raisedHandRating != 0) {
				checkPosition = row.get();
			} else {
				addedToBottom = row.get();
			}
			delegate()->peerListAppendRow(std::move(row));
		}
		delegate()->peerListRefreshRows();
	}
	static constexpr auto kInvited = Row::State::Invited;
	const auto reorder = [&] {
		const auto count = reorderIfInvitedBefore;
		if (count <= 0) {
			return false;
		}
		const auto row = delegate()->peerListRowAt(
			reorderIfInvitedBefore - 1).get();
		return (static_cast<Row*>(row)->state() == kInvited);
	}();
	if (reorder) {
		delegate()->peerListPartitionRows([](const PeerListRow &row) {
			return static_cast<const Row&>(row).state() != kInvited;
		});
	}
	if (checkPosition) {
		checkRowPosition(checkPosition);
	} else if (addedToBottom) {
		const auto real = _call->lookupReal();
		if (real && real->joinedToTop()) {
			const auto proj = [&](const PeerListRow &other) {
				const auto &real = static_cast<const Row&>(other);
				return real.speaking()
					? 2
					: (&real == addedToBottom)
					? 1
					: 0;
			};
			delegate()->peerListSortRows([&](
					const PeerListRow &a,
					const PeerListRow &b) {
				return proj(a) > proj(b);
			});
		}
	}
}

bool Members::Controller::allRowsAboveAreSpeaking(not_null<Row*> row) const {
	const auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		const auto above = delegate()->peerListRowAt(i);
		if (above == row) {
			// All rows above are speaking.
			return true;
		} else if (!static_cast<Row*>(above.get())->speaking()) {
			break;
		}
	}
	return false;
}

bool Members::Controller::allRowsAboveMoreImportantThanHand(
		not_null<Row*> row,
		uint64 raiseHandRating) const {
	Expects(raiseHandRating > 0);

	const auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		const auto above = delegate()->peerListRowAt(i);
		if (above == row) {
			// All rows above are 'more important' than this raised hand.
			return true;
		}
		const auto real = static_cast<Row*>(above.get());
		const auto state = real->state();
		if (state == Row::State::Muted
			|| (state == Row::State::RaisedHand
				&& real->raisedHandRating() < raiseHandRating)) {
			break;
		}
	}
	return false;
}

bool Members::Controller::needToReorder(not_null<Row*> row) const {
	// All reorder cases:
	// - bring speaking up
	// - bring raised hand up
	// - bring muted down

	if (row->speaking()) {
		return !allRowsAboveAreSpeaking(row);
	} else if (!_peer->canManageGroupCall()) {
		// Raising hands reorder participants only for voice chat admins.
		return false;
	}

	const auto rating = row->raisedHandRating();
	if (!rating && row->state() != Row::State::Muted) {
		return false;
	}
	if (rating > 0 && !allRowsAboveMoreImportantThanHand(row, rating)) {
		return true;
	}
	const auto index = row->absoluteIndex();
	if (index + 1 == delegate()->peerListFullRowsCount()) {
		// Last one, can't bring lower.
		return false;
	}
	const auto next = delegate()->peerListRowAt(index + 1);
	const auto state = static_cast<Row*>(next.get())->state();
	if ((state != Row::State::Muted) && (state != Row::State::RaisedHand)) {
		return true;
	}
	if (!rating && static_cast<Row*>(next.get())->raisedHandRating()) {
		return true;
	}
	return false;
}

void Members::Controller::checkRowPosition(not_null<Row*> row) {
	if (_menu) {
		// Don't reorder rows while we show the popup menu.
		_menuCheckRowsAfterHidden.emplace(row->peer());
		return;
	} else if (!needToReorder(row)) {
		return;
	}

	// Someone started speaking and has a non-speaking row above him.
	// Or someone raised hand and has force muted above him.
	// Or someone was forced muted and had can_unmute_self below him. Sort.
	static constexpr auto kTop = std::numeric_limits<uint64>::max();
	const auto projForAdmin = [&](const PeerListRow &other) {
		const auto &real = static_cast<const Row&>(other);
		return real.speaking()
			// Speaking 'row' to the top, all other speaking below it.
			? (&real == row.get() ? kTop : (kTop - 1))
			: (real.raisedHandRating() > 0)
			// Then all raised hands sorted by rating.
			? real.raisedHandRating()
			: (real.state() == Row::State::Muted)
			// All force muted at the bottom, but 'row' still above others.
			? (&real == row.get() ? 1ULL : 0ULL)
			// All not force-muted lie between raised hands and speaking.
			: (kTop - 2);
	};
	const auto projForOther = [&](const PeerListRow &other) {
		const auto &real = static_cast<const Row&>(other);
		return real.speaking()
			// Speaking 'row' to the top, all other speaking below it.
			? (&real == row.get() ? kTop : (kTop - 1))
			: 0ULL;
	};

	using Comparator = Fn<bool(const PeerListRow&, const PeerListRow&)>;
	const auto makeComparator = [&](const auto &proj) -> Comparator {
		return [&](const PeerListRow &a, const PeerListRow &b) {
			return proj(a) > proj(b);
		};
	};
	delegate()->peerListSortRows(_peer->canManageGroupCall()
		? makeComparator(projForAdmin)
		: makeComparator(projForOther));
}

void Members::Controller::updateRow(
		not_null<Row*> row,
		const Data::GroupCallParticipant *participant) {
	const auto wasSounding = row->sounding();
	const auto wasSsrc = row->ssrc();
	const auto wasInChat = (row->state() != Row::State::Invited);
	row->setSkipLevelUpdate(_skipRowLevelUpdate);
	row->updateState(participant);
	const auto nowSounding = row->sounding();
	const auto nowSsrc = row->ssrc();

	const auto wasNoSounding = _soundingRowBySsrc.empty();
	if (wasSsrc == nowSsrc) {
		if (nowSounding != wasSounding) {
			if (nowSounding) {
				_soundingRowBySsrc.emplace(nowSsrc, row);
			} else {
				_soundingRowBySsrc.remove(nowSsrc);
			}
		}
	} else {
		_soundingRowBySsrc.remove(wasSsrc);
		if (nowSounding) {
			Assert(nowSsrc != 0);
			_soundingRowBySsrc.emplace(nowSsrc, row);
		}
	}
	const auto nowNoSounding = _soundingRowBySsrc.empty();
	if (wasNoSounding && !nowNoSounding) {
		_soundingAnimation.start();
	} else if (nowNoSounding && !wasNoSounding) {
		_soundingAnimation.stop();
	}

	delegate()->peerListUpdateRow(row);
}

void Members::Controller::removeRow(not_null<Row*> row) {
	_soundingRowBySsrc.remove(row->ssrc());
	delegate()->peerListRemoveRow(row);
}

void Members::Controller::updateRowLevel(
		not_null<Row*> row,
		float level) {
	if (_skipRowLevelUpdate) {
		return;
	}
	row->updateLevel(level);
}

Row *Members::Controller::findRow(
		not_null<PeerData*> participantPeer) const {
	return static_cast<Row*>(
		delegate()->peerListFindRow(participantPeer->id.value));
}

void Members::Controller::setMode(PanelMode mode) {
	if (_mode == mode) {
		return;
	}
	_mode = mode;
}

const Data::GroupCallParticipant *Members::Controller::findParticipant(
		const std::string &endpoint) const {
	if (endpoint.empty()) {
		return nullptr;
	}
	const auto real = _call->lookupReal();
	if (!real) {
		return nullptr;
	} else if (endpoint == _call->screenSharingEndpoint()
		|| endpoint == _call->cameraSharingEndpoint()) {
		return real->participantByPeer(_call->joinAs());
	} else {
		return real->participantByEndpoint(endpoint);
	}
}

const std::string &Members::Controller::computeScreenEndpoint(
		not_null<const Data::GroupCallParticipant*> participant) const {
	return (participant->peer == _call->joinAs())
		? _call->screenSharingEndpoint()
		: participant->screenEndpoint();
}

const std::string &Members::Controller::computeCameraEndpoint(
		not_null<const Data::GroupCallParticipant*> participant) const {
	return (participant->peer == _call->joinAs())
		? _call->cameraSharingEndpoint()
		: participant->cameraEndpoint();
}

Main::Session &Members::Controller::session() const {
	return _call->peer()->session();
}

void Members::Controller::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Disabled);
	//delegate()->peerListSetTitle(std::move(title));
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));

	if (const auto real = _call->lookupReal()) {
		prepareRows(real);
	} else if (auto row = createRowForMe()) {
		delegate()->peerListAppendRow(std::move(row));
		delegate()->peerListRefreshRows();
	}

	loadMoreRows();
	appendInvitedUsers();
	_prepared = true;

	setupListChangeViewers();
}

bool Members::Controller::isMe(not_null<PeerData*> participantPeer) const {
	return (_call->joinAs() == participantPeer);
}

void Members::Controller::prepareRows(not_null<Data::GroupCall*> real) {
	auto foundMe = false;
	auto changed = false;
	auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count;) {
		auto row = delegate()->peerListRowAt(i);
		auto participantPeer = row->peer();
		if (isMe(participantPeer)) {
			foundMe = true;
			++i;
			continue;
		}
		if (real->participantByPeer(participantPeer)) {
			++i;
		} else {
			changed = true;
			removeRow(static_cast<Row*>(row.get()));
			--count;
		}
	}
	if (!foundMe) {
		const auto me = _call->joinAs();
		const auto participant = real->participantByPeer(me);
		auto row = participant
			? createRow(*participant)
			: createRowForMe();
		if (row) {
			changed = true;
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	for (const auto &participant : real->participants()) {
		if (auto row = createRow(participant)) {
			changed = true;
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	if (changed) {
		delegate()->peerListRefreshRows();
	}
}

void Members::Controller::loadMoreRows() {
	if (const auto real = _call->lookupReal()) {
		real->requestParticipants();
	}
}

auto Members::Controller::toggleMuteRequests() const
-> rpl::producer<MuteRequest> {
	return _toggleMuteRequests.events();
}

auto Members::Controller::changeVolumeRequests() const
-> rpl::producer<VolumeRequest> {
	return _changeVolumeRequests.events();
}

bool Members::Controller::rowIsMe(not_null<PeerData*> participantPeer) {
	return isMe(participantPeer);
}

bool Members::Controller::rowCanMuteMembers() {
	return _peer->canManageGroupCall();
}

void Members::Controller::rowUpdateRow(not_null<Row*> row) {
	delegate()->peerListUpdateRow(row);
}

void Members::Controller::rowScheduleRaisedHandStatusRemove(
		not_null<Row*> row) {
	const auto id = row->id();
	const auto when = crl::now() + kKeepRaisedHandStatusDuration;
	const auto i = _raisedHandStatusRemoveAt.find(id);
	if (i != _raisedHandStatusRemoveAt.end()) {
		i->second = when;
	} else {
		_raisedHandStatusRemoveAt.emplace(id, when);
	}
	scheduleRaisedHandStatusRemove();
}

void Members::Controller::scheduleRaisedHandStatusRemove() {
	auto waiting = crl::time(0);
	const auto now = crl::now();
	for (auto i = begin(_raisedHandStatusRemoveAt)
		; i != end(_raisedHandStatusRemoveAt);) {
		if (i->second <= now) {
			if (const auto row = delegate()->peerListFindRow(i->first)) {
				static_cast<Row*>(row)->clearRaisedHandStatus();
			}
			i = _raisedHandStatusRemoveAt.erase(i);
		} else {
			if (!waiting || waiting > (i->second - now)) {
				waiting = i->second - now;
			}
			++i;
		}
	}
	if (waiting > 0) {
		if (!_raisedHandStatusRemoveTimer.isActive()
			|| _raisedHandStatusRemoveTimer.remainingTime() > waiting) {
			_raisedHandStatusRemoveTimer.callOnce(waiting);
		}
	}
}

void Members::Controller::rowPaintIcon(
		Painter &p,
		QRect rect,
		const IconState &state) {
	if (_mode == PanelMode::Wide && state.style == MembersRowStyle::None) {
		return;
	}
	//const auto narrowUserpic = (state.style == MembersRowStyle::Userpic);
	//const auto narrowVideo = (state.style == MembersRowStyle::Video);
	const auto narrow = (state.style == MembersRowStyle::Narrow);
	if (!narrow && state.invited) {
		st::groupCallMemberInvited.paintInCenter(
			p,
			QRect(
				rect.topLeft() + st::groupCallMemberInvitedPosition,
				st::groupCallMemberInvited.size()));
		return;
	}
	const auto largeVideo = (state.style == MembersRowStyle::LargeVideo);
	const auto &greenIcon = largeVideo
		? st::groupCallLargeVideoCrossLine.icon
		//: narrowVideo
		//? st::groupCallVideoCrossLine.icon
		//: narrowUserpic
		//? st::groupCallNarrowColoredCrossLine.icon
		: narrow
		? st::groupCallNarrowColoredCrossLine.icon
		: st::groupCallMemberColoredCrossLine.icon;
	const auto left = rect.x() + (rect.width() - greenIcon.width()) / 2;
	const auto top = rect.y() + (rect.height() - greenIcon.height()) / 2;
	if (state.speaking == 1. && !state.mutedByMe) {
		// Just green icon, no cross, no coloring.
		greenIcon.paintInCenter(p, rect);
		return;
	} else if (state.speaking == 0. && (!narrow || !state.mutedByMe)) {
		if (state.active == 1.) {
			// Just gray icon, no cross, no coloring.
			const auto &grayIcon = largeVideo
				? st::groupCallLargeVideoCrossLine.icon
				//: narrowVideo
				//? st::groupCallVideoCrossLine.icon
				//: narrowUserpic
				//? st::groupCallNarrowInactiveCrossLine.icon
				: narrow
				? st::groupCallNarrowInactiveCrossLine.icon
				: st::groupCallMemberInactiveCrossLine.icon;
			grayIcon.paintInCenter(p, rect);
			return;
		} else if (state.active == 0.) {
			if (state.muted == 1.) {
				if (state.raisedHand) {
					(narrow
						? st::groupCallNarrowRaisedHand
						: st::groupCallMemberRaisedHand).paintInCenter(p, rect);
					return;
				}
				// Red crossed icon, colorized once, cached as last frame.
				auto &line = largeVideo
					? _videoLargeCrossLine
					//: narrowVideo
					//? _videoNarrowCrossLine
					//: narrowUserpic
					//? _coloredNarrowCrossLine
					: narrow
					? _coloredNarrowCrossLine
					: _coloredCrossLine;
				const auto color = (largeVideo/* || narrowVideo*/)
					? std::nullopt
					: std::make_optional(narrow
						? st::groupCallMemberNotJoinedStatus->c
						: st::groupCallMemberMutedIcon->c);
				line.paint(
					p,
					left,
					top,
					1.,
					color);
				return;
			} else if (state.muted == 0.) {
				// Gray crossed icon, no coloring, cached as last frame.
				auto &line = largeVideo
					? _videoLargeCrossLine
					//: narrowVideo
					//? _videoNarrowCrossLine
					//: narrowUserpic
					//? _inactiveNarrowCrossLine
					: narrow
					? _inactiveNarrowCrossLine
					: _inactiveCrossLine;
				line.paint(p, left, top, 1.);
				return;
			}
		}
	}
	const auto activeInactiveColor = anim::color(
		(narrow
			? st::groupCallMemberNotJoinedStatus
			: st::groupCallMemberInactiveIcon),
		(narrow
			? st::groupCallMemberActiveStatus
			: state.mutedByMe
			? st::groupCallMemberMutedIcon
			: st::groupCallMemberActiveIcon),
		state.speaking);
	const auto iconColor = anim::color(
		activeInactiveColor,
		(narrow
			? st::groupCallMemberNotJoinedStatus
			: st::groupCallMemberMutedIcon),
		state.muted);
	const auto color = (largeVideo/* || narrowVideo*/)
		? std::nullopt
		: std::make_optional((narrow && state.mutedByMe)
			? st::groupCallMemberMutedIcon->c
			: (narrow && state.raisedHand)
			? st::groupCallMemberInactiveStatus->c
			: iconColor);

	// Don't use caching of the last frame,
	// because 'muted' may animate color.
	const auto crossProgress = std::min(1. - state.active, 0.9999);
	auto &line = largeVideo
		? _videoLargeCrossLine
		//: narrowVideo
		//? _videoNarrowCrossLine
		//: narrowUserpic
		//? _inactiveNarrowCrossLine
		: narrow
		? _inactiveNarrowCrossLine
		: _inactiveCrossLine;
	line.paint(p, left, top, crossProgress, color);
}

int Members::Controller::rowPaintStatusIcon(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		not_null<MembersRow*> row,
		const IconState &state) {
	Expects(state.style == MembersRowStyle::Narrow);

	if (_mode != PanelMode::Wide) {
		return 0;
	}
	const auto &icon = st::groupCallNarrowColoredCrossLine.icon;
	x += st::groupCallNarrowIconPosition.x();
	y += st::groupCallNarrowIconPosition.y();
	const auto rect = QRect(x, y, icon.width(), icon.height());
	rowPaintIcon(p, rect, state);
	x += icon.width();
	auto result = st::groupCallNarrowIconSkip;
	const auto participantPeer = row->peer();
	const auto camera = _cameraActive.contains(participantPeer);
	const auto screen = _screenActive.contains(participantPeer);
	if (camera || screen) {
		const auto activeInactiveColor = anim::color(
			st::groupCallMemberNotJoinedStatus,
			st::groupCallMemberActiveStatus,
			state.speaking);
		const auto iconColor = anim::color(
			activeInactiveColor,
			st::groupCallMemberNotJoinedStatus,
			state.muted);
		const auto other = state.mutedByMe
			? st::groupCallMemberMutedIcon->c
			: state.raisedHand
			? st::groupCallMemberInactiveStatus->c
			: iconColor;
		const auto color = (state.speaking == 1. && !state.mutedByMe)
			? st::groupCallMemberActiveStatus->c
			: (state.speaking == 0.
				? (state.active == 1.
					? st::groupCallMemberNotJoinedStatus->c
					: (state.active == 0.
						? (state.muted == 1.
							? (state.raisedHand
								? st::groupCallMemberInactiveStatus->c
								: st::groupCallMemberNotJoinedStatus->c)
							: (state.muted == 0.
								? st::groupCallMemberNotJoinedStatus->c
								: other))
						: other))
				: other);
		if (camera) {
			st::groupCallNarrowCameraIcon.paint(p, x, y, outerWidth, other);
			x += st::groupCallNarrowCameraIcon.width();
			result += st::groupCallNarrowCameraIcon.width();
		}
		if (screen) {
			st::groupCallNarrowScreenIcon.paint(p, x, y, outerWidth, other);
			x += st::groupCallNarrowScreenIcon.width();
			result += st::groupCallNarrowScreenIcon.width();
		}
	}
	return result;
}

bool Members::Controller::rowIsNarrow() {
	return (_mode == PanelMode::Wide);
}

//void Members::Controller::rowPaintNarrowBackground(
//		Painter &p,
//		int x,
//		int y,
//		bool selected) {
//	(selected ? _narrowRoundRectSelected : _narrowRoundRect).paint(
//		p,
//		{ QPoint(x, y), st::groupCallNarrowSize });
//}
//
//void Members::Controller::rowPaintNarrowBorder(
//		Painter &p,
//		int x,
//		int y,
//		not_null<Row*> row) {
//	if (_call->videoEndpointLarge().peer != row->peer().get()) {
//		return;
//	}
//	auto hq = PainterHighQualityEnabler(p);
//	p.setBrush(Qt::NoBrush);
//	auto pen = st::groupCallMemberActiveIcon->p;
//	pen.setWidthF(st::groupCallNarrowOutline);
//	p.setPen(pen);
//	p.drawRoundedRect(
//		QRect{ QPoint(x, y), st::groupCallNarrowSize },
//		st::roundRadiusLarge,
//		st::roundRadiusLarge);
//}
//
//void Members::Controller::rowPaintNarrowShadow(
//		Painter &p,
//		int x,
//		int y,
//		int sizew,
//		int sizeh) {
//	if (_narrowShadow.isNull()) {
//		_narrowShadow = GenerateShadow(
//			st::groupCallNarrowShadowHeight,
//			0,
//			kShadowMaxAlpha);
//	}
//	const auto height = st::groupCallNarrowShadowHeight;
//	p.drawImage(
//		QRect(x, y + sizeh - height, sizew, height),
//		_narrowShadow);
//}
//
//int Members::Controller::customRowHeight() {
//	return st::groupCallNarrowSize.height() + st::groupCallNarrowRowSkip * 2;
//}
//
//void Members::Controller::customRowPaint(
//		Painter &p,
//		crl::time now,
//		not_null<PeerListRow*> row,
//		bool selected) {
//	const auto real = static_cast<Row*>(row.get());
//	const auto width = st::groupCallNarrowSize.width();
//	const auto height = st::groupCallNarrowSize.height();
//	real->paintComplexUserpic(
//		p,
//		st::groupCallNarrowSkip,
//		st::groupCallNarrowRowSkip,
//		width,
//		width,
//		height,
//		PanelMode::Wide,
//		selected);
//}
//
//bool Members::Controller::customRowSelectionPoint(
//		not_null<PeerListRow*> row,
//		int x,
//		int y) {
//	return x >= st::groupCallNarrowSkip
//		&& x < st::groupCallNarrowSkip + st::groupCallNarrowSize.width()
//		&& y >= st::groupCallNarrowRowSkip
//		&& y < st::groupCallNarrowRowSkip + st::groupCallNarrowSize.height();
//}
//
//Fn<QImage()> Members::Controller::customRowRippleMaskGenerator() {
//	return [] {
//		return Ui::RippleAnimation::roundRectMask(
//			st::groupCallNarrowSize,
//			st::roundRadiusLarge);
//	};
//}

auto Members::Controller::kickParticipantRequests() const
-> rpl::producer<not_null<PeerData*>>{
	return _kickParticipantRequests.events();
}

void Members::Controller::rowClicked(not_null<PeerListRow*> row) {
	if (!toggleRowVideo(row)) {
		showRowMenu(row);
	}
}

void Members::Controller::showRowMenu(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, [=](not_null<Ui::PopupMenu*> menu) {
		if (!_menu || _menu.get() != menu) {
			return;
		}
		auto saved = base::take(_menu);
		for (const auto peer : base::take(_menuCheckRowsAfterHidden)) {
			if (const auto row = findRow(peer)) {
				checkRowPosition(row);
			}
		}
		_menu = std::move(saved);
	});
}

bool Members::Controller::toggleRowVideo(not_null<PeerListRow*> row) {
	return false;
	//const auto real = _call->lookupReal();
	//if (!real) {
	//	return false;
	//}
	//const auto participantPeer = row->peer();
	//const auto isMe = (participantPeer == _call->joinAs());
	//const auto participant = real->participantByPeer(participantPeer);
	//if (!participant) {
	//	return false;
	//}
	//const auto params = participant->videoParams.get();
	//const auto empty = std::string();
	//const auto &camera = isMe
	//	? _call->cameraSharingEndpoint()
	//	: (params && _call->streamsVideo(params->camera.endpoint))
	//	? params->camera.endpoint
	//	: empty;
	//const auto &screen = isMe
	//	? _call->screenSharingEndpoint()
	//	: (params && _call->streamsVideo(params->screen.endpoint))
	//	? params->screen.endpoint
	//	: empty;
	//const auto &large = _call->videoEndpointLarge().endpoint;
	//const auto show = [&] {
	//	if (!screen.empty() && large != screen) {
	//		return screen;
	//	} else if (!camera.empty() && large != camera) {
	//		return camera;
	//	}
	//	return std::string();
	//}();
	//if (show.empty()) {
	//	return false;
	//} else if (_call->videoEndpointPinned()) {
	//	_call->pinVideoEndpoint({ participantPeer, show });
	//} else {
	//	_call->showVideoEndpointLarge({ participantPeer, show });
	//}
	//return true;
}

void Members::Controller::rowActionClicked(
		not_null<PeerListRow*> row) {
	showRowMenu(row);
}

base::unique_qptr<Ui::PopupMenu> Members::Controller::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = createRowContextMenu(parent, row);

	if (result) {
		// First clear _menu value, so that we don't check row positions yet.
		base::take(_menu);

		// Here unique_qptr is used like a shared pointer, where
		// not the last destroyed pointer destroys the object, but the first.
		_menu = base::unique_qptr<Ui::PopupMenu>(result.get());
	}

	return result;
}

base::unique_qptr<Ui::PopupMenu> Members::Controller::createRowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto participantPeer = row->peer();
	const auto real = static_cast<Row*>(row.get());

	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::groupCallPopupMenu);

	const auto muteState = real->state();
	const auto admin = IsGroupCallAdmin(_peer, participantPeer);
	const auto session = &_peer->session();
	const auto getCurrentWindow = [=]() -> Window::SessionController* {
		if (const auto window = Core::App().activeWindow()) {
			if (const auto controller = window->sessionController()) {
				if (&controller->session() == session) {
					return controller;
				}
			}
		}
		return nullptr;
	};
	const auto getWindow = [=] {
		if (const auto current = getCurrentWindow()) {
			return current;
		} else if (&Core::App().domain().active() != &session->account()) {
			Core::App().domain().activate(&session->account());
		}
		return getCurrentWindow();
	};
	const auto performOnMainWindow = [=](auto callback) {
		if (const auto window = getWindow()) {
			if (_menu) {
				_menu->discardParentReActivate();

				// We must hide PopupMenu before we activate the MainWindow,
				// otherwise we set focus in field inside MainWindow and then
				// PopupMenu::hide activates back the group call panel :(
				_menu = nullptr;
			}
			callback(window);
			window->widget()->activate();
		}
	};
	const auto showProfile = [=] {
		performOnMainWindow([=](not_null<Window::SessionController*> window) {
			window->showPeerInfo(participantPeer);
		});
	};
	const auto showHistory = [=] {
		performOnMainWindow([=](not_null<Window::SessionController*> window) {
			window->showPeerHistory(
				participantPeer,
				Window::SectionShow::Way::Forward);
		});
	};
	const auto removeFromVoiceChat = crl::guard(this, [=] {
		_kickParticipantRequests.fire_copy(participantPeer);
	});

	if (const auto real = _call->lookupReal()) {
		const auto participant = real->participantByPeer(participantPeer);
		if (participant) {
			const auto &pinned = _call->videoEndpointPinned();
			const auto &camera = computeCameraEndpoint(participant);
			const auto &screen = computeScreenEndpoint(participant);
			if (!camera.empty()) {
				if (pinned.id == camera) {
					result->addAction(
						tr::lng_group_call_context_unpin_camera(tr::now),
						[=] { _call->pinVideoEndpoint(VideoEndpoint()); });
				} else {
					result->addAction(
						tr::lng_group_call_context_pin_camera(tr::now),
						[=] { _call->pinVideoEndpoint(VideoEndpoint{
							participantPeer,
							camera }); });
				}
			}
			if (!screen.empty()) {
				if (pinned.id == screen) {
					result->addAction(
						tr::lng_group_call_context_unpin_screen(tr::now),
						[=] { _call->pinVideoEndpoint(VideoEndpoint()); });
				} else {
					result->addAction(
						tr::lng_group_call_context_pin_screen(tr::now),
						[=] { _call->pinVideoEndpoint(VideoEndpoint{
							participantPeer,
							screen }); });
				}
			}
		}
	}

	if (real->ssrc() != 0
		&& (!isMe(participantPeer) || _peer->canManageGroupCall())) {
		addMuteActionsToContextMenu(result, participantPeer, admin, real);
	}

	if (isMe(participantPeer)) {
		if (_call->muted() == MuteState::RaisedHand) {
			const auto removeHand = [=] {
				if (_call->muted() == MuteState::RaisedHand) {
					_call->setMutedAndUpdate(MuteState::ForceMuted);
				}
			};
			result->addAction(
				tr::lng_group_call_context_remove_hand(tr::now),
				removeHand);
		}
	} else {
		result->addAction(
			(participantPeer->isUser()
				? tr::lng_context_view_profile(tr::now)
				: participantPeer->isBroadcast()
				? tr::lng_context_view_channel(tr::now)
				: tr::lng_context_view_group(tr::now)),
			showProfile);
		if (participantPeer->isUser()) {
			result->addAction(
				tr::lng_context_send_message(tr::now),
				showHistory);
		}
		const auto canKick = [&] {
			const auto user = participantPeer->asUser();
			if (static_cast<Row*>(row.get())->state()
				== Row::State::Invited) {
				return false;
			} else if (const auto chat = _peer->asChat()) {
				return chat->amCreator()
					|| (user
						&& chat->canBanMembers()
						&& !chat->admins.contains(user));
			} else if (const auto channel = _peer->asChannel()) {
				return channel->canRestrictParticipant(participantPeer);
			}
			return false;
		}();
		if (canKick) {
			result->addAction(MakeAttentionAction(
				result->menu(),
				tr::lng_group_call_context_remove(tr::now),
				removeFromVoiceChat));
		}
	}
	if (result->empty()) {
		return nullptr;
	}
	return result;
}

void Members::Controller::addMuteActionsToContextMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> participantPeer,
		bool participantIsCallAdmin,
		not_null<Row*> row) {
	const auto muteString = [=] {
		return (_peer->canManageGroupCall()
			? tr::lng_group_call_context_mute
			: tr::lng_group_call_context_mute_for_me)(tr::now);
	};

	const auto unmuteString = [=] {
		return (_peer->canManageGroupCall()
			? tr::lng_group_call_context_unmute
			: tr::lng_group_call_context_unmute_for_me)(tr::now);
	};

	const auto toggleMute = crl::guard(this, [=](bool mute, bool local) {
		_toggleMuteRequests.fire(Group::MuteRequest{
			.peer = participantPeer,
			.mute = mute,
			.locallyOnly = local,
		});
	});
	const auto changeVolume = crl::guard(this, [=](
			int volume,
			bool local) {
		_changeVolumeRequests.fire(Group::VolumeRequest{
			.peer = participantPeer,
			.volume = std::clamp(volume, 1, Group::kMaxVolume),
			.locallyOnly = local,
		});
	});

	const auto muteState = row->state();
	const auto isMuted = (muteState == Row::State::Muted)
		|| (muteState == Row::State::RaisedHand)
		|| (muteState == Row::State::MutedByMe);

	auto mutesFromVolume = rpl::never<bool>() | rpl::type_erased();

	if (!isMuted || _call->joinAs() == participantPeer) {
		auto otherParticipantStateValue
			= _call->otherParticipantStateValue(
		) | rpl::filter([=](const Group::ParticipantState &data) {
			return data.peer == participantPeer;
		});

		auto volumeItem = base::make_unique_q<MenuVolumeItem>(
			menu->menu(),
			st::groupCallPopupMenu.menu,
			otherParticipantStateValue,
			row->volume(),
			Group::kMaxVolume,
			isMuted);

		mutesFromVolume = volumeItem->toggleMuteRequests();

		volumeItem->toggleMuteRequests(
		) | rpl::start_with_next([=](bool muted) {
			if (muted) {
				// Slider value is changed after the callback is called.
				// To capture good state inside the slider frame we postpone.
				crl::on_main(menu, [=] {
					menu->hideMenu();
				});
			}
			toggleMute(muted, false);
		}, volumeItem->lifetime());

		volumeItem->toggleMuteLocallyRequests(
		) | rpl::start_with_next([=](bool muted) {
			if (!isMe(participantPeer)) {
				toggleMute(muted, true);
			}
		}, volumeItem->lifetime());

		volumeItem->changeVolumeRequests(
		) | rpl::start_with_next([=](int volume) {
			changeVolume(volume, false);
		}, volumeItem->lifetime());

		volumeItem->changeVolumeLocallyRequests(
		) | rpl::start_with_next([=](int volume) {
			if (!isMe(participantPeer)) {
				changeVolume(volume, true);
			}
		}, volumeItem->lifetime());

		menu->addAction(std::move(volumeItem));
	};

	const auto muteAction = [&]() -> QAction* {
		if (muteState == Row::State::Invited
			|| isMe(participantPeer)
			|| (muteState == Row::State::Inactive
				&& participantIsCallAdmin
				&& _peer->canManageGroupCall())) {
			return nullptr;
		}
		auto callback = [=] {
			const auto state = row->state();
			const auto muted = (state == Row::State::Muted)
				|| (state == Row::State::RaisedHand)
				|| (state == Row::State::MutedByMe);
			toggleMute(!muted, false);
		};
		return menu->addAction(
			isMuted ? unmuteString() : muteString(),
			std::move(callback));
	}();

	if (muteAction) {
		std::move(
			mutesFromVolume
		) | rpl::start_with_next([=](bool muted) {
			muteAction->setText(muted ? unmuteString() : muteString());
		}, menu->lifetime());
	}
}

std::unique_ptr<Row> Members::Controller::createRowForMe() {
	auto result = std::make_unique<Row>(this, _call->joinAs());
	updateRow(result.get(), nullptr);
	return result;
}

std::unique_ptr<Row> Members::Controller::createRow(
		const Data::GroupCallParticipant &participant) {
	auto result = std::make_unique<Row>(this, participant.peer);
	updateRow(result.get(), &participant);
	//const auto &camera = computeCameraEndpoint(&participant);
	//const auto &screen = computeScreenEndpoint(&participant);
	//if (!screen.empty() && _largeEndpoint != screen) {
	//	setRowVideoEndpoint(result.get(), screen);
	//} else if (!camera.empty() && _largeEndpoint != camera) {
	//	setRowVideoEndpoint(result.get(), camera);
	//}
	return result;
}

std::unique_ptr<Row> Members::Controller::createInvitedRow(
		not_null<PeerData*> participantPeer) {
	if (findRow(participantPeer)) {
		return nullptr;
	}
	auto result = std::make_unique<Row>(this, participantPeer);
	updateRow(result.get(), nullptr);
	return result;
}

Members::Members(
	not_null<QWidget*> parent,
	not_null<GroupCall*> call,
	not_null<Viewport*> viewport,
	PanelMode mode)
: RpWidget(parent)
, _call(call)
, _viewport(viewport)
, _mode(mode)
, _scroll(this)
, _listController(std::make_unique<Controller>(call, parent, mode))
, _layout(_scroll->setOwnedWidget(
	object_ptr<Ui::VerticalLayout>(_scroll.data())))
, _videoWrap(_layout->add(object_ptr<Ui::RpWidget>(_layout.get()))) {
	setupAddMember(call);
	setupList();
	setContent(_list);
	setupFakeRoundCorners();
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));
	grabViewport();
	trackViewportGeometry();
}

Members::~Members() = default;

auto Members::toggleMuteRequests() const
-> rpl::producer<Group::MuteRequest> {
	return _listController->toggleMuteRequests();
}

auto Members::changeVolumeRequests() const
-> rpl::producer<Group::VolumeRequest> {
	return _listController->changeVolumeRequests();
}

auto Members::kickParticipantRequests() const
-> rpl::producer<not_null<PeerData*>> {
	return _listController->kickParticipantRequests();
}

int Members::desiredHeight() const {
	const auto count = [&] {
		if (const auto real = _call->lookupReal()) {
			return real->fullCount();
		}
		return 0;
	}();
	const auto use = std::max(count, _list->fullRowsCount());
	const auto single = /*(_mode.current() == PanelMode::Wide)
		? (st::groupCallNarrowSize.height() + st::groupCallNarrowRowSkip * 2)
		: */st::groupCallMembersList.item.height;
	const auto desired = (_layout->height() - _list->height())
		+ (use * single)
		+ (use ? st::lineWidth : 0);
	return std::max(height(), desired);
}

rpl::producer<int> Members::desiredHeightValue() const {
	return rpl::combine(
		heightValue(),
		_addMemberButton.value(),
		_listController->fullCountValue(),
		_mode.value()
	) | rpl::map([=] {
		return desiredHeight();
	});
}

void Members::setupAddMember(not_null<GroupCall*> call) {
	using namespace rpl::mappers;

	const auto peer = call->peer();
	if (const auto channel = peer->asBroadcast()) {
		_canAddMembers = rpl::single(
			false
		) | rpl::then(_call->real(
		) | rpl::map([=] {
			return Data::PeerFlagValue(
				channel,
				MTPDchannel::Flag::f_username);
		}) | rpl::flatten_latest());
	} else {
		_canAddMembers = Data::CanWriteValue(peer.get());
		SubscribeToMigration(
			peer,
			lifetime(),
			[=](not_null<ChannelData*> channel) {
				_canAddMembers = Data::CanWriteValue(channel.get());
			});
	}

	rpl::combine(
		_canAddMembers.value(),
		_mode.value()
	) | rpl::start_with_next([=](bool can, PanelMode mode) {
		if (!can) {
			if (const auto old = _addMemberButton.current()) {
				delete old;
				_addMemberButton = nullptr;
				updateControlsGeometry();
			}
			return;
		}
		auto addMember = (Ui::AbstractButton*)nullptr;
		auto wrap = [&]() -> object_ptr<Ui::RpWidget> {
			//if (mode == PanelMode::Default) {
				auto result = Settings::CreateButton(
					_layout.get(),
					tr::lng_group_call_invite(),
					st::groupCallAddMember,
					&st::groupCallAddMemberIcon,
					st::groupCallAddMemberIconLeft,
					&st::groupCallMemberInactiveIcon);
				addMember = result.data();
				return result;
			//}
			//auto result = object_ptr<Ui::RpWidget>(_layout.get());
			//const auto skip = st::groupCallNarrowSkip;
			//const auto fullwidth = st::groupCallNarrowSize.width()
			//	+ 2 * skip;
			//const auto fullheight = st::groupCallNarrowAddMember.height
			//	+ st::groupCallNarrowRowSkip;
			//result->resize(fullwidth, fullheight);
			//const auto button = Ui::CreateChild<Ui::RoundButton>(
			//	result.data(),
			//	rpl::single(QString()),
			//	st::groupCallNarrowAddMember);
			//button->move(skip, 0);
			//const auto width = fullwidth - 2 * skip;
			//button->setFullWidth(width);
			//Settings::AddButtonIcon(
			//	button,
			//	&st::groupCallAddMemberIcon,
			//	(width - st::groupCallAddMemberIcon.width()) / 2,
			//	&st::groupCallMemberInactiveIcon);
			//addMember = button;
			//return result;
		}();
		addMember->show();
		addMember->clicks(
		) | rpl::to_empty | rpl::start_to_stream(
			_addMemberRequests,
			addMember->lifetime());
		wrap->show();
		wrap->resizeToWidth(_layout->width());
		delete _addMemberButton.current();
		_addMemberButton = wrap.data();
		_layout->insert(1, std::move(wrap));
	}, lifetime());
}

rpl::producer<> Members::enlargeVideo() const {
	return _enlargeVideoClicks.events();
}

Row *Members::lookupRow(not_null<PeerData*> peer) const {
	return _listController->findRow(peer);
}

void Members::setMode(PanelMode mode) {
	if (_mode.current() == mode) {
		return;
	}
	grabViewport(mode);
	_mode = mode;
	_listController->setMode(mode);
	trackViewportGeometry();
	//_list->setMode((mode == PanelMode::Wide)
	//	? PeerListContent::Mode::Custom
	//	: PeerListContent::Mode::Default);
}

QRect Members::getInnerGeometry() const {
	const auto addMembers = _addMemberButton.current();
	const auto add = addMembers ? addMembers->height() : 0;
	return QRect(
		0,
		-_scroll->scrollTop(),
		width(),
		_list->y() + _list->height());
}

rpl::producer<int> Members::fullCountValue() const {
	return _listController->fullCountValue();
}

void Members::setupList() {
	_listController->setStyleOverrides(&st::groupCallMembersList);
	_list = _layout->add(object_ptr<ListWidget>(
		_layout.get(),
		_listController.get()));
	const auto skip = _layout->add(object_ptr<Ui::RpWidget>(_layout.get()));
	_mode.value(
	) | rpl::start_with_next([=](PanelMode mode) {
		skip->resize(skip->width(), (mode == PanelMode::Default)
			? st::groupCallMembersBottomSkip
			: 0);
	}, skip->lifetime());

	rpl::combine(
		_mode.value(),
		_layout->heightValue()
	) | rpl::start_with_next([=] {
		resizeToList();
	}, _layout->lifetime());

	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue()
	) | rpl::start_with_next([=](int scrollTop, int scrollHeight) {
		_layout->setVisibleTopBottom(scrollTop, scrollTop + scrollHeight);
	}, _scroll->lifetime());

	updateControlsGeometry();
}

void Members::grabViewport() {
	grabViewport(_mode.current());
}

void Members::grabViewport(PanelMode mode) {
	if (mode != PanelMode::Default) {
		_viewportGrabLifetime.destroy();
		_videoWrap->resize(_videoWrap->width(), 0);
		return;
	}
	_viewport->setMode(mode, _videoWrap.get());
}

void Members::trackViewportGeometry() {
	if (_mode.current() != PanelMode::Default) {
		return;
	}
	const auto move = [=] {
		const auto maxTop = _viewport->fullHeight()
			- _viewport->widget()->height();
		if (maxTop < 0) {
			return;
		}
		const auto scrollTop = _scroll->scrollTop();
		const auto shift = std::min(scrollTop, maxTop);
		_viewport->setScrollTop(shift);
		if (_viewport->widget()->y() != shift) {
			_viewport->widget()->move(0, shift);
		}
	};
	const auto resize = [=] {
		_viewport->widget()->resize(
			_layout->width(),
			std::min(_scroll->height(), _viewport->fullHeight()));
	};
	_layout->widthValue(
	) | rpl::start_with_next([=](int width) {
		_viewport->resizeToWidth(width);
		resize();
	}, _viewportGrabLifetime);

	_scroll->heightValue(
	) | rpl::skip(1) | rpl::start_with_next(resize, _viewportGrabLifetime);

	_scroll->scrollTopValue(
	) | rpl::skip(1) | rpl::start_with_next(move, _viewportGrabLifetime);

	_viewport->fullHeightValue(
	) | rpl::start_with_next([=](int height) {
		_videoWrap->resize(_videoWrap->width(), height);
		move();
		resize();
	}, _viewportGrabLifetime);
}

void Members::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Members::resizeToList() {
	if (!_list) {
		return;
	}
	const auto newHeight = (_list->height() > 0)
		? (_layout->height() + st::lineWidth)
		: 0;
	if (height() == newHeight) {
		updateControlsGeometry();
	} else {
		resize(width(), newHeight);
	}
}

void Members::updateControlsGeometry() {
	_scroll->setGeometry(rect());
	_layout->resizeToWidth(width());
}

void Members::setupFakeRoundCorners() {
	const auto size = st::roundRadiusLarge;
	const auto full = 3 * size;
	const auto imagePartSize = size * cIntRetinaFactor();
	const auto imageSize = full * cIntRetinaFactor();
	const auto image = std::make_shared<QImage>(
		QImage(imageSize, imageSize, QImage::Format_ARGB32_Premultiplied));
	image->setDevicePixelRatio(cRetinaFactor());

	const auto refreshImage = [=] {
		image->fill(st::groupCallBg->c);
		{
			QPainter p(image.get());
			PainterHighQualityEnabler hq(p);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.setPen(Qt::NoPen);
			p.setBrush(Qt::transparent);
			p.drawRoundedRect(0, 0, full, full, size, size);
		}
	};

	const auto create = [&](QPoint imagePartOrigin) {
		const auto result = Ui::CreateChild<Ui::RpWidget>(_layout.get());
		result->show();
		result->resize(size, size);
		result->setAttribute(Qt::WA_TransparentForMouseEvents);
		result->paintRequest(
		) | rpl::start_with_next([=] {
			QPainter(result).drawImage(
				result->rect(),
				*image,
				QRect(imagePartOrigin, QSize(imagePartSize, imagePartSize)));
		}, result->lifetime());
		result->raise();
		return result;
	};
	const auto shift = imageSize - imagePartSize;
	const auto topleft = create({ 0, 0 });
	const auto topright = create({ shift, 0 });
	const auto bottomleft = create({ 0, shift });
	const auto bottomright = create({ shift, shift });

	rpl::combine(
		_list->geometryValue(),
		_addMemberButton.value() | rpl::map([=](Ui::RpWidget *widget) {
			topleft->raise();
			topright->raise();
			bottomleft->raise();
			bottomright->raise();
			return widget ? widget->heightValue() : rpl::single(0);
		}) | rpl::flatten_latest()
	) | rpl::start_with_next([=](QRect list, int addMembers) {
		const auto left = list.x();
		const auto top = list.y() - addMembers;
		const auto right = list.x() + list.width() - topright->width();
		const auto bottom = list.y() + list.height() - bottomleft->height();
		topleft->move(left, top);
		topright->move(right, top);
		bottomleft->move(left, bottom);
		bottomright->move(right, bottom);
	}, lifetime());

	refreshImage();
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		refreshImage();
		topleft->update();
		topright->update();
		bottomleft->update();
		bottomright->update();
	}, lifetime());
}

void Members::peerListSetTitle(rpl::producer<QString> title) {
}

void Members::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

void Members::peerListSetHideEmpty(bool hide) {
}

bool Members::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

void Members::peerListScrollToTop() {
}

int Members::peerListSelectedRowsCount() {
	return 0;
}

void Members::peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Calls::Members.");
}

void Members::peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) {
	Unexpected("Item selection in Calls::Members.");
}

void Members::peerListFinishSelectedRowsBunch() {
}

void Members::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

} // namespace Calls::Group
