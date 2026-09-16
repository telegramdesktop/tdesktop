/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

namespace Ui {
class MaskedInputField;
} // namespace Ui

namespace Editor {

struct TrimTimelineDescriptor {
	crl::time duration = 0;

	crl::time maxDuration = 0;
	crl::time minDuration = 0;

	// A zero |till| means the whole allowed window is selected.
	crl::time from = 0;
	crl::time till = 0;
	crl::time cover = 0;

	bool trimOnly = false;
};

class TrimTimeline
	: public Ui::RpWidget
	, public base::has_weak_ptr {
public:
	TrimTimeline(
		not_null<Ui::RpWidget*> parent,
		TrimTimelineDescriptor descriptor);

	[[nodiscard]] crl::time from() const {
		return _from;
	}
	[[nodiscard]] crl::time till() const {
		return _till;
	}
	[[nodiscard]] crl::time cover() const {
		return _cover;
	}
	[[nodiscard]] crl::time playbackPosition() const {
		return _playback;
	}
	[[nodiscard]] float64 zoom() const {
		return _zoom;
	}
	[[nodiscard]] crl::time visibleFrom() const;
	[[nodiscard]] crl::time visibleTill() const;

	[[nodiscard]] rpl::producer<crl::time> trimChanges() const {
		return _trimChanges.events();
	}
	[[nodiscard]] rpl::producer<crl::time> coverChanges() const {
		return _coverChanges.events();
	}
	[[nodiscard]] rpl::producer<bool> draggingChanges() const {
		return _draggingChanges.events();
	}

	void setTrim(crl::time from, crl::time till);
	void setPlaybackPosition(crl::time position);
	void commitPendingEdit();

	void setSizeLabel(const QString &text);

	[[nodiscard]] bool draggingHead() const;

	[[nodiscard]] int resizeGetHeight(int newWidth) override;

protected:
	[[nodiscard]] crl::time duration() const {
		return _duration;
	}
	[[nodiscard]] bool trimOnly() const {
		return _trimOnly;
	}
	[[nodiscard]] QRect stripRect() const;
	[[nodiscard]] crl::time timeAt(int x) const;
	[[nodiscard]] int xAt(crl::time time) const;
	[[nodiscard]] float64 visibleSpan() const;

	virtual void paintStrip(QPainter &p, const QRect &strip) = 0;
	virtual void paintOverlay(QPainter &p);
	virtual void headGrabChanged(bool grabbed);
	virtual void visibleRangeChanged();

	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	bool eventHook(QEvent *e) override;

private:
	enum class Grab {
		None,
		Left,
		Right,
		Head,

		Window,
		Scroll,
		Hint,
		Label,
	};
	struct DurationLabel {
		QString text;
		QRect rect;
		bool sizeShown = false;
	};

	[[nodiscard]] QRect labelRect() const;
	[[nodiscard]] DurationLabel durationLabel() const;
	[[nodiscard]] QRect durationHitRect() const;
	[[nodiscard]] QRect durationFieldRect() const;
	[[nodiscard]] QString durationEditText() const;
	void updateDurationFieldGeometry();
	void editDuration();
	void finishDurationEdit(bool apply, bool restoreFocus);
	void applyDurationText(const QString &text);
	[[nodiscard]] crl::time minSelection() const;
	void moveWindowTo(crl::time center);
	[[nodiscard]] Grab grabAt(
		QPoint position,
		Qt::KeyboardModifiers modifiers) const;

	[[nodiscard]] float64 maxZoom() const;
	bool setVisibleRange(float64 zoom, float64 from);
	void zoomBy(float64 factor, int anchorX);
	bool scrollBy(float64 pixels);
	void updateEdgeScroll(QPoint position);
	bool edgeScrollStep(crl::time now);
	[[nodiscard]] bool grabClamped(bool forward) const;

	[[nodiscard]] bool selectionHiddenLeft() const;
	[[nodiscard]] bool selectionHiddenRight() const;
	[[nodiscard]] QRect hintRect(bool left) const;
	void updateHints();
	void scrollToSelection();

	[[nodiscard]] bool grabMovesSelection() const;
	void applyGrab(QPoint position);
	void releaseGrab();
	void setCover(crl::time cover, bool notify);
	void updateCursor(Grab grab);
	void paintSelection(QPainter &p, const QRect &strip);
	void paintOverview(QPainter &p, const QRect &strip);
	void paintHints(QPainter &p);
	void paintHead(QPainter &p, const QRect &strip);
	void paintDuration(QPainter &p);

	const crl::time _duration = 0;
	const crl::time _maxDuration = 0;
	const crl::time _minDuration = 0;
	const bool _trimOnly = false;

	crl::time _from = 0;
	crl::time _till = 0;
	crl::time _cover = 0;
	// Negative means nothing played yet; zero is a real position.
	crl::time _playback = -1;

	float64 _zoom = 1.;
	float64 _visibleFrom = 0.;

	QString _sizeLabel;
	int _labelWidth = 0;
	int _durationFieldWidth = 0;
	base::unique_qptr<Ui::MaskedInputField> _durationField;
	QPointer<QWidget> _durationFocusReturn;

	Grab _grab = Grab::None;
	Qt::MouseButton _grabButton = Qt::NoButton;
	int _grabShift = 0;
	QPoint _dragPosition;
	int _edgeOvershoot = 0;
	crl::time _edgeScrollLast = 0;
	Ui::Animations::Basic _edgeScrollAnimation;
	Ui::ScrollDirectionLock _wheelDirectionLock;

	bool _hintLeftShown = false;
	bool _hintRightShown = false;
	bool _hintGrabLeft = false;
	Ui::Animations::Simple _hintLeft;
	Ui::Animations::Simple _hintRight;
	Ui::Animations::Simple _scrollAnimation;

	rpl::event_stream<crl::time> _trimChanges;
	rpl::event_stream<crl::time> _coverChanges;
	rpl::event_stream<bool> _draggingChanges;

};

} // namespace Editor
