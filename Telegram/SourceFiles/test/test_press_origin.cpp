/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_press_origin.h"

#include "base/unique_qptr.h"
#include "test/test_animation_clock.h"
#include "test/test_capture.h"
#include "test/test_ink.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/effects/animation_value.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/buttons.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QWindow>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>

#include "styles/style_widgets.h"

namespace Test {
namespace {

// Logical pixels at 100 % interface scale, scaled with style::ConvertScale
// when the fixture is built: the side of each button, the space around
// them, and how far the real cursor must stay from every button.
constexpr auto kSide = 120;
constexpr auto kGap = 24;
constexpr auto kCursorClearance = kSide / 2;

// The two clocked frames after the press, scaled by anim::SlowMultiplier().
constexpr auto kEarlyFrame = crl::time(40);
constexpr auto kLateFrame = crl::time(120);
constexpr auto kFrameCount = 2;
constexpr auto kFrameTimes = std::array<crl::time, kFrameCount>{
	kEarlyFrame,
	kLateFrame,
};

// The oracle's tolerances; test_press_origin.h says where each comes from.
// Pixel counts are device pixels unless the name says logical.
constexpr auto kChangedDelta = 8;
constexpr auto kMaskMargin = 2;
constexpr auto kMinArcPixels = 16;
constexpr auto kMaxFitRms = 1.5;
constexpr auto kCentreFloor = 2.;
constexpr auto kCentreShare = 0.05;
constexpr auto kRadiusSlack = 1.5; // logical
constexpr auto kOutsideSlack = 1.5; // logical
constexpr auto kInteriorInset = 2.; // logical
constexpr auto kMinGrowth = 4.; // logical
constexpr auto kOpacitySlack = 0.05;
constexpr auto kOpacityShare = 0.95;
constexpr auto kColourChannelFloor = 48;
constexpr auto kColourSame = 3;
constexpr auto kMinInteriorPixels = 10;
constexpr auto kMaskColourShare = 0.99;
constexpr auto kMaskBoxSlack = 1;
constexpr auto kSingularShare = 1e-12;

// A visible displaced target gets the facility's move and its move back,
// and ClickWithPressOrigin withholds both from it.
constexpr auto kMovesWithheld = 2;

constexpr auto kSubjectCount = 3;
constexpr auto kProbeIndex = 0;
constexpr auto kRoundIndex = 1;
constexpr auto kIconIndex = 2;

// What a button's own events() stream records: every input and geometry
// event a press could bring, so a stray enter, move or double click shows
// up in the record instead of passing unseen.
constexpr auto kRecordedEvents = std::array{
	QEvent::MouseButtonPress,
	QEvent::MouseButtonRelease,
	QEvent::MouseButtonDblClick,
	QEvent::MouseMove,
	QEvent::Enter,
	QEvent::Leave,
	QEvent::Move,
};

enum class PressKind {
	Plain,
	Facility,
};

[[nodiscard]] QColor BackgroundColor() {
	return QColor(0xff, 0xff, 0xff);
}

[[nodiscard]] QColor RoundOverColor() {
	return QColor(0xd8, 0xd8, 0xd8);
}

[[nodiscard]] QColor ProbeRippleColor() {
	return QColor(0xc0, 0x20, 0x50);
}

[[nodiscard]] QColor RoundRippleColor() {
	return QColor(0x20, 0x50, 0xc0);
}

[[nodiscard]] QColor IconRippleColor() {
	return QColor(0x20, 0xa0, 0x40);
}

// st::defaultRippleAnimation's durations with an owned colour. The
// IconButton's default ripple is empty, colour included, so its copy is
// replaced as a whole.
[[nodiscard]] style::RippleAnimation OwnRipple(const style::color &color) {
	auto result = st::defaultRippleAnimation;
	result.color = color;
	return result;
}

// A bare Ui::RippleButton paints nothing. This paints a background and the
// ripple only, and keeps RippleButton's own prepareRippleStartPosition()
// and prepareRippleMask().
class RippleProbe final : public Ui::RippleButton {
public:
	RippleProbe(
		QWidget *parent,
		const style::RippleAnimation &st,
		QColor background);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const QColor _background;

};

// Every style::color below points into an owned colour of the same object
// (OwnedColor keeps a pointer to its own data), so a Styles is built once
// in place, never copied or moved, and outlives every button that keeps a
// reference into it.
struct Styles {
	Styles(int side, int area);
	Styles(const Styles &other) = delete;
	Styles &operator=(const Styles &other) = delete;

