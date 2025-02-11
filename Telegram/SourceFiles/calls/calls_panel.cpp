/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_panel.h"

#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_cloud_file.h"
#include "data/data_changes.h"
#include "calls/group/calls_group_common.h"
#include "calls/ui/calls_device_menu.h"
#include "calls/calls_emoji_fingerprint.h"
#include "calls/calls_signal_bars.h"
#include "calls/calls_userpic.h"
#include "calls/calls_video_bubble.h"
#include "calls/calls_video_incoming.h"
#include "ui/platform/ui_platform_window_title.h"
#include "ui/widgets/call_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/rp_window.h"
#include "ui/layers/layer_manager.h"
#include "ui/layers/generic_box.h"
#include "ui/image/image.h"
#include "ui/text/format_values.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_shader.h"
#include "ui/toast/toast.h"
#include "ui/empty_userpic.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/integration.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "platform/platform_specific.h"
#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "base/power_save_blocker.h"
#include "media/streaming/media_streaming_utility.h"
#include "window/main_window.h"
#include "webrtc/webrtc_environment.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"

#include <QtWidgets/QApplication>
#include <QtGui/QWindow>
#include <QtCore/QTimer>
#include <QtSvg/QSvgRenderer>

namespace Calls {
namespace {

constexpr auto kHideControlsTimeout = 5 * crl::time(1000);
constexpr auto kHideControlsQuickTimeout = 2 * crl::time(1000);

[[nodiscard]] QByteArray BatterySvg(
		const QSize &s,
		const QColor &c) {
	const auto color = u"rgb(%1,%2,%3)"_q
		.arg(c.red())
		.arg(c.green())
		.arg(c.blue())
		.toUtf8();
	const auto width = QString::number(s.width()).toUtf8();
	const auto height = QString::number(s.height()).toUtf8();
	return R"(
<svg width=")" + width + R"(" height=")" + height
	+ R"(" viewBox="0 0 )" + width + R"( )" + height + R"(" fill="none">
	<rect x="1.33598" y="0.5" width="24" height="12" rx="4" stroke=")" + color + R"("/>
	<path
		d="M26.836 4.66666V8.66666C27.6407 8.32788 28.164 7.53979 28.164 6.66666C28.164 5.79352 27.6407 5.00543 26.836 4.66666Z"
		fill=")" + color + R"("/>
	<path
		d="M 5.5 3.5 H 5.5 A 0.5 0.5 0 0 1 6 4 V 9 A 0.5 0.5 0 0 1 5.5 9.5 H 5.5 A 0.5 0.5 0 0 1 5 9 V 4 A 0.5 0.5 0 0 1 5.5 3.5 Z M 5 4 V 9 A 0.5 0.5 0 0 0 5.5 9.5 H 5.5 A 0.5 0.5 0 0 0 6 9 V 4 A 0.5 0.5 0 0 0 5.5 3.5 H 5.5 A 0.5 0.5 0 0 0 5 4 Z"
		transform="matrix(1, 0, 0, 1, 0, 0)" + ")\" stroke=\"" + color + R"("/>
</svg>)";
}

} // namespace

Panel::Panel(not_null<Call*> call)
: _call(call)
, _user(call->user())
, _layerBg(std::make_unique<Ui::LayerManager>(widget()))
#ifndef Q_OS_MAC
, _controls(Ui::Platform::SetupSeparateTitleControls(
	window(),
	st::callTitle,
	[=](bool maximized) { toggleFullScreen(maximized); }))
#endif // !Q_OS_MAC
, _bodySt(&st::callBodyLayout)
, _answerHangupRedial(widget(), st::callAnswer, &st::callHangup)
, _decline(widget(), object_ptr<Ui::CallButton>(widget(), st::callHangup))
, _cancel(widget(), object_ptr<Ui::CallButton>(widget(), st::callCancel))
, _screencast(
	widget(),
	object_ptr<Ui::CallButton>(
		widget(),
		st::callScreencastOn,
		&st::callScreencastOff))
, _camera(widget(), st::callCameraMute, &st::callCameraUnmute)
, _mute(
	widget(),
	object_ptr<Ui::CallButton>(
		widget(),
		st::callMicrophoneMute,
		&st::callMicrophoneUnmute))
, _name(widget(), st::callName)
, _status(widget(), st::callStatus)
, _hideControlsTimer([=] { requestControlsHidden(true); })
, _controlsShownForceTimer([=] { controlsShownForce(false); }) {
	_layerBg->setStyleOverrides(&st::groupCallBox, &st::groupCallLayerBox);
	_layerBg->setHideByBackgroundClick(true);

	_decline->setDuration(st::callPanelDuration);
	_decline->entity()->setText(tr::lng_call_decline());
	_cancel->setDuration(st::callPanelDuration);
	_cancel->entity()->setText(tr::lng_call_cancel());
	_screencast->setDuration(st::callPanelDuration);

	initWindow();
	initWidget();
	initControls();
	initLayout();
	initMediaDeviceToggles();
	showAndActivate();
}

Panel::~Panel() = default;

bool Panel::isVisible() const {
	return window()->isVisible()
		&& !(window()->windowState() & Qt::WindowMinimized);
}

