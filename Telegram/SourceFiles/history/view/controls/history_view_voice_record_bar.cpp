/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_voice_record_bar.h"

#include "api/api_send_progress.h"
#include "base/event_filter.h"
#include "boxes/confirm_box.h"
#include "core/application.h"
#include "history/view/controls/history_view_voice_record_button.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_capture.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "ui/controls/send_button.h"
#include "ui/text/format_values.h"
#include "window/window_session_controller.h"

namespace HistoryView::Controls {

namespace {

using SendActionUpdate = VoiceRecordBar::SendActionUpdate;
using VoiceToSend = VoiceRecordBar::VoiceToSend;

constexpr auto kLockDelay = crl::time(100);
constexpr auto kRecordingUpdateDelta = crl::time(100);
constexpr auto kAudioVoiceMaxLength = 100 * 60; // 100 minutes
constexpr auto kMaxSamples =
	::Media::Player::kDefaultFrequency * kAudioVoiceMaxLength;

constexpr auto kPrecision = 10;

enum class FilterType {
	Continue,
	ShowBox,
	Cancel,
};

[[nodiscard]] auto Duration(int samples) {
	return samples / ::Media::Player::kDefaultFrequency;
}

[[nodiscard]] auto FormatVoiceDuration(int samples) {
	const int duration = kPrecision
		* (float64(samples) / ::Media::Player::kDefaultFrequency);
	const auto durationString = Ui::FormatDurationText(duration / kPrecision);
	const auto decimalPart = duration % kPrecision;
	return QString("%1%2%3")
		.arg(durationString)
		.arg(QLocale::system().decimalPoint())
		.arg(decimalPart);
}

} // namespace

class RecordLock final : public Ui::RpWidget {
public:
	RecordLock(not_null<Ui::RpWidget*> parent);

	void requestPaintProgress(float64 progress);

	[[nodiscard]] rpl::producer<> locks() const;
	[[nodiscard]] bool isLocked() const;

private:
	void init();

	void drawProgress(Painter &p);
	void setProgress(float64 progress);
	void startLockingAnimation(float64 to);

	Ui::Animations::Simple _lockAnimation;
	Ui::Animations::Simple _lockEnderAnimation;

	rpl::variable<float64> _progress = 0.;
};

RecordLock::RecordLock(not_null<Ui::RpWidget*> parent) : RpWidget(parent) {
	resize(
		st::historyRecordLockTopShadow.width(),
		st::historyRecordLockSize.height());
	// resize(st::historyRecordLockSize);
	init();
}

void RecordLock::init() {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	shownValue(
	) | rpl::start_with_next([=](bool shown) {
		if (!shown) {
			_lockAnimation.stop();
			_lockEnderAnimation.stop();
			_progress = 0.;
		}
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);
		if (isLocked()) {
			const auto top = anim::interpolate(
				0,
				height() - st::historyRecordLockTopShadow.height() * 2,
				_lockAnimation.value(1.));
			p.translate(0, top);
			drawProgress(p);
			return;
		}
		drawProgress(p);
	}, lifetime());

	locks(
	) | rpl::start_with_next([=] {
		const auto duration = st::historyRecordVoiceShowDuration;
		_lockAnimation.start([=] { update(); }, 0., 1., duration);
	}, lifetime());
}

void RecordLock::drawProgress(Painter &p) {
	const auto progress = _progress.current();

	const auto &originTop = st::historyRecordLockTop;
	const auto &originBottom = st::historyRecordLockBottom;
	const auto &originBody = st::historyRecordLockBody;
	const auto &shadowTop = st::historyRecordLockTopShadow;
	const auto &shadowBottom = st::historyRecordLockBottomShadow;
	const auto &shadowBody = st::historyRecordLockBodyShadow;
	const auto &shadowMargins = st::historyRecordLockMargin;

	const auto bottomMargin = anim::interpolate(
		0,
		rect().height() - shadowTop.height() - shadowBottom.height(),
		progress);

	const auto topMargin = anim::interpolate(
		rect().height() / 4,
		0,
		progress);

	const auto full = rect().marginsRemoved(
		style::margins(0, topMargin, 0, bottomMargin));
	const auto inner = full.marginsRemoved(shadowMargins);
	const auto content = inner.marginsRemoved(style::margins(
		0,
		originTop.height(),
		0,
		originBottom.height()));
	const auto contentShadow = full.marginsRemoved(style::margins(
		0,
		shadowTop.height(),
		0,
		shadowBottom.height()));

	const auto w = full.width();
	{
		shadowTop.paint(p, full.topLeft(), w);
		originTop.paint(p, inner.topLeft(), w);
	}
	{
		const auto shadowPos = QPoint(
			full.x(),
			contentShadow.y() + contentShadow.height());
		const auto originPos = QPoint(
			inner.x(),
			content.y() + content.height());
		shadowBottom.paint(p, shadowPos, w);
		originBottom.paint(p, originPos, w);
	}
	{
		shadowBody.fill(p, contentShadow);
		originBody.fill(p, content);
	}
	{
		const auto &arrow = st::historyRecordLockArrow;
		const auto arrowRect = QRect(
			inner.x(),
			content.y() + content.height() - arrow.height() / 2,
			inner.width(),
			arrow.height());
		p.setOpacity(1. - progress);
		arrow.paintInCenter(p, arrowRect);
		p.setOpacity(1.);
	}
	{
		const auto &icon = isLocked()
			? st::historyRecordLockIcon
			: st::historyRecordUnlockIcon;
		icon.paint(
			p,
			inner.x() + (inner.width() - icon.width()) / 2,
			inner.y() + (originTop.height() * 2 - icon.height()) / 2,
			inner.width());
	}
}

