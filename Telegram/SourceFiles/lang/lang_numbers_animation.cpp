/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_numbers_animation.h"

#include "lang/lang_tag.h"

namespace Lang {

Ui::StringWithNumbers ReplaceTag<Ui::StringWithNumbers>::Call(
		Ui::StringWithNumbers &&original,
		ushort tag,
		const Ui::StringWithNumbers &replacement) {
	original.offset = FindTagReplacementPosition(original.text, tag);
	if (original.offset < 0) {
		return std::move(original);
	}
	original.text = ReplaceTag<QString>::Call(
		std::move(original.text),
		tag,
		replacement.text);
	original.length = replacement.text.size();
	return std::move(original);
}

} // namespace Lang