bool Panel::isActive() const {
	return window()->isActiveWindow() && isVisible();
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

void Panel::minimize() {
	window()->setWindowState(window()->windowState() | Qt::WindowMinimized);
}

void Panel::toggleFullScreen() {
	toggleFullScreen(!window()->isFullScreen());
}

void Panel::replaceCall(not_null<Call*> call) {
	reinitWithCall(call);
	updateControlsGeometry();
}

void Panel::initWindow() {
	window()->setAttribute(Qt::WA_OpaquePaintEvent);
	window()->setAttribute(Qt::WA_NoSystemBackground);
	window()->setTitle(_user->name());
	window()->setTitleStyle(st::callTitle);

	base::install_event_filter(window().get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close && handleClose()) {
			e->ignore();
			return base::EventFilterResult::Cancel;
		} else if (e->type() == QEvent::KeyPress) {
			if ((static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Escape)
				&& window()->isFullScreen()) {
				window()->showNormal();
			}
		} else if (e->type() == QEvent::WindowStateChange) {
			const auto state = window()->windowState();
			_fullScreenOrMaximized = (state & Qt::WindowFullScreen)
				|| (state & Qt::WindowMaximized);
		} else if (e->type() == QEvent::Enter) {
			_mouseInside = true;
			Ui::Integration::Instance().registerLeaveSubscription(
				window().get());
			if (!_fullScreenOrMaximized.current()) {
				requestControlsHidden(false);
				_hideControlsTimer.cancel();
			}
		} else if (e->type() == QEvent::Leave) {
			_mouseInside = false;
			Ui::Integration::Instance().unregisterLeaveSubscription(
				window().get());
			if (!_fullScreenOrMaximized.current()) {
				_hideControlsTimer.callOnce(kHideControlsQuickTimeout);
			}
		}
		return base::EventFilterResult::Continue;
	});

	window()->setBodyTitleArea([=](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		if (!widget()->rect().contains(widgetPoint)) {
			return Flag::None | Flag(0);
		}
#ifndef Q_OS_MAC
		using Result = Ui::Platform::HitTestResult;
		const auto windowPoint = widget()->mapTo(window(), widgetPoint);
		if (_controls->controls.hitTest(windowPoint) != Result::None) {
			return Flag::None | Flag(0);
		}
#endif // !Q_OS_MAC
		const auto buttonWidth = st::callCancel.button.width;
		const auto buttonsWidth = buttonWidth * 4;
		const auto inControls = (_fingerprint
			&& _fingerprint->geometry().contains(widgetPoint))
			|| QRect(
				(widget()->width() - buttonsWidth) / 2,
				_answerHangupRedial->y(),
				buttonsWidth,
				_answerHangupRedial->height()).contains(widgetPoint)
			|| (!_outgoingPreviewInBody
				&& _outgoingVideoBubble->geometry().contains(widgetPoint));
		if (inControls) {
			return Flag::None | Flag(0);
		}
		const auto shown = _layerBg->topShownLayer();
		return (!shown || !shown->geometry().contains(widgetPoint))
			? (Flag::Move | Flag::Menu | Flag::FullScreen)
			: Flag::None;
	});

	// Don't do that, it looks awful :(
//#ifdef Q_OS_WIN
//	// On Windows we replace snap-to-top maximizing with fullscreen.
//	//
//	// We have to switch first to showNormal, so that showFullScreen
//	// will remember correct normal window geometry and next showNormal
//	// will show it instead of a moving maximized window.
//	//
//	// We have to do it in InvokeQueued, otherwise it still captures
//	// the maximized window geometry and saves it.
//	//
//	// I couldn't find a less glitchy way to do that *sigh*.
//	const auto object = window()->windowHandle();
//	const auto signal = &QWindow::windowStateChanged;
//	QObject::connect(object, signal, [=](Qt::WindowState state) {
//		if (state == Qt::WindowMaximized) {
//			InvokeQueued(object, [=] {
//				window()->showNormal();
//				InvokeQueued(object, [=] {
//					window()->showFullScreen();
//				});
//			});
//		}
//	});
//#endif // Q_OS_WIN
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
	}, widget()->lifetime());
}

void Panel::initControls() {
	_hangupShown = (_call->type() == Type::Outgoing);
	_mute->entity()->setClickedCallback([=] {
		if (_call) {
			_call->setMuted(!_call->muted());
		}
	});
	_screencast->entity()->setClickedCallback([=] {
		const auto env = &Core::App().mediaDevices();
		if (!_call) {
			return;
		} else if (!env->desktopCaptureAllowed()) {
			if (auto box = Group::ScreenSharingPrivacyRequestBox()) {
				_layerBg->showBox(std::move(box));
			}
		} else if (const auto source = env->uniqueDesktopCaptureSource()) {
			if (!chooseSourceActiveDeviceId().isEmpty()) {
				chooseSourceStop();
			} else {
				chooseSourceAccepted(*source, false);
			}
		} else {
			Group::Ui::DesktopCapture::ChooseSource(this);
		}
	});
	_camera->setClickedCallback([=] {
		if (!_call) {
			return;
		} else {
			_call->toggleCameraSharing(!_call->isSharingCamera());
		}
	});

	_updateDurationTimer.setCallback([this] {
		if (_call) {
			updateStatusText(_call->state());
		}
	});
	_updateOuterRippleTimer.setCallback([this] {
		if (_call) {
			_answerHangupRedial->setOuterValue(
				_call->getWaitingSoundPeakValue());
		} else {
			_answerHangupRedial->setOuterValue(0.);
			_updateOuterRippleTimer.cancel();
		}
	});
	_answerHangupRedial->setClickedCallback([this] {
		if (!_call || _hangupShownProgress.animating()) {
			return;
		}
		auto state = _call->state();
		if (state == State::Busy) {
			_call->redial();
		} else if (_call->isIncomingWaiting()) {
			_call->answer();
		} else if (state == State::WaitingUserConfirmation) {
			_startOutgoingRequests.fire(false);
		} else {
			_call->hangup();
		}
	});
	auto hangupCallback = [this] {
		if (_call) {
			_call->hangup();
		}
	};
	_decline->entity()->setClickedCallback(hangupCallback);
	_cancel->entity()->setClickedCallback(hangupCallback);

	reinitWithCall(_call);

	_decline->finishAnimating();
	_cancel->finishAnimating();
	_screencast->finishAnimating();
}

