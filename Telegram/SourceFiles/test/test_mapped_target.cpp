/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_mapped_target.h"

#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_controller.h"

#include <QtGui/QPainter>

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

constexpr auto kWidthFactor = 4;
constexpr auto kElasticViewportFactor = 3;
constexpr auto kAreaViewportFactor = 2;
constexpr auto kElasticContentFactor = 12;
constexpr auto kAreaContentFactor = 6;
constexpr auto kSkipBands = 4;

struct Fixture {
	base::unique_qptr<Ui::RpWidget> owner;
	base::unique_qptr<Ui::RpWidget> stray;
	base::unique_qptr<Ui::RpWidget> detached;
	Ui::ElasticScroll *elastic = nullptr;
	Ui::ScrollArea *area = nullptr;
	Ui::RpWidget *elasticOrigin = nullptr;
	Ui::RpWidget *elasticChild = nullptr;
	Ui::RpWidget *areaOrigin = nullptr;
	Ui::RpWidget *areaChild = nullptr;
	Ui::RpWidget *strayBand = nullptr;
	Ui::RpWidget *translated = nullptr;
	Ui::RpWidget *overhang = nullptr;
	QRect elasticLocal;
	QRect areaLocal;
};

[[nodiscard]] QString FixtureDetails(const Fixture &fixture) {
	const auto elastic = ReadMappedTarget(
		fixture.owner.get(),
		fixture.elasticOrigin,
		fixture.elasticLocal);
	const auto area = ReadMappedTarget(
		fixture.owner.get(),
		fixture.areaOrigin,
		fixture.areaLocal);
	return u"elastic {%1} area {%2} innerY=%3 areaInnerY=%4"_q
		.arg(MappedTargetDetails(elastic))
		.arg(MappedTargetDetails(area))
		.arg(fixture.elasticOrigin ? fixture.elasticOrigin->y() : 0)
		.arg(fixture.areaOrigin ? fixture.areaOrigin->y() : 0);
}

void PaintFill(not_null<Ui::RpWidget*> widget, const style::color &color) {
	widget->paintOn([=](QPainter &p) {
		p.fillRect(widget->rect(), color);
	});
}

// Two halves of strongly different lightness, so LooksBlank accepts the
// frame: st::windowBgActive and st::attentionButtonFg differ by too little.
void PaintSplit(not_null<Ui::RpWidget*> widget) {
	widget->paintOn([=](QPainter &p) {
		p.fillRect(widget->rect(), st::attentionButtonFg);
		p.fillRect(
			QRect(0, 0, widget->width() / 2, widget->height()),
			st::windowFg);
	});
}

