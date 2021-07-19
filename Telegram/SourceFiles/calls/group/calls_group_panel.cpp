/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_panel.h"

#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_members.h"
#include "calls/group/calls_group_settings.h"
#include "calls/group/calls_group_menu.h"
#include "calls/group/calls_group_viewport.h"
#include "calls/group/calls_group_toasts.h"
#include "calls/group/calls_group_invite_controller.h"
#include "calls/group/ui/calls_group_scheduled_labels.h"
#include "calls/group/ui/desktop_capture_choose_source.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/controls/call_mute_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/tooltip.h"
#include "ui/widgets/window.h"
#include "ui/chat/group_call_bar.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/toasts/common_toasts.h"
#include "ui/round_rect.h"
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
#include "base/unixtime.h"
#include "base/qt_signal_producer.h"
#include "base/timer_rpl.h"
#include "app.h"
#include "apiwrap.h" // api().kickParticipant.
#include "webrtc/webrtc_video_track.h"
#include "webrtc/webrtc_media_devices.h" // UniqueDesktopCaptureSource.
#include "webrtc/webrtc_audio_input_tester.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>
#include <QtGui/QScreen>

namespace Calls::Group {
namespace {

constexpr auto kSpacePushToTalkDelay = crl::time(250);
constexpr auto kRecordingAnimationDuration = crl::time(1200);
constexpr auto kRecordingOpacity = 0.6;
constexpr auto kStartNoConfirmation = TimeId(10);
constexpr auto kControlsBackgroundOpacity = 0.8;
constexpr auto kOverrideActiveColorBgAlpha = 172;
constexpr auto kMicrophoneTooltipAfterLoudCount = 3;
constexpr auto kDropLoudAfterQuietCount = 5;
constexpr auto kMicrophoneTooltipLevelThreshold = 0.2;
constexpr auto kMicrophoneTooltipCheckInterval = crl::time(500);

} // namespace

struct Panel::ControlsBackgroundNarrow {
	explicit ControlsBackgroundNarrow(not_null<QWidget*> parent)
	: shadow(parent)
	, blocker(parent) {
	}

	Ui::RpWidget shadow;
	Ui::RpWidget blocker;
};

class Panel::MicLevelTester final {
public:
	explicit MicLevelTester(Fn<void()> show);

	[[nodiscard]] bool showTooltip() const;

private:
	void check();

	Fn<void()> _show;
	base::Timer _timer;
	Webrtc::AudioInputTester _tester;
	int _loudCount = 0;
	int _quietCount = 0;

};

Panel::MicLevelTester::MicLevelTester(Fn<void()> show)
: _show(std::move(show))
, _timer([=] { check(); })
, _tester(
		Core::App().settings().callAudioBackend(),
		Core::App().settings().callInputDeviceId()) {
	_timer.callEach(kMicrophoneTooltipCheckInterval);
}

bool Panel::MicLevelTester::showTooltip() const {
	return (_loudCount >= kMicrophoneTooltipAfterLoudCount);
}

void Panel::MicLevelTester::check() {
	const auto level = _tester.getAndResetLevel();
	if (level >= kMicrophoneTooltipLevelThreshold) {
		_quietCount = 0;
		if (++_loudCount >= kMicrophoneTooltipAfterLoudCount) {
			_show();
		}
	} else if (_loudCount > 0 && ++_quietCount >= kDropLoudAfterQuietCount) {
		_quietCount = 0;
		_loudCount = 0;
	}
}

Panel::Panel(not_null<GroupCall*> call)
: _call(call)
, _peer(call->peer())
, _layerBg(std::make_unique<Ui::LayerManager>(widget()))
#ifndef Q_OS_MAC
, _controls(std::make_unique<Ui::Platform::TitleControls>(
	widget(),
	st::groupCallTitle))
#endif // !Q_OS_MAC
, _viewport(
	std::make_unique<Viewport>(widget(), PanelMode::Wide, _window.backend()))
, _mute(std::make_unique<Ui::CallMuteButton>(
	widget(),
	st::callMuteButton,
	Core::App().appDeactivatedValue(),
	Ui::CallMuteButtonState{
		.text = (_call->scheduleDate()
			? tr::lng_group_call_start_now(tr::now)
			: tr::lng_group_call_connecting(tr::now)),
		.type = (!_call->scheduleDate()
			? Ui::CallMuteButtonType::Connecting
			: _peer->canManageGroupCall()
			? Ui::CallMuteButtonType::ScheduledCanStart
			: _call->scheduleStartSubscribed()
			? Ui::CallMuteButtonType::ScheduledNotify
			: Ui::CallMuteButtonType::ScheduledSilent),
	}))
, _hangup(widget(), st::groupCallHangup)
, _stickedTooltipsShown(Core::App().settings().hiddenGroupCallTooltips()
	& ~StickedTooltip::Microphone) // Always show tooltip about mic.
, _toasts(std::make_unique<Toasts>(this)) {
	_layerBg->setStyleOverrides(&st::groupCallBox, &st::groupCallLayerBox);
	_layerBg->setHideByBackgroundClick(true);

	_viewport->widget()->hide();
	if (!_viewport->requireARGB32()) {
		_call->setNotRequireARGB32();
	}

	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(channel); });
	setupRealCallViewers();

	initWindow();
	initWidget();
	initControls();
	initLayout();
	showAndActivate();
}

Panel::~Panel() {
	_menu.destroy();
	_viewport = nullptr;
}

void Panel::setupRealCallViewers() {
	_call->real(
	) | rpl::start_with_next([=](not_null<Data::GroupCall*> real) {
		subscribeToChanges(real);
	}, lifetime());
}

not_null<GroupCall*> Panel::call() const {
	return _call;
}

bool Panel::isActive() const {
	return window()->isActiveWindow()
		&& window()->isVisible()
		&& !(window()->windowState() & Qt::WindowMinimized);
}

void Panel::showToast(TextWithEntities &&text, crl::time duration) {
	if (const auto strong = _lastToast.get()) {
		strong->hideAnimated();
	}
	_lastToast = Ui::ShowMultilineToast({
		.parentOverride = widget(),
		.text = std::move(text),
		.duration = duration,
	});
}

void Panel::minimize() {
	window()->setWindowState(window()->windowState() | Qt::WindowMinimized);
}

void Panel::close() {
	window()->close();
}

void Panel::showAndActivate() {
	if (window()->isHidden()) {
		window()->show();
	}
	const auto state = window()->windowState();
	if (state & Qt::WindowMinimized) {
		window()->setWindowState(state & ~Qt::WindowMinimized);
	}
	window()->raise();
	window()->activateWindow();
	window()->setFocus();
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
		window()->setTitle(name.text);
	}, _peerLifetime);
}

QWidget *Panel::chooseSourceParent() {
	return window().get();
}

QString Panel::chooseSourceActiveDeviceId() {
	return _call->screenSharingDeviceId();
}

bool Panel::chooseSourceActiveWithAudio() {
	return _call->screenSharingWithAudio();
}

bool Panel::chooseSourceWithAudioSupported() {
#ifdef Q_OS_WIN
	return true;
#else // Q_OS_WIN
	return false;
#endif // Q_OS_WIN
}

rpl::lifetime &Panel::chooseSourceInstanceLifetime() {
	return lifetime();
}

void Panel::chooseSourceAccepted(
		const QString &deviceId,
		bool withAudio) {
	_call->toggleScreenSharing(deviceId, withAudio);
}

void Panel::chooseSourceStop() {
	_call->toggleScreenSharing(std::nullopt);
}

