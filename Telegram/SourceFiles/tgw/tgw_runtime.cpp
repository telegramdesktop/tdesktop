/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_runtime.h"

#include "base/event_filter.h"
#include "base/qt_signal_producer.h"
#include "core/src/renderer.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "platform/platform_specific.h"
#include "test/test_widget_bot.h"
#include "test/test_widget_host.h"
#include "test/test_widget_ops.h"
#include "tgw/tgw_bridge.h"
#include "tgw/tgw_emoji_images.h"
#include "tgw/tgw_intl.h"
#include "tgw/tgw_media.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_resources.h"
#include "tgw/tgw_state.h"
#include "tgw/tgw_storage.h"
#include "tgw/tgw_theme.h"
#include "tgw/tgw_wallet.h"
#include "twidget/sdk_policy.h"
#include "twidget/widget_host.h"
#include "ui/style/style_core_scale.h"
#include "wallet/wallet_panel.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtCore/QStringDecoder>
#else // Qt >= 6.0.0
#include <QtCore/QTextCodec>
#endif // Qt < 6.0.0

#include "styles/style_tgw.h"

namespace Tgw {

struct MessageMemory {
	std::optional<double> height;
	QByteArray html;
	// WHY: equal bytes prove no ownership after an A -> B -> A edit, so each
	// runtime keeps the serial of the content it was built for, and a host of
	// older content never changes the crash count of the newer content.
	uint64 contentSerial = 0;
	int crashes = 0;
	ResizeAnchor anchor = ResizeAnchor::Top;
};

namespace {

constexpr auto kMaxAlive = 8;
// WHY: a card scrolled back into view shows its kept picture instead of
// painting it again in the middle of the scroll, within this many bytes.
constexpr auto kKeptPictureBytes = int64(48) * 1024 * 1024;
constexpr auto kMaxCrashes = 2;
constexpr auto kMaxNotes = 100;
constexpr auto kMaxLogLine = 240;
constexpr auto kMaxNoteLength = kMaxLogLine + 16;
constexpr auto kMaxLogDetails = 2;
constexpr auto kHostLogLimit = 5000;
constexpr auto kStateBytes = 8 * 1024;
constexpr auto kKindsBytes = 16 * 1024;
constexpr auto kConsoleBytes = 24 * 1024;
constexpr auto kBridgeBytes = 16 * 1024;
constexpr auto kOmittedBytes = 128;
constexpr auto kSnapshotBytes = 64 * 1024;
constexpr auto kNotesBytes = 56 * 1024;
constexpr auto kLogBytes = 128 * 1024;

static_assert(kStateBytes + kKindsBytes + kConsoleBytes + kBridgeBytes
	== kSnapshotBytes);
static_assert(kMaxNotes * kMaxNoteLength * int(sizeof(QChar))
	<= kNotesBytes);
static_assert(kSnapshotBytes + kNotesBytes < kLogBytes);

#ifdef _DEBUG
constexpr auto kCpuLimitScale = 4.; // unoptimized Debug QuickJS runs ~3x slower
#else
constexpr auto kCpuLimitScale = 1.;
#endif // _DEBUG

class Reservation final {
public:
	explicit Reservation(Fn<void()> released);
	Reservation(const Reservation &other) = delete;
	Reservation &operator=(const Reservation &other) = delete;
	~Reservation();

private:
	Fn<void()> _released;

};

using Slot = std::shared_ptr<Reservation>;

struct HostDeleter {
	Slot slot;

