/*
WARNING! All changes made in this file will be lost!
Created from 'empty' by 'codegen_emoji'

This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "emoji_config.h"

namespace Ui {
namespace Emoji {
namespace {

auto WorkingIndex = -1;

} // namespace

void Init() {
	auto scaleForEmoji = cRetina() ? dbisTwo : cScale();

	switch (scaleForEmoji) {
	case dbisOne: WorkingIndex = 0; break;
	case dbisOneAndQuarter: WorkingIndex = 1; break;
	case dbisOneAndHalf: WorkingIndex = 2; break;
	case dbisTwo: WorkingIndex = 3; break;
	};

	internal::Init();
}

int Index() {
	return WorkingIndex;
}

int One::variantsCount() const {
	return hasVariants() ? 5 : 0;
}

int One::variantIndex(EmojiPtr variant) const {
	return (variant - original());
}

EmojiPtr One::variant(int index) const {
	return (index >= 0 && index <= variantsCount()) ? (original() + index) : this;
}

int One::index() const {
	return (this - internal::ByIndex(0));
}

} // namespace Emoji
} // namespace Ui
