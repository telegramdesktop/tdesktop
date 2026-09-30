/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_text_drop.h"

#include "base/unique_qptr.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDragLeaveEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>
#include <private/qdnd_p.h>

#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

const auto kText = u"harness drop reaches the field"_q;
const auto kSeed = u"seed"_q;

// A parentless, never-shown host that accepts every drop offered to it,
// so it stands in for a product drop area above the field.
class DropCounter final : public Ui::RpWidget {
public:
	explicit DropCounter(QWidget *parent);

	[[nodiscard]] int enters() const;
	[[nodiscard]] int drops() const;
	[[nodiscard]] int leaves() const;
	void reset();

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;

private:
	int _enters = 0;
	int _drops = 0;
	int _leaves = 0;

};

// Turns the editor read-only on the drop itself, after it accepted the
// enter. Installed last on the viewport, it runs before the viewport's
// QAbstractScrollAreaFilter, so the text control sees the drop read-only.
class ReadOnlyOnDrop final : public QObject {
public:
	explicit ReadOnlyOnDrop(not_null<QTextEdit*> editor);

	[[nodiscard]] int fired() const;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	not_null<QTextEdit*> _editor;
	int _fired = 0;

};

enum class HookMode {
	Take,
	Leave,
};

struct Fixture {
	base::unique_qptr<DropCounter> counter;
	Ui::InputField *field = nullptr;
	int changes = 0;
	rpl::lifetime lifetime;
};

struct Leg {
	TextDrop drop;
	int clipboardChanges = 0;
	int enters = 0;
	int drops = 0;
	int leaves = 0;
	int hookChecks = 0;
	int hookInserts = 0;
	QStringList hookTexts;
	QString fieldText;
	QString dragTarget;
	bool targetNull = false;
};

struct State {
	Fixture fixture;
	base::unique_qptr<QObject> clipboardContext;
	std::unique_ptr<ReadOnlyOnDrop> filter;
	HookMode mode = HookMode::Take;
	int hookChecks = 0;
	int hookInserts = 0;
	QStringList hookTexts;
	int clipboardChanges = 0;
	std::array<int, 3> clipboardModes = {};
	Leg taken;
	Leg left;
	Leg noDrops;
	Leg disabled;
	Leg ignored;
	Leg refusedDrop;
	bool gateViewportEnabled = false;
	bool gateAcceptDrops = false;
	bool gateEmpty = false;
	bool leftEmptyBefore = false;
	int leftChanges = 0;
	int failuresBefore = 0;
	int failuresAfter = 0;
	bool premiseEnterAccepted = false;
	bool premiseTargetWasCounter = false;
	bool premiseTargetNullAfter = false;
	int premiseEnters = 0;
	int premiseDrops = 0;
	int premiseHookChecks = 0;
	int premiseHookInserts = 0;
	QString premiseText;
	int filterFired = 0;
	bool built = false;
};

[[nodiscard]] QString DragTargetReading(QObject *counter) {
	const auto manager = QDragManager::self();
	const auto target = manager ? manager->currentTarget() : nullptr;
	if (!target) {
		return u"null"_q;
	} else if (target == counter) {
		return u"counter"_q;
	}
	return u"other"_q;
}

void ResetLogs(not_null<State*> state) {
	state->hookChecks = 0;
	state->hookInserts = 0;
	state->hookTexts.clear();
	if (const auto counter = state->fixture.counter.get()) {
		counter->reset();
	}
}

void ReadLeg(not_null<State*> state, Leg &leg) {
	const auto counter = state->fixture.counter.get();
	leg.enters = counter ? counter->enters() : 0;
	leg.drops = counter ? counter->drops() : 0;
	leg.leaves = counter ? counter->leaves() : 0;
	leg.hookChecks = state->hookChecks;
	leg.hookInserts = state->hookInserts;
	leg.hookTexts = state->hookTexts;
	leg.fieldText = state->fixture.field
		? state->fixture.field->getLastText()
		: QString();
	leg.dragTarget = DragTargetReading(counter);
	leg.targetNull = (leg.dragTarget == u"null"_q);
}

[[nodiscard]] Leg DropLeg(not_null<State*> state) {
	ResetLogs(state);
	auto result = Leg();
	const auto before = state->clipboardChanges;
	result.drop = DropText(state->fixture.field, kText);
	result.clipboardChanges = state->clipboardChanges - before;
	ReadLeg(state, result);
	return result;
}

