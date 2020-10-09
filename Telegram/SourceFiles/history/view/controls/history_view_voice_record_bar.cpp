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
	void reset();

	[[nodiscard]] rpl::producer<> locks() const;
	[[nodiscard]] bool isLocked() const;

private:
	void init();

	void drawProgress(Painter &p);

	Ui::Animations::Simple _lockAnimation;

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

void RecordLock::requestPaintProgress(float64 progress) {
	if (isHidden() || isLocked()) {
		return;
	}
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
	not_null<Window::SessionController*> controller,
	std::shared_ptr<Ui::SendButton> send,
	int recorderHeight)
: RpWidget(parent)
, _controller(controller)
, _send(send)
, _lock(std::make_unique<RecordLock>(parent))
, _cancelFont(st::historyRecordFont)
, _recordingAnimation([=](crl::time now) {
	return recordingAnimationCallback(now);
}) {
	resize(QSize(parent->width(), recorderHeight));
	init();
}

VoiceRecordBar::~VoiceRecordBar() {
	if (isRecording()) {
		stopRecording(false);
	}
}

void VoiceRecordBar::updateControlsGeometry(QSize size) {
	_centerY = size.height() / 2;
	{
		const auto maxD = st::historyRecordSignalMax * 2;
		const auto point = _centerY - st::historyRecordSignalMax;
		_redCircleRect = { point, point, maxD, maxD };
	}
	{
		const auto durationLeft = _redCircleRect.x()
			+ _redCircleRect.width()
			+ st::historyRecordDurationSkip;
		_durationRect = QRect(
			durationLeft,
			_redCircleRect.y(),
			_cancelFont->width(FormatVoiceDuration(kMaxSamples)),
			_redCircleRect.height());
	}
	{
		const auto left = _durationRect.x()
			+ _durationRect.width()
			+ ((_send->width() - st::historyRecordVoice.width()) / 2);
		const auto right = width() - _send->width();
		const auto width = _cancelFont->width(cancelMessage());
		_messageRect = QRect(
			left + (right - left - width) / 2,
			st::historyRecordTextTop,
			width + st::historyRecordDurationSkip,
			_cancelFont->height);
	}
}

void VoiceRecordBar::init() {
	hide();
	// Keep VoiceRecordBar behind SendButton.
	rpl::single(
	) | rpl::then(
		_send->events(
		) | rpl::filter([](not_null<QEvent*> e) {
			return e->type() == QEvent::ZOrderChange;
		}) | rpl::to_empty
	) | rpl::start_with_next([=] {
		stackUnder(_send.get());
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		updateControlsGeometry(size);
	}, lifetime());

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);
		if (_showAnimation.animating()) {
			p.setOpacity(_showAnimation.value(1.));
		}
		p.fillRect(clip, st::historyComposeAreaBg);

		if (clip.intersects(_messageRect)) {
			// The message should be painted first to avoid flickering.
			drawMessage(p, activeAnimationRatio());
		}
		if (clip.intersects(_redCircleRect)) {
			drawRecording(p);
		}
		if (clip.intersects(_durationRect)) {
			drawDuration(p);
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
			const auto right = anim::interpolate(
				-_lock->width(),
				st::historyRecordLockPosition.x(),
				value);
			_lock->moveToRight(right, _lock->y());
			if (value == 0. && !show) {
				_lock->hide();
			} else if (value == 1. && show) {
				computeAndSetLockProgress(QCursor::pos());
			}
		};
		_showLockAnimation.start(std::move(callback), from, to, duration);
	}, lifetime());

	_lock->hide();
	_lock->locks(
	) | rpl::start_with_next([=] {

		updateControlsGeometry(rect().size());
		update(_messageRect);

		installClickOutsideFilter();

		_send->clicks(
		) | rpl::filter([=] {
			return _send->type() == Ui::SendButton::Type::Record;
		}) | rpl::start_with_next([=] {
			stop(true);
		}, _recordingLifetime);

		auto hover = _send->events(
		) | rpl::filter([=](not_null<QEvent*> e) {
			return e->type() == QEvent::Enter
				|| e->type() == QEvent::Leave;
		}) | rpl::map([=](not_null<QEvent*> e) {
			return (e->type() == QEvent::Enter);
		});

		_send->setLockRecord(true);
		_send->setForceRippled(true);
		rpl::single(
			false
		) | rpl::then(
			std::move(hover)
		) | rpl::start_with_next([=](bool enter) {
			_inField = enter;
		}, _recordingLifetime);
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
			_send->requestPaintRecord(activeAnimationRatio());
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

void VoiceRecordBar::setLockBottom(rpl::producer<int> &&bottom) {
	std::move(
		bottom
	) | rpl::start_with_next([=](int value) {
		_lock->moveToLeft(_lock->x(), value - _lock->height());
	}, lifetime());
}

void VoiceRecordBar::startRecording() {
	auto appearanceCallback = [=] {
		Expects(!_showAnimation.animating());

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
			const auto localPos = mapFromGlobal(mouse->globalPos());
			_inField = rect().contains(localPos);

			if (_showLockAnimation.animating()) {
				return;
			}
			computeAndSetLockProgress(mouse->globalPos());
		} else if (type == QEvent::MouseButtonRelease) {
			stop(_inField.current());
		}
	}, _recordingLifetime);
}