void Panel::initWindow() {
	window()->setAttribute(Qt::WA_OpaquePaintEvent);
	window()->setAttribute(Qt::WA_NoSystemBackground);
	window()->setTitleStyle(st::groupCallTitle);

	subscribeToPeerChanges();

	base::install_event_filter(window().get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close && handleClose()) {
			e->ignore();
			return base::EventFilterResult::Cancel;
		} else if (e->type() == QEvent::KeyPress
			|| e->type() == QEvent::KeyRelease) {
			if (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Space) {
				_call->pushToTalk(
					e->type() == QEvent::KeyPress,
					kSpacePushToTalkDelay);
			}
		}
		return base::EventFilterResult::Continue;
	});

	window()->setBodyTitleArea([=](QPoint widgetPoint) {
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

	_call->hasVideoWithFramesValue(
	) | rpl::start_with_next([=] {
		updateMode();
	}, lifetime());
}

void Panel::initWidget() {
	widget()->setMouseTracking(true);

	widget()->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		paint(clip);
	}, lifetime());

	widget()->sizeValue(
	) | rpl::skip(1) | rpl::start_with_next([=](QSize size) {
		if (!updateMode()) {
			updateControlsGeometry();
		}

		// title geometry depends on _controls->geometry,
		// which is not updated here yet.
		crl::on_main(widget(), [=] { refreshTitle(); });
	}, lifetime());
}

void Panel::endCall() {
	if (!_call->canManage()) {
		_call->hangup();
		return;
	}
	_layerBg->showBox(Box(
		LeaveBox,
		_call,
		false,
		BoxContext::GroupCallPanel));
}

void Panel::startScheduledNow() {
	const auto date = _call->scheduleDate();
	const auto now = base::unixtime::now();
	if (!date) {
		return;
	} else if (now + kStartNoConfirmation >= date) {
		_call->startScheduledNow();
	} else {
		const auto box = std::make_shared<QPointer<Ui::GenericBox>>();
		const auto done = [=] {
			if (*box) {
				(*box)->closeBox();
			}
			_call->startScheduledNow();
		};
		auto owned = ConfirmBox({
			.text = { tr::lng_group_call_start_now_sure(tr::now) },
			.button = tr::lng_group_call_start_now(),
			.callback = done,
		});
		*box = owned.data();
		_layerBg->showBox(std::move(owned));
	}
}

