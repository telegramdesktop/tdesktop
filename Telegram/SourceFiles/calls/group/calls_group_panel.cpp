/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_panel.h"

#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_invite_controller.h"
#include "calls/group/calls_group_members.h"
#include "calls/group/calls_group_menu.h"
#include "calls/group/calls_group_message_field.h"
#include "calls/group/calls_group_messages.h"
#include "calls/group/calls_group_messages_ui.h"
#include "calls/group/calls_group_settings.h"
#include "calls/group/calls_group_toasts.h"
#include "calls/group/calls_group_viewport.h"
#include "calls/group/ui/calls_group_scheduled_labels.h"
#include "calls/group/ui/desktop_capture_choose_source.h"
#include "calls/calls_emoji_fingerprint.h"
#include "calls/calls_window.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_file_origin.h"
#include "ui/platform/ui_platform_window_title.h" // TitleLayout
#include "ui/platform/ui_platform_utility.h"
#include "ui/controls/call_mute_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/tooltip.h"
#include "ui/widgets/rp_window.h"
#include "ui/chat/group_call_bar.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/image/image_prepare.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "info/profile/info_profile_values.h" // Info::Profile::Value.
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "base/event_filter.h"
#include "base/unixtime.h"
#include "base/qt_signal_producer.h"
#include "base/timer_rpl.h"
#include "apiwrap.h" // api().kick.
#include "api/api_chat_participants.h" // api().kick.
#include "webrtc/webrtc_environment.h"
#include "webrtc/webrtc_video_track.h"
#include "webrtc/webrtc_audio_input_tester.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"

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
constexpr auto kHideControlsTimeout = 5 * crl::time(1000);

#ifdef Q_OS_WIN
void UnpinMaximized(not_null<QWidget*> widget) {
	SetWindowPos(
		reinterpret_cast<HWND>(widget->window()->windowHandle()->winId()),
		HWND_NOTOPMOST,
		0,
		0,
		0,
		0,
		(SWP_NOMOVE
			| SWP_NOSIZE
			| SWP_NOOWNERZORDER
			| SWP_FRAMECHANGED
			| SWP_NOACTIVATE));
}
#endif // Q_OS_WIN

class Show final : public ChatHelpers::Show {
public:
	Show(not_null<Panel*> panel, std::shared_ptr<Ui::Show> base)
	: _panel(panel)
	, _base(std::move(base)) {
	}

	void activate() override {
		if (const auto panel = _panel.get()) {
			if (!panel->window()->isHidden()) {
				panel->window()->activateWindow();
			}
		}
	}

	void showOrHideBoxOrLayer(
			std::variant<
				v::null_t,
				object_ptr<Ui::BoxContent>,
				std::unique_ptr<Ui::LayerWidget>> &&layer,
			Ui::LayerOptions options,
			anim::type animated) const override {
		_base->showOrHideBoxOrLayer(
			std::move(layer),
			options,
			anim::type::normal);
	}
	not_null<QWidget*> toastParent() const override {
		return _base->toastParent();
	}
	bool valid() const override {
		return _panel.get() != nullptr;
	}
	operator bool() const override {
		return valid();
	}

	Main::Session &session() const override {
		const auto panel = _panel.get();
		Assert(panel != nullptr);

		return panel->call()->peer()->session();
	}
	bool paused(ChatHelpers::PauseReason reason) const override {
		const auto panel = _panel.get();
		if (!panel) {
			return false;
		} else if (panel->window()->isHidden()
			|| (!panel->window()->isFullScreen()
				&& !panel->window()->isActiveWindow())) {
			return true;
		} else if (reason < ChatHelpers::PauseReason::Layer
			&& panel->callWindow()->topShownLayer() != nullptr) {
			return true;
		}
		return false;
	}
	rpl::producer<> pauseChanged() const override {
		return rpl::never<>();
	}

	rpl::producer<bool> adjustShadowLeft() const override {
		return rpl::single(false);
	}
	SendMenu::Details sendMenuDetails() const override {
		return { SendMenu::Type::Disabled };
	}

	bool showMediaPreview(
			Data::FileOrigin origin,
			not_null<DocumentData*> document) const override {
		return false; // #TODO stories
	}
	bool showMediaPreview(
			Data::FileOrigin origin,
			not_null<PhotoData*> photo) const override {
		return false; // #TODO stories
	}

	void processChosenSticker(
			ChatHelpers::FileChosen &&chosen) const override {
		//_panel->emojiChosen(std::move(chosen));
	}

private:
	const base::weak_ptr<Panel> _panel;
	const std::shared_ptr<Ui::Show> _base;

};

} // namespace

struct Panel::ControlsBackgroundNarrow {
	explicit ControlsBackgroundNarrow(not_null<QWidget*> parent)
	: shadow(parent)
	, blocker(parent) {
	}

	Ui::RpWidget shadow;
	Ui::RpWidget blocker;
	int shadowHeight = 0;
};

Panel::Panel(not_null<GroupCall*> call)
: Panel(call, ConferencePanelMigration()) {
}

Panel::Panel(not_null<GroupCall*> call, ConferencePanelMigration info)
: _call(call)
, _peer(call->peer())
, _window(info.window ? info.window : std::make_shared<Window>())
, _viewport(
	std::make_unique<Viewport>(
		widget(),
		PanelMode::Wide,
		_window->backend()))
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
		.expandType = ((_call->scheduleDate() || !_call->rtmp())
			? Ui::CallMuteButtonExpandType::None
			: Ui::CallMuteButtonExpandType::Normal),
	}))
, _hangup(widget(), st::groupCallHangup)
, _stickedTooltipsShown(Core::App().settings().hiddenGroupCallTooltips()
	& ~StickedTooltip::Microphone) // Always show tooltip about mic.
, _messages(std::make_unique<MessagesUi>(
	widget(),
	uiShow(),
	_call->messages()->listValue(),
	_call->messagesEnabledValue()))
