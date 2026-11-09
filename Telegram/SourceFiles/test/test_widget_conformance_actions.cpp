/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_actions.h"

#include "history/view/history_view_element.h"
#include "history/history_inner_widget.h"
#include "test/test_capture.h"
#include "test/test_chat_show.h"
#include "test/test_log.h"
#include "test/test_palette.h"
#include "test/test_runner.h"
#include "test/test_widget_conformance_input.h"
#include "test/test_widget_conformance_state.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_ops.h"
#include "test/test_widgets.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_runtime.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/elastic_scroll.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QPoint>
#include <QtCore/QPointF>
#include <QtCore/QRect>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>
#include <QtCore/QStringList>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Test::Conformance {

// The action stages' record of one run: one Action per manifest action,
// the time the list got its last event, the action whose trusted widget
// events are being counted since its mark (|window|), and the host the
// first visible action found.
struct ConformanceActions {
	// A point an action aims at, in CSS px from the widget's top-left: the
	// centre of a published target, whose rect it keeps, or a point the
	// manifest gives.
	struct Target {
		QString name;
		QPointF point;
		QRectF rect;
	};

	// What identifies the run's host: the runtime's address, never
	// dereferenced, the tap's host generation and its factories.
	struct Host {
		quintptr runtime = 0;
		int generation = 0;
		int factories = 0;

		friend inline bool operator==(const Host &a, const Host &b) = default;
	};

	// One manifest action's stages. |blocked| names the refusal that makes
	// its later stages skip; |json| is what ActionReading::reading gets.
	struct Action {
		QString kind;
		QJsonObject manifest;
		QJsonObject json;
		QString blocked;
		std::optional<ConformancePointerKind> pointer;
		std::optional<Target> from;
		std::optional<Target> to;
		std::shared_ptr<const ChatListShow> show;
		WidgetCardReading accepted;
		std::unique_ptr<ConformancePointer> sequence;
		std::shared_ptr<const PaletteSwitch> previous;
		std::shared_ptr<const PaletteRestore> restore;
		std::shared_ptr<const PaletteSwitch> switched;
		WidgetOpsLatest before;
		double bandTop = 0.;
		double bandBottom = 0.;
		crl::time hold = 0;
		crl::time gapFrom = 0;
		crl::time began = 0;
		crl::time firstAt = 0;
		crl::time lastAt = 0;
		crl::time waitStarted = 0;
		int steps = 0;
		bool finished = false;
	};

	std::vector<Action> actions;
	Host host;
	crl::time startedAt = 0;
	crl::time lastInputAt = 0;
	crl::time windowAt = 0;
	int window = -1;
	bool hostKnown = false;
	bool hostReplaced = false;
};

