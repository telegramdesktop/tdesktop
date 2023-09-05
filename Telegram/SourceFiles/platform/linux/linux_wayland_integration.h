/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Platform {
namespace internal {

class WaylandIntegration {
public:
	WaylandIntegration();
	~WaylandIntegration();

	[[nodiscard]] static WaylandIntegration *Instance();

	[[nodiscard]] bool skipTaskbarSupported();
	void skipTaskbar(QWindow *window, bool skip);

private:
	struct Private;
	const std::unique_ptr<Private> _private;
};

} // namespace internal
} // namespace Platform
