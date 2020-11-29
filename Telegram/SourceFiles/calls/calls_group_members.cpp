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
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/ripple_animation.h"
#include "main/main_session.h"
#include "base/timer.h"
#include "boxes/peers/edit_participants_box.h"
#include "lang/lang_keys.h"
#include "facades.h" // Ui::showPeerHistory.
#include "mainwindow.h" // App::wnd()->activate.
#include "styles/style_calls.h"

namespace Calls {
namespace {

constexpr auto kLevelThreshold = 0.2;

struct UpdateLevelResult {
	bool levelChanged = false;
	bool stateChanged = false;
	crl::time nextUpdateTime = 0;
};

class Row final : public PeerListRow {
public:
	Row(not_null<ChannelData*> channel, not_null<UserData*> user);

	enum class State {
		Active,
		Inactive,
		Muted,
	};

	void updateState(const Data::GroupCall::Participant *participant);
	//UpdateLevelResult updateLevel(float level);
	[[nodiscard]] State state() const {
		return _state;
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

private:
	void refreshStatus() override;
	void setSpeaking(bool speaking);

	[[nodiscard]] static State ComputeState(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	[[nodiscard]] static not_null<const style::IconButton*> ComputeIconStyle(
		State state);

	State _state = State::Inactive;
	not_null<ChannelData*> _channel;
	not_null<const style::IconButton*> _st;
	bool _speaking = false;
	//float _level = 0.;

	std::unique_ptr<Ui::RippleAnimation> _actionRipple;

};

class MembersController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	explicit MembersController(not_null<GroupCall*> call);

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

private:
	[[nodiscard]] std::unique_ptr<PeerListRow> createSelfRow() const;
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(
		const Data::GroupCall::Participant &participant) const;

	void prepareRows(not_null<Data::GroupCall*> real);
	//void repaintByTimer();

	void setupListChangeViewers(not_null<GroupCall*> call);
	void subscribeToChanges(not_null<Data::GroupCall*> real);
	void updateRow(
		const std::optional<Data::GroupCall::Participant> &was,
		const Data::GroupCall::Participant &now);
	void updateRow(
		not_null<Row*> row,
		const Data::GroupCall::Participant *participant) const;
	void checkSpeakingRowPosition(not_null<Row*> row);
	//void updateRowLevel(not_null<UserData*> user, float level);
	Row *findRow(not_null<UserData*> user) const;

	[[nodiscard]] Data::GroupCall *resolvedRealCall() const;

	const base::weak_ptr<GroupCall> _call;
	const not_null<ChannelData*> _channel;

	// Use only resolvedRealCall() method, not this value directly.
	Data::GroupCall *_realCallRawValue = nullptr;
	uint64 _realId = 0;

	rpl::event_stream<MuteRequest> _toggleMuteRequests;
	rpl::variable<int> _fullCount = 1;
	Ui::BoxPointer _addBox;

	//base::flat_map<not_null<UserData*>, crl::time> _repaintByTimer;
	//base::Timer _repaintTimer;

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
		setSpeaking(participant->speaking);
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
	refreshStatus();
	//if (!_speaking) {
	//	updateLevel(0.);
	//}
}

//UpdateLevelResult Row::updateLevel(float level) {
//	if (_level == level) {
//		return UpdateLevelResult{ .nextUpdateTime = _markInactiveAt };
//	}
//	const auto now = crl::now();
//	const auto stillActive = (now < _markInactiveAt);
//	const auto wasActive = (_level >= kLevelThreshold) && stillActive;
//	const auto nowActive = (level >= kLevelThreshold);
//	if (nowActive) {
//		_markInactiveAt = now + kLevelActiveTimeout;
//		if (_state != State::Active) {
//			_state = State::Active;
//			_st = ComputeIconStyle(_state);
//		}
//	}
//	_level = level;
//	const auto changed = wasActive != (nowActive || stillActive);
//	if (!changed) {
//		return UpdateLevelResult{
//			.levelChanged = true,
//			.nextUpdateTime = _markInactiveAt,
//		};
//	}
//	refreshStatus(now);
//	return UpdateLevelResult{
//		.levelChanged = true,
//		.stateChanged = true,
//		.nextUpdateTime = _markInactiveAt,
//	};
//}

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

MembersController::MembersController(not_null<GroupCall*> call)
: _call(call)
, _channel(call->channel()) {
//, _repaintTimer([=] { repaintByTimer(); }) {
	setupListChangeViewers(call);
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

	//call->levelUpdates(
	//) | rpl::start_with_next([=](const LevelUpdate &update) {
	//	const auto findUserBySource = [&](uint32 source) -> UserData* {
	//		if (const auto real = resolvedRealCall()) {
	//			return real->userBySource(source);
	//		}
	//		return nullptr;
	//	};
	//	const auto user = update.self
	//		? _channel->session().user().get()
	//		: findUserBySource(update.source);
	//	if (user) {
	//		updateRowLevel(user, update.value);
	//	}
	//}, _lifetime);
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
					row->updateState(nullptr);
					delegate()->peerListUpdateRow(row);
				} else {
					delegate()->peerListRemoveRow(row);
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
		if (now.speaking) {
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
		const Data::GroupCall::Participant *participant) const {
	row->updateState(participant);
	delegate()->peerListUpdateRow(row);
}

//void MembersController::updateRowLevel(
//		not_null<UserData*> user,
//		float level) {
//	if (const auto row = findRow(user)) {
//		const auto result = row->updateLevel(level);
//		if (result.stateChanged) {
//			// #TODO calls reorder.
//		}
//		if (result.stateChanged) {
//			delegate()->peerListUpdateRow(row);
//		}
//		if (result.nextUpdateTime) {
//			_repaintByTimer[user] = result.nextUpdateTime;
//			if (!_repaintTimer.isActive()) {
//				_repaintTimer.callOnce(kLevelActiveTimeout);
//			}
//		} else if (_repaintByTimer.remove(user) && _repaintByTimer.empty()) {
//			_repaintTimer.cancel();
//		}
//	}
//}

//void MembersController::repaintByTimer() {
//	const auto now = crl::now();
//	auto next = crl::time(0);
//	for (auto i = begin(_repaintByTimer); i != end(_repaintByTimer);) {
//		if (i->second > now) {
//			if (!next || next > i->second) {
//				next = i->second;
//			}
//		} else if (const auto row = findRow(i->first)) {
//			delegate()->peerListUpdateRow(row);
//			i = _repaintByTimer.erase(i);
//			continue;
//		}
//		++i;
//	}
//	if (next) {
//		_repaintTimer.callOnce(next - now);
//	}
//}

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
			delegate()->peerListRemoveRow(row);
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
-> rpl::producer<GroupMembers::MuteRequest> {
	return _toggleMuteRequests.events();
}

void MembersController::rowClicked(not_null<PeerListRow*> row) {
	Ui::showPeerHistory(row->peer(), ShowAtUnreadMsgId);
	App::wnd()->activate();
}

void MembersController::rowActionClicked(
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto real = static_cast<Row*>(row.get());
	const auto mute = (real->state() != Row::State::Muted);
	_toggleMuteRequests.fire(MuteRequest{
		.user = row->peer()->asUser(),
		.mute = mute,
	});
}

base::unique_qptr<Ui::PopupMenu> MembersController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto user = row->peer()->asUser();
	return nullptr;
}

std::unique_ptr<PeerListRow> MembersController::createSelfRow() const {
	const auto self = _channel->session().user();
	auto result = std::make_unique<Row>(_channel, self);
	updateRow(result.get(), nullptr);
	return result;
}

std::unique_ptr<PeerListRow> MembersController::createRow(
		const Data::GroupCall::Participant &participant) const {
	auto result = std::make_unique<Row>(_channel, participant.user);
	updateRow(result.get(), &participant);
	return result;
}

} // namespace

GroupMembers::GroupMembers(
	QWidget *parent,
	not_null<GroupCall*> call)
: RpWidget(parent)
, _call(call)
, _scroll(this, st::defaultSolidScroll)
, _listController(std::make_unique<MembersController>(call)) {
	setupHeader(call);
	setupList();
	setContent(_list);
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));

	paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		QPainter(this).fillRect(clip, st::groupCallMembersBg);
	}, lifetime());
}

auto GroupMembers::toggleMuteRequests() const
-> rpl::producer<GroupMembers::MuteRequest> {
	return static_cast<MembersController*>(
		_listController.get())->toggleMuteRequests();
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
	_list = _scroll->setOwnedWidget(object_ptr<ListWidget>(
		this,
		_listController.get(),
		st::groupCallMembersList));

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
