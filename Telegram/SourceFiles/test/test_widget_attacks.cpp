/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_widget_attacks.h"

#include "base/options.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "history/view/media/history_view_html_widget.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/history_widget.h"
#include "lang/lang_text_entity.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/mtproto_proxy_data.h"
#include "test/test_capture.h"
#include "test/test_chat_show.h"
#include "test/test_launch_fuse.h"
#include "test/test_log.h"
#include "test/test_probe.h"
#include "test/test_runner.h"
#include "test/test_widget_ops.h"
#include "test/test_widgets.h"
#include "test/test_window_exposure.h"
#include "tgw/tgw_media.h"
#include "tgw/tgw_option.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_runtime.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/elastic_scroll.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QUdpSocket>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <zlib.h>

namespace Test {
namespace {

using Element = HistoryView::Element;
using HtmlWidget = HistoryView::HtmlWidget;
using RuntimeState = Tgw::Runtime::State;
using Way = Window::SectionShow::Way;

// The test-only private-member accessor of test_chat_show.cpp, kept
// file-private here: the card rectangle of HtmlWidget, the interactive
// core's cached frame and the card area it was cut for, and whether
// Runtime::requestLog() kept its callback. Every use only reads.
template <typename Tag, typename Tag::type Member>
struct PrivateMember {
	friend typename Tag::type Get(Tag) {
		return Member;
	}
};

struct HtmlCardRectTag {
	using type = QRect (HtmlWidget::*)() const;
	friend type Get(HtmlCardRectTag);
};

template struct PrivateMember<HtmlCardRectTag, &HtmlWidget::cardRect>;

struct CoreFrameTag {
	using type = QImage Tgw::RendererCore::*;
	friend type Get(CoreFrameTag);
};

template struct PrivateMember<CoreFrameTag, &Tgw::RendererCore::_frame>;

struct CoreFrameAreaTag {
	using type = QSize Tgw::RendererCore::*;
	friend type Get(CoreFrameAreaTag);
};

template struct PrivateMember<
	CoreFrameAreaTag,
	&Tgw::RendererCore::_frameArea>;

struct RuntimeLogDoneTag {
	using type = Fn<void(const QString &text)> Tgw::Runtime::*;
	friend type Get(RuntimeLogDoneTag);
};

template struct PrivateMember<RuntimeLogDoneTag, &Tgw::Runtime::_logDone>;

constexpr auto kPrefix = "widget attack audit: ";

// The audit's bounded waits. Each ends in a named reading inside its
// stage's until, and the stage timeout is the bound plus
// kDefaultStageTimeout, so a bound is never a stage timeout.
constexpr auto kWindowBound = crl::time(4000);
constexpr auto kOpenBound = crl::time(20000);
constexpr auto kLayoutBound = crl::time(8000);
constexpr auto kShowTimeout = crl::time(10000);
constexpr auto kLoadBound = crl::time(25000);
constexpr auto kLogBound = crl::time(3000);
constexpr auto kBarrierBound = crl::time(10000);
constexpr auto kNetworkBound = crl::time(3000);
constexpr auto kRestoreBound = crl::time(6000);

// The reference --attacks loop's Options::waitMs.
constexpr auto kWatch = crl::time(1500);

// The reference click's clientX and clientY, in CSS px from the card's
// top-left corner.
constexpr auto kPressCss = 5.;

// A card taller than this share of the list's scroll is shown by a band
// at its top, holding the press point, instead of whole.
constexpr auto kTallPercent = 75;
constexpr auto kTallBand = 48;

// How a sample must read the same before the chat counts as opened (the
// show helper's own kSettle), and the main window's repaint settle after
// its state is restored (test_chat_show_self_test.cpp's kRepaintSettle).
constexpr auto kSettle = crl::time(400);
constexpr auto kRepaintSettle = crl::time(1500);

// The runtime log is bounded by the product (kLogBytes); its row keeps the
// head, which holds the state and diagnostic kinds.
constexpr auto kMaxLogChars = 32768;

struct WaitState {
	QString sample;
	QString details;
	crl::time started = 0;
	crl::time stableSince = 0;
};

// What a card shows now, read only. |view| and |core| are addresses,
// compared and never dereferenced.
struct CardReading {
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

// One selected corpus file. Its message is named by id only, and its tap
// by the session-qualified key it was armed with, kept for every disarm.
struct AttackFile {
	QString name;
	QByteArray html;
	QString sha256;
	QString notRun;
	FullMsgId id;
	GlobalMsgId key;
	std::shared_ptr<WidgetOpsTap> tap;
	std::shared_ptr<const ChatListShow> loadShow;
	std::shared_ptr<const ChatListShow> clickShow;
	WidgetOpsReading reading;
	WidgetOpsReading watched;
	CardReading loadCard;
	CardReading accepted;
	Verdict verdict;
	WaitState wait;
	QString fixture;
	QString loadOutcome;
	QString stateAfterLoad;
	QString stateAfterWatch;
	QString clickOutcome;
	QString clickDetails;
	QString log;
	QString logSource;
	QString diedReason;
	QString rowPath;
	QStringList layers;
	crl::time watchStarted = 0;
	crl::time removedAt = 0;
	crl::time loadWaitMs = 0;
	crl::time watchMs = 0;
	crl::time barrierMs = 0;
	qint64 startMs = 0;
	qint64 endMs = 0;
	qint64 pressMs = 0;
	int payloadBytes = 0;
	int logChars = 0;
	bool armed = false;
	bool live = false;
	bool clickAttempted = false;
	bool logPending = false;
	bool logReceived = false;
	bool logHeader = false;
	bool diedLine = false;
	bool removed = false;
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

// One control connection or datagram. The socket is declared last, so it
// is destroyed first; CloseProbe() disconnects it before that.
struct ControlProbe {
	QString protocol;
	QByteArray line;
	QString error;
	qint64 startedMs = 0;
	qint64 doneMs = 0;
	qint64 written = 0;
	int port = 0;
	int localPort = 0;
	bool connected = false;
	bool done = false;
	std::unique_ptr<QTcpSocket> tcp;
	std::unique_ptr<QUdpSocket> udp;
};

struct ControlPhase {
	QString name;
	std::vector<std::unique_ptr<ControlProbe>> probes;
	crl::time started = 0;
};

struct AuditRelease {
	QString path;
	QStringList done;
	QString items;
	QString option;
	QString chat;
	QString window;
	int destroyed = 0;
	int disarmed = 0;
	bool itemsGone = false;
	bool optionRestored = false;
	bool chatRestored = false;
};

// The one State of the audit. The window, its session and the history are
// held weakly and the list as a QPointer, so nothing dangles across turns.
struct AuditState {
	WidgetAttackAuditArgs args;
	std::vector<AttackFile> files;
	std::vector<NonWidget> others;
	QStringList htmlNames;
	QStringList ignored;
	QStringList unknown;
	QStringList unreadable;
	QString corpusError;
	QString networkUnavailable;
	WidgetOpsReading control;
	bool selfChecked = false;

	base::weak_ptr<Window::Controller> controller;
	base::weak_ptr<Window::SessionController> navigation;
	base::weak_ptr<Main::Session> session;
	base::weak_ptr<History> history;
	QPointer<QWidget> list;

	// The baseline, read before anything is changed.
	Qt::WindowStates windowState;
	WindowExposure exposure;
	QString baselineChat;
	PeerId baselinePeer = 0;
	bool windowHidden = false;
	bool optionFound = false;
	bool captured = false;

	// Every mutation is marked the moment it is made.
	bool shownNormal = false;
	bool exposureKept = false;
	bool optionChanged = false;
	bool chatOpened = false;

	QString gate;
	QString abort;
	WaitState wait;

	ControlPhase before;
	ControlPhase after;

