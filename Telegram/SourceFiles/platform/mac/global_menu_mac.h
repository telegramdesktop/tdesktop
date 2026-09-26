/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
struct MarkdownEnabledState;
} // namespace Ui

namespace Platform {

void CreateGlobalMenu();
void DestroyGlobalMenu();
void RequestUpdateGlobalMenu();

[[nodiscard]] rpl::producer<Ui::MarkdownEnabledState> GlobalMenuMarkdownState();

} // namespace Platform
