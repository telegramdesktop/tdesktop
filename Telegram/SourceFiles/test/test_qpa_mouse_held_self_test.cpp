/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_qpa_mouse.h"

#include "base/unique_qptr.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/widgets/scroll_area.h"
#include "ui/abstract_button.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QLocale>
#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>

#include <cmath>
#include <vector>

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

// A point a widget got matches the expected one within kPositionTolerance
// logical px, as in the QPA mouse self-test.
constexpr auto kPositionTolerance = 0.01;

#ifdef DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPrivateApi = false;
#else // DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPrivateApi = true;
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

// A mouse move as the fixture window or the button got it. |position| is
// in the receiver's coordinates. For a window arrival |inside| says
// whether its global point was inside the button right then, and
// |afterEnd| that it came after the sequence's end().
struct MoveRecord {
	QPointF position;
	QPointF global;
	Qt::MouseButtons buttons = Qt::NoButton;
	Qt::MouseEventSource source = Qt::MouseEventNotSynthesized;
	bool spontaneous = false;
	bool inside = false;
	bool afterEnd = false;
};

// A Ui::AbstractButton that records every move it gets and whether it
// read Over right before it handled a release.
class HeldButton final : public Ui::AbstractButton {
public:
	explicit HeldButton(QWidget *parent);

	void reset();
	[[nodiscard]] const std::vector<MoveRecord> &moves() const;
	[[nodiscard]] int releases() const;
	[[nodiscard]] bool overAtRelease() const;

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	std::vector<MoveRecord> _moves;
	int _releases = 0;
	bool _overAtRelease = false;

};

// An application event filter installed after a leg's press, so Qt puts
// it in front of the sequence's held-move filter: it records every mouse
// move that reaches the fixture's QWindow as it arrived, before anything
// repositions it. It never vetoes.
class ArrivalRecorder final : public QObject {
public:
	ArrivalRecorder(
		not_null<QWindow*> window,
		not_null<HeldButton*> button,
		std::vector<MoveRecord> &arrivals);
	~ArrivalRecorder();

	void markEnded();

protected:
	bool eventFilter(QObject *receiver, QEvent *event) override;

private:
	const QPointer<QWindow> _window;
	const QPointer<HeldButton> _button;
	std::vector<MoveRecord> &_arrivals;
	bool _ended = false;

};

// One leg, measured in one turn. The held leg releases at the press point
// P; the control moves through the sequence to Q before the scroll and
// releases there. |expected| is the button-local point of the leg's last
// sequence point after the scroll, |expectedMove| the control's Q before
// it, |pressInside| whether P was inside the scrolled button.
struct HeldLeg {
	QString name;
	QpaMouseDelivery press;
	QpaMouseDelivery move;
	QpaMouseDelivery release;
	QpaMouseEnd end;
	std::vector<MoveRecord> arrivals;
	std::vector<MoveRecord> buttonMoves;
	QPoint cursorBefore;
	QPoint cursorAtRestore;
	QPoint cursorAfter;
	QPointF expected;
	QPointF expectedMove;
	QString button;
	int scrollBefore = 0;
	int scrollAfter = 0;
	int scrollRestored = 0;
	int releases = 0;
	int clicks = 0;
	bool control = false;
	bool pressInside = false;
	bool overAtRelease = false;
	bool measured = false;
};

// The stages and the finish callback share it. |gate| stays a refusal
// until the fixture stage accepts the window. |press| (P) and |outside|
// (Q) are in the scroll area's coordinates.
struct State {
	base::unique_qptr<Ui::RpWidget> window;
	QPointer<Ui::ScrollArea> scroll;
	QPointer<HeldButton> button;
	HeldLeg held;
	HeldLeg control;
	QRect available;
	QPoint press;
	QPoint outside;
	QPoint cursorFirst;
	QPoint cursorLast;
	QString gate = u"fixture gate: the fixture stage has not run"_q;
	QString buildRefusal;
	int quarter = 0;
	int clicks = 0;
};

HeldButton::HeldButton(QWidget *parent)
: AbstractButton(parent) {
}

