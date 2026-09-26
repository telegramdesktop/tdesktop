/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Ui::GL {
class Renderer;
} // namespace Ui::GL

namespace Ui::Premium {

class Object3dCover : public RpWidget {
public:
	struct Descriptor {
		crl::time spinDuration = 0;
		crl::time respinDelay = 0;
		float64 period = 0.;
		bool spinEaseOut = false;
		int sampleCount = 4;
	};

	[[nodiscard]] static bool Supported();

	void setShownProgress(float64 progress);
	void setPaused(bool paused);
	void setNight(bool night);

	[[nodiscard]] rpl::producer<float64> flungStrength() const;

protected:
	Object3dCover(QWidget *parent, Descriptor descriptor);
	~Object3dCover();

	[[nodiscard]] virtual std::unique_ptr<Ui::GL::Renderer> createRenderer()
		= 0;
	virtual void applyState(
		float yaw,
		float pitch,
		float time,
		float alpha,
		bool night) = 0;

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
	};
	enum class Easing {
		Linear,
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
	void startSpin();
	void startBackSpring();
	void startTapTilt(QPoint position);

	const Descriptor _descriptor;
	std::unique_ptr<RpWidgetWrap> _surface;
	QImage _frozen;
	Ui::Animations::Basic _animation;
	crl::time _lastFrame = 0;

	float64 _yaw = 0.;
	float64 _pitch = 0.;
	float64 _time = 0.;
	float64 _opacity = 1.;
	bool _night = false;

	std::optional<Gesture> _gesture;
	crl::time _idleAt = 0;

	bool _dragging = false;
	bool _moved = false;
	QPoint _pressPos;
	QPoint _lastDragPos;

	rpl::event_stream<float64> _flung;

	bool _paused = false;
	bool _entered = false;
	crl::time _pausedAt = 0;

};

} // namespace Ui::Premium