void Panel::setIncomingSize(QSize size) {
	if (_incomingFrameSize == size) {
		return;
	}
	_incomingFrameSize = size;
	refreshIncomingGeometry();
	showControls();
}

QWidget *Panel::chooseSourceParent() {
	return window().get();
}

QString Panel::chooseSourceActiveDeviceId() {
	return _call->screenSharingDeviceId();
}

bool Panel::chooseSourceActiveWithAudio() {
	return false;// _call->screenSharingWithAudio();
}

bool Panel::chooseSourceWithAudioSupported() {
//#ifdef Q_OS_WIN
//	return true;
//#else // Q_OS_WIN
	return false;
//#endif // Q_OS_WIN
}

rpl::lifetime &Panel::chooseSourceInstanceLifetime() {
	return lifetime();
}

rpl::producer<bool> Panel::startOutgoingRequests() const {
	return _startOutgoingRequests.events(
	) | rpl::filter([=] {
		return _call && (_call->state() == State::WaitingUserConfirmation);
	});
}

void Panel::chooseSourceAccepted(
		const QString &deviceId,
		bool withAudio) {
	_call->toggleScreenSharing(deviceId/*, withAudio*/);
}

void Panel::chooseSourceStop() {
	_call->toggleScreenSharing(std::nullopt);
}

void Panel::refreshIncomingGeometry() {
	Expects(_call != nullptr);
	Expects(_incoming != nullptr);

	if (_incomingFrameSize.isEmpty()) {
		_incoming->widget()->hide();
		return;
	}
	const auto to = widget()->size();
	const auto use = ::Media::Streaming::DecideFrameResize(
		to,
		_incomingFrameSize
	).result;
	const auto pos = QPoint(
		(to.width() - use.width()) / 2,
		(to.height() - use.height()) / 2);
	_incoming->widget()->setGeometry(QRect(pos, use));
	_incoming->widget()->show();
}

