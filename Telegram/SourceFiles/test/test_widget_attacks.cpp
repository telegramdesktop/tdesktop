/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_attacks.h"

#include "base/weak_qptr.h"
#include "test/test_capture.h"
#include "test/test_chat_show.h"
#include "test/test_launch_fuse.h"
#include "test/test_log.h"
#include "test/test_probe.h"
#include "test/test_runner.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_ops.h"
#include "test/test_widgets.h"
#include "tgw/tgw_hosted_wire.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_renderer_input.h"
#include "tgw/tgw_runtime.h"
#include "twidget/widget_host.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/elastic_scroll.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSet>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Test {
namespace {

using RuntimeState = Tgw::Runtime::State;

constexpr auto kPrefix = "widget attack audit: ";

// The audit's bounded waits. Each ends in a named reading inside its
// stage's until, and the stage timeout is the bound plus
// kDefaultStageTimeout, so a bound is never a stage timeout. The
// fixture's own (test_widget_fixture.h) bound the layout, the log and the
// barrier stages.
constexpr auto kShowTimeout = crl::time(10000);
constexpr auto kLoadBound = crl::time(25000);

// The reference --attacks loop's Options::waitMs.
constexpr auto kWatch = crl::time(1500);

// The reference click's clientX and clientY, in CSS px from the card's
// top-left corner.
constexpr auto kPressCss = 5.;

// A card taller than this share of the list's scroll is shown by a band
// at its top, holding the press point, instead of whole.
constexpr auto kTallPercent = 75;
constexpr auto kTallBand = 48;

// The runtime log is bounded by the product (kLogBytes); its row keeps the
// head, which holds the state and diagnostic kinds.
constexpr auto kMaxLogChars = 32768;

// |died| and |deathReason| are what the file's row reports, and its
// totals' dead list entry (DeadFiles()) follows them: for a file whose tap
// selected hosted works (WidgetTapHosted()), the shared reading of the
// final charges (ReadHostedDeath()), its reason measured (|deathMeasured|)
// only from the record of the work a fresh or final summary of the log was
// sampled from (|sampled|), else "charged, reason unmeasured"; a file whose
// tap selected no hosted work (refused, skipped or finished before any
// renderer of it was selected) is alive, its reason empty and unmeasured.
struct Verdict {
	QString notRun;
	// A fixture check's reason for a file that ran but was then refused.
	QString refusal;
	QString deathReason;
	QStringList incomplete;
	std::optional<WidgetHostedRun> sampled;
	bool ran = false;
	bool loaded = false;
	bool died = false;
	bool deathMeasured = false;
	bool notLoaded = false;
	bool complete = false;
	bool clean = false;
};

// One selected corpus file. Its fixture record names the message by id
// only, and its tap by the session-qualified key it was armed with, kept
// for every disarm. Its display log's "Died: " line after the first sandbox
// header gives |diedLine|, whether there is one, written in the row's `log`
// fields with |logHeader|; |diedText| is the "Died: " line the summary the
// log took starts with, verbatim, paired with |summaryWork|, the work that
// summary was sampled from, and |summaryOwn|, whether it was the fresh or
// final summary of the host the log was requested for
// (WidgetLogSummaryOwn()), the only one a hosted reading takes a cause
// from; |logDeath| is the hosted reading of that sample at the log stage,
// when the tap had selected hosted works by then.
struct AttackFile {
	QString name;
	QByteArray html;
	QString sha256;
	QString notRun;
	std::shared_ptr<WidgetFixtureMessage> message;
	std::shared_ptr<const ChatListShow> loadShow;
	std::shared_ptr<const ChatListShow> clickShow;
	WidgetOpsReading reading;
	WidgetOpsReading watched;
	WidgetCardReading loadCard;
	WidgetCardReading accepted;
	Verdict verdict;
	QString loadOutcome;
	QString stateAfterLoad;
	QString stateAfterWatch;
	QString clickOutcome;
	QString clickDetails;
	QString diedText;
	QString rowPath;
	WidgetHostedDeath logDeath;
	crl::time watchStarted = 0;
	crl::time loadWaitMs = 0;
	crl::time watchMs = 0;
	crl::time barrierMs = 0;
	qint64 startMs = 0;
	qint64 endMs = 0;
	qint64 pressMs = 0;
	uint64 playedWork = 0;
	uint64 summaryWork = 0;
	bool live = false;
	bool clickAttempted = false;
	bool logHeader = false;
	bool diedLine = false;
	bool summaryOwn = false;
	bool barrierClosed = false;
	bool itemGone = false;
	bool rowEmitted = false;
};

// A corpus entry that is not an HTML file, with its disposition.
struct NonWidget {
	QString name;
	QString disposition;
	QString inlinedIn;
	QStringList referencedBy;
	int bytes = 0;
	int lines = 0;
	bool file = false;
	bool known = false;
};

// What the audit's emitters wrote of a state that captures them
// (AuditState::capture), in order: each row's marker and fields, and each
// check they decided.
struct AuditCapture {
	std::vector<std::pair<QString, QJsonObject>> rows;
	std::vector<WidgetOpsSelfCheckRow> checks;
};

// The one State of the audit. The window, the chat, the list, the
// controls and the release are its shared fixture's
// (test_widget_fixture.h); every file's record is registered there.
struct AuditState {
	WidgetAttackAuditArgs args;
	std::shared_ptr<WidgetFixture> fixture;
	std::vector<AttackFile> files;
	std::vector<NonWidget> others;
	QStringList htmlNames;
	QStringList ignored;
	QStringList unknown;
	QStringList unreadable;
	QString corpusError;
	WidgetOpsReading control;
	bool selfChecked = false;

	QString abort;
	bool totalsEmitted = false;

	// Only the verdict self-check's controls of the emitters (Emitted())
	// set it: what they write goes there, and nothing of it to the log.
	AuditCapture *capture = nullptr;
};

[[nodiscard]] QString Prefixed(const QString &text) {
	return QString::fromLatin1(kPrefix) + text;
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString NumberText(qint64 value) {
	return QString::number(value);
}

[[nodiscard]] QString PointText(QPoint point) {
	return u"%1,%2"_q.arg(NumberText(point.x()), NumberText(point.y()));
}

[[nodiscard]] QString IdText(FullMsgId id) {
	return id
		? u"%1:%2"_q.arg(
			NumberText(qint64(id.peer.value)),
			NumberText(qint64(id.msg.bare)))
		: u"none"_q;
}

[[nodiscard]] qint64 WallNow() {
	return QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] QJsonArray Strings(const std::vector<std::string> &list) {
	auto result = QJsonArray();
	for (const auto &entry : list) {
		result.push_back(QString::fromStdString(entry));
	}
	return result;
}

[[nodiscard]] QJsonArray Strings(const QStringList &list) {
	auto result = QJsonArray();
	for (const auto &entry : list) {
		result.push_back(entry);
	}
	return result;
}

// One machine-readable row: "NOTE: <marker>: <compact JSON>", every string
// escaped by QJsonDocument.
void Row(const QString &marker, const QJsonObject &fields) {
	Note(marker
		+ u": "_q
		+ QString::fromUtf8(
			QJsonDocument(fields).toJson(QJsonDocument::Compact)));
}

// An emitter's Row(), into |state|'s capture when it has one.
void WriteRow(
		const AuditState &state,
		const QString &marker,
		const QJsonObject &fields) {
	if (state.capture) {
		state.capture->rows.emplace_back(marker, fields);
	} else {
		Row(marker, fields);
	}
}

// An emitter's Check(), into |state|'s capture when it has one.
void Decide(
		const AuditState &state,
		bool passed,
		const QString &name,
		const QString &details) {
	if (state.capture) {
		state.capture->checks.push_back({
			.name = name,
			.details = details,
			.passed = passed,
		});
	} else {
		Check(passed, name, details);
	}
}

// ---------------------------------------------------------------------
// The corpus, read once while the stages are registered.

[[nodiscard]] QByteArray ReadBytes(const QString &path, QString *error) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		*error = file.errorString();
		return QByteArray();
	}
	return file.readAll();
}

[[nodiscard]] QString Sha256(const QByteArray &bytes) {
	return QString::fromLatin1(QCryptographicHash::hash(
		bytes,
		QCryptographicHash::Sha256).toHex());
}

[[nodiscard]] QSet<QByteArray> TrimmedLines(const QByteArray &text) {
	auto result = QSet<QByteArray>();
	for (const auto &line : text.split('\n')) {
		const auto trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			result.insert(trimmed);
		}
	}
	return result;
}

// The reference htmlFiles(): every entry whose extension is ".html", in
// std::sort order of the paths, which is the byte order of their UTF-8
// names in one directory.
[[nodiscard]] bool HtmlName(const QString &name) {
	return name.endsWith(u".html"_q) && (name.size() > 5);
}

// A non-HTML entry is known when no HTML file names it and every non-empty
// line of it is a line of the HTML file of its stem: it is not a widget,
// and its code runs as that file.
[[nodiscard]] NonWidget ClassifyOther(
		const QString &dir,
		const QString &name,
		const std::vector<std::pair<QString, QByteArray>> &html) {
	auto result = NonWidget{ .name = name };
	const auto path = QDir(dir).filePath(name);
	result.file = QFileInfo(path).isFile();
	auto error = QString();
	const auto bytes = result.file ? ReadBytes(path, &error) : QByteArray();
	result.bytes = int(bytes.size());
	const auto needle = name.toUtf8();
	for (const auto &[htmlName, htmlBytes] : html) {
		if (htmlBytes.contains(needle)) {
			result.referencedBy.push_back(htmlName);
		}
	}
	const auto lines = TrimmedLines(bytes);
	result.lines = int(lines.size());
	const auto dot = name.lastIndexOf(QChar('.'));
	const auto stem = (dot > 0) ? name.left(dot) : name;
	const auto host = stem + u".html"_q;
	for (const auto &[htmlName, htmlBytes] : html) {
		if (htmlName != host || lines.isEmpty()) {
			continue;
		}
		const auto hostLines = TrimmedLines(htmlBytes);
		auto inlined = true;
		for (const auto &line : lines) {
			if (!hostLines.contains(line)) {
				inlined = false;
				break;
			}
		}
		if (inlined) {
			result.inlinedIn = htmlName;
		}
	}
	result.known = result.file
		&& error.isEmpty()
		&& result.referencedBy.isEmpty()
		&& !result.inlinedIn.isEmpty();
	result.disposition = result.known
		? (u"not a widget and not run on its own: no .html file names it, "
			u"and its code runs inlined in "_q + result.inlinedIn)
		: !error.isEmpty()
		? (u"unclassified: it could not be read: "_q + error)
		: u"unclassified: not an HTML file, not run, and not shown to be "
			u"inlined in the HTML file of its stem"_q;
	return result;
}

void ListCorpus(AuditState &state) {
	const auto &dir = state.args.corpus;
	const auto info = QFileInfo(dir);
	if (dir.isEmpty() || !info.isAbsolute() || !info.isDir()) {
		state.corpusError = u"the corpus directory [%1] is not an absolute "
			u"directory"_q.arg(dir);
		return;
	}
	const auto entries = QDir(dir).entryList(
		QDir::AllEntries
			| QDir::Hidden
			| QDir::System
			| QDir::NoDotAndDotDot,
		QDir::NoSort);
	auto names = std::vector<std::string>();
	auto others = QStringList();
	for (const auto &entry : entries) {
		if (HtmlName(entry)) {
			names.push_back(entry.toStdString());
		} else if (entry.startsWith(QChar('.'))) {
			state.ignored.push_back(entry);
		} else {
			others.push_back(entry);
		}
	}
	std::sort(begin(names), end(names));
	others.sort();
	auto html = std::vector<std::pair<QString, QByteArray>>();
	auto errors = std::vector<QString>();
	for (const auto &name : names) {
		const auto qname = QString::fromStdString(name);
		auto error = QString();
		html.emplace_back(
			qname,
			ReadBytes(QDir(dir).filePath(qname), &error));
		errors.push_back(error);
		state.htmlNames.push_back(qname);
	}
	auto requested = QStringList();
	for (const auto &name : state.args.only) {
		if (!requested.contains(name)) {
			requested.push_back(name);
		}
		if (!state.htmlNames.contains(name) && !state.unknown.contains(name)) {
			state.unknown.push_back(name);
		}
	}
	for (auto i = 0; i != int(html.size()); ++i) {
		const auto &[name, bytes] = html[i];
		if (!requested.isEmpty() && !requested.contains(name)) {
			continue;
		}
		auto file = AttackFile{
			.name = name,
			.html = bytes,
			.sha256 = Sha256(bytes),
			.message = AddWidgetFixtureMessage(*state.fixture, name),
		};
		if (!errors[i].isEmpty()) {
			file.notRun = u"unreadable: "_q + errors[i];
			state.unreadable.push_back(name + u": "_q + errors[i]);
		} else if (bytes.isEmpty()) {
			file.notRun = u"empty: a widget message needs HTML"_q;
			state.unreadable.push_back(name + u": empty"_q);
		}
		state.files.push_back(std::move(file));
	}
	for (const auto &name : others) {
		state.others.push_back(ClassifyOther(dir, name, html));
	}
}

// ---------------------------------------------------------------------
// The card.

[[nodiscard]] QPoint PressLocal(QRect card) {
	const auto offset = Tgw::LogicalWidth(kPressCss);
	return card.topLeft() + QPoint(offset, offset);
}

// The element-local rectangle both shows ask for: the whole card, or a
// band at its top that holds the press point when the card is taller than
// kTallPercent of the list's scroll.
[[nodiscard]] QRect ShowLocal(QRect card, QWidget *list) {
	const auto scroll = FixtureListScroll(list);
	const auto limit = scroll ? scroll->height() : 0;
	if (card.isEmpty() || (card.height() * 100 <= limit * kTallPercent)) {
		return card;
	}
	const auto band = std::max(
		style::ConvertScale(kTallBand),
		2 * Tgw::LogicalWidth(kPressCss) + 1);
	return QRect(
		card.x(),
		card.y(),
		card.width(),
		std::min(card.height(), band));
}

// ---------------------------------------------------------------------
// One file.

// The current runtime of the list's own view of the file's message.
[[nodiscard]] Tgw::Runtime *OwnRuntime(
		const AuditState &state,
		const AttackFile &file) {
	return FixtureOwnRuntime(*state.fixture, file.message->id);
}

[[nodiscard]] QString RuntimeStateText(
		const AuditState &state,
		const AttackFile &file) {
	const auto runtime = OwnRuntime(state, file);
	return runtime ? RuntimeStateName(runtime->state()) : u"no-runtime"_q;
}

