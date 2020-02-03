/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_instance.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

#include <QtCore/QPointer>

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
	[[nodiscard]] bool dragging() const;

	[[nodiscard]] rpl::producer<> saveGeometryRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void setPositionDefault();
	void setPositionOnScreen(Position position, QRect available);

	QScreen *myScreen() const;
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
		std::shared_ptr<Streaming::Document> document,
		FnMut<void()> closeAndContinue,
		FnMut<void()> destroy);

private:
	enum class OverState {
		None,
		Close,
		Enlarge,
		Playback,
		Other,
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

	[[nodiscard]] QImage videoFrame(const FrameRequest &request) const;
	[[nodiscard]] QImage videoFrameForDirectPaint(
		const FrameRequest &request) const;
	[[nodiscard]] OverState computeState(QPoint position) const;
	void setOverState(OverState state);

	void handleMouseMove(QPoint position);
	void handleMousePress(Qt::MouseButton button);
	void handleMouseRelease(Qt::MouseButton button);
	void handleDoubleClick(Qt::MouseButton button);
	void handleLeave();
	void handleClose();

	void paintControls(QPainter &p);

	const not_null<Delegate*> _delegate;
	Streaming::Instance _instance;
	PipPanel _panel;
	QSize _size;

	bool _showPause = false;
	OverState _over = OverState::None;
	std::optional<OverState> _pressed;
	std::optional<OverState> _lastHandledPress;
	Button _close;
	Button _enlarge;
	Button _playback;
	Button _play;
	Ui::Animations::Simple _controlsShown;

	FnMut<void()> _closeAndContinue;
	FnMut<void()> _destroy;

#ifdef USE_OPENGL_OVERLAY_WIDGET
	mutable QImage _frameForDirectPaint;
#endif // USE_OPENGL_OVERLAY_WIDGET

	mutable QImage _preparedCoverStorage;
	mutable FrameRequest _preparedCoverRequest;

};

} // namespace View
} // namespace Media
