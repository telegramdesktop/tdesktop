/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "history/view/controls/compose_controls_common.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Ui {
class SendButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView::Controls {

class VoiceRecordBar final : public Ui::RpWidget {
public:
	using SendActionUpdate = Controls::SendActionUpdate;
	using VoiceToSend = Controls::VoiceToSend;

	void startRecording();
	void finishAnimating();

	[[nodiscard]] rpl::producer<SendActionUpdate> sendActionUpdates() const;
	[[nodiscard]] rpl::producer<VoiceToSend> sendVoiceRequests() const;
	[[nodiscard]] rpl::producer<bool> recordingStateChanges() const;
	[[nodiscard]] rpl::producer<> startRecordingRequests() const;

	[[nodiscard]] bool isRecording() const;

	VoiceRecordBar(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<Ui::SendButton> send,
		int recorderHeight);
	~VoiceRecordBar();

private:
	void init();

	void updateControlsGeometry(QSize size);

	void recordError();
	void recordUpdated(quint16 level, int samples);

	bool recordingAnimationCallback(crl::time now);

	void stop(bool send);
	void stopRecording(bool send);
	void visibilityAnimate(bool show, Fn<void()> &&callback);

	void recordStopCallback(bool active);
	void recordUpdateCallback(QPoint globalPos);

	bool showRecordButton() const;
	void drawRecording(Painter &p, float64 recordActive);
	void updateOverStates(QPoint pos);

	bool isTypeRecord() const;

	void activeAnimate(bool active);
	float64 activeAnimationRatio() const;

	const not_null<Window::SessionController*> _controller;
	const std::unique_ptr<Ui::RpWidget> _wrap;
	const std::shared_ptr<Ui::SendButton> _send;

	rpl::event_stream<SendActionUpdate> _sendActionUpdates;
	rpl::event_stream<VoiceToSend> _sendVoiceRequests;

	int _centerY = 0;
	QRect _redCircleRect;
	QRect _durationRect;

	rpl::variable<bool> _recording = false;
	rpl::variable<bool> _inField = false;
	int _recordingSamples = 0;

	const style::font &_cancelFont;
	int _recordCancelWidth;

	rpl::lifetime _recordingLifetime;

	// This can animate for a very long time (like in music playing),
	// so it should be a Basic, not a Simple animation.
	Ui::Animations::Basic _recordingAnimation;
	Ui::Animations::Simple _activeAnimation;
	Ui::Animations::Simple _showAnimation;
	anim::value _recordingLevel;

};

} // namespace HistoryView::Controls
