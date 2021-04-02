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
#include "ui/platform/ui_platform_utility.h"
#include "ui/controls/call_mute_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/window.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/input_fields.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toasts/common_toasts.h"
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

namespace Calls::Group {
namespace {

constexpr auto kSpacePushToTalkDelay = crl::time(250);
constexpr auto kRecordingAnimationDuration = crl::time(1200);
constexpr auto kRecordingOpacity = 0.6;

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
		not_null<PeerData*> participant) const override;

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
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (!user || user->isSelf() || user->isBot()) {
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

Panel::Panel(not_null<GroupCall*> call)
: _call(call)
, _peer(call->peer())
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
	setupJoinAsChangedToasts();
	setupTitleChangedToasts();

	call->allowedToSpeakNotifications(
	) | rpl::start_with_next([=] {
		if (isActive()) {
			Ui::ShowMultilineToast({
				.parentOverride = widget(),
				.text = { tr::lng_group_call_can_speak_here(tr::now) },
			});
		} else {
			const auto real = _peer->groupCall();
			const auto name = (real
				&& (real->id() == call->id())
				&& !real->title().isEmpty())
				? real->title()
				: _peer->name;
			Ui::ShowMultilineToast({
				.text = tr::lng_group_call_can_speak(
					tr::now,
					lt_chat,
					Ui::Text::Bold(name),
					Ui::Text::WithEntities),
			});
		}
	}, widget()->lifetime());
}

Panel::~Panel() {
	if (_menu) {
		_menu.destroy();
	}
}

void Panel::setupRealCallViewers(not_null<GroupCall*> call) {
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

bool Panel::isActive() const {
	return _window->isActiveWindow()
		&& _window->isVisible()
		&& !(_window->windowState() & Qt::WindowMinimized);
}

void Panel::minimize() {
	_window->setWindowState(_window->windowState() | Qt::WindowMinimized);
}

void Panel::close() {
	_window->close();
}

void Panel::showAndActivate() {
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

void Panel::migrate(not_null<ChannelData*> channel) {
	_peer = channel;
	_peerLifetime.destroy();
	subscribeToPeerChanges();
	_title.destroy();
	refreshTitle();
}

void Panel::subscribeToPeerChanges() {
	Info::Profile::NameValue(
		_peer
	) | rpl::start_with_next([=](const TextWithEntities &name) {
		_window->setTitle(name.text);
	}, _peerLifetime);
}

void Panel::initWindow() {
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
		return (titleRect.contains(widgetPoint)
			&& (!_menuToggle || !_menuToggle->geometry().contains(widgetPoint))
			&& (!_menu || !_menu->geometry().contains(widgetPoint))
			&& (!_recordingMark || !_recordingMark->geometry().contains(widgetPoint))
			&& (!_joinAsToggle || !_joinAsToggle->geometry().contains(widgetPoint)))
			? (Flag::Move | Flag::Maximize)
			: Flag::None;
	});
}

void Panel::initWidget() {
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

void Panel::endCall() {
	if (!_call) {
		return;
	} else if (!_call->peer()->canManageGroupCall()) {
		_call->hangup();
		return;
	}
	_layerBg->showBox(Box(
		LeaveBox,
		_call,
		false,
		BoxContext::GroupCallPanel));
}

void Panel::initControls() {
	_mute->clicks(
	) | rpl::filter([=](Qt::MouseButton button) {
		return (button == Qt::LeftButton) && (_call != nullptr);
	}) | rpl::start_with_next([=] {
		const auto oldState = _call->muted();
		const auto newState = (oldState == MuteState::ForceMuted)
			? MuteState::RaisedHand
			: (oldState == MuteState::RaisedHand)
			? MuteState::RaisedHand
			: (oldState == MuteState::Muted)
			? MuteState::Active
			: MuteState::Muted;
		_call->setMutedAndUpdate(newState);
	}, _mute->lifetime());

	_hangup->setClickedCallback([=] { endCall(); });
	_settings->setClickedCallback([=] {
		if (_call) {
			_layerBg->showBox(Box(SettingsBox, _call));
		}
	});

	_settings->setText(tr::lng_group_call_settings());
	_hangup->setText(tr::lng_group_call_leave());

	_members->desiredHeightValue(
	) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, _members->lifetime());

	initWithCall(_call);
}

void Panel::initWithCall(GroupCall *call) {
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
	) | rpl::start_with_next([=](MuteRequest request) {
		if (_call) {
			_call->toggleMute(request);
		}
	}, _callLifetime);

