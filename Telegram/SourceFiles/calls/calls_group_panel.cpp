/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_group_panel.h"

#include "calls/calls_group_common.h"
#include "calls/calls_group_members.h"
#include "calls/calls_group_settings.h"
#include "calls/calls_group_menu.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/window.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/call_mute_button.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/input_fields.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "info/profile/info_profile_values.h" // Info::Profile::Value.
#include "core/application.h"
#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "main/main_session.h"
#include "base/event_filter.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peer_lists_box.h"
#include "boxes/confirm_box.h"
#include "app.h"
#include "apiwrap.h" // api().kickParticipant.
#include "styles/style_calls.h"
#include "styles/style_layers.h"

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>

namespace Calls {
namespace {

constexpr auto kSpacePushToTalkDelay = crl::time(250);

class InviteController final : public ParticipantsBoxController {
public:
	InviteController(
		not_null<PeerData*> peer,
		base::flat_set<not_null<UserData*>> alreadyIn);

	void prepare() override;

	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

	void itemDeselectedHook(not_null<PeerData*> peer) override;

	[[nodiscard]] auto peersWithRows() const
		-> not_null<const base::flat_set<not_null<UserData*>>*>;
	[[nodiscard]] rpl::producer<not_null<UserData*>> rowAdded() const;

	[[nodiscard]] bool hasRowFor(not_null<PeerData*> peer) const;

private:
	[[nodiscard]] bool isAlreadyIn(not_null<UserData*> user) const;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) const override;

	not_null<PeerData*> _peer;
	const base::flat_set<not_null<UserData*>> _alreadyIn;
	mutable base::flat_set<not_null<UserData*>> _inGroup;
	rpl::event_stream<not_null<UserData*>> _rowAdded;

};

class InviteContactsController final : public AddParticipantsBoxController {
public:
	InviteContactsController(
		not_null<PeerData*> peer,
		base::flat_set<not_null<UserData*>> alreadyIn,
		not_null<const base::flat_set<not_null<UserData*>>*> inGroup,
		rpl::producer<not_null<UserData*>> discoveredInGroup);

private:
	void prepareViewHook() override;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

	bool needsInviteLinkButton() override {
		return false;
	}

	const not_null<const base::flat_set<not_null<UserData*>>*> _inGroup;
	rpl::producer<not_null<UserData*>> _discoveredInGroup;

	rpl::lifetime _lifetime;

};

[[nodiscard]] object_ptr<Ui::RpWidget> CreateSectionSubtitle(
		QWidget *parent,
		rpl::producer<QString> text) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::searchedBarHeight);

	const auto raw = result.data();
	raw->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(raw);
		p.fillRect(clip, st::groupCallMembersBgOver);
	}, raw->lifetime());

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		raw,
		std::move(text),
		st::groupCallBoxLabel);
	raw->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto padding = st::groupCallInviteDividerPadding;
		const auto available = width - padding.left() - padding.right();
		label->resizeToNaturalWidth(available);
		label->moveToLeft(padding.left(), padding.top(), width);
	}, label->lifetime());

	return result;
}

InviteController::InviteController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> alreadyIn)
: ParticipantsBoxController(CreateTag{}, nullptr, peer, Role::Members)
, _peer(peer)
, _alreadyIn(std::move(alreadyIn)) {
	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { _peer = channel; });
}

void InviteController::prepare() {
	delegate()->peerListSetHideEmpty(true);
	ParticipantsBoxController::prepare();
	delegate()->peerListSetAboveWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_members()));
	delegate()->peerListSetAboveSearchWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_members()));
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

bool InviteController::hasRowFor(not_null<PeerData*> peer) const {
	return (delegate()->peerListFindRow(peer->id) != nullptr);
}

bool InviteController::isAlreadyIn(not_null<UserData*> user) const {
	return _alreadyIn.contains(user);
}

std::unique_ptr<PeerListRow> InviteController::createRow(
		not_null<UserData*> user) const {
	if (user->isSelf() || user->isBot()) {
		return nullptr;
	}
	auto result = std::make_unique<PeerListRow>(user);
	_rowAdded.fire_copy(user);
	_inGroup.emplace(user);
	if (isAlreadyIn(user)) {
		result->setDisabledState(PeerListRow::State::DisabledChecked);
	}
	return result;
}

