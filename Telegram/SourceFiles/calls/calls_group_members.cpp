/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_members.h"

#include "calls/calls_group_call.h"
#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "data/data_peer_values.h" // Data::CanWriteValue.
#include "ui/paint/blobs.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/ripple_animation.h"
#include "core/application.h" // Core::App().domain, Core::App().activeWindow.
#include "main/main_domain.h" // Core::App().domain().activate.
#include "main/main_session.h"
#include "base/timer.h"
#include "boxes/peers/edit_participants_box.h"
#include "lang/lang_keys.h"
#include "window/window_controller.h" // Controller::sessionController.
#include "window/window_session_controller.h"
#include "styles/style_calls.h"

namespace Calls {
namespace {

constexpr auto kLevelThreshold = 0.2;
constexpr auto kRowBlobRadiusFactor = (float)(50. / 57.);
constexpr auto kLevelDuration = 100. + 500. * 0.33;
constexpr auto kScaleSmall = 0.704 - 0.1;
constexpr auto kScaleSmallMin = 0.926;
constexpr auto kScaleSmallMax = (float)(kScaleSmallMin + kScaleSmall);
constexpr auto kMaxLevel = 1.;

auto RowBlobs() -> std::array<Ui::Paint::Blobs::BlobData, 2> {
	return { {
		{
			.segmentsCount = 6,
			.minScale = kScaleSmallMin / kScaleSmallMax,
			.minRadius = st::groupCallRowBlobMinRadius
				* kRowBlobRadiusFactor,
			.maxRadius = st::groupCallRowBlobMaxRadius
				* kRowBlobRadiusFactor,
			.speedScale = 1.,
			.alpha = (76. / 255.),
		},
		{
			.segmentsCount = 8,
			.minScale = kScaleSmallMin / kScaleSmallMax,
			.minRadius = st::groupCallRowBlobMinRadius
				* kRowBlobRadiusFactor,
			.maxRadius = st::groupCallRowBlobMaxRadius
				* kRowBlobRadiusFactor,
			.speedScale = 1.,
			.alpha = (76. / 255.),
		},
	} };
}

class Row final : public PeerListRow {
public:
	Row(not_null<ChannelData*> channel, not_null<UserData*> user);

	enum class State {
		Active,
		Inactive,
		Muted,
	};

	void updateState(const Data::GroupCall::Participant *participant);
	void updateLevel(float level);
	void updateBlobAnimation(crl::time now);
	[[nodiscard]] State state() const {
		return _state;
	}
	[[nodiscard]] uint32 ssrc() const {
		return _ssrc;
	}
	[[nodiscard]] bool speaking() const {
		return _speaking;
	}

	void addActionRipple(QPoint point, Fn<void()> updateCallback) override;
	void stopLastActionRipple() override;

