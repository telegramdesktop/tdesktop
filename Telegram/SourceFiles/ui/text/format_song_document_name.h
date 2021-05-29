/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/format_song_name.h"

class DocumentData;

namespace Ui::Text {

[[nodiscard]] FormatSongName FormatSongNameFor(
	not_null<DocumentData*> document);

} // namespace Ui::Text