	void operator()(twidget::WidgetHost *host) const;
};

using HostPointer = std::shared_ptr<twidget::WidgetHost>;

struct Job {
	QByteArray html;
	std::shared_ptr<twidget::SandboxResources> resources;
	std::shared_ptr<twidget::IntlProvider> intl;
	twidget::RendererFactory factory;
	Fn<void(const twidget::WidgetAction &)> action;
	RendererCore::RequestHandler request;
	RendererCore::WalletHandler wallet;
	RendererCore::CopyHandler copy;
	RendererCore::WatchHandler watch;
	Fn<void(
		const std::string &,
		const std::map<std::string, std::string> &)> storage;
	twidget::WidgetHostOptions options;
	twidget::WidgetTheme theme;
	std::string stoppedLabel;
	Slot slot;
	double width = 0.;
};

struct Built {
	HostPointer host;
	twidget::WidgetData data;
	QString error;
};

using MessageMemories = base::flat_map<
	not_null<const HistoryItem*>,
	std::shared_ptr<MessageMemory>>;

struct Registry {
	std::vector<not_null<Runtime*>> list;
	base::flat_map<not_null<Main::Session*>, MessageMemories> sessions;
	Fn<void()> exitRetry;
	rpl::lifetime applicationLifetime;
	uint64 hiddenSerial = 0;
	uint64 keptSerial = 0;
	uint64 queueSerial = 0;
	uint64 hostSerial = 0;
	uint64 requestSerial = 0;
	int reserved = 0;
	int jobs = 0;
	bool checkPosted = false;
	bool drainPosted = false;
	bool draining = false;
	bool applicationHidden = false;
};

[[nodiscard]] Registry &Runtimes() {
	static auto result = Registry();
	return result;
}

void RememberHtml(MessageMemory &memory, const QByteArray &html) {
	if (memory.html != html) {
		memory.html = html;
		++memory.contentSerial;
		memory.crashes = 0;
	}
}

void RememberItemHtml(
		not_null<Main::Session*> session,
		not_null<const HistoryItem*> item) {
	auto &sessions = Runtimes().sessions;
	const auto i = sessions.find(session);
	if (i == end(sessions)) {
		return;
	}
	const auto j = i->second.find(item);
	if (j == end(i->second)) {
		return;
	} else if (const auto media = dynamic_cast<WidgetMedia*>(item->media())) {
		RememberHtml(*j->second, media->html());
	}
}

[[nodiscard]] bool AutoHeight(not_null<const HistoryItem*> item) {
	const auto media = dynamic_cast<WidgetMedia*>(item->media());
	return media && (media->fields().anchors.height <= 0);
}

void StoreMemory(not_null<HistoryItem*> item, const MessageMemory &memory) {
	if (memory.height && AutoHeight(item)) {
		StoreHeight(item, {
			.height = *memory.height,
			.bottom = (memory.anchor == ResizeAnchor::Bottom),
		});
	}
}

void StoreItemMemory(
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item) {
	const auto &sessions = Runtimes().sessions;
	const auto i = sessions.find(session);
	if (i == end(sessions)) {
		return;
	}
	const auto j = i->second.find(item);
	if (j != end(i->second)) {
		StoreMemory(item, *j->second);
	}
}

[[nodiscard]] bool Busy() {
	const auto &registry = Runtimes();
	return (registry.jobs > 0) || (registry.reserved > 0);
}

void Drained() {
	auto &registry = Runtimes();
	registry.drainPosted = false;
	if (Busy()) {
		return;
	}
	LOG(("Tgw doesn't prevent quit any more."));
	if (const auto retry = base::take(registry.exitRetry)) {
		retry();
	} else if (Core::IsAppLaunched()) {
		Core::App().quitPreventFinished();
	}
}

void CheckDrained() {
	auto &registry = Runtimes();
	if (registry.draining && !registry.drainPosted && !Busy()) {
		registry.drainPosted = true;
		crl::on_main([] {
			Drained();
		});
	}
}

void JobDone() {
	--Runtimes().jobs;
	CheckDrained();
}

Reservation::Reservation(Fn<void()> released)
: _released(std::move(released)) {
	++Runtimes().reserved;
}

Reservation::~Reservation() {
	crl::on_main([released = std::move(_released)] {
		--Runtimes().reserved;
		released();
		CheckDrained();
	});
}

void HostDeleter::operator()(twidget::WidgetHost *host) const {
	// WHY: ~WidgetHost joins its engine and host threads for up to a CPU
	// slice, so every owner, dropped guarded posts included, deletes it on
	// a worker, and the slot is freed only after that join has finished.
	crl::async([host, slot = slot]() mutable {
		delete host;
		slot = nullptr;
	});
}

[[nodiscard]] double CoreRatio() {
	return style::DevicePixelRatio() * style::Scale() / 100.;
}

[[nodiscard]] ResizeAnchor AnchorOf(const RendererCore &core) {
	return core.bottomAnchored() ? ResizeAnchor::Bottom : ResizeAnchor::Top;
}

void ApplyTheme(
		twidget::WidgetHost &host,
		const twidget::WidgetTheme &theme) {
	const auto apply = [&] {
		host.onHostThread([&] {
			host.setTheme(theme);
		});
	};
	apply();
	// WHY: the engine may still post a batch validated in the old theme, so
	// the styles are replayed again behind a barrier on its thread; probe()
	// wraps its code in String(), shadowed here to call no widget global.
	host.probe("0); function String() { return ''; } return (0");
	apply();
}

[[nodiscard]] twidget::WidgetHostOptions DefaultHostOptions(
		double ratio,
		bool visible) {
	auto result = twidget::WidgetHostOptions{
		.visible = visible,
		.devicePixelRatio = ratio,
		.policy = "strict",
		.features = { "webAnimations" },
		.locale = UiLanguageTag().toStdString(),
	};
	result.cpuLimitMs *= kCpuLimitScale;
	// Both quotas share the scaled window, so each scales to keep its rate.
	result.cpuQuotaMs *= kCpuLimitScale;
	result.mainThreadQuotaMs *= kCpuLimitScale;
	result.cpuQuotaWindowMs *= kCpuLimitScale;
	return result;
}

[[nodiscard]] std::optional<std::string> HostBytes(
		const std::optional<QByteArray> &value) {
	return value
		? std::make_optional(
			std::string(value->constData(), size_t(value->size())))
		: std::nullopt;
}

[[nodiscard]] twidget::WidgetData HostData(not_null<HistoryItem*> item) {
	const auto media = dynamic_cast<WidgetMedia*>(item->media());
	if (!media) {
		return {};
	}
	const auto &state = media->fields().state;
	return {
		.chatData = HostBytes(state.chatData),
		.publicData = HostBytes(state.publicData),
	};
}

[[nodiscard]] twidget::WidgetContentSafeArea HostSafeArea(
		ContentSafeArea area) {
	return {
		.bottom = double(area.bottom),
		.cornerLeft = double(area.cornerLeft),
		.cornerRight = double(area.cornerRight),
	};
}

[[nodiscard]] std::optional<std::string> ViewerText(const QString &value) {
	return value.isEmpty()
		? std::nullopt
		: std::make_optional(value.toStdString());
}

[[nodiscard]] twidget::WidgetViewer HostViewer(not_null<UserData*> user) {
	return {
		.id = int64(peerToUser(user->id).bare),
		.firstName = ViewerText(user->firstName),
		.lastName = ViewerText(user->lastName),
		.username = ViewerText(user->username()),
		.isPremium = user->isPremium(),
	};
}

[[nodiscard]] std::string ChatTypeName(ChatType type) {
	switch (type) {
	case ChatType::None: return std::string();
	case ChatType::Private: return "private";
	case ChatType::Group: return "group";
	case ChatType::Supergroup: return "supergroup";
	case ChatType::Channel: return "channel";
	}
	Unexpected("ChatType in Tgw::ChatTypeName.");
}

[[nodiscard]] twidget::WidgetContext HostContext(
		const MessageContext &context,
		not_null<UserData*> self) {
	return {
		.isCopy = context.flags.isCopy,
		.canQuery = context.flags.canQuery,
		.backgroundQuery = context.flags.backgroundQuery,
		.chatType = ChatTypeName(context.chatType),
		.viewer = (context.flags.canQuery
			? std::make_optional(HostViewer(self))
			: std::nullopt),
		.wallet = context.wallet,
	};
}

[[nodiscard]] QString Clip(QString text, int length = kMaxLogLine) {
	if (text.size() <= length) {
		return text;
	}
	const auto ellipsis = u"..."_q;
	text.truncate(length - ellipsis.size());
	if (text.at(text.size() - 1).isHighSurrogate()) {
		text.chop(1);
	}
	return text + ellipsis;
}

[[nodiscard]] QString Clip(const std::string &text) {
	const auto size = std::min(text.size(), size_t(kMaxLogLine) * 4);
	return Clip(QString::fromUtf8(text.data(), int(size)));
}

// fromStdString() strips a leading U+FEFF, and the copy must be exact.
[[nodiscard]] QString DecodeCopiedText(const std::string &text) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	auto decoder = QStringDecoder(
		QStringDecoder::Utf8,
		QStringDecoder::Flag::Stateless
			| QStringDecoder::Flag::ConvertInitialBom);
	return decoder(text);
#else // Qt >= 6.0.0
	auto state = QTextCodec::ConverterState(QTextCodec::IgnoreHeader);
	return QTextCodec::codecForName("UTF-8")->toUnicode(
		text.data(),
		int(text.size()),
		&state);
#endif // Qt < 6.0.0
}

[[nodiscard]] QString YesNo(bool value) {
	return value ? u"yes"_q : u"no"_q;
}

[[nodiscard]] QString Milliseconds(double value) {
	return u"%1 ms"_q.arg(QString::number(value, 'f', 0));
}

[[nodiscard]] QString Milliseconds(std::optional<double> value) {
	return value ? Milliseconds(*value) : u"-"_q;
}

[[nodiscard]] QString StateName(Runtime::State state) {
	using State = Runtime::State;
	switch (state) {
	case State::Idle: return u"idle"_q;
	case State::Waiting: return u"waiting for resources"_q;
	case State::Constructing: return u"constructing"_q;
	case State::Live: return u"live"_q;
	case State::Released: return u"released"_q;
	case State::Stopped: return u"stopped"_q;
	case State::Failed: return u"failed"_q;
	}
	Unexpected("State in Tgw::StateName.");
}

[[nodiscard]] int Bytes(const QString &line) {
	return (int(line.size()) + 1) * int(sizeof(QChar));
}

[[nodiscard]] bool Take(int &left, const QString &line) {
	const auto bytes = Bytes(line);
	if (bytes > left) {
		return false;
	}
	left -= bytes;
	return true;
}

void AppendState(
		QStringList &lines,
		const twidget::WidgetDiagnostics &diagnostics) {
	auto left = kStateBytes;
	const auto add = [&](QString line) {
		if (Take(left, line)) {
			lines.push_back(std::move(line));
		}
	};
	if (diagnostics.dead) {
		add(u"Died: "_q + Clip(*diagnostics.dead));
	}
	if (diagnostics.lastError) {
		add(u"Last error: "_q + Clip(*diagnostics.lastError));
	}
	add(u"Loaded: %1, rendered: %2, nodes: %3, height: %4."_q.arg(
		YesNo(diagnostics.loaded),
		YesNo(diagnostics.rendered),
		QString::number(diagnostics.nodes),
		QString::number(diagnostics.height)));
	add(u"Timing: renderer ready %1, loaded %2, rendered %3, "
		"first content %4."_q.arg(
			Milliseconds(diagnostics.rendererReadyMs),
			Milliseconds(diagnostics.loadedMs),
			Milliseconds(diagnostics.renderedMs),
			Milliseconds(diagnostics.firstContentMs)));
	const auto &stats = diagnostics.stats;
	add(u"Engine: %1 tasks, %2 of JS, %3 KB of memory, "
		"%4 throttled tasks."_q.arg(
			QString::number(stats.tasks),
			Milliseconds(stats.jsMs),
			QString::number(stats.memoryBytes / 1024),
			QString::number(stats.throttledTasks)));
	add(u"Renderer: %1 ops, %2 queries in %3 (%4 cached), "
		"%5 missed mutations."_q.arg(
			QString::number(stats.ops),
			QString::number(stats.queries),
			Milliseconds(stats.queryMs),
			QString::number(stats.queryCacheHits),
			QString::number(stats.missedMutations)));
}

void AppendKinds(
		QStringList &lines,
		const twidget::WidgetDiagnostics &diagnostics) {
	const auto kinds = int(diagnostics.diags.size());
	auto left = kKindsBytes - kOmittedBytes;
	auto header = u"Diagnostics: %1 kinds."_q.arg(kinds);
	if (Take(left, header)) {
		lines.push_back(std::move(header));
	}
	auto shown = 0;
	for (const auto &[kind, entry] : diagnostics.diags) {
		const auto &[count, details] = entry;
		auto block = QStringList{
			u"%1 x%2"_q.arg(Clip(kind), QString::number(count)),
		};
		const auto take = std::min(int(details.size()), kMaxLogDetails);
		for (auto i = 0; i != take; ++i) {
			block.push_back(u"  "_q + Clip(details[i]));
		}
		auto bytes = 0;
		for (const auto &line : block) {
			bytes += Bytes(line);
		}
		if (bytes > left) {
			break;
		}
		left -= bytes;
		lines.append(block);
		++shown;
	}
	if (shown < kinds) {
		lines.push_back(u"... %1 more kinds not shown."_q.arg(kinds - shown));
	}
}

void AppendTail(
		QStringList &lines,
		QString header,
		const std::vector<std::pair<std::string, std::string>> &entries,
		int budget) {
	auto left = budget - kOmittedBytes;
	if (Take(left, header)) {
		lines.push_back(std::move(header));
	}
	auto tail = QStringList();
	auto from = int(entries.size());
	while (from > 0) {
		const auto &[first, second] = entries[from - 1];
		auto line = u"%1: %2"_q.arg(Clip(first), Clip(second));
		if (!Take(left, line)) {
			break;
		}
		tail.push_back(std::move(line));
		--from;
	}
	if (from > 0) {
		lines.push_back(u"... %1 earlier entries not shown."_q.arg(from));
	}
	for (auto i = tail.size(); i != 0;) {
		lines.push_back(tail[--i]);
	}
}

[[nodiscard]] QString Summarize(const twidget::WidgetHost &host) {
	const auto diagnostics = host.diagnostics();
	auto lines = QStringList();
	AppendState(lines, diagnostics);
	AppendKinds(lines, diagnostics);
	const auto logs = int(diagnostics.logs.size());
	const auto console = (logs < kHostLogLimit)
		? u"Console: %1 entries."_q
		: u"Console: %1 entries, the host dropped all later ones."_q;
	AppendTail(lines, console.arg(logs), diagnostics.logs, kConsoleBytes);
	AppendTail(
		lines,
		u"Bridge calls: %1."_q.arg(int(diagnostics.bridge.size())),
		diagnostics.bridge,
		kBridgeBytes);
	return lines.join(QChar('\n'));
}

[[nodiscard]] Built Build(Job job) {
	try {
		auto host = HostPointer(
			new twidget::WidgetHost(
				std::string(job.html.constData(), size_t(job.html.size())),
				std::move(job.theme),
				job.width,
				job.options,
				std::move(job.resources),
				std::move(job.factory),
				std::move(job.intl)),
			HostDeleter{ std::move(job.slot) });
		host->onAction = std::move(job.action);
		host->onRequest = std::move(job.request);
		host->onWallet = std::move(job.wallet);
		host->onCopyText = std::move(job.copy);
		host->onWatchPublicData = std::move(job.watch);
		host->onStorageCommit = std::move(job.storage);
		host->stoppedLabel = std::move(job.stoppedLabel);
		return {
			.host = std::move(host),
			.data = std::move(job.options.data),
		};
	} catch (const std::exception &e) {
		return {
			.error = u"Could not create the sandbox: %1"_q.arg(
				QString::fromUtf8(e.what())),
		};
	}
}

} // namespace

