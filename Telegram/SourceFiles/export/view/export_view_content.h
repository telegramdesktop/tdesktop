/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_controller.h"

namespace Export {
struct Settings;
} // namespace Export

namespace Export {
namespace View {

struct Content {
	struct Row {
		QString id;
		QString label;
		QString info;
		float64 progress = 0.;
	};

	std::vector<Row> rows;

	static const QString kDoneId;

};

[[nodiscard]] Content ContentFromState(
	not_null<Settings*> settings,
	const ProcessingState &state);
[[nodiscard]] Content ContentFromState(const FinishedState &state);

[[nodiscard]] inline auto ContentFromState(
		not_null<Settings*> settings,
		rpl::producer<State> state) {
	return std::move(
		state
	) | rpl::filter([](const State &state) {
		return state.is<ProcessingState>() || state.is<FinishedState>();
	}) | rpl::map([=](const State &state) {
		if (const auto process = base::get_if<ProcessingState>(&state)) {
			return ContentFromState(settings, *process);
		} else if (const auto done = base::get_if<FinishedState>(&state)) {
			return ContentFromState(*done);
		}
		Unexpected("State type in ContentFromState.");
	});
}

} // namespace View
} // namespace Export
