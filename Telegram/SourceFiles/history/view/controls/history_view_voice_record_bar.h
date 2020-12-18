/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/timer.h"
#include "history/view/controls/compose_controls_common.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

struct VoiceData;

namespace Ui {
class SendButton;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace HistoryView::Controls {

class VoiceRecordButton;
class ListenWrap;
class RecordLock;
class CancelButton;

class VoiceRecordBar final : public Ui::RpWidget {
public:
	using SendActionUpdate = Controls::SendActionUpdate;
	using VoiceToSend = Controls::VoiceToSend;

	VoiceRecordBar(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> sectionWidget,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<Ui::SendButton> send,
		int recorderHeight);
	VoiceRecordBar(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		std::shared_ptr<Ui::SendButton> send,
		int recorderHeight);
	~VoiceRecordBar();

	void showDiscardBox(
		Fn<void()> &&callback,
		anim::type animated = anim::type::instant);

	void startRecording();
	void finishAnimating();
	void hideAnimated();
	void hideFast();
	void clearListenState();

	void orderControls();

	[[nodiscard]] rpl::producer<SendActionUpdate> sendActionUpdates() const;
	[[nodiscard]] rpl::producer<VoiceToSend> sendVoiceRequests() const;
	[[nodiscard]] rpl::producer<> cancelRequests() const;
	[[nodiscard]] rpl::producer<bool> recordingStateChanges() const;
	[[nodiscard]] rpl::producer<bool> lockShowStarts() const;
	[[nodiscard]] rpl::producer<not_null<QEvent*>> lockViewportEvents() const;
	[[nodiscard]] rpl::producer<> updateSendButtonTypeRequests() const;

	void requestToSendWithOptions(Api::SendOptions options);

	void setLockBottom(rpl::producer<int> &&bottom);
	void setSendButtonGeometryValue(rpl::producer<QRect> &&geometry);
	void setStartRecordingFilter(Fn<bool()> &&callback);

	[[nodiscard]] bool isRecording() const;
	[[nodiscard]] bool isLockPresent() const;
	[[nodiscard]] bool isListenState() const;
	[[nodiscard]] bool isActive() const;

private:
	enum class StopType {
		Cancel,
		Send,
		Listen,
	};

	void init();

	void updateMessageGeometry();
	void updateLockGeometry();

	void recordUpdated(quint16 level, int samples);

	bool recordingAnimationCallback(crl::time now);

	void stop(bool send);
	void stopRecording(StopType type);
	void visibilityAnimate(bool show, Fn<void()> &&callback);

	bool showRecordButton() const;
	void drawDuration(Painter &p);
	void drawRedCircle(Painter &p);
	void drawMessage(Painter &p, float64 recordActive);

	void startRedCircleAnimation();
	void installListenStateFilter();

	bool isTypeRecord() const;
	bool hasDuration() const;

	void finish();

	void activeAnimate(bool active);
	float64 showAnimationRatio() const;
	float64 showListenAnimationRatio() const;
	float64 activeAnimationRatio() const;

	void computeAndSetLockProgress(QPoint globalPos);

	const not_null<Ui::RpWidget*> _sectionWidget;
	const not_null<Window::SessionController*> _controller;
	const std::shared_ptr<Ui::SendButton> _send;
	const std::unique_ptr<RecordLock> _lock;
	const std::unique_ptr<VoiceRecordButton> _level;
	const std::unique_ptr<CancelButton> _cancel;
	std::unique_ptr<ListenWrap> _listen;

	base::Timer _startTimer;

	rpl::event_stream<SendActionUpdate> _sendActionUpdates;
	rpl::event_stream<VoiceToSend> _sendVoiceRequests;
	rpl::event_stream<> _cancelRequests;
	rpl::event_stream<> _listenChanges;

	int _centerY = 0;
	QRect _redCircleRect;
	QRect _durationRect;
	QRect _messageRect;

	Ui::Text::String _message;

	Fn<bool()> _startRecordingFilter;

	rpl::variable<bool> _recording = false;
	rpl::variable<bool> _inField = false;
	rpl::variable<bool> _lockShowing = false;
	int _recordingSamples = 0;
	float64 _redCircleProgress = 0.;

	const style::font &_cancelFont;

	rpl::lifetime _recordingLifetime;

	Ui::Animations::Simple _showLockAnimation;
	Ui::Animations::Simple _lockToStopAnimation;
	Ui::Animations::Simple _showListenAnimation;
	Ui::Animations::Simple _activeAnimation;
	Ui::Animations::Simple _showAnimation;

};

} // namespace HistoryView::Controls
