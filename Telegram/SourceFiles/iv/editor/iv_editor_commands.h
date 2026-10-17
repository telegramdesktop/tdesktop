/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/editor/iv_editor_state.h"

namespace Iv::Editor {

struct CommandContext {
	not_null<State*> state;
	State::ActiveEnterContext enter;
	bool caretAtStart = false;
	int targetOrdinal = -1;
};

[[nodiscard]] bool RunEnterChain(CommandContext &context);

} // namespace Iv::Editor