	int nameIconWidth() const override {
		return 0;
	}
	QSize actionSize() const override {
		return QSize(_st->width, _st->height);
	}
	bool actionDisabled() const override {
		return peer()->isSelf() || !_channel->canManageCall();
	}
	QMargins actionMargins() const override {
		return QMargins(
			0,
			0,
			st::groupCallMemberButtonSkip,
			0);
	}
	void paintAction(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

	auto generatePaintUserpicCallback() -> PaintRoundImageCallback override;

private:
	void refreshStatus() override;
	void setSpeaking(bool speaking);
	void setSsrc(uint32 ssrc);

	[[nodiscard]] static State ComputeState(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	[[nodiscard]] static not_null<const style::IconButton*> ComputeIconStyle(
		State state);

	State _state = State::Inactive;
	not_null<ChannelData*> _channel;
	not_null<const style::IconButton*> _st;
	std::unique_ptr<Ui::RippleAnimation> _actionRipple;
	std::unique_ptr<Ui::Paint::Blobs> _blobs;
	crl::time _blobsLastTime = 0;
	uint32 _ssrc = 0;
	float _level = 0.;
	bool _speaking = false;

};

class MembersController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	MembersController(
		not_null<GroupCall*> call,
		not_null<QWidget*> menuParent);

	using MuteRequest = GroupMembers::MuteRequest;

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
	[[nodiscard]] auto kickMemberRequests() const
		-> rpl::producer<not_null<UserData*>>;

private:
	[[nodiscard]] std::unique_ptr<Row> createSelfRow();
	[[nodiscard]] std::unique_ptr<Row> createRow(
		const Data::GroupCall::Participant &participant);

	void prepareRows(not_null<Data::GroupCall*> real);
	//void repaintByTimer();

	void setupListChangeViewers(not_null<GroupCall*> call);
	void subscribeToChanges(not_null<Data::GroupCall*> real);
	void updateRow(
		const std::optional<Data::GroupCall::Participant> &was,
		const Data::GroupCall::Participant &now);
	void updateRow(
		not_null<Row*> row,
		const Data::GroupCall::Participant *participant);
	void removeRow(not_null<Row*> row);
	void updateRowLevel(not_null<Row*> row, float level);
	void checkSpeakingRowPosition(not_null<Row*> row);
	Row *findRow(not_null<UserData*> user) const;

	[[nodiscard]] Data::GroupCall *resolvedRealCall() const;

	const base::weak_ptr<GroupCall> _call;
	const not_null<ChannelData*> _channel;

	// Use only resolvedRealCall() method, not this value directly.
	Data::GroupCall *_realCallRawValue = nullptr;
	uint64 _realId = 0;

	rpl::event_stream<MuteRequest> _toggleMuteRequests;
	rpl::event_stream<not_null<UserData*>> _kickMemberRequests;
	rpl::variable<int> _fullCount = 1;

	not_null<QWidget*> _menuParent;
	base::unique_qptr<Ui::PopupMenu> _menu;

	base::flat_map<uint32, not_null<Row*>> _speakingRowBySsrc;
	Ui::Animations::Basic _speakingAnimation;

	rpl::lifetime _lifetime;

};

Row::Row(not_null<ChannelData*> channel, not_null<UserData*> user)
: PeerListRow(user)
, _state(ComputeState(channel, user))
, _channel(channel)
, _st(ComputeIconStyle(_state)) {
	refreshStatus();
}

void Row::updateState(const Data::GroupCall::Participant *participant) {
	setSsrc(participant ? participant->ssrc : 0);
	if (!participant) {
		if (peer()->isSelf()) {
			setCustomStatus(tr::lng_group_call_connecting(tr::now));
		} else {
			setCustomStatus(QString());
		}
		_state = State::Inactive;
		setSpeaking(false);
	} else if (!participant->muted) {
		_state = State::Active;
		setSpeaking(participant->speaking && participant->ssrc != 0);
	} else if (participant->canSelfUnmute) {
		_state = State::Inactive;
		setSpeaking(false);
	} else {
		_state = State::Muted;
		setSpeaking(false);
	}
	_st = ComputeIconStyle(_state);
}

void Row::setSpeaking(bool speaking) {
	if (_speaking == speaking) {
		return;
	}
	_speaking = speaking;
	if (!_speaking) {
		_blobs = nullptr;
	}
	refreshStatus();
}

void Row::setSsrc(uint32 ssrc) {
	_ssrc = ssrc;
}

void Row::updateLevel(float level) {
	Expects(_speaking);

	if (!_blobs) {
		_blobs = std::make_unique<Ui::Paint::Blobs>(
			RowBlobs() | ranges::to_vector,
			kLevelDuration,
			kMaxLevel);
		_blobsLastTime = crl::now();
	}
	_blobs->setLevel(level + 0.5);
}

void Row::updateBlobAnimation(crl::time now) {
	if (_blobs) {
		_blobs->updateLevel(now - _blobsLastTime);
		_blobsLastTime = now;
	}
}

auto Row::generatePaintUserpicCallback() -> PaintRoundImageCallback {
	auto userpic = ensureUserpicView();
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		if (_blobs) {
			const auto shift = QPointF(x + size / 2., y + size / 2.);
			p.translate(shift);
			_blobs->paint(p, st::groupCallLive1);
			p.translate(-shift);
			p.setOpacity(1.);
		}
		peer()->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
	};
}

void Row::paintAction(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	auto size = actionSize();
	if (_actionRipple) {
		_actionRipple->paint(
			p,
			x + _st->rippleAreaPosition.x(),
			y + _st->rippleAreaPosition.y(),
			outerWidth);
		if (_actionRipple->empty()) {
			_actionRipple.reset();
		}
	}
	_st->icon.paintInCenter(
		p,
		style::rtlrect(x, y, size.width(), size.height(), outerWidth));
}

void Row::refreshStatus() {
	setCustomStatus(
		(_speaking
			? tr::lng_group_call_active(tr::now)
			: tr::lng_group_call_inactive(tr::now)),
		_speaking);
}

Row::State Row::ComputeState(
		not_null<ChannelData*> channel,
		not_null<UserData*> user) {
	const auto call = channel->call();
	if (!call) {
		return State::Inactive;
	}
	const auto &participants = call->participants();
	const auto i = ranges::find(
		participants,
		user,
		&Data::GroupCall::Participant::user);
	if (i == end(participants)) {
		return State::Inactive;
	}
	return !i->muted
		? State::Active
		: i->canSelfUnmute
		? State::Inactive
		: State::Muted;
}

not_null<const style::IconButton*> Row::ComputeIconStyle(
		State state) {
	switch (state) {
	case State::Inactive: return &st::groupCallInactiveButton;
	case State::Active: return &st::groupCallActiveButton;
	case State::Muted: return &st::groupCallMutedButton;
	}
	Unexpected("State in Row::ComputeIconStyle.");
}

void Row::addActionRipple(QPoint point, Fn<void()> updateCallback) {
	if (!_actionRipple) {
		auto mask = Ui::RippleAnimation::ellipseMask(
			QSize(_st->rippleAreaSize, _st->rippleAreaSize));
		_actionRipple = std::make_unique<Ui::RippleAnimation>(
			_st->ripple,
			std::move(mask),
			std::move(updateCallback));
	}
	_actionRipple->add(point - _st->rippleAreaPosition);
}

void Row::stopLastActionRipple() {
	if (_actionRipple) {
		_actionRipple->lastStop();
	}
}

MembersController::MembersController(
	not_null<GroupCall*> call,
	not_null<QWidget*> menuParent)
: _call(call)
, _channel(call->channel())
, _menuParent(menuParent) {
	setupListChangeViewers(call);

	_speakingAnimation.init([=](crl::time now) {
		for (const auto [ssrc, row] : _speakingRowBySsrc) {
			row->updateBlobAnimation(now);
			delegate()->peerListUpdateRow(row);
		}
		return true;
	});
}

void MembersController::setupListChangeViewers(not_null<GroupCall*> call) {
	const auto channel = call->channel();
	channel->session().changes().peerFlagsValue(
		channel,
		Data::PeerUpdate::Flag::GroupCall
	) | rpl::map([=] {
		return channel->call();
	}) | rpl::filter([=](Data::GroupCall *real) {
		const auto call = _call.get();
		return call && real && (real->id() == call->id());
	}) | rpl::take(
		1
	) | rpl::start_with_next([=](not_null<Data::GroupCall*> real) {
		subscribeToChanges(real);
	}, _lifetime);

	call->stateValue(
	) | rpl::start_with_next([=] {
		const auto call = _call.get();
		const auto real = channel->call();
		if (call && real && (real->id() == call->id())) {
			//updateRow(channel->session().user());
		}
	}, _lifetime);

	call->levelUpdates(
	) | rpl::start_with_next([=](const LevelUpdate &update) {
		const auto i = _speakingRowBySsrc.find(update.ssrc);
		if (i != end(_speakingRowBySsrc)) {
			updateRowLevel(i->second, update.value);
		}
	}, _lifetime);
}

void MembersController::subscribeToChanges(not_null<Data::GroupCall*> real) {
	_realCallRawValue = real;
	_realId = real->id();

	_fullCount = real->fullCountValue(
	) | rpl::map([](int value) {
		return std::max(value, 1);
	});

	real->participantsSliceAdded(
	) | rpl::start_with_next([=] {
		prepareRows(real);
	}, _lifetime);

	using Update = Data::GroupCall::ParticipantUpdate;
	real->participantUpdated(
	) | rpl::start_with_next([=](const Update &update) {
		Expects(update.was.has_value() || update.now.has_value());

		const auto user = update.was ? update.was->user : update.now->user;
		if (!update.now) {
			if (const auto row = findRow(user)) {
				if (user->isSelf()) {
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
}

void MembersController::updateRow(
		const std::optional<Data::GroupCall::Participant> &was,
		const Data::GroupCall::Participant &now) {
	if (const auto row = findRow(now.user)) {
		if (now.speaking && (!was || !was->speaking)) {
			checkSpeakingRowPosition(row);
		}
		updateRow(row, &now);
	} else if (auto row = createRow(now)) {
		if (row->speaking()) {
			delegate()->peerListPrependRow(std::move(row));
		} else {
			delegate()->peerListAppendRow(std::move(row));
		}
		delegate()->peerListRefreshRows();
	}
}

void MembersController::checkSpeakingRowPosition(not_null<Row*> row) {
	// Check if there are non-speaking rows above this one.
	const auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		const auto above = delegate()->peerListRowAt(i);
		if (above == row) {
			// All rows above are speaking.
			return;
		} else if (!static_cast<Row*>(above.get())->speaking()) {
			break;
		}
	}
	// Someone started speaking and has a non-speaking row above him. Sort.
	const auto proj = [&](const PeerListRow &other) {
		if (&other == row.get()) {
			// Bring this new one to the top.
			return 0;
		} else if (static_cast<const Row&>(other).speaking()) {
			// Bring all the speaking ones below him.
			return 1;
		} else {
			return 2;
		}
	};
	delegate()->peerListSortRows([&](
			const PeerListRow &a,
			const PeerListRow &b) {
		return proj(a) < proj(b);
	});
}

void MembersController::updateRow(
		not_null<Row*> row,
		const Data::GroupCall::Participant *participant) {
	const auto wasSpeaking = row->speaking();
	const auto wasSsrc = row->ssrc();
	row->updateState(participant);
	const auto nowSpeaking = row->speaking();
	const auto nowSsrc = row->ssrc();

	const auto wasNoSpeaking = _speakingRowBySsrc.empty();
	if (wasSsrc == nowSsrc) {
		if (nowSpeaking != wasSpeaking) {
			if (nowSpeaking) {
				_speakingRowBySsrc.emplace(nowSsrc, row);
			} else {
				_speakingRowBySsrc.remove(nowSsrc);
			}
		}
	} else {
		_speakingRowBySsrc.remove(wasSsrc);
		if (nowSpeaking) {
			Assert(nowSsrc != 0);
			_speakingRowBySsrc.emplace(nowSsrc, row);
		}
	}
	const auto nowNoSpeaking = _speakingRowBySsrc.empty();
	if (wasNoSpeaking && !nowNoSpeaking) {
		_speakingAnimation.start();
	} else if (nowNoSpeaking && !wasNoSpeaking) {
		_speakingAnimation.stop();
	}

	delegate()->peerListUpdateRow(row);
}

void MembersController::removeRow(not_null<Row*> row) {
	_speakingRowBySsrc.remove(row->ssrc());
	delegate()->peerListRemoveRow(row);
}

void MembersController::updateRowLevel(
		not_null<Row*> row,
		float level) {
	row->updateLevel(level);
}

Row *MembersController::findRow(not_null<UserData*> user) const {
	return static_cast<Row*>(delegate()->peerListFindRow(user->id));
}

Data::GroupCall *MembersController::resolvedRealCall() const {
	return (_realCallRawValue
		&& (_channel->call() == _realCallRawValue)
		&& (_realCallRawValue->id() == _realId))
		? _realCallRawValue
		: nullptr;
}

Main::Session &MembersController::session() const {
	return _call->channel()->session();
}

void MembersController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Disabled);
	//delegate()->peerListSetTitle(std::move(title));
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));

	const auto call = _call.get();
	if (const auto real = _channel->call();
		real && call && real->id() == call->id()) {
		prepareRows(real);
	} else if (auto row = createSelfRow()) {
		delegate()->peerListAppendRow(std::move(row));
		delegate()->peerListRefreshRows();
	}
	loadMoreRows();
}

void MembersController::prepareRows(not_null<Data::GroupCall*> real) {
	auto foundSelf = false;
	auto changed = false;
	const auto &participants = real->participants();
	auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count;) {
		auto row = delegate()->peerListRowAt(i);
		auto user = row->peer()->asUser();
		if (user->isSelf()) {
			foundSelf = true;
			++i;
			continue;
		}
		const auto contains = ranges::contains(
			participants,
			not_null{ user },
			&Data::GroupCall::Participant::user);
		if (contains) {
			++i;
		} else {
			changed = true;
			removeRow(static_cast<Row*>(row.get()));
			--count;
		}
	}
	if (!foundSelf) {
		const auto self = _channel->session().user();
		const auto i = ranges::find(
			participants,
			_channel->session().user(),
			&Data::GroupCall::Participant::user);
		auto row = (i != end(participants)) ? createRow(*i) : createSelfRow();
		if (row) {
			changed = true;
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	for (const auto &participant : participants) {
		if (auto row = createRow(participant)) {
			changed = true;
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	if (changed) {
		delegate()->peerListRefreshRows();
	}
}

void MembersController::loadMoreRows() {
	if (const auto real = _channel->call()) {
		real->requestParticipants();
	}
}

auto MembersController::toggleMuteRequests() const
-> rpl::producer<MuteRequest> {
	return _toggleMuteRequests.events();
}

auto MembersController::kickMemberRequests() const
-> rpl::producer<not_null<UserData*>>{
	return _kickMemberRequests.events();
}

void MembersController::rowClicked(not_null<PeerListRow*> row) {
	if (_menu) {
		_menu->deleteLater();
		_menu = nullptr;
	}
	_menu = rowContextMenu(_menuParent, row);
	_menu->popup(QCursor::pos());
}

void MembersController::rowActionClicked(
		not_null<PeerListRow*> row) {
	rowClicked(row);
}

base::unique_qptr<Ui::PopupMenu> MembersController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto real = static_cast<Row*>(row.get());
	const auto user = row->peer()->asUser();
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::groupCallPopupMenu);

	const auto mute = (real->state() != Row::State::Muted);
	const auto toggleMute = crl::guard(this, [=] {
		_toggleMuteRequests.fire(MuteRequest{
			.user = user,
			.mute = mute,
		});
	});

	const auto session = &user->session();
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
			window->showPeerInfo(user);
		});
	};
	const auto showHistory = [=] {
		performOnMainWindow([=](not_null<Window::SessionController*> window) {
			window->showPeerHistory(user);
		});
	};
	const auto removeFromGroup = crl::guard(this, [=] {
		_kickMemberRequests.fire_copy(user);
	});