void RecordLock::startLockingAnimation(float64 to) {
	auto callback = [=](auto value) { setProgress(value); };
	const auto duration = st::historyRecordVoiceShowDuration;
	_lockEnderAnimation.start(std::move(callback), 0., to, duration);
}

void RecordLock::requestPaintProgress(float64 progress) {
	if (isHidden() || isLocked() || _lockEnderAnimation.animating()) {
		return;
	}
	if (!_progress.current() && (progress > .3)) {
		startLockingAnimation(progress);
		return;
	}
	setProgress(progress);
}

void RecordLock::setProgress(float64 progress) {
	_progress = progress;
	update();
}

bool RecordLock::isLocked() const {
	return _progress.current() == 1.;
}

rpl::producer<> RecordLock::locks() const {
	return _progress.changes(
	) | rpl::filter([=] { return isLocked(); }) | rpl::to_empty;
}

VoiceRecordBar::VoiceRecordBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::RpWidget*> sectionWidget,
	not_null<Window::SessionController*> controller,
	std::shared_ptr<Ui::SendButton> send,
	int recorderHeight)
: RpWidget(parent)
, _sectionWidget(sectionWidget)
, _controller(controller)
, _send(send)
, _lock(std::make_unique<RecordLock>(sectionWidget))
, _level(std::make_unique<VoiceRecordButton>(
	sectionWidget,
	_controller->widget()->leaveEvents()))
, _startTimer([=] { startRecording(); })
, _cancelFont(st::historyRecordFont) {
	resize(QSize(parent->width(), recorderHeight));
	init();
	hideFast();
}

VoiceRecordBar::VoiceRecordBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	std::shared_ptr<Ui::SendButton> send,
	int recorderHeight)
: VoiceRecordBar(parent, parent, controller, send, recorderHeight) {
}

VoiceRecordBar::~VoiceRecordBar() {
	if (isRecording()) {
		stopRecording(false);
	}
}

void VoiceRecordBar::updateMessageGeometry() {
	const auto left = _durationRect.x()
		+ _durationRect.width()
		+ st::historyRecordTextLeft;
	const auto right = width()
		- _send->width()
		- st::historyRecordTextRight;
	const auto textWidth = _message.maxWidth();
	const auto width = ((right - left) < textWidth)
		? st::historyRecordTextWidthForWrap
		: textWidth;
	const auto countLines = std::ceil((float)textWidth / width);
	const auto textHeight = _message.minHeight() * countLines;
	_messageRect = QRect(
		left + (right - left - width) / 2,
		(height() - textHeight) / 2,
		width,
		textHeight);
}

void VoiceRecordBar::updateLockGeometry() {
	const auto right = anim::interpolate(
		-_lock->width(),
		st::historyRecordLockPosition.x(),
		_showLockAnimation.value(_lockShowing.current() ? 1. : 0.));
	_lock->moveToRight(right, _lock->y());
}

