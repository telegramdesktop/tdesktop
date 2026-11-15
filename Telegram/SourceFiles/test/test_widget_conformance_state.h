/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "test/test_widget_bot.h"
#include "test/test_widget_conformance.h"
#include "test/test_widget_conformance_bridge.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_host.h"
#include "test/test_widget_ops.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <rpl/lifetime.h>

#include <memory>
#include <optional>
#include <vector>

class QImage;
class QWidget;

namespace Ui {
class InputField;
} // namespace Ui

// The widget conformance runner's state, shared by its translation units:
// test_widget_conformance.cpp (selection, the per-run pipeline, snapshots
// and the run file), test_widget_conformance_actions.cpp (the scripted
// actions), test_widget_conformance_context.cpp (message contexts and the
// played bot's records), test_widget_conformance_hosts.cpp (the content
// safe area readings and their controls) and
// test_widget_conformance_pictures.cpp (the requested pictures' bands and
// the floating-date wait before every capture). Internal to them;
// reference: docs/helpers/test_widget_conformance.md.

namespace Test {

struct ChatListShow;
struct PaletteRestore;
struct PaletteSwitch;

} // namespace Test

namespace Test::Conformance {

// The show helper's own deadline for each show of the runner.
inline constexpr auto kShowTimeout = crl::time(10000);

// Image pixels per CSS px of every stage snapshot, crop and picture band,
// CLIENTS.md's 2x: it must come from the device pixel ratio and the
// interface scale themselves, as no frame is ever upscaled.
inline constexpr auto kCropScale = 2;

// One stage of a manifest entry: its rect in whole CSS px from the
// widget's top-left, and the themes it is cropped in.
struct ManifestStage {
	QString name;
	QStringList themes;
	QRect rect;
};

// What a manifest `bot` entry asks for besides its answer (`edit`,
// `editShared`, `repeat`): named in the run file, never played.
struct UnplayedBotOperation {
	QString data;
	QString operation;
};

// A manifest entry's message context, as ParseManifestMessage() reads it:
// |kind| is its first `message` spec's kind as requested, "private" with
// |implicit| for an entry without `message`, empty until it was read or
// when the kind is malformed; |fields| are what its run's message is
// injected with: the flags of the kind it runs as (a preview as private)
// and that spec's `chat_data` / `public_data` as given, an absent one
// unset, never a widget id. |specs| counts the entry's specs; only the
// first is run.
struct ManifestMessage {
	QString kind;
	WidgetMessageFields fields;
	int specs = 0;
	bool implicit = false;
};

// One manifest entry, as read at setup: only the fields the runner uses,
// its actions and host checks kept as the manifest's own JSON. |message|
// is the context its run's message gets. |bot| is its `bot` table as the
// played bot answers it, empty without one, and |botUnplayed| what the
// table asks for that is not played. |error| says why a selected entry
// cannot run, |htmlError| why its HTML cannot. |stops| is its `stops`
// prefix, empty without one: its run waits for the death, never for done.
// |safeArea| is whether it has a `safeArea`: its run's message gets no
// caption, so the desktop draws its time over the widget's corner.
struct ManifestTest {
	QString id;
	QString set;
	QString group;
	QString file;
	QStringList themes;
	QJsonArray actions;
	QJsonArray host;
	std::vector<ManifestStage> stages;
	ManifestMessage message;
	WidgetBotTable bot;
	std::vector<UnplayedBotOperation> botUnplayed;
	QByteArray html;
	QString error;
	QString htmlError;
	QString stops;
	bool selected = false;
	bool read = false;
	bool safeArea = false;
};

// The newest published value of a run's widget, taken once per tap
// generation and serial. |value|, |checks|, |targets| and |status| are set
// only when |present|: the newest value parsed as a JSON object published
// for this run's id, with a checks array copied verbatim. |error| names
// why the newest value is not used; no value yet is no error.
struct ConformanceResult {
	QJsonObject value;
	QJsonArray checks;
	QJsonObject targets;
	QString status;
	QString error;
	int64 htmlId = 0;
	int generation = 0;
	int serial = 0;
	int updates = 0;
	bool read = false;
	bool present = false;
};

// The two result waits of a run: from its load show, and after its
// actions.
enum class ResultWait {
	Initial,
	AfterActions,
};

// One expected stage crop of an auto run: |crop| is the PNG's path
// relative to the run file once it is saved; until then |refusal| names
// why there is none.
struct StageReading {
	QString name;
	QRect rect;
	QString crop;
	QString refusal = u"not-captured"_q;
	QJsonObject capture;
};

// The one snapshot of an auto run's stages: the union of their rects in
// CSS px from the widget's top-left (|css|), the rect the snapshot show
// showed in the list's logical px, the accepted image's size in pixels
// and its |file| relative to the run file. |refusal| names why there is
// none; |ratio| and |scale| are the device pixel ratio and the interface
// scale it was taken at.
struct SnapshotReading {
	QRect css;
	QRect list;
	QSize image;
	QString file;
	QString refusal = u"not-captured"_q;
	QString detail;
	int ratio = 0;
	int scale = 0;
};

// The wait between a list-paint capture's show and its capture for the
// floating date HistoryInner paints over its viewport's top after a
// scroll, filled by AppendDateWait(): |hidden| once the list's own scroll
// read the show's accepted |shownTop| unchanged long enough for the date
// to hide. |top| is the last reading, |changes| the readings that differed
// from the one before, |stillMs| how long the last one held and |waitedMs|
// the whole wait; |detail| says why a |finished| wait was not accepted.
struct DateWaitReading {
	QString detail;
	crl::time started = 0;
	crl::time stillSince = 0;
	crl::time stillMs = 0;
	crl::time waitedMs = 0;
	int shownTop = 0;
	int top = 0;
	int changes = 0;
	bool finished = false;
	bool hidden = false;
};

// One band of a run's picture: the card's rows |css|, in CSS px from the
// widget's top-left at the card's whole width, the rect its show showed in
// the list's logical px, the accepted image's size in pixels and its
// |file| relative to the run file. |card| is what its show accepted (the
// card's rect in its view, its core), |date| the floating-date wait after
// that show and |generation| the tap's host generation at its capture.
// |refusal| names why there is no file; a band below the card is not
// needed and keeps none.
struct PictureBand {
	QRect css;
	QRect list;
	QSize image;
	QString file;
	QString refusal = u"not-captured"_q;
	QString detail;
	WidgetCardReading card;
	std::shared_ptr<const ChatListShow> show;
	std::shared_ptr<const DateWaitReading> date;
	int generation = 0;
};

// The picture of a run whose entry the caller asked one of (|requested|):
// its possible bands, and from its first capture the card's size in CSS
// px, its core and host generation, which every later band must match,
// and the stage rects the widget published then (null when none). |ratio|
// and |scale| are the device pixel ratio and the interface scale.
struct PictureReading {
	std::vector<PictureBand> bands;
	QJsonValue stages;
	QSize card;
	quintptr core = 0;
	int generation = 0;
	int ratio = 0;
	int scale = 0;
	bool requested = false;
};

// One manifest action of an auto run. It stays not performed, with a
// named refusal, until real input delivers it. Only an action this build
// cannot perform at all gets a |skipReason|, and only that puts it into
// skippedActions. |reading| is what its stages measured.
struct ActionReading {
	QString kind;
	QString refusal = u"not-performed"_q;
	QString skipReason;
	QJsonObject reading;
	int index = 0;
	bool performed = false;
};

using BridgeCall = ConformanceBridgeCall;
using RunnerRefusal = ConformanceRefusal;

// The chat's compose field, read only: the legacy HistoryWidget that owns
// the fixture's list, its field's editor, what makes the field other than
// an ordinary, empty one an Enter could send nothing from (|unusual|), and
// the window's focus child (|focus|, described in |holder|), whose being
// the editor is |focused|. The window is not active: QApplication's focus
// (|application|) and |active| are recorded, never required.
struct ComposeReading {
	QPointer<QWidget> editor;
	QPointer<QWidget> focus;
	QString owner;
	QString holder;
	QString application;
	QStringList unusual;
	int length = 0;
	bool found = false;
	bool active = false;
	bool focused = false;
};

// The run's card and host at the end of its result wait: the list and
// the list's own view (as the show helper prints them; |loadView| is the
// load show's), the interactive core's address, never dereferenced, the
// current core's width in CSS px, and the scheme, the scheme changes and
// the raw theme message its renderer was given, of host |generation|.
struct ViewReading {
	QString list;
	QString loadView;
	QString view;
	QString core;
	QString state;
	QString scheme;
	QString themeMessage;
	QRect card;
	std::optional<double> width;
	int generation = 0;
	int schemeChanges = 0;
	bool read = false;
};

// The action stages' own record of a run, defined in
// test_widget_conformance_actions.cpp.
struct ConformanceActions;

// One selected run: a manifest entry in one theme at kWidth. Its fixture
// record names the message by id and its tap by the key it was armed
// with. Every observation stays unset until it is measured, and only a
// run marked |completed| is written into the run file's runs. |retained|
// is the last value a host published for it, kept when a newer host of
// the run (a restart after a death) cleared |result|. |palette| indexes
// the palette phase it runs in. |fillers| are the local text messages a
// run with a visible action gets above its widget, registered at setup.
// |bot| is the played bot's record of the message, armed under the same
// key in the injection's own call, null when nothing was injected, and
// held until the run file is written. |context| is WidgetMedia::context()
// of the injected item, the flags and chat type the client derived, read
// once the injection was checked; unset otherwise. |picture| is requested
// only for a visual run whose entry the caller asked a picture of.
// |safeAreas| is the content safe area each armed construction of the
// run's host got (WidgetHostApplied::contentSafeArea), in order, copied
// from the message's host watch before the removal; |safeAreaRead| once it
// was, so a run with no watch reading has none.
struct ConformanceRun {
	QString id;
	QString set;
	QString theme;
	QString key;
	QString notRun;
	QString progress = u"not started"_q;
	std::shared_ptr<WidgetFixtureMessage> message;
	std::vector<std::shared_ptr<WidgetFixtureMessage>> fillers;
	std::shared_ptr<WidgetBotRecord> bot;
	std::optional<Tgw::MessageContext> context;
	std::shared_ptr<ConformanceActions> live;
	ConformanceResult result;
	std::vector<StageReading> stages;
	std::vector<ActionReading> actions;
	std::optional<std::vector<BridgeCall>> bridge;
	std::vector<RunnerRefusal> refusals;
	std::optional<QStringList> errors;
	QString dead;
	QString focusHolder;
	WidgetOpsReading reading;
	std::optional<double> height;
	std::optional<bool> chatFocused;
	int test = -1;
	int width = 0;
	bool loaded = false;
	bool died = false;
	bool readingFinal = false;
	bool completed = false;