struct Runtime::Host {
	HostPointer pointer;
	base::weak_ptr<Runtime> owner;
	uint64 id = 0;
	uint64 storage = 0;
	bool sampling = false;
	bool theming = false;
	bool retired = false;
	bool watching = false;
};

Runtime::Runtime(RuntimeDescriptor &&descriptor)
: _view(descriptor.view)
, _item(descriptor.item)
, _html(std::move(descriptor.html))
, _context(descriptor.context)
, _resize(std::move(descriptor.resize))
, _repaint(std::move(descriptor.repaint))
, _heavyChanged(std::move(descriptor.heavyChanged))
, _bridge(std::make_unique<Bridge>(descriptor.item))
, _memory(RememberMessage(descriptor.item, _html))
, _theme(std::make_shared<twidget::WidgetTheme>())
, _hostTheme(_theme)
, _contentSerial(_memory->contentSerial)
, _maxHeight(kMaxHeight) {
	Expects(!_html.isEmpty());

	_shownHeight = height();
	auto &registry = Runtimes();
	if (registry.list.empty()) {
		Platform::ApplicationHiddenValue(
		) | rpl::on_next([](bool hidden) {
			Runtimes().applicationHidden = hidden;
			PostCheck();
		}, registry.applicationLifetime);
		EmojiRefreshes() | rpl::on_next([] {
			for (const auto &runtime : Runtimes().list) {
				runtime->emojiRefreshed();
			}
		}, registry.applicationLifetime);
	}
	registry.list.emplace_back(this);
}

Runtime::~Runtime() {
	_windowLifetime.destroy();
	auto &registry = Runtimes();
	auto &list = registry.list;
	list.erase(ranges::remove(list, not_null<Runtime*>(this)), end(list));
	if (list.empty()) {
		registry.applicationLifetime.destroy();
	}
	invalidate_weak_ptrs(this);
	_liveLifetime.destroy();
	breakCrashRun();
	if (_live) {
		_live->detach();
	}
	if (heavy()) {
		PostCheck();
	}
	if (const auto host = base::take(_host)) {
		StorageHostRetired(host->storage);
		host->retired = true;
		Sample(host);
		if (host->watching) {
			WatchersChanged();
		}
	}
}

