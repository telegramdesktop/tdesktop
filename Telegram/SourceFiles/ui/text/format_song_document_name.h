/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/text/format_song_name.h"

class DocumentData;

namespace Ui::Text {

[[nodiscard]] FormatSongName FormatSongNameFor(
	not_null<DocumentData*> document);

[[nodiscard]] TextWithEntities FormatDownloadsName(
	not_null<DocumentData*> document);

} // namespace Ui::Text
