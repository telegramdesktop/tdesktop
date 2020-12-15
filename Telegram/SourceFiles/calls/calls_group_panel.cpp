/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_panel.h"

#include "calls/calls_group_members.h"
#include "calls/calls_group_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/window.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/call_mute_button.h"
#include "ui/widgets/checkbox.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "info/profile/info_profile_values.h" // Info::Profile::Value.
#include "core/application.h"
#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "base/event_filter.h"
#include "boxes/peers/edit_participants_box.h"
#include "app.h"
#include "apiwrap.h" // api().kickParticipant.
#include "styles/style_calls.h"
#include "styles/style_layers.h"

#ifdef Q_OS_WIN
#include "ui/platform/win/ui_window_title_win.h"
#endif // Q_OS_WIN

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>

namespace Calls {
namespace {

class InviteController final : public ParticipantsBoxController {
public:
	InviteController(
		not_null<PeerData*> peer,
		base::flat_set<not_null<UserData*>> alreadyIn,
		int fullInCount);

	void prepare() override;

	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

	void itemDeselectedHook(not_null<PeerData*> peer) override;

	std::variant<int, not_null<UserData*>> inviteSelectedUsers(
		not_null<PeerListBox*> box,
		not_null<GroupCall*> call) const;

private:
	[[nodiscard]] int alreadyInCount() const;
	[[nodiscard]] bool isAlreadyIn(not_null<UserData*> user) const;
	[[nodiscard]] int fullCount() const;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) const override;

	not_null<PeerData*> _peer;
	const base::flat_set<not_null<UserData*>> _alreadyIn;
	const int _fullInCount = 0;
	mutable base::flat_set<not_null<UserData*>> _skippedUsers;

};

InviteController::InviteController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> alreadyIn,
	int fullInCount)
: ParticipantsBoxController(CreateTag{}, nullptr, peer, Role::Members)
, _peer(peer)
, _alreadyIn(std::move(alreadyIn))
, _fullInCount(std::max(fullInCount, int(_alreadyIn.size()))) {
	_skippedUsers.emplace(peer->session().user());
	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { _peer = channel; });
}

void InviteController::prepare() {
	ParticipantsBoxController::prepare();
	delegate()->peerListSetTitle(tr::lng_group_call_invite_title());
}

void InviteController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

base::unique_qptr<Ui::PopupMenu> InviteController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

void InviteController::itemDeselectedHook(not_null<PeerData*> peer) {
}

int InviteController::alreadyInCount() const {
	return std::max(_fullInCount, int(_alreadyIn.size()));
}

bool InviteController::isAlreadyIn(not_null<UserData*> user) const {
	return _alreadyIn.contains(user);
}

int InviteController::fullCount() const {
	return alreadyInCount() + delegate()->peerListSelectedRowsCount();
}

std::unique_ptr<PeerListRow> InviteController::createRow(
		not_null<UserData*> user) const {
	if (user->isSelf() || user->isBot()) {
		_skippedUsers.emplace(user);
		return nullptr;
	}
	auto result = std::make_unique<PeerListRow>(user);
	if (isAlreadyIn(user)) {
		result->setDisabledState(PeerListRow::State::DisabledChecked);
	}
	return result;
}

std::variant<int, not_null<UserData*>> InviteController::inviteSelectedUsers(
		not_null<PeerListBox*> box,
		not_null<GroupCall*> call) const {
	const auto rows = box->peerListCollectSelectedRows();
	const auto users = ranges::view::all(
		rows
	) | ranges::view::transform([](not_null<PeerData*> peer) {
		Expects(peer->isUser());
		Expects(!peer->isSelf());

		return not_null<UserData*>(peer->asUser());
	}) | ranges::to_vector;
	return call->inviteUsers(users);
}

} // namespace

