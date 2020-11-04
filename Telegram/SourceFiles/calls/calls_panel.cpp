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
#include "calls/calls_emoji_fingerprint.h"
#include "calls/calls_signal_bars.h"
#include "calls/calls_userpic.h"
#include "calls/calls_video_bubble.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/window.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/text/format_values.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/toast/toast.h"
#include "ui/empty_userpic.h"
#include "ui/emoji_config.h"
#include "core/application.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "platform/platform_specific.h"
#include "base/platform/base_platform_info.h"
#include "window/main_window.h"
#include "app.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h"

#ifdef Q_OS_WIN
#include "ui/platform/win/ui_window_title_win.h"
#endif // Q_OS_WIN

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QApplication>
#include <QtGui/QWindow>

namespace Calls {
namespace {

#if defined Q_OS_MAC && !defined OS_MAC_OLD
#define USE_OPENGL_OVERLAY_WIDGET
#endif // Q_OS_MAC && !OS_MAC_OLD

#ifdef USE_OPENGL_OVERLAY_WIDGET
using IncomingParent = Ui::RpWidgetWrap<QOpenGLWidget>;
#else // USE_OPENGL_OVERLAY_WIDGET
using IncomingParent = Ui::RpWidget;
#endif // USE_OPENGL_OVERLAY_WIDGET

} // namespace

class Panel::Incoming final : public IncomingParent {
public:
	Incoming(
		not_null<QWidget*> parent,
		not_null<Webrtc::VideoTrack*> track);

private:
	void paintEvent(QPaintEvent *e) override;

	void initBottomShadow();
	void fillTopShadow(QPainter &p);
	void fillBottomShadow(QPainter &p);

	const not_null<Webrtc::VideoTrack*> _track;
	QPixmap _bottomShadow;

};

Panel::Incoming::Incoming(
	not_null<QWidget*> parent,
	not_null<Webrtc::VideoTrack*> track)
: IncomingParent(parent)
, _track(track) {
	initBottomShadow();
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_TransparentForMouseEvents);
}

void Panel::Incoming::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	const auto frame = _track->frame(Webrtc::FrameRequest());
	if (frame.isNull()) {
		p.fillRect(e->rect(), Qt::black);
	} else {
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(rect(), frame);
		fillBottomShadow(p);
		fillTopShadow(p);
	}
	_track->markFrameShown();
}