bool Runtime::IsQuitPrevent() {
	Drain();
	if (!Busy()) {
		return false;
	}
	LOG(("Tgw prevents quit, stopping widget sandboxes..."));
	return true;
}

bool Runtime::IsExitPrevent(Fn<void()> retry) {
	Drain();
	if (!Busy()) {
		return false;
	}
	// A JS task may still call into app statics until its engine is joined.
	Runtimes().exitRetry = std::move(retry);
	return true;
}

void Runtime::StateChanged(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	if (items.empty()) {
		return;
	}
	for (const auto &runtime : Runtimes().list) {
		if (runtime->_host && items.contains(runtime->_item)) {
			runtime->syncHostData();
		}
	}
}

auto Runtime::WatchingOnScreen()
-> std::vector<not_null<HistoryItem*>> {
	auto result = std::vector<not_null<HistoryItem*>>();
	const auto &registry = Runtimes();
	if (registry.draining || Core::Quitting()) {
		return result;
	}
	for (const auto &runtime : registry.list) {
		if (runtime->_state == State::Live
			&& runtime->_host
			&& runtime->_host->watching
			&& runtime->_live->attached()
			&& runtime->_visible
			&& runtime->queryVisible()) {
			result.push_back(runtime->_item);
		}
	}
	return result;
}

not_null<HistoryView::Element*> Runtime::view() const {
	return _view;
}

not_null<HistoryItem*> Runtime::item() const {
	return _item;
}

bool Runtime::matches(
		const QByteArray &html,
		const MessageContext &context) const {
	return currentContent() && (_html == html) && (_context == context);
}

Runtime::State Runtime::state() const {
	return _state;
}

bool Runtime::heavy() const {
	return (_state == State::Waiting)
		|| (_state == State::Constructing)
		|| (_state == State::Live);
}

bool Runtime::stopped() const {
	return (_state == State::Stopped);
}

std::optional<int> Runtime::height() const {
	const auto shown = core();
	const auto reported = shown ? shown->height() : std::optional<double>();
	const auto css = reported ? reported : _memory->height;
	return css ? std::make_optional(LogicalHeight(*css)) : std::nullopt;
}

RendererCore *Runtime::core() const {
	return (_live && _live->loaded()) ? _live.get() : _still.get();
}

RendererCore *Runtime::interactive() const {
	const auto live = _live.get();
	return ((_state == State::Live)
		&& live
		&& (live == core())
		&& live->acceptsInput())
		? live
		: nullptr;
}

std::optional<HeightLayout> Runtime::heightLayout() const {
	return _layout.serial ? std::make_optional(_layout) : std::nullopt;
}

void Runtime::attachView(
		not_null<HistoryView::Element*> view,
		Fn<void()> resize,
		Fn<void()> repaint,
		Fn<void()> heavyChanged) {
	Expects(view->data() == _item);

	_view = view;
	_resize = std::move(resize);
	_repaint = std::move(repaint);
	_heavyChanged = std::move(heavyChanged);
	if (_live) {
		_live->resetInput();
	}
	if (stopped()) {
		release();
	}
	PostCheck();
	syncHostData();
	if (_host && _host->watching) {
		WatchersChanged();
	}
	StoreMemory(_item, *_memory);
}

void Runtime::setSize(int width, double maxHeight) {
	if (_width == width && _maxHeight == maxHeight) {
		return;
	}
	_width = width;
	_maxHeight = maxHeight;
	if (_live || _still) {
		applySize();
	}
}

void Runtime::setSafeArea(ContentSafeArea area) {
	if (_safeArea == area) {
		return;
	}
	_safeArea = area;
	syncHostSafeArea();
}

void Runtime::laidOut(int height) {
	const auto pending = base::take(_clientPending);
	if (_layout.serial && (_layout.height == height)) {
		return;
	}
	const auto own = _layout.serial
		&& !pending
		&& _visible
		&& _live
		&& _live->loaded()
		&& _live->height().has_value();
	_layout = HeightLayout{
		.serial = _layout.serial + 1,
		.height = height,
		.change = own ? HeightChange::Own : HeightChange::Client,
		.anchor = heightAnchor(),
	};
}

void Runtime::setTheme(std::shared_ptr<const twidget::WidgetTheme> theme) {
	if (SameTheme(*_theme, *theme)) {
		return;
	}
	_theme = std::move(theme);
	syncHostTheme();
}

void Runtime::setWindow(not_null<QWidget*> window) {
	if (_window == window.get()) {
		return;
	}
	_windowLifetime.destroy();
	_window = window.get();
	_windowKnown = true;
	base::install_event_filter(window, [](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Show
			|| type == QEvent::Hide
			|| type == QEvent::WindowStateChange) {
			PostCheck();
		}
		return base::EventFilterResult::Continue;
	}, _windowLifetime);
	base::qt_signal_producer(
		window.get(),
		&QObject::destroyed
	) | rpl::on_next([=] {
		_window = nullptr;
		PostCheck();
	}, _windowLifetime);
	PostCheck();
}

void Runtime::painted() {
	_keptSerial = 0;
	if (heavy() || (stopped() && _displayDeferred)) {
		if (!_visible) {
			PostCheck();
		}
		return;
	} else if (!stopped() && (_state != State::Failed) && crashedOut()) {
		stopForCrashes();
		return;
	} else if (!startable()) {
		return;
	}
	_painted = true;
	PostCheck();
}

void Runtime::unload() {
	_windowLifetime.destroy();
	_window = nullptr;
	_windowKnown = false;
	_painted = false;
	_queueSerial = 0;
	const auto wasHeavy = heavy();
	release();
	if (wasHeavy) {
		addNote(u"Released with the unloaded messages."_q);
	}
	if (_still && _still->frameBytes()) {
		_keptSerial = ++Runtimes().keptSerial;
		TrimKeptPictures();
	}
}

void Runtime::TrimKeptPictures() {
	auto kept = std::vector<not_null<Runtime*>>();
	auto bytes = int64();
	for (const auto &runtime : Runtimes().list) {
		if (runtime->_keptSerial && runtime->_still) {
			kept.push_back(runtime);
			bytes += runtime->_still->frameBytes();
		}
	}
	if (bytes <= kKeptPictureBytes) {
		return;
	}
	ranges::sort(kept, ranges::less(), [](not_null<Runtime*> runtime) {
		return runtime->_keptSerial;
	});
	for (const auto &runtime : kept) {
		bytes -= runtime->_still->frameBytes();
		runtime->_still->dropFrame();
		runtime->_keptSerial = 0;
		if (bytes <= kKeptPictureBytes) {
			break;
		}
	}
}

