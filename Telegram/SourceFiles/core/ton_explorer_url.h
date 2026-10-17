/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Core {

[[nodiscard]] QString TonExplorerUrl(
	const QString &base,
	const QString &route);
[[nodiscard]] QString TonExplorerUrl(
	not_null<Main::Session*> session,
	const QString &route);

} // namespace Core
