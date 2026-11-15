/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "tgw/tgw_renderer.h"
#include "ui/effects/animations.h"

namespace twidget {
class Json;
} // namespace twidget

namespace twlite {
class Renderer;
struct Command;
} // namespace twlite

namespace Tgw {

class CursorStyles;
class QtBackend;

class RendererWork final : public base::has_weak_ptr {
public:
	using Change = RendererCore::Change;
	using Changes = RendererCore::Changes;

	struct Host {
		Fn<void(const std::string &json)> send;
		Fn<void(const std::string &kind, const std::string &detail)> diag;
		Fn<std::optional<bool>()> bottom;
	};

	RendererWork(
		double width,
		double dpr,
		Host host,
		Fn<void(Changes changes)> changed);
	~RendererWork();

	void hostLoaded();
	void hostMessage(const twidget::Json &message);
	void hostTheme(bool dark);

	void detach();
	[[nodiscard]] bool attached() const;
	void setOnScreen(bool onScreen);
	[[nodiscard]] bool acceptsInput() const;

	void pointerEnter();
	void pointerMove(QPointF point, int modifiers);
	void pointerLeave();
	void pointerPress(QPointF point, int modifiers);
	void pointerRelease(QPointF point, int modifiers);
	[[nodiscard]] bool menuPress(QPointF point, int modifiers);
	void menuRelease(int modifiers);
	[[nodiscard]] std::optional<twidget::WidgetGestureClaim> fingerPress(
		QPointF point,
		int modifiers);
	void fingerMove(QPointF point, int modifiers);
	void fingerHold();
	void fingerRelease(QPointF point, int modifiers);
	void fingerCancel();
	void resetInput();
	[[nodiscard]] bool hovered() const;
	[[nodiscard]] bool pressed() const;
	[[nodiscard]] bool fingerPressed() const;
	[[nodiscard]] bool handCursorAt(QPointF point);
	[[nodiscard]] bool hasSelection() const;
	[[nodiscard]] QString selectedText() const;
	void clearSelection();

	void setWidth(double width);
	void setMaxHeight(double height);
	[[nodiscard]] double width() const;
	[[nodiscard]] std::optional<double> height() const;
	[[nodiscard]] bool bottomAnchored() const;
	[[nodiscard]] bool hasContent() const;
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] bool stopped() const;
	[[nodiscard]] const QString &stopReason() const;
	[[nodiscard]] bool stopFatal() const;
	[[nodiscard]] not_null<twlite::Renderer*> renderer() const;
	[[nodiscard]] const QImage &frame(
		Images::CornersMaskRef corners,
		QSize cardArea,
		bool autoHeight);
	[[nodiscard]] QSize frameArea() const;
	void dropFrame();
	void refreshFrame();
	void takeFrameFrom(RendererWork &other);

private:
	struct Pointer {
		QPointF position;
		QPointF origin;
		int64 hit = 0;
		int64 hover = 0;
		int64 press = 0;
		int parsed = 0;
		bool over = false;
		bool hovering = false;
		bool pressed = false;
		bool moved = false;
		bool active = false;
		bool secondary = false;
	};
	struct Finger {
		QPointF position;
		bool pressed = false;
	};
	struct Held {
		crl::time since = 0;
		int builds = 0;
	};

	[[nodiscard]] int64 elementAt(QPointF point);
	[[nodiscard]] CursorStyles &cursorStyles();
	void holdPointer(QPointF point, int64 hit, bool secondary);
	void trackHover();
	[[nodiscard]] std::vector<twidget::Json> leaveHover();
	void inputDone(const std::vector<twidget::Json> &out, bool wasDirty);
	void releaseInput();
	void stop(QString reason, bool fatal);
	[[nodiscard]] std::vector<twidget::Json> takeSettled();
	void flushQueued();

	[[nodiscard]] bool canTick() const;
	[[nodiscard]] bool clockWanted() const;
	void refreshClock();
	void scheduleWake();
	void dropHeld();
	void releaseHeld();
	[[nodiscard]] bool step(crl::time now);
	void wake();
	void tick();

	[[nodiscard]] std::optional<QString> repaintFrame(
		const std::vector<twlite::Command> &list,
		QRect area,
		Images::CornersMaskRef corners,
		QSize card);

	void finish(const std::vector<twidget::Json> &out, Changes changes);
	[[nodiscard]] Changes forwardAll(const std::vector<twidget::Json> &out);
	[[nodiscard]] Changes applySize(const twidget::Json &message);
	void bindHeightAnchor();
	void forward(const twidget::Json &message);
	void notifyPosted(Changes changes);

	const Host _host;
	const Fn<void(Changes changes)> _changed;
	std::unique_ptr<QtBackend> _backend;
	std::unique_ptr<twlite::Renderer> _renderer;
	Ui::Animations::Basic _clock;
	base::Timer _wake;
	base::Timer _heldTimer;
	std::optional<Held> _held;
	crl::time _recentReplyUntil = 0;
	QImage _frame;
	QSize _frameSize;
	QSize _frameArea;
	Images::CornersMaskRef _frameCorners;
	std::optional<double> _height;
	std::string _pendingSize;
	QString _stopReason;
	std::unique_ptr<CursorStyles> _cursor;
	Pointer _pointer;
	Finger _finger;
	Changes _postedChanges;
	bool _ready = false;
	bool _detached = false;
	bool _hasContent = false;
	bool _acked = false;
	bool _loaded = false;
	bool _heightBottom = false;
	bool _stopped = false;
	bool _stopFatal = false;
	bool _onScreen = false;
	bool _frameRequested = false;
	bool _frameRepaintable = false;
	bool _idleRequest = false;
	bool _stepBusy = false;

};

} // namespace Tgw
