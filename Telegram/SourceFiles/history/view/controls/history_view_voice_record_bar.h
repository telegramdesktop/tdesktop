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
#include "media/audio/media_audio_capture_common.h"
#include "ui/controls/round_video_recorder.h"
#include "ui/effects/animations.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"

struct VoiceData;

namespace style {
struct RecordBar;
} // namespace style

namespace Media::Capture {
enum class Error : uchar;
} // namespace Media::Capture

namespace Ui {
class AbstractButton;
class SendButton;
class RoundVideoRecorder;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace HistoryView::Controls {

class VoiceRecordButton;
class ListenWrap;
class RecordLock;
class CancelButton;

struct VoiceRecordBarDescriptor {
	not_null<Ui::RpWidget*> outerContainer;
	std::shared_ptr<ChatHelpers::Show> show;
	std::shared_ptr<Ui::SendButton> send;
	QString customCancelText;
	const style::RecordBar *stOverride = nullptr;
	int recorderHeight = 0;
	bool lockFromBottom = false;
};

class VoiceRecordBar final : public Ui::RpWidget {
public:
	using SendActionUpdate = Controls::SendActionUpdate;
	using VoiceToSend = Controls::VoiceToSend;
	using FilterCallback = Fn<bool()>;
	using Error = ::Media::Capture::Error;

	VoiceRecordBar(
		not_null<Ui::RpWidget*> parent,
		VoiceRecordBarDescriptor &&descriptor);
	VoiceRecordBar(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Ui::SendButton> send,
		int recorderHeight);
	~VoiceRecordBar();

	void showDiscardBox(
		Fn<void()> &&callback,
		anim::type animated = anim::type::instant);

	void startRecordingAndLock(bool round);

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
	[[nodiscard]] rpl::producer<> recordingTipRequests() const;
	[[nodiscard]] rpl::producer<Error> errors() const;

	void requestToSendWithOptions(Api::SendOptions options);

	void setStartRecordingFilter(FilterCallback &&callback);
	void setTTLFilter(FilterCallback &&callback);
	void setPauseInsteadSend(bool pauseInsteadSend);

	[[nodiscard]] bool isRecording() const;
	[[nodiscard]] bool isRecordingLocked() const;
	[[nodiscard]] bool isLockPresent() const;
	[[nodiscard]] bool isListenState() const;
	[[nodiscard]] bool isActive() const;
	[[nodiscard]] bool isRecordingByAnotherBar() const;
	[[nodiscard]] bool isTTLButtonShown() const;

private:
	enum class StopType {
		Cancel,
		Send,
		Listen,
	};

	enum class TTLAnimationType {
		RightLeft,
		TopBottom,
		RightTopStatic,
	};

	void init();
	void initLockGeometry();
	void initLevelGeometry();

	void updateMessageGeometry();
	void updateLockGeometry();
	void updateTTLGeometry(TTLAnimationType type, float64 progress);

	void recordUpdated(quint16 level, int samples);
	void checkTipRequired();

	void stop(bool send);
	void stopRecording(StopType type, bool ttlBeforeHide = false);
	void visibilityAnimate(bool show, Fn<void()> &&callback);

	void drawDuration(QPainter &p);
	void drawRedCircle(QPainter &p);
	void drawMessage(QPainter &p, float64 recordActive);

	void startRedCircleAnimation();
	void installListenStateFilter();

	void startRecording();
	void prepareOnSendPress();

	[[nodiscard]] bool isTypeRecord() const;
	[[nodiscard]] bool hasDuration() const;

	void finish();

	void activeAnimate(bool active);
	[[nodiscard]] float64 showAnimationRatio() const;
	[[nodiscard]] float64 showListenAnimationRatio() const;
	[[nodiscard]] float64 activeAnimationRatio() const;

	void computeAndSetLockProgress(QPoint globalPos);
	[[nodiscard]] float64 calcLockProgress(QPoint globalPos);

	[[nodiscard]] bool peekTTLState() const;
	[[nodiscard]] bool takeTTLState() const;

	[[nodiscard]] bool createVideoRecorder();

	const style::RecordBar &_st;
	const not_null<Ui::RpWidget*> _outerContainer;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const std::shared_ptr<Ui::SendButton> _send;
	const std::unique_ptr<RecordLock> _lock;
	const std::unique_ptr<VoiceRecordButton> _level;
	const std::unique_ptr<CancelButton> _cancel;
	std::unique_ptr<Ui::AbstractButton> _ttlButton;
	std::unique_ptr<ListenWrap> _listen;

	Ui::RoundVideoResult _data;
	rpl::variable<bool> _paused;

	base::Timer _startTimer;

	rpl::event_stream<SendActionUpdate> _sendActionUpdates;
	rpl::event_stream<VoiceToSend> _sendVoiceRequests;
	rpl::event_stream<> _cancelRequests;
	rpl::event_stream<> _listenChanges;
	rpl::event_stream<Error> _errors;

	int _centerY = 0;
	QRect _redCircleRect;
	QRect _durationRect;
	QRect _messageRect;

	Ui::Text::String _message;

	FilterCallback _startRecordingFilter;
	FilterCallback _hasTTLFilter;

	base::unique_qptr<QObject> _keyFilterInRecordingState;

	bool _warningShown = false;
	bool _pauseInsteadSend = false;

	rpl::variable<bool> _recording = false;
	rpl::variable<bool> _inField = false;
	rpl::variable<bool> _lockShowing = false;
	int _recordingSamples = 0;
	float64 _redCircleProgress = 0.;

	rpl::event_stream<> _recordingTipRequests;
	crl::time _recordingTipRequire = 0;
	bool _lockFromBottom = false;

	std::unique_ptr<Ui::RoundVideoRecorder> _videoRecorder;
	std::vector<std::unique_ptr<Ui::RoundVideoRecorder>> _videoHiding;
	rpl::lifetime _videoCapturerLifetime;
	bool _recordingVideo = false;
	bool _fullRecord = false;

	const style::font &_cancelFont;

	rpl::lifetime _recordingLifetime;

	std::optional<Ui::RoundRect> _backgroundRect;
	Ui::Animations::Simple _showLockAnimation;
	Ui::Animations::Simple _lockToStopAnimation;
	Ui::Animations::Simple _showListenAnimation;
	Ui::Animations::Simple _activeAnimation;
	Ui::Animations::Simple _showAnimation;

};

} // namespace HistoryView::Controls