namespace {

using Action = ConformanceActions::Action;
using Host = ConformanceActions::Host;
using RuntimeState = Tgw::Runtime::State;
using Target = ConformanceActions::Target;

// CLIENTS.md's "about 350 ms between actions", from the last event the
// list got; a manifest wait is no input and keeps its own interval.
constexpr auto kActionGap = crl::time(350);

// The bounds of what an action waits for: the widget's theme after the
// palette switched, and the runtime's answer to a scroll.
constexpr auto kThemeBound = crl::time(4000);
constexpr auto kVisibilityBound = crl::time(4000);

// How far, in CSS px, the shown band reaches beyond an action's targets,
// and how far a target may move between its show and the press before
// the hovered point no longer names it.
constexpr auto kBandMarginCss = 8.;
constexpr auto kTargetTolerance = 0.5;

// A press without ms holds as long as in twidget's reference runner.
constexpr auto kDefaultHold = crl::time(400);

// Chromium's mouse wheel: 120 eighths of a degree scroll 100 CSS px, so a
// manifest dy in CSS px is dy * 1.2 units, negative for a scroll down.
constexpr auto kWheelUnitsPerCss = 1.2;

// The local text messages above a run's widget with a visible action:
// enough room above the card for the list to scroll it wholly below the
// viewport, which HistoryInner keeps loaded within two viewports.
constexpr auto kFillers = 4;
constexpr auto kFillerLines = 40;

// Logical px the view's top lies below the viewport after the scroll.
constexpr auto kOffscreenMargin = 8;

// auto/23-gestures-b's mid drag passes only with at least three pointermove
// events with buttons, which moves coalesced into one frame cannot give:
// fewer is the runner's timing, never the widget's verdict.
struct MoveRequirement {
	const char *test = nullptr;
	const char *target = nullptr;
	int moves = 0;
};

constexpr auto kMoveRequirements = std::array{
	MoveRequirement{ "auto/23-gestures-b", "mid", 3 },
};

// The test-only private-member accessor of test_chat_show.cpp, kept
// file-private here: the runtime's own on-screen answer, read only.
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct RuntimeVisibleTag {
	using type = bool Tgw::Runtime::*;
	friend type Get(RuntimeVisibleTag);
};

template struct PrivateMember<RuntimeVisibleTag, &Tgw::Runtime::_visible>;

[[nodiscard]] QString ActionTitle(const ConformanceRun &run, int i) {
	return u"action #%1 %2"_q.arg(NumberText(i), run.actions[i].kind);
}

[[nodiscard]] QString StageName(
		const ConformanceRun &run,
		int i,
		const QString &what) {
	return Prefixed(run.key + u": "_q + ActionTitle(run, i) + u": "_q + what);
}

[[nodiscard]] QJsonValue Since(crl::time from, crl::time at) {
	return (from && at)
		? QJsonValue(qint64(at - from))
		: QJsonValue(QJsonValue::Null);
}

[[nodiscard]] QJsonArray PointArray(QPoint point) {
	return QJsonArray{ point.x(), point.y() };
}

[[nodiscard]] QJsonObject TargetObject(const Target &target) {
	return QJsonObject{
		{ u"name"_q, target.name },
		{ u"point"_q, QJsonArray{ target.point.x(), target.point.y() } },
		{
			u"rect"_q,
			QJsonArray{
				target.rect.x(),
				target.rect.y(),
				target.rect.width(),
				target.rect.height(),
			},
		},
	};
}

[[nodiscard]] QString ValueText(const QJsonValue &value) {
	return value.isString()
		? value.toString()
		: QString::fromUtf8(QJsonDocument(QJsonArray{ value }).toJson(
			QJsonDocument::Compact));
}

void Publish(ConformanceRun &run, int i) {
	run.actions[i].reading = run.live->actions[i].json;
}

void EmitRow(const ConformanceRun &run, int i) {
	const auto &reading = run.actions[i];
	Row(u"CONFORMANCE_ACTION"_q, {
		{ u"run"_q, run.key },
		{ u"index"_q, i },
		{ u"kind"_q, reading.kind },
		{ u"performed"_q, reading.performed },
		{ u"refusal"_q, reading.refusal },
		{ u"skipReason"_q, reading.skipReason },
		{ u"reading"_q, reading.reading },
	});
}

// The action's end: performed when |refusal| is empty, else refused by
// that name, which also skips its later stages. Its row goes out now,
// unless its window of widget events is open: CloseWindow() sends it.
void Conclude(
		ConformanceRun &run,
		int i,
		const QString &refusal,
		const QString &detail) {
	auto &actions = *run.live;
	auto &live = actions.actions[i];
	auto &reading = run.actions[i];
	live.finished = true;
	reading.performed = refusal.isEmpty();
	reading.refusal = refusal;
	if (!refusal.isEmpty()) {
		live.blocked = refusal;
		live.json.insert(u"refusalDetail"_q, detail);
	}
	live.json.insert(u"firstEventMs"_q, Since(actions.startedAt, live.firstAt));
	live.json.insert(u"lastEventMs"_q, Since(actions.startedAt, live.lastAt));
	live.json.insert(u"sinceLastInputMs"_q, Since(live.gapFrom, live.firstAt));
	Publish(run, i);
	if (actions.window != i) {
		EmitRow(run, i);
	}
}

// The last event of an input the stage sent itself just went out.
void EndInput(ConformanceRun &run, int i) {
	auto &actions = *run.live;
	auto &live = actions.actions[i];
	live.lastAt = crl::now();
	if (!live.firstAt) {
		live.firstAt = live.lastAt;
	}
	actions.lastInputAt = live.lastAt;
}

// Why the run's widget can take no input now, or empty.
[[nodiscard]] QString LiveRefusal(
		const ConformanceState &state,
		const ConformanceRun &run) {
	const auto runtime = FixtureOwnRuntime(*state.fixture, run.message->id);
	const auto frozen = ReadWidgetOps(*run.message->tap).fatalFrozen;
	return !WidgetFixtureItem(*state.fixture, run.message->id)
		? u"the widget message is gone"_q
		: !runtime
		? u"the list shows no runtime for the card"_q
		: (runtime->state() != RuntimeState::Live)
		? (u"the runtime is "_q + RuntimeStateName(runtime->state()))
		: (frozen > 0)
		? u"the host sent a fatal frozen notice"_q
		: QString();
}

[[nodiscard]] QString ActionSkip(
		const ConformanceState &state,
		const ConformanceRun &run,
		int i) {
	const auto skip = RunSkip(state, run);
	if (!skip.isEmpty()) {
		return skip;
	}
	const auto &reading = run.actions[i];
	return !reading.skipReason.isEmpty()
		? (u"skipped: "_q + reading.skipReason)
		: !run.live->actions[i].blocked.isEmpty()
		? (u"not performed: "_q + reading.refusal)
		: QString();
}

[[nodiscard]] bool GapKept(const ConformanceActions &actions) {
	return !actions.lastInputAt
		|| (crl::now() - actions.lastInputAt >= kActionGap);
}

// ---------------------------------------------------------------------
// The trusted widget events of each action.

struct Trusted {
	int moves = 0;
	int pressedMoves = 0;
	bool pressed = false;
	bool released = false;
};

// The pointermove events, and those between the first pointerdown and
// the pointerup or pointercancel after it: the renderer flushes pending
// moves before every other event, so the order is the delivery order.
[[nodiscard]] Trusted ReadTrusted(const WidgetOpsReading &reading) {
	auto result = Trusted();
	auto inside = false;
	for (const auto &type : reading.trustedEventTypes) {
		if (type == "pointermove") {
			++result.moves;
			if (inside) {
				++result.pressedMoves;
			}
		} else if (type == "pointerdown" && !result.pressed) {
			result.pressed = inside = true;
		} else if ((type == "pointerup" || type == "pointercancel")
			&& inside) {
			inside = false;
			result.released = true;
		}
	}
	return result;
}

[[nodiscard]] QJsonObject TrustedObject(
		const WidgetOpsReading &reading,
		const Trusted &trusted,
		crl::time windowMs) {
	auto types = QJsonArray();
	for (const auto &type : reading.trustedEventTypes) {
		types.push_back(QString::fromStdString(type));
	}
	return QJsonObject{
		{ u"events"_q, reading.trustedEvents },
		{ u"clicks"_q, reading.trustedClicks },
		{ u"types"_q, types },
		{ u"moves"_q, trusted.moves },
		{ u"pressedMoves"_q, trusted.pressedMoves },
		{ u"pressed"_q, trusted.pressed },
		{ u"released"_q, trusted.released },
		{ u"windowMs"_q, qint64(windowMs) },
	};
}

// A drag kMoveRequirements names must have reached the widget as at
// least that many moves with the button down.
void CheckMoves(ConformanceRun &run, int i, const Trusted &trusted) {
	auto &live = run.live->actions[i];
	if (live.kind != u"drag"_q || !live.from || !run.actions[i].performed) {
		return;
	}
	for (const auto &requirement : kMoveRequirements) {
		if (run.id != QString::fromLatin1(requirement.test)
			|| live.from->name != QString::fromLatin1(requirement.target)) {
			continue;
		}
		const auto met = (trusted.pressedMoves >= requirement.moves);
		live.json.insert(u"moveRequirement"_q, QJsonObject{
			{ u"moves"_q, requirement.moves },
			{ u"met"_q, met },
		});
		if (!met) {
			AddRefusal(
				run,
				u"too-few-moves"_q,
				u"%1 reached the widget as %2 moves with the button down, "
					u"not the %3 its check needs: the moves were coalesced "
					u"or lost on the way"_q.arg(
						ActionTitle(run, i),
						NumberText(trusted.pressedMoves),
						NumberText(requirement.moves)));
		}
	}
}

// Ends the open window: the trusted events since the action's mark,
// which include its show's hover and whatever arrived after its last
// event, then its row.
void CloseWindow(ConformanceRun &run) {
	auto &actions = *run.live;
	if (actions.window < 0) {
		return;
	}
	const auto i = std::exchange(actions.window, -1);
	auto &live = actions.actions[i];
	const auto reading = ReadWidgetOps(*run.message->tap);
	const auto trusted = ReadTrusted(reading);
	live.json.insert(u"trusted"_q, TrustedObject(
		reading,
		trusted,
		crl::now() - actions.windowAt));
	CheckMoves(run, i, trusted);
	Publish(run, i);
	EmitRow(run, i);
}

// ---------------------------------------------------------------------
// Targets.

// The centre of targets[name] in the newest value, or a point [x, y].
[[nodiscard]] std::optional<Target> TargetOf(
		const QJsonObject &targets,
		const QJsonValue &value) {
	if (value.isString()) {
		const auto rect = targets.value(value.toString()).toArray();
		if (rect.size() < 4) {
			return std::nullopt;
		}
		auto numbers = std::array<double, 4>();
		for (auto k = 0; k != 4; ++k) {
			numbers[k] = rect[k].toDouble(-1.);
			if (!rect[k].isDouble() || !std::isfinite(numbers[k])) {
				return std::nullopt;
			}
		}
		if (numbers[2] < 0. || numbers[3] < 0.) {
			return std::nullopt;
		}
		const auto box = QRectF(numbers[0], numbers[1], numbers[2], numbers[3]);
		return Target{
			.name = value.toString(),
			.point = box.center(),
			.rect = box,
		};
	}
	const auto point = value.toArray();
	if (point.size() < 2 || !point[0].isDouble() || !point[1].isDouble()) {
		return std::nullopt;
	}
	const auto at = QPointF(point[0].toDouble(), point[1].toDouble());
	return Target{ .point = at, .rect = QRectF(at, QSizeF()) };
}

struct Points {
	Target from;
	std::optional<Target> to;
};

// The action's points from the newest value: its own key's target, and
// for a drag "to" (a target, a point, or with "by" a delta from the
// start).
[[nodiscard]] std::optional<Points> ResolvePoints(
		const ConformanceRun &run,
		const Action &live,
		QString *error) {
	const auto &result = run.result;
	if (!result.present) {
		*error = result.error.isEmpty()
			? u"the widget has no value of its own to take targets from"_q
			: (u"the newest value is refused: "_q + result.error);
		return std::nullopt;
	}
	const auto value = live.manifest.value(live.kind);
	const auto from = TargetOf(result.targets, value);
	if (!from) {
		*error = u"[%1] names no target of the newest value (targets: "
			u"%2)"_q.arg(
				ValueText(value),
				QStringList(result.targets.keys()).join(u","_q));
		return std::nullopt;
	}
	auto points = Points{ .from = *from };
	if (live.kind != u"drag"_q) {
		return points;
	}
	const auto to = live.manifest.value(u"to"_q);
	if (live.manifest.value(u"by"_q).toBool()) {
		const auto delta = to.toArray();
		if (delta.size() < 2 || !delta[0].isDouble() || !delta[1].isDouble()) {
			*error = u"a drag by a delta needs \"to\": [dx, dy], not "_q
				+ ValueText(to);
			return std::nullopt;
		}
		const auto end = from->point
			+ QPointF(delta[0].toDouble(), delta[1].toDouble());
		points.to = Target{ .point = end, .rect = QRectF(end, QSizeF()) };
	} else if (const auto end = TargetOf(result.targets, to)) {
		points.to = *end;
	} else {
		*error = u"the drag's \"to\" [%1] names no target of the newest "
			u"value"_q.arg(ValueText(to));
		return std::nullopt;
	}
	return points;
}

[[nodiscard]] bool Moved(const Target &was, const Target &now) {
	return (std::abs(was.point.x() - now.point.x()) > kTargetTolerance)
		|| (std::abs(was.point.y() - now.point.y()) > kTargetTolerance);
}

// The targets at the gap's end: the points, and the band of the card
// their show brings into view.
void ResolveAction(ConformanceRun &run, int i) {
	auto &live = run.live->actions[i];
	RefreshResult(run);
	auto error = QString();
	const auto points = ResolvePoints(run, live, &error);
	if (!points) {
		Conclude(run, i, u"target-unresolved"_q, error);
		return;
	}
	live.from = points->from;
	live.to = points->to;
	auto top = std::min(live.from->rect.top(), live.from->point.y());
	auto bottom = std::max(live.from->rect.bottom(), live.from->point.y());
	if (live.to) {
		top = std::min({ top, live.to->rect.top(), live.to->point.y() });
		bottom = std::max({
			bottom,
			live.to->rect.bottom(),
			live.to->point.y(),
		});
	}
	live.bandTop = top - kBandMarginCss;
	live.bandBottom = bottom + kBandMarginCss;
	auto target = QJsonObject{
		{ u"from"_q, TargetObject(*live.from) },
		{ u"band"_q, QJsonArray{ live.bandTop, live.bandBottom } },
		{ u"resultSerial"_q, run.result.serial },
	};
	if (live.to) {
		target.insert(u"to"_q, TargetObject(*live.to));
	}
	live.json.insert(u"target"_q, target);
}

// |css| on |card|, in its view's logical px.
[[nodiscard]] QPoint CardPoint(QRect card, QPointF css) {
	const auto scale = style::Scale() / 100.;
	return card.topLeft() + QPoint(
		int(std::lround(css.x() * scale)),
		int(std::lround(css.y() * scale)));
}

// The card's full width over the action's band, inside the card.
[[nodiscard]] QRect BandLocal(QRect card, const Action &live) {
	if (card.isEmpty()) {
		return card;
	}
	const auto scale = style::Scale() / 100.;
	const auto last = card.y() + card.height();
	const auto top = std::clamp(
		card.y() + int(std::floor(live.bandTop * scale)),
		card.y(),
		last - 1);
	const auto bottom = std::clamp(
		card.y() + int(std::ceil(live.bandBottom * scale)),
		top + 1,
		last);
	return QRect(card.x(), top, card.width(), bottom - top);
}

[[nodiscard]] QString ShowRefusalText(const ChatListShow &shown) {
	return shown.finished
		? (ChatListShowRefusalName(shown.refusal) + u" - "_q + shown.reason)
		: u"the show did not run"_q;
}

// Right before the press: the show, the view, the core, the geometry
// and the targets are the accepted ones. A refusal's name and detail, or
// empty ones.
[[nodiscard]] std::pair<QString, QString> Revalidate(
		const ConformanceState &state,
		ConformanceRun &run,
		int i) {
	auto &live = run.live->actions[i];
	const auto &shown = *live.show;
	if (!shown.shown()) {
		return { u"show-refused"_q, ShowRefusalText(shown) };
	}
	const auto changed = RevalidateWidgetCard(
		*state.fixture,
		run.message->id,
		ActionTitle(run, i),
		shown,
		live.accepted);
	if (!changed.isEmpty()) {
		return { u"revalidation-failed"_q, changed };
	}
	RefreshResult(run);
	auto error = QString();
	const auto now = ResolvePoints(run, live, &error);
	if (!now) {
		return { u"target-unresolved"_q, u"at the press: "_q + error };
	}
	const auto toMoved = live.to
		&& now->to
		&& !live.to->name.isEmpty()
		&& Moved(*live.to, *now->to);
	if (Moved(*live.from, now->from) || toMoved) {
		return {
			u"target-moved"_q,
			u"the targets moved after their show: was %1, now %2"_q.arg(
				CompactJson(TargetObject(*live.from)),
				CompactJson(TargetObject(now->from))),
		};
	}
	return {};
}

// ---------------------------------------------------------------------
// Pointer actions: tap, wheel and the timed sequences.

[[nodiscard]] QString CoreText(
		const std::shared_ptr<WidgetFixture> &fixture,
		FullMsgId id) {
	const auto runtime = fixture ? FixtureOwnRuntime(*fixture, id) : nullptr;
	const auto core = runtime ? runtime->interactive() : nullptr;
	return core
		? u"pressed=%1 hovered=%2"_q.arg(
			NumberText(core->pressed() ? 1 : 0),
			NumberText(core->hovered() ? 1 : 0))
		: u"no core"_q;
}

void StartSequence(
		ConformanceState &state,
		ConformanceRun &run,
		int i,
		not_null<QWidget*> list) {
	auto &live = run.live->actions[i];
	const auto &shown = *live.show;
	const auto card = live.accepted.card;
	const auto delta = live.to
		? (CardPoint(card, live.to->point) - CardPoint(card, live.from->point))
		: QPoint();
	const auto weak = std::weak_ptr<WidgetFixture>(state.fixture);
	const auto id = run.message->id;
	live.sequence = std::make_unique<ConformancePointer>(
		ConformancePointerArgs{
			.kind = *live.pointer,
			.list = list,
			.from = shown.hover,
			.to = shown.hover + delta,
			.hold = live.hold,
			.steps = live.steps,
			.item = [=]() -> HistoryItem* {
				const auto strong = weak.lock();
				return strong ? WidgetFixtureItem(*strong, id) : nullptr;
			},
			.observe = [=] {
				return CoreText(weak.lock(), id);
			},
		});
	live.sequence->start();
}

void SendWheel(
		ConformanceState &state,
		ConformanceRun &run,
		int i,
		not_null<QWidget*> list) {
	auto &live = run.live->actions[i];
	const auto dy = live.manifest.value(u"dy"_q).toDouble(0.);
	const auto angle = QPoint(0, -int(std::lround(dy * kWheelUnitsPerCss)));
	const auto delivery = Wheel(list, angle, live.show->hover);
	DeliverListLeave(WidgetFixtureList(*state.fixture));
	EndInput(run, i);
	live.json.insert(u"wheel"_q, QJsonObject{
		{ u"dy"_q, dy },
		{ u"angleDelta"_q, PointArray(angle) },
		{ u"delivered"_q, delivery.delivered },
		{ u"receiver"_q, delivery.receiver },
		{ u"inert"_q, delivery.inert },
		{ u"refusal"_q, delivery.refusal },
		{ u"details"_q, WheelDeliveryDetails(delivery) },
	});
	if (!WidgetFixtureList(*state.fixture)) {
		Conclude(
			run,
			i,
			u"receiver-gone"_q,
			u"the list was destroyed by the wheel"_q);
	} else {
		Conclude(run, i, QString(), QString());
	}
}

// In the stage right after the action's show: revalidated, then the
// click and the wheel at once, or the timed sequence started.
void PerformPointer(ConformanceState &state, ConformanceRun &run, int i) {
	auto &live = run.live->actions[i];
	const auto list = WidgetFixtureList(*state.fixture);
	live.json.insert(u"show"_q, ShowObject(live.show.get()));
	live.json.insert(u"accepted"_q, WidgetCardText(live.accepted));
	const auto [refusal, detail] = Revalidate(state, run, i);
	if (!refusal.isEmpty()) {
		DeliverListLeave(list);
		EndInput(run, i);
		Conclude(run, i, refusal, detail);
		return;
	}
	live.firstAt = crl::now();
	live.json.insert(u"point"_q, PointArray(live.show->hover));
	if (live.kind == u"tap"_q) {
		Click(list, live.show->hover);
		EndInput(run, i);
		const auto alive = (WidgetFixtureList(*state.fixture) != nullptr);
		live.json.insert(u"listAlive"_q, alive);
		Conclude(run, i, QString(), QString());
	} else if (live.kind == u"wheel"_q) {
		SendWheel(state, run, i, list);
	} else {
		StartSequence(state, run, i, list);
	}
}

[[nodiscard]] QString UndeliveredText(
		const ConformancePointerReading &reading) {
	if (reading.refusal != ConformancePointerRefusal::None) {
		return ConformancePointerRefusalName(reading.refusal)
			+ u" - "_q
			+ reading.reason;
	}
	auto missing = QStringList();
	for (const auto &event : reading.events) {
		if (!event.delivered) {
			missing.push_back(ConformancePointerEventName(event.type));
		}
	}
	return u"not every planned event reached the list; undelivered: [%1]"_q
		.arg(missing.join(u","_q));
}

void RecordPointer(ConformanceRun &run, int i) {
	auto &actions = *run.live;
	auto &live = actions.actions[i];
	if (live.finished || !live.sequence) {
		return;
	}
	const auto &reading = live.sequence->reading();
	live.json.insert(u"sequence"_q, ConformancePointerJson(reading));
	if (reading.kind == ConformancePointerKind::RightClick) {
		live.json.insert(u"menuBuilt"_q, reading.menu.built());
	}
	if (reading.startedAt) {
		live.firstAt = reading.startedAt;
	}
	live.lastAt = reading.lastAt ? reading.lastAt : crl::now();
	actions.lastInputAt = live.lastAt;
	if (reading.delivered()) {
		Conclude(run, i, QString(), QString());
	} else {
		Conclude(run, i, u"input-not-delivered"_q, UndeliveredText(reading));
	}
}

// The show's hover is the action, left in place for the next one.
void RecordHover(ConformanceRun &run, int i) {
	auto &live = run.live->actions[i];
	if (live.finished) {
		return;
	}
	const auto &shown = *live.show;
	live.json.insert(u"show"_q, ShowObject(&shown));
	live.json.insert(u"accepted"_q, WidgetCardText(live.accepted));
	if (shown.hoverSent) {
		live.json.insert(u"point"_q, PointArray(shown.hover));
		EndInput(run, i);
	}
	if (shown.shown() && shown.hovered) {
		Conclude(run, i, QString(), QString());
	} else {
		Conclude(run, i, u"show-refused"_q, ShowRefusalText(shown));
	}
}

// ---------------------------------------------------------------------
// Keys.

[[nodiscard]] bool KeyKnown(const QString &key) {
	return (key == u"Enter"_q)
		|| (key == u"Space"_q)
		|| ((key.size() == 1) && key[0].isPrint() && !key[0].isSpace());
}

// Enter as the key it is, with no text, as Test::PressKey sends it; Space
// and a single character as the text they type (Test::TypeText).
void SendKey(not_null<QWidget*> receiver, const QString &key) {
	if (key == u"Enter"_q) {
		PressKey(receiver, Qt::Key_Return);
	} else {
		TypeText(receiver, (key == u"Space"_q) ? u" "_q : key);
	}
}

// To the window's current focus child, as the OS sends a key to the
// active window's, only into an ordinary empty compose field; a focus
// child that is not its editor is recorded, never redirected. What the
// key typed into the field is cleared in the same turn, before the
// field's queued typing update can read it.
void PerformKey(ConformanceState &state, ConformanceRun &run, int i) {
	auto &live = run.live->actions[i];
	const auto before = ReadCompose(state);
	live.json.insert(u"composeBefore"_q, ComposeObject(before));
	if (!before.found || !before.unusual.isEmpty()) {
		Conclude(
			run,
			i,
			u"compose-not-ordinary"_q,
			before.unusual.join(u"; "_q));
		return;
	}
	const auto receiver = before.focus.data();
	if (!receiver) {
		Conclude(
			run,
			i,
			u"no-focus-child"_q,
			u"the window has no focus child to send the key to"_q);
		return;
	}
	live.json.insert(u"receiver"_q, WidgetDescription(receiver));
	live.json.insert(u"focusMoved"_q, !before.focused);
	live.firstAt = crl::now();
	SendKey(receiver, live.manifest.value(u"key"_q).toString());
	EndInput(run, i);
	const auto field = ComposeField(state);
	const auto typed = field ? field->getTextWithTags().text : QString();
	live.json.insert(u"typed"_q, typed);
	if (field && !typed.isEmpty()) {
		Settle([&] {
			field->clear();
		});
	}
	live.json.insert(u"cleared"_q, field && !typed.isEmpty());
	live.json.insert(u"composeAfter"_q, ComposeObject(ReadCompose(state)));
	Conclude(run, i, QString(), QString());
}

// ---------------------------------------------------------------------
// Themes.

[[nodiscard]] QString ThemeTarget(const Action &live) {
	return live.manifest.value(u"theme"_q).toString();
}

// The palette changes the action's restore and switch made together.
[[nodiscard]] int Transitions(const Action &live) {
	const auto restored = live.restore
		&& live.restore->finished
		&& (live.restore->before.night != live.restore->after.night);
	const auto switched = live.switched->finished
		&& (live.switched->before.night != live.switched->after.night);
	return (restored ? 1 : 0) + (switched ? 1 : 0);
}

[[nodiscard]] bool SchemeReached(
		const Action &live,
		const WidgetOpsLatest &latest) {
	return (latest.generation == live.before.generation)
		&& (latest.scheme == ThemeTarget(live).toStdString())
		&& (latest.schemeChanges > live.before.schemeChanges);
}

// The chain's restore and switch of the action, judged whether or not
// the run itself is performed.
void JudgeTheme(
		const ConformanceState &state,
		const ConformanceRun &run,
		int i) {
	const auto &live = run.live->actions[i];
	const auto what = Prefixed(u"%1: %2 to %3"_q.arg(
		run.key,
		ActionTitle(run, i),
		ThemeTarget(live)));
	if (live.restore) {
		JudgeRestore(
			live.previous,
			live.restore,
			what + u": the chain's last switch is restored first"_q);
	}
	const auto &switched = *live.switched;
	if (!switched.finished) {
		Note(what + u": the palette switch did not run - "_q
			+ GlobalSkip(state));
		return;
	}
	Check(
		switched.succeeded(),
		what + u": the app shows the %1 palette"_q.arg(
			PaletteModeName(switched.requested)),
		PaletteSwitchText(switched));
	const auto transitions = Transitions(live);
	const auto night = (ThemeTarget(live) == u"dark"_q);
	Check(
		(transitions == 1) && (switched.after.night == night),
		what + u": exactly one palette transition, to the target"_q,
		u"transitions=%1 night=%2"_q.arg(
			NumberText(transitions),
			NumberText(switched.after.night ? 1 : 0)));
}

[[nodiscard]] bool ThemeDecided(
		const ConformanceState &state,
		const ConformanceRun &run,
		int i) {
	const auto &live = run.live->actions[i];
	if (!live.switched->succeeded()
		|| !LiveRefusal(state, run).isEmpty()) {
		return true;
	}
	const auto latest = ReadWidgetOpsLatest(*run.message->tap);
	return SchemeReached(live, latest)
		|| (crl::now() - live.waitStarted >= kThemeBound);
}

void RecordTheme(ConformanceRun &run, int i) {
	auto &actions = *run.live;
	auto &live = actions.actions[i];
	if (live.finished) {
		return;
	}
	const auto latest = ReadWidgetOpsLatest(*run.message->tap);
	const auto transitions = Transitions(live);
	const auto &switched = *live.switched;
	live.json.insert(u"palette"_q, QJsonObject{
		{
			u"restore"_q,
			live.restore ? PaletteRestoreText(*live.restore) : QString(),
		},
		{ u"switch"_q, PaletteSwitchText(switched) },
		{ u"transitions"_q, transitions },
	});
	live.json.insert(u"widgetTheme"_q, QJsonObject{
		{ u"schemeBefore"_q, QString::fromStdString(live.before.scheme) },
		{ u"schemeAfter"_q, QString::fromStdString(latest.scheme) },
		{
			u"changes"_q,
			latest.schemeChanges - live.before.schemeChanges,
		},
		{ u"generationBefore"_q, live.before.generation },
		{ u"generationAfter"_q, latest.generation },
		{ u"reached"_q, SchemeReached(live, latest) },
		{ u"waitMs"_q, qint64(crl::now() - live.waitStarted) },
		{ u"themeMessage"_q, QString::fromStdString(latest.themeMessage) },
	});
	live.firstAt = live.began;
	EndInput(run, i);
	const auto restored = !live.restore
		|| !live.previous
		|| !live.previous->succeeded()
		|| live.restore->restored();
	const auto night = (ThemeTarget(live) == u"dark"_q);
	if (!switched.succeeded() || !restored) {
		Conclude(
			run,
			i,
			u"palette-refused"_q,
			PaletteSwitchText(switched)
				+ (live.restore
					? (u"; "_q + PaletteRestoreText(*live.restore))
					: QString()));
	} else if ((transitions != 1) || (switched.after.night != night)) {
		Conclude(
			run,
			i,
			u"palette-transitions"_q,
			u"the restore and the switch made %1 palette transitions and "
				u"left night=%2, not one transition to the %3 theme"_q.arg(
					NumberText(transitions),
					NumberText(switched.after.night ? 1 : 0),
					ThemeTarget(live)));
	} else {
		Conclude(run, i, QString(), QString());
	}
}

// ---------------------------------------------------------------------
// Visibility.

// Where the run's view and card are and what the list's scroll shows,
// as HistoryWidget's item visibility query reads them: the view counts
// visible while [top, top + height) meets the scroll's range.
struct Placement {
	QRect card;
	QRect area;
	int viewTop = -1;
	int viewHeight = 0;
	int scrollTop = 0;
	int scrollHeight = 0;
	bool read = false;
};

[[nodiscard]] Placement ReadPlacement(
		const ConformanceState &state,
		const ConformanceRun &run) {
	auto result = Placement();
	const auto list = WidgetFixtureList(*state.fixture);
	const auto inner = dynamic_cast<HistoryInner*>(list);
	const auto scroll = FixtureListScroll(list);
	const auto view = FixtureOwnView(*state.fixture, run.message->id);
	if (!inner || !scroll || !view) {
		return result;
	}
	result.viewTop = inner->itemTop(view);
	result.viewHeight = view->height();
	result.card = WidgetCardLocal(view).translated(0, result.viewTop);
	result.area = ChatListShowVisibleArea(list);
	result.scrollTop = scroll->scrollTop();
	result.scrollHeight = scroll->height();
	result.read = true;
	return result;
}

[[nodiscard]] bool Below(const Placement &placement) {
	return placement.read
		&& (placement.viewTop
			>= placement.scrollTop + placement.scrollHeight);
}

[[nodiscard]] QJsonObject PlacementObject(const Placement &placement) {
	return QJsonObject{
		{ u"read"_q, placement.read },
		{ u"viewTop"_q, placement.viewTop },
		{ u"viewHeight"_q, placement.viewHeight },
		{ u"card"_q, RectArray(placement.card) },
		{ u"visibleArea"_q, RectArray(placement.area) },
		{ u"scrollTop"_q, placement.scrollTop },
		{ u"scrollHeight"_q, placement.scrollHeight },
		{ u"below"_q, Below(placement) },
	};
}

[[nodiscard]] std::optional<bool> RuntimeVisible(
		const ConformanceState &state,
		const ConformanceRun &run) {
	const auto runtime = FixtureOwnRuntime(*state.fixture, run.message->id);
	if (!runtime) {
		return std::nullopt;
	}
	return runtime->*Get(RuntimeVisibleTag());
}

[[nodiscard]] QJsonValue VisibleValue(std::optional<bool> visible) {
	return visible ? QJsonValue(*visible) : QJsonValue(QJsonValue::Null);
}

[[nodiscard]] Host ReadHost(
		const ConformanceState &state,
		const ConformanceRun &run) {
	const auto runtime = FixtureOwnRuntime(*state.fixture, run.message->id);
	return {
		.runtime = reinterpret_cast<quintptr>(runtime),
		.generation = ReadWidgetOpsLatest(*run.message->tap).generation,
		.factories = ReadWidgetOps(*run.message->tap).factoriesSelected,
	};
}

[[nodiscard]] QJsonObject HostObject(const Host &host) {
	return QJsonObject{
		{ u"runtime"_q, u"0x"_q + QString::number(quint64(host.runtime), 16) },
		{ u"generation"_q, host.generation },
		{ u"factories"_q, host.factories },
	};
}

// The host the run's first visible action found, at its gap's end.
void RememberHost(
		const ConformanceState &state,
		ConformanceRun &run,
		int i) {
	auto &actions = *run.live;
	const auto host = ReadHost(state, run);
	actions.actions[i].json.insert(u"hostBefore"_q, HostObject(host));
	if (!actions.hostKnown) {
		actions.host = host;
		actions.hostKnown = true;
	}
}

// The same runtime and host as before the first visible action: a host
// unloaded or restarted while off screen would not measure the pause.
void CheckHost(
		const ConformanceState &state,
		ConformanceRun &run,
		int i) {
	auto &actions = *run.live;
	const auto host = ReadHost(state, run);
	actions.actions[i].json.insert(u"hostAfter"_q, HostObject(host));
	if (!actions.hostKnown
		|| (host == actions.host)
		|| actions.hostReplaced) {
		return;
	}
	actions.hostReplaced = true;
	AddRefusal(
		run,
		u"host-replaced"_q,
		u"%1 found the host %2, the first visible action %3"_q.arg(
			ActionTitle(run, i),
			CompactJson(HostObject(host)),
			CompactJson(HostObject(actions.host))));
}

[[nodiscard]] QJsonArray FillerShapes(const ConformanceRun &run) {
	auto result = QJsonArray();
	for (const auto &filler : run.fillers) {
		result.push_back(filler->shape);
	}
	return result;
}

// The view's top just below the scroll's range, kOffscreenMargin into
// the list's loaded area, in one scroll of the owning ElasticScroll.
void ScrollAway(ConformanceState &state, ConformanceRun &run, int i) {
	auto &live = run.live->actions[i];
	const auto before = ReadPlacement(state, run);
	const auto scroll = FixtureListScroll(WidgetFixtureList(*state.fixture));
	live.json.insert(u"before"_q, PlacementObject(before));
	live.json.insert(u"fillers"_q, FillerShapes(run));
	if (!before.read || !scroll) {
		Conclude(
			run,
			i,
			u"no-list"_q,
			u"the list, its owning scroll or its view of the card is "
				u"gone"_q);
		return;
	}
	const auto top = before.viewTop
		- before.scrollHeight
		- kOffscreenMargin;
	live.json.insert(u"scrollTo"_q, top);
	if (top < 0) {
		Conclude(
			run,
			i,
			u"no-room"_q,
			u"the card's view starts %1 px into the list, so no scroll "
				u"top puts it wholly below the %2 px viewport"_q.arg(
					NumberText(before.viewTop),
					NumberText(before.scrollHeight)));
		return;
	}
	live.firstAt = crl::now();
	if (top < before.scrollTop) {
		Settle([&] {
			scroll->scrollToY(top);
		});
	}
	EndInput(run, i);
	live.waitStarted = live.lastAt;
	live.json.insert(u"scrolled"_q, PlacementObject(ReadPlacement(state, run)));
}

[[nodiscard]] bool AwayDecided(
		const ConformanceState &state,
		const ConformanceRun &run,
		int i) {
	const auto &live = run.live->actions[i];
	if (live.finished) {
		return true;
	}
	const auto visible = RuntimeVisible(state, run);
	return !visible
		|| (Below(ReadPlacement(state, run)) && !*visible)
		|| (crl::now() - live.waitStarted >= kVisibilityBound);
}

// Performed once the view is wholly below the viewport; whether the
// runtime counted it off screen is the product's answer, recorded.
void RecordAway(ConformanceState &state, ConformanceRun &run, int i) {
	auto &live = run.live->actions[i];
	if (live.finished) {
		return;
	}
	const auto placement = ReadPlacement(state, run);
	live.json.insert(u"after"_q, PlacementObject(placement));
	live.json.insert(
		u"runtimeVisible"_q,
		VisibleValue(RuntimeVisible(state, run)));
	live.json.insert(
		u"waitMs"_q,
		qint64(crl::now() - live.waitStarted));
	CheckHost(state, run, i);
	if (Below(placement)) {
		Conclude(run, i, QString(), QString());
	} else {
		Conclude(
			run,
			i,
			u"still-in-view"_q,
			u"after the scroll the view still meets the scroll's range: "_q
				+ CompactJson(PlacementObject(placement)));
	}
}

[[nodiscard]] bool BackDecided(
		const ConformanceState &state,
		const ConformanceRun &run,
		int i) {
	const auto &live = run.live->actions[i];
	const auto visible = RuntimeVisible(state, run);
	return live.finished
		|| !live.show->shown()
		|| !visible
		|| *visible
		|| (crl::now() - live.waitStarted >= kVisibilityBound);
}

void RecordBack(ConformanceState &state, ConformanceRun &run, int i) {
	auto &live = run.live->actions[i];
	if (live.finished) {
		return;
	}
	const auto &shown = *live.show;
	live.json.insert(u"show"_q, ShowObject(&shown));
	live.json.insert(u"after"_q, PlacementObject(ReadPlacement(state, run)));
	live.json.insert(
		u"runtimeVisible"_q,
		VisibleValue(RuntimeVisible(state, run)));
	live.json.insert(
		u"waitMs"_q,
		qint64(crl::now() - live.waitStarted));
	CheckHost(state, run, i);
	live.firstAt = live.began;
	EndInput(run, i);
	if (shown.shown()) {
		Conclude(run, i, QString(), QString());
	} else {
		Conclude(run, i, u"show-refused"_q, ShowRefusalText(shown));
	}
}

// ---------------------------------------------------------------------
// The plan and the stages.

[[nodiscard]] bool AimsAtPoint(const QString &kind) {
	return ConformancePointerKindOf(kind).has_value()
		|| (kind == u"tap"_q)
		|| (kind == u"hover"_q)
		|| (kind == u"wheel"_q);
}

// The action as the manifest gives it. Only an action this build cannot
// perform at all gets its skip reason here.
void PlanAction(
		Action &live,
		ActionReading &reading,
		const QJsonObject &manifest) {
	live.kind = reading.kind;
	live.manifest = manifest;
	live.pointer = ConformancePointerKindOf(live.kind);
	live.json = QJsonObject{
		{ u"index"_q, reading.index },
		{ u"kind"_q, reading.kind },
		{ u"manifest"_q, manifest },
	};
	auto skip = QString();
	if (live.kind == u"press"_q) {
		const auto ms = manifest.value(u"ms"_q);
		live.hold = ms.isDouble()
			? crl::time(std::lround(std::max(ms.toDouble(), 0.)))
			: kDefaultHold;
		live.json.insert(u"hold"_q, qint64(live.hold));
		live.json.insert(u"holdDefaulted"_q, !ms.isDouble());
	} else if (live.kind == u"drag"_q) {
		live.steps = manifest.value(u"steps"_q).toInt(0);
	} else if (live.kind == u"key"_q) {
		const auto key = manifest.value(u"key"_q).toString();
		if (!KeyKnown(key)) {
			skip = u"the runner has no key for [%1]: it sends Enter, Space "
				u"and single characters"_q.arg(key);
		}
	} else if (live.kind == u"theme"_q) {
		const auto theme = manifest.value(u"theme"_q).toString();
		if (theme != u"light"_q && theme != u"dark"_q) {
			skip = u"the chat has no [%1] theme to switch to"_q.arg(theme);
		}
	} else if (live.kind == u"visible"_q) {
		if (!manifest.value(u"visible"_q).isBool()) {
			skip = u"\"visible\" is not true or false"_q;
		}
	} else if (live.kind == u"wait"_q) {
		if (!manifest.value(u"wait"_q).isDouble()
			|| manifest.value(u"wait"_q).toDouble() < 0.) {
			skip = u"\"wait\" is not a number of ms"_q;
		}
	} else if (!AimsAtPoint(live.kind)) {
		skip = u"the action has none of CLIENTS.md's action keys"_q;
	}
	if (!skip.isEmpty()) {
		reading.skipReason = skip;
		reading.refusal = QString();
		live.finished = true;
		live.json.insert(u"skipReason"_q, skip);
	}
	reading.reading = live.json;
}

// The gap's end: the previous action's window closed, any layer it
// opened hidden, the widget still live, the events marked, and the
// action's own targets or state read.
void BeginAction(ConformanceState &state, ConformanceRun &run, int i) {
	auto &actions = *run.live;
	auto &live = actions.actions[i];
	HideFixtureLayers(
		*state.fixture,
		*run.message,
		u"before "_q + ActionTitle(run, i));
	CloseWindow(run);
	live.began = crl::now();
	live.json.insert(u"gapMs"_q, Since(live.gapFrom, live.began));
	live.json.insert(u"beganMs"_q, Since(actions.startedAt, live.began));
	const auto dead = LiveRefusal(state, run);
	if (!dead.isEmpty()) {
		Conclude(run, i, u"not-live"_q, dead);
		return;
	}
	MarkWidgetOpsEvents(*run.message->tap);
	actions.window = i;
	actions.windowAt = live.began;
	if (AimsAtPoint(live.kind)) {
		ResolveAction(run, i);
	} else if (live.kind == u"theme"_q) {
		live.before = ReadWidgetOpsLatest(*run.message->tap);
	} else if (live.kind == u"visible"_q) {
		RememberHost(state, run, i);
	}
}

// CLIENTS.md performs the actions on a widget awaiting them: any other
// first result leaves every action not performed, by name.
void StartActions(ConformanceRun &run) {
	auto &actions = *run.live;
	actions.startedAt = crl::now();
	run.progress = u"performing the actions"_q;
	for (auto i = 0; i != int(run.actions.size()); ++i) {
		if (!run.actions[i].skipReason.isEmpty()) {
			EmitRow(run, i);
		}
	}
	if (run.resultOutcome == u"awaiting"_q && !run.died) {
		return;
	}
	const auto detail = u"the first result wait ended [%1] with status "
		u"[%2]%3"_q.arg(
			run.resultOutcome,
			run.result.status,
			run.died ? (u", the widget died: "_q + run.dead) : QString());
	for (auto i = 0; i != int(run.actions.size()); ++i) {
		if (run.actions[i].skipReason.isEmpty()) {
			Conclude(run, i, u"not-awaiting"_q, detail);
		}
	}
}

void EndActions(ConformanceState &state, ConformanceRun &run) {
	HideFixtureLayers(
		*state.fixture,
		*run.message,
		u"after the last action"_q);
	CloseWindow(run);
	auto performed = 0;
	auto refused = QStringList();
	for (const auto &action : run.actions) {
		if (action.performed) {
			++performed;
		} else if (action.skipReason.isEmpty()) {
			refused.push_back(u"#%1 %2 (%3)"_q.arg(
				NumberText(action.index),
				action.kind,
				action.refusal));
		}
	}
	run.progress = u"actions done"_q;
	Row(u"CONFORMANCE_ACTIONS"_q, {
		{ u"run"_q, run.key },
		{ u"actions"_q, int(run.actions.size()) },
		{ u"performed"_q, performed },
		{ u"notPerformed"_q, refused.join(u"; "_q) },
		{ u"elapsedMs"_q, Since(run.live->startedAt, crl::now()) },
	});
}

[[nodiscard]] auto ActionOf(
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	return [=]() -> Action& {
		return state->runs[index].live->actions[i];
	};
}

[[nodiscard]] auto SkipOf(
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	return [=] {
		return ActionSkip(*state, state->runs[index], i);
	};
}

void AppendGap(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	runner->add({
		.name = StageName(state->runs[index], i, u"keep the gap"_q),
		.skipReason = SkipOf(state, index, i),
		.run = [=] {
			auto &run = state->runs[index];
			run.live->actions[i].gapFrom = run.live->lastInputAt;
		},
		.until = [=] {
			return GapKept(*state->runs[index].live);
		},
		.then = [=] {
			BeginAction(*state, state->runs[index], i);
		},
		.timeout = kActionGap + kDefaultStageTimeout,
	});
}

void AppendWait(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	auto &run = state->runs[index];
	const auto ms = crl::time(std::lround(
		run.live->actions[i].manifest.value(u"wait"_q).toDouble()));
	const auto action = ActionOf(state, index, i);
	runner->add({
		.name = StageName(run, i, u"wait %1 ms"_q.arg(NumberText(ms))),
		.skipReason = SkipOf(state, index, i),
		.run = [=] {
			action().waitStarted = crl::now();
		},
		.until = [=] {
			return (crl::now() - action().waitStarted) >= ms;
		},
		.then = [=] {
			auto &live = action();
			live.json.insert(
				u"waitedMs"_q,
				qint64(crl::now() - live.waitStarted));
			live.json.insert(
				u"afterLastInputMs"_q,
				Since(state->runs[index].live->lastInputAt, crl::now()));
			Conclude(state->runs[index], i, QString(), QString());
		},
		.timeout = ms + kDefaultStageTimeout,
	});
}

// The show of the action's band of the card, ready to take input, with
// the hover at its start point.
void AppendTargetShow(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	auto &run = state->runs[index];
	const auto action = ActionOf(state, index, i);
	run.live->actions[i].show = AppendShowInChatList(runner, {
		.name = u"widget conformance %1 %2"_q.arg(
			run.key,
			ActionTitle(run, i)),
		.resolve = [=] {
			return ChatListShowTarget{
				WidgetFixtureList(*state->fixture),
				state->runs[index].message->id,
			};
		},
		.rect = [=](ChatListShowView view) {
			return BandLocal(WidgetCardLocal(view), action());
		},
		.ready = [=](ChatListShowView view) {
			const auto card = ReadWidgetCard(view);
			if (!WidgetCardReady(card)) {
				return false;
			}
			action().accepted = card;
			return true;
		},
		.readyDetails = [](ChatListShowView view) {
			return WidgetCardText(ReadWidgetCard(view));
		},
		.hover = [=](ChatListShowView view) {
			const auto card = WidgetCardLocal(view);
			const auto &live = action();
			return live.from
				? CardPoint(card, live.from->point)
				: card.center();
		},
		.skipReason = SkipOf(state, index, i),
		.timeout = kShowTimeout,
	});
}

void AppendPointerInput(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	const auto &live = state->runs[index].live->actions[i];
	const auto planned = live.pointer
		? ConformancePointerPlannedMs(*live.pointer, live.hold, live.steps)
		: crl::time(0);
	const auto action = ActionOf(state, index, i);
	runner->add({
		.name = StageName(
			state->runs[index],
			i,
			live.pointer
				? u"send the timed mouse events"_q
				: u"send the input"_q),
		.skipReason = SkipOf(state, index, i),
		.run = [=] {
			PerformPointer(*state, state->runs[index], i);
		},
		.until = [=] {
			const auto &sequence = action().sequence;
			return !sequence || sequence->finished();
		},
		.then = [=] {
			RecordPointer(state->runs[index], i);
		},
		.timeout = planned + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			const auto &sequence = action().sequence;
			return sequence
				? CompactJson(ConformancePointerJson(sequence->reading()))
				: u"no sequence"_q;
		},
	});
}

