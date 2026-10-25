/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_controller_ready.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "test/test_log.h"
#include "test/test_session_gate.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Test {
namespace {

// The self-test's synchronous purity polls, and the refused polls its
// primary-window ActOnController stage sees before its target appears.
constexpr auto kObservationPolls = 20;
constexpr auto kDelayedPolls = 3;

// One read of the registry: an entry per registered window, in
// enumeration order, and the size of the activation history.
struct ControllerRegistry {
	std::vector<ControllerEntry> entries;
	int activationSize = 0;
};

// Identity by liveness: a destroyed or never-bound handle matches nothing,
// and two live objects never share an address.
[[nodiscard]] bool Same(
		const ControllerHandle &a,
		const ControllerHandle &b) {
	return a.get() && (a.get() == b.get());
}

[[nodiscard]] QString SeparateTypeName(Window::SeparateType type) {
	using Type = Window::SeparateType;
	switch (type) {
	case Type::Primary: return u"Primary"_q;
	case Type::Archive: return u"Archive"_q;
	case Type::Chat: return u"Chat"_q;
	case Type::Forum: return u"Forum"_q;
	case Type::Community: return u"Community"_q;
	case Type::SavedSublist: return u"SavedSublist"_q;
	case Type::SharedMedia: return u"SharedMedia"_q;
	}
	Unexpected("Type in Test::SeparateTypeName.");
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString IndexText(int index) {
	return (index >= 0) ? QString::number(index) : u"none"_q;
}

[[nodiscard]] QString ActivationText(int activation) {
	return (activation >= 0) ? QString::number(activation) : u"absent"_q;
}

[[nodiscard]] QString MatchText(
		const ControllerHandle &handle,
		const ControllerHandle &requested) {
	return Same(handle, requested)
		? u"same"_q
		: handle.empty()
		? u"none"_q
		: u"other"_q;
}

[[nodiscard]] QString RequestText(const ControllerRequest &request) {
	const auto window = !request.exactWindow
		? u"account-primary"_q
		: request.window.empty()
		? u"exact-gone"_q
		: u"exact-live"_q;
	return u"window=%1 session=%2"_q.arg(
		window,
		request.session.empty() ? u"none"_q : u"live"_q);
}

[[nodiscard]] QString EntryText(
		const ControllerEntry &entry,
		const ControllerHandle &session) {
	if (entry.window.null()) {
		return u"none"_q;
	}
	const auto controller = entry.controller.empty()
		? u"none"_q
		: u"present"_q;
	const auto fields = QStringList{
		u"index=%1"_q.arg(IndexText(entry.accountIndex)),
		u"type=%1"_q.arg(SeparateTypeName(entry.type)),
		u"test=%1"_q.arg(FlagText(entry.test)),
		u"session=%1"_q.arg(MatchText(entry.session, session)),
		u"controller=%1"_q.arg(controller),
		u"shown=%1"_q.arg(MatchText(entry.shownSession, session)),
		u"visible=%1"_q.arg(FlagText(entry.visible)),
		u"minimized=%1"_q.arg(FlagText(entry.minimized)),
		u"sized=%1"_q.arg(FlagText(entry.sized)),
		u"locked=%1"_q.arg(FlagText(entry.locked)),
		u"activation=%1"_q.arg(ActivationText(entry.activation)),
		u"activePrimary=%1"_q.arg(FlagText(entry.activePrimary)),
	};
	return fields.join(u" "_q);
}

[[nodiscard]] int StorageIndex(const Main::Account *account) {
	const auto &domain = Core::App().domain();
	if (!domain.started()) {
		return -1;
	}
	for (const auto &[index, owned] : domain.accounts()) {
		if (owned.get() == account) {
			return index;
		}
	}
	return -1;
}

[[nodiscard]] ControllerEntry ReadEntry(
		not_null<Window::Controller*> window,
		const std::vector<not_null<Window::Controller*>> &stack) {
	const auto id = window->id();
	const auto account = id.account; // account() asserts on startup window.
	const auto session = account ? account->maybeSession() : nullptr;
	const auto controller = window->sessionController();
	const auto widget = window->widget();
	const auto i = ranges::find(stack, window);
	return {
		.window = window.get(),
		.account = account,
		.session = session,
		.controller = controller,
		.shownSession = controller ? &controller->session() : nullptr,
		.target = controller,
		.type = id.type,
		.accountIndex = StorageIndex(account),
		.activation = (i == end(stack)) ? -1 : (int(end(stack) - i) - 1),
		.test = session && session->isTestMode(),
		.visible = widget->isVisible(),
		.minimized = widget->isMinimized(),
		.sized = !widget->size().isEmpty(),
		.locked = window->locked(),
		.activePrimary = (Core::App().activePrimaryWindow() == window.get()),
	};
}

[[nodiscard]] ControllerRegistry ReadControllerRegistry() {
	const auto &stack = Core::App().windowStack();
	auto result = ControllerRegistry{ .activationSize = int(stack.size()) };
	Core::App().enumerateWindows([&](not_null<Window::Controller*> window) {
		result.entries.push_back(ReadEntry(window, stack));
	});
	return result;
}

// The one decision, over a real or a caller-controlled registry, in the
// order documented at ControllerRefusal. Activation facts are copied with
// the entry and never tested.
[[nodiscard]] ControllerReading DecideController(
		const ControllerRequest &request,
		const ControllerRegistry &registry) {
	auto result = ControllerReading();
	result.request = request;
	result.activationSize = registry.activationSize;
	for (const auto &entry : registry.entries) {
		++result.registered;
		if (Same(entry.account, request.account)) {
			++result.accountWindows;
		}
	}
	const auto refuse = [&](ControllerRefusal why) {
		result.refusal = why;
		return result;
	};
	if (request.session.empty()) {
		return refuse(ControllerRefusal::NoSession);
	}
	if (request.exactWindow) {
		if (request.window.empty()) {
			return refuse(ControllerRefusal::DestroyedWindow);
		}
		const auto i = ranges::find_if(registry.entries, [&](
				const ControllerEntry &entry) {
			return Same(entry.window, request.window);
		});
		if (i == end(registry.entries)) {
			return refuse(ControllerRefusal::WrongWindow);
		}
		result.entry = *i;
		if (!Same(i->account, request.account)) {
			return refuse(ControllerRefusal::WrongAccount);
		}
	} else {
		const auto i = ranges::find_if(registry.entries, [&](
				const ControllerEntry &entry) {
			return (entry.type == Window::SeparateType::Primary)
				&& Same(entry.account, request.account);
		});
		if (i == end(registry.entries)) {
			return refuse(ControllerRefusal::NoRegisteredWindow);
		}
		result.entry = *i;
	}
	const auto &entry = result.entry;
	if (!Same(entry.session, request.session)
		|| (!entry.controller.empty()
			&& !Same(entry.shownSession, request.session))) {
		return refuse(ControllerRefusal::WrongSession);
	} else if (!entry.test) {
		return refuse(ControllerRefusal::NonTest);
	} else if (entry.controller.empty() || !entry.sized) {
		return refuse(ControllerRefusal::Unready);
	} else if (entry.locked) {
		return refuse(ControllerRefusal::Locked);
	} else if (!entry.visible || entry.minimized) {
		return refuse(ControllerRefusal::Hidden);
	}
	return refuse(ControllerRefusal::None);
}

// A refused retained reading never turns valid: callers take fresh
// readings. An accepted one stays accepted only for the same live window,
// controller and session; the session is tested before the controller, so
// a logout reads replaced-session rather than replaced-window.
[[nodiscard]] ControllerReading RecheckController(
		const ControllerReading &retained,
		const ControllerRegistry &fresh) {
	if (!retained.accepted()) {
		return retained;
	}
	const auto stale = [&](ControllerRefusal why) {
		auto result = retained;
		result.refusal = why;
		result.dispatched = false;
		return result;
	};
	if (retained.entry.window.empty()) {
		return stale(ControllerRefusal::DestroyedWindow);
	} else if (retained.entry.session.empty()) {
		return stale(ControllerRefusal::ReplacedSession);
	} else if (retained.entry.controller.empty()) {
		return stale(ControllerRefusal::ReplacedWindow);
	}
	auto result = DecideController(retained.request, fresh);
	if (result.accepted()
		&& (!Same(result.entry.window, retained.entry.window)
			|| !Same(result.entry.controller, retained.entry.controller))) {
		result.refusal = ControllerRefusal::ReplacedWindow;
	}
	return result;
}

// The only place an action is invoked: once, and only for a reading that
// survives the identity re-check against a fresh registry read.
[[nodiscard]] ControllerReading DispatchController(
		const ControllerReading &retained,
		const Fn<void(not_null<Window::SessionController*>)> &action) {
	auto result = RecheckController(retained, ReadControllerRegistry());
	if (const auto controller = result.controller()) {
		action(controller);
		result.dispatched = true;
	}
	return result;
}

// A caller-controlled identity: a fixture account, session, window or
// controller that goes through the same decision as a real object.
struct FixtureIdentity final : base::has_weak_ptr {
};

// Which windows the registry and activation history hold, in their own
// order, and which primary window and account are active: everything a
// pure poll must leave as it was.
struct SelectionSnapshot {
	std::vector<ControllerHandle> registry;
	std::vector<ControllerHandle> activation;
	ControllerHandle activePrimary;
	ControllerHandle activeAccount;
};

// The self-test's state. Everything live is held weakly, as
// test_chat_menu.cpp's SelfTestState does; only the fixture identities are
// owned here.
struct SelfTest {
	std::unique_ptr<FixtureIdentity> accountA;
	std::unique_ptr<FixtureIdentity> accountB;
	std::unique_ptr<FixtureIdentity> accountC;
	std::unique_ptr<FixtureIdentity> sessionA;
	std::unique_ptr<FixtureIdentity> sessionB;
	std::unique_ptr<FixtureIdentity> sessionC;
	std::unique_ptr<FixtureIdentity> windowA;
	std::unique_ptr<FixtureIdentity> windowB;
	std::unique_ptr<FixtureIdentity> windowX;
	std::unique_ptr<FixtureIdentity> controllerA;
	std::unique_ptr<FixtureIdentity> controllerB;
	QString gate;
	base::weak_ptr<Main::Session> session;
	base::weak_ptr<Window::Controller> primary;
	base::weak_ptr<Window::Controller> second;
	base::weak_ptr<Window::SessionController> primaryController;
	base::weak_ptr<Window::SessionController> primaryActed;
	base::weak_ptr<Window::SessionController> secondActed;
	ControllerReading primaryReading;
	ControllerReading secondReading;
	SelectionSnapshot before;
	int primaryPolls = 0;
	int pollsAtAction = 0;
	int primaryActions = 0;
	int secondActions = 0;
	int refusedActions = 0;
	int controlActions = 0;
	bool secondPreexisted = false;
};

// One live target the self-test expects refused, by name.
struct RefusedTarget {
	QString name;
	ControllerRequest request;
	ControllerRefusal expected = ControllerRefusal::NotEvaluated;
};

[[nodiscard]] SelectionSnapshot ReadSelection() {
	auto result = SelectionSnapshot();
	Core::App().enumerateWindows([&](not_null<Window::Controller*> window) {
		result.registry.push_back(window.get());
	});
	for (const auto window : Core::App().windowStack()) {
		result.activation.push_back(window.get());
	}
	result.activePrimary = Core::App().activePrimaryWindow();
	const auto &domain = Core::App().domain();
	if (domain.started()) {
		result.activeAccount = &domain.active();
	}
	return result;
}

[[nodiscard]] bool SameHandles(
		const std::vector<ControllerHandle> &a,
		const std::vector<ControllerHandle> &b) {
	return ranges::equal(a, b, Same);
}

[[nodiscard]] bool SameRegistry(
		const SelectionSnapshot &a,
		const SelectionSnapshot &b) {
	return SameHandles(a.registry, b.registry);
}

[[nodiscard]] bool SameSelection(
		const SelectionSnapshot &a,
		const SelectionSnapshot &b) {
	return SameRegistry(a, b)
		&& SameHandles(a.activation, b.activation)
		&& Same(a.activePrimary, b.activePrimary)
		&& Same(a.activeAccount, b.activeAccount);
}

[[nodiscard]] QString SelectionText(const SelectionSnapshot &snapshot) {
	return u"registered=%1 activationSize=%2 activePrimary=%3 "
		u"activeAccount=%4"_q.arg(
			QString::number(int(snapshot.registry.size())),
			QString::number(int(snapshot.activation.size())),
			FlagText(!snapshot.activePrimary.empty()),
			FlagText(!snapshot.activeAccount.empty()));
}

[[nodiscard]] QString SelfText(const QString &what) {
	return u"controller ready self-test: "_q + what;
}

void CheckReading(
		bool ok,
		const QString &what,
		const ControllerReading &reading) {
	Check(ok, SelfText(what), ControllerReadingText(reading));
}

// The same verdict for the same live window and controller.
[[nodiscard]] bool SameAnswer(
		const ControllerReading &a,
		const ControllerReading &b) {
	return (a.refusal == b.refusal)
		&& Same(a.entry.window, b.entry.window)
		&& Same(a.entry.controller, b.entry.controller);
}

[[nodiscard]] ControllerRequest AccountRequest(
		const ControllerHandle &account,
		const ControllerHandle &session) {
	return { .account = account, .session = session };
}

[[nodiscard]] ControllerRequest WindowRequest(
		const ControllerHandle &account,
		const ControllerHandle &session,
		const ControllerHandle &window) {
	return {
		.account = account,
		.session = session,
		.window = window,
		.exactWindow = true,
	};
}

// A Primary, Test, visible, sized and unlocked window showing |session|,
// with no typed target and no storage index.
[[nodiscard]] ControllerEntry FixtureEntry(
		const FixtureIdentity &window,
		const FixtureIdentity &account,
		const FixtureIdentity &session,
		const FixtureIdentity &controller,
		int activation) {
	return {
		.window = &window,
		.account = &account,
		.session = &session,
		.controller = &controller,
		.shownSession = &session,
		.type = Window::SeparateType::Primary,
		.activation = activation,
		.test = true,
		.visible = true,
		.sized = true,
	};
}

// Two Test controllers of distinct accounts; activation history holds
// only A's window.
[[nodiscard]] ControllerRegistry TwoFixtures(const SelfTest &state) {
	auto result = ControllerRegistry{ .activationSize = 1 };
	result.entries.push_back(FixtureEntry(
		*state.windowA,
		*state.accountA,
		*state.sessionA,
		*state.controllerA,
		0));
	result.entries.push_back(FixtureEntry(
		*state.windowB,
		*state.accountB,
		*state.sessionB,
		*state.controllerB,
		-1));
	return result;
}

// |registry| with a caller-controlled activation history holding only
// |only|, or nothing.
[[nodiscard]] ControllerRegistry WithActivation(
		ControllerRegistry registry,
		const ControllerHandle &only) {
	registry.activationSize = 0;
	for (auto &entry : registry.entries) {
		entry.activation = Same(entry.window, only) ? 0 : -1;
		if (entry.activation == 0) {
			++registry.activationSize;
		}
	}
	return registry;
}

void MakeFixtures(SelfTest &state) {
	const auto identities = {
		&state.accountA,
		&state.accountB,
		&state.accountC,
		&state.sessionA,
		&state.sessionB,
		&state.sessionC,
		&state.windowA,
		&state.windowB,
		&state.windowX,
		&state.controllerA,
		&state.controllerB,
	};
	for (const auto identity : identities) {
		*identity = std::make_unique<FixtureIdentity>();
	}
}

// Destroys |identity| before allocating its replacement, so the allocator
// may hand the replacement the same address. Whether it did is returned as
// a diagnostic, never an oracle.
[[nodiscard]] bool ReplaceIdentity(std::unique_ptr<FixtureIdentity> &identity) {
	const auto old = reinterpret_cast<quintptr>(identity.get());
	identity = nullptr;
	identity = std::make_unique<FixtureIdentity>();
	return (reinterpret_cast<quintptr>(identity.get()) == old);
}

[[nodiscard]] Window::SessionController *ControllerOf(
		const base::weak_ptr<Window::Controller> &window) {
	const auto strong = window.get();
	return strong ? strong->sessionController() : nullptr;
}

// The live session requested in an account no registered window names.
[[nodiscard]] ControllerRequest AbsentRequest(const SelfTest &state) {
	return AccountRequest(state.accountC.get(), state.session.get());
}

// A caller-controlled readiness delay that changes no product state: the
// first kDelayedPolls polls request the absent account, later ones the
// session's primary window.
[[nodiscard]] ControllerRequest DelayedPrimaryRequest(SelfTest &state) {
	return (++state.primaryPolls <= kDelayedPolls)
		? AbsentRequest(state)
		: ExactController(state.session.get());
}

// The reading's window and controller show exactly |session|, and the
// session itself lists that controller: an oracle independent of the
// registry walk.
[[nodiscard]] bool ShowsSession(
		const ControllerReading &reading,
		Main::Session *session) {
	return session
		&& Same(reading.entry.account, &session->account())
		&& Same(reading.entry.session, session)
		&& Same(reading.entry.shownSession, session)
		&& ranges::any_of(session->windows(), [&](
				not_null<Window::SessionController*> window) {
			return Same(reading.entry.controller, window.get());
		});
}

[[nodiscard]] QString RankText(
		const ControllerRegistry &registry,
		const ControllerHandle &window) {
	for (const auto &entry : registry.entries) {
		if (Same(entry.window, window)) {
			return ActivationText(entry.activation);
		}
	}
	return u"unregistered"_q;
}

// The real activation ranks of both live windows, as the reader records
// them.
[[nodiscard]] QString RanksText(const SelfTest &state) {
	const auto registry = ReadControllerRegistry();
	return u"primaryActivation=%1 secondActivation=%2 "
		u"activationSize=%3"_q.arg(
			RankText(registry, state.primary),
			RankText(registry, state.second),
			QString::number(registry.activationSize));
}

[[nodiscard]] QString CountsText(const SelfTest &state) {
	return u"primaryPolls=%1 pollsAtAction=%2 delayedPolls=%3 "
		u"primaryActions=%4 secondActions=%5 refusedActions=%6 "
		u"controlActions=%7"_q.arg(
			QString::number(state.primaryPolls),
			QString::number(state.pollsAtAction),
			QString::number(kDelayedPolls),
			QString::number(state.primaryActions),
			QString::number(state.secondActions),
			QString::number(state.refusedActions),
			QString::number(state.controlActions));
}

[[nodiscard]] bool SecondReady(const SelfTest &state) {
	const auto second = state.second.get();
	return second
		&& second->sessionController()
		&& second->widget()->isVisible();
}

[[nodiscard]] QString SecondText(const SelfTest &state) {
	const auto second = state.second.get();
	return u"preexisted=%1 second=%2 controller=%3 visible=%4"_q.arg(
		FlagText(state.secondPreexisted),
		second ? u"live"_q : u"none"_q,
		FlagText(second && second->sessionController()),
		FlagText(second && second->widget()->isVisible()));
}

// A refusal beside its accepted counterpart: |control| differs from
// |refused| in exactly the one request or entry field the refusal names.
void CheckPair(
		const QString &what,
		const ControllerReading &control,
		const ControllerReading &refused,
		ControllerRefusal expected) {
	Check(
		control.accepted() && (refused.refusal == expected),
		SelfText(u"fixture: %1 refuses as %2 beside an accepted "
			u"control"_q.arg(what, ControllerRefusalName(expected))),
		u"control=[%1] refused=[%2]"_q.arg(
			ControllerReadingText(control),
			ControllerReadingText(refused)));
}

// Activation facts are copied, never tested: each accepted reading's own
// request, decided over |registry| with the activation history holding
// only |only| (or nothing), gives the same answer.
void CheckSameUnderHistory(
		const QString &what,
		const ControllerRegistry &registry,
		const std::vector<ControllerReading> &accepted,
		const ControllerHandle &only) {
	const auto history = WithActivation(registry, only);
	auto ok = !accepted.empty();
	auto details = QStringList();
	for (const auto &reading : accepted) {
		const auto again = DecideController(reading.request, history);
		ok = ok && reading.accepted() && SameAnswer(reading, again);
		details.push_back(u"[%1]"_q.arg(ControllerReadingText(again)));
	}
	Check(
		ok,
		SelfText(what
			+ u", every reading stays accepted with the same window and "
			u"controller"_q),
		details.join(u" "_q));
}

// Closes only a window the self-test opened. A live Window::Controller is
// always owned by the registry, so liveness is the registration check.
// Used by the close stage and by Runner::onFinish; safe to repeat.
void CloseOpened(const SelfTest &state) {
	if (state.secondPreexisted) {
		return;
	}
	if (const auto window = state.second.get()) {
		Core::App().closeWindow(window);
	}
}

// What SessionController::showInNewWindow does for the product's
// "Archive -> New window" (Window::Filler::addNewWindow): a second
// registered window of the same Test session, which activation history
// may never record.
void OpenSecond(SelfTest &state) {
	const auto session = state.session.get();
	if (!session) {
		return;
	}
	const auto id = Window::SeparateId(
		Window::SeparateType::Archive,
		session);
	state.secondPreexisted = (Core::App().separateWindowFor(id) != nullptr);
	state.second = Core::App().ensureSeparateWindowFor(id);
}

void SelfFixturePositives(SelfTest &state) {
	MakeFixtures(state);
	const auto registry = TwoFixtures(state);
	const auto a = DecideController(
		AccountRequest(state.accountA.get(), state.sessionA.get()),
		registry);
	const auto b = DecideController(
		AccountRequest(state.accountB.get(), state.sessionB.get()),
		registry);
	CheckReading(
		a.accepted()
			&& Same(a.entry.window, state.windowA.get())
			&& Same(a.entry.controller, state.controllerA.get())
			&& (a.entry.activation == 0),
		u"fixture: account A, in activation history, is accepted with its "
		u"own window and controller"_q,
		a);
	CheckReading(
		b.accepted()
			&& Same(b.entry.window, state.windowB.get())
			&& Same(b.entry.controller, state.controllerB.get())
			&& (b.entry.activation == -1),
		u"fixture: account B, absent from activation history, is accepted "
		u"with its own window and controller"_q,
		b);
	CheckSameUnderHistory(
		u"fixture: with an empty activation history"_q,
		registry,
		{ a, b },
		{});
	CheckSameUnderHistory(
		u"fixture: with activation history holding only B"_q,
		registry,
		{ a, b },
		state.windowB.get());
	const auto c = DecideController(
		AccountRequest(state.accountC.get(), state.sessionC.get()),
		registry);
	CheckReading(
		(c.refusal == ControllerRefusal::NoRegisteredWindow)
			&& (c.accountWindows == 0)
			&& (c.registered == 2),
		u"fixture: account C without a registered window is the absent "
		u"controller, refused as no-registered-window, unlike B's accepted "
		u"activation=absent"_q,
		c);
}

void SelfFixtureRefusals(const SelfTest &state) {
	using Refusal = ControllerRefusal;
	const auto registry = TwoFixtures(state);
	const auto requestA = AccountRequest(
		state.accountA.get(),
		state.sessionA.get());
	const auto exactA = WindowRequest(
		state.accountA.get(),
		state.sessionA.get(),
		state.windowA.get());
	const auto control = DecideController(requestA, registry);
	const auto exactControl = DecideController(exactA, registry);
	const auto withWindow = [&](const ControllerHandle &window) {
		auto request = exactA;
		request.window = window;
		return DecideController(request, registry);
	};
	const auto withSession = [&](const ControllerHandle &session) {
		auto request = requestA;
		request.session = session;
		return DecideController(request, registry);
	};
	const auto withEntryA = [&](Fn<void(ControllerEntry&)> change) {
		auto changed = registry;
		change(changed.entries.front());
		return DecideController(requestA, changed);
	};
	auto temporary = std::make_unique<FixtureIdentity>();
	const auto gone = ControllerHandle(temporary.get());
	temporary = nullptr;

	CheckPair(
		u"account B's exact window requested for account A"_q,
		exactControl,
		withWindow(state.windowB.get()),
		Refusal::WrongAccount);
	CheckPair(
		u"account A's window requested with session B"_q,
		control,
		withSession(state.sessionB.get()),
		Refusal::WrongSession);
	CheckPair(
		u"account A's window whose controller shows session B"_q,
		control,
		withEntryA([&](ControllerEntry &entry) {
			entry.shownSession = state.sessionB.get();
		}),
		Refusal::WrongSession);
	CheckPair(
		u"an alive but unregistered exact window"_q,
		exactControl,
		withWindow(state.windowX.get()),
		Refusal::WrongWindow);
	CheckPair(
		u"a destroyed exact window"_q,
		exactControl,
		withWindow(gone),
		Refusal::DestroyedWindow);
	CheckPair(
		u"a destroyed session"_q,
		control,
		withSession(gone),
		Refusal::NoSession);
	CheckPair(
		u"a session off the Test environment"_q,
		control,
		withEntryA([](ControllerEntry &entry) { entry.test = false; }),
		Refusal::NonTest);
	CheckPair(
		u"a window without a SessionController"_q,
		control,
		withEntryA([](ControllerEntry &entry) {
			entry.controller = ControllerHandle();
		}),
		Refusal::Unready);
	CheckPair(
		u"a window with an empty widget size"_q,
		control,
		withEntryA([](ControllerEntry &entry) { entry.sized = false; }),
		Refusal::Unready);
	CheckPair(
		u"a locked window"_q,
		control,
		withEntryA([](ControllerEntry &entry) { entry.locked = true; }),
		Refusal::Locked);
	CheckPair(
		u"a window whose widget is not visible"_q,
		control,
		withEntryA([](ControllerEntry &entry) { entry.visible = false; }),
		Refusal::Hidden);
	CheckPair(
		u"a minimized window"_q,
		control,
		withEntryA([](ControllerEntry &entry) { entry.minimized = true; }),
		Refusal::Hidden);
}

void SelfFixtureGenerations(SelfTest &state) {
	using Refusal = ControllerRefusal;
	const auto requestA = [&] {
		return AccountRequest(state.accountA.get(), state.sessionA.get());
	};
	const auto retained = DecideController(requestA(), TwoFixtures(state));
	const auto control = RecheckController(retained, TwoFixtures(state));
	CheckReading(
		control.accepted() && SameAnswer(retained, control),
		u"fixture: a retained reading re-checked over an unchanged registry "
		u"stays accepted (control)"_q,
		control);

	const auto controllerReused = ReplaceIdentity(state.controllerA);
	const auto controllerRegistry = TwoFixtures(state);
	const auto controllerStale = RecheckController(
		retained,
		controllerRegistry);
	const auto controllerFresh = DecideController(
		requestA(),
		controllerRegistry);
	CheckReading(
		controllerStale.refusal == Refusal::ReplacedWindow,
		u"fixture: a retained reading whose controller was replaced refuses "
		u"as replaced-window"_q,
		controllerStale);
	CheckReading(
		controllerFresh.accepted()
			&& Same(controllerFresh.entry.controller, state.controllerA.get()),
		u"fixture: a fresh decision accepts the replacement controller"_q,
		controllerFresh);

	const auto sessionReused = ReplaceIdentity(state.sessionA);
	const auto sessionRegistry = TwoFixtures(state);
	const auto sessionStale = RecheckController(
		controllerFresh,
		sessionRegistry);
	const auto sessionRetainedRequest = DecideController(
		controllerFresh.request,
		sessionRegistry);
	const auto sessionFresh = DecideController(requestA(), sessionRegistry);
	CheckReading(
		sessionStale.refusal == Refusal::ReplacedSession,
		u"fixture: a retained reading whose session was replaced refuses as "
		u"replaced-session"_q,
		sessionStale);
	CheckReading(
		sessionRetainedRequest.refusal == Refusal::NoSession,
		u"fixture: the retained request for the replaced session refuses as "
		u"no-session"_q,
		sessionRetainedRequest);
	CheckReading(
		sessionFresh.accepted()
			&& Same(sessionFresh.entry.session, state.sessionA.get()),
		u"fixture: a request for the replacement session is accepted"_q,
		sessionFresh);

	const auto windowReused = ReplaceIdentity(state.windowA);
	const auto windowRegistry = TwoFixtures(state);
	const auto windowStale = RecheckController(sessionFresh, windowRegistry);
	const auto windowFresh = DecideController(requestA(), windowRegistry);
	CheckReading(
		windowStale.refusal == Refusal::DestroyedWindow,
		u"fixture: a retained reading whose window was replaced refuses as "
		u"destroyed-window"_q,
		windowStale);
	CheckReading(
		windowFresh.accepted()
			&& Same(windowFresh.entry.window, state.windowA.get()),
		u"fixture: a fresh account-primary request accepts the replacement "
		u"window"_q,
		windowFresh);
	Note(SelfText(u"fixture: generations reusedAddress controller=%1 "
		u"session=%2 window=%3"_q.arg(
			FlagText(controllerReused),
			FlagText(sessionReused),
			FlagText(windowReused))));

	const auto requestC = AccountRequest(
		state.accountC.get(),
		state.sessionC.get());
	const auto refusedC = DecideController(requestC, TwoFixtures(state));
	const auto windowC = FixtureIdentity();
	const auto controllerC = FixtureIdentity();
	auto withC = TwoFixtures(state);
	withC.entries.push_back(FixtureEntry(
		windowC,
		*state.accountC,
		*state.sessionC,
		controllerC,
		-1));
	const auto recheckedC = RecheckController(refusedC, withC);
	const auto freshC = DecideController(requestC, withC);
	Check(
		(refusedC.refusal == Refusal::NoRegisteredWindow)
			&& (recheckedC.refusal == Refusal::NoRegisteredWindow)
			&& freshC.accepted(),
		SelfText(u"fixture: a refused retained reading stays "
			u"no-registered-window after its account gains a window, which a "
			u"fresh decision accepts"_q),
		u"rechecked=[%1] fresh=[%2]"_q.arg(
			ControllerReadingText(recheckedC),
			ControllerReadingText(freshC)));
}

// The fixture gate of AppendMainWindowNotMarkingReadSelfTest's shape: on a
// copy without an unlocked Test session in the primary window this is the
// run's one FAIL and every live stage reads N/A.
void SelfLivePrerequisite(SelfTest &state) {
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	const auto session = controller ? &controller->session() : nullptr;
	const auto gate = ReadSessionGate();
	const auto test = session && session->isTestMode();
	const auto locked = window && window->locked();
	const auto ok = window
		&& controller
		&& test
		&& !locked
		&& gate.primarySessionTest;
	Check(
		ok,
		u"fixture gate: the primary window shows an unlocked Test session"_q,
		u"window=%1 controller=%2 test=%3 locked=%4 "
		u"gatePrimarySessionTest=%5"_q.arg(
			FlagText(window != nullptr),
			FlagText(controller != nullptr),
			FlagText(test),
			FlagText(locked),
			FlagText(gate.primarySessionTest)));
	if (!ok) {
		state.gate = u"fixture gate: no unlocked Test session in the "
			u"primary window"_q;
		return;
	}
	state.session = session;
	state.primary = window;
	state.primaryController = controller;
	state.before = ReadSelection();
}

void SelfLiveOpened(const SelfTest &state) {
	Note(SelfText(u"live: second window %1 %2"_q.arg(
		SecondText(state),
		RanksText(state))));
	const auto after = ReadSelection();
	Check(
		state.secondPreexisted || !SameRegistry(state.before, after),
		SelfText(u"live: the registry snapshot sees the opened window, the "
			u"sensitivity control of the poll purity check"_q),
		u"preexisted=%1 before=[%2] after=[%3]"_q.arg(
			FlagText(state.secondPreexisted),
			SelectionText(state.before),
			SelectionText(after)));
}

void SelfLiveTwo(SelfTest &state) {
	const auto session = state.session.get();
	const auto account = session ? &session->account() : nullptr;
	const auto primaryKey = account
		? Core::App().separateWindowFor(Window::SeparateId(account))
		: nullptr;
	const auto secondKey = session
		? Core::App().separateWindowFor(Window::SeparateId(
			Window::SeparateType::Archive,
			session))
		: nullptr;
	const auto registry = ReadControllerRegistry();
	const auto primaryRequest = ExactController(session);
	const auto secondRequest = ExactController(session, state.second);
	const auto p = DecideController(primaryRequest, registry);
	const auto q = DecideController(secondRequest, registry);
	const auto both = u"primary=[%1] second=[%2]"_q.arg(
		ControllerReadingText(p),
		ControllerReadingText(q));
	Check(
		p.accepted()
			&& q.accepted()
			&& !Same(p.entry.window, q.entry.window)
			&& !Same(p.entry.controller, q.entry.controller),
		SelfText(u"live: both windows of the Test session are accepted, "
			u"with distinct windows and controllers"_q),
		both);
	Check(
		SameAnswer(p, ResolveController(primaryRequest))
			&& SameAnswer(q, ResolveController(secondRequest)),
		SelfText(u"live: ResolveController gives the decision's answers in "
			u"the same turn"_q),
		both);
	CheckReading(
		Same(p.entry.window, state.primary)
			&& Same(p.entry.window, primaryKey)
			&& Same(p.entry.controller, ControllerOf(state.primary))
			&& (p.entry.type == Window::SeparateType::Primary)
			&& p.entry.test
			&& (p.entry.accountIndex >= 0)
			&& (p.entry.accountIndex
				== ReadSessionGate().primarySessionIndex),
		u"live: the primary reading names the primary window, the window "
		u"its account key finds, that window's controller and the session "
		u"gate's account index"_q,
		p);
	CheckReading(
		Same(q.entry.window, state.second)
			&& Same(q.entry.window, secondKey)
			&& Same(q.entry.controller, ControllerOf(state.second))
			&& (q.entry.type == Window::SeparateType::Archive)
			&& q.entry.test,
		u"live: the second reading names the opened Archive window, the "
		u"window its key finds and that window's controller"_q,
		q);
	Check(
		ShowsSession(p, session)
			&& ShowsSession(q, session)
			&& (p.registered >= 2)
			&& (p.accountWindows >= 2),
		SelfText(u"live: both readings show the requested account and "
			u"session, whose own window list holds both controllers"_q),
		both);
	const auto primaryOnly = WithActivation(registry, state.primary);
	const auto qPrimaryOnly = DecideController(secondRequest, primaryOnly);
	CheckReading(
		q.accepted()
			&& SameAnswer(q, qPrimaryOnly)
			&& (qPrimaryOnly.entry.activation == -1),
		u"live: with activation history holding only the primary window, "
		u"the second window is accepted by registry and reads "
		u"activation=absent (the Run 1 shape)"_q,
		qPrimaryOnly);
	CheckSameUnderHistory(
		u"live: with activation history holding only the primary window"_q,
		registry,
		{ p, q },
		state.primary);
	CheckSameUnderHistory(
		u"live: with an empty activation history"_q,
		registry,
		{ p, q },
		{});
	Note(SelfText(u"live: real activation ranks "_q + RanksText(state)));
	state.primaryReading = p;
	state.secondReading = q;
}

// One synchronous turn: many polls, and a selection read on either side.
void SelfLivePolls(const SelfTest &state) {
	const auto session = state.session.get();
	const auto requests = std::array{
		ExactController(session),
		ExactController(session, state.second),
	};
	const auto absent = AbsentRequest(state);
	const auto before = ReadSelection();
	auto accepted = 0;
	auto absentRefused = 0;
	for (auto i = 0; i != kObservationPolls; ++i) {
		for (const auto &request : requests) {
			if (ResolveController(request).accepted()) {
				++accepted;
			}
		}
		const auto refusal = ResolveController(absent).refusal;
		if (refusal == ControllerRefusal::NoRegisteredWindow) {
			++absentRefused;
		}
	}
	const auto after = ReadSelection();
	Check(
		SameSelection(before, after),
		SelfText(u"live: polls change neither the registry, activation "
			u"history, the active primary window nor the active account"_q),
		u"before=[%1] after=[%2]"_q.arg(
			SelectionText(before),
			SelectionText(after)));
	Check(
		(accepted == 2 * kObservationPolls)
			&& (absentRefused == kObservationPolls),
		SelfText(u"live: every poll gives the same answer"_q),
		u"polls=%1 accepted=%2 absentRefused=%3"_q.arg(
			QString::number(kObservationPolls),
			QString::number(accepted),
			QString::number(absentRefused)));
	Check(
		!state.primaryActions
			&& !state.secondActions
			&& !state.refusedActions
			&& !state.controlActions,
		SelfText(u"live: no poll ran an action"_q),
		CountsText(state));
}

void SelfLiveCounts(const SelfTest &state) {
	Check(
		(state.primaryActions == 1)
			&& (state.pollsAtAction == state.primaryPolls)
			&& (state.primaryPolls >= kDelayedPolls + 1),
		SelfText(u"live: the primary window's action ran once, after its "
			u"refused polls and with no poll after it"_q),
		CountsText(state));
	Check(
		state.secondActions == 1,
		SelfText(u"live: the second window's action ran once"_q),
		CountsText(state));
	Check(
		Same(state.primaryActed, ControllerOf(state.primary))
			&& Same(state.secondActed, ControllerOf(state.second)),
		SelfText(u"live: each action received its own window's "
			u"controller"_q),
		u"primaryActed=%1 secondActed=%2"_q.arg(
			MatchText(state.primaryActed, ControllerOf(state.primary)),
			MatchText(state.secondActed, ControllerOf(state.second))));
	const auto now = ReadSelection();
	Check(
		Same(state.before.activePrimary, now.activePrimary)
			&& Same(state.before.activeAccount, now.activeAccount),
		SelfText(u"live: the active primary window and the active account "
			u"are unchanged"_q),
		u"activePrimary=%1 activeAccount=%2 before=[%3] now=[%4]"_q.arg(
			MatchText(now.activePrimary, state.before.activePrimary),
			MatchText(now.activeAccount, state.before.activeAccount),
			SelectionText(state.before),
			SelectionText(now)));
}

void SelfLiveRefused(SelfTest &state) {
	using Refusal = ControllerRefusal;
	const auto session = state.session.get();
	const auto account = session ? &session->account() : nullptr;
	const auto targets = std::vector<RefusedTarget>{
		{
			.name = u"the primary window requested for another account"_q,
			.request = WindowRequest(
				state.accountC.get(),
				session,
				state.primary),
			.expected = Refusal::WrongAccount,
		},
		{
			.name = u"another session requested in the account's window"_q,
			.request = AccountRequest(account, state.sessionC.get()),
			.expected = Refusal::WrongSession,
		},
		{
			.name = u"an alive but unregistered exact window"_q,
			.request = WindowRequest(
				account,
				session,
				state.windowX.get()),
			.expected = Refusal::WrongWindow,
		},
		{
			.name = u"an account without a registered window"_q,
			.request = AbsentRequest(state),
			.expected = Refusal::NoRegisteredWindow,
		},
	};
	const auto refused = [&](not_null<Window::SessionController*>) {
		++state.refusedActions;
	};
	for (const auto &target : targets) {
		const auto resolved = ResolveController(target.request);
		const auto dispatched = DispatchController(resolved, refused);
		Check(
			(resolved.refusal == target.expected)
				&& (dispatched.refusal == target.expected)
				&& !dispatched.dispatched,
			SelfText(u"live: %1 refuses as %2 and gets no action"_q.arg(
				target.name,
				ControllerRefusalName(target.expected))),
			u"resolved=[%1] dispatched=[%2]"_q.arg(
				ControllerReadingText(resolved),
				ControllerReadingText(dispatched)));
	}
	Check(
		state.refusedActions == 0,
		SelfText(u"live: no refused target received an action"_q),
		CountsText(state));
	const auto control = DispatchController(
		ResolveController(ExactController(session)),
		[&](not_null<Window::SessionController*>) {
			++state.controlActions;
		});
	CheckReading(
		control.dispatched && (state.controlActions == 1),
		u"live: the same dispatch acts once on a fresh accepted primary "
		u"reading, so the refused counter was reachable (control)"_q,
		control);
}

void SelfLiveClosed(SelfTest &state) {
	using Refusal = ControllerRefusal;
	const auto session = state.session.get();
	const auto fresh = ReadControllerRegistry();
	const auto recheck = RecheckController(state.secondReading, fresh);
	CheckReading(
		recheck.refusal == Refusal::DestroyedWindow,
		u"live: the closed window's retained reading refuses as "
		u"destroyed-window"_q,
		recheck);
	const auto dispatched = DispatchController(
		state.secondReading,
		[&](not_null<Window::SessionController*>) {
			++state.refusedActions;
		});
	CheckReading(
		(dispatched.refusal == Refusal::DestroyedWindow)
			&& !dispatched.dispatched
			&& !state.refusedActions
			&& (state.secondActions == 1),
		u"live: dispatching the closed window's retained reading runs no "
		u"action"_q,
		dispatched);
	const auto retainedRequest = ResolveController(
		state.secondReading.request);
	CheckReading(
		retainedRequest.refusal == Refusal::DestroyedWindow,
		u"live: the retained request for the closed window refuses as "
		u"destroyed-window"_q,
		retainedRequest);
	const auto consumer = ResolveController(
		ExactController(session, state.second));
	CheckReading(
		(consumer.refusal == Refusal::DestroyedWindow)
			&& consumer.entry.window.null(),
		u"live: a request made from the closed window's weak handle refuses "
		u"as destroyed-window and never falls back to the primary window"_q,
		consumer);
	const auto primary = RecheckController(state.primaryReading, fresh);
	CheckReading(
		primary.accepted()
			&& (primary.controller() != nullptr)
			&& Same(primary.entry.controller, state.primaryController),
		u"live: the primary window's retained reading stays accepted for "
		u"the same controller generation"_q,
		primary);
	const auto archive = session
		? Core::App().separateWindowFor(Window::SeparateId(
			Window::SeparateType::Archive,
			session))
		: nullptr;
	const auto registered = ranges::any_of(fresh.entries, [&](
			const ControllerEntry &entry) {
		return Same(entry.window, state.primary);
	});
	const auto now = ReadSelection();
	Check(
		!archive
			&& registered
			&& Same(state.before.activePrimary, now.activePrimary)
			&& Same(state.before.activeAccount, now.activeAccount),
		SelfText(u"live: the Archive window left the registry, the primary "
			u"window stayed, and the active primary window and account are "
			u"unchanged"_q),
		u"archive=%1 primaryRegistered=%2 activePrimary=%3 "
		u"activeAccount=%4"_q.arg(
			FlagText(archive != nullptr),
			FlagText(registered),
			MatchText(now.activePrimary, state.before.activePrimary),
			MatchText(now.activeAccount, state.before.activeAccount)));
	Note(SelfText(u"live: selection before=[%1] after close=[%2]; a "
		u"deferred saved-window restore may add windows across turns, so the "
		u"whole registry is not compared"_q.arg(
			SelectionText(state.before),
			SelectionText(now))));
}

} // namespace

bool ControllerReading::accepted() const {
	return (refusal == ControllerRefusal::None);
}

Window::SessionController *ControllerReading::controller() const {
	return accepted() ? entry.target.get() : nullptr;
}

ControllerRequest ExactController(Main::Session *session) {
	auto result = ControllerRequest();
	if (session) {
		result.account = &session->account();
		result.session = session;
	}
	return result;
}

ControllerRequest ExactController(
		Main::Session *session,
		const base::weak_ptr<Window::Controller> &window) {
	auto result = ExactController(session);
	result.window = window;
	result.exactWindow = true;
	return result;
}

QString ControllerRefusalName(ControllerRefusal refusal) {
	switch (refusal) {
	case ControllerRefusal::None: return u"none"_q;
	case ControllerRefusal::NotEvaluated: return u"not-evaluated"_q;
	case ControllerRefusal::NoSession: return u"no-session"_q;
	case ControllerRefusal::DestroyedWindow: return u"destroyed-window"_q;
	case ControllerRefusal::WrongWindow: return u"wrong-window"_q;
	case ControllerRefusal::WrongAccount: return u"wrong-account"_q;
	case ControllerRefusal::NoRegisteredWindow:
		return u"no-registered-window"_q;
	case ControllerRefusal::WrongSession: return u"wrong-session"_q;
	case ControllerRefusal::NonTest: return u"non-test"_q;
	case ControllerRefusal::Unready: return u"unready"_q;
	case ControllerRefusal::Locked: return u"locked"_q;
	case ControllerRefusal::Hidden: return u"hidden"_q;
	case ControllerRefusal::ReplacedWindow: return u"replaced-window"_q;
	case ControllerRefusal::ReplacedSession: return u"replaced-session"_q;
	}
	Unexpected("Refusal in Test::ControllerRefusalName.");
}

ControllerReading ResolveController(const ControllerRequest &request) {
	return DecideController(request, ReadControllerRegistry());
}

QString ControllerReadingText(const ControllerReading &reading) {
	const auto verdict = reading.accepted() ? u"accepted"_q : u"refused"_q;
	const auto entry = EntryText(reading.entry, reading.request.session);
	const auto fields = QStringList{
		u"controller verdict=%1"_q.arg(verdict),
		u"refusal=%1"_q.arg(ControllerRefusalName(reading.refusal)),
		u"request=[%1]"_q.arg(RequestText(reading.request)),
		u"registered=%1"_q.arg(reading.registered),
		u"accountWindows=%1"_q.arg(reading.accountWindows),
		u"activationSize=%1"_q.arg(reading.activationSize),
		u"entry=[%1]"_q.arg(entry),
		u"dispatched=%1"_q.arg(FlagText(reading.dispatched)),
	};
	return fields.join(u" "_q);
}

void ActOnController(
		not_null<Runner*> runner,
		const QString &name,
		Fn<ControllerRequest()> request,
		Fn<void(not_null<Window::SessionController*>)> action,
		crl::time timeout) {
	const auto last = std::make_shared<ControllerReading>();
	runner->add({
		.name = u"act on controller: %1"_q.arg(name),
		.until = [=] {
			*last = ResolveController(request());
			return last->accepted();
		},
		.then = [=] {
			const auto result = DispatchController(*last, action);
			if (result.dispatched) {
				Note(u"CONTROLLER_READY: act=[%1] %2"_q.arg(
					name,
					ControllerReadingText(result)));
			} else {
				Fail(
					u"act on controller: %1"_q.arg(name),
					(u"accepted controller changed before the action: "_q
						+ ControllerReadingText(result)));
			}
		},
		.timeout = timeout,
		.timeoutDetails = [=] { return ControllerReadingText(*last); },
	});
}

void AppendControllerReadySelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<SelfTest>();

