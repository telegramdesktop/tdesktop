/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/deep_links/deep_links_types.h"

namespace Core::DeepLinks {

class Router final {
public:
	static Router &Instance();

	[[nodiscard]] bool tryHandle(
		Window::SessionController *controller,
		const QString &command);

	void add(const QString &section, Entry entry);

private:
	struct DispatchResult {
		Result result = Result::NotFound;
		bool skipActivation = false;
	};

	Router();

	[[nodiscard]] DispatchResult dispatch(const Context &ctx);
	[[nodiscard]] DispatchResult handleSection(
		const QString &section,
		const Context &ctx);
	[[nodiscard]] Result executeAction(
		const Action &action,
		const Context &ctx);

	void showUnsupportedMessage(
		Window::SessionController *controller,
		const QString &url);

	std::map<QString, std::vector<Entry>> _handlers;

};

} // namespace Core::DeepLinks