void Panel::reinitWithCall(Call *call) {
	_callLifetime.destroy();
	_call = call;
	const auto guard = gsl::finally([&] {
		updateControlsShown();
	});
	if (!_call) {
		_fingerprint.destroy();
		_incoming = nullptr;
		_outgoingVideoBubble = nullptr;
		_powerSaveBlocker = nullptr;
		return;
	}

	_user = _call->user();

	auto remoteMuted = _call->remoteAudioStateValue(
	) | rpl::map(rpl::mappers::_1 == Call::RemoteAudioState::Muted);
	rpl::duplicate(
		remoteMuted
	) | rpl::start_with_next([=](bool muted) {
		if (muted) {
			createRemoteAudioMute();
		} else {
			_remoteAudioMute.destroy();
			showRemoteLowBattery();
		}
	}, _callLifetime);
	_call->remoteBatteryStateValue(
	) | rpl::start_with_next([=](Call::RemoteBatteryState state) {
		if (state == Call::RemoteBatteryState::Low) {
			createRemoteLowBattery();
		} else {
			_remoteLowBattery.destroy();
		}
	}, _callLifetime);
	_userpic = std::make_unique<Userpic>(
		widget(),
		_user,
		std::move(remoteMuted));
	_outgoingVideoBubble = std::make_unique<VideoBubble>(
		widget(),
		_call->videoOutgoing());
	_incoming = std::make_unique<Incoming>(
		widget(),
		_call->videoIncoming(),
		_window.backend());
	_incoming->widget()->hide();

	_incoming->rp()->shownValue() | rpl::start_with_next([=] {
		updateControlsShown();
	}, _incoming->rp()->lifetime());

	_hideControlsFilter = nullptr;
	_fullScreenOrMaximized.value(
	) | rpl::start_with_next([=](bool fullScreenOrMaximized) {
		if (fullScreenOrMaximized) {
			class Filter final : public QObject {
			public:
				explicit Filter(Fn<void(QObject*)> moved) : _moved(moved) {
					qApp->installEventFilter(this);
				}

				bool eventFilter(QObject *watched, QEvent *event) {
					if (event->type() == QEvent::MouseMove) {
						_moved(watched);
					}
					return false;
				}

			private:
				Fn<void(QObject*)> _moved;

			};
			_hideControlsFilter.reset(new Filter([=](QObject *what) {
				_mouseInside = true;
				if (what->isWidgetType()
					&& window()->isAncestorOf(static_cast<QWidget*>(what))) {
					_hideControlsTimer.callOnce(kHideControlsTimeout);
					requestControlsHidden(false);
					updateControlsShown();
				}
			}));
			_hideControlsTimer.callOnce(kHideControlsTimeout);
		} else {
			_hideControlsFilter = nullptr;
			_hideControlsTimer.cancel();
			if (_mouseInside) {
				requestControlsHidden(false);
				updateControlsShown();
			}
		}
	}, _incoming->rp()->lifetime());

	_call->mutedValue(
	) | rpl::start_with_next([=](bool mute) {
		_mute->entity()->setProgress(mute ? 1. : 0.);
		_mute->entity()->setText(mute
			? tr::lng_call_unmute_audio()
			: tr::lng_call_mute_audio());
	}, _callLifetime);

	_call->videoOutgoing()->stateValue(
	) | rpl::start_with_next([=] {
		{
			const auto active = _call->isSharingCamera();
			_camera->setProgress(active ? 0. : 1.);
			_camera->setText(active
				? tr::lng_call_stop_video()
				: tr::lng_call_start_video());
		}
		{
			const auto active = _call->isSharingScreen();
			_screencast->entity()->setProgress(active ? 0. : 1.);
			_screencast->entity()->setText(tr::lng_call_screencast());
			_outgoingVideoBubble->setMirrored(!active);
		}
	}, _callLifetime);

	_call->stateValue(
	) | rpl::start_with_next([=](State state) {
		stateChanged(state);
	}, _callLifetime);

	_call->videoIncoming()->renderNextFrame(
	) | rpl::start_with_next([=] {
		const auto track = _call->videoIncoming();
		setIncomingSize(track->state() == Webrtc::VideoState::Active
			? track->frameSize()
			: QSize());
		if (_incoming->widget()->isHidden()) {
			return;
		}
		const auto incoming = incomingFrameGeometry();
		const auto outgoing = outgoingFrameGeometry();
		_incoming->widget()->update();
		if (incoming.intersects(outgoing)) {
			widget()->update(outgoing);
		}
	}, _callLifetime);

	_call->videoIncoming()->stateValue(
	) | rpl::start_with_next([=](Webrtc::VideoState state) {
		setIncomingSize((state == Webrtc::VideoState::Active)
			? _call->videoIncoming()->frameSize()
			: QSize());
	}, _callLifetime);

	_call->videoOutgoing()->renderNextFrame(
	) | rpl::start_with_next([=] {
		const auto incoming = incomingFrameGeometry();
		const auto outgoing = outgoingFrameGeometry();
		widget()->update(outgoing);
		if (incoming.intersects(outgoing)) {
			_incoming->widget()->update();
		}
	}, _callLifetime);

	rpl::combine(
		_call->stateValue(),
		rpl::single(
			rpl::empty_value()
		) | rpl::then(_call->videoOutgoing()->renderNextFrame())
	) | rpl::start_with_next([=](State state, auto) {
		if (state != State::Ended
			&& state != State::EndedByOtherDevice
			&& state != State::Failed
			&& state != State::FailedHangingUp
			&& state != State::HangingUp) {
			refreshOutgoingPreviewInBody(state);
		}
	}, _callLifetime);

	_call->errors(
	) | rpl::start_with_next([=](Error error) {
		const auto text = [=] {
			switch (error.type) {
			case ErrorType::NoCamera:
				return tr::lng_call_error_no_camera(tr::now);
			case ErrorType::NotVideoCall:
				return tr::lng_call_error_camera_outdated(
					tr::now,
					lt_user,
					_user->name());
			case ErrorType::NotStartedCall:
				return tr::lng_call_error_camera_not_started(tr::now);
				//case ErrorType::NoMicrophone:
				//	return tr::lng_call_error_no_camera(tr::now);
			case ErrorType::Unknown:
				return Lang::Hard::CallErrorIncompatible();
			}
			Unexpected("Error type in _call->errors().");
		}();
		Ui::Toast::Show(widget(), Ui::Toast::Config{
			.text = { text },
			.st = &st::callErrorToast,
		});
	}, _callLifetime);

	_name->setText(_user->name());
	updateStatusText(_call->state());

	_answerHangupRedial->raise();
	_decline->raise();
	_cancel->raise();
	_camera->raise();
	if (_startVideo) {
		_startVideo->raise();
	}
	_mute->raise();

	_powerSaveBlocker = std::make_unique<base::PowerSaveBlocker>(
		base::PowerSaveBlockType::PreventDisplaySleep,
		u"Video call is active"_q,
		window()->windowHandle());

	_incoming->widget()->lower();
}

