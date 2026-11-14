/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_fixture.h"

#include "base/options.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_widget.h"
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
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "test/test_window_exposure.h"
#include "tgw/tgw_media.h"
#include "tgw/tgw_renderer.h"
#include "ui/widgets/elastic_scroll.h"
#include "window/window_adaptive.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMargins>
#include <QtCore/QPointer>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QUdpSocket>
#include <QtWidgets/QApplication>

#include <algorithm>
#include <memory>
#include <vector>

#include <zlib.h>

#include "styles/style_window.h"

namespace Test {

// The one State of the fixture. The window, its session and the history
// are held weakly and the list as a QPointer, so nothing dangles across
// turns.
struct WidgetFixture {
	// One control connection or datagram. The socket is declared last, so
	// it is destroyed first; CloseProbe() disconnects it before that.
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

	struct FixtureRelease {
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

	// AppendWidgetFixtureOneColumn(): the window and the screen it was on
	// when the placement started, why it was refused, the window and its
	// columns read before it, both moves and whether the window was active
	// before it. A state it left is marked by normalized below.
	struct OneColumnPlacement {
		QPointer<QWidget> window;
		QPointer<QScreen> screen;
		QString refusal;
		QString before;
		std::optional<FixtureWindowMove> target;
		std::optional<FixtureWindowMove> corrected;
		bool activeBefore = false;
	};

	WidgetFixtureArgs args;
	QString networkUnavailable;
	std::vector<std::shared_ptr<WidgetFixtureMessage>> messages;

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
	bool normalized = false;

	// The state the release leaves and its settle requires: the captured
	// one, or the normal one a one-column placement left, which is never
	// maximized again, as that would activate the window.
	Qt::WindowStates finalState;

	QString gate;
	WidgetFixtureWait wait;

	ControlPhase before;
	ControlPhase after;

	OneColumnPlacement oneColumn;

