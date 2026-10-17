/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_drafts.h"
#include "data/data_types.h"
#include "ui/chat/attach/attach_prepare.h"

namespace Data {

struct ComposeStash {
	Draft draft;
	ForwardDraft forward;
	Ui::PreparedList files;

	[[nodiscard]] bool hasFiles() const {
		return !files.files.empty() || !files.filesToProcess.empty();
	}
	[[nodiscard]] bool hasText() const {
		return !draft.textWithTags.text.isEmpty() || draft.hasRichMessage();
	}
};

} // namespace Data