void AppendHoverRecord(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	runner->add({
		.name = StageName(state->runs[index], i, u"keep the hover"_q),
		.skipReason = SkipOf(state, index, i),
		.run = [=] {
			RecordHover(state->runs[index], i);
		},
	});
}

void AppendKey(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	runner->add({
		.name = StageName(state->runs[index], i, u"press the key"_q),
		.skipReason = SkipOf(state, index, i),
		.run = [=] {
			PerformKey(*state, state->runs[index], i);
		},
	});
}

// The chain's last switch restored, then the switch to the target: one
// real palette change, whichever palette the launch began in. Both run
// whenever the chain does, so the next phase finds one obligation.
void AppendTheme(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	auto &run = state->runs[index];
	auto &live = run.live->actions[i];
	const auto target = ThemeTarget(live);
	const auto title = u"%1 %2 to %3"_q.arg(
		run.key,
		ActionTitle(run, i),
		target);
	live.previous = state->paletteCurrent;
	if (live.previous) {
		live.restore = AppendPaletteRestore(
			runner,
			live.previous,
			Prefixed(title + u": the chain's last switch"_q));
	}
	live.switched = AppendPaletteSwitch(runner, {
		.name = Prefixed(title),
		.mode = (target == u"dark"_q) ? PaletteMode::Night : PaletteMode::Day,
		.skipReason = [=] {
			return GlobalSkip(*state);
		},
	});
	state->paletteCurrent = live.switched;
	runner->add({
		.name = StageName(run, i, u"judge the palette chain"_q),
		.run = [=] {
			JudgeTheme(*state, state->runs[index], i);
		},
	});
	const auto action = ActionOf(state, index, i);
	runner->add({
		.name = StageName(run, i, u"wait for the widget's theme"_q),
		.skipReason = SkipOf(state, index, i),
		.run = [=] {
			action().waitStarted = crl::now();
		},
		.until = [=] {
			return ThemeDecided(*state, state->runs[index], i);
		},
		.then = [=] {
			RecordTheme(state->runs[index], i);
		},
		.timeout = kThemeBound + kDefaultStageTimeout,
	});
}

