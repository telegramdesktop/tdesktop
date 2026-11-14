/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_shared_media.h"

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Info::Media {

[[nodiscard]] bool HasVoiceFilter(Storage::SharedMediaType sectionType);

void FillVoiceFilterMenu(
	const Ui::Menu::MenuCallback &addAction,
	Storage::SharedMediaType sourceType,
	Fn<void(Storage::SharedMediaType)> apply);

} // namespace Info::Media