, _toasts(std::make_unique<Toasts>(this))
, _controlsBackgroundColor([] {
	auto result = st::groupCallBg->c;
	result.setAlphaF(kControlsBackgroundOpacity);
	return result;
})
, _hideControlsTimer([=] { toggleWideControls(false); }) {
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
	initLayout(info);
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

bool Panel::isVisible() const {
	return window()->isVisible()
		&& !(window()->windowState() & Qt::WindowMinimized);
}

bool Panel::isActive() const {
	return window()->isActiveWindow() && isVisible();
}

std::shared_ptr<ChatHelpers::Show> Panel::uiShow() {
	if (!_cachedShow) {
		_cachedShow = std::make_shared<Show>(this, _window->uiShow());
	}
	return _cachedShow;
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
	_titleSeparator.destroy();
	_viewers.destroy();
	refreshTitle();
}

void Panel::subscribeToPeerChanges() {
	Info::Profile::NameValue(
		_peer
	) | rpl::start_with_next([=](const QString &name) {
		window()->setTitle(name);
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

	if (_call->conference()) {
		titleText() | rpl::start_with_next([=](const QString &text) {
			window()->setTitle(text);
		}, lifetime());
	} else {
		subscribeToPeerChanges();
	}

	const auto updateFullScreen = [=] {
		const auto state = window()->windowState();
		const auto full = (state & Qt::WindowFullScreen)
			|| (state & Qt::WindowMaximized);
		_rtmpFull = _call->rtmp() && full;
		_fullScreenOrMaximized = full;
	};
	base::install_event_filter(window().get(), [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Close && handleClose()) {
			e->ignore();
			return base::EventFilterResult::Cancel;
		} else if (_call->rtmp()
			&& (type == QEvent::KeyPress || type == QEvent::KeyRelease)) {
			const auto key = static_cast<QKeyEvent*>(e.get())->key();
			if (key == Qt::Key_Space) {
				_call->pushToTalk(
					e->type() == QEvent::KeyPress,
					kSpacePushToTalkDelay);
			} else if (key == Qt::Key_Escape
				&& _fullScreenOrMaximized.current()) {
				toggleFullScreen();
			}
		} else if (type == QEvent::WindowStateChange) {
			updateFullScreen();
		}
		return base::EventFilterResult::Continue;
	}, lifetime());
	updateFullScreen();

	const auto guard = base::make_weak(this);
	window()->setBodyTitleArea([=](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		if (!guard) {
			return (Flag::None | Flag(0));
		}
		const auto titleRect = QRect(
			0,
			0,
			widget()->width(),
			(mode() == PanelMode::Wide
				? st::groupCallWideVideoTop
				: st::groupCallMembersTop));
		const auto moveable = (titleRect.contains(widgetPoint)
			&& (!_menuToggle || !_menuToggle->geometry().contains(widgetPoint))
			&& (!_menu || !_menu->geometry().contains(widgetPoint))
			&& (!_recordingMark || !_recordingMark->geometry().contains(widgetPoint))
			&& (!_joinAsToggle || !_joinAsToggle->geometry().contains(widgetPoint)));
		if (!moveable) {
			return (Flag::None | Flag(0));
		}
		const auto shown = _window->topShownLayer();
		return (!shown || !shown->geometry().contains(widgetPoint))
			? (Flag::Move | Flag::Menu | Flag::Maximize)
			: Flag::None;
	});

	_call->hasVideoWithFramesValue(
	) | rpl::start_with_next([=] {
		updateMode();
	}, lifetime());

	_window->maximizeRequests() | rpl::start_with_next([=](bool maximized) {
		if (_call->rtmp()) {
			toggleFullScreen(maximized);
		} else {
			window()->setWindowState(maximized
				? Qt::WindowMaximized
				: Qt::WindowNoState);
		}
	}, lifetime());

	_window->showingLayer() | rpl::start_with_next([=] {
		hideStickedTooltip(StickedTooltipHide::Unavailable);
	}, lifetime());

	_window->setControlsStyle(st::groupCallTitle);
	_window->togglePowerSaveBlocker(true);

	uiShow()->hideLayer(anim::type::instant);
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

		// some geometries depends on _controls->controls.geometry,
		// which is not updated here yet.
		crl::on_main(this, [=] { updateControlsGeometry(); });
	}, lifetime());
}

void Panel::toggleMessageTyping() {
	const auto typing = !_messageTyping.current();
	if (_messageField) {
		_messageField->toggle(typing);
	} else if (typing) {
		_messageField = std::make_unique<MessageField>(
			widget(),
			uiShow(),
			_call->conference() ? nullptr : _call->peer().get());

		updateButtonsGeometry();
		_messageField->toggle(true);

		_messageField->submitted(
		) | rpl::start_with_next([=](TextWithTags text) {
			_call->sendMessage(std::move(text));

			_messageField->toggle(false);
			_messageTyping = false;
			updateWideControlsVisibility();
		}, _messageField->lifetime());

		_messageField->heightValue() | rpl::start_with_next([=] {
			updateButtonsGeometry();
		}, _messageField->lifetime());

		_messageField->closeRequests() | rpl::start_with_next([=] {
			if (_messageTyping.current()) {
				toggleMessageTyping();
			}
		}, _messageField->lifetime());

		_messageField->closed() | rpl::start_with_next([=] {
			_messageField = nullptr;
			updateButtonsGeometry();
		}, _messageField->lifetime());
	}
	_messageTyping = typing;
	updateWideControlsVisibility();
}

void Panel::endCall() {
	if (!_call->canManage()) {
		_call->hangup();
		return;
	}
	uiShow()->showBox(Box(
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
		const auto box = std::make_shared<base::weak_qptr<Ui::GenericBox>>();
		const auto done = [=] {
			if (*box) {
				(*box)->closeBox();
			}
			_call->startScheduledNow();
		};
		auto owned = ConfirmBox({
			.text = (_call->peer()->isBroadcast()
				? tr::lng_group_call_start_now_sure_channel
				: tr::lng_group_call_start_now_sure)(),
			.confirmed = done,
			.confirmText = tr::lng_group_call_start_now(),
		});
		*box = owned.data();
		uiShow()->showBox(std::move(owned));
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
		} else if (_call->rtmp()) {
			toggleFullScreen();
			return;
		}

		const auto oldState = _call->muted();
		const auto newState = (oldState == MuteState::ForceMuted)
			? (_call->conference()
				? MuteState::ForceMuted
				: MuteState::RaisedHand)
			: (oldState == MuteState::RaisedHand)
			? MuteState::RaisedHand
			: (oldState == MuteState::Muted)
			? MuteState::Active
			: MuteState::Muted;
		_call->setMutedAndUpdate(newState);
	}, _mute->lifetime());

	initShareAction();
	createMessageButton();
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

