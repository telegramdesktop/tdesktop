/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "tgw/tgw_renderer_work.h"

#include <deque>
#include <mutex>

namespace twlite::remote {
class PlatformEnd;
struct ClaimAnswer;
struct Pointer;
} // namespace twlite::remote

namespace Tgw {

class RendererPlatform;

struct PressStep {
	enum class Kind : uchar {
		Sent,
		Down,
		Up,
		Click,
	};

	Kind kind = Kind::Sent;
	uint64 serial = 0;
};

struct TouchInput {
	QPointF point;
	int action = 0;
	int modifiers = 0;
	int pointer = 0;
	int button = 0;
};

void DeliverTouch(
	twlite::remote::PlatformEnd &end,
	const TouchInput &input);
[[nodiscard]] bool MenuTaken(
	const twidget::WidgetGestureClaim &claim,
	QPointF point);

class ReportPost final {
public:
	struct Latest {
		std::optional<RendererSelection> selection;
		std::optional<RendererCursor> cursor;
	};

	[[nodiscard]] bool offer(RendererSelection selection);
	[[nodiscard]] bool offer(RendererCursor cursor);
	[[nodiscard]] Latest take();
	void close();

private:
	std::mutex _mutex;
	Latest _latest;
	bool _posted = false;
	bool _closed = false;

};

class HostedInput final {
public:
	struct Delegate {
		Fn<bool()> accepts;
		Fn<void()> input;
		Fn<void()> schedule;
		Fn<void()> reports;
		Fn<void(PressStep step, uint64 press)> observed;
		Fn<void(QString limit)> overflow;
	};

	HostedInput(
		not_null<RendererPlatform*> platform,
		std::shared_ptr<ReportPost> post,
		Delegate delegate);

	void enter(uint64 serial);
	void move(QPointF point, int modifiers, uint64 serial);
	void leave(uint64 serial);
	void press(QPointF point, int modifiers, uint64 serial, uint64 selection);
	void release(QPointF point, int modifiers, bool cancel, uint64 serial);
	void menuPress(QPointF point, int modifiers, uint64 serial);
	void menuDecided(uint64 serial, bool deliver);
	void menuRelease(int modifiers, bool cancel);
	void fingerPress(QPointF point, int modifiers, uint64 serial);
	void fingerMove(QPointF point, int modifiers);
	[[nodiscard]] bool fingerHold(bool asking);
	void fingerRelease(QPointF point, int modifiers);
	void fingerCancel();
	void clearSelection(uint64 serial);
	void cancel(bool stopped);
	void reset();
	void close();
	void detach();
	void deactivate();

	void beforeTick();
	[[nodiscard]] bool tickWanted() const;
	void report(RendererAnswer &answer, bool live);

	void pointer(const twlite::remote::Pointer &pointer);
	void clicked(uint64 press);
	void cursor(std::string_view cursor);
	void selection(QString text);
	void claim(const twlite::remote::ClaimAnswer &answer);

private:
	enum class Reply : uchar {
		Cursor,
		Move,
		Menu,
		Finger,
	};
	enum class Press : uchar {
		None,
		Down,
		Up,
	};
	struct Mouse {
		QPointF position;
		uint64 menu = 0;
		uint64 down = 0;
		bool over = false;
		bool hovering = false;
		bool pressed = false;
		bool secondary = false;
	};
	struct Finger {
		QPointF position;
		uint64 down = 0;
		bool pressed = false;
	};
	struct Ask {
		uint64 serial = 0;
		uint64 pointer = 0;
		uint64 selection = 0;
		uint64 down = 0;
		Reply reply = Reply::Cursor;
		Press press = Press::None;
	};
	struct Move {
		QPointF point;
		uint64 pointer = 0;
		uint64 selection = 0;
		int modifiers = 0;
		int device = 0;
		bool hover = false;
	};
	struct MenuQuery {
		QPointF point;
		uint64 serial = 0;
		uint64 seq = 0;
		int modifiers = 0;
	};
	struct MenuDown {
		QPointF point;
		uint64 serial = 0;
		uint64 press = 0;
	};
	struct Reports {
		std::optional<RendererMenu> menu;
		std::optional<RendererClaim> claim;
		QString text;
		uint64 cursorSent = 0;
		uint64 textSent = 0;
		bool hand = false;
		bool has = false;
	};

	void releaseMouse(QPointF point, int modifiers, bool cancel);
	void holdMouse(QPointF point, bool secondary);
	void leaveHover();
	void pressMenu(uint64 serial, QPointF point, int modifiers);
	void cancelMenuDown();
	void endMenuQuery();
	void answerMenu(
		uint64 serial,
		const std::optional<twidget::WidgetGestureClaim> &claim);
	void queueMove(QPointF point, int modifiers, int device, bool hover);
	void flushMove();
	[[nodiscard]] bool solicit();
	bool send(
		const TouchInput &input,
		Reply reply = Reply::Cursor,
		uint64 serial = 0,
		Press press = Press::None,
		uint64 down = 0);
	[[nodiscard]] bool sendDown(
		const TouchInput &input,
		Reply reply,
		uint64 serial);
	void expect(
		Reply reply,
		uint64 serial,
		uint64 pointer,
		uint64 selection,
		Press press = Press::None,
		uint64 down = 0);
	void endPress(bool finger);
	void endFinger();
	void reportMenu(uint64 serial, bool taken);
	void reportClaim(uint64 serial, twidget::WidgetGestureClaim claim);
	[[nodiscard]] std::optional<RendererCursor> reportCursor();
	[[nodiscard]] bool reportSelection();

	const not_null<RendererPlatform*> _platform;
	const std::shared_ptr<ReportPost> _post;
	const Delegate _delegate;
	Mouse _mouse;
	Finger _finger;
	Reports _reports;
	std::deque<Ask> _asks;
	std::optional<Move> _move;
	std::optional<MenuQuery> _menuQuery;
	std::optional<MenuDown> _menuDown;
	QString _selection;
	uint64 _pointerSerial = 0;
	uint64 _selectionSerial = 0;
	uint64 _selectionAt = 0;
	uint64 _selectionStamp = 0;
	uint64 _cursorSerial = 0;
	uint64 _fingerAsk = 0;
	uint64 _clickable = 0;
	int _movesOut = 0;
	int _claimsOut = 0;
	bool _hand = false;
	bool _fingerHold = false;
	bool _fingerTick = false;
	bool _mouseLive = false;
	bool _fingerLive = false;
	bool _selectionDirty = false;
	bool _inactive = false;
	bool _overflowed = false;

};

} // namespace Tgw