void VoiceRecordBar::init() {
	// Keep VoiceRecordBar behind SendButton.
	rpl::single(
	) | rpl::then(
		_send->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::ZOrderChange;
		}) | rpl::to_empty
	) | rpl::start_with_next([=] {
		orderControls();
	}, lifetime());

	shownValue(
	) | rpl::start_with_next([=](bool show) {
		if (!show) {
			finish();
		}
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		_centerY = size.height() / 2;
		{
			const auto maxD = st::historyRecordSignalRadius * 2;
			const auto point = _centerY - st::historyRecordSignalRadius;
			_redCircleRect = { point, point, maxD, maxD };
		}
		{
			const auto durationLeft = _redCircleRect.x()
				+ _redCircleRect.width()
				+ st::historyRecordDurationSkip;
			const auto &ascent = _cancelFont->ascent;
			_durationRect = QRect(
				durationLeft,
				_redCircleRect.y() - (ascent - _redCircleRect.height()) / 2,
				_cancelFont->width(FormatVoiceDuration(kMaxSamples)),
				ascent);
		}
		updateMessageGeometry();
		updateLockGeometry();
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);
		if (_showAnimation.animating()) {
			p.setOpacity(showAnimationRatio());
		}
		p.fillRect(clip, st::historyComposeAreaBg);

		if (clip.intersects(_messageRect)) {
			// The message should be painted first to avoid flickering.
			drawMessage(p, activeAnimationRatio());
		}
		if (clip.intersects(_durationRect)) {
			drawDuration(p);
		}
		if (clip.intersects(_redCircleRect)) {
			// Should be the last to be drawn.
			drawRedCircle(p);
		}
	}, lifetime());

	_inField.changes(
	) | rpl::start_with_next([=](bool value) {
		activeAnimate(value);
	}, lifetime());

	_lockShowing.changes(
	) | rpl::start_with_next([=](bool show) {
		const auto to = show ? 1. : 0.;
		const auto from = show ? 0. : 1.;
		const auto duration = st::historyRecordLockShowDuration;
		_lock->show();
		auto callback = [=](auto value) {
			updateLockGeometry();
			if (value == 0. && !show) {
				_lock->hide();
			} else if (value == 1. && show) {
				computeAndSetLockProgress(QCursor::pos());
			}
		};
		_showLockAnimation.start(std::move(callback), from, to, duration);
	}, lifetime());

	_lock->locks(
	) | rpl::start_with_next([=] {
		installClickOutsideFilter();

		_level->clicks(
		) | rpl::start_with_next([=] {
			stop(true);
		}, _recordingLifetime);

		rpl::single(
			false
		) | rpl::then(
			_level->actives()
		) | rpl::start_with_next([=](bool enter) {
			_inField = enter;
		}, _recordingLifetime);
	}, lifetime());

	rpl::merge(
		_lock->locks(),
		shownValue() | rpl::to_empty
	) | rpl::start_with_next([=] {
		const auto direction = Qt::LayoutDirectionAuto;
		_message.setText(
			st::historyRecordTextStyle,
			_lock->isLocked()
				? tr::lng_record_lock_cancel(tr::now)
				: tr::lng_record_cancel(tr::now),
			TextParseOptions{ TextParseMultiline, 0, 0, direction });

		updateMessageGeometry();
		update(_messageRect);
	}, lifetime());

	_send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isTypeRecord()
			&& !isRecording()
			&& !_showAnimation.animating()
			&& !_lock->isLocked()
			&& (e->type() == QEvent::MouseButtonPress
				|| e->type() == QEvent::MouseButtonRelease);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			if (_startRecordingFilter && _startRecordingFilter()) {
				return;
			}
			_startTimer.callOnce(st::historyRecordVoiceShowDuration);
		} else if (e->type() == QEvent::MouseButtonRelease) {
			_startTimer.cancel();
		}
	}, lifetime());
}

void VoiceRecordBar::activeAnimate(bool active) {
	const auto to = active ? 1. : 0.;
	const auto duration = st::historyRecordVoiceDuration;
	if (_activeAnimation.animating()) {
		_activeAnimation.change(to, duration);
	} else {
		auto callback = [=] {
			update(_messageRect);
			_level->requestPaintColor(activeAnimationRatio());
		};
		const auto from = active ? 0. : 1.;
		_activeAnimation.start(std::move(callback), from, to, duration);
	}
}

void VoiceRecordBar::visibilityAnimate(bool show, Fn<void()> &&callback) {
	const auto to = show ? 1. : 0.;
	const auto from = show ? 0. : 1.;
	const auto duration = st::historyRecordVoiceShowDuration;
	auto animationCallback = [=, callback = std::move(callback)](auto value) {
		_level->requestPaintProgress(value);
		update();
		if ((show && value == 1.) || (!show && value == 0.)) {
			if (callback) {
				callback();
			}
		}
	};
	_showAnimation.start(std::move(animationCallback), from, to, duration);
}

void VoiceRecordBar::setEscFilter(Fn<bool()> &&callback) {
	_escFilter = std::move(callback);
}

void VoiceRecordBar::setStartRecordingFilter(Fn<bool()> &&callback) {
	_startRecordingFilter = std::move(callback);
}