void Panel::createRemoteAudioMute() {
	_remoteAudioMute.create(
		widget(),
		object_ptr<Ui::FlatLabel>(
			widget(),
			tr::lng_call_microphone_off(
				lt_user,
				_user->session().changes().peerFlagsValue(
					_user,
					Data::PeerUpdate::Flag::Name
				) | rpl::map([=] { return _user->shortName(); })),
			st::callRemoteAudioMute),
		st::callTooltipPadding);
	_remoteAudioMute->setAttribute(Qt::WA_TransparentForMouseEvents);

	_remoteAudioMute->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_remoteAudioMute);
		const auto r = _remoteAudioMute->rect();

		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(_controlsShownAnimation.value(
			_controlsShown ? 1. : 0.));
		p.setBrush(st::videoPlayIconBg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(r, r.height() / 2, r.height() / 2);

		st::callTooltipMutedIcon.paint(
			p,
			st::callTooltipMutedIconPosition,
			_remoteAudioMute->width());
	}, _remoteAudioMute->lifetime());

	showControls();
	updateControlsGeometry();
}

void Panel::createRemoteLowBattery() {
	_remoteLowBattery.create(
		widget(),
		object_ptr<Ui::FlatLabel>(
			widget(),
			tr::lng_call_battery_level_low(
				lt_user,
				_user->session().changes().peerFlagsValue(
					_user,
					Data::PeerUpdate::Flag::Name
				) | rpl::map([=] { return _user->shortName(); })),
			st::callRemoteAudioMute),
		st::callTooltipPadding);
	_remoteLowBattery->setAttribute(Qt::WA_TransparentForMouseEvents);

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_remoteLowBattery.destroy();
		createRemoteLowBattery();
	}, _remoteLowBattery->lifetime());

	constexpr auto kBatterySize = QSize(29, 13);

	const auto icon = [&] {
		auto svg = QSvgRenderer(
			BatterySvg(kBatterySize, st::videoPlayIconFg->c));
		auto image = QImage(
			kBatterySize * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(style::DevicePixelRatio());
		image.fill(Qt::transparent);
		{
			auto p = QPainter(&image);
			svg.render(&p, Rect(kBatterySize));
		}
		return image;
	}();

	_remoteLowBattery->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_remoteLowBattery);
		const auto r = _remoteLowBattery->rect();

		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(_controlsShownAnimation.value(
			_controlsShown ? 1. : 0.));
		p.setBrush(st::videoPlayIconBg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(r, r.height() / 2, r.height() / 2);

		p.drawImage(
			st::callTooltipMutedIconPosition.x(),
			(r.height() - kBatterySize.height()) / 2,
			icon);
	}, _remoteLowBattery->lifetime());

	showControls();
	updateControlsGeometry();
}

void Panel::showRemoteLowBattery() {
	if (_remoteLowBattery) {
		_remoteLowBattery->setVisible(!_remoteAudioMute
			|| _remoteAudioMute->isHidden());
	}
}

void Panel::initLayout() {
	initGeometry();

	_name->setAttribute(Qt::WA_TransparentForMouseEvents);
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);

	using UpdateFlag = Data::PeerUpdate::Flag;
	_user->session().changes().peerUpdates(
		UpdateFlag::Name
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		// _user may change for the same Panel.
		return (_call != nullptr) && (update.peer == _user);
	}) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		_name->setText(_call->user()->name());
		updateControlsGeometry();
	}, widget()->lifetime());

#ifndef Q_OS_MAC
	_controls->wrap.raise();
#endif // !Q_OS_MAC
}

void Panel::showControls() {
	Expects(_call != nullptr);

	widget()->showChildren();
	_decline->setVisible(_decline->toggled());
	_cancel->setVisible(_cancel->toggled());
	_screencast->setVisible(_screencast->toggled());

	const auto shown = !_incomingFrameSize.isEmpty();
	_incoming->widget()->setVisible(shown);
	_name->setVisible(!shown);
	_status->setVisible(!shown);
	_userpic->setVisible(!shown);
	if (_remoteAudioMute) {
		_remoteAudioMute->setVisible(shown);
	}
	showRemoteLowBattery();
}

void Panel::closeBeforeDestroy() {
	window()->close();
	reinitWithCall(nullptr);
}

rpl::lifetime &Panel::lifetime() {
	return window()->lifetime();
}

void Panel::initGeometry() {
	const auto center = Core::App().getPointForCallPanelCenter();
	const auto initRect = QRect(0, 0, st::callWidth, st::callHeight);
	window()->setGeometry(initRect.translated(center - initRect.center()));
	window()->setMinimumSize({ st::callWidthMin, st::callHeightMin });
	window()->show();
	updateControlsGeometry();
}

void Panel::initMediaDeviceToggles() {
	_cameraDeviceToggle = _camera->addCornerButton(
		st::callCornerButton,
		&st::callCornerButtonInactive);
	_audioDeviceToggle = _mute->entity()->addCornerButton(
		st::callCornerButton,
		&st::callCornerButtonInactive);

	_cameraDeviceToggle->setClickedCallback([=] {
		showDevicesMenu(_cameraDeviceToggle, {
			{ Webrtc::DeviceType::Camera, _call->cameraDeviceIdValue() },
		});
	});
	_audioDeviceToggle->setClickedCallback([=] {
		showDevicesMenu(_audioDeviceToggle, {
			{ Webrtc::DeviceType::Playback, _call->playbackDeviceIdValue() },
			{ Webrtc::DeviceType::Capture, _call->captureDeviceIdValue() },
		});
	});
}