auto InviteController::peersWithRows() const
-> not_null<const base::flat_set<not_null<UserData*>>*> {
	return &_inGroup;
}

rpl::producer<not_null<UserData*>> InviteController::rowAdded() const {
	return _rowAdded.events();
}

InviteContactsController::InviteContactsController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> alreadyIn,
	not_null<const base::flat_set<not_null<UserData*>>*> inGroup,
	rpl::producer<not_null<UserData*>> discoveredInGroup)
: AddParticipantsBoxController(peer, std::move(alreadyIn))
, _inGroup(inGroup)
, _discoveredInGroup(std::move(discoveredInGroup)) {
}

void InviteContactsController::prepareViewHook() {
	AddParticipantsBoxController::prepareViewHook();

	delegate()->peerListSetAboveWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_contacts_header()));
	delegate()->peerListSetAboveSearchWidget(CreateSectionSubtitle(
		nullptr,
		tr::lng_group_call_invite_search_results()));

	std::move(
		_discoveredInGroup
	) | rpl::start_with_next([=](not_null<UserData*> user) {
		if (auto row = delegate()->peerListFindRow(user->id)) {
			delegate()->peerListRemoveRow(row);
		}
	}, _lifetime);
}

std::unique_ptr<PeerListRow> InviteContactsController::createRow(
		not_null<UserData*> user) {
	return _inGroup->contains(user)
		? nullptr
		: AddParticipantsBoxController::createRow(user);
}

} // namespace

GroupPanel::GroupPanel(not_null<GroupCall*> call)
: _call(call)
, _peer(call->peer())
, _window(std::make_unique<Ui::Window>(Core::App().getModalParent()))
, _layerBg(std::make_unique<Ui::LayerManager>(_window->body()))
#ifndef Q_OS_MAC
, _controls(std::make_unique<Ui::Platform::TitleControls>(
	_window->body(),
	st::groupCallTitle))
#endif // !Q_OS_MAC
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
	_layerBg->setHideByBackgroundClick(true);

	SubscribeToMigration(
		_peer,
		_window->lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(channel); });
	setupRealCallViewers(call);

	initWindow();
	initWidget();
	initControls();
	initLayout();
	showAndActivate();
}

GroupPanel::~GroupPanel() = default;

void GroupPanel::setupRealCallViewers(not_null<GroupCall*> call) {
	const auto peer = call->peer();
	peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::GroupCall
	) | rpl::map([=] {
		return peer->groupCall();
	}) | rpl::filter([=](Data::GroupCall *real) {
		return _call && real && (real->id() == _call->id());
	}) | rpl::take(
		1
	) | rpl::start_with_next([=](not_null<Data::GroupCall*> real) {
		subscribeToChanges(real);
	}, _window->lifetime());
}

bool GroupPanel::isActive() const {
	return _window->isActiveWindow()
		&& _window->isVisible()
		&& !(_window->windowState() & Qt::WindowMinimized);
}

void GroupPanel::minimize() {
	_window->setWindowState(_window->windowState() | Qt::WindowMinimized);
}

