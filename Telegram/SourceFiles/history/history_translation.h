/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "spellcheck/spellcheck_types.h"

class History;

class HistoryTranslation final {
public:
	HistoryTranslation(
		not_null<History*> history,
		const LanguageId &offerFrom);

	void offerFrom(LanguageId id);
	[[nodiscard]] LanguageId offeredFrom() const;

	void translateTo(LanguageId id);
	[[nodiscard]] LanguageId translatedTo() const;

private:
	const not_null<History*> _history;

	LanguageId _offerFrom;
	LanguageId _translatedTo;

};