void LeaveGroupCallBox(
		not_null<Ui::GenericBox*> box,
		not_null<GroupCall*> call,
		bool discardChecked,
		BoxContext context) {
	box->setTitle(tr::lng_group_call_leave_title());
	const auto inCall = (context == BoxContext::GroupCallPanel);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box.get(),
		tr::lng_group_call_leave_sure(),
		(inCall ? st::groupCallBoxLabel : st::boxLabel)));
	const auto discard = call->peer()->canManageGroupCall()
		? box->addRow(object_ptr<Ui::Checkbox>(
			box.get(),
			tr::lng_group_call_end(),
			discardChecked,
			(inCall ? st::groupCallCheckbox : st::defaultBoxCheckbox),
			(inCall ? st::groupCallCheck : st::defaultCheck)),
			style::margins(
				st::boxRowPadding.left(),
				st::boxRowPadding.left(),
				st::boxRowPadding.right(),
				st::boxRowPadding.bottom()))
		: nullptr;
	const auto weak = base::make_weak(call.get());
	box->addButton(tr::lng_group_call_leave(), [=] {
		const auto discardCall = (discard && discard->checked());
		box->closeBox();

		if (!weak) {
			return;
		} else if (discardCall) {
			call->discard();
		} else {
			call->hangup();
		}
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

GroupPanel::GroupPanel(not_null<GroupCall*> call)
: _call(call)
, _peer(call->peer())
, _window(std::make_unique<Ui::Window>(Core::App().getModalParent()))
, _layerBg(std::make_unique<Ui::LayerManager>(_window->body()))
#ifdef Q_OS_WIN
, _controls(std::make_unique<Ui::Platform::TitleControls>(
	_window.get(),
	st::groupCallTitle))
#endif // Q_OS_WIN
, _members(widget(), call)
, _settings(widget(), st::groupCallSettings)
, _mute(std::make_unique<Ui::CallMuteButton>(
	widget(),
	Core::App().appDeactivatedValue(),
	Ui::CallMuteButtonState{
		.text = tr::lng_group_call_connecting(tr::now),
		.type = Ui::CallMuteButtonType::Connecting,
	}))
, _hangup(widget(), st::groupCallHangup) {
	_layerBg->setStyleOverrides(&st::groupCallBox, &st::groupCallLayerBox);
	_settings->setColorOverrides(_mute->colorOverrides());

	SubscribeToMigration(
		_peer,
		_window->lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(channel); });

	initWindow();
	initWidget();
	initControls();
	initLayout();
	showAndActivate();
}

GroupPanel::~GroupPanel() = default;

bool GroupPanel::isActive() const {
	return _window->isActiveWindow()
		&& _window->isVisible()
		&& !(_window->windowState() & Qt::WindowMinimized);
}

void GroupPanel::showAndActivate() {
	if (_window->isHidden()) {
		_window->show();
	}
	const auto state = _window->windowState();
	if (state & Qt::WindowMinimized) {
		_window->setWindowState(state & ~Qt::WindowMinimized);
	}
	_window->raise();
	_window->activateWindow();
	_window->setFocus();
}

void GroupPanel::migrate(not_null<ChannelData*> channel) {
	_peer = channel;
	_peerLifetime.destroy();
	subscribeToPeerChanges();
	_title.destroy();
	refreshTitle();
}

void GroupPanel::subscribeToPeerChanges() {
	Info::Profile::NameValue(
		_peer
	) | rpl::start_with_next([=](const TextWithEntities &name) {
		_window->setTitle(name.text);
	}, _peerLifetime);
}

void GroupPanel::initWindow() {
	_window->setAttribute(Qt::WA_OpaquePaintEvent);
	_window->setAttribute(Qt::WA_NoSystemBackground);
	_window->setWindowIcon(
		QIcon(QPixmap::fromImage(Image::Empty()->original(), Qt::ColorOnly)));
	_window->setTitleStyle(st::callTitle);

	subscribeToPeerChanges();

	base::install_event_filter(_window.get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close && handleClose()) {
			e->ignore();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	_window->setBodyTitleArea([=](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		const auto titleRect = QRect(
			0,
			0,
			widget()->width(),
			computeMembersListTop());
		return titleRect.contains(widgetPoint)
			? (Flag::Move | Flag::Maximize)
			: Flag::None;
	});
}

void GroupPanel::initWidget() {
	widget()->setMouseTracking(true);

	widget()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		paint(clip);
	}, widget()->lifetime());

	widget()->sizeValue(
	) | rpl::skip(1) | rpl::start_with_next([=] {
		updateControlsGeometry();

		// title geometry depends on _controls->geometry,
		// which is not updated here yet.
		crl::on_main(widget(), [=] { refreshTitle(); });
	}, widget()->lifetime());
}

