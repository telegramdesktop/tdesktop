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

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setPositionDefault();
	void setPositionOnScreen(Position position, QRect available);

	QScreen *myScreen() const;
	void finishDrag(QPoint point);
	void updatePosition(QPoint point);
	void updatePositionAnimated();
	void updateOverState(QPoint point);
	void moveAnimated(QPoint to);

	QPointer<QWidget> _parent;
	Fn<void(QPainter&, FrameRequest)> _paint;
	RectParts _attached = RectParts();
	QSize _ratio;

	RectPart _overState = RectPart();
	std::optional<RectPart> _pressState;
	QPoint _pressPoint;
	std::optional<QRect> _dragStartGeometry;

	QPoint _positionAnimationFrom;
	QPoint _positionAnimationTo;
	Ui::Animations::Simple _positionAnimation;

};

class Pip final {
public:
	Pip(
		QWidget *parent,
		std::shared_ptr<Streaming::Document> document,
		FnMut<void()> closeAndContinue,
		FnMut<void()> destroy);

private:
	using FrameRequest = Streaming::FrameRequest;

	void setupPanel();
	void setupStreaming();
	void paint(QPainter &p, FrameRequest request);
	void playbackPauseResume();
	void waitingAnimationCallback();
	void handleStreamingUpdate(Streaming::Update &&update);
	void handleStreamingError(Streaming::Error &&error);

	[[nodiscard]] QImage videoFrame(const FrameRequest &request) const;
	[[nodiscard]] QImage videoFrameForDirectPaint(
		const FrameRequest &request) const;

	Streaming::Instance _instance;
	PipPanel _panel;
	QSize _size;

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