void Panel::initControls() {
	_mute->clicks(
	) | rpl::filter([=](Qt::MouseButton button) {
		return (button == Qt::LeftButton);
	}) | rpl::start_with_next([=] {
		if (_call->scheduleDate()) {
			if (_call->canManage()) {
				startScheduledNow();
			} else if (const auto real = _call->lookupReal()) {
				_call->toggleScheduleStartSubscribed(
					!real->scheduleStartSubscribed());
			}
			return;
		}
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

	initShareAction();
	refreshLeftButton();
	refreshVideoButtons();

	rpl::combine(
		_mode.value(),
		_call->canManageValue()
	) | rpl::start_with_next([=] {
		refreshTopButton();
	}, lifetime());

	_hangup->setClickedCallback([=] { endCall(); });

	const auto scheduleDate = _call->scheduleDate();
	if (scheduleDate) {
		auto changes = _call->real(
		) | rpl::map([=](not_null<Data::GroupCall*> real) {
			return real->scheduleDateValue();
		}) | rpl::flatten_latest();

		setupScheduledLabels(rpl::single(
			scheduleDate
		) | rpl::then(rpl::duplicate(changes)));

		auto started = std::move(changes) | rpl::filter([](TimeId date) {
			return (date == 0);
		}) | rpl::take(1);

		rpl::merge(
			rpl::duplicate(started) | rpl::to_empty,
			_peer->session().changes().peerFlagsValue(
				_peer,
				Data::PeerUpdate::Flag::Username
			) | rpl::skip(1) | rpl::to_empty
		) | rpl::start_with_next([=] {
			refreshLeftButton();
			updateControlsGeometry();
		}, _callLifetime);

		std::move(started) | rpl::start_with_next([=] {
			refreshVideoButtons();
			updateButtonsStyles();
			setupMembers();
		}, _callLifetime);
	}

	_call->stateValue(
	) | rpl::before_next([=] {
		showStickedTooltip();
	}) | rpl::filter([](State state) {
		return (state == State::HangingUp)
			|| (state == State::Ended)
			|| (state == State::FailedHangingUp)
			|| (state == State::Failed);
	}) | rpl::start_with_next([=] {
		closeBeforeDestroy();
	}, _callLifetime);

	_call->levelUpdates(
	) | rpl::filter([=](const LevelUpdate &update) {
		return update.me;
	}) | rpl::start_with_next([=](const LevelUpdate &update) {
		_mute->setLevel(update.value);
	}, _callLifetime);

	_call->real(
	) | rpl::start_with_next([=](not_null<Data::GroupCall*> real) {
		setupRealMuteButtonState(real);
	}, _callLifetime);

	refreshControlsBackground();
}

void Panel::refreshLeftButton() {
	const auto share = _call->scheduleDate()
		&& _peer->isBroadcast()
		&& _peer->asChannel()->hasUsername();
	if ((share && _callShare) || (!share && _settings)) {
		return;
	}
	if (share) {
		_settings.destroy();
		_callShare.create(widget(), st::groupCallShare);
		_callShare->setClickedCallback(_callShareLinkCallback);
	} else {
		_callShare.destroy();
		_settings.create(widget(), st::groupCallSettings);
		_settings->setClickedCallback([=] {
			_layerBg->showBox(Box(SettingsBox, _call));
		});
	}
	const auto raw = _callShare ? _callShare.data() : _settings.data();
	raw->show();
	raw->setColorOverrides(_mute->colorOverrides());
	updateButtonsStyles();
}

void Panel::refreshVideoButtons(std::optional<bool> overrideWideMode) {
	const auto create = overrideWideMode.value_or(mode() == PanelMode::Wide)
		|| (!_call->scheduleDate() && _call->videoIsWorking());
	const auto created = _video && _screenShare;
	if (created == create) {
		return;
	} else if (created) {
		_video.destroy();
		_screenShare.destroy();
		if (!overrideWideMode) {
			updateButtonsGeometry();
		}
		return;
	}
	auto toggleableOverrides = [&](rpl::producer<bool> active) {
		return rpl::combine(
			std::move(active),
			_mute->colorOverrides()
		) | rpl::map([](bool active, Ui::CallButtonColors colors) {
			if (active && colors.bg) {
				colors.bg->setAlpha(kOverrideActiveColorBgAlpha);
			}
			return colors;
		});
	};
	if (!_video) {
		_video.create(
			widget(),
			st::groupCallVideoSmall,
			&st::groupCallVideoActiveSmall);
		_video->show();
		_video->setClickedCallback([=] {
			hideStickedTooltip(
				StickedTooltip::Camera,
				StickedTooltipHide::Activated);
			_call->toggleVideo(!_call->isSharingCamera());
		});
		_video->setColorOverrides(
			toggleableOverrides(_call->isSharingCameraValue()));
		_call->isSharingCameraValue(
		) | rpl::start_with_next([=](bool sharing) {
			if (sharing) {
				hideStickedTooltip(
					StickedTooltip::Camera,
					StickedTooltipHide::Activated);
			}
			_video->setProgress(sharing ? 1. : 0.);
		}, _video->lifetime());
	}
	if (!_screenShare) {
		_screenShare.create(widget(), st::groupCallScreenShareSmall);
		_screenShare->show();
		_screenShare->setClickedCallback([=] {
			chooseShareScreenSource();
		});
		_screenShare->setColorOverrides(
			toggleableOverrides(_call->isSharingScreenValue()));
		_call->isSharingScreenValue(
		) | rpl::start_with_next([=](bool sharing) {
			_screenShare->setProgress(sharing ? 1. : 0.);
		}, _screenShare->lifetime());
	}
	if (!_wideMenu) {
		_wideMenu.create(widget(), st::groupCallMenuToggleSmall);
		_wideMenu->show();
		_wideMenu->setClickedCallback([=] { showMainMenu(); });
		_wideMenu->setColorOverrides(
			toggleableOverrides(_wideMenuShown.value()));
	}
	updateButtonsStyles();
	updateButtonsGeometry();
}

void Panel::hideStickedTooltip(StickedTooltipHide hide) {
	if (!_stickedTooltipClose || !_niceTooltipControl) {
		return;
	}
	if (_niceTooltipControl.data() == _video.data()) {
		hideStickedTooltip(StickedTooltip::Camera, hide);
	} else if (_niceTooltipControl.data() == _mute->outer().get()) {
		hideStickedTooltip(StickedTooltip::Microphone, hide);
	}
}

void Panel::hideStickedTooltip(
		StickedTooltip type,
		StickedTooltipHide hide) {
	if (hide != StickedTooltipHide::Unavailable) {
		_stickedTooltipsShown |= type;
		if (hide == StickedTooltipHide::Discarded) {
			Core::App().settings().setHiddenGroupCallTooltip(type);
			Core::App().saveSettingsDelayed();
		}
	}
	const auto control = (type == StickedTooltip::Camera)
		? _video.data()
		: (type == StickedTooltip::Microphone)
		? _mute->outer().get()
		: nullptr;
	if (_niceTooltipControl.data() == control) {
		hideNiceTooltip();
	}
}

void Panel::hideNiceTooltip() {
	if (!_niceTooltip) {
		return;
	}
	_stickedTooltipClose = nullptr;
	_niceTooltip.release()->toggleAnimated(false);
	_niceTooltipControl = nullptr;
}

void Panel::initShareAction() {
	const auto showBoxCallback = [=](object_ptr<Ui::BoxContent> next) {
		_layerBg->showBox(std::move(next));
	};
	const auto showToastCallback = [=](QString text) {
		showToast({ text });
	};
	auto [shareLinkCallback, shareLinkLifetime] = ShareInviteLinkAction(
		_peer,
		showBoxCallback,
		showToastCallback);
	_callShareLinkCallback = [=, callback = std::move(shareLinkCallback)] {
		if (_call->lookupReal()) {
			callback();
		}
	};
	lifetime().add(std::move(shareLinkLifetime));
}

void Panel::setupRealMuteButtonState(not_null<Data::GroupCall*> real) {
	using namespace rpl::mappers;
	rpl::combine(
		_call->mutedValue() | MapPushToTalkToActive(),
		_call->instanceStateValue(),
		real->scheduleDateValue(),
		real->scheduleStartSubscribedValue(),
		_call->canManageValue(),
		_mode.value()
	) | rpl::distinct_until_changed(
	) | rpl::filter(
		_2 != GroupCall::InstanceState::TransitionToRtc
	) | rpl::start_with_next([=](
			MuteState mute,
			GroupCall::InstanceState state,
			TimeId scheduleDate,
			bool scheduleStartSubscribed,
			bool canManage,
			PanelMode mode) {
		const auto wide = (mode == PanelMode::Wide);
		using Type = Ui::CallMuteButtonType;
		_mute->setState(Ui::CallMuteButtonState{
			.text = (wide
				? QString()
				: scheduleDate
				? (canManage
					? tr::lng_group_call_start_now(tr::now)
					: scheduleStartSubscribed
					? tr::lng_group_call_cancel_reminder(tr::now)
					: tr::lng_group_call_set_reminder(tr::now))
				: state == GroupCall::InstanceState::Disconnected
				? tr::lng_group_call_connecting(tr::now)
				: mute == MuteState::ForceMuted
				? tr::lng_group_call_force_muted(tr::now)
				: mute == MuteState::RaisedHand
				? tr::lng_group_call_raised_hand(tr::now)
				: mute == MuteState::Muted
				? tr::lng_group_call_unmute(tr::now)
				: tr::lng_group_call_you_are_live(tr::now)),
			.tooltip = ((!scheduleDate && mute == MuteState::Muted)
				? tr::lng_group_call_unmute_sub(tr::now)
				: QString()),
			.type = (scheduleDate
				? (canManage
					? Type::ScheduledCanStart
					: scheduleStartSubscribed
					? Type::ScheduledNotify
					: Type::ScheduledSilent)
				: state == GroupCall::InstanceState::Disconnected
				? Type::Connecting
				: mute == MuteState::ForceMuted
				? Type::ForceMuted
				: mute == MuteState::RaisedHand
				? Type::RaisedHand
				: mute == MuteState::Muted
				? Type::Muted
				: Type::Active),
		});
	}, _callLifetime);
}

void Panel::setupScheduledLabels(rpl::producer<TimeId> date) {
	using namespace rpl::mappers;
	date = std::move(date) | rpl::take_while(_1 != 0);
	_startsWhen.create(
		widget(),
		Ui::StartsWhenText(rpl::duplicate(date)),
		st::groupCallStartsWhen);
	auto countdownCreated = std::move(
		date
	) | rpl::map([=](TimeId date) {
		_countdownData = std::make_shared<Ui::GroupCallScheduledLeft>(date);
		return rpl::empty_value();
	}) | rpl::start_spawning(lifetime());

	_countdown = Ui::CreateGradientLabel(widget(), rpl::duplicate(
		countdownCreated
	) | rpl::map([=] {
		return _countdownData->text(
			Ui::GroupCallScheduledLeft::Negative::Ignore);
	}) | rpl::flatten_latest());

	_startsIn.create(
		widget(),
		rpl::conditional(
			std::move(
				countdownCreated
			) | rpl::map(
				[=] { return _countdownData->late(); }
			) | rpl::flatten_latest(),
			tr::lng_group_call_late_by(),
			tr::lng_group_call_starts_in()),
		st::groupCallStartsIn);

	const auto top = [=] {
		const auto muteTop = widget()->height() - st::groupCallMuteBottomSkip;
		const auto membersTop = st::groupCallMembersTop;
		const auto height = st::groupCallScheduledBodyHeight;
		return (membersTop + (muteTop - membersTop - height) / 2);
	};
	rpl::combine(
		widget()->sizeValue(),
		_startsIn->widthValue()
	) | rpl::start_with_next([=](QSize size, int width) {
		_startsIn->move(
			(size.width() - width) / 2,
			top() + st::groupCallStartsInTop);
	}, _startsIn->lifetime());

	rpl::combine(
		widget()->sizeValue(),
		_startsWhen->widthValue()
	) | rpl::start_with_next([=](QSize size, int width) {
		_startsWhen->move(
			(size.width() - width) / 2,
			top() + st::groupCallStartsWhenTop);
	}, _startsWhen->lifetime());

	rpl::combine(
		widget()->sizeValue(),
		_countdown->widthValue()
	) | rpl::start_with_next([=](QSize size, int width) {
		_countdown->move(
			(size.width() - width) / 2,
			top() + st::groupCallCountdownTop);
	}, _startsWhen->lifetime());
}

PanelMode Panel::mode() const {
	return _mode.current();
}

void Panel::setupMembers() {
	if (_members) {
		return;
	}

	_startsIn.destroy();
	_countdown.destroy();
	_startsWhen.destroy();

	_members.create(widget(), _call, mode(), _window.backend());

	setupVideo(_viewport.get());
	setupVideo(_members->viewport());
	_viewport->mouseInsideValue(
	) | rpl::start_with_next([=](bool inside) {
		toggleWideControls(inside);
	}, _viewport->lifetime());

	_members->show();

	refreshControlsBackground();
	raiseControls();

	_members->desiredHeightValue(
	) | rpl::start_with_next([=] {
		updateMembersGeometry();
	}, _members->lifetime());

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

	_members->addMembersRequests(
	) | rpl::start_with_next([=] {
		if (_peer->isBroadcast() && _peer->asChannel()->hasUsername()) {
			_callShareLinkCallback();
		} else {
			addMembers();
		}
	}, _callLifetime);

	_call->videoEndpointLargeValue(
	) | rpl::start_with_next([=](const VideoEndpoint &large) {
		if (large && mode() != PanelMode::Wide) {
			enlargeVideo();
		}
		_viewport->showLarge(large);
	}, _callLifetime);
}

void Panel::enlargeVideo() {
	_lastSmallGeometry = window()->geometry();

	const auto available = window()->screen()->availableGeometry();
	const auto width = std::max(
		window()->width(),
		std::max(
			std::min(available.width(), st::groupCallWideModeSize.width()),
			st::groupCallWideModeWidthMin));
	const auto height = std::max(
		window()->height(),
		std::min(available.height(), st::groupCallWideModeSize.height()));
	auto geometry = QRect(window()->pos(), QSize(width, height));
	if (geometry.x() < available.x()) {
		geometry.moveLeft(std::min(available.x(), window()->x()));
	}
	if (geometry.x() + geometry.width()
		> available.x() + available.width()) {
		geometry.moveLeft(std::max(
			available.x() + available.width(),
			window()->x() + window()->width()) - geometry.width());
	}
	if (geometry.y() < available.y()) {
		geometry.moveTop(std::min(available.y(), window()->y()));
	}
	if (geometry.y() + geometry.height() > available.y() + available.height()) {
		geometry.moveTop(std::max(
			available.y() + available.height(),
			window()->y() + window()->height()) - geometry.height());
	}
	if (_lastLargeMaximized) {
		window()->setWindowState(
			window()->windowState() | Qt::WindowMaximized);
	} else {
		window()->setGeometry((_lastLargeGeometry
			&& available.intersects(*_lastLargeGeometry))
			? *_lastLargeGeometry
			: geometry);
	}
}

void Panel::minimizeVideo() {
	if (window()->windowState() & Qt::WindowMaximized) {
		_lastLargeMaximized = true;
		window()->setWindowState(
			window()->windowState() & ~Qt::WindowMaximized);
	} else {
		_lastLargeMaximized = false;
		_lastLargeGeometry = window()->geometry();
	}
	const auto available = window()->screen()->availableGeometry();
	const auto width = st::groupCallWidth;
	const auto height = st::groupCallHeight;
	auto geometry = QRect(
		window()->x() + (window()->width() - width) / 2,
		window()->y() + (window()->height() - height) / 2,
		width,
		height);
	window()->setGeometry((_lastSmallGeometry
		&& available.intersects(*_lastSmallGeometry))
		? *_lastSmallGeometry
		: geometry);
}

void Panel::raiseControls() {
	if (_controlsBackgroundWide) {
		_controlsBackgroundWide->raise();
	}
	if (_controlsBackgroundNarrow) {
		_controlsBackgroundNarrow->shadow.raise();
		_controlsBackgroundNarrow->blocker.raise();
	}
	const auto buttons = {
		&_settings,
		&_callShare,
		&_screenShare,
		&_wideMenu,
		&_video,
		&_hangup
	};
	for (const auto button : buttons) {
		if (const auto raw = button->data()) {
			raw->raise();
		}
	}
	_mute->raise();
	if (_niceTooltip) {
		_niceTooltip->raise();
	}
}

void Panel::setupVideo(not_null<Viewport*> viewport) {
	const auto setupTile = [=](
			const VideoEndpoint &endpoint,
			const std::unique_ptr<GroupCall::VideoTrack> &track) {
		using namespace rpl::mappers;
		const auto row = _members->lookupRow(GroupCall::TrackPeer(track));
		Assert(row != nullptr);
		auto pinned = rpl::combine(
			_call->videoEndpointLargeValue(),
			_call->videoEndpointPinnedValue()
		) | rpl::map(_1 == endpoint && _2);
		viewport->add(
			endpoint,
			VideoTileTrack{ GroupCall::TrackPointer(track), row },
			GroupCall::TrackSizeValue(track),
			std::move(pinned));
	};
	for (const auto &[endpoint, track] : _call->activeVideoTracks()) {
		setupTile(endpoint, track);
	}
	_call->videoStreamActiveUpdates(
	) | rpl::start_with_next([=](const VideoStateToggle &update) {
		if (update.value) {
			// Add async (=> the participant row is definitely in Members).
			const auto endpoint = update.endpoint;
			crl::on_main(viewport->widget(), [=] {
				const auto &tracks = _call->activeVideoTracks();
				const auto i = tracks.find(endpoint);
				if (i != end(tracks)) {
					setupTile(endpoint, i->second);
				}
			});
		} else {
			// Remove sync.
			viewport->remove(update.endpoint);
		}
	}, viewport->lifetime());

	viewport->pinToggled(
	) | rpl::start_with_next([=](bool pinned) {
		_call->pinVideoEndpoint(pinned
			? _call->videoEndpointLarge()
			: VideoEndpoint{});
	}, viewport->lifetime());

	viewport->clicks(
	) | rpl::start_with_next([=](VideoEndpoint &&endpoint) {
		if (_call->videoEndpointLarge() == endpoint) {
			_call->showVideoEndpointLarge({});
		} else if (_call->videoEndpointPinned()) {
			_call->pinVideoEndpoint(std::move(endpoint));
		} else {
			_call->showVideoEndpointLarge(std::move(endpoint));
		}
	}, viewport->lifetime());

	viewport->qualityRequests(
	) | rpl::start_with_next([=](const VideoQualityRequest &request) {
		_call->requestVideoQuality(request.endpoint, request.quality);
	}, viewport->lifetime());
}

void Panel::toggleWideControls(bool shown) {
	if (_showWideControls == shown) {
		return;
	}
	_showWideControls = shown;
	crl::on_main(widget(), [=] {
		updateWideControlsVisibility();
	});
}

void Panel::updateWideControlsVisibility() {
	const auto shown = _showWideControls
		|| (_stickedTooltipClose != nullptr);
	if (_wideControlsShown == shown) {
		return;
	}
	_wideControlsShown = shown;
	_wideControlsAnimation.start(
		[=] { updateButtonsGeometry(); },
		_wideControlsShown ? 0. : 1.,
		_wideControlsShown ? 1. : 0.,
		st::slideWrapDuration);
}

void Panel::subscribeToChanges(not_null<Data::GroupCall*> real) {
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
				showToast({ tr::lng_group_call_is_recorded(tr::now) });
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
		showToast((recorded
			? tr::lng_group_call_recording_started
			: _call->recordingStoppedByMe()
			? tr::lng_group_call_recording_saved
			: tr::lng_group_call_recording_stopped)(
				tr::now,
				Ui::Text::RichLangValue));
	}, lifetime());
	validateRecordingMark(real->recordStartDate() != 0);

	rpl::combine(
		_call->videoIsWorkingValue(),
		_call->isSharingCameraValue()
	) | rpl::start_with_next([=] {
		refreshVideoButtons();
		showStickedTooltip();
	}, lifetime());

	rpl::combine(
		_call->videoIsWorkingValue(),
		_call->isSharingScreenValue()
	) | rpl::start_with_next([=] {
		refreshTopButton();
	}, lifetime());

	_call->mutedValue(
	) | rpl::skip(1) | rpl::start_with_next([=](MuteState state) {
		updateButtonsGeometry();
		if (state == MuteState::Active
			|| state == MuteState::PushToTalk) {
			hideStickedTooltip(
				StickedTooltip::Microphone,
				StickedTooltipHide::Activated);
		}
		showStickedTooltip();
	}, lifetime());

	updateControlsGeometry();
}

