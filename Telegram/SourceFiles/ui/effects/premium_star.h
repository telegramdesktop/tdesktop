/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Ui::Premium {

class StarRenderer;

class Star final : public RpWidget {
public:
	explicit Star(QWidget *parent);
	~Star();

	[[nodiscard]] static bool Supported();

	void setColors(QColor gradient1, QColor gradient2);
	void setGolden(bool golden);
	void setShownProgress(float64 progress);
	void setPaused(bool paused);
	void startEnter();

	[[nodiscard]] rpl::producer<float64> flungStrength() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void showEvent(QShowEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	enum class Channel {
		Yaw,
		Pitch,
		Bob,
	};
	enum class Easing {
		Linear,
		Default,
		EaseOut,
		EaseOutQuint,
		Overshoot,
	};
	struct Track {
		Channel channel = Channel::Yaw;
		std::vector<float64> values;
		crl::time delay = 0;
		crl::time duration = 0;
		Easing easing = Easing::Linear;
	};
	struct Gesture {
		std::vector<Track> tracks;
		crl::time start = 0;
		crl::time total = 0;
	};

	void ensureSurface();
	[[nodiscard]] QWidget *surfaceWidget() const;
	void freeze();
	void unfreeze();
	void startAnimation();
	void stopAnimation();
	void frame();
	void advance(float64 dt);
	void tick(crl::time now);
	void pushState();

	void applyChannel(Channel channel, float64 value);
	void play(Gesture gesture);
	void scheduleIdle(crl::time delay);
	void cancelIdle();
	void startIdleGesture();
	void startBackSpring();
	void pullGesture();
	void slowFlipGesture();
	void flipGesture();
	void sleepGesture();

	StarRenderer *_renderer = nullptr;
	std::unique_ptr<RpWidgetWrap> _surface;
	QImage _frozen;
	Ui::Animations::Basic _animation;
	crl::time _lastFrame = 0;

	QColor _gradient1;
	QColor _gradient2;
	bool _golden = false;

	float64 _yaw = 0.;
	float64 _pitch = 0.;
	float64 _bob = 0.;
	float64 _shimmer = 0.;
	float64 _fadeIn = 0.;
	float64 _opacity = 1.;

	std::optional<Gesture> _gesture;
	crl::time _idleAt = 0;
	std::vector<int> _idleBag;

	bool _dragging = false;
	QPoint _lastDragPos;

	rpl::event_stream<float64> _flung;

	bool _paused = false;
	bool _entered = false;
	crl::time _pausedAt = 0;

};

} // namespace Ui::Premium