QString Runtime::logText() const {
	auto lines = QStringList();
	lines.push_back(u"State: %1%2, generation %3, %4."_q.arg(
		StateName(_state),
		_queueSerial ? u" (waiting for a free sandbox)"_q : QString(),
		QString::number(_generation),
		_visible ? u"on screen"_q : u"off screen"_q));
	if (_state == State::Stopped) {
		lines.push_back(u"Stopped%1: %2"_q.arg(
			_stopFatal ? u" (fatal)"_q : QString(),
			_stopReason));
	}
	lines.push_back(currentContent()
		? u"Crashes in a row: %1 of %2."_q.arg(
			QString::number(_memory->crashes),
			QString::number(kMaxCrashes))
		: u"Crashes not counted, the message has newer HTML."_q);
	const auto updating = _logDone ? u", updating"_q : QString();
	if (_summaryHost) {
		const auto current = _host && (_host->id == _summaryHost);
		lines.push_back(u"%1 sandbox%2:"_q.arg(
			current ? u"Current"_q : u"Last"_q,
			updating));
		lines.push_back(_summary);
	} else if (_lastHost) {
		lines.push_back(u"No sandbox diagnostics yet%1."_q.arg(updating));
	} else {
		lines.push_back(u"No sandbox has run in this view."_q);
	}
	lines.push_back(u"Notes:"_q);
	for (const auto &note : _notes) {
		lines.push_back(note);
	}
	return Clip(lines.join(QChar('\n')), kLogBytes / int(sizeof(QChar)));
}

void Runtime::requestLog(Fn<void(const QString &text)> done) {
	if (Runtimes().draining || Core::Quitting()) {
		return;
	} else if (_host) {
		_logDone = std::move(done);
		_logHost = _host->id;
		Sample(_host);
	} else if (_lastHost
		&& (_summaryHost != _lastHost || !_summaryLast)) {
		_logDone = std::move(done);
		_logHost = _lastHost;
	} else {
		_logDone = nullptr;
	}
}

std::shared_ptr<MessageMemory> Runtime::RememberMessage(
		not_null<HistoryItem*> item,
		const QByteArray &html) {
	const auto session = &item->history()->session();
	auto &sessions = Runtimes().sessions;
	if (!sessions.contains(session)) {
		session->data().itemVisibilitiesUpdates(
		) | rpl::on_next([] {
			PostCheck();
		}, session->lifetime());
		session->data().itemViewRefreshRequest(
		) | rpl::on_next([=](not_null<const HistoryItem*> refreshed) {
			RememberItemHtml(session, refreshed);
		}, session->lifetime());
		session->data().itemIdChanged(
		) | rpl::on_next([=](const Data::Session::IdChange &change) {
			const auto item = session->data().message(change.newId);
			const auto watching = [&](not_null<Runtime*> runtime) {
				return (runtime->_item.get() == item)
					&& runtime->_host
					&& runtime->_host->watching;
			};
			if (ranges::any_of(Runtimes().list, watching)) {
				WatchersChanged();
			}
			if (item) {
				StoreItemMemory(session, item);
			}
		}, session->lifetime());
		session->data().itemRemoved(
		) | rpl::on_next([=](not_null<const HistoryItem*> removed) {
			auto &registry = Runtimes();
			const auto i = registry.sessions.find(session);
			if (i != end(registry.sessions)) {
				i->second.remove(removed);
			}
		}, session->lifetime());
		Wallet::AvailableValue(
			session
		) | rpl::skip(1) | rpl::on_next([=](bool) {
			WalletAvailabilityChanged(session);
		}, session->lifetime());
		session->lifetime().add([=] {
			Runtimes().sessions.remove(session);
		});
	}
	auto &memory = sessions[session][item];
	if (!memory) {
		memory = std::make_shared<MessageMemory>();
		const auto stored = AutoHeight(item)
			? StoredHeight(item)
			: std::nullopt;
		if (stored
			&& stored->height >= kMinHeight
			&& stored->height <= kMaxHeight) {
			memory->height = stored->height;
			memory->anchor = stored->bottom
				? ResizeAnchor::Bottom
				: ResizeAnchor::Top;
		}
	}
	RememberHtml(*memory, html);
	return memory;
}

void Runtime::WalletAvailabilityChanged(not_null<Main::Session*> session) {
	auto items = base::flat_set<not_null<HistoryItem*>>();
	for (const auto &runtime : Runtimes().list) {
		const auto item = runtime->_item;
		if (runtime->_context.flags.canQuery
			&& (&item->history()->session() == session)) {
			items.emplace(item);
		}
	}
	for (const auto &item : items) {
		item->history()->owner().requestItemViewRefresh(item);
	}
	if (!items.empty()) {
		// WHY: availability changes arrive from MTP answers, not from
		// applyUpdates, so nothing else flushes the pending refresh and the
		// chat would neither lay out nor start the new widget host.
		session->data().sendHistoryChangeNotifications();
	}
}

void Runtime::PostCheck() {
	auto &registry = Runtimes();
	if (registry.checkPosted || registry.draining) {
		return;
	}
	registry.checkPosted = true;
	crl::on_main([] {
		Runtimes().checkPosted = false;
		Check();
	});
}

void Runtime::Check() {
	if (Runtimes().draining) {
		return;
	}
	RefreshVisibility();
	ScheduleStarts();
}

void Runtime::Drain() {
	auto &registry = Runtimes();
	if (registry.draining) {
		return;
	}
	registry.draining = true;
	if (StopResources([] { JobDone(); })) {
		++registry.jobs;
	}
	auto runtimes = std::vector<base::weak_ptr<Runtime>>();
	runtimes.reserve(registry.list.size());
	for (const auto &runtime : registry.list) {
		runtimes.emplace_back(runtime.get());
	}
	for (const auto &weak : runtimes) {
		if (const auto strong = weak.get()) {
			strong->_logDone = nullptr;
			strong->release();
		}
	}
}

void Runtime::Sample(const std::shared_ptr<Host> &host) {
	auto &registry = Runtimes();
	if (host->sampling) {
		return;
	} else if (host->retired && (registry.draining || !host->owner)) {
		host->pointer = nullptr;
		return;
	}
	const auto last = host->retired;
	const auto request = ++registry.requestSerial;
	auto pointer = last ? base::take(host->pointer) : host->pointer;
	host->sampling = true;
	++registry.jobs;
	crl::async([
		host = host,
		pointer = std::move(pointer),
		request,
		last
	]() mutable {
		auto text = Summarize(*pointer);
		pointer = nullptr;
		crl::on_main([
			host = std::move(host),
			text = std::move(text),
			request,
			last
		]() mutable {
			SampleDone(host, request, last, std::move(text));
		});
	});
}

void Runtime::SampleDone(
		const std::shared_ptr<Host> &host,
		uint64 request,
		bool last,
		QString text) {
	host->sampling = false;
	if (const auto owner = host->owner.get()) {
		owner->applySample(host->id, request, last, std::move(text));
	}
	if (host->retired && host->pointer) {
		Sample(host);
	}
	JobDone();
}

void Runtime::RefreshVisibility() {
	auto &registry = Runtimes();
	auto changed = std::vector<not_null<Runtime*>>();
	for (const auto &runtime : registry.list) {
		if ((runtime->heavy()
				|| runtime->stopped()
				|| runtime->_painted
				|| runtime->_displayDeferred)
			&& (runtime->queryVisible() != runtime->_visible)) {
			changed.push_back(runtime);
		}
	}
	auto watched = false;
	for (const auto &runtime : changed) {
		if (runtime->_host && runtime->_host->watching) {
			watched = true;
		}
		runtime->_visible = !runtime->_visible;
		if (!runtime->_visible) {
			runtime->_hiddenSerial = ++registry.hiddenSerial;
			if (runtime->stopped()) {
				runtime->release();
			}
		} else {
			runtime->markHeight(true);
		}
		runtime->syncHostVisibility();
	}
	if (watched) {
		WatchersChanged();
	}
}