void Panel::Incoming::initBottomShadow() {
	auto image = QImage(
		QSize(1, st::callBottomShadowSize) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	const auto colorFrom = uint32(0);
	const auto colorTill = uint32(74);
	const auto rows = image.height();
	const auto step = (uint64(colorTill - colorFrom) << 32) / rows;
	auto accumulated = uint64();
	auto bytes = image.bits();
	for (auto y = 0; y != rows; ++y) {
		accumulated += step;
		const auto color = (colorFrom + uint32(accumulated >> 32)) << 24;
		for (auto x = 0; x != image.width(); ++x) {
			*(reinterpret_cast<uint32*>(bytes) + x) = color;
		}
		bytes += image.bytesPerLine();
	}
	_bottomShadow = Images::PixmapFast(std::move(image));
}

void Panel::Incoming::fillTopShadow(QPainter &p) {
#ifdef Q_OS_WIN
	const auto width = parentWidget()->width();
	const auto position = QPoint(width - st::callTitleShadow.width(), 0);
	const auto shadowArea = QRect(
		position,
		st::callTitleShadow.size());
	const auto fill = shadowArea.intersected(geometry()).translated(-pos());
	if (fill.isEmpty()) {
		return;
	}
	p.save();
	p.setClipRect(fill);
	st::callTitleShadow.paint(p, position - pos(), width);
	p.restore();
#endif // Q_OS_WIN
}

void Panel::Incoming::fillBottomShadow(QPainter &p) {
	const auto shadowArea = QRect(
		0,
		parentWidget()->height() - st::callBottomShadowSize,
		parentWidget()->width(),
		st::callBottomShadowSize);
	const auto fill = shadowArea.intersected(geometry()).translated(-pos());
	if (fill.isEmpty()) {
		return;
	}
	const auto factor = cIntRetinaFactor();
	p.drawPixmap(
		fill,
		_bottomShadow,
		QRect(
			0,
			factor * (fill.y() - shadowArea.translated(-pos()).y()),
			factor,
			factor * fill.height()));
}


class Panel::Button final : public Ui::RippleButton {
public:
	Button(
		QWidget *parent,
		const style::CallButton &stFrom,
		const style::CallButton *stTo = nullptr);

	void setProgress(float64 progress);
	void setOuterValue(float64 value);
	void setText(rpl::producer<QString> text);

protected:
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	QPoint iconPosition(not_null<const style::CallButton*> st) const;
	void mixIconMasks();

	not_null<const style::CallButton*> _stFrom;
	const style::CallButton *_stTo = nullptr;
	float64 _progress = 0.;

	object_ptr<Ui::FlatLabel> _label = { nullptr };

	QImage _bgMask, _bg;
	QPixmap _bgFrom, _bgTo;
	QImage _iconMixedMask, _iconFrom, _iconTo, _iconMixed;

	float64 _outerValue = 0.;
	Ui::Animations::Simple _outerAnimation;

};

Panel::Button::Button(
	QWidget *parent,
	const style::CallButton &stFrom,
	const style::CallButton *stTo)
: Ui::RippleButton(parent, stFrom.button.ripple)
, _stFrom(&stFrom)
, _stTo(stTo) {
	resize(_stFrom->button.width, _stFrom->button.height);

	_bgMask = prepareRippleMask();
	_bgFrom = App::pixmapFromImageInPlace(style::colorizeImage(_bgMask, _stFrom->bg));
	if (_stTo) {
		Assert(_stFrom->button.width == _stTo->button.width);
		Assert(_stFrom->button.height == _stTo->button.height);
		Assert(_stFrom->button.rippleAreaPosition == _stTo->button.rippleAreaPosition);
		Assert(_stFrom->button.rippleAreaSize == _stTo->button.rippleAreaSize);

		_bg = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_bg.setDevicePixelRatio(cRetinaFactor());
		_bgTo = App::pixmapFromImageInPlace(style::colorizeImage(_bgMask, _stTo->bg));
		_iconMixedMask = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixedMask.setDevicePixelRatio(cRetinaFactor());
		_iconFrom = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconFrom.setDevicePixelRatio(cRetinaFactor());
		_iconFrom.fill(Qt::black);
		{
			Painter p(&_iconFrom);
			p.drawImage((_stFrom->button.rippleAreaSize - _stFrom->button.icon.width()) / 2, (_stFrom->button.rippleAreaSize - _stFrom->button.icon.height()) / 2, _stFrom->button.icon.instance(Qt::white));
		}
		_iconTo = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconTo.setDevicePixelRatio(cRetinaFactor());
		_iconTo.fill(Qt::black);
		{
			Painter p(&_iconTo);
			p.drawImage((_stTo->button.rippleAreaSize - _stTo->button.icon.width()) / 2, (_stTo->button.rippleAreaSize - _stTo->button.icon.height()) / 2, _stTo->button.icon.instance(Qt::white));
		}
		_iconMixed = QImage(_bgMask.size(), QImage::Format_ARGB32_Premultiplied);
		_iconMixed.setDevicePixelRatio(cRetinaFactor());
	}
}

void Panel::Button::setOuterValue(float64 value) {
	if (_outerValue != value) {
		_outerAnimation.start([this] {
			if (_progress == 0. || _progress == 1.) {
				update();
			}
		}, _outerValue, value, Call::kSoundSampleMs);
		_outerValue = value;
	}
}

void Panel::Button::setText(rpl::producer<QString> text) {
	_label.create(this, std::move(text), _stFrom->label);
	_label->show();
	rpl::combine(
		sizeValue(),
		_label->sizeValue()
	) | rpl::start_with_next([=](QSize my, QSize label) {
		_label->moveToLeft(
			(my.width() - label.width()) / 2,
			my.height() - label.height(),
			my.width());
	}, _label->lifetime());
}

void Panel::Button::setProgress(float64 progress) {
	_progress = progress;
	update();
}

void Panel::Button::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto bgPosition = myrtlpoint(_stFrom->button.rippleAreaPosition);
	auto paintFrom = (_progress == 0.) || !_stTo;
	auto paintTo = !paintFrom && (_progress == 1.);

	auto outerValue = _outerAnimation.value(_outerValue);
	if (outerValue > 0.) {
		auto outerRadius = paintFrom ? _stFrom->outerRadius : paintTo ? _stTo->outerRadius : (_stFrom->outerRadius * (1. - _progress) + _stTo->outerRadius * _progress);
		auto outerPixels = outerValue * outerRadius;
		auto outerRect = QRectF(myrtlrect(bgPosition.x(), bgPosition.y(), _stFrom->button.rippleAreaSize, _stFrom->button.rippleAreaSize));
		outerRect = outerRect.marginsAdded(QMarginsF(outerPixels, outerPixels, outerPixels, outerPixels));

		PainterHighQualityEnabler hq(p);
		if (paintFrom) {
			p.setBrush(_stFrom->outerBg);
		} else if (paintTo) {
			p.setBrush(_stTo->outerBg);
		} else {
			p.setBrush(anim::brush(_stFrom->outerBg, _stTo->outerBg, _progress));
		}
		p.setPen(Qt::NoPen);
		p.drawEllipse(outerRect);
	}

	if (paintFrom) {
		p.drawPixmap(bgPosition, _bgFrom);
	} else if (paintTo) {
		p.drawPixmap(bgPosition, _bgTo);
	} else {
		style::colorizeImage(_bgMask, anim::color(_stFrom->bg, _stTo->bg, _progress), &_bg);
		p.drawImage(bgPosition, _bg);
	}

	auto rippleColorInterpolated = QColor();
	auto rippleColorOverride = &rippleColorInterpolated;
	if (paintFrom) {
		rippleColorOverride = nullptr;
	} else if (paintTo) {
		rippleColorOverride = &_stTo->button.ripple.color->c;
	} else {
		rippleColorInterpolated = anim::color(_stFrom->button.ripple.color, _stTo->button.ripple.color, _progress);
	}
	paintRipple(p, _stFrom->button.rippleAreaPosition.x(), _stFrom->button.rippleAreaPosition.y(), rippleColorOverride);

	auto positionFrom = iconPosition(_stFrom);
	if (paintFrom) {
		const auto icon = &_stFrom->button.icon;
		icon->paint(p, positionFrom, width());
	} else {
		auto positionTo = iconPosition(_stTo);
		if (paintTo) {
			_stTo->button.icon.paint(p, positionTo, width());
		} else {
			mixIconMasks();
			style::colorizeImage(_iconMixedMask, st::callIconFg->c, &_iconMixed);
			p.drawImage(myrtlpoint(_stFrom->button.rippleAreaPosition), _iconMixed);
		}
	}
}

