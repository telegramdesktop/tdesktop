/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer.h"

#include "base/platform/base_platform_info.h"
#include "test/test_widget_ops.h"
#include "tgw/tgw_hosted_wire.h"
#include "tgw/tgw_render_thread.h"
#include "tgw/tgw_renderer_input.h"
#include "tgw/tgw_renderer_platform.h"
#include "tgw/tgw_renderer_work.h"
#include "tgw/tgw_touch.h"
#include "twidget/json.h"
#include "twidget/sandbox_engine.h"
#include "twidget/sdk_policy.h"
#include "twidget/widget_host.h"
#include "ui/style/style_core_scale.h"

#include <mutex>

#include "styles/style_basic.h"

namespace Tgw {
namespace {

class KeptSlot final {
public:
	KeptSlot(const KeptSlot &other) = delete;
	KeptSlot &operator=(const KeptSlot &other) = delete;
	~KeptSlot();

	[[nodiscard]] static std::unique_ptr<KeptSlot> Reserve();

private:
	KeptSlot() = default;

};

} // namespace

class HostLink final : public std::enable_shared_from_this<HostLink> {
public:
	HostLink(
		not_null<RendererCore*> core,
		uint64 work,
		std::optional<double> hosted);
	HostLink(const HostLink &other) = delete;
	HostLink &operator=(const HostLink &other) = delete;
	~HostLink();

	[[nodiscard]] bool attach(not_null<twidget::Renderer*> proxy);
	void detach(not_null<twidget::Renderer*> proxy);
	void close();
	void forget();
	[[nodiscard]] bool gone() const;
	[[nodiscard]] uint64 workId() const;
	[[nodiscard]] bool bindHost(std::weak_ptr<twidget::WidgetHost> host);
	void toHost(const std::string &json);
	void diag(const std::string &kind, const std::string &detail);
	[[nodiscard]] std::optional<bool> hostBottom() const;
	void work(Fn<void(RendererWork&)> task, bool host);

	void toPlatform(std::string message);
	[[nodiscard]] bool pressPlatform(std::string message);
	void released();
	[[nodiscard]] bool reserveKeep();
	void keep(twidget::WidgetHost &host);
	void keepDropped();
	void releaseRoute(Fn<void()> done);

	void postLoaded();
	void postMessage(twidget::Json message);
	void postTheme(bool dark);
	void postFrozen(QString reason, bool fatal);
	void postDeath(const std::string &reason, QString label);
	void postTraffic(HostedTraffic traffic);
	void postLinkRequest(RendererCore::LinkRequest request);
	void postBotRequest(RendererCore::BotRequest request);
	void postCopyText(std::string text);
	void postWatch(bool on);

	static void PostTraffic(
		const std::weak_ptr<HostLink> &weak,
		const std::string &message,
		Fn<void(std::string)> respond);

private:
	enum class Route : uchar {
		Local,
		Pending,
		Live,
		Keeping,
		Kept,
		Closed,
	};

	[[nodiscard]] RendererCore *current() const;
	bool platformSend(std::string &&message, bool press);
	[[nodiscard]] twidget::HostedKeptRenderer::Observers keptObservers();
	[[nodiscard]] bool bufferLocked(std::string &&message);
	void closeLocked();
	[[nodiscard]] std::vector<Fn<void()>> takeReleasedLocked();
	void completeRelease(
		std::unique_ptr<twidget::HostedKeptRenderer> kept,
		std::unique_ptr<KeptSlot> slot);
	void markGone();

	const base::weak_ptr<RendererCore> _core;
	const uint64 _work;
	const bool _hosted = false;
	mutable std::mutex _mutex;
	twidget::Renderer *_proxy = nullptr;
	std::weak_ptr<twidget::WidgetHost> _host;
	std::weak_ptr<twidget::WidgetHost> _live;
	std::unique_ptr<twidget::HostedKeptRenderer> _kept;
	std::unique_ptr<KeptSlot> _slot;
	std::vector<std::string> _buffered;
	std::vector<Fn<void()>> _released;
	std::size_t _bufferedBytes = 0;
	int _releases = 0;
	Route _route = Route::Local;
	bool _releaseWanted = false;
	std::atomic<bool> _closed = false;
	std::atomic<bool> _gone = false;
	std::atomic<bool> _routeClosed = false;
	std::atomic<bool> _documentGone = false;

};

namespace {

constexpr auto kFallbackWidth = 1.;
constexpr auto kDomAlt = 1;
constexpr auto kDomCtrl = 2;
constexpr auto kDomMeta = 4;
constexpr auto kDomShift = 8;
constexpr auto kMaxKeptRoutes = 8;
constexpr auto kMaxBufferedSends = std::size_t(256);
constexpr auto kMaxBufferedBytes = std::size_t(1) << 20;

auto WorkSerial = uint64(0);
std::atomic<int> KeptRoutes = 0;

class HostRenderer final : public twidget::Renderer {
public:
	explicit HostRenderer(std::shared_ptr<HostLink> link);
	~HostRenderer();

	void load() override;
	void send(const std::string &json) override;
	void setTheme(const std::string &theme) override;
	void setWidth(double width) override;
	void destroy() override;

private:
	const std::shared_ptr<HostLink> _link;

};

class KeepTicket final {
public:
	explicit KeepTicket(std::shared_ptr<HostLink> link);
	KeepTicket(const KeepTicket &other) = delete;
	KeepTicket &operator=(const KeepTicket &other) = delete;
	~KeepTicket();