void VoiceRecordBar::setLockBottom(rpl::producer<int> &&bottom) {
	std::move(
		bottom
	) | rpl::start_with_next([=](int value) {
		_lock->moveToLeft(_lock->x(), value - _lock->height());
	}, lifetime());
}

void VoiceRecordBar::setSendButtonGeometryValue(
		rpl::producer<QRect> &&geometry) {
	std::move(
		geometry
	) | rpl::start_with_next([=](QRect r) {
		const auto center = (r.width() - _level->width()) / 2;
		_level->moveToLeft(r.x() + center, r.y() + center);
	}, lifetime());
}

void VoiceRecordBar::startRecording() {
	if (isRecording()) {
		return;
	}
	auto appearanceCallback = [=] {
		if(_showAnimation.animating()) {
			return;
		}

		using namespace ::Media::Capture;
		if (!instance()->available()) {
			stop(false);
			return;
		}

		const auto shown = _recordingLifetime.make_state<bool>(false);

		_recording = true;
		_controller->widget()->setInnerFocus();
		instance()->start();
		instance()->updated(
		) | rpl::start_with_next_error([=](const Update &update) {
			if (!(*shown) && !_showAnimation.animating()) {
				// Show the lock widget after the first successful update.
				*shown = true;
				_lockShowing = true;
				startRedCircleAnimation();
			}
			recordUpdated(update.level, update.samples);
		}, [=] {
			stop(false);
		}, _recordingLifetime);
	};
	visibilityAnimate(true, std::move(appearanceCallback));
	show();

	_inField = true;

	_send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isTypeRecord()
			&& !_lock->isLocked()
			&& (e->type() == QEvent::MouseMove
				|| e->type() == QEvent::MouseButtonRelease);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			const auto globalPos = mouse->globalPos();
			const auto localPos = mapFromGlobal(globalPos);
			const auto inField = rect().contains(localPos);
			_inField = inField
				? inField
				: _level->inCircle(_level->mapFromGlobal(globalPos));

			if (_showLockAnimation.animating() || !hasDuration()) {
				return;
			}
			computeAndSetLockProgress(mouse->globalPos());
		} else if (type == QEvent::MouseButtonRelease) {
			stop(_inField.current());
		}
	}, _recordingLifetime);
}

void VoiceRecordBar::recordUpdated(quint16 level, int samples) {
	_level->requestPaintLevel(level);
	_recordingSamples = samples;
	if (samples < 0 || samples >= kMaxSamples) {
		stop(samples > 0 && _inField.current());
	}
	Core::App().updateNonIdle();
	update(_durationRect);
	_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice });
}

void VoiceRecordBar::stop(bool send) {
	auto disappearanceCallback = [=] {
		hide();

		stopRecording(send);
	};
	_lockShowing = false;
	visibilityAnimate(false, std::move(disappearanceCallback));
}

void VoiceRecordBar::finish() {
	_recordingLifetime.destroy();
	_lockShowing = false;
	_recording = false;
	_inField = false;
	_redCircleProgress = 0.;
	_recordingSamples = 0;

	_showAnimation.stop();

	_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice, -1 });
	_controller->widget()->setInnerFocus();
}

void VoiceRecordBar::hideFast() {
	hide();
	_lock->hide();
	_level->hide();
	stopRecording(false);
}

void VoiceRecordBar::stopRecording(bool send) {
	using namespace ::Media::Capture;
	if (!send) {
		instance()->stop();
		return;
	}
	instance()->stop(crl::guard(this, [=](const Result &data) {
		if (data.bytes.isEmpty()) {
			return;
		}

		Window::ActivateWindow(_controller);
		const auto duration = Duration(data.samples);
		_sendVoiceRequests.fire({ data.bytes, data.waveform, duration });
	}));
}

void VoiceRecordBar::drawDuration(Painter &p) {
	const auto duration = FormatVoiceDuration(_recordingSamples);
	p.setFont(_cancelFont);
	p.setPen(st::historyRecordDurationFg);

	p.drawText(_durationRect, style::al_left, duration);
}

void VoiceRecordBar::startRedCircleAnimation() {
	if (anim::Disabled()) {
		return;
	}
	const auto animation = _recordingLifetime
		.make_state<Ui::Animations::Basic>();
	animation->init([=](crl::time now) {
		const auto diffTime = now - animation->started();
		_redCircleProgress = std::abs(std::sin(diffTime / 400.));
		update(_redCircleRect);
		return true;
	});
	animation->start();
}