QPoint Panel::Button::iconPosition(not_null<const style::CallButton*> st) const {
	auto result = st->button.iconPosition;
	if (result.x() < 0) {
		result.setX((width() - st->button.icon.width()) / 2);
	}
	if (result.y() < 0) {
		result.setY((height() - st->button.icon.height()) / 2);
	}
	return result;
}

void Panel::Button::mixIconMasks() {
	_iconMixedMask.fill(Qt::black);

	Painter p(&_iconMixedMask);
	PainterHighQualityEnabler hq(p);
	auto paintIconMask = [this, &p](const QImage &mask, float64 angle) {
		auto skipFrom = _stFrom->button.rippleAreaSize / 2;
		p.translate(skipFrom, skipFrom);
		p.rotate(angle);
		p.translate(-skipFrom, -skipFrom);
		p.drawImage(0, 0, mask);
	};
	p.save();
	paintIconMask(_iconFrom, (_stFrom->angle - _stTo->angle) * _progress);
	p.restore();
	p.setOpacity(_progress);
	paintIconMask(_iconTo, (_stTo->angle - _stFrom->angle) * (1. - _progress));
}

void Panel::Button::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	auto over = isOver();
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (over != wasOver) {
		update();
	}
}

QPoint Panel::Button::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _stFrom->button.rippleAreaPosition;
}