void Runtime::ScheduleStarts() {
	auto &registry = Runtimes();
	const auto serial = registry.queueSerial;
	auto queue = std::vector<not_null<Runtime*>>();
	auto waiting = 0;
	auto owned = 0;
	auto visibleOwned = 0;
	for (const auto &runtime : registry.list) {
		if (runtime->heavy()) {
			++owned;
			if (runtime->_state == State::Waiting) {
				++waiting;
			}
			if (runtime->_visible) {
				++visibleOwned;
			}
		} else if (!runtime->wantsStart()) {
			runtime->_queueSerial = 0;
		} else {
			if (!runtime->_queueSerial) {
				runtime->_queueSerial = ++registry.queueSerial;
			}
			queue.push_back(runtime);
		}
	}
	ranges::sort(queue, ranges::less(), [](not_null<Runtime*> runtime) {
		return runtime->_queueSerial;
	});
	const auto capacity = std::max(
		kMaxAlive,
		visibleOwned + int(queue.size()));
	auto occupied = registry.reserved + waiting;
	auto left = int(queue.size());
	for (const auto &runtime : queue) {
		if (occupied >= capacity) {
			break;
		}
		runtime->_queueSerial = 0;
		runtime->start();
		++occupied;
		++owned;
		--left;
	}
	for (const auto &runtime : queue) {
		if (runtime->_queueSerial > serial) {
			runtime->addNote(u"Waiting for a free sandbox."_q);
		}
	}
	while (owned + left > capacity) {
		const auto victim = FindVictim();
		if (!victim) {
			break;
		}
		victim->release();
		victim->addNote(u"Released off screen to stay in the limit."_q);
		--owned;
	}
}

Runtime *Runtime::FindVictim() {
	auto result = (Runtime*)nullptr;
	for (const auto &runtime : Runtimes().list) {
		if (runtime->heavy()
			&& !runtime->_visible
			&& (!result || runtime->_hiddenSerial < result->_hiddenSerial)) {
			result = runtime.get();
		}
	}
	return result;
}

bool Runtime::currentContent() const {
	return (_memory->contentSerial == _contentSerial);
}

bool Runtime::crashedOut() const {
	return currentContent() && (_memory->crashes >= kMaxCrashes);
}

bool Runtime::startable() const {
	return (_state != State::Stopped)
		&& (_state != State::Failed)
		&& currentContent()
		&& !crashedOut()
		&& !Runtimes().draining
		&& !Core::Quitting();
}

bool Runtime::wantsStart() const {
	return _painted
		&& _visible
		&& !heavy()
		&& startable()
		&& !StorageRetiring(_item);
}

bool Runtime::queryVisible() const {
	if (Runtimes().applicationHidden
		|| (_windowKnown
			&& (!_window || _window->isHidden() || _window->isMinimized()))) {
		return false;
	}
	return _item->history()->owner().queryItemVisibility(
		_item,
		_view.get());
}

bool Runtime::recordStop(not_null<RendererCore*> renderer) {
	_stopReason = Clip(renderer->stopReason());
	_stopFatal = renderer->stopFatal();
	if (_stopFatal
		&& currentContent()
		&& (_memory->crashes < kMaxCrashes)) {
		return (++_memory->crashes == kMaxCrashes);
	}
	return false;
}

void Runtime::breakCrashRun() {
	if (_state == State::Live && _live->stopped()) {
		// A release may overtake the stop that RendererCore::frame() posted.
		if (recordStop(_live.get())) {
			_item->history()->owner().requestItemRepaint(_item);
		}
		return;
	}
	if (_state == State::Live
		&& _live->hasContent()
		&& !_live->stopped()
		&& currentContent()
		&& (_memory->crashes < kMaxCrashes)) {
		_memory->crashes = 0;
	}
}

void Runtime::start() {
	if (heavy() || !startable()) {
		return;
	}
	const auto generation = ++_generation;
	_state = State::Waiting;
	_stopReason = QString();
	_stopFatal = false;
	addNote(u"Starting."_q);
	heavyStateChanged(false);
	PrepareResources(crl::guard(this, [=](
			std::shared_ptr<twidget::SandboxResources> resources) {
		resourcesReady(generation, std::move(resources));
	}));
}

void Runtime::release() {
	if (heavy()) {
		breakCrashRun();
		retire(State::Released);
	} else if (stopped() && currentContent() && !crashedOut()) {
		retire(State::Released);
	}
}

void Runtime::resourcesReady(
		int generation,
		std::shared_ptr<twidget::SandboxResources> resources) {
	if (generation != _generation || _state != State::Waiting) {
		return;
	} else if (!resources) {
		failResources();
		return;
	} else if (Core::Quitting() || !currentContent()) {
		release();
		return;
	} else if (crashedOut()) {
		stopForCrashes();
		displayChanged(true);
		return;
	} else if (StorageRetiring(_item)) {
		release();
		addNote(u"Released, an earlier sandbox is still handing over "
			"its storage."_q);
		_repaint();
		return;
	}
	construct(std::move(resources));
}

void Runtime::construct(
		std::shared_ptr<twidget::SandboxResources> resources) {
	const auto width = CssFromLogical(_width);
	const auto ratio = CoreRatio();
	_live = std::make_unique<RendererCore>(width, ratio);
	_live->setMaxHeight(_maxHeight);
	subscribeLive();
	_state = State::Constructing;
	_hostVisible = _visible;
	_hostTheme = _theme;
	_hostSafeArea = _safeArea;
	auto options = DefaultHostOptions(ratio, _visible);
	options.storage = StoredAreas(_item);
	options.data = HostData(_item);
	options.context = HostContext(
		_context,
		_item->history()->session().user());
	options.contentSafeArea = HostSafeArea(_hostSafeArea);
	auto storage = OpenStorageHost(_item);
	const auto id = storage.id;
	StorageHostVisibility(id, _hostVisible);
	auto job = Job{
		.html = _html,
		.resources = std::move(resources),
		.intl = CreateHostIntl(),
		// leak and conformance checks need the validated ops only renderers get
		.factory = Test::TapWidgetRenderer(
			_item->globalId(),
			_live->hostFactory()),
		.action = _live->hostActionHandler(),
		.request = _live->hostRequestHandler(),
		.wallet = _live->hostWalletHandler(),
		.copy = _live->hostCopyHandler(),
		.watch = _live->hostWatchHandler(),
		.storage = std::move(storage.commit),
		.options = Test::OverrideWidgetHostOptions(
			_item->globalId(),
			std::move(options)),
		.theme = *_theme,
		.stoppedLabel = tr::lng_tgw_stopped(tr::now).toStdString(),
		// WHY: the slot is freed after ~WidgetHost delivered its last commits,
		// which were posted to main before, so the host leaves the store only
		// after they were applied and before the next Check() starts anyone.
		.slot = std::make_shared<Reservation>([id] {
			StorageHostReleased(id);
			Check();
		}),
		.width = width,
	};
	const auto weak = base::make_weak(this);
	const auto generation = _generation;
	++Runtimes().jobs;
	crl::async([=, job = std::move(job)]() mutable {
		auto built = Build(std::move(job));
		crl::on_main([=, built = std::move(built)]() mutable {
			if (const auto strong = weak.get()) {
				strong->constructed(
					generation,
					base::take(built.host),
					built.data,
					built.error,
					id);
			}
			built.host = nullptr;
			JobDone();
		});
	});
}

