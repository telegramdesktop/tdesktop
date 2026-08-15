/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Editor {

struct VideoTimelineDescriptor {
	QString path;
	QSize dimensions;
	crl::time duration = 0;

	crl::time maxDuration = 0;
	crl::time minDuration = 0;

	// A zero |till| means the whole allowed window is selected.
	crl::time from = 0;
	crl::time till = 0;
	crl::time cover = 0;
};

class VideoTimeline final
	: public Ui::RpWidget
	, public base::has_weak_ptr {
public:
	VideoTimeline(
		not_null<Ui::RpWidget*> parent,
		VideoTimelineDescriptor descriptor);
	~VideoTimeline();

	[[nodiscard]] crl::time from() const {
		return _from;
	}
	[[nodiscard]] crl::time till() const {
		return _till;
	}
	[[nodiscard]] crl::time cover() const {
		return _cover;
	}

	[[nodiscard]] rpl::producer<crl::time> trimChanges() const {
		return _trimChanges.events();
	}
	[[nodiscard]] rpl::producer<crl::time> coverChanges() const {
		return _coverChanges.events();
	}
	[[nodiscard]] rpl::producer<bool> draggingChanges() const {
		return _draggingChanges.events();
	}

	void setPlaybackPosition(crl::time position);

	[[nodiscard]] QPoint coverDot() const;

	[[nodiscard]] bool draggingHead() const;

	[[nodiscard]] int resizeGetHeight(int newWidth) override;

private:
	enum class Grab {
		None,
		Left,
		Right,
		Head,

		Window,
	};

	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	[[nodiscard]] QRect stripRect() const;
	[[nodiscard]] QRect labelRect() const;
	[[nodiscard]] crl::time minSelection() const;
	void moveWindowTo(crl::time center);
	[[nodiscard]] crl::time timeAt(int x) const;
	[[nodiscard]] int xAt(crl::time time) const;
	[[nodiscard]] Grab grabAt(QPoint position) const;

	void applyGrab(QPoint position);
	void setCover(crl::time cover, bool notify);
	void updateCursor(Grab grab);
	void reloadFrames();
	void paintFrames(QPainter &p, const QRect &strip);
	void paintSelection(QPainter &p, const QRect &strip);
	void paintHead(QPainter &p, const QRect &strip);
	void paintCoverDot(QPainter &p);
	void paintDuration(QPainter &p, const QRect &strip);

	const VideoTimelineDescriptor _descriptor;
	const crl::time _duration = 0;
	const crl::time _maxDuration = 0;
	const crl::time _minDuration = 0;

	crl::time _from = 0;
	crl::time _till = 0;
	crl::time _cover = 0;
	// Negative means nothing played yet; zero is a real position.
	crl::time _playback = -1;

	std::vector<QImage> _frames;
	int _frameWidth = 0;
	std::shared_ptr<std::atomic<bool>> _framesCancel;

	Grab _grab = Grab::None;
	int _grabShift = 0;
	Ui::Animations::Simple _dotActive;

	rpl::event_stream<crl::time> _trimChanges;
	rpl::event_stream<crl::time> _coverChanges;
	rpl::event_stream<bool> _draggingChanges;

};

} // namespace Editor
