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
#include "main/main_account.h" // account().appConfig().
#include "main/main_app_config.h" // appConfig().get<double>().
#include "boxes/peers/edit_participants_box.h" // SubscribeToMigration.
#include "window/window_controller.h" // Controller::sessionController.
#include "window/window_session_controller.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_calls.h"

namespace Calls::Group {
namespace {

constexpr auto kKeepRaisedHandStatusDuration = 3 * crl::time(1000);
constexpr auto kUserpicSizeForBlur = 40;
constexpr auto kUserpicBlurRadius = 8;

using Row = MembersRow;

void SetupVideoPlaceholder(
		not_null<Ui::RpWidget*> widget,
		not_null<PeerData*> chat) {
	struct State {
		QImage blurred;
		QImage rounded;
		InMemoryKey key = {};
		std::shared_ptr<Data::CloudImageView> view;
		qint64 blurredCacheKey = 0;
	};
	const auto state = widget->lifetime().make_state<State>();
	const auto refreshBlurred = [=] {
		const auto key = chat->userpicUniqueKey(state->view);
		if (state->key == key && !state->blurred.isNull()) {
			return;
		}
		constexpr auto size = kUserpicSizeForBlur;
		state->key = key;
		state->blurred = QImage(
			QSize(size, size),
			QImage::Format_ARGB32_Premultiplied);
		{
			auto p = Painter(&state->blurred);
			auto hq = PainterHighQualityEnabler(p);
			chat->paintUserpicSquare(p, state->view, 0, 0, size);
		}
		state->blurred = Images::BlurLargeImage(
			std::move(state->blurred),
			kUserpicBlurRadius);
		widget->update();
	};
	const auto refreshRounded = [=](QSize size) {
		refreshBlurred();
		const auto key = state->blurred.cacheKey();
		if (state->rounded.size() == size && state->blurredCacheKey == key) {
			return;
		}
		state->blurredCacheKey = key;
		state->rounded = Images::prepare(
			state->blurred,
			size.width(),
			size.width(), // Square
			Images::Option::Smooth,
			size.width(),
			size.height());
		{
			auto p = QPainter(&state->rounded);
			p.fillRect(
				0,
				0,
				size.width(),
				size.height(),
				QColor(0, 0, 0, Viewport::kShadowMaxAlpha));
		}
		state->rounded = Images::prepare(
			std::move(state->rounded),
			size.width(),
			size.height(),
			(Images::Option::RoundedLarge | Images::Option::RoundedAll),
			size.width(),
			size.height());
	};
	chat->loadUserpic();
	refreshBlurred();

	widget->paintRequest(
	) | rpl::start_with_next([=] {
		const auto size = QSize(
			widget->width(),
			widget->height() - st::groupCallVideoSmallSkip);
		refreshRounded(size * cIntRetinaFactor());

		auto p = QPainter(widget);
		const auto inner = QRect(QPoint(), size);
		p.drawImage(inner, state->rounded);
		st::groupCallPaused.paint(
			p,
			(size.width() - st::groupCallPaused.width()) / 2,
			st::groupCallVideoPlaceholderIconTop,
			size.width());

		const auto skip = st::groupCallVideoLargeSkip;
		const auto limit = chat->session().account().appConfig().get<double>(
			"groupcall_video_participants_max",
			30.);
		p.setPen(st::groupCallVideoTextFg);
		const auto text = QRect(
			skip,
			st::groupCallVideoPlaceholderTextTop,
			(size.width() - 2 * skip),
			size.height() - st::groupCallVideoPlaceholderTextTop);
		p.setFont(st::semiboldFont);
		p.drawText(
			text,
			tr::lng_group_call_limit(tr::now, lt_count, int(limit)),
			style::al_top);
	}, widget->lifetime());
}

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
	void rowShowContextMenu(not_null<PeerListRow*> row) override;

private:
	[[nodiscard]] std::unique_ptr<Row> createRowForMe();
	[[nodiscard]] std::unique_ptr<Row> createRow(
		const Data::GroupCallParticipant &participant);
	[[nodiscard]] std::unique_ptr<Row> createInvitedRow(
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
		const Data::GroupCallParticipant *participant);
	void removeRow(not_null<Row*> row);
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

