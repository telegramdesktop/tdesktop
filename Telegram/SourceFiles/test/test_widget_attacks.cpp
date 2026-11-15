/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_attacks.h"

#include "test/test_capture.h"
#include "test/test_chat_show.h"
#include "test/test_launch_fuse.h"
#include "test/test_log.h"
#include "test/test_probe.h"
#include "test/test_runner.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_ops.h"
#include "test/test_widgets.h"
#include "tgw/tgw_runtime.h"
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

#include <algorithm>
#include <memory>
#include <string>
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

[[nodiscard]] bool Loaded(const WidgetOpsReading &reading) {
	return (reading.ackQueries > 0) && (reading.ackResults > 0);
}

// Load, death or failure, or the bound: the tap's ack and frozen counts
// and the current runtime's state, read only.
[[nodiscard]] bool LoadDecided(AuditState &state, AttackFile &file) {
	auto &message = *file.message;
	const auto reading = ReadWidgetOps(*message.tap);
	const auto runtime = OwnRuntime(state, file);
	const auto current = runtime ? runtime->state() : RuntimeState::Idle;
	message.wait.details = u"state=%1 ackQueries=%2 ackResults=%3 frozen=%4 "
		u"built=%5"_q.arg(
			runtime ? RuntimeStateName(current) : u"no-runtime"_q,
			NumberText(reading.ackQueries),
			NumberText(reading.ackResults),
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
	MarkWidgetOpsEvents(*file.message->tap);
	file.pressMs = WallNow();
	file.clickAttempted = true;
	Click(list, file.clickShow->hover);
	file.watchStarted = crl::now();
}

void RecordWatch(AuditState &state, AttackFile &file) {
	file.watchMs = crl::now() - file.watchStarted;
	file.watched = ReadWidgetOps(*file.message->tap);
	file.stateAfterWatch = RuntimeStateText(state, file);
	if (file.clickAttempted) {
		file.clickOutcome = (file.watched.trustedClicks > 0)
			? u"observed"_q
			: u"not-observed"_q;
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

// Pure Judge() controls, each failing on a predicate that ignores what it
// varies: the clean row above, the same row after a fixture refusal that
// came once its renderer was built (its load wait, click and watch
// skipped), and the clean row with one renderer violation and no leak.
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
			u"its trusted click observed, every barrier closed, no scan "
			u"gap"_q),
		u"liveAtClick=%1 attempted=%2 observed=%3 incomplete=[%4]"_q.arg(
			NumberText(liveAtClick),
			NumberText(clicksAttempted),
			NumberText(clicksObserved),
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