void Panel::showDevicesMenu(
		not_null<QWidget*> button,
		std::vector<DeviceSelection> types) {
	if (!_call || _devicesMenu) {
		return;
	}
	const auto chosen = [=](Webrtc::DeviceType type, QString id) {
		switch (type) {
		case Webrtc::DeviceType::Playback:
			Core::App().settings().setCallPlaybackDeviceId(id);
			break;
		case Webrtc::DeviceType::Capture:
			Core::App().settings().setCallCaptureDeviceId(id);
			break;
		case Webrtc::DeviceType::Camera:
			Core::App().settings().setCameraDeviceId(id);
			break;
		}
		Core::App().saveSettingsDelayed();
	};
	controlsShownForce(true);
	updateControlsShown();

	_devicesMenu = MakeDeviceSelectionMenu(
		widget(),
		&Core::App().mediaDevices(),
		std::move(types),
		chosen);
	_devicesMenu->setForcedVerticalOrigin(
		Ui::PopupMenu::VerticalOrigin::Bottom);
	_devicesMenu->popup(button->mapToGlobal(QPoint())
		- QPoint(st::callDeviceSelectionMenu.menu.widthMin / 2, 0));
	QObject::connect(_devicesMenu.get(), &QObject::destroyed, window(), [=] {
		_controlsShownForceTimer.callOnce(kHideControlsQuickTimeout);
	});
}

void Panel::refreshOutgoingPreviewInBody(State state) {
	const auto inBody = (state != State::Established)
		&& (_call->videoOutgoing()->state() != Webrtc::VideoState::Inactive)
		&& !_call->videoOutgoing()->frameSize().isEmpty();
	if (_outgoingPreviewInBody == inBody) {
		return;
	}
	_outgoingPreviewInBody = inBody;
	_bodySt = inBody ? &st::callBodyWithPreview : &st::callBodyLayout;
	updateControlsGeometry();
}

void Panel::toggleFullScreen(bool fullscreen) {
	if (fullscreen) {
		window()->showFullScreen();
	} else {
		window()->showNormal();
	}
}

QRect Panel::incomingFrameGeometry() const {
	return (!_incoming || _incoming->widget()->isHidden())
		? QRect()
		: _incoming->widget()->geometry();
}

QRect Panel::outgoingFrameGeometry() const {
	return _outgoingVideoBubble->geometry();
}

void Panel::requestControlsHidden(bool hidden) {
	_hideControlsRequested = hidden;
	updateControlsShown();
}

void Panel::controlsShownForce(bool shown) {
	_controlsShownForce = shown;
	if (shown) {
		_controlsShownForceTimer.cancel();
	}
	updateControlsShown();
}

void Panel::updateControlsShown() {
	const auto shown = !_incoming
		|| _incoming->widget()->isHidden()
		|| _controlsShownForce
		|| !_hideControlsRequested;
	if (_controlsShown != shown) {
		_controlsShown = shown;
		_controlsShownAnimation.start([=] {
			updateControlsGeometry();
		}, shown ? 0. : 1., shown ? 1. : 0., st::slideDuration);
		updateControlsGeometry();
	}
}

