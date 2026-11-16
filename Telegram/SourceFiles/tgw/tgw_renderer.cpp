/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer.h"

#include "base/platform/base_platform_info.h"
#include "tgw/tgw_renderer_work.h"
#include "tgw/tgw_touch.h"
#include "twidget/json.h"
#include "twidget/sdk_policy.h"
#include "twidget/widget_host.h"
#include "ui/style/style_core_scale.h"

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

constexpr auto kFallbackWidth = 1.;
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

[[nodiscard]] bool ValidWidth(double width) {
	return std::isfinite(width) && (width > 0.);
}

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

int LogicalHeight(double css, int scale) {
	return int(std::ceil(css * scale / 100.));
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
, _size{ .width = ValidWidth(width) ? width : kFallbackWidth }
, _menuDeadline([=] { menuDeadline(); })
, _claimDeadline([=] { claimDeadline(); })
, _work(std::make_unique<RendererWork>(
	this,
	_size.width,
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
	})) {
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
	_detached = true;
	endInput();
	forgetSelection();
	_work->detach();
	_link->close();
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
		_work->setOnScreen(onScreen, ++_visibility);
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
		_work->pointerEnter(++_pointerSerial);
	}
}

void RendererCore::pointerMove(QPointF point, int modifiers) {
	if (acceptsInput() && !TouchBridge::Active()) {
		_work->pointerMove(point, modifiers, ++_pointerSerial);
	}
}

void RendererCore::pointerLeave() {
	_input.hand = false;
	_work->pointerLeave(++_pointerSerial);
}

void RendererCore::pointerPress(QPointF point, int modifiers) {
	if (!acceptsInput() || TouchBridge::Active()) {
		return;
	}
	endMenu();
	_input.pressed = true;
	_input.menuPressed = false;
	forgetSelection();
	_work->pointerPress(point, modifiers, ++_pointerSerial, _selectionSerial);
}

void RendererCore::pointerRelease(QPointF point, int modifiers) {
	_input.pressed = _input.menuPressed = false;
	_work->pointerRelease(
		point,
		modifiers,
		TouchBridge::Active(),
		++_pointerSerial);
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
	_work->menuPress(point, modifiers, serial);
	return true;
}

void RendererCore::menuRelease(int modifiers) {
	endMenu();
	_input.menuPressed = false;
	_work->menuRelease(modifiers, TouchBridge::Active());
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
	_work->fingerPress(point, modifiers, serial);
	return true;
}

void RendererCore::fingerMove(QPointF point, int modifiers) {
	if (_input.fingerPressed) {
		_work->fingerMove(point, modifiers);
	}
}

void RendererCore::fingerHold() {
	if (_input.fingerPressed) {
		_work->fingerHold();
	}
}

void RendererCore::fingerRelease(QPointF point, int modifiers) {
	endClaim();
	_input.fingerPressed = false;
	_work->fingerRelease(point, modifiers);
}

void RendererCore::fingerCancel() {
	endClaim();
	_input.fingerPressed = false;
	_work->fingerCancel();
}

void RendererCore::resetInput() {
	endInput();
	_work->resetInput();
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
	_work->clearSelection(_selectionSerial);
}

void RendererCore::setSize(RendererSize size) {
	if (!ValidWidth(size.width)) {
		size.width = _size.width;
	}
	if (_sizeSerial && (_size == size)) {
		return;
	}
	_size = size;
	_work->setSize(_size, ++_sizeSerial);
}

void RendererCore::setCut(const RendererCut &cut) {
	_work->setCut(cut);
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
	if (base::take(_refresh) || !held) {
		_work->demand();
	}
	if (!held) {
		return QImage();
	} else if (base::take(_undrawn)) {
		_work->drawn();
	}
	return _frame;
}

int64 RendererCore::frameBytes() const {
	return _frame.sizeInBytes();
}

void RendererCore::dropFrame() {
	_frame = QImage();
	_frameArea = QSize();
	_refresh = false;
	_work->dropFrame(++_epoch);
}

void RendererCore::refreshFrame() {
	_refresh = true;
	_work->refreshFrame();
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
	if (_frozen) {
		return;
	} else if (message["type"].asString() == "frozen") {
		// WHY: HtmlWidget::drawStopped() paints the themed stop pill; the
		// core's own freeze would rebuild its badge and fatal dimming into
		// the last frame, so the core never sees this message.
		_frozen = true;
		_work->freeze(
			QString::fromStdString(message["reason"].asString()),
			message["fatal"].asBool());
		endInput();
		forgetSelection();
		return;
	}
	_work->hostMessage(message);
}

void RendererCore::hostTheme(bool dark) {
	_work->hostTheme(dark);
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

void RendererCore::apply(RendererAnswer &&answer) {
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
	}
	if (!_detached && !refusesHost()) {
		changes |= applyInput(answer);
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
	_frameArea = layout.area;
	_frameSerial = layout.serial;
	_undrawn = true;
	return result;
}

RendererCore::Changes RendererCore::applyInput(RendererAnswer &answer) {
	auto result = Changes();
	if (const auto &cursor = answer.cursor) {
		const auto current = (cursor->serial == _pointerSerial);
		if ((current || !cursor->hand) && (_input.hand != cursor->hand)) {
			_input.hand = cursor->hand;
			result |= Change::Cursor;
		}
	}
	if (auto &selection = answer.selection) {
		const auto current = (selection->serial == _selectionSerial);
		if (current
			&& ((_hasSelection != selection->has)
				|| (_selectedText != selection->text))) {
			_hasSelection = selection->has;
			_selectedText = std::move(selection->text);
			result |= Change::Selection;
		}
	}
	return result;
}

Fn<void()> RendererCore::takeMenu(const RendererMenu &menu) {
	if (!_menu || (_menu->serial != menu.serial)) {
		return nullptr;
	}
	auto decided = std::move(base::take(_menu)->decided);
	_menuDeadline.cancel();
	_work->menuDecided(menu.serial, menu.taken);
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
	_work->menuDecided(serial, false);
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
	_work->menuDecided(pending.serial, false);
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