	if (!user->isSelf() && _channel->canManageCall()) {
		result->addAction(
			(mute
				? tr::lng_group_call_context_mute(tr::now)
				: tr::lng_group_call_context_unmute(tr::now)),
			toggleMute);
	}
	result->addAction(
		tr::lng_context_view_profile(tr::now),
		showProfile);
	result->addAction(
		tr::lng_context_send_message(tr::now),
		showHistory);
	if (_channel->canRestrictUser(user)) {
		result->addAction(
			tr::lng_context_remove_from_group(tr::now),
			removeFromGroup);
	}
	return result;
}

std::unique_ptr<Row> MembersController::createSelfRow() {
	const auto self = _channel->session().user();
	auto result = std::make_unique<Row>(_channel, self);
	updateRow(result.get(), nullptr);
	return result;
}

std::unique_ptr<Row> MembersController::createRow(
		const Data::GroupCall::Participant &participant) {
	auto result = std::make_unique<Row>(_channel, participant.user);
	updateRow(result.get(), &participant);
	return result;
}

} // namespace

GroupMembers::GroupMembers(
	not_null<QWidget*> parent,
	not_null<GroupCall*> call)
: RpWidget(parent)
, _call(call)
, _scroll(this, st::defaultSolidScroll)
, _listController(std::make_unique<MembersController>(call, parent)) {
	setupHeader(call);
	setupList();
	setContent(_list);
	setupFakeRoundCorners();
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));

	paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		const auto headerPart = clip.intersected(
			QRect(0, 0, width(), _header->height()));
		if (!headerPart.isEmpty()) {
			QPainter(this).fillRect(headerPart, st::groupCallMembersBg);
		}
	}, lifetime());
}