void HeldButton::reset() {
	_moves.clear();
	_releases = 0;
	_overAtRelease = false;
}

const std::vector<MoveRecord> &HeldButton::moves() const {
	return _moves;
}

int HeldButton::releases() const {
	return _releases;
}

bool HeldButton::overAtRelease() const {
	return _overAtRelease;
}

void HeldButton::mouseMoveEvent(QMouseEvent *e) {
	_moves.push_back({
		.position = e->position(),
		.global = e->globalPosition(),
		.buttons = e->buttons(),
		.source = e->source(),
		.spontaneous = e->spontaneous(),
		.inside = QRectF(rect()).contains(e->position()),
	});
	AbstractButton::mouseMoveEvent(e);
}

void HeldButton::mouseReleaseEvent(QMouseEvent *e) {
	++_releases;
	_overAtRelease = isOver();
	AbstractButton::mouseReleaseEvent(e);
}

ArrivalRecorder::ArrivalRecorder(
	not_null<QWindow*> window,
	not_null<HeldButton*> button,
	std::vector<MoveRecord> &arrivals)
: _window(window.get())
, _button(button.get())
, _arrivals(arrivals) {
	QCoreApplication::instance()->installEventFilter(this);
}

ArrivalRecorder::~ArrivalRecorder() {
	QCoreApplication::instance()->removeEventFilter(this);
}

void ArrivalRecorder::markEnded() {
	_ended = true;
}