void GroupPanel::hangup(bool discardCallChecked) {
	if (!_call) {
		return;
	}
	_layerBg->showBox(Box(
		LeaveGroupCallBox,
		_call,
		discardCallChecked,
		BoxContext::GroupCallPanel));
}

void GroupPanel::initControls() {
	_mute->clicks(
	) | rpl::filter([=](Qt::MouseButton button) {
		return (button == Qt::LeftButton)
			&& _call
			&& (_call->muted() != MuteState::ForceMuted);
	}) | rpl::start_with_next([=] {
		_call->setMuted((_call->muted() == MuteState::Muted)
			? MuteState::Active
			: MuteState::Muted);
	}, _mute->lifetime());

	_hangup->setClickedCallback([=] { hangup(false); });
	_settings->setClickedCallback([=] {
		if (_call) {
			_layerBg->showBox(Box(GroupCallSettingsBox, _call));
		}
	});

	_settings->setText(tr::lng_menu_settings());
	_hangup->setText(tr::lng_box_leave());

	_members->desiredHeightValue(
	) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, _members->lifetime());

	initWithCall(_call);
}

void GroupPanel::initWithCall(GroupCall *call) {
	_callLifetime.destroy();
	_call = call;
	if (!_call) {
		return;
	}

	_peer = _call->peer();

	call->stateValue(
	) | rpl::filter([](State state) {
		return (state == State::HangingUp)
			|| (state == State::Ended)
			|| (state == State::FailedHangingUp)
			|| (state == State::Failed);
	}) | rpl::start_with_next([=] {
		closeBeforeDestroy();
	}, _callLifetime);

	call->levelUpdates(
	) | rpl::filter([=](const LevelUpdate &update) {
		return update.self;
	}) | rpl::start_with_next([=](const LevelUpdate &update) {
		_mute->setLevel(update.value);
	}, _callLifetime);

	_members->toggleMuteRequests(
	) | rpl::start_with_next([=](GroupMembers::MuteRequest request) {
		if (_call) {
			_call->toggleMute(request.user, request.mute);
		}
	}, _callLifetime);

	_members->kickMemberRequests(
	) | rpl::start_with_next([=](not_null<UserData*> user) {
		kickMember(user);
	}, _callLifetime);

	_members->addMembersRequests(
	) | rpl::start_with_next([=] {
		if (_call) {
			addMembers();
		}
	}, _callLifetime);

	using namespace rpl::mappers;
	rpl::combine(
		_call->mutedValue() | MapPushToTalkToActive(),
		_call->stateValue() | rpl::map(
			_1 == State::Creating
			|| _1 == State::Joining
			|| _1 == State::Connecting
		)
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](MuteState mute, bool connecting) {
		_mute->setState(Ui::CallMuteButtonState{
			.text = (connecting
				? tr::lng_group_call_connecting(tr::now)
				: mute == MuteState::ForceMuted
				? tr::lng_group_call_force_muted(tr::now)
				: mute == MuteState::Muted
				? tr::lng_group_call_unmute(tr::now)
				: tr::lng_group_call_you_are_live(tr::now)),
			.type = (connecting
				? Ui::CallMuteButtonType::Connecting
				: mute == MuteState::ForceMuted
				? Ui::CallMuteButtonType::ForceMuted
				: mute == MuteState::Muted
				? Ui::CallMuteButtonType::Muted
				: Ui::CallMuteButtonType::Active),
		});
	}, _callLifetime);
}