void AppendVisible(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	auto &run = state->runs[index];
	auto &live = run.live->actions[i];
	const auto action = ActionOf(state, index, i);
	if (!live.manifest.value(u"visible"_q).toBool()) {
		runner->add({
			.name = StageName(run, i, u"scroll the card off screen"_q),
			.skipReason = SkipOf(state, index, i),
			.run = [=] {
				ScrollAway(*state, state->runs[index], i);
			},
			.until = [=] {
				return AwayDecided(*state, state->runs[index], i);
			},
			.then = [=] {
				RecordAway(*state, state->runs[index], i);
			},
			.timeout = kVisibilityBound + kDefaultStageTimeout,
		});
		return;
	}
	live.show = AppendShowInChatList(runner, {
		.name = u"widget conformance %1 %2"_q.arg(
			run.key,
			ActionTitle(run, i)),
		.resolve = [=] {
			return ChatListShowTarget{
				WidgetFixtureList(*state->fixture),
				state->runs[index].message->id,
			};
		},
		.rect = [](ChatListShowView view) {
			return LoadRect(WidgetCardLocal(view));
		},
		.ready = [](ChatListShowView view) {
			const auto card = ReadWidgetCard(view);
			return card.html && card.runtime;
		},
		.readyDetails = [](ChatListShowView view) {
			return WidgetCardText(ReadWidgetCard(view));
		},
		.skipReason = SkipOf(state, index, i),
		.timeout = kShowTimeout,
	});
	runner->add({
		.name = StageName(run, i, u"wait for the card to count visible"_q),
		.skipReason = SkipOf(state, index, i),
		.run = [=] {
			action().waitStarted = crl::now();
		},
		.until = [=] {
			return BackDecided(*state, state->runs[index], i);
		},
		.then = [=] {
			RecordBack(*state, state->runs[index], i);
		},
		.timeout = kVisibilityBound + kDefaultStageTimeout,
	});
}