[[nodiscard]] bool HookTextsMatch(const Leg &leg) {
	if (leg.hookTexts.isEmpty()) {
		return false;
	}
	for (const auto &text : leg.hookTexts) {
		if (text != kText) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QString ClipboardDetails(int changes) {
	return u"clipboard: platform=%1 oracle=QClipboard::changed within the "
		u"call legChanges=%2"_q
		.arg(QGuiApplication::platformName())
		.arg(changes);
}

[[nodiscard]] QString LegDetails(const QString &label, const Leg &leg) {
	return u"%1: %2 | hook: checks=%3 inserts=%4 texts=[%5] | counter: "
		u"enters=%6 drops=%7 leaves=%8 | field=\"%9\" | dragTarget=%10 "
		u"| %11"_q
		.arg(label)
		.arg(TextDropDetails(leg.drop))
		.arg(leg.hookChecks)
		.arg(leg.hookInserts)
		.arg(leg.hookTexts.join(u"; "_q))
		.arg(leg.enters)
		.arg(leg.drops)
		.arg(leg.leaves)
		.arg(leg.fieldText)
		.arg(leg.dragTarget)
		.arg(ClipboardDetails(leg.clipboardChanges));
}

void CheckQuietNeighbours(const QString &label, const Leg &leg) {
	Check(
		!leg.enters && !leg.drops && !leg.leaves && leg.targetNull,
		label + u": the drop-accepting container received no drag event "
			"and no drag target is left behind"_q,
		LegDetails(label, leg));
}

void CheckNoClipboardChange(const QString &label, const Leg &leg) {
	Check(
		!leg.clipboardChanges,
		label + u": QClipboard::changed did not fire within the call"_q,
		LegDetails(label, leg));
}

void CheckViewportRefusal(
		const QString &label,
		const Leg &leg,
		const QString &marker) {
	const auto details = LegDetails(label, leg);
	Check(
		(leg.drop.refusal == TextDropRefusal::ViewportRefusesDrops)
			&& (TextDropRefusalName(leg.drop.refusal)
				== u"viewport-refuses-drops"_q)
			&& !leg.drop.delivered
			&& !leg.drop.enterAccepted
			&& !leg.drop.dropSent
			&& leg.drop.reason.contains(marker),
		label + u": refused by name as viewport-refuses-drops, nothing "
			"sent, reason quotes "_q + marker,
		details);
	Check(
		(leg.fieldText == kSeed) && !leg.hookChecks && !leg.hookInserts,
		label + u": the field still holds the seed and its hook was never "
			"called"_q,
		details);
	CheckQuietNeighbours(label, leg);
	CheckNoClipboardChange(label, leg);
}

[[nodiscard]] bool BuildFixture(not_null<State*> state) {
	auto &fixture = state->fixture;
	fixture.counter = base::make_unique_q<DropCounter>(nullptr);
	const auto counter = fixture.counter.get();
	const auto field = Ui::CreateChild<Ui::InputField>(
		counter,
		st::defaultInputField,
		rpl::single(u"Harness"_q));
	field->resize(st::boxWidth, field->height());
	counter->resize(st::boxWidth, field->height());
	Ui::SendPendingMoveResizeEvents(counter);
	Ui::SendPendingMoveResizeEvents(field);
	fixture.field = field;
	field->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			++state->hookChecks;
		} else {
			++state->hookInserts;
		}
		state->hookTexts.push_back(data->hasText()
			? data->text()
			: QString());
		return (state->mode == HookMode::Take);
	});
	field->changes() | rpl::on_next([=] {
		++state->fixture.changes;
	}, fixture.lifetime);

	state->clipboardContext = base::make_unique_q<QObject>();
	QObject::connect(
		QGuiApplication::clipboard(),
		&QClipboard::changed,
		state->clipboardContext.get(),
		[=](QClipboard::Mode mode) {
			++state->clipboardChanges;
			const auto index = int(mode);
			if (index >= 0 && index < int(state->clipboardModes.size())) {
				++state->clipboardModes[index];
			}
		});
	return true;
}

DropCounter::DropCounter(QWidget *parent)
: RpWidget(parent) {
	setAcceptDrops(true);
}