	std::shared_ptr<const ChatListShow> loadShow;
	std::shared_ptr<const ChatListShow> snapshotShow;
	std::shared_ptr<const DateWaitReading> snapshotDate;
	WidgetCardReading snapshotCard;
	SnapshotReading snapshot;
	PictureReading picture;
	ComposeReading compose;
	ViewReading view;
	ConformanceResult retained;
	ConformanceSummary summary;
	QString resultOutcome;
	QString doneOutcome;
	QString runtimeState;
	QString deathLine;
	std::optional<double> contentHeight;
	std::vector<twidget::WidgetContentSafeArea> safeAreas;
	crl::time resultMs = 0;
	crl::time doneMs = 0;
	crl::time barrierMs = 0;
	int palette = -1;
	bool measured = false;
	bool summaryRead = false;
	bool itemGone = false;
	bool safeAreaRead = false;
};

// One palette phase: the restore of the chain's switch before it, when
// there is one, then the switch to its theme's palette. Restoring first
// and then switching makes exactly one real palette change between two
// phases, whichever palette the launch began in.
struct PalettePhase {
	QString theme;
	std::shared_ptr<const PaletteSwitch> previous;
	std::shared_ptr<const PaletteRestore> restore;
	std::shared_ptr<const PaletteSwitch> switched;
};

// An outgoing message the session added that the fixture did not
// register, and the run that was going on.
struct OutgoingItem {
	QString run;
	QString id;
	qint64 wallMs = 0;
	bool inChat = false;
	bool local = false;
	bool regular = false;
	bool service = false;
};

// A run the selection left out. |elsewhere|: only because this launch's
// chat cannot host its message context, so a launch on a fitting chat runs
// it; otherwise the manifest's own omission, the same in every launch.
struct OmittedRun {
	QString id;
	QString theme;
	QString reason;
	bool elsewhere = false;
};

struct ConformanceEnvironment {
	QString app;
	QString os;
	QString cpu;
	QString qt;
	QString locale;
	int ratio = 0;
	int scale = 0;
	bool captured = false;
};

// The one State of the runner. The window, the chat, the list, the
// controls and the release are its shared fixture's
// (test_widget_fixture.h); every run's message record is registered there.
struct ConformanceState {
	WidgetConformanceArgs args;
	std::shared_ptr<WidgetFixture> fixture;
	std::vector<ManifestTest> tests;
	std::vector<ConformanceRun> runs;
	std::vector<OmittedRun> omitted;
	QStringList themes;
	QStringList unknownThemes;
	QStringList unknownSets;
	QStringList unknownIds;
	QStringList unknownPictures;
	QStringList invalid;
	QByteArray probeFixture;
	QString packageError;
	QString date;
	ConformanceEnvironment environment;
	WidgetOpsReading control;
	QStringList selfCheckFailed;
	int selfCheckRows = 0;
	bool selfChecked = false;

