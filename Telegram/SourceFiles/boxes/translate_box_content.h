/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "spellcheck/platform/platform_language.h"

namespace Ui::Text {
struct MarkedContext;
} // namespace Ui::Text

namespace Ui {

enum class TranslateBoxContentError {
	None = 0,
	Unknown,
	LocalLanguagePackMissing,
};

struct TranslateBoxContentResult {
	std::optional<TextWithEntities> text;
	TranslateBoxContentError error = TranslateBoxContentError::None;
};

class GenericBox;

struct TranslateBoxContentArgs {
	TextWithEntities text;
	bool hasCopyRestriction = false;
	Text::MarkedContext textContext;
	rpl::producer<LanguageId> to;
	Fn<void()> chooseTo;
	Fn<void(LanguageId, Fn<void(TranslateBoxContentResult)>)> request;
};

void TranslateBoxContent(
	not_null<GenericBox*> box,
	TranslateBoxContentArgs &&args);

} // namespace Ui