void GroupPanel::close() {
	_window->close();
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
	_window->setTitleStyle(st::groupCallTitle);

	subscribeToPeerChanges();

	base::install_event_filter(_window.get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close && handleClose()) {
			e->ignore();
			return base::EventFilterResult::Cancel;
		} else if (e->type() == QEvent::KeyPress
			|| e->type() == QEvent::KeyRelease) {
			if (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Space) {
				if (_call) {
					_call->pushToTalk(
						e->type() == QEvent::KeyPress,
						kSpacePushToTalkDelay);
				}
			}
		}
		return base::EventFilterResult::Continue;
	});

	_window->setBodyTitleArea([=](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		const auto titleRect = QRect(
			0,
			0,
			widget()->width(),
			st::groupCallMembersTop);
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

void GroupPanel::endCall() {
	if (!_call) {
		return;
	} else if (!_call->peer()->canManageGroupCall()) {
		_call->hangup();
		return;
	}
	_layerBg->showBox(Box(
		Group::LeaveBox,
		_call,
		false,
		Group::BoxContext::GroupCallPanel));
}

void GroupPanel::initControls() {
	_mute->clicks(
	) | rpl::filter([=](Qt::MouseButton button) {
		return (button == Qt::LeftButton) && (_call != nullptr);
	}) | rpl::start_with_next([=] {
		const auto oldState = _call->muted();
		const auto newState = (oldState == MuteState::ForceMuted)
			? MuteState::RaisedHand
			: (oldState == MuteState::RaisedHand)
			? MuteState::ForceMuted
			: (oldState == MuteState::Muted)
			? MuteState::Active
			: MuteState::Muted;
		_call->setMutedAndUpdate(newState);
	}, _mute->lifetime());

	_hangup->setClickedCallback([=] { endCall(); });
	_settings->setClickedCallback([=] {
		if (_call) {
			_layerBg->showBox(Box(Group::SettingsBox, _call));
		}
	});

	_settings->setText(tr::lng_menu_settings());
	_hangup->setText(tr::lng_group_call_leave());

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
		return update.me;
	}) | rpl::start_with_next([=](const LevelUpdate &update) {
		_mute->setLevel(update.value);
	}, _callLifetime);

	_members->toggleMuteRequests(
	) | rpl::start_with_next([=](Group::MuteRequest request) {
		if (_call) {
			_call->toggleMute(request);
		}
	}, _callLifetime);

	_members->changeVolumeRequests(
	) | rpl::start_with_next([=](Group::VolumeRequest request) {
		if (_call) {
			_call->changeVolume(request);
		}
	}, _callLifetime);

	_members->kickParticipantRequests(
	) | rpl::start_with_next([=](not_null<PeerData*> participantPeer) {
		if (const auto user = participantPeer->asUser()) {
			kickMember(user); // #TODO calls kick
		}
	}, _callLifetime);

	_members->addMembersRequests(
	) | rpl::start_with_next([=] {
		if (_call) {
			addMembers();
		}
	}, _callLifetime);

	rpl::combine(
		_call->mutedValue() | MapPushToTalkToActive(),
		_call->connectingValue()
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](MuteState mute, bool connecting) {
		_mute->setState(Ui::CallMuteButtonState{
			.text = (connecting
				? tr::lng_group_call_connecting(tr::now)
				: mute == MuteState::ForceMuted
				? tr::lng_group_call_force_muted(tr::now)
				: mute == MuteState::RaisedHand
				? tr::lng_group_call_raised_hand(tr::now)
				: mute == MuteState::Muted
				? tr::lng_group_call_unmute(tr::now)
				: tr::lng_group_call_you_are_live(tr::now)),
			.subtext = (connecting
				? QString()
				: mute == MuteState::ForceMuted
				? tr::lng_group_call_raise_hand_tip(tr::now)
				: mute == MuteState::RaisedHand
				? tr::lng_group_call_raised_hand_sub(tr::now)
				: mute == MuteState::Muted
				? tr::lng_group_call_unmute_sub(tr::now)
				: QString()),
			.type = (connecting
				? Ui::CallMuteButtonType::Connecting
				: mute == MuteState::ForceMuted
				? Ui::CallMuteButtonType::ForceMuted
				: mute == MuteState::RaisedHand
				? Ui::CallMuteButtonType::RaisedHand
				: mute == MuteState::Muted
				? Ui::CallMuteButtonType::Muted
				: Ui::CallMuteButtonType::Active),
		});
	}, _callLifetime);
}