QImage Panel::Button::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(_stFrom->button.rippleAreaSize, _stFrom->button.rippleAreaSize));
}

Panel::Panel(not_null<Call*> call)
: _call(call)
, _user(call->user())
, _window(std::make_unique<Ui::Window>(Core::App().getModalParent()))
#ifdef Q_OS_WIN
, _controls(std::make_unique<Ui::Platform::TitleControls>(
	_window.get(),
	st::callTitle,
	[=](bool maximized) { toggleFullScreen(maximized); }))
#endif // Q_OS_WIN
, _bodySt(&st::callBodyLayout)
, _answerHangupRedial(widget(), st::callAnswer, &st::callHangup)
, _decline(widget(), object_ptr<Button>(widget(), st::callHangup))
, _cancel(widget(), object_ptr<Button>(widget(), st::callCancel))
, _camera(widget(), st::callCameraMute, &st::callCameraUnmute)
, _mute(widget(), st::callMicrophoneMute, &st::callMicrophoneUnmute)
, _name(widget(), st::callName)
, _status(widget(), st::callStatus) {
	_decline->setDuration(st::callPanelDuration);
	_decline->entity()->setText(tr::lng_call_decline());
	_cancel->setDuration(st::callPanelDuration);
	_cancel->entity()->setText(tr::lng_call_cancel());

	initWindow();
	initWidget();
	initControls();
	initLayout();
	showAndActivate();
}

Panel::~Panel() = default;

void Panel::showAndActivate() {
	_window->raise();
	_window->setWindowState(_window->windowState() | Qt::WindowActive);
	_window->activateWindow();
	_window->setFocus();
}

void Panel::replaceCall(not_null<Call*> call) {
	reinitWithCall(call);
	updateControlsGeometry();
}

void Panel::initWindow() {
	_window->setAttribute(Qt::WA_OpaquePaintEvent);
	_window->setAttribute(Qt::WA_NoSystemBackground);
	_window->setWindowIcon(
		QIcon(QPixmap::fromImage(Image::Empty()->original(), Qt::ColorOnly)));
	_window->setTitle(u" "_q);
	_window->setTitleStyle(st::callTitle);

	_window->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			handleClose();
		} else if (e->type() == QEvent::KeyPress) {
			if ((static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Escape)
				&& _window->isFullScreen()) {
				_window->showNormal();
			}
		}
	}, _window->lifetime());

	_window->setBodyTitleArea([=](QPoint widgetPoint) {
		using Flag = Ui::WindowTitleHitTestFlag;
		if (!widget()->rect().contains(widgetPoint)) {
			return Flag::None | Flag(0);
		}
#ifdef Q_OS_WIN
		if (_controls->geometry().contains(widgetPoint)) {
			return Flag::None | Flag(0);
		}
#endif // Q_OS_WIN
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
		return inControls
			? Flag::None
			: (Flag::Move | Flag::FullScreen);
	});

#ifdef Q_OS_WIN
	// On Windows we replace snap-to-top maximizing with fullscreen.
	//
	// We have to switch first to showNormal, so that showFullScreen
	// will remember correct normal window geometry and next showNormal
	// will show it instead of a moving maximized window.
	//
	// We have to do it in InvokeQueued, otherwise it still captures
	// the maximized window geometry and saves it.
	//
	// I couldn't find a less glitchy way to do that *sigh*.
	const auto object = _window->windowHandle();
	const auto signal = &QWindow::windowStateChanged;
	QObject::connect(object, signal, [=](Qt::WindowState state) {
		if (state == Qt::WindowMaximized) {
			InvokeQueued(object, [=] {
				_window->showNormal();
				_window->showFullScreen();
			});
		}
	});
