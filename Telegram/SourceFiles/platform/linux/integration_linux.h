/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Platform {

class Integration;

[[nodiscard]] std::unique_ptr<Integration> CreateIntegration();

} // namespace Platform