[[nodiscard]] QString GlobalSkip(const AuditState &state) {
	const auto gate = WidgetFixtureGate(*state.fixture);
	return !gate.isEmpty() ? gate : state.abort;
}

[[nodiscard]] QString FileSkip(const AuditState &state, int index) {
	const auto global = GlobalSkip(state);
	return global.isEmpty() ? state.files[index].notRun : global;
}

// Arms the tap for the message id the injection will use, then adds the
// local outgoing widget message: its host can only be built after this.
void Inject(AuditState &state, AttackFile &file) {
	file.startMs = WallNow();
	const auto refusal = InjectWidgetMessage(
		*state.fixture,
		*file.message,
		file.html,
		0, // width
		0); // height
	if (!refusal.isEmpty()) {
		file.notRun = refusal;
	}
}

[[nodiscard]] bool InjectLaidOut(AuditState &state, AttackFile &file) {
	if (!file.notRun.isEmpty()) {
		return true;
	}
	return WidgetMessageLaidOut(*state.fixture, *file.message);
}

void CheckInjected(AuditState &state, AttackFile &file) {
	if (!file.notRun.isEmpty()) {
		return;
	}
	const auto refusal = CheckWidgetMessage(*state.fixture, *file.message);
	if (!refusal.isEmpty()) {
		file.notRun = refusal;
	}
}

// The host's post-load query and the renderer's answer to it, or for a
// hosted host its own rendered acknowledgment, the one a Release harness
// sees without the Debug helper's mirror.
[[nodiscard]] bool Loaded(const WidgetOpsReading &reading) {
	return ((reading.ackQueries > 0) && (reading.ackResults > 0))
		|| (reading.renderedHosts > 0);
}

// Load, death or failure, or the bound: the tap's ack, rendered and frozen
// counts and the current runtime's state, read only.
[[nodiscard]] bool LoadDecided(AuditState &state, AttackFile &file) {
	auto &message = *file.message;
	const auto reading = ReadWidgetOps(*message.tap);
	const auto runtime = OwnRuntime(state, file);
	const auto current = runtime ? runtime->state() : RuntimeState::Idle;
	message.wait.details = u"state=%1 ackQueries=%2 ackResults=%3 "
		u"rendered=%4 frozen=%5 desktopCharges=%6 built=%7"_q.arg(
			runtime ? RuntimeStateName(current) : u"no-runtime"_q,
			NumberText(reading.ackQueries),
			NumberText(reading.ackResults),
			NumberText(reading.renderedHosts),
			NumberText(reading.fatalFrozen),
			NumberText(reading.desktopCharges),
			NumberText(reading.wrappersBuilt));
	return Loaded(reading)
		|| WidgetTapCharged(reading)
		|| !WidgetFixtureItem(*state.fixture, message.id)
		|| (current == RuntimeState::Stopped)
		|| (current == RuntimeState::Failed)
		|| FixtureBoundReached(message.wait, kLoadBound);
}

void RecordLoad(AuditState &state, AttackFile &file) {
	const auto reading = ReadWidgetOps(*file.message->tap);
	const auto runtime = OwnRuntime(state, file);
	const auto current = runtime ? runtime->state() : RuntimeState::Idle;
	file.loadWaitMs = crl::now() - file.message->wait.started;
	file.stateAfterLoad = runtime
		? RuntimeStateName(current)
		: u"no-runtime"_q;
	file.loadOutcome = Loaded(reading)
		? u"loaded"_q
		: WidgetTapCharged(reading)
		? u"died"_q
		: !runtime
		? u"no-runtime"_q
		: (current == RuntimeState::Stopped)
		? u"stopped"_q
		: (current == RuntimeState::Failed)
		? u"failed"_q
		: u"bound"_q;
	file.live = runtime && (current == RuntimeState::Live);
	if (!file.live) {
		file.clickOutcome = u"not-live"_q;
		file.clickDetails = u"state=%1 after the load wait (%2)"_q.arg(
			file.stateAfterLoad,
			file.loadOutcome);
	}
}

[[nodiscard]] bool DiedNow(const AuditState &state, const AttackFile &file) {
	const auto runtime = OwnRuntime(state, file);
	const auto current = runtime ? runtime->state() : RuntimeState::Idle;
	if (runtime && (current == RuntimeState::Live)) {
		return false;
	}
	return WidgetTapCharged(ReadWidgetOps(*file.message->tap))
		|| (current == RuntimeState::Stopped)
		|| (current == RuntimeState::Failed);
}

// The accepted show, view, core and geometry, read again right before the
// press; an empty answer means they are all still the ones accepted.
[[nodiscard]] QString Revalidate(
		const AuditState &state,
		const AttackFile &file) {
	return RevalidateWidgetCard(
		*state.fixture,
		file.message->id,
		u"click"_q,
		*file.clickShow,
		file.accepted);
}

// Click()'s press, release and leave (test_widgets.h), each delivered and
// settled as there, the press's own synchronous delivery inside
// DeliverMarkedPress(): only a press the chat dispatches while this
// mouse-down is being delivered can bind the mark, which is sealed before
// the settle drains the postponed calls and before the watch.
void PlayMarkedClick(QWidget *list, QPoint point, WidgetOpsTap &tap) {
	if (!list) {
		return;
	}
	const auto alive = base::make_weak(list);
	const auto local = QPointF(point);
	const auto global = QPointF(list->mapToGlobal(point));
	const auto deliver = [&](QEvent &event, bool press) {
		const auto strong = alive.get();
		if (!strong) {
			return false;
		}
		Settle([&] {
			if (!press) {
				QApplication::sendEvent(strong, &event);
				return;
			}
			DeliverMarkedPress(tap, [&] {
				QApplication::sendEvent(strong, &event);
			});
		});
		return (alive.get() != nullptr);
	};
	auto press = QMouseEvent(
		QEvent::MouseButtonPress,
		local,
		global,
		Qt::LeftButton,
		Qt::LeftButton,
		Qt::NoModifier);
	if (!deliver(press, true)) {
		return;
	}
	auto release = QMouseEvent(
		QEvent::MouseButtonRelease,
		local,
		global,
		Qt::LeftButton,
		Qt::NoButton,
		Qt::NoModifier);
	deliver(release, false);
	auto leave = QEvent(QEvent::Leave);
	deliver(leave, false);
}

// The click in the stage right after the click show, then the watch. A
// card that is not live gets no press and is watched from now.
void ClickAndWatch(AuditState &state, AttackFile &file) {
	file.watchStarted = crl::now();
	if (!file.live) {
		return;
	}
	const auto list = WidgetFixtureList(*state.fixture);
	const auto problem = Revalidate(state, file);
	if (!problem.isEmpty()) {
		file.clickOutcome = DiedNow(state, file)
			? u"died-before-press"_q
			: file.clickShow->shown()
			? u"revalidation-failed"_q
			: u"show-refused"_q;
		file.clickDetails = problem;
		if (file.clickShow->hovered) {
			DeliverListLeave(list);
		}
		return;
	}
	const auto runtime = OwnRuntime(state, file);
	const auto core = runtime ? runtime->interactive() : nullptr;
	file.playedWork = core ? core->workId() : 0;
	MarkWidgetOpsEvents(*file.message->tap);
	file.pressMs = WallNow();
	file.clickAttempted = true;
	PlayMarkedClick(list, file.clickShow->hover, *file.message->tap);
	file.watchStarted = crl::now();
}

// Where a click is read, each for the generation the mark found newest
// alone, while it stays the newest, and each for the press the click
// played: the primary press the chat dispatched to the core it clicked
// while the click's own mouse-down was being delivered
// (PlayMarkedClick()). With the Debug helper's tap the hosted host's
// mirrored trusted click and the click decision the helper's renderer
// reported for the up of that press, sent with the pointer serial of that
// dispatch; without it (a Release harness) that decision alone. An
// answered down or up is delivery, never a click, and a trusted click the
// played press did not make never completes it.
enum class ClickReading {
	HelperMirror,
	Decision,
};

[[nodiscard]] ClickReading ClickReadingHere() {
	return WidgetHelperMirrors()
		? ClickReading::HelperMirror
		: ClickReading::Decision;
}

