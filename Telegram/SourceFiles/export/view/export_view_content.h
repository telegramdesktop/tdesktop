/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/export_controller.h"

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

Content ContentFromState(const ProcessingState &state);
Content ContentFromState(const FinishedState &state);

inline auto ContentFromState(rpl::producer<State> state) {
	return std::move(
		state
	) | rpl::filter([](const State &state) {
		return state.is<ProcessingState>() || state.is<FinishedState>();
	}) | rpl::map([](const State &state) {
		if (const auto process = base::get_if<ProcessingState>(&state)) {
			return ContentFromState(*process);
		} else if (const auto done = base::get_if<FinishedState>(&state)) {
			return ContentFromState(*done);
		}
		Unexpected("State type in ContentFromState.");
	});
}

} // namespace View
} // namespace Export
