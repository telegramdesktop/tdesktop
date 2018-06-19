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

};

Content ContentFromState(const ProcessingState &state);

inline auto ContentFromState(rpl::producer<State> state) {
	return rpl::single(Content()) | rpl::then(std::move(
		state
	) | rpl::filter([](const State &state) {
		return state.template is<ProcessingState>();
	}) | rpl::map([](const State &state) {
		return ContentFromState(
			state.template get_unchecked<ProcessingState>());
	}));
}

} // namespace View
} // namespace Export