#endif // Q_OS_WIN
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
	_mute->setClickedCallback([=] {
		if (_call) {
			_call->setMuted(!_call->muted());
		}
	});
	_camera->setClickedCallback([=] {
		if (_call) {
			_call->videoOutgoing()->setState(
				(_call->videoOutgoing()->state() == Webrtc::VideoState::Active)
				? Webrtc::VideoState::Inactive
				: Webrtc::VideoState::Active);
		}
	});

	_updateDurationTimer.setCallback([this] {
		if (_call) {
			updateStatusText(_call->state());
		}
	});
	_updateOuterRippleTimer.setCallback([this] {
		if (_call) {
			_answerHangupRedial->setOuterValue(_call->getWaitingSoundPeakValue());
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
}

void Panel::setIncomingSize(QSize size) {
	if (_incomingFrameSize == size) {
		return;
	}
	_incomingFrameSize = size;
	refreshIncomingGeometry();
	showControls();
}

void Panel::refreshIncomingGeometry() {
	Expects(_call != nullptr);
	Expects(_incoming != nullptr);

	if (_incomingFrameSize.isEmpty()) {
		_incoming->hide();
		return;
	}
	const auto to = widget()->size();
	const auto small = _incomingFrameSize.scaled(to, Qt::KeepAspectRatio);
	const auto big = _incomingFrameSize.scaled(
		to,
		Qt::KeepAspectRatioByExpanding);

	// If we cut out no more than 0.33 of the original, let's use expanding.
	const auto use = ((big.width() * 3 <= to.width() * 4)
		&& (big.height() * 3 <= to.height() * 4))
		? big
		: small;
	const auto pos = QPoint(
		(to.width() - use.width()) / 2,
		(to.height() - use.height()) / 2);
	_incoming->setGeometry(QRect(pos, use));
	_incoming->show();
}

void Panel::reinitWithCall(Call *call) {
	_callLifetime.destroy();
	_call = call;
	if (!_call) {
		_incoming = nullptr;
		_outgoingVideoBubble = nullptr;
		return;
	}

	_user = _call->user();

	auto remoteMuted = _call->remoteAudioStateValue(
	) | rpl::map([=](Call::RemoteAudioState state) {
		return (state == Call::RemoteAudioState::Muted);
	});
	rpl::duplicate(
		remoteMuted
	) | rpl::start_with_next([=](bool muted) {
		if (muted) {
			createRemoteAudioMute();
		} else {
			_remoteAudioMute.destroy();
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
		_call->videoIncoming());
	_incoming->hide();

	_call->mutedValue(
	) | rpl::start_with_next([=](bool mute) {
		_mute->setProgress(mute ? 1. : 0.);
		_mute->setText(mute
			? tr::lng_call_unmute_audio()
			: tr::lng_call_mute_audio());
	}, _callLifetime);

	_call->videoOutgoing()->stateValue(
	) | rpl::start_with_next([=](Webrtc::VideoState state) {
		const auto active = (state == Webrtc::VideoState::Active);
		_camera->setProgress(active ? 0. : 1.);
		_camera->setText(active
			? tr::lng_call_stop_video()
			: tr::lng_call_start_video());
	}, _callLifetime);

	_call->stateValue(
	) | rpl::start_with_next([=](State state) {
		stateChanged(state);
	}, _callLifetime);

	_call->videoIncoming()->renderNextFrame(
	) | rpl::start_with_next([=] {
		setIncomingSize(_call->videoIncoming()->frame({}).size());
		if (_incoming->isHidden()) {
			return;
		}
		const auto incoming = incomingFrameGeometry();
		const auto outgoing = outgoingFrameGeometry();
		_incoming->update();
		if (incoming.intersects(outgoing)) {
			widget()->update(outgoing);
		}
	}, _callLifetime);

	_call->videoOutgoing()->renderNextFrame(
	) | rpl::start_with_next([=] {
		const auto incoming = incomingFrameGeometry();
		const auto outgoing = outgoingFrameGeometry();
		widget()->update(outgoing);
		if (incoming.intersects(outgoing)) {
			_incoming->update();
		}
	}, _callLifetime);

	rpl::combine(
		_call->stateValue(),
		_call->videoOutgoing()->renderNextFrame()
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
				return tr::lng_call_error_camera_outdated(tr::now, lt_user, _user->name);
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
			.multiline = true,
		});
	}, _callLifetime);

	_name->setText(_user->name);
	updateStatusText(_call->state());

	_incoming->lower();
}

void Panel::createRemoteAudioMute() {
	_remoteAudioMute.create(
		widget(),
		object_ptr<Ui::FlatLabel>(
			widget(),
			tr::lng_call_microphone_off(
				lt_user,
				rpl::single(_user->shortName())),
			st::callRemoteAudioMute),
		st::callTooltipPadding);
	_remoteAudioMute->setAttribute(Qt::WA_TransparentForMouseEvents);

	_remoteAudioMute->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_remoteAudioMute);
		const auto height = _remoteAudioMute->height();

		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::videoPlayIconBg);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(_remoteAudioMute->rect(), height / 2, height / 2);

		st::callTooltipMutedIcon.paint(
			p,
			st::callTooltipMutedIconPosition,
			_remoteAudioMute->width());
	}, _remoteAudioMute->lifetime());

	showControls();
	updateControlsGeometry();
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
		_name->setText(_call->user()->name);
		updateControlsGeometry();
	}, widget()->lifetime());

