/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer.h"

#include "renderer-litehtml/src/renderer.h"
#include "tgw/tgw_backend.h"
#include "tgw/tgw_painter.h"
#include "twidget/json.h"
#include "ui/style/style_core_scale.h"

#include <mutex>

namespace Tgw {

class HostLink final : public std::enable_shared_from_this<HostLink> {
public:
	explicit HostLink(not_null<RendererCore*> core);

	[[nodiscard]] bool attach(not_null<twidget::Renderer*> proxy);
	void detach(not_null<twidget::Renderer*> proxy);
	void close();
	void toHost(const std::string &json);

	void postLoaded();
	void postMessage(twidget::Json message);
	void postTheme(bool dark);
	void postBridgeCall(RendererCore::BridgeCall call);

private:
	[[nodiscard]] RendererCore *current() const;

	const base::weak_ptr<RendererCore> _core;
	mutable std::mutex _mutex;
	twidget::Renderer *_proxy = nullptr;
	bool _closed = false;

};

namespace {

constexpr auto kMinHeight = 24.;
constexpr auto kMaxHeight = 900.;
constexpr auto kFallbackWidth = 1.;
constexpr auto kReadyMessage = "{\"type\":\"ready\"}";

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

[[nodiscard]] bool ValidWidth(double width) {
	return std::isfinite(width) && (width > 0.);
}

[[nodiscard]] int DeviceSide(uint32 side) {
	return int(std::min(side, uint32(std::numeric_limits<int>::max())));
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
	}
}

void HostLink::close() {
	auto lock = std::lock_guard(_mutex);
	_closed = true;
	_proxy = nullptr;
}

void HostLink::toHost(const std::string &json) {
	auto lock = std::lock_guard(_mutex);
	if (_proxy && _proxy->onMessage) {
		_proxy->onMessage(json);
	}
}

RendererCore *HostLink::current() const {
	auto lock = std::lock_guard(_mutex);
	return _closed ? nullptr : _core.get();
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

void HostLink::postBridgeCall(RendererCore::BridgeCall call) {
	crl::on_main([link = shared_from_this(), call = std::move(call)] {
		if (const auto core = link->current()) {
			core->hostBridgeCall(call);
		}
	});
}

RendererCore::RendererCore(double width, double dpr)
: _link(std::make_shared<HostLink>(this))
, _backend(std::make_unique<QtBackend>(dpr))
, _renderer(std::make_unique<twlite::Renderer>(
	ValidWidth(width) ? width : kFallbackWidth,
	dpr,
	false,
	*_backend)) {
}

RendererCore::~RendererCore() {
	_link->close();
}

twidget::RendererFactory RendererCore::hostFactory() const {
	return [link = _link](
			std::shared_ptr<twidget::SandboxResources>,
			const std::string &,
			double) {
		return std::unique_ptr<twidget::Renderer>(
			std::make_unique<HostRenderer>(link));
	};
}

auto RendererCore::hostBridgeHandler() const
-> Fn<void(const std::string &method, const std::string &arguments)> {
	return [link = _link](
			const std::string &method,
			const std::string &arguments) {
		link->postBridgeCall({ method, arguments });
	};
}

void RendererCore::detach() {
	_detached = true;
	_pendingSize = std::string();
	_link->close();
}

bool RendererCore::attached() const {
	return !_detached;
}

void RendererCore::setWidth(double width) {
	if (!ValidWidth(width)) {
		return;
	}
	const auto was = _renderer->width();
	auto out = std::vector<twidget::Json>();
	_renderer->set_width(width, out);
	auto changes = Changes();
	if (_renderer->width() != was) {
		changes |= Change::Repaint;
	}
	finish(out, changes);
}

double RendererCore::width() const {
	return _renderer->width();
}

std::optional<double> RendererCore::height() const {
	return _height;
}

bool RendererCore::hasContent() const {
	return _hasContent;
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

not_null<twlite::Renderer*> RendererCore::renderer() const {
	return _renderer.get();
}

const QImage &RendererCore::frame() {
	const auto size = QSize(
		DeviceSide(_renderer->px_width()),
		DeviceSide(_renderer->px_height()));
	const auto ratio = style::DevicePixelRatio();
	if (!_renderer->dirty()
		&& (_frameSize == size)
		&& (_frame.isNull() || _frame.devicePixelRatio() == ratio)) {
		return _frame;
	}
	// WHY: the display list names backend font handles that the next
	// rebuild deletes, so it is rasterized at once and only the pixels
	// are kept.
	_frame = PaintFrame(
		_renderer->paint(),
		*_backend,
		size,
		_renderer->dpr());
	_frameSize = size;
	if (!_frame.isNull()) {
		_frame.setDevicePixelRatio(ratio);
	}
	return _frame;
}

void RendererCore::dropFrame() {
	_frame = QImage();
	_frameSize = QSize();
}

rpl::producer<RendererCore::Changes> RendererCore::updates() const {
	return _updates.events();
}

rpl::producer<RendererCore::BridgeCall> RendererCore::bridgeCalls() const {
	return _bridgeCalls.events();
}

void RendererCore::hostLoaded() {
	if (_ready || _stopped) {
		return;
	}
	_ready = true;
	_link->toHost(kReadyMessage);
	if (!_pendingSize.empty()) {
		_link->toHost(base::take(_pendingSize));
	}
}

void RendererCore::hostMessage(const twidget::Json &message) {
	const auto &type = message["type"].asString();
	if (_stopped || type == "requestFrame") {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	const auto out = _renderer->receive(message);
	auto changes = Changes();
	if (type == "ops" && !message["ops"].arr().empty()) {
		changes |= Change::Repaint;
		if (!_hasContent) {
			_hasContent = true;
			changes |= Change::Content;
		}
	} else if (type == "anim") {
		changes |= Change::Repaint;
	} else if (type == "frozen") {
		_stopped = true;
		_stopReason = QString::fromStdString(message["reason"].asString());
		_stopFatal = message["fatal"].asBool();
		changes |= Change::Stopped | Change::Repaint;
	}
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	finish(out, changes);
}

void RendererCore::hostTheme(bool dark) {
	if (_stopped) {
		return;
	}
	auto out = std::vector<twidget::Json>();
	_renderer->set_theme(dark, out);
	finish(out, Change::Repaint);
}

void RendererCore::hostBridgeCall(const BridgeCall &call) {
	if (!_stopped) {
		_bridgeCalls.fire_copy(call);
	}
}

void RendererCore::finish(
		const std::vector<twidget::Json> &out,
		Changes changes) {
	for (const auto &message : out) {
		if (message["type"].asString() == "size") {
			changes |= applySize(message);
		}
		forward(message);
	}
	if (changes) {
		_updates.fire_copy(changes);
	}
}

RendererCore::Changes RendererCore::applySize(
		const twidget::Json &message) {
	const auto &height = message["height"];
	if (!height.isNumber() || !std::isfinite(height.asNumber())) {
		return {};
	}
	const auto clamped = std::clamp(
		height.asNumber(),
		kMinHeight,
		kMaxHeight);
	if (_height == clamped) {
		return {};
	}
	_height = clamped;
	return Change::Height;
}

void RendererCore::forward(const twidget::Json &message) {
	if (_detached || _stopped) {
		return;
	} else if (_ready) {
		_link->toHost(message.dump());
	} else if (message["type"].asString() == "size") {
		_pendingSize = message.dump();
	}
}

} // namespace Tgw
