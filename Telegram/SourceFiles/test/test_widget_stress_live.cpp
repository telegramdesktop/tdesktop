/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_stress.h"

#include "data/data_wall_paper.h"
#include "history/view/media/history_view_html_widget.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "test/test_capture.h"
#include "test/test_chat_show.h"
#include "test/test_console_lock.h"
#include "test/test_log.h"
#include "test/test_palette.h"
#include "test/test_runner.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_host.h"
#include "test/test_widget_ops.h"
#include "test/test_widget_stress_internal.h"
#include "test/test_window_exposure.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_runtime.h"
#include "ui/style/style_core_scale.h"
#include "ui/style/style_palette_colorizer.h"
#include "ui/widgets/elastic_scroll.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"

#include <QtCore/QEvent>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif // Q_OS_WIN

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include "styles/palette.h"

namespace Test::Stress {
namespace {

using HtmlWidget = HistoryView::HtmlWidget;
using RuntimeState = Tgw::Runtime::State;

// The bounded waits. Each ends in a named reading inside its stage's
// until, and the stage's timeout is its bound plus kDefaultStageTimeout.
// CLIENTS.md step 2: loaded or dead within 60 s of the host's creation.
constexpr auto kLoadBound = crl::time(60000);
constexpr auto kShowBound = crl::time(10000);
constexpr auto kPlaceBound = crl::time(6000);
constexpr auto kPlaceStable = crl::time(1000);
constexpr auto kThemeBound = crl::time(4000);
constexpr auto kThemeStable = crl::time(400);

// HistoryInner's floating date hides 3000 ms after a scroll down across a
// day, then fades for 200 ms; 150 ms more for the coarse timer, as the
// conformance runner's captures wait. A window starts only after its
// list's scroll held that long.
constexpr auto kDateStill = crl::time(3350);
constexpr auto kDateBound = crl::time(8000);

// CLIENTS.md step 7.
constexpr auto kLevelPause = crl::time(1000);
constexpr auto kLoadCooldown = crl::time(20000);

// How often the warm-up and the window sample the window and the card.
constexpr auto kObserveEvery = crl::time(500);

// Room for one level: the work log for everything from the injection to
// the window's end (about 125 ticks, rebuilt frames and messages a second
// each, at most), and the window's update stamps.
constexpr auto kWorkCapacity = 65536;
constexpr auto kUpdateCapacity = 16384;

constexpr auto kWidthTolerance = 0.01;

// D4's placement, in logical px: harness constants, not product styles.
constexpr auto kWindowMargin = 64;
constexpr auto kWindowMaxWidth = 1280;
constexpr auto kWindowMaxHeight = 1000;

#ifdef Q_OS_WIN
// A bound on the z-order walk, which the window manager can reorder while
// it runs.
constexpr auto kMaxZOrderWalk = 4096;
#endif // Q_OS_WIN

// The live runner's own observers, carried by every run file it writes.
constexpr auto kObserverNote = "window: every level waits until its "
	"list's scroll held for 3350 ms (HistoryInner's floating date hidden) "
	"before its warm-up, recorded apart as desktop.setup; through the "
	"warm-up and the window the runner samples the window's screen, frame, "
	"device pixel ratio, visibility and exposure and the card's bounds every "
	"500 ms on the main thread (desktop.window.samples and sampleCostMs), "
	"and at the window's start and end it reads the console lock and, on "
	"Windows, walks the top-level z-order above the window for a visible, "
	"non-minimized, non-cloaked window over the card's screen rectangle. "
	"Any change, lock or covering window makes the level a runner error. "
	"Samples do not prove the unsampled instants between them.";

// Stamps every QEvent::UpdateRequest delivered to one top-level widget
// while recording, into room reserved at construction; handles nothing,
// so the window updates exactly as it would without it.
class UpdateFilter final : public QObject {
public:
	UpdateFilter(not_null<QWidget*> target, int capacity);
	UpdateFilter(const UpdateFilter &other) = delete;
	UpdateFilter &operator=(const UpdateFilter &other) = delete;
	~UpdateFilter();

	// Forgets the stamps and drops, then records.
	void begin();
	void end();