	// The abort-path backstop (a stage timeout, the watchdog): an Archive
	// window left open would join the local saved-window set at quit and
	// reappear on the next launch.
	runner->onFinish([=] { CloseOpened(*state); });

	const auto leg = [&](const QString &name, Fn<void()> then) {
		runner->add({
			.name = SelfText(name),
			.then = std::move(then),
		});
	};
	leg(u"fixture: two Test controllers, activation history holds one"_q, [=] {
		SelfFixturePositives(*state);
	});
	leg(u"fixture: refusals"_q, [=] { SelfFixtureRefusals(*state); });
	leg(u"fixture: retained generations"_q, [=] {
		SelfFixtureGenerations(*state);
	});
	leg(u"live: Test session prerequisite"_q, [=] {
		SelfLivePrerequisite(*state);
	});
	runner->addGated([=] { return state->gate; }, [&] {
		runner->add({
			.name = SelfText(
				u"live: open a second window of the Test session"_q),
			.run = [=] { OpenSecond(*state); },
			.until = [=] { return SecondReady(*state); },
			.then = [=] { SelfLiveOpened(*state); },
			.timeoutDetails = [=] { return SecondText(*state); },
		});
		leg(u"live: two controllers by registry"_q, [=] {
			SelfLiveTwo(*state);
		});
		leg(u"live: polls observe only"_q, [=] { SelfLivePolls(*state); });
		ActOnController(
			runner,
			u"self-test primary window"_q,
			[=] { return DelayedPrimaryRequest(*state); },
			[=](not_null<Window::SessionController*> controller) {
				++state->primaryActions;
				state->pollsAtAction = state->primaryPolls;
				state->primaryActed = base::make_weak(controller);
			});
		ActOnController(
			runner,
			u"self-test second window"_q,
			[=] {
				return ExactController(state->session.get(), state->second);
			},
			[=](not_null<Window::SessionController*> controller) {
				++state->secondActions;
				state->secondActed = base::make_weak(controller);
			});
		leg(u"live: one action per accepted target"_q, [=] {
			SelfLiveCounts(*state);
		});
		leg(u"live: refused targets receive none"_q, [=] {
			SelfLiveRefused(*state);
		});
		runner->add({
			.name = SelfText(u"live: a closed window refuses"_q),
			.skipReason = [=] {
				return state->secondPreexisted
					? (u"the Archive window preexisted; the self-test closes "
						u"only windows it opened"_q)
					: QString();
			},
			.run = [=] { CloseOpened(*state); },
			.until = [=] { return state->second.empty(); },
			.then = [=] { SelfLiveClosed(*state); },
			.timeoutDetails = [=] { return SecondText(*state); },
		});
	});
}

} // namespace Test

#endif // _DEBUG