[[nodiscard]] bool BuildFixture(Fixture &fixture) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto row = st::defaultActiveButton.height;
	const auto width = row * kWidthFactor;
	const auto header = row;
	const auto elasticH = row * kElasticViewportFactor;
	const auto areaH = row * kAreaViewportFactor;
	const auto footer = row * 3;
	const auto ownerH = header + elasticH + areaH + footer;

	fixture.owner = base::make_unique_q<Ui::RpWidget>(window->widget().get());
	const auto owner = fixture.owner.get();
	owner->setAttribute(Qt::WA_OpaquePaintEvent);
	PaintFill(owner, st::windowBg);
	owner->setGeometry(0, 0, width, ownerH);

	const auto elastic = Ui::CreateChild<Ui::ElasticScroll>(
		owner,
		st::defaultScrollArea);
	elastic->setGeometry(0, header, width, elasticH);
	auto elasticInner = object_ptr<Ui::RpWidget>(elastic);
	elasticInner->resize(width, row * kElasticContentFactor);
	const auto elasticOrigin = elasticInner.data();
	const auto elasticChild = Ui::CreateChild<Ui::RpWidget>(elasticOrigin);
	elasticChild->setGeometry(0, row * 2, width, row * 2);
	PaintFill(elasticChild, st::attentionButtonFg);
	elasticChild->show();
	elastic->setOwnedWidget(std::move(elasticInner));
	elastic->show();

	const auto area = Ui::CreateChild<Ui::ScrollArea>(
		owner,
		st::defaultScrollArea);
	area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	area->setGeometry(0, header + elasticH, width, areaH);
	auto areaInner = object_ptr<Ui::RpWidget>(area);
	areaInner->resize(width, row * kAreaContentFactor);
	const auto areaOrigin = areaInner.data();
	const auto areaChild = Ui::CreateChild<Ui::RpWidget>(areaOrigin);
	areaChild->setGeometry(0, row * 2, width, row * 2);
	PaintFill(areaChild, st::attentionButtonFg);
	areaChild->show();
	area->setOwnedWidget(std::move(areaInner));
	area->show();

	// Inside the header band at a nonzero x and y, so its parent-relative
	// geometry handed as child-local maps to a doubled position that still
	// fits the owner: containment alone accepts it.
	const auto translated = Ui::CreateChild<Ui::RpWidget>(owner);
	translated->setGeometry(row, row / 4, row * 2, row / 2);
	PaintSplit(translated);
	translated->show();

	const auto overhang = Ui::CreateChild<Ui::RpWidget>(owner);
	overhang->setGeometry(width - row, row / 4, row * 2, row / 2);
	PaintFill(overhang, st::attentionButtonFg);
	overhang->show();

	owner->show();
	Ui::SendPendingMoveResizeEvents(elastic);
	if (const auto content = elastic->widget()) {
		Ui::SendPendingMoveResizeEvents(content);
	}
	Ui::SendPendingMoveResizeEvents(area);
	if (const auto content = area->widget()) {
		Ui::SendPendingMoveResizeEvents(content);
	}

	fixture.elastic = elastic;
	fixture.area = area;
	fixture.elasticOrigin = elasticOrigin;
	fixture.elasticChild = elasticChild;
	fixture.areaOrigin = areaOrigin;
	fixture.areaChild = areaChild;
	fixture.translated = translated;
	fixture.overhang = overhang;
	fixture.elasticLocal = elasticChild->geometry();
	fixture.areaLocal = areaChild->geometry();

	fixture.stray = base::make_unique_q<Ui::RpWidget>(window->widget().get());
	const auto stray = fixture.stray.get();
	const auto strayH = row * (kSkipBands * 2 + 1);
	stray->setGeometry(width, 0, width, strayH);
	const auto band = Ui::CreateChild<Ui::RpWidget>(stray);
	band->setGeometry(0, row * kSkipBands, width, row);
	PaintFill(band, st::attentionButtonFg);
	band->show();
	stray->show();
	Ui::SendPendingMoveResizeEvents(stray);
	fixture.strayBand = band;

	// A parentless widget, never shown: no window and no coordinate owner.
	fixture.detached = base::make_unique_q<Ui::RpWidget>();
	fixture.detached->resize(row * 2, row / 2);
	return true;
}

// The typed children are raw pointers into the owner and the stray, so
// they live exactly while those do.
[[nodiscard]] bool TypedFixtureAlive(const Fixture &fixture) {
	if (fixture.owner && fixture.stray && fixture.detached) {
		return true;
	}
	Check(
		false,
		u"typed mapped rects: the fixture is alive"_q,
		u"owner=%1 stray=%2 detached=%3"_q
			.arg(fixture.owner ? 1 : 0)
			.arg(fixture.stray ? 1 : 0)
			.arg(fixture.detached ? 1 : 0));
	return false;
}