void Panel::refreshTopButton() {
	if (_mode.current() == PanelMode::Wide) {
		_menuToggle.destroy();
		_joinAsToggle.destroy();
		updateButtonsGeometry(); // _wideMenu <-> _settings
		return;
	}
	const auto hasJoinAs = _call->showChooseJoinAs();
	const auto showNarrowMenu = _call->canManage()
		|| _call->videoIsWorking();
	const auto showNarrowUserpic = !showNarrowMenu && hasJoinAs;
	if (showNarrowMenu) {
		_joinAsToggle.destroy();
		if (!_menuToggle) {
			_menuToggle.create(widget(), st::groupCallMenuToggle);
			_menuToggle->show();
			_menuToggle->setClickedCallback([=] { showMainMenu(); });
			updateControlsGeometry();
		}
	} else if (showNarrowUserpic) {
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
		}, lifetime());
	} else {
		_menuToggle.destroy();
		_joinAsToggle.destroy();
	}
}

void Panel::screenSharingPrivacyRequest() {
#ifdef Q_OS_MAC
	if (!Platform::IsMac10_15OrGreater()) {
		return;
	}
	const auto requestInputMonitoring = Platform::IsMac10_15OrGreater();
	_layerBg->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box.get(),
				rpl::combine(
					tr::lng_group_call_mac_screencast_access(),
					tr::lng_group_call_mac_recording()
				) | rpl::map([](QString a, QString b) {
					auto result = Ui::Text::RichLangValue(a);
					result.append("\n\n").append(Ui::Text::RichLangValue(b));
					return result;
				}),
				st::groupCallBoxLabel),
			style::margins(
				st::boxRowPadding.left(),
				st::boxPadding.top(),
				st::boxRowPadding.right(),
				st::boxPadding.bottom()));
		box->addButton(tr::lng_group_call_mac_settings(), [=] {
			Platform::OpenDesktopCapturePrivacySettings();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
#endif // Q_OS_MAC
}

void Panel::chooseShareScreenSource() {
	if (_call->emitShareScreenError()) {
		return;
	}
	const auto choose = [=] {
		if (!Webrtc::DesktopCaptureAllowed()) {
			screenSharingPrivacyRequest();
		} else if (const auto source = Webrtc::UniqueDesktopCaptureSource()) {
			if (_call->isSharingScreen()) {
				_call->toggleScreenSharing(std::nullopt);
			} else {
				chooseSourceAccepted(*source, false);
			}
		} else {
			Ui::DesktopCapture::ChooseSource(this);
		}
	};
	const auto screencastFromPeer = [&]() -> PeerData* {
		for (const auto &[endpoint, track] : _call->activeVideoTracks()) {
			if (endpoint.type == VideoEndpointType::Screen) {
				return endpoint.peer;
			}
		}
		return nullptr;
	}();
	if (!screencastFromPeer || _call->isSharingScreen()) {
		choose();
		return;
	}
	const auto text = tr::lng_group_call_sure_screencast(
		tr::now,
		lt_user,
		screencastFromPeer->shortName());
	const auto shared = std::make_shared<QPointer<Ui::GenericBox>>();
	const auto done = [=] {
		if (*shared) {
			base::take(*shared)->closeBox();
		}
		choose();
	};
	auto box = ConfirmBox({
		.text = { text },
		.button = tr::lng_continue(),
		.callback = done,
	});
	*shared = box.data();
	_layerBg->showBox(std::move(box));
}

void Panel::chooseJoinAs() {
	const auto context = ChooseJoinAsProcess::Context::Switch;
	const auto callback = [=](JoinInfo info) {
		_call->rejoinAs(info);
	};
	const auto showBoxCallback = [=](object_ptr<Ui::BoxContent> next) {
		_layerBg->showBox(std::move(next));
	};
	const auto showToastCallback = [=](QString text) {
		showToast({ text });
	};
	_joinAsProcess.start(
		_peer,
		context,
		showBoxCallback,
		showToastCallback,
		callback,
		_call->joinAs());
}

void Panel::showMainMenu() {
	if (_menu) {
		return;
	}
	const auto wide = (_mode.current() == PanelMode::Wide) && _wideMenu;
	if (!wide && !_menuToggle) {
		return;
	}
	_menu.create(widget(), st::groupCallDropdownMenu);
	FillMenu(
		_menu.data(),
		_peer,
		_call,
		wide,
		[=] { chooseJoinAs(); },
		[=] { chooseShareScreenSource(); },
		[=](auto box) { _layerBg->showBox(std::move(box)); });
	if (_menu->empty()) {
		_wideMenuShown = false;
		_menu.destroy();
		return;
	}

	const auto raw = _menu.data();
	raw->setHiddenCallback([=] {
		raw->deleteLater();
		if (_menu == raw) {
			_menu = nullptr;
			_wideMenuShown = false;
			_trackControlsMenuLifetime.destroy();
			if (_menuToggle) {
				_menuToggle->setForceRippled(false);
			}
		}
	});
	raw->setShowStartCallback([=] {
		if (_menu == raw) {
			if (wide) {
				_wideMenuShown = true;
			} else if (_menuToggle) {
				_menuToggle->setForceRippled(true);
			}
		}
	});
	raw->setHideStartCallback([=] {
		if (_menu == raw) {
			_wideMenuShown = false;
			if (_menuToggle) {
				_menuToggle->setForceRippled(false);
			}
		}
	});

	if (wide) {
		_wideMenu->installEventFilter(_menu);
		const auto x = st::groupCallWideMenuPosition.x();
		const auto y = st::groupCallWideMenuPosition.y();
		_menu->moveToLeft(
			_wideMenu->x() + x,
			_wideMenu->y() - _menu->height() + y);
		_menu->showAnimated(Ui::PanelAnimation::Origin::BottomLeft);
		trackControl(_menu, _trackControlsMenuLifetime);
	} else {
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
}

void Panel::addMembers() {
	const auto showToastCallback = [=](TextWithEntities &&text) {
		showToast(std::move(text));
	};
	if (auto box = PrepareInviteBox(_call, showToastCallback)) {
		_layerBg->showBox(std::move(box));
	}
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
				return ChatRestrictionsInfo();
			}
			const auto i = channel->mgInfo->lastRestricted.find(user);
			return (i != channel->mgInfo->lastRestricted.cend())
				? i->second.rights
				: ChatRestrictionsInfo();
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
	}, lifetime());