	style::owned_color background;
	style::owned_color roundOver;
	style::owned_color probeRipple;
	style::owned_color roundRipple;
	style::owned_color iconRipple;
	style::RippleAnimation probe;
	style::RoundButton round;
	style::IconButton icon;
};

// One event as the button's events() stream saw it, before the button's
// own handler: |pos| is the button's pos() and |seen| its
// mapFromGlobal(QCursor::pos()) at delivery - the expression every
// prepareRippleStartPosition() evaluates. |local| and |global| are set for
// mouse events only.
struct EventRecord {
	QEvent::Type type = QEvent::None;
	QPoint local;
	QPoint global;
	QPoint pos;
	QPoint seen;
};

// One button under test. |press| is its press point in button coordinates;
// |maskOrigin| and |maskSize| place the class's ripple mask in the button,
// the origin being what prepareRippleStartPosition() subtracts. |baseline|
// and |mask| are stage 1's grabs. The initial geometry and attributes are
// read once the window was shown.
struct Subject {
	QString name;
	QPointer<Ui::RippleButton> button;
	QPoint press;
	QPoint maskOrigin;
	QSize maskSize;
	QColor ripple;
	int showDuration = 0;
	int hideDuration = 0;
	QImage baseline;
	QImage mask;
	QRect initialGeometry;
	bool initialMoved = false;
	bool initialNoPropagation = false;
	bool initialPendingMove = false;
	std::vector<EventRecord> events;
};

struct Placement {
	QPoint topLeft;
	double distance = 0.;
};

struct PixelMap {
	int width = 0;
	int height = 0;
	std::vector<char> set;
};

// |solved| means a centre and a radius exist; |ok| adds the point count and
// the residual the oracle requires. Coordinates are device pixels of the
// grab, pixel x covering [x, x + 1).
struct CircleFit {
	bool solved = false;
	bool ok = false;
	int points = 0;
	double cx = 0.;
	double cy = 0.;
	double r = 0.;
	double rms = 0.;
	QString refusal;
};

// What the oracle expects of one frame of a press at a point: the radius
// window in logical pixels, the hide-opacity window, the centre tolerance
// in device pixels. |valid| only for a taken frame.
struct FrameWindow {
	bool valid = false;
	int radiusTo = 0;
	double radiusLo = 0.;
	double radiusHi = 0.;
	double opacityLo = 0.;
	double opacityHi = 0.;
	double centreTolerance = 0.;
};

struct DiscReading {
	bool readable = false;
	bool pressCovered = false;
	int changedCount = 0;
	int edgeCount = 0;
	int outsideDisc = 0;
	int colourSamples = 0;
	int kValues = 0;
	double medianK = 0.;
	double kShare = 0.;
	double centreError = 0.;
	double ratio = 1.;
	QPointF expected;
	CircleFit fit;
	QString refusal;
};

// The forced full ripple of stage 1: its covered box against the class's
// mask box, both in device pixels, and how much of its eroded interior is
// the style's own ripple colour.
struct MaskReading {
	QRect box;
	QRect expected;
	int covered = 0;
	int interior = 0;
	int own = 0;
	bool boxOk = false;
	bool colourOk = false;
};

// One clocked press on one subject. |pressGlobal| is the subject's
// mapToGlobal(point) read before the press, which is Click's global;
// |cursorBefore| and |cursorAfter| are QCursor::pos() around the whole
// clocked run; |events| are the subject's records made during it.
struct PressLeg {
	QString subject;
	QString leg;
	PressKind kind = PressKind::Plain;
	QPoint point;
	QPoint pressGlobal;
	QPoint cursorBefore;
	QPoint cursorAfter;
	QRect geometryBefore;
	QRect geometryAfter;
	double ratio = 1.;
	ClockedRun run;
	QImage baseline;
	std::array<QImage, kFrameCount> frames;
	std::array<FrameWindow, kFrameCount> windows;
	std::array<DiscReading, kFrameCount> discs;
	std::optional<PressOrigin> reading;
	std::vector<EventRecord> events;
	QString refusal;
	bool ran = false;
};

// One ClickWithPressOrigin call that must be refused: its reading, the
// target's geometry around it and every event the target's own events()
// stream saw during it. For a native-target case |native| is the
// WidgetDescription the reason must name, |nativeRole| whether that widget
// is the target itself or a descendant, and |nativeCreated| whether it or
// its container ever got a platform window (it must not).
struct RefusalLeg {
	QString name;
	PressOriginRefusal expected = PressOriginRefusal::None;
	PressOrigin reading;
	QString native;
	QString nativeRole;
	bool nativeCreated = false;
	QRect geometryBefore;
	QRect geometryAfter;
	std::vector<EventRecord> events;
};

struct GateReading {
	bool ok = false;
	QString details;
};

// Leaked (see AppendPressOriginSelfTest). |legs| is a deque so a reference
// to a kept leg survives later legs being appended. |doomedEvents| is the
// doomed probe's own events() record, kept here because the probe dies in
// stage 6, and |doomedReading| that stage's reading of its press.
struct State {
	std::unique_ptr<Styles> styles;
	base::unique_qptr<Ui::RpWidget> window;
	base::unique_qptr<RippleProbe> doomed;
	std::array<Subject, kSubjectCount> subjects;
	std::deque<PressLeg> legs;
	std::vector<EventRecord> doomedEvents;
	std::optional<PressOrigin> doomedReading;
	QPoint startCursor;
	QPoint guard;
	QRect screen;
	QRect placement;
	double placementDistance = 0.;
	int side = 0;
	int gap = 0;
	int clearance = 0;
	QString buildRefusal;
	QString gate;
	bool built = false;
};

RippleProbe::RippleProbe(
	QWidget *parent,
	const style::RippleAnimation &st,
	QColor background)
: RippleButton(parent, st)
, _background(background) {
}

void RippleProbe::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(rect(), _background);
	paintRipple(p, 0, 0);
}

Styles::Styles(int side, int area)
: background(BackgroundColor())
, roundOver(RoundOverColor())
, probeRipple(ProbeRippleColor())
, roundRipple(RoundRippleColor())
, iconRipple(IconRippleColor())
, probe(OwnRipple(probeRipple.color()))
, round(st::defaultActiveButton)
, icon(st::defaultIconButton) {
	round.textBg = background.color();
	round.textBgOver = roundOver.color();
	round.width = side;
	round.height = side;
	round.padding = style::margins();
	round.ripple = OwnRipple(roundRipple.color());

	icon.width = side;
	icon.height = side;
	icon.duration = 0;
	icon.rippleAreaSize = area;
	icon.rippleAreaPosition = style::point(
		(side - area) / 2,
		(side - area) / 2);
	icon.ripple = OwnRipple(iconRipple.color());
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString NumberText(double value) {
	return QString::number(value, 'f', 2);
}

[[nodiscard]] QString PointText(QPoint point) {
	return u"%1,%2"_q.arg(point.x()).arg(point.y());
}

[[nodiscard]] QString PointFText(QPointF point) {
	return u"%1,%2"_q.arg(NumberText(point.x()), NumberText(point.y()));
}

[[nodiscard]] QString GateText() {
	return u"press origin self-test: fixture gate: a never-shown "
		u"harness-owned window holds the three S x S ripple buttons, every "
		u"one at least S/2 from the real cursor, with animations enabled"_q;
}

[[nodiscard]] bool IsMouse(QEvent::Type type) {
	return (type == QEvent::MouseButtonPress)
		|| (type == QEvent::MouseButtonRelease)
		|| (type == QEvent::MouseButtonDblClick)
		|| (type == QEvent::MouseMove);
}

[[nodiscard]] QString EventTypeName(QEvent::Type type) {
	switch (type) {
	case QEvent::MouseButtonPress: return u"press"_q;
	case QEvent::MouseButtonRelease: return u"release"_q;
	case QEvent::MouseButtonDblClick: return u"double-click"_q;
	case QEvent::MouseMove: return u"mouse-move"_q;
	case QEvent::Enter: return u"enter"_q;
	case QEvent::Leave: return u"leave"_q;
	case QEvent::Move: return u"move"_q;
	default: break;
	}
	return u"event-%1"_q.arg(int(type));
}

[[nodiscard]] QString EventText(const EventRecord &record) {
	const auto where = u"pos=%1 seen=%2"_q.arg(
		PointText(record.pos),
		PointText(record.seen));
	return IsMouse(record.type)
		? u"%1 local=%2 global=%3 %4"_q.arg(
			EventTypeName(record.type),
			PointText(record.local),
			PointText(record.global),
			where)
		: u"%1 %2"_q.arg(EventTypeName(record.type), where);
}

[[nodiscard]] QString EventsText(const std::vector<EventRecord> &events) {
	auto parts = QStringList();
	for (const auto &record : events) {
		parts.push_back(EventText(record));
	}
	return parts.isEmpty() ? u"none"_q : parts.join(u"; "_q);
}

void RecordEvent(
		std::vector<EventRecord> &events,
		not_null<QWidget*> widget,
		not_null<QEvent*> e) {
	const auto type = e->type();
	if (!ranges::contains(kRecordedEvents, type)) {
		return;
	}
	auto record = EventRecord{
		.type = type,
		.pos = widget->pos(),
		.seen = widget->mapFromGlobal(QCursor::pos()),
	};
	if (IsMouse(type)) {
		const auto mouse = static_cast<QMouseEvent*>(e.get());
		record.local = mouse->position().toPoint();
		record.global = mouse->globalPosition().toPoint();
	}
	events.push_back(record);
}

// On the button's own lifetime: the records stop with the button.
void RecordEvents(not_null<Subject*> subject) {
	const auto button = subject->button.data();
	if (!button) {
		return;
	}
	button->events() | rpl::on_next([=](not_null<QEvent*> e) {
		RecordEvent(subject->events, button, e);
	}, button->lifetime());
}

[[nodiscard]] QRect GlobalRect(not_null<QWidget*> widget) {
	return QRect(widget->mapToGlobal(QPoint()), widget->size());
}

[[nodiscard]] double DistanceToRect(QPoint point, const QRect &rect) {
	if (rect.isEmpty()) {
		return 0.;
	}
	const auto dx = std::max({
		rect.left() - point.x(),
		0,
		point.x() - rect.right() });
	const auto dy = std::max({
		rect.top() - point.y(),
		0,
		point.y() - rect.bottom() });
	return std::hypot(double(dx), double(dy));
}

// The corner of |available| where the nearest button lies farthest from
// |cursor|.
[[nodiscard]] Placement PlaceWindow(
		const QRect &available,
		QSize size,
		const std::array<QPoint, kSubjectCount> &positions,
		int side,
		QPoint cursor) {
	const auto right = available.x() + available.width() - size.width();
	const auto bottom = available.y() + available.height() - size.height();
	const auto corners = std::array{
		available.topLeft(),
		QPoint(right, available.y()),
		QPoint(available.x(), bottom),
		QPoint(right, bottom),
	};
	auto result = Placement{ .distance = -1. };
	for (const auto &corner : corners) {
		auto nearest = std::numeric_limits<double>::max();
		for (const auto &position : positions) {
			nearest = std::min(
				nearest,
				DistanceToRect(
					cursor,
					QRect(corner + position, QSize(side, side))));
		}
		if (nearest > result.distance) {
			result = Placement{ .topLeft = corner, .distance = nearest };
		}
	}
	return result;
}

[[nodiscard]] int PixelIndex(const PixelMap &map, int x, int y) {
	return y * map.width + x;
}

[[nodiscard]] bool At(const PixelMap &map, int x, int y) {
	return (x >= 0)
		&& (y >= 0)
		&& (x < map.width)
		&& (y < map.height)
		&& (map.set[PixelIndex(map, x, y)] != 0);
}

// Pixels where |image| differs from |baseline| by more than |threshold| on
// some channel. Empty when the two do not have the same size.
[[nodiscard]] PixelMap DeltaMap(
		const QImage &image,
		const QImage &baseline,
		int threshold) {
	auto result = PixelMap();
	if (image.isNull() || image.size() != baseline.size()) {
		return result;
	}
	result.width = image.width();
	result.height = image.height();
	result.set.resize(std::size_t(result.width) * result.height);
	for (auto y = 0; y != result.height; ++y) {
		for (auto x = 0; x != result.width; ++x) {
			const auto delta = ChannelDelta(
				image.pixelColor(x, y),
				baseline.pixelColor(x, y));
			result.set[PixelIndex(result, x, y)] = (delta > threshold)
				? 1
				: 0;
		}
	}
	return result;
}

// Pixels whose whole (2 * margin + 1) square neighbourhood is set.
[[nodiscard]] PixelMap Erode(const PixelMap &map, int margin) {
	auto result = PixelMap{
		.width = map.width,
		.height = map.height,
		.set = std::vector<char>(map.set.size(), 0),
	};
	for (auto y = margin; y < map.height - margin; ++y) {
		for (auto x = margin; x < map.width - margin; ++x) {
			auto all = true;
			for (auto dy = -margin; all && dy <= margin; ++dy) {
				for (auto dx = -margin; all && dx <= margin; ++dx) {
					all = At(map, x + dx, y + dy);
				}
			}
			result.set[PixelIndex(result, x, y)] = all ? 1 : 0;
		}
	}
	return result;
}

// Pixels of |image| farther than kColourSame from |color| on some channel.
[[nodiscard]] int CountOff(const QImage &image, QColor color) {
	auto result = 0;
	for (auto y = 0; y != image.height(); ++y) {
		for (auto x = 0; x != image.width(); ++x) {
			if (ChannelDelta(image.pixelColor(x, y), color) > kColourSame) {
				++result;
			}
		}
	}
	return result;
}

[[nodiscard]] QRect DeviceRect(const QRect &logical, double ratio) {
	return QRectF(
		logical.x() * ratio,
		logical.y() * ratio,
		logical.width() * ratio,
		logical.height() * ratio).toRect();
}

[[nodiscard]] MaskReading ReadMask(const Subject &subject) {
	auto result = MaskReading();
	const auto &mask = subject.mask;
	const auto &baseline = subject.baseline;
	if (mask.isNull() || mask.size() != baseline.size()) {
		return result;
	}
	result.expected = DeviceRect(
		QRect(subject.maskOrigin, subject.maskSize),
		mask.devicePixelRatio());
	const auto covered = DeltaMap(mask, baseline, kChangedDelta);
	const auto interior = Erode(covered, kMaskMargin);
	auto left = covered.width;
	auto top = covered.height;
	auto right = -1;
	auto bottom = -1;
	for (auto y = 0; y != covered.height; ++y) {
		for (auto x = 0; x != covered.width; ++x) {
			if (At(covered, x, y)) {
				++result.covered;
				left = std::min(left, x);
				top = std::min(top, y);
				right = std::max(right, x);
				bottom = std::max(bottom, y);
			}
			if (At(interior, x, y)) {
				++result.interior;
				const auto delta = ChannelDelta(
					mask.pixelColor(x, y),
					subject.ripple);
				if (delta <= kColourSame) {
					++result.own;
				}
			}
		}
	}
	if (result.covered > 0) {
		result.box = QRect(QPoint(left, top), QPoint(right, bottom));
	}
	const auto withinSlack = [](int a, int b) {
		return std::abs(a - b) <= kMaskBoxSlack;
	};
	result.boxOk = (result.covered > 0)
		&& withinSlack(result.box.left(), result.expected.left())
		&& withinSlack(result.box.top(), result.expected.top())
		&& withinSlack(result.box.right(), result.expected.right())
		&& withinSlack(result.box.bottom(), result.expected.bottom());
	result.colourOk = (result.interior > 0)
		&& (result.own >= kMaskColourShare * result.interior);
	return result;
}

[[nodiscard]] QString MaskText(const MaskReading &reading, QColor ripple) {
	const auto share = (reading.interior > 0)
		? (double(reading.own) / reading.interior)
		: 0.;
	return u"covered=%1 box=%2 expected=%3 (slack %4) interior=%5 own=%6 "
		u"share=%7 (need %8) rippleColour=%9"_q
		.arg(reading.covered)
		.arg(reading.covered ? RectText(reading.box) : u"none"_q)
		.arg(RectText(reading.expected))
		.arg(kMaskBoxSlack)
		.arg(reading.interior)
		.arg(reading.own)
		.arg(NumberText(share))
		.arg(NumberText(kMaskColourShare))
		.arg(ripple.name());
}

// Gaussian elimination with partial pivoting on an augmented 3 x 4 system;
// nothing when the system is near singular (collinear points).
[[nodiscard]] std::optional<std::array<double, 3>> Solve3(
		std::array<std::array<double, 4>, 3> m) {
	auto scale = 0.;
	for (const auto &row : m) {
		for (auto column = 0; column != 3; ++column) {
			scale = std::max(scale, std::abs(row[column]));
		}
	}
	if (scale <= 0.) {
		return std::nullopt;
	}
	for (auto column = 0; column != 3; ++column) {
		auto pivot = column;
		for (auto row = column + 1; row != 3; ++row) {
			if (std::abs(m[row][column]) > std::abs(m[pivot][column])) {
				pivot = row;
			}
		}
		if (std::abs(m[pivot][column]) < kSingularShare * scale) {
			return std::nullopt;
		}
		std::swap(m[column], m[pivot]);
		for (auto row = column + 1; row != 3; ++row) {
			const auto factor = m[row][column] / m[column][column];
			for (auto j = column; j != 4; ++j) {
				m[row][j] -= factor * m[column][j];
			}
		}
	}
	auto result = std::array<double, 3>();
	for (auto row = 2; row >= 0; --row) {
		auto sum = m[row][3];
		for (auto j = row + 1; j != 3; ++j) {
			sum -= m[row][j] * result[j];
		}
		result[row] = sum / m[row][row];
	}
	return result;
}

// Kasa least squares: the circle x^2 + y^2 + D x + E y + F = 0 closest to
// the points in the algebraic sense, solved in coordinates centred on the
// points' mean so the normal equations stay well conditioned.
[[nodiscard]] CircleFit FitCircle(const std::vector<QPointF> &points) {
	auto result = CircleFit();
	result.points = int(points.size());
	if (result.points < 3) {
		result.refusal = u"fewer than three edge pixels"_q;
		return result;
	}
	auto meanX = 0.;
	auto meanY = 0.;
	for (const auto &point : points) {
		meanX += point.x();
		meanY += point.y();
	}
	meanX /= result.points;
	meanY /= result.points;
	auto system = std::array<std::array<double, 4>, 3>();
	for (const auto &point : points) {
		const auto x = point.x() - meanX;
		const auto y = point.y() - meanY;
		const auto row = std::array<double, 3>{ x, y, 1. };
		const auto squared = x * x + y * y;
		for (auto i = 0; i != 3; ++i) {
			for (auto j = 0; j != 3; ++j) {
				system[i][j] += row[i] * row[j];
			}
			system[i][3] -= row[i] * squared;
		}
	}
	const auto solution = Solve3(system);
	if (!solution) {
		result.refusal = u"the normal equations are singular (the edge "
			u"pixels are collinear)"_q;
		return result;
	}
	const auto cx = -(*solution)[0] / 2.;
	const auto cy = -(*solution)[1] / 2.;
	const auto squared = cx * cx + cy * cy - (*solution)[2];
	if (squared <= 0.) {
		result.refusal = u"the fitted radius is not real"_q;
		return result;
	}
	result.solved = true;
	result.cx = cx + meanX;
	result.cy = cy + meanY;
	result.r = std::sqrt(squared);
	auto residuals = 0.;
	for (const auto &point : points) {
		const auto distance = std::hypot(
			point.x() - result.cx,
			point.y() - result.cy);
		residuals += (distance - result.r) * (distance - result.r);
	}
	result.rms = std::sqrt(residuals / result.points);
	result.ok = (result.points >= kMinArcPixels)
		&& (result.rms <= kMaxFitRms);
	return result;
}

// Ui::RippleAnimation::Ripple's own target radius, in logical pixels: the
// distance from the origin to the mask's farthest corner, rounded.
[[nodiscard]] int RadiusTo(QPoint origin, QSize mask) {
	const auto corners = std::array{
		QPoint(0, 0),
		QPoint(mask.width(), 0),
		QPoint(mask.width(), mask.height()),
		QPoint(0, mask.height()),
	};
	auto squared = 0;
	for (const auto &corner : corners) {
		const auto delta = origin - corner;
		squared = std::max(squared, QPoint::dotProduct(delta, delta));
	}
	return int(std::round(std::sqrt(double(squared))));
}

// The ripple's radius |elapsed| ms into its show: anim::easeOutQuint,
// 1 - (1 - x)^5, written out so the oracle does not call the code it judges.
[[nodiscard]] double ShownRadius(
		int radiusTo,
		crl::time elapsed,
		double duration) {
	const auto progress = std::clamp(double(elapsed) / duration, 0., 1.);
	return radiusTo * (1. - std::pow(1. - progress, 5));
}

// The ripple's opacity |elapsed| ms into the linear hide its release starts.
[[nodiscard]] double HiddenOpacity(crl::time elapsed, double duration) {
	return std::clamp(1. - double(elapsed) / duration, 0., 1.);
}

[[nodiscard]] const ClockedFrame *FindFrame(
		const ClockedRun &run,
		int index) {
	const auto i = ranges::find(run.frames, index, &ClockedFrame::index);
	return (i != end(run.frames)) ? &*i : nullptr;
}

// Both the show and the hide start inside the clocked action, so at a tick
// each has run for some time within the frame's reached bounds [lo, hi]:
// the radius lies in [R(lo), R(hi)] and the opacity in [O(hi), O(lo)].
[[nodiscard]] FrameWindow ComputeWindow(
		const Subject &subject,
		QPoint point,
		const ClockedFrame *frame,
		double ratio) {
	auto result = FrameWindow();
	result.radiusTo = RadiusTo(point - subject.maskOrigin, subject.maskSize);
	if (!frame || !frame->taken()) {
		return result;
	}
	const auto slow = anim::SlowMultiplier();
	const auto show = double(subject.showDuration) * slow;
	const auto hide = double(subject.hideDuration) * slow;
	result.radiusLo = ShownRadius(result.radiusTo, frame->reachedLo, show);
	result.radiusHi = ShownRadius(result.radiusTo, frame->reachedHi, show);
	result.opacityLo = HiddenOpacity(frame->reachedHi, hide);
	result.opacityHi = HiddenOpacity(frame->reachedLo, hide);
	result.centreTolerance = std::max(
		kCentreFloor,
		kCentreShare * result.radiusHi * ratio);
	result.valid = true;
	return result;
}

// Interior pixels are the ripple colour composited over the baseline at the
// ripple's opacity, so on each channel the mask reading separates well from
// the baseline, (frame - baseline) / (mask - baseline) is that opacity.
void AppendOpacities(
		QColor frame,
		QColor baseline,
		QColor mask,
		std::vector<double> &values) {
	const auto channels = std::array{
		std::array{ frame.red(), baseline.red(), mask.red() },
		std::array{ frame.green(), baseline.green(), mask.green() },
		std::array{ frame.blue(), baseline.blue(), mask.blue() },
	};
	for (const auto &[f, b, m] : channels) {
		const auto full = m - b;
		if (std::abs(full) >= kColourChannelFloor) {
			values.push_back(double(f - b) / full);
		}
	}
}

[[nodiscard]] DiscReading ReadDisc(
		const QImage &frame,
		const QImage &baseline,
		const QImage &mask,
		QPoint press,
		const FrameWindow &window) {
	auto result = DiscReading();
	if (frame.isNull()
		|| (baseline.size() != frame.size())
		|| (mask.size() != frame.size())) {
		result.refusal = u"unreadable: frame %1x%2, baseline %3x%4, mask "
			u"%5x%6"_q
			.arg(frame.width())
			.arg(frame.height())
			.arg(baseline.width())
			.arg(baseline.height())
			.arg(mask.width())
			.arg(mask.height());
		return result;
	}
	result.readable = true;
	result.ratio = frame.devicePixelRatio();
	result.expected = QPointF(press) * result.ratio;
	const auto changed = DeltaMap(frame, baseline, kChangedDelta);
	const auto interior = Erode(
		DeltaMap(mask, baseline, kChangedDelta),
		kMaskMargin);
	auto edges = std::vector<QPointF>();
	for (auto y = 0; y != changed.height; ++y) {
		for (auto x = 0; x != changed.width; ++x) {
			if (!At(changed, x, y)) {
				continue;
			}
			++result.changedCount;
			if (At(interior, x, y)
				&& (!At(changed, x - 1, y)
					|| !At(changed, x + 1, y)
					|| !At(changed, x, y - 1)
					|| !At(changed, x, y + 1))) {
				edges.emplace_back(x + 0.5, y + 0.5);
			}
		}
	}
	result.edgeCount = int(edges.size());
	result.pressCovered = At(
		changed,
		int(std::floor(result.expected.x())),
		int(std::floor(result.expected.y())));
	result.fit = FitCircle(edges);
	if (!result.fit.solved) {
		return result;
	}
	const auto &fit = result.fit;
	result.centreError = std::hypot(
		fit.cx - result.expected.x(),
		fit.cy - result.expected.y());
	const auto outside = fit.r + kOutsideSlack * result.ratio;
	const auto inner = fit.r - kInteriorInset * result.ratio;
	auto values = std::vector<double>();
	for (auto y = 0; y != changed.height; ++y) {
		for (auto x = 0; x != changed.width; ++x) {
			if (!At(changed, x, y)) {
				continue;
			}
			const auto distance = std::hypot(
				x + 0.5 - fit.cx,
				y + 0.5 - fit.cy);
			if (distance > outside) {
				++result.outsideDisc;
			} else if (distance <= inner && At(interior, x, y)) {
				++result.colourSamples;
				AppendOpacities(
					frame.pixelColor(x, y),
					baseline.pixelColor(x, y),
					mask.pixelColor(x, y),
					values);
			}
		}
	}
	result.kValues = int(values.size());
	if (!values.empty()) {
		ranges::sort(values);
		result.medianK = values[values.size() / 2];
		const auto lo = window.opacityLo - kOpacitySlack;
		const auto hi = window.opacityHi + kOpacitySlack;
		const auto inside = ranges::count_if(values, [&](double value) {
			return (value >= lo) && (value <= hi);
		});
		result.kShare = double(inside) / values.size();
	}
	return result;
}

// A disc centred at the press point: a valid fit within the frame's centre
// tolerance of it, with the press pixel itself changed.
[[nodiscard]] bool DiscAt(
		const DiscReading &disc,
		const FrameWindow &window) {
	return window.valid
		&& disc.readable
		&& disc.fit.ok
		&& disc.pressCovered
		&& (disc.centreError <= window.centreTolerance);
}

[[nodiscard]] bool RadiusOk(
		const DiscReading &disc,
		const FrameWindow &window) {
	const auto radius = disc.fit.r / disc.ratio;
	return window.valid
		&& disc.fit.ok
		&& (radius >= window.radiusLo - kRadiusSlack)
		&& (radius <= window.radiusHi + kRadiusSlack);
}

[[nodiscard]] bool ColourOk(
		const DiscReading &disc,
		const FrameWindow &window) {
	return window.valid
		&& disc.fit.ok
		&& (disc.colourSamples >= kMinInteriorPixels)
		&& (disc.medianK >= window.opacityLo - kOpacitySlack)
		&& (disc.medianK <= window.opacityHi + kOpacitySlack)
		&& (disc.kShare >= kOpacityShare);
}

[[nodiscard]] QString FitText(const CircleFit &fit, double ratio) {
	if (!fit.solved) {
		return u"fit{none points=%1: %2}"_q.arg(
			QString::number(fit.points),
			fit.refusal);
	}
	return u"fit{ok=%1 points=%2 centre=%3 r=%4 (logical %5) rms=%6}"_q.arg(
		FlagText(fit.ok),
		QString::number(fit.points),
		PointFText(QPointF(fit.cx, fit.cy)),
		NumberText(fit.r),
		NumberText(fit.r / ratio),
		NumberText(fit.rms));
}

[[nodiscard]] QString DiscText(
		const DiscReading &disc,
		const FrameWindow &window) {
	if (!disc.readable) {
		return u"disc=none (%1)"_q.arg(disc.refusal);
	}
	const auto colour = u"colour{samples=%1 k=%2 median=%3 share=%4 "
		u"window=[%5,%6]}"_q.arg(
			QString::number(disc.colourSamples),
			QString::number(disc.kValues),
			NumberText(disc.medianK),
			NumberText(disc.kShare),
			NumberText(window.opacityLo - kOpacitySlack),
			NumberText(window.opacityHi + kOpacitySlack));
	const auto parts = QStringList{
		u"changed=%1"_q.arg(disc.changedCount),
		u"edges=%1"_q.arg(disc.edgeCount),
		FitText(disc.fit, disc.ratio),
		u"expectedCentre=%1"_q.arg(PointFText(disc.expected)),
		u"centreError=%1"_q.arg(disc.fit.solved
			? NumberText(disc.centreError)
			: u"none"_q),
		u"tolerance=%1"_q.arg(NumberText(window.centreTolerance)),
		u"pressCovered=%1"_q.arg(FlagText(disc.pressCovered)),
		u"radiusWindow=[%1,%2]"_q.arg(
			NumberText(window.radiusLo - kRadiusSlack),
			NumberText(window.radiusHi + kRadiusSlack)),
		u"radiusTo=%1"_q.arg(window.radiusTo),
		u"outsideDisc=%1"_q.arg(disc.outsideDisc),
		colour,
		u"ratio=%1"_q.arg(NumberText(disc.ratio)),
	};
	return parts.join(u' ');
}

[[nodiscard]] QString FrameDetails(const PressLeg &leg, int index) {
	const auto frame = FindFrame(leg.run, index);
	return u"~%1 ms{%2 %3}"_q.arg(
		QString::number(kFrameTimes[index]),
		frame ? ClockedFrameText(*frame) : u"frame=none"_q,
		DiscText(leg.discs[index], leg.windows[index]));
}

[[nodiscard]] QString LegDetails(const PressLeg &leg) {
	auto parts = QStringList{
		u"leg=%1"_q.arg(leg.leg),
		u"point=%1"_q.arg(PointText(leg.point)),
		u"cursor=%1->%2"_q.arg(
			PointText(leg.cursorBefore),
			PointText(leg.cursorAfter)),
	};
	if (!leg.refusal.isEmpty()) {
		parts.push_back(u"refused=%1"_q.arg(leg.refusal));
	}
	for (auto index = 0; index != kFrameCount; ++index) {
		parts.push_back(FrameDetails(leg, index));
	}
	return parts.join(u"; "_q);
}

void ReadFrames(const Subject &subject, not_null<PressLeg*> leg) {
	leg->ratio = leg->baseline.isNull()
		? 1.
		: leg->baseline.devicePixelRatio();
	for (auto index = 0; index != kFrameCount; ++index) {
		const auto frame = FindFrame(leg->run, index);
		leg->windows[index] = ComputeWindow(
			subject,
			leg->point,
			frame,
			leg->ratio);
		leg->discs[index] = ReadDisc(
			leg->frames[index],
			leg->baseline,
			subject.mask,
			leg->point,
			leg->windows[index]);
	}
}

void SaveLegFrames(const PressLeg &leg) {
	for (auto index = 0; index != kFrameCount; ++index) {
		if (!leg.frames[index].isNull()) {
			SaveImage(
				leg.frames[index],
				u"press_origin_%1_%2_%3"_q.arg(
					leg.subject,
					leg.leg,
					QString::number(kFrameTimes[index])));
		}
	}
}

// One clocked press: the ripple finished, a fresh baseline, the cursor
// read, the press inside Test::RunClockedFrames with its two frames grabbed
// synchronously, the cursor read again; then the frames are read against
// the subject's stage 1 mask and saved - never inside the render.
[[nodiscard]] PressLeg RunPress(
		const State &state,
		not_null<Subject*> subject,
		const QString &leg,
		PressKind kind,
		QPoint point) {
	auto result = PressLeg{
		.subject = subject->name,
		.leg = leg,
		.kind = kind,
		.point = point,
	};
	const auto button = subject->button.data();
	const auto window = state.window.get();
	if (!button || !window) {
		result.refusal = u"the button or the fixture window is gone"_q;
		return result;
	}
	button->finishAnimating();
	result.geometryBefore = button->geometry();
	result.baseline = GrabRect(window, result.geometryBefore);
	result.pressGlobal = button->mapToGlobal(point);
	const auto eventsBefore = subject->events.size();
	const auto weak = QPointer<Ui::RippleButton>(button);
	const auto slow = anim::SlowMultiplier();
	auto elapsed = std::vector<crl::time>();
	for (const auto time : kFrameTimes) {
		elapsed.push_back(time * slow);
	}
	result.cursorBefore = QCursor::pos();
	result.run = RunClockedFrames({
		.name = u"press origin %1: %2"_q.arg(leg, subject->name),
		.action = [&] {
			const auto alive = weak.data();
			if (!alive) {
				return;
			} else if (kind == PressKind::Facility) {
				result.reading = ClickWithPressOrigin(alive, point);
			} else {
				Click(alive, point);
			}
		},
		.elapsed = elapsed,
		.animationEnd = subject->hideDuration * slow,
		.render = [&](const ClockedFrame &frame) {
			const auto alive = weak.data();
			if (alive && frame.index >= 0 && frame.index < kFrameCount) {
				result.frames[frame.index] = GrabRect(
					window,
					alive->geometry());
			}
		},
	});
	result.cursorAfter = QCursor::pos();
	if (const auto alive = weak.data()) {
		result.geometryAfter = alive->geometry();
	}
	result.events.assign(
		subject->events.begin() + std::ptrdiff_t(eventsBefore),
		subject->events.end());
	result.ran = true;
	ReadFrames(*subject, &result);
	SaveLegFrames(result);
	return result;
}

[[nodiscard]] Subject MakeSubject(
		QString name,
		not_null<Ui::RippleButton*> button,
		const style::RippleAnimation &ripple,
		QPoint press,
		const QRect &mask) {
	return Subject{
		.name = std::move(name),
		.button = button.get(),
		.press = press,
		.maskOrigin = mask.topLeft(),
		.maskSize = mask.size(),
		.ripple = ripple.color->c,
		.showDuration = ripple.showDuration,
		.hideDuration = ripple.hideDuration,
		.initialGeometry = button->geometry(),
		.initialMoved = button->testAttribute(Qt::WA_Moved),
		.initialNoPropagation = button->testAttribute(
			Qt::WA_NoMousePropagation),
		.initialPendingMove = button->testAttribute(
			Qt::WA_PendingMoveEvent),
	};
}

// The doomed probe destroys itself in the drain after its press, through
// the same Ui::PostponeCall route product code uses. Its events are
// recorded into the leaked state, which outlives it.
void SetupDoomed(not_null<State*> state) {
	const auto doomed = state->doomed.get();
	const auto window = state->window.get();
	if (!doomed || !window) {
		return;
	}
	doomed->events() | rpl::on_next([=](not_null<QEvent*> e) {
		RecordEvent(state->doomedEvents, doomed, e);
		if (e->type() == QEvent::MouseButtonPress) {
			Ui::PostponeCall(window, [=] {
				state->doomed = nullptr;
			});
		}
	}, doomed->lifetime());
}

[[nodiscard]] bool BuildFixture(not_null<State*> state) {
	state->side = style::ConvertScale(kSide);
	state->gap = style::ConvertScale(kGap);
	state->clearance = style::ConvertScale(kCursorClearance);
	const auto side = state->side;
	const auto gap = state->gap;
	const auto area = side * 2 / 3;
	state->startCursor = QCursor::pos();
	auto screen = QGuiApplication::screenAt(state->startCursor);
	if (!screen) {
		screen = QGuiApplication::primaryScreen();
	}
	if (!screen) {
		state->buildRefusal = u"no screen: neither one at the cursor nor a "
			u"primary one"_q;
		return false;
	}
	state->screen = screen->availableGeometry();
	const auto positions = std::array{
		QPoint(gap, gap),
		QPoint(2 * gap + side, gap),
		QPoint(3 * gap + 2 * side, gap),
	};
	const auto size = QSize(3 * side + 4 * gap, side + side / 2 + 3 * gap);
	const auto placement = PlaceWindow(
		state->screen,
		size,
		positions,
		side,
		state->startCursor);
	state->placement = QRect(placement.topLeft, size);
	state->placementDistance = placement.distance;

	state->styles = std::make_unique<Styles>(side, area);
	const auto &styles = *state->styles;
	state->window = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto window = state->window.get();
	window->setAttribute(Qt::WA_DontShowOnScreen);
	window->setAttribute(Qt::WA_QuitOnClose, false);

	// Before show(): afterwards QWidgetPrivate::setGeometry_sys no longer
	// moves the QWindow of a Qt::WA_DontShowOnScreen top level.
	window->setGeometry(state->placement);
	window->paintOn([=](QPainter &p) {
		p.fillRect(window->rect(), BackgroundColor());
	});
	const auto probe = Ui::CreateChild<RippleProbe>(
		window,
		styles.probe,
		BackgroundColor());
	const auto round = Ui::CreateChild<Ui::RoundButton>(
		window,
		rpl::single(QString()),
		styles.round);
	const auto icon = Ui::CreateChild<Ui::IconButton>(window, styles.icon);
	const auto buttons = std::array<Ui::RippleButton*, kSubjectCount>{
		probe,
		round,
		icon,
	};
	for (auto index = 0; index != kSubjectCount; ++index) {
		buttons[index]->setGeometry(
			QRect(positions[index], QSize(side, side)));
		buttons[index]->show();
	}
	state->doomed = base::make_unique_q<RippleProbe>(
		window,
		styles.probe,
		BackgroundColor());
	state->doomed->setGeometry(gap, 2 * gap + side, side / 2, side / 2);
	state->doomed->show();
	window->show();

	const auto full = QRect(0, 0, side, side);
	const auto areaPosition = styles.icon.rippleAreaPosition;
	state->subjects[kProbeIndex] = MakeSubject(
		u"probe"_q,
		probe,
		styles.probe,
		QPoint(side * 7 / 20, side * 7 / 20),
		full);
	state->subjects[kRoundIndex] = MakeSubject(
		u"round"_q,
		round,
		styles.round.ripple,
		QPoint(side * 13 / 20, side * 7 / 20),
		full);
	state->subjects[kIconIndex] = MakeSubject(
		u"icon"_q,
		icon,
		styles.icon.ripple,
		areaPosition + QPoint(area * 3 / 10, area * 7 / 20),
		QRect(areaPosition, QSize(area, area)));
	state->guard = QPoint(areaPosition.x() / 2, areaPosition.y() / 2);
	for (auto &subject : state->subjects) {
		RecordEvents(&subject);
	}
	SetupDoomed(state);
	return true;
}

[[nodiscard]] GateReading ReadGate(const State &state) {
	const auto window = state.window.get();
	const auto handle = window ? window->windowHandle() : nullptr;
	const auto centre = QPointF(state.side / 2., state.side / 2.);
	auto sizesOk = (window != nullptr);
	auto pressesOk = true;
	auto subjects = QStringList();
	for (const auto &subject : state.subjects) {
		const auto button = subject.button.data();
		const auto size = button ? button->size() : QSize();
		const auto offCentre = std::hypot(
			subject.press.x() - centre.x(),
			subject.press.y() - centre.y());
		sizesOk = sizesOk && (size == QSize(state.side, state.side));
		pressesOk = pressesOk && (offCentre >= state.side / 10.);
		subjects.push_back(u"%1{size=%2x%3 press=%4 offCentre=%5 "
			u"mask=%6}"_q.arg(
				subject.name,
				QString::number(size.width()),
				QString::number(size.height()),
				PointText(subject.press),
				NumberText(offCentre),
				RectText(QRect(subject.maskOrigin, subject.maskSize))));
	}
	const auto disabled = anim::Disabled();
	const auto placed = state.built
		&& (state.placementDistance >= state.clearance);
	auto result = GateReading();
	result.ok = state.built
		&& sizesOk
		&& handle
		&& !handle->isVisible()
		&& !disabled
		&& placed
		&& pressesOk;
	auto parts = QStringList{
		u"built=%1"_q.arg(FlagText(state.built)),
		u"window=%1"_q.arg(window ? WidgetDescription(window) : u"none"_q),
		u"windowHandle=%1"_q.arg(FlagText(handle != nullptr)),
		u"windowVisible=%1"_q.arg(FlagText(handle && handle->isVisible())),
		u"animationsDisabled=%1"_q.arg(FlagText(disabled)),
		u"slowMultiplier=%1"_q.arg(anim::SlowMultiplier()),
		u"ratio=%1"_q.arg(style::DevicePixelRatio()),
		u"scale=%1"_q.arg(style::Scale()),
		u"side=%1 gap=%2 clearance=%3"_q
			.arg(state.side)
			.arg(state.gap)
			.arg(state.clearance),
		u"screen=%1"_q.arg(RectText(state.screen)),
		u"startCursor=%1"_q.arg(PointText(state.startCursor)),
		u"placement=%1"_q.arg(RectText(state.placement)),
		u"nearestButton=%1 (need %2)"_q.arg(
			NumberText(state.placementDistance),
			QString::number(state.clearance)),
		u"guard=%1"_q.arg(PointText(state.guard)),
		subjects.join(u' '),
	};
	if (!state.buildRefusal.isEmpty()) {
		parts.push_back(u"refused=%1"_q.arg(state.buildRefusal));
	}
	result.details = parts.join(u' ');
	return result;
}

[[nodiscard]] QString FixtureSkipReason(const State &state) {
	if (!state.gate.isEmpty()) {
		return state.gate;
	} else if (!state.window) {
		return u"press origin self-test: fixture gate: the fixture window "
			u"was destroyed before this stage"_q;
	}
	for (const auto &subject : state.subjects) {
		if (!subject.button) {
			return u"press origin self-test: fixture gate: the %1 button "
				u"was destroyed before this stage"_q.arg(subject.name);
		}
	}
	return QString();
}

[[nodiscard]] QString FrameSkipReason(const State &state) {
	const auto fixture = FixtureSkipReason(state);
	if (!fixture.isEmpty()) {
		return fixture;
	} else if (anim::Disabled()) {
		return u"press origin self-test: fixture gate: animations are "
			u"disabled, so the first tick would jump every ripple to its "
			u"end"_q;
	}
	return QString();
}

void CheckCursorOutside(const State &state, const Subject &subject) {
	const auto button = subject.button.data();
	const auto global = button ? GlobalRect(button) : QRect();
	const auto distance = DistanceToRect(state.startCursor, global);
	Check(
		button
			&& !global.contains(state.startCursor)
			&& (distance >= state.clearance),
		u"press origin self-test: fixture: the real cursor lies outside the "
		u"%1 button, at least S/2 from it"_q.arg(subject.name),
		u"cursor=%1 button=%2 distance=%3 need=%4"_q.arg(
			PointText(state.startCursor),
			RectText(global),
			NumberText(distance),
			QString::number(state.clearance)));
}

// The baseline before any input, and the forced full ripple: the class's
// own mask, painted at opacity 1 in the style's own colour.
void ReadReferenceImages(const State &state, not_null<Subject*> subject) {
	const auto button = subject->button.data();
	const auto window = state.window.get();
	if (!button || !window) {
		return;
	}
	button->finishAnimating();
	subject->baseline = GrabRect(window, button->geometry());
	const auto off = CountOff(subject->baseline, BackgroundColor());
	Check(
		!subject->baseline.isNull() && (off == 0),
		u"press origin self-test: fixture: the %1 button's baseline is "
		u"uniformly %2"_q.arg(subject->name, BackgroundColor().name()),
		u"image=%1x%2 ratio=%3 pixelsOff=%4 (tolerance %5 per channel)"_q
			.arg(subject->baseline.width())
			.arg(subject->baseline.height())
			.arg(NumberText(subject->baseline.devicePixelRatio()))
			.arg(off)
			.arg(kColourSame));

	button->setForceRippled(true, anim::type::instant);
	subject->mask = GrabRect(window, button->geometry());
	button->setForceRippled(false, anim::type::instant);
	button->finishAnimating();
	const auto mask = ReadMask(*subject);
	Check(
		mask.boxOk && mask.colourOk,
		u"press origin self-test: fixture: the %1 button's forced full "
		u"ripple covers exactly the class's mask in the style's own ripple "
		u"colour"_q.arg(subject->name),
		MaskText(mask, subject->ripple));
	SaveImage(
		subject->baseline,
		u"press_origin_%1_baseline"_q.arg(subject->name));
	SaveImage(subject->mask, u"press_origin_%1_mask"_q.arg(subject->name));
}

void CheckNoDisc(
		const Subject &subject,
		const PressLeg &leg,
		const QString &stage) {
	auto ok = leg.ran;
	for (auto index = 0; index != kFrameCount; ++index) {
		const auto frame = FindFrame(leg.run, index);
		const auto taken = frame
			&& frame->taken()
			&& leg.discs[index].readable;
		ok = ok && taken && !DiscAt(leg.discs[index], leg.windows[index]);
	}
	Check(
		ok,
		u"press origin self-test: %1: %2: a plain Test::Click at the press "
		u"point shows no disc centred there in either taken frame"_q.arg(
			stage,
			subject.name),
		LegDetails(leg));
}

void CheckPlainCursor(
		const State &state,
		const Subject &subject,
		const PressLeg &leg,
		const QString &stage) {
	const auto button = subject.button.data();
	Check(
		leg.ran
			&& (leg.cursorBefore == state.startCursor)
			&& (leg.cursorAfter == state.startCursor),
		u"press origin self-test: %1: %2: the real cursor read before and "
		u"after the press equals the one read at the start"_q.arg(
			stage,
			subject.name),
		u"before=%1 after=%2 start=%3 button=%4"_q.arg(
			PointText(leg.cursorBefore),
			PointText(leg.cursorAfter),
			PointText(state.startCursor),
			button ? RectText(GlobalRect(button)) : u"none"_q));
}

void CheckFacilityReading(
		const State &state,
		const Subject &subject,
		const PressLeg &leg,
		const QString &stage) {
	const auto reading = leg.reading ? &*leg.reading : nullptr;
	const auto ok = leg.ran
		&& reading
		&& (reading->refusal == PressOriginRefusal::None)
		&& reading->delivered
		&& reading->displaced
		&& (reading->originSeen == leg.point)
		&& reading->restored
		&& !reading->geometryChangedByPress
		&& reading->pressAccepted
		&& reading->targetAlive
		&& reading->cursorStable
		&& (reading->cursorBefore == state.startCursor)
		&& (reading->moveEventsWithheld == kMovesWithheld)
		&& (reading->paintsWhileDisplaced == 0)
		&& (reading->pressGlobal == leg.pressGlobal)
		&& (leg.geometryAfter == subject.initialGeometry);
	Check(
		ok,
		u"press origin self-test: %1: %2: ClickWithPressOrigin pressed with "
		u"the cursor mapped to the point and put the button back"_q.arg(
			stage,
			subject.name),
		u"%1; start=%2 clickGlobal=%3 geometry=%4->%5 initial=%6 "
		u"legCursor=%7->%8 movesWithheldNeeded=%9"_q.arg(
			reading ? PressOriginDetails(*reading) : u"reading=none"_q,
			PointText(state.startCursor),
			PointText(leg.pressGlobal),
			RectText(leg.geometryBefore),
			RectText(leg.geometryAfter),
			RectText(subject.initialGeometry),
			PointText(leg.cursorBefore),
			PointText(leg.cursorAfter),
			QString::number(kMovesWithheld)));
}

// Exactly Click's press, release and leave, and nothing else - no Move
// reached the stream, because the facility withheld both of its own.
void CheckFacilityEvents(
		const Subject &subject,
		const PressLeg &leg,
		const QString &stage) {
	const auto initial = subject.initialGeometry.topLeft();
	const auto displacement = leg.reading
		? leg.reading->displacement
		: QPoint();
	const auto &events = leg.events;
	const auto ok = leg.reading
		&& (events.size() == 3)
		&& (events[0].type == QEvent::MouseButtonPress)
		&& (events[0].local == leg.point)
		&& (events[0].global == leg.pressGlobal)
		&& (events[0].pos == initial + displacement)
		&& (events[0].seen == leg.point)
		&& (events[1].type == QEvent::MouseButtonRelease)
		&& (events[1].local == leg.point)
		&& (events[1].global == leg.pressGlobal)
		&& (events[1].pos == initial)
		&& (events[2].type == QEvent::Leave);
	Check(
		ok,
		u"press origin self-test: %1: %2: the button received exactly "
		u"Click's press, release and leave, the press while displaced with "
		u"the cursor mapped to the point"_q.arg(stage, subject.name),
		u"events=[%1] expected press local=%2 global=%3 pos=%4 seen=%2, "
		u"release local=%2 global=%3 pos=%5, leave"_q.arg(
			EventsText(events),
			PointText(leg.point),
			PointText(leg.pressGlobal),
			PointText(initial + displacement),
			PointText(initial)));
}

void CheckFacilityFrame(
		const Subject &subject,
		const PressLeg &leg,
		int index) {
	const auto frame = FindFrame(leg.run, index);
	const auto &disc = leg.discs[index];
	const auto &window = leg.windows[index];
	const auto taken = leg.ran && frame && frame->taken() && disc.readable;
	const auto what = u"press origin self-test: press-point ripple: %1 at "
		u"~%2 ms: "_q.arg(subject.name, QString::number(kFrameTimes[index]));
	const auto details = FrameDetails(leg, index);
	Check(
		taken && DiscAt(disc, window),
		what + u"the ripple is a disc centred at the press point"_q,
		details);
	Check(
		taken && RadiusOk(disc, window),
		what + u"the disc's radius lies in the anim::easeOutQuint growth "
		u"window at the frame's reached bounds"_q,
		details);
	Check(
		taken && ColourOk(disc, window),
		what + u"the disc's interior is the button's own ripple colour at "
		u"the hide opacity"_q,
		details);
	Check(
		taken && disc.fit.solved && (disc.outsideDisc == 0),
		what + u"nothing changed outside the disc, so no hover highlight "
		u"was painted"_q,
		details);
}

void CheckGrowth(const Subject &subject, const PressLeg &leg) {
	const auto &early = leg.discs[0];
	const auto &late = leg.discs[1];
	const auto growth = (late.fit.r - early.fit.r) / leg.ratio;
	Check(
		leg.ran
			&& early.fit.ok
			&& late.fit.ok
			&& (growth >= kMinGrowth)
			&& (late.changedCount > early.changedCount),
		u"press origin self-test: press-point ripple: %1: the disc grows "
		u"between ~40 and ~120 ms"_q.arg(subject.name),
		u"radius %1 -> %2 logical (growth %3, need %4) changed %5 -> %6"_q
			.arg(NumberText(early.fit.r / leg.ratio))
			.arg(NumberText(late.fit.r / leg.ratio))
			.arg(NumberText(growth))
			.arg(NumberText(kMinGrowth))
			.arg(early.changedCount)
			.arg(late.changedCount));
}

[[nodiscard]] int CountSet(const PixelMap &map) {
	return int(ranges::count(map.set, char(1)));
}

[[nodiscard]] const PressLeg *FindLeg(
		const State &state,
		const QString &subject,
		const QString &leg) {
	const auto i = ranges::find_if(state.legs, [&](const PressLeg &kept) {
		return (kept.subject == subject) && (kept.leg == leg);
	});
	return (i != end(state.legs)) ? &*i : nullptr;
}

[[nodiscard]] QString LegSummary(const PressLeg &leg) {
	auto changed = QStringList();
	auto discs = QStringList();
	for (auto index = 0; index != kFrameCount; ++index) {
		const auto &disc = leg.discs[index];
		changed.push_back(QString::number(disc.changedCount));
		discs.push_back(FlagText(DiscAt(disc, leg.windows[index])));
	}
	return u"%1 leg at %2: changed=[%3] discAt=[%4]"_q.arg(
		leg.leg,
		PointText(leg.point),
		changed.join(u','),
		discs.join(u','));
}

// The class saw Q and refused it, so no ripple started and no pixel of the
// button changed in either frame. The stage 3 IconButton leg - the same
// button and helper at a point inside the ripple area - is the positive
// control printed beside it.
void CheckGuardFrames(
		const Subject &subject,
		const PressLeg &leg,
		const PressLeg *control) {
	auto ok = leg.ran;
	for (auto index = 0; index != kFrameCount; ++index) {
		const auto frame = FindFrame(leg.run, index);
		ok = ok
			&& frame
			&& frame->taken()
			&& leg.discs[index].readable
			&& (leg.discs[index].changedCount == 0);
	}
	Check(
		ok,
		u"press origin self-test: guard kept: %1: a ClickWithPressOrigin "
		u"press outside the ripple area changes no pixel in either taken "
		u"frame, as a pointer press there would"_q.arg(subject.name),
		u"%1; positive control: %2"_q.arg(
			LegDetails(leg),
			control ? LegSummary(*control) : u"none"_q));
}

// After every facility press: the button, its ripple finished, grabs
// exactly as it did before any input, where it stood, with the attributes
// the facility saves and restores as they were. The attributes are read
// before the grab, because a render sends a pending move event and would
// clear a Qt::WA_PendingMoveEvent the facility left behind.
void CheckNothingLeftBehind(const State &state, const Subject &subject) {
	const auto button = subject.button.data();
	const auto window = state.window.get();
	auto image = QImage();
	auto geometry = QRect();
	auto moved = false;
	auto noPropagation = false;
	auto pendingMove = false;
	if (button && window) {
		button->finishAnimating();
		geometry = button->geometry();
		moved = button->testAttribute(Qt::WA_Moved);
		noPropagation = button->testAttribute(Qt::WA_NoMousePropagation);
		pendingMove = button->testAttribute(Qt::WA_PendingMoveEvent);
		image = GrabRect(window, geometry);
	}
	const auto sameSize = !image.isNull()
		&& (image.size() == subject.baseline.size());
	const auto differing = sameSize
		? CountSet(DeltaMap(image, subject.baseline, 0))
		: -1;
	const auto parts = QStringList{
		u"image=%1x%2"_q.arg(image.width()).arg(image.height()),
		u"baseline=%1x%2"_q
			.arg(subject.baseline.width())
			.arg(subject.baseline.height()),
		u"differingPixels=%1"_q.arg(differing),
		u"geometry=%1 initial=%2"_q.arg(
			RectText(geometry),
			RectText(subject.initialGeometry)),
		u"WA_Moved=%1 (initial %2)"_q.arg(
			FlagText(moved),
			FlagText(subject.initialMoved)),
		u"WA_NoMousePropagation=%1 (initial %2)"_q.arg(
			FlagText(noPropagation),
			FlagText(subject.initialNoPropagation)),
		u"WA_PendingMoveEvent=%1 (initial %2)"_q.arg(
			FlagText(pendingMove),
			FlagText(subject.initialPendingMove)),
	};
	Check(
		sameSize
			&& (differing == 0)
			&& (geometry == subject.initialGeometry)
			&& (moved == subject.initialMoved)
			&& (noPropagation == subject.initialNoPropagation)
			&& (pendingMove == subject.initialPendingMove),
		u"press origin self-test: nothing left behind: %1: the button grabs "
		u"identical to its stage 1 baseline, at its initial geometry, with "
		u"its initial Qt::WA_Moved, Qt::WA_NoMousePropagation and "
		u"Qt::WA_PendingMoveEvent"_q.arg(subject.name),
		parts.join(u' '));
}

// The target's own events() stream is recorded for the call only; a
// refusal must leave it empty.
[[nodiscard]] RefusalLeg RunRefusal(
		QString name,
		not_null<Ui::RpWidget*> target,
		std::optional<QPoint> point,
		PressOriginRefusal expected) {
	auto result = RefusalLeg{
		.name = std::move(name),
		.expected = expected,
	};
	auto events = std::vector<EventRecord>();
	auto lifetime = rpl::lifetime();
	target->events() | rpl::on_next([&](not_null<QEvent*> e) {
		RecordEvent(events, target, e);
	}, lifetime);
	result.geometryBefore = target->geometry();
	result.reading = ClickWithPressOrigin(target, point);
	result.geometryAfter = target->geometry();
	lifetime.destroy();
	result.events = std::move(events);
	return result;
}

void CheckRefusal(const RefusalLeg &leg) {
	const auto &reading = leg.reading;
	const auto named = leg.native.isEmpty()
		|| (reading.reason.contains(leg.native)
			&& reading.reason.contains(leg.nativeRole));
	const auto ok = (reading.refusal == leg.expected)
		&& !reading.pressSent
		&& !reading.displaced
		&& !reading.delivered
		&& reading.targetAlive
		&& (reading.moveEventsWithheld == 0)
		&& !reading.reason.isEmpty()
		&& named
		&& !leg.nativeCreated
		&& leg.events.empty()
		&& (leg.geometryAfter == leg.geometryBefore);
	Check(
		ok,
		u"press origin self-test: refusals: %1: ClickWithPressOrigin refuses "
		u"%2 and sends and moves nothing"_q.arg(
			leg.name,
			PressOriginRefusalName(leg.expected)),
		u"%1; events=[%2] geometry=%3->%4 native=%5 role=%6 "
		u"nativeCreated=%7"_q.arg(
			PressOriginDetails(reading),
			EventsText(leg.events),
			RectText(leg.geometryBefore),
			RectText(leg.geometryAfter),
			leg.native.isEmpty() ? u"none"_q : leg.native,
			leg.nativeRole.isEmpty() ? u"none"_q : leg.nativeRole,
			FlagText(leg.nativeCreated)));
}

// Each native case lives in its own parentless container that is never
// shown, so neither it nor its children are ever created: setting
// Qt::WA_NativeWindow on a widget that was never created makes no platform
// window, and it calls the parent's enforceNativeChildren(), which marks
// every sibling native too - one shared container would make the plain
// target itself native, and the fixture window would make its buttons so.
void RunNativeRefusals(not_null<State*> state) {
	const auto half = state->side / 2;
	const auto quarter = half / 2;

	const auto first = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto native = Ui::CreateChild<Ui::RpWidget>(first.get());
	native->setGeometry(0, 0, half, half);
	native->setAttribute(Qt::WA_NativeWindow);
	auto own = RunRefusal(
		u"native target"_q,
		native,
		std::nullopt,
		PressOriginRefusal::NativeTarget);
	own.native = WidgetDescription(native);
	own.nativeRole = u"the target itself"_q;
	own.nativeCreated = (native->internalWinId() != 0)
		|| first->testAttribute(Qt::WA_WState_Created);
	CheckRefusal(own);

	const auto second = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto plain = Ui::CreateChild<Ui::RpWidget>(second.get());
	plain->setGeometry(0, 0, half, half);
	const auto descendant = Ui::CreateChild<Ui::RpWidget>(plain);
	descendant->setGeometry(quarter / 2, quarter / 2, quarter, quarter);
	descendant->setAttribute(Qt::WA_NativeWindow);
	auto below = RunRefusal(
		u"native descendant"_q,
		plain,
		std::nullopt,
		PressOriginRefusal::NativeTarget);
	below.native = WidgetDescription(descendant);
	below.nativeRole = u"a descendant of the target"_q;
	below.nativeCreated = (descendant->internalWinId() != 0)
		|| second->testAttribute(Qt::WA_WState_Created);
	CheckRefusal(below);
}

// The doomed probe's press posts its destruction through Ui::PostponeCall,
// which Settle defers to its drain: the facility has put the probe back by
// then, and after the drain it touches nothing - no release, no leave.
void RunLifetimeLeg(not_null<State*> state) {
	const auto doomed = state->doomed.get();
	if (!doomed) {
		Check(
			false,
			u"press origin self-test: lifetime: the doomed probe exists "
			u"before its press"_q,
			u"doomed=0"_q);
		return;
	}
	const auto centre = doomed->rect().center();
	const auto eventsBefore = state->doomedEvents.size();
	const auto cursorBefore = QCursor::pos();
	state->doomedReading = ClickWithPressOrigin(doomed, centre);
	const auto cursorAfter = QCursor::pos();
	const auto &reading = *state->doomedReading;
	const auto events = std::vector<EventRecord>(
		state->doomedEvents.begin() + std::ptrdiff_t(eventsBefore),
		state->doomedEvents.end());
	const auto ok = (reading.refusal == PressOriginRefusal::None)
		&& reading.pressSent
		&& reading.restored
		&& !reading.targetAlive
		&& !reading.delivered
		&& reading.cursorStable
		&& !state->doomed
		&& (events.size() == 1)
		&& (events[0].type == QEvent::MouseButtonPress)
		&& (cursorBefore == state->startCursor)
		&& (cursorAfter == state->startCursor);
	Check(
		ok,
		u"press origin self-test: lifetime: the doomed probe, destroyed in "
		u"Settle's drain by its own press's Ui::PostponeCall, was put back "
		u"before it and got no release or leave"_q,
		u"%1; doomedAlive=%2 events=[%3] cursor=%4->%5 start=%6"_q.arg(
			PressOriginDetails(reading),
			FlagText(state->doomed.get() != nullptr),
			EventsText(events),
			PointText(cursorBefore),
			PointText(cursorAfter),
			PointText(state->startCursor)));
}

void RunFixtureStage(not_null<State*> state) {
	state->built = BuildFixture(state);
	const auto gate = ReadGate(*state);
	Check(gate.ok, GateText(), gate.details);
	if (!gate.ok) {
		state->gate = GateText();
		return;
	}
	for (auto &subject : state->subjects) {
		CheckCursorOutside(*state, subject);
		ReadReferenceImages(*state, &subject);
	}
}

void RunControlStage(not_null<State*> state) {
	const auto stage = u"negative control"_q;
	for (auto &subject : state->subjects) {
		const auto &leg = state->legs.emplace_back(RunPress(
			*state,
			&subject,
			u"control"_q,
			PressKind::Plain,
			subject.press));
		CheckNoDisc(subject, leg, stage);
		CheckPlainCursor(*state, subject, leg, stage);
	}
}

void RunFacilityStage(not_null<State*> state) {
	const auto stage = u"press-point ripple"_q;
	for (auto &subject : state->subjects) {
		const auto &leg = state->legs.emplace_back(RunPress(
			*state,
			&subject,
			u"facility"_q,
			PressKind::Facility,
			subject.press));
		CheckFacilityReading(*state, subject, leg, stage);
		CheckFacilityEvents(subject, leg, stage);
		for (auto index = 0; index != kFrameCount; ++index) {
			CheckFacilityFrame(subject, leg, index);
		}
		CheckGrowth(subject, leg);
	}
}

// The IconButton at Q, inside the button and outside its ripple-area
// square: the press reaches the button with the cursor mapped to Q, exactly
// as at the stage 3 point, and the class's own guard refuses Q.
void RunGuardStage(not_null<State*> state) {
	const auto stage = u"guard kept"_q;
	auto &icon = state->subjects[kIconIndex];
	const auto &leg = state->legs.emplace_back(RunPress(
		*state,
		&icon,
		u"guard"_q,
		PressKind::Facility,
		state->guard));
	CheckFacilityReading(*state, icon, leg, stage);
	CheckFacilityEvents(icon, leg, stage);
	CheckGuardFrames(icon, leg, FindLeg(*state, icon.name, u"facility"_q));
}

void RunAfterStage(not_null<State*> state) {
	const auto stage = u"nothing left behind"_q;
	for (auto &subject : state->subjects) {
		CheckNothingLeftBehind(*state, subject);
		const auto &leg = state->legs.emplace_back(RunPress(
			*state,
			&subject,
			u"after"_q,
			PressKind::Plain,
			subject.press));
		CheckNoDisc(subject, leg, stage);
		CheckPlainCursor(*state, subject, leg, stage);
	}
}

void RunRefusalStage(not_null<State*> state) {
	const auto window = state->window.get();
	const auto &probe = state->subjects[kProbeIndex];
	const auto button = probe.button.data();
	if (!window || !button) {
		Check(
			false,
			u"press origin self-test: refusals: the fixture window and the "
			u"probe exist"_q,
			u"window=%1 probe=%2"_q.arg(
				FlagText(window != nullptr),
				FlagText(button != nullptr)));
		return;
	}
	CheckRefusal(RunRefusal(
		u"top-level window"_q,
		window,
		std::nullopt,
		PressOriginRefusal::TopLevelTarget));
	RunNativeRefusals(state);
	button->setEnabled(false);
	CheckRefusal(RunRefusal(
		u"disabled probe"_q,
		button,
		probe.press,
		PressOriginRefusal::DisabledTarget));
	button->setEnabled(true);
	CheckRefusal(RunRefusal(
		u"point outside"_q,
		button,
		QPoint(-1, -1),
		PressOriginRefusal::PointOutside));
	RunLifetimeLeg(state);
}

// Every press read the cursor around itself: the clocked runs before and
// after, and each facility reading before its move and after its press.
// All of them, and a fresh read now, must be the cursor read when the
// fixture was built.
void RunCursorStage(not_null<State*> state) {
	const auto start = state->startCursor;
	const auto now = QCursor::pos();
	auto ok = (now == start);
	auto reads = QStringList();
	const auto add = [&](const QString &name, QPoint before, QPoint after) {
		ok = ok && (before == start) && (after == start);
		reads.push_back(u"%1=%2->%3"_q.arg(
			name,
			PointText(before),
			PointText(after)));
	};
	for (const auto &leg : state->legs) {
		if (!leg.ran) {
			continue;
		}
		const auto name = u"%1/%2"_q.arg(leg.subject, leg.leg);
		add(name, leg.cursorBefore, leg.cursorAfter);
		if (leg.reading && leg.reading->pressSent) {
			add(
				name + u"/reading"_q,
				leg.reading->cursorBefore,
				leg.reading->cursorAfter);
		}
	}
	if (state->doomedReading && state->doomedReading->pressSent) {
		add(
			u"doomed/reading"_q,
			state->doomedReading->cursorBefore,
			state->doomedReading->cursorAfter);
	}
	Check(
		ok,
		u"press origin self-test: real cursor untouched: QCursor::pos() "
		u"still reads the cursor read at the start, and so did every read "
		u"around every press"_q,
		u"start=%1 now=%2 reads(%3)=[%4]"_q.arg(
			PointText(start),
			PointText(now),
			QString::number(reads.size()),
			reads.join(u"; "_q)));
}

// Idempotent: the teardown stage and Runner::onFinish both call it. The
// doomed probe goes first, then the window with every button; the styles
// stay, because nothing that references them is left.
void ReleaseFixture(State &state) {
	state.doomed = nullptr;
	state.window = nullptr;
	for (auto &subject : state.subjects) {
		subject.button = nullptr;
	}
}

} // namespace

void AppendPressOriginSelfTest(not_null<Runner*> runner) {
	// Leaked on purpose, as test_hover's State: the stages outlive this
	// call, and the buttons keep const references into its Styles, so the
	// styles must outlive every button on every path, a skipped teardown
	// included.
	const auto state = new State();
	runner->onFinish([=] {
		ReleaseFixture(*state);
	});

	runner->add({
		.name = u"press origin self-test: fixture"_q,
		.run = [=] {
			RunFixtureStage(state);
		},
	});
	runner->add({
		.name = u"press origin self-test: negative control"_q,
		.skipReason = [=] {
			return FrameSkipReason(*state);
		},
		.run = [=] {
			RunControlStage(state);
		},
	});
	runner->add({
		.name = u"press origin self-test: press-point ripple"_q,
		.skipReason = [=] {
			return FrameSkipReason(*state);
		},
		.run = [=] {
			RunFacilityStage(state);
		},
	});
	runner->add({
		.name = u"press origin self-test: guard kept"_q,
		.skipReason = [=] {
			return FrameSkipReason(*state);
		},
		.run = [=] {
			RunGuardStage(state);
		},
	});
	runner->add({
		.name = u"press origin self-test: nothing left behind"_q,
		.skipReason = [=] {
			return FrameSkipReason(*state);
		},
		.run = [=] {
			RunAfterStage(state);
		},
	});
	runner->add({
		.name = u"press origin self-test: refusals and lifetime"_q,
		.skipReason = [=] {
			return FixtureSkipReason(*state);
		},
		.run = [=] {
			RunRefusalStage(state);
		},
	});
	runner->add({
		.name = u"press origin self-test: real cursor untouched"_q,
		.skipReason = [=] {
			return FixtureSkipReason(*state);
		},
		.run = [=] {
			RunCursorStage(state);
		},
	});
	runner->add({
		.name = u"press origin self-test: teardown"_q,
		.run = [=] {
			ReleaseFixture(*state);
			Note(u"press origin self-test: fixture released, alive=%1"_q.arg(
				FlagText(state->window.get() != nullptr)));
		},
	});
}

} // namespace Test

#endif // _DEBUG