	_members->changeVolumeRequests(
	) | rpl::start_with_next([=](VolumeRequest request) {
		if (_call) {
			_call->changeVolume(request);
		}
	}, _callLifetime);

	_members->kickParticipantRequests(
	) | rpl::start_with_next([=](not_null<PeerData*> participantPeer) {
		kickParticipant(participantPeer);
	}, _callLifetime);

	const auto showBox = [=](object_ptr<Ui::BoxContent> next) {
		_layerBg->showBox(std::move(next));
	};
	const auto showToast = [=](QString text) {
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = { text },
		});
	};
	auto [shareLinkCallback, shareLinkLifetime] = ShareInviteLinkAction(
		_peer,
		showBox,
		showToast);
	auto shareLink = std::move(shareLinkCallback);
	_members->lifetime().add(std::move(shareLinkLifetime));

	_members->addMembersRequests(
	) | rpl::start_with_next([=] {
		if (_call) {
			if (_peer->isBroadcast() && _peer->asChannel()->hasUsername()) {
				shareLink();
			} else {
				addMembers();
			}
		}
	}, _callLifetime);

	using namespace rpl::mappers;
	rpl::combine(
		_call->mutedValue() | MapPushToTalkToActive(),
		_call->instanceStateValue()
	) | rpl::distinct_until_changed(
	) | rpl::filter(
		_2 != GroupCall::InstanceState::TransitionToRtc
	) | rpl::start_with_next([=](
			MuteState mute,
			GroupCall::InstanceState state) {
		_mute->setState(Ui::CallMuteButtonState{
			.text = (state == GroupCall::InstanceState::Disconnected
				? tr::lng_group_call_connecting(tr::now)
				: mute == MuteState::ForceMuted
				? tr::lng_group_call_force_muted(tr::now)
				: mute == MuteState::RaisedHand
				? tr::lng_group_call_raised_hand(tr::now)
				: mute == MuteState::Muted
				? tr::lng_group_call_unmute(tr::now)
				: tr::lng_group_call_you_are_live(tr::now)),
			.subtext = (state == GroupCall::InstanceState::Disconnected
				? QString()
				: mute == MuteState::ForceMuted
				? tr::lng_group_call_raise_hand_tip(tr::now)
				: mute == MuteState::RaisedHand
				? tr::lng_group_call_raised_hand_sub(tr::now)
				: mute == MuteState::Muted
				? tr::lng_group_call_unmute_sub(tr::now)
				: QString()),
			.type = (state == GroupCall::InstanceState::Disconnected
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

void Panel::setupJoinAsChangedToasts() {
	_call->rejoinEvents(
	) | rpl::filter([](RejoinEvent event) {
		return (event.wasJoinAs != event.nowJoinAs);
	}) | rpl::map([=] {
		return _call->stateValue() | rpl::filter([](State state) {
			return (state == State::Joined);
		}) | rpl::take(1);
	}) | rpl::flatten_latest() | rpl::start_with_next([=] {
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = tr::lng_group_call_join_as_changed(
				tr::now,
				lt_name,
				Ui::Text::Bold(_call->joinAs()->name),
				Ui::Text::WithEntities),
		});
	}, widget()->lifetime());
}

void Panel::setupTitleChangedToasts() {
	_call->titleChanged(
	) | rpl::filter([=] {
		return _peer->groupCall() && _peer->groupCall()->id() == _call->id();
	}) | rpl::map([=] {
		return _peer->groupCall()->title().isEmpty()
			? _peer->name
			: _peer->groupCall()->title();
	}) | rpl::start_with_next([=](const QString &title) {
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = tr::lng_group_call_title_changed(
				tr::now,
				lt_title,
				Ui::Text::Bold(title),
				Ui::Text::WithEntities),
		});
	}, widget()->lifetime());
}

void Panel::subscribeToChanges(not_null<Data::GroupCall*> real) {
	_titleText = real->titleValue();

	const auto validateRecordingMark = [=](bool recording) {
		if (!recording && _recordingMark) {
			_recordingMark.destroy();
		} else if (recording && !_recordingMark) {
			struct State {
				Ui::Animations::Simple animation;
				base::Timer timer;
				bool opaque = true;
			};
			_recordingMark.create(widget());
			_recordingMark->show();
			const auto state = _recordingMark->lifetime().make_state<State>();
			const auto size = st::groupCallRecordingMark;
			const auto skip = st::groupCallRecordingMarkSkip;
			_recordingMark->resize(size + 2 * skip, size + 2 * skip);
			_recordingMark->setClickedCallback([=] {
				Ui::ShowMultilineToast({
					.parentOverride = widget(),
					.text = { tr::lng_group_call_is_recorded(tr::now) },
				});
			});
			const auto animate = [=] {
				const auto opaque = state->opaque;
				state->opaque = !opaque;
				state->animation.start(
					[=] { _recordingMark->update(); },
					opaque ? 1. : kRecordingOpacity,
					opaque ? kRecordingOpacity : 1.,
					kRecordingAnimationDuration);
			};
			state->timer.setCallback(animate);
			state->timer.callEach(kRecordingAnimationDuration);
			animate();

			_recordingMark->paintRequest(
			) | rpl::start_with_next([=] {
				auto p = QPainter(_recordingMark.data());
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::groupCallMemberMutedIcon);
				p.setOpacity(state->animation.value(
					state->opaque ? 1. : kRecordingOpacity));
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
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = (recorded
				? tr::lng_group_call_recording_started
				: (_call && _call->recordingStoppedByMe())
				? tr::lng_group_call_recording_saved
				: tr::lng_group_call_recording_stopped)(
					tr::now,
					Ui::Text::RichLangValue),
		});
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
		) | rpl::map([](const RejoinEvent &event) {
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
			updateControlsGeometry();
		}, widget()->lifetime());
	} else {
		_menuToggle.destroy();
		_joinAsToggle.destroy();
	}
	updateControlsGeometry();
}

