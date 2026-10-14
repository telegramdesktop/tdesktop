/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer.h"

#include "renderer-litehtml/src/renderer.h"
#include "tgw/tgw_backend.h"
#include "tgw/tgw_cursor.h"
#include "tgw/tgw_input.h"
#include "tgw/tgw_painter.h"
#include "twidget/json.h"
#include "ui/style/style_core_scale.h"

#include <charconv>
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
constexpr auto kMinWakeDelay = crl::time(1);
constexpr auto kMaxWakeDelay = crl::time(std::numeric_limits<int>::max());
constexpr auto kTouchDown = 0;
constexpr auto kTouchMove = 1;
constexpr auto kTouchUp = 2;
constexpr auto kTouchCancel = 3;
constexpr auto kHoverMove = 0;
constexpr auto kHoverLeave = 1;
constexpr auto kMousePointer = 0;
constexpr auto kTapSlop = 8.;
constexpr auto kWheelPixels = 0;
constexpr auto kLocalQueryId = 0;
constexpr auto kWheelSuffix = std::string_view(":wheel");

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

[[nodiscard]] crl::time WakeDelay(double seconds) {
	return crl::time(std::clamp(
		std::ceil(seconds * 1000.),
		double(kMinWakeDelay),
		double(kMaxWakeDelay)));
}

[[nodiscard]] twidget::Json FrameMessage() {
	auto result = twidget::Json::object();
	result.set("type", "frame");
	result.set("time", twlite::Renderer::clock() * 1000.);
	return result;
}

[[nodiscard]] twidget::Json EmptyAnimMessage() {
	auto result = twidget::Json::object();
	result.set("type", "anim");
	result.set("now", twlite::Renderer::clock() * 1000.);
	result.set("anims", twidget::Json::array());
	return result;
}

[[nodiscard]] twidget::Json ElementAtQuery(QPointF point) {
	auto query = twidget::Json::object();
	query.set("q", "elementFromPoint");
	query.set("x", point.x());
	query.set("y", point.y());
	auto result = twidget::Json::object();
	result.set("type", "query");
	result.set("qid", kLocalQueryId);
	result.set("query", std::move(query));
	return result;
}

[[nodiscard]] twidget::Json TakeMessage(
		std::vector<twidget::Json> &out,
		std::string_view type) {
	const auto i = ranges::find_if(out, [&](const twidget::Json &message) {
		return (message["type"].asString() == type);
	});
	if (i == end(out)) {
		return twidget::Json();
	}
	auto result = std::move(*i);
	out.erase(i);
	return result;
}

void Append(
		std::vector<twidget::Json> &to,
		std::vector<twidget::Json> &&from) {
	to.insert(
		end(to),
		std::make_move_iterator(begin(from)),
		std::make_move_iterator(end(from)));
}

[[nodiscard]] std::vector<twidget::Json> Touch(
		twlite::Renderer &renderer,
		int action,
		QPointF point,
		int modifiers) {
	return renderer.touch(
		action,
		point.x(),
		point.y(),
		kMousePointer,
		true,
		true,
		modifiers);
}

[[nodiscard]] bool OutsideSlop(QPointF point, QPointF origin) {
	return (std::abs(point.x() - origin.x()) >= kTapSlop)
		|| (std::abs(point.y() - origin.y()) >= kTapSlop);
}

[[nodiscard]] int ParsedDocuments(const twlite::Renderer &renderer) {
	const auto &stats = renderer.stats();
	return stats.builds - stats.patched;
}

[[nodiscard]] int64 PreventedWheelTarget(const std::string &key) {
	if (key.size() <= kWheelSuffix.size() || !key.ends_with(kWheelSuffix)) {
		return 0;
	}
	const auto from = key.data();
	const auto till = from + (key.size() - kWheelSuffix.size());
	auto result = int64();
	const auto [last, error] = std::from_chars(from, till, result);
	return (error == std::errc() && last == till) ? result : 0;
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
	*_backend))
, _clock([=] { return step(); })
, _wake([=] { wake(); }) {
}