auto GroupMembers::toggleMuteRequests() const
-> rpl::producer<GroupMembers::MuteRequest> {
	return static_cast<MembersController*>(
		_listController.get())->toggleMuteRequests();
}

auto GroupMembers::kickMemberRequests() const
-> rpl::producer<not_null<UserData*>> {
	return static_cast<MembersController*>(
		_listController.get())->kickMemberRequests();
}

int GroupMembers::desiredHeight() const {
	auto desired = _header ? _header->height() : 0;
	auto count = [&] {
		if (const auto call = _call.get()) {
			if (const auto real = call->channel()->call()) {
				if (call->id() == real->id()) {
					return real->fullCount();
				}
			}
		}
		return 0;
	}();
	const auto use = std::max(count, _list->fullRowsCount());
	return (_header ? _header->height() : 0)
		+ (use * st::groupCallMembersList.item.height)
		+ (use ? st::lineWidth : 0);
}

rpl::producer<int> GroupMembers::desiredHeightValue() const {
	const auto controller = static_cast<MembersController*>(
		_listController.get());
	return rpl::combine(
		heightValue(),
		controller->fullCountValue()
	) | rpl::map([=] {
		return desiredHeight();
	});
}

void GroupMembers::setupHeader(not_null<GroupCall*> call) {
	_header = object_ptr<Ui::FixedHeightWidget>(
		this,
		st::groupCallMembersHeader);
	auto parent = _header.data();

	_titleWrap = Ui::CreateChild<Ui::RpWidget>(parent);
	_title = setupTitle(call);
	_addMember = Ui::CreateChild<Ui::IconButton>(
		parent,
		st::groupCallAddMember);
	setupButtons(call);

	widthValue(
	) | rpl::start_with_next([this](int width) {
		_header->resizeToWidth(width);
	}, _header->lifetime());
}