void Panel::toggleFullScreen() {
	toggleFullScreen(
		!_fullScreenOrMaximized.current() && !window()->isFullScreen());
}

void Panel::toggleFullScreen(bool fullscreen) {
	if (fullscreen) {
		window()->showFullScreen();
	} else {
		window()->showNormal();
	}
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
			uiShow()->showBox(Box(SettingsBox, _call));
		});
		trackControls(_trackControls, true);
	}
	const auto raw = _callShare ? _callShare.data() : _settings.data();
	raw->show();
	raw->setColorOverrides(_mute->colorOverrides());
	updateButtonsStyles();
}

rpl::producer<Ui::CallButtonColors> Panel::toggleableOverrides(
		rpl::producer<bool> active) {
	return rpl::combine(
		std::move(active),
		_mute->colorOverrides()
	) | rpl::map([](bool active, Ui::CallButtonColors colors) {
		if (active && colors.bg) {
			colors.bg->setAlpha(kOverrideActiveColorBgAlpha);
		}
		return colors;
	});
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
	raiseControls();
}

void Panel::createMessageButton() {
	if (!_message) {
		_message.create(
			widget(),
			st::groupCallMessageSmall,
			&st::groupCallMessageActiveSmall);
		_message->show();
		_message->setClickedCallback([=] { toggleMessageTyping(); });
		_message->setColorOverrides(
			toggleableOverrides(_messageTyping.value()));
	}
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
	auto [shareLinkCallback, shareLinkLifetime] = ShareInviteLinkAction(
		_peer,
		uiShow());
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
		_mode.value(),
		_fullScreenOrMaximized.value()
	) | rpl::distinct_until_changed(
	) | rpl::filter(
		_2 != GroupCall::InstanceState::TransitionToRtc
	) | rpl::start_with_next([=](
			MuteState mute,
			GroupCall::InstanceState state,
			TimeId scheduleDate,
			bool scheduleStartSubscribed,
			bool canManage,
			PanelMode mode,
			bool fullScreenOrMaximized) {
		const auto wide = (mode == PanelMode::Wide);
		using Type = Ui::CallMuteButtonType;
		using ExpandType = Ui::CallMuteButtonExpandType;
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
				? (_call->conference()
					? Type::ConferenceForceMuted
					: Type::ForceMuted)
				: mute == MuteState::RaisedHand
				? Type::RaisedHand
				: mute == MuteState::Muted
				? Type::Muted
				: Type::Active),
			.expandType = ((scheduleDate || !_call->rtmp())
				? ExpandType::None
				: fullScreenOrMaximized
				? ExpandType::Expanded
				: ExpandType::Normal),
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
		return rpl::empty;
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

	_members.create(widget(), _call, mode(), _window->backend());

	setupVideo(_viewport.get());
	setupVideo(_members->viewport());
	_viewport->mouseInsideValue(
	) | rpl::filter([=] {
		return !_rtmpFull;
	}) | rpl::start_with_next([=](bool inside) {
		toggleWideControls(inside);
	}, _viewport->lifetime());

	_members->show();

	setupEmptyRtmp();
	refreshControlsBackground();
	raiseControls();

	_members->desiredHeightValue(
	) | rpl::start_with_next([=] {
		updateMembersGeometry();
	}, _members->lifetime());

	_members->toggleMuteRequests(
	) | rpl::start_with_next([=](MuteRequest request) {
		_call->toggleMute(request);
	}, _callLifetime);

	_members->changeVolumeRequests(
	) | rpl::start_with_next([=](VolumeRequest request) {
		_call->changeVolume(request);
	}, _callLifetime);

	_members->kickParticipantRequests(
	) | rpl::start_with_next([=](not_null<PeerData*> participantPeer) {
		kickParticipant(participantPeer);
	}, _callLifetime);

	_members->addMembersRequests(
	) | rpl::start_with_next([=] {
		if (_call->conference()) {
			addMembers();
		} else if (!_peer->isBroadcast()
			&& Data::CanSend(_peer, ChatRestriction::SendOther, false)
			&& _call->joinAs()->isSelf()) {
			addMembers();
		} else if (const auto channel = _peer->asChannel()) {
			if (channel->hasUsername()) {
				_callShareLinkCallback();
			}
		}
	}, _callLifetime);

	_members->shareLinkRequests(
	) | rpl::start_with_next(shareConferenceLinkCallback(), _callLifetime);

	_call->videoEndpointLargeValue(
	) | rpl::start_with_next([=](const VideoEndpoint &large) {
		if (large && mode() != PanelMode::Wide) {
			enlargeVideo();
		}
		_viewport->showLarge(large);
	}, _callLifetime);
}

Fn<void()> Panel::shareConferenceLinkCallback() {
	return [=] {
		Expects(_call->conference());

		ShowConferenceCallLinkBox(uiShow(), _call->conferenceCall(), {
			.st = DarkConferenceCallLinkStyle(),
		});
	};
}

void Panel::migrationShowShareLink() {
	ShowConferenceCallLinkBox(
		uiShow(),
		_call->conferenceCall(),
		{ .st = DarkConferenceCallLinkStyle() });
}

void Panel::migrationInviteUsers(std::vector<InviteRequest> users) {
	const auto done = [=](InviteResult result) {
		uiShow()->showToast({ ComposeInviteResultToast(result) });
	};
	_call->inviteUsers(std::move(users), crl::guard(this, done));
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
		&_message,
		&_hangup
	};
	for (const auto button : buttons) {
		if (const auto raw = button->data()) {
			raw->raise();
		}
	}
	_mute->raise();
	if (_titleBackground) {
		_titleBackground->raise();
	}
	if (_title) {
		_title->raise();
	}
	if (_viewers) {
		_titleSeparator->raise();
		_viewers->raise();
	}
	if (_menuToggle) {
		_menuToggle->raise();
	}
	if (_recordingMark) {
		_recordingMark->raise();
	}
	if (_pinOnTop) {
		_pinOnTop->raise();
	}
	_messages->raise();
	if (_messageField) {
		_messageField->raise();
	}
	_window->raiseLayers();
	if (_niceTooltip) {
		_niceTooltip->raise();
	}
}

