/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_members.h"

#include "calls/group/calls_cover_item.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_menu.h"
#include "calls/group/calls_volume_item.h"
#include "calls/group/calls_group_members_row.h"
#include "calls/group/calls_group_viewport.h"
#include "calls/calls_emoji_fingerprint.h"
#include "calls/calls_instance.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_peer.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "data/data_peer_values.h" // Data::CanWriteValue.
#include "data/data_session.h" // Data::Session::invitedToCallUsers.
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/cross_line.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "core/application.h" // Core::App().domain, .activeWindow.
#include "main/main_domain.h" // Core::App().domain().activate.
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_values.h" // Info::Profile::NameValue.
#include "boxes/peers/edit_participants_box.h" // SubscribeToMigration.
#include "boxes/peers/prepare_short_info_box.h" // PrepareShortInfo...
#include "window/window_controller.h" // Controller::sessionController.
#include "window/window_session_controller.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_calls.h"

namespace Calls::Group {
namespace {

constexpr auto kKeepRaisedHandStatusDuration = 3 * crl::time(1000);

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
	void rowRightActionClicked(not_null<PeerListRow*> row) override;
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
		QPainter &p,
		QRect rect,
		const IconState &state) override;
	int rowPaintStatusIcon(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		not_null<MembersRow*> row,
		const IconState &state) override;
	bool rowIsNarrow() override;
	void rowShowContextMenu(not_null<PeerListRow*> row) override;

private:
	[[nodiscard]] std::unique_ptr<Row> createRowForMe();
	[[nodiscard]] std::unique_ptr<Row> createRow(
		const Data::GroupCallParticipant &participant);
	[[nodiscard]] std::unique_ptr<Row> createInvitedRow(
		not_null<PeerData*> participantPeer,
		bool calling);
	[[nodiscard]] std::unique_ptr<Row> createWithAccessRow(
		not_null<PeerData*> participantPeer);