// One physical target, the translated child, in both valid coordinate
// spaces, then the doubled-position premise and the wrong pairings.
void CheckTypedEquality(const Fixture &fixture) {
	if (!TypedFixtureAlive(fixture)) {
		return;
	}
	const auto owner = fixture.owner.get();
	const auto translated = fixture.translated;
	const auto local = translated->rect();
	const auto geometry = translated->geometry();
	const auto expected = Ui::MapFrom(owner, translated, local);
	const auto asLocal = ReadMappedTarget(
		owner,
		MappedRect::Local(translated, local));
	const auto asParent = ReadMappedTarget(
		owner,
		MappedRect::InParent(translated, geometry));
	Check(
		(translated->x() != 0)
			&& (translated->y() != 0)
			&& asLocal.resolved()
			&& asParent.resolved()
			&& (asLocal.mapped == asParent.mapped)
			&& (asLocal.mapped == expected)
			&& (asLocal.mapped.size() == translated->size()),
		u"typed Local and InParent rects of one translated child map to "
		"the same full bounds in the painted owner, equal to the "
		"independent Ui::MapFrom of the child's own rect"_q,
		u"pos=%1,%2 expected=%3 local={%4} parent={%5}"_q
			.arg(translated->x())
			.arg(translated->y())
			.arg(RectText(expected))
			.arg(MappedTargetDetails(asLocal))
			.arg(MappedTargetDetails(asParent)));

	const auto doubled = ReadMappedTarget(owner, translated, geometry);
	Check(
		doubled.resolved()
			&& (doubled.mapped == geometry.translated(translated->pos()))
			&& (doubled.mapped != expected),
		u"premise: the untyped reading of the child's parent-relative "
		"geometry with the child as origin resolves at the doubled "
		"position, so containment alone accepts it"_q,
		u"expected=%1 doubled={%2}"_q.arg(
			RectText(expected),
			MappedTargetDetails(doubled)));

	const auto failuresBefore = FailureCount();
	const auto wrong = ReadMappedTarget(
		owner,
		MappedRect::Local(translated, geometry));
	auto probe = PreparedWidgetCapture();
	const auto prepared = probe.prepare(
		owner,
		MappedRect::Local(translated, geometry));
	const auto reverse = ReadMappedTarget(
		owner,
		MappedRect::InParent(translated, local));
	const auto failuresAfter = FailureCount();
	Check(
		!wrong.resolved()
			&& wrong.refusal.startsWith(u"coordinate mismatch"_q)
			&& wrong.refusal.contains(RectText(geometry))
			&& wrong.refusal.contains(RectText(local))
			&& wrong.refusal.contains(WidgetDescription(translated))
			&& wrong.refusal.contains(WidgetDescription(owner)),
		u"typed: the child's parent-relative geometry handed as child-local "
		"is refused as a coordinate mismatch naming the requested rect, "
		"the child's own bounds, the origin and painted child and the "
		"owner"_q,
		MappedTargetDetails(wrong));
	Check(
		!prepared
			&& !probe.widget()
			&& probe.image().isNull()
			&& (probe.pendingReason() == wrong.refusal)
			&& (failuresAfter == failuresBefore),
		u"typed: prepare stores the same coordinate-mismatch refusal, "
		"accepts no frame and logs no failure"_q,
		u"failures=%1->%2 reason=%3"_q
			.arg(failuresBefore)
			.arg(failuresAfter)
			.arg(probe.pendingReason()));
	Check(
		!reverse.resolved()
			&& reverse.refusal.startsWith(u"coordinate mismatch"_q),
		u"typed: the reverse pairing, the child's own rect handed as "
		"parent-relative, is refused too"_q,
		MappedTargetDetails(reverse));
}