object_ptr<Ui::FlatLabel> GroupMembers::setupTitle(
		not_null<GroupCall*> call) {
	const auto controller = static_cast<MembersController*>(
		_listController.get());
	auto result = object_ptr<Ui::FlatLabel>(
		_titleWrap,
		tr::lng_chat_status_members(
			lt_count_decimal,
			controller->fullCountValue() | tr::to_count(),
			Ui::Text::Upper
		),
		st::groupCallHeaderLabel);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	return result;
}

void GroupMembers::setupButtons(not_null<GroupCall*> call) {
	using namespace rpl::mappers;

	_addMember->showOn(Data::CanWriteValue(
		call->channel().get()
	));
	_addMember->addClickHandler([=] { // TODO throttle(ripple duration)
		_addMemberRequests.fire({});
	});
}

void GroupMembers::setupList() {
	auto topSkip = _header ? _header->height() : 0;

	_listController->setStyleOverrides(&st::groupCallMembersList);
	_list = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		_listController.get()));

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_scroll->setGeometry(0, topSkip, size.width(), size.height() - topSkip);
		_list->resizeToWidth(size.width());
	}, _list->lifetime());

	_list->heightValue(
	) | rpl::start_with_next([=](int listHeight) {
		auto newHeight = (listHeight > 0)
			? (topSkip + listHeight + st::lineWidth)
			: 0;
		resize(width(), newHeight);
	}, _list->lifetime());
	_list->moveToLeft(0, topSkip);
	_list->show();
}

