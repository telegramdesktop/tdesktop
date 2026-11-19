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
#include "tgw/tgw_remote.h"
#include "tgw/tgw_renderer_input.h"
#include "tgw/tgw_runtime.h"
#include "twidget/json.h"
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

struct Verdict {
	QString notRun;
	// A fixture check's reason for a file that ran but was then refused.
	QString refusal;
	QStringList incomplete;
	bool ran = false;
	bool loaded = false;
	bool died = false;
	bool notLoaded = false;
	bool complete = false;
	bool clean = false;
};

// One selected corpus file. Its fixture record names the message by id
// only, and its tap by the session-qualified key it was armed with, kept
// for every disarm.
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
	QString diedReason;
	QString rowPath;
	crl::time watchStarted = 0;
	crl::time loadWaitMs = 0;
	crl::time watchMs = 0;
	crl::time barrierMs = 0;
	qint64 startMs = 0;
	qint64 endMs = 0;
	qint64 pressMs = 0;
	uint64 playedWork = 0;
	bool live = false;
	bool clickAttempted = false;
	bool logHeader = false;
	bool diedLine = false;
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
		u"rendered=%4 frozen=%5 built=%6"_q.arg(
			runtime ? RuntimeStateName(current) : u"no-runtime"_q,
			NumberText(reading.ackQueries),
			NumberText(reading.ackResults),
			NumberText(reading.renderedHosts),
			NumberText(reading.fatalFrozen),
			NumberText(reading.wrappersBuilt));
	return Loaded(reading)
		|| (reading.fatalFrozen > 0)
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
		: (reading.fatalFrozen > 0)
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
	return (ReadWidgetOps(*file.message->tap).fatalFrozen > 0)
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
// (PlayMarkedClick()). A local renderer's trusted click and the trusted
// click of the release of that press's own down, as its work's forwarding
// handed it to the host before the core was detached; with the Debug
// helper's tap a hosted host's mirrored trusted click and the click
// decision the helper's renderer reported for the up of that press, sent
// with the pointer serial of that dispatch; without it (a Release harness)
// that decision alone. An answered down or up is delivery, never a click,
// and a trusted click the played press did not make never completes it.
enum class ClickReading {
	Renderer,
	HelperMirror,
	Decision,
};

[[nodiscard]] ClickReading ClickReadingHere() {
	return !Tgw::WidgetsHosted()
		? ClickReading::Renderer
		: WidgetHelperMirrors()
		? ClickReading::HelperMirror
		: ClickReading::Decision;
}