#endif // !Q_OS_MAC
}

void Panel::showControls() {
	Expects(_call != nullptr);

	widget()->showChildren();
}

void Panel::closeBeforeDestroy() {
	window()->close();
	_callLifetime.destroy();
}

rpl::lifetime &Panel::lifetime() {
	return window()->lifetime();
}

void Panel::initGeometry() {
	const auto center = Core::App().getPointForCallPanelCenter();
	const auto rect = QRect(0, 0, st::groupCallWidth, st::groupCallHeight);
	window()->setGeometry(rect.translated(center - rect.center()));
	window()->setMinimumSize(rect.size());
	window()->show();
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

bool Panel::updateMode() {
	if (!_viewport) {
		return false;
	}
	const auto wide = _call->hasVideoWithFrames()
		&& (widget()->width() >= st::groupCallWideModeWidthMin);
	const auto mode = wide ? PanelMode::Wide : PanelMode::Default;
	if (_mode.current() == mode) {
		return false;
	}
	if (!wide && _call->videoEndpointLarge()) {
		_call->showVideoEndpointLarge({});
	}
	refreshVideoButtons(wide);
	if (!_stickedTooltipClose
		|| _niceTooltipControl.data() != _mute->outer().get()) {
		_niceTooltip.destroy();
	}
	_mode = mode;
	if (_title) {
		_title->setTextColorOverride(wide
			? std::make_optional(st::groupCallMemberNotJoinedStatus->c)
			: std::nullopt);
	}
	if (wide && _subtitle) {
		_subtitle.destroy();
	} else if (!wide && !_subtitle) {
		refreshTitle();
	}
	_wideControlsShown = _showWideControls = true;
	_wideControlsAnimation.stop();
	_viewport->widget()->setVisible(wide);
	if (_members) {
		_members->setMode(mode);
	}
	updateButtonsStyles();
	refreshControlsBackground();
	updateControlsGeometry();
	showStickedTooltip();
	return true;
}

void Panel::updateButtonsStyles() {
	const auto wide = (_mode.current() == PanelMode::Wide);
	_mute->setStyle(wide ? st::callMuteButtonSmall : st::callMuteButton);
	if (_video) {
		_video->setStyle(
			wide ? st::groupCallVideoSmall : st::groupCallVideo,
			(wide
				? &st::groupCallVideoActiveSmall
				: &st::groupCallVideoActive));
		_video->setText(wide
			? rpl::single(QString())
			: tr::lng_group_call_video());
	}
	if (_settings) {
		_settings->setText(wide
			? rpl::single(QString())
			: tr::lng_group_call_settings());
		_settings->setStyle(wide
			? st::groupCallSettingsSmall
			: st::groupCallSettings);
	}
	_hangup->setText(wide
		? rpl::single(QString())
		: _call->scheduleDate()
		? tr::lng_group_call_close()
		: tr::lng_group_call_leave());
	_hangup->setStyle(wide
		? st::groupCallHangupSmall
		: st::groupCallHangup);
}

void Panel::refreshControlsBackground() {
	if (!_members) {
		return;
	}
	if (mode() == PanelMode::Default) {
		trackControls(false);
		_controlsBackgroundWide.destroy();
		if (_controlsBackgroundNarrow) {
			return;
		}
		setupControlsBackgroundNarrow();
	} else {
		_controlsBackgroundNarrow = nullptr;
		if (_controlsBackgroundWide) {
			return;
		}
		setupControlsBackgroundWide();
	}
	raiseControls();
	updateButtonsGeometry();
}

void Panel::setupControlsBackgroundNarrow() {
	_controlsBackgroundNarrow = std::make_unique<ControlsBackgroundNarrow>(
		widget());
	_controlsBackgroundNarrow->shadow.show();
	_controlsBackgroundNarrow->blocker.show();
	auto &lifetime = _controlsBackgroundNarrow->shadow.lifetime();

	const auto factor = cIntRetinaFactor();
	const auto height = std::max(
		st::groupCallMembersShadowHeight,
		st::groupCallMembersFadeSkip + st::groupCallMembersFadeHeight);
	const auto full = lifetime.make_state<QImage>(
		QSize(1, height * factor),
		QImage::Format_ARGB32_Premultiplied);
	rpl::single(
		rpl::empty_value()
	) | rpl::then(
		style::PaletteChanged()
	) | rpl::start_with_next([=] {
		full->fill(Qt::transparent);

		auto p = QPainter(full);
		const auto bottom = (height - st::groupCallMembersFadeSkip) * factor;
		p.fillRect(
			0,
			bottom,
			full->width(),
			st::groupCallMembersFadeSkip * factor,
			st::groupCallMembersBg);
		p.drawImage(
			QRect(
				0,
				bottom - (st::groupCallMembersFadeHeight * factor),
				full->width(),
				st::groupCallMembersFadeHeight * factor),
			GenerateShadow(
				st::groupCallMembersFadeHeight,
				0,
				255,
				st::groupCallMembersBg->c));
		p.drawImage(
			QRect(
				0,
				(height - st::groupCallMembersShadowHeight) * factor,
				full->width(),
				st::groupCallMembersShadowHeight * factor),
			GenerateShadow(
				st::groupCallMembersShadowHeight,
				0,
				255,
				st::groupCallBg->c));
	}, lifetime);

	_controlsBackgroundNarrow->shadow.resize(
		(widget()->width()
			- st::groupCallMembersMargin.left()
			- st::groupCallMembersMargin.right()),
		height);
	_controlsBackgroundNarrow->shadow.paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(&_controlsBackgroundNarrow->shadow);
		clip = clip.intersected(_controlsBackgroundNarrow->shadow.rect());
		const auto inner = _members->getInnerGeometry().translated(
			_members->x() - _controlsBackgroundNarrow->shadow.x(),
			_members->y() - _controlsBackgroundNarrow->shadow.y());
		const auto faded = clip.intersected(inner);
		if (!faded.isEmpty()) {
			const auto factor = cIntRetinaFactor();
			p.drawImage(
				faded,
				*full,
				QRect(
					0,
					faded.y() * factor,
					full->width(),
					faded.height() * factor));
		}
		const auto bottom = inner.y() + inner.height();
		const auto after = clip.intersected(QRect(
			0,
			bottom,
			inner.width(),
			_controlsBackgroundNarrow->shadow.height() - bottom));
		if (!after.isEmpty()) {
			p.fillRect(after, st::groupCallBg);
		}
	}, lifetime);
	_controlsBackgroundNarrow->shadow.setAttribute(
		Qt::WA_TransparentForMouseEvents);
	_controlsBackgroundNarrow->blocker.setUpdatesEnabled(false);
}