	AuditRelease release;
	crl::time restoredAt = 0;
	bool optionRestored = false;
	bool chatClosed = false;
	bool exposureRestored = false;
	bool windowRestored = false;
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

[[nodiscard]] QString SizeText(QSize size) {
	return u"%1x%2"_q.arg(
		NumberText(size.width()),
		NumberText(size.height()));
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

[[nodiscard]] QString StateName(RuntimeState state) {
	switch (state) {
	case RuntimeState::Idle: return u"idle"_q;
	case RuntimeState::Waiting: return u"waiting"_q;
	case RuntimeState::Constructing: return u"constructing"_q;
	case RuntimeState::Live: return u"live"_q;
	case RuntimeState::Released: return u"released"_q;
	case RuntimeState::Stopped: return u"stopped"_q;
	case RuntimeState::Failed: return u"failed"_q;
	}
	return u"unknown"_q;
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

void StartWait(WaitState &wait) {
	wait = WaitState{ .started = crl::now() };
}

// True once |sample| read the same for kSettle. Only the wait's own
// bookkeeping is written.
[[nodiscard]] bool SampleSettled(WaitState &wait, const QString &sample) {
	const auto now = crl::now();
	if (sample != wait.sample) {
		wait.sample = sample;
		wait.stableSince = now;
		return false;
	}
	return (now - wait.stableSince) >= kSettle;
}

[[nodiscard]] bool BoundReached(const WaitState &wait, crl::time bound) {
	return (crl::now() - wait.started) >= bound;
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

[[nodiscard]] QString NetworkUnavailable(const WidgetAttackAuditArgs &args) {
	auto missing = QStringList();
	if (args.token.isEmpty()) {
		missing.push_back(u"no control token"_q);
	}
	if (args.ports.empty()) {
		missing.push_back(u"no control port"_q);
	}
	for (const auto port : args.ports) {
		if (port <= 0 || port > 65535) {
			missing.push_back(u"port %1 is out of range"_q.arg(port));
		}
	}
	return missing.isEmpty()
		? QString()
		: (u"network controls unavailable: "_q + missing.join(u"; "_q));
}

// ---------------------------------------------------------------------
// The fixture: the window, the chat and its legacy HistoryInner.

// Qt::WindowActive is a reading, not a state to restore: setting it back
// through QWidget::setWindowState would activate the window.
[[nodiscard]] Qt::WindowStates WithoutActive(Qt::WindowStates states) {
	auto result = states;
	result.setFlag(Qt::WindowActive, false);
	return result;
}

[[nodiscard]] QString WindowStateText(Qt::WindowStates states) {
	return u"0x"_q + QString::number(WithoutActive(states).toInt(), 16);
}

[[nodiscard]] ::MainWindow *WindowOf(const AuditState &state) {
	const auto controller = state.controller.get();
	return controller ? controller->widget().get() : nullptr;
}

[[nodiscard]] MainWidget *ContentOf(const AuditState &state) {
	const auto window = WindowOf(state);
	return window ? window->sessionContent() : nullptr;
}

[[nodiscard]] HistoryItem *Message(const AuditState &state, FullMsgId id) {
	const auto session = state.session.get();
	return (session && id) ? session->data().message(id) : nullptr;
}

[[nodiscard]] Window::SectionShow Background() {
	return Window::SectionShow(
		Way::ClearStack,
		anim::type::instant,
		anim::activation::background);
}

void DeliverLeave(QWidget *list) {
	if (!list) {
		return;
	}
	auto leave = QEvent(QEvent::Leave);
	Settle([&] {
		QApplication::sendEvent(list, &leave);
	});
}

[[nodiscard]] QString ProxyTypeName(MTP::ProxyData::Type type) {
	using Type = MTP::ProxyData::Type;
	switch (type) {
	case Type::None: return u"none"_q;
	case Type::Socks5: return u"socks5"_q;
	case Type::Http: return u"http"_q;
	case Type::Mtproto: return u"mtproto"_q;
	case Type::Web: return u"web"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] bool LoopbackHost(const QString &host) {
	const auto address = QHostAddress(host);
	return !host.compare(u"localhost"_q, Qt::CaseInsensitive)
		|| (!address.isNull() && address.isLoopback());
}

// The account's proxy, without credentials, and every endpoint it can use
// that is a loopback control port.
[[nodiscard]] QJsonObject ReadProxy(
		const std::vector<int> &ports,
		QStringList &conflicts) {
	const auto &proxy = Core::App().settings().proxy();
	const auto endpoint = [](const MTP::ProxyData &data) {
		return QJsonObject{
			{ u"type"_q, ProxyTypeName(data.type) },
			{ u"host"_q, data.host },
			{ u"port"_q, int(data.port) },
		};
	};
	const auto check = [&](const MTP::ProxyData &data, const QString &what) {
		if (data.type != MTP::ProxyData::Type::None
			&& LoopbackHost(data.host)
			&& ranges::contains(ports, int(data.port))) {
			conflicts.push_back(u"%1 %2:%3"_q.arg(
				what,
				data.host,
				NumberText(data.port)));
		}
	};
	const auto selected = proxy.selected();
	auto list = QJsonArray();
	for (const auto &data : proxy.list()) {
		list.push_back(endpoint(data));
	}
	if (proxy.isEnabled()) {
		check(selected, u"the selected proxy"_q);
		if (proxy.proxyRotationEnabled()) {
			for (const auto &data : proxy.list()) {
				check(data, u"a rotation proxy"_q);
			}
		}
	}
	return QJsonObject{
		{ u"mode"_q, proxy.isEnabled()
			? u"enabled"_q
			: proxy.isSystem()
			? u"system"_q
			: u"disabled"_q },
		{ u"selected"_q, endpoint(selected) },
		{ u"rotation"_q, proxy.proxyRotationEnabled() },
		{ u"list"_q, list },
	};
}

// One fixture-gate Check; a failure gates every later file.
void GateCheck(
		AuditState &state,
		const QString &what,
		const QStringList &missing,
		const QString &details) {
	const auto full = u"fixture gate: "_q + Prefixed(what);
	Check(
		missing.isEmpty(),
		full,
		missing.isEmpty()
			? details
			: (missing.join(u"; "_q) + u" - "_q + details));
	if (!missing.isEmpty() && state.gate.isEmpty()) {
		state.gate = full + u" - "_q + missing.join(u"; "_q);
	}
}

[[nodiscard]] QString BaselineText(const AuditState &state) {
	const auto history = state.history.get();
	const auto known = history && history->unreadCountKnown();
	const auto fields = QStringList{
		u"chat=%1"_q.arg(NumberText(qint64(state.args.chat.value))),
		u"windowState=%1"_q.arg(WindowStateText(state.windowState)),
		u"hidden=%1"_q.arg(FlagText(state.windowHidden)),
		u"use-new-chat-view=%1"_q.arg(FlagText(state.optionFound)),
		u"baseline={%1}"_q.arg(state.baselineChat),
		u"widgetMessages=%1"_q.arg(FlagText(Tgw::Enabled())),
		u"folderKnown=%1"_q.arg(FlagText(history && history->folderKnown())),
		u"unread=%1"_q.arg(known
			? NumberText(history->unreadCount())
			: u"unknown"_q),
		u"loadedAtBottom=%1"_q.arg(
			FlagText(history && history->loadedAtBottom())),
	};
	return fields.join(QChar(' '));
}

// Reads the baseline and the fixture's prerequisites; changes nothing.
[[nodiscard]] QStringList CaptureBaseline(AuditState &state) {
	const auto controller = Core::App().activePrimaryWindow();
	const auto navigation = controller
		? controller->sessionController()
		: nullptr;
	if (!controller) {
		return { u"Core::App().activePrimaryWindow() is null"_q };
	} else if (!navigation) {
		return { u"the primary window has no session controller"_q };
	}
	const auto window = controller->widget().get();
	if (!window->sessionContent()) {
		return { u"the primary window shows no session content"_q };
	}
	const auto session = &navigation->session();
	state.controller = base::make_weak(controller);
	state.navigation = base::make_weak(navigation);
	state.session = base::make_weak(session);
	state.windowState = WithoutActive(window->windowState());
	state.windowHidden = window->isHidden();
	state.optionFound = base::options::lookup<bool>(
		kOptionUseNewChatView).value();
	const auto peer = navigation->activeChatCurrent().peer();
	state.baselinePeer = peer ? peer->id : PeerId();
	state.baselineChat = peer
		? (u"peer="_q + NumberText(qint64(peer->id.value)))
		: u"none"_q;
	state.captured = true;
	auto missing = QStringList();
	if (session->account().mtp().environment() != MTP::Environment::Test) {
		missing.push_back(u"the window's session is not on the Test "
			u"environment"_q);
	}
	if (!Tgw::Enabled()) {
		missing.push_back(u"widget messages are disabled, so a widget "
			u"message gets no HtmlWidget view"_q);
	}
	if (controller->isLayerShown()) {
		missing.push_back(u"a layer is shown over the chat"_q);
	}
	if (state.windowHidden) {
		missing.push_back(u"the primary window is hidden"_q);
	}
	const auto chat = state.args.chat
		? session->data().peerLoaded(state.args.chat)
		: nullptr;
	if (!chat) {
		missing.push_back(u"the chat is not loaded in this session"_q);
		return missing;
	}
	const auto history = session->data().history(chat);
	state.history = base::make_weak(history.get());
	if (!history->folderKnown()) {
		missing.push_back(u"its folder is not known, so "
			u"History::newItemAdded would request a dialog entry"_q);
	}
	if (!history->unreadCountKnown() || history->unreadCount()) {
		missing.push_back(u"it may hold unread messages, which a shown "
			u"window marking read would send a read request for"_q);
	}
	return missing;
}

void StartBaseline(AuditState &state) {
	StartWait(state.wait);
	auto conflicts = QStringList();
	const auto proxy = ReadProxy(state.args.ports, conflicts);
	Row(u"ATTACK_ENVIRONMENT"_q, {
		{ u"pid"_q, qint64(QCoreApplication::applicationPid()) },
		{ u"wallMs"_q, WallNow() },
		{ u"token"_q, state.args.token },
		{ u"ports"_q, [&] {
			auto result = QJsonArray();
			for (const auto port : state.args.ports) {
				result.push_back(port);
			}
			return result;
		}() },
		{ u"proxy"_q, proxy },
		{ u"proxyConflicts"_q, Strings(conflicts) },
	});
	const auto missing = CaptureBaseline(state);
	GateCheck(
		state,
		u"a Test-session window shows the chat, loaded with its folder "
		u"known and no unread messages, widget messages are enabled, no "
		u"layer is shown and the window is not hidden"_q,
		missing,
		BaselineText(state));
	GateCheck(
		state,
		u"no proxy the account can use points at a loopback control "
		u"port, so account traffic cannot reach the control listener"_q,
		conflicts,
		QString::fromUtf8(QJsonDocument(proxy).toJson(
			QJsonDocument::Compact)));
	if (!state.gate.isEmpty()) {
		return;
	}
	const auto window = WindowOf(state);
	if (window && window->isMinimized()) {
		state.shownNormal = true;
		window->showNormal();
		Note(Prefixed(u"the primary window was minimized, so showNormal() "
			u"was called; nothing activates it"_q));
	}
}

[[nodiscard]] QString WindowText(const AuditState &state) {
	const auto window = WindowOf(state);
	if (!window) {
		return u"window=gone"_q;
	}
	return u"captured{state=%1 hidden=%2} now{state=%3 minimized=%4 "
		u"hidden=%5}"_q.arg(
			WindowStateText(state.windowState),
			FlagText(state.windowHidden),
			WindowStateText(window->windowState()),
			FlagText(window->isMinimized()),
			FlagText(window->isHidden()));
}

[[nodiscard]] bool WindowShown(AuditState &state) {
	if (!state.gate.isEmpty()) {
		return true;
	}
	const auto window = WindowOf(state);
	state.wait.details = WindowText(state);
	return (window && !window->isMinimized() && !window->isHidden())
		|| BoundReached(state.wait, kWindowBound);
}

void CheckWindowShown(AuditState &state) {
	if (!state.gate.isEmpty()) {
		return;
	}
	const auto window = WindowOf(state);
	auto missing = QStringList();
	if (!window || window->isMinimized() || window->isHidden()) {
		missing.push_back(u"the primary window is not shown normally"_q);
	}
	GateCheck(
		state,
		u"the primary window is shown and not minimized"_q,
		missing,
		state.wait.details);
}

// KeepWindowExposed only sets a stacking hint: it never activates the
// window, and the audit activates nothing at all.
void KeepExposed(AuditState &state) {
	StartWait(state.wait);
	state.exposure = KeepWindowExposed(WindowOf(state));
	state.exposureKept = true;
	Note(Prefixed(u"KeepWindowExposed: "_q)
		+ WindowExposureText(state.exposure));
}

[[nodiscard]] bool WindowExposed(AuditState &state) {
	if (!state.gate.isEmpty() || !state.exposure.refusal.isEmpty()) {
		return true;
	}
	const auto reading = ReadWindowExposure(WindowOf(state));
	state.wait.details = WindowExposureText(reading);
	return reading.exposed || BoundReached(state.wait, kWindowBound);
}

void CheckWindowExposed(AuditState &state) {
	if (!state.gate.isEmpty()) {
		return;
	}
	const auto reading = ReadWindowExposure(WindowOf(state));
	auto missing = QStringList();
	if (!state.exposure.refusal.isEmpty()) {
		missing.push_back(u"KeepWindowExposed refused: "_q
			+ state.exposure.refusal);
	} else if (!reading.exposed) {
		missing.push_back(u"the primary window is not exposed"_q);
	}
	GateCheck(
		state,
		u"the primary window is exposed, so its list gets real paints"_q,
		missing,
		WindowExposureText(reading));
}

// Shows the chat in the background at its end in the legacy
// HistoryWidget, through the existing use-new-chat-view option.
void OpenChat(AuditState &state) {
	StartWait(state.wait);
	auto &option = base::options::lookup<bool>(kOptionUseNewChatView);
	if (option.value()) {
		state.optionChanged = true;
		option.set(false);
	}
	if (const auto navigation = state.navigation.get()) {
		state.chatOpened = true;
		navigation->showPeerHistory(
			state.args.chat,
			Background(),
			ShowAtTheEndMsgId);
	}
}

[[nodiscard]] Element *LastView(not_null<History*> history) {
	if (history->blocks.empty()) {
		return nullptr;
	}
	const auto &block = history->blocks.back();
	return block->messages.empty() ? nullptr : block->messages.back().get();
}

// The Ui::ElasticScroll that owns |list| directly, as every chat section
// builds it, or null.
[[nodiscard]] Ui::ElasticScroll *OwningScroll(QWidget *list) {
	const auto scroll = list
		? dynamic_cast<Ui::ElasticScroll*>(list->parentWidget())
		: nullptr;
	return (scroll && (scroll->widget() == list)) ? scroll : nullptr;
}

// The HistoryInner of the legacy HistoryWidget that shows the chat: its
// scroll's owned widget there and, when the history has views, the list
// itemTop() places its last one in.
[[nodiscard]] HistoryInner *ResolveLegacyInner(const AuditState &state) {
	const auto content = ContentOf(state);
	const auto history = state.history.get();
	if (!content || !history) {
		return nullptr;
	}
	const auto last = LastView(history);
	for (const auto widget : FindAll<HistoryWidget>(content)) {
		if ((widget->history() != history) || widget->isHidden()) {
			continue;
		}
		for (const auto inner : FindAll<HistoryInner>(widget)) {
			if (OwningScroll(inner)
				&& inner->isVisible()
				&& (!last || (inner->itemTop(last) >= 0))) {
				return inner;
			}
		}
	}
	return nullptr;
}

[[nodiscard]] QString ListText(QWidget *list) {
	if (!list) {
		return u"list=none"_q;
	}
	const auto scroll = OwningScroll(list);
	return u"list=%1 scrollHeight=%2"_q.arg(
		WidgetDescription(list),
		scroll ? NumberText(scroll->height()) : u"none"_q);
}

[[nodiscard]] bool LegacyOpened(AuditState &state) {
	if (!state.gate.isEmpty()) {
		return true;
	}
	const auto inner = ResolveLegacyInner(state);
	const auto history = state.history.get();
	const auto ready = inner
		&& history
		&& history->loadedAtBottom()
		&& !history->hasPendingResizedItems()
		&& (inner->height() > 0);
	const auto sample = u"ready=%1 loadedAtBottom=%2 pending=%3 %4"_q.arg(
		FlagText(ready),
		FlagText(history && history->loadedAtBottom()),
		FlagText(history && history->hasPendingResizedItems()),
		ListText(inner));
	state.wait.details = sample;
	return (SampleSettled(state.wait, sample) && ready)
		|| BoundReached(state.wait, kOpenBound);
}

void CheckLegacyOpened(AuditState &state) {
	if (!state.gate.isEmpty()) {
		return;
	}
	const auto inner = ResolveLegacyInner(state);
	const auto history = state.history.get();
	state.list = inner;
	auto missing = QStringList();
	if (!inner) {
		missing.push_back(u"no HistoryInner of a legacy HistoryWidget "
			u"shows the chat as its scroll's owned widget"_q);
	}
	if (!history || !history->loadedAtBottom()) {
		missing.push_back(u"the history is not loaded at its bottom, so "
			u"History::addNewItem would keep a local message out of its "
			u"blocks"_q);
	} else if (history->hasPendingResizedItems()) {
		missing.push_back(u"it still has pending resized items"_q);
	}
	GateCheck(
		state,
		u"the chat is shown by its own HistoryInner in the legacy "
		u"HistoryWidget, loaded at its bottom with nothing pending"_q,
		missing,
		state.wait.details);
}

// The legacy list recounts a pending history layout in HistoryWidget's
// paint, and History::addNewItem leaves it to whoever calls
// Data::Session::sendHistoryChangeNotifications next. This is that call,
// made only in the injecting stage, never from an until.
void ProcessPendingLayout(History *history) {
	if (history && history->hasPendingResizedItems()) {
		history->owner().notifyHistoryChangeDelayed(history);
		history->owner().sendHistoryChangeNotifications();
	}
}

// ---------------------------------------------------------------------
// The card.

[[nodiscard]] QRect CardLocal(not_null<const Element*> view) {
	const auto html = dynamic_cast<const HtmlWidget*>(view->media());
	return html
		? (html->*Get(HtmlCardRectTag()))().translated(view->mediaTopLeft())
		: QRect();
}

[[nodiscard]] QPoint PressLocal(QRect card) {
	const auto offset = Tgw::LogicalWidth(kPressCss);
	return card.topLeft() + QPoint(offset, offset);
}

// The element-local rectangle both shows ask for: the whole card, or a
// band at its top that holds the press point when the card is taller than
// kTallPercent of the list's scroll.
[[nodiscard]] QRect ShowLocal(QRect card, QWidget *list) {
	const auto scroll = OwningScroll(list);
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

// Only reads: the media, its runtime and current interactive core, and the
// core's cached frame. Runs in the show helper's samples.
[[nodiscard]] CardReading ReadCard(not_null<const Element*> view) {
	auto result = CardReading();
	result.view = reinterpret_cast<quintptr>(view.get());
	const auto html = dynamic_cast<const HtmlWidget*>(view->media());
	if (!html) {
		return result;
	}
	result.html = true;
	result.card = CardLocal(view);
	const auto runtime = Tgw::FindRuntime(view);
	if (!runtime) {
		return result;
	}
	result.runtime = true;
	result.state = StateName(runtime->state());
	result.live = (runtime->state() == RuntimeState::Live);
	const auto core = runtime->interactive();
	if (!core) {
		return result;
	}
	result.interactive = (core == runtime->core());
	result.core = reinterpret_cast<quintptr>(core);
	result.attached = core->attached();
	result.accepts = core->acceptsInput();
	const auto &frame = core->*Get(CoreFrameTag());
	result.frame = !frame.isNull();
	result.frameArea = core->*Get(CoreFrameAreaTag());
	result.frameAtCard = result.frame
		&& !result.card.isEmpty()
		&& (result.frameArea == result.card.size());
	return result;
}

// The click's readiness: a Live runtime whose interactive core is its
// current core, attached and accepting input, with a cached frame cut for
// the current card size. No renderer quiescence: the corpus promises none.
[[nodiscard]] bool ClickReady(const CardReading &card) {
	return card.html
		&& card.live
		&& card.interactive
		&& card.attached
		&& card.accepts
		&& card.frameAtCard;
}

[[nodiscard]] QString CardText(const CardReading &card) {
	const auto fields = QStringList{
		u"html=%1"_q.arg(FlagText(card.html)),
		u"runtime=%1"_q.arg(FlagText(card.runtime)),
		u"state=%1"_q.arg(card.state),
		u"interactive=%1"_q.arg(FlagText(card.interactive)),
		u"attached=%1"_q.arg(FlagText(card.attached)),
		u"acceptsInput=%1"_q.arg(FlagText(card.accepts)),
		u"frame=%1"_q.arg(FlagText(card.frame)),
		u"frameArea=%1"_q.arg(SizeText(card.frameArea)),
		u"card=%1"_q.arg(RectText(card.card)),
		u"frameAtCard=%1"_q.arg(FlagText(card.frameAtCard)),
	};
	return u"card{"_q + fields.join(QChar(' ')) + u"}"_q;
}

// ---------------------------------------------------------------------
// One file.

// The list's own view of the message: found through the history's blocks,
// accepted only when the HistoryInner's public itemTop() places it.
[[nodiscard]] Element *OwnView(const AuditState &state, FullMsgId id) {
	const auto inner = dynamic_cast<HistoryInner*>(state.list.data());
	const auto item = Message(state, id);
	if (!inner || !item) {
		return nullptr;
	}
	for (const auto &block : item->history()->blocks) {
		for (const auto &view : block->messages) {
			if (view->data() == item) {
				return (inner->itemTop(view.get()) >= 0)
					? view.get()
					: nullptr;
			}
		}
	}
	return nullptr;
}

[[nodiscard]] Tgw::Runtime *OwnRuntime(
		const AuditState &state,
		const AttackFile &file) {
	const auto view = OwnView(state, file.id);
	return view ? Tgw::FindRuntime(view) : nullptr;
}

[[nodiscard]] QString RuntimeStateText(
		const AuditState &state,
		const AttackFile &file) {
	const auto runtime = OwnRuntime(state, file);
	return runtime ? StateName(runtime->state()) : u"no-runtime"_q;
}

[[nodiscard]] QString GlobalSkip(const AuditState &state) {
	return !state.gate.isEmpty() ? state.gate : state.abort;
}

[[nodiscard]] QString FileSkip(const AuditState &state, int index) {
	const auto global = GlobalSkip(state);
	return global.isEmpty() ? state.files[index].notRun : global;
}

[[nodiscard]] bool LocalShape(not_null<HistoryItem*> item) {
	return item->isLocal()
		&& !item->isRegular()
		&& item->out()
		&& !item->isService()
		&& IsClientMsgId(item->id);
}

// The widget payload as the server packs it, PHP gzcompress($html, 9).
[[nodiscard]] QByteArray Zlib(const QByteArray &data) {
	auto stream = z_stream();
	if (deflateInit2(
			&stream,
			Z_BEST_COMPRESSION,
			Z_DEFLATED,
			MAX_WBITS,
			8,
			Z_DEFAULT_STRATEGY) != Z_OK) {
		return QByteArray();
	}
	const auto guard = gsl::finally([&] {
		deflateEnd(&stream);
	});
	const auto bound = qsizetype(deflateBound(&stream, uLong(data.size())));
	auto result = QByteArray(bound + 64, Qt::Uninitialized);
	stream.next_in = reinterpret_cast<Bytef*>(
		const_cast<char*>(data.constData()));
	stream.avail_in = uInt(data.size());
	stream.next_out = reinterpret_cast<Bytef*>(result.data());
	stream.avail_out = uInt(result.size());
	if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
		return QByteArray();
	}
	result.resize(qsizetype(stream.total_out));
	return result;
}

// Arms the tap for the message id the injection will use, then adds the
// local outgoing widget message: its host can only be built after this.
void Inject(AuditState &state, AttackFile &file) {
	StartWait(file.wait);
	file.startMs = WallNow();
	const auto history = state.history.get();
	const auto inner = state.list.data();
	const auto payload = Zlib(file.html);
	auto missing = QStringList();
	if (!history || !history->loadedAtBottom()) {
		missing.push_back(u"the history is gone or not loaded at its "
			u"bottom"_q);
	}
	if (!inner || !inner->isVisible()) {
		missing.push_back(u"the HistoryInner is not shown"_q);
	}
	if (payload.isEmpty() || (Tgw::UnpackPayload(payload) != file.html)) {
		missing.push_back(u"the zlib payload does not unpack to the file's "
			u"bytes"_q);
	}
	if (!missing.isEmpty()) {
		file.notRun = u"not injected: "_q + missing.join(u"; "_q);
		return;
	}
	file.payloadBytes = int(payload.size());
	file.id = FullMsgId(
		history->peer->id,
		history->owner().nextLocalMessageId());
	file.key = GlobalMsgId{ file.id, history->session().uniqueId() };
	file.tap = ArmWidgetOpsTap(file.key);
	file.armed = true;
	const auto item = history->addNewLocalMessage({
		.id = file.id.msg,
		.flags = (MessageFlag::Local
			| MessageFlag::Outgoing
			| MessageFlag::HasFromId),
		.from = history->session().userPeerId(),
		.date = base::unixtime::now(),
	}, tr::marked(), MTP_messageMediaWidget(
		MTP_int(0), // width
		MTP_int(0), // height
		MTP_bytes(payload)));
	const auto got = item->globalId();
	if (got != file.key) {
		file.notRun = u"the injected message got id %1%2, not the armed "
			u"%3"_q.arg(
				IdText(got.itemId),
				((got.sessionUniqueId == file.key.sessionUniqueId)
					? QString()
					: u" in another session"_q),
				IdText(file.id));
	}
	ProcessPendingLayout(history);
}

[[nodiscard]] bool InjectLaidOut(AuditState &state, AttackFile &file) {
	if (!file.notRun.isEmpty()) {
		return true;
	}
	const auto history = state.history.get();
	const auto view = OwnView(state, file.id);
	const auto card = view ? CardLocal(view) : QRect();
	const auto pending = !history
		|| history->hasPendingResizedItems()
		|| (view && view->pendingResize());
	file.wait.details = u"view=%1 card=%2 pending=%3"_q.arg(
		FlagText(view != nullptr),
		RectText(card),
		FlagText(pending));
	return !Message(state, file.id)
		|| (view && !card.isEmpty() && !pending)
		|| BoundReached(file.wait, kLayoutBound);
}

void CheckInjected(AuditState &state, AttackFile &file) {
	if (!file.notRun.isEmpty()) {
		return;
	}
	const auto item = Message(state, file.id);
	const auto media = item
		? dynamic_cast<Tgw::WidgetMedia*>(item->media())
		: nullptr;
	const auto view = OwnView(state, file.id);
	const auto card = view ? CardLocal(view) : QRect();
	auto missing = QStringList();
	if (!item || !LocalShape(item)) {
		missing.push_back(u"the message is not an outgoing local client-id "
			u"message"_q);
	}
	if (!media || (media->html() != file.html)) {
		missing.push_back(u"its media is not a Tgw::WidgetMedia holding the "
			u"file's bytes"_q);
	}
	if (!view || card.isEmpty()) {
		missing.push_back(u"the list shows no laid-out HtmlWidget card of "
			u"its own for it"_q);
	}
	file.fixture = u"id=%1 local=%2 regular=%3 out=%4 clientMsgId=%5 "
		u"widgetMedia=%6 htmlMatches=%7 payload=%8 %9"_q.arg(
			IdText(file.id),
			FlagText(item && item->isLocal()),
			FlagText(item && item->isRegular()),
			FlagText(item && item->out()),
			FlagText(item && IsClientMsgId(item->id)),
			FlagText(media != nullptr),
			FlagText(media && (media->html() == file.html)),
			NumberText(file.payloadBytes),
			file.wait.details);
	if (!missing.isEmpty()) {
		file.notRun = u"no HtmlWidget view: "_q + missing.join(u"; "_q);
	}
}

[[nodiscard]] bool Loaded(const WidgetOpsReading &reading) {
	return (reading.ackQueries > 0) && (reading.ackResults > 0);
}

// Load, death or failure, or the bound: the tap's ack and frozen counts
// and the current runtime's state, read only.
[[nodiscard]] bool LoadDecided(AuditState &state, AttackFile &file) {
	const auto reading = ReadWidgetOps(*file.tap);
	const auto runtime = OwnRuntime(state, file);
	const auto current = runtime ? runtime->state() : RuntimeState::Idle;
	file.wait.details = u"state=%1 ackQueries=%2 ackResults=%3 frozen=%4 "
		u"built=%5"_q.arg(
			runtime ? StateName(current) : u"no-runtime"_q,
			NumberText(reading.ackQueries),
			NumberText(reading.ackResults),
			NumberText(reading.fatalFrozen),
			NumberText(reading.wrappersBuilt));
	return Loaded(reading)
		|| (reading.fatalFrozen > 0)
		|| !Message(state, file.id)
		|| (current == RuntimeState::Stopped)
		|| (current == RuntimeState::Failed)
		|| BoundReached(file.wait, kLoadBound);
}

void RecordLoad(AuditState &state, AttackFile &file) {
	const auto reading = ReadWidgetOps(*file.tap);
	const auto runtime = OwnRuntime(state, file);
	const auto current = runtime ? runtime->state() : RuntimeState::Idle;
	file.loadWaitMs = crl::now() - file.wait.started;
	file.stateAfterLoad = runtime ? StateName(current) : u"no-runtime"_q;
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
	return (ReadWidgetOps(*file.tap).fatalFrozen > 0)
		|| (current == RuntimeState::Stopped)
		|| (current == RuntimeState::Failed);
}

// The accepted show, view, core and geometry, read again right before the
// press; an empty answer means they are all still the ones accepted.
[[nodiscard]] QString Revalidate(
		const AuditState &state,
		const AttackFile &file) {
	const auto &shown = *file.clickShow;
	const auto list = state.list.data();
	const auto inner = dynamic_cast<HistoryInner*>(list);
	if (!shown.shown()) {
		return u"the click show refused: %1 - %2"_q.arg(
			ChatListShowRefusalName(shown.refusal),
			shown.reason);
	} else if (!inner || (shown.list.data() != list)) {
		return u"the accepted list is gone or is another list"_q;
	} else if (!shown.hovered) {
		return u"the show left no hover at the press point"_q;
	}
	const auto view = OwnView(state, file.id);
	if (!view) {
		return u"the list no longer shows a view of its own for the "
			u"message"_q;
	} else if (reinterpret_cast<quintptr>(view) != file.accepted.view) {
		return u"the list's view of the message is not the accepted one"_q;
	}
	const auto now = ReadCard(view);
	if (!ClickReady(now)) {
		return u"the card is no longer ready: "_q + CardText(now);
	} else if (now.core != file.accepted.core) {
		return u"the interactive core is not the accepted one"_q;
	} else if (now.card != file.accepted.card) {
		return u"the card moved or resized in its view: accepted=%1 now=%2"_q
			.arg(RectText(file.accepted.card), RectText(now.card));
	} else if ((inner->itemTop(view) != shown.element.y())
		|| (QSize(view->width(), view->height()) != shown.element.size())) {
		return u"the view moved or resized in the list: accepted=%1 top=%2 "
			u"size=%3x%4"_q.arg(
				RectText(shown.element),
				NumberText(inner->itemTop(view)),
				NumberText(view->width()),
				NumberText(view->height()));
	}
	return QString();
}

// The click in the stage right after the click show, then the watch. A
// card that is not live gets no press and is watched from now.
void ClickAndWatch(AuditState &state, AttackFile &file) {
	file.watchStarted = crl::now();
	if (!file.live) {
		return;
	}
	const auto problem = Revalidate(state, file);
	if (!problem.isEmpty()) {
		file.clickOutcome = DiedNow(state, file)
			? u"died-before-press"_q
			: file.clickShow->shown()
			? u"revalidation-failed"_q
			: u"show-refused"_q;
		file.clickDetails = problem;
		if (file.clickShow->hovered) {
			DeliverLeave(state.list.data());
		}
		return;
	}
	MarkWidgetOpsEvents(*file.tap);
	file.pressMs = WallNow();
	file.clickAttempted = true;
	Click(state.list.data(), file.clickShow->hover);
	file.watchStarted = crl::now();
}

void HideLayers(AuditState &state, AttackFile &file, const QString &when) {
	const auto controller = state.controller.get();
	if (!controller || !controller->isLayerShown()) {
		return;
	}
	controller->hideSettingsAndLayer(anim::type::instant);
	file.layers.push_back(when);
	Note(Prefixed(u"%1 - a layer was shown %2; it was hidden at once and "
		u"nothing in it was confirmed"_q.arg(file.name, when)));
}

void RecordWatch(AuditState &state, AttackFile &file) {
	file.watchMs = crl::now() - file.watchStarted;
	file.watched = ReadWidgetOps(*file.tap);
	file.stateAfterWatch = RuntimeStateText(state, file);
	if (file.clickAttempted) {
		file.clickOutcome = (file.watched.trustedClicks > 0)
			? u"observed"_q
			: u"not-observed"_q;
	}
	HideLayers(state, file, u"after the watch"_q);
}

void RequestLog(
		const std::shared_ptr<AuditState> &state,
		int index) {
	auto &file = state->files[index];
	StartWait(file.wait);
	const auto runtime = OwnRuntime(*state, file);
	if (!runtime) {
		return;
	}
	const auto weak = std::weak_ptr<AuditState>(state);
	runtime->requestLog([=](const QString &text) {
		if (const auto strong = weak.lock()) {
			auto &entry = strong->files[index];
			if (!entry.logReceived) {
				entry.log = text;
				entry.logSource = u"requestLog"_q;
				entry.logReceived = true;
			}
		}
	});
	file.logPending = bool(runtime->*Get(RuntimeLogDoneTag()));
	if (!file.logPending) {
		// A retired host whose final sample is applied calls nothing back:
		// its logText() already holds the "Last sandbox:" block.
		file.log = runtime->logText();
		file.logSource = u"logText"_q;
		file.logReceived = true;
	}
}

[[nodiscard]] bool LogDecided(AuditState &state, AttackFile &file) {
	if (file.logReceived) {
		return true;
	}
	const auto runtime = OwnRuntime(state, file);
	return !runtime
		|| !(runtime->*Get(RuntimeLogDoneTag()))
		|| BoundReached(file.wait, kLogBound);
}

// The one field of the runtime log the row needs: the "Died: " line that
// Summarize() puts right after the first sandbox header, before any text a
// widget controls (its errors and console may hold line breaks).
void ParseLog(AttackFile &file) {
	const auto lines = file.log.split(QChar('\n'));
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
	if (!file.logReceived) {
		if (const auto runtime = OwnRuntime(state, file)) {
			file.log = runtime->logText();
			file.logSource = u"logText-at-bound"_q;
		} else {
			file.logSource = u"unavailable"_q;
		}
	}
	file.logChars = int(file.log.size());
	ParseLog(file);
	Row(u"ATTACK_LOG"_q, {
		{ u"file"_q, file.name },
		{ u"source"_q, file.logSource },
		{ u"pending"_q, file.logPending },
		{ u"waitedMs"_q, qint64(crl::now() - file.wait.started) },
		{ u"chars"_q, file.logChars },
		{ u"truncated"_q, (file.logChars > kMaxLogChars) },
		{ u"text"_q, file.log.left(kMaxLogChars) },
	});
}

void Remove(AuditState &state, AttackFile &file) {
	HideLayers(state, file, u"before the removal"_q);
	if (file.armed) {
		DisarmWidgetOpsTap(file.key);
		file.armed = false;
	}
	if (const auto item = Message(state, file.id)) {
		item->destroy();
	}
	file.removed = true;
	file.removedAt = crl::now();
}

[[nodiscard]] bool Finished(const WidgetOpsReading &reading) {
	return (reading.factoriesReleased == reading.factoriesSelected)
		&& (reading.wrappersDestroyed == reading.wrappersBuilt);
}

[[nodiscard]] bool BarrierDecided(const AttackFile &file) {
	return !file.tap
		|| Finished(ReadWidgetOps(*file.tap))
		|| (crl::now() - file.removedAt >= kBarrierBound);
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
	} else if (!file.tap) {
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
	file.rowEmitted = true;
	file.rowPath = path;
	if (file.tap && !file.barrierClosed) {
		file.reading = ReadWidgetOps(*file.tap);
	}
	if (!file.tap) {
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
		{ u"item"_q, IdText(file.id) },
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
		{ u"fixture"_q, file.fixture },
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
			{ u"source"_q, file.logSource },
			{ u"chars"_q, file.logChars },
			{ u"sandboxHeader"_q, file.logHeader },
			{ u"diedLine"_q, file.diedLine },
		} },
		{ u"layers"_q, Strings(file.layers) },
		{ u"barrierClosed"_q, file.barrierClosed },
		{ u"barrierMs"_q, qint64(file.barrierMs) },
		{ u"itemGone"_q, file.itemGone || !Message(state, file.id) },
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
	file.barrierMs = crl::now() - file.removedAt;
	file.reading = file.tap ? ReadWidgetOps(*file.tap) : WidgetOpsReading();
	file.barrierClosed = Finished(file.reading);
	file.itemGone = !Message(state, file.id);
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
// The network controls.

[[nodiscard]] QByteArray ControlLine(
		const AuditState &state,
		const QString &phase,
		const QString &protocol,
		int port) {
	const auto json = QJsonDocument(QJsonObject{
		{ u"token"_q, state.args.token },
		{ u"phase"_q, phase },
		{ u"protocol"_q, protocol },
		{ u"port"_q, port },
		{ u"pid"_q, qint64(QCoreApplication::applicationPid()) },
	}).toJson(QJsonDocument::Compact);
	return json + '\n';
}

void FinishProbe(not_null<ControlProbe*> probe, const QString &error) {
	if (probe->done) {
		return;
	}
	probe->error = error;
	probe->done = true;
	probe->doneMs = WallNow();
}

void StartTcp(not_null<ControlProbe*> probe) {
	probe->tcp = std::make_unique<QTcpSocket>();
	const auto socket = probe->tcp.get();
	socket->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	QObject::connect(socket, &QTcpSocket::connected, socket, [=] {
		probe->connected = true;
		probe->localPort = int(socket->localPort());
		if (socket->write(probe->line) < 0) {
			FinishProbe(probe, socket->errorString());
		}
	});
	QObject::connect(socket, &QTcpSocket::bytesWritten, socket, [=](
			qint64 bytes) {
		probe->written += bytes;
		if (!probe->done && (probe->written >= probe->line.size())) {
			FinishProbe(probe, QString());
			socket->disconnectFromHost();
		}
	});
	QObject::connect(socket, &QAbstractSocket::errorOccurred, socket, [=] {
		FinishProbe(probe, socket->errorString());
	});
	probe->startedMs = WallNow();
	socket->connectToHost(
		QHostAddress(QHostAddress::LocalHost),
		quint16(probe->port));
}

void SendUdp(not_null<ControlProbe*> probe) {
	probe->udp = std::make_unique<QUdpSocket>();
	const auto socket = probe->udp.get();
	socket->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
	probe->startedMs = WallNow();
	const auto written = socket->writeDatagram(
		probe->line,
		QHostAddress(QHostAddress::LocalHost),
		quint16(probe->port));
	probe->written = std::max(written, qint64(0));
	probe->localPort = int(socket->localPort());
	FinishProbe(probe, (written < 0) ? socket->errorString() : QString());
}

void CloseProbe(ControlProbe &probe) {
	if (const auto socket = probe.tcp.get()) {
		socket->disconnect();
		socket->abort();
		probe.tcp = nullptr;
	}
	if (const auto socket = probe.udp.get()) {
		socket->disconnect();
		socket->close();
		probe.udp = nullptr;
	}
}

void CloseControls(ControlPhase &phase) {
	for (const auto &probe : phase.probes) {
		CloseProbe(*probe);
	}
}

void StartControls(AuditState &state, ControlPhase &phase) {
	phase.started = crl::now();
	for (const auto port : state.args.ports) {
		for (const auto &protocol : { u"tcp"_q, u"udp"_q }) {
			auto probe = std::make_unique<ControlProbe>();
			probe->protocol = protocol;
			probe->port = port;
			probe->line = ControlLine(state, phase.name, protocol, port);
			phase.probes.push_back(std::move(probe));
			const auto raw = phase.probes.back().get();
			if (protocol == u"tcp"_q) {
				StartTcp(raw);
			} else {
				SendUdp(raw);
			}
		}
	}
}

[[nodiscard]] bool ControlsDecided(const ControlPhase &phase) {
	return ranges::all_of(phase.probes, [](const auto &probe) {
		return probe->done;
	}) || (crl::now() - phase.started >= kNetworkBound);
}

void ReportControls(ControlPhase &phase) {
	for (const auto &probe : phase.probes) {
		if (!probe->done) {
			FinishProbe(
				probe.get(),
				u"no outcome within %1 ms"_q.arg(kNetworkBound));
		}
		const auto tcp = (probe->protocol == u"tcp"_q);
		const auto ok = probe->error.isEmpty()
			&& (probe->written == probe->line.size())
			&& (!tcp || probe->connected);
		Row(u"ATTACK_NET_CONTROL"_q, {
			{ u"phase"_q, phase.name },
			{ u"protocol"_q, probe->protocol },
			{ u"port"_q, probe->port },
			{ u"connected"_q, probe->connected },
			{ u"written"_q, probe->written },
			{ u"lineBytes"_q, int(probe->line.size()) },
			{ u"error"_q, probe->error },
			{ u"localPort"_q, probe->localPort },
			{ u"startedMs"_q, probe->startedMs },
			{ u"doneMs"_q, probe->doneMs },
			{ u"line"_q, QString::fromUtf8(probe->line).trimmed() },
		});
		Check(
			ok,
			Prefixed(u"network control %1 - the %2 control to 127.0.0.1:%3 "
				u"was %4 by this process"_q.arg(
					phase.name,
					probe->protocol,
					NumberText(probe->port),
					tcp
						? u"accepted and its line written"_q
						: u"written as one datagram"_q)),
			u"connected=%1 written=%2/%3 localPort=%4 error=[%5]"_q.arg(
				FlagText(probe->connected),
				NumberText(probe->written),
				NumberText(probe->line.size()),
				NumberText(probe->localPort),
				probe->error));
	}
	CloseControls(phase);
}

// ---------------------------------------------------------------------
// Totals, release and settle.

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
		layers += int(file.layers.size());
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
		if (!file.layers.isEmpty()) {
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

void RestoreChat(AuditState &state) {
	const auto navigation = state.navigation.get();
	if (!navigation) {
		return;
	} else if (state.baselinePeer) {
		navigation->showPeerHistory(
			state.baselinePeer,
			Background(),
			ShowAtUnreadMsgId);
	} else {
		navigation->clearSectionStack(Background());
	}
}

[[nodiscard]] bool WindowMatches(const AuditState &state) {
	const auto window = WindowOf(state);
	return window
		&& (WithoutActive(window->windowState()) == state.windowState)
		&& (window->isHidden() == state.windowHidden);
}

// The one release, run by the teardown stage and again by Runner::onFinish.
// Idempotent: a step already done is only read again.
[[nodiscard]] AuditRelease Release(AuditState &state, const QString &path) {
	auto result = AuditRelease{ .path = path };
	for (auto &file : state.files) {
		if (file.armed) {
			DisarmWidgetOpsTap(file.key);
			file.armed = false;
			++result.disarmed;
		}
		if (const auto item = Message(state, file.id)) {
			item->destroy();
			++result.destroyed;
			if (!file.removed) {
				file.removed = true;
				file.removedAt = crl::now();
			}
		}
	}
	CloseControls(state.before);
	CloseControls(state.after);
	auto left = 0;
	for (const auto &file : state.files) {
		if (Message(state, file.id)) {
			++left;
		}
	}
	result.itemsGone = !left;
	result.items = u"files=%1 destroyed=%2 disarmed=%3 left=%4"_q.arg(
		NumberText(int(state.files.size())),
		NumberText(result.destroyed),
		NumberText(result.disarmed),
		NumberText(left));
	if (!state.captured) {
		result.done.push_back(u"no baseline was captured or changed"_q);
		result.optionRestored = true;
		result.chatRestored = true;
		return result;
	}
	if (const auto list = state.list.data()) {
		DeliverLeave(list);
	}
	auto &option = base::options::lookup<bool>(kOptionUseNewChatView);
	if (state.optionChanged && !state.optionRestored) {
		state.optionRestored = true;
		option.set(state.optionFound);
		result.done.push_back(u"option restored"_q);
	}
	result.optionRestored = (option.value() == state.optionFound);
	result.option = u"use-new-chat-view found=%1 now=%2 changed=%3"_q.arg(
		FlagText(state.optionFound),
		FlagText(option.value()),
		FlagText(state.optionChanged));
	if (state.chatOpened && !state.chatClosed) {
		state.chatClosed = true;
		RestoreChat(state);
		result.done.push_back(u"chat restored"_q);
	}
	const auto navigation = state.navigation.get();
	const auto peer = navigation
		? navigation->activeChatCurrent().peer()
		: nullptr;
	const auto active = peer ? peer->id : PeerId();
	result.chatRestored = !state.chatOpened
		|| (navigation && (active == state.baselinePeer));
	result.chat = u"baseline={%1} now=peer=%2 opened=%3"_q.arg(
		state.baselineChat,
		NumberText(qint64(active.value)),
		FlagText(state.chatOpened));
	if (state.exposureKept && !state.exposureRestored) {
		state.exposureRestored = true;
		RestoreWindowExposure(state.exposure);
		result.done.push_back(u"exposure hint restored"_q);
	}
	const auto window = WindowOf(state);
	if (state.shownNormal && !state.windowRestored && window) {
		state.windowRestored = true;
		state.restoredAt = crl::now();
		window->setWindowState(state.windowState);
		result.done.push_back(u"window state restored"_q);
	}
	result.window = WindowText(state);
	return result;
}

[[nodiscard]] QString ReleaseText(const AuditRelease &release) {
	const auto fields = QStringList{
		u"path=%1"_q.arg(release.path),
		u"done=[%1]"_q.arg(release.done.join(u", "_q)),
		u"items={%1}"_q.arg(release.items),
		u"option={%1}"_q.arg(release.option),
		u"chat={%1}"_q.arg(release.chat),
		u"window={%1}"_q.arg(release.window),
	};
	return fields.join(QChar(' '));
}

void CheckRelease(const AuditState &state) {
	const auto &r = state.release;
	const auto prefix = Prefixed(u"teardown - "_q);
	Check(
		r.itemsGone,
		prefix + u"every local widget message is destroyed by id and every "
			u"tap disarmed"_q,
		r.items);
	Check(
		r.optionRestored,
		prefix + u"the use-new-chat-view option is back to the value it "
			u"was found with"_q,
		r.option);
	Check(
		r.chatRestored,
		prefix + u"the chat found at the baseline (or none) is shown "
			u"again"_q,
		r.chat);
	Note(prefix + ReleaseText(r));
}

[[nodiscard]] QString SettleText(const AuditState &state) {
	const auto exposure = ReadWindowExposure(WindowOf(state));
	return u"%1 restored=%2 stayOnTop{found=%3 now=%4}"_q.arg(
		WindowText(state),
		FlagText(state.windowRestored),
		FlagText(state.exposure.stayOnTop),
		FlagText(exposure.stayOnTop));
}

[[nodiscard]] bool RestoreSettled(AuditState &state) {
	const auto since = state.restoredAt
		? state.restoredAt
		: state.wait.started;
	state.wait.details = SettleText(state);
	return (((crl::now() - since) >= kRepaintSettle) && WindowMatches(state))
		|| BoundReached(state.wait, kRestoreBound);
}

void CheckSettled(const AuditState &state) {
	const auto prefix = Prefixed(u"settle - "_q);
	const auto details = SettleText(state);
	Check(
		WindowMatches(state),
		prefix + u"the main window's state is the captured one again and "
			u"held it through the repaint settle"_q,
		details);
	const auto window = WindowOf(state);
	Check(
		!state.exposureKept
			|| (window
				&& (ReadWindowExposure(window).stayOnTop
					== state.exposure.stayOnTop)),
		prefix + u"the window's stacking hint is the one it was found "
			u"with"_q,
		details);
}

// ---------------------------------------------------------------------
// The stages.

void ReportCorpus(AuditState &state) {
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
		{ u"network"_q, state.networkUnavailable.isEmpty()
			? u"available"_q
			: state.networkUnavailable },
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
		.timeout = kLayoutBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return file().wait.details;
		},
	});
	const auto rect = [=](ChatListShowView view) {
		return ShowLocal(CardLocal(view), state->list.data());
	};
	const auto details = [](ChatListShowView view) {
		return CardText(ReadCard(view));
	};
	file().loadShow = AppendShowInChatList(runner, {
		.name = u"widget attack "_q + name + u" load"_q,
		.resolve = [=] {
			return ChatListShowTarget{ state->list.data(), file().id };
		},
		.rect = rect,
		.ready = [=](ChatListShowView view) {
			const auto card = ReadCard(view);
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
			StartWait(file().wait);
		},
		.until = [=] {
			return LoadDecided(*state, file());
		},
		.then = [=] {
			RecordLoad(*state, file());
		},
		.timeout = kLoadBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return file().wait.details;
		},
	});
	file().clickShow = AppendShowInChatList(runner, {
		.name = u"widget attack "_q + name + u" click"_q,
		.resolve = [=] {
			return ChatListShowTarget{ state->list.data(), file().id };
		},
		.rect = rect,
		.ready = [=](ChatListShowView view) {
			const auto card = ReadCard(view);
			if (!ClickReady(card)) {
				return false;
			}
			file().accepted = card;
			return true;
		},
		.readyDetails = details,
		.hover = [](ChatListShowView view) {
			return PressLocal(CardLocal(view));
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
			RequestLog(state, index);
		},
		.until = [=] {
			return LogDecided(*state, file());
		},
		.then = [=] {
			RecordLog(*state, file());
		},
		.timeout = kLogBound + kDefaultStageTimeout,
	});
	runner->add({
		.name = prefix + u"remove the message and wait for its hosts"_q,
		.skipReason = [=] {
			return (file().armed || Message(*state, file().id))
				? QString()
				: u"nothing was armed or injected for this file"_q;
		},
		.run = [=] {
			Remove(*state, file());
		},
		.until = [=] {
			return BarrierDecided(file());
		},
		.then = [=] {
			Finalize(*state, index);
		},
		.timeout = kBarrierBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			const auto &tap = file().tap;
			return tap
				? u"tap{%1}"_q.arg(QString::fromUtf8(QJsonDocument(
					ReadingObject(ReadWidgetOps(*tap))).toJson(
						QJsonDocument::Compact)))
				: u"no tap"_q;
		},
	});
}

void AppendControlStage(
		not_null<Runner*> runner,
		const std::shared_ptr<AuditState> &state,
		ControlPhase AuditState::*member,
		const QString &title) {
	runner->add({
		.name = Prefixed(u"network control "_q + title),
		.skipReason = [=] {
			return state->networkUnavailable;
		},
		.run = [=] {
			StartControls(*state, (*state).*member);
		},
		.until = [=] {
			return ControlsDecided((*state).*member);
		},
		.then = [=] {
			ReportControls((*state).*member);
		},
		.timeout = kNetworkBound + kDefaultStageTimeout,
	});
}

} // namespace

void AppendWidgetAttackAudit(
		not_null<Runner*> runner,
		WidgetAttackAuditArgs args) {
	const auto state = std::make_shared<AuditState>();
	state->args = std::move(args);
	state->before.name = u"before"_q;
	state->after.name = u"after"_q;
	state->networkUnavailable = NetworkUnavailable(state->args);
	ListCorpus(*state);

	// Registered before anything is changed: every path that reaches
	// Runner::finish() runs the same idempotent release, then writes the
	// rows no stage wrote, so no selected file goes unreported.
	runner->onFinish([=] {
		const auto release = Release(*state, u"onFinish"_q);
		EmitPendingRows(*state, u"onFinish"_q);
		if (!state->totalsEmitted) {
			EmitTotals(*state, u"onFinish"_q, false);
		}
		Note(Prefixed(u"onFinish release: "_q) + ReleaseText(release));
	});
	const auto gate = [=] {
		return state->gate;
	};
	const auto captured = [=] {
		return state->captured
			? QString()
			: u"nothing was captured, so nothing was changed"_q;
	};

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
	runner->add({
		.name = Prefixed(u"capture the baseline and show the window "
			u"normally"_q),
		.run = [=] {
			StartBaseline(*state);
		},
		.until = [=] {
			return WindowShown(*state);
		},
		.then = [=] {
			CheckWindowShown(*state);
		},
		.timeout = kWindowBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	runner->add({
		.name = Prefixed(u"keep the window exposed"_q),
		.skipReason = gate,
		.run = [=] {
			KeepExposed(*state);
		},
		.until = [=] {
			return WindowExposed(*state);
		},
		.then = [=] {
			CheckWindowExposed(*state);
		},
		.timeout = kWindowBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	runner->add({
		.name = Prefixed(u"open the chat in the legacy HistoryWidget"_q),
		.skipReason = gate,
		.run = [=] {
			OpenChat(*state);
		},
		.until = [=] {
			return LegacyOpened(*state);
		},
		.then = [=] {
			CheckLegacyOpened(*state);
		},
		.timeout = kOpenBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
	AppendControlStage(
		runner,
		state,
		&AuditState::before,
		u"before the first file"_q);
	for (auto i = 0; i != int(state->files.size()); ++i) {
		AppendFileStages(runner, state, i);
	}
	AppendControlStage(
		runner,
		state,
		&AuditState::after,
		u"after the last file"_q);
	runner->add({
		.name = Prefixed(u"totals"_q),
		.run = [=] {
			EmitPendingRows(*state, u"totals"_q);
			EmitTotals(*state, u"totals"_q, true);
		},
	});
	runner->add({
		.name = Prefixed(u"teardown"_q),
		.skipReason = captured,
		.run = [=] {
			state->release = Release(*state, u"teardown"_q);
		},
		.then = [=] {
			CheckRelease(*state);
		},
	});
	runner->add({
		.name = Prefixed(u"settle after the teardown"_q),
		.skipReason = captured,
		.run = [=] {
			StartWait(state->wait);
		},
		.until = [=] {
			return RestoreSettled(*state);
		},
		.then = [=] {
			CheckSettled(*state);
		},
		.timeout = kRestoreBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.details;
		},
	});
}

} // namespace Test

#endif // _DEBUG
