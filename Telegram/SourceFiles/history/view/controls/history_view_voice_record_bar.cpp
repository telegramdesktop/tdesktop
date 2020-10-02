/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_voice_record_bar.h"

#include "api/api_send_progress.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "media/audio/media_audio.h"
#include "media/audio/media_audio_capture.h"
#include "styles/style_chat.h"
#include "ui/controls/send_button.h"
#include "ui/text/format_values.h"
#include "window/window_session_controller.h"

namespace HistoryView::Controls {

namespace {

using SendActionUpdate = VoiceRecordBar::SendActionUpdate;
using VoiceToSend = VoiceRecordBar::VoiceToSend;

constexpr auto kRecordingUpdateDelta = crl::time(100);
constexpr auto kMaxSamples =
	::Media::Player::kDefaultFrequency * AudioVoiceMsgMaxLength;

[[nodiscard]] auto Duration(int samples) {
	return samples / ::Media::Player::kDefaultFrequency;
}

} // namespace

VoiceRecordBar::VoiceRecordBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	std::shared_ptr<Ui::SendButton> send,
	int recorderHeight)
: RpWidget(parent)
, _controller(controller)
, _wrap(std::make_unique<Ui::RpWidget>(parent))
, _send(send)
, _cancelFont(st::historyRecordFont)
, _recordCancelWidth(_cancelFont->width(tr::lng_record_cancel(tr::now)))
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
	) | rpl::start_with_next([=] {
		Painter p(this);
		p.fillRect(rect(), st::historyComposeAreaBg);

		drawRecording(p, _send->recordActiveRatio());
	}, lifetime());
}

void VoiceRecordBar::startRecording() {
	using namespace ::Media::Capture;
	if (!instance()->available()) {
		return;
	}
	show();
	_recording = true;

	instance()->start();
	instance()->updated(
	) | rpl::start_with_next_error([=](const Update &update) {
		recordUpdated(update.level, update.samples);
	}, [=] {
		stopRecording(false);
	}, _recordingLifetime);

	_inField = true;
	_controller->widget()->setInnerFocus();

	update();
	_send->setRecordActive(true);

	_send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isTypeRecord()
			&& (e->type() == QEvent::MouseMove
				|| e->type() == QEvent::MouseButtonRelease);
	}) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			const auto pos = mapFromGlobal(mouse->globalPos());
			const auto inField = rect().contains(pos);
			if (inField != _inField) {
				_inField = inField;
				_send->setRecordActive(_inField);
			}
		} else if (type == QEvent::MouseButtonRelease) {
			stopRecording(_inField);
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
		stopRecording(samples > 0 && _inField);
	}
	Core::App().updateNonIdle();
	update();
	_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice });
}

void VoiceRecordBar::stopRecording(bool send) {
	hide();
	_recording = false;

	using namespace ::Media::Capture;
	if (send) {
		instance()->stop(crl::guard(this, [=](const Result &data) {
			if (data.bytes.isEmpty()) {
				return;
			}

			Window::ActivateWindow(_controller);
			const auto duration = Duration(data.samples);
			_sendVoiceRequests.fire({ data.bytes, data.waveform, duration });
		}));
	} else {
		instance()->stop();
	}

	_recordingLevel = anim::value();
	_recordingAnimation.stop();

	_recordingLifetime.destroy();
	_recordingSamples = 0;
	_sendActionUpdates.fire({ Api::SendProgressType::RecordVoice, -1 });

	_controller->widget()->setInnerFocus();

	update();
	_send->setRecordActive(false);
}

void VoiceRecordBar::drawRecording(Painter &p, float64 recordActive) {
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordSignalColor);

	{
		PainterHighQualityEnabler hq(p);
		const auto min = st::historyRecordSignalMin;
		const auto max = st::historyRecordSignalMax;
		const auto delta = std::min(_recordingLevel.current() / 0x4000, 1.);
		const auto radii = qRound(min + (delta * (max - min)));
		const auto center = _redCircleRect.center() + QPoint(1, 1);
		p.drawEllipse(center, radii, radii);
	}

	const auto duration = Ui::FormatDurationText(Duration(_recordingSamples));
	p.setFont(_cancelFont);
	p.setPen(st::historyRecordDurationFg);

	const auto durationLeft = _redCircleRect.x()
		+ _redCircleRect.width()
		+ st::historyRecordDurationSkip;
	const auto durationWidth = _cancelFont->width(duration);
	p.drawText(
		QRect(
			durationLeft,
			_redCircleRect.y(),
			durationWidth,
			_redCircleRect.height()),
		style::al_left,
		duration);

	const auto leftCancel = durationLeft
		+ durationWidth
		+ ((_send->width() - st::historyRecordVoice.width()) / 2);
	const auto rightCancel = width() - _send->width();

	p.setPen(
		anim::pen(
			st::historyRecordCancel,
			st::historyRecordCancelActive,
			1. - recordActive));
	p.drawText(
		leftCancel + (rightCancel - leftCancel - _recordCancelWidth) / 2,
		st::historyRecordTextTop + _cancelFont->ascent,
		tr::lng_record_cancel(tr::now));
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
}

rpl::producer<bool> VoiceRecordBar::recordingStateChanges() const {
	return _recording.changes();
}

rpl::producer<> VoiceRecordBar::startRecordingRequests() const {
	return _send->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return isTypeRecord() && (e->type() == QEvent::MouseButtonPress);
	}) | rpl::to_empty;
}

bool VoiceRecordBar::isTypeRecord() const {
	return (_send->type() == Ui::SendButton::Type::Record);
}

} // namespace HistoryView::Controls