#ifdef Q_OS_WIN
	_controls->raise();
#endif // Q_OS_WIN
}

void Panel::showControls() {
	Expects(_call != nullptr);

	widget()->showChildren();
	_decline->setVisible(_decline->toggled());
	_cancel->setVisible(_cancel->toggled());

	const auto shown = !_incomingFrameSize.isEmpty();
	_incoming->setVisible(shown);
	_name->setVisible(!shown);
	_status->setVisible(!shown);
	_userpic->setVisible(!shown);
	if (_remoteAudioMute) {
		_remoteAudioMute->setVisible(shown);
	}
}

void Panel::closeBeforeDestroy() {
	_window->close();
	reinitWithCall(nullptr);
}

void Panel::initGeometry() {
	const auto center = Core::App().getPointForCallPanelCenter();
	const auto initRect = QRect(0, 0, st::callWidth, st::callHeight);
	_window->setGeometry(initRect.translated(center - initRect.center()));
	_window->setMinimumSize({ st::callWidthMin, st::callHeightMin });
	_window->show();
	updateControlsGeometry();
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
		_window->showFullScreen();
	} else {
		_window->showNormal();
	}
}

QRect Panel::incomingFrameGeometry() const {
	return (!_incoming || _incoming->isHidden())
		? QRect()
		: _incoming->geometry();
}

QRect Panel::outgoingFrameGeometry() const {
	return _outgoingVideoBubble->geometry();
}

void Panel::updateControlsGeometry() {
	if (widget()->size().isEmpty()) {
		return;
	}
	if (_incoming) {
		refreshIncomingGeometry();
	}
	if (_fingerprint) {
#ifdef Q_OS_WIN
		const auto minRight = _controls->geometry().width()
			+ st::callFingerprintTop;
#else // Q_OS_WIN
		const auto minRight = 0;
#endif // _controls
		const auto desired = (widget()->width() - _fingerprint->width()) / 2;
		_fingerprint->moveToRight(
			std::max(desired, minRight),
			st::callFingerprintTop);
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
	_buttonsTop = availableTop + available;
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
	}

	if (_outgoingPreviewInBody) {
		_outgoingVideoBubble->updateGeometry(
			VideoBubble::DragMode::None,
			QRect(
				(widget()->width() - bodyPreviewSize.width()) / 2,
				previewTop,
				bodyPreviewSize.width(),
				bodyPreviewSize.height()));
	} else {
		updateOutgoingVideoBubbleGeometry();
	}

	auto bothWidth = _answerHangupRedial->width() + st::callCancel.button.width;
	_decline->moveToLeft((widget()->width() - bothWidth) / 2, _buttonsTop);
	_cancel->moveToLeft((widget()->width() - bothWidth) / 2, _buttonsTop);

	updateHangupGeometry();
}