void Panel::setupControlsBackgroundWide() {
	_controlsBackgroundWide.create(widget());
	_controlsBackgroundWide->show();
	auto &lifetime = _controlsBackgroundWide->lifetime();
	const auto color = lifetime.make_state<style::complex_color>([] {
		auto result = st::groupCallBg->c;
		result.setAlphaF(kControlsBackgroundOpacity);
		return result;
	});
	const auto corners = lifetime.make_state<Ui::RoundRect>(
		st::groupCallControlsBackRadius,
		color->color());
	_controlsBackgroundWide->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_controlsBackgroundWide.data());
		corners->paint(p, _controlsBackgroundWide->rect());
	}, lifetime);

	trackControls(true);
}

void Panel::trackControl(Ui::RpWidget *widget, rpl::lifetime &lifetime) {
	if (!widget) {
		return;
	}
	widget->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Enter) {
			trackControlOver(widget, true);
		} else if (e->type() == QEvent::Leave) {
			trackControlOver(widget, false);
		}
	}, lifetime);
}

void Panel::trackControlOver(not_null<Ui::RpWidget*> control, bool over) {
	if (_stickedTooltipClose) {
		if (!over) {
			return;
		}
	} else {
		hideNiceTooltip();
	}
	if (over) {
		Ui::Integration::Instance().registerLeaveSubscription(control);
		showNiceTooltip(control);
	} else {
		Ui::Integration::Instance().unregisterLeaveSubscription(control);
	}
	toggleWideControls(over);
}