void AppendAction(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index,
		int i) {
	const auto &run = state->runs[index];
	if (!run.actions[i].skipReason.isEmpty()) {
		return;
	}
	const auto kind = run.live->actions[i].kind;
	if (kind == u"wait"_q) {
		AppendWait(runner, state, index, i);
		return;
	}
	AppendGap(runner, state, index, i);
	if (kind == u"key"_q) {
		AppendKey(runner, state, index, i);
	} else if (kind == u"theme"_q) {
		AppendTheme(runner, state, index, i);
	} else if (kind == u"visible"_q) {
		AppendVisible(runner, state, index, i);
	} else {
		AppendTargetShow(runner, state, index, i);
		if (kind == u"hover"_q) {
			AppendHoverRecord(runner, state, index, i);
		} else {
			AppendPointerInput(runner, state, index, i);
		}
	}
}

[[nodiscard]] QString FillerText(int index) {
	auto lines = QStringList();
	for (auto line = 1; line <= kFillerLines; ++line) {
		lines.push_back(u"widget conformance filler %1 of %2, line %3"_q.arg(
			NumberText(index + 1),
			NumberText(kFillers),
			NumberText(line)));
	}
	return lines.join(QChar('\n'));
}

void InjectFillers(ConformanceState &state, ConformanceRun &run) {
	run.progress = u"injecting the fillers"_q;
	auto injected = 0;
	for (auto k = 0; k != int(run.fillers.size()); ++k) {
		const auto refusal = InjectTextMessage(
			*state.fixture,
			*run.fillers[k],
			FillerText(k));
		if (!refusal.isEmpty()) {
			AddRefusal(
				run,
				u"fillers-refused"_q,
				u"filler %1: %2"_q.arg(NumberText(k + 1), refusal));
			break;
		}
		++injected;
	}
	Row(u"CONFORMANCE_FILLERS"_q, {
		{ u"run"_q, run.key },
		{ u"injected"_q, injected },
		{ u"fillers"_q, FillerShapes(run) },
	});
}

} // namespace