bool VoiceRecordBar::recordingAnimationCallback(crl::time now) {
	const auto dt = anim::Disabled()
		? 1.
		: ((now - _recordingAnimation.started())
			/ float64(kRecordingUpdateDelta));
	if (dt >= 1.) {
		_recordingLevel.finish();
	} else {
		_recordingLevel.update(dt, anim::linear);
	}
	if (!anim::Disabled()) {
		update(_redCircleRect);
	}
	return (dt < 1.);
}

void VoiceRecordBar::recordUpdated(quint16 level, int samples) {
	_recordingLevel.start(level);
	_recordingAnimation.start();
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
		Expects(!_showAnimation.animating());

		hide();
		_recording = false;

		stopRecording(send);

		_recordingLevel = anim::value();
		_recordingAnimation.stop();

		_inField = false;

		_recordingLifetime.destroy();
		_recordingSamples = 0;
		_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice, -1 });

		_send->setForceRippled(false);
		_send->clearRecordState();

		_controller->widget()->setInnerFocus();
	};
	_lockShowing = false;
	visibilityAnimate(false, std::move(disappearanceCallback));
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

void VoiceRecordBar::drawRecording(Painter &p) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordSignalColor);

	const auto min = st::historyRecordSignalMin;
	const auto max = st::historyRecordSignalMax;
	const auto delta = std::min(_recordingLevel.current() / 0x4000, 1.);
	const auto radii = qRound(min + (delta * (max - min)));
	const auto center = _redCircleRect.center() + QPoint(1, 1);
	p.drawEllipse(center, radii, radii);
}

void VoiceRecordBar::drawMessage(Painter &p, float64 recordActive) {
	p.setPen(
		anim::pen(
			st::historyRecordCancel,
			st::historyRecordCancelActive,
			1. - recordActive));
	p.drawText(
		_messageRect.x(),
		_messageRect.y() + _cancelFont->ascent,
		cancelMessage());
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
	_recordingAnimation.stop();
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

rpl::producer<> VoiceRecordBar::startRecordingRequests() const {
	return _send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isTypeRecord()
			&& !_showAnimation.animating()
			&& !_lock->isLocked()
			&& (e->type() == QEvent::MouseButtonPress);
	}) | rpl::to_empty;
}

bool VoiceRecordBar::isTypeRecord() const {
	return (_send->type() == Ui::SendButton::Type::Record);
}

float64 VoiceRecordBar::activeAnimationRatio() const {
	return _activeAnimation.value(_inField.current() ? 1. : 0.);
}

QString VoiceRecordBar::cancelMessage() const {
	return _lock->isLocked()
		? tr::lng_record_lock_cancel(tr::now)
		: tr::lng_record_cancel(tr::now);
}

void VoiceRecordBar::computeAndSetLockProgress(QPoint globalPos) {
	const auto localPos = mapFromGlobal(globalPos);
	const auto lower = _lock->height();
	const auto higher = 0;
	const auto progress = localPos.y() / (float64)(higher - lower);
	_lock->requestPaintProgress(std::clamp(progress, 0., 1.));
}

void VoiceRecordBar::installClickOutsideFilter() {
	const auto box = _recordingLifetime.make_state<QPointer<ConfirmBox>>();
	const auto showBox = [=] {
		if (*box || _send->underMouse()) {
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
		using Result = base::EventFilterResult;
		if (!_lock->isLocked()) {
			return Result::Continue;
		}
		const auto type = e->type();
		const auto noBox = !(*box);
		if (type == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get())->key();
			const auto isEsc = (key == Qt::Key_Escape);
			if (noBox) {
				if (isEsc && (_escFilter && _escFilter())) {
					return Result::Continue;
				}
				return Result::Cancel;
			}
			const auto cancelOrConfirmBox = (isEsc
				|| (key == Qt::Key_Enter || key == Qt::Key_Return));
			return cancelOrConfirmBox ? Result::Continue : Result::Cancel;
		} else if (type == QEvent::ContextMenu || type == QEvent::Shortcut) {
			return Result::Cancel;
		} else if (type == QEvent::MouseButtonPress) {
			return (noBox && !_send->underMouse())
				? Result::Cancel
				: Result::Continue;
		}
		return Result::Continue;
	};

	auto filterCallback = [=](not_null<QEvent*> e) {
		const auto result = computeResult(e);
		if (result == base::EventFilterResult::Cancel) {
			showBox();
		}
		return result;
	};

	auto filter = base::install_event_filter(
		QCoreApplication::instance(),
		std::move(filterCallback));

	_recordingLifetime.make_state<base::unique_qptr<QObject>>(
		std::move(filter));
}

} // namespace HistoryView::Controls