void GroupPanel::addMembers() {
	const auto real = _peer->groupCall();
	if (!_call || !real || real->id() != _call->id()) {
		return;
	}
	auto alreadyIn = _peer->owner().invitedToCallUsers(real->id());
	for (const auto &participant : real->participants()) {
		alreadyIn.emplace(participant.user);
	}
	alreadyIn.emplace(_peer->session().user());
	auto controller = std::make_unique<InviteController>(
		_peer,
		std::move(alreadyIn),
		real->fullCount());
	controller->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);

	const auto weak = base::make_weak(_call);
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		box->addButton(tr::lng_group_call_invite_button(), [=] {
			if (const auto call = weak.get()) {
				const auto result = controller->inviteSelectedUsers(box, call);

				if (const auto user = std::get_if<not_null<UserData*>>(&result)) {
					Ui::Toast::Show(
						widget(),
						Ui::Toast::Config{
							.text = tr::lng_group_call_invite_done_user(
								tr::now,
								lt_user,
								Ui::Text::Bold((*user)->firstName),
								Ui::Text::WithEntities),
							.st = &st::defaultToast,
						});
				} else if (const auto count = std::get_if<int>(&result)) {
					if (*count > 0) {
						Ui::Toast::Show(
							widget(),
							Ui::Toast::Config{
								.text = tr::lng_group_call_invite_done_many(
									tr::now,
									lt_count,
									*count,
									Ui::Text::RichLangValue),
								.st = &st::defaultToast,
							});
					}
				} else {
					Unexpected("Result in GroupCall::inviteUsers.");
				}
			}
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	_layerBg->showBox(Box<PeerListBox>(std::move(controller), initBox));
}

void GroupPanel::kickMember(not_null<UserData*> user) {
	_layerBg->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box.get(),
				tr::lng_profile_sure_kick(
					tr::now,
					lt_user,
					user->firstName),
				st::groupCallBoxLabel),
			style::margins(
				st::boxRowPadding.left(),
				st::boxPadding.top(),
				st::boxRowPadding.right(),
				st::boxPadding.bottom()));
		box->addButton(tr::lng_box_remove(), [=] {
			box->closeBox();
			kickMemberSure(user);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void GroupPanel::kickMemberSure(not_null<UserData*> user) {
	if (const auto chat = _peer->asChat()) {
		chat->session().api().kickParticipant(chat, user);
	} else if (const auto channel = _peer->asChannel()) {
		const auto currentRestrictedRights = [&]() -> MTPChatBannedRights {
			const auto it = channel->mgInfo->lastRestricted.find(user);
			return (it != channel->mgInfo->lastRestricted.cend())
				? it->second.rights
				: MTP_chatBannedRights(MTP_flags(0), MTP_int(0));
		}();

		channel->session().api().kickParticipant(
			channel,
			user,
			currentRestrictedRights);
	}
}

void GroupPanel::initLayout() {
	initGeometry();

#ifdef Q_OS_WIN
	_controls->raise();
#endif // Q_OS_WIN
}

void GroupPanel::showControls() {
	Expects(_call != nullptr);

	widget()->showChildren();
}

void GroupPanel::closeBeforeDestroy() {
	_window->close();
	initWithCall(nullptr);
}

void GroupPanel::initGeometry() {
	const auto center = Core::App().getPointForCallPanelCenter();
	const auto rect = QRect(0, 0, st::groupCallWidth, st::groupCallHeight);
	_window->setGeometry(rect.translated(center - rect.center()));
	_window->setMinimumSize(rect.size());
	_window->show();
	updateControlsGeometry();
}

int GroupPanel::computeMembersListTop() const {
#ifdef Q_OS_WIN
	return st::callTitleButton.height + st::groupCallMembersMargin.top() / 2;
#elif defined Q_OS_MAC // Q_OS_WIN
	return st::groupCallMembersMargin.top() * 2;
#else // Q_OS_WIN || Q_OS_MAC
	return st::groupCallMembersMargin.top();
#endif // Q_OS_WIN || Q_OS_MAC
}

std::optional<QRect> GroupPanel::computeTitleRect() const {
#ifdef Q_OS_WIN
	const auto controls = _controls->geometry();
	return QRect(0, 0, controls.x(), controls.height());
#else // Q_OS_WIN
	return std::nullopt;
#endif // Q_OS_WIN
}

void GroupPanel::updateControlsGeometry() {
	if (widget()->size().isEmpty()) {
		return;
	}
	const auto desiredHeight = _members->desiredHeight();
	const auto membersWidthAvailable = widget()->width()
		- st::groupCallMembersMargin.left()
		- st::groupCallMembersMargin.right();
	const auto membersWidthMin = st::groupCallWidth
		- st::groupCallMembersMargin.left()
		- st::groupCallMembersMargin.right();
	const auto membersWidth = std::clamp(
		membersWidthAvailable,
		membersWidthMin,
		st::groupCallMembersWidthMax);
	const auto muteTop = widget()->height() - st::groupCallMuteBottomSkip;
	const auto buttonsTop = widget()->height() - st::groupCallButtonBottomSkip;
	const auto membersTop = computeMembersListTop();
	const auto availableHeight = muteTop
		- membersTop
		- st::groupCallMembersMargin.bottom();
	_members->setGeometry(
		(widget()->width() - membersWidth) / 2,
		membersTop,
		membersWidth,
		std::min(desiredHeight, availableHeight));
	const auto muteSize = _mute->innerSize().width();
	const auto fullWidth = muteSize
		+ 2 * _settings->width()
		+ 2 * st::groupCallButtonSkip;
	_mute->moveInner({ (widget()->width() - muteSize) / 2, muteTop });
	_settings->moveToLeft((widget()->width() - fullWidth) / 2, buttonsTop);
	_hangup->moveToRight((widget()->width() - fullWidth) / 2, buttonsTop);
	refreshTitle();
}

void GroupPanel::refreshTitle() {
	if (const auto titleRect = computeTitleRect()) {
		if (!_title) {
			_title.create(
				widget(),
				Info::Profile::NameValue(_peer),
				st::groupCallHeaderLabel);
			_title->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
		const auto best = _title->naturalWidth();
		const auto from = (widget()->width() - best) / 2;
		const auto top = (computeMembersListTop() - _title->height()) / 2;
		const auto left = titleRect->x();
		if (from >= left && from + best <= left + titleRect->width()) {
			_title->resizeToWidth(best);
			_title->moveToLeft(from, top);
		} else if (titleRect->width() < best) {
			_title->resizeToWidth(titleRect->width());
			_title->moveToLeft(left, top);
		} else if (from < left) {
			_title->resizeToWidth(best);
			_title->moveToLeft(left, top);
		} else {
			_title->resizeToWidth(best);
			_title->moveToLeft(left + titleRect->width() - best, top);
		}
	} else if (_title) {
		_title.destroy();
	}
}

void GroupPanel::paint(QRect clip) {
	Painter p(widget());

	auto region = QRegion(clip);
	for (const auto rect : region) {
		p.fillRect(rect, st::groupCallBg);
	}
}

bool GroupPanel::handleClose() {
	if (_call) {
		_window->hide();
		return true;
	}
	return false;
}

not_null<Ui::RpWidget*> GroupPanel::widget() const {
	return _window->body();
}

} // namespace Calls