void Runtime::constructed(
		int generation,
		std::shared_ptr<twidget::WidgetHost> host,
		const twidget::WidgetData &data,
		const QString &error,
		uint64 storage) {
	if (generation != _generation || _state != State::Constructing) {
		return;
	} else if (!host) {
		fail(error);
		return;
	} else if (Core::Quitting() || !currentContent()) {
		release();
		return;
	} else if (crashedOut()) {
		stopForCrashes();
		displayChanged(true);
		return;
	} else if (!SameTheme(*_hostTheme, *_theme)) {
		// Its engine would boot in the stale theme ahead of setTheme().
		release();
		addNote(u"Released, the theme changed before it started."_q);
		_repaint();
		return;
	} else if (StorageHostStale(storage)) {
		release();
		addNote(u"Released, its storage changed before it started."_q);
		_repaint();
		return;
	} else if (data != HostData(_item)) {
		// Its scripts would read the stale data ahead of setData().
		release();
		addNote(u"Released, its data changed before it started."_q);
		_repaint();
		return;
	}
	_host = std::make_shared<Host>(Host{
		.pointer = std::move(host),
		.owner = base::make_weak(this),
		.id = ++Runtimes().hostSerial,
		.storage = storage,
	});
	_lastHost = _host->id;
	_logDone = nullptr;
	_state = State::Live;
	addNote(u"Started."_q);
	const auto width = CssFromLogical(_width);
	if (_live->width() != width) {
		_live->setWidth(width);
	}
	_live->setMaxHeight(_maxHeight);
	syncHostVisibility();
	syncHostSafeArea();
	Test::WatchWidgetHost(_item->globalId(), *_host->pointer, _live.get());
	_live->bindHost(_host->pointer.get());
	_host->pointer->start();
}

void Runtime::retire(State state) {
	const auto wasHeavy = heavy();
	++_generation;
	_state = state;
	_painted = false;
	_queueSerial = 0;
	_liveLifetime.destroy();
	if (_live) {
		_live->detach();
		if (_live->loaded()) {
			_still = std::move(_live);
			_displayDeferred = _displayDeferred
				|| (height() != _shownHeight);
		} else {
			addNote(u"Its start did not finish."_q);
			_live = nullptr;
		}
	}
	if (const auto host = base::take(_host)) {
		StorageHostRetired(host->storage);
		host->retired = true;
		Sample(host);
		if (host->watching) {
			WatchersChanged();
		}
	}
	heavyStateChanged(wasHeavy);
	if (wasHeavy) {
		PostCheck();
	}
}

void Runtime::stopForCrashes() {
	_stopReason = u"%1 crashes in a row."_q.arg(kMaxCrashes);
	_stopFatal = false;
	retire(State::Stopped);
	addNote(u"Stopped: "_q + _stopReason);
}

void Runtime::fail(const QString &error) {
	retire(State::Failed);
	addNote(error);
	LOG(("Tgw Error: %1").arg(error));
	displayChanged(true);
}

void Runtime::failResources() {
	fail(u"The sandbox resources are unavailable."_q);
}

void Runtime::subscribeLive() {
	using Change = RendererCore::Change;
	const auto renderer = _live.get();
	renderer->updates(
	) | rpl::on_next([=](RendererCore::Changes changes) {
		if (changes & Change::Stopped) {
			coreStopped(renderer);
		} else {
			coreUpdated(
				renderer,
				bool(changes
					& (Change::Repaint | Change::Content | Change::Loaded)),
				bool(changes & Change::Loaded));
		}
	}, _liveLifetime);

	renderer->linkRequests(
	) | rpl::on_next([=](const RendererCore::LinkRequest &request) {
		linkRequested(renderer, request.url, request.telegram);
	}, _liveLifetime);

	renderer->botRequests(
	) | rpl::on_next([=](const RendererCore::BotRequest &request) {
		botRequested(
			renderer,
			request.data,
			request.wallet,
			request.call.get(),
			request.respond);
	}, _liveLifetime);

	renderer->copyRequests(
	) | rpl::on_next([=](const std::string &text) {
		copyRequested(renderer, text);
	}, _liveLifetime);

	renderer->watchChanges(
	) | rpl::on_next([=](bool on) {
		watchChanged(renderer, on);
	}, _liveLifetime);
}

void Runtime::coreUpdated(
		not_null<RendererCore*> renderer,
		bool repaint,
		bool finished) {
	if (renderer.get() != _live.get() || !renderer->loaded()) {
		return;
	}
	if (_still) {
		renderer->takeFrameFrom(*_still);
	}
	_still = nullptr;
	_keptSerial = 0;
	if (finished) {
		markHeight(true);
	}
	displayChanged(repaint);
}

void Runtime::coreStopped(not_null<RendererCore*> renderer) {
	if (renderer.get() != _live.get() || _state != State::Live) {
		return;
	}
	const auto limitReached = recordStop(renderer);
	// A scroll away may not have reached the posted Check() yet.
	_visible = queryVisible();
	retire(State::Stopped);
	addNote(u"Stopped%1: %2"_q.arg(
		_stopFatal ? u" (fatal)"_q : QString(),
		_stopReason));
	displayChanged(true);
	if (!_visible) {
		release();
	}
	if (limitReached) {
		// Other views of the message apply the limit only when painted.
		_item->history()->owner().requestItemRepaint(_item);
	}
}

void Runtime::linkRequested(
		not_null<RendererCore*> renderer,
		const std::string &url,
		bool telegram) {
	if (_state != State::Live
		|| renderer.get() != _live.get()
		|| !renderer->attached()) {
		return;
	}
	if (!queryVisible()) {
		addNote(u"Link refused, the widget is not visible."_q);
		return;
	}
	const auto weak = base::make_weak(this);
	const auto result = _bridge->openLink(
		QString::fromStdString(url),
		telegram);
	if (weak) {
		addNote(result);
	}
}

void Runtime::botRequested(
		not_null<RendererCore*> renderer,
		const std::string &data,
		bool wallet,
		const twidget::WalletCall *call,
		Fn<void(const twidget::WidgetRequestOutcome &outcome)> respond) {
	if (_state != State::Live
		|| renderer.get() != _live.get()
		|| !renderer->attached()) {
		respond({ .failure = twidget::WidgetRequestFailure::Failed });
		return;
	} else if (call) {
		walletCalled(*call, std::move(respond));
		return;
	}
	// conformance runs play their bot here: the bridge refuses local messages
	if (Test::PlayWidgetBot(_item->globalId(), data, respond, wallet)) {
		return;
	}
	_bridge->request(data, wallet, std::move(respond));
}