void Panel::updateOutgoingVideoBubbleGeometry() {
	Expects(!_outgoingPreviewInBody);

	const auto margins = QMargins{
		st::callInnerPadding,
		st::callInnerPadding,
		st::callInnerPadding,
		st::callInnerPadding,
	};
	const auto size = st::callOutgoingDefaultSize;
	_outgoingVideoBubble->updateGeometry(
		VideoBubble::DragMode::SnapToCorners,
		widget()->rect().marginsRemoved(margins),
		size);
}

void Panel::updateHangupGeometry() {
	auto singleWidth = _answerHangupRedial->width();
	auto bothWidth = singleWidth + st::callCancel.button.width;
	auto rightFrom = (widget()->width() - bothWidth) / 2;
	auto rightTo = (widget()->width() - singleWidth) / 2;
	auto hangupProgress = _hangupShownProgress.value(_hangupShown ? 1. : 0.);
	auto hangupRight = anim::interpolate(rightFrom, rightTo, hangupProgress);
	_answerHangupRedial->moveToRight(hangupRight, _buttonsTop);
	_answerHangupRedial->setProgress(hangupProgress);
	_mute->moveToRight(hangupRight - _mute->width(), _buttonsTop);
	_camera->moveToLeft(hangupRight - _mute->width(), _buttonsTop);
}

void Panel::updateStatusGeometry() {
	_status->moveToLeft(
		(widget()->width() - _status->width()) / 2,
		_bodyTop + _bodySt->statusTop);
}

void Panel::paint(QRect clip) {
	Painter p(widget());

	auto region = QRegion(clip);
	if (!_incoming->isHidden()) {
		region = region.subtracted(QRegion(_incoming->geometry()));
	}
	for (const auto rect : region) {
		p.fillRect(rect, st::callBgOpaque);
	}
	if (_incoming && _incoming->isHidden()) {
		_call->videoIncoming()->markFrameShown();
	}
}

void Panel::handleClose() {
	if (_call) {
		_call->hangup();
	}
}

not_null<Ui::RpWidget*> Panel::widget() const {
	return _window->body();
}

void Panel::stateChanged(State state) {
	Expects(_call != nullptr);

	updateStatusText(state);

	if ((state != State::HangingUp)
		&& (state != State::Ended)
		&& (state != State::EndedByOtherDevice)
		&& (state != State::FailedHangingUp)
		&& (state != State::Failed)) {
		auto toggleButton = [&](auto &&button, bool visible) {
			button->toggle(
				visible,
				_window->isHidden()
				? anim::type::instant
				: anim::type::normal);
		};
		auto incomingWaiting = _call->isIncomingWaiting();
		if (incomingWaiting) {
			_updateOuterRippleTimer.callEach(Call::kSoundSampleMs);
		}
		toggleButton(_decline, incomingWaiting);
		toggleButton(_cancel, (state == State::Busy));
		auto hangupShown = !_decline->toggled()
			&& !_cancel->toggled();
		if (_hangupShown != hangupShown) {
			_hangupShown = hangupShown;
			_hangupShownProgress.start([this] { updateHangupGeometry(); }, _hangupShown ? 0. : 1., _hangupShown ? 1. : 0., st::callPanelDuration, anim::sineInOut);
		}
		const auto answerHangupRedialState = incomingWaiting
			? AnswerHangupRedialState::Answer
			: (state == State::Busy)
			? AnswerHangupRedialState::Redial
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
