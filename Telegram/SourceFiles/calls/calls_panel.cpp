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
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/platform/ui_platform_utility.h"
#include "ui/empty_userpic.h"
#include "ui/emoji_config.h"
#include "core/application.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "platform/platform_specific.h"
#include "window/main_window.h"
#include "layout.h"
#include "app.h"
#include "webrtc/webrtc_video_track.h"
#include "styles/style_calls.h"
#include "styles/style_history.h"

#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QApplication>

namespace Calls {
namespace {

constexpr auto kTooltipShowTimeoutMs = 1000;

} // namespace

class Panel::Button : public Ui::RippleButton {
public:
	Button(QWidget *parent, const style::CallButton &stFrom, const style::CallButton *stTo = nullptr);

	void setProgress(float64 progress);
	void setOuterValue(float64 value);

	void setIconOverride(const style::icon *iconOverride);

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

	const style::icon *_iconOverride = nullptr;

	QImage _bgMask, _bg;
	QPixmap _bgFrom, _bgTo;
	QImage _iconMixedMask, _iconFrom, _iconTo, _iconMixed;

	float64 _outerValue = 0.;
	Ui::Animations::Simple _outerAnimation;

};

Panel::Button::Button(QWidget *parent, const style::CallButton &stFrom, const style::CallButton *stTo) : Ui::RippleButton(parent, stFrom.button.ripple)
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

void Panel::Button::setIconOverride(const style::icon *iconOverride) {
	_iconOverride = iconOverride;
	update();
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
		const auto icon = _iconOverride ? _iconOverride : &_stFrom->button.icon;
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
: RpWidget(Core::App().getModalParent())
, _call(call)
, _user(call->user())
, _bodySt(&st::callBodyLayout)
, _answerHangupRedial(this, st::callAnswer, &st::callHangup)
, _decline(this, object_ptr<Button>(this, st::callHangup))
, _cancel(this, object_ptr<Button>(this, st::callCancel))
, _camera(this, st::callCameraToggle)
, _mute(this, st::callMuteToggle)
, _name(this, st::callName)
, _status(this, st::callStatus) {
	_decline->setDuration(st::callPanelDuration);
	_cancel->setDuration(st::callPanelDuration);

	setMouseTracking(true);
	setWindowIcon(Window::CreateIcon(&_user->session()));
	initControls();
	initLayout();
	showAndActivate();
}

Panel::~Panel() = default;

void Panel::showAndActivate() {
	toggleOpacityAnimation(true);
	raise();
	setWindowState(windowState() | Qt::WindowActive);
	activateWindow();
	setFocus();
}

void Panel::replaceCall(not_null<Call*> call) {
	reinitWithCall(call);
	updateControlsGeometry();
}

bool Panel::eventHook(QEvent *e) {
	if (e->type() == QEvent::WindowDeactivate) {
		checkForInactiveHide();
	}
	return RpWidget::eventHook(e);
}

void Panel::hideDeactivated() {
	toggleOpacityAnimation(false);
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

void Panel::setIncomingShown(bool shown) {
	if (_incomingShown == shown) {
		return;
	}
	_incomingShown = shown;
	if (_animationCache.isNull()) {
		showControls();
	}
}

void Panel::reinitWithCall(Call *call) {
	_callLifetime.destroy();
	_call = call;
	if (!_call) {
		_outgoingVideoBubble = nullptr;
		return;
	}

	_user = _call->user();

	_signalBars.create(
		this,
		_call,
		st::callPanelSignalBars,
		[=] { rtlupdate(signalBarsRect()); });

	auto remoteMuted = _call->remoteAudioStateValue(
	) | rpl::map([=](Call::RemoteAudioState state) {
		return (state == Call::RemoteAudioState::Muted);
	});
	_userpic = std::make_unique<Userpic>(this, _user, std::move(remoteMuted));
	_outgoingVideoBubble = std::make_unique<VideoBubble>(
		this,
		_call->videoOutgoing());

	_call->mutedValue(
	) | rpl::start_with_next([=](bool mute) {
		_mute->setIconOverride(mute ? &st::callUnmuteIcon : nullptr);
	}, _callLifetime);

	_call->videoOutgoing()->stateValue(
	) | rpl::start_with_next([=](Webrtc::VideoState state) {
		_camera->setIconOverride((state == Webrtc::VideoState::Active)
			? nullptr
			: &st::callNoCameraIcon);
	}, _callLifetime);

	_call->stateValue(
	) | rpl::start_with_next([=](State state) {
		stateChanged(state);
	}, _callLifetime);

	_call->videoIncoming()->renderNextFrame(
	) | rpl::start_with_next([=] {
		setIncomingShown(!_call->videoIncoming()->frame({}).isNull());
		update();
	}, _callLifetime);

	rpl::merge(
		_call->videoIncoming()->stateChanges(),
		_call->videoOutgoing()->stateChanges()
	) | rpl::start_with_next([=] {
		checkForInactiveShow();
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

	_name->setText(_user->name);
	updateStatusText(_call->state());
}

void Panel::initLayout() {
	setWindowFlags(Qt::WindowFlags(Qt::FramelessWindowHint) | Qt::NoDropShadowWindowHint | Qt::Dialog);
	setAttribute(Qt::WA_MacAlwaysShowToolWindow);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);

	initGeometry();

	using UpdateFlag = Data::PeerUpdate::Flag;
	_user->session().changes().peerUpdates(
		UpdateFlag::Name
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		// _user may change for the same Panel.
		return (_call != nullptr) && (update.peer == _user);
	}) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		_name->setText(_call->user()->name);
		updateControlsGeometry();
	}, lifetime());

	createDefaultCacheImage();

	Ui::Platform::InitOnTopPanel(this);
}

void Panel::toggleOpacityAnimation(bool visible) {
	if (!_call || _visible == visible) {
		return;
	}

	_visible = visible;
	if (_useTransparency) {
		if (_animationCache.isNull()) {
			showControls();
			_animationCache = Ui::GrabWidget(this);
			hideChildren();
		}
		_opacityAnimation.start(
			[this] { update(); },
			_visible ? 0. : 1.,
			_visible ? 1. : 0.,
			st::callPanelDuration,
			_visible ? anim::easeOutCirc : anim::easeInCirc);
	} else if (!isHidden() && !_visible) {
		hide();
	}
	if (isHidden() && _visible) {
		show();
	}
}

void Panel::finishAnimating() {
	_animationCache = QPixmap();
	if (_call) {
		if (!_visible) {
			hide();
		} else {
			showControls();
		}
	} else {
		destroyDelayed();
	}
}

void Panel::showControls() {
	Expects(_call != nullptr);

	showChildren();
	_decline->setVisible(_decline->toggled());
	_cancel->setVisible(_cancel->toggled());
	_name->setVisible(!_incomingShown);
	_status->setVisible(!_incomingShown);
	_userpic->setVisible(!_incomingShown);
}

void Panel::destroyDelayed() {
	hide();
	crl::on_main(this, [=] {
		delete this;
	});
}

void Panel::hideAndDestroy() {
	toggleOpacityAnimation(false);
	reinitWithCall(nullptr);
	if (_animationCache.isNull()) {
		destroyDelayed();
	}
}

void Panel::initGeometry() {
	const auto center = Core::App().getPointForCallPanelCenter();
	_useTransparency = Ui::Platform::TranslucentWindowsSupported(center);
	setAttribute(Qt::WA_OpaquePaintEvent, !_useTransparency);
	_padding = _useTransparency ? st::callShadow.extend : style::margins(st::lineWidth, st::lineWidth, st::lineWidth, st::lineWidth);
	const auto rect = [&] {
		const QRect initRect(0, 0, st::callWidth, st::callHeight);
		return initRect.translated(center - initRect.center()).marginsAdded(_padding);
	}();
	setGeometry(rect);
	setMinimumSize(rect.size());
	setMaximumSize(rect.size());
	createBottomImage();
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

void Panel::createBottomImage() {
	if (!_useTransparency) {
		return;
	}
	auto bottomWidth = width();
	auto bottomHeight = height();
	auto image = QImage(QSize(bottomWidth, bottomHeight) * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	const auto inner = rect().marginsRemoved(_padding);
	image.fill(Qt::transparent);
	{
		Painter p(&image);
		Ui::Shadow::paint(p, inner, width(), st::callShadow);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::callBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(inner, st::callRadius, st::callRadius);
	}
	_bottomCache = App::pixmapFromImageInPlace(std::move(image));
}

void Panel::createDefaultCacheImage() {
	if (!_useTransparency || !_cache.isNull()) {
		return;
	}
	auto cache = QImage(size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	cache.fill(Qt::transparent);
	{
		Painter p(&cache);
		auto inner = rect().marginsRemoved(_padding);
		Ui::Shadow::paint(p, inner, width(), st::callShadow);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(st::callBg);
		p.setPen(Qt::NoPen);
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(myrtlrect(inner), st::callRadius, st::callRadius);
	}
	_cache = App::pixmapFromImageInPlace(std::move(cache));
}

void Panel::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Panel::updateControlsGeometry() {
	const auto innerHeight = height() - _padding.top() - _padding.bottom();
	const auto availableTop = _padding.top() + _fingerprintHeight;
	const auto available = height()
		- (st::callBottomControlsHeight + _padding.bottom())
		- availableTop;
	const auto bodyPreviewSizeMax = st::callOutgoingPreviewMin
		+ ((st::callOutgoingPreview
			- st::callOutgoingPreviewMin)
			* (innerHeight - st::callHeightMin)
			/ (st::callHeight - st::callHeightMin));
	const auto bodyPreviewSize = QSize(
		std::min(bodyPreviewSizeMax.width(), st::callOutgoingPreviewMax.width()),
		std::min(bodyPreviewSizeMax.height(), st::callOutgoingPreviewMax.height()));
	const auto contentHeight = _bodySt->height
		+ (_outgoingPreviewInBody ? bodyPreviewSize.height() : 0);
	const auto remainingHeight = available - contentHeight;
	const auto skipHeight = remainingHeight
		/ (_outgoingPreviewInBody ? 3 : 2);

	_bodyTop = availableTop + skipHeight;
	_buttonsTop = availableTop + available;
	const auto previewTop = _bodyTop + _bodySt->height + skipHeight;

	_userpic->setGeometry(
		(width() - _bodySt->photoSize) / 2,
		_bodyTop + _bodySt->photoTop,
		_bodySt->photoSize);

	_name->moveToLeft(
		(width() - _name->width()) / 2,
		_bodyTop + _bodySt->nameTop);
	updateStatusGeometry();

	if (_outgoingPreviewInBody) {
		_outgoingVideoBubble->updateGeometry(
			VideoBubble::DragMode::None,
			QRect(
				(width() - bodyPreviewSize.width()) / 2,
				previewTop,
				bodyPreviewSize.width(),
				bodyPreviewSize.height()));
	} else {
		updateOutgoingVideoBubbleGeometry();
	}

	auto bothWidth = _answerHangupRedial->width() + st::callCancel.button.width;
	_decline->moveToLeft((width() - bothWidth) / 2, _buttonsTop);
	_cancel->moveToLeft((width() - bothWidth) / 2, _buttonsTop);

	updateHangupGeometry();

	const auto skip = st::callSignalMargin + st::callSignalPadding;
	const auto delta = (_signalBars->width() - _signalBars->height());
	_signalBars->moveToLeft(
		_padding.left() + skip,
		_padding.top() + skip + delta / 2);
}

void Panel::updateOutgoingVideoBubbleGeometry() {
	Expects(!_outgoingPreviewInBody);

	const auto size = st::callOutgoingDefaultSize;
	const auto availableHeight = height() - st::callBottomControlsHeight;
	const auto padding = 2 * _padding;
	_outgoingVideoBubble->updateGeometry(
		VideoBubble::DragMode::SnapToCorners,
		QRect(
			padding.left(),
			padding.top(),
			width() - padding.left() - padding.right(),
			height() - padding.left() - padding.bottom()),
		size);
}

void Panel::updateHangupGeometry() {
	auto singleWidth = _answerHangupRedial->width();
	auto bothWidth = singleWidth + st::callCancel.button.width;
	auto rightFrom = (width() - bothWidth) / 2;
	auto rightTo = (width() - singleWidth) / 2;
	auto hangupProgress = _hangupShownProgress.value(_hangupShown ? 1. : 0.);
	auto hangupRight = anim::interpolate(rightFrom, rightTo, hangupProgress);
	_answerHangupRedial->moveToRight(hangupRight, _buttonsTop);
	_answerHangupRedial->setProgress(hangupProgress);
	_mute->moveToRight(hangupRight - _mute->width(), _buttonsTop);
	_camera->moveToLeft(hangupRight - _mute->width(), _buttonsTop);
}

void Panel::updateStatusGeometry() {
	_status->moveToLeft((width() - _status->width()) / 2, _bodyTop + _bodySt->statusTop);
}

void Panel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (!_animationCache.isNull()) {
		auto opacity = _opacityAnimation.value(_call ? 1. : 0.);
		if (!_opacityAnimation.animating()) {
			finishAnimating();
			if (!_call || isHidden()) return;
		} else {
			p.setOpacity(opacity);

			PainterHighQualityEnabler hq(p);
			auto marginRatio = (1. - opacity) / 5;
			auto marginWidth = qRound(width() * marginRatio);
			auto marginHeight = qRound(height() * marginRatio);
			p.drawPixmap(rect().marginsRemoved(QMargins(marginWidth, marginHeight, marginWidth, marginHeight)), _animationCache, QRect(QPoint(0, 0), _animationCache.size()));
			return;
		}
	}

	if (_useTransparency) {
		p.drawPixmapLeft(0, 0, width(), _cache);
	} else {
		auto callBgOpaque = st::callBg->c;
		callBgOpaque.setAlpha(255);
		p.fillRect(rect(), QBrush(callBgOpaque));
	}

	const auto incomingFrame = _call
		? _call->videoIncoming()->frame(Webrtc::FrameRequest())
		: QImage();
	if (!incomingFrame.isNull()) {
		const auto to = rect().marginsRemoved(_padding);
		p.save();
		p.setClipRect(to);
		const auto big = incomingFrame.size().scaled(to.size(), Qt::KeepAspectRatio);
		const auto pos = QPoint(
			to.left() + (to.width() - big.width()) / 2,
			to.top() + (to.height() - big.height()) / 2);
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(QRect(pos, big), incomingFrame);
		p.restore();
	}
	_call->videoIncoming()->markFrameShown();

	if (_signalBars->isDisplayed()) {
		paintSignalBarsBg(p);
	}

	if (!_fingerprint.empty()) {
		App::roundRect(p, _fingerprintArea, st::callFingerprintBg, ImageRoundRadius::Large);

		const auto realSize = Ui::Emoji::GetSizeLarge();
		const auto size = realSize / cIntRetinaFactor();
		auto left = _fingerprintArea.left() + st::callFingerprintPadding.left();
		const auto top = _fingerprintArea.top() + st::callFingerprintPadding.top();
		for (const auto emoji : _fingerprint) {
			Ui::Emoji::Draw(p, emoji, realSize, left, top);
			left += st::callFingerprintSkip + size;
		}
	}
}

QRect Panel::signalBarsRect() const {
	const auto size = 2 * st::callSignalPadding + _signalBars->width();
	return QRect(
		_padding.left() + st::callSignalMargin,
		_padding.top() + st::callSignalMargin,
		size,
		size);
}

void Panel::paintSignalBarsBg(Painter &p) {
	App::roundRect(
		p,
		signalBarsRect(),
		st::callFingerprintBg,
		ImageRoundRadius::Small);
}

void Panel::closeEvent(QCloseEvent *e) {
	if (_call) {
		_call->hangup();
	}
}

void Panel::mousePressEvent(QMouseEvent *e) {
	auto dragArea = myrtlrect(_padding.left(), _padding.top(), st::callWidth, st::callWidth);
	if (e->button() == Qt::LeftButton) {
		if (dragArea.contains(e->pos())) {
			_dragging = true;
			_dragStartMousePosition = e->globalPos();
			_dragStartMyPosition = QPoint(x(), y());
		}
	}
}

void Panel::mouseMoveEvent(QMouseEvent *e) {
	if (_dragging) {
		Ui::Tooltip::Hide();
		if (!(e->buttons() & Qt::LeftButton)) {
			_dragging = false;
		} else {
			move(_dragStartMyPosition + (e->globalPos() - _dragStartMousePosition));
		}
	} else if (_fingerprintArea.contains(e->pos())) {
		Ui::Tooltip::Show(kTooltipShowTimeoutMs, this);
	} else {
		Ui::Tooltip::Hide();
	}
}

void Panel::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_dragging = false;
	}
}

void Panel::leaveEventHook(QEvent *e) {
	Ui::Tooltip::Hide();
}

void Panel::leaveToChildEvent(QEvent *e, QWidget *child) {
	Ui::Tooltip::Hide();
}

QString Panel::tooltipText() const {
	return tr::lng_call_fingerprint_tooltip(tr::now, lt_user, _user->name);
}

QPoint Panel::tooltipPos() const {
	return QCursor::pos();
}

bool Panel::tooltipWindowActive() const {
	return !isHidden();
}

void Panel::stateChanged(State state) {
	Expects(_call != nullptr);

	updateStatusText(state);

	if ((state != State::HangingUp)
		&& (state != State::Ended)
		&& (state != State::EndedByOtherDevice)
		&& (state != State::FailedHangingUp)
		&& (state != State::Failed)) {
		auto toggleButton = [this](auto &&button, bool visible) {
			button->toggle(
				visible,
				isHidden()
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
		if (_fingerprint.empty() && _call->isKeyShaForFingerprintReady()) {
			fillFingerprint();
		}
	}
	if (windowHandle()) {
		// First stateChanged() is called before
		// the first Platform::InitOnTopPanel(this).
		if ((state == State::Starting) || (state == State::WaitingIncoming)) {
			Ui::Platform::ReInitOnTopPanel(this);
		} else {
			Ui::Platform::DeInitOnTopPanel(this);
		}
		checkForInactiveHide();
	}
}

bool Panel::hasActiveVideo() const {
	const auto inactive = Webrtc::VideoState::Inactive;
	return (_call->videoIncoming()->state() != inactive)
		|| (_call->videoOutgoing()->state() != inactive);
}

void Panel::checkForInactiveHide() {
	if (!_call
		|| (_call->state() != State::Established)
		|| isActiveWindow()
		|| hasActiveVideo()) {
		return;
	}
	hideDeactivated();
}

void Panel::checkForInactiveShow() {
	if (!_visible && hasActiveVideo()) {
		toggleOpacityAnimation(true);
	}
}

void Panel::fillFingerprint() {
	Expects(_call != nullptr);
	_fingerprint = ComputeEmojiFingerprint(_call);

	auto realSize = Ui::Emoji::GetSizeLarge();
	auto size = realSize / cIntRetinaFactor();
	auto count = _fingerprint.size();
	auto rectWidth = count * size + (count - 1) * st::callFingerprintSkip;
	auto rectHeight = size;
	auto left = (width() - rectWidth) / 2;
	_fingerprintArea = QRect(
		left,
		_padding.top() + st::callFingerprintTop + st::callFingerprintPadding.top(),
		rectWidth,
		rectHeight
	).marginsAdded(st::callFingerprintPadding);
	_fingerprintHeight = st::callFingerprintTop + _fingerprintArea.height() + st::callFingerprintBottom;
	updateControlsGeometry();

	update();
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
				return formatDurationText(durationSeconds);
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