void Panel::showStickedTooltip() {
	static const auto kHasCamera = !Webrtc::GetVideoInputList().empty();
	const auto callReady = (_call->state() == State::Joined
		|| _call->state() == State::Connecting);
	if (!(_stickedTooltipsShown & StickedTooltip::Camera)
		&& callReady
		&& (_mode.current() == PanelMode::Wide)
		&& _video
		&& _call->videoIsWorking()
		&& !_call->mutedByAdmin()
		&& kHasCamera) { // Don't recount this every time for now.
		showNiceTooltip(_video, NiceTooltipType::Sticked);
		return;
	}
	hideStickedTooltip(
		StickedTooltip::Camera,
		StickedTooltipHide::Unavailable);

	if (!(_stickedTooltipsShown & StickedTooltip::Microphone)
		&& callReady
		&& _mute
		&& !_call->mutedByAdmin()) {
		if (_stickedTooltipClose) {
			// Showing already.
			return;
		} else if (!_micLevelTester) {
			// Check if there is incoming sound.
			_micLevelTester = std::make_unique<MicLevelTester>([=] {
				showStickedTooltip();
			});
		}
		if (_micLevelTester->showTooltip()) {
			_micLevelTester = nullptr;
			showNiceTooltip(_mute->outer(), NiceTooltipType::Sticked);
		}
		return;
	}
	_micLevelTester = nullptr;
	hideStickedTooltip(
		StickedTooltip::Microphone,
		StickedTooltipHide::Unavailable);
}

void Panel::showNiceTooltip(
		not_null<Ui::RpWidget*> control,
		NiceTooltipType type) {
	auto text = [&]() -> rpl::producer<QString> {
		if (control == _screenShare.data()) {
			if (_call->mutedByAdmin()) {
				return nullptr;
			}
			return tr::lng_group_call_tooltip_screen();
		} else if (control == _video.data()) {
			if (_call->mutedByAdmin()) {
				return nullptr;
			}
			return _call->isSharingCameraValue(
			) | rpl::map([=](bool sharing) {
				return sharing
					? tr::lng_group_call_tooltip_camera_off()
					: tr::lng_group_call_tooltip_camera();
			}) | rpl::flatten_latest();
		} else if (control == _settings.data()) {
			return tr::lng_group_call_settings();
		} else if (control == _mute->outer()) {
			return MuteButtonTooltip(_call);
		} else if (control == _hangup.data()) {
			return tr::lng_group_call_leave();
		}
		return rpl::producer<QString>();
	}();
	if (!text || _stickedTooltipClose) {
		return;
	} else if (_wideControlsAnimation.animating() || !_wideControlsShown) {
		if (type == NiceTooltipType::Normal) {
			return;
		}
	}
	const auto inner = [&]() -> Ui::RpWidget* {
		const auto normal = (type == NiceTooltipType::Normal);
		auto container = normal
			? nullptr
			: Ui::CreateChild<Ui::RpWidget>(widget().get());
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			(normal ? widget().get() : container),
			std::move(text),
			st::groupCallNiceTooltipLabel);
		if (normal) {
			return label;
		}
		const auto button = Ui::CreateChild<Ui::IconButton>(
			container,
			st::groupCallStickedTooltipClose);
		rpl::combine(
			label->sizeValue(),
			button->sizeValue()
		) | rpl::start_with_next([=](QSize text, QSize close) {
			const auto height = std::max(text.height(), close.height());
			container->resize(text.width() + close.width(), height);
			label->move(0, (height - text.height()) / 2);
			button->move(text.width(), (height - close.height()) / 2);
		}, container->lifetime());
		button->setClickedCallback([=] {
			hideStickedTooltip(StickedTooltipHide::Discarded);
		});
		_stickedTooltipClose = button;
		updateWideControlsVisibility();
		return container;
	}();
	_niceTooltip.create(
		widget().get(),
		object_ptr<Ui::RpWidget>::fromRaw(inner),
		(type == NiceTooltipType::Sticked
			? st::groupCallStickedTooltip
			: st::groupCallNiceTooltip));
	const auto tooltip = _niceTooltip.data();
	const auto weak = QPointer<QWidget>(tooltip);
	const auto destroy = [=] {
		delete weak.data();
	};
	if (type != NiceTooltipType::Sticked) {
		tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	tooltip->setHiddenCallback(destroy);
	base::qt_signal_producer(
		control.get(),
		&QObject::destroyed
	) | rpl::start_with_next(destroy, tooltip->lifetime());

	_niceTooltipControl = control;
	updateTooltipGeometry();
	tooltip->toggleAnimated(true);
}

void Panel::updateTooltipGeometry() {
	if (!_niceTooltip) {
		return;
	} else if (!_niceTooltipControl) {
		hideNiceTooltip();
		return;
	}
	const auto geometry = _niceTooltipControl->geometry();
	const auto weak = QPointer<QWidget>(_niceTooltip);
	const auto countPosition = [=](QSize size) {
		const auto strong = weak.data();
		const auto wide = (_mode.current() == PanelMode::Wide);
		const auto top = geometry.y()
			- (wide ? st::groupCallNiceTooltipTop : 0)
			- size.height();
		const auto middle = geometry.center().x();
		if (!strong) {
			return QPoint();
		} else if (!wide) {
			return QPoint(
				std::max(
					std::min(
						middle - size.width() / 2,
						(widget()->width()
							- st::groupCallMembersMargin.right()
							- size.width())),
					st::groupCallMembersMargin.left()),
				top);
		}
		const auto back = _controlsBackgroundWide.data();
		if (size.width() >= _viewport->widget()->width()) {
			return QPoint(_viewport->widget()->x(), top);
		} else if (back && size.width() >= back->width()) {
			return QPoint(
				back->x() - (size.width() - back->width()) / 2,
				top);
		} else if (back && (middle - back->x() < size.width() / 2)) {
			return QPoint(back->x(), top);
		} else if (back
			&& (back->x() + back->width() - middle < size.width() / 2)) {
			return QPoint(back->x() + back->width() - size.width(), top);
		} else {
			return QPoint(middle - size.width() / 2, top);
		}
	};
	_niceTooltip->pointAt(geometry, RectPart::Top, countPosition);
}

void Panel::trackControls(bool track) {
	if (_trackControls == track) {
		return;
	}
	_trackControls = track;
	if (!track) {
		_trackControlsLifetime.destroy();
		_trackControlsOverStateLifetime.destroy();
		_trackControlsMenuLifetime.destroy();
		toggleWideControls(true);
		if (_wideControlsAnimation.animating()) {
			_wideControlsAnimation.stop();
			updateButtonsGeometry();
		}
		return;
	}

	const auto trackOne = [=](auto &&widget) {
		trackControl(widget, _trackControlsOverStateLifetime);
	};
	trackOne(_mute->outer());
	trackOne(_video);
	trackOne(_screenShare);
	trackOne(_wideMenu);
	trackOne(_settings);
	trackOne(_hangup);
	trackOne(_controlsBackgroundWide);
	trackControl(_menu, _trackControlsMenuLifetime);
}