[[nodiscard]] QString ClickSource(ClickReading reading) {
	switch (reading) {
	case ClickReading::HelperMirror: return u"helper-mirror"_q;
	case ClickReading::Decision: return u"click-decision"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] QString ClickSource() {
	return ClickSource(ClickReadingHere());
}

// The click's outcome from the watch's reading as |reading| reads it:
// "observed" from a trusted click of the marked generation together with
// the played press's own click (with the Debug tap its decision),
// "decided" from that decision alone; the press's own click counts only
// for the press the click played to |played|, the work of the core it
// clicked, never for 0. Anything else, an answered down or up, a late step
// of an earlier press or another generation, a press dispatched before the
// mark, after a mouse-down no core took, or to another core, a trusted
// click another press made and a click after a newer generation was
// selected included, is "not-observed".
[[nodiscard]] QString ClickOutcome(
		const WidgetOpsReading &watched,
		ClickReading reading,
		uint64 played) {
	const auto trusted = (watched.trustedClicks > 0);
	const auto identified = played
		&& (watched.markedWork == played)
		&& watched.markedSerial
		&& watched.markedPress;
	const auto own = identified && (watched.clicksCompleted > 0);
	switch (reading) {
	case ClickReading::HelperMirror:
		return (trusted && own) ? u"observed"_q : u"not-observed"_q;
	case ClickReading::Decision:
		return own ? u"decided"_q : u"not-observed"_q;
	}
	return u"not-observed"_q;
}

void RecordWatch(AuditState &state, AttackFile &file) {
	file.watchMs = crl::now() - file.watchStarted;
	file.watched = ReadWidgetOps(*file.message->tap);
	file.stateAfterWatch = RuntimeStateText(state, file);
	if (file.clickAttempted) {
		file.clickOutcome = ClickOutcome(
			file.watched,
			ClickReadingHere(),
			file.playedWork);
	}
	HideFixtureLayers(*state.fixture, *file.message, u"after the watch"_q);
}

// The one field of the runtime log the row needs, the "Died: " line, read
// twice. The display log's own line (Runtime::logText()), the one
// Summarize() puts right after the first sandbox header, before any text a
// widget controls (its errors and console may hold line breaks), gives the
// row's `log.sandboxHeader` and `log.diedLine` alone.
// For a hosted reading, from the summary the log took itself
// (WidgetRuntimeLog::summary), whose first line Summarize() writes,
// verbatim and paired with the work that summary was sampled from
// (summaryWork, read with the text), the only work it can describe, and
// with whether that summary was the requested host's own: no text the
// display log puts before its sandbox header, a renderer's multiline stop
// reason among them, can stand in for that line or hide it.
void ParseLog(AttackFile &file) {
	const auto &log = file.message->log;
	const auto first = log.summary.section(QChar('\n'), 0, 0);
	file.diedText = first.startsWith(u"Died: "_q) ? first : QString();
	file.summaryWork = log.summaryWork;
	file.summaryOwn = WidgetLogSummaryOwn(log);
	const auto lines = log.text.split(QChar('\n'));
	for (auto i = 0; i + 1 < int(lines.size()); ++i) {
		const auto &line = lines[i];
		const auto header = line.endsWith(QChar(':'))
			&& (line.startsWith(u"Current sandbox"_q)
				|| line.startsWith(u"Last sandbox"_q));
		if (!header) {
			continue;
		}
		const auto &next = lines[i + 1];
		file.diedLine = next.startsWith(u"Died: "_q);
		file.logHeader = true;
		return;
	}
}

// ReadHostedDeath() of |reading| for the sample the log paired
// (ParseLog()), only while that sample was the summary of the host the log
// was requested for (`summaryOwn`), as the conformance runner reads its
// own: a stale or absent one, possibly an older host's line and work left
// in the runtime when the request timed out, keeps them raw in the file but
// names no work whose record may give a cause, neither now nor through
// |earlier|, a reading kept for a record evicted since.
[[nodiscard]] WidgetHostedDeath ReadSampledDeath(
		const AttackFile &file,
		const WidgetOpsReading &reading,
		const WidgetHostedDeath *earlier) {
	return file.summaryOwn
		? ReadHostedDeath(reading, file.summaryWork, file.diedText, earlier)
		: ReadHostedDeath(reading, 0, QString(), earlier);
}

// The hosted reading of the log's sample with the tap's |reading| then,
// once that tap selected hosted works, whose measured reason, of an own
// sample alone, stays with the sampled work once its record is evicted.
void ReadLogDeath(AttackFile &file, const WidgetOpsReading &reading) {
	if (WidgetTapHosted(reading)) {
		file.logDeath = ReadSampledDeath(file, reading, nullptr);
	}
}

void RecordLog(AuditState &state, AttackFile &file) {
	auto &message = *file.message;
	FinishRuntimeLog(*state.fixture, message);
	ParseLog(file);
	ReadLogDeath(file, ReadWidgetOps(*message.tap));
	const auto &log = message.log;
	Row(u"ATTACK_LOG"_q, {
		{ u"file"_q, file.name },
		{ u"source"_q, log.source },
		{ u"summaryWork"_q, QString::number(file.summaryWork) },
		{ u"summaryOwn"_q, file.summaryOwn },
		{ u"diedText"_q, file.diedText },
		{ u"pending"_q, log.pending },
		{ u"waitedMs"_q, qint64(crl::now() - message.wait.started) },
		{ u"chars"_q, log.chars },
		{ u"truncated"_q, (log.chars > kMaxLogChars) },
		{ u"text"_q, log.text.left(kMaxLogChars) },
	});
}

// Only the remove stage writes a "final" row; a row written by the totals
// stage or Runner::onFinish is for a file that never got that far.
[[nodiscard]] bool FinalRow(const AttackFile &file) {
	return (file.rowPath == u"final"_q);
}

[[nodiscard]] QString NotRunReason(
		const AuditState &state,
		const AttackFile &file) {
	const auto progress = u"load=%1 state=%2"_q.arg(
		file.loadOutcome.isEmpty() ? u"none"_q : file.loadOutcome,
		file.stateAfterLoad.isEmpty() ? u"none"_q : file.stateAfterLoad);
	if (!file.notRun.isEmpty()) {
		return file.notRun;
	} else if (!file.message->tap) {
		const auto global = GlobalSkip(state);
		return global.isEmpty()
			? u"the scenario finished before this file's stages ran"_q
			: global;
	} else if (!FinalRow(file)) {
		return u"aborted before its final reading: "_q + progress;
	} else if (!file.barrierClosed) {
		return u"no renderer was built for it, and a factory selected for it "
			u"was still pending at the bound: "_q + progress;
	}
	return u"no renderer was built for it: "_q + progress;
}

// The death of |file| from its reading: once its tap selected hosted
// works, ReadSampledDeath() for the sample its log paired (ParseLog()),
// none for a skipped log stage, the log stage's own reading standing for
// a record evicted since, never a raw line by itself; a file whose tap
// selected no hosted work keeps the alive, unmeasured, reasonless verdict
// Judge() starts from.
void JudgeDeath(Verdict &verdict, const AttackFile &file) {
	const auto &reading = file.reading;
	if (!WidgetTapHosted(reading)) {
		return;
	}
	const auto death = ReadSampledDeath(file, reading, &file.logDeath);
	verdict.died = death.died;
	verdict.deathMeasured = death.measured;
	verdict.deathReason = WidgetHostedDeathText(death);
	verdict.sampled = death.sampled;
}

// Independent statuses from the final reading: a death with a leak is in
// both lists, and only the copied scanner decides the leak count. A
// renderer violation is no leak, but a file with one is never clean.
[[nodiscard]] Verdict Judge(const AuditState &state, const AttackFile &file) {
	const auto &reading = file.reading;
	auto result = Verdict();
	result.ran = (reading.wrappersBuilt > 0);
	result.loaded = Loaded(reading);
	JudgeDeath(result, file);
	result.notLoaded = result.ran && !result.loaded;
	if (!result.ran) {
		result.notRun = NotRunReason(state, file);
	} else {
		// A fixture check refused the file after a renderer was built: its
		// later stages were skipped, so it ran but was not observed.
		if (!file.notRun.isEmpty()) {
			result.refusal = file.notRun;
			result.incomplete.push_back(u"fixture-refused"_q);
		}
		if (!FinalRow(file)) {
			result.incomplete.push_back(u"aborted"_q);
		} else if (!file.barrierClosed) {
			result.incomplete.push_back(u"hosts-pending"_q);
		}
		if (file.watchMs < kWatch) {
			result.incomplete.push_back(u"watch-missing"_q);
		}
		if (reading.scanGaps > 0) {
			result.incomplete.push_back(u"scan-gaps"_q);
		}
		if (file.live
			&& (file.clickOutcome != u"observed"_q)
			&& (file.clickOutcome != u"decided"_q)
			&& (file.clickOutcome != u"died-before-press"_q)) {
			result.incomplete.push_back(u"click-"_q
				+ (file.clickOutcome.isEmpty()
					? u"pending"_q
					: file.clickOutcome));
		}
	}
	result.complete = result.ran && result.incomplete.isEmpty();
	result.clean = result.complete
		&& result.loaded
		&& !result.died
		&& !reading.leaks
		&& !reading.violations;
	return result;
}

[[nodiscard]] QString VerdictText(const Verdict &verdict) {
	const auto fields = QStringList{
		u"ran=%1"_q.arg(FlagText(verdict.ran)),
		u"loaded=%1"_q.arg(FlagText(verdict.loaded)),
		u"died=%1"_q.arg(FlagText(verdict.died)),
		u"deathReason=[%1]"_q.arg(verdict.deathReason),
		u"deathMeasured=%1"_q.arg(FlagText(verdict.deathMeasured)),
		u"complete=%1"_q.arg(FlagText(verdict.complete)),
		u"clean=%1"_q.arg(FlagText(verdict.clean)),
		u"notRun=[%1]"_q.arg(verdict.notRun),
		u"refusal=[%1]"_q.arg(verdict.refusal),
		u"incomplete=[%1]"_q.arg(verdict.incomplete.join(u","_q)),
	};
	return fields.join(QChar(' '));
}

// The totals' dead list: each file whose row's verdict died, with that
// verdict's reason, so the list and its row never disagree.
[[nodiscard]] QStringList DeadFiles(const AuditState &state) {
	auto result = QStringList();
	for (const auto &file : state.files) {
		if (!file.verdict.died) {
			continue;
		}
		result.push_back(file.name + u": "_q + file.verdict.deathReason);
	}
	return result;
}

// A final row of a file whose one renderer loaded, sent one clean batch and
// was destroyed, whose live card took its observed click and whose watch
// ran to the end: complete and clean.
[[nodiscard]] AttackFile VerdictControlFile() {
	auto result = AttackFile();
	result.name = u"verdict control"_q;
	result.message = std::make_shared<WidgetFixtureMessage>();
	auto &reading = result.reading;
	reading.factoriesSelected = 1;
	reading.factoriesCompleted = 1;
	reading.factoriesReleased = 1;
	reading.wrappersBuilt = 1;
	reading.wrappersDestroyed = 1;
	reading.hostMessages = 2;
	reading.batches = 1;
	reading.ops = 1;
	reading.ackQueries = 1;
	reading.ackResults = 1;
	result.rowPath = u"final"_q;
	result.barrierClosed = true;
	result.live = true;
	result.clickAttempted = true;
	result.clickOutcome = u"observed"_q;
	result.watchMs = kWatch;
	return result;
}

// Pure ClickOutcome() controls on readings of the marked interaction: an
// answered down, and a down and up with no decision (a target replaced
// before the up), are never a click; a decision completes the click
// without the Debug helper's tap, only for the press played to the core
// the click hit (another core's work, or no bound press, is none); with
// the tap a decision needs the mirror's trusted click and that click the
// decision (an earlier press's mirrored click is no decision of the marked
// one).
void CheckClickOutcomes(const QString &prefix) {
	constexpr auto kPlayed = uint64(7);
	auto down = WidgetOpsReading();
	down.markedGeneration = 1;
	down.markedWork = kPlayed;
	down.markedSerial = 3;
	down.markedPress = 1;
	down.downsConfirmed = 1;
	auto replaced = down;
	replaced.upsConfirmed = 1;
	auto decided = replaced;
	decided.clicksCompleted = 1;
	auto unbound = decided;
	unbound.markedSerial = 0;
	unbound.markedPress = 0;
	auto mirrored = replaced;
	mirrored.trustedClicks = 1;
	auto both = decided;
	both.trustedClicks = 1;
	using Reading = ClickReading;
	const auto release = [&](const WidgetOpsReading &reading) {
		return ClickOutcome(reading, Reading::Decision, kPlayed);
	};
	const auto releaseDown = release(down);
	const auto releaseReplaced = release(replaced);
	const auto releaseNone = release(WidgetOpsReading());
	const auto releaseClick = release(decided);
	const auto releaseUnbound = release(unbound);
	const auto releaseOther = ClickOutcome(
		decided,
		Reading::Decision,
		kPlayed + 1);
	const auto debugDecided = ClickOutcome(
		decided,
		Reading::HelperMirror,
		kPlayed);
	const auto debugMirrored = ClickOutcome(
		mirrored,
		Reading::HelperMirror,
		kPlayed);
	const auto debugBoth = ClickOutcome(both, Reading::HelperMirror, kPlayed);
	Check(
		(releaseDown == u"not-observed"_q)
			&& (releaseReplaced == u"not-observed"_q)
			&& (releaseNone == u"not-observed"_q)
			&& (releaseClick == u"decided"_q)
			&& (releaseUnbound == u"not-observed"_q)
			&& (releaseOther == u"not-observed"_q)
			&& (debugDecided == u"not-observed"_q)
			&& (debugMirrored == u"not-observed"_q)
			&& (debugBoth == u"observed"_q),
		prefix + u"a hosted click is the click decision of the press played "
			u"to the clicked core (with the Debug helper's tap and the "
			u"mirror's trusted click), never an answered down or up or an "
			u"unbound or other core's press"_q,
		u"releaseDown=%1 releaseReplaced=%2 releaseNone=%3 releaseClick=%4 "
			u"releaseUnbound=%5 releaseOther=%6 debugDecided=%7 "
			u"debugMirrored=%8 debugBoth=%9"_q.arg(
				releaseDown,
				releaseReplaced,
				releaseNone,
				releaseClick,
				releaseUnbound,
				releaseOther,
				debugDecided,
				debugMirrored,
				debugBoth));
}

// Work ids no RendererCore is given and the ops self-check does not use,
// the older and newer generations' of the hosted tap, then its three later
// generations' (a click completed before its core retired, a press whose
// up was queued across that retirement, a press whose up came between its
// core's detach and its host's retirement), and the session of the
// message the verdict check drives.
constexpr auto kVerdictOlderWork = std::numeric_limits<uint64>::max() - 5;
constexpr auto kVerdictNewerWork = kVerdictOlderWork + 1;
constexpr auto kVerdictKeptWork = kVerdictOlderWork - 12;
constexpr auto kVerdictRetiredWork = kVerdictOlderWork - 13;
constexpr auto kVerdictGapWork = kVerdictOlderWork - 16;
constexpr auto kVerdictTapSession = uint64(3);

// The trusted click a renderer sends its host.
constexpr auto kVerdictClick = R"json({"type":"event",)json"
	R"json("ev":{"type":"click","targetId":2,"detail":1},)json"
	R"json("trusted":true})json";

// How the verdict check plays a press of a work through the product's
// seams: its down taken (its Sent and Down), then its up, with or without
// the click its renderer decided (its Up and Click, then its host's
// mirrored trusted click, which only a Debug tap reads), a third
// generation selected for the message, a later generation selected and
// built for a work, the detach of a work's core (RendererCore::detach():
// WidgetCoreDetached(), then its link closed), after which a reply its
// route had already taken still reports the up and the click its renderer
// decided, its host, still live, mirroring that click, and the retirement
// of that work's host (WidgetHostRetired()), after which such a reply
// reports with no mirrored trusted click, while the host keeps the
// wrapper.
struct VerdictPlayer {
	Fn<void(uint64 work, uint64 press, uint64 serial)> down;
	Fn<void(uint64 work, uint64 press, bool clicked)> up;
	Fn<void()> supersede;
	Fn<bool(uint64 work)> next;
	Fn<void(uint64 work)> detach;
	Fn<void(uint64 work)> retire;
	uint64 older = 0;
	uint64 newer = 0;
	uint64 kept = 0;
	uint64 retired = 0;
	uint64 gap = 0;
};

// The readings the verdict check's interactions leave on its tap, and
// whether its hosts were built.
struct VerdictRun {
	WidgetOpsReading late;
	WidgetOpsReading replaced;
	WidgetOpsReading queued;
	WidgetOpsReading stable;
	WidgetOpsReading missed;
	WidgetOpsReading withheld;
	WidgetOpsReading superseded;
	WidgetOpsReading kept;
	WidgetOpsReading retired;
	WidgetOpsReading detached;
	WidgetOpsReading gap;
	uint64 keptWork = 0;
	uint64 retiredWork = 0;
	uint64 gapWork = 0;
	bool built = false;
};

// A hosted host with no channel, never started: the tap's observers are
// installed on it and nothing else calls them.
[[nodiscard]] std::shared_ptr<twidget::WidgetHost> VerdictHost() {
	return std::make_shared<twidget::WidgetHost>(
		std::string("<p>attack verdict check</p>"),
		twidget::WidgetTheme(),
		320.,
		twidget::WidgetHostOptions(),
		std::shared_ptr<twidget::SandboxHostedChannel>());
}

// |work|'s primary press |press| through the product's press seam, |steps|
// in order, its Sent with the pointer |serial| the chat dispatched it with.
void ObservePress(
		uint64 work,
		uint64 press,
		std::initializer_list<Tgw::PressStep::Kind> steps,
		uint64 serial = 0) {
	using Kind = Tgw::PressStep::Kind;
	for (const auto kind : steps) {
		WidgetPressObserved(work, press, {
			.kind = kind,
			.serial = (kind == Kind::Sent) ? serial : uint64(),
		});
	}
}

// One message's tap with two generations, marked on the newer, each played
// press dispatched inside DeliverMarkedPress() as PlayMarkedClick()
// delivers it: the older generation's press, down before the mark, clicked
// after it (late); the newer's played press down and up with no click, its
// target replaced by its down handler (replaced); after another mark a
// press the chat dispatched before it that clicks after it, beside a played
// press that clicks none (queued); after another the played stable click
// (stable); after another a delivery that dispatched nothing, then a
// genuine click of the same work (missed); after another a played down
// whose up never comes (withheld); after another a played press whose
// generation a third selection supersedes between its down and its click
// (superseded); then on a later generation, after another mark, the
// played click completed before its core retired (kept); on a later
// one still a press taken before the mark that clicks after it, then a
// played down whose core retires before its up, which stays queued and
// comes after the retirement while the host keeps the wrapper (retired);
// and on a later one still a played down whose core is detached before
// its up, which comes, clicked, before the host's retirement (detached,
// read before that retirement, and gap, read after it).
void PlayVerdicts(
		VerdictRun &run,
		WidgetOpsTap &tap,
		const VerdictPlayer &player) {
	const auto older = player.older;
	const auto newer = player.newer;
	const auto kept = player.kept;
	const auto retired = player.retired;
	const auto play = [&](uint64 work, uint64 serial) {
		DeliverMarkedPress(tap, [&] {
			WidgetPressDispatched(work, serial);
		});
	};
	WidgetPressDispatched(older, 1);
	player.down(older, 1, 1);
	MarkWidgetOpsEvents(tap);
	player.up(older, 1, true);
	run.late = ReadWidgetOps(tap);
	play(newer, 1);
	player.down(newer, 1, 1);
	player.up(newer, 1, false);
	run.replaced = ReadWidgetOps(tap);
	WidgetPressDispatched(newer, 2);
	MarkWidgetOpsEvents(tap);
	play(newer, 3);
	player.down(newer, 2, 2);
	player.up(newer, 2, true);
	player.down(newer, 3, 3);
	player.up(newer, 3, false);
	run.queued = ReadWidgetOps(tap);
	MarkWidgetOpsEvents(tap);
	play(newer, 4);
	player.down(newer, 4, 4);
	player.up(newer, 4, true);
	run.stable = ReadWidgetOps(tap);
	MarkWidgetOpsEvents(tap);
	DeliverMarkedPress(tap, [] {});
	WidgetPressDispatched(newer, 5);
	player.down(newer, 5, 5);
	player.up(newer, 5, true);
	run.missed = ReadWidgetOps(tap);
	MarkWidgetOpsEvents(tap);
	play(newer, 6);
	player.down(newer, 6, 6);
	run.withheld = ReadWidgetOps(tap);
	MarkWidgetOpsEvents(tap);
	play(newer, 7);
	player.down(newer, 7, 7);
	player.supersede();
	player.up(newer, 7, true);
	run.superseded = ReadWidgetOps(tap);
	run.built = player.next(kept) && run.built;
	MarkWidgetOpsEvents(tap);
	play(kept, 8);
	player.down(kept, 8, 8);
	player.up(kept, 8, true);
	player.detach(kept);
	player.retire(kept);
	run.kept = ReadWidgetOps(tap);
	run.keptWork = kept;
	run.built = player.next(retired) && run.built;
	WidgetPressDispatched(retired, 9);
	player.down(retired, 9, 9);
	MarkWidgetOpsEvents(tap);
	player.up(retired, 9, true);
	play(retired, 10);
	player.down(retired, 10, 10);
	player.detach(retired);
	player.retire(retired);
	player.up(retired, 10, true);
	run.retired = ReadWidgetOps(tap);
	run.retiredWork = retired;
	const auto gap = player.gap;
	run.built = player.next(gap) && run.built;
	MarkWidgetOpsEvents(tap);
	play(gap, 11);
	player.down(gap, 11, 11);
	player.detach(gap);
	player.up(gap, 11, true);
	run.detached = ReadWidgetOps(tap);
	player.retire(gap);
	run.gap = ReadWidgetOps(tap);
	run.gapWork = gap;
}

// A hosted generation of the verdict check: the function that installs
// its tap and the stub host it was installed on, if both were made.
struct VerdictHosted {
	Fn<void(const std::shared_ptr<twidget::WidgetHost> &host)> install;
	std::shared_ptr<twidget::WidgetHost> host;
};

// The next hosted generation of |id| for |work|, installed on a stub host.
[[nodiscard]] VerdictHosted VerdictHostedGeneration(
		GlobalMsgId id,
		uint64 work) {
	auto result = VerdictHosted{
		.install = TapHostedWidget(id, work, twidget::WidgetTheme()),
	};
	try {
		result.host = VerdictHost();
	} catch (const std::exception &) {
	}
	if (result.install && result.host) {
		result.install(result.host);
	}
	return result;
}

// The cases on hosted generations (stub hosts, never started) through the
// product's WidgetPressDispatched(), WidgetCoreDetached() and
// WidgetPressObserved(), a detached core's queued up and click still
// reported, as the widget render thread reports a reply it took before the
// work's detach, mirrored by its host while that host is not retired and
// by none after.
[[nodiscard]] VerdictRun PlayHostedVerdicts() {
	using Step = Tgw::PressStep::Kind;
	const auto id = GlobalMsgId{
		FullMsgId(PeerId(), MsgId(-1)),
		kVerdictTapSession,
	};
	const auto tap = ArmWidgetOpsTap(id);
	auto generations = std::map<uint64, VerdictHosted>();
	const auto add = [&](uint64 work) {
		const auto &added = generations.emplace(
			work,
			VerdictHostedGeneration(id, work)).first->second;
		return added.install && added.host;
	};
	const auto hostOf = [&](uint64 work) {
		const auto i = generations.find(work);
		return (i != end(generations))
			? i->second.host
			: std::shared_ptr<twidget::WidgetHost>();
	};
	auto result = VerdictRun();
	result.built = add(kVerdictOlderWork) && add(kVerdictNewerWork);
	const auto mirrored = Tgw::HostedWire::EncodeMirrored(
		Tgw::HostedWire::kMirroredRenderer,
		kVerdictClick).value_or(std::string());
	auto closed = QSet<uint64>();
	auto third = Fn<void(const std::shared_ptr<twidget::WidgetHost> &)>();
	PlayVerdicts(result, *tap, {
		.down = [&](uint64 work, uint64 press, uint64 serial) {
			ObservePress(work, press, { Step::Sent, Step::Down }, serial);
		},
		.up = [&](uint64 work, uint64 press, bool clicked) {
			if (!clicked) {
				ObservePress(work, press, { Step::Up });
				return;
			}
			ObservePress(work, press, { Step::Up, Step::Click });
			const auto host = closed.contains(work)
				? nullptr
				: hostOf(work);
			if (host && host->onPlatformMessage) {
				host->onPlatformMessage(mirrored);
			}
		},
		.supersede = [&] {
			third = TapHostedWidget(id, 0, twidget::WidgetTheme());
		},
		.next = add,
		.detach = [](uint64 work) {
			WidgetCoreDetached(work);
		},
		.retire = [&](uint64 work) {
			closed.insert(work);
			if (const auto host = hostOf(work)) {
				WidgetHostRetired(*host);
			}
		},
		.older = kVerdictOlderWork,
		.newer = kVerdictNewerWork,
		.kept = kVerdictKeptWork,
		.retired = kVerdictRetiredWork,
		.gap = kVerdictGapWork,
	});
	result.built = result.built && !mirrored.empty();
	third = nullptr;
	generations.clear();
	DisarmWidgetOpsTap(id);
	return result;
}

// |reading|'s click outcome as |reader| reads it for a press played to
// |played|, and the row verdict of a clean final row with that outcome.
[[nodiscard]] std::pair<QString, Verdict> JudgedClick(
		const AuditState &state,
		const WidgetOpsReading &reading,
		ClickReading reader,
		uint64 played) {
	auto file = VerdictControlFile();
	file.watched = reading;
	file.playedWork = played;
	file.clickOutcome = ClickOutcome(file.watched, reader, played);
	return { file.clickOutcome, Judge(state, file) };
}

// Through |reader|'s RecordWatch() consumer, ClickOutcome() and Judge():
// every case of |run| but its stable and kept clicks is not-observed for
// the work it played (|played|, the retired case its own) and leaves the
// row incomplete (`click-not-observed`), the missed and retired cases
// beside |trusted| trusted clicks of the generation (another press's, where
// this build's tap reads them); the stable click is |stable| for its work
// |played| (complete and clean unless that is not-observed) and
// not-observed for work 0, and the kept click, completed before its core
// retired, is |stable| for its own work too.
void CheckVerdictReader(
		const AuditState &state,
		const QString &prefix,
		const VerdictRun &run,
		ClickReading reader,
		uint64 played,
		const QString &stable,
		int trusted) {
	using Negative = std::tuple<QString, WidgetOpsReading, uint64>;
	const auto negatives = std::vector<Negative>{
		{ u"late"_q, run.late, played },
		{ u"replaced"_q, run.replaced, played },
		{ u"queued"_q, run.queued, played },
		{ u"missed"_q, run.missed, played },
		{ u"withheld"_q, run.withheld, played },
		{ u"superseded"_q, run.superseded, played },
		{ u"retired"_q, run.retired, run.retiredWork },
		{ u"gap"_q, run.gap, run.gapWork },
	};
	const auto bound = [&](const WidgetOpsReading &reading) {
		return (reading.markedWork == run.gapWork)
			&& (reading.markedSerial == 11)
			&& (reading.markedPress == 11)
			&& (reading.downsConfirmed == 1)
			&& !reading.upsConfirmed
			&& !reading.clicksCompleted
			&& reading.markDetached
			&& (reading.pressStepsElsewhere == 2)
			&& (reading.trustedClicks == trusted);
	};
	const auto gapText = [](const WidgetOpsReading &reading) {
		return u"%1/%2/%3 down=%4 up=%5 click=%6 detached=%7 "
			"elsewhere=%8 trusted=%9"_q
			.arg(QString::number(reading.markedWork))
			.arg(QString::number(reading.markedSerial))
			.arg(QString::number(reading.markedPress))
			.arg(reading.downsConfirmed)
			.arg(reading.upsConfirmed)
			.arg(reading.clicksCompleted)
			.arg(FlagText(reading.markDetached))
			.arg(reading.pressStepsElsewhere)
			.arg(reading.trustedClicks);
	};
	auto passed = run.built
		&& (run.missed.trustedClicks == trusted)
		&& (run.retired.trustedClicks == trusted)
		&& bound(run.detached)
		&& bound(run.gap);
	auto details = QStringList{
		u"built=%1 missedTrustedClicks=%2 retiredTrustedClicks=%3"_q.arg(
			FlagText(run.built),
			NumberText(run.missed.trustedClicks),
			NumberText(run.retired.trustedClicks)),
		u"detached=[%1] gapRetired=[%2]"_q.arg(
			gapText(run.detached),
			gapText(run.gap)),
	};
	for (const auto &[name, reading, work] : negatives) {
		const auto judged = JudgedClick(state, reading, reader, work);
		passed = passed
			&& (judged.first == u"not-observed"_q)
			&& !judged.second.complete
			&& judged.second.incomplete.contains(u"click-not-observed"_q);
		details.push_back(name + QChar('=') + judged.first);
	}
	const auto clicked = JudgedClick(state, run.stable, reader, played);
	const auto unplayed = JudgedClick(state, run.stable, reader, 0);
	const auto kept = JudgedClick(state, run.kept, reader, run.keptWork);
	const auto counted = (stable != u"not-observed"_q);
	passed = passed
		&& (clicked.first == stable)
		&& (clicked.second.complete == counted)
		&& (clicked.second.clean == counted)
		&& (unplayed.first == u"not-observed"_q)
		&& !unplayed.second.complete
		&& (kept.first == stable)
		&& (kept.second.complete == counted);
	details.push_back(u"stable=%1 unplayed=%2 kept=%3 | %4"_q.arg(
		clicked.first,
		unplayed.first,
		kept.first,
		VerdictText(clicked.second)));
	Check(
		passed,
		prefix + u"through the %1 reader, an older generation's click that "
			u"completes late on the same tap, a played press down and up "
			u"without a click, a press dispatched before the mark that clicks "
			u"after it, a genuine same-work click after a played mouse-down "
			u"that reached no core, a played down whose up never comes, a "
			u"played press superseded by a newer generation before its click, "
			u"a played press whose up was queued across its core's "
			u"retirement, beside an earlier press's trusted click, and a "
			u"played press bound at its down whose up and click came between "
			u"its core's detach and its host's retirement, counted elsewhere "
			u"against the detached mark, while the host keeps the wrapper "
			u"are not-observed and incomplete; the "
			u"played stable click is %2, also when its core retires after it, "
			u"and nothing for no played work"_q.arg(
				ClickSource(reader),
				stable),
		details.join(QChar(' ')));
}

// The final consumers on real taps' marked interactions: the Release
// consumer (the click decision) and the Debug one (the mirror and the
// decision) on hosted generations, whose mirrored trusted clicks only a
// Debug tap reads, so the Debug reader certifies nothing in a Release
// build.
void CheckMarkedClicks(const AuditState &state, const QString &prefix) {
	const auto mirrors = WidgetHelperMirrors();
	const auto hosted = PlayHostedVerdicts();
	CheckVerdictReader(
		state,
		prefix,
		hosted,
		ClickReading::Decision,
		kVerdictNewerWork,
		u"decided"_q,
		mirrors ? 1 : 0);
	CheckVerdictReader(
		state,
		prefix,
		hosted,
		ClickReading::HelperMirror,
		kVerdictNewerWork,
		mirrors ? u"observed"_q : u"not-observed"_q,
		mirrors ? 1 : 0);
}

// The death verdicts' texts: the end of the widget helper, which every
// host it served reports alike, spared or charged, and the line of a
// summary that carries it, StalledReason(), an ordinary death's label,
// one of Telegram's own refusals and a forged line a renderer's multiline
// stop reason could put before the sandbox header; and their works, below
// the verdict check's own.
constexpr auto kDeathHelperEnded = "the widget engine process ended";
constexpr auto kDeathHelperLine = "Died: the widget engine process ended";
constexpr auto kDeathStalled = "The widget stopped responding";
constexpr auto kDeathLabel = "Widget stopped";
constexpr auto kDeathRefusal
	= "The widget renderer skipped a frame asked to paint";
constexpr auto kDeathForged = "Died: a forged line in the stop reason";
constexpr auto kDeathWork = kVerdictOlderWork - 100;

[[nodiscard]] GlobalMsgId DeathVerdictId(int index) {
	return { FullMsgId(PeerId(), MsgId(-index)), kVerdictTapSession };
}

[[nodiscard]] Tgw::HostedDecision DeathDecision(
		Tgw::HostedOutcome outcome,
		const char *reason,
		Tgw::HostedOutcome transport) {
	return {
		.reason = QString::fromLatin1(reason),
		.outcome = outcome,
		.transport = transport,
	};
}

// Plays |play| as a construction whose host rendered and then reported
// |died| (none for null) as its death, raw, as a host of an ended helper
// does: selected and installed.
[[nodiscard]] bool InstallDeath(WidgetHostedPlay &play, const char *died) {
	const auto built = play.selected() && play.install();
	return built && play.change(died, true);
}

// A runtime log whose summary, sampled from |work| and read as |state|
// says, starts with |line| (a healthy one for null), and whose display log
// puts |stopped|, a fatal stop's reason, before its sandbox header; no
// summary and no header for a 0 |work|.
[[nodiscard]] WidgetRuntimeLog DeathLog(
		uint64 work,
		const char *line,
		const QString &stopped = QString(),
		WidgetLogSummary state = WidgetLogSummary::Fresh) {
	auto result = WidgetRuntimeLog();
	result.summary = work
		? ((line ? (QString::fromLatin1(line) + QChar('\n')) : QString())
			+ u"Loaded: yes"_q)
		: QString();
	result.summaryState = work ? state : WidgetLogSummary::Absent;
	result.summaryWork = work;
	result.text = u"State: Live, generation 1, on screen.\n"_q
		+ (stopped.isEmpty()
			? QString()
			: (u"Stopped (fatal): "_q + stopped + QChar('\n')))
		+ u"Crashes in a row: 0 of 2.\n"_q
		+ (work
			? (u"Current sandbox:\n"_q + result.summary + QChar('\n'))
			: u"No sandbox diagnostics yet.\n"_q)
		+ u"Notes:"_q;
	return result;
}

// The clean control row's file named |name|, its runtime log |log| parsed
// by ParseLog() and read by the log stage on |tap| now.
[[nodiscard]] AttackFile DeathFile(
		const char *name,
		const WidgetOpsTap &tap,
		const WidgetRuntimeLog &log) {
	auto result = VerdictControlFile();
	result.name = QString::fromLatin1(name);
	result.message->log = log;
	ParseLog(result);
	ReadLogDeath(result, ReadWidgetOps(tap));
	return result;
}

// Finalize()'s reading of the file after its removal, then its row's
// verdict.
void SettleDeath(
		const AuditState &state,
		AttackFile &file,
		const WidgetOpsTap &tap) {
	file.reading = ReadWidgetOps(tap);
	file.barrierClosed = WidgetTapFinished(file.reading);
	file.verdict = Judge(state, file);
}

// Whether |file|'s verdict died with |reason| (alive for an empty one),
// measured or not.
[[nodiscard]] bool DeathReads(
		const AttackFile &file,
		const QString &reason,
		bool measured) {
	const auto &verdict = file.verdict;
	return (verdict.died == !reason.isEmpty())
		&& (verdict.deathReason == reason)
		&& (verdict.deathMeasured == measured);
}

[[nodiscard]] QString DeathRecordText(
		const std::optional<WidgetHostedRun> &run) {
	return run ? WidgetHostedRunText(*run) : u"none"_q;
}

[[nodiscard]] QString DeathFileText(const AttackFile &file) {
	return u"%1: %2 diedText=[%3] summaryWork=%4 own=%5 sampled=[%6] "
		"tap=[%7]"_q.arg(
			file.name,
			VerdictText(file.verdict),
			file.diedText,
			QString::number(file.summaryWork),
			FlagText(file.summaryOwn),
			DeathRecordText(file.verdict.sampled),
			WidgetTapCounts(file.reading));
}

[[nodiscard]] QString DeathFilesText(const std::vector<AttackFile> &files) {
	auto result = QStringList();
	for (const auto &file : files) {
		result.push_back(DeathFileText(file));
	}
	return result.join(u" | "_q);
}

// Two works of one message printing the same "Died: " line, the older
// spared and its replacement charged with an ordinary death, played
// through the tap's installed observers, which must have kept each host's
// raw death in that work's own record before any verdict counts; each
// sampled in turn, a log with no sandbox header, and the charged sample
// behind a display log whose stop reason forges a sandbox header and a
// "Died: " line of its own. Then a spared sample beside a work withdrawn
// first, nothing charged.
[[nodiscard]] std::vector<AttackFile> PlaySameLine(
		const AuditState &state,
		const QString &prefix) {
	using Outcome = Tgw::HostedOutcome;
	const auto helperEnded = QString::fromLatin1(kDeathHelperEnded);
	const auto id = DeathVerdictId(3);
	const auto tap = ArmWidgetOpsTap(id);
	auto spared = WidgetHostedPlay(tap, id, kDeathWork);
	auto charged = WidgetHostedPlay(tap, id, kDeathWork - 1);
	const auto sparedBuilt = InstallDeath(spared, kDeathHelperEnded);
	const auto chargedBuilt = InstallDeath(charged, kDeathHelperEnded);
	const auto sparedRecord = spared.record();
	const auto chargedRecord = charged.record();
	const auto observed = sparedBuilt
		&& chargedBuilt
		&& sparedRecord
		&& chargedRecord
		&& (sparedRecord->work != chargedRecord->work)
		&& (sparedRecord->died == helperEnded)
		&& (chargedRecord->died == helperEnded);
	spared.finish(DeathDecision(
		Outcome::Spared,
		kDeathLabel,
		Outcome::Spared));
	charged.finish(DeathDecision(
		Outcome::Ordinary,
		kDeathLabel,
		Outcome::Ordinary));
	const auto sparedDropped = spared.drop();
	const auto chargedDropped = charged.drop();
	Check(
		observed && sparedDropped && chargedDropped,
		prefix + u"the same-line works are played through the tap's "
			u"installed observers: both selected and installed, each "
			u"work's own record keeping the raw death its host reported, "
			u"both dropped with their factories and wrappers released"_q,
		u"spared=[%1] charged=[%2] dropped=%3/%4"_q.arg(
			DeathRecordText(sparedRecord),
			DeathRecordText(chargedRecord),
			FlagText(sparedDropped),
			FlagText(chargedDropped)));
	auto result = std::vector<AttackFile>();
	result.push_back(DeathFile(
		"spared sample",
		*tap,
		DeathLog(spared.work(), kDeathHelperLine)));
	result.push_back(DeathFile(
		"charged replacement sample",
		*tap,
		DeathLog(charged.work(), kDeathHelperLine)));
	result.push_back(DeathFile(
		"no sandbox header",
		*tap,
		DeathLog(0, nullptr)));
	result.push_back(DeathFile(
		"charged sample behind a forged stop reason",
		*tap,
		DeathLog(
			charged.work(),
			kDeathHelperLine,
			(u"a renderer's stop\nCurrent sandbox:\n"_q
				+ QString::fromLatin1(kDeathForged)))));
	result.push_back(DeathFile(
		"spared sample behind a forged stop reason",
		*tap,
		DeathLog(
			spared.work(),
			kDeathHelperLine,
			u"a renderer's stop\nLast sandbox:\nLoaded: no"_q)));
	for (auto &file : result) {
		SettleDeath(state, file, *tap);
	}
	DisarmWidgetOpsTap(id);

	const auto collateralId = DeathVerdictId(4);
	const auto collateralTap = ArmWidgetOpsTap(collateralId);
	auto healthy = WidgetHostedPlay(
		collateralTap,
		collateralId,
		kDeathWork - 2);
	auto withdrawn = WidgetHostedPlay(
		collateralTap,
		collateralId,
		kDeathWork - 3);
	const auto healthyBuilt = InstallDeath(healthy, kDeathHelperEnded);
	const auto healthyRecord = healthy.record();
	healthy.finish(DeathDecision(
		Outcome::Spared,
		kDeathLabel,
		Outcome::Spared));
	withdrawn.finish(Tgw::HostedDecision());
	const auto healthyDropped = healthy.drop();
	const auto withdrawnDropped = withdrawn.drop();
	result.push_back(DeathFile(
		"healthy collateral sample",
		*collateralTap,
		DeathLog(healthy.work(), kDeathHelperLine)));
	SettleDeath(state, result.back(), *collateralTap);
	DisarmWidgetOpsTap(collateralId);

	const auto unmeasured = WidgetHostedDeathText({ .died = true });
	const auto line = QString::fromLatin1(kDeathHelperLine);
	const auto &ofSpared = result[0];
	const auto &ofCharged = result[1];
	const auto &unsampled = result[2];
	const auto &forged = result[3];
	const auto &hidden = result[4];
	const auto &collateral = result[5];
	Check(
		observed
			&& healthyBuilt
			&& withdrawn.selected()
			&& healthyRecord
			&& (healthyRecord->died == helperEnded)
			&& healthyDropped
			&& withdrawnDropped
			&& DeathReads(ofSpared, unmeasured, false)
			&& DeathReads(ofCharged, helperEnded, true)
			&& DeathReads(unsampled, unmeasured, false)
			&& DeathReads(collateral, QString(), false)
			&& ofSpared.verdict.complete
			&& !ofSpared.verdict.clean
			&& !ofCharged.verdict.clean
			&& collateral.verdict.complete
			&& collateral.verdict.clean
			&& (ofSpared.diedText == line)
			&& (ofCharged.diedText == line)
			&& (collateral.diedText == line)
			&& (ofSpared.summaryWork == spared.work())
			&& (ofCharged.summaryWork == charged.work())
			&& !unsampled.summaryWork,
		prefix + u"two works printing the same \"Died: \" line, a spared "
			u"one and its charged replacement, give a dead file whichever is "
			u"sampled, its reason measured only from the charged work's own "
			u"sample and \"charged, reason unmeasured\" for the spared one's "
			u"or a log with no sample; a spared sample beside a work "
			u"withdrawn first is alive and clean; each raw line and sampled "
			u"work is kept"_q,
		DeathFilesText(result));
	Check(
		DeathReads(forged, helperEnded, true)
			&& (forged.diedText == line)
			&& (forged.summaryWork == charged.work())
			&& forged.diedLine
			&& DeathReads(hidden, unmeasured, false)
			&& (hidden.diedText == line)
			&& (hidden.summaryWork == spared.work())
			&& !hidden.diedLine,
		prefix + u"a hosted file's \"Died: \" line is the first line of "
			u"the summary its log took, paired with the work that summary "
			u"was sampled from: a stop reason that forges a sandbox header "
			u"and another \"Died: \" line before it neither replaces nor "
			u"hides it, while the display log's own line, the local rule's, "
			u"is kept apart"_q,
		DeathFilesText({ forged, hidden }));
	return result;
}

// A work of the message |index| installed and sampled healthy at the log
// stage, then dropped, its final left deliberately last at the barrier,
// then given as |decision|, and a copy of its observation called again
// with an ordinary death, which the tap's own guard stops; whether the
// file read incomplete and alive meanwhile and the second call counted
// nothing.
[[nodiscard]] std::pair<AttackFile, bool> PlayLastFinal(
		const AuditState &state,
		int index,
		const char *name,
		const Tgw::HostedDecision &decision) {
	const auto id = DeathVerdictId(index);
	const auto tap = ArmWidgetOpsTap(id);
	auto played = WidgetHostedPlay(tap, id, kDeathWork - 10 - index);
	const auto built = InstallDeath(played, nullptr);
	auto file = DeathFile(
		name,
		*tap,
		DeathLog(played.work(), nullptr));
	const auto dropped = played.drop();
	SettleDeath(state, file, *tap);
	const auto pending = !file.barrierClosed
		&& file.verdict.incomplete.contains(u"hosts-pending"_q)
		&& !file.verdict.complete
		&& !file.verdict.died;
	const auto again = played.observation();
	played.finish(decision);
	if (again) {
		again(DeathDecision(
			Tgw::HostedOutcome::Ordinary,
			kDeathLabel,
			Tgw::HostedOutcome::Ordinary));
	}
	SettleDeath(state, file, *tap);
	DisarmWidgetOpsTap(id);
	const auto once = (file.reading.hostedFinal == 1)
		&& !file.reading.fatalFrozen;
	return { std::move(file), built && dropped && pending && once };
}

// The barrier with a final left deliberately last: a refusal Telegram
// made of the sampled work's frame on a route the stream spared, against
// a valid reply on that route.
[[nodiscard]] std::vector<AttackFile> PlayLateCharges(
		const AuditState &state,
		const QString &prefix) {
	using Outcome = Tgw::HostedOutcome;
	auto [refused, refusedBarrier] = PlayLastFinal(
		state,
		5,
		"refusal on a spared route",
		DeathDecision(Outcome::Refused, kDeathRefusal, Outcome::Spared));
	auto [valid, validBarrier] = PlayLastFinal(
		state,
		6,
		"valid reply on a spared route",
		DeathDecision(Outcome::Spared, kDeathLabel, Outcome::Spared));
	auto result = std::vector<AttackFile>();
	result.push_back(std::move(refused));
	result.push_back(std::move(valid));
	const auto &sampled = result[0].verdict.sampled;
	Check(
		refusedBarrier
			&& validBarrier
			&& result[0].barrierClosed
			&& result[0].verdict.complete
			&& !result[0].verdict.clean
			&& DeathReads(
				result[0],
				QString::fromLatin1(kDeathRefusal),
				true)
			&& sampled
			&& (sampled->transport == Outcome::Spared)
			&& result[1].barrierClosed
			&& result[1].verdict.complete
			&& result[1].verdict.clean
			&& DeathReads(result[1], QString(), false),
		prefix + u"a file sampled healthy is incomplete and alive while its "
			u"work's final is left last at the barrier, then dead, measured, "
			u"of a refusal Telegram made of its frame on a route the stream "
			u"spared, and clean after a valid reply on that route; a second "
			u"call of a copy of its final observation, which the tap's own "
			u"guard stops, charges nothing"_q,
		DeathFilesText(result));
	return result;
}

// A charged first selection, then kWidgetHostedRuns + 1 later uncharged
// ones, its file's log stage read before the eviction and after it.
[[nodiscard]] std::vector<AttackFile> PlayEviction(
		const AuditState &state,
		const QString &prefix) {
	using Outcome = Tgw::HostedOutcome;
	const auto id = DeathVerdictId(7);
	const auto tap = ArmWidgetOpsTap(id);
	auto first = WidgetHostedPlay(tap, id, kDeathWork - 100);
	const auto firstBuilt = InstallDeath(first, kDeathHelperEnded);
	const auto firstFinished = first.finish(DeathDecision(
		Outcome::Stalled,
		kDeathStalled,
		Outcome::Stalled));
	const auto firstDropped = first.drop();
	auto played = firstBuilt && firstFinished && firstDropped;
	auto result = std::vector<AttackFile>();
	result.push_back(DeathFile(
		"first sampled before the eviction",
		*tap,
		DeathLog(first.work(), kDeathHelperLine)));
	for (auto i = 1; i <= kWidgetHostedRuns + 1; ++i) {
		auto later = WidgetHostedPlay(tap, id, first.work() - i);
		const auto chosen = later.selected();
		const auto finished = later.finish((i % 2)
			? DeathDecision(Outcome::Spared, kDeathLabel, Outcome::Spared)
			: Tgw::HostedDecision());
		const auto dropped = later.drop();
		played = played && chosen && finished && dropped;
	}
	result.push_back(DeathFile(
		"first sampled after the eviction",
		*tap,
		DeathLog(first.work(), kDeathHelperLine)));
	for (auto &file : result) {
		SettleDeath(state, file, *tap);
	}
	DisarmWidgetOpsTap(id);
	const auto &reading = result[0].reading;
	const auto evicted = std::none_of(
		begin(reading.hostedRuns),
		end(reading.hostedRuns),
		[&](const WidgetHostedRun &run) { return (run.work == first.work()); });
	Check(
		played
			&& evicted
			&& (int(reading.hostedRuns.size()) == kWidgetHostedRuns)
			&& (reading.fatalFrozen == 1)
			&& result[0].barrierClosed
			&& DeathReads(
				result[0],
				QString::fromLatin1(kDeathStalled),
				true)
			&& DeathReads(
				result[1],
				WidgetHostedDeathText({ .died = true }),
				false),
		prefix + u"a charged first selection evicted from the records by 33 "
			u"later uncharged ones keeps its file dead through the "
			u"cumulative charge, measured only when the log stage read its "
			u"record before the eviction"_q,
		DeathFilesText(result));
	return result;
}

// A charged work, its refusal recorded, and a replacement the stream
// spared, both played through the tap's installed observers; the charged
// work's own fresh summary and a stale one holding its line and work, as a
// log request for the replacement that timed out leaves the runtime's
// last sample, each read at the log stage, then at the removal while the
// charged work's record is kept and again once kWidgetHostedRuns later
// uncharged selections evicted it.
[[nodiscard]] std::vector<AttackFile> PlayStaleSample(
		const AuditState &state,
		const QString &prefix) {
	using Outcome = Tgw::HostedOutcome;
	const auto refusal = QString::fromLatin1(kDeathRefusal);
	const auto unmeasured = WidgetHostedDeathText({ .died = true });
	const auto line = QString::fromLatin1(kDeathHelperLine);
	const auto id = DeathVerdictId(8);
	const auto tap = ArmWidgetOpsTap(id);
	auto charged = WidgetHostedPlay(tap, id, kDeathWork - 200);
	auto replacement = WidgetHostedPlay(tap, id, kDeathWork - 201);
	const auto chargedBuilt = InstallDeath(charged, kDeathHelperEnded);
	const auto replacementBuilt = InstallDeath(
		replacement,
		kDeathHelperEnded);
	const auto chargedFinished = charged.finish(DeathDecision(
		Outcome::Refused,
		kDeathRefusal,
		Outcome::None));
	const auto replacementFinished = replacement.finish(DeathDecision(
		Outcome::Spared,
		kDeathLabel,
		Outcome::Spared));
	const auto chargedDropped = charged.drop();
	const auto replacementDropped = replacement.drop();
	auto played = chargedBuilt
		&& replacementBuilt
		&& chargedFinished
		&& replacementFinished
		&& chargedDropped
		&& replacementDropped;
	auto result = std::vector<AttackFile>();
	result.push_back(DeathFile(
		"charged work's own fresh sample",
		*tap,
		DeathLog(charged.work(), kDeathHelperLine)));
	result.push_back(DeathFile(
		"stale sample of the charged work",
		*tap,
		DeathLog(
			charged.work(),
			kDeathHelperLine,
			QString(),
			WidgetLogSummary::Stale)));
	for (auto &file : result) {
		SettleDeath(state, file, *tap);
	}
	const auto &fresh = result[0];
	const auto &stale = result[1];
	const auto kept = fresh.barrierClosed
		&& stale.barrierClosed
		&& DeathReads(fresh, refusal, true)
		&& DeathReads(stale, unmeasured, false);
	const auto keptText = DeathFilesText(result);
	for (auto i = 1; i <= kWidgetHostedRuns; ++i) {
		auto later = WidgetHostedPlay(tap, id, replacement.work() - i);
		const auto chosen = later.selected();
		const auto finished = later.finish(Tgw::HostedDecision());
		const auto dropped = later.drop();
		played = played && chosen && finished && dropped;
	}
	for (auto &file : result) {
		SettleDeath(state, file, *tap);
	}
	DisarmWidgetOpsTap(id);
	const auto &reading = fresh.reading;
	const auto evicted = std::none_of(
		begin(reading.hostedRuns),
		end(reading.hostedRuns),
		[&](const WidgetHostedRun &run) {
			return (run.work == charged.work());
		});
	Check(
		played
			&& kept
			&& evicted
			&& fresh.barrierClosed
			&& stale.barrierClosed
			&& (reading.desktopCharges == 1)
			&& DeathReads(fresh, refusal, true)
			&& DeathReads(stale, unmeasured, false)
			&& fresh.summaryOwn
			&& fresh.logDeath.measured
			&& !stale.summaryOwn
			&& !stale.logDeath.measured
			&& !stale.verdict.sampled
			&& (stale.diedText == line)
			&& (stale.summaryWork == charged.work()),
		prefix + u"a charged work's refusal is measured only from its own "
			u"fresh sample: a stale summary holding its line and work, as a "
			u"timed-out log request for its replacement leaves the "
			u"runtime's last sample, keeps that line and work raw and the "
			u"file dead through the cumulative charge, but \"charged, reason "
			u"unmeasured\" at the log stage, at the removal and after the "
			u"record's eviction, never taking that work's reason"_q,
		u"kept: %1 | evicted: %2"_q.arg(keptText, DeathFilesText(result)));
	return result;
}

// The emitters of the totals stage and the onFinish callback, defined with
// the totals; the controls below capture what they write.
void EmitPendingRows(AuditState &state, const QString &path);
void EmitTotals(AuditState &state, const QString &path, bool checks);

// The click source the field contract names for this build: the Debug
// helper's mirror, or the click decision alone in a Release harness.
[[nodiscard]] QString ConfiguredClickSource() {
#ifdef _DEBUG
	return u"helper-mirror"_q;
#else // _DEBUG
	return u"click-decision"_q;
#endif // _DEBUG
}

// A file named |name| refused before any renderer of it was selected, as
// the first file of a run can be, or every file of a run that finishes
// before any selection: its reading asks nothing of a runtime.
[[nodiscard]] AttackFile UnselectedFile(
		const AuditState &state,
		const QString &name) {
	auto file = AttackFile();
	file.name = name;
	file.message = std::make_shared<WidgetFixtureMessage>();
	file.notRun = u"refused at its injection"_q;
	file.rowPath = u"final"_q;
	file.barrierClosed = true;
	ParseLog(file);
	file.verdict = Judge(state, file);
	return file;
}

// What EmitPendingRows() of |files| at |path| writes, then EmitTotals()
// with |checks| unless none, on |state|'s fixture, captured.
[[nodiscard]] AuditCapture Emitted(
		const AuditState &state,
		std::vector<AttackFile> files,
		const QString &path,
		std::optional<bool> checks) {
	auto result = AuditCapture();
	auto control = AuditState();
	control.fixture = state.fixture;
	control.files = std::move(files);
	control.capture = &result;
	EmitPendingRows(control, path);
	if (checks) {
		EmitTotals(control, path, *checks);
	}
	return result;
}

// The fields of the first |marker| row |capture| holds, of the file named
// |file| when given; none when it holds no such row.
[[nodiscard]] QJsonObject CapturedRow(
		const AuditCapture &capture,
		const QString &marker,
		const QString &file = QString()) {
	for (const auto &[written, fields] : capture.rows) {
		if ((written == marker)
			&& (file.isEmpty()
				|| (fields.value(u"file"_q).toString() == file))) {
			return fields;
		}
	}
	return QJsonObject();
}

[[nodiscard]] QJsonObject RowClick(const QJsonObject &row) {
	return row.value(u"click"_q).toObject();
}

// The click source each of |capture|'s checks names in its details
// ("source=<value>"), in order.
[[nodiscard]] QStringList CheckedSources(const AuditCapture &capture) {
	const auto key = u"source="_q;
	auto result = QStringList();
	for (const auto &check : capture.checks) {
		for (const auto &field : check.details.split(QChar(' '))) {
			if (field.startsWith(key)) {
				result.push_back(field.mid(key.size()));
			}
		}
	}
	return result;
}

[[nodiscard]] QString CaptureText(const AuditCapture &capture) {
	auto result = QStringList();
	for (const auto &[marker, fields] : capture.rows) {
		const auto path = fields.value(u"path"_q).toString();
		if (marker == u"ATTACK"_q) {
			const auto click = RowClick(fields);
			result.push_back(u"%1 of %2 at %3: source=%4 attempted=%5"_q.arg(
				marker,
				fields.value(u"file"_q).toString(),
				path,
				click.value(u"source"_q).toString(),
				FlagText(click.value(u"attempted"_q).toBool())));
		} else {
			result.push_back(u"%1 at %2: clickSource=%3 clicksAttempted=%4 "
				u"ran=%5"_q.arg(
					marker,
					path,
					fields.value(u"clickSource"_q).toString(),
					NumberText(fields.value(u"clicksAttempted"_q).toInt()),
					NumberText(fields.value(u"ran"_q).toInt())));
		}
	}
	result.push_back(u"checks=%1 sources=[%2]"_q.arg(
		NumberText(int(capture.checks.size())),
		CheckedSources(capture).join(u","_q)));
	return result.join(u"; "_q);
}

// The rows and totals of a run of only files no renderer was selected
// for, as its last stages write them, captured: the onFinish callback's
// pending rows and totals of such a run that finishes early, deciding no
// check, then the totals stage's totals of such a run with its checks
// decided, the one whose details name a click source the completeness
// check. Each names the build's source, its clicks unattempted.
void CheckUnselectedRuns(const AuditState &state, const QString &prefix) {
	const auto configured = ConfiguredClickSource();
	const auto finished = Emitted(
		state,
		{
			UnselectedFile(state, u"refused before a selection"_q),
			UnselectedFile(state, u"refused before a selection too"_q),
		},
		u"onFinish"_q,
		false);
	auto rows = 0;
	auto unattempted = true;
	for (const auto &[marker, fields] : finished.rows) {
		if (marker == u"ATTACK"_q) {
			const auto click = RowClick(fields);
			++rows;
			unattempted = unattempted
				&& (click.value(u"source"_q).toString() == configured)
				&& !click.value(u"attempted"_q).toBool();
		}
	}
	const auto early = CapturedRow(finished, u"ATTACK_TOTALS"_q);
	Check(
		(rows == 2)
			&& unattempted
			&& (early.value(u"clickSource"_q).toString() == configured)
			&& (early.value(u"clicksAttempted"_q).toInt() == 0)
			&& (early.value(u"ran"_q).toInt() == 0)
			&& finished.checks.empty(),
		prefix + u"the onFinish callback's EmitPendingRows() and "
			u"EmitTotals() of a run of only files refused before any of "
			u"their renderers was selected, finishing early, write each "
			u"file's row and the totals with the build's click source, "
			u"\"%1\", no click attempted and nothing run, and decide no "
			u"check"_q.arg(configured),
		CaptureText(finished));
	const auto decided = Emitted(
		state,
		{ UnselectedFile(state, u"refused before a selection"_q) },
		u"totals"_q,
		true);
	const auto totals = CapturedRow(decided, u"ATTACK_TOTALS"_q);
	Check(
		!decided.checks.empty()
			&& (CheckedSources(decided) == QStringList{ configured })
			&& (totals.value(u"clickSource"_q).toString() == configured)
			&& (totals.value(u"clicksAttempted"_q).toInt() == 0),
		prefix + u"the totals stage's EmitTotals() of a run of only such "
			u"files, its checks decided, names the build's click source, "
			u"\"%1\", in the totals it writes and in the details of its "
			u"completeness check, the one check that names a source, no "
			u"click attempted"_q.arg(configured),
		CaptureText(decided));
}

// A file refused before any renderer of it was selected: its reading
// reads not run, alive and uncharged, its click unattempted, and the row
// EmitRow() writes of it beside a selected, clicked file names the
// build's click source as that file's row does; then such runs
// (CheckUnselectedRuns()).
[[nodiscard]] std::vector<AttackFile> PlayUnselected(
		const AuditState &state,
		const QString &prefix) {
	const auto configured = ConfiguredClickSource();
	auto file = UnselectedFile(state, u"refused before a selection"_q);
	auto selected = VerdictControlFile();
	selected.name = u"selected beside it"_q;
	const auto mixed = Emitted(
		state,
		{ selected, file },
		u"totals"_q,
		std::nullopt);
	const auto click = RowClick(CapturedRow(mixed, u"ATTACK"_q, file.name));
	const auto sibling = RowClick(
		CapturedRow(mixed, u"ATTACK"_q, selected.name));
	Check(
		!WidgetTapHosted(file.reading)
			&& (file.reading.factoriesSelected == 0)
			&& !WidgetTapCharged(file.reading)
			&& !file.verdict.ran
			&& !file.verdict.died
			&& file.verdict.deathReason.isEmpty()
			&& (file.verdict.notRun == file.notRun)
			&& !file.clickAttempted
			&& (click.value(u"source"_q).toString() == configured)
			&& !click.value(u"attempted"_q).toBool()
			&& (sibling.value(u"source"_q).toString() == configured)
			&& sibling.value(u"attempted"_q).toBool()
			&& mixed.checks.empty(),
		prefix + u"a file refused before any of its renderers was selected "
			u"reads not run, alive and uncharged with its click unattempted, "
			u"and the row EmitRow() writes of it beside a selected, clicked "
			u"file names the build's click source, \"%1\", unattempted, as "
			u"that file's row does"_q.arg(configured),
		DeathFileText(file) + u" | "_q + CaptureText(mixed));
	CheckUnselectedRuns(state, prefix);
	auto result = std::vector<AttackFile>();
	result.push_back(std::move(file));
	return result;
}

// Judge() of hosted files whose readings come from real taps of messages
// of no history, each work played as WidgetHostedPlay plays it, then of a
// file refused before any selection, then the totals' dead list of them
// all.
void CheckDeathVerdicts(const AuditState &state, const QString &prefix) {
	auto control = AuditState();
	const auto take = [&](std::vector<AttackFile> files) {
		for (auto &file : files) {
			control.files.push_back(std::move(file));
		}
	};
	take(PlaySameLine(state, prefix));
	take(PlayLateCharges(state, prefix));
	take(PlayEviction(state, prefix));
	take(PlayStaleSample(state, prefix));
	take(PlayUnselected(state, prefix));
	const auto dead = DeadFiles(control);
	const auto unmeasured = WidgetHostedDeathText({ .died = true });
	const auto expected = QStringList{
		u"spared sample: "_q + unmeasured,
		u"charged replacement sample: "_q
			+ QString::fromLatin1(kDeathHelperEnded),
		u"no sandbox header: "_q + unmeasured,
		u"charged sample behind a forged stop reason: "_q
			+ QString::fromLatin1(kDeathHelperEnded),
		u"spared sample behind a forged stop reason: "_q + unmeasured,
		u"refusal on a spared route: "_q
			+ QString::fromLatin1(kDeathRefusal),
		u"first sampled before the eviction: "_q
			+ QString::fromLatin1(kDeathStalled),
		u"first sampled after the eviction: "_q + unmeasured,
		u"charged work's own fresh sample: "_q
			+ QString::fromLatin1(kDeathRefusal),
		u"stale sample of the charged work: "_q + unmeasured,
	};
	Check(
		(dead == expected) && (int(control.files.size()) == 13),
		prefix + u"the totals' dead list names every file whose row is "
			u"dead, with that row's reason, and no file alive in its row"_q,
		dead.join(u"; "_q));
}

// Pure Judge() controls, each failing on a predicate that ignores what it
// varies: the clean row above, the same row after a fixture refusal that
// came once its renderer was built (its load wait, click and watch
// skipped), the clean row with one renderer violation and no leak, and the
// clean row whose click decision a Release harness's helper reported; then
// the click outcomes those rows stand for, each reader's consumer on real
// taps' marked interactions, and the death of a file and the totals' dead
// list (CheckDeathVerdicts()).
void CheckVerdicts(const AuditState &state) {
	const auto prefix = Prefixed(u"verdict self-check: "_q);

	const auto clean = Judge(state, VerdictControlFile());
	Check(
		clean.ran && clean.loaded && clean.complete && clean.clean,
		prefix + u"a loaded, clicked, watched and finished file with no "
			u"leak, gap or violation is complete and clean"_q,
		VerdictText(clean));

	const auto reason = u"no HtmlWidget view: the list shows no laid-out "
		u"HtmlWidget card of its own for it"_q;
	auto refusedFile = VerdictControlFile();
	refusedFile.notRun = reason;
	refusedFile.live = false;
	refusedFile.clickAttempted = false;
	refusedFile.clickOutcome = QString();
	refusedFile.watchMs = 0;
	const auto refused = Judge(state, refusedFile);
	Check(
		refused.ran
			&& refused.loaded
			&& refused.notRun.isEmpty()
			&& (refused.refusal == reason)
			&& refused.incomplete.contains(u"fixture-refused"_q)
			&& refused.incomplete.contains(u"watch-missing"_q)
			&& !refused.complete
			&& !refused.clean,
		prefix + u"a fixture refusal after a renderer was built, with no "
			u"watch, ran but is incomplete with its reason, never clean"_q,
		VerdictText(refused));

	auto violatingFile = VerdictControlFile();
	violatingFile.reading.violations = 1;
	violatingFile.reading.violationDetails = {
		"image refused: https://127.0.0.1:9999/refused.png",
	};
	const auto violating = Judge(state, violatingFile);
	Check(
		violating.complete && violating.loaded && !violating.clean,
		prefix + u"a complete loaded file with no leak and one renderer "
			u"violation is complete but not clean"_q,
		VerdictText(violating));

	auto decidedFile = VerdictControlFile();
	decidedFile.clickOutcome = u"decided"_q;
	const auto decided = Judge(state, decidedFile);
	auto unclickedFile = VerdictControlFile();
	unclickedFile.clickOutcome = u"not-observed"_q;
	const auto unclicked = Judge(state, unclickedFile);
	Check(
		decided.complete
			&& decided.clean
			&& !unclicked.complete
			&& unclicked.incomplete.contains(u"click-not-observed"_q),
		prefix + u"a live card whose click the helper's renderer decided "
			u"(hosted without the Debug helper's tap) is complete, one whose "
			u"click was not observed is not"_q,
		VerdictText(decided) + u" | "_q + VerdictText(unclicked));

	CheckClickOutcomes(prefix);
	CheckMarkedClicks(state, prefix);
	CheckDeathVerdicts(state, prefix);
}

[[nodiscard]] QJsonObject ReadingObject(const WidgetOpsReading &r) {
	return QJsonObject{
		{ u"factoriesSelected"_q, r.factoriesSelected },
		{ u"factoriesCompleted"_q, r.factoriesCompleted },
		{ u"factoriesThrew"_q, r.factoriesThrew },
		{ u"factoriesReleased"_q, r.factoriesReleased },
		{ u"wrappersBuilt"_q, r.wrappersBuilt },
		{ u"wrappersDestroyed"_q, r.wrappersDestroyed },
		{ u"hostMessages"_q, r.hostMessages },
		{ u"batches"_q, r.batches },
		{ u"ops"_q, r.ops },
		{ u"scanGaps"_q, r.scanGaps },
		{ u"gapExamples"_q, Strings(r.gapExamples) },
		{ u"leaks"_q, r.leaks },
		{ u"leakExamples"_q, Strings(r.leakExamples) },
		{ u"notes"_q, r.notes },
		{ u"noteExamples"_q, Strings(r.noteExamples) },
		{ u"violations"_q, r.violations },
		{ u"violationDetails"_q, Strings(r.violationDetails) },
		{ u"ackQueries"_q, r.ackQueries },
		{ u"ackResults"_q, r.ackResults },
		{ u"renderedHosts"_q, r.renderedHosts },
		{ u"fatalFrozen"_q, r.fatalFrozen },
		{ u"hostedSelected"_q, r.hostedSelected },
		{ u"hostedFinal"_q, r.hostedFinal },
		{ u"desktopCharges"_q, r.desktopCharges },
		{ u"sparedRuns"_q, r.sparedRuns },
		{ u"unaffectedRuns"_q, r.unaffectedRuns },
		{ u"counts"_q, WidgetTapCounts(r) },
	};
}

[[nodiscard]] QJsonObject ShowObject(
		const std::shared_ptr<const ChatListShow> &shown,
		QRect card) {
	if (!shown || !shown->finished) {
		return QJsonObject{ { u"finished"_q, false } };
	}
	return QJsonObject{
		{ u"finished"_q, true },
		{ u"shown"_q, shown->shown() },
		{ u"kind"_q, shown->kind },
		{ u"refusal"_q, ChatListShowRefusalName(shown->refusal) },
		{ u"reason"_q, shown->reason },
		{ u"local"_q, RectText(shown->local) },
		{ u"card"_q, RectText(card) },
		{ u"wholeCard"_q, !card.isEmpty() && (shown->local == card) },
		{ u"rect"_q, RectText(shown->rect) },
		{ u"viewport"_q, RectText(shown->viewport) },
		{ u"hover"_q, shown->hoverSent
			? PointText(shown->hover)
			: u"none"_q },
		{ u"hovered"_q, shown->hovered },
		{ u"paints"_q, shown->paints },
		{ u"elapsedMs"_q, qint64(shown->elapsedMs) },
		{ u"readiness"_q, shown->readyDetails },
	};
}

// The file's one ATTACK row. Its reading is final only when |barrierClosed|
// in its "final" row (`readingFinal`), and its death is read from that
// reading.
void EmitRow(AuditState &state, int index, const QString &path) {
	auto &file = state.files[index];
	if (file.rowEmitted) {
		return;
	}
	const auto &message = *file.message;
	file.rowEmitted = true;
	file.rowPath = path;
	if (message.tap && !file.barrierClosed) {
		file.reading = ReadWidgetOps(*message.tap);
	}
	if (!message.tap) {
		file.barrierClosed = true;
	}
	if (!file.endMs) {
		file.endMs = WallNow();
	}
	file.verdict = Judge(state, file);
	const auto &verdict = file.verdict;
	WriteRow(state, u"ATTACK"_q, {
		{ u"file"_q, file.name },
		{ u"index"_q, index },
		{ u"bytes"_q, int(file.html.size()) },
		{ u"sha256"_q, file.sha256 },
		{ u"item"_q, IdText(message.id) },
		{ u"path"_q, path },
		{ u"startMs"_q, file.startMs },
		{ u"endMs"_q, file.endMs },
		{ u"ran"_q, verdict.ran },
		{ u"notRun"_q, verdict.notRun },
		{ u"loaded"_q, verdict.loaded },
		{ u"notLoaded"_q, verdict.notLoaded },
		{ u"died"_q, verdict.died },
		{ u"deathReason"_q, verdict.deathReason },
		{ u"deathMeasured"_q, verdict.deathMeasured },
		{ u"complete"_q, verdict.complete },
		{ u"incomplete"_q, Strings(verdict.incomplete) },
		{ u"fixtureRefusal"_q, verdict.refusal },
		{ u"clean"_q, verdict.clean },
		{ u"fixture"_q, message.shape },
		{ u"loadShow"_q, ShowObject(file.loadShow, file.loadCard.card) },
		{ u"loadOutcome"_q, file.loadOutcome },
		{ u"loadWaitMs"_q, qint64(file.loadWaitMs) },
		{ u"stateAfterLoad"_q, file.stateAfterLoad },
		{ u"liveAtClick"_q, file.live },
		{ u"clickShow"_q, ShowObject(file.clickShow, file.accepted.card) },
		{ u"click"_q, QJsonObject{
			{ u"outcome"_q, file.clickOutcome },
			{ u"details"_q, file.clickDetails },
			{ u"attempted"_q, file.clickAttempted },
			{ u"pressMs"_q, file.pressMs },
			{ u"source"_q, ClickSource() },
			{ u"playedWork"_q, QString::number(file.playedWork) },
			{ u"markedGeneration"_q, file.watched.markedGeneration },
			{
				u"markedWork"_q,
				QString::number(file.watched.markedWork),
			},
			{ u"markedSerial"_q, qint64(file.watched.markedSerial) },
			{ u"markedPress"_q, qint64(file.watched.markedPress) },
			{ u"markSuperseded"_q, file.watched.markSuperseded },
			{ u"markDetached"_q, file.watched.markDetached },
			{ u"downsConfirmed"_q, file.watched.downsConfirmed },
			{ u"upsConfirmed"_q, file.watched.upsConfirmed },
			{ u"clicksCompleted"_q, file.watched.clicksCompleted },
			{
				u"pressStepsElsewhere"_q,
				file.watched.pressStepsElsewhere,
			},
			{ u"trustedClicks"_q, file.watched.trustedClicks },
			{ u"trustedEvents"_q, file.watched.trustedEvents },
			{ u"eventTypes"_q, Strings(file.watched.trustedEventTypes) },
		} },
		{ u"watchMs"_q, qint64(file.watchMs) },
		{ u"stateAfterWatch"_q, file.stateAfterWatch },
		{ u"log"_q, QJsonObject{
			{ u"source"_q, message.log.source },
			{ u"chars"_q, message.log.chars },
			{ u"sandboxHeader"_q, file.logHeader },
			{ u"diedLine"_q, file.diedLine },
			{ u"diedText"_q, file.diedText },
			{ u"summaryWork"_q, QString::number(file.summaryWork) },
			{ u"summaryOwn"_q, file.summaryOwn },
			{ u"sampledRun"_q, verdict.sampled
				? QJsonValue(WidgetHostedRunText(*verdict.sampled))
				: QJsonValue(QJsonValue::Null) },
		} },
		{ u"layers"_q, Strings(message.layers) },
		{ u"barrierClosed"_q, file.barrierClosed },
		{ u"readingFinal"_q, FinalRow(file) && file.barrierClosed },
		{ u"barrierMs"_q, qint64(file.barrierMs) },
		{ u"itemGone"_q, file.itemGone
			|| !WidgetFixtureItem(*state.fixture, message.id) },
		{ u"examplesTruncated"_q, (file.reading.leaks
				> int(file.reading.leakExamples.size()))
			|| (file.reading.notes
				> int(file.reading.noteExamples.size()))
			|| (file.reading.violations
				> int(file.reading.violationDetails.size())) },
		{ u"tap"_q, ReadingObject(file.reading) },
	});
}

void Finalize(AuditState &state, int index) {
	auto &file = state.files[index];
	const auto &message = *file.message;
	file.barrierMs = crl::now() - message.removedAt;
	file.reading = message.tap
		? ReadWidgetOps(*message.tap)
		: WidgetOpsReading();
	file.barrierClosed = WidgetTapFinished(file.reading);
	file.itemGone = !WidgetFixtureItem(*state.fixture, message.id);
	file.endMs = WallNow();
	if (!file.barrierClosed && state.abort.isEmpty()) {
		state.abort = u"not run: the hosts of %1 had not finished %2 ms "
			u"after its removal (%3), so no later file was started"_q.arg(
				file.name,
				NumberText(file.barrierMs),
				WidgetTapCounts(file.reading));
	}
	EmitRow(state, index, u"final"_q);
}

// ---------------------------------------------------------------------
// Totals.

void EmitPendingRows(AuditState &state, const QString &path) {
	for (auto i = 0; i != int(state.files.size()); ++i) {
		EmitRow(state, i, path);
	}
}

[[nodiscard]] QJsonObject NamedList(const QStringList &names) {
	return QJsonObject{
		{ u"count"_q, int(names.size()) },
		{ u"files"_q, Strings(names) },
	};
}

// The totals, from the final rows only; |checks| decides them as well.
void EmitTotals(AuditState &state, const QString &path, bool checks) {
	state.totalsEmitted = true;
	auto ran = 0;
	auto loaded = 0;
	auto complete = 0;
	auto clean = 0;
	auto leaks = 0;
	auto notes = 0;
	auto gaps = 0;
	auto violations = 0;
	auto batches = 0;
	auto clicksAttempted = 0;
	auto clicksObserved = 0;
	auto clicksDecided = 0;
	auto liveAtClick = 0;
	auto layers = 0;
	const auto dead = DeadFiles(state);
	auto notLoaded = QStringList();
	auto incomplete = QStringList();
	auto notRun = QStringList();
	auto leaking = QStringList();
	auto violating = QStringList();
	auto unbatched = QStringList();
	auto pending = QStringList();
	auto opened = QStringList();
	auto scan = DiscriminatingScan(
		Prefixed(u"validated-ops leak scan"_q),
		u"corpus leak"_q,
		u"self-check control leak"_q);
	scan.examined(state.control.batches);
	for (auto i = 0; i != state.control.leaks; ++i) {
		scan.matchedControl((i < int(state.control.leakExamples.size()))
			? QString::fromStdString(state.control.leakExamples[i])
			: QString());
	}
	for (const auto &file : state.files) {
		const auto &verdict = file.verdict;
		const auto &reading = file.reading;
		scan.examined(reading.batches);
		for (auto i = 0; i != reading.leaks; ++i) {
			scan.matchedSubject(file.name
				+ u": "_q
				+ ((i < int(reading.leakExamples.size()))
					? QString::fromStdString(reading.leakExamples[i])
					: u"(example not kept)"_q));
		}
		ran += verdict.ran ? 1 : 0;
		loaded += verdict.loaded ? 1 : 0;
		complete += verdict.complete ? 1 : 0;
		clean += verdict.clean ? 1 : 0;
		leaks += reading.leaks;
		notes += reading.notes;
		gaps += reading.scanGaps;
		violations += reading.violations;
		batches += reading.batches;
		clicksAttempted += file.clickAttempted ? 1 : 0;
		clicksObserved += (file.clickOutcome == u"observed"_q) ? 1 : 0;
		clicksDecided += (file.clickOutcome == u"decided"_q) ? 1 : 0;
		liveAtClick += file.live ? 1 : 0;
		layers += int(file.message->layers.size());
		if (verdict.notLoaded) {
			notLoaded.push_back(file.name);
		}
		if (verdict.ran && !verdict.incomplete.isEmpty()) {
			incomplete.push_back(file.name
				+ u": "_q
				+ verdict.incomplete.join(u","_q)
				+ (verdict.refusal.isEmpty()
					? QString()
					: (u" ("_q + verdict.refusal + u")"_q)));
		}
		if (!verdict.ran) {
			notRun.push_back(file.name + u": "_q + verdict.notRun);
		}
		if (reading.leaks) {
			leaking.push_back(file.name);
		}
		if (reading.violations) {
			violating.push_back(file.name);
		}
		if (verdict.loaded && !reading.batches) {
			unbatched.push_back(file.name);
		}
		if (verdict.ran && !file.barrierClosed) {
			pending.push_back(file.name);
		}
		if (!file.message->layers.isEmpty()) {
			opened.push_back(file.name);
		}
	}
	const auto files = int(state.files.size());
	WriteRow(state, u"ATTACK_TOTALS"_q, {
		{ u"path"_q, path },
		{ u"files"_q, files },
		{ u"ran"_q, ran },
		{ u"loaded"_q, loaded },
		{ u"dead"_q, NamedList(dead) },
		{ u"notLoaded"_q, NamedList(notLoaded) },
		{ u"notRun"_q, NamedList(notRun) },
		{ u"incomplete"_q, NamedList(incomplete) },
		{ u"complete"_q, complete },
		{ u"clean"_q, clean },
		{ u"leaks"_q, leaks },
		{ u"leaking"_q, NamedList(leaking) },
		{ u"notes"_q, notes },
		{ u"gaps"_q, gaps },
		{ u"batches"_q, batches },
		{ u"violations"_q, violations },
		{ u"violating"_q, NamedList(violating) },
		{ u"liveAtClick"_q, liveAtClick },
		{ u"clicksAttempted"_q, clicksAttempted },
		{ u"clicksObserved"_q, clicksObserved },
		{ u"clicksDecided"_q, clicksDecided },
		{ u"clickSource"_q, ClickSource() },
		{ u"layers"_q, layers },
		{ u"unknownRequested"_q, Strings(state.unknown) },
		{ u"selfChecked"_q, state.selfChecked },
		{ u"selfCheckControl"_q, ReadingObject(state.control) },
	});
	if (!checks) {
		return;
	}
	const auto discriminates = state.capture
		? (scan.controlCount() > 0)
		: scan.report();
	Decide(
		state,
		discriminates && !leaks,
		Prefixed(u"no attack file leaks a URL into the validated ops"_q),
		u"leaks=%1 files=%2 batches=%3 control=%4"_q.arg(
			NumberText(leaks),
			leaking.join(u","_q),
			NumberText(batches),
			NumberText(state.control.leaks)));
	Decide(
		state,
		!gaps,
		Prefixed(u"every host message of every file was scanned: no scan "
			u"gaps"_q),
		u"gaps=%1"_q.arg(NumberText(gaps)));
	Decide(
		state,
		unbatched.isEmpty(),
		Prefixed(u"every loaded file's validated batches were observed"_q),
		u"loaded=%1 without batches=[%2]"_q.arg(
			NumberText(loaded),
			unbatched.join(u","_q)));
	Decide(
		state,
		(ran == files) && (files > 0) && state.unknown.isEmpty(),
		Prefixed(u"every requested file ran"_q),
		u"files=%1 ran=%2 notRun=[%3] unknown=[%4]"_q.arg(
			NumberText(files),
			NumberText(ran),
			notRun.join(u"; "_q),
			state.unknown.join(u","_q)));
	Decide(
		state,
		incomplete.isEmpty(),
		Prefixed(u"every file that ran was observed completely: no fixture "
			u"refusal, every watch run, every live card clicked once with "
			u"its trusted click observed (or, hosted without the Debug "
			u"helper's tap, its marked press's click decided by the helper's "
			u"renderer), every barrier closed, no scan gap"_q),
		u"liveAtClick=%1 attempted=%2 observed=%3 decided=%4 "
		u"source=%5 incomplete=[%6]"_q.arg(
			NumberText(liveAtClick),
			NumberText(clicksAttempted),
			NumberText(clicksObserved),
			NumberText(clicksDecided),
			ClickSource(),
			incomplete.join(u"; "_q)));
	Decide(
		state,
		pending.isEmpty() && state.abort.isEmpty(),
		Prefixed(u"every file's factories and wrappers finished before its "
			u"final reading"_q),
		u"pending=[%1] abort=[%2]"_q.arg(
			pending.join(u","_q),
			state.abort));
	Decide(
		state,
		opened.isEmpty(),
		Prefixed(u"no attack file opened a layer"_q),
		u"layers=%1 files=[%2]"_q.arg(
			NumberText(layers),
			opened.join(u","_q)));
	if (state.capture) {
		return;
	}
	Note(Prefixed(u"recorded, not failed, and never counted clean: dead=[%1] "
		u"notLoaded=[%2] violating=[%3]"_q.arg(
			dead.join(u"; "_q),
			notLoaded.join(u","_q),
			violating.join(u","_q))));
	CheckNoBlockedLaunches(Prefixed(u"no widget handed anything to the "
		u"operating system"_q));
}

// ---------------------------------------------------------------------
// The stages.

void ReportCorpus(AuditState &state) {
	const auto network = WidgetFixtureNetwork(*state.fixture);
	auto selected = QJsonArray();
	for (const auto &file : state.files) {
		selected.push_back(file.name);
	}
	Row(u"ATTACK_CORPUS"_q, {
		{ u"dir"_q, state.args.corpus },
		{ u"error"_q, state.corpusError },
		{ u"html"_q, int(state.htmlNames.size()) },
		{ u"nonWidget"_q, int(state.others.size()) },
		{ u"ignoredDotEntries"_q, Strings(state.ignored) },
		{ u"requested"_q, Strings(state.args.only) },
		{ u"unknown"_q, Strings(state.unknown) },
		{ u"unreadable"_q, Strings(state.unreadable) },
		{ u"selected"_q, selected },
		{ u"network"_q, network.isEmpty() ? u"available"_q : network },
	});
	auto unknownDisposition = QStringList();
	for (const auto &other : state.others) {
		Row(u"ATTACK_NON_WIDGET"_q, {
			{ u"name"_q, other.name },
			{ u"file"_q, other.file },
			{ u"bytes"_q, other.bytes },
			{ u"lines"_q, other.lines },
			{ u"referencedBy"_q, Strings(other.referencedBy) },
			{ u"inlinedIn"_q, other.inlinedIn },
			{ u"known"_q, other.known },
			{ u"disposition"_q, other.disposition },
		});
		if (!other.known) {
			unknownDisposition.push_back(other.name);
		}
	}
	Check(
		state.corpusError.isEmpty() && !state.htmlNames.isEmpty(),
		Prefixed(u"the corpus directory lists its HTML files"_q),
		state.corpusError.isEmpty()
			? u"html=%1"_q.arg(NumberText(state.htmlNames.size()))
			: state.corpusError);
	Check(
		state.unknown.isEmpty(),
		Prefixed(u"every requested file name is a corpus HTML file"_q),
		u"requested=%1 unknown=[%2]"_q.arg(
			NumberText(state.args.only.size()),
			state.unknown.join(u","_q)));
	Check(
		state.unreadable.isEmpty(),
		Prefixed(u"every selected HTML file was read"_q),
		u"selected=%1 unreadable=[%2]"_q.arg(
			NumberText(int(state.files.size())),
			state.unreadable.join(u"; "_q)));
	Check(
		unknownDisposition.isEmpty(),
		Prefixed(u"every non-HTML corpus entry has a known disposition"_q),
		u"entries=%1 unclassified=[%2]"_q.arg(
			NumberText(int(state.others.size())),
			unknownDisposition.join(u","_q)));
}

void AppendFileStages(
		not_null<Runner*> runner,
		const std::shared_ptr<AuditState> &state,
		int index) {
	const auto name = state->files[index].name;
	const auto prefix = Prefixed(name + u": "_q);
	const auto skip = [=] {
		return FileSkip(*state, index);
	};
	const auto file = [=]() -> AttackFile& {
		return state->files[index];
	};
	runner->add({
		.name = prefix + u"arm the tap and inject the widget message"_q,
		.skipReason = skip,
		.run = [=] {
			Inject(*state, file());
		},
		.until = [=] {
			return InjectLaidOut(*state, file());
		},
		.then = [=] {
			CheckInjected(*state, file());
		},
		.timeout = kWidgetLayoutBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return file().message->wait.details;
		},
	});
	const auto list = [=] {
		return WidgetFixtureList(*state->fixture);
	};
	const auto rect = [=](ChatListShowView view) {
		return ShowLocal(WidgetCardLocal(view), list());
	};
	const auto details = [](ChatListShowView view) {
		return WidgetCardText(ReadWidgetCard(view));
	};
	file().loadShow = AppendShowInChatList(runner, {
		.name = u"widget attack "_q + name + u" load"_q,
		.resolve = [=] {
			return ChatListShowTarget{ list(), file().message->id };
		},
		.rect = rect,
		.ready = [=](ChatListShowView view) {
			const auto card = ReadWidgetCard(view);
			file().loadCard = card;
			return card.html && card.runtime;
		},
		.readyDetails = details,
		.skipReason = skip,
		.timeout = kShowTimeout,
	});
	runner->add({
		.name = prefix + u"wait for load, death or failure"_q,
		.skipReason = skip,
		.run = [=] {
			StartFixtureWait(file().message->wait);
		},
		.until = [=] {
			return LoadDecided(*state, file());
		},
		.then = [=] {
			RecordLoad(*state, file());
		},
		.timeout = kLoadBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return file().message->wait.details;
		},
	});
	file().clickShow = AppendShowInChatList(runner, {
		.name = u"widget attack "_q + name + u" click"_q,
		.resolve = [=] {
			return ChatListShowTarget{ list(), file().message->id };
		},
		.rect = rect,
		.ready = [=](ChatListShowView view) {
			const auto card = ReadWidgetCard(view);
			if (!WidgetCardReady(card)) {
				return false;
			}
			file().accepted = card;
			return true;
		},
		.readyDetails = details,
		.hover = [](ChatListShowView view) {
			return PressLocal(WidgetCardLocal(view));
		},
		.skipReason = [=] {
			const auto reason = FileSkip(*state, index);
			return !reason.isEmpty()
				? reason
				: file().live
				? QString()
				: (u"not live: "_q + file().clickDetails);
		},
		.timeout = kShowTimeout,
	});
	runner->add({
		.name = prefix + u"click once and watch"_q,
		.skipReason = skip,
		.run = [=] {
			ClickAndWatch(*state, file());
		},
		.until = [=] {
			return (crl::now() - file().watchStarted) >= kWatch;
		},
		.then = [=] {
			RecordWatch(*state, file());
		},
		.timeout = kWatch + kDefaultStageTimeout,
	});
	runner->add({
		.name = prefix + u"read the runtime log"_q,
		.skipReason = skip,
		.run = [=] {
			RequestRuntimeLog(*state->fixture, file().message);
		},
		.until = [=] {
			return RuntimeLogDecided(*state->fixture, *file().message);
		},
		.then = [=] {
			RecordLog(*state, file());
		},
		.timeout = kWidgetLogBound + kDefaultStageTimeout,
	});
	runner->add({
		.name = prefix + u"remove the message and wait for its hosts"_q,
		.skipReason = [=] {
			const auto &message = *file().message;
			return (message.armed
				|| WidgetFixtureItem(*state->fixture, message.id))
				? QString()
				: u"nothing was armed or injected for this file"_q;
		},
		.run = [=] {
			RemoveFixtureMessage(*state->fixture, *file().message);
		},
		.until = [=] {
			return FixtureBarrierDecided(*file().message);
		},
		.then = [=] {
			Finalize(*state, index);
		},
		.timeout = kWidgetBarrierBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			const auto &tap = file().message->tap;
			return tap
				? u"tap{%1}"_q.arg(QString::fromUtf8(QJsonDocument(
					ReadingObject(ReadWidgetOps(*tap))).toJson(
						QJsonDocument::Compact)))
				: u"no tap"_q;
		},
	});
}

} // namespace