void AppendRunFillers(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index) {
	auto &run = state->runs[index];
	const auto visible = std::any_of(
		begin(run.actions),
		end(run.actions),
		[](const ActionReading &action) {
			return (action.kind == u"visible"_q);
		});
	if (!visible) {
		return;
	}
	for (auto k = 0; k != kFillers; ++k) {
		run.fillers.push_back(AddWidgetFixtureMessage(
			*state->fixture,
			u"%1 filler %2"_q.arg(run.key, NumberText(k + 1))));
	}
	runner->add({
		.name = Prefixed(run.key + u": inject the fillers above the widget"_q),
		.skipReason = [=] {
			return RunSkip(*state, state->runs[index]);
		},
		.run = [=] {
			InjectFillers(*state, state->runs[index]);
		},
	});
}

void AppendRunActions(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index) {
	auto &run = state->runs[index];
	if (run.actions.empty()) {
		return;
	}
	const auto &manifest = state->tests[run.test].actions;
	run.live = std::make_shared<ConformanceActions>();
	run.live->actions.resize(run.actions.size());
	for (auto i = 0; i != int(run.actions.size()); ++i) {
		PlanAction(
			run.live->actions[i],
			run.actions[i],
			manifest[i].toObject());
	}
	runner->add({
		.name = Prefixed(run.key + u": start the actions"_q),
		.skipReason = [=] {
			return RunSkip(*state, state->runs[index]);
		},
		.run = [=] {
			StartActions(state->runs[index]);
		},
	});
	for (auto i = 0; i != int(run.actions.size()); ++i) {
		AppendAction(runner, state, index, i);
	}
	runner->add({
		.name = Prefixed(run.key + u": close the last action"_q),
		.skipReason = [=] {
			return RunSkip(*state, state->runs[index]);
		},
		.until = [=] {
			return GapKept(*state->runs[index].live);
		},
		.then = [=] {
			EndActions(*state, state->runs[index]);
		},
		.timeout = kActionGap + kDefaultStageTimeout,
	});
}

void RemoveRunFillers(ConformanceState &state, ConformanceRun &run) {
	for (const auto &filler : run.fillers) {
		if (filler->id) {
			RemoveFixtureMessage(*state.fixture, *filler);
		}
	}
}

void AbortRunActions(ConformanceState &state, const QString &reason) {
	for (auto &run : state.runs) {
		if (!run.live) {
			continue;
		}
		for (auto &live : run.live->actions) {
			if (live.sequence && !live.sequence->finished()) {
				live.sequence->abort(reason);
			}
		}
	}
}

} // namespace Test::Conformance

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
