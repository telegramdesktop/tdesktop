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
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/ripple_animation.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "styles/style_calls.h"

namespace Calls {
namespace {

class MembersController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	explicit MembersController(not_null<GroupCall*> call);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

private:
	[[nodiscard]] std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) const;

	void prepareRows();

	void setupListChangeViewers();
	bool appendRow(not_null<UserData*> user);
	bool prependRow(not_null<UserData*> user);
	bool removeRow(not_null<UserData*> user);

	const base::weak_ptr<GroupCall> _call;
	const not_null<ChannelData*> _channel;

	Ui::BoxPointer _addBox;
	rpl::lifetime _lifetime;

};

class Row final : public PeerListRow {
public:
	Row(not_null<ChannelData*> channel, not_null<UserData*> user);

	enum class State {
		Active,
		Inactive,
		Muted,
	};

	void addActionRipple(QPoint point, Fn<void()> updateCallback) override;
	void stopLastActionRipple() override;

	int nameIconWidth() const override {
		return 0;
	}
	QSize actionSize() const override {
		return QSize(_st->width, _st->height);
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

	[[nodiscard]] static State ComputeState(
		not_null<ChannelData*> channel,
		not_null<UserData*> user);
	[[nodiscard]] static not_null<const style::IconButton*> ComputeIconStyle(
		State state);

	State _state = State::Inactive;
	not_null<const style::IconButton*> _st;

	std::unique_ptr<Ui::RippleAnimation> _actionRipple;

};

Row::Row(not_null<ChannelData*> channel, not_null<UserData*> user)
: PeerListRow(user)
, _state(ComputeState(channel, user))
, _st(ComputeIconStyle(_state)) {
	refreshStatus();
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
	setCustomStatus([&] {
		switch (_state) {
		case State::Inactive:
		case State::Muted: return tr::lng_group_call_inactive(tr::now);
		case State::Active: return tr::lng_group_call_active(tr::now);
		}
		Unexpected("State in Row::refreshStatus.");
	}());
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
	setupListChangeViewers();
}

void MembersController::setupListChangeViewers() {
	const auto call = _call.get();
	const auto channel = call->channel();
	channel->session().changes().peerUpdates(
		channel,
		Data::PeerUpdate::Flag::GroupCall
	) | rpl::start_with_next([=] {
		prepareRows();
	}, _lifetime);
}

Main::Session &MembersController::session() const {
	return _call->channel()->session();
}

void MembersController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Disabled);
	//delegate()->peerListSetTitle(std::move(title));
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));

	prepareRows();
	delegate()->peerListRefreshRows();

	loadMoreRows();
}

void MembersController::prepareRows() {
	const auto real = _channel->call();
	if (!real) {
		return;
	}
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
		if (auto row = createRow(_channel->session().user())) {
			changed = true;
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	for (const auto &participant : participants) {
		if (auto row = createRow(participant.user)) {
			changed = true;
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	if (changed) {
		delegate()->peerListRefreshRows();
	}
}

void MembersController::loadMoreRows() {
	if (const auto call = _call.get()) {
		if (const auto real = call->channel()->call()) {
			real->requestParticipants();
		}
	}
}

void MembersController::rowClicked(not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto user = row->peer()->asUser();
}

void MembersController::rowActionClicked(
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto user = row->peer()->asUser();
}

base::unique_qptr<Ui::PopupMenu> MembersController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto user = row->peer()->asUser();
	return nullptr;
}

bool MembersController::appendRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	return true;
}

bool MembersController::prependRow(not_null<UserData*> user) {
	if (auto row = delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	return true;
}

bool MembersController::removeRow(not_null<UserData*> user) {
	if (auto row = delegate()->peerListFindRow(user->id)) {
		delegate()->peerListRemoveRow(row);
		return true;
	}
	return false;
}

std::unique_ptr<PeerListRow> MembersController::createRow(
		not_null<UserData*> user) const {
	return std::make_unique<Row>(_channel, user);
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

int GroupMembers::desiredHeight() const {
	auto desired = _header ? _header->height() : 0;
	auto count = [this] {
		if (const auto call = _call.get()) {
			if (const auto real = call->channel()->call()) {
				return real->fullCount();
			}
		}
		return 0;
	}();
	desired += std::max(count, _list->fullRowsCount())
		* st::groupCallMembersList.item.height;
	return std::max(height(), desired);
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
	setupButtons();

	widthValue(
	) | rpl::start_with_next([this](int width) {
		_header->resizeToWidth(width);
	}, _header->lifetime());
}

object_ptr<Ui::FlatLabel> GroupMembers::setupTitle(
		not_null<GroupCall*> call) {
	const auto channel = call->channel();
	auto count = channel->session().changes().peerFlagsValue(
		channel,
		Data::PeerUpdate::Flag::GroupCall
	) | rpl::map([=] {
		const auto call = channel->call();
		return std::max(call ? call->fullCount() : 0, 1);
	});
	auto result = object_ptr<Ui::FlatLabel>(
		_titleWrap,
		tr::lng_chat_status_members(
			lt_count_decimal,
			std::move(count) | tr::to_count(),
			Ui::Text::Upper
		),
		st::groupCallHeaderLabel);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	return result;
}

void GroupMembers::setupButtons() {
	using namespace rpl::mappers;

	_addMember->showOn(rpl::single(true));
	_addMember->addClickHandler([=] { // TODO throttle(ripple duration)
		addMember();
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
			? (topSkip + listHeight)
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

void GroupMembers::addMember() {
	// #TODO calls
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