void AppendWidgetAttackAudit(
		not_null<Runner*> runner,
		WidgetAttackAuditArgs args) {
	const auto state = std::make_shared<AuditState>();
	state->args = std::move(args);
	state->fixture = MakeWidgetFixture({
		.chat = state->args.chat,
		.prefix = QString::fromLatin1(kPrefix),
		.marker = u"ATTACK"_q,
		.token = state->args.token,
		.ports = state->args.ports,
	});
	ListCorpus(*state);

	// Registered before anything is changed: every path that reaches
	// Runner::finish() stops the self-check's exchange leg, without waiting
	// for it, runs the same idempotent release, then writes the rows no
	// stage wrote, so no selected file goes unreported.
	const auto exchanges = std::make_shared<WidgetOpsExchanges>();
	runner->onFinish([=] {
		exchanges->stop();
		const auto release = ReleaseWidgetFixture(
			*state->fixture,
			u"onFinish"_q);
		EmitPendingRows(*state, u"onFinish"_q);
		if (!state->totalsEmitted) {
			EmitTotals(*state, u"onFinish"_q, false);
		}
		Note(Prefixed(u"onFinish release: "_q) + release);
	});

	runner->add({
		.name = Prefixed(u"tap, scanner and verdict self-check"_q),
		.run = [=] {
			auto check = RunWidgetOpsSelfCheck(WidgetOpsExchangeLeg::Apart);
			for (const auto &row : check.rows) {
				Check(row.passed, row.name, row.details);
			}
			CheckVerdicts(*state);
			state->control = std::move(check.control);
			exchanges->start();
		},
		.until = [=] {
			return exchanges->ready();
		},
		.then = [=] {
			for (const auto &row : exchanges->take()) {
				Check(row.passed, row.name, row.details);
			}
			state->selfChecked = true;
		},
		.timeout = kWidgetOpsExchangesTimeout,
		.timeoutDetails = [] {
			return u"the scanner's exchange leg is still playing on its "
				u"thread"_q;
		},
	});
	runner->add({
		.name = Prefixed(u"report the corpus"_q),
		.run = [=] {
			ReportCorpus(*state);
		},
	});
	AppendWidgetFixtureOpen(runner, state->fixture);
	AppendWidgetFixtureControls(
		runner,
		state->fixture,
		WidgetFixtureControls::Before,
		u"before the first file"_q);
	for (auto i = 0; i != int(state->files.size()); ++i) {
		AppendFileStages(runner, state, i);
	}
	AppendWidgetFixtureControls(
		runner,
		state->fixture,
		WidgetFixtureControls::After,
		u"after the last file"_q);
	runner->add({
		.name = Prefixed(u"totals"_q),
		.run = [=] {
			EmitPendingRows(*state, u"totals"_q);
			EmitTotals(*state, u"totals"_q, true);
		},
	});
	AppendWidgetFixtureClose(runner, state->fixture);
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
