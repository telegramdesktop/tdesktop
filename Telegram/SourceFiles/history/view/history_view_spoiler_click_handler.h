/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace HistoryView {

class Element;

void FillTextWithAnimatedSpoilers(
	not_null<Element*> view,
	Ui::Text::String &text);

} // namespace HistoryView