void Panel::setupVideo(not_null<Viewport*> viewport) {
	const auto setupTile = [=](
			const VideoEndpoint &endpoint,
			const std::unique_ptr<GroupCall::VideoTrack> &track) {
		using namespace rpl::mappers;
		const auto row = endpoint.rtmp()
			? _members->rtmpFakeRow(GroupCall::TrackPeer(track)).get()
			: _members->lookupRow(GroupCall::TrackPeer(track));
		Assert(row != nullptr);

		auto pinned = rpl::combine(
			_call->videoEndpointLargeValue(),
			_call->videoEndpointPinnedValue()
		) | rpl::map(_1 == endpoint && _2);
		const auto self = (endpoint.peer == _call->joinAs());
		viewport->add(
			endpoint,
			VideoTileTrack{ GroupCall::TrackPointer(track), row },
			GroupCall::TrackSizeValue(track),
			std::move(pinned),
			self);
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
	crl::on_main(this, [=] {
		updateWideControlsVisibility();
	});
}

void Panel::updateWideControlsVisibility() {
	const auto shown = _showWideControls
		|| (_stickedTooltipClose != nullptr)
		|| _messageTyping.current();
	if (_wideControlsShown == shown) {
		return;
	}
	_viewport->setCursorShown(!_rtmpFull || shown);
	_wideControlsShown = shown;
	_wideControlsAnimation.start(
		[=] { updateButtonsGeometry(); },
		_wideControlsShown ? 0. : 1.,
		_wideControlsShown ? 1. : 0.,
		st::slideWrapDuration);
}

void Panel::subscribeToChanges(not_null<Data::GroupCall*> real) {
	const auto livestream = real->peer()->isBroadcast();
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
				uiShow()->showToast({ (livestream
					? tr::lng_group_call_is_recorded_channel
					: real->recordVideo()
					? tr::lng_group_call_is_recorded_video
					: tr::lng_group_call_is_recorded)(tr::now) });
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
	const auto startedAsVideo = std::make_shared<bool>(real->recordVideo());
	real->recordStartDateChanges(
	) | rpl::map(
		_1 != 0
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool recorded) {
		const auto livestream = _call->peer()->isBroadcast();
		const auto isVideo = real->recordVideo();
		if (recorded) {
			*startedAsVideo = isVideo;
			_call->playSoundRecordingStarted();
		}
		validateRecordingMark(recorded);
		uiShow()->showToast((recorded
			? (livestream
				? tr::lng_group_call_recording_started_channel
				: isVideo
				? tr::lng_group_call_recording_started_video
				: tr::lng_group_call_recording_started)
			: _call->recordingStoppedByMe()
			? ((*startedAsVideo)
				? tr::lng_group_call_recording_saved_video
				: tr::lng_group_call_recording_saved)
			: (livestream
				? tr::lng_group_call_recording_stopped_channel
				: tr::lng_group_call_recording_stopped))(
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

	_call->messagesEnabledValue() | rpl::start_with_next([=] {
		updateButtonsGeometry();
		raiseControls();
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

void Panel::createPinOnTop() {
	_pinOnTop.create(widget(), st::groupCallPinOnTop);
	const auto pinned = [=] {
		const auto handle = window()->windowHandle();
		return handle && (handle->flags() & Qt::WindowStaysOnTopHint);
	};
	const auto pin = [=](bool pin) {
		if (const auto handle = window()->windowHandle()) {
			handle->setFlag(Qt::WindowStaysOnTopHint, pin);
			_pinOnTop->setIconOverride(
				pin ? &st::groupCallPinnedOnTop : nullptr,
				pin ? &st::groupCallPinnedOnTop : nullptr);
			if (!_pinOnTop->isHidden()) {
				uiShow()->showToast({ pin
					? tr::lng_group_call_pinned_on_top(tr::now)
					: tr::lng_group_call_unpinned_on_top(tr::now) });
			}
		}
	};
	_fullScreenOrMaximized.value(
	) | rpl::start_with_next([=](bool fullScreenOrMaximized) {
		_window->setControlsStyle(fullScreenOrMaximized
			? st::callTitle
			: st::groupCallTitle);

		_pinOnTop->setVisible(!fullScreenOrMaximized);
		if (fullScreenOrMaximized) {
#ifdef Q_OS_WIN
			UnpinMaximized(window());
			_unpinnedMaximized = true;
#else // Q_OS_WIN
			pin(false);
#endif // Q_OS_WIN

			_viewport->rp()->events(
			) | rpl::filter([](not_null<QEvent*> event) {
				return (event->type() == QEvent::MouseMove);
			}) | rpl::start_with_next([=] {
				_hideControlsTimer.callOnce(kHideControlsTimeout);
				toggleWideControls(true);
			}, _hideControlsTimerLifetime);

			_hideControlsTimer.callOnce(kHideControlsTimeout);
		} else {
			if (_unpinnedMaximized) {
				pin(false);
			}
			_hideControlsTimerLifetime.destroy();
			_hideControlsTimer.cancel();
			refreshTitleGeometry();
		}
		refreshTitleBackground();
		updateMembersGeometry();
	}, _pinOnTop->lifetime());

	_pinOnTop->setClickedCallback([=] {
		pin(!pinned());
	});

	updateControlsGeometry();
}

void Panel::refreshTopButton() {
	if (_call->rtmp() && !_pinOnTop) {
		createPinOnTop();
	}
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
			raiseControls();
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
	if (auto box = ScreenSharingPrivacyRequestBox()) {
		uiShow()->showBox(std::move(box));
	}
}

void Panel::chooseShareScreenSource() {
	if (_call->emitShareScreenError()) {
		return;
	}
	const auto choose = [=] {
		const auto env = &Core::App().mediaDevices();
		if (!env->desktopCaptureAllowed()) {
			screenSharingPrivacyRequest();
		} else if (const auto source = env->uniqueDesktopCaptureSource()) {
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
	const auto shared = std::make_shared<base::weak_qptr<Ui::GenericBox>>();
	const auto done = [=] {
		if (*shared) {
			base::take(*shared)->closeBox();
		}
		choose();
	};
	auto box = ConfirmBox({
		.text = text,
		.confirmed = done,
		.confirmText = tr::lng_continue(),
	});
	*shared = box.data();
	uiShow()->showBox(std::move(box));
}

void Panel::chooseJoinAs() {
	const auto context = ChooseJoinAsProcess::Context::Switch;
	const auto callback = [=](JoinInfo info) {
		_call->rejoinAs(info);
	};
	_joinAsProcess.start(
		_peer,
		context,
		uiShow(),
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
		[=](auto box) { uiShow()->showBox(std::move(box)); });
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
		trackControl(_menu, _trackControlsMenuLifetime);

		const auto x = st::groupCallWideMenuPosition.x();
		const auto y = st::groupCallWideMenuPosition.y();
		_menu->moveToLeft(
			_wideMenu->x() + x,
			_wideMenu->y() - _menu->height() + y);
		_menu->showAnimated(Ui::PanelAnimation::Origin::BottomLeft);
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
	const auto &appConfig = _call->peer()->session().appConfig();
	const auto conferenceLimit = appConfig.confcallSizeLimit();
	if (_call->conference()
		&& _call->conferenceCall()->fullCount() >= conferenceLimit) {
		uiShow()->showToast({ tr::lng_group_call_invite_limit(tr::now) });
	}
	const auto showToastCallback = [=](TextWithEntities &&text) {
		uiShow()->showToast(std::move(text));
	};
	const auto link = _call->conference()
		? shareConferenceLinkCallback()
		: nullptr;
	if (auto box = PrepareInviteBox(_call, showToastCallback, link)) {
		uiShow()->showBox(std::move(box));
	}
}

void Panel::kickParticipant(not_null<PeerData*> participantPeer) {
	uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box.get(),
				(!participantPeer->isUser()
					? (_peer->isBroadcast()
						? tr::lng_group_call_remove_channel_from_channel
						: tr::lng_group_call_remove_channel)(
							tr::now,
							lt_channel,
							participantPeer->name())
					: (_call->conference()
						? tr::lng_confcall_sure_remove
						: _peer->isBroadcast()
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
	if (_call->conference()) {
		if (const auto user = participantPeer->asUser()) {
			_call->removeConferenceParticipants({ peerToUser(user->id) });
		}
	} else if (const auto chat = _peer->asChat()) {
		chat->session().api().chatParticipants().kick(chat, participantPeer);
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
		channel->session().api().chatParticipants().kick(
			channel,
			participantPeer,
			currentRestrictedRights);
	}
}

void Panel::initLayout(ConferencePanelMigration info) {
	initGeometry(info);

	_window->raiseControls();

	_window->controlsLayoutChanges(
	) | rpl::start_with_next([=] {
		// _menuToggle geometry depends on _controls arrangement.
		crl::on_main(this, [=] { updateControlsGeometry(); });
	}, lifetime());

	raiseControls();
	updateControlsGeometry();
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
	return _lifetime;
}

void Panel::initGeometry(ConferencePanelMigration info) {
	const auto minWidth = _call->rtmp()
		? st::groupCallWidthRtmpMin
		: st::groupCallWidth;
	const auto minHeight = _call->rtmp()
		? st::groupCallHeightRtmpMin
		: st::groupCallHeight;
	if (!info.window) {
		const auto center = Core::App().getPointForCallPanelCenter();
		const auto width = _call->rtmp()
			? st::groupCallWidthRtmp
			: st::groupCallWidth;
		const auto height = _call->rtmp()
			? st::groupCallHeightRtmp
			: st::groupCallHeight;
		const auto rect = QRect(0, 0, width, height);
		window()->setGeometry(rect.translated(center - rect.center()));
	}
	window()->setMinimumSize({ minWidth, minHeight });
	window()->show();
}

QRect Panel::computeTitleRect() const {
	const auto skip = st::groupCallTitleSeparator;
	const auto remove = skip
		+ (_menuToggle
			? (_menuToggle->width() + st::groupCallMenuTogglePosition.x())
			: 0)
		+ (_joinAsToggle
			? (_joinAsToggle->width() + st::groupCallMenuTogglePosition.x())
			: 0)
		+ (_pinOnTop
			? (_pinOnTop->width() + skip)
			: 0);
	const auto width = widget()->width();
#ifdef Q_OS_MAC
	return QRect(70, 0, width - remove - 70, 28);
#else // Q_OS_MAC
	const auto controls = _window->controlsGeometry();
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
	const auto wide = _call->rtmp()
		|| (_call->hasVideoWithFrames()
			&& (widget()->width() >= st::groupCallWideModeWidthMin));
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
	refreshTitleColors();
	if (wide && _subtitle) {
		_subtitle.destroy();
	} else if (!wide && !_subtitle) {
		refreshTitle();
	} else if (!_members) {
		setupMembers();
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
	if (_message) {
		_message->setStyle(
			wide ? st::groupCallMessageSmall : st::groupCallMessage,
			(wide
				? &st::groupCallMessageActiveSmall
				: &st::groupCallMessageActive));
		_message->setText(wide
			? rpl::single(QString())
			: tr::lng_group_call_message());
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

void Panel::setupEmptyRtmp() {
	_call->emptyRtmpValue(
	) | rpl::start_with_next([=](bool empty) {
		if (!empty) {
			_emptyRtmp.destroy();
			return;
		} else if (_emptyRtmp) {
			return;
		}
		struct Label {
			Label(
				QWidget *parent,
				rpl::producer<QString> text,
				const style::color &color)
			: widget(parent, std::move(text), st::groupCallVideoLimitLabel)
			, corners(st::groupCallControlsBackRadius, color) {
			}

			Ui::FlatLabel widget;
			Ui::RoundRect corners;
		};
		_emptyRtmp.create(widget());
		const auto label = _emptyRtmp->lifetime().make_state<Label>(
			_emptyRtmp.data(),
			(_call->rtmpInfo().url.isEmpty()
				? tr::lng_group_call_no_stream(
					lt_group,
					rpl::single(_peer->name()))
				: tr::lng_group_call_no_stream_admin()),
			_controlsBackgroundColor.color());
		_emptyRtmp->setAttribute(Qt::WA_TransparentForMouseEvents);
		_emptyRtmp->show();
		_emptyRtmp->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(_emptyRtmp.data());
			label->corners.paint(p, _emptyRtmp->rect());
		}, _emptyRtmp->lifetime());

		widget()->sizeValue(
		) | rpl::start_with_next([=](QSize size) {
			const auto padding = st::groupCallWidth / 30;
			const auto width = std::min(
				size.width() - padding * 4,
				st::groupCallWidth);
			label->widget.resizeToWidth(width);
			label->widget.move(padding, padding);
			_emptyRtmp->resize(
				width + 2 * padding,
				label->widget.height() + 2 * padding);
			_emptyRtmp->move(
				(size.width() - _emptyRtmp->width()) / 2,
				(size.height() - _emptyRtmp->height()) / 3);
		}, _emptyRtmp->lifetime());

		raiseControls();
	}, lifetime());

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

void Panel::refreshTitleBackground() {
	if (!_rtmpFull) {
		_titleBackground.destroy();
		return;
	} else if (_titleBackground) {
		return;
	}
	_titleBackground.create(widget());
	_titleBackground->show();
	raiseControls();
	auto &lifetime = _titleBackground->lifetime();
	const auto corners = lifetime.make_state<Ui::RoundRect>(
		st::roundRadiusLarge,
		_controlsBackgroundColor.color());
	_titleBackground->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_titleBackground.data());
		corners->paintSomeRounded(
			p,
			_titleBackground->rect(),
			RectPart::FullBottom);
	}, lifetime);
	refreshTitleGeometry();
}

void Panel::setupControlsBackgroundNarrow() {
	_controlsBackgroundNarrow = std::make_unique<ControlsBackgroundNarrow>(
		widget());
	_controlsBackgroundNarrow->shadow.show();
	_controlsBackgroundNarrow->blocker.show();
	auto &lifetime = _controlsBackgroundNarrow->shadow.lifetime();

	const auto factor = style::DevicePixelRatio();
	const auto height = std::max(
		st::groupCallMembersShadowHeight,
		st::groupCallMembersFadeSkip + st::groupCallMembersFadeHeight);
	_controlsBackgroundNarrow->shadowHeight = height;
	const auto full = lifetime.make_state<QImage>(
		QSize(1, height * factor),
		QImage::Format_ARGB32_Premultiplied);
	rpl::single(rpl::empty) | rpl::then(
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
			Images::GenerateShadow(
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
			Images::GenerateShadow(
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
		const auto bottom = _controlsBackgroundNarrow->shadowHeight;
		const auto faded = clip.intersected(inner).intersected(
			QRect(clip.x(), 0, clip.width(), bottom));
		if (!faded.isEmpty()) {
			const auto factor = style::DevicePixelRatio();
			p.drawImage(
				faded,
				*full,
				QRect(
					0,
					faded.y() * factor,
					full->width(),
					faded.height() * factor));
		}
		const auto after = clip.intersected(QRect(
			inner.x(),
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
	const auto corners = lifetime.make_state<Ui::RoundRect>(
		st::groupCallControlsBackRadius,
		_controlsBackgroundColor.color());
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
	if (_rtmpFull) {
		return;
	} else if (_stickedTooltipClose) {
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
	static const auto kHasCamera = !Core::App().mediaDevices().defaultId(
		Webrtc::DeviceType::Camera).isEmpty();
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
		&& !_call->mutedByAdmin()
		&& !_window->topShownLayer()) {
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
		} else if (control == _message.data()) {
			return tr::lng_group_call_message();
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
		label->resizeToWidth(label->textMaxWidth());
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
	const auto weak = base::make_weak(tooltip);
	const auto destroy = [=] {
		delete weak.get();
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
	const auto weak = base::make_weak(_niceTooltip);
	const auto countPosition = [=](QSize size) {
		const auto strong = weak.get();
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

void Panel::trackControls(bool track, bool force) {
	if (!force && _trackControls == track) {
		return;
	}
	_trackControls = track;
	_trackControlsOverStateLifetime.destroy();
	_trackControlsMenuLifetime.destroy();
	if (!track) {
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
	trackOne(_message);
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
	const auto controlsPadding = 0;
#else // Q_OS_MAC
	const auto center = _window->controlsGeometry().center();
	const auto controlsOnTheLeft = center.x()
		< widget()->width() / 2;
	const auto controlsPadding = _window->controlsWrapTop();
#endif // Q_OS_MAC
	const auto menux = st::groupCallMenuTogglePosition.x();
	const auto menuy = st::groupCallMenuTogglePosition.y();
	if (controlsOnTheLeft) {
		if (_pinOnTop) {
			_pinOnTop->moveToRight(controlsPadding, controlsPadding);
		}
		if (_menuToggle) {
			_menuToggle->moveToRight(menux, menuy);
		} else if (_joinAsToggle) {
			_joinAsToggle->moveToRight(menux, menuy);
		}
	} else {
		if (_pinOnTop) {
			_pinOnTop->moveToLeft(controlsPadding, controlsPadding);
		}
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
	const auto messagesEnabled = _call->messagesEnabled();
	auto messagesBottomSkip = 0;
	if (mode() == PanelMode::Wide) {
		Assert(_video != nullptr);
		Assert(_message != nullptr);
		Assert(_screenShare != nullptr);
		Assert(_wideMenu != nullptr);
		Assert(_settings != nullptr);
		Assert(_callShare == nullptr);

		const auto rtmp = _call->rtmp();
		const auto shown = _wideControlsAnimation.value(
			_wideControlsShown ? 1. : 0.);
		const auto hidden = (shown == 0.);

		if (_viewport) {
			_viewport->setControlsShown(rtmp ? 0. : shown);
		}

		const auto addSkip = st::callMuteButtonSmall.active.outerRadius;
		const auto muteSize = _mute->innerSize().width() + 2 * addSkip;
		const auto skip = st::groupCallButtonSkipSmall;
		const auto fullWidth = (rtmp ? 0 : (_video->width() + skip))
			+ (rtmp ? 0 : (_message->width() + skip))
			+ (muteSize + skip)
			+ (_settings->width() + skip)
			+ _hangup->width();
		const auto membersSkip = st::groupCallNarrowSkip;
		const auto membersWidth = rtmp
			? membersSkip
			: (st::groupCallNarrowMembersWidth + 2 * membersSkip);
		auto left = membersSkip + (widget()->width()
			- membersWidth
			- membersSkip
			- fullWidth) / 2;

		const auto forMessagesLeft = left
			- st::groupCallControlsBackMargin.left();
		const auto forMessagesWidth = fullWidth
			+ st::groupCallControlsBackMargin.left()
			+ st::groupCallControlsBackMargin.right();
		const auto existingBottomSkip = st::groupCallButtonBottomSkipWide
			- _hangup->height()
			- st::groupCallControlsBackMargin.bottom();
		if (_messageField) {
			_messageField->resizeToWidth(forMessagesWidth);
			messagesBottomSkip += _messageField->height();

			const auto y = widget()->height()
				- messagesBottomSkip
				- (existingBottomSkip / 2);
			_messageField->move(forMessagesLeft, y);
		}

		const auto buttonsTop = widget()->height()
			- messagesBottomSkip
			- anim::interpolate(0, st::groupCallButtonBottomSkipWide, shown);
		const auto muteTop = buttonsTop + addSkip;

		const auto forMessagesBottom = buttonsTop
			- st::groupCallControlsBackMargin.top()
			- (existingBottomSkip / 6);
		const auto forMessagesHeight = forMessagesBottom
			- (st::groupCallWideVideoTop * 1.5);

		_messages->move(
			forMessagesLeft,
			forMessagesBottom,
			forMessagesWidth,
			forMessagesHeight);

		toggle(_screenShare, !hidden && !rtmp && !messagesEnabled);
		toggle(_message, !hidden && !rtmp && messagesEnabled);
		if (!rtmp && !messagesEnabled) {
			_screenShare->moveToLeft(left, buttonsTop);
			left += _screenShare->width() + skip;
		} else if (!rtmp) {
			_wideMenu->moveToLeft(left, buttonsTop);
			_settings->moveToLeft(left, buttonsTop);
			left += _settings->width() + skip;
		}

		const auto wideMenuShown = _call->canManage()
			|| _call->showChooseJoinAs();
		toggle(_settings, !hidden && !wideMenuShown);
		toggle(_wideMenu, !hidden && wideMenuShown);

		toggle(_video, !hidden && !rtmp);
		if (!rtmp) {
			_video->moveToLeft(left, buttonsTop);
			left += _video->width() + skip;
		} else {
			_wideMenu->moveToLeft(left, buttonsTop);
			_settings->moveToLeft(left, buttonsTop);
			left += _settings->width() + skip;
		}
		toggle(_mute, !hidden);
		_mute->moveInner({ left + addSkip, muteTop });
		left += muteSize + skip;
		if (!rtmp && messagesEnabled) {
			_message->moveToLeft(left, buttonsTop);
			left += _message->width() + skip;
		} else if (!rtmp) {
			_wideMenu->moveToLeft(left, buttonsTop);
			_settings->moveToLeft(left, buttonsTop);
			left += _settings->width() + skip;
		}
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
		if (_rtmpFull) {
			refreshTitleGeometry();
		}
	} else {
		const auto addSkip = st::callMuteButton.active.outerRadius;
		const auto muteSize = _mute->innerSize().width();
		const auto single = (_settings ? _settings : _callShare)->width();
		const auto showVideoButton = videoButtonInNarrowMode();
		const auto four = !_callShare && !showVideoButton && messagesEnabled;
		const auto five = !four && !_callShare && messagesEnabled;
		const auto buttonSkip = four
			? (st::groupCallButtonSkip / 2)
			: five
			? ((st::groupCallWidth - 5 * single) / 6)
			: st::groupCallButtonSkip;
		const auto fullWidth = five
			? st::groupCallWidth
			: four
			? (4 * single + 3 * buttonSkip)
			: (muteSize + 2 * (single + st::groupCallButtonSkip));
		const auto forMessagesWidth = st::groupCallWidth
			- st::groupCallMembersMargin.left()
			- st::groupCallMembersMargin.right();
		const auto forMessagesLeft = (widget()->width() - forMessagesWidth)
			/ 2;
		const auto existingBottomSkip = st::groupCallButtonBottomSkip
			- _mute->innerSize().height();
		if (_messageField) {
			_messageField->resizeToWidth(forMessagesWidth);

			const auto height = _messageField->height();
			messagesBottomSkip += height;

			const auto y = widget()->height()
				- messagesBottomSkip
				- (existingBottomSkip / 3);
			_messageField->move(forMessagesLeft, y);
		}

		const auto buttonsTop = widget()->height()
			- messagesBottomSkip
			- st::groupCallButtonBottomSkip;
		const auto muteTop = buttonsTop + addSkip;
		//const auto muteTop = widget()->height()
		//	- messagesBottomSkip
		//	- st::groupCallMuteBottomSkip;
		const auto forMessagesBottom = muteTop
			- (existingBottomSkip / 3);
		const auto forMessagesHeight = forMessagesBottom
			- st::groupCallMembersTop
			- (st::normalFont->height / 2);

		_messages->move(
			forMessagesLeft,
			forMessagesBottom,
			forMessagesWidth,
			forMessagesHeight);

		toggle(_mute, true);
		const auto leftButtonLeft = (widget()->width() - fullWidth) / 2
			+ (five ? buttonSkip : 0);
		const auto nextButtonLeft = leftButtonLeft
			+ ((five || four) ? (single + buttonSkip) : 0);
		const auto muteButtonLeft = four
			? (nextButtonLeft + addSkip)
			: ((widget()->width() - muteSize) / 2);
		_mute->moveInner({ muteButtonLeft, muteTop });
		toggle(_screenShare, false);
		toggle(_wideMenu, false);
		toggle(_callShare, true);
		if (_callShare) {
			_callShare->moveToLeft(leftButtonLeft, buttonsTop);
		}
		toggle(_video, !_callShare && showVideoButton);
		if (_video) {
			_video->setStyle(st::groupCallVideo, &st::groupCallVideoActive);
			_video->moveToLeft(leftButtonLeft, buttonsTop);
		}
		toggle(_settings, !_callShare && (five || !showVideoButton));
		if (_settings) {
			_settings->moveToLeft(
				four ? leftButtonLeft : nextButtonLeft,
				buttonsTop);
		}
		toggle(_message, !_callShare && messagesEnabled);
		if (_message) {
			_message->moveToRight(nextButtonLeft, buttonsTop);
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
				- messagesBottomSkip
				- st::groupCallMembersMargin.bottom()
				- _controlsBackgroundNarrow->shadowHeight),
			width,
			messagesBottomSkip + _controlsBackgroundNarrow->shadowHeight);
		_controlsBackgroundNarrow->blocker.setGeometry(
			left,
			(widget()->height()
				- messagesBottomSkip
				- st::groupCallMembersMargin.bottom()
				- st::groupCallMembersBottomSkip),
			width,
			messagesBottomSkip + st::groupCallMembersBottomSkip);
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
	_members->setVisible(!_call->rtmp());
	const auto desiredHeight = _members->desiredHeight();
	if (mode() == PanelMode::Wide) {
		const auto skip = _rtmpFull ? 0 : st::groupCallNarrowSkip;
		const auto membersWidth = st::groupCallNarrowMembersWidth;
		const auto top = _rtmpFull ? 0 : st::groupCallWideVideoTop;
		_members->setGeometry(
			widget()->width() - skip - membersWidth,
			top,
			membersWidth,
			std::min(desiredHeight, widget()->height() - top - skip));
		const auto viewportSkip = _call->rtmp()
			? 0
			: (skip + membersWidth);
		_viewport->setGeometry(_rtmpFull, {
			skip,
			top,
			widget()->width() - viewportSkip - 2 * skip,
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

rpl::producer<QString> Panel::titleText() {
	if (_call->conference()) {
		return tr::lng_confcall_join_title();
	}
	return rpl::combine(
		Info::Profile::NameValue(_peer),
		rpl::single(
			QString()
		) | rpl::then(_call->real(
		) | rpl::map([=](not_null<Data::GroupCall*> real) {
		return real->titleValue();
	}) | rpl::flatten_latest())
	) | rpl::map([=](const QString &name, const QString &title) {
		return title.isEmpty() ? name : title;
	});
}

void Panel::refreshTitle() {
	if (!_title) {
		auto text = titleText() | rpl::after_next([=] {
			refreshTitleGeometry();
		});
		_title.create(
			widget(),
			rpl::duplicate(text),
			st::groupCallTitleLabel);
		_title->show();
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
		if (_call->rtmp()) {
			_titleSeparator.create(
				widget(),
				rpl::single(QString::fromUtf8("\xE2\x80\xA2")),
				st::groupCallTitleLabel);
			_titleSeparator->show();
			_titleSeparator->setAttribute(Qt::WA_TransparentForMouseEvents);
			auto countText = _call->real(
			) | rpl::map([=](not_null<Data::GroupCall*> real) {
				return tr::lng_group_call_rtmp_viewers(
					lt_count_decimal,
					real->fullCountValue(
					) | rpl::map([=](int count) {
						return std::max(float64(count), 1.);
					}));
			}) | rpl::flatten_latest(
			) | rpl::after_next([=] {
				refreshTitleGeometry();
			});
			_viewers.create(
				widget(),
				std::move(countText),
				st::groupCallTitleLabel);
			_viewers->show();
			_viewers->setAttribute(Qt::WA_TransparentForMouseEvents);
		}

		refreshTitleColors();
		style::PaletteChanged(
		) | rpl::start_with_next([=] {
			refreshTitleColors();
		}, _title->lifetime());
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
	const auto sep = st::groupCallTitleSeparator;
	const auto best = _title->textMaxWidth() + (_viewers
		? (_titleSeparator->width() + sep * 2 + _viewers->textMaxWidth())
		: 0);
	const auto from = (widget()->width() - best) / 2;
	const auto shownTop = (mode() == PanelMode::Default)
		? st::groupCallTitleTop
		: (st::groupCallWideVideoTop
			- st::groupCallTitleLabel.style.font->height) / 2;
	const auto shown = _rtmpFull
		? _wideControlsAnimation.value(_wideControlsShown ? 1. : 0.)
		: 1.;
	const auto top = anim::interpolate(
		-_title->height() - st::boxRadius,
		shownTop,
		shown);
	const auto left = titleRect.x();

	const auto notEnough = std::max(0, best - titleRect.width());
	const auto titleMaxWidth = _title->textMaxWidth();
	const auto viewersMaxWidth = _viewers ? _viewers->textMaxWidth() : 0;
	const auto viewersNotEnough = std::clamp(
		viewersMaxWidth - titleMaxWidth,
		0,
		notEnough
	) + std::max(
		(notEnough - std::abs(viewersMaxWidth - titleMaxWidth)) / 2,
		0);
	_title->resizeToWidth(
		_title->textMaxWidth() - (notEnough - viewersNotEnough));
	if (_viewers) {
		_viewers->resizeToWidth(_viewers->textMaxWidth() - viewersNotEnough);
	}
	const auto layout = [&](int position) {
		_title->moveToLeft(position, top);
		position += _title->width();
		if (_viewers) {
			_titleSeparator->moveToLeft(position + sep, top);
			position += sep + _titleSeparator->width() + sep;
			_viewers->moveToLeft(position, top);
			position += _viewers->width();
		}
		if (_recordingMark) {
			const auto markTop = top + st::groupCallRecordingMarkTop;
			_recordingMark->move(
				position,
				markTop - st::groupCallRecordingMarkSkip);
		}
		if (_titleBackground) {
			const auto bottom = _title->y()
				+ _title->height()
				+ (st::boxRadius / 2);
			const auto height = std::max(bottom, st::boxRadius * 2);
			_titleBackground->setGeometry(
				_title->x() - st::boxRadius,
				bottom - height,
				(position - _title->x()
					+ st::boxRadius
					+ (_recordingMark
						? (_recordingMark->width() + st::boxRadius / 2)
						: st::boxRadius)),
				height);
		}
	};

	if (from >= left && from + best <= left + titleRect.width()) {
		layout(from);
	} else if (titleRect.width() < best) {
		layout(left);
	} else if (from < left) {
		layout(left);
	} else {
		layout(left + titleRect.width() - best);
	}
	_window->setControlsShown(shown);
}

void Panel::refreshTitleColors() {
	if (!_title) {
		return;
	}
	auto gray = st::groupCallMemberNotJoinedStatus->c;
	const auto wide = (_mode.current() == PanelMode::Wide);
	_title->setTextColorOverride(wide
		? std::make_optional(gray)
		: std::nullopt);
	if (_viewers) {
		_viewers->setTextColorOverride(gray);
		gray.setAlphaF(gray.alphaF() * 0.5);
		_titleSeparator->setTextColorOverride(gray);
	}
}

void Panel::paint(QRect clip) {
	auto p = QPainter(widget());

	auto region = QRegion(clip);
	for (const auto &rect : region) {
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

not_null<Window*> Panel::callWindow() const {
	return _window.get();
}

not_null<Ui::RpWindow*> Panel::window() const {
	return _window->window();
}

not_null<Ui::RpWidget*> Panel::widget() const {
	return _window->widget();
}

} // namespace Calls::Group
