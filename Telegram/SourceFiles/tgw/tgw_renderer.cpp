/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer.h"

#include "base/platform/base_platform_info.h"
#include "tgw/tgw_renderer_work.h"
#include "twidget/json.h"
#include "twidget/sdk_policy.h"
#include "twidget/widget_host.h"

#include <mutex>

namespace Tgw {

class HostLink final : public std::enable_shared_from_this<HostLink> {
public:
	explicit HostLink(not_null<RendererCore*> core);

	[[nodiscard]] bool attach(not_null<twidget::Renderer*> proxy);
	void detach(not_null<twidget::Renderer*> proxy);
	void close();
	void bindHost(not_null<twidget::WidgetHost*> host);
	void toHost(const std::string &json);
	void diag(const std::string &kind, const std::string &detail);
	[[nodiscard]] std::optional<bool> hostBottom() const;

	void postLoaded();
	void postMessage(twidget::Json message);
	void postTheme(bool dark);
	void postLinkRequest(RendererCore::LinkRequest request);
	void postBotRequest(RendererCore::BotRequest request);
	void postCopyText(std::string text);
	void postWatch(bool on);

private:
	[[nodiscard]] RendererCore *current() const;

	const base::weak_ptr<RendererCore> _core;
	mutable std::mutex _mutex;
	twidget::Renderer *_proxy = nullptr;
	twidget::WidgetHost *_host = nullptr;
	bool _closed = false;

};

namespace {

constexpr auto kDomAlt = 1;
constexpr auto kDomCtrl = 2;
constexpr auto kDomMeta = 4;
constexpr auto kDomShift = 8;

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

void RespondFailed(const RendererCore::Respond &respond) {
	respond({ .failure = twidget::WidgetRequestFailure::Failed });
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

HostLink::HostLink(not_null<RendererCore*> core)
: _core(core) {
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
		_host = nullptr;
	}
}

void HostLink::close() {
	auto lock = std::lock_guard(_mutex);
	_closed = true;
	_proxy = nullptr;
	_host = nullptr;
}

void HostLink::bindHost(not_null<twidget::WidgetHost*> host) {
	auto lock = std::lock_guard(_mutex);
	if (!_closed) {
		_host = host.get();
	}
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
	}
}

RendererCore *HostLink::current() const {
	auto lock = std::lock_guard(_mutex);
	return _closed ? nullptr : _core.get();
}

std::optional<bool> HostLink::hostBottom() const {
	const auto host = [&] {
		auto lock = std::lock_guard(_mutex);
		return _host;
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

void HostLink::postLoaded() {
	crl::on_main([link = shared_from_this()] {
		if (const auto core = link->current()) {
			core->hostLoaded();
		}
	});
}

void HostLink::postMessage(twidget::Json message) {
	crl::on_main([link = shared_from_this(), message = std::move(message)] {
		if (const auto core = link->current()) {
			core->hostMessage(message);
		}
	});
}

void HostLink::postTheme(bool dark) {
	crl::on_main([link = shared_from_this(), dark] {
		if (const auto core = link->current()) {
			core->hostTheme(dark);
		}
	});
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

RendererCore::RendererCore(double width, double dpr)
: _link(std::make_shared<HostLink>(this))
, _work(std::make_unique<RendererWork>(
	width,
	dpr,
	RendererWork::Host{
		.send = [link = _link](const std::string &json) {
			link->toHost(json);
		},
		.diag = [link = _link](
				const std::string &kind,
				const std::string &detail) {
			link->diag(kind, detail);
		},
		.bottom = [link = _link] {
			return link->hostBottom();
		},
	},
	[=](Changes changes) { _updates.fire_copy(changes); })) {
}

RendererCore::~RendererCore() {
	_link->close();
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

void RendererCore::bindHost(not_null<twidget::WidgetHost*> host) {
	_link->bindHost(host);
}

void RendererCore::detach() {
	_work->detach();
	_link->close();
}

bool RendererCore::attached() const {
	return _work->attached();
}

void RendererCore::setOnScreen(bool onScreen) {
	_work->setOnScreen(onScreen);
}

bool RendererCore::acceptsInput() const {
	return _work->acceptsInput();
}

void RendererCore::pointerEnter() {
	_work->pointerEnter();
}

void RendererCore::pointerMove(QPointF point, int modifiers) {
	_work->pointerMove(point, modifiers);
}

void RendererCore::pointerLeave() {
	_work->pointerLeave();
}

void RendererCore::pointerPress(QPointF point, int modifiers) {
	_work->pointerPress(point, modifiers);
}

void RendererCore::pointerRelease(QPointF point, int modifiers) {
	_work->pointerRelease(point, modifiers);
}

bool RendererCore::menuPress(QPointF point, int modifiers) {
	return _work->menuPress(point, modifiers);
}

void RendererCore::menuRelease(int modifiers) {
	_work->menuRelease(modifiers);
}

std::optional<twidget::WidgetGestureClaim> RendererCore::fingerPress(
		QPointF point,
		int modifiers) {
	return _work->fingerPress(point, modifiers);
}

void RendererCore::fingerMove(QPointF point, int modifiers) {
	_work->fingerMove(point, modifiers);
}

void RendererCore::fingerHold() {
	_work->fingerHold();
}

void RendererCore::fingerRelease(QPointF point, int modifiers) {
	_work->fingerRelease(point, modifiers);
}

void RendererCore::fingerCancel() {
	_work->fingerCancel();
}

void RendererCore::resetInput() {
	_work->resetInput();
}

bool RendererCore::hovered() const {
	return _work->hovered();
}

bool RendererCore::pressed() const {
	return _work->pressed();
}

bool RendererCore::fingerPressed() const {
	return _work->fingerPressed();
}

bool RendererCore::handCursorAt(QPointF point) {
	return _work->handCursorAt(point);
}

bool RendererCore::hasSelection() const {
	return _work->hasSelection();
}

QString RendererCore::selectedText() const {
	return _work->selectedText();
}

void RendererCore::clearSelection() {
	_work->clearSelection();
}

void RendererCore::setWidth(double width) {
	_work->setWidth(width);
}

void RendererCore::setMaxHeight(double height) {
	_work->setMaxHeight(height);
}

double RendererCore::width() const {
	return _work->width();
}

std::optional<double> RendererCore::height() const {
	return _work->height();
}

bool RendererCore::bottomAnchored() const {
	return _work->bottomAnchored();
}

bool RendererCore::hasContent() const {
	return _work->hasContent();
}

bool RendererCore::loaded() const {
	return _work->loaded();
}

bool RendererCore::stopped() const {
	return _work->stopped();
}

const QString &RendererCore::stopReason() const {
	return _work->stopReason();
}

bool RendererCore::stopFatal() const {
	return _work->stopFatal();
}

not_null<twlite::Renderer*> RendererCore::renderer() const {
	return _work->renderer();
}

const QImage &RendererCore::frame(
		Images::CornersMaskRef corners,
		QSize cardArea,
		bool autoHeight) {
	_frame = QImage();
	_frame = _work->frame(corners, cardArea, autoHeight);
	_frameArea = _work->frameArea();
	return _frame;
}

int64 RendererCore::frameBytes() const {
	return _frame.sizeInBytes();
}

void RendererCore::dropFrame() {
	_work->dropFrame();
	_frame = QImage();
	_frameArea = QSize();
}

void RendererCore::refreshFrame() {
	_work->refreshFrame();
}

void RendererCore::takeFrameFrom(RendererCore &other) {
	_work->takeFrameFrom(*other._work);
	_frame = base::take(other._frame);
	_frameArea = base::take(other._frameArea);
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

void RendererCore::hostLoaded() {
	_work->hostLoaded();
}

void RendererCore::hostMessage(const twidget::Json &message) {
	_work->hostMessage(message);
}

void RendererCore::hostTheme(bool dark) {
	_work->hostTheme(dark);
}

void RendererCore::hostLinkRequest(const LinkRequest &request) {
	if (!_work->stopped()) {
		_linkRequests.fire_copy(request);
	}
}

void RendererCore::hostBotRequest(const BotRequest &request) {
	if (_work->stopped() || !_botRequests.has_consumers()) {
		RespondFailed(request.respond);
	} else {
		_botRequests.fire_copy(request);
	}
}

void RendererCore::hostCopyText(const std::string &text) {
	if (!_work->stopped()) {
		_copyRequests.fire_copy(text);
	}
}

void RendererCore::hostWatch(bool on) {
	if (!_work->stopped()) {
		_watchChanges.fire_copy(on);
	}
}

} // namespace Tgw