// The typed reading keeps every complete-target refusal: clipped by the
// owner or by the viewport with the full requested size mapped, detached,
// wrong owner, a hidden parent-relative child, blank and nonpainting root.
void CheckTypedRefusals(const Fixture &fixture) {
	if (!TypedFixtureAlive(fixture)) {
		return;
	}
	const auto owner = fixture.owner.get();
	const auto stray = fixture.stray.get();
	const auto detached = fixture.detached.get();
	const auto failuresBefore = FailureCount();

	const auto overhang = ReadMappedTarget(
		owner,
		MappedRect::Local(fixture.overhang, fixture.overhang->rect()));
	Check(
		!overhang.resolved()
			&& overhang.refusal.contains(
				u"not fully inside the painted owner"_q)
			&& (overhang.mapped.size() == fixture.overhang->size()),
		u"typed: a child hanging over the owner's edge is refused whole, "
		"and the stored mapping keeps the full requested size"_q,
		MappedTargetDetails(overhang));

	const auto clipped = ReadMappedTarget(
		owner,
		MappedRect::Local(fixture.elasticChild, fixture.elasticChild->rect()));
	Check(
		!clipped.resolved()
			&& clipped.refusal.contains(u"relevant viewport"_q)
			&& (clipped.mapped.size() == fixture.elasticChild->size()),
		u"typed: the ElasticScroll child scrolled back to the top is "
		"refused by its viewport, with the full mapped size"_q,
		MappedTargetDetails(clipped));

	const auto parentless = ReadMappedTarget(
		owner,
		MappedRect::InParent(detached, detached->geometry()));
	Check(
		!parentless.resolved()
			&& parentless.refusal.startsWith(u"detached"_q),
		u"typed: a parent-relative rect of a parentless widget is refused "
		"as detached"_q,
		MappedTargetDetails(parentless));

	const auto foreign = ReadMappedTarget(
		owner,
		MappedRect::Local(detached, detached->rect()));
	const auto band = ReadMappedTarget(
		owner,
		MappedRect::Local(fixture.strayBand, fixture.strayBand->rect()));
	Check(
		!foreign.resolved()
			&& foreign.refusal.startsWith(u"wrong owner"_q)
			&& !band.resolved()
			&& band.refusal.startsWith(u"wrong owner"_q),
		u"typed: a detached widget and a painted widget of another owner "
		"are refused as the wrong owner"_q,
		u"detached={%1} band={%2}"_q.arg(
			MappedTargetDetails(foreign),
			MappedTargetDetails(band)));

	fixture.translated->hide();
	const auto hidden = ReadMappedTarget(
		owner,
		MappedRect::InParent(
			fixture.translated,
			fixture.translated->geometry()));
	fixture.translated->show();
	Check(
		!hidden.resolved()
			&& hidden.refusal.startsWith(u"painted widget is not visible"_q),
		u"typed: a hidden child's parent-relative rect is refused although "
		"its parent shows"_q,
		MappedTargetDetails(hidden));

	auto blank = PreparedWidgetCapture();
	const auto blankReady = blank.prepare(
		stray,
		MappedRect::Local(fixture.strayBand, fixture.strayBand->rect()));
	auto unpainted = PreparedWidgetCapture();
	const auto unpaintedReady = unpainted.prepare(
		stray,
		MappedRect::Local(stray, stray->rect()));
	const auto failuresAfter = FailureCount();
	Check(
		!blankReady && blank.pendingReason().contains(u"looks blank"_q),
		u"typed: prepare of a uniform band still refuses it as blank"_q,
		blank.pendingReason());
	Check(
		!unpaintedReady
			&& unpainted.pendingReason().contains(
				u"render root paints no background"_q),
		u"typed: prepare of a nonpainting root still refuses it as a "
		"capture root"_q,
		unpainted.pendingReason());
	Check(
		failuresAfter == failuresBefore,
		u"typed: every refusal above is stored, none logs a failure"_q,
		u"failures=%1->%2"_q.arg(failuresBefore).arg(failuresAfter));
}

// The valid painted crop through the shared capture path, from both
// coordinate spaces of the same translated child.
void CheckTypedCrop(const Fixture &fixture) {
	if (!TypedFixtureAlive(fixture)) {
		return;
	}
	const auto owner = fixture.owner.get();
	const auto translated = fixture.translated;
	const auto local = MappedRect::Local(translated, translated->rect());
	const auto parent = MappedRect::InParent(
		translated,
		translated->geometry());
	auto localCapture = PreparedWidgetCapture();
	const auto localReady = localCapture.prepare(owner, local);
	auto parentCapture = PreparedWidgetCapture();
	const auto parentReady = parentCapture.prepare(owner, parent);
	const auto localReading = ReadMappedTarget(owner, local);
	const auto parentReading = ReadMappedTarget(owner, parent);
	const auto &image = localCapture.image();
	const auto ratio = image.devicePixelRatio();
	const auto expected = translated->size() * ratio;
	Check(
		localReady
			&& (localCapture.widget() == owner)
			&& (image.size() == expected),
		u"typed: prepare of the translated child's own rect accepts the "
		"painted owner with the child's complete extent at the image's "
		"ratio"_q,
		u"image=%1x%2 expected=%3x%4 dpr=%5 reason=%6 reading={%7}"_q
			.arg(image.width())
			.arg(image.height())
			.arg(expected.width())
			.arg(expected.height())
			.arg(ratio)
			.arg(localCapture.pendingReason())
			.arg(MappedTargetDetails(localReading)));
	Check(
		parentReady
			&& (parentCapture.widget() == owner)
			&& localReading.resolved()
			&& parentReading.resolved()
			&& (parentReading.mapped == localReading.mapped)
			&& !image.isNull()
			&& (parentCapture.image() == image),
		u"typed: prepare of the same child's parent-relative geometry "
		"accepts the same mapping and a byte-equal image"_q,
		u"reason=%1 local={%2} parent={%3}"_q
			.arg(parentCapture.pendingReason())
			.arg(MappedTargetDetails(localReading))
			.arg(MappedTargetDetails(parentReading)));
	Check(
		CaptureMappedTarget(owner, local, u"mapped_target_typed_local"_q),
		u"typed: CaptureMappedTarget saves the translated child's complete "
		"frame"_q,
		MappedTargetDetails(localReading));
}

} // namespace

void AppendMappedTargetSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		int failuresBefore = 0;
		int failuresAfter = 0;
		bool built = false;
	};
	const auto state = new State();
	const auto details = [=] {
		return FixtureDetails(state->fixture);
	};

	runner->add({
		.name = u"mapped-target self-test: a viewport-clipped child that "
			"still fits the owner is unready"_q,
		.run = [=] {
			state->built = BuildFixture(state->fixture);
			Check(
				state->built,
				u"fixture gate: the self-test fixture was built"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null"_q);
			if (!state->built) {
				return;
			}
			state->fixture.elastic->scrollToY(0);
			state->fixture.area->scrollToY(0);
			Ui::SendPendingMoveResizeEvents(state->fixture.elastic);
			Ui::SendPendingMoveResizeEvents(state->fixture.area);
			if (const auto content = state->fixture.elastic->widget()) {
				Ui::SendPendingMoveResizeEvents(content);
			}
			if (const auto content = state->fixture.area->widget()) {
				Ui::SendPendingMoveResizeEvents(content);
			}
			state->failuresBefore = FailureCount();
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto owner = state->fixture.owner.get();
			const auto elastic = ReadMappedTarget(
				owner,
				state->fixture.elasticOrigin,
				state->fixture.elasticLocal);
			auto probe = PreparedWidgetCapture();
			const auto prepared = probe.prepare(
				owner,
				state->fixture.elasticOrigin,
				state->fixture.elasticLocal);
			state->failuresAfter = FailureCount();
			Check(
				!elastic.resolved()
					&& !MappedTargetReady(
						owner,
						state->fixture.elasticOrigin,
						state->fixture.elasticLocal)
					&& (elastic.mapped.size()
						== state->fixture.elasticLocal.size())
					&& elastic.refusal.contains(u"relevant viewport"_q)
					&& elastic.refusal.contains(RectText(elastic.mapped))
					&& elastic.refusal.contains(RectText(elastic.inViewport))
					&& state->fixture.elastic
					&& elastic.refusal.contains(
						WidgetDescription(state->fixture.elastic)),
				u"an ElasticScroll-clipped child that still maps inside "
				"the painted owner is unready; the refusal names the "
				"mapped rect and the ElasticScroll as the clipper, not "
				"the inner widget ElasticScroll::viewport() returns"_q,
				MappedTargetDetails(elastic));
			Check(
				!prepared
					&& probe.pendingReason().contains(u"relevant viewport"_q)
					&& (state->failuresAfter == state->failuresBefore),
				u"prepare(owner, origin, rect) stores that viewport "
				"refusal and does not log a FAIL"_q,
				probe.pendingReason());
			Check(
				owner->rect().contains(elastic.mapped)
					&& (elastic.mapped.size()
						== state->fixture.elasticLocal.size()),
				u"the requested mapping still fits the painted owner and "
				"was not intersected down to the visible overlap"_q,
				u"owner=%1 mapped=%2 local=%3"_q
					.arg(RectText(owner->rect()))
					.arg(RectText(elastic.mapped))
					.arg(RectText(state->fixture.elasticLocal)));
			const auto area = ReadMappedTarget(
				owner,
				state->fixture.areaOrigin,
				state->fixture.areaLocal);
			Check(
				!area.resolved()
					&& state->fixture.area
					&& (area.inViewport.size()
						== state->fixture.areaLocal.size())
					&& area.refusal.contains(u"relevant viewport"_q)
					&& area.refusal.contains(
						WidgetDescription(state->fixture.area->viewport())),
				u"the sibling Ui::ScrollArea child's clipper is "
				"viewport(), not the scroll area, and a clipped child is "
				"unready"_q,
				MappedTargetDetails(area));
			const auto oversized = QRect(
				state->fixture.elasticLocal.topLeft(),
				state->fixture.elasticLocal.size()
					+ QSize(0, owner->height()));
			const auto overlap = ReadMappedTarget(
				owner,
				state->fixture.elasticOrigin,
				oversized);
			Check(
				!overlap.resolved()
					&& (overlap.local.size() == oversized.size())
					&& (overlap.mapped.size() == oversized.size())
					&& overlap.refusal.contains(u"not fully inside"_q),
				u"overlap alone is not readiness: a rect that intersects "
				"the owner or viewport but is not contained stays unready, "
				"and the stored mapping keeps the full requested size"_q,
				MappedTargetDetails(overlap));
			const auto empty = ReadMappedTarget(
				owner,
				state->fixture.elasticOrigin,
				QRect());
			Check(
				!empty.resolved()
					&& empty.refusal.contains(u"empty"_q),
				u"an empty target rect is unready by name"_q,
				MappedTargetDetails(empty));
			const auto missing = ReadMappedTarget(
				nullptr,
				nullptr,
				state->fixture.elasticLocal);
			Check(
				!missing.resolved()
					&& missing.refusal.contains(u"no painted owner"_q),
				u"a null owner and origin are refused by name, never "
				"dereferenced"_q,
				missing.refusal);
			const auto outside = QRect(
				owner->width(),
				owner->height(),
				16,
				16);
			const auto misframed = MisframedDetails(owner, outside);
			Check(
				!misframed.isEmpty()
					&& misframed.contains(
						u"requested rect is not fully inside"_q),
				u"CaptureMappedRect's misframing refusal still fires for "
				"a rect outside the painted owner, observed through "
				"MisframedDetails so this stage logs no failure"_q,
				misframed);
			const auto layerOrigin = PaintingLayerRoot(
				state->fixture.elasticOrigin);
			const auto layerOwner = PaintingLayerRoot(owner);
			Check(
				!layerOrigin.resolved()
					&& !layerOwner.resolved()
					&& layerOrigin.refusal.contains(
						u"no Ui::BoxLayerWidget"_q)
					&& layerOwner.refusal.contains(
						u"no Ui::BoxLayerWidget"_q),
				u"PaintingLayerRoot resolves boxes inside layers "
				"specifically and does not answer a scroll fixture; "
				"callers still own semantic navigation and item identity"_q,
				u"origin=%1 owner=%2"_q
					.arg(layerOrigin.refusal, layerOwner.refusal));
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"mapped-target self-test: scrolling makes the same full "
			"target capturable"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto local = state->fixture.elasticLocal;
			state->fixture.elastic->scrollToY(
				local.top(),
				local.top() + local.height());
			Ui::SendPendingMoveResizeEvents(state->fixture.elastic);
			if (const auto content = state->fixture.elastic->widget()) {
				Ui::SendPendingMoveResizeEvents(content);
			}
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto owner = state->fixture.owner.get();
			const auto origin = state->fixture.elasticOrigin;
			const auto local = state->fixture.elasticLocal;
			const auto reading = ReadMappedTarget(owner, origin, local);
			auto probe = PreparedWidgetCapture();
			const auto prepared = probe.prepare(owner, origin, local);
			Check(
				reading.resolved()
					&& MappedTargetReady(owner, origin, local)
					&& (reading.origin.data() == origin)
					&& (reading.owner.data() == owner)
					&& (reading.viewport.data() == state->fixture.elastic)
					&& (reading.local == local)
					&& (reading.mapped.size() == local.size())
					&& origin
					&& (origin->y() != 0),
				u"ordinary scrolling makes the same origin, owner and "
				"local rect ready, with a nonzero inner offset, without "
				"shrinking the requested rectangle"_q,
				MappedTargetDetails(reading));
			Check(
				prepared && probe.widget() == owner && !probe.image().isNull(),
				u"prepare(owner, origin, rect) accepts the painted owner "
				"once the complete mapped target is visible"_q,
				probe.pendingReason());
			const auto saved = CaptureMappedTarget(
				owner,
				origin,
				local,
				u"mapped_target_visible"_q);
			const auto &image = probe.image();
			const auto ratio = image.devicePixelRatio();
			Check(
				saved
					&& (image.width() >= int(local.width() * ratio))
					&& (image.height() >= int(local.height() * ratio)),
				u"CaptureMappedTarget saves the full requested frame of "
				"the wholly visible nonzero-offset target"_q,
				u"saved=%1 image=%2x%3 local=%4 dpr=%5"_q
					.arg(saved ? 1 : 0)
					.arg(image.width())
					.arg(image.height())
					.arg(RectText(local))
					.arg(ratio));
			Check(
				CaptureMappedRect(
					owner,
					origin,
					local,
					u"mapped_target_owner_safeguard"_q),
				u"CaptureMappedRect still accepts the same contained "
				"mapping as the owner-containment safeguard"_q,
				details());
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"mapped-target self-test: the ScrollArea sibling becomes "
			"ready after scrolling"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto local = state->fixture.areaLocal;
			state->fixture.area->scrollToY(
				local.top(),
				local.top() + local.height());
			Ui::SendPendingMoveResizeEvents(state->fixture.area);
			if (const auto content = state->fixture.area->widget()) {
				Ui::SendPendingMoveResizeEvents(content);
			}
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto reading = ReadMappedTarget(
				state->fixture.owner.get(),
				state->fixture.areaOrigin,
				state->fixture.areaLocal);
			Check(
				reading.resolved()
					&& state->fixture.area
					&& (reading.viewport.data()
						== state->fixture.area->viewport())
					&& (reading.local == state->fixture.areaLocal)
					&& (reading.mapped.size()
						== state->fixture.areaLocal.size()),
				u"after ordinary scrolling the same ScrollArea-local rect "
				"is ready and still names viewport() as the clipper"_q,
				MappedTargetDetails(reading));
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"mapped-target self-test: hidden origin and nonpainting "
			"root"_q,
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto owner = state->fixture.owner.get();
			const auto origin = state->fixture.elasticOrigin;
			origin->hide();
			const auto hidden = ReadMappedTarget(
				owner,
				origin,
				state->fixture.elasticLocal);
			origin->show();
			Check(
				!hidden.resolved()
					&& hidden.refusal.contains(u"not visible"_q),
				u"a hidden rectangle origin is unready by name"_q,
				MappedTargetDetails(hidden));
			const auto stray = state->fixture.stray.get();
			auto unpainted = PreparedWidgetCapture();
			const auto unpaintedReady = unpainted.prepare(
				stray,
				stray,
				stray->rect());
			auto painted = PreparedWidgetCapture();
			const auto paintedReady = painted.prepare(
				owner,
				origin,
				state->fixture.elasticLocal);
			Check(
				!unpaintedReady
					&& unpainted.pendingReason().contains(
						u"render root paints no background"_q),
				u"a nonpainting render root is not capture-ready merely "
				"because its geometry fits"_q,
				unpainted.pendingReason());
			Check(
				paintedReady,
				u"the painted owner of the same visible mapping remains "
				"capture-ready"_q,
				painted.pendingReason());
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"mapped-target self-test: typed coordinate owners map one "
			"translated child identically"_q,
		.then = [=] {
			if (state->built) {
				CheckTypedEquality(state->fixture);
			}
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"mapped-target self-test: typed refusals: clipped, "
			"detached, wrong owner, hidden, blank and nonpainting root"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			state->fixture.elastic->scrollToY(0);
			Ui::SendPendingMoveResizeEvents(state->fixture.elastic);
			if (const auto content = state->fixture.elastic->widget()) {
				Ui::SendPendingMoveResizeEvents(content);
			}
		},
		.then = [=] {
			if (state->built) {
				CheckTypedRefusals(state->fixture);
			}
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"mapped-target self-test: typed painted crop"_q,
		.then = [=] {
			if (state->built) {
				CheckTypedCrop(state->fixture);
			}
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"mapped-target self-test: teardown"_q,
		.run = [=] {
			state->fixture.owner = nullptr;
			state->fixture.stray = nullptr;
			state->fixture.detached = nullptr;
			state->fixture.elastic = nullptr;
			state->fixture.area = nullptr;
			state->fixture.elasticOrigin = nullptr;
			state->fixture.elasticChild = nullptr;
			state->fixture.areaOrigin = nullptr;
			state->fixture.areaChild = nullptr;
			state->fixture.strayBand = nullptr;
			state->fixture.translated = nullptr;
			state->fixture.overhang = nullptr;
			Note(u"mapped-target self-test: fixture released, owner=%1 "
				u"stray=%2 elastic=%3 detached=%4"_q
				.arg(state->fixture.owner ? 1 : 0)
				.arg(state->fixture.stray ? 1 : 0)
				.arg(state->fixture.elastic ? 1 : 0)
				.arg(state->fixture.detached ? 1 : 0));
		},
	});
}

} // namespace Test

#endif // _DEBUG
