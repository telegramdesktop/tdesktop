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

namespace Media {
namespace View {

#if defined Q_OS_MAC && !defined OS_MAC_OLD
#define USE_OPENGL_OVERLAY_WIDGET
#endif // Q_OS_MAC && !OS_MAC_OLD

#ifdef USE_OPENGL_OVERLAY_WIDGET
using PipParent = Ui::RpWidgetWrap<QOpenGLWidget>;
#else // USE_OPENGL_OVERLAY_WIDGET
using PipParent = Ui::RpWidget;
#endif // USE_OPENGL_OVERLAY_WIDGET

class Pip final : public PipParent {
public:
	Pip(
		std::shared_ptr<Streaming::Document> document,
		FnMut<void()> closeAndContinue);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupSize();
	void setupStreaming();
	void playbackPauseResume();
	void finishDrag(QPoint point);
	void updatePosition(QPoint point);
	void waitingAnimationCallback();
	void handleStreamingUpdate(Streaming::Update &&update);
	void handleStreamingError(Streaming::Error &&error);
	void updatePositionAnimated();
	void moveAnimated(QPoint to);

	[[nodiscard]] QImage videoFrame() const;
	[[nodiscard]] QImage videoFrameForDirectPaint() const;

	Streaming::Instance _instance;
	QSize _size;
	std::optional<QPoint> _pressPoint;
	std::optional<QPoint> _dragStartPosition;
	FnMut<void()> _closeAndContinue;

	QPoint _positionAnimationFrom;
	QPoint _positionAnimationTo;
	Ui::Animations::Simple _positionAnimation;

};

} // namespace View
} // namespace Media