void GroupPanel::subscribeToChanges(not_null<Data::GroupCall*> real) {
	_titleText = real->titleValue();

	const auto validateRecordingMark = [=](bool recording) {
		if (!recording && _recordingMark) {
			_recordingMark.destroy();
		} else if (recording && !_recordingMark) {
			_recordingMark.create(widget());
			_recordingMark->show();
			const auto size = st::groupCallRecordingMark;
			const auto skip = st::groupCallRecordingMarkSkip;
			_recordingMark->resize(size + 2 * skip, size + 2 * skip);
			_recordingMark->setClickedCallback([=] {
				Ui::Toast::Show(
					widget(),
					tr::lng_group_call_is_recorded(tr::now));
			});
			_recordingMark->paintRequest(
			) | rpl::start_with_next([=] {
				auto p = QPainter(_recordingMark.data());
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::groupCallMemberMutedIcon);
				p.drawEllipse(skip, skip, size, size);
			}, _recordingMark->lifetime());
		}
		refreshTitleGeometry();
	};

	using namespace rpl::mappers;
	real->recordStartDateChanges(
	) | rpl::map(
		_1 != 0
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool recorded) {
		validateRecordingMark(recorded);
		Ui::Toast::Show(
			widget(),
			(recorded
				? tr::lng_group_call_recording_started(tr::now)
				: tr::lng_group_call_recording_stopped(tr::now)));
	}, widget()->lifetime());
	validateRecordingMark(real->recordStartDate() != 0);

	const auto showMenu = _peer->canManageGroupCall();
	const auto showUserpic = !showMenu && _call->showChooseJoinAs();
	if (showMenu) {
		_joinAsToggle.destroy();
		if (!_menuToggle) {
			_menuToggle.create(widget(), st::groupCallMenuToggle);
			_menuToggle->show();
			_menuToggle->setClickedCallback([=] { showMainMenu(); });
		}
	} else if (showUserpic) {
		_menuToggle.destroy();
		rpl::single(
			_call->joinAs()
		) | rpl::then(_call->rejoinEvents(
		) | rpl::map([](const Group::RejoinEvent &event) {
			return event.nowJoinAs;
		})) | rpl::start_with_next([=](not_null<PeerData*> joinAs) {
			auto joinAsToggle = object_ptr<Ui::UserpicButton>(
				widget(),
				joinAs,
				Ui::UserpicButton::Role::Custom,
				st::groupCallJoinAsToggle);
			_joinAsToggle.destroy();
			_joinAsToggle = std::move(joinAsToggle);
			_joinAsToggle->show();
			_joinAsToggle->setClickedCallback([=] {
				chooseJoinAs();
			});
		}, widget()->lifetime());
	} else {
		_menuToggle.destroy();
		_joinAsToggle.destroy();
	}
	updateControlsGeometry();
}

void GroupPanel::chooseJoinAs() {
	const auto context = Group::ChooseJoinAsProcess::Context::Switch;
	const auto callback = [=](Group::JoinInfo info) {
		if (_call) {
			_call->rejoinAs(info);
		}
	};
	const auto showBox = [=](object_ptr<Ui::BoxContent> next) {
		_layerBg->showBox(std::move(next));
	};
	const auto showToast = [=](QString text) {
		Ui::Toast::Show(widget(), text);
	};
	_joinAsProcess.start(
		_peer,
		context,
		showBox,
		showToast,
		callback,
		_call->joinAs());
}

void GroupPanel::showMainMenu() {
	if (_menu || !_call) {
		return;
	}
	_menu.create(widget(), st::groupCallDropdownMenu);
	Group::FillMenu(
		_menu.data(),
		_peer,
		_call,
		[=] { chooseJoinAs(); },
		[=](auto box) { _layerBg->showBox(std::move(box)); });

	const auto raw = _menu.data();
	raw->setHiddenCallback([=] {
		raw->deleteLater();
		if (_menu == raw) {
			_menu = nullptr;
			_menuToggle->setForceRippled(false);
		}
	});
	raw->setShowStartCallback([=] {
		if (_menu == raw) {
			_menuToggle->setForceRippled(true);
		}
	});
	raw->setHideStartCallback([=] {
		if (_menu == raw) {
			_menuToggle->setForceRippled(false);
		}
	});
	_menuToggle->installEventFilter(_menu);

	const auto x = st::groupCallMenuPosition.x();
	const auto y = st::groupCallMenuPosition.y();
	if (_menuToggle->x() > widget()->width() / 2) {
		_menu->moveToRight(x, y);
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
	} else {
		_menu->moveToLeft(x, y);
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopLeft);
	}
}