	void appendInvitedUsers();
	void scheduleRaisedHandStatusRemove();

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
		const std::optional<Data::GroupCallParticipant> &was,
		const Data::GroupCallParticipant *participant) {
	const auto wasSounding = row->sounding();
	const auto wasSsrc = was ? was->ssrc : 0;
	const auto wasAdditionalSsrc = was
		? GetAdditionalAudioSsrc(was->videoParams)
		: 0;
	row->setSkipLevelUpdate(_skipRowLevelUpdate);
	row->updateState(participant);
	const auto nowSounding = row->sounding();
	const auto nowSsrc = participant ? participant->ssrc : 0;
	const auto nowAdditionalSsrc = participant
		? GetAdditionalAudioSsrc(participant->videoParams)
		: 0;

	const auto wasNoSounding = _soundingRowBySsrc.empty();

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
	if (wasAdditionalSsrc == nowAdditionalSsrc) {
		if (nowAdditionalSsrc && nowSounding != wasSounding) {
			if (nowSounding) {
				_soundingRowBySsrc.emplace(nowAdditionalSsrc, row);
			} else {
				_soundingRowBySsrc.remove(nowAdditionalSsrc);
			}
		}
	} else {
		_soundingRowBySsrc.remove(wasAdditionalSsrc);
		if (nowSounding && nowAdditionalSsrc) {
			_soundingRowBySsrc.emplace(nowAdditionalSsrc, row);
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
	// There may be 0, 1 or 2 entries for a row.
	for (auto i = begin(_soundingRowBySsrc); i != end(_soundingRowBySsrc);) {
		if (i->second == row) {
			i = _soundingRowBySsrc.erase(i);
		} else {
			++i;
		}
	}
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
	if (_mode == PanelMode::Wide
		&& state.style == MembersRowStyle::Default) {
		return;
	}
	const auto narrow = (state.style == MembersRowStyle::Narrow);
	if (!narrow && state.invited) {
		st::groupCallMemberInvited.paintInCenter(
			p,
			QRect(
				rect.topLeft() + st::groupCallMemberInvitedPosition,
				st::groupCallMemberInvited.size()));
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
		for (const auto peer : base::take(_menuCheckRowsAfterHidden)) {
			if (const auto row = findRow(peer)) {
				checkRowPosition(row);
			}
		}
		_menu = std::move(saved);
	};
	delegate()->peerListShowRowMenu(row, highlightRow, cleanup);
}

void Members::Controller::rowActionClicked(
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
	const auto muted = (muteState == Row::State::Muted)
		|| (muteState == Row::State::RaisedHand);
	const auto addVolumeItem = !muted || isMe(participantPeer);
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

	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		(addVolumeItem
			? st::groupCallPopupMenuWithVolume
			: st::groupCallPopupMenu));
	const auto weakMenu = Ui::MakeWeak(result.get());
	const auto performOnMainWindow = [=](auto callback) {
		if (const auto window = getWindow()) {
			if (const auto menu = weakMenu.data()) {
				menu->discardParentReActivate();

				// We must hide PopupMenu before we activate the MainWindow,
				// otherwise we set focus in field inside MainWindow and then
				// PopupMenu::hide activates back the group call panel :(
				delete weakMenu;
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

		if (participant
			&& (!isMe(participantPeer) || _peer->canManageGroupCall())
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
	const auto muteUnmuteString = [=](bool muted, bool mutedByMe) {
		return (muted && _peer->canManageGroupCall())
			? tr::lng_group_call_context_unmute(tr::now)
			: mutedByMe
			? tr::lng_group_call_context_unmute_for_me(tr::now)
			: _peer->canManageGroupCall()
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

	const auto addVolumeItem = !muted || isMe(participantPeer);
	if (addVolumeItem) {
		auto otherParticipantStateValue
			= _call->otherParticipantStateValue(
		) | rpl::filter([=](const Group::ParticipantState &data) {
			return data.peer == participantPeer;
		});

		auto volumeItem = base::make_unique_q<MenuVolumeItem>(
			menu->menu(),
			st::groupCallPopupVolumeMenu,
			otherParticipantStateValue,
			row->volume(),
			Group::kMaxVolume,
			muted);

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

		if (!menu->empty()) {
			menu->addSeparator();
		}

		menu->addAction(std::move(volumeItem));

		if (!isMe(participantPeer)) {
			menu->addSeparator();
		}
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
		not_null<PeerData*> participantPeer) {
	if (findRow(participantPeer)) {
		return nullptr;
	}
	auto result = std::make_unique<Row>(this, participantPeer);
	updateRow(result.get(), std::nullopt, nullptr);
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
, _videoWrap(_layout->add(object_ptr<Ui::RpWidget>(_layout.get())))
, _videoPlaceholder(std::make_unique<Ui::RpWidget>(_videoWrap.get()))
, _viewport(
	std::make_unique<Viewport>(
		_videoWrap.get(),
		PanelMode::Default,
		backend)) {
	setupList();
	setupAddMember(call);
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
				ChannelDataFlag::Username);
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
		auto addMember = Settings::CreateButton(
			_layout.get(),
			tr::lng_group_call_invite(),
			st::groupCallAddMember,
			&st::groupCallAddMemberIcon,
			st::groupCallAddMemberIconLeft,
			&st::groupCallMemberInactiveIcon);
		addMember->clicks(
		) | rpl::to_empty | rpl::start_to_stream(
			_addMemberRequests,
			addMember->lifetime());
		addMember->show();
		addMember->resizeToWidth(_layout->width());
		delete _addMemberButton.current();
		_addMemberButton = addMember.data();
		_layout->insert(3, std::move(addMember));
	}, lifetime());

	updateControlsGeometry();
}

Row *Members::lookupRow(not_null<PeerData*> peer) const {
	return _listController->findRow(peer);
}

void Members::setMode(PanelMode mode) {
	if (_mode.current() == mode) {
		return;
	}
	_mode = mode;
	_listController->setMode(mode);
}

QRect Members::getInnerGeometry() const {
	const auto addMembers = _addMemberButton.current();
	const auto add = addMembers ? addMembers->height() : 0;
	return QRect(
		0,
		-_scroll->scrollTop(),
		width(),
		_list->y() + _list->height() + _bottomSkip->height() + add);
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

	rpl::combine(
		_layout->widthValue(),
		_call->hasNotShownVideoValue()
	) | rpl::start_with_next([=](int width, bool has) {
		const auto height = has ? st::groupCallVideoPlaceholderHeight : 0;
		_videoPlaceholder->setGeometry(0, 0, width, height);
	}, _videoPlaceholder->lifetime());

	SetupVideoPlaceholder(_videoPlaceholder.get(), _call->peer());

	rpl::combine(
		_videoPlaceholder->heightValue(),
		_viewport->fullHeightValue()
	) | rpl::start_with_next([=](int placeholder, int viewport) {
		_videoWrap->resize(
			_videoWrap->width(),
			std::max(placeholder, viewport));
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
		const auto top = list.y() - _topSkip->height();
		const auto right = left + list.width() - topright->width();
		const auto bottom = top
			+ _topSkip->height()
			+ list.height()
			+ _bottomSkip->height()
			+ addMembers
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

} // namespace Calls::Group