	QString outputDir;
	QString runFile;
	QString outputError;
	QString saveError;
	QString writtenBy;
	qint64 writtenBytes = 0;
	int writes = 0;
	bool outputReady = false;
	bool totalsDone = false;

	// The palette chain, in the order its stages are appended: each phase
	// and each theme action restores |paletteCurrent|, the chain's last
	// switch, before it switches, so one obligation is outstanding at a
	// time; the final restore restores the last one.
	std::vector<PalettePhase> palette;
	std::shared_ptr<const PaletteSwitch> paletteCurrent;
	std::shared_ptr<const PaletteRestore> paletteRestore;

	// The session's new-item watcher, installed once the fixture has its
	// session and destroyed before the fixture's teardown.
	rpl::lifetime watcher;
	std::vector<OutgoingItem> outgoing;
	QString watcherError;
	QString currentRun;
	bool watching = false;

	// Why no later run may start, once one run's hosts did not finish.
	QString abort;
};

// The helpers the translation units read the state with, defined in
// test_widget_conformance.cpp.

// kPrefix, then |text|: every stage name, Check and Note of the runner.
[[nodiscard]] QString Prefixed(const QString &text);
[[nodiscard]] QString NumberText(qint64 value);
[[nodiscard]] QString CompactJson(const QJsonObject &object);
// One machine-readable row: "NOTE: <marker>: <compact JSON>".
void Row(const QString &marker, const QJsonObject &fields);
[[nodiscard]] QJsonArray RectArray(QRect rect);
// A finished show's reading, or {"finished": false}.
[[nodiscard]] QJsonObject ShowObject(const ChatListShow *shown);

// Why no run may start now, or empty.
[[nodiscard]] QString GlobalSkip(const ConformanceState &state);
// Why |run| does not run (globally, by its palette phase or its own), or
// empty.
[[nodiscard]] QString RunSkip(
	const ConformanceState &state,
	const ConformanceRun &run);
// Done. Before the actions awaiting as well, for an auto test with actions
// and for every visual test: the SDK's mini harness waits for a person's
// input. After the actions only done, as CLIENTS.md waits for. None for a
// stops entry: CLIENTS.md waits for its death instead, which only the
// result wait's other conditions or its bound end.
[[nodiscard]] bool WantedStatus(
	const ConformanceState &state,
	const ConformanceRun &run,
	ResultWait wait);
// Takes the tap's newest result into |run.result| when it is new.
void RefreshResult(ConformanceRun &run);
void AddRefusal(
	ConformanceRun &run,
	const QString &name,
	const QString &detail);

// The fixture chat's compose field and the window's focus child, read
// only; the field itself, or null.
[[nodiscard]] ComposeReading ReadCompose(const ConformanceState &state);
[[nodiscard]] QJsonObject ComposeObject(const ComposeReading &compose);
[[nodiscard]] Ui::InputField *ComposeField(const ConformanceState &state);

// The load show's rectangle: the top band of |card|, in its view.
[[nodiscard]] QRect LoadRect(QRect card);

// A Check of |restore| when |switched| succeeded, a Note otherwise.
void JudgeRestore(
	const std::shared_ptr<const PaletteSwitch> &switched,
	const std::shared_ptr<const PaletteRestore> &restore,
	const QString &what);

// The stage snapshot's capture helpers, shared with the picture bands.

// Whether the device pixel ratio times the interface scale is
// kCropScale * 100, so a capture is 2x without upscaling.
[[nodiscard]] bool CropScaleHolds();
// |css| in logical px, exact while CropScaleHolds().
[[nodiscard]] QRect LogicalFromCss(QRect css);
// Why no capture of |run| can be taken at all, before any show: not-2x
// or not-live; an empty name when one can.
[[nodiscard]] RunnerRefusal SnapshotBlock(const ConformanceRun &run);
// "<id>.<theme>", every character of the id but ASCII letters, digits,
// '-' and '_' replaced, for the run's file names.
[[nodiscard]] QString FileBase(const ConformanceRun &run);
// Saves |image| as a PNG at |relative| under the output directory and
// reads its size back from the file; empty, or what failed.
[[nodiscard]] QString SavePng(
	const ConformanceState &state,
	const QImage &image,
	const QString &relative);

} // namespace Test::Conformance