	void run(twidget::WidgetHost &host);

private:
	const std::shared_ptr<HostLink> _link;
	std::atomic<bool> _used = false;

};

[[nodiscard]] bool ValidWidth(double width) {
	return std::isfinite(width) && (width > 0.);
}

[[nodiscard]] std::optional<twlite::remote::Kind> SettingKind(
		std::string_view message) {
	using Kind = twlite::remote::Kind;
	constexpr auto kSettings = std::array{
		Kind::SetWidth,
		Kind::SetMaxHeight,
		Kind::ClearSelection,
	};
	const auto kind = twlite::remote::kind_of(message);
	return (kind && ranges::contains(kSettings, *kind))
		? kind
		: std::nullopt;
}

void RespondFailed(const RendererCore::Respond &respond) {
	respond({ .failure = twidget::WidgetRequestFailure::Failed });
}

void PostReleased(std::vector<Fn<void()>> released) {
	if (released.empty()) {
		return;
	}
	crl::on_main([released = std::move(released)] {
		for (const auto &done : released) {
			done();
		}
	});
}

[[nodiscard]] RendererWork::Host HostOf(
		std::shared_ptr<HostLink> link,
		bool hosted) {
	auto result = RendererWork::Host{
		.send = [=](const std::string &json) {
			link->toHost(json);
		},
		.diag = [=](const std::string &kind, const std::string &detail) {
			link->diag(kind, detail);
		},
		.bottom = [=] {
			return link->hostBottom();
		},
		.gone = [=] {
			return link->gone();
		},
	};
	if (hosted) {
		result.platform = [=](std::string message) {
			link->toPlatform(std::move(message));
		};
		result.press = [=](std::string message) {
			return link->pressPlatform(std::move(message));
		};
		result.released = [=] {
			link->released();
		};
	}
	return result;
}

KeptSlot::~KeptSlot() {
	--KeptRoutes;
}

std::unique_ptr<KeptSlot> KeptSlot::Reserve() {
	auto held = KeptRoutes.load();
	do {
		if (held >= kMaxKeptRoutes) {
			return nullptr;
		}
	} while (!KeptRoutes.compare_exchange_weak(held, held + 1));
	return std::unique_ptr<KeptSlot>(new KeptSlot());
}

KeepTicket::KeepTicket(std::shared_ptr<HostLink> link)
: _link(std::move(link)) {
}

KeepTicket::~KeepTicket() {
	if (!_used) {
		_link->keepDropped();
	}
}

void KeepTicket::run(twidget::WidgetHost &host) {
	if (!_used.exchange(true)) {
		_link->keep(host);
	}
}

HostRenderer::HostRenderer(std::shared_ptr<HostLink> link)
: _link(std::move(link)) {
}

HostRenderer::~HostRenderer() {
	_link->detach(this);
}

void HostRenderer::load() {
	if (_link->attach(this)) {
		_link->postLoaded();
	}
}

void HostRenderer::send(const std::string &json) {
	if (auto message = twidget::Json::parse(json)) {
		_link->postMessage(std::move(*message));
	} else if (onDiag) {
		onDiag("renderer-message", "unparsed host message");
	}
}

void HostRenderer::setTheme(const std::string &theme) {
	_link->postTheme(theme == "dark");
}

void HostRenderer::setWidth(double) {
}

void HostRenderer::destroy() {
	_link->detach(this);
}

} // namespace

int DomModifiers(Qt::KeyboardModifiers modifiers) {
	const auto mac = Platform::IsMac();
	const auto bits = std::array{
		std::pair(Qt::AltModifier, kDomAlt),
		std::pair(mac ? Qt::MetaModifier : Qt::ControlModifier, kDomCtrl),
		std::pair(mac ? Qt::ControlModifier : Qt::MetaModifier, kDomMeta),
		std::pair(Qt::ShiftModifier, kDomShift),
	};
	auto result = 0;
	for (const auto &[modifier, bit] : bits) {
		if (modifiers.testFlag(modifier)) {
			result |= bit;
		}
	}
	return result;
}

int LogicalHeight(double css, int scale) {
	return int(std::ceil(css * scale / 100.));
}

HostLink::HostLink(
	not_null<RendererCore*> core,
	uint64 work,
	std::optional<double> hosted)
: _core(core)
, _work(work)
, _hosted(hosted.has_value()) {
	if (hosted) {
		_route = Route::Pending;
		_buffered.push_back(HostedWire::EncodeOpen(*hosted));
		_bufferedBytes = _buffered.back().size();
	}
}

HostLink::~HostLink() {
	if (_kept || _slot) {
		crl::async([
			kept = std::move(_kept),
			slot = std::move(_slot)
		]() mutable {
			kept = nullptr;
			slot = nullptr;
		});
	}
	PostReleased(base::take(_released));
}

bool HostLink::attach(not_null<twidget::Renderer*> proxy) {
	auto lock = std::lock_guard(_mutex);
	if (_closed) {
		return false;
	}
	_proxy = proxy.get();
	return true;
}

void HostLink::detach(not_null<twidget::Renderer*> proxy) {
	auto lock = std::lock_guard(_mutex);
	if (_proxy == proxy.get()) {
		_proxy = nullptr;
		_host.reset();
	}
}

void HostLink::close() {
	auto lock = std::lock_guard(_mutex);
	_closed = true;
	_proxy = nullptr;
	_host.reset();
}

void HostLink::forget() {
	_gone = true;
}

bool HostLink::gone() const {
	return _gone;
}

uint64 HostLink::workId() const {
	return _work;
}

bool HostLink::bindHost(std::weak_ptr<twidget::WidgetHost> host) {
	auto lock = std::lock_guard(_mutex);
	if (_route == Route::Local) {
		if (!_closed) {
			_host = std::move(host);
		}
		return true;
	} else if (_route != Route::Pending) {
		return false;
	}
	const auto strong = host.lock();
	if (!strong) {
		return false;
	} else if (!_closed) {
		_host = host;
	}
	_live = std::move(host);
	_route = Route::Live;
	for (auto &message : base::take(_buffered)) {
		strong->sendPlatform(std::move(message));
	}
	_bufferedBytes = 0;
	return true;
}

void HostLink::toHost(const std::string &json) {
	auto lock = std::lock_guard(_mutex);
	if (_proxy && _proxy->onMessage) {
		_proxy->onMessage(json);
	}
}

void HostLink::diag(const std::string &kind, const std::string &detail) {
	auto lock = std::lock_guard(_mutex);
	if (_proxy && _proxy->onDiag) {
		_proxy->onDiag(kind, detail);
	} else if (const auto host = _live.lock()) {
		host->platformDiag(kind, detail);
	}
}

RendererCore *HostLink::current() const {
	auto lock = std::lock_guard(_mutex);
	return _closed ? nullptr : _core.get();
}

std::optional<bool> HostLink::hostBottom() const {
	const auto host = [&] {
		auto lock = std::lock_guard(_mutex);
		return _host.lock();
	}();
	if (!host) {
		return std::nullopt;
	}
	auto result = false;
	// WHY: the host tells of a new anchor only through onChange, which copies
	// every console line first, so the anchor standing when a height is
	// applied is read in place, under the host's own lock, with no copy.
	host->waitUntil([&](const twidget::WidgetDiagnostics &diagnostics) {
		result = (diagnostics.resizeAnchor == "bottom");
		return true;
	}, 0.);
	return result;
}

void HostLink::work(Fn<void(RendererWork&)> task, bool host) {
	PostToRenderThread([
		link = shared_from_this(),
		task = std::move(task),
		host
	] {
		if (link->_gone || (host && link->_closed)) {
			return;
		} else if (const auto work = RendererWork::Find(link->_work)) {
			const auto operation = work->operation();
			task(*work);
		}
	});
}

void HostLink::toPlatform(std::string message) {
	platformSend(std::move(message), false);
}

bool HostLink::pressPlatform(std::string message) {
	return platformSend(std::move(message), true);
}

bool HostLink::platformSend(std::string &&message, bool press) {
	auto lost = false;
	auto live = false;
	{
		auto lock = std::lock_guard(_mutex);
		if (_routeClosed) {
			return false;
		}
		switch (_route) {
		case Route::Pending:
		case Route::Keeping:
			lost = !bufferLocked(std::move(message));
			break;
		case Route::Live:
			if (const auto host = _live.lock()) {
				if (press) {
					host->sendPlatformPress(std::move(message));
					live = true;
				} else {
					host->sendPlatform(std::move(message));
				}
			} else {
				lost = true;
			}
			break;
		case Route::Kept:
			_kept->sendPlatform(std::move(message));
			break;
		case Route::Local:
		case Route::Closed:
			break;
		}
	}
	if (lost) {
		releaseRoute(nullptr);
	}
	return live;
}

void HostLink::released() {
	auto lock = std::lock_guard(_mutex);
	if (_route != Route::Live) {
		return;
	} else if (const auto host = _live.lock()) {
		host->releaseDelivered();
	}
}

bool HostLink::reserveKeep() {
	auto lock = std::lock_guard(_mutex);
	if (_route != Route::Live || _slot || _live.expired()) {
		return false;
	}
	_slot = KeptSlot::Reserve();
	return (_slot != nullptr);
}

void HostLink::keep(twidget::WidgetHost &host) {
	auto slot = std::unique_ptr<KeptSlot>();
	auto released = std::vector<Fn<void()>>();
	{
		auto lock = std::lock_guard(_mutex);
		if (!_slot) {
			return;
		} else if (_route == Route::Live && _live.lock().get() == &host) {
			_route = Route::Keeping;
		} else {
			slot = base::take(_slot);
			closeLocked();
			released = takeReleasedLocked();
		}
	}
	if (slot) {
		slot = nullptr;
		PostReleased(std::move(released));
		markGone();
		return;
	}
	auto kept = host.keepRenderer(keptObservers());
	{
		auto lock = std::lock_guard(_mutex);
		if (kept && !_releaseWanted) {
			_kept = std::move(kept);
			_route = Route::Kept;
			_live.reset();
			for (auto &message : base::take(_buffered)) {
				_kept->sendPlatform(std::move(message));
			}
			_bufferedBytes = 0;
			return;
		}
		_releaseWanted = false;
		slot = base::take(_slot);
		closeLocked();
		++_releases;
	}
	markGone();
	completeRelease(std::move(kept), std::move(slot));
}

void HostLink::keepDropped() {
	auto slot = std::unique_ptr<KeptSlot>();
	auto released = std::vector<Fn<void()>>();
	{
		auto lock = std::lock_guard(_mutex);
		if (!_slot || _route == Route::Keeping || _route == Route::Kept) {
			return;
		}
		slot = base::take(_slot);
		closeLocked();
		released = takeReleasedLocked();
	}
	slot = nullptr;
	PostReleased(std::move(released));
	markGone();
}

void HostLink::releaseRoute(Fn<void()> done) {
	auto kept = std::unique_ptr<twidget::HostedKeptRenderer>();
	auto slot = std::unique_ptr<KeptSlot>();
	auto released = std::vector<Fn<void()>>();
	{
		auto lock = std::lock_guard(_mutex);
		if (done) {
			_released.push_back(std::move(done));
		}
		if (_route == Route::Keeping) {
			_releaseWanted = true;
			_routeClosed = true;
			_buffered.clear();
			_bufferedBytes = 0;
		} else {
			if (_route == Route::Kept) {
				kept = base::take(_kept);
				slot = base::take(_slot);
				++_releases;
			}
			closeLocked();
		}
		released = takeReleasedLocked();
	}
	if (kept) {
		crl::async([
			link = shared_from_this(),
			kept = std::move(kept),
			slot = std::move(slot)
		]() mutable {
			link->completeRelease(std::move(kept), std::move(slot));
		});
	}
	PostReleased(std::move(released));
	if (_hosted) {
		markGone();
	}
}

twidget::HostedKeptRenderer::Observers HostLink::keptObservers() {
	const auto weak = weak_from_this();
	return {
		.onPlatformMessage = [=](const std::string &message) {
			PostTraffic(weak, message, nullptr);
		},
		.onPlatformRequest = [=](
				const std::string &message,
				Fn<void(std::string answer)> respond) {
			PostTraffic(weak, message, std::move(respond));
		},
		.onEnded = [=](const std::string &) {
			crl::async([=] {
				if (const auto link = weak.lock()) {
					link->releaseRoute(nullptr);
				}
			});
		},
	};
}

bool HostLink::bufferLocked(std::string &&message) {
	if (const auto kind = SettingKind(message)) {
		for (auto i = _buffered.rbegin(); i != _buffered.rend(); ++i) {
			const auto was = SettingKind(*i);
			if (!was) {
				break;
			} else if (*was == *kind) {
				_bufferedBytes -= i->size();
				_buffered.erase(std::next(i).base());
				break;
			}
		}
	}
	if ((_buffered.size() >= kMaxBufferedSends)
		|| (message.size() > kMaxBufferedBytes - _bufferedBytes)) {
		return false;
	}
	_bufferedBytes += message.size();
	_buffered.push_back(std::move(message));
	return true;
}

void HostLink::closeLocked() {
	if (_route != Route::Local) {
		_route = Route::Closed;
	}
	_routeClosed = true;
	_buffered.clear();
	_bufferedBytes = 0;
	_live.reset();
}

std::vector<Fn<void()>> HostLink::takeReleasedLocked() {
	return (_slot || _releases > 0)
		? std::vector<Fn<void()>>()
		: base::take(_released);
}

void HostLink::completeRelease(
		std::unique_ptr<twidget::HostedKeptRenderer> kept,
		std::unique_ptr<KeptSlot> slot) {
	kept = nullptr;
	slot = nullptr;
	auto released = std::vector<Fn<void()>>();
	{
		auto lock = std::lock_guard(_mutex);
		--_releases;
		released = takeReleasedLocked();
	}
	PostReleased(std::move(released));
}

void HostLink::markGone() {
	if (_documentGone.exchange(true)) {
		return;
	}
	work([](RendererWork &work) {
		work.routeEnded();
	}, false);
	crl::on_main([link = shared_from_this()] {
		if (const auto core = link->_core.get()) {
			core->routeLost();
		}
	});
}

void HostLink::postLoaded() {
	work([](RendererWork &work) {
		work.hostLoaded();
	}, true);
}

void HostLink::postMessage(twidget::Json message) {
	if (message["type"].asString() == "frozen") {
		// WHY: HtmlWidget::drawStopped() paints the themed stop pill; the
		// core's own freeze would rebuild its badge and fatal dimming into
		// the last frame, so the core never sees this message.
		postFrozen(
			QString::fromStdString(message["reason"].asString()),
			message["fatal"].asBool());
		return;
	}
	work([message = std::move(message)](RendererWork &work) {
		work.hostMessage(message);
	}, true);
}

void HostLink::postTheme(bool dark) {
	work([=](RendererWork &work) {
		work.hostTheme(dark);
	}, true);
}

void HostLink::postFrozen(QString reason, bool fatal) {
	crl::on_main([link = shared_from_this()] {
		if (const auto core = link->current()) {
			core->hostFrozen();
		}
	});
	work([=](RendererWork &work) {
		work.freeze(reason, fatal);
	}, true);
}

void HostLink::postDeath(const std::string &, QString label) {
	postFrozen(std::move(label), true);
}

void HostLink::postTraffic(HostedTraffic traffic) {
	if (_gone || _routeClosed) {
		return;
	}
	PostToRenderThread([
		link = shared_from_this(),
		shared = std::make_shared<HostedTraffic>(std::move(traffic))
	] {
		if (link->_gone || link->_routeClosed) {
			return;
		} else if (const auto work = RendererWork::Find(link->_work)) {
			work->platformTraffic(std::move(*shared));
		}
	});
}

void HostLink::PostTraffic(
		const std::weak_ptr<HostLink> &weak,
		const std::string &message,
		Fn<void(std::string)> respond) {
	const auto delivery = twidget::WidgetHost::platformDelivery();
	auto traffic = HostedTraffic(
		message,
		delivery.hold,
		delivery.request,
		std::move(respond));
	if (const auto link = weak.lock()) {
		link->postTraffic(std::move(traffic));
	}
}

void HostLink::postLinkRequest(RendererCore::LinkRequest request) {
	crl::on_main([link = shared_from_this(), request = std::move(request)] {
		if (const auto core = link->current()) {
			core->hostLinkRequest(request);
		}
	});
}

void HostLink::postBotRequest(RendererCore::BotRequest request) {
	crl::on_main([link = shared_from_this(), request = std::move(request)] {
		if (const auto core = link->current()) {
			core->hostBotRequest(request);
		} else {
			RespondFailed(request.respond);
		}
	});
}

void HostLink::postCopyText(std::string text) {
	crl::on_main([link = shared_from_this(), text = std::move(text)] {
		if (const auto core = link->current()) {
			core->hostCopyText(text);
		}
	});
}

void HostLink::postWatch(bool on) {
	crl::on_main([link = shared_from_this(), on] {
		if (const auto core = link->current()) {
			core->hostWatch(on);
		}
	});
}

RendererCore::RendererCore(double width, double dpr, bool hosted)
: _link(std::make_shared<HostLink>(
	this,
	++WorkSerial,
	(hosted
		? std::make_optional(RendererWork::DeviceRatio(dpr))
		: std::nullopt)))
, _wait(hosted ? std::make_shared<twidget::SandboxHostedWait>() : nullptr)
, _reports(hosted ? std::make_shared<ReportPost>() : nullptr)
, _pictures(hosted ? std::make_shared<PicturePost>() : nullptr)
, _size{ .width = ValidWidth(width) ? width : kFallbackWidth }
, _menuDeadline([=] { menuDeadline(); })
, _claimDeadline([=] { claimDeadline(); }) {
	auto values = RendererValues{
		.regularFamily = st::normalFont->f.family(),
		.monospaceFamily = st::normalFont->monospace()->f.family(),
		.positions = CurrentFontPositions(),
		.emoji = CurrentEmojiSides(),
	};
	PostToRenderThread([
		link = _link,
		chat = base::make_weak(this),
		width = _size.width,
		dpr,
		values = std::move(values),
		wait = _wait,
		reports = _reports,
		pictures = _pictures
	] {
		if (!link->gone()) {
			RendererWork::Create(
				link->workId(),
				chat,
				width,
				dpr,
				values,
				HostOf(link, (wait != nullptr)),
				(wait
					? std::make_optional(RendererHosted{
						.wait = wait,
						.reports = reports,
						.pictures = pictures,
					})
					: std::nullopt));
		}
	});
}

RendererCore::~RendererCore() {
	if (_pictures) {
		_pictures->close();
	}
	_link->close();
	_link->forget();
	_link->releaseRoute(nullptr);
	PostToRenderThread([id = _link->workId()] {
		RendererWork::Destroy(id);
	});
}

uint64 RendererCore::workId() const {
	return _link->workId();
}

std::shared_ptr<twidget::SandboxHostedWait> RendererCore::hostedWait() const {
	return _wait;
}

RendererCore::HostedHandlers RendererCore::hostedHandlers(
		QString stoppedLabel) const {
	const auto weak = std::weak_ptr<HostLink>(_link);
	return {
		.message = [=](const std::string &message) {
			HostLink::PostTraffic(weak, message, nullptr);
		},
		.request = [=](
				const std::string &message,
				Fn<void(std::string answer)> respond) {
			HostLink::PostTraffic(weak, message, std::move(respond));
		},
		.death = [=](const std::string &reason) {
			if (const auto link = weak.lock()) {
				link->postDeath(reason, stoppedLabel);
			}
		},
	};
}

Fn<void(twidget::WidgetHost &host)> RendererCore::keepRoute() {
	if (!_wait || _routeChecked || _documentGone || !_link->reserveKeep()) {
		return nullptr;
	}
	const auto ticket = std::make_shared<KeepTicket>(_link);
	return [=](twidget::WidgetHost &host) {
		ticket->run(host);
	};
}

void RendererCore::releaseRoute(Fn<void()> done) {
	_link->releaseRoute(std::move(done));
}

bool RendererCore::routeChecked() const {
	return _routeChecked;
}

bool RendererCore::documentGone() const {
	return _documentGone;
}

rpl::producer<> RendererCore::keptUpdates() const {
	return _keptUpdates.events();
}

twidget::RendererFactory RendererCore::hostFactory() const {
	return [link = _link](
			std::shared_ptr<twidget::SandboxResources>,
			const std::string &theme,
			double) {
		link->postTheme(theme == "dark");
		return std::unique_ptr<twidget::Renderer>(
			std::make_unique<HostRenderer>(link));
	};
}

auto RendererCore::hostActionHandler() const
-> Fn<void(const twidget::WidgetAction &action)> {
	return [link = _link](const twidget::WidgetAction &action) {
		using Action = twidget::WidgetAction;
		const auto telegram = (action.kind == Action::OpenTelegramLink);
		if (telegram || action.kind == Action::OpenLink) {
			link->postLinkRequest({
				.url = action.url,
				.telegram = telegram,
			});
		}
	};
}

RendererCore::RequestHandler RendererCore::hostRequestHandler() const {
	return [link = _link](
			const twidget::WidgetRequest &request,
			Respond respond) {
		link->postBotRequest({
			.data = request.data,
			.respond = std::move(respond),
			.wallet = request.wallet,
		});
	};
}

RendererCore::WalletHandler RendererCore::hostWalletHandler() const {
	return [link = _link](const twidget::WalletCall &call, Respond respond) {
		link->postBotRequest({
			.respond = std::move(respond),
			.call = std::make_shared<const twidget::WalletCall>(call),
		});
	};
}

RendererCore::CopyHandler RendererCore::hostCopyHandler() const {
	return [link = _link](const std::string &text) {
		link->postCopyText(text);
	};
}

RendererCore::WatchHandler RendererCore::hostWatchHandler() const {
	return [link = _link](bool on) {
		link->postWatch(on);
	};
}

bool RendererCore::bindHost(std::weak_ptr<twidget::WidgetHost> host) {
	return _link->bindHost(std::move(host));
}

void RendererCore::detach() {
	_detached = true;
	Test::WidgetCoreDetached(workId());
	endInput();
	forgetSelection();
	if (_reports) {
		_reports->close();
	}
	_link->close();
	work([](RendererWork &work) {
		work.detach();
	});
}

bool RendererCore::attached() const {
	return !_detached;
}

void RendererCore::setOnScreen(bool onScreen) {
	if (_onScreen != onScreen) {
		_onScreen = onScreen;
		if (!onScreen) {
			endInput();
		}
		work([=, visibility = ++_visibility](RendererWork &work) {
			work.setOnScreen(onScreen, visibility);
		});
	}
}

bool RendererCore::acceptsInput() const {
	return _onScreen
		&& !_detached
		&& !_frozen
		&& !_stopped
		&& _hasContent;
}

void RendererCore::pointerEnter() {
	if (acceptsInput() && !TouchBridge::Active()) {
		work([serial = ++_pointerSerial](RendererWork &work) {
			work.pointerEnter(serial);
		});
	}
}

void RendererCore::pointerMove(QPointF point, int modifiers) {
	if (acceptsInput() && !TouchBridge::Active()) {
		work([=, serial = ++_pointerSerial](RendererWork &work) {
			work.pointerMove(point, modifiers, serial);
		});
	}
}

void RendererCore::pointerLeave() {
	_input.hand = false;
	work([serial = ++_pointerSerial](RendererWork &work) {
		work.pointerLeave(serial);
	});
}

void RendererCore::pointerPress(QPointF point, int modifiers) {
	if (!acceptsInput() || TouchBridge::Active()) {
		return;
	}
	endMenu();
	_input.pressed = true;
	_input.menuPressed = false;
	forgetSelection();
	const auto serial = ++_pointerSerial;
	const auto selection = _selectionSerial;
	Test::WidgetPressDispatched(workId(), serial);
	work([=](RendererWork &work) {
		work.pointerPress(point, modifiers, serial, selection);
	});
}

void RendererCore::pointerRelease(QPointF point, int modifiers) {
	_input.pressed = _input.menuPressed = false;
	const auto cancel = TouchBridge::Active();
	const auto serial = ++_pointerSerial;
	work([=](RendererWork &work) {
		work.pointerRelease(point, modifiers, cancel, serial);
	});
}

bool RendererCore::menuPress(
		QPointF point,
		int modifiers,
		MenuDecided decided) {
	endMenu();
	if (!acceptsInput()
		|| TouchBridge::Active()
		|| pressed()
		|| _input.fingerPressed) {
		return false;
	}
	const auto serial = ++_pressSerial;
	_menu = PendingMenu{ .decided = std::move(decided), .serial = serial };
	_menuDeadline.callOnce(kAnswerDeadline);
	work([=](RendererWork &work) {
		work.menuPress(point, modifiers, serial);
	});
	return true;
}

void RendererCore::menuRelease(int modifiers) {
	endMenu();
	_input.menuPressed = false;
	work([=, cancel = TouchBridge::Active()](RendererWork &work) {
		work.menuRelease(modifiers, cancel);
	});
}

bool RendererCore::fingerPress(
		QPointF point,
		int modifiers,
		ClaimArrived claimed) {
	if (!acceptsInput() || _input.fingerPressed) {
		return false;
	}
	endClaim();
	const auto serial = ++_pressSerial;
	_input.fingerPressed = true;
	_claim = PendingClaim{ .claimed = std::move(claimed), .serial = serial };
	_claimDeadline.callOnce(kAnswerDeadline);
	work([=](RendererWork &work) {
		work.fingerPress(point, modifiers, serial);
	});
	return true;
}

void RendererCore::fingerMove(QPointF point, int modifiers) {
	if (_input.fingerPressed) {
		work([=](RendererWork &work) {
			work.fingerMove(point, modifiers);
		});
	}
}

void RendererCore::fingerHold() {
	if (_input.fingerPressed) {
		work([](RendererWork &work) {
			work.fingerHold();
		});
	}
}

void RendererCore::fingerRelease(QPointF point, int modifiers) {
	endClaim();
	_input.fingerPressed = false;
	work([=](RendererWork &work) {
		work.fingerRelease(point, modifiers);
	});
}

void RendererCore::fingerCancel() {
	endClaim();
	_input.fingerPressed = false;
	work([](RendererWork &work) {
		work.fingerCancel();
	});
}

void RendererCore::resetInput() {
	endInput();
	work([](RendererWork &work) {
		work.resetInput();
	});
}

bool RendererCore::pressed() const {
	return _input.pressed || _input.menuPressed;
}

bool RendererCore::fingerPressed() const {
	return _input.fingerPressed;
}

bool RendererCore::handCursor() const {
	return _input.hand;
}

bool RendererCore::hasSelection() const {
	return _hasSelection;
}

QString RendererCore::selectedText() const {
	return _selectedText;
}

void RendererCore::clearSelection() {
	forgetSelection();
	work([serial = _selectionSerial](RendererWork &work) {
		work.clearSelection(serial);
	});
}

void RendererCore::setSize(RendererSize size) {
	if (!ValidWidth(size.width)) {
		size.width = _size.width;
	}
	if (_sizeSerial && (_size == size)) {
		return;
	}
	_size = size;
	work([size, serial = ++_sizeSerial](RendererWork &work) {
		work.setSize(size, serial);
	});
}

void RendererCore::setCut(const RendererCut &cut) {
	work([cut](RendererWork &work) {
		work.setCut(cut);
	});
}

double RendererCore::width() const {
	return _size.width;
}

std::optional<double> RendererCore::height() const {
	return _height;
}

bool RendererCore::bottomAnchored() const {
	return _heightBottom;
}

bool RendererCore::hasContent() const {
	return _hasContent;
}

bool RendererCore::loaded() const {
	return _loaded;
}

bool RendererCore::stopped() const {
	return _stopped;
}

const QString &RendererCore::stopReason() const {
	return _stopReason;
}

bool RendererCore::stopFatal() const {
	return _stopFatal;
}

QImage RendererCore::frame(QSize cardArea) {
	const auto held = !_frame.isNull()
		&& (_frameSerial == _sizeSerial)
		&& (_frameArea == cardArea)
		&& (_frame.devicePixelRatio() == style::DevicePixelRatio());
	const auto outdated = _pictures && _pictures->takeOutdated();
	if (base::take(_refresh) || outdated || !held) {
		work([](RendererWork &work) {
			work.demand();
		});
	}
	if (!held) {
		return QImage();
	} else if (base::take(_undrawn)) {
		work([](RendererWork &work) {
			work.drawn();
		});
	}
	return _frame;
}

int64 RendererCore::frameBytes() const {
	return _frame.sizeInBytes();
}

void RendererCore::dropFrame() {
	_frame = QImage();
	_pixels = nullptr;
	_frameArea = QSize();
	_refresh = false;
	work([epoch = ++_epoch](RendererWork &work) {
		work.dropFrame(epoch);
	});
}

void RendererCore::refreshFrame() {
	_refresh = true;
	work([](RendererWork &work) {
		work.refreshFrame();
	});
}

rpl::producer<RendererCore::Changes> RendererCore::updates() const {
	return _updates.events();
}

rpl::producer<RendererCore::LinkRequest> RendererCore::linkRequests() const {
	return _linkRequests.events();
}

rpl::producer<RendererCore::BotRequest> RendererCore::botRequests() const {
	return _botRequests.events();
}

rpl::producer<std::string> RendererCore::copyRequests() const {
	return _copyRequests.events();
}

rpl::producer<bool> RendererCore::watchChanges() const {
	return _watchChanges.events();
}

void RendererCore::hostFrozen() {
	if (_frozen) {
		return;
	}
	_frozen = true;
	endInput();
	forgetSelection();
	if (_reports) {
		_reports->close();
	}
}

void RendererCore::routeLost() {
	if (!std::exchange(_documentGone, true)) {
		_keptUpdates.fire({});
	}
}

void RendererCore::hostLinkRequest(const LinkRequest &request) {
	if (!refusesHost()) {
		_linkRequests.fire_copy(request);
	}
}

void RendererCore::hostBotRequest(const BotRequest &request) {
	if (refusesHost() || !_botRequests.has_consumers()) {
		RespondFailed(request.respond);
	} else {
		_botRequests.fire_copy(request);
	}
}

void RendererCore::hostCopyText(const std::string &text) {
	if (!refusesHost()) {
		_copyRequests.fire_copy(text);
	}
}

void RendererCore::hostWatch(bool on) {
	if (!refusesHost()) {
		_watchChanges.fire_copy(on);
	}
}

bool RendererCore::refusesHost() const {
	return _frozen || _stopped;
}

void RendererCore::work(Fn<void(RendererWork&)> task) const {
	_link->work(std::move(task), false);
}

void RendererCore::apply(RendererAnswer &&answer) {
	if (answer.closed && !std::exchange(_routeChecked, true)) {
		_link->releaseRoute(nullptr);
	}
	auto changes = Changes();
	if (answer.state.hasContent && !_hasContent) {
		_hasContent = true;
		changes |= Change::Content;
	}
	if (answer.state.loaded && !_loaded) {
		_loaded = true;
		changes |= Change::Loaded;
	}
	if (answer.layout) {
		changes |= applyLayout(std::move(*answer.layout));
	} else if (_pictures) {
		const auto post = answer.post;
		if (auto layout = _pictures->take(post, _sizeSerial, _epoch)) {
			changes |= applyLayout(std::move(*layout));
		}
	}
	if (answer.stop && !_stopped) {
		_stopped = true;
		_stopReason = std::move(answer.stop->reason);
		_stopFatal = answer.stop->fatal;
		changes |= Change::Stopped | Change::Repaint;
		if (_input.hand || _frozen) {
			changes |= Change::Cursor;
		}
		endInput();
		forgetSelection();
		if (_reports) {
			_reports->close();
		}
	}
	if (!_detached && !refusesHost()) {
		changes |= applyInput(answer.selection, answer.cursor);
	}
	const auto decided = answer.menu
		? takeMenu(*answer.menu)
		: Fn<void()>();
	const auto claimed = answer.claim
		? takeClaim(*answer.claim)
		: Fn<void()>();
	const auto weak = base::make_weak(this);
	if (changes) {
		_updates.fire_copy(changes);
	}
	if (decided && weak) {
		decided();
	}
	if (claimed && weak) {
		claimed();
	}
}

RendererCore::Changes RendererCore::applyLayout(RendererLayout &&layout) {
	if ((layout.serial != _sizeSerial) || (layout.epoch != _epoch)) {
		return {};
	}
	auto result = Changes(Change::Repaint);
	if (_height != layout.height) {
		_height = layout.height;
		result |= Change::Height;
	}
	if (layout.client || (layout.visibility != _visibility)) {
		result |= Change::Client;
	}
	_heightBottom = layout.bottom;
	_frame = std::move(layout.picture);
	_pixels = std::move(layout.pixels);
	if (_pixels) {
		_pixels->dequeue();
	}
	_frameArea = layout.area;
	_frameSerial = layout.serial;
	_undrawn = true;
	return result;
}

RendererCore::Changes RendererCore::applyInput(
		std::optional<RendererSelection> &selection,
		const std::optional<RendererCursor> &cursor) {
	auto result = Changes();
	if (selection && applySelection(*selection)) {
		result |= Change::Selection;
	}
	if (cursor) {
		const auto current = (cursor->serial == _pointerSerial);
		if ((current || !cursor->hand) && (_input.hand != cursor->hand)) {
			_input.hand = cursor->hand;
			result |= Change::Cursor;
		}
	}
	return result;
}

bool RendererCore::applySelection(RendererSelection &selection) {
	if ((selection.serial != _selectionSerial)
		|| ((_hasSelection == selection.has)
			&& (_selectedText == selection.text))) {
		return false;
	}
	_hasSelection = selection.has;
	_selectedText = std::move(selection.text);
	return true;
}

void RendererCore::takeReports() {
	if (!_reports) {
		return;
	}
	auto latest = _reports->take();
	if (_detached || refusesHost()) {
		return;
	}
	const auto changes = applyInput(latest.selection, latest.cursor);
	if (changes) {
		_updates.fire_copy(changes);
	}
}

Fn<void()> RendererCore::takeMenu(const RendererMenu &menu) {
	if (!_menu || (_menu->serial != menu.serial)) {
		return nullptr;
	}
	auto decided = std::move(base::take(_menu)->decided);
	_menuDeadline.cancel();
	work([serial = menu.serial, taken = menu.taken](RendererWork &work) {
		work.menuDecided(serial, taken);
	});
	if (menu.taken) {
		_input.menuPressed = true;
	}
	return [decided = std::move(decided), taken = menu.taken] {
		decided(taken);
	};
}

Fn<void()> RendererCore::takeClaim(const RendererClaim &claim) {
	if (!_claim || (_claim->serial != claim.serial)) {
		return nullptr;
	}
	auto claimed = std::move(base::take(_claim)->claimed);
	_claimDeadline.cancel();
	return [claimed = std::move(claimed), claim = claim.claim] {
		claimed(claim);
	};
}

void RendererCore::endInput() {
	endMenu();
	endClaim();
	_input = Input();
	++_pointerSerial;
}

void RendererCore::endMenu() {
	if (!_menu) {
		return;
	}
	const auto serial = base::take(_menu)->serial;
	_menuDeadline.cancel();
	work([=](RendererWork &work) {
		work.menuDecided(serial, false);
	});
}

void RendererCore::endClaim() {
	_claim = std::nullopt;
	_claimDeadline.cancel();
}

void RendererCore::forgetSelection() {
	_hasSelection = false;
	_selectedText = QString();
	++_selectionSerial;
}

void RendererCore::menuDeadline() {
	if (!_menu) {
		return;
	}
	auto pending = *base::take(_menu);
	work([serial = pending.serial](RendererWork &work) {
		work.menuDecided(serial, false);
	});
	pending.decided(false);
}

void RendererCore::claimDeadline() {
	if (!_claim) {
		return;
	}
	auto pending = *base::take(_claim);
	pending.claimed(twidget::gesture::NO_CLAIM);
}

} // namespace Tgw
