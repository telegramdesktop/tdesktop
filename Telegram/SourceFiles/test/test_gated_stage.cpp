/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_gated_stage.h"

#include "test/test_log.h"
#include "test/test_runner.h"

namespace Test {

void AppendGatedStageSelfTest(not_null<Runner*> runner) {
	struct State {
		crl::time armedAt = 0;
		crl::time appliedAt = 0;
		int skippedRuns = 0;
		int skippedUntilPolls = 0;
		int skippedThens = 0;
		int appliedRuns = 0;
	};
	const auto state = std::make_shared<State>();

	runner->add({
		.name = u"gated stage self-test: arm"_q,
		.then = [=] { state->armedAt = crl::now(); },
	});

	runner->add({
		.name = u"gated stage self-test: a stage whose gate reads false"_q,
		.skipReason = [] {
			return u"applies=0: the self-test gate is false by "
				"construction"_q;
		},
		.run = [=] { ++state->skippedRuns; },
		.until = [=] {
			++state->skippedUntilPolls;
			return false;
		},
		.then = [=] { ++state->skippedThens; },
		.timeout = crl::time(1000),
	});

	runner->add({
		.name = u"gated stage self-test: a stage whose gate reads true"_q,
		.skipReason = [] { return QString(); },
		.run = [=] {
			state->appliedAt = crl::now();
			++state->appliedRuns;
		},
		.until = [] { return true; },
		.then = [=] {
			Check(
				(state->appliedRuns == 1)
					&& !state->skippedRuns
					&& !state->skippedUntilPolls
					&& !state->skippedThens,
				u"gated stage self-test: the false gate skipped run, "
				"until and then and the true gate ran"_q,
				u"skippedRun=%1 skippedUntilPolls=%2 skippedThen=%3 "
				"appliedRun=%4 elapsedSinceArmMs=%5"_q
					.arg(state->skippedRuns)
					.arg(state->skippedUntilPolls)
					.arg(state->skippedThens)
					.arg(state->appliedRuns)
					.arg(state->appliedAt - state->armedAt));
		},
	});
}

} // namespace Test

#endif // _DEBUG
