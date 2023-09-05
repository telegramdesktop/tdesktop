/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Platform {

inline std::unique_ptr<OverlayWidgetHelper> CreateOverlayWidgetHelper(
		not_null<Ui::RpWindow*> window,
		Fn<void(bool)> maximize) {
	return std::make_unique<DefaultOverlayWidgetHelper>(
		window,
		std::move(maximize));
}

} // namespace Platform