void Runtime::walletCalled(
		const twidget::WalletCall &call,
		Fn<void(const twidget::WidgetRequestOutcome &outcome)> respond) {
	const auto window = _window.data();
	const auto refusal = !queryVisible()
		? u"Wallet call refused, the widget is not visible."_q
		: !window
		? u"Wallet call refused, the widget has no window."_q
		: QString();
	if (!refusal.isEmpty()) {
		addNote(refusal);
		respond({ .failure = twidget::WidgetRequestFailure::Failed });
		return;
	}
	RunWalletCall(_item, window, call, std::move(respond));
}

void Runtime::copyRequested(
		not_null<RendererCore*> renderer,
		const std::string &text) {
	if (_state != State::Live
		|| renderer.get() != _live.get()
		|| !renderer->attached()) {
		return;
	}
	if (!queryVisible()) {
		addNote(u"Copy refused, the widget is not visible."_q);
		return;
	}
	const auto window = _window.data();
	if (!window) {
		addNote(u"Copy refused, the widget has no window."_q);
		return;
	}
	const auto weak = base::make_weak(this);
	const auto result = _bridge->copyText(DecodeCopiedText(text), window);
	if (weak) {
		addNote(result);
	}
}

void Runtime::watchChanged(not_null<RendererCore*> renderer, bool on) {
	if (_state != State::Live
		|| renderer.get() != _live.get()
		|| !renderer->attached()
		|| !_host
		|| (_host->watching == on)) {
		return;
	}
	_host->watching = on;
	WatchersChanged();
}

void Runtime::syncHostVisibility() {
	if (_live) {
		_live->setOnScreen(_visible);
	}
	if (_visible) {
		postShownDisplay();
	}
	if (_host && _hostVisible != _visible) {
		_hostVisible = _visible;
		_host->pointer->setVisible(_visible);
		StorageHostVisibility(_host->storage, _visible);
	}
}

void Runtime::syncHostTheme() {
	if (!_host || _host->theming || SameTheme(*_hostTheme, *_theme)) {
		return;
	}
	_hostTheme = _theme;
	_host->theming = true;
	addNote(u"Switched to a new %1 theme."_q.arg(
		QString::fromStdString(_hostTheme->colorScheme)));
	++Runtimes().jobs;
	crl::async([
		host = _host,
		pointer = _host->pointer,
		theme = _hostTheme
	]() mutable {
		ApplyTheme(*pointer, *theme);
		pointer = nullptr;
		crl::on_main([host = std::move(host)] {
			ThemeDone(host);
		});
	});
}

void Runtime::ThemeDone(const std::shared_ptr<Host> &host) {
	host->theming = false;
	const auto owner = host->owner.get();
	if (owner && owner->_host == host) {
		owner->syncHostTheme();
	}
	JobDone();
}

void Runtime::syncHostData() {
	if (_host) {
		_host->pointer->setData(HostData(_item));
	}
}

void Runtime::syncHostSafeArea() {
	if (_host && _hostSafeArea != _safeArea) {
		_hostSafeArea = _safeArea;
		_host->pointer->setContentSafeArea(HostSafeArea(_safeArea));
	}
}

void Runtime::applySize() {
	// WHY: the list layout that calls HtmlWidget::countCurrentSize() lays
	// the new height out right after setSize(), so a resize or a repaint
	// requested from here would re-enter that same layout.
	_sizing = true;
	const auto guard = gsl::finally([&] { _sizing = false; });
	const auto was = effectiveHeight();
	const auto width = CssFromLogical(_width);
	if (_still) {
		_still->setWidth(width);
		_still->setMaxHeight(_maxHeight);
	}
	if (_live) {
		_live->setWidth(width);
		_live->setMaxHeight(_maxHeight);
	}
	displayChanged(false);
	markHeight(effectiveHeight() != was);
}

int Runtime::effectiveHeight() const {
	return height().value_or(st::tgwPlaceholderHeight);
}

ResizeAnchor Runtime::heightAnchor() const {
	const auto shown = core();
	return (shown && shown->height())
		? AnchorOf(*shown)
		: _memory->anchor;
}

void Runtime::markHeight(bool client) {
	// WHY: a list may lay a new height out before its notification, or with
	// none at all, so a client cause stays pending until a layout consumes
	// the difference it made or the height is back at the consumed one.
	if (_layout.serial && (_layout.height == effectiveHeight())) {
		_clientPending = false;
	} else if (client) {
		_clientPending = true;
	}
}

void Runtime::displayChanged(bool repaint) {
	if (const auto shown = core()) {
		if (const auto reported = shown->height()) {
			const auto anchor = AnchorOf(*shown);
			if (_memory->height != reported || _memory->anchor != anchor) {
				_memory->height = reported;
				_memory->anchor = anchor;
				StoreMemory(_item, *_memory);
			}
		}
	}
	const auto now = height();
	const auto hidden = !_visible
		&& (_state == State::Live || _state == State::Stopped);
	markHeight(hidden);
	if (_sizing) {
		_shownHeight = now;
		return;
	}
	const auto resize = (_shownHeight != now);
	if (!resize && !repaint) {
		return;
	} else if (hidden) {
		_displayDeferred = true;
		return;
	}
	_displayDeferred = false;
	if (resize) {
		_shownHeight = now;
		_resize();
	} else {
		_repaint();
	}
}

void Runtime::postShownDisplay() {
	const auto shown = core();
	if (shown && shown->renderer()->dirty()) {
		_displayDeferred = true;
	}
	if (!_displayDeferred || _displayPosted) {
		return;
	}
	_displayPosted = true;
	crl::on_main(this, [=] {
		_displayPosted = false;
		if (base::take(_displayDeferred)) {
			displayChanged(true);
		}
	});
}

void Runtime::emojiRefreshed() {
	if (!_live && !_still) {
		return;
	}
	if (_live) {
		_live->refreshFrame();
	}
	if (_still) {
		_still->refreshFrame();
	}
	_repaint();
}

void Runtime::heavyStateChanged(bool wasHeavy) {
	if (heavy() != wasHeavy) {
		_heavyChanged();
	}
}

void Runtime::addNote(const QString &text) {
	_notes.push_back(
		QTime::currentTime().toString(u"HH:mm:ss "_q) + Clip(text));
	if (int(_notes.size()) > kMaxNotes) {
		_notes.erase(begin(_notes));
	}
}

void Runtime::applySample(
		uint64 host,
		uint64 request,
		bool last,
		QString text) {
	if (host < _summaryHost
		|| (host == _summaryHost && request < _summaryRequest)) {
		return;
	}
	_summary = std::move(text);
	_summaryHost = host;
	_summaryRequest = request;
	_summaryLast = last;
	if (_logDone
		&& (_logHost == host)
		&& (last || _host)
		&& !Runtimes().draining) {
		const auto done = base::take(_logDone);
		done(logText());
	}
}

double CssFromLogical(int logical) {
	return logical * 100. / style::Scale();
}

int LogicalHeight(double css) {
	return int(std::ceil(css * style::Scale() / 100.));
}

int LogicalWidth(double css) {
	return int(std::round(css * style::Scale() / 100.));
}

Runtime *FindRuntime(not_null<const HistoryView::Element*> view) {
	for (const auto &runtime : Runtimes().list) {
		if (runtime->view().get() == view.get()) {
			return runtime.get();
		}
	}
	return nullptr;
}

} // namespace Tgw