void Panel::updateControlsGeometry() {
	if (widget()->size().isEmpty() || (!_settings && !_callShare)) {
		return;
	}
	updateButtonsGeometry();
	updateMembersGeometry();
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

void Panel::updateButtonsGeometry() {
	if (widget()->size().isEmpty() || (!_settings && !_callShare)) {
		return;
	}
	const auto toggle = [](auto &widget, bool shown) {
		if (widget && widget->isHidden() == shown) {
			widget->setVisible(shown);
		}
	};
	if (mode() == PanelMode::Wide) {
		Assert(_video != nullptr);
		Assert(_screenShare != nullptr);
		Assert(_wideMenu != nullptr);
		Assert(_settings != nullptr);
		Assert(_callShare == nullptr);

		const auto shown = _wideControlsAnimation.value(
			_wideControlsShown ? 1. : 0.);
		const auto hidden = (shown == 0.);

		if (_viewport) {
			_viewport->setControlsShown(shown);
		}

		const auto buttonsTop = widget()->height() - anim::interpolate(
			0,
			st::groupCallButtonBottomSkipWide,
			shown);
		const auto addSkip = st::callMuteButtonSmall.active.outerRadius;
		const auto muteSize = _mute->innerSize().width() + 2 * addSkip;
		const auto skip = st::groupCallButtonSkipSmall;
		const auto fullWidth = (_video->width() + skip)
			+ (_screenShare->width() + skip)
			+ (muteSize + skip)
			+ (_settings ->width() + skip)
			+ _hangup->width();
		const auto membersSkip = st::groupCallNarrowSkip;
		const auto membersWidth = st::groupCallNarrowMembersWidth
			+ 2 * membersSkip;
		auto left = membersSkip + (widget()->width()
			- membersWidth
			- membersSkip
			- fullWidth) / 2;
		toggle(_screenShare, !hidden);
		_screenShare->moveToLeft(left, buttonsTop);
		left += _screenShare->width() + skip;
		toggle(_video, !hidden);
		_video->moveToLeft(left, buttonsTop);
		left += _video->width() + skip;
		toggle(_mute, !hidden);
		_mute->moveInner({ left + addSkip, buttonsTop + addSkip });
		left += muteSize + skip;
		const auto wideMenuShown = _call->canManage()
			|| _call->showChooseJoinAs();
		toggle(_settings, !hidden && !wideMenuShown);
		toggle(_wideMenu, !hidden && wideMenuShown);
		_wideMenu->moveToLeft(left, buttonsTop);
		_settings->moveToLeft(left, buttonsTop);
		left += _settings->width() + skip;
		toggle(_hangup, !hidden);
		_hangup->moveToLeft(left, buttonsTop);
		left += _hangup->width();
		if (_controlsBackgroundWide) {
			const auto rect = QRect(
				left - fullWidth,
				buttonsTop,
				fullWidth,
				_hangup->height());
			_controlsBackgroundWide->setGeometry(
				rect.marginsAdded(st::groupCallControlsBackMargin));
		}
	} else {
		const auto muteTop = widget()->height()
			- st::groupCallMuteBottomSkip;
		const auto buttonsTop = widget()->height()
			- st::groupCallButtonBottomSkip;
		const auto muteSize = _mute->innerSize().width();
		const auto fullWidth = muteSize
			+ 2 * (_settings ? _settings : _callShare)->width()
			+ 2 * st::groupCallButtonSkip;
		toggle(_mute, true);
		_mute->moveInner({ (widget()->width() - muteSize) / 2, muteTop });
		const auto leftButtonLeft = (widget()->width() - fullWidth) / 2;
		toggle(_screenShare, false);
		toggle(_wideMenu, false);
		toggle(_callShare, true);
		if (_callShare) {
			_callShare->moveToLeft(leftButtonLeft, buttonsTop);
		}
		const auto showVideoButton = videoButtonInNarrowMode();
		toggle(_video, !_callShare && showVideoButton);
		if (_video) {
			_video->setStyle(st::groupCallVideo, &st::groupCallVideoActive);
			_video->moveToLeft(leftButtonLeft, buttonsTop);
		}
		toggle(_settings, !_callShare && !showVideoButton);
		if (_settings) {
			_settings->moveToLeft(leftButtonLeft, buttonsTop);
		}
		toggle(_hangup, true);
		_hangup->moveToRight(leftButtonLeft, buttonsTop);
	}
	if (_controlsBackgroundNarrow) {
		const auto left = st::groupCallMembersMargin.left();
		const auto width = (widget()->width()
			- st::groupCallMembersMargin.left()
			- st::groupCallMembersMargin.right());
		_controlsBackgroundNarrow->shadow.setGeometry(
			left,
			(widget()->height()
				- st::groupCallMembersMargin.bottom()
				- _controlsBackgroundNarrow->shadow.height()),
			width,
			_controlsBackgroundNarrow->shadow.height());
		_controlsBackgroundNarrow->blocker.setGeometry(
			left,
			(widget()->height()
				- st::groupCallMembersMargin.bottom()
				- st::groupCallMembersBottomSkip),
			width,
			st::groupCallMembersBottomSkip);
	}
	updateTooltipGeometry();
}

bool Panel::videoButtonInNarrowMode() const {
	return (_video != nullptr) && !_call->mutedByAdmin();
}

void Panel::updateMembersGeometry() {
	if (!_members) {
		return;
	}
	const auto desiredHeight = _members->desiredHeight();
	if (mode() == PanelMode::Wide) {
		const auto skip = st::groupCallNarrowSkip;
		const auto membersWidth = st::groupCallNarrowMembersWidth;
		const auto top = st::groupCallWideVideoTop;
		_members->setGeometry(
			widget()->width() - skip - membersWidth,
			top,
			membersWidth,
			std::min(desiredHeight, widget()->height() - top - skip));
		_viewport->setGeometry({
			skip,
			top,
			widget()->width() - membersWidth - 3 * skip,
			widget()->height() - top - skip,
		});
	} else {
		const auto membersBottom = widget()->height();
		const auto membersTop = st::groupCallMembersTop;
		const auto availableHeight = membersBottom
			- st::groupCallMembersMargin.bottom()
			- membersTop;
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
		_members->setGeometry(
			(widget()->width() - membersWidth) / 2,
			membersTop,
			membersWidth,
			std::min(desiredHeight, availableHeight));
	}
}

void Panel::refreshTitle() {
	if (!_title) {
		auto text = rpl::combine(
			Info::Profile::NameValue(_peer),
			rpl::single(
				QString()
			) | rpl::then(_call->real(
			) | rpl::map([=](not_null<Data::GroupCall*> real) {
				return real->titleValue();
			}) | rpl::flatten_latest())
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
	if (!_subtitle && mode() == PanelMode::Default) {
		_subtitle.create(
			widget(),
			rpl::single(
				_call->scheduleDate()
			) | rpl::then(
				_call->real(
				) | rpl::map([=](not_null<Data::GroupCall*> real) {
					return real->scheduleDateValue();
				}) | rpl::flatten_latest()
			) | rpl::map([=](TimeId scheduleDate) {
				if (scheduleDate) {
					return tr::lng_group_call_scheduled_status();
				} else if (!_members) {
					setupMembers();
				}
				return tr::lng_group_call_members(
					lt_count_decimal,
					_members->fullCountValue() | rpl::map([](int value) {
						return (value > 0) ? float64(value) : 1.;
					}));
			}) | rpl::flatten_latest(),
			st::groupCallSubtitleLabel);
		_subtitle->show();
		_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	if (_subtitle) {
		const auto top = _title
			? st::groupCallSubtitleTop
			: st::groupCallTitleTop;
		_subtitle->moveToLeft(
			(widget()->width() - _subtitle->width()) / 2,
			top);
	}
}

void Panel::refreshTitleGeometry() {
	if (!_title) {
		return;
	}
	const auto fullRect = computeTitleRect();
	const auto titleRect = _recordingMark
		? QRect(
			fullRect.x(),
			fullRect.y(),
			fullRect.width() - _recordingMark->width(),
			fullRect.height())
		: fullRect;
	const auto best = _title->naturalWidth();
	const auto from = (widget()->width() - best) / 2;
	const auto top = (mode() == PanelMode::Default)
		? st::groupCallTitleTop
		: (st::groupCallWideVideoTop
			- st::groupCallTitleLabel.style.font->height) / 2;
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
		window()->hide();
		return true;
	}
	return false;
}

not_null<Ui::Window*> Panel::window() const {
	return _window.window();
}

not_null<Ui::RpWidget*> Panel::widget() const {
	return _window.widget();
}

} // namespace Calls::Group
