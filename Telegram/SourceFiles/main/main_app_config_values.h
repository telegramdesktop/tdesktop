/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace AppConfig {

[[nodiscard]] std::optional<QString> FragmentLink(not_null<Main::Session*>);

} // namespace AppConfig
