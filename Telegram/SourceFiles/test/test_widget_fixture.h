/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "crl/crl_time.h"
#include "data/data_msg_id.h"
#include "data/data_peer_id.h"
#include "test/test_widget_host.h"
#include "test/test_widget_ops.h"
#include "tgw/tgw_media.h"
#include "tgw/tgw_runtime.h"

#include <QtCore/QByteArray>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>
#include <optional>
#include <vector>

class History;
class HistoryItem;
class QScreen;
class QWidget;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class ElasticScroll;
} // namespace Ui

// The live-chat fixture the widget attack audit (test_widget_attacks.cpp)
// and the widget conformance runner share: a Test chat shown in its legacy
// HistoryInner of the primary window, shown normally and kept exposed
// without activation, local outgoing widget and text messages with tapped
// widget ops, card readings, the runtime log, loopback network controls and
// one idempotent release. It also holds the window's one-column placement
// the conformance runner and the widget stress kit
// (test_widget_stress_live.cpp) share. Internal dependency of those packs,
// of the chat-list place self-test (test_chat_show_place.cpp) and of one
// task's scoped evidence overlay, not a general overlay fixture; contract
// and callers: docs/helpers/test_widget_fixture.md.

namespace Test {

class Runner;
struct ChatListShow;

// |chat| is a chat of the Test session the primary window shows, loaded,
// with its folder known and no unread messages. Every stage name, Check
// and Note of the fixture starts with |prefix|; its two rows are
// "<marker>_ENVIRONMENT" and "<marker>_NET_CONTROL". |token| and |ports|
// are the network controls: one TCP connection and one UDP datagram to
// 127.0.0.1 on each port, carrying the token; an empty token, no port or a
// port out of range makes the control stages N/A, never a pass.
struct WidgetFixtureArgs {
	PeerId chat = 0;
	QString prefix;
	QString marker;
	QString token;
	std::vector<int> ports;
};

// Opaque; every function below reads or writes it on the main thread.
struct WidgetFixture;

enum class WidgetFixtureControls {
	Before,
	After,
};

// One bounded wait: |details| is the last named reading, |started| and
// |stableSince| crl::now() values.
struct WidgetFixtureWait {
	QString sample;
	QString details;
	crl::time started = 0;
	crl::time stableSince = 0;
};

// Where WidgetRuntimeLog::summary came from. Only Fresh and Final are the
// summary of the host the request was made for:
// - Absent: no summary, or no runtime to read it from;
// - Fresh: read in the requestLog() callback, right after the runtime
//   stored that sample;
// - Final: requestLog() kept no callback because the last host's final
//   sample was already applied;
// - Stale: read when no fresh sample answered (at the bound, or no
//   callback kept while the held sample was not final).
enum class WidgetLogSummary {
	Absent,
	Fresh,
	Final,
	Stale,
};

// The runtime log of one message. |text| is Runtime::logText() as the
// callback or the fallback gave it, |source| how ("requestLog", "logText",
// "logText-at-bound" or "unavailable"), |summary| the raw Summarize() text
// Tgw::Runtime::_summary held, with |summaryState|. |pending| means the
// runtime kept the callback, |received| that a full log was taken.
struct WidgetRuntimeLog {
	QString text;
	QString source;
	QString summary;
	WidgetLogSummary summaryState = WidgetLogSummary::Absent;
	int chars = 0;
	bool pending = false;
	bool received = false;
};

// The layer-233 fields of an injected widget message's media besides its
// anchors and payload, sent as given; by default no flag, state or id.
// |viaBot|, when nonzero, makes the local message come via that bot
// (HistoryItemCommonFields::viaBotId); the caller resolves and loads the
// bot first, the fixture does not.
struct WidgetMessageFields {
	Tgw::WidgetFlags flags;
	Tgw::WidgetState state;
	std::optional<uint64> widgetId;
	UserId viaBot = 0;
};

// One message record, registered by AddWidgetFixtureMessage() before
// anything is injected for it. |id|, and for a widget |key| and |tap|, are
// set before History::addNewLocalMessage() can notify observers; the
// session-qualified |key| is kept for every disarm. |fields| are the
// injected media fields, |shape| describes the injected message, |layers|
// when a layer was hidden over it. |caption|, set before
// InjectWidgetMessage(), is the widget message's text, shown as the caption
// below its media; empty, the default, is no text. A caller that sets
// |watchHost| before InjectWidgetMessage() also gets |host|, the message's
// host watch, armed with that override next to the tap (an empty override
// only watches) and disarmed with it.
struct WidgetFixtureMessage {
	QString name;
	QByteArray html;
	WidgetMessageFields fields;
	QString caption;
	std::optional<WidgetHostOverride> watchHost;
	QString shape;
	QStringList layers;
	FullMsgId id;
	GlobalMsgId key;
	std::shared_ptr<WidgetOpsTap> tap;
	std::shared_ptr<WidgetHostWatch> host;
	WidgetFixtureWait wait;
	WidgetRuntimeLog log;
	crl::time removedAt = 0;
	int payloadBytes = 0;
	bool armed = false;
	bool removed = false;
};

// What a card shows now, read only. |view| and |core| are addresses,
// compared and never dereferenced.
struct WidgetCardReading {
	QString state = u"none"_q;
	QRect card;
	QSize frameArea;
	quintptr view = 0;
	quintptr core = 0;
	bool html = false;
	bool runtime = false;
	bool live = false;
	bool interactive = false;
	bool attached = false;
	bool accepts = false;
	bool frame = false;
	bool frameAtCard = false;
};

// The bounds of the waits below. Each ends inside its stage's until with a
// named reading; a stage built on one takes it plus kDefaultStageTimeout.
inline constexpr auto kWidgetLayoutBound = crl::time(8000);
inline constexpr auto kWidgetLogBound = crl::time(3000);
inline constexpr auto kWidgetBarrierBound = crl::time(10000);
inline constexpr auto kWidgetPlaceBound = crl::time(6000);

// Changes nothing. The caller registers its Runner::onFinish, calling
// ReleaseWidgetFixture(), before it appends any stage that changes state.
[[nodiscard]] std::shared_ptr<WidgetFixture> MakeWidgetFixture(
	WidgetFixtureArgs args);

// "capture the baseline and show the window normally", "keep the window
// exposed" and "open the chat in the legacy HistoryWidget": the fixture
// gates (Test session, the chat, widget messages enabled, no layer, not
// hidden, no proxy at a control port, shown, exposed, the chat's own
// HistoryInner loaded at its bottom). A failed gate is one "fixture gate:"
// FAIL; WidgetFixtureGate() then names it and the later stages skip.
void AppendWidgetFixtureOpen(
	not_null<Runner*> runner,
	const std::shared_ptr<WidgetFixture> &fixture);

// After AppendWidgetFixtureOpen(): "move the window for the one-column
// layout" and "size the window for the one-column layout", two settled
// MoveFixtureWindow() calls on the window's own screen. A missing window or
// screen, a refused target and, after the second, any layout but only the
// chat in one column are "fixture gate:" FAILs. A maximized or full-screen
// window leaves that state first and stays normal, the state the teardown's
// settle then expects: maximizing it again would activate it. Nothing
// requests activation and no geometry is restored.
void AppendWidgetFixtureOneColumn(
	not_null<Runner*> runner,
	const std::shared_ptr<WidgetFixture> &fixture);

// "network control <title>": the controls of that phase, one Check and one
// <marker>_NET_CONTROL row per probe.
void AppendWidgetFixtureControls(
	not_null<Runner*> runner,
	const std::shared_ptr<WidgetFixture> &fixture,
	WidgetFixtureControls phase,
	const QString &title);

// "teardown" (the release, then its Checks) and "settle after the
// teardown" (the window state held through the repaint settle).
void AppendWidgetFixtureClose(
	not_null<Runner*> runner,
	const std::shared_ptr<WidgetFixture> &fixture);

// Idempotent: disarms every registered tap and host watch by its kept key,
// destroys every registered message by id, closes the control sockets,
// leaves the list and restores the option, the chat, the exposure hint and
// the window state the fixture changed. Returns its text for the caller's
// Note.
[[nodiscard]] QString ReleaseWidgetFixture(
	WidgetFixture &fixture,
	const QString &path);

// The first failed fixture gate, or empty.
[[nodiscard]] QString WidgetFixtureGate(const WidgetFixture &fixture);
// Why the network controls are unavailable, or empty.
[[nodiscard]] QString WidgetFixtureNetwork(const WidgetFixture &fixture);
// The gated session, history and list, or null once gone.
[[nodiscard]] Main::Session *WidgetFixtureSession(
	const WidgetFixture &fixture);
[[nodiscard]] History *WidgetFixtureHistory(const WidgetFixture &fixture);
[[nodiscard]] QWidget *WidgetFixtureList(const WidgetFixture &fixture);
// The fixture session's message |id|, or null.
[[nodiscard]] HistoryItem *WidgetFixtureItem(
	const WidgetFixture &fixture,
	FullMsgId id);
// Whether |id| is a registered record's id, from before its injection.
[[nodiscard]] bool WidgetFixtureOwns(
	const WidgetFixture &fixture,
	FullMsgId id);

// Registers a record; the release destroys only registered messages.
[[nodiscard]] std::shared_ptr<WidgetFixtureMessage> AddWidgetFixtureMessage(
	WidgetFixture &fixture,
	const QString &name);

// Starts |message|'s wait, arms the tap (and the host watch |watchHost|
// asks for) for the id the injection uses and adds a local outgoing
// client-id widget message of |html| with the width and height anchors (0
// is auto) and the record's |caption| as its text, then processes the
// pending layout. The media carries |fields| as given (an absent field
// stays unset, a present empty one or a zero id is sent), the message
// comes via |fields|.viaBot when it is nonzero, the record keeps them and
// CheckWidgetMessage() reads them back. Returns empty, or the refusal
// ("not injected: ..." when nothing was added).
[[nodiscard]] QString InjectWidgetMessage(
	WidgetFixture &fixture,
	WidgetFixtureMessage &message,
	const QByteArray &html,
	int width,
	int height,
	const WidgetMessageFields &fields = {});

// The same for a local outgoing client-id text message, no tap. Its layout
// is settled when the next widget message's WidgetMessageLaidOut() is.
[[nodiscard]] QString InjectTextMessage(
	WidgetFixture &fixture,
	WidgetFixtureMessage &message,
	const QString &text);

// An until: the message is gone, or its list's own view shows a laid-out
// card with nothing pending, or kWidgetLayoutBound passed.
[[nodiscard]] bool WidgetMessageLaidOut(
	const WidgetFixture &fixture,
	WidgetFixtureMessage &message);

// After WidgetMessageLaidOut(): fills |shape|; returns empty, or "no
// HtmlWidget view: ..." naming what is not a local widget message card,
// including media fields other than the injected |fields| and a via bot
// other than |fields|.viaBot (none for zero).
[[nodiscard]] QString CheckWidgetMessage(
	const WidgetFixture &fixture,
	WidgetFixtureMessage &message);

// Hides a shown layer at once, confirming nothing, records |when| in
// |layers| and Notes it.
void HideFixtureLayers(
	WidgetFixture &fixture,
	WidgetFixtureMessage &message,
	const QString &when);

// Hides layers, disarms the tap and the host watch and destroys the message
// by id.
void RemoveFixtureMessage(
	WidgetFixture &fixture,
	WidgetFixtureMessage &message);

// No factory call and no wrapper of the reading's tap is pending.
[[nodiscard]] bool WidgetTapFinished(const WidgetOpsReading &reading);

// An until after RemoveFixtureMessage(): no tap, the tap finished, or
// kWidgetBarrierBound passed since the removal. True at the bound as well:
// a caller that needs the message's hosts gone checks
// WidgetTapFinished(ReadWidgetOps(*message.tap)) after it.
[[nodiscard]] bool FixtureBarrierDecided(const WidgetFixtureMessage &message);

// Starts the wait and asks the runtime of the list's own view for its log.
// The callback holds |message| weakly. A runtime that keeps no callback
// gives logText() at once (source "logText").
void RequestRuntimeLog(
	const WidgetFixture &fixture,
	const std::shared_ptr<WidgetFixtureMessage> &message);

// An until: received, no runtime, no callback kept, or kWidgetLogBound.
[[nodiscard]] bool RuntimeLogDecided(
	const WidgetFixture &fixture,
	const WidgetFixtureMessage &message);

// After RuntimeLogDecided(): a log never received is logText() now
// ("logText-at-bound", its summary Stale) or "unavailable"; sets |chars|.
void FinishRuntimeLog(
	const WidgetFixture &fixture,
	WidgetFixtureMessage &message);

// The list's own view of |id|, found through the history's blocks and
// accepted only when the HistoryInner's itemTop() places it, and its
// runtime.
[[nodiscard]] HistoryView::Element *FixtureOwnView(
	const WidgetFixture &fixture,
	FullMsgId id);
[[nodiscard]] Tgw::Runtime *FixtureOwnRuntime(
	const WidgetFixture &fixture,
	FullMsgId id);

// A stop the desktop decided itself, such as a paint bound's, which no
// host death carries: the RendererCore's own stop reason. |core| is the
// watched host's core (WidgetHostSample::core), |fatalFrozen| the tap's
// count. Empty unless |core| is nonzero, the list's own runtime of |id| is
// Stopped with |core| as its Runtime::core() (the still once retired), that
// core stopped fatally and the host sent no fatal frozen. Only reads.
[[nodiscard]] QString WidgetDesktopStop(
	const WidgetFixture &fixture,
	FullMsgId id,
	quintptr core,
	int fatalFrozen);

// The HtmlWidget card's rectangle in |view|'s coordinates, or empty.
[[nodiscard]] QRect WidgetCardLocal(
	not_null<const HistoryView::Element*> view);
// Only reads; safe in the show helper's samples.
[[nodiscard]] WidgetCardReading ReadWidgetCard(
	not_null<const HistoryView::Element*> view);
// A Live runtime whose interactive core is its current core, attached and
// accepting input, with a cached frame cut for the current card size.
[[nodiscard]] bool WidgetCardReady(const WidgetCardReading &card);
[[nodiscard]] QString WidgetCardText(const WidgetCardReading &card);

// Right before a press: the |shown| show (named |purpose| in the text), the
// list, its own view of |id|, the core and the geometry are still the
// |accepted| ones. Empty when they are, else what changed.
[[nodiscard]] QString RevalidateWidgetCard(
	const WidgetFixture &fixture,
	FullMsgId id,
	const QString &purpose,
	const ChatListShow &shown,
	const WidgetCardReading &accepted);

// The Ui::ElasticScroll that owns |list| directly, or null.
[[nodiscard]] Ui::ElasticScroll *FixtureListScroll(QWidget *list);
// A settled QEvent::Leave to |list|, when there is one.
void DeliverListLeave(QWidget *list);

// The primary window's one-column placement its callers share; each
// caller keeps its own screen choice, window state and gates. The
// calculation and the settle: docs/helpers/test_widget_fixture.md.

// How far inside its screen's available area the placed window goes, in
// logical px: a harness constant, not a product style.
inline constexpr auto kWidgetWindowMargin = 64;

// The app's columns as the primary window's session controller laid them
// out, read only. |threshold| is columnMinimalWidthLeft +
// columnMinimalWidthMain: SessionController::computeColumnLayout() lays a
// body narrower than it out in one column. |body| is that body,
// bodyWidget()->width() - filtersWidth(); |content| is MainWidget's width
// and |chat| the width of the legacy HistoryWidget showing the fixture's
// chat, when it is shown.
struct FixtureColumns {
	QString columns;
	QString error;
	int threshold = 0;
	int bodyWidget = 0;
	int filters = 0;
	int body = 0;
	int content = 0;
	int chat = 0;
	int dialogsShown = 0;
	int minimumWidth = 0;
	bool chatShown = false;
};

// Where the window goes, in global logical coordinates: |frame| with its
// frame, |client| the geometry that gives it, and the bodyWidget() width
// it is for.
struct FixtureWindowTarget {
	QRect frame;
	QRect client;
	QString error;
	int bodyWidget = 0;
};

// One MoveFixtureWindow(): the columns it read before the move, the target
// computed from them, and whether the window was given that target.
struct FixtureWindowMove {
	FixtureColumns columns;
	FixtureWindowTarget target;
	bool moved = false;
};

// Only reads |window|, the primary window showing the fixture's chat; a
// window without a session controller, body or content is an error.
[[nodiscard]] FixtureColumns ReadFixtureColumns(
	const WidgetFixture &fixture,
	QWidget *window);
// One column, its body below the threshold, no chat list shown and the
// chat as wide as MainWidget, so no third column beside it either.
[[nodiscard]] bool OnlyFixtureChatShown(const FixtureColumns &layout);
[[nodiscard]] QString FixtureColumnsText(const FixtureColumns &layout);

// Reads the columns, computes the one-column target on |screen| and calls
// only setGeometry(target.client), when the target has no error and
// differs from the window's geometry.
[[nodiscard]] FixtureWindowMove MoveFixtureWindow(
	const WidgetFixture &fixture,
	not_null<QWidget*> window,
	not_null<QScreen*> screen);

[[nodiscard]] QString FixtureWindowText(not_null<QWidget*> window);
// On |screen|, not minimized, maximized or full screen, and its frame
// wholly inside the screen's available area.
[[nodiscard]] bool FixtureWindowPlaced(
	not_null<QWidget*> window,
	not_null<QScreen*> screen);

// An until after a move, on a |wait| started before it: placed with
// FixtureWindowText() and the columns' text unchanged for a second, or
// kWidgetPlaceBound passed; |wait|.sample keeps the last reading. The bound
// only ends the wait: the caller judges the placement after it.
[[nodiscard]] bool FixtureWindowSettled(
	const WidgetFixture &fixture,
	WidgetFixtureWait &wait,
	not_null<QWidget*> window,
	not_null<QScreen*> screen);

[[nodiscard]] QString RuntimeStateName(Tgw::Runtime::State state);

void StartFixtureWait(WidgetFixtureWait &wait);
[[nodiscard]] bool FixtureBoundReached(
	const WidgetFixtureWait &wait,
	crl::time bound);

} // namespace Test