	FixtureRelease release;
	crl::time restoredAt = 0;
	bool optionRestored = false;
	bool chatClosed = false;
	bool exposureRestored = false;
	bool windowRestored = false;
};

namespace {

using Element = HistoryView::Element;
using HtmlWidget = HistoryView::HtmlWidget;
using RuntimeState = Tgw::Runtime::State;
using Way = Window::SectionShow::Way;
using ControlProbe = WidgetFixture::ControlProbe;
using ControlPhase = WidgetFixture::ControlPhase;
using FixtureRelease = WidgetFixture::FixtureRelease;

// The test-only private-member accessor of test_chat_show.cpp, kept
// file-private here: the card rectangle of HtmlWidget, the interactive
// core's cached frame and the card area it was cut for, whether
// Runtime::requestLog() kept its callback, and the raw Summarize() text
// Runtime::applySample() stored with whether it was its host's last. Every
// use only reads.
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

struct RuntimeSummaryTag {
	using type = QString Tgw::Runtime::*;
	friend type Get(RuntimeSummaryTag);
};

template struct PrivateMember<RuntimeSummaryTag, &Tgw::Runtime::_summary>;

struct RuntimeSummaryLastTag {
	using type = bool Tgw::Runtime::*;
	friend type Get(RuntimeSummaryLastTag);
};

template struct PrivateMember<
	RuntimeSummaryLastTag,
	&Tgw::Runtime::_summaryLast>;

// The fixture's own bounded waits. Each ends in a named reading inside its
// stage's until, and the stage timeout is the bound plus
// kDefaultStageTimeout, so a bound is never a stage timeout.
constexpr auto kWindowBound = crl::time(4000);
constexpr auto kOpenBound = crl::time(20000);
constexpr auto kNetworkBound = crl::time(3000);
constexpr auto kRestoreBound = crl::time(6000);

// How a sample must read the same before the chat counts as opened (the
// show helper's own kSettle), and the main window's repaint settle after
// its state is restored (test_chat_show_self_test.cpp's kRepaintSettle).
constexpr auto kSettle = crl::time(400);
constexpr auto kRepaintSettle = crl::time(1500);

[[nodiscard]] QString Prefixed(
		const WidgetFixture &state,
		const QString &text) {
	return state.args.prefix + text;
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString NumberText(qint64 value) {
	return QString::number(value);
}

[[nodiscard]] QString SizeText(QSize size) {
	return u"%1x%2"_q.arg(
		NumberText(size.width()),
		NumberText(size.height()));
}

// One machine-readable row: "NOTE: <marker>: <compact JSON>", every string
// escaped by QJsonDocument.
void Row(const QString &marker, const QJsonObject &fields) {
	Note(marker
		+ u": "_q
		+ QString::fromUtf8(
			QJsonDocument(fields).toJson(QJsonDocument::Compact)));
}

// True once |sample| read the same for kSettle. Only the wait's own
// bookkeeping is written.
[[nodiscard]] bool SampleSettled(
		WidgetFixtureWait &wait,
		const QString &sample) {
	const auto now = crl::now();
	if (sample != wait.sample) {
		wait.sample = sample;
		wait.stableSince = now;
		return false;
	}
	return (now - wait.stableSince) >= kSettle;
}

[[nodiscard]] QString NetworkUnavailable(const WidgetFixtureArgs &args) {
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

[[nodiscard]] ::MainWindow *WindowOf(const WidgetFixture &state) {
	const auto controller = state.controller.get();
	return controller ? controller->widget().get() : nullptr;
}

[[nodiscard]] MainWidget *ContentOf(const WidgetFixture &state) {
	const auto window = WindowOf(state);
	return window ? window->sessionContent() : nullptr;
}

[[nodiscard]] Window::SectionShow Background() {
	return Window::SectionShow(
		Way::ClearStack,
		anim::type::instant,
		anim::activation::background);
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

[[nodiscard]] QJsonArray Strings(const QStringList &list) {
	auto result = QJsonArray();
	for (const auto &entry : list) {
		result.push_back(entry);
	}
	return result;
}

// One fixture-gate Check; a failure gates every later message.
void GateCheck(
		WidgetFixture &state,
		const QString &what,
		const QStringList &missing,
		const QString &details) {
	const auto full = u"fixture gate: "_q + Prefixed(state, what);
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

[[nodiscard]] QString BaselineText(const WidgetFixture &state) {
	const auto history = state.history.get();
	const auto known = history && history->unreadCountKnown();
	const auto fields = QStringList{
		u"chat=%1"_q.arg(NumberText(qint64(state.args.chat.value))),
		u"windowState=%1"_q.arg(WindowStateText(state.windowState)),
		u"hidden=%1"_q.arg(FlagText(state.windowHidden)),
		u"use-new-chat-view=%1"_q.arg(FlagText(state.optionFound)),
		u"baseline={%1}"_q.arg(state.baselineChat),
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
[[nodiscard]] QStringList CaptureBaseline(WidgetFixture &state) {
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
	state.finalState = state.windowState;
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

[[nodiscard]] qint64 WallNow() {
	return QDateTime::currentMSecsSinceEpoch();
}

void StartBaseline(WidgetFixture &state) {
	StartFixtureWait(state.wait);
	auto conflicts = QStringList();
	const auto proxy = ReadProxy(state.args.ports, conflicts);
	Row(state.args.marker + u"_ENVIRONMENT"_q, {
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
		Note(Prefixed(state, u"the primary window was minimized, so "
			u"showNormal() was called; nothing activates it"_q));
	}
}

[[nodiscard]] QString WindowText(const WidgetFixture &state) {
	const auto window = WindowOf(state);
	if (!window) {
		return u"window=gone"_q;
	}
	return u"captured{state=%1 hidden=%2} expected{state=%3 normalized=%4} "
		u"now{state=%5 minimized=%6 hidden=%7}"_q.arg(
			WindowStateText(state.windowState),
			FlagText(state.windowHidden),
			WindowStateText(state.finalState),
			FlagText(state.normalized),
			WindowStateText(window->windowState()),
			FlagText(window->isMinimized()),
			FlagText(window->isHidden()));
}

[[nodiscard]] bool WindowShown(WidgetFixture &state) {
	if (!state.gate.isEmpty()) {
		return true;
	}
	const auto window = WindowOf(state);
	state.wait.details = WindowText(state);
	return (window && !window->isMinimized() && !window->isHidden())
		|| FixtureBoundReached(state.wait, kWindowBound);
}

void CheckWindowShown(WidgetFixture &state) {
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
// window, and the fixture activates nothing at all.
void KeepExposed(WidgetFixture &state) {
	StartFixtureWait(state.wait);
	state.exposure = KeepWindowExposed(WindowOf(state));
	state.exposureKept = true;
	Note(Prefixed(state, u"KeepWindowExposed: "_q)
		+ WindowExposureText(state.exposure));
}

[[nodiscard]] bool WindowExposed(WidgetFixture &state) {
	if (!state.gate.isEmpty() || !state.exposure.refusal.isEmpty()) {
		return true;
	}
	const auto reading = ReadWindowExposure(WindowOf(state));
	state.wait.details = WindowExposureText(reading);
	return reading.exposed || FixtureBoundReached(state.wait, kWindowBound);
}

void CheckWindowExposed(WidgetFixture &state) {
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
void OpenChat(WidgetFixture &state) {
	StartFixtureWait(state.wait);
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

// The HistoryInner of the legacy HistoryWidget that shows the chat: its
// scroll's owned widget there and, when the history has views, the list
// itemTop() places its last one in.
[[nodiscard]] HistoryInner *ResolveLegacyInner(const WidgetFixture &state) {
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
			if (FixtureListScroll(inner)
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
	const auto scroll = FixtureListScroll(list);
	return u"list=%1 scrollHeight=%2"_q.arg(
		WidgetDescription(list),
		scroll ? NumberText(scroll->height()) : u"none"_q);
}

[[nodiscard]] bool LegacyOpened(WidgetFixture &state) {
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
		|| FixtureBoundReached(state.wait, kOpenBound);
}

void CheckLegacyOpened(WidgetFixture &state) {
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
// The one-column window.

// How long a placed window's reading must hold before it counts as
// settled.
constexpr auto kPlaceStable = crl::time(1000);
// The placed window's height cap in logical px: a harness constant like
// kWidgetWindowMargin, not a product style. The width is not one: it comes
// from the column styles at runtime.
constexpr auto kWindowMaxHeight = 1000;

[[nodiscard]] QString ColumnsName(const Window::Adaptive &adaptive) {
	return adaptive.isOneColumn()
		? u"one-column"_q
		: adaptive.isThreeColumn()
		? u"three-column"_q
		: u"normal"_q;
}

// The frame kWidgetWindowMargin inside the screen's available area, at
// most kWindowMaxHeight tall, as wide as a bodyWidget() one logical px
// below the one-column threshold needs, with the filters sidebar's width
// when it is shown and the window's own margins beside its body, never
// below its minimum width. From the window's readings now, so a second
// call after a move between screens of other scales corrects the first.
[[nodiscard]] FixtureWindowTarget ComputeTarget(
		not_null<QWidget*> window,
		not_null<QScreen*> screen,
		const FixtureColumns &layout) {
	auto result = FixtureWindowTarget();
	if (!layout.error.isEmpty()) {
		result.error = layout.error;
		return result;
	}
	result.bodyWidget = layout.threshold - 1 + layout.filters;
	const auto client = window->geometry();
	const auto frame = window->frameGeometry();
	const auto margins = QMargins(
		client.left() - frame.left(),
		client.top() - frame.top(),
		frame.right() - client.right(),
		frame.bottom() - client.bottom());
	const auto width = std::max(
		result.bodyWidget + (client.width() - layout.bodyWidget),
		layout.minimumWidth);
	const auto inner = screen->availableGeometry().marginsRemoved({
		kWidgetWindowMargin,
		kWidgetWindowMargin,
		kWidgetWindowMargin,
		kWidgetWindowMargin,
	});
	result.frame = QRect(
		inner.topLeft(),
		QSize(
			width + margins.left() + margins.right(),
			std::min(kWindowMaxHeight, inner.height())));
	result.client = result.frame.marginsRemoved(margins);
	if (result.frame.width() > inner.width()) {
		result.error = u"the one-column frame %1 does not fit in the "
			u"screen's available area %2 inside its margin"_q.arg(
				RectText(result.frame),
				RectText(inner));
	}
	return result;
}

// The settle's own sample: the window and the columns it shows.
[[nodiscard]] QString PlacementReading(
		const WidgetFixture &state,
		not_null<QWidget*> window) {
	return FixtureWindowText(window)
		+ u" "_q
		+ FixtureColumnsText(ReadFixtureColumns(state, window));
}

// Every reading of the placement, for both of its gates' details.
[[nodiscard]] QString OneColumnText(const WidgetFixture &state) {
	const auto &place = state.oneColumn;
	const auto moveText = [](const std::optional<FixtureWindowMove> &move) {
		if (!move) {
			return u"none"_q;
		}
		const auto &target = move->target;
		return u"frame=%1 client=%2 bodyWidget=%3 moved=%4 error=[%5] %6"_q
			.arg(
				RectText(target.frame),
				RectText(target.client),
				NumberText(target.bodyWidget),
				FlagText(move->moved),
				target.error,
				FixtureColumnsText(move->columns));
	};
	const auto window = place.window.data();
	return QStringList{
		u"before={%1}"_q.arg(place.before),
		u"target={%1}"_q.arg(moveText(place.target)),
		u"corrected={%1}"_q.arg(moveText(place.corrected)),
		u"after={%1}"_q.arg(window
			? PlacementReading(state, window)
			: u"window=gone"_q),
		u"window={%1}"_q.arg(WindowText(state)),
		u"shownNormal=%1"_q.arg(FlagText(state.shownNormal)),
		u"active=%1->%2"_q.arg(
			FlagText(place.activeBefore),
			(window ? FlagText(window->isActiveWindow()) : u"gone"_q)),
	}.join(QChar(' '));
}

// A refused target ends the placement: the window keeps the geometry it
// has, and the gate after the wait names the refusal.
void MoveOneColumn(
		WidgetFixture &state,
		std::optional<FixtureWindowMove> &move) {
	auto &place = state.oneColumn;
	const auto window = place.window.data();
	const auto screen = place.screen.data();
	if (!place.refusal.isEmpty() || !window || !screen) {
		return;
	}
	move = MoveFixtureWindow(state, window, screen);
	if (!move->target.error.isEmpty()) {
		place.refusal = u"the one-column target is refused: "_q
			+ move->target.error;
	}
}

// The window's own screen, never another one, so its scale stays the one
// the caller found. A maximized or full-screen window cannot take the
// target geometry: its state is cleared without Qt::WindowActive, which
// QWidget::setWindowState would answer with activateWindow(), and it stays
// normal: on Windows maximizing it again at the release would activate it.
void StartOneColumn(WidgetFixture &state) {
	StartFixtureWait(state.wait);
	auto &place = state.oneColumn;
	const auto list = state.list.data();
	const auto window = list ? list->window() : nullptr;
	const auto handle = window ? window->windowHandle() : nullptr;
	const auto screen = handle ? handle->screen() : nullptr;
	place.window = window;
	place.screen = screen;
	if (!window) {
		place.refusal = u"no window shows the chat's list"_q;
		return;
	} else if (!screen) {
		place.refusal = u"the window is on no screen"_q;
		return;
	}
	place.before = PlacementReading(state, window);
	place.activeBefore = window->isActiveWindow();
	const auto states = Qt::WindowMaximized | Qt::WindowFullScreen;
	const auto current = WithoutActive(window->windowState());
	if (current & states) {
		state.normalized = true;
		state.finalState = current & ~states;
		window->setWindowState(current & ~states);
		Note(Prefixed(state, u"the primary window was maximized or full "
			u"screen, so it left that state for the one-column size and "
			u"stays normal; the release does not maximize it again"_q));
	}
	MoveOneColumn(state, place.target);
}

// The settled window's frame margins and its filters sidebar, shown only
// in a wide enough window, can ask for another width than the first move
// read: the same move again, from the settled geometry.
void CorrectOneColumn(WidgetFixture &state) {
	StartFixtureWait(state.wait);
	MoveOneColumn(state, state.oneColumn.corrected);
}

[[nodiscard]] bool OneColumnSettled(WidgetFixture &state) {
	const auto &place = state.oneColumn;
	const auto window = place.window.data();
	const auto screen = place.screen.data();
	return !place.refusal.isEmpty()
		|| !window
		|| !screen
		|| FixtureWindowSettled(state, state.wait, window, screen);
}

[[nodiscard]] QStringList OneColumnMissing(const WidgetFixture &state) {
	const auto &place = state.oneColumn;
	if (!place.refusal.isEmpty()) {
		return { place.refusal };
	} else if (!place.window || !place.screen) {
		return { u"the window or the screen it started on is gone"_q };
	}
	return {};
}

// The first move's prerequisites and target; the layout is judged after
// the second.
void CheckOneColumnMoved(WidgetFixture &state) {
	GateCheck(
		state,
		u"the window is moved for the one-column layout on its own "
		u"screen"_q,
		OneColumnMissing(state),
		OneColumnText(state));
}

void CheckOneColumn(WidgetFixture &state) {
	auto missing = OneColumnMissing(state);
	const auto window = state.oneColumn.window.data();
	if (missing.isEmpty()
		&& !OnlyFixtureChatShown(ReadFixtureColumns(state, window))) {
		missing.push_back(u"the window does not show only the chat in one "
			u"column"_q);
	}
	GateCheck(
		state,
		u"only the chat is shown, in the one-column layout"_q,
		missing,
		OneColumnText(state));
}

// ---------------------------------------------------------------------
// One message.

[[nodiscard]] QString IdText(FullMsgId id) {
	return id
		? u"%1:%2"_q.arg(
			NumberText(qint64(id.peer.value)),
			NumberText(qint64(id.msg.bare)))
		: u"none"_q;
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

// Each set flag, and each present field even when it is empty or zero.
[[nodiscard]] MTPDmessageMediaWidget::Flags MediaFlags(
		const WidgetMessageFields &fields) {
	using Flag = MTPDmessageMediaWidget::Flag;
	const auto &flags = fields.flags;
	const auto &state = fields.state;
	return (flags.isCopy ? Flag::f_is_copy : Flag())
		| (flags.canQuery ? Flag::f_can_query : Flag())
		| (flags.backgroundQuery ? Flag::f_background_query : Flag())
		| (state.publicData ? Flag::f_public_data : Flag())
		| (state.chatData ? Flag::f_chat_data : Flag())
		| (fields.widgetId ? Flag::f_widget_id : Flag());
}

[[nodiscard]] WidgetMessageFields MediaFields(
		not_null<const Tgw::WidgetMedia*> media) {
	const auto &fields = media->fields();
	return {
		.flags = fields.flags,
		.state = fields.state,
		.widgetId = fields.widgetId,
	};
}

// Absent and present-empty bytes differ, and so do an absent and a zero id.
[[nodiscard]] bool SameFields(
		const WidgetMessageFields &a,
		const WidgetMessageFields &b) {
	return (a.flags == b.flags)
		&& (a.state == b.state)
		&& (a.widgetId == b.widgetId);
}

[[nodiscard]] QString BytesText(const std::optional<QByteArray> &value) {
	return value ? NumberText(value->size()) : u"none"_q;
}

[[nodiscard]] UserId ViaBotId(not_null<HistoryItem*> item) {
	const auto bot = item->viaBot();
	return bot ? peerToUser(bot->id) : UserId();
}

[[nodiscard]] QString UserIdText(UserId id) {
	return id ? NumberText(qint64(id.bare)) : u"none"_q;
}

// Sizes, never the state's contents.
[[nodiscard]] QString FieldsText(const WidgetMessageFields &fields) {
	auto flags = QStringList();
	if (fields.flags.isCopy) {
		flags.push_back(u"copy"_q);
	}
	if (fields.flags.canQuery) {
		flags.push_back(u"query"_q);
	}
	if (fields.flags.backgroundQuery) {
		flags.push_back(u"background"_q);
	}
	return u"flags=%1 chat=%2 public=%3 widgetId=%4"_q.arg(
		(flags.isEmpty() ? u"none"_q : flags.join(u"|"_q)),
		BytesText(fields.state.chatData),
		BytesText(fields.state.publicData),
		(fields.widgetId
			? QString::number(*fields.widgetId)
			: u"none"_q));
}

[[nodiscard]] bool Registered(
		const WidgetFixture &state,
		const WidgetFixtureMessage &message) {
	return ranges::any_of(state.messages, [&](const auto &entry) {
		return (entry.get() == &message);
	});
}

// What an injection needs before it adds anything.
[[nodiscard]] QStringList InjectionMissing(
		const WidgetFixture &state,
		const WidgetFixtureMessage &message) {
	const auto history = state.history.get();
	const auto inner = state.list.data();
	auto missing = QStringList();
	if (!Registered(state, message)) {
		missing.push_back(u"the message record is not registered with the "
			u"fixture"_q);
	}
	if (!history || !history->loadedAtBottom()) {
		missing.push_back(u"the history is gone or not loaded at its "
			u"bottom"_q);
	}
	if (!inner || !inner->isVisible()) {
		missing.push_back(u"the HistoryInner is not shown"_q);
	}
	return missing;
}

// An outgoing local client-id message, the shape the product's streamed
// drafts add (history_streamed_drafts.cpp): not isRegular(), and never
// sent.
[[nodiscard]] HistoryItemCommonFields LocalFields(
		not_null<History*> history,
		MsgId id) {
	return {
		.id = id,
		.flags = (MessageFlag::Local
			| MessageFlag::Outgoing
			| MessageFlag::HasFromId),
		.from = history->session().userPeerId(),
		.date = base::unixtime::now(),
	};
}

// The read-only part of the runtime log: the raw Summarize() text the
// runtime holds now, as |reading| says it was read.
void ReadSummary(
		WidgetRuntimeLog &log,
		const Tgw::Runtime *runtime,
		WidgetLogSummary reading) {
	log.summary = runtime ? (runtime->*Get(RuntimeSummaryTag())) : QString();
	log.summaryState = log.summary.isEmpty()
		? WidgetLogSummary::Absent
		: reading;
}

// ---------------------------------------------------------------------
// The network controls.

[[nodiscard]] QByteArray ControlLine(
		const WidgetFixture &state,
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

void StartControls(WidgetFixture &state, ControlPhase &phase) {
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

void ReportControls(const WidgetFixture &state, ControlPhase &phase) {
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
		Row(state.args.marker + u"_NET_CONTROL"_q, {
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
			Prefixed(state, u"network control %1 - the %2 control to "
				u"127.0.0.1:%3 was %4 by this process"_q.arg(
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
// Release and settle.

void RestoreChat(WidgetFixture &state) {
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

[[nodiscard]] bool WindowMatches(const WidgetFixture &state) {
	const auto window = WindowOf(state);
	return window
		&& (WithoutActive(window->windowState()) == state.finalState)
		&& (window->isHidden() == state.windowHidden);
}

// The tap and the host watch, both armed by the kept key.
void DisarmMessage(WidgetFixtureMessage &message) {
	if (!message.armed) {
		return;
	}
	DisarmWidgetOpsTap(message.key);
	if (message.host) {
		DisarmWidgetHostWatch(message.key);
	}
	message.armed = false;
}

// The one release, run by the teardown stage and again by the caller's
// Runner::onFinish. Idempotent: a step already done is only read again.
[[nodiscard]] FixtureRelease Release(
		WidgetFixture &state,
		const QString &path) {
	auto result = FixtureRelease{ .path = path };
	for (const auto &entry : state.messages) {
		auto &message = *entry;
		if (message.armed) {
			DisarmMessage(message);
			++result.disarmed;
		}
		if (const auto item = WidgetFixtureItem(state, message.id)) {
			item->destroy();
			++result.destroyed;
			if (!message.removed) {
				message.removed = true;
				message.removedAt = crl::now();
			}
		}
	}
	CloseControls(state.before);
	CloseControls(state.after);
	auto left = 0;
	for (const auto &message : state.messages) {
		if (WidgetFixtureItem(state, message->id)) {
			++left;
		}
	}
	result.itemsGone = !left;
	result.items = u"files=%1 destroyed=%2 disarmed=%3 left=%4"_q.arg(
		NumberText(int(state.messages.size())),
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
		DeliverListLeave(list);
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
	// Only a minimized window's showNormal() is undone, to finalState: a
	// normalized window stays normal, as maximizing it would activate it.
	const auto window = WindowOf(state);
	if (state.shownNormal && !state.windowRestored && window) {
		state.windowRestored = true;
		state.restoredAt = crl::now();
		window->setWindowState(state.finalState);
		result.done.push_back(u"window state restored"_q);
	}
	result.window = WindowText(state);
	return result;
}

[[nodiscard]] QString ReleaseText(const FixtureRelease &release) {
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

void CheckRelease(const WidgetFixture &state) {
	const auto &r = state.release;
	const auto prefix = Prefixed(state, u"teardown - "_q);
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

[[nodiscard]] QString SettleText(const WidgetFixture &state) {
	const auto exposure = ReadWindowExposure(WindowOf(state));
	return u"%1 restored=%2 stayOnTop{found=%3 now=%4}"_q.arg(
		WindowText(state),
		FlagText(state.windowRestored),
		FlagText(state.exposure.stayOnTop),
		FlagText(exposure.stayOnTop));
}

[[nodiscard]] bool RestoreSettled(WidgetFixture &state) {
	const auto since = state.restoredAt
		? state.restoredAt
		: state.wait.started;
	state.wait.details = SettleText(state);
	return (((crl::now() - since) >= kRepaintSettle) && WindowMatches(state))
		|| FixtureBoundReached(state.wait, kRestoreBound);
}

void CheckSettled(const WidgetFixture &state) {
	const auto prefix = Prefixed(state, u"settle - "_q);
	const auto details = SettleText(state);
	Check(
		WindowMatches(state),
		prefix + u"the main window's state is the expected one (the captured "
			u"one, or normal after the one-column placement) and held it "
			u"through the repaint settle"_q,
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

void AppendControlStage(
		not_null<Runner*> runner,
		const std::shared_ptr<WidgetFixture> &state,
		ControlPhase WidgetFixture::*member,
		const QString &title) {
	runner->add({
		.name = Prefixed(*state, u"network control "_q + title),
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
			ReportControls(*state, (*state).*member);
		},
		.timeout = kNetworkBound + kDefaultStageTimeout,
	});
}

} // namespace

std::shared_ptr<WidgetFixture> MakeWidgetFixture(WidgetFixtureArgs args) {
	const auto result = std::make_shared<WidgetFixture>();
	result->args = std::move(args);
	result->before.name = u"before"_q;
	result->after.name = u"after"_q;
	result->networkUnavailable = NetworkUnavailable(result->args);
	return result;
}

void AppendWidgetFixtureOpen(
		not_null<Runner*> runner,
		const std::shared_ptr<WidgetFixture> &state) {
	const auto gate = [=] {
		return state->gate;
	};
	runner->add({
		.name = Prefixed(*state, u"capture the baseline and show the window "
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
		.name = Prefixed(*state, u"keep the window exposed"_q),
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
		.name = Prefixed(*state, u"open the chat in the legacy "
			u"HistoryWidget"_q),
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
}

void AppendWidgetFixtureOneColumn(
		not_null<Runner*> runner,
		const std::shared_ptr<WidgetFixture> &state) {
	const auto gate = [=] {
		return state->gate;
	};
	runner->add({
		.name = Prefixed(*state, u"move the window for the one-column "
			u"layout"_q),
		.skipReason = gate,
		.run = [=] {
			StartOneColumn(*state);
		},
		.until = [=] {
			return OneColumnSettled(*state);
		},
		.then = [=] {
			CheckOneColumnMoved(*state);
		},
		.timeout = kWidgetPlaceBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.sample;
		},
	});
	runner->add({
		.name = Prefixed(*state, u"size the window for the one-column "
			u"layout"_q),
		.skipReason = gate,
		.run = [=] {
			CorrectOneColumn(*state);
		},
		.until = [=] {
			return OneColumnSettled(*state);
		},
		.then = [=] {
			CheckOneColumn(*state);
		},
		.timeout = kWidgetPlaceBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->wait.sample;
		},
	});
}

void AppendWidgetFixtureControls(
		not_null<Runner*> runner,
		const std::shared_ptr<WidgetFixture> &fixture,
		WidgetFixtureControls phase,
		const QString &title) {
	AppendControlStage(
		runner,
		fixture,
		((phase == WidgetFixtureControls::Before)
			? &WidgetFixture::before
			: &WidgetFixture::after),
		title);
}

void AppendWidgetFixtureClose(
		not_null<Runner*> runner,
		const std::shared_ptr<WidgetFixture> &state) {
	const auto captured = [=] {
		return state->captured
			? QString()
			: u"nothing was captured, so nothing was changed"_q;
	};
	runner->add({
		.name = Prefixed(*state, u"teardown"_q),
		.skipReason = captured,
		.run = [=] {
			state->release = Release(*state, u"teardown"_q);
		},
		.then = [=] {
			CheckRelease(*state);
		},
	});
	runner->add({
		.name = Prefixed(*state, u"settle after the teardown"_q),
		.skipReason = captured,
		.run = [=] {
			StartFixtureWait(state->wait);
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

QString ReleaseWidgetFixture(WidgetFixture &fixture, const QString &path) {
	return ReleaseText(Release(fixture, path));
}

QString WidgetFixtureGate(const WidgetFixture &fixture) {
	return fixture.gate;
}

QString WidgetFixtureNetwork(const WidgetFixture &fixture) {
	return fixture.networkUnavailable;
}

Main::Session *WidgetFixtureSession(const WidgetFixture &fixture) {
	return fixture.session.get();
}

History *WidgetFixtureHistory(const WidgetFixture &fixture) {
	return fixture.history.get();
}

QWidget *WidgetFixtureList(const WidgetFixture &fixture) {
	return fixture.list.data();
}

HistoryItem *WidgetFixtureItem(const WidgetFixture &state, FullMsgId id) {
	const auto session = state.session.get();
	return (session && id) ? session->data().message(id) : nullptr;
}

bool WidgetFixtureOwns(const WidgetFixture &fixture, FullMsgId id) {
	return id && ranges::any_of(fixture.messages, [&](const auto &entry) {
		return (entry->id == id);
	});
}

std::shared_ptr<WidgetFixtureMessage> AddWidgetFixtureMessage(
		WidgetFixture &fixture,
		const QString &name) {
	auto result = std::make_shared<WidgetFixtureMessage>();
	result->name = name;
	fixture.messages.push_back(result);
	return result;
}

QString InjectWidgetMessage(
		WidgetFixture &state,
		WidgetFixtureMessage &message,
		const QByteArray &html,
		int width,
		int height,
		const WidgetMessageFields &fields) {
	StartFixtureWait(message.wait);
	message.html = html;
	message.fields = fields;
	const auto history = state.history.get();
	const auto payload = Zlib(message.html);
	auto missing = InjectionMissing(state, message);
	if (payload.isEmpty() || (Tgw::UnpackPayload(payload) != message.html)) {
		missing.push_back(u"the zlib payload does not unpack to the file's "
			u"bytes"_q);
	}
	if (!missing.isEmpty()) {
		return u"not injected: "_q + missing.join(u"; "_q);
	}
	message.payloadBytes = int(payload.size());
	message.id = FullMsgId(
		history->peer->id,
		history->owner().nextLocalMessageId());
	message.key = GlobalMsgId{ message.id, history->session().uniqueId() };
	message.tap = ArmWidgetOpsTap(message.key);
	if (message.watchHost) {
		message.host = ArmWidgetHostWatch(message.key, *message.watchHost);
	}
	message.armed = true;
	auto common = LocalFields(history, message.id.msg);
	common.viaBotId = fields.viaBot;
	const auto item = history->addNewLocalMessage(
		std::move(common),
		tr::marked(message.caption),
		MTP_messageMediaWidget(
			MTP_flags(MediaFlags(fields)),
			MTP_int(width),
			MTP_int(height),
			MTP_bytes(payload),
			MTP_bytes(fields.state.publicData.value_or(QByteArray())),
			MTP_long(0),
			MTP_bytes(fields.state.chatData.value_or(QByteArray())),
			MTP_long(fields.widgetId.value_or(0))));
	const auto got = item->globalId();
	auto result = QString();
	if (got != message.key) {
		result = u"the injected message got id %1%2, not the armed "
			u"%3"_q.arg(
				IdText(got.itemId),
				((got.sessionUniqueId == message.key.sessionUniqueId)
					? QString()
					: u" in another session"_q),
				IdText(message.id));
	}
	ProcessPendingLayout(history);
	return result;
}

QString InjectTextMessage(
		WidgetFixture &state,
		WidgetFixtureMessage &message,
		const QString &text) {
	StartFixtureWait(message.wait);
	const auto history = state.history.get();
	auto missing = InjectionMissing(state, message);
	if (text.isEmpty()) {
		missing.push_back(u"the text is empty"_q);
	}
	if (!missing.isEmpty()) {
		return u"not injected: "_q + missing.join(u"; "_q);
	}
	message.id = FullMsgId(
		history->peer->id,
		history->owner().nextLocalMessageId());
	const auto item = history->addNewLocalMessage(
		LocalFields(history, message.id.msg),
		tr::marked(text),
		MTP_messageMediaEmpty());
	const auto got = item->fullId();
	message.shape = u"id=%1 local=%2 text=%3"_q.arg(
		IdText(got),
		FlagText(LocalShape(item)),
		NumberText(text.size()));
	ProcessPendingLayout(history);
	return (got == message.id)
		? QString()
		: u"the injected text message got id %1, not %2"_q.arg(
			IdText(got),
			IdText(message.id));
}

bool WidgetMessageLaidOut(
		const WidgetFixture &state,
		WidgetFixtureMessage &message) {
	const auto history = state.history.get();
	const auto view = FixtureOwnView(state, message.id);
	const auto card = view ? WidgetCardLocal(view) : QRect();
	const auto pending = !history
		|| history->hasPendingResizedItems()
		|| (view && view->pendingResize());
	message.wait.details = u"view=%1 card=%2 pending=%3"_q.arg(
		FlagText(view != nullptr),
		RectText(card),
		FlagText(pending));
	return !WidgetFixtureItem(state, message.id)
		|| (view && !card.isEmpty() && !pending)
		|| FixtureBoundReached(message.wait, kWidgetLayoutBound);
}

QString CheckWidgetMessage(
		const WidgetFixture &state,
		WidgetFixtureMessage &message) {
	const auto item = WidgetFixtureItem(state, message.id);
	const auto media = item
		? dynamic_cast<Tgw::WidgetMedia*>(item->media())
		: nullptr;
	const auto view = FixtureOwnView(state, message.id);
	const auto card = view ? WidgetCardLocal(view) : QRect();
	auto missing = QStringList();
	if (!item || !LocalShape(item)) {
		missing.push_back(u"the message is not an outgoing local client-id "
			u"message"_q);
	}
	if (!media || (media->html() != message.html)) {
		missing.push_back(u"its media is not a Tgw::WidgetMedia holding the "
			u"file's bytes"_q);
	}
	if (media) {
		const auto fields = MediaFields(media);
		if (!SameFields(fields, message.fields)) {
			missing.push_back(u"its media's flags, state or widget id are "
				u"not the injected ones (the media has %1)"_q.arg(
					FieldsText(fields)));
		}
	}
	if (item) {
		const auto viaBot = ViaBotId(item);
		if (viaBot != message.fields.viaBot) {
			missing.push_back(u"it comes via bot %1, not the injected "
				u"%2"_q.arg(
					UserIdText(viaBot),
					UserIdText(message.fields.viaBot)));
		}
	}
	if (!view || card.isEmpty()) {
		missing.push_back(u"the list shows no laid-out HtmlWidget card of "
			u"its own for it"_q);
	}
	message.shape = u"id=%1 local=%2 regular=%3 out=%4 clientMsgId=%5 "
		u"widgetMedia=%6 htmlMatches=%7 payload=%8 %9 %10"_q.arg(
			IdText(message.id),
			FlagText(item && item->isLocal()),
			FlagText(item && item->isRegular()),
			FlagText(item && item->out()),
			FlagText(item && IsClientMsgId(item->id)),
			FlagText(media != nullptr),
			FlagText(media && (media->html() == message.html)),
			NumberText(message.payloadBytes),
			FieldsText(message.fields),
			message.wait.details);
	return missing.isEmpty()
		? QString()
		: (u"no HtmlWidget view: "_q + missing.join(u"; "_q));
}

void HideFixtureLayers(
		WidgetFixture &state,
		WidgetFixtureMessage &message,
		const QString &when) {
	const auto controller = state.controller.get();
	if (!controller || !controller->isLayerShown()) {
		return;
	}
	controller->hideSettingsAndLayer(anim::type::instant);
	message.layers.push_back(when);
	Note(Prefixed(state, u"%1 - a layer was shown %2; it was hidden at once "
		u"and nothing in it was confirmed"_q.arg(message.name, when)));
}

void RemoveFixtureMessage(
		WidgetFixture &state,
		WidgetFixtureMessage &message) {
	HideFixtureLayers(state, message, u"before the removal"_q);
	DisarmMessage(message);
	if (const auto item = WidgetFixtureItem(state, message.id)) {
		item->destroy();
	}
	message.removed = true;
	message.removedAt = crl::now();
}

bool WidgetTapFinished(const WidgetOpsReading &reading) {
	return (reading.factoriesReleased == reading.factoriesSelected)
		&& (reading.wrappersDestroyed == reading.wrappersBuilt);
}

bool FixtureBarrierDecided(const WidgetFixtureMessage &message) {
	return !message.tap
		|| WidgetTapFinished(ReadWidgetOps(*message.tap))
		|| (crl::now() - message.removedAt >= kWidgetBarrierBound);
}

void RequestRuntimeLog(
		const WidgetFixture &state,
		const std::shared_ptr<WidgetFixtureMessage> &shared) {
	auto &message = *shared;
	StartFixtureWait(message.wait);
	const auto runtime = FixtureOwnRuntime(state, message.id);
	if (!runtime) {
		return;
	}
	const auto weak = std::weak_ptr<WidgetFixtureMessage>(shared);
	const auto owner = base::make_weak(runtime);
	runtime->requestLog([=](const QString &text) {
		if (const auto strong = weak.lock()) {
			auto &entry = strong->log;
			if (!entry.received) {
				entry.text = text;
				entry.source = u"requestLog"_q;
				entry.received = true;
				ReadSummary(entry, owner.get(), WidgetLogSummary::Fresh);
			}
		}
	});
	message.log.pending = bool(runtime->*Get(RuntimeLogDoneTag()));
	if (!message.log.pending) {
		// A retired host whose final sample is applied calls nothing back:
		// its logText() already holds the "Last sandbox:" block.
		message.log.text = runtime->logText();
		message.log.source = u"logText"_q;
		message.log.received = true;
		ReadSummary(
			message.log,
			runtime,
			((runtime->*Get(RuntimeSummaryLastTag()))
				? WidgetLogSummary::Final
				: WidgetLogSummary::Stale));
	}
}

bool RuntimeLogDecided(
		const WidgetFixture &state,
		const WidgetFixtureMessage &message) {
	if (message.log.received) {
		return true;
	}
	const auto runtime = FixtureOwnRuntime(state, message.id);
	return !runtime
		|| !(runtime->*Get(RuntimeLogDoneTag()))
		|| FixtureBoundReached(message.wait, kWidgetLogBound);
}

void FinishRuntimeLog(
		const WidgetFixture &state,
		WidgetFixtureMessage &message) {
	auto &log = message.log;
	if (!log.received) {
		if (const auto runtime = FixtureOwnRuntime(state, message.id)) {
			log.text = runtime->logText();
			log.source = u"logText-at-bound"_q;
			ReadSummary(log, runtime, WidgetLogSummary::Stale);
		} else {
			log.source = u"unavailable"_q;
			ReadSummary(log, nullptr, WidgetLogSummary::Stale);
		}
	}
	log.chars = int(log.text.size());
}

// The list's own view of the message: found through the history's blocks,
// accepted only when the HistoryInner's public itemTop() places it.
Element *FixtureOwnView(const WidgetFixture &state, FullMsgId id) {
	const auto inner = dynamic_cast<HistoryInner*>(state.list.data());
	const auto item = WidgetFixtureItem(state, id);
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

Tgw::Runtime *FixtureOwnRuntime(const WidgetFixture &state, FullMsgId id) {
	const auto view = FixtureOwnView(state, id);
	return view ? Tgw::FindRuntime(view) : nullptr;
}

// Runtime::core(), never interactive(): a stopped runtime has no
// interactive core, and its still keeps the watched core's address.
QString WidgetDesktopStop(
		const WidgetFixture &state,
		FullMsgId id,
		quintptr core,
		int fatalFrozen) {
	const auto runtime = FixtureOwnRuntime(state, id);
	const auto current = runtime ? runtime->core() : nullptr;
	if (!core
		|| (fatalFrozen > 0)
		|| !current
		|| (reinterpret_cast<quintptr>(current) != core)
		|| (runtime->state() != RuntimeState::Stopped)
		|| !current->stopFatal()) {
		return QString();
	}
	return current->stopReason();
}

QRect WidgetCardLocal(not_null<const Element*> view) {
	const auto html = dynamic_cast<const HtmlWidget*>(view->media());
	return html
		? (html->*Get(HtmlCardRectTag()))().translated(view->mediaTopLeft())
		: QRect();
}

// Only reads: the media, its runtime and current interactive core, and the
// core's cached frame. Runs in the show helper's samples.
WidgetCardReading ReadWidgetCard(not_null<const Element*> view) {
	auto result = WidgetCardReading();
	result.view = reinterpret_cast<quintptr>(view.get());
	const auto html = dynamic_cast<const HtmlWidget*>(view->media());
	if (!html) {
		return result;
	}
	result.html = true;
	result.card = WidgetCardLocal(view);
	const auto runtime = Tgw::FindRuntime(view);
	if (!runtime) {
		return result;
	}
	result.runtime = true;
	result.state = RuntimeStateName(runtime->state());
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
bool WidgetCardReady(const WidgetCardReading &card) {
	return card.html
		&& card.live
		&& card.interactive
		&& card.attached
		&& card.accepts
		&& card.frameAtCard;
}

QString WidgetCardText(const WidgetCardReading &card) {
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

// The accepted show, view, core and geometry, read again right before the
// press; an empty answer means they are all still the ones accepted.
QString RevalidateWidgetCard(
		const WidgetFixture &state,
		FullMsgId id,
		const QString &purpose,
		const ChatListShow &shown,
		const WidgetCardReading &accepted) {
	const auto list = state.list.data();
	const auto inner = dynamic_cast<HistoryInner*>(list);
	if (!shown.shown()) {
		return u"the %1 show refused: %2 - %3"_q.arg(
			purpose,
			ChatListShowRefusalName(shown.refusal),
			shown.reason);
	} else if (!inner || (shown.list.data() != list)) {
		return u"the accepted list is gone or is another list"_q;
	} else if (!shown.hovered) {
		return u"the show left no hover at the press point"_q;
	}
	const auto view = FixtureOwnView(state, id);
	if (!view) {
		return u"the list no longer shows a view of its own for the "
			u"message"_q;
	} else if (reinterpret_cast<quintptr>(view) != accepted.view) {
		return u"the list's view of the message is not the accepted one"_q;
	}
	const auto now = ReadWidgetCard(view);
	if (!WidgetCardReady(now)) {
		return u"the card is no longer ready: "_q + WidgetCardText(now);
	} else if (now.core != accepted.core) {
		return u"the interactive core is not the accepted one"_q;
	} else if (now.card != accepted.card) {
		return u"the card moved or resized in its view: accepted=%1 now=%2"_q
			.arg(RectText(accepted.card), RectText(now.card));
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

// The Ui::ElasticScroll that owns |list| directly, as every chat section
// builds it, or null.
Ui::ElasticScroll *FixtureListScroll(QWidget *list) {
	const auto scroll = list
		? dynamic_cast<Ui::ElasticScroll*>(list->parentWidget())
		: nullptr;
	return (scroll && (scroll->widget() == list)) ? scroll : nullptr;
}

void DeliverListLeave(QWidget *list) {
	if (!list) {
		return;
	}
	auto leave = QEvent(QEvent::Leave);
	Settle([&] {
		QApplication::sendEvent(list, &leave);
	});
}

FixtureColumns ReadFixtureColumns(
		const WidgetFixture &fixture,
		QWidget *window) {
	auto result = FixtureColumns{
		.threshold = st::columnMinimalWidthLeft + st::columnMinimalWidthMain,
	};
	const auto primary = dynamic_cast<::MainWindow*>(window);
	const auto controller = primary ? primary->sessionController() : nullptr;
	const auto body = primary ? primary->bodyWidget() : nullptr;
	const auto content = primary ? primary->sessionContent() : nullptr;
	if (!controller || !body || !content) {
		result.error = u"the primary window has no session controller, body "
			u"or content"_q;
		return result;
	}
	result.columns = ColumnsName(controller->adaptive());
	result.bodyWidget = body->width();
	result.filters = controller->filtersWidth();
	result.body = controller->computeColumnLayout().bodyWidth;
	result.content = content->width();
	result.minimumWidth = primary->minimumWidth();
	for (const auto dialogs : FindAll<Dialogs::Widget>(content)) {
		if (dialogs->isVisible()) {
			++result.dialogsShown;
		}
	}
	const auto history = fixture.history.get();
	for (const auto widget : FindAll<HistoryWidget>(content)) {
		if (history && (widget->history() == history) && widget->isVisible()) {
			result.chatShown = true;
			result.chat = widget->width();
		}
	}
	return result;
}

bool OnlyFixtureChatShown(const FixtureColumns &layout) {
	return layout.error.isEmpty()
		&& (layout.columns == u"one-column"_q)
		&& (layout.body < layout.threshold)
		&& !layout.dialogsShown
		&& layout.chatShown
		&& (layout.chat == layout.content);
}

QString FixtureColumnsText(const FixtureColumns &layout) {
	if (!layout.error.isEmpty()) {
		return u"layout: "_q + layout.error;
	}
	return QStringList{
		u"columns="_q + layout.columns,
		u"body=%1"_q.arg(layout.body),
		u"threshold=%1"_q.arg(layout.threshold),
		u"bodyWidget=%1"_q.arg(layout.bodyWidget),
		u"filters=%1"_q.arg(layout.filters),
		u"content=%1"_q.arg(layout.content),
		u"chat=%1"_q.arg(layout.chat),
		u"chatShown="_q + FlagText(layout.chatShown),
		u"dialogsShown=%1"_q.arg(layout.dialogsShown),
		u"minimumWidth=%1"_q.arg(layout.minimumWidth),
	}.join(QChar(' '));
}

FixtureWindowMove MoveFixtureWindow(
		const WidgetFixture &fixture,
		not_null<QWidget*> window,
		not_null<QScreen*> screen) {
	auto result = FixtureWindowMove{
		.columns = ReadFixtureColumns(fixture, window),
	};
	result.target = ComputeTarget(window, screen, result.columns);
	result.moved = result.target.error.isEmpty()
		&& (window->geometry() != result.target.client);
	if (result.moved) {
		window->setGeometry(result.target.client);
	}
	return result;
}

QString FixtureWindowText(not_null<QWidget*> window) {
	const auto handle = window->windowHandle();
	const auto screen = handle ? handle->screen() : nullptr;
	return u"screen=[%1] frame=%2 dpr=%3 state=%4 minimized=%5"_q.arg(
		screen ? screen->name() : u"none"_q,
		RectText(window->frameGeometry()),
		QString::number(handle ? handle->devicePixelRatio() : 0.),
		QString::number(int(window->windowState())),
		FlagText(window->isMinimized()));
}

bool FixtureWindowPlaced(
		not_null<QWidget*> window,
		not_null<QScreen*> screen) {
	const auto handle = window->windowHandle();
	const auto states = Qt::WindowMinimized
		| Qt::WindowMaximized
		| Qt::WindowFullScreen;
	return handle
		&& (handle->screen() == screen.get())
		&& !(window->windowState() & states)
		&& screen->availableGeometry().contains(window->frameGeometry());
}

bool FixtureWindowSettled(
		const WidgetFixture &fixture,
		WidgetFixtureWait &wait,
		not_null<QWidget*> window,
		not_null<QScreen*> screen) {
	const auto now = crl::now();
	const auto sample = FixtureWindowText(window)
		+ u" "_q
		+ FixtureColumnsText(ReadFixtureColumns(fixture, window));
	if (sample != wait.sample) {
		wait.sample = sample;
		wait.stableSince = now;
	}
	return (FixtureWindowPlaced(window, screen)
			&& (now - wait.stableSince >= kPlaceStable))
		|| (now - wait.started >= kWidgetPlaceBound);
}

QString RuntimeStateName(RuntimeState state) {
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

void StartFixtureWait(WidgetFixtureWait &wait) {
	wait = WidgetFixtureWait{ .started = crl::now() };
}

bool FixtureBoundReached(const WidgetFixtureWait &wait, crl::time bound) {
	return (crl::now() - wait.started) >= bound;
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
