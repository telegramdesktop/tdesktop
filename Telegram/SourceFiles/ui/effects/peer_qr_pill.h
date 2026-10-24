/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "ui/effects/animated_string.h"
#include "ui/effects/animations.h"
#include "ui/effects/drifting_particles.h"
#include "ui/effects/peer_qr_surface.h"
#include "ui/text/text.h"
#include "ui/rp_widget.h"

namespace Ui {

class AbstractButton;

class PeerQrPill final : public RpWidget {
public:
	PeerQrPill(not_null<QWidget*> parent, PeerQrColors colors);
	~PeerQrPill();

	void setColors(PeerQrColors colors);
	void setPillPosition(int centerX, int top);
	void showFee(int amount);
	void showTemporary(crl::time arrival, crl::time deadline);
	void setDeadline(crl::time deadline);
	void showPermanent();
	void hidePill();

	[[nodiscard]] QRect pillGeometry() const;
	[[nodiscard]] QPainterPath pillPath() const;
	[[nodiscard]] rpl::producer<> clicks() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	enum class Mode { Hidden, Fee, Temporary, Permanent };
	enum class Transition { None, Sweep, Refill, Retext, Shatter };
	struct Particle;
	struct Frame;

	[[nodiscard]] Frame computeFrame(crl::time now) const;
	[[nodiscard]] crl::time frameTime() const;
	[[nodiscard]] float64 targetWidth() const;
	[[nodiscard]] float64 edgeAt(const Frame &frame, float64 y) const;
	[[nodiscard]] float64 targetFill(crl::time now) const;
	[[nodiscard]] QString stateText() const;
	[[nodiscard]] Transition freeTransition() const;
	void startTransition(Transition transition, crl::time now);
	void stateChanged();
	void checkAnimation();
	[[nodiscard]] bool animationNeeded(crl::time now) const;
	[[nodiscard]] bool animationCallback(crl::time now);
	void tick();
	void refreshCountdown(crl::time now);
	void refreshButton(const Frame &frame);
	void updatePill();
	void updateDirty(const Frame &frame);

	void emitParticles(const Frame &frame, float64 seconds, crl::time now);
	void spawnFill(const Frame &frame, float64 x, crl::time now);
	void spawnEmber(const Frame &frame, crl::time now);
	void spawnShatter(const Frame &frame, crl::time now);
	void stepParticles(const Frame &frame, float64 seconds, crl::time now);

	void paintPill(QPainter &p, const Frame &frame) const;
	void paintParticles(QPainter &p, crl::time now) const;

	PeerQrColors _colors;
	QImage _crossedInk;
	QImage _crossedForeground;
	base::unique_qptr<AbstractButton> _button;
	AnimatedString _freeText;
	Text::String _feeText;
	std::vector<Particle> _particles;
	ParticlesRandom _random;
	Animations::Basic _animation;
	base::Timer _timer;
	QRect _dirty;
	crl::time _transitionStartedAt = 0;
	crl::time _arrival = 0;
	crl::time _deadline = 0;
	crl::time _lastStep = 0;
	float64 _widthFrom = 0.;
	float64 _fillFrom = 0.;
	float64 _fillCarry = 0.;
	float64 _emberCarry = 0.;
	uint32 _edgeSeed = 0;
	int _centerX = 0;
	int _seconds = -1;
	Mode _mode = Mode::Hidden;
	Transition _transition = Transition::None;

};

} // namespace Ui