int DropCounter::enters() const {
	return _enters;
}

int DropCounter::drops() const {
	return _drops;
}

int DropCounter::leaves() const {
	return _leaves;
}

void DropCounter::reset() {
	_enters = 0;
	_drops = 0;
	_leaves = 0;
}

void DropCounter::dragEnterEvent(QDragEnterEvent *e) {
	++_enters;
	e->acceptProposedAction();
}

void DropCounter::dragLeaveEvent(QDragLeaveEvent *e) {
	++_leaves;
}

void DropCounter::dropEvent(QDropEvent *e) {
	++_drops;
	e->acceptProposedAction();
}

ReadOnlyOnDrop::ReadOnlyOnDrop(not_null<QTextEdit*> editor)
: _editor(editor) {
}

int ReadOnlyOnDrop::fired() const {
	return _fired;
}

bool ReadOnlyOnDrop::eventFilter(QObject *watched, QEvent *event) {
	if (event->type() == QEvent::Drop) {
		++_fired;
		_editor->setReadOnly(true);
	}
	return false;
}

} // namespace

void AppendTextDropSelfTest(not_null<Runner*> runner) {
	// Leaked on purpose, as the other self-tests leak theirs: the stages
	// outlive this call, and the teardown stage releases the fixture.
	const auto state = new State();

	runner->add({
		.name = u"text drop self-test: the field's own hook receives the "
			"dropped text"_q,
		.run = [=] {
			state->built = BuildFixture(state);
			const auto field = state->fixture.field;
			const auto viewport = field
				? field->rawTextEdit()->viewport()
				: nullptr;
			state->gateViewportEnabled = viewport && viewport->isEnabled();
			state->gateAcceptDrops = viewport && viewport->acceptDrops();
			state->gateEmpty = field && field->getLastText().isEmpty();
			Check(
				state->built
					&& state->gateViewportEnabled
					&& state->gateAcceptDrops
					&& state->gateEmpty,
				u"fixture gate: a real InputField in a parentless "
				"drop-accepting container, its viewport enabled and taking "
				"drops, the field empty"_q,
				u"built=%1 viewportEnabled=%2 acceptDrops=%3 empty=%4 "
				"viewport=%5 counter=%6"_q
					.arg(state->built ? 1 : 0)
					.arg(state->gateViewportEnabled ? 1 : 0)
					.arg(state->gateAcceptDrops ? 1 : 0)
					.arg(state->gateEmpty ? 1 : 0)
					.arg(viewport
						? WidgetDescription(viewport)
						: u"null"_q)
					.arg(state->fixture.counter
						? WidgetDescription(state->fixture.counter.get())
						: u"null"_q));
			if (!state->built) {
				return;
			}
			state->mode = HookMode::Take;
			state->taken = DropLeg(state);
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto label = u"take"_q;
			const auto &leg = state->taken;
			const auto details = LegDetails(label, leg);
			Check(
				leg.drop.delivered
					&& (leg.drop.refusal == TextDropRefusal::None)
					&& leg.drop.enterAccepted
					&& leg.drop.dropSent
					&& leg.drop.dropAccepted
					&& leg.drop.shielded.isEmpty(),
				u"take: the field's own editor accepted the enter and the "
				"drop, and no other widget was offered either"_q,
				details);
			Check(
				(leg.hookInserts == 1)
					&& (leg.hookChecks >= 1)
					&& HookTextsMatch(leg),
				u"take: the mime hook received exactly the dropped text, "
				"inserted once"_q,
				details);
			Check(
				leg.fieldText.isEmpty(),
				u"take: the field stays empty because its hook took the "
				"insertion"_q,
				details);
			CheckQuietNeighbours(label, leg);
			CheckNoClipboardChange(label, leg);
		},
	});

	runner->add({
		.name = u"text drop self-test: the field inserts the text itself "
			"when its hook leaves it"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto field = state->fixture.field;
			state->mode = HookMode::Leave;
			state->fixture.changes = 0;
			state->leftEmptyBefore = field->getLastText().isEmpty();
			Check(
				state->leftEmptyBefore,
				u"leave: precondition - the field is empty before the "
				"drop"_q,
				u"field=\"%1\""_q.arg(field->getLastText()));
			state->left = DropLeg(state);
			state->leftChanges = state->fixture.changes;
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto label = u"leave"_q;
			const auto &leg = state->left;
			const auto details = LegDetails(label, leg)
				+ u" | changes=%1"_q.arg(state->leftChanges);
			Check(
				leg.drop.delivered
					&& (leg.drop.refusal == TextDropRefusal::None)
					&& leg.drop.enterAccepted
					&& leg.drop.dropSent
					&& leg.drop.dropAccepted
					&& leg.drop.shielded.isEmpty(),
				u"leave: the field's own editor accepted the enter and the "
				"drop, and no other widget was offered either"_q,
				details);
			Check(
				(leg.hookInserts == 1)
					&& (leg.hookChecks >= 1)
					&& HookTextsMatch(leg),
				u"leave: the mime hook was offered exactly the dropped "
				"text and left it"_q,
				details);
			Check(
				(leg.fieldText == kText) && (state->leftChanges > 0),
				u"leave: the field holds exactly the dropped text and "
				"reported a change"_q,
				details);
			CheckQuietNeighbours(label, leg);
			CheckNoClipboardChange(label, leg);
		},
	});

	runner->add({
		.name = u"text drop self-test: a viewport that takes no drops is "
			"refused by name"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto field = state->fixture.field;
			const auto raw = field->rawTextEdit();
			state->mode = HookMode::Leave;
			Settle([=] {
				field->setText(kSeed);
			});
			state->failuresBefore = FailureCount();
			raw->setAcceptDrops(false);
			state->noDrops = DropLeg(state);
			raw->setAcceptDrops(true);
			field->setEnabled(false);
			state->disabled = DropLeg(state);
			field->setEnabled(true);
			state->failuresAfter = FailureCount();
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			CheckViewportRefusal(
				u"acceptDrops off"_q,
				state->noDrops,
				u"acceptDrops=0"_q);
			CheckViewportRefusal(
				u"field disabled"_q,
				state->disabled,
				u"enabled=0"_q);
			Check(
				state->failuresAfter == state->failuresBefore,
				u"a refusal is a returned value, not a logged failure"_q,
				u"FailureCount %1 -> %2"_q
					.arg(state->failuresBefore)
					.arg(state->failuresAfter));
		},
	});

	runner->add({
		.name = u"text drop self-test: an enter the field ignores beneath a "
			"drop-accepting ancestor is refused by name"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto field = state->fixture.field;
			const auto raw = field->rawTextEdit();
			const auto viewport = raw->viewport();
			const auto counter = state->fixture.counter.get();
			raw->setReadOnly(true);
			ResetLogs(state);
			Settle([=] {
				auto mime = QMimeData();
				mime.setText(kText);
				const auto point = viewport->rect().center();
				auto enter = QDragEnterEvent(
					point,
					Qt::CopyAction,
					&mime,
					Qt::LeftButton,
					Qt::NoModifier);
				QApplication::sendEvent(viewport, &enter);
				state->premiseEnterAccepted = enter.isAccepted();
				state->premiseEnters = counter->enters();
				state->premiseTargetWasCounter
					= (DragTargetReading(counter) == u"counter"_q);
				auto drop = QDropEvent(
					QPointF(point),
					Qt::CopyAction,
					&mime,
					Qt::LeftButton,
					Qt::NoModifier);
				QApplication::sendEvent(viewport, &drop);
				state->premiseDrops = counter->drops();
			});
			state->premiseText = field->getLastText();
			state->premiseHookChecks = state->hookChecks;
			state->premiseHookInserts = state->hookInserts;
			state->premiseTargetNullAfter
				= (DragTargetReading(counter) == u"null"_q);
			state->ignored = DropLeg(state);
			raw->setReadOnly(false);
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto premise = u"premise (raw route, no helper): "
				"enterAccepted=%1 enters=%2 targetWasCounter=%3 drops=%4 "
				"field=\"%5\" hookChecks=%6 hookInserts=%7 "
				"targetNullAfter=%8"_q
				.arg(state->premiseEnterAccepted ? 1 : 0)
				.arg(state->premiseEnters)
				.arg(state->premiseTargetWasCounter ? 1 : 0)
				.arg(state->premiseDrops)
				.arg(state->premiseText)
				.arg(state->premiseHookChecks)
				.arg(state->premiseHookInserts)
				.arg(state->premiseTargetNullAfter ? 1 : 0);
			Check(
				state->premiseEnterAccepted
					&& (state->premiseEnters == 1)
					&& state->premiseTargetWasCounter
					&& (state->premiseDrops == 1)
					&& (state->premiseText == kSeed)
					&& !state->premiseHookChecks
					&& !state->premiseHookInserts
					&& state->premiseTargetNullAfter,
				u"premise: without the helper Qt hands the ignored enter "
				"and then the drop to the drop-accepting ancestor"_q,
				premise);
			const auto label = u"ignored enter"_q;
			const auto &leg = state->ignored;
			const auto details = LegDetails(label, leg);
			Check(
				(leg.drop.refusal == TextDropRefusal::EnterNotAccepted)
					&& (TextDropRefusalName(leg.drop.refusal)
						== u"enter-not-accepted"_q)
					&& !leg.drop.delivered
					&& !leg.drop.enterAccepted
					&& !leg.drop.dropSent,
				u"ignored enter: refused by name as enter-not-accepted, "
				"no drop sent"_q,
				details);
			const auto counterId = WidgetDescription(
				state->fixture.counter.get());
			Check(
				leg.drop.shielded.contains(counterId),
				u"ignored enter: the shielded list names the "
				"drop-accepting container Qt would have offered it to"_q,
				details + u" | counterId=%1"_q.arg(counterId));
			CheckQuietNeighbours(label, leg);
			Check(
				(leg.fieldText == kSeed) && !leg.hookInserts,
				u"ignored enter: the field still holds the seed and its "
				"hook inserted nothing"_q,
				details);
			CheckNoClipboardChange(label, leg);
		},
	});

	runner->add({
		.name = u"text drop self-test: a drop the editor does not accept is "
			"refused by name"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto raw = state->fixture.field->rawTextEdit();
			const auto viewport = raw->viewport();
			state->filter = std::make_unique<ReadOnlyOnDrop>(raw);
			viewport->installEventFilter(state->filter.get());
			state->refusedDrop = DropLeg(state);
			state->filterFired = state->filter->fired();
			viewport->removeEventFilter(state->filter.get());
			state->filter = nullptr;
			raw->setReadOnly(false);
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto label = u"refused drop"_q;
			const auto &leg = state->refusedDrop;
			const auto details = LegDetails(label, leg)
				+ u" | fired=%1"_q.arg(state->filterFired);
			Check(
				leg.drop.enterAccepted
					&& leg.drop.dropSent
					&& !leg.drop.dropAccepted
					&& !leg.drop.delivered
					&& (leg.drop.refusal == TextDropRefusal::DropNotAccepted)
					&& (TextDropRefusalName(leg.drop.refusal)
						== u"drop-not-accepted"_q)
					&& (state->filterFired == 1),
				u"refused drop: the enter was accepted, the drop was sent "
				"and refused by name as drop-not-accepted"_q,
				details);
			Check(
				(leg.fieldText == kSeed) && !leg.hookInserts,
				u"refused drop: the field still holds the seed and its hook "
				"inserted nothing"_q,
				details);
			CheckQuietNeighbours(label, leg);
			CheckNoClipboardChange(label, leg);
		},
	});

	runner->add({
		.name = u"text drop self-test: teardown"_q,
		.run = [=] {
			state->clipboardContext = nullptr;
			state->filter = nullptr;
			state->fixture.field = nullptr;
			state->fixture.lifetime.destroy();
			state->fixture.counter = nullptr;
			Note(u"text drop self-test: fixture released, alive=%1 "
				"filter=%2 clipboardContext=%3; whole-self-test "
				"QClipboard::changed total=%4 (clipboard=%5 selection=%6 "
				"findBuffer=%7) platform=%8 - an observation, never a "
				"check"_q
				.arg(state->fixture.counter ? 1 : 0)
				.arg(state->filter ? 1 : 0)
				.arg(state->clipboardContext ? 1 : 0)
				.arg(state->clipboardChanges)
				.arg(state->clipboardModes[0])
				.arg(state->clipboardModes[1])
				.arg(state->clipboardModes[2])
				.arg(QGuiApplication::platformName()));
		},
	});
}

} // namespace Test

#endif // _DEBUG
