/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_renderer.h"

#include "base/platform/base_platform_info.h"
#include "tgw/tgw_render_thread.h"
#include "tgw/tgw_renderer_work.h"
#include "tgw/tgw_touch.h"
#include "twidget/json.h"
#include "twidget/sdk_policy.h"
#include "twidget/widget_host.h"
#include "ui/style/style_core_scale.h"

#include <mutex>

#include "styles/style_basic.h"

namespace Tgw {

class HostLink final : public std::enable_shared_from_this<HostLink> {
public:
	HostLink(not_null<RendererCore*> core, uint64 work);

	[[nodiscard]] bool attach(not_null<twidget::Renderer*> proxy);
	void detach(not_null<twidget::Renderer*> proxy);
	void close();
	void forget();
	[[nodiscard]] bool gone() const;
	[[nodiscard]] uint64 workId() const;
	void bindHost(std::weak_ptr<twidget::WidgetHost> host);
	void toHost(const std::string &json);
	void diag(const std::string &kind, const std::string &detail);
	[[nodiscard]] std::optional<bool> hostBottom() const;
	void work(Fn<void(RendererWork&)> task, bool host);

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
	const uint64 _work;
	mutable std::mutex _mutex;
	twidget::Renderer *_proxy = nullptr;
	std::weak_ptr<twidget::WidgetHost> _host;
	std::atomic<bool> _closed = false;
	std::atomic<bool> _gone = false;

};

namespace {

constexpr auto kFallbackWidth = 1.;
constexpr auto kDomAlt = 1;
constexpr auto kDomCtrl = 2;
constexpr auto kDomMeta = 4;
constexpr auto kDomShift = 8;

auto WorkSerial = uint64(0);

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

[[nodiscard]] RendererWork::Host HostOf(std::shared_ptr<HostLink> link) {
	return {
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

HostLink::HostLink(not_null<RendererCore*> core, uint64 work)
: _core(core)
, _work(work) {
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

void HostLink::bindHost(std::weak_ptr<twidget::WidgetHost> host) {
	auto lock = std::lock_guard(_mutex);
	if (!_closed) {
		_host = std::move(host);
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
			task(*work);
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
		const auto reason = QString::fromStdString(
			message["reason"].asString());
		const auto fatal = message["fatal"].asBool();
		crl::on_main([link = shared_from_this()] {
			if (const auto core = link->current()) {
				core->hostFrozen();
			}
		});
		work([=](RendererWork &work) {
			work.freeze(reason, fatal);
		}, true);
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
: _link(std::make_shared<HostLink>(this, ++WorkSerial))
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
		values = std::move(values)
	] {
		if (!link->gone()) {
			RendererWork::Create(
				link->workId(),
				chat,
				width,
				dpr,
				values,
				HostOf(link));
		}
	});
}

RendererCore::~RendererCore() {
	_link->close();
	_link->forget();
	PostToRenderThread([id = _link->workId()] {
		RendererWork::Destroy(id);
	});
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

void RendererCore::bindHost(std::weak_ptr<twidget::WidgetHost> host) {
	_link->bindHost(std::move(host));
}

void RendererCore::detach() {
	_detached = true;
	endInput();
	forgetSelection();
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
	if (base::take(_refresh) || !held) {
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