void GroupPanel::addMembers() {
	const auto real = _peer->groupCall();
	if (!_call || !real || real->id() != _call->id()) {
		return;
	}
	auto alreadyIn = _peer->owner().invitedToCallUsers(real->id());
	for (const auto &participant : real->participants()) {
		if (const auto user = participant.peer->asUser()) {
			alreadyIn.emplace(user);
		}
	}
	alreadyIn.emplace(_peer->session().user());
	auto controller = std::make_unique<InviteController>(
		_peer,
		alreadyIn);
	controller->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);

	auto contactsController = std::make_unique<InviteContactsController>(
		_peer,
		std::move(alreadyIn),
		controller->peersWithRows(),
		controller->rowAdded());
	contactsController->setStyleOverrides(
		&st::groupCallInviteMembersList,
		&st::groupCallMultiSelect);

	const auto weak = base::make_weak(_call);
	const auto invite = [=](const std::vector<not_null<UserData*>> &users) {
		const auto call = weak.get();
		if (!call) {
			return;
		}
		const auto result = call->inviteUsers(users);
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
	};
	const auto inviteWithAdd = [=](
			const std::vector<not_null<UserData*>> &users,
			const std::vector<not_null<UserData*>> &nonMembers,
			Fn<void()> finish) {
		_peer->session().api().addChatParticipants(
			_peer,
			nonMembers,
			[=](bool) { invite(users); finish(); });
	};
	const auto inviteWithConfirmation = [=](
			const std::vector<not_null<UserData*>> &users,
			const std::vector<not_null<UserData*>> &nonMembers,
			Fn<void()> finish) {
		if (nonMembers.empty()) {
			invite(users);
			finish();
			return;
		}
		const auto name = _peer->name;
		const auto text = (nonMembers.size() == 1)
			? tr::lng_group_call_add_to_group_one(
				tr::now,
				lt_user,
				nonMembers.front()->shortName(),
				lt_group,
				name)
			: (nonMembers.size() < users.size())
			? tr::lng_group_call_add_to_group_some(tr::now, lt_group, name)
			: tr::lng_group_call_add_to_group_all(tr::now, lt_group, name);
		const auto shared = std::make_shared<QPointer<Ui::GenericBox>>();
		const auto finishWithConfirm = [=] {
			if (*shared) {
				(*shared)->closeBox();
			}
			finish();
		};
		auto box = Box(
			Group::ConfirmBox,
			text,
			tr::lng_participant_invite(),
			[=] { inviteWithAdd(users, nonMembers, finishWithConfirm); });
		*shared = box.data();
		_layerBg->showBox(std::move(box));
	};
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListsBox*> box) {
		box->setTitle(tr::lng_group_call_invite_title());
		box->addButton(tr::lng_group_call_invite_button(), [=] {
			const auto rows = box->collectSelectedRows();

			const auto users = ranges::view::all(
				rows
			) | ranges::view::transform([](not_null<PeerData*> peer) {
				return not_null<UserData*>(peer->asUser());
			}) | ranges::to_vector;

			const auto nonMembers = ranges::view::all(
				users
			) | ranges::view::filter([&](not_null<UserData*> user) {
				return !controller->hasRowFor(user);
			}) | ranges::to_vector;

			const auto finish = [box = Ui::MakeWeak(box)]() {
				if (box) {
					box->closeBox();
				}
			};
			inviteWithConfirmation(users, nonMembers, finish);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};

	auto controllers = std::vector<std::unique_ptr<PeerListController>>();
	controllers.push_back(std::move(controller));
	controllers.push_back(std::move(contactsController));
	_layerBg->showBox(Box<PeerListsBox>(std::move(controllers), initBox));
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

#ifndef Q_OS_MAC
	_controls->raise();
#endif // !Q_OS_MAC
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

QRect GroupPanel::computeTitleRect() const {
	const auto skip = st::groupCallTitleTop;
	const auto remove = skip + (_menuToggle
		? (_menuToggle->width() + st::groupCallMenuTogglePosition.x())
		: 0) + (_joinAsToggle
			? (_joinAsToggle->width() + st::groupCallMenuTogglePosition.x())
			: 0);
	const auto width = widget()->width();
#ifdef Q_OS_MAC
	return QRect(70, 0, width - skip - 70, 28);
#else // Q_OS_MAC
	const auto controls = _controls->geometry();
	const auto right = controls.x() + controls.width() + skip;
	return (controls.center().x() < width / 2)
		? QRect(right, 0, width - right - remove, controls.height())
		: QRect(remove, 0, controls.x() - skip - remove, controls.height());
#endif // !Q_OS_MAC
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
	const auto membersTop = st::groupCallMembersTop;
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

#ifdef Q_OS_MAC
	const auto controlsOnTheLeft = true;
#else // Q_OS_MAC
	const auto controlsOnTheLeft = _controls->geometry().center().x()
		< widget()->width() / 2;
#endif // Q_OS_MAC
	const auto menux = st::groupCallMenuTogglePosition.x();
	const auto menuy = st::groupCallMenuTogglePosition.y();
	if (controlsOnTheLeft) {
		if (_menuToggle) {
			_menuToggle->moveToRight(menux, menuy);
		} else if (_joinAsToggle) {
			_joinAsToggle->moveToRight(menux, menuy);
		}
	} else {
		if (_menuToggle) {
			_menuToggle->moveToLeft(menux, menuy);
		} else if (_joinAsToggle) {
			_joinAsToggle->moveToLeft(menux, menuy);
		}
	}
}

void GroupPanel::refreshTitle() {
	if (!_title) {
		auto text = rpl::combine(
			Info::Profile::NameValue(_peer),
			_titleText.value()
		) | rpl::map([=](
				const TextWithEntities &name,
				const QString &title) {
			return title.isEmpty() ? name.text : title;
		}) | rpl::after_next([=] {
			refreshTitleGeometry();
		});
		_title.create(
			widget(),
			rpl::duplicate(text),
			st::groupCallTitleLabel);
		_title->show();
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	refreshTitleGeometry();
	if (!_subtitle) {
		_subtitle.create(
			widget(),
			tr::lng_group_call_members(
				lt_count_decimal,
				_members->fullCountValue() | tr::to_count()),
			st::groupCallSubtitleLabel);
		_subtitle->show();
		_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	const auto middle = _title
		? (_title->x() + _title->width() / 2)
		: (widget()->width() / 2);
	const auto top = _title
		? st::groupCallSubtitleTop
		: st::groupCallTitleTop;
	_subtitle->moveToLeft(
		(widget()->width() - _subtitle->width()) / 2,
		top);
}

void GroupPanel::refreshTitleGeometry() {
	if (!_title) {
		return;
	}
	const auto fullRect = computeTitleRect();
	const auto recordingWidth = 2 * st::groupCallRecordingMarkSkip
		+ st::groupCallRecordingMark;
	const auto titleRect = _recordingMark
		? QRect(
			fullRect.x(),
			fullRect.y(),
			fullRect.width() - _recordingMark->width(),
			fullRect.height())
		: fullRect;
	const auto best = _title->naturalWidth();
	const auto from = (widget()->width() - best) / 2;
	const auto top = st::groupCallTitleTop;
	const auto left = titleRect.x();
	if (from >= left && from + best <= left + titleRect.width()) {
		_title->resizeToWidth(best);
		_title->moveToLeft(from, top);
	} else if (titleRect.width() < best) {
		_title->resizeToWidth(titleRect.width());
		_title->moveToLeft(left, top);
	} else if (from < left) {
		_title->resizeToWidth(best);
		_title->moveToLeft(left, top);
	} else {
		_title->resizeToWidth(best);
		_title->moveToLeft(left + titleRect.width() - best, top);
	}
	if (_recordingMark) {
		const auto markTop = top + st::groupCallRecordingMarkTop;
		_recordingMark->move(
			_title->x() + _title->width(),
			markTop - st::groupCallRecordingMarkSkip);
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