bool ArrivalRecorder::eventFilter(QObject *receiver, QEvent *event) {
	if ((event->type() != QEvent::MouseMove)
		|| (receiver != _window.data())) {
		return false;
	}
	const auto move = static_cast<QMouseEvent*>(event);
	const auto button = static_cast<QWidget*>(_button.data());
	const auto global = move->globalPosition();
	_arrivals.push_back({
		.position = move->position(),
		.global = global,
		.buttons = move->buttons(),
		.source = move->source(),
		.spontaneous = move->spontaneous(),
		.inside = button
			&& QRectF(button->rect()).contains(button->mapFromGlobal(global)),
		.afterEnd = _ended,
	});
	return false;
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString NumberText(double value) {
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

[[nodiscard]] QString PointText(QPointF point) {
	return u"%1,%2"_q.arg(NumberText(point.x()), NumberText(point.y()));
}

[[nodiscard]] QString GlobalRectText(QWidget *widget) {
	return widget
		? RectText(QRect(widget->mapToGlobal(QPoint()), widget->size()))
		: u"none"_q;
}

[[nodiscard]] QString UnsupportedReason() {
	return u"unsupported: built with DESKTOP_APP_DISABLE_PRIVATE_QT_API, so "
		u"there is no QWindowSystemInterface delivery and no held "
		u"sequence"_q;
}

[[nodiscard]] bool Near(QPointF a, QPointF b) {
	return (std::abs(a.x() - b.x()) <= kPositionTolerance)
		&& (std::abs(a.y() - b.y()) <= kPositionTolerance);
}

[[nodiscard]] bool FromApplication(const MoveRecord &move) {
	return (move.source == Qt::MouseEventSynthesizedByApplication);
}

// The button-local point of |point| in the scroll area's coordinates.
[[nodiscard]] QPointF ButtonPoint(
		not_null<QWidget*> button,
		not_null<QWidget*> scroll,
		QPoint point) {
	return button->mapFromGlobal(scroll->mapToGlobal(QPointF(point)));
}

[[nodiscard]] bool ButtonContains(
		not_null<QWidget*> button,
		QPointF point) {
	return QRectF(button->rect()).contains(point);
}

// The top-left that puts a |size| window against the edges of |available|
// on the far side of |cursor| in each axis, below room for a title bar.
[[nodiscard]] QPoint FarPlacement(
		const QRect &available,
		QSize size,
		QPoint cursor) {
	const auto titleRoom = st::defaultActiveButton.height;
	const auto center = available.center();
	const auto x = (cursor.x() < center.x())
		? (available.x() + available.width() - size.width())
		: available.x();
	const auto y = (cursor.y() < center.y())
		? (available.y() + available.height() - size.height())
		: (available.y() + titleRoom);
	return QPoint(x, y);
}

// A parentless Ui::RpWidget top-level shown without activation and never
// raised, with Qt::WA_QuitOnClose cleared, holding a scroll area whose
// viewport is four by two cells (a cell is st::defaultActiveButton.height)
// half a cell in from the window's edges, its content four by four cells,
// and in that content the button, three by one cells, half a cell in.
[[nodiscard]] QString BuildFixture(not_null<State*> state) {
	const auto cursor = QCursor::pos();
	state->cursorFirst = cursor;
	const auto at = QGuiApplication::screenAt(cursor);
	const auto screen = at ? at : QGuiApplication::primaryScreen();
	if (!screen) {
		return u"no screen: neither one at the cursor nor a primary one"_q;
	}
	const auto cell = st::defaultActiveButton.height;
	const auto half = cell / 2;
	state->quarter = cell / 4;
	state->available = screen->availableGeometry();
	state->window = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto window = state->window.get();
	window->setObjectName(u"qpa_mouse_held_self_test"_q);
	window->setWindowTitle(u"Harness QPA held pointer"_q);
	window->setAttribute(Qt::WA_ShowWithoutActivating);
	window->setAttribute(Qt::WA_QuitOnClose, false);
	window->setAttribute(Qt::WA_OpaquePaintEvent);
	window->paintOn([=](QPainter &p) {
		p.fillRect(window->rect(), st::windowBg);
	});
	const auto size = QSize(half + 4 * cell + half, half + 2 * cell + half);
	window->setGeometry(QRect(
		FarPlacement(state->available, size, cursor),
		size));
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		window,
		st::defaultScrollArea);
	scroll->setObjectName(u"qpa_mouse_held_self_test.scroll"_q);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setGeometry(half, half, 4 * cell, 2 * cell);
	auto inner = object_ptr<Ui::RpWidget>(scroll);
	inner->setObjectName(u"qpa_mouse_held_self_test.content"_q);
	inner->resize(4 * cell, 4 * cell);
	const auto button = Ui::CreateChild<HeldButton>(inner.data());
	button->setObjectName(u"qpa_mouse_held_self_test.button"_q);
	button->setGeometry(half, half, 3 * cell, cell);
	button->setClickedCallback([=] {
		++state->clicks;
	});
	button->show();
	scroll->setOwnedWidget(std::move(inner));
	scroll->show();
	window->show();
	Ui::SendPendingMoveResizeEvents(window);
	state->scroll = scroll;
	state->button = button;
	return QString();
}

// Readiness only, never a success: a shown QWindow.
[[nodiscard]] bool WindowReady(const State &state) {
	const auto window = state.window.get();
	const auto handle = window ? window->windowHandle() : nullptr;
	return handle && handle->isVisible();
}

// P is the button's centre and Q a quarter cell right of the scroll
// area, inside the window's margin, both in the scroll area's
// coordinates, read once the window is shown.
void ResolvePoints(State &state) {
	const auto scroll = state.scroll.data();
	const auto button = state.button.data();
	if (scroll && button) {
		state.press = button->mapTo(scroll, button->rect().center());
		state.outside = QPoint(
			scroll->width() + state.quarter,
			state.press.y());
	}
}

// Empty when both legs can start: the window shown inside the screen with
// the OS pointer outside its frame, P on the button, Q outside it and
// inside the window, a scroll range for the quarter-cell scroll, and Qt
// holding no button, grab, popup or modal.
[[nodiscard]] QString FixtureRefusal(const State &state) {
	const auto window = state.window.get();
	const auto scroll = state.scroll.data();
	const auto button = state.button.data();
	if (!state.buildRefusal.isEmpty()) {
		return state.buildRefusal;
	} else if (!WindowReady(state) || !scroll || !button) {
		return u"the fixture window is not shown"_q;
	} else if (!state.available.contains(window->geometry())) {
		return u"the window does not fit the available screen geometry"_q;
	} else if (window->frameGeometry().contains(QCursor::pos())) {
		return u"the OS pointer is over the fixture window's frame"_q;
	}
	const auto pressGlobal = scroll->mapToGlobal(state.press);
	const auto outsideGlobal = scroll->mapToGlobal(state.outside);
	if (window->childAt(window->mapFromGlobal(pressGlobal)) != button) {
		return u"the press point does not hit the button"_q;
	} else if (ButtonContains(button, button->mapFromGlobal(outsideGlobal))
		|| !window->rect().contains(window->mapFromGlobal(outsideGlobal))) {
		return u"the outside point is not outside the button inside the "
			u"window"_q;
	} else if ((state.quarter <= 0)
		|| (scroll->scrollTop() != 0)
		|| (scroll->scrollTopMax() < state.quarter)) {
		return u"the scroll area cannot scroll by a quarter cell from its "
			u"top"_q;
	} else if (QGuiApplication::mouseButtons()) {
		return u"Qt already holds a mouse button"_q;
	} else if (const auto grabber = QWidget::mouseGrabber()) {
		return u"a mouse grab is held: "_q + WidgetDescription(grabber);
	} else if (const auto popup = QApplication::activePopupWidget()) {
		return u"a popup is open: "_q + WidgetDescription(popup);
	} else if (const auto modal = QApplication::activeModalWidget()) {
		return u"a modal window is open: "_q + WidgetDescription(modal);
	}
	return QString();
}

[[nodiscard]] QString FixtureText(const State &state) {
	const auto window = state.window.get();
	const auto scroll = state.scroll.data();
	const auto handle = window ? window->windowHandle() : nullptr;
	return u"QPA_MOUSE_HELD: fixture qt=%1 privateApi=%2 dpr=%3 screen=%4 "
		u"window=%5 frame=%6 scroll=%7 button=%8 press=%9 pressGlobal=%10 "
		u"outside=%11 outsideGlobal=%12 quarter=%13 scrollTopMax=%14 "
		u"cursor=%15 buttons=%16"_q.arg(
			QString::fromLatin1(qVersion()),
			FlagText(kPrivateApi),
			handle ? NumberText(handle->devicePixelRatio()) : u"none"_q,
			RectText(state.available),
			window ? RectText(window->geometry()) : u"none"_q,
			window ? RectText(window->frameGeometry()) : u"none"_q,
			GlobalRectText(scroll),
			GlobalRectText(state.button.data()),
			PointText(state.press),
			scroll ? PointText(scroll->mapToGlobal(state.press)) : u"none"_q,
			PointText(state.outside),
			(scroll
				? PointText(scroll->mapToGlobal(state.outside))
				: u"none"_q),
			QString::number(state.quarter),
			scroll ? QString::number(scroll->scrollTopMax()) : u"none"_q,
			PointText(QCursor::pos()),
			QString::number(QGuiApplication::mouseButtons().toInt()));
}

void FinishFixture(not_null<State*> state) {
	ResolvePoints(*state);
	const auto refusal = FixtureRefusal(*state);
	state->gate = refusal.isEmpty()
		? QString()
		: (u"fixture gate: "_q + refusal);
	const auto text = FixtureText(*state);
	Check(
		refusal.isEmpty(),
		u"fixture gate: the QPA held pointer self-test's window is shown "
		u"inside the screen with the OS pointer outside its frame, the "
		u"press point on the button, the outside point off it, room to "
		u"scroll, and Qt holding no button, grab, popup or modal"_q,
		refusal.isEmpty() ? text : (text + u" - "_q + refusal));
}

void DestroyFixture(State &state) {
	state.button = nullptr;
	state.scroll = nullptr;
	state.window = nullptr;
}

// One turn: the press, the arrival recorder, the control's move to Q, the
// scroll inside Settle, the release at the last sequence point, end(),
// then the restoring scroll inside Settle after end().
void RunLeg(State &state, HeldLeg &leg) {
	const auto window = state.window.get();
	const auto handle = window ? window->windowHandle() : nullptr;
	const auto scroll = state.scroll.data();
	const auto button = state.button.data();
	if (!handle || !scroll || !button) {
		return;
	}
	leg.button = WidgetDescription(button);
	leg.cursorBefore = QCursor::pos();
	leg.scrollBefore = scroll->scrollTop();
	button->reset();
	state.clicks = 0;
	const auto last = leg.control ? state.outside : state.press;
	auto sequence = QpaMouseSequence(scroll);
	leg.press = sequence.deliver({
		.type = QpaMouseType::Press,
		.point = state.press,
		.button = Qt::LeftButton,
		.buttons = Qt::LeftButton,
	});
	{
		auto recorder = ArrivalRecorder(handle, button, leg.arrivals);
		if (leg.control) {
			leg.expectedMove = ButtonPoint(button, scroll, state.outside);
			leg.move = sequence.deliver({
				.type = QpaMouseType::Move,
				.point = state.outside,
				.buttons = Qt::LeftButton,
			});
		}
		Settle([&] {
			scroll->scrollToY(state.quarter);
		});
		leg.scrollAfter = scroll->scrollTop();
		leg.expected = ButtonPoint(button, scroll, last);
		leg.pressInside = ButtonContains(
			button,
			ButtonPoint(button, scroll, state.press));
		leg.release = sequence.deliver({
			.type = QpaMouseType::Release,
			.point = last,
			.button = Qt::LeftButton,
			.buttons = Qt::NoButton,
		});
		leg.end = sequence.end();
		recorder.markEnded();
		leg.cursorAtRestore = QCursor::pos();
		Settle([&] {
			scroll->scrollToY(0);
		});
		leg.scrollRestored = scroll->scrollTop();
	}
	leg.cursorAfter = QCursor::pos();
	state.cursorLast = leg.cursorAfter;
	leg.clicks = state.clicks;
	if (const auto alive = state.button.data()) {
		leg.buttonMoves = alive->moves();
		leg.releases = alive->releases();
		leg.overAtRelease = alive->overAtRelease();
		leg.measured = true;
	}
}

[[nodiscard]] QString SourceName(Qt::MouseEventSource source) {
	switch (source) {
	case Qt::MouseEventNotSynthesized: return u"qpa"_q;
	case Qt::MouseEventSynthesizedBySystem: return u"system"_q;
	case Qt::MouseEventSynthesizedByQt: return u"qt"_q;
	case Qt::MouseEventSynthesizedByApplication: return u"application"_q;
	}
	return QString::number(int(source));
}

[[nodiscard]] QString MovesText(const std::vector<MoveRecord> &moves) {
	auto parts = QStringList();
	for (const auto &move : moves) {
		parts.push_back(u"%1 pos=%2 global=%3 buttons=%4 spontaneous=%5 "
			u"inside=%6 afterEnd=%7"_q.arg(
				SourceName(move.source),
				PointText(move.position),
				PointText(move.global),
				QString::number(move.buttons.toInt()),
				FlagText(move.spontaneous),
				FlagText(move.inside),
				FlagText(move.afterEnd)));
	}
	return parts.join(u"; "_q);
}

// "QPA_MOUSE_HELD: leg=<name> measured=<0|1> cursor=<before>-><restore>->
// <after> scroll=<before>-><after>-><restored> pressInside=<0|1>
// expected=<x,y> expectedMove=<x,y> overAtRelease=<0|1> releases=<n>
// clicks=<n> arrivals=[...] buttonMoves=[...] press=[...] move=[...]
// release=[...] end=[...]".
[[nodiscard]] QString LegText(const HeldLeg &leg) {
	return u"QPA_MOUSE_HELD: leg=%1 measured=%2 cursor=%3->%4->%5 "
		u"scroll=%6->%7->%8 pressInside=%9 expected=%10 expectedMove=%11 "
		u"overAtRelease=%12 releases=%13 clicks=%14 arrivals=[%15] "
		u"buttonMoves=[%16] press=[%17] move=[%18] release=[%19] "
		u"end=[%20]"_q.arg(
			leg.name,
			FlagText(leg.measured),
			PointText(leg.cursorBefore),
			PointText(leg.cursorAtRestore),
			PointText(leg.cursorAfter),
			QString::number(leg.scrollBefore),
			QString::number(leg.scrollAfter),
			QString::number(leg.scrollRestored),
			FlagText(leg.pressInside),
			PointText(leg.expected),
			PointText(leg.expectedMove),
			FlagText(leg.overAtRelease),
			QString::number(leg.releases),
			QString::number(leg.clicks),
			MovesText(leg.arrivals),
			MovesText(leg.buttonMoves),
			QpaMouseDeliveryText(leg.press),
			(leg.control ? QpaMouseDeliveryText(leg.move) : u"none"_q),
			QpaMouseDeliveryText(leg.release),
			QpaMouseEndText(leg.end));
}

// lib_ui's held move from the OS pointer, as it reached the window during
// the hold: application-synthesized, not spontaneous, the left button
// held, at a point outside the button.
[[nodiscard]] bool Reached(const HeldLeg &leg) {
	return ranges::any_of(leg.arrivals, [](const MoveRecord &move) {
		return !move.afterEnd
			&& FromApplication(move)
			&& !move.spontaneous
			&& (move.buttons & Qt::LeftButton)
			&& !move.inside;
	});
}

// Every application-synthesized move the button got came at |expected|,
// and there was one.
[[nodiscard]] bool ButtonGotApplicationMoveAt(
		const HeldLeg &leg,
		QPointF expected) {
	auto found = false;
	for (const auto &move : leg.buttonMoves) {
		if (!FromApplication(move)) {
			continue;
		} else if (!Near(move.position, expected)) {
			return false;
		}
		found = true;
	}
	return found;
}

// After end(), the restoring scroll's application move reached the window
// at the OS pointer, with no button: unchanged.
[[nodiscard]] bool RestoreUnchanged(const HeldLeg &leg) {
	return ranges::any_of(leg.arrivals, [&](const MoveRecord &move) {
		return move.afterEnd
			&& FromApplication(move)
			&& !move.buttons
			&& (move.global == QPointF(leg.cursorAtRestore));
	});
}

[[nodiscard]] bool Delivered(
		const QpaMouseDelivery &delivery,
		const QString &receiver) {
	return (delivery.refusal == QpaMouseRefusal::None)
		&& delivery.delivered
		&& (delivery.receiver == receiver);
}

[[nodiscard]] bool Scrolled(const HeldLeg &leg) {
	return (leg.scrollAfter > leg.scrollBefore)
		&& (leg.scrollRestored == leg.scrollBefore);
}

void CheckReached(const HeldLeg &leg, const QString &text) {
	Check(
		leg.measured && Reached(leg),
		u"QPA held pointer self-test: %1: reached - lib_ui's "
		u"application-synthesized held move from the OS pointer reached "
		u"the fixture window during the hold at a point outside the "
		u"button (without it the leg is unreached, never a PASS)"_q.arg(
			leg.name),
		text);
}

void CheckEndAndRestore(const HeldLeg &leg, const QString &text) {
	Check(
		leg.measured
			&& leg.end.clean()
			&& !leg.end.held
			&& RestoreUnchanged(leg),
		u"QPA held pointer self-test: %1: end() needs no cleanup release "
		u"and ends clean, and after it the restoring scroll's "
		u"application move reaches the window at the OS pointer, "
		u"unchanged"_q.arg(leg.name),
		text);
}

void CheckHeldLeg(const State &state) {
	const auto &leg = state.held;
	const auto text = LegText(leg);
	CheckReached(leg, text);
	Check(
		leg.measured
			&& Reached(leg)
			&& Delivered(leg.press, leg.button)
			&& Delivered(leg.release, leg.button)
			&& Scrolled(leg)
			&& leg.pressInside
			&& ButtonGotApplicationMoveAt(leg, leg.expected)
			&& leg.overAtRelease
			&& (leg.releases == 1)
			&& (leg.clicks == 1),
		u"QPA held pointer self-test: held: a press held on the button "
		u"through a scroll of its area, with the OS pointer outside it, "
		u"keeps it at the sequence's point - it gets lib_ui's move there, "
		u"with the press point still inside it - so it reads Over at the "
		u"release at the press point and fires exactly one click"_q,
		text);
	CheckEndAndRestore(leg, text);
}

void CheckControlLeg(const State &state) {
	const auto &leg = state.control;
	const auto text = LegText(leg);
	CheckReached(leg, text);
	const auto qpaMove = !leg.buttonMoves.empty()
		&& !FromApplication(leg.buttonMoves.front())
		&& Near(leg.buttonMoves.front().position, leg.expectedMove);
	Check(
		leg.measured
			&& Reached(leg)
			&& Delivered(leg.press, leg.button)
			&& Delivered(leg.move, leg.button)
			&& Delivered(leg.release, leg.button)
			&& Scrolled(leg)
			&& qpaMove
			&& ButtonGotApplicationMoveAt(leg, leg.expected)
			&& !leg.overAtRelease
			&& (leg.releases == 1)
			&& (leg.clicks == 0),
		u"QPA held pointer self-test: control: after a held move through "
		u"the sequence to a point outside the button, lib_ui's move from "
		u"the OS pointer reaches the button at that outside point, not "
		u"the press point, so it reads no Over at the release there and "
		u"fires no click"_q,
		text);
	CheckEndAndRestore(leg, text);
}

void CheckCursor(const State &state) {
	const auto first = state.cursorFirst;
	const auto same = (state.held.cursorBefore == first)
		&& (state.held.cursorAfter == first)
		&& (state.control.cursorBefore == first)
		&& (state.control.cursorAfter == first)
		&& (state.cursorLast == first)
		&& (QCursor::pos() == first);
	Check(
		state.held.measured && state.control.measured && same,
		u"QPA held pointer self-test: QCursor::pos() reads the same at the "
		u"fixture, before and after each leg and now"_q,
		u"QPA_MOUSE_HELD: cursor first=%1 held=%2->%3 control=%4->%5 "
		u"last=%6 now=%7"_q.arg(
			PointText(first),
			PointText(state.held.cursorBefore),
			PointText(state.held.cursorAfter),
			PointText(state.control.cursorBefore),
			PointText(state.control.cursorAfter),
			PointText(state.cursorLast),
			PointText(QCursor::pos())));
}

} // namespace

void AppendQpaMouseHeldPointerSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();
	state->held.name = u"held"_q;
	state->control.name = u"control"_q;
	state->control.control = true;
	const auto gate = [=] {
		return kPrivateApi ? state->gate : UnsupportedReason();
	};

	runner->add({
		.name = u"QPA held pointer self-test: fixture - a button in a "
			u"scroll area in an ordinary window shown without activation "
			u"away from the OS pointer"_q,
		.skipReason = [] {
			return kPrivateApi ? QString() : UnsupportedReason();
		},
		.run = [=] {
			state->buildRefusal = BuildFixture(state.get());
		},
		.until = [=] {
			return !state->buildRefusal.isEmpty() || WindowReady(*state);
		},
		.then = [=] {
			FinishFixture(state.get());
		},
		.timeoutDetails = [=] {
			return FixtureText(*state);
		},
	});

	runner->addGated(gate, [&] {
		runner->add({
			.name = u"QPA held pointer self-test: held - press, scroll, "
				u"release at the press point"_q,
			.run = [=] {
				RunLeg(*state, state->held);
			},
			.then = [=] {
				CheckHeldLeg(*state);
			},
		});
		runner->add({
			.name = u"QPA held pointer self-test: control - press, move "
				u"outside, scroll, release outside"_q,
			.run = [=] {
				RunLeg(*state, state->control);
			},
			.then = [=] {
				CheckControlLeg(*state);
			},
		});
		runner->add({
			.name = u"QPA held pointer self-test: the OS cursor is "
				u"untouched"_q,
			.run = [=] {
				CheckCursor(*state);
			},
		});
	});

	runner->addRecovery({
		.name = u"QPA held pointer self-test: teardown - destroy the "
			u"window"_q,
		.run = [=] {
			const auto had = state->window ? 1 : 0;
			DestroyFixture(*state);
			Note(u"QPA_MOUSE_HELD: teardown window=%1->%2"_q.arg(
				QString::number(had),
				QString::number(state->window ? 1 : 0)));
		},
	});

	runner->onFinish([=] {
		DestroyFixture(*state);
	});
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