void Panel::chooseJoinAs() {
	const auto context = ChooseJoinAsProcess::Context::Switch;
	const auto callback = [=](JoinInfo info) {
		if (_call) {
			_call->rejoinAs(info);
		}
	};
	const auto showBox = [=](object_ptr<Ui::BoxContent> next) {
		_layerBg->showBox(std::move(next));
	};
	const auto showToast = [=](QString text) {
		Ui::ShowMultilineToast({
			.parentOverride = widget(),
			.text = { text },
		});
	};
	_joinAsProcess.start(
		_peer,
		context,
		showBox,
		showToast,
		callback,
		_call->joinAs());
}

void Panel::showMainMenu() {
	if (_menu || !_call) {
		return;
	}
	_menu.create(widget(), st::groupCallDropdownMenu);
	FillMenu(
		_menu.data(),
		_peer,
		_call,
		[=] { chooseJoinAs(); },
		[=](auto box) { _layerBg->showBox(std::move(box)); });
	if (_menu->empty()) {
		_menu.destroy();
		return;
	}

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

void Panel::addMembers() {
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
			Ui::ShowMultilineToast({
				.parentOverride = widget(),
				.text = tr::lng_group_call_invite_done_user(
					tr::now,
					lt_user,
					Ui::Text::Bold((*user)->firstName),
					Ui::Text::WithEntities),
			});
		} else if (const auto count = std::get_if<int>(&result)) {
			if (*count > 0) {
				Ui::ShowMultilineToast({
					.parentOverride = widget(),
					.text = tr::lng_group_call_invite_done_many(
						tr::now,
						lt_count,
						*count,
						Ui::Text::RichLangValue),
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
			ConfirmBox,
			TextWithEntities{ text },
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

			const auto users = ranges::views::all(
				rows
			) | ranges::views::transform([](not_null<PeerData*> peer) {
				return not_null<UserData*>(peer->asUser());
			}) | ranges::to_vector;

			const auto nonMembers = ranges::views::all(
				users
			) | ranges::views::filter([&](not_null<UserData*> user) {
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

void Panel::kickParticipant(not_null<PeerData*> participantPeer) {
	_layerBg->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box.get(),
				(!participantPeer->isUser()
					? tr::lng_group_call_remove_channel(
						tr::now,
						lt_channel,
						participantPeer->name)
					: (_peer->isBroadcast()
						? tr::lng_profile_sure_kick_channel
						: tr::lng_profile_sure_kick)(
						tr::now,
						lt_user,
						participantPeer->asUser()->firstName)),
				st::groupCallBoxLabel),
			style::margins(
				st::boxRowPadding.left(),
				st::boxPadding.top(),
				st::boxRowPadding.right(),
				st::boxPadding.bottom()));
		box->addButton(tr::lng_box_remove(), [=] {
			box->closeBox();
			kickParticipantSure(participantPeer);
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void Panel::kickParticipantSure(not_null<PeerData*> participantPeer) {
	if (const auto chat = _peer->asChat()) {
		chat->session().api().kickParticipant(chat, participantPeer);
	} else if (const auto channel = _peer->asChannel()) {
		const auto currentRestrictedRights = [&] {
			const auto user = participantPeer->asUser();
			if (!channel->mgInfo || !user) {
				return ChannelData::EmptyRestrictedRights(participantPeer);
			}
			const auto i = channel->mgInfo->lastRestricted.find(user);
			return (i != channel->mgInfo->lastRestricted.cend())
				? i->second.rights
				: ChannelData::EmptyRestrictedRights(participantPeer);
		}();
		channel->session().api().kickParticipant(
			channel,
			participantPeer,
			currentRestrictedRights);
	}
}

void Panel::initLayout() {
	initGeometry();

#ifndef Q_OS_MAC
	_controls->raise();

	Ui::Platform::TitleControlsLayoutChanged(
	) | rpl::start_with_next([=] {
		// _menuToggle geometry depends on _controls arrangement.
		crl::on_main(widget(), [=] { updateControlsGeometry(); });
	}, widget()->lifetime());

#endif // !Q_OS_MAC
}

void Panel::showControls() {
	Expects(_call != nullptr);

	widget()->showChildren();
}

void Panel::closeBeforeDestroy() {
	_window->close();
	initWithCall(nullptr);
}

void Panel::initGeometry() {
	const auto center = Core::App().getPointForCallPanelCenter();
	const auto rect = QRect(0, 0, st::groupCallWidth, st::groupCallHeight);
	_window->setGeometry(rect.translated(center - rect.center()));
	_window->setMinimumSize(rect.size());
	_window->show();
	updateControlsGeometry();
}

QRect Panel::computeTitleRect() const {
	const auto skip = st::groupCallTitleTop;
	const auto remove = skip + (_menuToggle
		? (_menuToggle->width() + st::groupCallMenuTogglePosition.x())
		: 0) + (_joinAsToggle
			? (_joinAsToggle->width() + st::groupCallMenuTogglePosition.x())
			: 0);
	const auto width = widget()->width();
#ifdef Q_OS_MAC
	return QRect(70, 0, width - remove - 70, 28);
#else // Q_OS_MAC
	const auto controls = _controls->geometry();
	const auto right = controls.x() + controls.width() + skip;
	return (controls.center().x() < width / 2)
		? QRect(right, 0, width - right - remove, controls.height())
		: QRect(remove, 0, controls.x() - skip - remove, controls.height());
#endif // !Q_OS_MAC
}

void Panel::updateControlsGeometry() {
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

void Panel::refreshTitle() {
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
				_members->fullCountValue() | rpl::map([](int value) {
					return (value > 0) ? float64(value) : 1.;
				})),
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

void Panel::refreshTitleGeometry() {
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

void Panel::paint(QRect clip) {
	Painter p(widget());

	auto region = QRegion(clip);
	for (const auto rect : region) {
		p.fillRect(rect, st::groupCallBg);
	}
}

bool Panel::handleClose() {
	if (_call) {
		_window->hide();
		return true;
	}
	return false;
}

not_null<Ui::RpWidget*> Panel::widget() const {
	return _window->body();
}

} // namespace Calls::Group
