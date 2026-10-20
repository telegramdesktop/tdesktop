/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_session_gate.h"

#include "base/weak_ptr.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/mtproto_dc_options.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <optional>

namespace Test {
namespace {

enum class GateCase {
	TestSession,
	NoAuthorizedAccount,
	SelectedUnauthorized,
	Declared,
	DomainNotStarted,
	InvalidDeclaration,
	NonTestPrimary,
	NonTestActive,
	SelectionUnreadable,
	HelperWithDeclaration,
	InvalidHelper,
	NoTestAccount,
	HelperSelectionSplit,
	HelperOriginalUnauthorized,
	SwitchNotApplied,
};

// The bounded wait after the restore stage or the finish backstop switched
// back, before its fresh reading; it mitigates the re-shown window's
// chat-background re-cache.
constexpr auto kTestSessionRestoreDrain = crl::time(1500);

// What the scenario asked of the gate before Runner::start().
struct GateRequests {
	std::vector<ProductionLegs> declarations;
	bool foreignRunner = false;
};

// |index| and |environment| name the account the verdict is about, when
// there is one.
struct GateVerdict {
	GateCase why = GateCase::DomainNotStarted;
	int index = -1;
	std::optional<MTP::Environment> environment;
	QString details;
};

// The account the gate refuses on, and whether the primary window (rather
// than domain().active() alone) selects it.
struct GateHazard {
	const SessionGateAccount *account = nullptr;
	bool primary = false;
};

// What the Test-session helper decides from the start reading alone,
// before it acts: a refusal, or the original and selected storage indices
// and whether a switch is needed.
struct HelperPlan {
	std::optional<GateVerdict> refusal;
	int originalIndex = -1;
	int selectedIndex = -1;
	bool switchNeeded = false;
};

// A restore that switched back: its reading taken right after the switch
// and when it switched. The post-switch drain after it is outstanding until
// a fresh reading at the end of that drain checked it.
struct RestoreDrain {
	TestSessionRestore switched;
	crl::time at = 0;
};

// One runner per process (Test::Start() owns a static one). |helper| is the
// Test-session helper's request; |captured| means it recorded |original|
// (held weakly), |originalIndex|, |originalEnvironment| and |selectedIndex|;
// |owed| is the restoration obligation of a switch that was called and not
// yet restored by a verified reading; |finishDue| is set by the helper's
// Runner::onFinish registration and taken by FinishSessionGate; |drain| is
// the outstanding post-switch drain of the last restore that switched back,
// whether the restore stage or the finish backstop made it, set by
// RestoreTestSession and taken only by the fresh reading at the drain's
// end: the restore stage's |then| or FinishSessionGateAfterDrain.
struct GateState {
	GateRequests requests;
	std::shared_ptr<TestSessionSwitch> helper;
	base::weak_ptr<Main::Account> original;
	std::optional<RestoreDrain> drain;
	Runner *runner = nullptr;
	MTP::Environment originalEnvironment = MTP::Environment();
	int originalIndex = -1;
	int selectedIndex = -1;
	bool applied = false;
	bool captured = false;
	bool owed = false;
	bool finishDue = false;
};

[[nodiscard]] GateState &ModuleState() {
	static auto result = GateState();
	return result;
}

// What is left of the outstanding post-switch drain, counted from its
// switch; zero when none is outstanding or its time is already over.
[[nodiscard]] crl::time DrainLeft() {
	const auto &drain = ModuleState().drain;
	return drain
		? std::max(
			kTestSessionRestoreDrain - (crl::now() - drain->at),
			crl::time(0))
		: crl::time(0);
}

[[nodiscard]] QString GateWhat() {
	return u"fixture gate: session gate"_q;
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString IndexText(int index) {
	return (index >= 0) ? QString::number(index) : u"none"_q;
}

[[nodiscard]] QString EnvironmentName(MTP::Environment environment) {
	switch (environment) {
	case MTP::Environment::Production: return u"Production"_q;
	case MTP::Environment::Test: return u"Test"_q;
	}
	return u"unknown-%1"_q.arg(int(environment));
}

[[nodiscard]] QString EnvironmentText(
		const std::optional<MTP::Environment> &environment) {
	return environment ? EnvironmentName(*environment) : u"none"_q;
}

[[nodiscard]] bool IsTest(MTP::Environment environment) {
	return (environment == MTP::Environment::Test);
}

[[nodiscard]] QString CaseName(GateCase why) {
	switch (why) {
	case GateCase::TestSession: return u"test-session"_q;
	case GateCase::NoAuthorizedAccount: return u"no-authorized-account"_q;
	case GateCase::SelectedUnauthorized: return u"selected-unauthorized"_q;
	case GateCase::Declared: return u"declared"_q;
	case GateCase::DomainNotStarted: return u"domain-not-started"_q;
	case GateCase::InvalidDeclaration: return u"invalid-declaration"_q;
	case GateCase::NonTestPrimary: return u"non-test-primary"_q;
	case GateCase::NonTestActive: return u"non-test-active"_q;
	case GateCase::SelectionUnreadable: return u"selection-unreadable"_q;
	case GateCase::HelperWithDeclaration:
		return u"helper-with-declaration"_q;
	case GateCase::InvalidHelper: return u"invalid-helper"_q;
	case GateCase::NoTestAccount: return u"no-test-account"_q;
	case GateCase::HelperSelectionSplit: return u"helper-selection-split"_q;
	case GateCase::HelperOriginalUnauthorized:
		return u"helper-original-unauthorized"_q;
	case GateCase::SwitchNotApplied: return u"switch-not-applied"_q;
	}
	Unexpected("GateCase in CaseName.");
}

[[nodiscard]] bool Refuses(GateCase why) {
	switch (why) {
	case GateCase::TestSession:
	case GateCase::NoAuthorizedAccount:
	case GateCase::SelectedUnauthorized:
	case GateCase::Declared:
		return false;
	case GateCase::DomainNotStarted:
	case GateCase::InvalidDeclaration:
	case GateCase::NonTestPrimary:
	case GateCase::NonTestActive:
	case GateCase::SelectionUnreadable:
	case GateCase::HelperWithDeclaration:
	case GateCase::InvalidHelper:
	case GateCase::NoTestAccount:
	case GateCase::HelperSelectionSplit:
	case GateCase::HelperOriginalUnauthorized:
	case GateCase::SwitchNotApplied:
		return true;
	}
	Unexpected("GateCase in Refuses.");
}

void GateNote(const QString &text) {
	Note(u"SESSION_GATE: "_q + text);
}

[[nodiscard]] const SessionGateAccount *FindAccount(
		const SessionGateReading &reading,
		int index) {
	if (index < 0) {
		return nullptr;
	}
	const auto i = ranges::find(
		reading.accounts,
		index,
		&SessionGateAccount::index);
	return (i != reading.accounts.end()) ? &*i : nullptr;
}

[[nodiscard]] int AuthorizedCount(const SessionGateReading &reading) {
	return int(ranges::count_if(reading.accounts, [](const auto &account) {
		return account.authorized;
	}));
}

[[nodiscard]] QString AccountRowText(
		const SessionGateAccount &account,
		const SessionGateReading &reading) {
	const auto valid = (account.index >= 0);
	return u"account index=%1 authorized=%2 environment=%3 primary=%4 "
		"active=%5"_q.arg(
			QString::number(account.index),
			FlagText(account.authorized),
			EnvironmentName(account.environment),
			FlagText(valid && (account.index == reading.primaryIndex)),
			FlagText(valid && (account.index == reading.activeIndex)));
}

[[nodiscard]] QString SelectionText(const SessionGateReading &reading) {
	return u"primaryIndex=%1 primarySessionIndex=%2 primarySessionTest=%3 "
		"activeIndex=%4"_q.arg(
			IndexText(reading.primaryIndex),
			IndexText(reading.primarySessionIndex),
			FlagText(reading.primarySessionTest),
			IndexText(reading.activeIndex));
}

[[nodiscard]] QString ReadingText(const SessionGateReading &reading) {
	return u"reading domainStarted=%1 accounts=%2 authorized=%3 %4"_q.arg(
		FlagText(reading.domainStarted),
		QString::number(int(reading.accounts.size())),
		QString::number(AuthorizedCount(reading)),
		SelectionText(reading));
}

[[nodiscard]] QString RolesText(
		const SessionGateReading &reading,
		int index) {
	const auto valid = (index >= 0);
	return u"index=%1 primary=%2 session=%3 active=%4"_q.arg(
		IndexText(index),
		FlagText(valid && (index == reading.primaryIndex)),
		FlagText(valid && (index == reading.primarySessionIndex)),
		FlagText(valid && (index == reading.activeIndex)));
}

[[nodiscard]] bool SingleLineLabel(const QString &text) {
	if (text.trimmed().isEmpty()) {
		return false;
	}
	return ranges::none_of(text, [](QChar ch) {
		const auto category = ch.category();
		return (category == QChar::Other_Control)
			|| (category == QChar::Separator_Line)
			|| (category == QChar::Separator_Paragraph);
	});
}

[[nodiscard]] bool ValidDeclaration(const ProductionLegs &legs) {
	return SingleLineLabel(legs.task) && SingleLineLabel(legs.legs);
}

[[nodiscard]] const ProductionLegs *ValidSingleDeclaration(
		const GateRequests &requests) {
	return (!requests.foreignRunner
		&& (requests.declarations.size() == 1)
		&& ValidDeclaration(requests.declarations.front()))
		? &requests.declarations.front()
		: nullptr;
}

[[nodiscard]] GateVerdict MakeVerdict(
		GateCase why,
		const QString &text,
		const SessionGateAccount *account = nullptr) {
	auto result = GateVerdict{
		.why = why,
		.details = u"case="_q + CaseName(why) + u": "_q + text,
	};
	if (account) {
		result.index = account->index;
		result.environment = account->environment;
	}
	return result;
}

// The window's account, its shown session's account, then
// domain().active(): the first authorized one not on Test.
[[nodiscard]] GateHazard FindHazard(const SessionGateReading &reading) {
	const auto primary = FindAccount(reading, reading.primaryIndex);
	const auto session = FindAccount(reading, reading.primarySessionIndex);
	const auto active = FindAccount(reading, reading.activeIndex);
	if (primary && primary->authorized && !IsTest(primary->environment)) {
		return { .account = primary, .primary = true };
	} else if (session && !IsTest(session->environment)) {
		return { .account = session, .primary = true };
	} else if (active && active->authorized && !IsTest(active->environment)) {
		return { .account = active, .primary = false };
	}
	return {};
}

[[nodiscard]] bool AnySelected(const SessionGateReading &reading) {
	const auto primary = FindAccount(reading, reading.primaryIndex);
	const auto active = FindAccount(reading, reading.activeIndex);
	return (primary && primary->authorized)
		|| (reading.primarySessionIndex >= 0)
		|| (active && active->authorized);
}

// Once any account is authorized, domain().active() must name a domain
// account; once an authorized account is selected, the primary window must
// also name one and show exactly its session (none for an unauthorized one),
// with that session's test mode matching the account's environment.
[[nodiscard]] bool SelectionCoherent(const SessionGateReading &reading) {
	const auto selected = AnySelected(reading);
	if (!selected && !AuthorizedCount(reading)) {
		return true;
	} else if (!FindAccount(reading, reading.activeIndex)) {
		return false;
	} else if (!selected) {
		return true;
	}
	const auto primary = FindAccount(reading, reading.primaryIndex);
	if (!primary) {
		return false;
	} else if (!primary->authorized) {
		return (reading.primarySessionIndex < 0);
	}
	return (reading.primarySessionIndex == primary->index)
		&& (reading.primarySessionTest == IsTest(primary->environment));
}

[[nodiscard]] GateVerdict DecideSessionGate(
		const SessionGateReading &reading,
		const GateRequests &requests) {
	if (!reading.domainStarted) {
		return MakeVerdict(
			GateCase::DomainNotStarted,
			u"the domain is not started (a local passcode locks the copy), "
			"so no account's environment can be read"_q);
	} else if (requests.foreignRunner) {
		return MakeVerdict(
			GateCase::InvalidDeclaration,
			u"Test::DeclareProductionLegs was made for another runner"_q);
	} else if (requests.declarations.size() > 1) {
		return MakeVerdict(
			GateCase::InvalidDeclaration,
			u"Test::DeclareProductionLegs was made %1 times; a scenario "
			"declares its production legs once"_q.arg(
				QString::number(int(requests.declarations.size()))));
	} else if (!requests.declarations.empty()
		&& !ValidSingleDeclaration(requests)) {
		return MakeVerdict(
			GateCase::InvalidDeclaration,
			u"the production-leg declaration's task or legs label is empty "
			"or not a single line (labels not echoed)"_q);
	}
	const auto declared = (ValidSingleDeclaration(requests) != nullptr);
	const auto hazard = FindHazard(reading);
	if (hazard.account && !declared) {
		const auto environment = EnvironmentName(hazard.account->environment);
		return hazard.primary
			? MakeVerdict(
				GateCase::NonTestPrimary,
				u"the primary window shows an authorized account on the %1 "
				"environment (%2), so the scenario's stages would act as "
				"it"_q.arg(
					environment,
					RolesText(reading, hazard.account->index)),
				hazard.account)
			: MakeVerdict(
				GateCase::NonTestActive,
				u"domain().active() is an authorized account on the %1 "
				"environment (%2) that the primary window does not show "
				"(primaryIndex=%3), so every stage or shared helper reading "
				"domain().active() would act as it"_q.arg(
					environment,
					RolesText(reading, hazard.account->index),
					IndexText(reading.primaryIndex)),
				hazard.account);
	} else if (!SelectionCoherent(reading)) {
		return MakeVerdict(
			GateCase::SelectionUnreadable,
			u"an authorized session may be selected, but the primary "
			"window's or domain().active()'s reading is missing or "
			"contradicts itself (%1), so what the stages would act as is "
			"unknown"_q.arg(SelectionText(reading)));
	} else if (hazard.account) {
		return MakeVerdict(
			GateCase::Declared,
			u"the production-leg declaration accepts the %1 environment of "
			"the selected authorized account (%2)"_q.arg(
				EnvironmentName(hazard.account->environment),
				RolesText(reading, hazard.account->index)),
			hazard.account);
	}
	const auto authorized = AuthorizedCount(reading);
	if (!authorized) {
		return MakeVerdict(
			GateCase::NoAuthorizedAccount,
			u"the started domain holds %1 account(s) and none is "
			"authorized"_q.arg(
				QString::number(int(reading.accounts.size()))));
	} else if (!AnySelected(reading)) {
		return MakeVerdict(
			GateCase::SelectedUnauthorized,
			u"neither the primary window nor domain().active() selects an "
			"authorized account (%1); %2 authorized account(s) are not "
			"selected"_q.arg(
				SelectionText(reading),
				QString::number(authorized)));
	}
	return MakeVerdict(
		GateCase::TestSession,
		u"every authorized account the primary window, its shown session "
		"and domain().active() select is on the Test environment (%1)"_q.arg(
			SelectionText(reading)));
}

[[nodiscard]] QString DeclarationText(
		const ProductionLegs &legs,
		const GateVerdict &verdict) {
	const auto accepted = (verdict.why == GateCase::Declared);
	return u"production-leg declaration task=[%1] legs=[%2] accepted=%3 "
		"index=%4"_q.arg(
			legs.task,
			legs.legs,
			accepted ? EnvironmentText(verdict.environment) : u"none"_q,
			accepted ? IndexText(verdict.index) : u"none"_q);
}

[[nodiscard]] QString SkipReasonText(const GateVerdict &verdict) {
	return u"fixture gate: session gate refused the scenario (case=%1)"_q.arg(
		CaseName(verdict.why));
}

[[nodiscard]] QString VerdictText(const GateVerdict &verdict) {
	return u"refused=%1 case=%2 index=%3 environment=%4 details=[%5]"_q.arg(
		FlagText(Refuses(verdict.why)),
		CaseName(verdict.why),
		IndexText(verdict.index),
		EnvironmentText(verdict.environment),
		verdict.details);
}

// Every reader - domain().active(), the primary window's account and the
// account owning the session it shows - selects the authorized account at
// |index| on |environment|, and the shown session's test mode agrees.
[[nodiscard]] bool SelectsOnly(
		const SessionGateReading &reading,
		int index,
		MTP::Environment environment) {
	const auto account = FindAccount(reading, index);
	return account
		&& account->authorized
		&& (account->environment == environment)
		&& (reading.activeIndex == index)
		&& (reading.primaryIndex == index)
		&& (reading.primarySessionIndex == index)
		&& (reading.primarySessionTest == IsTest(environment));
}

// The helper's switch proof: the selected Test session in every reader.
[[nodiscard]] bool ShowsTestSession(
		const SessionGateReading &reading,
		int index) {
	return SelectsOnly(reading, index, MTP::Environment::Test);
}

// The restore proof: the captured original, still alive, in every reader.
[[nodiscard]] bool ShowsOriginal(
		const SessionGateReading &reading,
		int index,
		MTP::Environment environment,
		bool alive) {
	return alive && SelectsOnly(reading, index, environment);
}

[[nodiscard]] int LowestTestIndex(const SessionGateReading &reading) {
	auto result = -1;
	for (const auto &account : reading.accounts) {
		if (account.authorized
			&& IsTest(account.environment)
			&& ((result < 0) || (account.index < result))) {
			result = account.index;
		}
	}
	return result;
}

// The helper's decision before it acts: pure, so the self-test drives it
// with controlled readings.
[[nodiscard]] HelperPlan PlanTestSession(
		const SessionGateReading &reading,
		const GateRequests &requests) {
	const auto refuse = [](GateVerdict verdict) {
		return HelperPlan{ .refusal = std::move(verdict) };
	};
	if (!reading.domainStarted) {
		return refuse(DecideSessionGate(reading, GateRequests()));
	} else if (!requests.declarations.empty()) {
		return refuse(MakeVerdict(
			GateCase::HelperWithDeclaration,
			u"Test::UseTestSession and Test::DeclareProductionLegs were "
			"both requested; they are mutually exclusive, so no product "
			"switch was called"_q));
	} else if (requests.foreignRunner) {
		return refuse(MakeVerdict(
			GateCase::InvalidHelper,
			u"Test::UseTestSession was made for another runner, so no "
			"product switch was called"_q));
	} else if (ShowsTestSession(reading, reading.primaryIndex)) {
		return {
			.originalIndex = reading.primaryIndex,
			.selectedIndex = reading.primaryIndex,
		};
	}
	const auto selected = LowestTestIndex(reading);
	const auto original = FindAccount(reading, reading.activeIndex);
	if (selected < 0) {
		return refuse(MakeVerdict(
			GateCase::NoTestAccount,
			u"the Test-session helper found no authorized Test-environment "
			"account (accounts=%1 authorized=%2), so no product switch was "
			"called"_q.arg(
				QString::number(int(reading.accounts.size())),
				QString::number(AuthorizedCount(reading)))));
	} else if (!original
		|| (reading.primaryIndex != original->index)
		|| !SelectionCoherent(reading)) {
		return refuse(MakeVerdict(
			GateCase::HelperSelectionSplit,
			u"domain().active(), the primary window's account and the "
			"session it shows do not name one coherent account (%1), so "
			"one restore could not put every reader back; no product "
			"switch was called"_q.arg(SelectionText(reading))));
	} else if (!original->authorized) {
		return refuse(MakeVerdict(
			GateCase::HelperOriginalUnauthorized,
			u"the original domain().active() account (index=%1) is not "
			"authorized, and Main::Domain::removeRedundantAccounts() may "
			"drop such an account once another one is active, so it could "
			"not be restored; no product switch was called"_q.arg(
				QString::number(original->index)),
			original));
	}
	return {
		.originalIndex = original->index,
		.selectedIndex = selected,
		.switchNeeded = true,
	};
}

[[nodiscard]] QString PlanText(const HelperPlan &plan) {
	return u"refusal=%1 originalIndex=%2 selectedIndex=%3 "
		"switchNeeded=%4 details=[%5]"_q.arg(
			plan.refusal ? CaseName(plan.refusal->why) : u"none"_q,
			IndexText(plan.originalIndex),
			IndexText(plan.selectedIndex),
			FlagText(plan.switchNeeded),
			plan.refusal ? plan.refusal->details : QString());
}

[[nodiscard]] Main::Account *DomainAccount(int index) {
	const auto &accounts = Core::App().domain().accounts();
	const auto i = ranges::find(
		accounts,
		index,
		&Main::Domain::AccountWithIndex::index);
	return (i != accounts.end()) ? i->account.get() : nullptr;
}

// The helper's action at start, before any stage; its verdict replaces the
// ordinary one. The obligation is recorded before the switch is called.
[[nodiscard]] GateVerdict ApplyTestSession(
		not_null<Runner*> runner,
		const SessionGateReading &reading) {
	auto &state = ModuleState();
	auto &helper = *state.helper;
	const auto plan = PlanTestSession(reading, state.requests);
	helper.decided = true;
	helper.originalIndex = plan.originalIndex;
	helper.selectedIndex = plan.selectedIndex;
	const auto refuse = [&](GateVerdict verdict) {
		helper.refusal = CaseName(verdict.why);
		return verdict;
	};
	if (plan.refusal) {
		return refuse(*plan.refusal);
	}
	const auto account = FindAccount(reading, plan.originalIndex);
	const auto original = DomainAccount(plan.originalIndex);
	const auto selected = DomainAccount(plan.selectedIndex);
	if (!account || !original || !selected) {
		return refuse(MakeVerdict(
			GateCase::SwitchNotApplied,
			u"the domain holds no account at a storage index the reading "
			"named (originalIndex=%1 selectedIndex=%2), so no product "
			"switch was called"_q.arg(
				IndexText(plan.originalIndex),
				IndexText(plan.selectedIndex))));
	}
	state.original = base::make_weak(original);
	state.originalEnvironment = account->environment;
	state.originalIndex = plan.originalIndex;
	state.selectedIndex = plan.selectedIndex;
	state.captured = true;
	runner->onFinish([] {
		ModuleState().finishDue = true;
	});
	if (!plan.switchNeeded) {
		helper.switchedNothing = true;
		GateNote(u"test session helper: switched nothing - the primary "
			"window already shows the authorized Test session (index=%1), "
			"so no product switch was called"_q.arg(
				QString::number(plan.selectedIndex)));
		return DecideSessionGate(reading, state.requests);
	}
	state.owed = true;
	helper.switchCalled = true;
	Core::App().domain().activate(selected);
	const auto after = ReadSessionGate();
	helper.proven = ShowsTestSession(after, plan.selectedIndex);
	GateNote(u"test session helper: switched index=%1 environment=%2 -> "
		"index=%3 environment=Test through Main::Domain::activate(); read "
		"back %4 proven=%5"_q.arg(
			QString::number(plan.originalIndex),
			EnvironmentName(account->environment),
			QString::number(plan.selectedIndex),
			SelectionText(after),
			FlagText(helper.proven)));
	if (!helper.proven) {
		return refuse(MakeVerdict(
			GateCase::SwitchNotApplied,
			u"Main::Domain::activate(index=%1) did not read back as the "
			"authorized Test session in every reader (%2); the restoration "
			"obligation stays, so Runner::finish() switches back to "
			"index=%3"_q.arg(
				QString::number(plan.selectedIndex),
				SelectionText(after),
				QString::number(plan.originalIndex))));
	}
	return DecideSessionGate(after, state.requests);
}

// The current reading against the captured original; applies nothing.
[[nodiscard]] TestSessionRestore ReadTestSessionRestore(
		const QString &source,
		bool owed,
		bool applied) {
	const auto &state = ModuleState();
	const auto reading = ReadSessionGate();
	const auto active = FindAccount(reading, reading.activeIndex);
	const auto alive = (state.original.get() != nullptr);
	auto activeEnvironment = std::optional<MTP::Environment>();
	if (active) {
		activeEnvironment = active->environment;
	}
	return {
		.source = source,
		.activeEnvironment = activeEnvironment,
		.originalEnvironment = state.originalEnvironment,
		.originalIndex = state.originalIndex,
		.selectedIndex = state.selectedIndex,
		.activeIndex = reading.activeIndex,
		.primaryIndex = reading.primaryIndex,
		.primarySessionIndex = reading.primarySessionIndex,
		.captured = state.captured,
		.owed = owed,
		.applied = applied,
		.originalGone = state.captured && !alive,
		.primarySessionTest = reading.primarySessionTest,
		.equal = state.captured
			&& ShowsOriginal(
				reading,
				state.originalIndex,
				state.originalEnvironment,
				alive),
	};
}

// The self-test's controlled inputs: plain literals, never a real account.
[[nodiscard]] SessionGateAccount ControlledAccount(
		int index,
		bool authorized,
		MTP::Environment environment) {
	return {
		.index = index,
		.authorized = authorized,
		.environment = environment,
	};
}

// Index 0 an authorized Production account, index 1 an authorized Test one.
[[nodiscard]] std::vector<SessionGateAccount> MixedAccounts() {
	return {
		ControlledAccount(0, true, MTP::Environment::Production),
		ControlledAccount(1, true, MTP::Environment::Test),
	};
}

[[nodiscard]] SessionGateReading ControlledReading(
		std::vector<SessionGateAccount> accounts,
		int primary,
		int session,
		bool sessionTest,
		int active) {
	return {
		.accounts = std::move(accounts),
		.primaryIndex = primary,
		.primarySessionIndex = session,
		.activeIndex = active,
		.domainStarted = true,
		.primarySessionTest = sessionTest,
	};
}

[[nodiscard]] SessionGateReading RefusedReading() {
	return ControlledReading(MixedAccounts(), 0, 0, false, 0);
}

[[nodiscard]] ProductionLegs ControlledLegs() {
	return {
		.task = u"2026/01/01/session-gate-self-test"_q,
		.legs = u"read-only self-test legs"_q,
	};
}

[[nodiscard]] GateRequests Declaring(std::vector<ProductionLegs> legs) {
	return { .declarations = std::move(legs) };
}

// One controlled verdict against the expected case, account index and
// environment (named in the details when expected), with |absent| text the
// details must not carry.
void CheckVerdict(
		const QString &what,
		const SessionGateReading &reading,
		const GateRequests &requests,
		GateCase why,
		int index = -1,
		std::optional<MTP::Environment> environment = std::nullopt,
		const QString &absent = QString()) {
	const auto verdict = DecideSessionGate(reading, requests);
	const auto named = !environment
		|| verdict.details.contains(
			u"the %1 environment"_q.arg(EnvironmentName(*environment)));
	const auto clean = absent.isEmpty() || !verdict.details.contains(absent);
	Check(
		(verdict.why == why)
			&& (verdict.index == index)
			&& (verdict.environment == environment)
			&& named
			&& clean,
		what,
		u"expected case=%1 index=%2 environment=%3; got %4; %5"_q.arg(
			CaseName(why),
			IndexText(index),
			EnvironmentText(environment),
			VerdictText(verdict),
			ReadingText(reading)));
}

// One controlled helper plan against the expected refusal (none when
// |refusal| is empty), original and selected indices and switch need.
void CheckPlan(
		const QString &what,
		const SessionGateReading &reading,
		const GateRequests &requests,
		std::optional<GateCase> refusal,
		int original = -1,
		int selected = -1,
		bool switchNeeded = false) {
	const auto plan = PlanTestSession(reading, requests);
	auto why = std::optional<GateCase>();
	if (plan.refusal) {
		why = plan.refusal->why;
	}
	Check(
		(why == refusal)
			&& (plan.originalIndex == original)
			&& (plan.selectedIndex == selected)
			&& (plan.switchNeeded == switchNeeded),
		what,
		u"expected refusal=%1 originalIndex=%2 selectedIndex=%3 "
		"switchNeeded=%4; got %5; %6"_q.arg(
			refusal ? CaseName(*refusal) : u"none"_q,
			IndexText(original),
			IndexText(selected),
			FlagText(switchNeeded),
			PlanText(plan),
			ReadingText(reading)));
}

// One controlled read-back predicate against its expected answer.
void CheckReadBack(
		const QString &what,
		bool actual,
		bool expected,
		const SessionGateReading &reading,
		const QString &subject) {
	Check(
		actual == expected,
		what,
		u"expected=%1 actual=%2 subject=[%3]; %4"_q.arg(
			FlagText(expected),
			FlagText(actual),
			subject,
			ReadingText(reading)));
}

} // namespace

SessionGateReading ReadSessionGate() {
	auto result = SessionGateReading();
	const auto &domain = Core::App().domain();
	result.domainStarted = domain.started();
	if (!result.domainStarted) {
		return result;
	}
	const auto window = Core::App().activePrimaryWindow();
	const auto shown = window ? window->id().account : nullptr;
	const auto controller = window ? window->sessionController() : nullptr;
	const auto session = controller ? &controller->session() : nullptr;
	const auto sessionAccount = session ? &session->account() : nullptr;
	const auto active = &domain.active();
	for (const auto &[index, account] : domain.accounts()) {
		const auto raw = account.get();
		result.accounts.push_back({
			.index = index,
			.authorized = raw->sessionExists(),
			.environment = raw->mtp().environment(),
		});
		if (raw == shown) {
			result.primaryIndex = index;
		}
		if (raw == sessionAccount) {
			result.primarySessionIndex = index;
		}
		if (raw == active) {
			result.activeIndex = index;
		}
	}
	result.primarySessionTest = session && session->isTestMode();
	return result;
}

void DeclareProductionLegs(not_null<Runner*> runner, ProductionLegs legs) {
	auto &state = ModuleState();
	if (state.applied) {
		GateNote(u"production-leg declaration ignored: "
			"Test::DeclareProductionLegs was made after the gate decided"_q);
		return;
	}
	if (state.runner && (state.runner != runner.get())) {
		state.requests.foreignRunner = true;
	}
	state.runner = runner.get();
	state.requests.declarations.push_back(std::move(legs));
}

std::shared_ptr<const TestSessionSwitch> UseTestSession(
		not_null<Runner*> runner) {
	auto &state = ModuleState();
	if (state.applied) {
		GateNote(u"test session helper ignored: Test::UseTestSession was "
			"made after the gate decided, so nothing was switched"_q);
		return state.helper
			? state.helper
			: std::make_shared<TestSessionSwitch>();
	}
	if (state.runner && (state.runner != runner.get())) {
		state.requests.foreignRunner = true;
	}
	state.runner = runner.get();
	if (!state.helper) {
		state.helper = std::make_shared<TestSessionSwitch>();
	}
	return state.helper;
}

QString ApplySessionGate(
		not_null<Runner*> runner,
		const SessionGateReading &reading) {
	auto &state = ModuleState();
	if (state.runner && (state.runner != runner.get())) {
		state.requests.foreignRunner = true;
	}
	state.runner = runner.get();
	state.applied = true;

	for (const auto &account : reading.accounts) {
		GateNote(AccountRowText(account, reading));
	}
	GateNote(ReadingText(reading));
	const auto verdict = state.helper
		? ApplyTestSession(runner, reading)
		: DecideSessionGate(reading, state.requests);
	if (const auto legs = ValidSingleDeclaration(state.requests)) {
		GateNote(DeclarationText(*legs, verdict));
	}
	if (Refuses(verdict.why)) {
		Fail(GateWhat(), verdict.details);
		return SkipReasonText(verdict);
	}
	GateNote(u"decision pass "_q + verdict.details);
	return QString();
}

Stage RefusedStage(Stage stage, const QString &reason) {
	const auto fixed = reason.isEmpty()
		? u"fixture gate: session gate refused the scenario"_q
		: reason;
	stage.skipReason = [=] { return fixed; };
	return stage;
}

QString TestSessionRestoreText(const TestSessionRestore &restore) {
	const auto readers = u"captured=%1 owed=%2 applied=%3 originalIndex=%4 "
		"selectedIndex=%5 activeIndex=%6 primaryIndex=%7 "
		"primarySessionIndex=%8 primarySessionTest=%9"_q.arg(
			FlagText(restore.captured),
			FlagText(restore.owed),
			FlagText(restore.applied),
			IndexText(restore.originalIndex),
			IndexText(restore.selectedIndex),
			IndexText(restore.activeIndex),
			IndexText(restore.primaryIndex),
			IndexText(restore.primarySessionIndex),
			FlagText(restore.primarySessionTest));
	const auto verdict = u"activeEnvironment=%1 originalEnvironment=%2 "
		"originalGone=%3 equal=%4"_q.arg(
			EnvironmentText(restore.activeEnvironment),
			(restore.captured
				? EnvironmentName(restore.originalEnvironment)
				: u"none"_q),
			FlagText(restore.originalGone),
			FlagText(restore.equal));
	return u"test session restore source=["_q
		+ restore.source
		+ u"] "_q
		+ readers
		+ u" "_q
		+ verdict;
}

TestSessionRestore RestoreTestSession(const QString &source) {
	auto &state = ModuleState();
	const auto owed = state.owed;
	const auto original = owed ? state.original.get() : nullptr;
	if (original) {
		Core::App().domain().activate(original);
	}
	const auto result = ReadTestSessionRestore(
		source,
		owed,
		(original != nullptr));
	if (result.applied) {
		state.drain = RestoreDrain{
			.switched = result,
			.at = crl::now(),
		};
	}
	if (result.applied && result.equal) {
		state.owed = false;
	}
	return result;
}

void AppendTestSessionRestore(
		not_null<Runner*> runner,
		std::shared_ptr<const TestSessionSwitch> switched) {
	const auto first = std::make_shared<TestSessionRestore>();
	runner->add({
		.name = u"test session restore: the original active account in the "
			"primary window"_q,
		.run = [=] {
			*first = RestoreTestSession(u"teardown"_q);
		},
		.until = [=] {
			return !DrainLeft();
		},
		.then = [=] {
			const auto helper = ModuleState().helper.get();
			const auto handle = !switched
				? u"null"_q
				: (switched.get() == helper)
				? u"own"_q
				: u"foreign"_q;
			const auto drained = base::take(ModuleState().drain);
			const auto settled = ReadTestSessionRestore(
				u"teardown-settled"_q,
				ModuleState().owed,
				false);
			Check(
				(switched.get() == helper)
					&& helper
					&& first->equal
					&& settled.equal,
				u"test session restore: the original account is active and "
				"shown by the primary window again after the post-switch "
				"drain"_q,
				u"handle=%1 waitedMs=%2; first: %3; at the end of the wait: "
				"%4"_q.arg(
					handle,
					QString::number(drained
						? (crl::now() - drained->at)
						: crl::time(0)),
					TestSessionRestoreText(*first),
					TestSessionRestoreText(settled)));
			const auto second = RestoreTestSession(u"teardown-second"_q);
			Check(
				!second.applied && second.equal,
				u"test session restore: a second restore applies nothing and "
				"still reads equal"_q,
				TestSessionRestoreText(second));
		},
		.timeout = kTestSessionRestoreDrain + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return TestSessionRestoreText(*first);
		},
	});
}

crl::time FinishSessionGate(not_null<Runner*> runner) {
	auto &state = ModuleState();
	if ((state.runner != runner.get()) || !base::take(state.finishDue)) {
		return 0;
	}
	const auto interrupted = state.drain.has_value();
	const auto last = RestoreTestSession(u"finish"_q);
	const auto text = TestSessionRestoreText(last);
	if (last.applied) {
		GateNote(text + u" - switched back at finish, checked again at the "
			"end of the post-switch drain"_q);
	} else if (!last.equal) {
		Fail(
			u"test session restore at finish: the original account is "
			"active and shown by the primary window again"_q,
			text);
	} else if (interrupted) {
		GateNote(text + u" - nothing was owed at finish, but the post-switch "
			"drain of an earlier switch back was not checked yet, checked "
			"again at its end"_q);
	} else {
		GateNote(text + u" - verified at finish, nothing was owed"_q);
	}
	return FinishSessionGateAfterDrain(runner);
}

crl::time FinishSessionGateAfterDrain(not_null<Runner*> runner) {
	auto &state = ModuleState();
	if ((state.runner != runner.get()) || !state.drain) {
		return 0;
	}
	if (const auto left = DrainLeft()) {
		return left;
	}
	const auto drain = *base::take(state.drain);
	const auto settled = ReadTestSessionRestore(
		u"finish-settled"_q,
		state.owed,
		false);
	Check(
		drain.switched.equal && settled.equal,
		u"test session restore at finish: the original account is active "
		"and shown by the primary window again after the post-switch "
		"drain"_q,
		u"waitedMs=%1; first: %2; at the end of the wait: %3"_q.arg(
			QString::number(crl::now() - drain.at),
			TestSessionRestoreText(drain.switched),
			TestSessionRestoreText(settled)));
	return 0;
}

void AppendSessionGateSelfTest(not_null<Runner*> runner) {
	struct State {
		int skippedBefore = 0;
		int originalSkipReasons = 0;
		int runs = 0;
		int untilPolls = 0;
		int thens = 0;
	};
	const auto state = std::make_shared<State>();
	const auto production = MTP::Environment::Production;

	runner->add({
		.name = u"session gate self-test: a Production primary reading "
			"refuses with the named row"_q,
		.then = [=] {
			const auto reading = RefusedReading();
			CheckVerdict(
				u"session gate self-test: a Production primary refuses as "
				"non-test-primary and names the Production environment"_q,
				reading,
				GateRequests(),
				GateCase::NonTestPrimary,
				0,
				production);
			const auto verdict = DecideSessionGate(reading, GateRequests());
			const auto reason = SkipReasonText(verdict);
			const auto expected = u"fixture gate: session gate refused the "
				"scenario (case=non-test-primary)"_q;
			Check(
				Refuses(verdict.why)
					&& (reason == expected)
					&& (GateWhat() == u"fixture gate: session gate"_q),
				u"session gate self-test: the refusal row is the named "
				"fixture gate and every stage gets its fixed skip reason"_q,
				u"what=[%1] reason=[%2]"_q.arg(GateWhat(), reason));
			state->skippedBefore = SkippedCount();
		},
	});

	const auto reason = SkipReasonText(
		DecideSessionGate(RefusedReading(), GateRequests()));
	for (const auto &which : { u"first"_q, u"second"_q }) {
		runner->add(RefusedStage({
			.name = u"session gate self-test: a stage behind the controlled "
				"refusal (%1)"_q.arg(which),
			.skipReason = [=] {
				++state->originalSkipReasons;
				return QString();
			},
			.run = [=] { ++state->runs; },
			.until = [=] {
				++state->untilPolls;
				return false;
			},
			.then = [=] { ++state->thens; },
			.timeout = crl::time(1000),
		}, reason));
	}

	runner->add({
		.name = u"session gate self-test: the stages behind a refusal are "
			"skipped without acting"_q,
		.then = [=] {
			const auto skipped = SkippedCount() - state->skippedBefore;
			Check(
				(skipped == 2)
					&& !state->originalSkipReasons
					&& !state->runs
					&& !state->untilPolls
					&& !state->thens,
				u"session gate self-test: exactly two N/A rows and no "
				"original skipReason, run, until or then entered"_q,
				u"skipped=%1 originalSkipReason=%2 run=%3 untilPolls=%4 "
				"then=%5"_q.arg(
					QString::number(skipped),
					QString::number(state->originalSkipReasons),
					QString::number(state->runs),
					QString::number(state->untilPolls),
					QString::number(state->thens)));
			const auto fallback = RefusedStage(Stage(), QString());
			const auto text = fallback.skipReason
				? fallback.skipReason()
				: QString();
			Check(
				!text.isEmpty(),
				u"session gate self-test: a stage refused with an empty "
				"reason is still skipped"_q,
				u"reason=[%1]"_q.arg(text));
		},
	});

	runner->add({
		.name = u"session gate self-test: a Test primary reading passes"_q,
		.then = [=] {
			CheckVerdict(
				u"session gate self-test: a Test primary passes beside an "
				"authorized Production account no reader selects"_q,
				ControlledReading(MixedAccounts(), 1, 1, true, 1),
				GateRequests(),
				GateCase::TestSession);
			CheckVerdict(
				u"session gate self-test: an unselected authorized "
				"Production account beside an unauthorized selection "
				"passes"_q,
				ControlledReading(
					{
						ControlledAccount(0, false, production),
						ControlledAccount(1, true, production),
					},
					0,
					-1,
					false,
					0),
				GateRequests(),
				GateCase::SelectedUnauthorized,
				-1,
				std::nullopt,
				u"none is authorized"_q);
		},
	});

	runner->add({
		.name = u"session gate self-test: a reading with no authorized "
			"account passes"_q,
		.then = [=] {
			CheckVerdict(
				u"session gate self-test: an account-free reading passes"_q,
				ControlledReading(
					{ ControlledAccount(0, false, production) },
					0,
					-1,
					false,
					0),
				GateRequests(),
				GateCase::NoAuthorizedAccount);
		},
	});

	runner->add({
		.name = u"session gate self-test: the production-leg declaration "
			"passes and names the environment it accepted"_q,
		.then = [=] {
			const auto reading = RefusedReading();
			const auto requests = Declaring({ ControlledLegs() });
			CheckVerdict(
				u"session gate self-test: the declaration accepts the "
				"Production primary"_q,
				reading,
				requests,
				GateCase::Declared,
				0,
				production);
			const auto accepted = DeclarationText(
				ControlledLegs(),
				DecideSessionGate(reading, requests));
			const auto expected = u"production-leg declaration "
				"task=[2026/01/01/session-gate-self-test] "
				"legs=[read-only self-test legs] accepted=Production "
				"index=0"_q;
			Check(
				accepted == expected,
				u"session gate self-test: the declaration row names the "
				"environment it accepted"_q,
				u"row=[%1]"_q.arg(accepted));
			const auto onTest = ControlledReading(
				MixedAccounts(),
				1,
				1,
				true,
				1);
			const auto unneeded = DeclarationText(
				ControlledLegs(),
				DecideSessionGate(onTest, requests));
			Check(
				unneeded.endsWith(u" accepted=none index=none"_q),
				u"session gate self-test: a declaration a Test primary does "
				"not need accepts nothing"_q,
				u"row=[%1]"_q.arg(unneeded));
		},
	});

	runner->add({
		.name = u"session gate self-test: the primary window and "
			"domain().active() are both read"_q,
		.then = [=] {
			CheckVerdict(
				u"session gate self-test: a Production domain().active() "
				"refuses although the window shows Test"_q,
				ControlledReading(MixedAccounts(), 1, 1, true, 0),
				GateRequests(),
				GateCase::NonTestActive,
				0,
				production);
			CheckVerdict(
				u"session gate self-test: a Production window refuses "
				"although domain().active() is Test"_q,
				ControlledReading(MixedAccounts(), 0, 0, false, 1),
				GateRequests(),
				GateCase::NonTestPrimary,
				0,
				production);
			CheckVerdict(
				u"session gate self-test: a window naming Test while showing "
				"the Production session refuses"_q,
				ControlledReading(MixedAccounts(), 1, 0, false, 1),
				GateRequests(),
				GateCase::NonTestPrimary,
				0,
				production);
		},
	});

	runner->add({
		.name = u"session gate self-test: missing or self-contradicting "
			"readings fail closed"_q,
		.then = [=] {
			CheckVerdict(
				u"session gate self-test: a Test window with no shown "
				"session refuses"_q,
				ControlledReading(MixedAccounts(), 1, -1, false, 1),
				GateRequests(),
				GateCase::SelectionUnreadable);
			CheckVerdict(
				u"session gate self-test: a shown session whose test mode "
				"disagrees with its account refuses"_q,
				ControlledReading(MixedAccounts(), 1, 1, false, 1),
				GateRequests(),
				GateCase::SelectionUnreadable);
			CheckVerdict(
				u"session gate self-test: no primary window beside an "
				"authorized domain().active() refuses"_q,
				ControlledReading(MixedAccounts(), -1, -1, false, 1),
				GateRequests(),
				GateCase::SelectionUnreadable);
			CheckVerdict(
				u"session gate self-test: a declaration does not waive an "
				"unreadable selection"_q,
				ControlledReading(MixedAccounts(), 0, -1, false, 0),
				Declaring({ ControlledLegs() }),
				GateCase::SelectionUnreadable);
			CheckVerdict(
				u"session gate self-test: an unstarted domain refuses"_q,
				SessionGateReading(),
				GateRequests(),
				GateCase::DomainNotStarted);
			CheckVerdict(
				u"session gate self-test: a declaration does not waive an "
				"unstarted domain"_q,
				SessionGateReading(),
				Declaring({ ControlledLegs() }),
				GateCase::DomainNotStarted);
		},
	});

	runner->add({
		.name = u"session gate self-test: an invalid declaration refuses "
			"without echoing its labels"_q,
		.then = [=] {
			const auto marker = u"gate-self-test-label-marker"_q;
			CheckVerdict(
				u"session gate self-test: an empty task label refuses"_q,
				RefusedReading(),
				Declaring({ { .task = u"  "_q, .legs = marker } }),
				GateCase::InvalidDeclaration,
				-1,
				std::nullopt,
				marker);
			CheckVerdict(
				u"session gate self-test: a multi-line legs label refuses"_q,
				RefusedReading(),
				Declaring({ {
					.task = marker,
					.legs = marker + QChar(0x2028) + marker,
				} }),
				GateCase::InvalidDeclaration,
				-1,
				std::nullopt,
				marker);
			CheckVerdict(
				u"session gate self-test: a repeated declaration refuses"_q,
				RefusedReading(),
				Declaring({ ControlledLegs(), ControlledLegs() }),
				GateCase::InvalidDeclaration,
				-1,
				std::nullopt,
				ControlledLegs().task);
			auto foreign = Declaring({ ControlledLegs() });
			foreign.foreignRunner = true;
			CheckVerdict(
				u"session gate self-test: a declaration for another runner "
				"refuses"_q,
				RefusedReading(),
				foreign,
				GateCase::InvalidDeclaration,
				-1,
				std::nullopt,
				ControlledLegs().task);
		},
	});

	runner->add({
		.name = u"session gate self-test: account rows carry the index, "
			"authorization, environment and window roles only"_q,
		.then = [=] {
			const auto reading = RefusedReading();
			const auto first = AccountRowText(reading.accounts[0], reading);
			const auto second = AccountRowText(reading.accounts[1], reading);
			const auto none = ControlledReading(
				{ ControlledAccount(0, false, production) },
				0,
				-1,
				false,
				0);
			const auto third = AccountRowText(none.accounts[0], none);
			Check(
				(first == u"account index=0 authorized=1 "
					"environment=Production primary=1 active=1"_q)
					&& (second == u"account index=1 authorized=1 "
						"environment=Test primary=0 active=0"_q)
					&& (third == u"account index=0 authorized=0 "
						"environment=Production primary=1 active=1"_q),
				u"session gate self-test: the account rows have the fixed "
				"format"_q,
				u"rows=[%1] [%2] [%3]"_q.arg(first, second, third));
			const auto unknown = AccountRowText(
				ControlledAccount(2, true, MTP::Environment(7)),
				reading);
			CheckVerdict(
				u"session gate self-test: an unknown environment counts as "
				"non-Test"_q,
				ControlledReading(
					{ ControlledAccount(2, true, MTP::Environment(7)) },
					2,
					2,
					false,
					2),
				GateRequests(),
				GateCase::NonTestPrimary,
				2,
				MTP::Environment(7));
			Check(
				unknown.contains(u" environment=unknown-7 "_q),
				u"session gate self-test: an unknown environment prints its "
				"value"_q,
				u"row=[%1]"_q.arg(unknown));
		},
	});

	runner->add({
		.name = u"session gate self-test: the Test-session helper decides "
			"from the reading before it acts"_q,
		.then = [=] {
			CheckPlan(
				u"session gate self-test: a coherent Test primary switches "
				"nothing"_q,
				ControlledReading(MixedAccounts(), 1, 1, true, 1),
				GateRequests(),
				std::nullopt,
				1,
				1,
				false);
			CheckPlan(
				u"session gate self-test: a Production primary selects the "
				"authorized Test account"_q,
				RefusedReading(),
				GateRequests(),
				std::nullopt,
				0,
				1,
				true);
			CheckPlan(
				u"session gate self-test: the lowest-index authorized Test "
				"account is selected"_q,
				ControlledReading(
					{
						ControlledAccount(0, false, MTP::Environment::Test),
						ControlledAccount(1, true, production),
						ControlledAccount(2, true, MTP::Environment::Test),
						ControlledAccount(3, true, MTP::Environment::Test),
					},
					1,
					1,
					false,
					1),
				GateRequests(),
				std::nullopt,
				1,
				2,
				true);
			CheckPlan(
				u"session gate self-test: no authorized Test account "
				"refuses"_q,
				ControlledReading(
					{
						ControlledAccount(0, true, production),
						ControlledAccount(1, false, MTP::Environment::Test),
					},
					0,
					0,
					false,
					0),
				GateRequests(),
				GateCase::NoTestAccount);
			CheckPlan(
				u"session gate self-test: an account-free reading has no "
				"Test account to select"_q,
				ControlledReading(
					{ ControlledAccount(0, false, production) },
					0,
					-1,
					false,
					0),
				GateRequests(),
				GateCase::NoTestAccount);
			CheckPlan(
				u"session gate self-test: a Production window beside a Test "
				"domain().active() refuses before switching"_q,
				ControlledReading(MixedAccounts(), 0, 0, false, 1),
				GateRequests(),
				GateCase::HelperSelectionSplit);
			CheckPlan(
				u"session gate self-test: a Test window beside a Production "
				"domain().active() refuses before switching"_q,
				ControlledReading(MixedAccounts(), 1, 1, true, 0),
				GateRequests(),
				GateCase::HelperSelectionSplit);
			CheckPlan(
				u"session gate self-test: an unauthorized original refuses "
				"before switching"_q,
				ControlledReading(
					{
						ControlledAccount(0, false, production),
						ControlledAccount(1, true, MTP::Environment::Test),
					},
					0,
					-1,
					false,
					0),
				GateRequests(),
				GateCase::HelperOriginalUnauthorized);
			CheckPlan(
				u"session gate self-test: the helper beside a declaration "
				"refuses"_q,
				RefusedReading(),
				Declaring({ ControlledLegs() }),
				GateCase::HelperWithDeclaration);
			auto foreign = GateRequests();
			foreign.foreignRunner = true;
			CheckPlan(
				u"session gate self-test: a helper request for another "
				"runner refuses"_q,
				RefusedReading(),
				foreign,
				GateCase::InvalidHelper);
			CheckPlan(
				u"session gate self-test: the helper on an unstarted domain "
				"refuses"_q,
				SessionGateReading(),
				GateRequests(),
				GateCase::DomainNotStarted);
		},
	});

	runner->add({
		.name = u"session gate self-test: the switch and restore "
			"read-backs"_q,
		.then = [=] {
			const auto test = MTP::Environment::Test;
			const auto proven = ControlledReading(
				MixedAccounts(),
				1,
				1,
				true,
				1);
			CheckReadBack(
				u"session gate self-test: a switch that shows the selected "
				"Test session in every reader is proven"_q,
				ShowsTestSession(proven, 1),
				true,
				proven,
				u"switch to index=1"_q);
			const auto activeOnly = ControlledReading(
				MixedAccounts(),
				0,
				0,
				false,
				1);
			CheckReadBack(
				u"session gate self-test: domain().active() changed while "
				"the window did not, so the switch is not proven"_q,
				ShowsTestSession(activeOnly, 1),
				false,
				activeOnly,
				u"switch to index=1"_q);
			const auto otherSession = ControlledReading(
				MixedAccounts(),
				1,
				0,
				false,
				1);
			CheckReadBack(
				u"session gate self-test: a window naming Test while showing "
				"the Production session is not proven"_q,
				ShowsTestSession(otherSession, 1),
				false,
				otherSession,
				u"switch to index=1"_q);
			const auto nonTestSession = ControlledReading(
				MixedAccounts(),
				1,
				1,
				false,
				1);
			CheckReadBack(
				u"session gate self-test: a shown session not in test mode "
				"is not proven"_q,
				ShowsTestSession(nonTestSession, 1),
				false,
				nonTestSession,
				u"switch to index=1"_q);
			const auto restored = RefusedReading();
			CheckReadBack(
				u"session gate self-test: a restore whose every reader shows "
				"the original again is equal"_q,
				ShowsOriginal(restored, 0, production, true),
				true,
				restored,
				u"original index=0 environment=Production alive=1"_q);
			const auto windowLeft = ControlledReading(
				MixedAccounts(),
				1,
				1,
				true,
				0);
			CheckReadBack(
				u"session gate self-test: a restore whose window still shows "
				"the Test session is not equal"_q,
				ShowsOriginal(windowLeft, 0, production, true),
				false,
				windowLeft,
				u"original index=0 environment=Production alive=1"_q);
			CheckReadBack(
				u"session gate self-test: a lost original is never equal, "
				"whatever the readers show"_q,
				ShowsOriginal(restored, 0, production, false),
				false,
				restored,
				u"original index=0 environment=Production alive=0"_q);
			CheckReadBack(
				u"session gate self-test: an original read on another "
				"environment is not equal"_q,
				ShowsOriginal(restored, 0, test, true),
				false,
				restored,
				u"original index=0 environment=Test alive=1"_q);
		},
	});
}

} // namespace Test

#endif // _DEBUG
