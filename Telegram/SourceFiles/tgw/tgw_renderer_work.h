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

struct RendererLayout {
	QImage picture;
	QSize area;
	double height = 0.;
	uint64 serial = 0;
	uint64 visibility = 0;
	uint64 epoch = 0;
	bool bottom = false;
	bool client = false;
};

struct RendererState {
	bool hasContent = false;
	bool loaded = false;
};

struct RendererStop {
	QString reason;
	bool fatal = false;
};

struct RendererCursor {
	uint64 serial = 0;
	bool hand = false;
};

struct RendererSelection {
	QString text;
	uint64 serial = 0;
	bool has = false;
};

struct RendererMenu {
	uint64 serial = 0;
	bool taken = false;
};

struct RendererClaim {
	twidget::WidgetGestureClaim claim;
	uint64 serial = 0;
};

struct RendererAnswer {
	std::optional<RendererLayout> layout;
	RendererState state;
	std::optional<RendererStop> stop;
	std::optional<RendererCursor> cursor;
	std::optional<RendererSelection> selection;
	std::optional<RendererMenu> menu;
	std::optional<RendererClaim> claim;
};

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
		not_null<RendererCore*> chat,
		double width,
		double dpr,
		Host host);
	~RendererWork();

	void hostLoaded();
	void hostMessage(const twidget::Json &message);
	void hostTheme(bool dark);
	void freeze(QString reason, bool fatal);

	void detach();
	void setOnScreen(bool onScreen, uint64 visibility);

	void pointerEnter(uint64 serial);
	void pointerMove(QPointF point, int modifiers, uint64 serial);
	void pointerLeave(uint64 serial);
	void pointerPress(
		QPointF point,
		int modifiers,
		uint64 serial,
		uint64 selection);
	void pointerRelease(
		QPointF point,
		int modifiers,
		bool cancel,
		uint64 serial);
	void menuPress(QPointF point, int modifiers, uint64 serial);
	void menuDecided(uint64 serial, bool deliver);
	void menuRelease(int modifiers, bool cancel);
	void fingerPress(QPointF point, int modifiers, uint64 serial);
	void fingerMove(QPointF point, int modifiers);
	void fingerHold();
	void fingerRelease(QPointF point, int modifiers);
	void fingerCancel();
	void resetInput();
	void clearSelection(uint64 serial);

	void setSize(const RendererSize &size, uint64 serial);
	void setCut(const RendererCut &cut);
	void demand();
	void drawn();
	void dropFrame(uint64 epoch);
	void refreshFrame();

private:
	struct Pointer {
		QPointF position;
		QPointF origin;
		std::vector<twidget::Json> held;
		int64 hit = 0;
		int64 hover = 0;
		int64 press = 0;
		uint64 menu = 0;
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
		std::vector<twidget::Json> held;
		bool pressed = false;
	};
	struct Held {
		crl::time since = 0;
		int builds = 0;
	};
	struct Reports {
		std::optional<RendererMenu> menu;
		std::optional<RendererClaim> claim;
		QString text;
		uint64 pointer = 0;
		uint64 selection = 0;
		uint64 cursorSent = 0;
		uint64 textSent = 0;
		int cursorBuilds = -1;
		int textBuilds = -1;
		bool hand = false;
		bool has = false;
		bool touched = false;
	};

	[[nodiscard]] bool acceptsInput() const;
	[[nodiscard]] int64 elementAt(QPointF point);
	[[nodiscard]] CursorStyles &cursorStyles();
	void flushBuilt(int builds);
	void holdPointer(QPointF point, int64 hit, bool secondary);
	void releasePointer(QPointF point, int modifiers, bool cancel);
	[[nodiscard]] bool selected() const;
	[[nodiscard]] bool handAt(QPointF point);
	void reportInput(RendererAnswer &answer);
	[[nodiscard]] std::optional<RendererCursor> reportCursor();
	[[nodiscard]] std::optional<RendererSelection> reportSelection();
	void trackHover();
	[[nodiscard]] std::vector<twidget::Json> leaveHover();
	void inputDone(const std::vector<twidget::Json> &out, bool wasDirty);
	void cancelInput();
	void releaseInput();
	void finishDetach();
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

	void schedule();
	void work();
	void applySizeRequest();
	[[nodiscard]] bool rasterWanted() const;
	[[nodiscard]] QSize layoutArea() const;
	[[nodiscard]] std::optional<RendererLayout> raster();
	[[nodiscard]] std::optional<RendererLayout> completed(QSize cardArea);
	[[nodiscard]] std::optional<QString> repaintFrame(
		const std::vector<twlite::Command> &list,
		QRect area,
		Images::CornersMaskRef corners,
		QSize card);
	void releaseCanvas();
	void dropCanvas();
	void deliver(RendererAnswer &&answer);

	void finish(const std::vector<twidget::Json> &out, Changes changes);
	[[nodiscard]] Changes forwardAll(const std::vector<twidget::Json> &out);
	[[nodiscard]] Changes applySize(const twidget::Json &message);
	void bindHeightAnchor();
	void forward(const twidget::Json &message);
	void post(Changes changes);

	const not_null<RendererCore*> _chat;
	const Host _host;
	std::unique_ptr<QtBackend> _backend;
	std::unique_ptr<twlite::Renderer> _renderer;
	Ui::Animations::Basic _clock;
	base::Timer _wake;
	base::Timer _heldTimer;
	std::optional<Held> _held;
	crl::time _recentReplyUntil = 0;
	RendererSize _size;
	std::optional<RendererCut> _cut;
	QImage _frame;
	QSize _frameSize;
	QSize _frameArea;
	RendererCut _frameCut;
	std::optional<double> _height;
	std::string _pendingSize;
	QString _stopReason;
	std::unique_ptr<CursorStyles> _cursor;
	Pointer _pointer;
	Finger _finger;
	Reports _reports;
	Changes _postedChanges;
	uint64 _sizeSerial = 0;
	uint64 _frameSerial = 0;
	uint64 _epoch = 0;
	uint64 _visibility = 0;
	bool _ready = false;
	bool _detached = false;
	bool _detaching = false;
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
	bool _scheduled = false;
	bool _sizePending = false;
	bool _demand = false;
	bool _undrawn = false;
	bool _client = false;
	bool _reported = false;

};

} // namespace Tgw
