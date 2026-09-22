/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_layer_root.h"

#include "base/unique_qptr.h"
#include "base/weak_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/layer_widget.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"

#include <QtGui/QPainter>

#include "styles/palette.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

// Four empty bands above and below one painted band leave the box eight
// ninths unpainted by itself and its children, which is far above
// kUnpaintedMinPermille, while the painted band keeps the luma spread of the
// grab above kBlankSpreadThreshold. Both halves are load-bearing. LooksBlank
// is checked before the blank-root refusal, so without the band the
// no-content-margin box would stop one refusal earlier, at "target grab still
// looks blank", and this module would quote the wrong text; and the same band
// is what keeps the layer-root grab from reading blank in a theme whose
// st::boxBg equals st::windowBg. Every number comes from a live scaled style
// token, never from a pixel count of its own.
constexpr auto kSkipBands = 4;

struct Fixture {
	base::unique_qptr<Ui::RpWidget> stray;
	base::weak_qptr<Ui::GenericBox> box;
	base::weak_qptr<Ui::RpWidget> grandchild;
};

void SelfTestBoxContent(not_null<Ui::GenericBox*> box, bool noContentMargin) {
	box->setTitle(u"Harness paint root"_q);
	if (noContentMargin) {
		// The whole difference between the two legs of this self-test.
		// Ui::BoxContent's constructor set Qt::WA_OpaquePaintEvent, and this
		// clears it again (ui/layers/box_content.h:224-230), which is the
		// only shape the blank-root refusal fires for. Without it the refusal
		// the second stage quotes is not in force at all, and the plain leg
		// exists precisely so the log says which boxes are refused and which
		// are not, instead of leaving a reader to assume every box is.
		box->setNoContentMargin(true);
	}
	box->setWidth(st::boxWidth);
	const auto content = box->verticalLayout();
	const auto band = st::defaultActiveButton.height;
	Ui::AddSkip(content, band * kSkipBands);
	auto owned = object_ptr<Ui::RpWidget>(content.get());
	const auto row = owned.data();
	row->resize(st::boxWidth, band);
	row->paintOn([=](QPainter &p) {
		p.fillRect(row->rect(), st::attentionButtonFg);
	});
	content->add(std::move(owned));
	Ui::AddSkip(content, band * kSkipBands);
}

[[nodiscard]] QString BoxDetails(const Fixture &fixture) {
	const auto box = fixture.box.get();
	if (!box) {
		return u"the fixture box no longer exists"_q;
	}
	const auto root = PaintingLayerRoot(box);
	return u"box visible=%1 opaquePaintEvent=%2 %3 - root %4"_q
		.arg(box->isVisible() ? 1 : 0)
		.arg(box->testAttribute(Qt::WA_OpaquePaintEvent) ? 1 : 0)
		.arg(
			WidgetDescription(box),
			root.resolved()
				? WidgetDescription(root.widget.data())
				: root.refusal);
}

[[nodiscard]] bool ShowFixtureBox(Fixture &fixture, bool noContentMargin) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	// anim::type::instant is why this fixture needs no animation wait at all:
	// LayerStackWidget::prepareAnimation takes its instant branch
	// (ui/layers/layer_widget.cpp:669-673), which reaches
	// BackgroundWidget::skipAnimation (:151-157) -> checkIfDone (:159-169) ->
	// LayerStackWidget::animationDone (:756-773), and that is where
	// layer->show() runs. The normal path instead hides the layer for the
	// whole animation in prepareForAnimation() (:732-754), which is why the
	// scenario that paid for this resolver had to gate on box->isVisible()
	// and wait for a new layer generation before it could capture anything.
	fixture.box = window->show(
		Box(SelfTestBoxContent, noContentMargin),
		Ui::LayerOption::CloseOther,
		anim::type::instant);
	const auto box = fixture.box.get();
	if (!box) {
		return false;
	}
	// A grandchild of the box, so the refusal-side stage can show the walk
	// climbing more than the single hop the layer stack normally leaves.
	fixture.grandchild = Ui::CreateChild<Ui::RpWidget>(box);
	return true;
}

[[nodiscard]] bool BuildFixture(Fixture &fixture) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	fixture.stray = base::make_unique_q<Ui::RpWidget>(window->widget().get());
	return ShowFixtureBox(fixture, false);
}

} // namespace

void AppendPaintingLayerRootSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		bool built = false;
	};
	// Leaked on purpose, the way the harness's other self-tests leak theirs:
	// the stages outlive this call. The teardown stage releases the fixture,
	// after which the State holds nothing but a released unique_qptr and two
	// empty weak pointers - no rpl subscription to anything the session owns.
	const auto state = new State();
	const auto details = [=] {
		return BoxDetails(state->fixture);
	};
	const auto ready = [=] {
		if (!state->built) {
			return true;
		}
		const auto box = state->fixture.box.get();
		// isVisible(), not !isHidden(): the contract here really is "the box
		// and every ancestor are shown", because a box inside a layer that
		// is still hidden holds no pixels for the capture stage to accept.
		return box
			&& box->isVisible()
			&& PaintingLayerRoot(box).resolved();
	};

	runner->add({
		.name = u"painting layer root self-test: a plain box is not "
			"refused"_q,
		.run = [=] {
			state->built = BuildFixture(state->fixture);
			Check(
				state->built,
				u"fixture gate: the self-test fixture was built"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null"_q);
		},
		.until = ready,
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto box = state->fixture.box.get();
			auto probe = PreparedWidgetCapture();
			const auto accepted = box && probe.prepare(box);
			const auto reason = probe.pendingReason();
			Check(
				accepted,
				u"a plain Ui::GenericBox is accepted as its own render "
				"root, because Ui::BoxContent's constructor sets "
				"Qt::WA_OpaquePaintEvent"_q,
				reason.isEmpty() ? details() : reason);
			Check(
				box && box->testAttribute(Qt::WA_OpaquePaintEvent),
				u"the attribute that acceptance rests on is the one the "
				"box actually carries"_q,
				details());
		},
		.timeoutDetails = details,
	});

	runner->add({
		.name = u"painting layer root self-test: the no-content-margin box "
			"is refused"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			state->built = ShowFixtureBox(state->fixture, true);
		},
		.until = ready,
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto box = state->fixture.box.get();
			const auto root = PaintingLayerRoot(box);
			Check(
				root.resolved()
					&& root.refusal.isEmpty()
					&& box
					&& (root.widget == box->parentWidget()),
				u"a box inside a layer resolves to the Ui::BoxLayerWidget "
				"that paints it, which is its direct parent"_q,
				root.resolved()
					? WidgetDescription(root.widget.data())
					: root.refusal);
			const auto grandchild = state->fixture.grandchild.get();
			const auto deeper = PaintingLayerRoot(grandchild);
			Check(
				grandchild
					&& deeper.resolved()
					&& (deeper.widget == root.widget),
				u"the walk climbs past intermediate widgets, not only one "
				"parent"_q,
				deeper.resolved()
					? WidgetDescription(deeper.widget.data())
					: deeper.refusal);
			auto probe = PreparedWidgetCapture();
			const auto refused = box && !probe.prepare(box);
			const auto reason = probe.pendingReason();
			Check(
				refused,
				u"capturing the bare Ui::GenericBox is still refused once "
				"it has cleared Qt::WA_OpaquePaintEvent"_q,
				reason);
			Check(
				reason.contains(
					u"render root paints no background of its own"_q)
					&& reason.contains(u"Qt::WA_OpaquePaintEvent"_q)
					&& reason.contains(u"Qt::WA_NoSystemBackground"_q),
				u"the refusal still says everything it said before"_q,
				reason);
			Check(
				reason.contains(u"Test::PaintingLayerRoot()"_q),
				u"the refusal now names the resolver that answers it, in "
				"the same breath"_q,
				reason);
		},
		.timeoutDetails = details,
	});

	runner->captureAndInspect(
		u"layer_root_box"_q,
		[=] {
			return state->built
				? PaintingLayerRoot(state->fixture.box.get()).widget.data()
				: nullptr;
		},
		[=](QWidget *widget) {
			const auto box = state->fixture.box.get();
			return box && (widget == box->parentWidget());
		},
		[=](QWidget *widget, const QImage &image) {
			const auto ratio = image.devicePixelRatio();
			Check(
				(image.width() >= int(widget->width() * ratio))
					&& (image.height() >= int(widget->height() * ratio)),
				u"the accepted frame covers the whole painting layer "
				"root"_q,
				u"image=%1x%2 root=%3x%4 devicePixelRatio=%5"_q
					.arg(image.width())
					.arg(image.height())
					.arg(widget->width())
					.arg(widget->height())
					.arg(ratio));
			const auto box = state->fixture.box.get();
			if (!box) {
				Check(
					false,
					u"the resolved root still owns the box whose pixels "
					"the crop is taken from"_q,
					u"the fixture box died before inspection"_q);
				return;
			}
			// A wrong ancestor would still be an accepted paint root, so
			// the frame covering the root proves nothing on its own. The
			// box's own rect, mapped into the root and cropped out of the
			// accepted frame, is what shows the resolver answered the
			// widget that actually holds the box's pixels.
			const auto mapped = Ui::MapFrom(widget, box, box->rect());
			const auto cropped = Crop(
				image,
				QRect(
					int(mapped.x() * ratio),
					int(mapped.y() * ratio),
					int(mapped.width() * ratio),
					int(mapped.height() * ratio)));
			Check(
				!cropped.isNull() && !LooksBlank(cropped),
				u"the resolved root really holds the box's own pixels: the "
				"box's rect mapped into it and cropped out of the accepted "
				"frame is not blank"_q,
				u"mapped=%1,%2 %3x%4 cropped=%5x%6"_q
					.arg(mapped.x())
					.arg(mapped.y())
					.arg(mapped.width())
					.arg(mapped.height())
					.arg(cropped.width())
					.arg(cropped.height()));
			Check(
				CaptureInLayerRoot(box, u"layer_root_box_cropped"_q),
				u"the composed helper saves a frame cropped to the box, so "
				"the box's rect maps inside its Ui::BoxLayerWidget and "
				"MisframedDetails does not fire"_q,
				details());
		},
		kDefaultStageTimeout,
		[=](QWidget*) {
			return details();
		});

	runner->add({
		.name = u"painting layer root self-test: refusal"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto missing = PaintingLayerRoot(nullptr);
			Check(
				!missing.resolved()
					&& (missing.widget == nullptr)
					&& !missing.refusal.isEmpty(),
				u"a null target is refused by name, never walked and never "
				"answered with a null the caller would dereference"_q,
				missing.refusal);
			const auto window = Core::App().activePrimaryWindow();
			const auto top = window ? window->widget().get() : nullptr;
			const auto topDescription = top
				? WidgetDescription(top)
				: QString();
			const auto stray = PaintingLayerRoot(state->fixture.stray.get());
			Check(
				!stray.resolved()
					&& (stray.widget == nullptr)
					&& stray.refusal.contains(u"no Ui::BoxLayerWidget"_q)
					&& stray.refusal.contains(u"walked"_q)
					&& !topDescription.isEmpty()
					&& stray.refusal.contains(topDescription),
				u"a widget with no Ui::BoxLayerWidget ancestor is refused, "
				"and the refusal names the window the walk stopped at"_q,
				stray.refusal);
			const auto owner = PaintingLayerRoot(top);
			Check(
				!owner.resolved()
					&& (owner.widget == nullptr)
					&& owner.refusal.contains(u"walked 1 widget(s)"_q),
				u"a widget that is its own window is refused after a single "
				"hop, which is the mechanism that keeps the resolver off a "
				"Ui::PopupMenu"_q,
				owner.refusal);
		},
	});

	runner->add({
		.name = u"painting layer root self-test: teardown"_q,
		.run = [=] {
			// Last on purpose. A timed-out stage or the watchdog skips every
			// stage after it, so anything still held here would outlive the
			// run: the fixture's layer would stay over the primary window
			// for the rest of the process, and its stray widget would stay
			// parented to that window. hideLayer destroys the
			// Ui::BoxLayerWidget and with it the box and the grandchild,
			// which is why both weak pointers are dropped in the same breath.
			if (const auto window = Core::App().activePrimaryWindow()) {
				window->hideLayer(anim::type::instant);
			}
			state->fixture.stray = nullptr;
			state->fixture.box.reset();
			state->fixture.grandchild.reset();
			Note(u"painting layer root self-test: fixture released, "
				u"stray=%1 box=%2 grandchild=%3"_q
				.arg(state->fixture.stray ? 1 : 0)
				.arg(state->fixture.box ? 1 : 0)
				.arg(state->fixture.grandchild ? 1 : 0));
		},
	});
}

} // namespace Test

#endif // _DEBUG