void Panel::updateControlsGeometry() {
	if (widget()->size().isEmpty()) {
		return;
	}
	if (_incoming) {
		refreshIncomingGeometry();
	}
	const auto shown = _controlsShownAnimation.value(
		_controlsShown ? 1. : 0.);
	if (_fingerprint) {
#ifndef Q_OS_MAC
		const auto controlsGeometry = _controls->controls.geometry();
		const auto halfWidth = widget()->width() / 2;
		const auto minLeft = (controlsGeometry.center().x() < halfWidth)
			? (controlsGeometry.width() + st::callFingerprintTop)
			: 0;
		const auto minRight = (controlsGeometry.center().x() >= halfWidth)
			? (controlsGeometry.width() + st::callFingerprintTop)
			: 0;
		_incoming->setControlsAlignment(minLeft
			? style::al_left
			: style::al_right);
#else // !Q_OS_MAC
		const auto minLeft = 0;
		const auto minRight = 0;
#endif // _controls
		const auto desired = (widget()->width() - _fingerprint->width()) / 2;
		const auto top = anim::interpolate(
			-_fingerprint->height(),
			st::callFingerprintTop,
			shown);
		if (minLeft) {
			_fingerprint->moveToLeft(std::max(desired, minLeft), top);
		} else {
			_fingerprint->moveToRight(std::max(desired, minRight), top);
		}
	}
	const auto innerHeight = std::max(widget()->height(), st::callHeightMin);
	const auto innerWidth = widget()->width() - 2 * st::callInnerPadding;
	const auto availableTop = st::callFingerprintTop
		+ (_fingerprint ? _fingerprint->height() : 0)
		+ st::callFingerprintBottom;
	const auto available = widget()->height()
		- st::callBottomControlsHeight
		- availableTop;
	const auto bodyPreviewSizeMax = st::callOutgoingPreviewMin
		+ ((st::callOutgoingPreview
			- st::callOutgoingPreviewMin)
			* (innerHeight - st::callHeightMin)
			/ (st::callHeight - st::callHeightMin));
	const auto bodyPreviewSize = QSize(
		std::min(
			bodyPreviewSizeMax.width(),
			std::min(innerWidth, st::callOutgoingPreviewMax.width())),
		std::min(
			bodyPreviewSizeMax.height(),
			st::callOutgoingPreviewMax.height()));
	const auto contentHeight = _bodySt->height
		+ (_outgoingPreviewInBody ? bodyPreviewSize.height() : 0);
	const auto remainingHeight = available - contentHeight;
	const auto skipHeight = remainingHeight
		/ (_outgoingPreviewInBody ? 3 : 2);

	_bodyTop = availableTop + skipHeight;
	_buttonsTopShown = availableTop + available;
	_buttonsTop = anim::interpolate(
		widget()->height(),
		_buttonsTopShown,
		shown);
	const auto previewTop = _bodyTop + _bodySt->height + skipHeight;

	_userpic->setGeometry(
		(widget()->width() - _bodySt->photoSize) / 2,
		_bodyTop + _bodySt->photoTop,
		_bodySt->photoSize);
	_userpic->setMuteLayout(
		_bodySt->mutePosition,
		_bodySt->muteSize,
		_bodySt->muteStroke);

	_name->moveToLeft(
		(widget()->width() - _name->width()) / 2,
		_bodyTop + _bodySt->nameTop);
	updateStatusGeometry();

	if (_remoteAudioMute) {
		_remoteAudioMute->moveToLeft(
			(widget()->width() - _remoteAudioMute->width()) / 2,
			(_buttonsTop
				- st::callRemoteAudioMuteSkip
				- _remoteAudioMute->height()));
		_remoteAudioMute->update();
		_remoteAudioMute->entity()->setOpacity(shown);
	}
	if (_remoteLowBattery) {
		_remoteLowBattery->moveToLeft(
			(widget()->width() - _remoteLowBattery->width()) / 2,
			(_buttonsTop
				- st::callRemoteAudioMuteSkip
				- _remoteLowBattery->height()));
		_remoteLowBattery->update();
		_remoteLowBattery->entity()->setOpacity(shown);
	}

	if (_outgoingPreviewInBody) {
		_outgoingVideoBubble->updateGeometry(
			VideoBubble::DragMode::None,
			QRect(
				(widget()->width() - bodyPreviewSize.width()) / 2,
				previewTop,
				bodyPreviewSize.width(),
				bodyPreviewSize.height()));
	} else if (_outgoingVideoBubble) {
		updateOutgoingVideoBubbleGeometry();
	}

	updateHangupGeometry();
}

void Panel::updateOutgoingVideoBubbleGeometry() {
	Expects(!_outgoingPreviewInBody);

	const auto size = st::callOutgoingDefaultSize;
	_outgoingVideoBubble->updateGeometry(
		VideoBubble::DragMode::SnapToCorners,
		widget()->rect() - Margins(st::callInnerPadding),
		size);
}

void Panel::updateHangupGeometry() {
	const auto isWaitingUser = (_call
		&& _call->state() == State::WaitingUserConfirmation);
	const auto hangupProgress = isWaitingUser
		? 0.
		: _hangupShownProgress.value(_hangupShown ? 1. : 0.);
	_answerHangupRedial->setProgress(hangupProgress);

	// Screencast - Camera - Cancel/Decline - Answer/Hangup/Redial - Mute.
	const auto buttonWidth = st::callCancel.button.width;
	const auto cancelWidth = buttonWidth * (1. - hangupProgress);
	const auto cancelLeft = (isWaitingUser)
		? ((widget()->width() - buttonWidth) / 2)
		: (_mute->animating())
		? ((widget()->width() - cancelWidth) / 2)
		: ((widget()->width() / 2) - cancelWidth);

	_cancel->moveToLeft(cancelLeft, _buttonsTop);
	_decline->moveToLeft(cancelLeft, _buttonsTop);
	_camera->moveToLeft(cancelLeft - buttonWidth, _buttonsTop);
	_screencast->moveToLeft(_camera->x() - buttonWidth, _buttonsTop);
	_answerHangupRedial->moveToLeft(cancelLeft + cancelWidth, _buttonsTop);
	_mute->moveToLeft(_answerHangupRedial->x() + buttonWidth, _buttonsTop);
	if (_startVideo) {
		_startVideo->moveToLeft(_camera->x(), _camera->y());
	}
}

void Panel::updateStatusGeometry() {
	_status->moveToLeft(
		(widget()->width() - _status->width()) / 2,
		_bodyTop + _bodySt->statusTop);
}

void Panel::paint(QRect clip) {
	auto p = QPainter(widget());

	auto region = QRegion(clip);
	if (!_incoming->widget()->isHidden()) {
		region = region.subtracted(QRegion(_incoming->widget()->geometry()));
	}
	for (const auto &rect : region) {
		p.fillRect(rect, st::callBgOpaque);
	}
	if (_incoming && _incoming->widget()->isHidden()) {
		_call->videoIncoming()->markFrameShown();
	}
}

bool Panel::handleClose() const {
	if (_call) {
		if (_call->state() == Call::State::WaitingUserConfirmation
			|| _call->state() == Call::State::Busy) {
			_call->hangup();
		} else {
			window()->hide();
		}
		return true;
	}
	return false;
}

not_null<Ui::RpWindow*> Panel::window() const {
	return _window.window();
}

not_null<Ui::RpWidget*> Panel::widget() const {
	return _window.widget();
}