	[[nodiscard]] bool isMe(not_null<PeerData*> participantPeer) const;
	void prepareRows(not_null<Data::GroupCall*> real);

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
		const std::optional<Data::GroupCallParticipant> &was,
		const Data::GroupCallParticipant *participant,
		Row::State noParticipantState = Row::State::Invited);
	void updateRowInSoundingMap(
		not_null<Row*> row,
		bool wasSounding,
		uint32 wasSsrc,
		uint32 wasAdditionalSsrc,
		const Data::GroupCallParticipant *participant);
	void updateRowInSoundingMap(
		not_null<Row*> row,
		bool wasSounding,
		uint32 wasSsrc,
		bool nowSounding,
		uint32 nowSsrc);
	void removeRow(not_null<Row*> row);
	void removeRowFromSoundingMap(not_null<Row*> row);
	void updateRowLevel(not_null<Row*> row, float level);
	void checkRowPosition(not_null<Row*> row);
	[[nodiscard]] bool needToReorder(not_null<Row*> row) const;
	[[nodiscard]] bool allRowsAboveAreSpeaking(not_null<Row*> row) const;
	[[nodiscard]] bool allRowsAboveMoreImportantThanHand(
		not_null<Row*> row,
		uint64 raiseHandRating) const;
	[[nodiscard]] const Data::GroupCallParticipant *findParticipant(
		const std::string &endpoint) const;
	[[nodiscard]] const std::string &computeScreenEndpoint(
		not_null<const Data::GroupCallParticipant*> participant) const;
	[[nodiscard]] const std::string &computeCameraEndpoint(
		not_null<const Data::GroupCallParticipant*> participant) const;
	void showRowMenu(not_null<PeerListRow*> row, bool highlightRow);

	void toggleVideoEndpointActive(
		const VideoEndpoint &endpoint,
		bool active);

	void partitionRows();
	void setupInvitedUsers();
	[[nodiscard]] bool appendInvitedUsers();
	void setupWithAccessUsers();
	[[nodiscard]] bool appendWithAccessUsers();
	void scheduleRaisedHandStatusRemove();
	void refreshWithAccessRows(base::flat_set<UserId> &&nowIds);

	void hideRowsWithVideoExcept(const VideoEndpoint &large);
	void showAllHiddenRows();
	void hideRowWithVideo(const VideoEndpoint &endpoint);
	void showRowWithVideo(const VideoEndpoint &endpoint);

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
	Ui::CrossLineAnimation _videoCrossLine;
	Ui::RoundRect _narrowRoundRectSelected;
	Ui::RoundRect _narrowRoundRect;
	QImage _narrowShadow;

	base::flat_set<UserId> _withAccess;

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
, _videoCrossLine(st::groupCallVideoCrossLine)
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
	}, _lifetime);

	rpl::combine(
		PowerSaving::OnValue(PowerSaving::kCalls),
		Core::App().appDeactivatedValue()
	) | rpl::start_with_next([=](bool disabled, bool deactivated) {
		const auto hide = disabled || deactivated;

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

	_call->videoEndpointLargeValue(
	) | rpl::start_with_next([=](const VideoEndpoint &large) {
		if (large) {
			hideRowsWithVideoExcept(large);
		} else {
			showAllHiddenRows();
		}
	}, _lifetime);

	_call->videoStreamShownUpdates(
	) | rpl::filter([=](const VideoStateToggle &update) {
		const auto &large = _call->videoEndpointLarge();
		return large && (update.endpoint != large);
	}) | rpl::start_with_next([=](const VideoStateToggle &update) {
		if (update.value) {
			hideRowWithVideo(update.endpoint);
		} else {
			showRowWithVideo(update.endpoint);
		}
	}, _lifetime);

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

void Members::Controller::hideRowsWithVideoExcept(
		const VideoEndpoint &large) {
	auto changed = false;
	auto showLargeRow = true;
	for (const auto &endpoint : _call->shownVideoTracks()) {
		if (endpoint != large) {
			if (const auto row = findRow(endpoint.peer)) {
				if (endpoint.peer == large.peer) {
					showLargeRow = false;
				}
				delegate()->peerListSetRowHidden(row, true);
				changed = true;
			}
		}
	}
	if (const auto row = showLargeRow ? findRow(large.peer) : nullptr) {
		delegate()->peerListSetRowHidden(row, false);
		changed = true;
	}
	if (changed) {
		delegate()->peerListRefreshRows();
	}
}

void Members::Controller::showAllHiddenRows() {
	auto shown = false;
	for (const auto &endpoint : _call->shownVideoTracks()) {
		if (const auto row = findRow(endpoint.peer)) {
			delegate()->peerListSetRowHidden(row, false);
			shown = true;
		}
	}
	if (shown) {
		delegate()->peerListRefreshRows();
	}
}

void Members::Controller::hideRowWithVideo(const VideoEndpoint &endpoint) {
	if (const auto row = findRow(endpoint.peer)) {
		delegate()->peerListSetRowHidden(row, true);
		delegate()->peerListRefreshRows();
	}
}

void Members::Controller::showRowWithVideo(const VideoEndpoint &endpoint) {
	const auto peer = endpoint.peer;
	const auto &large = _call->videoEndpointLarge();
	if (large) {
		for (const auto &endpoint : _call->shownVideoTracks()) {
			if (endpoint != large && endpoint.peer == peer) {
				// Still hidden with another video.
				return;
			}
		}
	}
	if (const auto row = findRow(endpoint.peer)) {
		delegate()->peerListSetRowHidden(row, false);
		delegate()->peerListRefreshRows();
	}
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
				if (isMe(participantPeer)) {
					updateRow(row, update.was, nullptr);
				} else if (_withAccess.contains(peerToUser(participantPeer->id))) {
					updateRow(row, update.was, nullptr, Row::State::WithAccess);
					partitionRows();
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
	) | rpl::start_with_next([=](const VideoStateToggle &update) {
		toggleVideoEndpointActive(update.endpoint, update.value);
	}, _lifetime);
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

bool Members::Controller::appendInvitedUsers() {
	auto changed = false;
	if (const auto id = _call->id()) {
		const auto &invited = _peer->owner().invitedToCallUsers(id);
		for (const auto &[user, calling] : invited) {
			if (auto row = createInvitedRow(user, calling)) {
				delegate()->peerListAppendRow(std::move(row));
				changed = true;
			}
		}
	}
	return changed;
}

void Members::Controller::setupInvitedUsers() {
	if (appendInvitedUsers()) {
		delegate()->peerListRefreshRows();
	}

	using Invite = Data::Session::InviteToCall;
	_peer->owner().invitesToCalls(
	) | rpl::filter([=](const Invite &invite) {
		return (invite.id == _call->id());
	}) | rpl::start_with_next([=](const Invite &invite) {
		const auto user = invite.user;
		if (invite.removed) {
			if (const auto row = findRow(user)) {
				if (row->state() == Row::State::Invited
					|| row->state() == Row::State::Calling) {
					delegate()->peerListRemoveRow(row);
					delegate()->peerListRefreshRows();
				}
			}
		} else if (auto row = createInvitedRow(user, invite.calling)) {
			delegate()->peerListAppendRow(std::move(row));
			delegate()->peerListRefreshRows();
		}
	}, _lifetime);
}

bool Members::Controller::appendWithAccessUsers() {
	auto changed = false;
	for (const auto id : _withAccess) {
		if (auto row = createWithAccessRow(_peer->owner().user(id))) {
			changed = true;
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	return changed;
}

void Members::Controller::setupWithAccessUsers() {
	const auto conference = _call->conferenceCall().get();
	if (!conference) {
		return;
	}
	conference->participantsWithAccessValue(
	) | rpl::start_with_next([=](base::flat_set<UserId> &&nowIds) {
		for (auto i = begin(_withAccess); i != end(_withAccess);) {
			const auto oldId = *i;
			if (nowIds.remove(oldId)) {
				++i;
				continue;
			}
			const auto user = _peer->owner().user(oldId);
			if (const auto row = findRow(user)) {
				if (row->state() == Row::State::WithAccess) {
					removeRow(row);
				}
			}
			i = _withAccess.erase(i);
		}
		auto partition = false;
		auto partitionChecked = false;
		for (const auto nowId : nowIds) {
			const auto user = _peer->owner().user(nowId);
			if (!findRow(user)) {
				if (auto row = createWithAccessRow(user)) {
					if (!partitionChecked) {
						partitionChecked = true;
						if (const auto count = delegate()->peerListFullRowsCount()) {
							const auto last = delegate()->peerListRowAt(count - 1);
							const auto state = static_cast<Row*>(last.get())->state();
							if (state == Row::State::Invited
								|| state == Row::State::Calling) {
								partition = true;
							}
						}
					}
					delegate()->peerListAppendRow(std::move(row));
				}
			}
			_withAccess.emplace(nowId);
		}
		if (partition) {
			delegate()->peerListPartitionRows([](const PeerListRow &row) {
				const auto state = static_cast<const Row&>(row).state();
				return (state != Row::State::Invited)
					&& (state != Row::State::Calling);
			});
		}
		delegate()->peerListRefreshRows();
	}, _lifetime);
}

void Members::Controller::updateRow(
		const std::optional<Data::GroupCallParticipant> &was,
		const Data::GroupCallParticipant &now) {
	auto reorderIfNonRealBefore = 0;
	auto checkPosition = (Row*)nullptr;
	auto addedToBottom = (Row*)nullptr;
	if (const auto row = findRow(now.peer)) {
		if (row->state() == Row::State::Invited
			|| row->state() == Row::State::Calling
			|| row->state() == Row::State::WithAccess) {
			reorderIfNonRealBefore = row->absoluteIndex();
		}
		updateRow(row, was, &now);
		if ((now.speaking && (!was || !was->speaking))
			|| (now.raisedHandRating != (was ? was->raisedHandRating : 0))
			|| (!now.canSelfUnmute && was && was->canSelfUnmute)) {
			checkPosition = row;
		}
	} else if (auto row = createRow(now)) {
		if (row->speaking()) {
			delegate()->peerListPrependRow(std::move(row));
		} else {
			reorderIfNonRealBefore = delegate()->peerListFullRowsCount();
			if (now.raisedHandRating != 0) {
				checkPosition = row.get();
			} else {
				addedToBottom = row.get();
			}
			delegate()->peerListAppendRow(std::move(row));
		}
		delegate()->peerListRefreshRows();
	}
	const auto reorder = [&] {
		const auto count = reorderIfNonRealBefore;
		if (count <= 0) {
			return false;
		}
		const auto row = delegate()->peerListRowAt(
			reorderIfNonRealBefore - 1).get();
		using State = Row::State;
		const auto state = static_cast<Row*>(row)->state();
		return (state == State::Invited)
			|| (state == State::Calling)
			|| (state == State::WithAccess);
	}();
	if (reorder) {
		partitionRows();
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

void Members::Controller::partitionRows() {
	auto hadWithAccess = false;
	delegate()->peerListPartitionRows([&](const PeerListRow &row) {
		using State = Row::State;
		const auto state = static_cast<const Row&>(row).state();
		if (state == State::WithAccess) {
			hadWithAccess = true;
		}
		return (state != State::Invited)
			&& (state != State::Calling)
			&& (state != State::WithAccess);
	});
	if (hadWithAccess) {
		delegate()->peerListPartitionRows([](const PeerListRow &row) {
			const auto state = static_cast<const Row&>(row).state();
			return (state != Row::State::Invited)
				&& (state != Row::State::Calling);
		});
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
	} else if (!_call->canManage()) {
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
	delegate()->peerListSortRows(_call->canManage()
		? makeComparator(projForAdmin)
		: makeComparator(projForOther));
}

void Members::Controller::updateRow(
		not_null<Row*> row,
		const std::optional<Data::GroupCallParticipant> &was,
		const Data::GroupCallParticipant *participant,
		Row::State noParticipantState) {
	const auto wasSounding = row->sounding();
	const auto wasSsrc = was ? was->ssrc : 0;
	const auto wasAdditionalSsrc = was
		? GetAdditionalAudioSsrc(was->videoParams)
		: 0;
	row->setSkipLevelUpdate(_skipRowLevelUpdate);
	if (participant) {
		row->updateState(*participant);
	} else if (noParticipantState == Row::State::WithAccess) {
		row->updateStateWithAccess();
	} else {
		row->updateStateInvited(noParticipantState == Row::State::Calling);
	}

	const auto wasNoSounding = _soundingRowBySsrc.empty();
	updateRowInSoundingMap(
		row,
		wasSounding,
		wasSsrc,
		wasAdditionalSsrc,
		participant);
	const auto nowNoSounding = _soundingRowBySsrc.empty();
	if (wasNoSounding && !nowNoSounding) {
		_soundingAnimation.start();
	} else if (nowNoSounding && !wasNoSounding) {
		_soundingAnimation.stop();
	}

	delegate()->peerListUpdateRow(row);
}

void Members::Controller::updateRowInSoundingMap(
		not_null<Row*> row,
		bool wasSounding,
		uint32 wasSsrc,
		uint32 wasAdditionalSsrc,
		const Data::GroupCallParticipant *participant) {
	const auto nowSounding = row->sounding();
	const auto nowSsrc = participant ? participant->ssrc : 0;
	const auto nowAdditionalSsrc = participant
		? GetAdditionalAudioSsrc(participant->videoParams)
		: 0;
	updateRowInSoundingMap(row, wasSounding, wasSsrc, nowSounding, nowSsrc);
	updateRowInSoundingMap(
		row,
		wasSounding,
		wasAdditionalSsrc,
		nowSounding,
		nowAdditionalSsrc);
}

void Members::Controller::updateRowInSoundingMap(
		not_null<Row*> row,
		bool wasSounding,
		uint32 wasSsrc,
		bool nowSounding,
		uint32 nowSsrc) {
	if (wasSsrc == nowSsrc) {
		if (nowSsrc && nowSounding != wasSounding) {
			if (nowSounding) {
				_soundingRowBySsrc.emplace(nowSsrc, row);
			} else {
				_soundingRowBySsrc.remove(nowSsrc);
			}
		}
	} else {
		_soundingRowBySsrc.remove(wasSsrc);
		if (nowSounding && nowSsrc) {
			_soundingRowBySsrc.emplace(nowSsrc, row);
		}
	}
}

void Members::Controller::removeRow(not_null<Row*> row) {
	removeRowFromSoundingMap(row);
	delegate()->peerListRemoveRow(row);
}

void Members::Controller::removeRowFromSoundingMap(not_null<Row*> row) {
	// There may be 0, 1 or 2 entries for a row.
	for (auto i = begin(_soundingRowBySsrc); i != end(_soundingRowBySsrc);) {
		if (i->second == row) {
			i = _soundingRowBySsrc.erase(i);
		} else {
			++i;
		}
	}
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
	setDescription(nullptr);
	setSearchNoResults(nullptr);

	if (const auto real = _call->lookupReal()) {
		prepareRows(real);
	} else if (auto row = createRowForMe()) {
		delegate()->peerListAppendRow(std::move(row));
		delegate()->peerListRefreshRows();
	}

	loadMoreRows();
	setupWithAccessUsers();
	setupInvitedUsers();
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
		const auto row = static_cast<Row*>(
			delegate()->peerListRowAt(i).get());
		removeRowFromSoundingMap(row);
		const auto participantPeer = row->peer();
		const auto me = isMe(participantPeer);
		if (me) {
			foundMe = true;
		}
		if (const auto found = real->participantByPeer(participantPeer)) {
			updateRowInSoundingMap(row, false, 0, 0, found);
			++i;
		} else if (me) {
			++i;
		} else {
			changed = true;
			removeRow(row);
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
	if (appendWithAccessUsers()) {
		changed = true;
	}
	if (appendInvitedUsers()) {
		changed = true;
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
	return _call->canManage();
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
		QPainter &p,
		QRect rect,
		const IconState &state) {
	if (_mode == PanelMode::Wide
		&& state.style == MembersRowStyle::Default) {
		return;
	}
	const auto narrow = (state.style == MembersRowStyle::Narrow);
	if (state.invited || state.calling) {
		if (narrow) {
			(state.invited
				? st::groupCallNarrowInvitedIcon
				: st::groupCallNarrowCallingIcon).paintInCenter(p, rect);
		} else {
			const auto &icon = state.invited
				? st::groupCallMemberInvited
				: st::groupCallMemberCalling;
			const auto shift = state.invited
				? st::groupCallMemberInvitedPosition
				: st::groupCallMemberCallingPosition;
			icon.paintInCenter(
				p,
				QRect(rect.topLeft() + shift, icon.size()));
		}
		return;
	}
	const auto video = (state.style == MembersRowStyle::Video);
	const auto &greenIcon = video
		? st::groupCallVideoCrossLine.icon
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
			const auto &grayIcon = video
				? st::groupCallVideoCrossLine.icon
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
				auto &line = video
					? _videoCrossLine
					: narrow
					? _coloredNarrowCrossLine
					: _coloredCrossLine;
				const auto color = video
					? std::nullopt
					: std::make_optional(st::groupCallMemberMutedIcon->c);
				line.paint(
					p,
					left,
					top,
					1.,
					color);
				return;
			} else if (state.muted == 0.) {
				// Gray crossed icon, no coloring, cached as last frame.
				auto &line = video
					? _videoCrossLine
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
		st::groupCallMemberMutedIcon,
		state.muted);
	const auto color = video
		? std::nullopt
		: std::make_optional((narrow && state.mutedByMe)
			? st::groupCallMemberMutedIcon->c
			: (narrow && state.raisedHand)
			? st::groupCallMemberInactiveStatus->c
			: iconColor);

	// Don't use caching of the last frame,
	// because 'muted' may animate color.
	const auto crossProgress = std::min(1. - state.active, 0.9999);
	auto &line = video
		? _videoCrossLine
		: narrow
		? _inactiveNarrowCrossLine
		: _inactiveCrossLine;
	line.paint(p, left, top, crossProgress, color);
}

int Members::Controller::rowPaintStatusIcon(
		QPainter &p,
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

void Members::Controller::rowShowContextMenu(not_null<PeerListRow*> row) {
	showRowMenu(row, false);
}

auto Members::Controller::kickParticipantRequests() const
-> rpl::producer<not_null<PeerData*>>{
	return _kickParticipantRequests.events();
}

void Members::Controller::rowClicked(not_null<PeerListRow*> row) {
	showRowMenu(row, true);
}

void Members::Controller::showRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow) {
	const auto cleanup = [=](not_null<Ui::PopupMenu*> menu) {
		if (!_menu || _menu.get() != menu) {
			return;
		}
		auto saved = base::take(_menu);
		for (const auto &peer : base::take(_menuCheckRowsAfterHidden)) {
			if (const auto row = findRow(peer)) {
				checkRowPosition(row);
			}
		}
		_menu = std::move(saved);
	};
	delegate()->peerListShowRowMenu(row, highlightRow, cleanup);
}

void Members::Controller::rowRightActionClicked(
		not_null<PeerListRow*> row) {
	showRowMenu(row, true);
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
	const auto muteState = real->state();
	if (muteState == Row::State::WithAccess) {
		return nullptr;
	}
	const auto muted = (muteState == Row::State::Muted)
		|| (muteState == Row::State::RaisedHand);
	const auto addCover = !_call->rtmp();
	const auto addVolumeItem = (!muted || isMe(participantPeer));
	const auto admin = IsGroupCallAdmin(_peer, participantPeer);
	const auto session = &_peer->session();
	const auto account = &session->account();

	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		(addCover
			? st::groupCallPopupMenuWithCover
			: addVolumeItem
			? st::groupCallPopupMenuWithVolume
			: st::groupCallPopupMenu));
	const auto weakMenu = base::make_weak(result.get());
	const auto withActiveWindow = [=](auto callback) {
		if (const auto window = Core::App().activePrimaryWindow()) {
			if (const auto menu = weakMenu.get()) {
				menu->discardParentReActivate();

				// We must hide PopupMenu before we activate the MainWindow,
				// otherwise we set focus in field inside MainWindow and then
				// PopupMenu::hide activates back the group call panel :(
				delete weakMenu.get();
			}
			window->invokeForSessionController(
				account,
				participantPeer,
				[&](not_null<::Window::SessionController*> newController) {
					callback(newController);
					newController->widget()->activate();
				});
		}
	};
	const auto showProfile = [=] {
		withActiveWindow([=](not_null<::Window::SessionController*> window) {
			window->showPeerInfo(participantPeer);
		});
	};
	const auto showHistory = [=] {
		withActiveWindow([=](not_null<::Window::SessionController*> window) {
			window->showPeerHistory(
				participantPeer,
				::Window::SectionShow::Way::Forward);
		});
	};
	const auto removeFromVoiceChat = crl::guard(this, [=] {
		_kickParticipantRequests.fire_copy(participantPeer);
	});

	if (addCover) {
		result->addAction(base::make_unique_q<CoverItem>(
			result->menu(),
			st::groupCallPopupCoverMenu,
			st::groupCallMenuCover,
			Info::Profile::NameValue(participantPeer),
			PrepareShortInfoStatus(participantPeer),
			PrepareShortInfoUserpic(
				participantPeer,
				st::groupCallMenuCover)));

		if (const auto about = participantPeer->about(); !about.isEmpty()) {
			result->addAction(base::make_unique_q<AboutItem>(
				result->menu(),
				st::groupCallPopupCoverMenu,
				Info::Profile::AboutWithEntities(participantPeer, about)));
		}
	}

	if (const auto real = _call->lookupReal()) {
		auto oneFound = false;
		auto hasTwoOrMore = false;
		const auto &shown = _call->shownVideoTracks();
		for (const auto &[endpoint, track] : _call->activeVideoTracks()) {
			if (shown.contains(endpoint)) {
				if (oneFound) {
					hasTwoOrMore = true;
					break;
				}
				oneFound = true;
			}
		}
		const auto participant = real->participantByPeer(participantPeer);
		if (participant && hasTwoOrMore) {
			const auto &large = _call->videoEndpointLarge();
			const auto pinned = _call->videoEndpointPinned();
			const auto camera = VideoEndpoint{
				VideoEndpointType::Camera,
				participantPeer,
				computeCameraEndpoint(participant),
			};
			const auto screen = VideoEndpoint{
				VideoEndpointType::Screen,
				participantPeer,
				computeScreenEndpoint(participant),
			};
			if (shown.contains(camera)) {
				if (pinned && large == camera) {
					result->addAction(
						tr::lng_group_call_context_unpin_camera(tr::now),
						[=] { _call->pinVideoEndpoint({}); });
				} else {
					result->addAction(
						tr::lng_group_call_context_pin_camera(tr::now),
						[=] { _call->pinVideoEndpoint(camera); });
				}
			}
			if (shown.contains(screen)) {
				if (pinned && large == screen) {
					result->addAction(
						tr::lng_group_call_context_unpin_screen(tr::now),
						[=] { _call->pinVideoEndpoint({}); });
				} else {
					result->addAction(
						tr::lng_group_call_context_pin_screen(tr::now),
						[=] { _call->pinVideoEndpoint(screen); });
				}
			}
		}

		if (_call->rtmp()) {
			addMuteActionsToContextMenu(
				result,
				row->peer(),
				false,
				static_cast<Row*>(row.get()));
		} else if (participant
			&& (!isMe(participantPeer) || _call->canManage())
			&& (participant->ssrc != 0
				|| GetAdditionalAudioSsrc(participant->videoParams) != 0)) {
			addMuteActionsToContextMenu(
				result,
				participantPeer,
				admin,
				static_cast<Row*>(row.get()));
		}
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
		const auto invited = (muteState == Row::State::Invited)
			|| (muteState == Row::State::Calling);
		const auto conference = _call->conferenceCall().get();
		if (conference
			&& participantPeer->isUser()
			&& invited) {
			const auto id = conference->id();
			const auto cancelInvite = [=](bool discard) {
				Core::App().calls().declineOutgoingConferenceInvite(
					id,
					participantPeer->asUser(),
					discard);
			};
			if (muteState == Row::State::Calling) {
				result->addAction(
					tr::lng_group_call_context_stop_ringing(tr::now),
					[=] { cancelInvite(false); });
			}
			result->addAction(
				tr::lng_group_call_context_cancel_invite(tr::now),
				[=] { cancelInvite(true); });
			result->addSeparator();
		}
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
			if (muteState == Row::State::Invited
				|| muteState == Row::State::Calling
				|| muteState == Row::State::WithAccess) {
				return false;
			} else if (conference && _call->canManage()) {
				return true;
			} else if (const auto chat = _peer->asChat()) {
				return chat->amCreator()
					|| (user
						&& chat->canBanMembers()
						&& !chat->admins.contains(user));
			} else if (const auto channel = _peer->asChannel()) {
				return !participantPeer->isMegagroup() // That's the creator.
					&& channel->canRestrictParticipant(participantPeer);
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
	if (result->actions().size() < (addCover ? 2 : 1)) {
		return nullptr;
	}
	return result;
}

void Members::Controller::addMuteActionsToContextMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> participantPeer,
		bool participantIsCallAdmin,
		not_null<Row*> row) {
	const auto muteUnmuteString = [=](bool muted, bool mutedByMe) {
		return (muted && _call->canManage())
			? tr::lng_group_call_context_unmute(tr::now)
			: mutedByMe
			? tr::lng_group_call_context_unmute_for_me(tr::now)
			: _call->canManage()
			? tr::lng_group_call_context_mute(tr::now)
			: tr::lng_group_call_context_mute_for_me(tr::now);
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
	const auto muted = (muteState == Row::State::Muted)
		|| (muteState == Row::State::RaisedHand);
	const auto mutedByMe = row->mutedByMe();

	auto mutesFromVolume = rpl::never<bool>() | rpl::type_erased();

	const auto addVolumeItem = (!muted || isMe(participantPeer));
	if (addVolumeItem) {
		auto otherParticipantStateValue
			= _call->otherParticipantStateValue(
		) | rpl::filter([=](const Group::ParticipantState &data) {
			return data.peer == participantPeer;
		});

		auto volumeItem = base::make_unique_q<MenuVolumeItem>(
			menu->menu(),
			st::groupCallPopupVolumeMenu,
			st::groupCallMenuVolumeSlider,
			otherParticipantStateValue,
			_call->rtmp() ? _call->rtmpVolume() : row->volume(),
			Group::kMaxVolume,
			muted,
			st::groupCallMenuVolumePadding);

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

		if (menu->actions().size() > 1) { // First - cover.
			menu->addSeparator();
		}

		menu->addAction(std::move(volumeItem));

		if (!_call->rtmp() && !isMe(participantPeer)) {
			menu->addSeparator();
		}
	};

	const auto muteAction = [&]() -> QAction* {
		if (muteState == Row::State::Invited
			|| muteState == Row::State::Calling
			|| muteState == Row::State::WithAccess
			|| _call->rtmp()
			|| isMe(participantPeer)
			|| (muteState == Row::State::Inactive
				&& participantIsCallAdmin
				&& _call->canManage())) {
			return nullptr;
		}
		auto callback = [=] {
			const auto state = row->state();
			const auto muted = (state == Row::State::Muted)
				|| (state == Row::State::RaisedHand);
			const auto mutedByMe = row->mutedByMe();
			toggleMute(!mutedByMe && (!_call->canManage() || !muted), false);
		};
		return menu->addAction(
			muteUnmuteString(muted, mutedByMe),
			std::move(callback));
	}();

	if (muteAction) {
		std::move(
			mutesFromVolume
		) | rpl::start_with_next([=](bool mutedFromVolume) {
			const auto state = _call->canManage()
				? (mutedFromVolume
					? (row->raisedHandRating()
						? Row::State::RaisedHand
						: Row::State::Muted)
					: Row::State::Inactive)
				: row->state();
			const auto muted = (state == Row::State::Muted)
				|| (state == Row::State::RaisedHand);
			const auto mutedByMe = _call->canManage()
				? false
				: mutedFromVolume;
			muteAction->setText(muteUnmuteString(muted, mutedByMe));
		}, menu->lifetime());
	}
}

std::unique_ptr<Row> Members::Controller::createRowForMe() {
	auto result = std::make_unique<Row>(this, _call->joinAs());
	updateRow(result.get(), std::nullopt, nullptr);
	return result;
}

std::unique_ptr<Row> Members::Controller::createRow(
		const Data::GroupCallParticipant &participant) {
	auto result = std::make_unique<Row>(this, participant.peer);
	updateRow(result.get(), std::nullopt, &participant);
	return result;
}

std::unique_ptr<Row> Members::Controller::createInvitedRow(
		not_null<PeerData*> participantPeer,
		bool calling) {
	if (const auto row = findRow(participantPeer)) {
		if (row->state() == Row::State::Invited
			|| row->state() == Row::State::Calling) {
			row->updateStateInvited(calling);
			delegate()->peerListUpdateRow(row);
		}
		return nullptr;
	}
	const auto state = calling ? Row::State::Calling : Row::State::Invited;
	auto result = std::make_unique<Row>(this, participantPeer);
	updateRow(result.get(), std::nullopt, nullptr, state);
	return result;
}

std::unique_ptr<Row> Members::Controller::createWithAccessRow(
		not_null<PeerData*> participantPeer) {
	if (findRow(participantPeer)) {
		return nullptr;
	}
	auto result = std::make_unique<Row>(this, participantPeer);
	updateRow(result.get(), std::nullopt, nullptr, Row::State::WithAccess);
	return result;
}

Members::Members(
	not_null<QWidget*> parent,
	not_null<GroupCall*> call,
	PanelMode mode,
	Ui::GL::Backend backend)
: RpWidget(parent)
, _call(call)
, _mode(mode)
, _scroll(this)
, _listController(std::make_unique<Controller>(call, parent, mode))
, _layout(_scroll->setOwnedWidget(
	object_ptr<Ui::VerticalLayout>(_scroll.data())))
, _fingerprint(call->conference()
	? _layout->add(object_ptr<Ui::RpWidget>(_layout.get()))
	: nullptr)
, _videoWrap(_layout->add(object_ptr<Ui::RpWidget>(_layout.get())))
, _viewport(
	std::make_unique<Viewport>(
		_videoWrap.get(),
		PanelMode::Default,
		backend)) {
	setupList();
	setupAddMember(call);
	setupFingerprint();
	setContent(_list);
	setupFakeRoundCorners();
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));
	trackViewportGeometry();
}

Members::~Members() {
	_viewport = nullptr;
}

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

not_null<Viewport*> Members::viewport() const {
	return _viewport.get();
}

int Members::desiredHeight() const {
	const auto count = [&] {
		if (const auto real = _call->lookupReal()) {
			return real->fullCount();
		}
		return 0;
	}();
	const auto use = std::max(count, _list->fullRowsCount());
	const auto single = st::groupCallMembersList.item.height;
	const auto desired = (_layout->height() - _list->height())
		+ (use * single)
		+ (use ? st::lineWidth : 0);
	return std::max(height(), desired);
}

rpl::producer<int> Members::desiredHeightValue() const {
	return rpl::combine(
		heightValue(),
		_addMemberButton.value(),
		_shareLinkButton.value(),
		_listController->fullCountValue(),
		_mode.value()
	) | rpl::map([=] {
		return desiredHeight();
	});
}

void Members::setupAddMember(not_null<GroupCall*> call) {
	using namespace rpl::mappers;

	const auto peer = call->peer();
	const auto conference = call->conference();
	const auto canAddByPeer = [=](not_null<PeerData*> peer) {
		if (conference) {
			return rpl::single(true) | rpl::type_erased();
		} else if (peer->isBroadcast()) {
			return rpl::single(false) | rpl::type_erased();
		}
		return rpl::combine(
			Data::CanSendValue(peer, ChatRestriction::SendOther, false),
			_call->joinAsValue()
		) | rpl::map([=](bool can, not_null<PeerData*> joinAs) {
			return can && joinAs->isSelf();
		}) | rpl::type_erased();
	};
	const auto canInviteByLinkByPeer = [=](not_null<PeerData*> peer) {
		if (conference) {
			return rpl::single(true) | rpl::type_erased();
		}
		const auto channel = peer->asChannel();
		if (!channel) {
			return rpl::single(false) | rpl::type_erased();
		}
		return rpl::single(
			false
		) | rpl::then(_call->real(
		) | rpl::map([=] {
			return Data::PeerFlagValue(
				channel,
				ChannelDataFlag::Username);
		}) | rpl::flatten_latest()) | rpl::type_erased();
	};
	_canAddMembers = canAddByPeer(peer);
	_canInviteByLink = canInviteByLinkByPeer(peer);
	SubscribeToMigration(
		peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) {
			_canAddMembers = canAddByPeer(channel);
			_canInviteByLink = canInviteByLinkByPeer(channel);
		});

	const auto baseIndex = _layout->count() - 2;

	rpl::combine(
		_canAddMembers.value(),
		_canInviteByLink.value(),
		_mode.value()
	) | rpl::start_with_next([=](bool add, bool invite, PanelMode mode) {
		if (!add && !invite) {
			if (const auto old = _addMemberButton.current()) {
				delete old;
				_addMemberButton = nullptr;
				updateControlsGeometry();
			}
			if (const auto old = _shareLinkButton.current()) {
				delete old;
				_shareLinkButton = nullptr;
				updateControlsGeometry();
			}
			return;
		}
		auto addMember = Settings::CreateButtonWithIcon(
			_layout.get(),
			(conference
				? tr::lng_group_call_invite_conf()
				: tr::lng_group_call_invite()),
			st::groupCallAddMember,
			{ .icon = &st::groupCallAddMemberIcon });
		addMember->clicks(
		) | rpl::to_empty | rpl::start_to_stream(
			_addMemberRequests,
			addMember->lifetime());
		addMember->show();
		addMember->resizeToWidth(_layout->width());
		delete _addMemberButton.current();
		_addMemberButton = addMember.data();
		_layout->insert(baseIndex, std::move(addMember));
		if (conference) {
			auto shareLink = Settings::CreateButtonWithIcon(
				_layout.get(),
				tr::lng_group_invite_share(),
				st::groupCallAddMember,
				{ .icon = &st::groupCallShareLinkIcon });
			shareLink->clicks() | rpl::to_empty | rpl::start_to_stream(
				_shareLinkRequests,
				shareLink->lifetime());
			shareLink->show();
			shareLink->resizeToWidth(_layout->width());
			delete _shareLinkButton.current();
			_shareLinkButton = shareLink.data();
			_layout->insert(baseIndex + 1, std::move(shareLink));
		}
	}, lifetime());

	updateControlsGeometry();
}

Row *Members::lookupRow(not_null<PeerData*> peer) const {
	return _listController->findRow(peer);
}

not_null<MembersRow*> Members::rtmpFakeRow(not_null<PeerData*> peer) const {
	if (!_rtmpFakeRow) {
		_rtmpFakeRow = std::make_unique<Row>(_listController.get(), peer);
	}
	return _rtmpFakeRow.get();
}

void Members::setMode(PanelMode mode) {
	if (_mode.current() == mode) {
		return;
	}
	_mode = mode;
	_listController->setMode(mode);
}

QRect Members::getInnerGeometry() const {
	const auto shareLink = _shareLinkButton.current();
	const auto addMembers = _addMemberButton.current();
	const auto share = shareLink ? shareLink->height() : 0;
	const auto add = addMembers ? addMembers->height() : 0;
	return QRect(
		0,
		-_scroll->scrollTop(),
		width(),
		_list->y() + _list->height() + _bottomSkip->height() + add + share);
}

rpl::producer<int> Members::fullCountValue() const {
	return _listController->fullCountValue();
}

void Members::setupList() {
	_listController->setStyleOverrides(&st::groupCallMembersList);
	const auto addSkip = [&] {
		const auto result = _layout->add(
			object_ptr<Ui::FixedHeightWidget>(
				_layout.get(),
				st::groupCallMembersTopSkip));
		result->paintRequest(
		) | rpl::start_with_next([=](QRect clip) {
			QPainter(result).fillRect(clip, st::groupCallMembersBg);
		}, result->lifetime());
		return result;
	};
	_topSkip = addSkip();
	_list = _layout->add(
		object_ptr<ListWidget>(
			_layout.get(),
			_listController.get()));
	_bottomSkip = addSkip();

	using namespace rpl::mappers;
	rpl::combine(
		_list->heightValue() | rpl::map(_1 > 0),
		_addMemberButton.value() | rpl::map(_1 != nullptr)
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool hasList, bool hasAddMembers) {
		_topSkip->resize(
			_topSkip->width(),
			hasList ? st::groupCallMembersTopSkip : 0);
		_bottomSkip->resize(
			_bottomSkip->width(),
			(hasList && !hasAddMembers) ? st::groupCallMembersTopSkip : 0);
	}, _list->lifetime());

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
}

void Members::setupFingerprint() {
	if (const auto raw = _fingerprint) {
		auto badge = SetupFingerprintBadge(
			raw->lifetime(),
			_call->emojiHashValue());
		std::move(badge.repaints) | rpl::start_to_stream(
			_fingerprintRepaints,
			raw->lifetime());
		_fingerprintState = badge.state;

		SetupFingerprintBadgeWidget(
			raw,
			_fingerprintState,
			_fingerprintRepaints.events());
	}
}

void Members::trackViewportGeometry() {
	_call->videoEndpointLargeValue(
	) | rpl::start_with_next([=](const VideoEndpoint &large) {
		_viewport->showLarge(large);
	}, _viewport->lifetime());

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
	}, _viewport->lifetime());

	_scroll->heightValue(
	) | rpl::skip(1) | rpl::start_with_next(resize, _viewport->lifetime());

	_scroll->scrollTopValue(
	) | rpl::skip(1) | rpl::start_with_next(move, _viewport->lifetime());

	_viewport->fullHeightValue(
	) | rpl::start_with_next([=](int viewport) {
		_videoWrap->resize(_videoWrap->width(), viewport);
		if (viewport > 0) {
			move();
			resize();
		}
	}, _viewport->lifetime());
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
	const auto imagePartSize = size * style::DevicePixelRatio();
	const auto imageSize = full * style::DevicePixelRatio();
	const auto image = std::make_shared<QImage>(
		QImage(imageSize, imageSize, QImage::Format_ARGB32_Premultiplied));
	image->setDevicePixelRatio(style::DevicePixelRatio());

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

	const auto heightValue = [=](Ui::RpWidget *widget) {
		topleft->raise();
		topright->raise();
		bottomleft->raise();
		bottomright->raise();
		return widget ? widget->heightValue() : rpl::single(0);
	};
	rpl::combine(
		_list->geometryValue(),
		_addMemberButton.value() | rpl::map(
			heightValue
		) | rpl::flatten_latest(),
		_shareLinkButton.value() | rpl::map(
			heightValue
		) | rpl::flatten_latest()
	) | rpl::start_with_next([=](QRect list, int addMembers, int shareLink) {
		const auto left = list.x();
		const auto top = list.y() - _topSkip->height();
		const auto right = left + list.width() - topright->width();
		const auto bottom = top
			+ _topSkip->height()
			+ list.height()
			+ _bottomSkip->height()
			+ addMembers
			+ shareLink
			- bottomleft->height();
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

std::shared_ptr<Main::SessionShow> Members::peerListUiShow() {
	Unexpected("...Members::peerListUiShow");
}

} // namespace Calls::Group