[[nodiscard]] QString ClickSource(ClickReading reading) {
	switch (reading) {
	case ClickReading::Renderer: return u"renderer"_q;
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
// the played press's own click (locally the trusted click its release
// answered as it reached the host, hosted with the Debug tap its
// decision), "decided" from that decision alone; the press's own click
// counts only for the press the click played to |played|, the work of the
// core it clicked, never for 0. Anything else, an answered down or up, a
// late step of an earlier press or another generation, a press dispatched
// before the mark, after a mouse-down no core took, or to another core, a
// trusted click another press made, a click after a newer generation was
// selected and a local click that reached no host (its up queued across
// the core's retirement) included, is "not-observed".
[[nodiscard]] QString ClickOutcome(
		const WidgetOpsReading &watched,
		ClickReading reading,
		uint64 played) {
	const auto trusted = (watched.trustedClicks > 0);
	const auto local = (reading == ClickReading::Renderer);
	const auto identified = played
		&& (watched.markedWork == played)
		&& watched.markedSerial
		&& (local || watched.markedPress);
	const auto own = identified && (watched.clicksCompleted > 0);
	switch (reading) {
	case ClickReading::Renderer:
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

// The one field of the runtime log the row needs: the "Died: " line that
// Summarize() puts right after the first sandbox header, before any text a
// widget controls (its errors and console may hold line breaks).
void ParseLog(AttackFile &file) {
	const auto lines = file.message->log.text.split(QChar('\n'));
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
		if (file.diedLine) {
			const auto reason = next.mid(6).trimmed();
			if (reason != u"terminated"_q) {
				file.diedReason = reason;
			}
		}
		file.logHeader = true;
		return;
	}
}

void RecordLog(AuditState &state, AttackFile &file) {
	auto &message = *file.message;
	FinishRuntimeLog(*state.fixture, message);
	ParseLog(file);
	const auto &log = message.log;
	Row(u"ATTACK_LOG"_q, {
		{ u"file"_q, file.name },
		{ u"source"_q, log.source },
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

// Independent statuses from the final reading: a death with a leak is in
// both lists, and only the copied scanner decides the leak count. A
// renderer violation is no leak, but a file with one is never clean.
[[nodiscard]] Verdict Judge(const AuditState &state, const AttackFile &file) {
	const auto &reading = file.reading;
	auto result = Verdict();
	result.ran = (reading.wrappersBuilt > 0);
	result.loaded = Loaded(reading);
	result.died = (reading.fatalFrozen > 0) || !file.diedReason.isEmpty();
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
		u"complete=%1"_q.arg(FlagText(verdict.complete)),
		u"clean=%1"_q.arg(FlagText(verdict.clean)),
		u"notRun=[%1]"_q.arg(verdict.notRun),
		u"refusal=[%1]"_q.arg(verdict.refusal),
		u"incomplete=[%1]"_q.arg(verdict.incomplete.join(u","_q)),
	};
	return fields.join(QChar(' '));
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
// one). A local renderer's trusted click alone is none either, a reading
// with no played work none whatever it counted: only the trusted click the
// played press's own release answered, beside the generation's count,
// completes it, and only for the core the click hit.
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
	auto localTrusted = mirrored;
	localTrusted.markedPress = 0;
	auto localOwn = both;
	localOwn.markedPress = 0;
	auto localUncounted = localOwn;
	localUncounted.trustedClicks = 0;
	auto localUnbound = unbound;
	localUnbound.trustedClicks = 1;
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
	const auto local = [&](const WidgetOpsReading &reading, uint64 played) {
		return ClickOutcome(reading, Reading::Renderer, played);
	};
	const auto localUnplayed = local(localTrusted, 0);
	const auto localTrustedOnly = local(localTrusted, kPlayed);
	const auto localOwnUnplayed = local(localOwn, 0);
	const auto localOwnOther = local(localOwn, kPlayed + 1);
	const auto localOwnUncounted = local(localUncounted, kPlayed);
	const auto localUnboundClick = local(localUnbound, kPlayed);
	const auto localClick = local(localOwn, kPlayed);
	Check(
		(releaseDown == u"not-observed"_q)
			&& (releaseReplaced == u"not-observed"_q)
			&& (releaseNone == u"not-observed"_q)
			&& (releaseClick == u"decided"_q)
			&& (releaseUnbound == u"not-observed"_q)
			&& (releaseOther == u"not-observed"_q)
			&& (debugDecided == u"not-observed"_q)
			&& (debugMirrored == u"not-observed"_q)
			&& (debugBoth == u"observed"_q)
			&& (localUnplayed == u"not-observed"_q)
			&& (localTrustedOnly == u"not-observed"_q)
			&& (localOwnUnplayed == u"not-observed"_q)
			&& (localOwnOther == u"not-observed"_q)
			&& (localOwnUncounted == u"not-observed"_q)
			&& (localUnboundClick == u"not-observed"_q)
			&& (localClick == u"observed"_q),
		prefix + u"a hosted click is the click decision of the press played "
			u"to the clicked core (with the Debug helper's tap and the "
			u"mirror's trusted click), never an answered down or up or an "
			u"unbound or other core's press; a local one the trusted click "
			u"its work matched to the played press's own release, with the "
			u"generation's trusted click, never a trusted click alone, an "
			u"unbound press, another core's or one with no played work"_q,
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
				debugBoth)
			+ u" localUnplayed=%1 localTrustedOnly=%2 localOwnUnplayed=%3 "
			u"localOwnOther=%4 localOwnUncounted=%5 localUnbound=%6 "
			u"localClick=%7"_q.arg(
				localUnplayed,
				localTrustedOnly,
				localOwnUnplayed,
				localOwnOther,
				localOwnUncounted,
				localUnboundClick,
				localClick));
}

// Work ids no RendererCore is given and the ops self-check does not use,
// the older and newer generations' of the hosted and of the local taps,
// then each tap's three later generations' (a click completed before its
// core retired, a press whose up was queued across that retirement, a
// press whose up came between its core's detach and its host's
// retirement), and the session of the messages the verdict check drives.
constexpr auto kVerdictOlderWork = std::numeric_limits<uint64>::max() - 5;
constexpr auto kVerdictNewerWork = kVerdictOlderWork + 1;
constexpr auto kVerdictLocalOlderWork = kVerdictOlderWork - 2;
constexpr auto kVerdictLocalNewerWork = kVerdictOlderWork - 1;
constexpr auto kVerdictKeptWork = kVerdictOlderWork - 12;
constexpr auto kVerdictRetiredWork = kVerdictOlderWork - 13;
constexpr auto kVerdictLocalKeptWork = kVerdictOlderWork - 14;
constexpr auto kVerdictLocalRetiredWork = kVerdictOlderWork - 15;
constexpr auto kVerdictGapWork = kVerdictOlderWork - 16;
constexpr auto kVerdictLocalGapWork = kVerdictOlderWork - 17;
constexpr auto kVerdictTapSession = uint64(3);

// The trusted click a renderer sends its host.
constexpr auto kVerdictClick = R"json({"type":"event",)json"
	R"json("ev":{"type":"click","targetId":2,"detail":1},)json"
	R"json("trusted":true})json";

// A local product renderer standing in for the verdict check's: it sends
// its host only what the check gives it.
class VerdictRenderer final : public twidget::Renderer {
public:
	void load() override;
	void send(const std::string &json) override;
	void setTheme(const std::string &theme) override;
	void setWidth(double width) override;
	void destroy() override;

};

// How the verdict check plays a press of a work through one placement's
// product seams: its down taken (hosted its Sent and Down, locally
// WidgetLocalPressDown()), then its up, with or without the click its
// renderer decided (hosted its Up and Click, then its host's mirrored
// trusted click, which only a Debug tap reads; locally
// WidgetLocalPressReleased() answered with the trusted click, which the
// work's forwarding hands its host through the wrapper, then
// WidgetLocalPressForwarded()), a third generation selected for the
// message, a later generation selected and built for a work, the detach
// of a work's core (RendererCore::detach(): WidgetCoreDetached(), then
// its link closed), after which a reply its route had already taken still
// reports the up and the click its renderer decided, its host, still
// live, mirroring that click (hosted) and its forwarding reaching no
// wrapper (locally), and the retirement of that work's host
// (WidgetHostRetired(), hosted alone), after which such a reply reports
// with no mirrored trusted click, while the host keeps the wrapper.
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

// The readings the verdict check's interactions leave on one placement's
// tap, and whether its hosts or wrappers were built.
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

// A local generation's wrapper, built by the wrapping factory, which is
// released then as a host releases it, and the renderer it wraps.
struct VerdictLocal {
	std::unique_ptr<twidget::Renderer> wrapper;
	VerdictRenderer *product = nullptr;
};

void VerdictRenderer::load() {
}

void VerdictRenderer::send(const std::string &) {
}

void VerdictRenderer::setTheme(const std::string &) {
}

void VerdictRenderer::setWidth(double) {
}

void VerdictRenderer::destroy() {
}

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

// The next generation of |id| for the local |work|; no product when the
// tap did not wrap it.
[[nodiscard]] VerdictLocal VerdictLocalGeneration(
		GlobalMsgId id,
		uint64 work) {
	auto result = VerdictLocal();
	auto factory = TapWidgetRenderer(id, work, [&](
			std::shared_ptr<twidget::SandboxResources>,
			const std::string &,
			double) {
		auto renderer = std::make_unique<VerdictRenderer>();
		result.product = renderer.get();
		return std::unique_ptr<twidget::Renderer>(std::move(renderer));
	});
	result.wrapper = factory(nullptr, "light", 320.);
	if (result.wrapper.get() == result.product) {
		result.product = nullptr;
	}
	return result;
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

// The cases on local generations through TapWidgetRenderer(), each wrapper
// built and its factory released first and kept to the end, as a host
// sample keeps a retired host, and the product's WidgetPressDispatched(),
// WidgetLocalPressDown(), WidgetLocalPressReleased(),
// WidgetLocalPressForwarded() and WidgetCoreDetached().
[[nodiscard]] VerdictRun PlayLocalVerdicts() {
	const auto id = GlobalMsgId{
		FullMsgId(PeerId(), MsgId(-2)),
		kVerdictTapSession,
	};
	const auto tap = ArmWidgetOpsTap(id);
	auto generations = std::map<uint64, VerdictLocal>();
	const auto add = [&](uint64 work) {
		const auto &added = generations.emplace(
			work,
			VerdictLocalGeneration(id, work)).first->second;
		return (added.product != nullptr);
	};
	const auto productOf = [&](uint64 work) {
		const auto i = generations.find(work);
		return (i != end(generations)) ? i->second.product : nullptr;
	};
	auto click = std::vector<twidget::Json>();
	if (auto parsed = twidget::Json::parse(kVerdictClick)) {
		click.push_back(std::move(*parsed));
	}
	auto result = VerdictRun();
	result.built = add(kVerdictLocalOlderWork)
		&& add(kVerdictLocalNewerWork)
		&& !click.empty();
	auto closed = QSet<uint64>();
	auto third = twidget::RendererFactory();
	PlayVerdicts(result, *tap, {
		.down = [](uint64 work, uint64, uint64 serial) {
			WidgetLocalPressDown(work, serial);
		},
		.up = [&](uint64 work, uint64, bool clicked) {
			WidgetLocalPressReleased(
				work,
				clicked ? click : std::vector<twidget::Json>());
			const auto product = productOf(work);
			if (clicked
				&& !closed.contains(work)
				&& product
				&& product->onMessage) {
				product->onMessage(kVerdictClick);
			}
			WidgetLocalPressForwarded(work);
		},
		.supersede = [&] {
			third = TapWidgetRenderer(id, twidget::RendererFactory());
		},
		.next = add,
		.detach = [&](uint64 work) {
			closed.insert(work);
			WidgetCoreDetached(work);
		},
		.retire = [](uint64) {
		},
		.older = kVerdictLocalOlderWork,
		.newer = kVerdictLocalNewerWork,
		.kept = kVerdictLocalKeptWork,
		.retired = kVerdictLocalRetiredWork,
		.gap = kVerdictLocalGapWork,
	});
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
	const auto hosted = (reader != ClickReading::Renderer);
	const auto gapTrusted = hosted ? trusted : 0;
	const auto bound = [&](const WidgetOpsReading &reading) {
		return (reading.markedWork == run.gapWork)
			&& (reading.markedSerial == 11)
			&& (!hosted || (reading.markedPress == 11))
			&& (reading.downsConfirmed == 1)
			&& !reading.upsConfirmed
			&& !reading.clicksCompleted
			&& reading.markDetached
			&& (reading.pressStepsElsewhere == 2)
			&& (reading.trustedClicks == gapTrusted);
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
// build; and the local consumer (the trusted click of the played press's
// own release as it reached the host, with the generation's) on local
// ones.
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
	CheckVerdictReader(
		state,
		prefix,
		PlayLocalVerdicts(),
		ClickReading::Renderer,
		kVerdictLocalNewerWork,
		u"observed"_q,
		1);
}

// Pure Judge() controls, each failing on a predicate that ignores what it
// varies: the clean row above, the same row after a fixture refusal that
// came once its renderer was built (its load wait, click and watch
// skipped), the clean row with one renderer violation and no leak, and the
// clean row whose click decision a Release harness's helper reported; then
// the click outcomes those rows stand for, and each reader's consumer on
// real taps' marked interactions, hosted and local.
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

// The file's one ATTACK row. Its reading is final only when |barrierClosed|.
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
	Row(u"ATTACK"_q, {
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
		{ u"deathReason"_q, !file.diedReason.isEmpty()
			? file.diedReason
			: (file.reading.fatalFrozen > 0)
			? u"frozen, reason unavailable"_q
			: QString() },
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
		} },
		{ u"layers"_q, Strings(message.layers) },
		{ u"barrierClosed"_q, file.barrierClosed },
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
			u"after its removal (factories %3 of %4 released, wrappers %5 "
			u"of %6 destroyed), so no later file was started"_q.arg(
				file.name,
				NumberText(file.barrierMs),
				NumberText(file.reading.factoriesReleased),
				NumberText(file.reading.factoriesSelected),
				NumberText(file.reading.wrappersDestroyed),
				NumberText(file.reading.wrappersBuilt));
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
	auto dead = QStringList();
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
		if (verdict.died) {
			dead.push_back(file.name + u": "_q + (file.diedReason.isEmpty()
				? u"frozen"_q
				: file.diedReason));
		}
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
	Row(u"ATTACK_TOTALS"_q, {
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
	const auto discriminates = scan.report();
	Check(
		discriminates && !leaks,
		Prefixed(u"no attack file leaks a URL into the validated ops"_q),
		u"leaks=%1 files=%2 batches=%3 control=%4"_q.arg(
			NumberText(leaks),
			leaking.join(u","_q),
			NumberText(batches),
			NumberText(state.control.leaks)));
	Check(
		!gaps,
		Prefixed(u"every host message of every file was scanned: no scan "
			u"gaps"_q),
		u"gaps=%1"_q.arg(NumberText(gaps)));
	Check(
		unbatched.isEmpty(),
		Prefixed(u"every loaded file's validated batches were observed"_q),
		u"loaded=%1 without batches=[%2]"_q.arg(
			NumberText(loaded),
			unbatched.join(u","_q)));
	Check(
		(ran == files) && (files > 0) && state.unknown.isEmpty(),
		Prefixed(u"every requested file ran"_q),
		u"files=%1 ran=%2 notRun=[%3] unknown=[%4]"_q.arg(
			NumberText(files),
			NumberText(ran),
			notRun.join(u"; "_q),
			state.unknown.join(u","_q)));
	Check(
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
	Check(
		pending.isEmpty() && state.abort.isEmpty(),
		Prefixed(u"every file's factories and wrappers finished before its "
			u"final reading"_q),
		u"pending=[%1] abort=[%2]"_q.arg(
			pending.join(u","_q),
			state.abort));
	Check(
		opened.isEmpty(),
		Prefixed(u"no attack file opened a layer"_q),
		u"layers=%1 files=[%2]"_q.arg(
			NumberText(layers),
			opened.join(u","_q)));
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
	// Runner::finish() runs the same idempotent release, then writes the
	// rows no stage wrote, so no selected file goes unreported.
	runner->onFinish([=] {
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
			auto check = RunWidgetOpsSelfCheck();
			for (const auto &row : check.rows) {
				Check(row.passed, row.name, row.details);
			}
			CheckVerdicts(*state);
			state->control = std::move(check.control);
			state->selfChecked = true;
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