void VoiceRecordBar::drawRedCircle(Painter &p) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordSignalColor);

	p.setOpacity(1. - _redCircleProgress);
	const int radii = st::historyRecordSignalRadius * showAnimationRatio();
	const auto center = _redCircleRect.center() + QPoint(1, 1);
	p.drawEllipse(center, radii, radii);
	p.setOpacity(1.);
}

void VoiceRecordBar::drawMessage(Painter &p, float64 recordActive) {
	p.setPen(
		anim::pen(
			st::historyRecordCancel,
			st::historyRecordCancelActive,
			1. - recordActive));

	_message.draw(
		p,
		_messageRect.x(),
		_messageRect.y(),
		_messageRect.width(),
		style::al_center);
}

rpl::producer<SendActionUpdate> VoiceRecordBar::sendActionUpdates() const {
	return _sendActionUpdates.events();
}

rpl::producer<VoiceToSend> VoiceRecordBar::sendVoiceRequests() const {
	return _sendVoiceRequests.events();
}

bool VoiceRecordBar::isRecording() const {
	return _recording.current();
}

void VoiceRecordBar::finishAnimating() {
	_showAnimation.stop();
}

rpl::producer<bool> VoiceRecordBar::recordingStateChanges() const {
	return _recording.changes();
}

rpl::producer<bool> VoiceRecordBar::lockShowStarts() const {
	return _lockShowing.changes();
}

bool VoiceRecordBar::isLockPresent() const {
	return _lockShowing.current();
}

bool VoiceRecordBar::isTypeRecord() const {
	return (_send->type() == Ui::SendButton::Type::Record);
}

bool VoiceRecordBar::hasDuration() const {
	return _recordingSamples > 0;
}

float64 VoiceRecordBar::activeAnimationRatio() const {
	return _activeAnimation.value(_inField.current() ? 1. : 0.);
}

float64 VoiceRecordBar::showAnimationRatio() const {
	// There is no reason to set the final value to zero,
	// because at zero this widget is hidden.
	return _showAnimation.value(1.);
}

void VoiceRecordBar::computeAndSetLockProgress(QPoint globalPos) {
	const auto localPos = mapFromGlobal(globalPos);
	const auto lower = _lock->height();
	const auto higher = 0;
	const auto progress = localPos.y() / (float64)(higher - lower);
	_lock->requestPaintProgress(std::clamp(progress, 0., 1.));
}

void VoiceRecordBar::orderControls() {
	stackUnder(_send.get());
	_level->raise();
	_lock->raise();
}

void VoiceRecordBar::installClickOutsideFilter() {
	const auto box = _recordingLifetime.make_state<QPointer<ConfirmBox>>();
	const auto showBox = [=] {
		if (*box) {
			return;
		}
		auto sure = [=](Fn<void()> &&close) {
			stop(false);
			close();
		};
		*box = Ui::show(Box<ConfirmBox>(
			tr::lng_record_lock_cancel_sure(tr::now),
			tr::lng_record_lock_discard(tr::now),
			st::attentionBoxButton,
			std::move(sure)));
	};

	const auto computeResult = [=](not_null<QEvent*> e) {
		using Type = FilterType;
		if (!_lock->isLocked()) {
			return Type::Continue;
		}
		const auto type = e->type();
		const auto noBox = !(*box);
		if (type == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get())->key();
			const auto isEsc = (key == Qt::Key_Escape);
			const auto isEnter = (key == Qt::Key_Enter
				|| key == Qt::Key_Return);
			if (noBox) {
				if (isEnter) {
					stop(true);
					return Type::Cancel;
				} else if (isEsc && (_escFilter && _escFilter())) {
					return Type::Continue;
				}
				return Type::ShowBox;
			}
			return (isEsc || isEnter) ? Type::Continue : Type::ShowBox;
		} else if (type == QEvent::ContextMenu || type == QEvent::Shortcut) {
			return Type::ShowBox;
		} else if (type == QEvent::MouseButtonPress) {
			return (noBox && !_inField.current())
				? Type::ShowBox
				: Type::Continue;
		}
		return Type::Continue;
	};

	auto filterCallback = [=](not_null<QEvent*> e) {
		using Result = base::EventFilterResult;
		switch(computeResult(e)) {
		case FilterType::ShowBox: {
			showBox();
			return Result::Cancel;
		}
		case FilterType::Continue: return Result::Continue;
		case FilterType::Cancel: return Result::Cancel;
		default: return Result::Continue;
		}
	};

	auto filter = base::install_event_filter(
		QCoreApplication::instance(),
		std::move(filterCallback));

	_recordingLifetime.make_state<base::unique_qptr<QObject>>(
		std::move(filter));
}

} // namespace HistoryView::Controls