RendererCore::~RendererCore() {
	_clock.stop();
	_wake.cancel();
	releaseInput();
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

auto RendererCore::hostBridgeHandler() const
-> Fn<void(const std::string &method, const std::string &arguments)> {
	return [link = _link](
			const std::string &method,
			const std::string &arguments) {
		link->postBridgeCall({ method, arguments });
	};
}

void RendererCore::detach() {
	resetInput();
	_detached = true;
	_pendingSize = std::string();
	refreshClock();
	_link->close();
}

bool RendererCore::attached() const {
	return !_detached;
}

void RendererCore::setOnScreen(bool onScreen) {
	if (_onScreen == onScreen) {
		return;
	}
	_onScreen = onScreen;
	_stepBusy = false;
	if (!onScreen) {
		resetInput();
	}
	refreshClock();
}

bool RendererCore::acceptsInput() const {
	return canTick() && _hasContent;
}

void RendererCore::pointerEnter() {
	if (acceptsInput()) {
		_pointer.over = true;
		HoverWheel(this);
	}
}

void RendererCore::pointerMove(QPointF point, int modifiers) {
	if (!acceptsInput()) {
		return;
	}
	_pointer.position = point;
	_pointer.hit = elementAt(point);
	const auto wasDirty = _renderer->dirty();
	if (_pointer.pressed) {
		auto out = Touch(*_renderer, kTouchMove, point, modifiers);
		_pointer.moved = _pointer.moved
			|| OutsideSlop(point, _pointer.origin);
		const auto restyled = (_pointer.moved && trackHover()) || !wasDirty;
		inputDone(std::move(out), wasDirty, restyled);
		return;
	}
	_pointer.over = _pointer.hovering = true;
	HoverWheel(this);
	auto out = _renderer->hover(kHoverMove, point.x(), point.y(), modifiers);
	const auto restyled = trackHover() || !wasDirty;
	inputDone(std::move(out), wasDirty, restyled);
}

void RendererCore::pointerLeave() {
	_cursor = nullptr;
	UnhoverWheel(this);
	if (!acceptsInput()) {
		return;
	}
	_pointer.over = false;
	if (_pointer.pressed || !_pointer.hovering) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	inputDone(leaveHover(), wasDirty, true);
}

void RendererCore::pointerPress(QPointF point, int modifiers) {
	if (!acceptsInput()) {
		return;
	}
	const auto hit = elementAt(point);
	const auto wasDirty = _renderer->dirty();
	auto out = Touch(*_renderer, kTouchDown, point, modifiers);
	_pointer.claimed = TakeMessage(out, "gesture")["wants"].asBool();
	_pointer.position = _pointer.origin = point;
	_pointer.hit = _pointer.hover = _pointer.press = hit;
	_pointer.parsed = ParsedDocuments(*_renderer);
	_pointer.over = _pointer.hovering = _pointer.pressed = true;
	_pointer.moved = false;
	_pointer.active = true;
	HoverWheel(this);
	inputDone(std::move(out), wasDirty, true);
}

void RendererCore::pointerRelease(QPointF point, int modifiers) {
	if (!acceptsInput() || !_pointer.pressed) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = Touch(*_renderer, kTouchUp, point, modifiers);
	_pointer.position = point;
	_pointer.press = 0;
	_pointer.pressed = _pointer.claimed = false;
	_pointer.moved = _pointer.active = false;
	if (!_pointer.over) {
		Append(out, leaveHover());
	}
	inputDone(std::move(out), wasDirty, true);
}

bool RendererCore::hovered() const {
	return _pointer.over;
}

bool RendererCore::pressed() const {
	return _pointer.pressed;
}

bool RendererCore::claimed() const {
	return _pointer.claimed;
}

bool RendererCore::handCursorAt(QPointF point) {
	if (!acceptsInput()) {
		return false;
	}
	const auto hit = elementAt(point);
	const auto synced = (_pointer.parsed == ParsedDocuments(*_renderer));
	const auto held = _pointer.pressed
		&& !_pointer.moved
		&& !OutsideSlop(point, _pointer.origin);
	const auto hover = held ? (synced ? _pointer.hover : 0) : hit;
	const auto active = (synced && _pointer.active && hover == _pointer.hover)
		? hover
		: 0;
	if (!_cursor) {
		_cursor = std::make_unique<CursorStyles>(_renderer->dpr());
	}
	return _cursor->pointer(*_renderer, hit, hover, active);
}

void RendererCore::wheel(QPointF delta, int modifiers) {
	if (!acceptsInput() || !_listensWheel || delta.isNull()) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = _renderer->wheel(
		_pointer.position.x(),
		_pointer.position.y(),
		delta.x(),
		delta.y(),
		kWheelPixels,
		modifiers);
	inputDone(std::move(out), wasDirty, false);
}

bool RendererCore::wheelPrevented() {
	return acceptsInput()
		&& !_wheelPrevented.empty()
		&& _wheelPrevented.contains(elementAt(_pointer.position));
}

void RendererCore::key(
		bool down,
		const std::string &key,
		const std::string &code,
		bool repeat,
		int modifiers) {
	if (!acceptsInput()) {
		return;
	}
	const auto wasDirty = _renderer->dirty();
	auto out = _renderer->key(
		down ? "keydown" : "keyup",
		key,
		code,
		repeat,
		modifiers);
	inputDone(std::move(out), wasDirty, false);
}

bool RendererCore::takesKeys() const {
	return _takesKeys;
}

rpl::producer<> RendererCore::keysReleased() const {
	return _keysReleased.events();
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
	refreshClock();
}

void RendererCore::hostMessage(const twidget::Json &message) {
	const auto &type = message["type"].asString();
	if (_stopped) {
		return;
	} else if (type == "requestFrame") {
		_frameRequested = true;
		refreshClock();
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
		_frameRequested = false;
		_stopReason = QString::fromStdString(message["reason"].asString());
		_stopFatal = message["fatal"].asBool();
		releaseInput();
		changes |= Change::Stopped | Change::Repaint;
	} else if (type == "listen") {
		learnListen(message["types"]);
	} else if (type == "preventDefault") {
		learnPrevented(message["keys"]);
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

void RendererCore::learnListen(const twidget::Json &types) {
	auto keys = false;
	auto wheel = false;
	for (const auto &type : types.arr()) {
		const auto &name = type.asString();
		keys = keys || (name == "keydown") || (name == "keyup");
		wheel = wheel || (name == "wheel");
	}
	_listensWheel = wheel;
	if (std::exchange(_takesKeys, keys) && !keys) {
		_keysReleased.fire({});
	}
}

void RendererCore::learnPrevented(const twidget::Json &keys) {
	_wheelPrevented.clear();
	for (const auto &key : keys.arr()) {
		if (const auto target = PreventedWheelTarget(key.asString())) {
			_wheelPrevented.emplace(target);
		}
	}
}

int64 RendererCore::elementAt(QPointF point) {
	auto out = _renderer->receive(ElementAtQuery(point));
	const auto result = TakeMessage(out, "result")["result"].asNumber();
	notifyPosted(forwardAll(out));
	return int64(result);
}

bool RendererCore::trackHover() {
	const auto parsed = ParsedDocuments(*_renderer);
	if (_pointer.parsed != parsed) {
		_pointer.parsed = parsed;
		_pointer.hover = 0;
		_pointer.active = false;
	}
	if (_pointer.hover == _pointer.hit) {
		return false;
	}
	_pointer.hover = _pointer.hit;
	_pointer.active = false;
	return true;
}

std::vector<twidget::Json> RendererCore::leaveHover() {
	_cursor = nullptr;
	_pointer.hit = _pointer.hover = 0;
	_pointer.hovering = false;
	return _renderer->hover(kHoverLeave, 0., 0., 0);
}

void RendererCore::inputDone(
		std::vector<twidget::Json> out,
		bool wasDirty,
		bool restyled) {
	if (restyled && _renderer->dirty()) {
		// WHY: hover() and touch() restyle :hover and :active without the
		// core's animate(), so a transition on them would snap; an empty anim
		// message runs it and the transition starts from the old value.
		Append(out, _renderer->receive(EmptyAnimMessage()));
	}
	auto changes = forwardAll(out);
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	notifyPosted(changes);
	refreshClock();
}

void RendererCore::resetInput() {
	if (!_detached
		&& !_stopped
		&& (_pointer.pressed || _pointer.hovering)) {
		const auto wasDirty = _renderer->dirty();
		auto out = _pointer.pressed
			? Touch(*_renderer, kTouchCancel, _pointer.position, 0)
			: std::vector<twidget::Json>();
		if (_pointer.hovering) {
			Append(out, leaveHover());
		}
		_pointer = Pointer();
		inputDone(std::move(out), wasDirty, false);
	}
	releaseInput();
}

void RendererCore::releaseInput() {
	_cursor = nullptr;
	_pointer = Pointer();
	UnhoverWheel(this);
	_keysReleased.fire({});
}

bool RendererCore::canTick() const {
	return _onScreen && _ready && !_detached && !_stopped;
}

bool RendererCore::clockWanted() const {
	return canTick()
		&& (_stepBusy || _frameRequested || _renderer->wants_tick());
}

void RendererCore::refreshClock() {
	if (clockWanted()) {
		_wake.cancel();
		if (!_clock.animating()) {
			_clock.start();
		}
	} else {
		_clock.stop();
		scheduleWake();
	}
}

void RendererCore::scheduleWake() {
	const auto seconds = canTick()
		? _renderer->next_tick_in()
		: std::numeric_limits<double>::infinity();
	if (std::isfinite(seconds)) {
		_wake.callOnce(WakeDelay(seconds));
	} else {
		_wake.cancel();
	}
}

bool RendererCore::step() {
	_stepBusy = canTick()
		&& (_frameRequested || _renderer->wants_tick());
	if (!_stepBusy) {
		scheduleWake();
		return false;
	}
	tick();
	return true;
}

void RendererCore::wake() {
	if (canTick()) {
		tick();
	}
	refreshClock();
}

void RendererCore::tick() {
	const auto wasDirty = _renderer->dirty();
	auto changes = forwardAll(_renderer->tick());
	if (base::take(_frameRequested)) {
		forward(FrameMessage());
	}
	if (!wasDirty && _renderer->dirty()) {
		changes |= Change::Repaint;
	}
	notifyPosted(changes);
}

void RendererCore::finish(
		const std::vector<twidget::Json> &out,
		Changes changes) {
	changes |= forwardAll(out);
	refreshClock();
	if (changes) {
		_updates.fire_copy(changes);
	}
}

RendererCore::Changes RendererCore::forwardAll(
		const std::vector<twidget::Json> &out) {
	auto changes = Changes();
	for (const auto &message : out) {
		if (message["type"].asString() == "size") {
			changes |= applySize(message);
		}
		forward(message);
	}
	return changes;
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

void RendererCore::notifyPosted(Changes changes) {
	if (!changes) {
		return;
	}
	const auto queued = bool(_postedChanges);
	_postedChanges |= changes;
	if (!queued) {
		crl::on_main(this, [=] {
			_updates.fire(base::take(_postedChanges));
		});
	}
}

} // namespace Tgw