	[[nodiscard]] const std::vector<crl::profile_time> &stamps() const;
	[[nodiscard]] int dropped() const;
	[[nodiscard]] int64 delivered() const;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	QPointer<QWidget> _target;
	std::vector<crl::profile_time> _stamps;
	int64 _delivered = 0;
	int _capacity = 0;
	int _dropped = 0;
	bool _recording = false;

};

// The measured window and the card in it, read only. Global logical
// coordinates.
struct WindowReading {
	QString screen;
	QRect frame;
	QRect card;
	double dpr = 0.;
	bool shown = false;
	bool exposed = false;
	bool cardVisible = false;
};

// The windows above the measured one in z-order over the card. |native|
// is the card's rectangle in native screen pixels.
struct CoverReading {
	QStringList covering;
	QString error;
	QString monitor;
	QRect native;
	int above = 0;
	int transparent = 0;
	bool applicable = false;
};

// One level as its stages drive it. Every crl::now() stamp is 0 until
// its moment.
struct LiveLevel {
	std::shared_ptr<WidgetFixtureMessage> message;
	std::shared_ptr<const ChatListShow> loadShow;
	std::shared_ptr<const ChatListShow> cardShow;
	StressLevel level;
	// The window as the setup left it; every later reading must equal it.
	WindowReading accepted;
	QJsonObject oneAlive;
	QJsonObject windowStart;
	QJsonObject windowEnd;
	// Set by a failure that ends the level's measurement.
	QString notRun;
	// The first sample that differed from |accepted|.
	QString changed;
	QString focus;
	std::optional<double> cardCssWidth;
	crl::time injectedAt = 0;
	crl::time laidOutAt = 0;
	crl::time decidedAt = 0;
	crl::time settleStarted = 0;
	crl::time stillSince = 0;
	crl::time settledAt = 0;
	crl::time warmupStarted = 0;
	crl::time windowStarted = 0;
	crl::time pauseStarted = 0;
	crl::time lastObserved = 0;
	crl::profile_time observeCost = 0;
	int observations = 0;
	int scrollTop = 0;
	int scrollChanges = 0;
	int updatesDropped = 0;
	// The manifest.widgets index.
	int widget = 0;
	bool lastOfLoad = false;
	bool started = false;
	bool settled = false;
	bool written = false;
};

struct LiveState {
	WidgetStressArgs args;
	StressPackage package;
	StressLaunch launch;
	std::shared_ptr<WidgetFixture> fixture;
	std::shared_ptr<const PaletteSwitch> palette;
	std::shared_ptr<const PaletteRestore> paletteRestore;
	std::unique_ptr<UpdateFilter> filter;
	QPointer<QWidget> window;
	QPointer<QScreen> screen;
	std::vector<LiveLevel> levels;
	std::vector<StressEntry> entries;
	// The placed frame every level must keep.
	QRect frame;
	// The first measured level's theme message, every later one's too.
	QString themeMessage;
	// The current settle wait's last sample.
	QString sample;
	// A failed setup gate: no level runs.
	QString setupError;
	// Why the ladder stopped: no later level runs.
	QString abort;
	// The kept theme the runner replaced with the default one, when it
	// did, and what its restore did.
	std::optional<Window::Theme::Object> themeBefore;
	QString themeRestore;
	crl::time waitStarted = 0;
	crl::time stableSince = 0;
	bool themeRestored = false;
	bool published = false;
};

UpdateFilter::UpdateFilter(not_null<QWidget*> target, int capacity)
: _target(target.get())
, _capacity(capacity) {
	_stamps.reserve(capacity);
	target->installEventFilter(this);
}

UpdateFilter::~UpdateFilter() {
	if (const auto target = _target.data()) {
		target->removeEventFilter(this);
	}
}

void UpdateFilter::begin() {
	_stamps.clear();
	_dropped = 0;
	_recording = true;
}

void UpdateFilter::end() {
	_recording = false;
}

const std::vector<crl::profile_time> &UpdateFilter::stamps() const {
	return _stamps;
}

int UpdateFilter::dropped() const {
	return _dropped;
}

int64 UpdateFilter::delivered() const {
	return _delivered;
}

bool UpdateFilter::eventFilter(QObject *watched, QEvent *event) {
	if ((event->type() != QEvent::UpdateRequest)
		|| (watched != _target.data())) {
		return false;
	}
	const auto now = crl::profile();
	++_delivered;
	if (_recording) {
		if (int(_stamps.size()) < _capacity) {
			_stamps.push_back(now);
		} else {
			++_dropped;
		}
	}
	return false;
}

// ---------------------------------------------------------------------
// Small helpers.

[[nodiscard]] QJsonValue Null() {
	return QJsonValue(QJsonValue::Null);
}

[[nodiscard]] QString Compact(const QJsonObject &object) {
	return QString::fromUtf8(
		QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QJsonArray RectArray(const QRect &rect) {
	return QJsonArray{ rect.x(), rect.y(), rect.width(), rect.height() };
}

[[nodiscard]] QString Flag(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QJsonValue Since(crl::time from, crl::time till) {
	return (from && till) ? QJsonValue(qint64(till - from)) : Null();
}

[[nodiscard]] QString LevelName(
		const LiveLevel &level,
		const QString &text) {
	return Prefixed(u"[%1] %2"_q.arg(level.level.id, text));
}

void AddError(LiveLevel &level, const QString &error) {
	level.level.runnerErrors.push_back(error);
}

// A failure that ends this level's measurement and stops the ladder; the
// level's entry still names it.
void Refuse(LiveState &state, LiveLevel &level, const QString &error) {
	AddError(level, error);
	if (level.notRun.isEmpty()) {
		level.notRun = error;
	}
	if (state.abort.isEmpty()) {
		state.abort = u"[%1] %2"_q.arg(level.level.id, error);
	}
	Note(LevelName(level, error));
}

[[nodiscard]] QString LadderSkip(const LiveState &state) {
	const auto gate = WidgetFixtureGate(*state.fixture);
	return !gate.isEmpty()
		? gate
		: !state.setupError.isEmpty()
		? state.setupError
		: state.abort;
}

[[nodiscard]] std::optional<WidgetHostSample> HostSample(
		const LiveLevel &level) {
	const auto &host = level.message->host;
	if (!host) {
		return std::nullopt;
	}
	const auto reading = ReadWidgetHost(*host);
	return reading.hosts.empty()
		? std::nullopt
		: std::make_optional(reading.hosts.front());
}

// Whether the level can have a window: loaded and not dead.
[[nodiscard]] QString WindowSkip(const LiveLevel &level) {
	if (!level.notRun.isEmpty()) {
		return level.notRun;
	}
	const auto sample = HostSample(level);
	if (!sample || !sample->loaded) {
		return u"the widget did not load, so it has no window"_q;
	} else if (sample->dead) {
		return u"the widget died before its window: "_q
			+ QString::fromStdString(*sample->dead);
	}
	return QString();
}

[[nodiscard]] std::optional<int> ScrollTop(const LiveState &state) {
	const auto scroll = FixtureListScroll(WidgetFixtureList(*state.fixture));
	return scroll ? std::make_optional(scroll->scrollTop()) : std::nullopt;
}

[[nodiscard]] QJsonObject ShowObject(const ChatListShow *shown) {
	if (!shown || !shown->finished) {
		return QJsonObject{ { u"finished"_q, false } };
	}
	return QJsonObject{
		{ u"finished"_q, true },
		{ u"shown"_q, shown->shown() },
		{ u"refusal"_q, ChatListShowRefusalName(shown->refusal) },
		{ u"reason"_q, shown->reason },
		{ u"scrolls"_q, shown->scrolls },
		{ u"elapsedMs"_q, qint64(shown->elapsedMs) },
	};
}

[[nodiscard]] QString ShowText(const ChatListShow *shown) {
	return (shown && shown->finished)
		? (ChatListShowRefusalName(shown->refusal) + u" - "_q + shown->reason)
		: u"the show did not run"_q;
}

// ---------------------------------------------------------------------
// The theme.

[[nodiscard]] QString EmbeddedName(
		std::optional<Window::Theme::EmbeddedType> type) {
	using Type = Window::Theme::EmbeddedType;
	if (!type) {
		return u"none"_q;
	}
	switch (*type) {
	case Type::DayBlue: return u"day-blue"_q;
	case Type::Default: return u"default"_q;
	case Type::Night: return u"night"_q;
	case Type::NightGreen: return u"night-green"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] bool TestingPaper(const Data::WallPaper &paper) {
	return Data::details::IsTestingThemeWallPaper(paper)
		|| Data::details::IsTestingDefaultWallPaper(paper)
		|| Data::details::IsTestingEditorWallPaper(paper);
}

[[nodiscard]] QString WallPaperKind(const Data::WallPaper &paper) {
	return TestingPaper(paper)
		? u"preview"_q
		: Data::IsDefaultWallPaper(paper)
		? u"default"_q
		: Data::IsThemeWallPaper(paper)
		? u"theme"_q
		: Data::IsCustomWallPaper(paper)
		? u"custom"_q
		: Data::IsCloudWallPaper(paper)
		? u"cloud"_q
		: u"other"_q;
}

// The kept theme is the default day one: no theme file, no cloud theme.
[[nodiscard]] bool DefaultThemeKept() {
	const auto &object = Window::Theme::Background()->themeObject();
	return object.pathAbsolute.isEmpty() && !object.cloud.id;
}

[[nodiscard]] QJsonObject ThemeObject() {
	const auto background = Window::Theme::Background();
	const auto &object = background->themeObject();
	return QJsonObject{
		{
			u"embedded"_q,
			EmbeddedName(Window::Theme::CurrentEmbeddedType()),
		},
		{ u"cloud"_q, (object.cloud.id != 0) },
		{ u"themeFile"_q, !object.pathAbsolute.isEmpty() },
		{ u"night"_q, Window::Theme::IsNightMode() },
		{ u"wallpaper"_q, WallPaperKind(background->paper()) },
		{
			u"accentColorizer"_q,
			bool(Window::Theme::ColorizerForTheme(QString())),
		},
		{ u"windowBg"_q, st::windowBg->c.name() },
	};
}

// After the day switch the kept day theme can be any; the default one is
// kept through the product's own Settings route, ApplyDefaultWithPath()
// with no path and KeepApplied() at once. A preview alone would not do:
// MainWindow covers the window with its keep-or-revert warning and
// reverts the preview 16 s later.
void ApplyDefaultTheme(LiveState &state) {
	state.waitStarted = state.stableSince = crl::now();
	state.sample = QString();
	const auto &palette = *state.palette;
	const auto background = Window::Theme::Background();
	auto refusal = QString();
	if (!palette.succeeded()) {
		refusal = u"the day palette switch refused: "_q
			+ PaletteSwitchText(palette);
	} else if (background->editingTheme().has_value()) {
		refusal = u"the theme editor is open"_q;
	} else if (TestingPaper(background->paper())) {
		refusal = u"a theme preview is being tested"_q;
	} else if (Window::Theme::IsNightMode()) {
		refusal = u"the night mode is on after the day switch"_q;
	}
	state.launch.record.insert(u"themeBefore"_q, ThemeObject());
	if (!refusal.isEmpty()) {
		state.setupError = Prefixed(u"the default day theme: "_q + refusal);
		Check(
			false,
			Prefixed(u"fixture gate: the default day theme is kept"_q),
			refusal);
		return;
	} else if (DefaultThemeKept()) {
		return;
	}
	state.themeBefore = background->themeObject();
	Window::Theme::ApplyDefaultWithPath(QString());
	Window::Theme::KeepApplied();
}

// The palette's windowBg sample held for kThemeStable, or kThemeBound
// passed.
[[nodiscard]] bool PaletteHeld(LiveState &state) {
	const auto now = crl::now();
	const auto sample = st::windowBg->c.name();
	if (sample != state.sample) {
		state.sample = sample;
		state.stableSince = now;
	}
	return (now - state.stableSince >= kThemeStable)
		|| (now - state.waitStarted >= kThemeBound);
}

void CheckDefaultTheme(LiveState &state) {
	if (!state.setupError.isEmpty()) {
		return;
	}
	const auto theme = ThemeObject();
	const auto held = (crl::now() - state.stableSince >= kThemeStable);
	const auto ok = held
		&& DefaultThemeKept()
		&& !TestingPaper(Window::Theme::Background()->paper())
		&& !Window::Theme::IsNightMode();
	state.launch.record.insert(u"theme"_q, theme);
	state.launch.record.insert(
		u"themeChanged"_q,
		state.themeBefore.has_value());
	const auto details = Compact(theme)
		+ u" changed="_q
		+ Flag(state.themeBefore.has_value())
		+ u" held="_q
		+ Flag(held);
	Check(
		ok,
		Prefixed(u"fixture gate: the default day theme is kept"_q),
		details);
	if (!ok) {
		state.setupError = Prefixed(u"the default day theme: "_q + details);
	}
}

// Keeps again the theme the runner replaced, through the routes the
// product keeps it with: ApplyDefaultWithPath() for an embedded one,
// Apply() for a theme file or a cloud theme's cached file. Once.
void RestoreTheme(LiveState &state) {
	if (!state.themeBefore || state.themeRestored) {
		return;
	}
	state.themeRestored = true;
	const auto &object = *state.themeBefore;
	if (Window::Theme::IsEmbeddedTheme(object.pathAbsolute)) {
		Window::Theme::ApplyDefaultWithPath(object.pathAbsolute);
	} else if (!Window::Theme::Apply(object.pathAbsolute, object.cloud)) {
		state.themeRestore = u"the theme file could not be applied again"_q;
		return;
	}
	Window::Theme::KeepApplied();
	state.themeRestore = u"kept again"_q;
}

void CheckThemeRestored(const LiveState &state) {
	const auto &before = *state.themeBefore;
	const auto &now = Window::Theme::Background()->themeObject();
	Check(
		(now.pathAbsolute == before.pathAbsolute)
			&& (now.cloud.id == before.cloud.id),
		Prefixed(u"the day theme the launch found is kept again"_q),
		state.themeRestore + u" "_q + Compact(ThemeObject()));
}

void JudgePalette(const LiveState &state) {
	const auto &palette = *state.palette;
	const auto &restore = *state.paletteRestore;
	if (!palette.finished) {
		Note(Prefixed(u"the day palette switch did not run, so its restore "
			u"is not judged: "_q) + PaletteRestoreText(restore));
		return;
	}
	Check(
		restore.restored(),
		Prefixed(u"the palette the launch began in is back"_q),
		PaletteRestoreText(restore));
}

// The renderer's scheme and theme message: kTheme, the same for every
// level of the launch.
void CheckRendererTheme(
		LiveState &state,
		LiveLevel &level,
		const WidgetOpsLatest &latest) {
	const auto scheme = QString::fromStdString(latest.scheme);
	const auto message = QString::fromStdString(latest.themeMessage);
	auto parse = QJsonParseError();
	const auto document = QJsonDocument::fromJson(message.toUtf8(), &parse);
	const auto theme = document.object().value(u"theme"_q).toObject();
	const auto colorScheme = theme.value(u"colorScheme"_q).toString();
	const auto expected = QString::fromLatin1(kTheme);
	if ((scheme != expected) || (colorScheme != expected)) {
		AddError(level, u"theme: the renderer got the scheme [%1] and a "
			u"theme message of colorScheme [%2], not %3"_q.arg(
				scheme,
				colorScheme,
				expected));
	}
	if (state.themeMessage.isEmpty()) {
		state.themeMessage = message;
		if (state.launch.applied) {
			state.launch.applied->theme = scheme;
		}
		state.launch.record.insert(u"rendererTheme"_q, QJsonObject{
			{ u"colorScheme"_q, colorScheme },
			{ u"params"_q, theme.value(u"params"_q) },
		});
	} else if (message != state.themeMessage) {
		AddError(level, u"theme: the renderer got another theme message "
			u"than the launch's first measured level"_q);
	}
}

// ---------------------------------------------------------------------
// The window.

[[nodiscard]] QScreen *FastestScreen() {
	const auto primary = QGuiApplication::primaryScreen();
	auto result = (QScreen*)nullptr;
	for (const auto screen : QGuiApplication::screens()) {
		if (!result
			|| (screen->refreshRate() > result->refreshRate())
			|| ((screen->refreshRate() == result->refreshRate())
				&& (screen == primary))) {
			result = screen;
		}
	}
	return result;
}

[[nodiscard]] QRect TargetFrame(not_null<QScreen*> screen) {
	const auto inner = screen->availableGeometry().marginsRemoved({
		kWindowMargin,
		kWindowMargin,
		kWindowMargin,
		kWindowMargin,
	});
	return QRect(
		inner.topLeft(),
		QSize(
			std::min(kWindowMaxWidth, inner.width()),
			std::min(kWindowMaxHeight, inner.height())));
}

[[nodiscard]] QJsonArray ScreensArray() {
	const auto primary = QGuiApplication::primaryScreen();
	auto result = QJsonArray();
	for (const auto screen : QGuiApplication::screens()) {
		result.push_back(QJsonObject{
			{ u"name"_q, screen->name() },
			{ u"geometry"_q, RectArray(screen->geometry()) },
			{ u"available"_q, RectArray(screen->availableGeometry()) },
			{ u"refreshHz"_q, screen->refreshRate() },
			{ u"dpr"_q, screen->devicePixelRatio() },
			{ u"primary"_q, (screen == primary) },
		});
	}
	return result;
}

[[nodiscard]] QString PlacementText(not_null<QWidget*> window) {
	const auto handle = window->windowHandle();
	const auto screen = handle ? handle->screen() : nullptr;
	return u"screen=[%1] frame=%2 dpr=%3 state=%4 minimized=%5"_q.arg(
		screen ? screen->name() : u"none"_q,
		RectText(window->frameGeometry()),
		QString::number(handle ? handle->devicePixelRatio() : 0.),
		QString::number(int(window->windowState())),
		Flag(window->isMinimized()));
}

[[nodiscard]] bool Placed(
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

// Moves the cursor off the window, so nothing in it is hovered, and
// clears the window's text focus, so no caret blinks.
[[nodiscard]] QString ClearFocusAndCursor(const LiveState &state) {
	const auto window = state.window.data();
	const auto screen = state.screen.data();
	if (!window || !screen) {
		return u"no window"_q;
	}
	auto parts = QStringList();
	if (const auto focused = window->focusWidget()) {
		parts.push_back(u"focus %1 cleared"_q.arg(WidgetDescription(focused)));
		focused->clearFocus();
	} else {
		parts.push_back(u"no focus"_q);
	}
	const auto frame = window->frameGeometry();
	if (frame.contains(QCursor::pos())) {
		const auto away = screen->availableGeometry().topLeft()
			+ QPoint(kWindowMargin / 2, kWindowMargin / 2);
		QCursor::setPos(screen, away);
		parts.push_back(u"cursor moved off the window to %1,%2"_q.arg(
			QString::number(away.x()),
			QString::number(away.y())));
	} else {
		parts.push_back(u"cursor off the window"_q);
	}
	DeliverListLeave(WidgetFixtureList(*state.fixture));
	return parts.join(u", "_q);
}

void PlaceWindow(LiveState &state) {
	state.waitStarted = state.stableSince = crl::now();
	state.sample = QString();
	const auto list = WidgetFixtureList(*state.fixture);
	const auto window = list ? list->window() : nullptr;
	const auto screen = FastestScreen();
	state.window = window;
	state.screen = screen;
	if (!window || !screen) {
		state.setupError = Prefixed(u"no window shows the list, or no "
			u"screen is known"_q);
		Check(false, Prefixed(u"fixture gate: the window is placed"_q));
		return;
	}
	const auto target = TargetFrame(screen);
	state.launch.record.insert(u"placement"_q, QJsonObject{
		{ u"screens"_q, ScreensArray() },
		{ u"chosen"_q, screen->name() },
		{ u"before"_q, PlacementText(window) },
		{ u"target"_q, RectArray(target) },
	});
	const auto states = Qt::WindowMaximized | Qt::WindowFullScreen;
	if (window->windowState() & states) {
		window->setWindowState(window->windowState() & ~states);
	}
	const auto client = window->geometry();
	const auto frame = window->frameGeometry();
	window->setGeometry(target.marginsRemoved({
		client.left() - frame.left(),
		client.top() - frame.top(),
		frame.right() - client.right(),
		frame.bottom() - client.bottom(),
	}));
}

[[nodiscard]] bool WindowPlaced(LiveState &state) {
	const auto window = state.window.data();
	const auto screen = state.screen.data();
	if (!state.setupError.isEmpty() || !window || !screen) {
		return true;
	}
	const auto now = crl::now();
	const auto sample = PlacementText(window);
	if (sample != state.sample) {
		state.sample = sample;
		state.stableSince = now;
	}
	return (Placed(window, screen) && (now - state.stableSince >= kPlaceStable))
		|| (now - state.waitStarted >= kPlaceBound);
}

void CheckPlacement(LiveState &state) {
	if (!state.setupError.isEmpty()) {
		return;
	}
	const auto window = state.window.data();
	const auto screen = state.screen.data();
	const auto handle = window ? window->windowHandle() : nullptr;
	const auto placed = handle && screen && Placed(window, screen);
	const auto raster = handle
		&& (handle->surfaceType() == QSurface::RasterSurface);
	auto &read = state.launch.screen;
	if (handle && screen) {
		read.name = screen->name();
		read.geometry = screen->geometry();
		read.refreshHz = screen->refreshRate();
		read.windowDpr = handle->devicePixelRatio();
		read.coreRatio = style::DevicePixelRatio() * style::Scale() / 100.;
		read.uiScale = style::Scale();
		read.surface = raster
			? u"RasterSurface"_q
			: u"surface type %1"_q.arg(int(handle->surfaceType()));
		state.frame = window->frameGeometry();
	}
	auto placement = state.launch.record.value(u"placement"_q).toObject();
	placement.insert(u"after"_q, window ? PlacementText(window) : QString());
	state.launch.record.insert(u"placement"_q, placement);
	const auto details = window
		? (PlacementText(window) + u" surface="_q + read.surface)
		: u"the window is gone"_q;
	Check(
		placed,
		Prefixed(u"fixture gate: the window is wholly on the screen with "
			u"the highest refresh rate"_q),
		details);
	Check(
		raster,
		Prefixed(u"fixture gate: the window paints into a raster surface"_q),
		details);
	if (!placed || !raster) {
		state.setupError = Prefixed(u"the window is not placed: "_q + details);
		return;
	}
	state.filter = std::make_unique<UpdateFilter>(window, kUpdateCapacity);
	state.launch.record.insert(
		u"focusAndCursor"_q,
		ClearFocusAndCursor(state));
}

void CheckEnvironment(LiveState &state) {
	const auto lock = ReadConsoleLock();
	const auto exposure = ReadWindowExposure(state.window.data());
	const auto details = ConsoleLockText(lock)
		+ u" "_q
		+ WindowExposureText(exposure);
	state.launch.record.insert(u"environment"_q, details);
	const auto ok = !lock.locked() && exposure.exposed;
	Check(
		ok,
		Prefixed(u"fixture gate: the console is not locked and the window "
			u"is exposed"_q),
		details);
	if (!ok) {
		state.setupError = Prefixed(u"the console or the window: "_q + details);
	}
}

[[nodiscard]] WindowReading ReadWindow(const LiveState &state, FullMsgId id) {
	auto result = WindowReading();
	const auto window = state.window.data();
	const auto handle = window ? window->windowHandle() : nullptr;
	if (!handle) {
		return result;
	}
	const auto screen = handle->screen();
	result.screen = screen ? screen->name() : QString();
	result.frame = window->frameGeometry();
	result.dpr = handle->devicePixelRatio();
	result.shown = window->isVisible() && !window->isMinimized();
	result.exposed = handle->isExposed();
	const auto list = WidgetFixtureList(*state.fixture);
	const auto view = list ? FixtureOwnView(*state.fixture, id) : nullptr;
	if (view) {
		const auto element = ChatListShowElementRect(list, view);
		const auto local = WidgetCardLocal(view).translated(
			element.topLeft());
		result.cardVisible = !local.isEmpty()
			&& ChatListShowVisibleArea(list).contains(local);
		result.card = QRect(list->mapToGlobal(local.topLeft()), local.size());
	}
	return result;
}

[[nodiscard]] QJsonObject WindowObject(const WindowReading &reading) {
	return QJsonObject{
		{ u"screen"_q, reading.screen },
		{ u"frame"_q, RectArray(reading.frame) },
		{ u"card"_q, RectArray(reading.card) },
		{ u"dpr"_q, reading.dpr },
		{ u"shown"_q, reading.shown },
		{ u"exposed"_q, reading.exposed },
		{ u"cardVisible"_q, reading.cardVisible },
	};
}

[[nodiscard]] QStringList Differences(
		const WindowReading &was,
		const WindowReading &now) {
	auto result = QStringList();
	const auto add = [&](
			bool same,
			const QString &name,
			const QString &from,
			const QString &to) {
		if (!same) {
			result.push_back(u"%1 %2 -> %3"_q.arg(name, from, to));
		}
	};
	add(was.screen == now.screen, u"screen"_q, was.screen, now.screen);
	add(
		was.frame == now.frame,
		u"frame"_q,
		RectText(was.frame),
		RectText(now.frame));
	add(
		was.card == now.card,
		u"card"_q,
		RectText(was.card),
		RectText(now.card));
	add(
		was.dpr == now.dpr,
		u"dpr"_q,
		QString::number(was.dpr),
		QString::number(now.dpr));
	add(was.shown == now.shown, u"shown"_q, Flag(was.shown), Flag(now.shown));
	add(
		was.exposed == now.exposed,
		u"exposed"_q,
		Flag(was.exposed),
		Flag(now.exposed));
	add(
		was.cardVisible == now.cardVisible,
		u"cardVisible"_q,
		Flag(was.cardVisible),
		Flag(now.cardVisible));
	return result;
}

// What the setup must leave: the placed window, shown and exposed, with
// the card wholly in the list's visible area.
[[nodiscard]] QStringList WindowProblems(
		const LiveState &state,
		const WindowReading &reading) {
	auto result = QStringList();
	if (!reading.shown) {
		result.push_back(u"the window is hidden or minimized"_q);
	}
	if (!reading.exposed) {
		result.push_back(u"the window is not exposed"_q);
	}
	if ((reading.screen != state.launch.screen.name)
		|| (reading.dpr != state.launch.screen.windowDpr)
		|| (reading.frame != state.frame)) {
		result.push_back(u"the window left its placement (screen [%1] "
			u"dpr %2 frame %3)"_q.arg(
				reading.screen,
				QString::number(reading.dpr),
				RectText(reading.frame)));
	}
	if (!reading.cardVisible) {
		result.push_back(u"the card %1 is not wholly in the list's visible "
			u"area"_q.arg(RectText(reading.card)));
	}
	return result;
}

#ifdef Q_OS_WIN
[[nodiscard]] QString WindowClassName(HWND handle) {
	auto buffer = std::array<wchar_t, 256>();
	const auto length = GetClassNameW(
		handle,
		buffer.data(),
		int(buffer.size()));
	return QString::fromWCharArray(buffer.data(), std::max(length, 0));
}

[[nodiscard]] bool Cloaked(HWND handle) {
	auto cloaked = DWORD(0);
	const auto read = DwmGetWindowAttribute(
		handle,
		DWMWA_CLOAKED,
		&cloaked,
		sizeof(cloaked));
	return SUCCEEDED(read) && (cloaked != 0);
}

// The visible frame, without the invisible resize borders where DWM
// reports them.
[[nodiscard]] QRect FrameBounds(HWND handle) {
	auto rect = RECT();
	const auto extended = SUCCEEDED(DwmGetWindowAttribute(
		handle,
		DWMWA_EXTENDED_FRAME_BOUNDS,
		&rect,
		sizeof(rect)));
	if (!extended && !GetWindowRect(handle, &rect)) {
		return QRect();
	}
	return QRect(
		QPoint(rect.left, rect.top),
		QSize(rect.right - rect.left, rect.bottom - rect.top));
}

// A layered window whose constant alpha is 0 draws nothing.
[[nodiscard]] bool FullyTransparent(HWND handle) {
	if (!(GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_LAYERED)) {
		return false;
	}
	auto alpha = BYTE(255);
	auto flags = DWORD(0);
	return GetLayeredWindowAttributes(handle, nullptr, &alpha, &flags)
		&& (flags & LWA_ALPHA)
		&& !alpha;
}

// |logical| in native pixels: Qt keeps a screen's top-left unscaled and
// scales from it, so the native point is the monitor's origin plus the
// scaled offset from the screen's origin.
[[nodiscard]] QRect NativeRect(
		HWND handle,
		not_null<QScreen*> screen,
		const QRect &logical,
		double dpr,
		CoverReading &reading) {
	const auto monitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONULL);
	auto info = MONITORINFOEXW();
	info.cbSize = sizeof(info);
	if (!monitor || !GetMonitorInfoW(monitor, &info)) {
		reading.error = u"the window's monitor could not be read"_q;
		return QRect();
	}
	const auto origin = QPoint(info.rcMonitor.left, info.rcMonitor.top);
	const auto base = screen->geometry().topLeft();
	reading.monitor = u"%1 at %2,%3"_q.arg(
		QString::fromWCharArray(info.szDevice),
		QString::number(origin.x()),
		QString::number(origin.y()));
	if (origin != base) {
		reading.error = u"the window's monitor %1 is not its screen at "
			u"%2,%3"_q.arg(
				reading.monitor,
				QString::number(base.x()),
				QString::number(base.y()));
		return QRect();
	}
	const auto map = [&](QPoint point) {
		return origin + QPoint(
			int(std::lround((point.x() - base.x()) * dpr)),
			int(std::lround((point.y() - base.y()) * dpr)));
	};
	const auto from = map(logical.topLeft());
	const auto till = map(
		logical.topLeft() + QPoint(logical.width(), logical.height()));
	return QRect(from, QSize(till.x() - from.x(), till.y() - from.y()));
}

// One window above the measured one: counted when it is visible, not
// minimized, not cloaked and over the card, named by its class and
// whether it is this process's, never by its title.
void AddCovering(HWND other, CoverReading &result) {
	if (!IsWindowVisible(other) || IsIconic(other) || Cloaked(other)) {
		return;
	}
	const auto overlap = FrameBounds(other).intersected(result.native);
	if (overlap.isEmpty()) {
		return;
	} else if (FullyTransparent(other)) {
		++result.transparent;
		return;
	}
	auto pid = DWORD(0);
	GetWindowThreadProcessId(other, &pid);
	result.covering.push_back(u"class=%1 process=%2 overlap=%3"_q.arg(
		WindowClassName(other),
		(pid == GetCurrentProcessId()) ? u"own"_q : u"other"_q,
		RectText(overlap)));
}

[[nodiscard]] CoverReading ReadCovering(QWidget *window, const QRect &card) {
	auto result = CoverReading{ .applicable = true };
	const auto handle = window ? window->windowHandle() : nullptr;
	const auto screen = handle ? handle->screen() : nullptr;
	if (!screen || card.isEmpty()) {
		result.error = u"no window, screen or card to check"_q;
		return result;
	}
	const auto own = reinterpret_cast<HWND>(handle->winId());
	result.native = NativeRect(
		own,
		screen,
		card,
		handle->devicePixelRatio(),
		result);
	if (!result.error.isEmpty()) {
		return result;
	}
	auto other = GetTopWindow(nullptr);
	auto walked = 0;
	while (other && (other != own) && (walked < kMaxZOrderWalk)) {
		AddCovering(other, result);
		other = GetWindow(other, GW_HWNDNEXT);
		++walked;
	}
	result.above = walked;
	if (other != own) {
		result.error = u"the window was not found in the top-level z-order "
			u"(%1 windows walked)"_q.arg(walked);
	}
	return result;
}
#else // Q_OS_WIN
[[nodiscard]] CoverReading ReadCovering(QWidget *, const QRect &) {
	return CoverReading();
}
#endif // Q_OS_WIN

[[nodiscard]] QJsonObject CoverObject(const CoverReading &cover) {
	return QJsonObject{
		{ u"applicable"_q, cover.applicable },
		{ u"monitor"_q, cover.monitor },
		{ u"native"_q, RectArray(cover.native) },
		{ u"above"_q, cover.above },
		{ u"transparent"_q, cover.transparent },
		{ u"covering"_q, QJsonArray::fromStringList(cover.covering) },
		{ u"error"_q, cover.error },
	};
}

// The lock, the covering windows and the window against the setup's, at
// one end of the window; every finding is a runner error of the level.
[[nodiscard]] QJsonObject WindowChecks(
		const LiveState &state,
		LiveLevel &level,
		const QString &end) {
	const auto lock = ReadConsoleLock();
	const auto reading = ReadWindow(state, level.message->id);
	const auto cover = ReadCovering(state.window.data(), reading.card);
	const auto differences = Differences(level.accepted, reading);
	if (lock.locked()) {
		AddError(level, u"window %1: the console is locked: %2"_q.arg(
			end,
			ConsoleLockText(lock)));
	}
	if (!cover.error.isEmpty()) {
		AddError(level, u"window %1: covering windows: %2"_q.arg(
			end,
			cover.error));
	} else if (!cover.covering.isEmpty()) {
		AddError(level, u"window %1: windows above the measured one cover "
			u"the card: %2"_q.arg(end, cover.covering.join(u"; "_q)));
	}
	if (!differences.isEmpty()) {
		AddError(level, u"window %1: the window or the card changed since "
			u"the setup: %2"_q.arg(end, differences.join(u"; "_q)));
	}
	return QJsonObject{
		{ u"lock"_q, ConsoleLockText(lock) },
		{ u"window"_q, WindowObject(reading) },
		{ u"cover"_q, CoverObject(cover) },
	};
}

// One sample every kObserveEvery, on the until's ticks: reads only.
void Observe(const LiveState &state, LiveLevel &level) {
	const auto now = crl::now();
	if (level.lastObserved && (now - level.lastObserved < kObserveEvery)) {
		return;
	}
	level.lastObserved = now;
	const auto started = crl::profile();
	const auto reading = ReadWindow(state, level.message->id);
	++level.observations;
	if (level.changed.isEmpty()) {
		const auto differences = Differences(level.accepted, reading);
		if (!differences.isEmpty()) {
			level.changed = u"sample %1: %2"_q.arg(
				QString::number(level.observations),
				differences.join(u"; "_q));
		}
	}
	level.observeCost += crl::profile() - started;
}

// ---------------------------------------------------------------------
// One level.

[[nodiscard]] bool Alive(RuntimeState state) {
	return (state == RuntimeState::Waiting)
		|| (state == RuntimeState::Constructing)
		|| (state == RuntimeState::Live);
}

// D5: no widget view of the chat has a host alive or about to start, none
// is in the list's visible area, and every earlier level's host is gone.
// Counts and states only: the run file names no message of the chat.
void CheckOneAlive(LiveState &state, LiveLevel &level) {
	level.started = true;
	const auto list = WidgetFixtureList(*state.fixture);
	const auto history = WidgetFixtureHistory(*state.fixture);
	auto views = 0;
	auto alive = QStringList();
	auto visible = 0;
	if (list && history) {
		const auto area = ChatListShowVisibleArea(list);
		for (const auto &block : history->blocks) {
			for (const auto &view : block->messages) {
				if (!dynamic_cast<const HtmlWidget*>(view->media())) {
					continue;
				}
				++views;
				const auto runtime = Tgw::FindRuntime(view.get());
				const auto current = runtime
					? runtime->state()
					: RuntimeState::Idle;
				if (Alive(current)) {
					alive.push_back(RuntimeStateName(current));
				}
				const auto rect = ChatListShowElementRect(list, view.get());
				if (rect.intersects(area)) {
					++visible;
				}
			}
		}
	}
	auto unfinished = QStringList();
	for (const auto &other : state.levels) {
		const auto &tap = other.message->tap;
		if ((&other != &level)
			&& tap
			&& !WidgetTapFinished(ReadWidgetOps(*tap))) {
			unfinished.push_back(other.level.id);
		}
	}
	level.oneAlive = QJsonObject{
		{
			u"checked"_q,
			u"the chat's loaded widget views: their runtime states and "
				u"the list's visible area; every earlier level's tap"_q,
		},
		{ u"widgetViews"_q, views },
		{ u"alive"_q, QJsonArray::fromStringList(alive) },
		{ u"visible"_q, visible },
		{ u"earlierUnfinished"_q, QJsonArray::fromStringList(unfinished) },
	};
	auto problems = QStringList();
	if (!list || !history) {
		problems.push_back(u"the fixture's list or history is gone"_q);
	}
	if (!alive.isEmpty()) {
		problems.push_back(u"%1 widget host(s) of the chat are alive (%2)"_q
			.arg(QString::number(alive.size()), alive.join(u", "_q)));
	}
	if (visible > 0) {
		problems.push_back(u"%1 widget message(s) are in the list's visible "
			u"area"_q.arg(visible));
	}
	if (!unfinished.isEmpty()) {
		problems.push_back(u"the hosts of [%1] are not gone"_q.arg(
			unfinished.join(u", "_q)));
	}
	if (!problems.isEmpty()) {
		Refuse(
			state,
			level,
			u"one widget alive at a time: "_q + problems.join(u"; "_q));
	}
}

// The tap and the host watch are armed in the injection; the work log is
// reserved before this run returns, so before RendererCore's host is
// attached on a later main-loop turn.
void InjectLevel(LiveState &state, LiveLevel &level) {
	auto &message = *level.message;
	const auto &widget = state.package.manifest.widgets[level.widget];
	message.watchHost = HostOverride(state.args.pass);
	level.injectedAt = crl::now();
	const auto refusal = InjectWidgetMessage(
		*state.fixture,
		message,
		widget.html,
		state.package.manifest.width,
		0); // height
	if (message.host) {
		ReserveWidgetWork(*message.host, kWorkCapacity);
	}
	if (!refusal.isEmpty()) {
		Refuse(state, level, refusal);
	}
}

void CheckLaidOut(LiveState &state, LiveLevel &level) {
	if (!level.notRun.isEmpty()) {
		return;
	}
	level.laidOutAt = crl::now();
	const auto refusal = CheckWidgetMessage(*state.fixture, *level.message);
	if (!refusal.isEmpty()) {
		Refuse(state, level, refusal);
	}
}

[[nodiscard]] bool LoadDecided(LiveLevel &level) {
	auto &message = *level.message;
	const auto reading = message.host
		? ReadWidgetHost(*message.host)
		: WidgetHostReading();
	const auto sample = reading.hosts.empty()
		? nullptr
		: &reading.hosts.front();
	const auto waited = crl::now() - level.injectedAt;
	message.wait.details = u"optionsCalls=%1 hosts=%2 loaded=%3 dead=%4 "
		u"waited=%5 ms"_q.arg(
			QString::number(reading.applied.size()),
			QString::number(reading.hosts.size()),
			Flag(sample && sample->loaded),
			Flag(sample && sample->dead),
			QString::number(waited));
	return (sample && (sample->loaded || sample->dead))
		|| (waited >= kLoadBound);
}

void CheckLoaded(LiveState &state, LiveLevel &level) {
	level.decidedAt = crl::now();
	const auto sample = HostSample(level);
	if (!sample || (!sample->loaded && !sample->dead)) {
		Refuse(state, level, u"neither loaded nor dead within %1 ms of the "
			u"injection: %2; the load show: %3"_q.arg(
				QString::number(kLoadBound),
				level.message->wait.details,
				ShowText(level.loadShow.get())));
	}
}

void StartSettle(LiveState &state, LiveLevel &level) {
	level.focus = ClearFocusAndCursor(state);
	level.settleStarted = level.stillSince = crl::now();
	level.scrollTop = ScrollTop(state).value_or(-1);
	level.scrollChanges = 0;
	level.settled = false;
}

[[nodiscard]] bool Settled(const LiveState &state, LiveLevel &level) {
	const auto top = ScrollTop(state);
	const auto now = crl::now();
	if (!top) {
		return true;
	} else if (*top != level.scrollTop) {
		level.scrollTop = *top;
		level.stillSince = now;
		++level.scrollChanges;
	}
	level.settled = (now - level.stillSince >= kDateStill);
	return level.settled || (now - level.settleStarted >= kDateBound);
}

void AcceptWindow(LiveState &state, LiveLevel &level) {
	level.settledAt = crl::now();
	const auto shown = level.cardShow.get();
	if (!shown || !shown->shown()) {
		const auto sample = HostSample(level);
		if (!sample || !sample->dead) {
			Refuse(state, level, u"the card show refused: "_q
				+ ShowText(shown));
		}
		return;
	} else if (!level.settled) {
		Refuse(state, level, u"the list's scroll did not hold for %1 ms "
			u"within %2 ms (top %3, %4 changes), so the floating date may "
			u"cover the card"_q.arg(
				QString::number(kDateStill),
				QString::number(kDateBound),
				QString::number(level.scrollTop),
				QString::number(level.scrollChanges)));
		return;
	}
	level.accepted = ReadWindow(state, level.message->id);
	const auto problems = WindowProblems(state, level.accepted);
	if (!problems.isEmpty()) {
		Refuse(state, level, u"the setup left "_q + problems.join(u"; "_q));
	}
}

[[nodiscard]] QString AppliedText(const WidgetHostApplied &applied) {
	auto features = QStringList();
	for (const auto &feature : applied.features) {
		features.push_back(QString::fromStdString(feature));
	}
	const auto &area = applied.contentSafeArea;
	return QStringList{
		u"policy="_q + QString::fromStdString(applied.policy),
		u"features=["_q + features.join(u","_q) + u"]"_q,
		u"locale="_q + QString::fromStdString(applied.locale),
		u"safeArea=%1,%2,%3,%4,%5,%6"_q
			.arg(area.top)
			.arg(area.right)
			.arg(area.bottom)
			.arg(area.left)
			.arg(area.cornerLeft)
			.arg(area.cornerRight),
		u"memoryLimitBytes="_q
			+ QString::number(qint64(applied.memoryLimitBytes)),
		u"cpuLimitMs="_q + QString::number(applied.cpuLimitMs),
		u"cpuQuotaMs="_q + QString::number(applied.cpuQuotaMs),
		u"cpuQuotaWindowMs="_q + QString::number(applied.cpuQuotaWindowMs),
		u"cpuKillAfterMs="_q + QString::number(applied.cpuKillAfterMs),
		u"mainThreadQuotaMs="_q + QString::number(applied.mainThreadQuotaMs),
		u"devicePixelRatio="_q + QString::number(applied.devicePixelRatio),
		u"maxTimers="_q + QString::number(applied.maxTimers),
		u"visible="_q + Flag(applied.visible),
	}.join(QChar(' '));
}

// The launch's applied options are the first level's; every later level's
// host must have got the same.
void CheckApplied(
		LiveState &state,
		LiveLevel &level,
		const WidgetHostReading &reading) {
	if (reading.applied.empty()) {
		return;
	}
	const auto &applied = reading.applied.front();
	auto &launch = state.launch;
	if (!launch.applied) {
		launch.applied = StressApplied{ .options = applied };
		return;
	}
	const auto was = AppliedText(launch.applied->options);
	const auto now = AppliedText(applied);
	if (was != now) {
		AddError(level, u"config.applied: this level's host got {%1}, the "
			u"launch's first got {%2}"_q.arg(now, was));
	}
}

// manifest.run's width, the same for every level of the launch.
void CheckCardWidth(LiveState &state, LiveLevel &level) {
	const auto runtime = FixtureOwnRuntime(*state.fixture, level.message->id);
	const auto core = runtime ? runtime->core() : nullptr;
	const auto width = core ? core->width() : 0.;
	level.cardCssWidth = width;
	if (std::abs(width - state.launch.width) > kWidthTolerance) {
		AddError(level, u"width: the card's core is %1 CSS px wide, not "
			u"manifest.run's %2"_q.arg(
				QString::number(width),
				QString::number(state.launch.width)));
	}
	if (!state.launch.applied) {
		AddError(level, u"config.applied: no options call was recorded "
			u"for this level's host"_q);
		return;
	}
	auto &applied = *state.launch.applied;
	if (applied.cardCssWidth <= 0.) {
		applied.cardCssWidth = width;
	} else if (width != applied.cardCssWidth) {
		AddError(level, u"width: the card is %1 CSS px wide, the launch's "
			u"first measured level's %2"_q.arg(
				QString::number(width),
				QString::number(applied.cardCssWidth)));
	}
}

// The window's start: the window, applied options and card width checks
// first, then the update stamps, then the snapshot, its stamp taken after
// the host and the tap were read; the renderer's theme is compared after
// it, from the tap's reading the snapshot took.
void StartWindow(LiveState &state, LiveLevel &level) {
	auto &message = *level.message;
	level.windowStart = WindowChecks(state, level, u"start"_q);
	CheckApplied(state, level, ReadWidgetHost(*message.host));
	CheckCardWidth(state, level);
	if (state.filter) {
		state.filter->begin();
	}
	const auto latest = ReadWidgetOpsLatest(*message.tap);
	const auto reading = ReadWidgetHost(*message.host);
	const auto at = crl::profile();
	level.level.start = MakeSnapshot(reading, 0, latest, at);
	level.windowStarted = crl::now();
	CheckRendererTheme(state, level, latest);
}

[[nodiscard]] bool WindowElapsed(const LiveState &state, LiveLevel &level) {
	Observe(state, level);
	return level.level.start
		&& ((crl::profile() - level.level.start->at)
			>= state.launch.measureMs * 1000);
}

void EndWindow(LiveState &state, LiveLevel &level) {
	auto &message = *level.message;
	const auto latest = ReadWidgetOpsLatest(*message.tap);
	const auto reading = ReadWidgetHost(*message.host);
	const auto at = crl::profile();
	level.level.end = MakeSnapshot(reading, 0, latest, at);
	if (state.filter) {
		state.filter->end();
		level.level.updates = state.filter->stamps();
		level.updatesDropped = state.filter->dropped();
	}
	level.windowEnd = WindowChecks(state, level, u"end"_q);
}

void CheckOneHost(
		LiveLevel &level,
		const WidgetHostReading &reading,
		const WidgetOpsLatest &latest) {
	const auto &tap = level.message->tap;
	const auto ops = tap ? ReadWidgetOps(*tap) : WidgetOpsReading();
	const auto one = (reading.applied.size() == 1)
		&& (reading.hosts.size() == 1)
		&& (latest.generation == 1)
		&& (ops.factoriesSelected == 1);
	const auto details = u"optionsCalls=%1 hosts=%2 generation=%3 "
		u"factories=%4"_q.arg(
			QString::number(reading.applied.size()),
			QString::number(reading.hosts.size()),
			QString::number(latest.generation),
			QString::number(ops.factoriesSelected));
	Check(
		one,
		LevelName(level, u"one options call, one host and one tap "
			u"generation"_q),
		details);
	if (!one) {
		AddError(level, u"not one host: "_q + details);
	}
}

[[nodiscard]] QJsonObject SetupObject(const LiveLevel &level) {
	return QJsonObject{
		{ u"layoutMs"_q, Since(level.injectedAt, level.laidOutAt) },
		{ u"loadDecidedMs"_q, Since(level.injectedAt, level.decidedAt) },
		{ u"loadShow"_q, ShowObject(level.loadShow.get()) },
		{ u"cardShow"_q, ShowObject(level.cardShow.get()) },
		{ u"dateWaitMs"_q, Since(level.settleStarted, level.settledAt) },
		{ u"dateHeld"_q, level.settled },
		{ u"scrollChanges"_q, level.scrollChanges },
		{ u"setupMs"_q, Since(level.injectedAt, level.warmupStarted) },
		{ u"warmupMs"_q, Since(level.warmupStarted, level.windowStarted) },
		{ u"focusAndCursor"_q, level.focus },
	};
}

[[nodiscard]] QJsonObject LevelDesktop(
		const LiveLevel &level,
		const WidgetHostReading &reading,
		const WidgetOpsLatest &latest) {
	return QJsonObject{
		{ u"oneAlive"_q, level.oneAlive },
		{ u"setup"_q, SetupObject(level) },
		{
			u"window"_q,
			QJsonObject{
				{ u"start"_q, level.windowStart },
				{ u"end"_q, level.windowEnd },
				{ u"samples"_q, level.observations },
				{ u"sampleEveryMs"_q, qint64(kObserveEvery) },
				{ u"sampleCostMs"_q, level.observeCost / 1000. },
				{ u"changed"_q, level.changed },
			},
		},
		{
			u"hosts"_q,
			QJsonObject{
				{ u"optionsCalls"_q, int(reading.applied.size()) },
				{ u"attached"_q, int(reading.hosts.size()) },
				{ u"generation"_q, latest.generation },
			},
		},
		{
			u"cardCssWidth"_q,
			level.cardCssWidth ? QJsonValue(*level.cardCssWidth) : Null(),
		},
		{ u"scheme"_q, QString::fromStdString(latest.scheme) },
		{ u"updatesDropped"_q, level.updatesDropped },
	};
}

// After the window, or for a level without one: its final reading, the
// values (read at or after the window's end), the work log since 0, the
// one-host and applied checks, then the entry.
void WriteLevel(LiveState &state, LiveLevel &level) {
	auto &message = *level.message;
	auto &entry = level.level;
	const auto reading = message.host
		? ReadWidgetHost(*message.host)
		: WidgetHostReading();
	const auto latest = message.tap
		? ReadWidgetOpsLatest(*message.tap)
		: WidgetOpsLatest();
	if (!entry.start) {
		entry.end = MakeSnapshot(reading, 0, latest, crl::profile());
	}
	entry.values = latest.stress;
	entry.valuesDropped = latest.stressDropped;
	if (message.host) {
		entry.work = ReadWidgetWork(*message.host, 0);
	}
	if (!level.changed.isEmpty()) {
		AddError(level, u"window: the window or the card changed during the "
			u"warm-up or the window: "_q + level.changed);
	}
	if (level.updatesDropped > 0) {
		AddError(level, u"frames: %1 UpdateRequest stamps found the filter "
			u"full at %2"_q.arg(
				QString::number(level.updatesDropped),
				QString::number(kUpdateCapacity)));
	}
	if (level.injectedAt) {
		CheckOneHost(level, reading, latest);
		CheckApplied(state, level, reading);
	}
	entry.desktop = LevelDesktop(level, reading, latest);
	const auto computed = ComputeEntry(entry);
	state.entries.push_back(computed);
	level.written = true;
	const auto &object = computed.object;
	Note(u"STRESS_ENTRY: "_q + Compact(QJsonObject{
		{ u"id"_q, entry.id },
		{ u"loaded"_q, object.value(u"loaded"_q) },
		{ u"dead"_q, object.value(u"dead"_q) },
		{ u"measured"_q, entry.start.has_value() },
		{ u"frames"_q, object.value(u"frames"_q) },
		{ u"renderer"_q, object.value(u"renderer"_q) },
		{
			u"widgetFps"_q,
			object.value(u"widget"_q).toObject().value(u"fps"_q),
		},
	}));
	Check(
		computed.runnerErrors.isEmpty(),
		LevelName(level, u"the entry has no runner error"_q),
		computed.runnerErrors.join(u"; "_q));
	if (!computed.runnerErrors.isEmpty() && state.abort.isEmpty()) {
		state.abort = u"[%1] %2"_q.arg(
			entry.id,
			computed.runnerErrors.join(u"; "_q));
	}
}

void CheckBarrier(LiveState &state, LiveLevel &level) {
	const auto &tap = level.message->tap;
	const auto ops = tap ? ReadWidgetOps(*tap) : WidgetOpsReading();
	const auto finished = tap && WidgetTapFinished(ops);
	const auto details = u"factories selected=%1 released=%2 wrappers "
		u"built=%3 destroyed=%4"_q.arg(
			QString::number(ops.factoriesSelected),
			QString::number(ops.factoriesReleased),
			QString::number(ops.wrappersBuilt),
			QString::number(ops.wrappersDestroyed));
	Check(
		finished,
		LevelName(level, u"the widget's host is gone before another "
			u"injection"_q),
		details);
	if (!finished && state.abort.isEmpty()) {
		state.abort = u"[%1] the host barrier did not pass: %2"_q.arg(
			level.level.id,
			details);
	}
}

// ---------------------------------------------------------------------
// The launch.

void ReleaseFilter(LiveState &state) {
	if (!state.filter) {
		return;
	}
	state.launch.record.insert(u"updateFilter"_q, QJsonObject{
		{ u"delivered"_q, qint64(state.filter->delivered()) },
		{ u"capacity"_q, kUpdateCapacity },
	});
	state.filter = nullptr;
}

[[nodiscard]] QStringList LaunchRefusals(const LiveState &state) {
	auto result = QStringList();
	const auto gate = WidgetFixtureGate(*state.fixture);
	if (!gate.isEmpty()) {
		result.push_back(u"fixture gate: "_q + gate);
	}
	if (!state.setupError.isEmpty()) {
		result.push_back(state.setupError);
	}
	if (!state.abort.isEmpty()) {
		result.push_back(u"the ladder was stopped: "_q + state.abort);
	}
	if (const auto failures = FailureCount()) {
		result.push_back(u"the launch logged %1 failure(s)"_q.arg(failures));
	}
	result += AcceptanceRefusals(
		state.package.manifest,
		state.package.selection,
		state.entries);
	return result;
}

void LogLaunch(
		const LiveState &state,
		const StressPublication &publication) {
	const auto size = publication.path.isEmpty()
		? Null()
		: QJsonValue(qint64(QFileInfo(publication.path).size()));
	Note(u"STRESS_LAUNCH: "_q + Compact(QJsonObject{
		{ u"pass"_q, PassName(state.args.pass) },
		{ u"loads"_q, QJsonArray::fromStringList(state.launch.loads) },
		{ u"selected"_q, int(state.levels.size()) },
		{ u"entries"_q, int(state.entries.size()) },
		{ u"merged"_q, publication.merged },
		{ u"output"_q, publication.path },
		{ u"size"_q, size },
		{ u"refusal"_q, publication.error },
	}));
}

StressPublication Publish(LiveState &state, QStringList refusals) {
	state.published = true;
	if (!state.abort.isEmpty()) {
		state.launch.record.insert(u"stopped"_q, state.abort);
	}
	const auto publication = PublishLaunch(
		state.args.output,
		state.package.manifest,
		state.launch,
		state.entries,
		std::move(refusals));
	LogLaunch(state, publication);
	return publication;
}

void PublishStage(LiveState &state) {
	const auto publication = Publish(state, LaunchRefusals(state));
	Check(
		publication.merged,
		Prefixed(u"the launch is merged into the pass's run file"_q),
		publication.merged ? publication.path : publication.error);
}

// Runner::onFinish: on every path, the filter is released, the kept day
// theme the runner replaced is kept again and the fixture (its taps and
// host watches, and so the work registry) released; a launch that never
// reached its publish stage writes what it has to its launch file, never
// to the run file.
void FinishLaunch(LiveState &state) {
	ReleaseFilter(state);
	RestoreTheme(state);
	Note(Prefixed(u"onFinish release: "_q)
		+ ReleaseWidgetFixture(*state.fixture, u"onFinish"_q));
	if (state.published || !state.package.error.isEmpty()) {
		return;
	}
	auto refusals = LaunchRefusals(state);
	refusals.push_front(u"the launch ended before its publish stage"_q);
	Publish(state, std::move(refusals));
}

void ReportPackage(const LiveState &state) {
	const auto &package = state.package;
	const auto ok = package.error.isEmpty();
	const auto details = ok
		? u"pass=%1 loads=[%2] levels=%3 warmupMs=%4 measureMs=%5 output=%6"_q
			.arg(PassName(state.args.pass))
			.arg(package.selection.loads.join(u","_q))
			.arg(int(package.selection.widgets.size()))
			.arg(state.launch.warmupMs)
			.arg(state.launch.measureMs)
			.arg(state.args.output)
		: package.error;
	Check(
		ok,
		Prefixed(u"the package, the loads and the output are usable"_q),
		details);
}

void AddLevels(LiveState &state) {
	const auto &manifest = state.package.manifest;
	const auto &selected = state.package.selection.widgets;
	for (auto i = 0; i != int(selected.size()); ++i) {
		const auto &widget = manifest.widgets[selected[i]];
		auto level = LiveLevel();
		level.widget = selected[i];
		level.level.id = widget.id;
		level.level.load = widget.load;
		level.level.value = widget.value;
		level.level.index = widget.index;
		level.message = AddWidgetFixtureMessage(*state.fixture, widget.id);
		level.lastOfLoad = (i + 1 == int(selected.size()))
			|| (manifest.widgets[selected[i + 1]].load != widget.load);
		state.levels.push_back(std::move(level));
	}
}

// ---------------------------------------------------------------------
// The stages.

void AppendSetup(
		not_null<Runner*> runner,
		const std::shared_ptr<LiveState> &state) {
	state->palette = AppendPaletteSwitch(runner, {
		.name = Prefixed(u"the day palette"_q),
		.mode = PaletteMode::Day,
	});
	runner->add({
		.name = Prefixed(u"keep the default day theme"_q),
		.run = [=] {
			ApplyDefaultTheme(*state);
		},
		.until = [=] {
			return !state->setupError.isEmpty() || PaletteHeld(*state);
		},
		.then = [=] {
			CheckDefaultTheme(*state);
		},
		.timeout = kThemeBound + kDefaultStageTimeout,
	});
	runner->add({
		.name = Prefixed(u"place the window on the fastest screen"_q),
		.run = [=] {
			PlaceWindow(*state);
		},
		.until = [=] {
			return WindowPlaced(*state);
		},
		.then = [=] {
			CheckPlacement(*state);
		},
		.timeout = kPlaceBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return state->sample;
		},
	});
	runner->add({
		.name = Prefixed(u"check the console and the window's exposure"_q),
		.run = [=] {
			CheckEnvironment(*state);
		},
	});
}

void AppendShows(
		not_null<Runner*> runner,
		const std::shared_ptr<LiveState> &state,
		int index,
		bool card) {
	const auto entry = [=]() -> LiveLevel& {
		return state->levels[index];
	};
	const auto id = entry().level.id;
	auto args = ChatListShowArgs{
		.name = u"widget stress %1 %2"_q.arg(id, card ? u"card"_q : u"load"_q),
		.resolve = [=] {
			return ChatListShowTarget{
				WidgetFixtureList(*state->fixture),
				entry().message->id,
			};
		},
		.rect = [](ChatListShowView view) {
			return WidgetCardLocal(view);
		},
		.readyDetails = [](ChatListShowView view) {
			return WidgetCardText(ReadWidgetCard(view));
		},
		.timeout = kShowBound,
	};
	if (card) {
		args.ready = [](ChatListShowView view) {
			return WidgetCardReady(ReadWidgetCard(view));
		};
		args.skipReason = [=] {
			return WindowSkip(entry());
		};
		entry().cardShow = AppendShowInChatList(runner, std::move(args));
	} else {
		args.ready = [](ChatListShowView view) {
			const auto read = ReadWidgetCard(view);
			return read.html && read.runtime;
		};
		args.skipReason = [=] {
			return entry().notRun;
		};
		entry().loadShow = AppendShowInChatList(runner, std::move(args));
	}
}

// The D5 check, the injection, the load show and the load wait.
void AppendLevelStart(
		not_null<Runner*> runner,
		const std::shared_ptr<LiveState> &state,
		int index) {
	const auto entry = [=]() -> LiveLevel& {
		return state->levels[index];
	};
	const auto skip = [=] {
		return entry().notRun;
	};
	runner->add({
		.name = LevelName(entry(), u"check that no other widget is alive"_q),
		.run = [=] {
			CheckOneAlive(*state, entry());
		},
	});
	runner->add({
		.name = LevelName(entry(), u"inject the watched widget"_q),
		.skipReason = skip,
		.run = [=] {
			InjectLevel(*state, entry());
		},
		.until = [=] {
			return !entry().notRun.isEmpty()
				|| WidgetMessageLaidOut(*state->fixture, *entry().message);
		},
		.then = [=] {
			CheckLaidOut(*state, entry());
		},
		.timeout = kWidgetLayoutBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return entry().message->wait.details;
		},
	});
	AppendShows(runner, state, index, false);
	runner->add({
		.name = LevelName(entry(), u"wait for loaded or dead"_q),
		.skipReason = skip,
		.until = [=] {
			return LoadDecided(entry());
		},
		.then = [=] {
			CheckLoaded(*state, entry());
		},
		.timeout = kLoadBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return entry().message->wait.details;
		},
	});
}

// The card show, the scroll and date settle, the warm-up and the window.
void AppendLevelWindow(
		not_null<Runner*> runner,
		const std::shared_ptr<LiveState> &state,
		int index) {
	const auto entry = [=]() -> LiveLevel& {
		return state->levels[index];
	};
	const auto skip = [=] {
		return WindowSkip(entry());
	};
	AppendShows(runner, state, index, true);
	runner->add({
		.name = LevelName(entry(), u"settle the list and its floating date"_q),
		.skipReason = skip,
		.run = [=] {
			StartSettle(*state, entry());
		},
		.until = [=] {
			return Settled(*state, entry());
		},
		.then = [=] {
			AcceptWindow(*state, entry());
		},
		.timeout = kDateBound + kDefaultStageTimeout,
	});
	runner->add({
		.name = LevelName(entry(), u"warm up"_q),
		.skipReason = skip,
		.run = [=] {
			entry().warmupStarted = crl::now();
			entry().lastObserved = 0;
		},
		.until = [=] {
			Observe(*state, entry());
			return (crl::now() - entry().warmupStarted)
				>= state->launch.warmupMs;
		},
		.timeout = state->launch.warmupMs + kDefaultStageTimeout,
	});
	runner->add({
		.name = LevelName(entry(), u"measure"_q),
		.skipReason = skip,
		.run = [=] {
			StartWindow(*state, entry());
		},
		.until = [=] {
			return WindowElapsed(*state, entry());
		},
		.then = [=] {
			EndWindow(*state, entry());
		},
		.timeout = state->launch.measureMs + kDefaultStageTimeout,
	});
}

// The entry, the removal and its barrier, then the pause: recovery stages,
// so a level that failed is still written and destroyed.
void AppendLevelEnd(
		not_null<Runner*> runner,
		const std::shared_ptr<LiveState> &state,
		int index) {
	const auto entry = [=]() -> LiveLevel& {
		return state->levels[index];
	};
	runner->addRecovery({
		.name = LevelName(entry(), u"write the entry"_q),
		.skipReason = [=] {
			return entry().started
				? QString()
				: u"the level did not start"_q;
		},
		.run = [=] {
			WriteLevel(*state, entry());
		},
	});
	runner->addRecovery({
		.name = LevelName(entry(), u"remove the widget and wait for its "
			u"host"_q),
		.skipReason = [=] {
			return entry().message->tap
				? QString()
				: u"nothing was armed or injected"_q;
		},
		.run = [=] {
			RemoveFixtureMessage(*state->fixture, *entry().message);
		},
		.until = [=] {
			return FixtureBarrierDecided(*entry().message);
		},
		.then = [=] {
			CheckBarrier(*state, entry());
		},
		.timeout = kWidgetBarrierBound + kDefaultStageTimeout,
	});
	const auto last = entry().lastOfLoad;
	const auto pause = last ? kLoadCooldown : kLevelPause;
	auto stage = Stage{
		.name = last
			? Prefixed(u"cool down after the last level of %1"_q.arg(
				entry().level.load))
			: LevelName(entry(), u"pause before the next level"_q),
		.run = [=] {
			entry().pauseStarted = crl::now();
		},
		.until = [=] {
			return (crl::now() - entry().pauseStarted) >= pause;
		},
		.timeout = pause + kDefaultStageTimeout,
	};
	if (!last) {
		runner->add(std::move(stage));
		return;
	}
	const auto load = entry().level.load;
	stage.skipReason = [=] {
		const auto started = std::any_of(
			state->levels.begin(),
			state->levels.end(),
			[&](const LiveLevel &level) {
				return (level.level.load == load) && level.injectedAt;
			});
		return started
			? QString()
			: u"no level of %1 was injected"_q.arg(load);
	};
	runner->addRecovery(std::move(stage));
}

// After the ladders: the day theme the launch found is kept again before
// the palette is restored, the filter released, the fixture closed, then
// the launch is published.
void AppendClose(
		not_null<Runner*> runner,
		const std::shared_ptr<LiveState> &state) {
	runner->add({
		.name = Prefixed(u"keep the day theme the launch found again"_q),
		.skipReason = [=] {
			return state->themeBefore
				? QString()
				: u"the runner did not change the kept theme"_q;
		},
		.run = [=] {
			RestoreTheme(*state);
			state->waitStarted = state->stableSince = crl::now();
			state->sample = QString();
		},
		.until = [=] {
			return PaletteHeld(*state);
		},
		.then = [=] {
			CheckThemeRestored(*state);
		},
		.timeout = kThemeBound + kDefaultStageTimeout,
	});
	state->paletteRestore = AppendPaletteRestore(
		runner,
		state->palette,
		Prefixed(u"the day palette"_q));
	runner->add({
		.name = Prefixed(u"judge the palette restore"_q),
		.run = [=] {
			JudgePalette(*state);
		},
	});
	runner->add({
		.name = Prefixed(u"release the window's update filter"_q),
		.run = [=] {
			ReleaseFilter(*state);
		},
	});
	AppendWidgetFixtureClose(runner, state->fixture);
	runner->add({
		.name = Prefixed(u"publish the launch"_q),
		.run = [=] {
			PublishStage(*state);
		},
	});
}

} // namespace
} // namespace Test::Stress

namespace Test {

void AppendWidgetStress(not_null<Runner*> runner, WidgetStressArgs args) {
	using namespace Stress;

	const auto state = std::make_shared<LiveState>();
	state->args = std::move(args);
	state->package = ReadStressPackage(state->args);
	state->fixture = MakeWidgetFixture({
		.chat = state->args.chat,
		.prefix = Prefixed(QString()),
		.marker = u"STRESS"_q,
	});
	if (state->package.error.isEmpty()) {
		state->launch = MakeLaunch(
			state->args,
			state->package.manifest,
			state->package.selection);
		state->launch.notes.push_front(QString::fromLatin1(kObserverNote));
		AddLevels(*state);
	}

	// Registered before anything is changed, and so before the palette
	// module's own release, which restores the palette after it.
	runner->onFinish([=] {
		FinishLaunch(*state);
	});

	runner->add({
		.name = Prefixed(u"report the package, the loads and the output"_q),
		.run = [=] {
			ReportPackage(*state);
		},
	});
	if (!state->package.error.isEmpty()) {
		return;
	}
	runner->addGated([=] {
		return LadderSkip(*state);
	}, [&] {
		AppendWidgetFixtureOpen(runner, state->fixture);
		AppendSetup(runner, state);
		for (auto i = 0; i != int(state->levels.size()); ++i) {
			AppendLevelStart(runner, state, i);
			AppendLevelWindow(runner, state, i);
			AppendLevelEnd(runner, state, i);
		}
	});
	AppendClose(runner, state);
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
