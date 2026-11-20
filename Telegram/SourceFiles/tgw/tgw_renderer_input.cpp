/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer_input.h"

#include "core/src/platform_end.h"
#include "tgw/tgw_painter.h"
#include "tgw/tgw_renderer_platform.h"
#include "tgw/tgw_renderer_rules.h"

namespace Tgw {
namespace {

using RendererRules::kFingerPointer;
using RendererRules::kHoverLeave;
using RendererRules::kHoverMove;
using RendererRules::kMousePointer;
using RendererRules::kPrimaryButton;
using RendererRules::kSecondaryButton;
using RendererRules::kTouchCancel;
using RendererRules::kTouchDown;
using RendererRules::kTouchMove;
using RendererRules::kTouchUp;

constexpr auto kMaxSolicited = 256;
constexpr auto kHandCursor = std::string_view("pointer");

template <typename Target>
decltype(auto) Touched(Target &target, const TouchInput &input) {
	return target.touch(
		input.action,
		input.point.x(),
		input.point.y(),
		input.pointer,
		true,
		(input.pointer == kMousePointer),
		input.modifiers,
		input.button);
}

[[nodiscard]] twidget::WidgetGestureClaim ClaimOf(
		const std::optional<twidget::WidgetGestureClaim> &claim) {
	if (!claim) {
		return {};
	}
	return {
		.x = claim->x,
		.hold = claim->hold,
	};
}

[[nodiscard]] TouchInput MouseCancel(QPointF point) {
	return {
		.point = point,
		.action = kTouchCancel,
		.pointer = kMousePointer,
		.button = kPrimaryButton,
	};
}

[[nodiscard]] TouchInput FingerTouch(
		QPointF point,
		int action,
		int modifiers = 0) {
	return {
		.point = point,
		.action = action,
		.modifiers = modifiers,
		.pointer = kFingerPointer,
		.button = kPrimaryButton,
	};
}

} // namespace

void DeliverTouch(
		twlite::remote::PlatformEnd &end,
		const TouchInput &input) {
	Touched(end, input);
}

bool MenuTaken(const twidget::WidgetGestureClaim &claim, QPointF point) {
	const auto verdict = twidget::GestureArbiter().down(
		double(crl::now()),
		point.x(),
		point.y(),
		claim,
		twidget::PointerKind::Mouse,
		kSecondaryButton);
	return (verdict.owner == twidget::GestureOwner::Widget);
}

bool ReportPost::offer(RendererSelection selection) {
	auto lock = std::lock_guard(_mutex);
	if (_closed) {
		return false;
	}
	_latest.selection = std::move(selection);
	return !std::exchange(_posted, true);
}

bool ReportPost::offer(RendererCursor cursor) {
	auto lock = std::lock_guard(_mutex);
	if (_closed) {
		return false;
	}
	_latest.cursor = cursor;
	return !std::exchange(_posted, true);
}

ReportPost::Latest ReportPost::take() {
	auto lock = std::lock_guard(_mutex);
	_posted = false;
	return base::take(_latest);
}

void ReportPost::close() {
	auto lock = std::lock_guard(_mutex);
	_closed = true;
	_latest = Latest();
}

HostedInput::HostedInput(
	not_null<RendererPlatform*> platform,
	std::shared_ptr<ReportPost> post,
	Delegate delegate)
: _platform(platform)
, _post(std::move(post))
, _delegate(std::move(delegate)) {
}

void HostedInput::enter(uint64 serial) {
	_pointerSerial = serial;
	_delegate.schedule();
	if (_delegate.accepts()) {
		_mouse.over = true;
	}
}

void HostedInput::move(QPointF point, int modifiers, uint64 serial) {
	_pointerSerial = serial;
	_delegate.schedule();
	if (!_delegate.accepts()) {
		return;
	}
	_mouse.position = point;
	const auto pressed = _mouse.pressed;
	if (!pressed) {
		_mouse.over = _mouse.hovering = true;
	}
	queueMove(point, modifiers, kMousePointer, !pressed);
	_delegate.input();
}

void HostedInput::leave(uint64 serial) {
	_pointerSerial = serial;
	_delegate.schedule();
	if (!_delegate.accepts()) {
		return;
	}
	_mouse.over = false;
	if (_mouse.pressed || !_mouse.hovering) {
		return;
	}
	leaveHover();
	_delegate.input();
}

void HostedInput::press(
		QPointF point,
		int modifiers,
		uint64 serial,
		uint64 selection) {
	_pointerSerial = serial;
	_selectionSerial = selection;
	_delegate.schedule();
	if (!_delegate.accepts()) {
		return;
	}
	menuRelease(modifiers, false);
	cancelMenuDown();
	endMenuQuery();
	const auto down = TouchInput{
		.point = point,
		.action = kTouchDown,
		.modifiers = modifiers,
		.pointer = kMousePointer,
		.button = kPrimaryButton,
	};
	if (!sendDown(down, Reply::Cursor, 0)) {
		return;
	}
	holdMouse(point, false);
	_mouse.down = _platform->lastDown();
	_delegate.observed(
		{ .kind = PressStep::Kind::Sent, .serial = serial },
		_mouse.down);
	_delegate.input();
}

void HostedInput::release(
		QPointF point,
		int modifiers,
		bool cancel,
		uint64 serial) {
	_pointerSerial = serial;
	_delegate.schedule();
	releaseMouse(point, modifiers, cancel);
}

void HostedInput::releaseMouse(QPointF point, int modifiers, bool cancel) {
	_mouse.menu = 0;
	if (!_delegate.accepts() || !_mouse.pressed) {
		return;
	}
	// A press the chat took for itself ends with no click.
	if (cancel) {
		send(MouseCancel(_mouse.position));
	} else {
		const auto up = TouchInput{
			.point = point,
			.action = kTouchUp,
			.modifiers = modifiers,
			.pointer = kMousePointer,
			.button = kPrimaryButton,
		};
		const auto press = _mouse.secondary ? Press::None : Press::Up;
		send(up, Reply::Cursor, 0, press, _mouse.down);
	}
	endPress(false);
	_mouse.position = point;
	_mouse.down = 0;
	_mouse.pressed = _mouse.secondary = false;
	if (!_mouse.over) {
		leaveHover();
	}
	_delegate.input();
}

void HostedInput::holdMouse(QPointF point, bool secondary) {
	_mouse.position = point;
	_mouse.over = _mouse.hovering = _mouse.pressed = true;
	_mouse.secondary = secondary;
}

void HostedInput::leaveHover() {
	_mouse.hovering = false;
	flushMove();
	if (solicit() && _platform->hover(kHoverLeave, QPointF(), 0)) {
		expect(Reply::Cursor, 0, _pointerSerial, _selectionSerial);
	}
}

void HostedInput::menuPress(QPointF point, int modifiers, uint64 serial) {
	if (!_delegate.accepts() || _mouse.pressed || _finger.pressed) {
		reportMenu(serial, false);
		return;
	}
	cancelMenuDown();
	endMenuQuery();
	flushMove();
	// Its answer follows the selection of every input sent before it.
	const auto seq = solicit() ? _platform->askClaim(point) : uint64();
	if (!seq) {
		reportMenu(serial, false);
		return;
	}
	++_claimsOut;
	_menuQuery = MenuQuery{
		.point = point,
		.serial = serial,
		.seq = seq,
		.modifiers = modifiers,
	};
	_delegate.input();
}

void HostedInput::menuDecided(uint64 serial, bool deliver) {
	if (!serial) {
		return;
	} else if (!deliver) {
		if (_menuQuery && (_menuQuery->serial == serial)) {
			_menuQuery = std::nullopt;
		}
		if (_menuDown && (_menuDown->serial == serial)) {
			cancelMenuDown();
		}
	}
	if (_mouse.menu != serial) {
		return;
	}
	_mouse.menu = 0;
	if (!_mouse.secondary || !_delegate.accepts()) {
		return;
	} else if (deliver) {
		_platform->deliverMenus(_mouse.down);
	} else {
		releaseMouse(_mouse.position, 0, true);
	}
}

void HostedInput::menuRelease(int modifiers, bool cancel) {
	if (_mouse.secondary) {
		releaseMouse(_mouse.position, modifiers, cancel);
	}
}

void HostedInput::pressMenu(uint64 serial, QPointF point, int modifiers) {
	cancelMenuDown();
	const auto down = TouchInput{
		.point = point,
		.action = kTouchDown,
		.modifiers = modifiers,
		.pointer = kMousePointer,
		.button = kSecondaryButton,
	};
	if (!send(down, Reply::Menu, serial)) {
		reportMenu(serial, false);
		return;
	}
	_menuDown = MenuDown{
		.point = point,
		.serial = serial,
		.press = _platform->lastDown(),
	};
}

void HostedInput::cancelMenuDown() {
	if (const auto down = base::take(_menuDown)) {
		send(MouseCancel(down->point));
	}
}

void HostedInput::endMenuQuery() {
	if (const auto query = base::take(_menuQuery)) {
		reportMenu(query->serial, false);
	}
}

void HostedInput::answerMenu(
		uint64 serial,
		const std::optional<twidget::WidgetGestureClaim> &claim) {
	if (!_menuDown || (_menuDown->serial != serial)) {
		return;
	}
	const auto down = *base::take(_menuDown);
	const auto idle = _delegate.accepts()
		&& !_mouse.pressed
		&& !_finger.pressed;
	const auto taken = idle && MenuTaken(ClaimOf(claim), down.point);
	if (taken) {
		holdMouse(down.point, true);
		_mouse.down = down.press;
		_mouse.menu = serial;
	} else {
		send(MouseCancel(down.point));
	}
	reportMenu(serial, taken);
}

void HostedInput::fingerPress(QPointF point, int modifiers, uint64 serial) {
	if (!_delegate.accepts() || _finger.pressed) {
		reportClaim(serial, {});
		return;
	}
	const auto down = FingerTouch(point, kTouchDown, modifiers);
	if (!sendDown(down, Reply::Finger, serial)) {
		reportClaim(serial, {});
		return;
	}
	_fingerAsk = serial;
	_finger = Finger{
		.position = point,
		.down = _platform->lastDown(),
		.pressed = true,
	};
	_delegate.input();
}

void HostedInput::fingerMove(QPointF point, int modifiers) {
	if (!_finger.pressed
		|| !_delegate.accepts()
		|| (point == _finger.position)) {
		return;
	}
	_finger.position = point;
	queueMove(point, modifiers, kFingerPointer, false);
	_delegate.input();
}

bool HostedInput::fingerHold(bool asking) {
	if (!_finger.pressed || !_delegate.accepts()) {
		return false;
	}
	flushMove();
	_fingerHold = true;
	_delegate.input();
	if (!asking) {
		return true;
	}
	_platform->fingerHold(_finger.down);
	_fingerTick = true;
	return false;
}

void HostedInput::fingerRelease(QPointF point, int modifiers) {
	if (!_finger.pressed || !_delegate.accepts()) {
		return;
	}
	flushMove();
	_platform->fingerRelease(_finger.down);
	if (point != _finger.position) {
		send(FingerTouch(point, kTouchMove, modifiers));
	}
	_finger = Finger();
	_fingerAsk = 0;
	send(FingerTouch(point, kTouchUp, modifiers));
	endFinger();
	_delegate.input();
}

void HostedInput::fingerCancel() {
	if (!_finger.pressed || !_delegate.accepts()) {
		return;
	}
	const auto position = base::take(_finger).position;
	_fingerAsk = 0;
	send(FingerTouch(position, kTouchCancel));
	endFinger();
	_delegate.input();
}

void HostedInput::clearSelection(uint64 serial) {
	_selectionSerial = serial;
	_delegate.schedule();
	const auto had = !_selection.isEmpty();
	flushMove();
	_platform->clearSelection();
	if (had) {
		_delegate.input();
	}
}

void HostedInput::cancel(bool stopped) {
	_move = std::nullopt;
	_menuQuery = std::nullopt;
	_fingerAsk = 0;
	if (stopped) {
		return;
	}
	cancelMenuDown();
	if (!(_mouse.pressed || _mouse.hovering || _finger.pressed)) {
		return;
	}
	if (_mouse.pressed) {
		send(MouseCancel(_mouse.position));
		endPress(false);
	}
	if (_finger.pressed) {
		send(FingerTouch(_finger.position, kTouchCancel));
		endFinger();
	}
	if (_mouse.hovering) {
		leaveHover();
	}
	_mouse = Mouse();
	_finger = Finger();
	_delegate.input();
}

void HostedInput::reset() {
	_move = std::nullopt;
	_menuQuery = std::nullopt;
	_fingerAsk = 0;
	endPress(false);
	endFinger();
	_mouse = Mouse();
	_finger = Finger();
}

void HostedInput::close() {
	if (const auto down = base::take(_menuDown)) {
		reportMenu(down->serial, false);
	}
	if (const auto query = base::take(_menuQuery)) {
		reportMenu(query->serial, false);
	}
	if (const auto serial = base::take(_fingerAsk)) {
		reportClaim(serial, {});
	}
	_asks.clear();
	_movesOut = _claimsOut = 0;
	_clickable = 0;
	_hand = false;
	deactivate();
}

void HostedInput::detach() {
	_platform->clearSelection();
	deactivate();
}

void HostedInput::deactivate() {
	_inactive = true;
	_selectionDirty = false;
	_selection = QString();
	_reports.text = QString();
	_post->close();
}

void HostedInput::beforeTick() {
	if (_fingerHold && _finger.pressed) {
		_platform->fingerHold(_finger.down);
	}
	_fingerTick = false;
}

bool HostedInput::tickWanted() const {
	return _fingerTick;
}

void HostedInput::report(RendererAnswer &answer, bool live) {
	if (live) {
		auto wake = reportSelection();
		if (const auto cursor = reportCursor()) {
			wake = _post->offer(*cursor) || wake;
		}
		if (wake) {
			_delegate.reports();
		}
	}
	answer.menu = base::take(_reports.menu);
	answer.claim = base::take(_reports.claim);
}

void HostedInput::pointer(const twlite::remote::Pointer &pointer) {
	if (_asks.empty()) {
		return;
	}
	const auto ask = _asks.front();
	_asks.pop_front();
	_hand = (pointer.cursor == kHandCursor);
	_cursorSerial = ask.pointer;
	_selectionAt = ask.selection;
	if ((ask.press != Press::None) && !_inactive) {
		const auto up = (ask.press == Press::Up);
		_clickable = up ? ask.down : 0;
		_delegate.observed(
			{ .kind = up ? PressStep::Kind::Up : PressStep::Kind::Down },
			ask.down);
	}
	switch (ask.reply) {
	case Reply::Cursor: break;
	case Reply::Move:
		_movesOut = std::max(_movesOut - 1, 0);
		if (!_movesOut) {
			flushMove();
		}
		break;
	case Reply::Menu: answerMenu(ask.serial, pointer.claim); break;
	case Reply::Finger:
		if (_fingerAsk == ask.serial) {
			_fingerAsk = 0;
			reportClaim(ask.serial, ClaimOf(pointer.claim));
		}
		break;
	}
	_delegate.schedule();
}

void HostedInput::clicked(uint64 press) {
	if (!_inactive && press && (base::take(_clickable) == press)) {
		_delegate.observed({ .kind = PressStep::Kind::Click }, press);
	}
}

void HostedInput::cursor(std::string_view cursor) {
	if (_inactive) {
		return;
	}
	_hand = (cursor == kHandCursor);
	_delegate.schedule();
}

void HostedInput::selection(QString text) {
	if (_inactive) {
		return;
	}
	_selection = std::move(text);
	_selectionStamp = _selectionAt;
	_selectionDirty = true;
	_delegate.schedule();
}

void HostedInput::claim(const twlite::remote::ClaimAnswer &answer) {
	_claimsOut = std::max(_claimsOut - 1, 0);
	if (!_menuQuery || (_menuQuery->seq != answer.seq)) {
		return;
	}
	const auto query = *base::take(_menuQuery);
	const auto idle = _delegate.accepts()
		&& !_mouse.pressed
		&& !_finger.pressed;
	// A core press drops the selection: keep it unless the hold is claimed.
	if (!idle || (!_selection.isEmpty() && !answer.claim.hold)) {
		reportMenu(query.serial, false);
		return;
	}
	pressMenu(query.serial, query.point, query.modifiers);
}

void HostedInput::queueMove(
		QPointF point,
		int modifiers,
		int device,
		bool hover) {
	if (_move && ((_move->device != device) || (_move->hover != hover))) {
		flushMove();
	}
	_move = Move{
		.point = point,
		.pointer = _pointerSerial,
		.selection = _selectionSerial,
		.modifiers = modifiers,
		.device = device,
		.hover = hover,
	};
	if (!_movesOut) {
		flushMove();
	}
}

void HostedInput::flushMove() {
	const auto move = base::take(_move);
	if (!move || !_delegate.accepts() || !solicit()) {
		return;
	}
	const auto sent = move->hover
		? _platform->hover(kHoverMove, move->point, move->modifiers)
		: _platform->touch({
			.point = move->point,
			.action = kTouchMove,
			.modifiers = move->modifiers,
			.pointer = move->device,
			.button = kPrimaryButton,
		});
	if (sent) {
		++_movesOut;
		expect(Reply::Move, 0, move->pointer, move->selection);
	}
}

bool HostedInput::solicit() {
	if (_overflowed) {
		return false;
	} else if (int(_asks.size()) + _claimsOut < kMaxSolicited) {
		return true;
	}
	_overflowed = true;
	_delegate.overflow(PaintLimit(
		u"%1 input replies outstanding from the widget renderer"_q.arg(
			int(_asks.size()) + _claimsOut),
		kMaxSolicited));
	return false;
}

bool HostedInput::send(
		const TouchInput &input,
		Reply reply,
		uint64 serial,
		Press press,
		uint64 down) {
	flushMove();
	if (!solicit() || !_platform->touch(input)) {
		return false;
	}
	expect(reply, serial, _pointerSerial, _selectionSerial, press, down);
	return true;
}

bool HostedInput::sendDown(
		const TouchInput &input,
		Reply reply,
		uint64 serial) {
	const auto finger = (input.pointer == kFingerPointer);
	endPress(finger);
	flushMove();
	if (!solicit()) {
		return false;
	}
	using Down = RendererPlatform::Down;
	const auto down = _platform->press(input);
	if (down == Down::Lost) {
		return false;
	}
	(finger ? _fingerLive : _mouseLive) = (down == Down::Live);
	expect(
		reply,
		serial,
		_pointerSerial,
		_selectionSerial,
		finger ? Press::None : Press::Down,
		finger ? 0 : _platform->lastDown());
	return true;
}

void HostedInput::expect(
		Reply reply,
		uint64 serial,
		uint64 pointer,
		uint64 selection,
		Press press,
		uint64 down) {
	_asks.push_back({
		.serial = serial,
		.pointer = pointer,
		.selection = selection,
		.down = down,
		.reply = reply,
		.press = press,
	});
}

void HostedInput::endPress(bool finger) {
	if (base::take(finger ? _fingerLive : _mouseLive)) {
		_platform->released();
	}
}

void HostedInput::endFinger() {
	_fingerHold = _fingerTick = false;
	endPress(true);
}

void HostedInput::reportMenu(uint64 serial, bool taken) {
	if (!_reports.menu || (_reports.menu->serial <= serial)) {
		_reports.menu = RendererMenu{ .serial = serial, .taken = taken };
	}
	_delegate.schedule();
}

void HostedInput::reportClaim(
		uint64 serial,
		twidget::WidgetGestureClaim claim) {
	if (!_reports.claim || (_reports.claim->serial <= serial)) {
		_reports.claim = RendererClaim{ .claim = claim, .serial = serial };
	}
	_delegate.schedule();
}

std::optional<RendererCursor> HostedInput::reportCursor() {
	const auto settled = _asks.empty() && !_move;
	const auto serial = settled ? _pointerSerial : _cursorSerial;
	const auto hand = _mouse.over && _hand;
	if ((hand == _reports.hand)
		&& (!settled || (serial == _reports.cursorSent))) {
		return std::nullopt;
	}
	_reports.cursorSent = serial;
	_reports.hand = hand;
	return RendererCursor{ .serial = serial, .hand = hand };
}

bool HostedInput::reportSelection() {
	if (!base::take(_selectionDirty)) {
		return false;
	}
	const auto has = !_selection.isEmpty();
	if ((_selectionStamp == _reports.textSent)
		&& (has == _reports.has)
		&& (_selection == _reports.text)) {
		return false;
	}
	_reports.textSent = _selectionStamp;
	_reports.has = has;
	_reports.text = _selection;
	return _post->offer(RendererSelection{
		.text = _selection,
		.serial = _selectionStamp,
		.has = has,
	});
}

} // namespace Tgw