void GroupMembers::resizeEvent(QResizeEvent *e) {
	if (_header) {
		updateHeaderControlsGeometry(width());
	}
}

void GroupMembers::updateHeaderControlsGeometry(int newWidth) {
	auto availableWidth = newWidth
		- st::groupCallAddButtonPosition.x();
	_addMember->moveToLeft(
		availableWidth - _addMember->width(),
		st::groupCallAddButtonPosition.y(),
		newWidth);
	if (!_addMember->isHidden()) {
		availableWidth -= _addMember->width();
	}

	_titleWrap->resize(
		availableWidth - _addMember->width() - st::groupCallHeaderPosition.x(),
		_title->height());
	_titleWrap->moveToLeft(
		st::groupCallHeaderPosition.x(),
		st::groupCallHeaderPosition.y(),
		newWidth);
	_titleWrap->setAttribute(Qt::WA_TransparentForMouseEvents);

	_title->resizeToWidth(_titleWrap->width());
	_title->moveToLeft(0, 0);
}

void GroupMembers::setupFakeRoundCorners() {
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
		const auto result = Ui::CreateChild<Ui::RpWidget>(this);
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

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		topleft->move(0, 0);
		topright->move(size.width() - topright->width(), 0);
		bottomleft->move(0, size.height() - bottomleft->height());
		bottomright->move(
			size.width() - bottomright->width(),
			size.height() - bottomright->height());
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

void GroupMembers::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

void GroupMembers::peerListSetTitle(rpl::producer<QString> title) {
}

void GroupMembers::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

bool GroupMembers::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

void GroupMembers::peerListScrollToTop() {
}

int GroupMembers::peerListSelectedRowsCount() {
	return 0;
}

std::vector<not_null<PeerData*>> GroupMembers::peerListCollectSelectedRows() {
	return {};
}

void GroupMembers::peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Calls::GroupMembers.");
}

void GroupMembers::peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) {
	Unexpected("Item selection in Calls::GroupMembers.");
}

void GroupMembers::peerListFinishSelectedRowsBunch() {
}

void GroupMembers::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

} // namespace Calls
