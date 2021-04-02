/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_instance.h"
#include "ui/effects/animations.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"

#include <QtCore/QPointer>

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Ui {
class IconButton;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player

namespace View {

class PlaybackProgress;

[[nodiscard]] QRect RotatedRect(QRect rect, int rotation);
[[nodiscard]] bool UsePainterRotation(int rotation);
[[nodiscard]] QSize FlipSizeByRotation(QSize size, int rotation);
[[nodiscard]] QImage RotateFrameImage(QImage image, int rotation);

#if defined Q_OS_MAC && !defined OS_MAC_OLD
#define USE_OPENGL_OVERLAY_WIDGET
#endif // Q_OS_MAC && !OS_MAC_OLD

#ifdef USE_OPENGL_OVERLAY_WIDGET
using PipParent = Ui::RpWidgetWrap<QOpenGLWidget>;
#else // USE_OPENGL_OVERLAY_WIDGET
using PipParent = Ui::RpWidget;
#endif // USE_OPENGL_OVERLAY_WIDGET

class PipPanel final : public PipParent {
public:
	struct Position {
		RectParts attached = RectPart(0);
		RectParts snapped = RectPart(0);
		QRect geometry;
		QRect screen;
	};
	using FrameRequest = Streaming::FrameRequest;

	PipPanel(
		QWidget *parent,
		Fn<void(QPainter&, FrameRequest)> paint);

	void setAspectRatio(QSize ratio);
	[[nodiscard]] Position countPosition() const;
	void setPosition(Position position);
	[[nodiscard]] QRect inner() const;
	[[nodiscard]] RectParts attached() const;
	void setDragDisabled(bool disabled);
	[[nodiscard]] bool dragging() const;

	[[nodiscard]] rpl::producer<> saveGeometryRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

	void setVisibleHook(bool visible) override;

private:
	void setPositionDefault();
	void setPositionOnScreen(Position position, QRect available);

	QScreen *myScreen() const;
	void startSystemDrag();
	void processDrag(QPoint point);
	void finishDrag(QPoint point);
	void updatePositionAnimated();
	void updateOverState(QPoint point);
	void moveAnimated(QPoint to);
	void updateDecorations();

	QPointer<QWidget> _parent;
	Fn<void(QPainter&, FrameRequest)> _paint;
	RectParts _attached = RectParts();
	RectParts _snapped = RectParts();
	QSize _ratio;

	bool _useTransparency = true;
	bool _dragDisabled = false;
	style::margins _padding;

	RectPart _overState = RectPart();
	std::optional<RectPart> _pressState;
	QPoint _pressPoint;
	QRect _dragStartGeometry;
	std::optional<RectPart> _dragState;
	rpl::event_stream<> _saveGeometryRequests;

	QPoint _positionAnimationFrom;
	QPoint _positionAnimationTo;
	Ui::Animations::Simple _positionAnimation;

};

class Pip final {
public:
	class Delegate {
	public:
		virtual void pipSaveGeometry(QByteArray geometry) = 0;
		[[nodiscard]] virtual QByteArray pipLoadGeometry() = 0;
		[[nodiscard]] virtual float64 pipPlaybackSpeed() = 0;
		[[nodiscard]] virtual QWidget *pipParentWidget() = 0;
	};

	Pip(
		not_null<Delegate*> delegate,
		not_null<DocumentData*> data,
		FullMsgId contextId,
		std::shared_ptr<Streaming::Document> shared,
		FnMut<void()> closeAndContinue,
		FnMut<void()> destroy);
	~Pip();

private:
	enum class OverState {
		None,
		Close,
		Enlarge,
		Playback,
		Other,
	};
	enum class ThumbState {
		Empty,
		Inline,
		Thumb,
		Good,
		Cover,
	};
	struct Button {
		QRect area;
		QRect icon;
		OverState state = OverState::None;
		Ui::Animations::Simple active;
	};
	using FrameRequest = Streaming::FrameRequest;

	void setupPanel();
	void setupButtons();
	void setupStreaming();
	void paint(QPainter &p, FrameRequest request);
	void playbackPauseResume();
	void waitingAnimationCallback();
	void handleStreamingUpdate(Streaming::Update &&update);
	void handleStreamingError(Streaming::Error &&error);
	void saveGeometry();

	void updatePlaybackState();
	void updatePlayPauseResumeState(const Player::TrackState &state);
	void restartAtSeekPosition(crl::time position);

	[[nodiscard]] bool canUseVideoFrame() const;
	[[nodiscard]] QImage videoFrame(const FrameRequest &request) const;
	[[nodiscard]] QImage videoFrameForDirectPaint(
		const FrameRequest &request) const;
	[[nodiscard]] OverState computeState(QPoint position) const;
	void setOverState(OverState state);
	void setPressedState(std::optional<OverState> state);
	[[nodiscard]] OverState activeState() const;
	[[nodiscard]] float64 activeValue(const Button &button) const;
	void updateActiveState(OverState was);
	void updatePlaybackTexts(int64 position, int64 length, int64 frequency);

	void handleMouseMove(QPoint position);
	void handleMousePress(QPoint position, Qt::MouseButton button);
	void handleMouseRelease(QPoint position, Qt::MouseButton button);
	void handleDoubleClick(Qt::MouseButton button);
	void handleLeave();
	void handleClose();

	void paintControls(QPainter &p) const;
	void paintFade(QPainter &p) const;
	void paintButtons(QPainter &p) const;
	void paintPlayback(QPainter &p) const;
	void paintPlaybackTexts(QPainter &p) const;
	void paintRadialLoading(QPainter &p) const;
	void paintRadialLoadingContent(QPainter &p, const QRect &inner) const;
	[[nodiscard]] QRect countRadialRect() const;

	void seekUpdate(QPoint position);
	void seekProgress(float64 value);
	void seekFinish(float64 value);

	const not_null<Delegate*> _delegate;
	not_null<DocumentData*> _data;
	FullMsgId _contextId;
	Streaming::Instance _instance;
	PipPanel _panel;
	QSize _size;
	std::unique_ptr<PlaybackProgress> _playbackProgress;
	std::shared_ptr<Data::DocumentMedia> _dataMedia;

	bool _showPause = false;
	bool _startPaused = false;
	bool _pausedBySeek = false;
	QString _timeAlready, _timeLeft;
	int _timeLeftWidth = 0;
	int _rotation = 0;
	crl::time _seekPositionMs = -1;
	crl::time _lastDurationMs = 0;
	OverState _over = OverState::None;
	std::optional<OverState> _pressed;
	std::optional<OverState> _lastHandledPress;
	Button _close;
	Button _enlarge;
	Button _playback;
	Button _play;
	Ui::Animations::Simple _controlsShown;
	Ui::RoundRect _roundRect;

	FnMut<void()> _closeAndContinue;
	FnMut<void()> _destroy;

#ifdef USE_OPENGL_OVERLAY_WIDGET
	mutable QImage _frameForDirectPaint;
	mutable QImage _radialCache;
#endif // USE_OPENGL_OVERLAY_WIDGET

	mutable QImage _preparedCoverStorage;
	mutable FrameRequest _preparedCoverRequest;
	mutable ThumbState _preparedCoverState;

};

} // namespace View
} // namespace Media