void Panel::stateChanged(State state) {
	Expects(_call != nullptr);

	updateStatusText(state);

	if ((state != State::HangingUp)
		&& (state != State::Ended)
		&& (state != State::EndedByOtherDevice)
		&& (state != State::FailedHangingUp)
		&& (state != State::Failed)) {
		const auto isBusy = (state == State::Busy);
		const auto isWaitingUser = (state == State::WaitingUserConfirmation);
		if (isBusy) {
			_powerSaveBlocker = nullptr;
		}
		if (_startVideo && !isWaitingUser) {
			_startVideo = nullptr;
		} else if (!_startVideo && isWaitingUser) {
			_startVideo = base::make_unique_q<Ui::CallButton>(
				widget(),
				st::callStartVideo);
			_startVideo->show();
			_startVideo->setText(tr::lng_call_start_video());
			_startVideo->clicks() | rpl::map_to(true) | rpl::start_to_stream(
				_startOutgoingRequests,
				_startVideo->lifetime());
		}
		_camera->setVisible(!_startVideo);

		const auto toggleButton = [&](auto &&button, bool visible) {
			button->toggle(
				visible,
				window()->isHidden()
				? anim::type::instant
				: anim::type::normal);
		};
		const auto incomingWaiting = _call->isIncomingWaiting();
		if (incomingWaiting) {
			_updateOuterRippleTimer.callEach(Call::kSoundSampleMs);
		}
		toggleButton(_decline, incomingWaiting);
		toggleButton(_cancel, (isBusy || isWaitingUser));
		toggleButton(_mute, !isWaitingUser);
		toggleButton(
			_screencast,
			!(isBusy || isWaitingUser || incomingWaiting));
		const auto hangupShown = !_decline->toggled()
			&& !_cancel->toggled();
		if (_hangupShown != hangupShown) {
			_hangupShown = hangupShown;
			_hangupShownProgress.start(
				[this] { updateHangupGeometry(); },
				_hangupShown ? 0. : 1.,
				_hangupShown ? 1. : 0.,
				st::callPanelDuration,
				anim::sineInOut);
		}
		const auto answerHangupRedialState = incomingWaiting
			? AnswerHangupRedialState::Answer
			: isBusy
			? AnswerHangupRedialState::Redial
			: isWaitingUser
			? AnswerHangupRedialState::StartCall
			: AnswerHangupRedialState::Hangup;
		if (_answerHangupRedialState != answerHangupRedialState) {
			_answerHangupRedialState = answerHangupRedialState;
			refreshAnswerHangupRedialLabel();
		}
		if (!_call->isKeyShaForFingerprintReady()) {
			_fingerprint.destroy();
		} else if (!_fingerprint) {
			_fingerprint = CreateFingerprintAndSignalBars(widget(), _call);
			updateControlsGeometry();
		}
	}
}

void Panel::refreshAnswerHangupRedialLabel() {
	Expects(_answerHangupRedialState.has_value());

	_answerHangupRedial->setText([&] {
		switch (*_answerHangupRedialState) {
		case AnswerHangupRedialState::Answer: return tr::lng_call_accept();
		case AnswerHangupRedialState::Hangup: return tr::lng_call_end_call();
		case AnswerHangupRedialState::Redial: return tr::lng_call_redial();
		case AnswerHangupRedialState::StartCall: return tr::lng_call_start();
		}
		Unexpected("AnswerHangupRedialState value.");
	}());
}

void Panel::updateStatusText(State state) {
	auto statusText = [this, state]() -> QString {
		switch (state) {
		case State::Starting:
		case State::WaitingInit:
		case State::WaitingInitAck: return tr::lng_call_status_connecting(tr::now);
		case State::Established: {
			if (_call) {
				auto durationMs = _call->getDurationMs();
				auto durationSeconds = durationMs / 1000;
				startDurationUpdateTimer(durationMs);
				return Ui::FormatDurationText(durationSeconds);
			}
			return tr::lng_call_status_ended(tr::now);
		} break;
		case State::FailedHangingUp:
		case State::Failed: return tr::lng_call_status_failed(tr::now);
		case State::HangingUp: return tr::lng_call_status_hanging(tr::now);
		case State::Ended:
		case State::EndedByOtherDevice: return tr::lng_call_status_ended(tr::now);
		case State::ExchangingKeys: return tr::lng_call_status_exchanging(tr::now);
		case State::Waiting: return tr::lng_call_status_waiting(tr::now);
		case State::Requesting: return tr::lng_call_status_requesting(tr::now);
		case State::WaitingIncoming: return tr::lng_call_status_incoming(tr::now);
		case State::Ringing: return tr::lng_call_status_ringing(tr::now);
		case State::Busy: return tr::lng_call_status_busy(tr::now);
		case State::WaitingUserConfirmation: return tr::lng_call_status_sure(tr::now);
		}
		Unexpected("State in stateChanged()");
	};
	_status->setText(statusText());
	updateStatusGeometry();
}

void Panel::startDurationUpdateTimer(crl::time currentDuration) {
	auto msTillNextSecond = 1000 - (currentDuration % 1000);
	_updateDurationTimer.callOnce(msTillNextSecond + 5);
}

} // namespace Calls
