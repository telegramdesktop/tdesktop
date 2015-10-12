/*
Created from emoji config by '/MetaEmoji' project

WARNING! All changes made in this file will be lost!

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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "gui/emoji_config.h"

namespace {
	EmojiData *emojis = 0;
	char emojisData[sizeof(EmojiData) * 1180];
}

int EmojiSizes[] = { 18, 22, 27, 36, 45 }, EIndex = -1, ESize = 0;
const char *EmojiNames[] = { ":/gui/art/emoji.webp", ":/gui/art/emoji_125x.webp", ":/gui/art/emoji_150x.webp", ":/gui/art/emoji_200x.webp", ":/gui/art/emoji_250x.webp" }, *EName = 0;
void emojiInit() {
	DBIScale emojiForScale = cRetina() ? dbisTwo : cScale();

	switch (emojiForScale) {
		case dbisOne: EIndex = 0; break;
		case dbisOneAndQuarter: EIndex = 1; break;
		case dbisOneAndHalf: EIndex = 2; break;
		case dbisTwo: EIndex = 3; break;
	};
	ESize = EmojiSizes[EIndex];
	EName = EmojiNames[EIndex];

	EmojiData *toFill = emojis = (EmojiData*)emojisData;

	new (toFill++) EmojiData(18, 27, 0xA9U, 0, 1, 0, 0);
	new (toFill++) EmojiData(19, 27, 0xAEU, 0, 1, 0, 0);
	new (toFill++) EmojiData(29, 27, 0x203CU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(30, 27, 0x2049U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(17, 27, 0x2122U, 0, 1, 0, 0);
	new (toFill++) EmojiData(6, 27, 0x2139U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(21, 26, 0x2194U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(20, 26, 0x2195U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(19, 26, 0x2196U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(16, 26, 0x2197U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(17, 26, 0x2198U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(18, 26, 0x2199U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(24, 26, 0x21A9U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(23, 26, 0x21AAU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(35, 20, 0x231AU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(1, 21, 0x231BU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(8, 26, 0x23E9U, 0, 1, 0, 0);
	new (toFill++) EmojiData(9, 26, 0x23EAU, 0, 1, 0, 0);
	new (toFill++) EmojiData(10, 26, 0x23EBU, 0, 1, 0, 0);
	new (toFill++) EmojiData(11, 26, 0x23ECU, 0, 1, 0, 0);
	new (toFill++) EmojiData(39, 20, 0x23F0U, 0, 1, 0, 0);
	new (toFill++) EmojiData(0, 21, 0x23F3U, 0, 1, 0, 0);
	new (toFill++) EmojiData(0, 28, 0x24C2U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(26, 28, 0x25AAU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(27, 28, 0x25ABU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(4, 26, 0x25B6U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(5, 26, 0x25C0U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(31, 28, 0x25FBU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(30, 28, 0x25FCU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(33, 28, 0x25FDU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(32, 28, 0x25FEU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(37, 11, 0x2600U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(39, 11, 0x2601U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(9, 21, 0x260EU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(14, 28, 0x2611U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(2, 12, 0x2614U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(3, 14, 0x2615U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(30, 7, 0x261DU, 0, 1, 0xFE0F, 0xFFFF0355U);
	new (toFill++) EmojiData(12, 0, 0x263AU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(24, 25, 0x2648U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(25, 25, 0x2649U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(26, 25, 0x264AU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(27, 25, 0x264BU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(28, 25, 0x264CU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(29, 25, 0x264DU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(30, 25, 0x264EU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(31, 25, 0x264FU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(32, 25, 0x2650U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(33, 25, 0x2651U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(34, 25, 0x2652U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(35, 25, 0x2653U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(10, 28, 0x2660U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(11, 28, 0x2663U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(12, 28, 0x2665U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(13, 28, 0x2666U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(6, 28, 0x2668U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(7, 28, 0x267BU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(0, 26, 0x267FU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(32, 18, 0x2693U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(5, 28, 0x26A0U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(34, 11, 0x26A1U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(15, 28, 0x26AAU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(16, 28, 0x26ABU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(35, 16, 0x26BDU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(38, 16, 0x26BEU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(14, 16, 0x26C4U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(38, 11, 0x26C5U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(1, 28, 0x26CEU, 0, 1, 0, 0);
	new (toFill++) EmojiData(13, 24, 0x26D4U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(30, 19, 0x26EAU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(11, 19, 0x26F2U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(1, 17, 0x26F3U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(35, 18, 0x26F5U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(33, 16, 0x26FAU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(24, 18, 0x26FDU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(24, 23, 0x2702U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(3, 25, 0x2705U, 0, 1, 0, 0);
	new (toFill++) EmojiData(30, 18, 0x2708U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(26, 22, 0x2709U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(38, 8, 0x270AU, 0, 1, 0, 0xFFFF035AU);
	new (toFill++) EmojiData(4, 9, 0x270BU, 0, 1, 0, 0xFFFF035FU);
	new (toFill++) EmojiData(26, 8, 0x270CU, 0, 1, 0xFE0F, 0xFFFF0364U);
	new (toFill++) EmojiData(32, 23, 0x270FU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(31, 23, 0x2712U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(15, 27, 0x2714U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(14, 27, 0x2716U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(25, 14, 0x2728U, 0, 1, 0, 0);
	new (toFill++) EmojiData(1, 25, 0x2733U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(4, 25, 0x2734U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(4, 12, 0x2744U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(0, 25, 0x2747U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(31, 27, 0x274CU, 0, 1, 0, 0);
	new (toFill++) EmojiData(2, 25, 0x274EU, 0, 1, 0, 0);
	new (toFill++) EmojiData(26, 27, 0x2753U, 0, 1, 0, 0);
	new (toFill++) EmojiData(28, 27, 0x2754U, 0, 1, 0, 0);
	new (toFill++) EmojiData(27, 27, 0x2755U, 0, 1, 0, 0);
	new (toFill++) EmojiData(25, 27, 0x2757U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(35, 14, 0x2764U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(10, 27, 0x2795U, 0, 1, 0, 0);
	new (toFill++) EmojiData(11, 27, 0x2796U, 0, 1, 0, 0);
	new (toFill++) EmojiData(13, 27, 0x2797U, 0, 1, 0, 0);
	new (toFill++) EmojiData(12, 26, 0x27A1U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(22, 27, 0x27B0U, 0, 1, 0, 0);
	new (toFill++) EmojiData(23, 27, 0x27BFU, 0, 1, 0, 0);
	new (toFill++) EmojiData(25, 26, 0x2934U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(26, 26, 0x2935U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(13, 26, 0x2B05U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(14, 26, 0x2B06U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(15, 26, 0x2B07U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(28, 28, 0x2B1BU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(29, 28, 0x2B1CU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(6, 12, 0x2B50U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(32, 27, 0x2B55U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(12, 27, 0x3030U, 0, 1, 0, 0);
	new (toFill++) EmojiData(24, 27, 0x303DU, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(25, 24, 0x3297U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(24, 24, 0x3299U, 0, 1, 0xFE0F, 0);
	new (toFill++) EmojiData(30, 26, 0x2320E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 26, 0x3020E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 26, 0x3120E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 26, 0x3220E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 26, 0x3320E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 26, 0x3420E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 26, 0x3520E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 26, 0x3620E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 26, 0x3720E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 26, 0x3820E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 27, 0x3920E3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 17, 0xD83CDC04U, 0, 2, 0xFE0F, 0);
	new (toFill++) EmojiData(28, 17, 0xD83CDCCFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 25, 0xD83CDD70U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 25, 0xD83CDD71U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 25, 0xD83CDD7EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 25, 0xD83CDD7FU, 0, 2, 0xFE0F, 0);
	new (toFill++) EmojiData(10, 25, 0xD83CDD8EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 25, 0xD83CDD91U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 25, 0xD83CDD92U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 25, 0xD83CDD93U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 25, 0xD83CDD94U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 25, 0xD83CDD95U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 25, 0xD83CDD96U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 25, 0xD83CDD97U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 25, 0xD83CDD98U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 25, 0xD83CDD99U, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 25, 0xD83CDD9AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 24, 0xD83CDE01U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 24, 0xD83CDE02U, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 24, 0xD83CDE1AU, 0, 2, 0xFE0F, 0);
	new (toFill++) EmojiData(38, 24, 0xD83CDE2FU, 0, 2, 0xFE0F, 0);
	new (toFill++) EmojiData(28, 24, 0xD83CDE32U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 24, 0xD83CDE33U, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 24, 0xD83CDE34U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 24, 0xD83CDE35U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 24, 0xD83CDE36U, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 24, 0xD83CDE37U, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 24, 0xD83CDE38U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 24, 0xD83CDE39U, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 24, 0xD83CDE3AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 24, 0xD83CDE50U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 24, 0xD83CDE51U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 27, 0xD83CDF00U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 19, 0xD83CDF01U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 21, 0xD83CDF02U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 19, 0xD83CDF03U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 12, 0xD83CDF04U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 12, 0xD83CDF05U, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 19, 0xD83CDF06U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 19, 0xD83CDF07U, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 12, 0xD83CDF08U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 19, 0xD83CDF09U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 12, 0xD83CDF0AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 12, 0xD83CDF0BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 12, 0xD83CDF0CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 12, 0xD83CDF0DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 12, 0xD83CDF0EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 12, 0xD83CDF0FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 12, 0xD83CDF10U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 12, 0xD83CDF11U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 12, 0xD83CDF12U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 12, 0xD83CDF13U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 12, 0xD83CDF14U, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 12, 0xD83CDF15U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 12, 0xD83CDF16U, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 12, 0xD83CDF17U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 12, 0xD83CDF18U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 11, 0xD83CDF19U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 12, 0xD83CDF1AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 12, 0xD83CDF1BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 12, 0xD83CDF1CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 12, 0xD83CDF1DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 12, 0xD83CDF1EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 12, 0xD83CDF1FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 12, 0xD83CDF20U, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 10, 0xD83CDF30U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 9, 0xD83CDF31U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 9, 0xD83CDF32U, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 9, 0xD83CDF33U, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 9, 0xD83CDF34U, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 9, 0xD83CDF35U, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 9, 0xD83CDF37U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 9, 0xD83CDF38U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 9, 0xD83CDF39U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 9, 0xD83CDF3AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 9, 0xD83CDF3BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 9, 0xD83CDF3CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 12, 0xD83CDF3DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 10, 0xD83CDF3EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 10, 0xD83CDF3FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 10, 0xD83CDF40U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 10, 0xD83CDF41U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 10, 0xD83CDF42U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 10, 0xD83CDF43U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 10, 0xD83CDF44U, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 12, 0xD83CDF45U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 12, 0xD83CDF46U, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 12, 0xD83CDF47U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 12, 0xD83CDF48U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 12, 0xD83CDF49U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 13, 0xD83CDF4AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 13, 0xD83CDF4BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 13, 0xD83CDF4CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 13, 0xD83CDF4DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 13, 0xD83CDF4EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 13, 0xD83CDF4FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 13, 0xD83CDF50U, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 13, 0xD83CDF51U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 13, 0xD83CDF52U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 13, 0xD83CDF53U, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 13, 0xD83CDF54U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 13, 0xD83CDF55U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 13, 0xD83CDF56U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 13, 0xD83CDF57U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 13, 0xD83CDF58U, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 13, 0xD83CDF59U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 13, 0xD83CDF5AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 13, 0xD83CDF5BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 13, 0xD83CDF5CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 13, 0xD83CDF5DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 13, 0xD83CDF5EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 13, 0xD83CDF5FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 12, 0xD83CDF60U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 13, 0xD83CDF61U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 13, 0xD83CDF62U, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 13, 0xD83CDF63U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 13, 0xD83CDF64U, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 13, 0xD83CDF65U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 13, 0xD83CDF66U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 13, 0xD83CDF67U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 13, 0xD83CDF68U, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 13, 0xD83CDF69U, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 13, 0xD83CDF6AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 13, 0xD83CDF6BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 13, 0xD83CDF6CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 13, 0xD83CDF6DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 13, 0xD83CDF6EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 13, 0xD83CDF6FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 13, 0xD83CDF70U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 13, 0xD83CDF71U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 13, 0xD83CDF72U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 14, 0xD83CDF73U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 14, 0xD83CDF74U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 14, 0xD83CDF75U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 14, 0xD83CDF76U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 14, 0xD83CDF77U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 14, 0xD83CDF78U, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 14, 0xD83CDF79U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 14, 0xD83CDF7AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 14, 0xD83CDF7BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 14, 0xD83CDF7CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 14, 0xD83CDF80U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 14, 0xD83CDF81U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 14, 0xD83CDF82U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 14, 0xD83CDF83U, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 14, 0xD83CDF84U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 4, 0xD83CDF85U, 0, 2, 0, 0xFFFF0393U);
	new (toFill++) EmojiData(19, 14, 0xD83CDF86U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 14, 0xD83CDF87U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 14, 0xD83CDF88U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 14, 0xD83CDF89U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 14, 0xD83CDF8AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 14, 0xD83CDF8BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 14, 0xD83CDF8CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 14, 0xD83CDF8DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 14, 0xD83CDF8EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 14, 0xD83CDF8FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 14, 0xD83CDF90U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 14, 0xD83CDF91U, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 21, 0xD83CDF92U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 14, 0xD83CDF93U, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 17, 0xD83CDFA0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 17, 0xD83CDFA1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 17, 0xD83CDFA2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 16, 0xD83CDFA3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 17, 0xD83CDFA4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 21, 0xD83CDFA5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 27, 0xD83CDFA6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 17, 0xD83CDFA7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 17, 0xD83CDFA8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 17, 0xD83CDFA9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 17, 0xD83CDFAAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 17, 0xD83CDFABU, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 17, 0xD83CDFACU, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 17, 0xD83CDFADU, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 17, 0xD83CDFAEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 17, 0xD83CDFAFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 17, 0xD83CDFB0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 17, 0xD83CDFB1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 17, 0xD83CDFB2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 17, 0xD83CDFB3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 17, 0xD83CDFB4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 17, 0xD83CDFB5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 17, 0xD83CDFB6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 17, 0xD83CDFB7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 17, 0xD83CDFB8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 17, 0xD83CDFB9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 17, 0xD83CDFBAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 17, 0xD83CDFBBU, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 17, 0xD83CDFBCU, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 17, 0xD83CDFBDU, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 16, 0xD83CDFBEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 16, 0xD83CDFBFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 16, 0xD83CDFC0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 17, 0xD83CDFC1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 16, 0xD83CDFC2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 15, 0xD83CDFC3U, 0, 2, 0, 0xFFFF0398U);
	new (toFill++) EmojiData(0, 16, 0xD83CDFC4U, 0, 2, 0, 0xFFFF039DU);
	new (toFill++) EmojiData(2, 17, 0xD83CDFC6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 16, 0xD83CDFC7U, 0, 2, 0, 0xFFFF03A2U);
	new (toFill++) EmojiData(37, 16, 0xD83CDFC8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 17, 0xD83CDFC9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 15, 0xD83CDFCAU, 0, 2, 0, 0xFFFF03A7U);
	new (toFill++) EmojiData(18, 19, 0xD83CDFE0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 19, 0xD83CDFE1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 19, 0xD83CDFE2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 19, 0xD83CDFE3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 19, 0xD83CDFE4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 19, 0xD83CDFE5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 19, 0xD83CDFE6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 25, 0xD83CDFE7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 19, 0xD83CDFE8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 19, 0xD83CDFE9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 19, 0xD83CDFEAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 19, 0xD83CDFEBU, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 19, 0xD83CDFECU, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 19, 0xD83CDFEDU, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 14, 0xD83CDFEEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 19, 0xD83CDFEFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 19, 0xD83CDFF0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 10, 0xD83DDC00U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 10, 0xD83DDC01U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 10, 0xD83DDC02U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 10, 0xD83DDC03U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 10, 0xD83DDC04U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 10, 0xD83DDC05U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 10, 0xD83DDC06U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 10, 0xD83DDC07U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 10, 0xD83DDC08U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 11, 0xD83DDC09U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 11, 0xD83DDC0AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 11, 0xD83DDC0BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 11, 0xD83DDC0CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 11, 0xD83DDC0DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 10, 0xD83DDC0EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 10, 0xD83DDC0FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 10, 0xD83DDC10U, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 10, 0xD83DDC11U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 11, 0xD83DDC12U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 10, 0xD83DDC13U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 10, 0xD83DDC14U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 11, 0xD83DDC15U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 10, 0xD83DDC16U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 10, 0xD83DDC17U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 10, 0xD83DDC18U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 11, 0xD83DDC19U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 11, 0xD83DDC1AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 11, 0xD83DDC1BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 11, 0xD83DDC1CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 11, 0xD83DDC1DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 11, 0xD83DDC1EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 11, 0xD83DDC1FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 11, 0xD83DDC20U, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 11, 0xD83DDC21U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 11, 0xD83DDC22U, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 10, 0xD83DDC23U, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 10, 0xD83DDC24U, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 10, 0xD83DDC25U, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 10, 0xD83DDC26U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 10, 0xD83DDC27U, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 11, 0xD83DDC28U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 11, 0xD83DDC29U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 10, 0xD83DDC2AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 10, 0xD83DDC2BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 11, 0xD83DDC2CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 10, 0xD83DDC2DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 10, 0xD83DDC2EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 10, 0xD83DDC2FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 10, 0xD83DDC30U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 10, 0xD83DDC31U, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 11, 0xD83DDC32U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 11, 0xD83DDC33U, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 10, 0xD83DDC34U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 11, 0xD83DDC35U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 11, 0xD83DDC36U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 11, 0xD83DDC37U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 11, 0xD83DDC38U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 10, 0xD83DDC39U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 11, 0xD83DDC3AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 11, 0xD83DDC3BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 11, 0xD83DDC3CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 11, 0xD83DDC3DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 11, 0xD83DDC3EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 6, 0xD83DDC40U, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 6, 0xD83DDC42U, 0, 2, 0, 0xFFFF03ACU);
	new (toFill++) EmojiData(37, 6, 0xD83DDC43U, 0, 2, 0, 0xFFFF03B1U);
	new (toFill++) EmojiData(3, 7, 0xD83DDC44U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 7, 0xD83DDC45U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 7, 0xD83DDC46U, 0, 2, 0, 0xFFFF03B6U);
	new (toFill++) EmojiData(2, 8, 0xD83DDC47U, 0, 2, 0, 0xFFFF03BBU);
	new (toFill++) EmojiData(8, 8, 0xD83DDC48U, 0, 2, 0, 0xFFFF03C0U);
	new (toFill++) EmojiData(14, 8, 0xD83DDC49U, 0, 2, 0, 0xFFFF03C5U);
	new (toFill++) EmojiData(32, 8, 0xD83DDC4AU, 0, 2, 0, 0xFFFF03CAU);
	new (toFill++) EmojiData(12, 7, 0xD83DDC4BU, 0, 2, 0, 0xFFFF03CFU);
	new (toFill++) EmojiData(20, 8, 0xD83DDC4CU, 0, 2, 0, 0xFFFF03D4U);
	new (toFill++) EmojiData(18, 7, 0xD83DDC4DU, 0, 2, 0, 0xFFFF03D9U);
	new (toFill++) EmojiData(24, 7, 0xD83DDC4EU, 0, 2, 0, 0xFFFF03DEU);
	new (toFill++) EmojiData(24, 6, 0xD83DDC4FU, 0, 2, 0, 0xFFFF03E3U);
	new (toFill++) EmojiData(16, 9, 0xD83DDC50U, 0, 2, 0, 0xFFFF03E8U);
	new (toFill++) EmojiData(28, 14, 0xD83DDC51U, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 21, 0xD83DDC52U, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 21, 0xD83DDC53U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 22, 0xD83DDC54U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 22, 0xD83DDC55U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 22, 0xD83DDC56U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 22, 0xD83DDC57U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 22, 0xD83DDC58U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 21, 0xD83DDC59U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 22, 0xD83DDC5AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 21, 0xD83DDC5BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 21, 0xD83DDC5CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 21, 0xD83DDC5DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 21, 0xD83DDC5EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 21, 0xD83DDC5FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 21, 0xD83DDC60U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 21, 0xD83DDC61U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 21, 0xD83DDC62U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 1, 0xD83DDC63U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 1, 0xD83DDC64U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 1, 0xD83DDC65U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 1, 0xD83DDC66U, 0, 2, 0, 0xFFFF03EDU);
	new (toFill++) EmojiData(2, 2, 0xD83DDC67U, 0, 2, 0, 0xFFFF03F2U);
	new (toFill++) EmojiData(8, 2, 0xD83DDC68U, 0, 2, 0, 0xFFFF03F7U);
	new (toFill++) EmojiData(14, 2, 0xD83DDC69U, 0, 2, 0, 0xFFFF03FCU);
	new (toFill++) EmojiData(20, 2, 0xD83DDC6AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 2, 0xD83DDC6BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 2, 0xD83DDC6CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 2, 0xD83DDC6DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 3, 0xD83DDC6EU, 0, 2, 0, 0xFFFF0401U);
	new (toFill++) EmojiData(38, 2, 0xD83DDC6FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 2, 0xD83DDC70U, 0, 2, 0, 0xFFFF0406U);
	new (toFill++) EmojiData(5, 3, 0xD83DDC71U, 0, 2, 0, 0xFFFF040BU);
	new (toFill++) EmojiData(11, 3, 0xD83DDC72U, 0, 2, 0, 0xFFFF0410U);
	new (toFill++) EmojiData(17, 3, 0xD83DDC73U, 0, 2, 0, 0xFFFF0415U);
	new (toFill++) EmojiData(23, 3, 0xD83DDC74U, 0, 2, 0, 0xFFFF041AU);
	new (toFill++) EmojiData(29, 3, 0xD83DDC75U, 0, 2, 0, 0xFFFF041FU);
	new (toFill++) EmojiData(30, 1, 0xD83DDC76U, 0, 2, 0, 0xFFFF0424U);
	new (toFill++) EmojiData(1, 4, 0xD83DDC77U, 0, 2, 0, 0xFFFF0429U);
	new (toFill++) EmojiData(7, 4, 0xD83DDC78U, 0, 2, 0, 0xFFFF042EU);
	new (toFill++) EmojiData(32, 4, 0xD83DDC79U, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 4, 0xD83DDC7AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 4, 0xD83DDC7BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 4, 0xD83DDC7CU, 0, 2, 0, 0xFFFF0433U);
	new (toFill++) EmojiData(36, 4, 0xD83DDC7DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 4, 0xD83DDC7EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 0, 0xD83DDC7FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 4, 0xD83DDC80U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 5, 0xD83DDC81U, 0, 2, 0, 0xFFFF0438U);
	new (toFill++) EmojiData(13, 4, 0xD83DDC82U, 0, 2, 0, 0xFFFF043DU);
	new (toFill++) EmojiData(22, 15, 0xD83DDC83U, 0, 2, 0, 0xFFFF0442U);
	new (toFill++) EmojiData(31, 21, 0xD83DDC84U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 7, 0xD83DDC85U, 0, 2, 0, 0xFFFF0447U);
	new (toFill++) EmojiData(0, 6, 0xD83DDC86U, 0, 2, 0, 0xFFFF044CU);
	new (toFill++) EmojiData(6, 6, 0xD83DDC87U, 0, 2, 0, 0xFFFF0451U);
	new (toFill++) EmojiData(10, 22, 0xD83DDC88U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 22, 0xD83DDC89U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 22, 0xD83DDC8AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 7, 0xD83DDC8BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 14, 0xD83DDC8CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 14, 0xD83DDC8DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 21, 0xD83DDC8EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 6, 0xD83DDC8FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 9, 0xD83DDC90U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 6, 0xD83DDC91U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 19, 0xD83DDC92U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 15, 0xD83DDC93U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 14, 0xD83DDC94U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 14, 0xD83DDC95U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 15, 0xD83DDC96U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 15, 0xD83DDC97U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 15, 0xD83DDC98U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 15, 0xD83DDC99U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 15, 0xD83DDC9AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 15, 0xD83DDC9BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 15, 0xD83DDC9CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 15, 0xD83DDC9DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 14, 0xD83DDC9EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 15, 0xD83DDC9FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 28, 0xD83DDCA0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 21, 0xD83DDCA1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 28, 0xD83DDCA2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 22, 0xD83DDCA3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 24, 0xD83DDCA4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 14, 0xD83DDCA5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 12, 0xD83DDCA6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 12, 0xD83DDCA7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 12, 0xD83DDCA8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 4, 0xD83DDCA9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 9, 0xD83DDCAAU, 0, 2, 0, 0xFFFF0456U);
	new (toFill++) EmojiData(24, 14, 0xD83DDCABU, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 24, 0xD83DDCACU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 24, 0xD83DDCADU, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 24, 0xD83DDCAEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 27, 0xD83DDCAFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 21, 0xD83DDCB0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 27, 0xD83DDCB1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 27, 0xD83DDCB2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 21, 0xD83DDCB3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 19, 0xD83DDCB4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 19, 0xD83DDCB5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 19, 0xD83DDCB6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 19, 0xD83DDCB7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 21, 0xD83DDCB8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 24, 0xD83DDCB9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 18, 0xD83DDCBAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 20, 0xD83DDCBBU, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 21, 0xD83DDCBCU, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 21, 0xD83DDCBDU, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 21, 0xD83DDCBEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 21, 0xD83DDCBFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 21, 0xD83DDCC0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 23, 0xD83DDCC1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 23, 0xD83DDCC2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 23, 0xD83DDCC3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 22, 0xD83DDCC4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 23, 0xD83DDCC5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 23, 0xD83DDCC6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 23, 0xD83DDCC7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 23, 0xD83DDCC8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 23, 0xD83DDCC9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 23, 0xD83DDCCAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 23, 0xD83DDCCBU, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 23, 0xD83DDCCCU, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 23, 0xD83DDCCDU, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 23, 0xD83DDCCEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 23, 0xD83DDCCFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 23, 0xD83DDCD0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 23, 0xD83DDCD1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 23, 0xD83DDCD2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 23, 0xD83DDCD3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 23, 0xD83DDCD4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 23, 0xD83DDCD5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 23, 0xD83DDCD6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 23, 0xD83DDCD7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 23, 0xD83DDCD8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 23, 0xD83DDCD9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 23, 0xD83DDCDAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 24, 0xD83DDCDBU, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 23, 0xD83DDCDCU, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 23, 0xD83DDCDDU, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 21, 0xD83DDCDEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 21, 0xD83DDCDFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 21, 0xD83DDCE0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 21, 0xD83DDCE1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 23, 0xD83DDCE2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 23, 0xD83DDCE3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 22, 0xD83DDCE4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 22, 0xD83DDCE5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 22, 0xD83DDCE6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 22, 0xD83DDCE7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 22, 0xD83DDCE8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 22, 0xD83DDCE9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 22, 0xD83DDCEAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 22, 0xD83DDCEBU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 22, 0xD83DDCECU, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 22, 0xD83DDCEDU, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 22, 0xD83DDCEEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 22, 0xD83DDCEFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 22, 0xD83DDCF0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 20, 0xD83DDCF1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 20, 0xD83DDCF2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 25, 0xD83DDCF3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 25, 0xD83DDCF4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 24, 0xD83DDCF5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 27, 0xD83DDCF6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 21, 0xD83DDCF7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 21, 0xD83DDCF9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 21, 0xD83DDCFAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 21, 0xD83DDCFBU, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 21, 0xD83DDCFCU, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 26, 0xD83DDD00U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 26, 0xD83DDD01U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 26, 0xD83DDD02U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 27, 0xD83DDD03U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 26, 0xD83DDD04U, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 23, 0xD83DDD05U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 23, 0xD83DDD06U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 24, 0xD83DDD07U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 24, 0xD83DDD08U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 24, 0xD83DDD09U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 24, 0xD83DDD0AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 21, 0xD83DDD0BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 21, 0xD83DDD0CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 24, 0xD83DDD0DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 24, 0xD83DDD0EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 23, 0xD83DDD0FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 23, 0xD83DDD10U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 22, 0xD83DDD11U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 23, 0xD83DDD12U, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 23, 0xD83DDD13U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 24, 0xD83DDD14U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 24, 0xD83DDD15U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 22, 0xD83DDD16U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 23, 0xD83DDD17U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 28, 0xD83DDD18U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 27, 0xD83DDD19U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 27, 0xD83DDD1AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 27, 0xD83DDD1BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 27, 0xD83DDD1CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 27, 0xD83DDD1DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 24, 0xD83DDD1EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 27, 0xD83DDD1FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 27, 0xD83DDD20U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 27, 0xD83DDD21U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 27, 0xD83DDD22U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 27, 0xD83DDD23U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 27, 0xD83DDD24U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 11, 0xD83DDD25U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 21, 0xD83DDD26U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 22, 0xD83DDD27U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 22, 0xD83DDD28U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 22, 0xD83DDD29U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 22, 0xD83DDD2AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 22, 0xD83DDD2BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 22, 0xD83DDD2CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 22, 0xD83DDD2DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 22, 0xD83DDD2EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 28, 0xD83DDD2FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 28, 0xD83DDD30U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 28, 0xD83DDD31U, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 28, 0xD83DDD32U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 28, 0xD83DDD33U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 28, 0xD83DDD34U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 28, 0xD83DDD35U, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 28, 0xD83DDD36U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 28, 0xD83DDD37U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 28, 0xD83DDD38U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 28, 0xD83DDD39U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 28, 0xD83DDD3AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 28, 0xD83DDD3BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 26, 0xD83DDD3CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 26, 0xD83DDD3DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 28, 0xD83DDD50U, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 28, 0xD83DDD51U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 28, 0xD83DDD52U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 28, 0xD83DDD53U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 29, 0xD83DDD54U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 29, 0xD83DDD55U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 29, 0xD83DDD56U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 29, 0xD83DDD57U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 29, 0xD83DDD58U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 29, 0xD83DDD59U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 29, 0xD83DDD5AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 29, 0xD83DDD5BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 29, 0xD83DDD5CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 29, 0xD83DDD5DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 29, 0xD83DDD5EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 29, 0xD83DDD5FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 29, 0xD83DDD60U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 29, 0xD83DDD61U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 29, 0xD83DDD62U, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 29, 0xD83DDD63U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 29, 0xD83DDD64U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 29, 0xD83DDD65U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 29, 0xD83DDD66U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 29, 0xD83DDD67U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 12, 0xD83DDDFBU, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 19, 0xD83DDDFCU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 19, 0xD83DDDFDU, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 12, 0xD83DDDFEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 19, 0xD83DDDFFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 0, 0xD83DDE00U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 0, 0xD83DDE01U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 0, 0xD83DDE02U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 0, 0xD83DDE03U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 0, 0xD83DDE04U, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 0, 0xD83DDE05U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 0, 0xD83DDE06U, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 0, 0xD83DDE07U, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 0, 0xD83DDE08U, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 0, 0xD83DDE09U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 0, 0xD83DDE0AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 0, 0xD83DDE0BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 0, 0xD83DDE0CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 0, 0xD83DDE0DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 0, 0xD83DDE0EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 0, 0xD83DDE0FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 0, 0xD83DDE10U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 0, 0xD83DDE11U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 0, 0xD83DDE12U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 0, 0xD83DDE13U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 0, 0xD83DDE14U, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 0, 0xD83DDE15U, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 0, 0xD83DDE16U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 0, 0xD83DDE17U, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 0, 0xD83DDE18U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 0, 0xD83DDE19U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 0, 0xD83DDE1AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 0, 0xD83DDE1BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(30, 0, 0xD83DDE1CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(31, 0, 0xD83DDE1DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(32, 0, 0xD83DDE1EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 0, 0xD83DDE1FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 0, 0xD83DDE20U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 0, 0xD83DDE21U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 0, 0xD83DDE22U, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 0, 0xD83DDE23U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 0, 0xD83DDE24U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 0, 0xD83DDE25U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 1, 0xD83DDE26U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 1, 0xD83DDE27U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 1, 0xD83DDE28U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 1, 0xD83DDE29U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 1, 0xD83DDE2AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 1, 0xD83DDE2BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 1, 0xD83DDE2CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 1, 0xD83DDE2DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 1, 0xD83DDE2EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 1, 0xD83DDE2FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 1, 0xD83DDE30U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 1, 0xD83DDE31U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 1, 0xD83DDE32U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 1, 0xD83DDE33U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 1, 0xD83DDE34U, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 1, 0xD83DDE35U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 1, 0xD83DDE36U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 1, 0xD83DDE37U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 1, 0xD83DDE38U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 1, 0xD83DDE39U, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 1, 0xD83DDE3AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 1, 0xD83DDE3BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 1, 0xD83DDE3CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 1, 0xD83DDE3DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(24, 1, 0xD83DDE3EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 1, 0xD83DDE3FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 1, 0xD83DDE40U, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 5, 0xD83DDE45U, 0, 2, 0, 0xFFFF045BU);
	new (toFill++) EmojiData(16, 5, 0xD83DDE46U, 0, 2, 0, 0xFFFF0460U);
	new (toFill++) EmojiData(38, 4, 0xD83DDE47U, 0, 2, 0, 0xFFFF0465U);
	new (toFill++) EmojiData(10, 11, 0xD83DDE48U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 11, 0xD83DDE49U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 11, 0xD83DDE4AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 5, 0xD83DDE4BU, 0, 2, 0, 0xFFFF046AU);
	new (toFill++) EmojiData(18, 6, 0xD83DDE4CU, 0, 2, 0, 0xFFFF046FU);
	new (toFill++) EmojiData(34, 5, 0xD83DDE4DU, 0, 2, 0, 0xFFFF0474U);
	new (toFill++) EmojiData(28, 5, 0xD83DDE4EU, 0, 2, 0, 0xFFFF0479U);
	new (toFill++) EmojiData(22, 9, 0xD83DDE4FU, 0, 2, 0, 0xFFFF047EU);
	new (toFill++) EmojiData(28, 18, 0xD83DDE80U, 0, 2, 0, 0);
	new (toFill++) EmojiData(29, 18, 0xD83DDE81U, 0, 2, 0, 0);
	new (toFill++) EmojiData(35, 17, 0xD83DDE82U, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 17, 0xD83DDE83U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 17, 0xD83DDE84U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 17, 0xD83DDE85U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 18, 0xD83DDE86U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 18, 0xD83DDE87U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 18, 0xD83DDE88U, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 18, 0xD83DDE89U, 0, 2, 0, 0);
	new (toFill++) EmojiData(4, 18, 0xD83DDE8AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 17, 0xD83DDE8BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(5, 18, 0xD83DDE8CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 18, 0xD83DDE8DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 18, 0xD83DDE8EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(23, 18, 0xD83DDE8FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(8, 18, 0xD83DDE90U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 18, 0xD83DDE91U, 0, 2, 0, 0);
	new (toFill++) EmojiData(10, 18, 0xD83DDE92U, 0, 2, 0, 0);
	new (toFill++) EmojiData(11, 18, 0xD83DDE93U, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 18, 0xD83DDE94U, 0, 2, 0, 0);
	new (toFill++) EmojiData(14, 18, 0xD83DDE95U, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 18, 0xD83DDE96U, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 18, 0xD83DDE97U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 18, 0xD83DDE98U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 18, 0xD83DDE99U, 0, 2, 0, 0);
	new (toFill++) EmojiData(19, 18, 0xD83DDE9AU, 0, 2, 0, 0);
	new (toFill++) EmojiData(20, 18, 0xD83DDE9BU, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 18, 0xD83DDE9CU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 17, 0xD83DDE9DU, 0, 2, 0, 0);
	new (toFill++) EmojiData(34, 17, 0xD83DDE9EU, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 18, 0xD83DDE9FU, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 18, 0xD83DDEA0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 18, 0xD83DDEA1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(33, 18, 0xD83DDEA2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 15, 0xD83DDEA3U, 0, 2, 0, 0xFFFF0483U);
	new (toFill++) EmojiData(34, 18, 0xD83DDEA4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(27, 18, 0xD83DDEA5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(26, 18, 0xD83DDEA6U, 0, 2, 0, 0);
	new (toFill++) EmojiData(25, 18, 0xD83DDEA7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(13, 18, 0xD83DDEA8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(28, 23, 0xD83DDEA9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 22, 0xD83DDEAAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(12, 24, 0xD83DDEABU, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 22, 0xD83DDEACU, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 26, 0xD83DDEADU, 0, 2, 0, 0);
	new (toFill++) EmojiData(3, 26, 0xD83DDEAEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 24, 0xD83DDEAFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 26, 0xD83DDEB0U, 0, 2, 0, 0);
	new (toFill++) EmojiData(18, 24, 0xD83DDEB1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(22, 18, 0xD83DDEB2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(17, 24, 0xD83DDEB3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(15, 16, 0xD83DDEB4U, 0, 2, 0, 0xFFFF0488U);
	new (toFill++) EmojiData(21, 16, 0xD83DDEB5U, 0, 2, 0, 0xFFFF048DU);
	new (toFill++) EmojiData(16, 15, 0xD83DDEB6U, 0, 2, 0, 0xFFFF0492U);
	new (toFill++) EmojiData(15, 24, 0xD83DDEB7U, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 24, 0xD83DDEB8U, 0, 2, 0, 0);
	new (toFill++) EmojiData(37, 25, 0xD83DDEB9U, 0, 2, 0, 0);
	new (toFill++) EmojiData(38, 25, 0xD83DDEBAU, 0, 2, 0, 0);
	new (toFill++) EmojiData(36, 25, 0xD83DDEBBU, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 25, 0xD83DDEBCU, 0, 2, 0, 0);
	new (toFill++) EmojiData(9, 22, 0xD83DDEBDU, 0, 2, 0, 0);
	new (toFill++) EmojiData(16, 25, 0xD83DDEBEU, 0, 2, 0, 0);
	new (toFill++) EmojiData(7, 22, 0xD83DDEBFU, 0, 2, 0, 0);
	new (toFill++) EmojiData(6, 16, 0xD83DDEC0U, 0, 2, 0, 0xFFFF0497U);
	new (toFill++) EmojiData(8, 22, 0xD83DDEC1U, 0, 2, 0, 0);
	new (toFill++) EmojiData(39, 18, 0xD83DDEC2U, 0, 2, 0, 0);
	new (toFill++) EmojiData(0, 19, 0xD83DDEC3U, 0, 2, 0, 0);
	new (toFill++) EmojiData(1, 19, 0xD83DDEC4U, 0, 2, 0, 0);
	new (toFill++) EmojiData(2, 19, 0xD83DDEC5U, 0, 2, 0, 0);
	new (toFill++) EmojiData(21, 2, 0xFFFF0000U, 0, 8, 0, 0);
	new (toFill++) EmojiData(22, 2, 0xFFFF0001U, 0, 11, 0, 0);
	new (toFill++) EmojiData(23, 2, 0xFFFF0002U, 0, 11, 0, 0);
	new (toFill++) EmojiData(24, 2, 0xFFFF0003U, 0, 11, 0, 0);
	new (toFill++) EmojiData(25, 2, 0xFFFF0004U, 0, 8, 0, 0);
	new (toFill++) EmojiData(26, 2, 0xFFFF0005U, 0, 8, 0, 0);
	new (toFill++) EmojiData(27, 2, 0xFFFF0006U, 0, 11, 0, 0);
	new (toFill++) EmojiData(28, 2, 0xFFFF0007U, 0, 11, 0, 0);
	new (toFill++) EmojiData(29, 2, 0xFFFF0008U, 0, 11, 0, 0);
	new (toFill++) EmojiData(30, 2, 0xFFFF0009U, 0, 8, 0, 0);
	new (toFill++) EmojiData(31, 2, 0xFFFF000AU, 0, 8, 0, 0);
	new (toFill++) EmojiData(32, 2, 0xFFFF000BU, 0, 11, 0, 0);
	new (toFill++) EmojiData(33, 2, 0xFFFF000CU, 0, 11, 0, 0);
	new (toFill++) EmojiData(34, 2, 0xFFFF000DU, 0, 11, 0, 0);
	new (toFill++) EmojiData(13, 6, 0xFFFF000EU, 0, 8, 0, 0);
	new (toFill++) EmojiData(14, 6, 0xFFFF000FU, 0, 8, 0, 0);
	new (toFill++) EmojiData(16, 6, 0xFFFF0010U, 0, 11, 0, 0);
	new (toFill++) EmojiData(17, 6, 0xFFFF0011U, 0, 11, 0, 0);
	new (toFill++) EmojiData(31, 7, 0x261DU, 0, 3, 0xFE0F, 0xD83CDFFBU);
	new (toFill++) EmojiData(32, 7, 0x261DU, 0, 3, 0xFE0F, 0xD83CDFFCU);
	new (toFill++) EmojiData(33, 7, 0x261DU, 0, 3, 0xFE0F, 0xD83CDFFDU);
	new (toFill++) EmojiData(34, 7, 0x261DU, 0, 3, 0xFE0F, 0xD83CDFFEU);
	new (toFill++) EmojiData(35, 7, 0x261DU, 0, 3, 0xFE0F, 0xD83CDFFFU);
	new (toFill++) EmojiData(39, 8, 0x270AU, 0, 3, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(0, 9, 0x270AU, 0, 3, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(1, 9, 0x270AU, 0, 3, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(2, 9, 0x270AU, 0, 3, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(3, 9, 0x270AU, 0, 3, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(5, 9, 0x270BU, 0, 3, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(6, 9, 0x270BU, 0, 3, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(7, 9, 0x270BU, 0, 3, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(8, 9, 0x270BU, 0, 3, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(9, 9, 0x270BU, 0, 3, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(27, 8, 0x270CU, 0, 3, 0xFE0F, 0xD83CDFFBU);
	new (toFill++) EmojiData(28, 8, 0x270CU, 0, 3, 0xFE0F, 0xD83CDFFCU);
	new (toFill++) EmojiData(29, 8, 0x270CU, 0, 3, 0xFE0F, 0xD83CDFFDU);
	new (toFill++) EmojiData(30, 8, 0x270CU, 0, 3, 0xFE0F, 0xD83CDFFEU);
	new (toFill++) EmojiData(31, 8, 0x270CU, 0, 3, 0xFE0F, 0xD83CDFFFU);
	new (toFill++) EmojiData(33, 20, 0xD83CDDE6U, 0xD83CDDEAU, 4, 0, 0);
	new (toFill++) EmojiData(34, 19, 0xD83CDDE6U, 0xD83CDDF9U, 4, 0, 0);
	new (toFill++) EmojiData(33, 19, 0xD83CDDE6U, 0xD83CDDFAU, 4, 0, 0);
	new (toFill++) EmojiData(35, 19, 0xD83CDDE7U, 0xD83CDDEAU, 4, 0, 0);
	new (toFill++) EmojiData(36, 19, 0xD83CDDE7U, 0xD83CDDF7U, 4, 0, 0);
	new (toFill++) EmojiData(37, 19, 0xD83CDDE8U, 0xD83CDDE6U, 4, 0, 0);
	new (toFill++) EmojiData(29, 20, 0xD83CDDE8U, 0xD83CDDEDU, 4, 0, 0);
	new (toFill++) EmojiData(38, 19, 0xD83CDDE8U, 0xD83CDDF1U, 4, 0, 0);
	new (toFill++) EmojiData(39, 19, 0xD83CDDE8U, 0xD83CDDF3U, 4, 0, 0);
	new (toFill++) EmojiData(0, 20, 0xD83CDDE8U, 0xD83CDDF4U, 4, 0, 0);
	new (toFill++) EmojiData(4, 20, 0xD83CDDE9U, 0xD83CDDEAU, 4, 0, 0);
	new (toFill++) EmojiData(1, 20, 0xD83CDDE9U, 0xD83CDDF0U, 4, 0, 0);
	new (toFill++) EmojiData(27, 20, 0xD83CDDEAU, 0xD83CDDF8U, 4, 0, 0);
	new (toFill++) EmojiData(2, 20, 0xD83CDDEBU, 0xD83CDDEEU, 4, 0, 0);
	new (toFill++) EmojiData(3, 20, 0xD83CDDEBU, 0xD83CDDF7U, 4, 0, 0);
	new (toFill++) EmojiData(31, 20, 0xD83CDDECU, 0xD83CDDE7U, 4, 0, 0);
	new (toFill++) EmojiData(5, 20, 0xD83CDDEDU, 0xD83CDDF0U, 4, 0, 0);
	new (toFill++) EmojiData(7, 20, 0xD83CDDEEU, 0xD83CDDE9U, 4, 0, 0);
	new (toFill++) EmojiData(8, 20, 0xD83CDDEEU, 0xD83CDDEAU, 4, 0, 0);
	new (toFill++) EmojiData(9, 20, 0xD83CDDEEU, 0xD83CDDF1U, 4, 0, 0);
	new (toFill++) EmojiData(6, 20, 0xD83CDDEEU, 0xD83CDDF3U, 4, 0, 0);
	new (toFill++) EmojiData(10, 20, 0xD83CDDEEU, 0xD83CDDF9U, 4, 0, 0);
	new (toFill++) EmojiData(11, 20, 0xD83CDDEFU, 0xD83CDDF5U, 4, 0, 0);
	new (toFill++) EmojiData(12, 20, 0xD83CDDF0U, 0xD83CDDF7U, 4, 0, 0);
	new (toFill++) EmojiData(13, 20, 0xD83CDDF2U, 0xD83CDDF4U, 4, 0, 0);
	new (toFill++) EmojiData(15, 20, 0xD83CDDF2U, 0xD83CDDFDU, 4, 0, 0);
	new (toFill++) EmojiData(14, 20, 0xD83CDDF2U, 0xD83CDDFEU, 4, 0, 0);
	new (toFill++) EmojiData(16, 20, 0xD83CDDF3U, 0xD83CDDF1U, 4, 0, 0);
	new (toFill++) EmojiData(18, 20, 0xD83CDDF3U, 0xD83CDDF4U, 4, 0, 0);
	new (toFill++) EmojiData(17, 20, 0xD83CDDF3U, 0xD83CDDFFU, 4, 0, 0);
	new (toFill++) EmojiData(19, 20, 0xD83CDDF5U, 0xD83CDDEDU, 4, 0, 0);
	new (toFill++) EmojiData(20, 20, 0xD83CDDF5U, 0xD83CDDF1U, 4, 0, 0);
	new (toFill++) EmojiData(22, 20, 0xD83CDDF5U, 0xD83CDDF7U, 4, 0, 0);
	new (toFill++) EmojiData(21, 20, 0xD83CDDF5U, 0xD83CDDF9U, 4, 0, 0);
	new (toFill++) EmojiData(23, 20, 0xD83CDDF7U, 0xD83CDDFAU, 4, 0, 0);
	new (toFill++) EmojiData(24, 20, 0xD83CDDF8U, 0xD83CDDE6U, 4, 0, 0);
	new (toFill++) EmojiData(28, 20, 0xD83CDDF8U, 0xD83CDDEAU, 4, 0, 0);
	new (toFill++) EmojiData(25, 20, 0xD83CDDF8U, 0xD83CDDECU, 4, 0, 0);
	new (toFill++) EmojiData(30, 20, 0xD83CDDF9U, 0xD83CDDF7U, 4, 0, 0);
	new (toFill++) EmojiData(32, 20, 0xD83CDDFAU, 0xD83CDDF8U, 4, 0, 0);
	new (toFill++) EmojiData(34, 20, 0xD83CDDFBU, 0xD83CDDF3U, 4, 0, 0);
	new (toFill++) EmojiData(26, 20, 0xD83CDDFFU, 0xD83CDDE6U, 4, 0, 0);
	new (toFill++) EmojiData(26, 4, 0xD83CDF85U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(27, 4, 0xD83CDF85U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(28, 4, 0xD83CDF85U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(29, 4, 0xD83CDF85U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(30, 4, 0xD83CDF85U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(11, 15, 0xD83CDFC3U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(12, 15, 0xD83CDFC3U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(13, 15, 0xD83CDFC3U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(14, 15, 0xD83CDFC3U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(15, 15, 0xD83CDFC3U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(1, 16, 0xD83CDFC4U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(2, 16, 0xD83CDFC4U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(3, 16, 0xD83CDFC4U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(4, 16, 0xD83CDFC4U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(5, 16, 0xD83CDFC4U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(28, 16, 0xD83CDFC7U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(29, 16, 0xD83CDFC7U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(30, 16, 0xD83CDFC7U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(31, 16, 0xD83CDFC7U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(32, 16, 0xD83CDFC7U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(35, 15, 0xD83CDFCAU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(36, 15, 0xD83CDFCAU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(37, 15, 0xD83CDFCAU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(38, 15, 0xD83CDFCAU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(39, 15, 0xD83CDFCAU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(31, 6, 0xD83DDC42U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(32, 6, 0xD83DDC42U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(33, 6, 0xD83DDC42U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(34, 6, 0xD83DDC42U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(35, 6, 0xD83DDC42U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(38, 6, 0xD83DDC43U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(39, 6, 0xD83DDC43U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(0, 7, 0xD83DDC43U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(1, 7, 0xD83DDC43U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(2, 7, 0xD83DDC43U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(37, 7, 0xD83DDC46U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(38, 7, 0xD83DDC46U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(39, 7, 0xD83DDC46U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(0, 8, 0xD83DDC46U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(1, 8, 0xD83DDC46U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(3, 8, 0xD83DDC47U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(4, 8, 0xD83DDC47U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(5, 8, 0xD83DDC47U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(6, 8, 0xD83DDC47U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(7, 8, 0xD83DDC47U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(9, 8, 0xD83DDC48U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(10, 8, 0xD83DDC48U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(11, 8, 0xD83DDC48U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(12, 8, 0xD83DDC48U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(13, 8, 0xD83DDC48U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(15, 8, 0xD83DDC49U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(16, 8, 0xD83DDC49U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(17, 8, 0xD83DDC49U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(18, 8, 0xD83DDC49U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(19, 8, 0xD83DDC49U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(33, 8, 0xD83DDC4AU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(34, 8, 0xD83DDC4AU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(35, 8, 0xD83DDC4AU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(36, 8, 0xD83DDC4AU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(37, 8, 0xD83DDC4AU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(13, 7, 0xD83DDC4BU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(14, 7, 0xD83DDC4BU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(15, 7, 0xD83DDC4BU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(16, 7, 0xD83DDC4BU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(17, 7, 0xD83DDC4BU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(21, 8, 0xD83DDC4CU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(22, 8, 0xD83DDC4CU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(23, 8, 0xD83DDC4CU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(24, 8, 0xD83DDC4CU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(25, 8, 0xD83DDC4CU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(19, 7, 0xD83DDC4DU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(20, 7, 0xD83DDC4DU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(21, 7, 0xD83DDC4DU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(22, 7, 0xD83DDC4DU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(23, 7, 0xD83DDC4DU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(25, 7, 0xD83DDC4EU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(26, 7, 0xD83DDC4EU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(27, 7, 0xD83DDC4EU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(28, 7, 0xD83DDC4EU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(29, 7, 0xD83DDC4EU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(25, 6, 0xD83DDC4FU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(26, 6, 0xD83DDC4FU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(27, 6, 0xD83DDC4FU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(28, 6, 0xD83DDC4FU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(29, 6, 0xD83DDC4FU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(17, 9, 0xD83DDC50U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(18, 9, 0xD83DDC50U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(19, 9, 0xD83DDC50U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(20, 9, 0xD83DDC50U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(21, 9, 0xD83DDC50U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(37, 1, 0xD83DDC66U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(38, 1, 0xD83DDC66U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(39, 1, 0xD83DDC66U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(0, 2, 0xD83DDC66U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(1, 2, 0xD83DDC66U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(3, 2, 0xD83DDC67U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(4, 2, 0xD83DDC67U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(5, 2, 0xD83DDC67U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(6, 2, 0xD83DDC67U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(7, 2, 0xD83DDC67U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(9, 2, 0xD83DDC68U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(10, 2, 0xD83DDC68U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(11, 2, 0xD83DDC68U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(12, 2, 0xD83DDC68U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(13, 2, 0xD83DDC68U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(15, 2, 0xD83DDC69U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(16, 2, 0xD83DDC69U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(17, 2, 0xD83DDC69U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(18, 2, 0xD83DDC69U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(19, 2, 0xD83DDC69U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(36, 3, 0xD83DDC6EU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(37, 3, 0xD83DDC6EU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(38, 3, 0xD83DDC6EU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(39, 3, 0xD83DDC6EU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(0, 4, 0xD83DDC6EU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(0, 3, 0xD83DDC70U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(1, 3, 0xD83DDC70U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(2, 3, 0xD83DDC70U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(3, 3, 0xD83DDC70U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(4, 3, 0xD83DDC70U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(6, 3, 0xD83DDC71U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(7, 3, 0xD83DDC71U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(8, 3, 0xD83DDC71U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(9, 3, 0xD83DDC71U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(10, 3, 0xD83DDC71U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(12, 3, 0xD83DDC72U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(13, 3, 0xD83DDC72U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(14, 3, 0xD83DDC72U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(15, 3, 0xD83DDC72U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(16, 3, 0xD83DDC72U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(18, 3, 0xD83DDC73U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(19, 3, 0xD83DDC73U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(20, 3, 0xD83DDC73U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(21, 3, 0xD83DDC73U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(22, 3, 0xD83DDC73U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(24, 3, 0xD83DDC74U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(25, 3, 0xD83DDC74U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(26, 3, 0xD83DDC74U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(27, 3, 0xD83DDC74U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(28, 3, 0xD83DDC74U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(30, 3, 0xD83DDC75U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(31, 3, 0xD83DDC75U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(32, 3, 0xD83DDC75U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(33, 3, 0xD83DDC75U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(34, 3, 0xD83DDC75U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(31, 1, 0xD83DDC76U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(32, 1, 0xD83DDC76U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(33, 1, 0xD83DDC76U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(34, 1, 0xD83DDC76U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(35, 1, 0xD83DDC76U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(2, 4, 0xD83DDC77U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(3, 4, 0xD83DDC77U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(4, 4, 0xD83DDC77U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(5, 4, 0xD83DDC77U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(6, 4, 0xD83DDC77U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(8, 4, 0xD83DDC78U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(9, 4, 0xD83DDC78U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(10, 4, 0xD83DDC78U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(11, 4, 0xD83DDC78U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(12, 4, 0xD83DDC78U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(20, 4, 0xD83DDC7CU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(21, 4, 0xD83DDC7CU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(22, 4, 0xD83DDC7CU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(23, 4, 0xD83DDC7CU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(24, 4, 0xD83DDC7CU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(5, 5, 0xD83DDC81U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(6, 5, 0xD83DDC81U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(7, 5, 0xD83DDC81U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(8, 5, 0xD83DDC81U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(9, 5, 0xD83DDC81U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(14, 4, 0xD83DDC82U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(15, 4, 0xD83DDC82U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(16, 4, 0xD83DDC82U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(17, 4, 0xD83DDC82U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(18, 4, 0xD83DDC82U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(23, 15, 0xD83DDC83U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(24, 15, 0xD83DDC83U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(25, 15, 0xD83DDC83U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(26, 15, 0xD83DDC83U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(27, 15, 0xD83DDC83U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(7, 7, 0xD83DDC85U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(8, 7, 0xD83DDC85U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(9, 7, 0xD83DDC85U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(10, 7, 0xD83DDC85U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(11, 7, 0xD83DDC85U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(1, 6, 0xD83DDC86U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(2, 6, 0xD83DDC86U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(3, 6, 0xD83DDC86U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(4, 6, 0xD83DDC86U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(5, 6, 0xD83DDC86U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(7, 6, 0xD83DDC87U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(8, 6, 0xD83DDC87U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(9, 6, 0xD83DDC87U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(10, 6, 0xD83DDC87U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(11, 6, 0xD83DDC87U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(11, 9, 0xD83DDCAAU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(12, 9, 0xD83DDCAAU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(13, 9, 0xD83DDCAAU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(14, 9, 0xD83DDCAAU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(15, 9, 0xD83DDCAAU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(11, 5, 0xD83DDE45U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(12, 5, 0xD83DDE45U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(13, 5, 0xD83DDE45U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(14, 5, 0xD83DDE45U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(15, 5, 0xD83DDE45U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(17, 5, 0xD83DDE46U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(18, 5, 0xD83DDE46U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(19, 5, 0xD83DDE46U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(20, 5, 0xD83DDE46U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(21, 5, 0xD83DDE46U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(39, 4, 0xD83DDE47U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(0, 5, 0xD83DDE47U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(1, 5, 0xD83DDE47U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(2, 5, 0xD83DDE47U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(3, 5, 0xD83DDE47U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(23, 5, 0xD83DDE4BU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(24, 5, 0xD83DDE4BU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(25, 5, 0xD83DDE4BU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(26, 5, 0xD83DDE4BU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(27, 5, 0xD83DDE4BU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(19, 6, 0xD83DDE4CU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(20, 6, 0xD83DDE4CU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(21, 6, 0xD83DDE4CU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(22, 6, 0xD83DDE4CU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(23, 6, 0xD83DDE4CU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(35, 5, 0xD83DDE4DU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(36, 5, 0xD83DDE4DU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(37, 5, 0xD83DDE4DU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(38, 5, 0xD83DDE4DU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(39, 5, 0xD83DDE4DU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(29, 5, 0xD83DDE4EU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(30, 5, 0xD83DDE4EU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(31, 5, 0xD83DDE4EU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(32, 5, 0xD83DDE4EU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(33, 5, 0xD83DDE4EU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(23, 9, 0xD83DDE4FU, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(24, 9, 0xD83DDE4FU, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(25, 9, 0xD83DDE4FU, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(26, 9, 0xD83DDE4FU, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(27, 9, 0xD83DDE4FU, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(29, 15, 0xD83DDEA3U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(30, 15, 0xD83DDEA3U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(31, 15, 0xD83DDEA3U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(32, 15, 0xD83DDEA3U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(33, 15, 0xD83DDEA3U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(16, 16, 0xD83DDEB4U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(17, 16, 0xD83DDEB4U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(18, 16, 0xD83DDEB4U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(19, 16, 0xD83DDEB4U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(20, 16, 0xD83DDEB4U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(22, 16, 0xD83DDEB5U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(23, 16, 0xD83DDEB5U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(24, 16, 0xD83DDEB5U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(25, 16, 0xD83DDEB5U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(26, 16, 0xD83DDEB5U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(17, 15, 0xD83DDEB6U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(18, 15, 0xD83DDEB6U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(19, 15, 0xD83DDEB6U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(20, 15, 0xD83DDEB6U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(21, 15, 0xD83DDEB6U, 0, 4, 0, 0xD83CDFFFU);
	new (toFill++) EmojiData(7, 16, 0xD83DDEC0U, 0, 4, 0, 0xD83CDFFBU);
	new (toFill++) EmojiData(8, 16, 0xD83DDEC0U, 0, 4, 0, 0xD83CDFFCU);
	new (toFill++) EmojiData(9, 16, 0xD83DDEC0U, 0, 4, 0, 0xD83CDFFDU);
	new (toFill++) EmojiData(10, 16, 0xD83DDEC0U, 0, 4, 0, 0xD83CDFFEU);
	new (toFill++) EmojiData(11, 16, 0xD83DDEC0U, 0, 4, 0, 0xD83CDFFFU);
};

EmojiPtr emojiGet(uint32 code) {
	if (!emojis) return 0;

	uint32 highCode = code >> 16;
	if (!highCode) {
		switch (code) {
			case 0xA9U: return &emojis[0];
			case 0xAEU: return &emojis[1];
		}

		if (code < 0x203CU || code > 0x3299U) return 0;

		switch (code) {
			case 0x203CU: return &emojis[2];
			case 0x2049U: return &emojis[3];
			case 0x2122U: return &emojis[4];
			case 0x2139U: return &emojis[5];
			case 0x2194U: return &emojis[6];
			case 0x2195U: return &emojis[7];
			case 0x2196U: return &emojis[8];
			case 0x2197U: return &emojis[9];
			case 0x2198U: return &emojis[10];
			case 0x2199U: return &emojis[11];
			case 0x21A9U: return &emojis[12];
			case 0x21AAU: return &emojis[13];
			case 0x231AU: return &emojis[14];
			case 0x231BU: return &emojis[15];
			case 0x23E9U: return &emojis[16];
			case 0x23EAU: return &emojis[17];
			case 0x23EBU: return &emojis[18];
			case 0x23ECU: return &emojis[19];
			case 0x23F0U: return &emojis[20];
			case 0x23F3U: return &emojis[21];
			case 0x24C2U: return &emojis[22];
			case 0x25AAU: return &emojis[23];
			case 0x25ABU: return &emojis[24];
			case 0x25B6U: return &emojis[25];
			case 0x25C0U: return &emojis[26];
			case 0x25FBU: return &emojis[27];
			case 0x25FCU: return &emojis[28];
			case 0x25FDU: return &emojis[29];
			case 0x25FEU: return &emojis[30];
			case 0x2600U: return &emojis[31];
			case 0x2601U: return &emojis[32];
			case 0x260EU: return &emojis[33];
			case 0x2611U: return &emojis[34];
			case 0x2614U: return &emojis[35];
			case 0x2615U: return &emojis[36];
			case 0x261DU: return &emojis[37];
			case 0x263AU: return &emojis[38];
			case 0x2648U: return &emojis[39];
			case 0x2649U: return &emojis[40];
			case 0x264AU: return &emojis[41];
			case 0x264BU: return &emojis[42];
			case 0x264CU: return &emojis[43];
			case 0x264DU: return &emojis[44];
			case 0x264EU: return &emojis[45];
			case 0x264FU: return &emojis[46];
			case 0x2650U: return &emojis[47];
			case 0x2651U: return &emojis[48];
			case 0x2652U: return &emojis[49];
			case 0x2653U: return &emojis[50];
			case 0x2660U: return &emojis[51];
			case 0x2663U: return &emojis[52];
			case 0x2665U: return &emojis[53];
			case 0x2666U: return &emojis[54];
			case 0x2668U: return &emojis[55];
			case 0x267BU: return &emojis[56];
			case 0x267FU: return &emojis[57];
			case 0x2693U: return &emojis[58];
			case 0x26A0U: return &emojis[59];
			case 0x26A1U: return &emojis[60];
			case 0x26AAU: return &emojis[61];
			case 0x26ABU: return &emojis[62];
			case 0x26BDU: return &emojis[63];
			case 0x26BEU: return &emojis[64];
			case 0x26C4U: return &emojis[65];
			case 0x26C5U: return &emojis[66];
			case 0x26CEU: return &emojis[67];
			case 0x26D4U: return &emojis[68];
			case 0x26EAU: return &emojis[69];
			case 0x26F2U: return &emojis[70];
			case 0x26F3U: return &emojis[71];
			case 0x26F5U: return &emojis[72];
			case 0x26FAU: return &emojis[73];
			case 0x26FDU: return &emojis[74];
			case 0x2702U: return &emojis[75];
			case 0x2705U: return &emojis[76];
			case 0x2708U: return &emojis[77];
			case 0x2709U: return &emojis[78];
			case 0x270AU: return &emojis[79];
			case 0x270BU: return &emojis[80];
			case 0x270CU: return &emojis[81];
			case 0x270FU: return &emojis[82];
			case 0x2712U: return &emojis[83];
			case 0x2714U: return &emojis[84];
			case 0x2716U: return &emojis[85];
			case 0x2728U: return &emojis[86];
			case 0x2733U: return &emojis[87];
			case 0x2734U: return &emojis[88];
			case 0x2744U: return &emojis[89];
			case 0x2747U: return &emojis[90];
			case 0x274CU: return &emojis[91];
			case 0x274EU: return &emojis[92];
			case 0x2753U: return &emojis[93];
			case 0x2754U: return &emojis[94];
			case 0x2755U: return &emojis[95];
			case 0x2757U: return &emojis[96];
			case 0x2764U: return &emojis[97];
			case 0x2795U: return &emojis[98];
			case 0x2796U: return &emojis[99];
			case 0x2797U: return &emojis[100];
			case 0x27A1U: return &emojis[101];
			case 0x27B0U: return &emojis[102];
			case 0x27BFU: return &emojis[103];
			case 0x2934U: return &emojis[104];
			case 0x2935U: return &emojis[105];
			case 0x2B05U: return &emojis[106];
			case 0x2B06U: return &emojis[107];
			case 0x2B07U: return &emojis[108];
			case 0x2B1BU: return &emojis[109];
			case 0x2B1CU: return &emojis[110];
			case 0x2B50U: return &emojis[111];
			case 0x2B55U: return &emojis[112];
			case 0x3030U: return &emojis[113];
			case 0x303DU: return &emojis[114];
			case 0x3297U: return &emojis[115];
			case 0x3299U: return &emojis[116];
		}

		return 0;
	}

	if (highCode == 35 || (highCode >= 48 && highCode < 58)) {
		if ((code & 0xFFFFU) != 0x20E3U) return 0;

		switch (code) {
			case 0x2320E3U: return &emojis[117];
			case 0x3020E3U: return &emojis[118];
			case 0x3120E3U: return &emojis[119];
			case 0x3220E3U: return &emojis[120];
			case 0x3320E3U: return &emojis[121];
			case 0x3420E3U: return &emojis[122];
			case 0x3520E3U: return &emojis[123];
			case 0x3620E3U: return &emojis[124];
			case 0x3720E3U: return &emojis[125];
			case 0x3820E3U: return &emojis[126];
			case 0x3920E3U: return &emojis[127];
		}

		return 0;
	}

	if (highCode == 0xFFFFU) {
		static const int sequenceOffset = 835;

		uint32 index = (code & 0xFFFFU);
		return (index < 18) ? &emojis[sequenceOffset + index] : 0;
	}

	if (code < 0xD83CDC04U || code > 0xD83DDEC5U) return 0;

	switch (code) {
		case 0xD83CDC04U: return &emojis[128];
		case 0xD83CDCCFU: return &emojis[129];
		case 0xD83CDD70U: return &emojis[130];
		case 0xD83CDD71U: return &emojis[131];
		case 0xD83CDD7EU: return &emojis[132];
		case 0xD83CDD7FU: return &emojis[133];
		case 0xD83CDD8EU: return &emojis[134];
		case 0xD83CDD91U: return &emojis[135];
		case 0xD83CDD92U: return &emojis[136];
		case 0xD83CDD93U: return &emojis[137];
		case 0xD83CDD94U: return &emojis[138];
		case 0xD83CDD95U: return &emojis[139];
		case 0xD83CDD96U: return &emojis[140];
		case 0xD83CDD97U: return &emojis[141];
		case 0xD83CDD98U: return &emojis[142];
		case 0xD83CDD99U: return &emojis[143];
		case 0xD83CDD9AU: return &emojis[144];
		case 0xD83CDE01U: return &emojis[145];
		case 0xD83CDE02U: return &emojis[146];
		case 0xD83CDE1AU: return &emojis[147];
		case 0xD83CDE2FU: return &emojis[148];
		case 0xD83CDE32U: return &emojis[149];
		case 0xD83CDE33U: return &emojis[150];
		case 0xD83CDE34U: return &emojis[151];
		case 0xD83CDE35U: return &emojis[152];
		case 0xD83CDE36U: return &emojis[153];
		case 0xD83CDE37U: return &emojis[154];
		case 0xD83CDE38U: return &emojis[155];
		case 0xD83CDE39U: return &emojis[156];
		case 0xD83CDE3AU: return &emojis[157];
		case 0xD83CDE50U: return &emojis[158];
		case 0xD83CDE51U: return &emojis[159];
		case 0xD83CDF00U: return &emojis[160];
		case 0xD83CDF01U: return &emojis[161];
		case 0xD83CDF02U: return &emojis[162];
		case 0xD83CDF03U: return &emojis[163];
		case 0xD83CDF04U: return &emojis[164];
		case 0xD83CDF05U: return &emojis[165];
		case 0xD83CDF06U: return &emojis[166];
		case 0xD83CDF07U: return &emojis[167];
		case 0xD83CDF08U: return &emojis[168];
		case 0xD83CDF09U: return &emojis[169];
		case 0xD83CDF0AU: return &emojis[170];
		case 0xD83CDF0BU: return &emojis[171];
		case 0xD83CDF0CU: return &emojis[172];
		case 0xD83CDF0DU: return &emojis[173];
		case 0xD83CDF0EU: return &emojis[174];
		case 0xD83CDF0FU: return &emojis[175];
		case 0xD83CDF10U: return &emojis[176];
		case 0xD83CDF11U: return &emojis[177];
		case 0xD83CDF12U: return &emojis[178];
		case 0xD83CDF13U: return &emojis[179];
		case 0xD83CDF14U: return &emojis[180];
		case 0xD83CDF15U: return &emojis[181];
		case 0xD83CDF16U: return &emojis[182];
		case 0xD83CDF17U: return &emojis[183];
		case 0xD83CDF18U: return &emojis[184];
		case 0xD83CDF19U: return &emojis[185];
		case 0xD83CDF1AU: return &emojis[186];
		case 0xD83CDF1BU: return &emojis[187];
		case 0xD83CDF1CU: return &emojis[188];
		case 0xD83CDF1DU: return &emojis[189];
		case 0xD83CDF1EU: return &emojis[190];
		case 0xD83CDF1FU: return &emojis[191];
		case 0xD83CDF20U: return &emojis[192];
		case 0xD83CDF30U: return &emojis[193];
		case 0xD83CDF31U: return &emojis[194];
		case 0xD83CDF32U: return &emojis[195];
		case 0xD83CDF33U: return &emojis[196];
		case 0xD83CDF34U: return &emojis[197];
		case 0xD83CDF35U: return &emojis[198];
		case 0xD83CDF37U: return &emojis[199];
		case 0xD83CDF38U: return &emojis[200];
		case 0xD83CDF39U: return &emojis[201];
		case 0xD83CDF3AU: return &emojis[202];
		case 0xD83CDF3BU: return &emojis[203];
		case 0xD83CDF3CU: return &emojis[204];
		case 0xD83CDF3DU: return &emojis[205];
		case 0xD83CDF3EU: return &emojis[206];
		case 0xD83CDF3FU: return &emojis[207];
		case 0xD83CDF40U: return &emojis[208];
		case 0xD83CDF41U: return &emojis[209];
		case 0xD83CDF42U: return &emojis[210];
		case 0xD83CDF43U: return &emojis[211];
		case 0xD83CDF44U: return &emojis[212];
		case 0xD83CDF45U: return &emojis[213];
		case 0xD83CDF46U: return &emojis[214];
		case 0xD83CDF47U: return &emojis[215];
		case 0xD83CDF48U: return &emojis[216];
		case 0xD83CDF49U: return &emojis[217];
		case 0xD83CDF4AU: return &emojis[218];
		case 0xD83CDF4BU: return &emojis[219];
		case 0xD83CDF4CU: return &emojis[220];
		case 0xD83CDF4DU: return &emojis[221];
		case 0xD83CDF4EU: return &emojis[222];
		case 0xD83CDF4FU: return &emojis[223];
		case 0xD83CDF50U: return &emojis[224];
		case 0xD83CDF51U: return &emojis[225];
		case 0xD83CDF52U: return &emojis[226];
		case 0xD83CDF53U: return &emojis[227];
		case 0xD83CDF54U: return &emojis[228];
		case 0xD83CDF55U: return &emojis[229];
		case 0xD83CDF56U: return &emojis[230];
		case 0xD83CDF57U: return &emojis[231];
		case 0xD83CDF58U: return &emojis[232];
		case 0xD83CDF59U: return &emojis[233];
		case 0xD83CDF5AU: return &emojis[234];
		case 0xD83CDF5BU: return &emojis[235];
		case 0xD83CDF5CU: return &emojis[236];
		case 0xD83CDF5DU: return &emojis[237];
		case 0xD83CDF5EU: return &emojis[238];
		case 0xD83CDF5FU: return &emojis[239];
		case 0xD83CDF60U: return &emojis[240];
		case 0xD83CDF61U: return &emojis[241];
		case 0xD83CDF62U: return &emojis[242];
		case 0xD83CDF63U: return &emojis[243];
		case 0xD83CDF64U: return &emojis[244];
		case 0xD83CDF65U: return &emojis[245];
		case 0xD83CDF66U: return &emojis[246];
		case 0xD83CDF67U: return &emojis[247];
		case 0xD83CDF68U: return &emojis[248];
		case 0xD83CDF69U: return &emojis[249];
		case 0xD83CDF6AU: return &emojis[250];
		case 0xD83CDF6BU: return &emojis[251];
		case 0xD83CDF6CU: return &emojis[252];
		case 0xD83CDF6DU: return &emojis[253];
		case 0xD83CDF6EU: return &emojis[254];
		case 0xD83CDF6FU: return &emojis[255];
		case 0xD83CDF70U: return &emojis[256];
		case 0xD83CDF71U: return &emojis[257];
		case 0xD83CDF72U: return &emojis[258];
		case 0xD83CDF73U: return &emojis[259];
		case 0xD83CDF74U: return &emojis[260];
		case 0xD83CDF75U: return &emojis[261];
		case 0xD83CDF76U: return &emojis[262];
		case 0xD83CDF77U: return &emojis[263];
		case 0xD83CDF78U: return &emojis[264];
		case 0xD83CDF79U: return &emojis[265];
		case 0xD83CDF7AU: return &emojis[266];
		case 0xD83CDF7BU: return &emojis[267];
		case 0xD83CDF7CU: return &emojis[268];
		case 0xD83CDF80U: return &emojis[269];
		case 0xD83CDF81U: return &emojis[270];
		case 0xD83CDF82U: return &emojis[271];
		case 0xD83CDF83U: return &emojis[272];
		case 0xD83CDF84U: return &emojis[273];
		case 0xD83CDF85U: return &emojis[274];
		case 0xD83CDF86U: return &emojis[275];
		case 0xD83CDF87U: return &emojis[276];
		case 0xD83CDF88U: return &emojis[277];
		case 0xD83CDF89U: return &emojis[278];
		case 0xD83CDF8AU: return &emojis[279];
		case 0xD83CDF8BU: return &emojis[280];
		case 0xD83CDF8CU: return &emojis[281];
		case 0xD83CDF8DU: return &emojis[282];
		case 0xD83CDF8EU: return &emojis[283];
		case 0xD83CDF8FU: return &emojis[284];
		case 0xD83CDF90U: return &emojis[285];
		case 0xD83CDF91U: return &emojis[286];
		case 0xD83CDF92U: return &emojis[287];
		case 0xD83CDF93U: return &emojis[288];
		case 0xD83CDFA0U: return &emojis[289];
		case 0xD83CDFA1U: return &emojis[290];
		case 0xD83CDFA2U: return &emojis[291];
		case 0xD83CDFA3U: return &emojis[292];
		case 0xD83CDFA4U: return &emojis[293];
		case 0xD83CDFA5U: return &emojis[294];
		case 0xD83CDFA6U: return &emojis[295];
		case 0xD83CDFA7U: return &emojis[296];
		case 0xD83CDFA8U: return &emojis[297];
		case 0xD83CDFA9U: return &emojis[298];
		case 0xD83CDFAAU: return &emojis[299];
		case 0xD83CDFABU: return &emojis[300];
		case 0xD83CDFACU: return &emojis[301];
		case 0xD83CDFADU: return &emojis[302];
		case 0xD83CDFAEU: return &emojis[303];
		case 0xD83CDFAFU: return &emojis[304];
		case 0xD83CDFB0U: return &emojis[305];
		case 0xD83CDFB1U: return &emojis[306];
		case 0xD83CDFB2U: return &emojis[307];
		case 0xD83CDFB3U: return &emojis[308];
		case 0xD83CDFB4U: return &emojis[309];
		case 0xD83CDFB5U: return &emojis[310];
		case 0xD83CDFB6U: return &emojis[311];
		case 0xD83CDFB7U: return &emojis[312];
		case 0xD83CDFB8U: return &emojis[313];
		case 0xD83CDFB9U: return &emojis[314];
		case 0xD83CDFBAU: return &emojis[315];
		case 0xD83CDFBBU: return &emojis[316];
		case 0xD83CDFBCU: return &emojis[317];
		case 0xD83CDFBDU: return &emojis[318];
		case 0xD83CDFBEU: return &emojis[319];
		case 0xD83CDFBFU: return &emojis[320];
		case 0xD83CDFC0U: return &emojis[321];
		case 0xD83CDFC1U: return &emojis[322];
		case 0xD83CDFC2U: return &emojis[323];
		case 0xD83CDFC3U: return &emojis[324];
		case 0xD83CDFC4U: return &emojis[325];
		case 0xD83CDFC6U: return &emojis[326];
		case 0xD83CDFC7U: return &emojis[327];
		case 0xD83CDFC8U: return &emojis[328];
		case 0xD83CDFC9U: return &emojis[329];
		case 0xD83CDFCAU: return &emojis[330];
		case 0xD83CDFE0U: return &emojis[331];
		case 0xD83CDFE1U: return &emojis[332];
		case 0xD83CDFE2U: return &emojis[333];
		case 0xD83CDFE3U: return &emojis[334];
		case 0xD83CDFE4U: return &emojis[335];
		case 0xD83CDFE5U: return &emojis[336];
		case 0xD83CDFE6U: return &emojis[337];
		case 0xD83CDFE7U: return &emojis[338];
		case 0xD83CDFE8U: return &emojis[339];
		case 0xD83CDFE9U: return &emojis[340];
		case 0xD83CDFEAU: return &emojis[341];
		case 0xD83CDFEBU: return &emojis[342];
		case 0xD83CDFECU: return &emojis[343];
		case 0xD83CDFEDU: return &emojis[344];
		case 0xD83CDFEEU: return &emojis[345];
		case 0xD83CDFEFU: return &emojis[346];
		case 0xD83CDFF0U: return &emojis[347];
		case 0xD83DDC00U: return &emojis[348];
		case 0xD83DDC01U: return &emojis[349];
		case 0xD83DDC02U: return &emojis[350];
		case 0xD83DDC03U: return &emojis[351];
		case 0xD83DDC04U: return &emojis[352];
		case 0xD83DDC05U: return &emojis[353];
		case 0xD83DDC06U: return &emojis[354];
		case 0xD83DDC07U: return &emojis[355];
		case 0xD83DDC08U: return &emojis[356];
		case 0xD83DDC09U: return &emojis[357];
		case 0xD83DDC0AU: return &emojis[358];
		case 0xD83DDC0BU: return &emojis[359];
		case 0xD83DDC0CU: return &emojis[360];
		case 0xD83DDC0DU: return &emojis[361];
		case 0xD83DDC0EU: return &emojis[362];
		case 0xD83DDC0FU: return &emojis[363];
		case 0xD83DDC10U: return &emojis[364];
		case 0xD83DDC11U: return &emojis[365];
		case 0xD83DDC12U: return &emojis[366];
		case 0xD83DDC13U: return &emojis[367];
		case 0xD83DDC14U: return &emojis[368];
		case 0xD83DDC15U: return &emojis[369];
		case 0xD83DDC16U: return &emojis[370];
		case 0xD83DDC17U: return &emojis[371];
		case 0xD83DDC18U: return &emojis[372];
		case 0xD83DDC19U: return &emojis[373];
		case 0xD83DDC1AU: return &emojis[374];
		case 0xD83DDC1BU: return &emojis[375];
		case 0xD83DDC1CU: return &emojis[376];
		case 0xD83DDC1DU: return &emojis[377];
		case 0xD83DDC1EU: return &emojis[378];
		case 0xD83DDC1FU: return &emojis[379];
		case 0xD83DDC20U: return &emojis[380];
		case 0xD83DDC21U: return &emojis[381];
		case 0xD83DDC22U: return &emojis[382];
		case 0xD83DDC23U: return &emojis[383];
		case 0xD83DDC24U: return &emojis[384];
		case 0xD83DDC25U: return &emojis[385];
		case 0xD83DDC26U: return &emojis[386];
		case 0xD83DDC27U: return &emojis[387];
		case 0xD83DDC28U: return &emojis[388];
		case 0xD83DDC29U: return &emojis[389];
		case 0xD83DDC2AU: return &emojis[390];
		case 0xD83DDC2BU: return &emojis[391];
		case 0xD83DDC2CU: return &emojis[392];
		case 0xD83DDC2DU: return &emojis[393];
		case 0xD83DDC2EU: return &emojis[394];
		case 0xD83DDC2FU: return &emojis[395];
		case 0xD83DDC30U: return &emojis[396];
		case 0xD83DDC31U: return &emojis[397];
		case 0xD83DDC32U: return &emojis[398];
		case 0xD83DDC33U: return &emojis[399];
		case 0xD83DDC34U: return &emojis[400];
		case 0xD83DDC35U: return &emojis[401];
		case 0xD83DDC36U: return &emojis[402];
		case 0xD83DDC37U: return &emojis[403];
		case 0xD83DDC38U: return &emojis[404];
		case 0xD83DDC39U: return &emojis[405];
		case 0xD83DDC3AU: return &emojis[406];
		case 0xD83DDC3BU: return &emojis[407];
		case 0xD83DDC3CU: return &emojis[408];
		case 0xD83DDC3DU: return &emojis[409];
		case 0xD83DDC3EU: return &emojis[410];
		case 0xD83DDC40U: return &emojis[411];
		case 0xD83DDC42U: return &emojis[412];
		case 0xD83DDC43U: return &emojis[413];
		case 0xD83DDC44U: return &emojis[414];
		case 0xD83DDC45U: return &emojis[415];
		case 0xD83DDC46U: return &emojis[416];
		case 0xD83DDC47U: return &emojis[417];
		case 0xD83DDC48U: return &emojis[418];
		case 0xD83DDC49U: return &emojis[419];
		case 0xD83DDC4AU: return &emojis[420];
		case 0xD83DDC4BU: return &emojis[421];
		case 0xD83DDC4CU: return &emojis[422];
		case 0xD83DDC4DU: return &emojis[423];
		case 0xD83DDC4EU: return &emojis[424];
		case 0xD83DDC4FU: return &emojis[425];
		case 0xD83DDC50U: return &emojis[426];
		case 0xD83DDC51U: return &emojis[427];
		case 0xD83DDC52U: return &emojis[428];
		case 0xD83DDC53U: return &emojis[429];
		case 0xD83DDC54U: return &emojis[430];
		case 0xD83DDC55U: return &emojis[431];
		case 0xD83DDC56U: return &emojis[432];
		case 0xD83DDC57U: return &emojis[433];
		case 0xD83DDC58U: return &emojis[434];
		case 0xD83DDC59U: return &emojis[435];
		case 0xD83DDC5AU: return &emojis[436];
		case 0xD83DDC5BU: return &emojis[437];
		case 0xD83DDC5CU: return &emojis[438];
		case 0xD83DDC5DU: return &emojis[439];
		case 0xD83DDC5EU: return &emojis[440];
		case 0xD83DDC5FU: return &emojis[441];
		case 0xD83DDC60U: return &emojis[442];
		case 0xD83DDC61U: return &emojis[443];
		case 0xD83DDC62U: return &emojis[444];
		case 0xD83DDC63U: return &emojis[445];
		case 0xD83DDC64U: return &emojis[446];
		case 0xD83DDC65U: return &emojis[447];
		case 0xD83DDC66U: return &emojis[448];
		case 0xD83DDC67U: return &emojis[449];
		case 0xD83DDC68U: return &emojis[450];
		case 0xD83DDC69U: return &emojis[451];
		case 0xD83DDC6AU: return &emojis[452];
		case 0xD83DDC6BU: return &emojis[453];
		case 0xD83DDC6CU: return &emojis[454];
		case 0xD83DDC6DU: return &emojis[455];
		case 0xD83DDC6EU: return &emojis[456];
		case 0xD83DDC6FU: return &emojis[457];
		case 0xD83DDC70U: return &emojis[458];
		case 0xD83DDC71U: return &emojis[459];
		case 0xD83DDC72U: return &emojis[460];
		case 0xD83DDC73U: return &emojis[461];
		case 0xD83DDC74U: return &emojis[462];
		case 0xD83DDC75U: return &emojis[463];
		case 0xD83DDC76U: return &emojis[464];
		case 0xD83DDC77U: return &emojis[465];
		case 0xD83DDC78U: return &emojis[466];
		case 0xD83DDC79U: return &emojis[467];
		case 0xD83DDC7AU: return &emojis[468];
		case 0xD83DDC7BU: return &emojis[469];
		case 0xD83DDC7CU: return &emojis[470];
		case 0xD83DDC7DU: return &emojis[471];
		case 0xD83DDC7EU: return &emojis[472];
		case 0xD83DDC7FU: return &emojis[473];
		case 0xD83DDC80U: return &emojis[474];
		case 0xD83DDC81U: return &emojis[475];
		case 0xD83DDC82U: return &emojis[476];
		case 0xD83DDC83U: return &emojis[477];
		case 0xD83DDC84U: return &emojis[478];
		case 0xD83DDC85U: return &emojis[479];
		case 0xD83DDC86U: return &emojis[480];
		case 0xD83DDC87U: return &emojis[481];
		case 0xD83DDC88U: return &emojis[482];
		case 0xD83DDC89U: return &emojis[483];
		case 0xD83DDC8AU: return &emojis[484];
		case 0xD83DDC8BU: return &emojis[485];
		case 0xD83DDC8CU: return &emojis[486];
		case 0xD83DDC8DU: return &emojis[487];
		case 0xD83DDC8EU: return &emojis[488];
		case 0xD83DDC8FU: return &emojis[489];
		case 0xD83DDC90U: return &emojis[490];
		case 0xD83DDC91U: return &emojis[491];
		case 0xD83DDC92U: return &emojis[492];
		case 0xD83DDC93U: return &emojis[493];
		case 0xD83DDC94U: return &emojis[494];
		case 0xD83DDC95U: return &emojis[495];
		case 0xD83DDC96U: return &emojis[496];
		case 0xD83DDC97U: return &emojis[497];
		case 0xD83DDC98U: return &emojis[498];
		case 0xD83DDC99U: return &emojis[499];
		case 0xD83DDC9AU: return &emojis[500];
		case 0xD83DDC9BU: return &emojis[501];
		case 0xD83DDC9CU: return &emojis[502];
		case 0xD83DDC9DU: return &emojis[503];
		case 0xD83DDC9EU: return &emojis[504];
		case 0xD83DDC9FU: return &emojis[505];
		case 0xD83DDCA0U: return &emojis[506];
		case 0xD83DDCA1U: return &emojis[507];
		case 0xD83DDCA2U: return &emojis[508];
		case 0xD83DDCA3U: return &emojis[509];
		case 0xD83DDCA4U: return &emojis[510];
		case 0xD83DDCA5U: return &emojis[511];
		case 0xD83DDCA6U: return &emojis[512];
		case 0xD83DDCA7U: return &emojis[513];
		case 0xD83DDCA8U: return &emojis[514];
		case 0xD83DDCA9U: return &emojis[515];
		case 0xD83DDCAAU: return &emojis[516];
		case 0xD83DDCABU: return &emojis[517];
		case 0xD83DDCACU: return &emojis[518];
		case 0xD83DDCADU: return &emojis[519];
		case 0xD83DDCAEU: return &emojis[520];
		case 0xD83DDCAFU: return &emojis[521];
		case 0xD83DDCB0U: return &emojis[522];
		case 0xD83DDCB1U: return &emojis[523];
		case 0xD83DDCB2U: return &emojis[524];
		case 0xD83DDCB3U: return &emojis[525];
		case 0xD83DDCB4U: return &emojis[526];
		case 0xD83DDCB5U: return &emojis[527];
		case 0xD83DDCB6U: return &emojis[528];
		case 0xD83DDCB7U: return &emojis[529];
		case 0xD83DDCB8U: return &emojis[530];
		case 0xD83DDCB9U: return &emojis[531];
		case 0xD83DDCBAU: return &emojis[532];
		case 0xD83DDCBBU: return &emojis[533];
		case 0xD83DDCBCU: return &emojis[534];
		case 0xD83DDCBDU: return &emojis[535];
		case 0xD83DDCBEU: return &emojis[536];
		case 0xD83DDCBFU: return &emojis[537];
		case 0xD83DDCC0U: return &emojis[538];
		case 0xD83DDCC1U: return &emojis[539];
		case 0xD83DDCC2U: return &emojis[540];
		case 0xD83DDCC3U: return &emojis[541];
		case 0xD83DDCC4U: return &emojis[542];
		case 0xD83DDCC5U: return &emojis[543];
		case 0xD83DDCC6U: return &emojis[544];
		case 0xD83DDCC7U: return &emojis[545];
		case 0xD83DDCC8U: return &emojis[546];
		case 0xD83DDCC9U: return &emojis[547];
		case 0xD83DDCCAU: return &emojis[548];
		case 0xD83DDCCBU: return &emojis[549];
		case 0xD83DDCCCU: return &emojis[550];
		case 0xD83DDCCDU: return &emojis[551];
		case 0xD83DDCCEU: return &emojis[552];
		case 0xD83DDCCFU: return &emojis[553];
		case 0xD83DDCD0U: return &emojis[554];
		case 0xD83DDCD1U: return &emojis[555];
		case 0xD83DDCD2U: return &emojis[556];
		case 0xD83DDCD3U: return &emojis[557];
		case 0xD83DDCD4U: return &emojis[558];
		case 0xD83DDCD5U: return &emojis[559];
		case 0xD83DDCD6U: return &emojis[560];
		case 0xD83DDCD7U: return &emojis[561];
		case 0xD83DDCD8U: return &emojis[562];
		case 0xD83DDCD9U: return &emojis[563];
		case 0xD83DDCDAU: return &emojis[564];
		case 0xD83DDCDBU: return &emojis[565];
		case 0xD83DDCDCU: return &emojis[566];
		case 0xD83DDCDDU: return &emojis[567];
		case 0xD83DDCDEU: return &emojis[568];
		case 0xD83DDCDFU: return &emojis[569];
		case 0xD83DDCE0U: return &emojis[570];
		case 0xD83DDCE1U: return &emojis[571];
		case 0xD83DDCE2U: return &emojis[572];
		case 0xD83DDCE3U: return &emojis[573];
		case 0xD83DDCE4U: return &emojis[574];
		case 0xD83DDCE5U: return &emojis[575];
		case 0xD83DDCE6U: return &emojis[576];
		case 0xD83DDCE7U: return &emojis[577];
		case 0xD83DDCE8U: return &emojis[578];
		case 0xD83DDCE9U: return &emojis[579];
		case 0xD83DDCEAU: return &emojis[580];
		case 0xD83DDCEBU: return &emojis[581];
		case 0xD83DDCECU: return &emojis[582];
		case 0xD83DDCEDU: return &emojis[583];
		case 0xD83DDCEEU: return &emojis[584];
		case 0xD83DDCEFU: return &emojis[585];
		case 0xD83DDCF0U: return &emojis[586];
		case 0xD83DDCF1U: return &emojis[587];
		case 0xD83DDCF2U: return &emojis[588];
		case 0xD83DDCF3U: return &emojis[589];
		case 0xD83DDCF4U: return &emojis[590];
		case 0xD83DDCF5U: return &emojis[591];
		case 0xD83DDCF6U: return &emojis[592];
		case 0xD83DDCF7U: return &emojis[593];
		case 0xD83DDCF9U: return &emojis[594];
		case 0xD83DDCFAU: return &emojis[595];
		case 0xD83DDCFBU: return &emojis[596];
		case 0xD83DDCFCU: return &emojis[597];
		case 0xD83DDD00U: return &emojis[598];
		case 0xD83DDD01U: return &emojis[599];
		case 0xD83DDD02U: return &emojis[600];
		case 0xD83DDD03U: return &emojis[601];
		case 0xD83DDD04U: return &emojis[602];
		case 0xD83DDD05U: return &emojis[603];
		case 0xD83DDD06U: return &emojis[604];
		case 0xD83DDD07U: return &emojis[605];
		case 0xD83DDD08U: return &emojis[606];
		case 0xD83DDD09U: return &emojis[607];
		case 0xD83DDD0AU: return &emojis[608];
		case 0xD83DDD0BU: return &emojis[609];
		case 0xD83DDD0CU: return &emojis[610];
		case 0xD83DDD0DU: return &emojis[611];
		case 0xD83DDD0EU: return &emojis[612];
		case 0xD83DDD0FU: return &emojis[613];
		case 0xD83DDD10U: return &emojis[614];
		case 0xD83DDD11U: return &emojis[615];
		case 0xD83DDD12U: return &emojis[616];
		case 0xD83DDD13U: return &emojis[617];
		case 0xD83DDD14U: return &emojis[618];
		case 0xD83DDD15U: return &emojis[619];
		case 0xD83DDD16U: return &emojis[620];
		case 0xD83DDD17U: return &emojis[621];
		case 0xD83DDD18U: return &emojis[622];
		case 0xD83DDD19U: return &emojis[623];
		case 0xD83DDD1AU: return &emojis[624];
		case 0xD83DDD1BU: return &emojis[625];
		case 0xD83DDD1CU: return &emojis[626];
		case 0xD83DDD1DU: return &emojis[627];
		case 0xD83DDD1EU: return &emojis[628];
		case 0xD83DDD1FU: return &emojis[629];
		case 0xD83DDD20U: return &emojis[630];
		case 0xD83DDD21U: return &emojis[631];
		case 0xD83DDD22U: return &emojis[632];
		case 0xD83DDD23U: return &emojis[633];
		case 0xD83DDD24U: return &emojis[634];
		case 0xD83DDD25U: return &emojis[635];
		case 0xD83DDD26U: return &emojis[636];
		case 0xD83DDD27U: return &emojis[637];
		case 0xD83DDD28U: return &emojis[638];
		case 0xD83DDD29U: return &emojis[639];
		case 0xD83DDD2AU: return &emojis[640];
		case 0xD83DDD2BU: return &emojis[641];
		case 0xD83DDD2CU: return &emojis[642];
		case 0xD83DDD2DU: return &emojis[643];
		case 0xD83DDD2EU: return &emojis[644];
		case 0xD83DDD2FU: return &emojis[645];
		case 0xD83DDD30U: return &emojis[646];
		case 0xD83DDD31U: return &emojis[647];
		case 0xD83DDD32U: return &emojis[648];
		case 0xD83DDD33U: return &emojis[649];
		case 0xD83DDD34U: return &emojis[650];
		case 0xD83DDD35U: return &emojis[651];
		case 0xD83DDD36U: return &emojis[652];
		case 0xD83DDD37U: return &emojis[653];
		case 0xD83DDD38U: return &emojis[654];
		case 0xD83DDD39U: return &emojis[655];
		case 0xD83DDD3AU: return &emojis[656];
		case 0xD83DDD3BU: return &emojis[657];
		case 0xD83DDD3CU: return &emojis[658];
		case 0xD83DDD3DU: return &emojis[659];
		case 0xD83DDD50U: return &emojis[660];
		case 0xD83DDD51U: return &emojis[661];
		case 0xD83DDD52U: return &emojis[662];
		case 0xD83DDD53U: return &emojis[663];
		case 0xD83DDD54U: return &emojis[664];
		case 0xD83DDD55U: return &emojis[665];
		case 0xD83DDD56U: return &emojis[666];
		case 0xD83DDD57U: return &emojis[667];
		case 0xD83DDD58U: return &emojis[668];
		case 0xD83DDD59U: return &emojis[669];
		case 0xD83DDD5AU: return &emojis[670];
		case 0xD83DDD5BU: return &emojis[671];
		case 0xD83DDD5CU: return &emojis[672];
		case 0xD83DDD5DU: return &emojis[673];
		case 0xD83DDD5EU: return &emojis[674];
		case 0xD83DDD5FU: return &emojis[675];
		case 0xD83DDD60U: return &emojis[676];
		case 0xD83DDD61U: return &emojis[677];
		case 0xD83DDD62U: return &emojis[678];
		case 0xD83DDD63U: return &emojis[679];
		case 0xD83DDD64U: return &emojis[680];
		case 0xD83DDD65U: return &emojis[681];
		case 0xD83DDD66U: return &emojis[682];
		case 0xD83DDD67U: return &emojis[683];
		case 0xD83DDDFBU: return &emojis[684];
		case 0xD83DDDFCU: return &emojis[685];
		case 0xD83DDDFDU: return &emojis[686];
		case 0xD83DDDFEU: return &emojis[687];
		case 0xD83DDDFFU: return &emojis[688];
		case 0xD83DDE00U: return &emojis[689];
		case 0xD83DDE01U: return &emojis[690];
		case 0xD83DDE02U: return &emojis[691];
		case 0xD83DDE03U: return &emojis[692];
		case 0xD83DDE04U: return &emojis[693];
		case 0xD83DDE05U: return &emojis[694];
		case 0xD83DDE06U: return &emojis[695];
		case 0xD83DDE07U: return &emojis[696];
		case 0xD83DDE08U: return &emojis[697];
		case 0xD83DDE09U: return &emojis[698];
		case 0xD83DDE0AU: return &emojis[699];
		case 0xD83DDE0BU: return &emojis[700];
		case 0xD83DDE0CU: return &emojis[701];
		case 0xD83DDE0DU: return &emojis[702];
		case 0xD83DDE0EU: return &emojis[703];
		case 0xD83DDE0FU: return &emojis[704];
		case 0xD83DDE10U: return &emojis[705];
		case 0xD83DDE11U: return &emojis[706];
		case 0xD83DDE12U: return &emojis[707];
		case 0xD83DDE13U: return &emojis[708];
		case 0xD83DDE14U: return &emojis[709];
		case 0xD83DDE15U: return &emojis[710];
		case 0xD83DDE16U: return &emojis[711];
		case 0xD83DDE17U: return &emojis[712];
		case 0xD83DDE18U: return &emojis[713];
		case 0xD83DDE19U: return &emojis[714];
		case 0xD83DDE1AU: return &emojis[715];
		case 0xD83DDE1BU: return &emojis[716];
		case 0xD83DDE1CU: return &emojis[717];
		case 0xD83DDE1DU: return &emojis[718];
		case 0xD83DDE1EU: return &emojis[719];
		case 0xD83DDE1FU: return &emojis[720];
		case 0xD83DDE20U: return &emojis[721];
		case 0xD83DDE21U: return &emojis[722];
		case 0xD83DDE22U: return &emojis[723];
		case 0xD83DDE23U: return &emojis[724];
		case 0xD83DDE24U: return &emojis[725];
		case 0xD83DDE25U: return &emojis[726];
		case 0xD83DDE26U: return &emojis[727];
		case 0xD83DDE27U: return &emojis[728];
		case 0xD83DDE28U: return &emojis[729];
		case 0xD83DDE29U: return &emojis[730];
		case 0xD83DDE2AU: return &emojis[731];
		case 0xD83DDE2BU: return &emojis[732];
		case 0xD83DDE2CU: return &emojis[733];
		case 0xD83DDE2DU: return &emojis[734];
		case 0xD83DDE2EU: return &emojis[735];
		case 0xD83DDE2FU: return &emojis[736];
		case 0xD83DDE30U: return &emojis[737];
		case 0xD83DDE31U: return &emojis[738];
		case 0xD83DDE32U: return &emojis[739];
		case 0xD83DDE33U: return &emojis[740];
		case 0xD83DDE34U: return &emojis[741];
		case 0xD83DDE35U: return &emojis[742];
		case 0xD83DDE36U: return &emojis[743];
		case 0xD83DDE37U: return &emojis[744];
		case 0xD83DDE38U: return &emojis[745];
		case 0xD83DDE39U: return &emojis[746];
		case 0xD83DDE3AU: return &emojis[747];
		case 0xD83DDE3BU: return &emojis[748];
		case 0xD83DDE3CU: return &emojis[749];
		case 0xD83DDE3DU: return &emojis[750];
		case 0xD83DDE3EU: return &emojis[751];
		case 0xD83DDE3FU: return &emojis[752];
		case 0xD83DDE40U: return &emojis[753];
		case 0xD83DDE45U: return &emojis[754];
		case 0xD83DDE46U: return &emojis[755];
		case 0xD83DDE47U: return &emojis[756];
		case 0xD83DDE48U: return &emojis[757];
		case 0xD83DDE49U: return &emojis[758];
		case 0xD83DDE4AU: return &emojis[759];
		case 0xD83DDE4BU: return &emojis[760];
		case 0xD83DDE4CU: return &emojis[761];
		case 0xD83DDE4DU: return &emojis[762];
		case 0xD83DDE4EU: return &emojis[763];
		case 0xD83DDE4FU: return &emojis[764];
		case 0xD83DDE80U: return &emojis[765];
		case 0xD83DDE81U: return &emojis[766];
		case 0xD83DDE82U: return &emojis[767];
		case 0xD83DDE83U: return &emojis[768];
		case 0xD83DDE84U: return &emojis[769];
		case 0xD83DDE85U: return &emojis[770];
		case 0xD83DDE86U: return &emojis[771];
		case 0xD83DDE87U: return &emojis[772];
		case 0xD83DDE88U: return &emojis[773];
		case 0xD83DDE89U: return &emojis[774];
		case 0xD83DDE8AU: return &emojis[775];
		case 0xD83DDE8BU: return &emojis[776];
		case 0xD83DDE8CU: return &emojis[777];
		case 0xD83DDE8DU: return &emojis[778];
		case 0xD83DDE8EU: return &emojis[779];
		case 0xD83DDE8FU: return &emojis[780];
		case 0xD83DDE90U: return &emojis[781];
		case 0xD83DDE91U: return &emojis[782];
		case 0xD83DDE92U: return &emojis[783];
		case 0xD83DDE93U: return &emojis[784];
		case 0xD83DDE94U: return &emojis[785];
		case 0xD83DDE95U: return &emojis[786];
		case 0xD83DDE96U: return &emojis[787];
		case 0xD83DDE97U: return &emojis[788];
		case 0xD83DDE98U: return &emojis[789];
		case 0xD83DDE99U: return &emojis[790];
		case 0xD83DDE9AU: return &emojis[791];
		case 0xD83DDE9BU: return &emojis[792];
		case 0xD83DDE9CU: return &emojis[793];
		case 0xD83DDE9DU: return &emojis[794];
		case 0xD83DDE9EU: return &emojis[795];
		case 0xD83DDE9FU: return &emojis[796];
		case 0xD83DDEA0U: return &emojis[797];
		case 0xD83DDEA1U: return &emojis[798];
		case 0xD83DDEA2U: return &emojis[799];
		case 0xD83DDEA3U: return &emojis[800];
		case 0xD83DDEA4U: return &emojis[801];
		case 0xD83DDEA5U: return &emojis[802];
		case 0xD83DDEA6U: return &emojis[803];
		case 0xD83DDEA7U: return &emojis[804];
		case 0xD83DDEA8U: return &emojis[805];
		case 0xD83DDEA9U: return &emojis[806];
		case 0xD83DDEAAU: return &emojis[807];
		case 0xD83DDEABU: return &emojis[808];
		case 0xD83DDEACU: return &emojis[809];
		case 0xD83DDEADU: return &emojis[810];
		case 0xD83DDEAEU: return &emojis[811];
		case 0xD83DDEAFU: return &emojis[812];
		case 0xD83DDEB0U: return &emojis[813];
		case 0xD83DDEB1U: return &emojis[814];
		case 0xD83DDEB2U: return &emojis[815];
		case 0xD83DDEB3U: return &emojis[816];
		case 0xD83DDEB4U: return &emojis[817];
		case 0xD83DDEB5U: return &emojis[818];
		case 0xD83DDEB6U: return &emojis[819];
		case 0xD83DDEB7U: return &emojis[820];
		case 0xD83DDEB8U: return &emojis[821];
		case 0xD83DDEB9U: return &emojis[822];
		case 0xD83DDEBAU: return &emojis[823];
		case 0xD83DDEBBU: return &emojis[824];
		case 0xD83DDEBCU: return &emojis[825];
		case 0xD83DDEBDU: return &emojis[826];
		case 0xD83DDEBEU: return &emojis[827];
		case 0xD83DDEBFU: return &emojis[828];
		case 0xD83DDEC0U: return &emojis[829];
		case 0xD83DDEC1U: return &emojis[830];
		case 0xD83DDEC2U: return &emojis[831];
		case 0xD83DDEC3U: return &emojis[832];
		case 0xD83DDEC4U: return &emojis[833];
		case 0xD83DDEC5U: return &emojis[834];
		case 0xFFFF0000U: return &emojis[835];
		case 0xFFFF0001U: return &emojis[836];
		case 0xFFFF0002U: return &emojis[837];
		case 0xFFFF0003U: return &emojis[838];
		case 0xFFFF0004U: return &emojis[839];
		case 0xFFFF0005U: return &emojis[840];
		case 0xFFFF0006U: return &emojis[841];
		case 0xFFFF0007U: return &emojis[842];
		case 0xFFFF0008U: return &emojis[843];
		case 0xFFFF0009U: return &emojis[844];
		case 0xFFFF000AU: return &emojis[845];
		case 0xFFFF000BU: return &emojis[846];
		case 0xFFFF000CU: return &emojis[847];
		case 0xFFFF000DU: return &emojis[848];
		case 0xFFFF000EU: return &emojis[849];
		case 0xFFFF000FU: return &emojis[850];
		case 0xFFFF0010U: return &emojis[851];
		case 0xFFFF0011U: return &emojis[852];
		case 0xD83CDDE6U: return TwoSymbolEmoji;
		case 0xD83CDDE7U: return TwoSymbolEmoji;
		case 0xD83CDDE8U: return TwoSymbolEmoji;
		case 0xD83CDDE9U: return TwoSymbolEmoji;
		case 0xD83CDDEAU: return TwoSymbolEmoji;
		case 0xD83CDDEBU: return TwoSymbolEmoji;
		case 0xD83CDDECU: return TwoSymbolEmoji;
		case 0xD83CDDEDU: return TwoSymbolEmoji;
		case 0xD83CDDEEU: return TwoSymbolEmoji;
		case 0xD83CDDEFU: return TwoSymbolEmoji;
		case 0xD83CDDF0U: return TwoSymbolEmoji;
		case 0xD83CDDF2U: return TwoSymbolEmoji;
		case 0xD83CDDF3U: return TwoSymbolEmoji;
		case 0xD83CDDF5U: return TwoSymbolEmoji;
		case 0xD83CDDF7U: return TwoSymbolEmoji;
		case 0xD83CDDF8U: return TwoSymbolEmoji;
		case 0xD83CDDF9U: return TwoSymbolEmoji;
		case 0xD83CDDFAU: return TwoSymbolEmoji;
		case 0xD83CDDFBU: return TwoSymbolEmoji;
		case 0xD83CDDFFU: return TwoSymbolEmoji;
	}

	return 0;
}

EmojiPtr emojiGet(uint32 code, uint32 code2) {
	if (code < 0xD83CDDE6U || code > 0xD83CDDFFU) return 0;

	switch (code) {
		case 0xD83CDDE6U: switch (code2) {
			case 0xD83CDDEAU: return &emojis[873];
			case 0xD83CDDF9U: return &emojis[874];
			case 0xD83CDDFAU: return &emojis[875];
			default: return 0;
		} break;
		case 0xD83CDDE7U: switch (code2) {
			case 0xD83CDDEAU: return &emojis[876];
			case 0xD83CDDF7U: return &emojis[877];
			default: return 0;
		} break;
		case 0xD83CDDE8U: switch (code2) {
			case 0xD83CDDE6U: return &emojis[878];
			case 0xD83CDDEDU: return &emojis[879];
			case 0xD83CDDF1U: return &emojis[880];
			case 0xD83CDDF3U: return &emojis[881];
			case 0xD83CDDF4U: return &emojis[882];
			default: return 0;
		} break;
		case 0xD83CDDE9U: switch (code2) {
			case 0xD83CDDEAU: return &emojis[883];
			case 0xD83CDDF0U: return &emojis[884];
			default: return 0;
		} break;
		case 0xD83CDDEAU: switch (code2) {
			case 0xD83CDDF8U: return &emojis[885];
			default: return 0;
		} break;
		case 0xD83CDDEBU: switch (code2) {
			case 0xD83CDDEEU: return &emojis[886];
			case 0xD83CDDF7U: return &emojis[887];
			default: return 0;
		} break;
		case 0xD83CDDECU: switch (code2) {
			case 0xD83CDDE7U: return &emojis[888];
			default: return 0;
		} break;
		case 0xD83CDDEDU: switch (code2) {
			case 0xD83CDDF0U: return &emojis[889];
			default: return 0;
		} break;
		case 0xD83CDDEEU: switch (code2) {
			case 0xD83CDDE9U: return &emojis[890];
			case 0xD83CDDEAU: return &emojis[891];
			case 0xD83CDDF1U: return &emojis[892];
			case 0xD83CDDF3U: return &emojis[893];
			case 0xD83CDDF9U: return &emojis[894];
			default: return 0;
		} break;
		case 0xD83CDDEFU: switch (code2) {
			case 0xD83CDDF5U: return &emojis[895];
			default: return 0;
		} break;
		case 0xD83CDDF0U: switch (code2) {
			case 0xD83CDDF7U: return &emojis[896];
			default: return 0;
		} break;
		case 0xD83CDDF2U: switch (code2) {
			case 0xD83CDDF4U: return &emojis[897];
			case 0xD83CDDFDU: return &emojis[898];
			case 0xD83CDDFEU: return &emojis[899];
			default: return 0;
		} break;
		case 0xD83CDDF3U: switch (code2) {
			case 0xD83CDDF1U: return &emojis[900];
			case 0xD83CDDF4U: return &emojis[901];
			case 0xD83CDDFFU: return &emojis[902];
			default: return 0;
		} break;
		case 0xD83CDDF5U: switch (code2) {
			case 0xD83CDDEDU: return &emojis[903];
			case 0xD83CDDF1U: return &emojis[904];
			case 0xD83CDDF7U: return &emojis[905];
			case 0xD83CDDF9U: return &emojis[906];
			default: return 0;
		} break;
		case 0xD83CDDF7U: switch (code2) {
			case 0xD83CDDFAU: return &emojis[907];
			default: return 0;
		} break;
		case 0xD83CDDF8U: switch (code2) {
			case 0xD83CDDE6U: return &emojis[908];
			case 0xD83CDDEAU: return &emojis[909];
			case 0xD83CDDECU: return &emojis[910];
			default: return 0;
		} break;
		case 0xD83CDDF9U: switch (code2) {
			case 0xD83CDDF7U: return &emojis[911];
			default: return 0;
		} break;
		case 0xD83CDDFAU: switch (code2) {
			case 0xD83CDDF8U: return &emojis[912];
			default: return 0;
		} break;
		case 0xD83CDDFBU: switch (code2) {
			case 0xD83CDDF3U: return &emojis[913];
			default: return 0;
		} break;
		case 0xD83CDDFFU: switch (code2) {
			case 0xD83CDDE6U: return &emojis[914];
			default: return 0;
		} break;
	}

	return 0;
}

EmojiPtr emojiGet(EmojiPtr emoji, uint32 color) {
	if (!emoji || ((emoji->color & 0xFFFF0000U) != 0xFFFF0000U)) return emoji;

	int index = 0;
	switch (color) {
		case 0xD83CDFFB: index = 0; break;
		case 0xD83CDFFC: index = 1; break;
		case 0xD83CDFFD: index = 2; break;
		case 0xD83CDFFE: index = 3; break;
		case 0xD83CDFFF: index = 4; break;
		default: return emoji;
	}

	return &emojis[(emoji->color & 0xFFFFU) + index];
}

EmojiPtr emojiGet(const QChar *from, const QChar *end) {
	static const int sequenceOffset = 835;

	if (end < from + 8 || (from + 2)->unicode() != 0x200D || (from + 5)->unicode() != 0x200D) return 0;

	static const uint32 man = 0xD83DDC68, woman = 0xD83DDC69, boy = 0xD83DDC66, girl = 0xD83DDC67, heart = 0x2764FE0F, kiss = 0xD83DDC8B;
	uint32 one = (uint32(from->unicode()) << 16) | uint32((from + 1)->unicode()), two = (uint32((from + 3)->unicode()) << 16) | uint32((from + 4)->unicode()), three = (uint32((from + 6)->unicode()) << 16) | uint32((from + 7)->unicode());

	if (one != man && one != woman) return 0;

	if (end > from + 10 && (from + 8)->unicode() == 0x200D) {
		uint32 four = (uint32((from + 9)->unicode()) << 16) | uint32((from + 10)->unicode());

		if (one == man) {
			if (two == man) {
				if (three == girl) {
					if (four == girl) return &emojis[sequenceOffset + 13];
					if (four == boy) return &emojis[sequenceOffset + 11];
				} else if (three == boy) {
					if (four == boy) return &emojis[sequenceOffset + 12];
				}
			} else if (two == woman) {
				if (three == girl) {
					if (four == girl) return &emojis[sequenceOffset + 3];
					if (four == boy) return &emojis[sequenceOffset + 1];
				} else if (three == boy) {
					if (four == boy) return &emojis[sequenceOffset + 2];
				}
			} else if (two == heart) {
				if (three == kiss && four == man) return &emojis[sequenceOffset + 17];
			}
		} else {
			if (two == woman) {
				if (three == girl) {
					if (four == girl) return &emojis[sequenceOffset + 8];
					if (four == boy) return &emojis[sequenceOffset + 6];
				} else if (three == boy) {
					if (four == boy) return &emojis[sequenceOffset + 7];
				}
			} else if (two == heart) {
				if (three == kiss && four == woman) return &emojis[sequenceOffset + 16];
			}
		}
	}
	if (one == man) {
		if (two == man) {
			if (three == girl) return &emojis[sequenceOffset + 10];
			if (three == boy) return &emojis[sequenceOffset + 9];
		} else if (two == woman) {
			if (three == girl) return &emojis[sequenceOffset + 0];
		} else if (two == heart) {
			if (three == man) return &emojis[sequenceOffset + 15];
		}
	} else {
		if (two == woman) {
			if (three == girl) return &emojis[sequenceOffset + 5];
			if (three == boy) return &emojis[sequenceOffset + 4];
		} else if (two == heart) {
			if (three == woman) return &emojis[sequenceOffset + 14];
		}
	}
	return 0;
}

QString emojiGetSequence(int index) {
	static QVector<QString> sequences;
	if (sequences.isEmpty()) {
		sequences.reserve(18);

		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa6"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x91\xa9"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x91\xa8"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa9"));
		sequences.push_back(QString::fromUtf8("\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa8"));
	}

	return (index >= 0 && index < sequences.size()) ? sequences.at(index) : QString();
}

void emojiFind(const QChar *ch, const QChar *e, const QChar *&newEmojiEnd, uint32 &emojiCode) {
	switch (ch->unicode()) {
	case '}':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case ':':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case ')':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE08U;
					return;
				}
			break;
			}
		break;
		}
	break;
	case 'x':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case 'D':
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE06U;
				return;
			}
		break;
		}
	break;
	case 'O':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case ':':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case ')':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE07U;
					return;
				}
			break;
			}
		break;
		}
	break;
	case 'B':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case '-':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case ')':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE0EU;
					return;
				}
			break;
			}
		break;
		}
	break;
	case '>':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case '(':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case '(':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE21U;
					return;
				}
			break;
			}
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE20U;
				return;
			}
		break;
		}
	break;
	case '<':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case '3':
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0x2764U;
				return;
			}
		break;
		}
	break;
	case ';':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case 'o':
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE30U;
				return;
			}
		break;
		case '-':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case 'P':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE1CU;
					return;
				}
			break;
			case ')':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE09U;
					return;
				}
			break;
			}
		break;
		}
	break;
	case ':':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case '|':
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE10U;
				return;
			}
		break;
		case 'v':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case ':':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0x270CU;
					return;
				}
			break;
			}
		break;
		case 'u':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case 'p':
				if (ch + 3 != e) switch ((ch + 3)->unicode()) {
				case ':':
					newEmojiEnd = ch + 4;
					if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
						emojiCode = 0x261DU;
						return;
					}
				break;
				}
			break;
			}
		break;
		case 'o':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case 'k':
				if (ch + 3 != e) switch ((ch + 3)->unicode()) {
				case ':':
					newEmojiEnd = ch + 4;
					if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
						emojiCode = 0xD83DDC4CU;
						return;
					}
				break;
				}
			break;
			}
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE28U;
				return;
			}
		break;
		case 'l':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case 'i':
				if (ch + 3 != e) switch ((ch + 3)->unicode()) {
				case 'k':
					if (ch + 4 != e) switch ((ch + 4)->unicode()) {
					case 'e':
						if (ch + 5 != e) switch ((ch + 5)->unicode()) {
						case ':':
							newEmojiEnd = ch + 6;
							if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
								emojiCode = 0xD83DDC4DU;
								return;
							}
						break;
						}
					break;
					}
				break;
				}
			break;
			}
		break;
		case 'k':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case 'i':
				if (ch + 3 != e) switch ((ch + 3)->unicode()) {
				case 's':
					if (ch + 4 != e) switch ((ch + 4)->unicode()) {
					case 's':
						if (ch + 5 != e) switch ((ch + 5)->unicode()) {
						case ':':
							newEmojiEnd = ch + 6;
							if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
								emojiCode = 0xD83DDC8BU;
								return;
							}
						break;
						}
					break;
					}
				break;
				}
			break;
			}
		break;
		case 'j':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case 'o':
				if (ch + 3 != e) switch ((ch + 3)->unicode()) {
				case 'y':
					if (ch + 4 != e) switch ((ch + 4)->unicode()) {
					case ':':
						newEmojiEnd = ch + 5;
						if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
							emojiCode = 0xD83DDE02U;
							return;
						}
					break;
					}
				break;
				}
			break;
			}
		break;
		case 'g':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case 'r':
				if (ch + 3 != e) switch ((ch + 3)->unicode()) {
				case 'i':
					if (ch + 4 != e) switch ((ch + 4)->unicode()) {
					case 'n':
						if (ch + 5 != e) switch ((ch + 5)->unicode()) {
						case ':':
							newEmojiEnd = ch + 6;
							if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
								emojiCode = 0xD83DDE01U;
								return;
							}
						break;
						}
					break;
					}
				break;
				}
			break;
			}
		break;
		case 'd':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case 'i':
				if (ch + 3 != e) switch ((ch + 3)->unicode()) {
				case 's':
					if (ch + 4 != e) switch ((ch + 4)->unicode()) {
					case 'l':
						if (ch + 5 != e) switch ((ch + 5)->unicode()) {
						case 'i':
							if (ch + 6 != e) switch ((ch + 6)->unicode()) {
							case 'k':
								if (ch + 7 != e) switch ((ch + 7)->unicode()) {
								case 'e':
									if (ch + 8 != e) switch ((ch + 8)->unicode()) {
									case ':':
										newEmojiEnd = ch + 9;
										if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
											emojiCode = 0xD83DDC4EU;
											return;
										}
									break;
									}
								break;
								}
							break;
							}
						break;
						}
					break;
					}
				break;
				}
			break;
			}
		break;
		case '_':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case '(':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE2DU;
					return;
				}
			break;
			}
		break;
		case ']':
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE0FU;
				return;
			}
		break;
		case 'X':
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE37U;
				return;
			}
		break;
		case '-':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case 'p':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE0BU;
					return;
				}
			break;
			case 'D':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE03U;
					return;
				}
			break;
			case '*':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE1AU;
					return;
				}
			break;
			case ')':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE0AU;
					return;
				}
			break;
			case '(':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE1EU;
					return;
				}
			break;
			}
		break;
		case '(':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case '(':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE29U;
					return;
				}
			break;
			}
		break;
		case '\'':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case '(':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE22U;
					return;
				}
			break;
			}
		break;
		}
	break;
	case '8':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case '|':
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE33U;
				return;
			}
		break;
		case 'o':
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE32U;
				return;
			}
		break;
		case '-':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case ')':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE0DU;
					return;
				}
			break;
			}
		break;
		}
	break;
	case '3':
		if (ch + 1 != e) switch ((ch + 1)->unicode()) {
		case '-':
			if (ch + 2 != e) switch ((ch + 2)->unicode()) {
			case ')':
				newEmojiEnd = ch + 3;
				if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
					emojiCode = 0xD83DDE0CU;
					return;
				}
			break;
			}
		break;
		case '(':
			newEmojiEnd = ch + 2;
			if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {
				emojiCode = 0xD83DDE14U;
				return;
			}
		break;
		}
	break;
	}
}

int emojiPackCount(DBIEmojiTab tab) {
	switch (tab) {
		case dbietRecent     : return cGetRecentEmojis().size();
		case dbietPeople     : return 153;
		case dbietNature     : return 125;
		case dbietFood       : return 58;
		case dbietCelebration: return 39;
		case dbietActivity   : return 53;
		case dbietTravel     : return 122;
		case dbietObjects    : return 345;
	};
	return 0;
}

EmojiPack emojiPack(DBIEmojiTab tab) {
	switch (tab) {

	case dbietPeople: {
		static QVector<EmojiPtr> vPeople;
		if (vPeople.isEmpty()) {
			vPeople.resize(153);
			vPeople[0] = &emojis[689];
			vPeople[1] = &emojis[690];
			vPeople[2] = &emojis[691];
			vPeople[3] = &emojis[692];
			vPeople[4] = &emojis[693];
			vPeople[5] = &emojis[694];
			vPeople[6] = &emojis[695];
			vPeople[7] = &emojis[696];
			vPeople[8] = &emojis[697];
			vPeople[9] = &emojis[473];
			vPeople[10] = &emojis[698];
			vPeople[11] = &emojis[699];
			vPeople[12] = &emojis[38];
			vPeople[13] = &emojis[700];
			vPeople[14] = &emojis[701];
			vPeople[15] = &emojis[702];
			vPeople[16] = &emojis[703];
			vPeople[17] = &emojis[704];
			vPeople[18] = &emojis[705];
			vPeople[19] = &emojis[706];
			vPeople[20] = &emojis[707];
			vPeople[21] = &emojis[708];
			vPeople[22] = &emojis[709];
			vPeople[23] = &emojis[710];
			vPeople[24] = &emojis[711];
			vPeople[25] = &emojis[712];
			vPeople[26] = &emojis[713];
			vPeople[27] = &emojis[714];
			vPeople[28] = &emojis[715];
			vPeople[29] = &emojis[716];
			vPeople[30] = &emojis[717];
			vPeople[31] = &emojis[718];
			vPeople[32] = &emojis[719];
			vPeople[33] = &emojis[720];
			vPeople[34] = &emojis[721];
			vPeople[35] = &emojis[722];
			vPeople[36] = &emojis[723];
			vPeople[37] = &emojis[724];
			vPeople[38] = &emojis[725];
			vPeople[39] = &emojis[726];
			vPeople[40] = &emojis[727];
			vPeople[41] = &emojis[728];
			vPeople[42] = &emojis[729];
			vPeople[43] = &emojis[730];
			vPeople[44] = &emojis[731];
			vPeople[45] = &emojis[732];
			vPeople[46] = &emojis[733];
			vPeople[47] = &emojis[734];
			vPeople[48] = &emojis[735];
			vPeople[49] = &emojis[736];
			vPeople[50] = &emojis[737];
			vPeople[51] = &emojis[738];
			vPeople[52] = &emojis[739];
			vPeople[53] = &emojis[740];
			vPeople[54] = &emojis[741];
			vPeople[55] = &emojis[742];
			vPeople[56] = &emojis[743];
			vPeople[57] = &emojis[744];
			vPeople[58] = &emojis[745];
			vPeople[59] = &emojis[746];
			vPeople[60] = &emojis[747];
			vPeople[61] = &emojis[748];
			vPeople[62] = &emojis[749];
			vPeople[63] = &emojis[750];
			vPeople[64] = &emojis[751];
			vPeople[65] = &emojis[752];
			vPeople[66] = &emojis[753];
			vPeople[67] = &emojis[445];
			vPeople[68] = &emojis[446];
			vPeople[69] = &emojis[447];
			vPeople[70] = &emojis[464];
			vPeople[71] = &emojis[448];
			vPeople[72] = &emojis[449];
			vPeople[73] = &emojis[450];
			vPeople[74] = &emojis[451];
			vPeople[75] = &emojis[452];
			vPeople[76] = &emojis[835];
			vPeople[77] = &emojis[836];
			vPeople[78] = &emojis[837];
			vPeople[79] = &emojis[838];
			vPeople[80] = &emojis[839];
			vPeople[81] = &emojis[840];
			vPeople[82] = &emojis[841];
			vPeople[83] = &emojis[842];
			vPeople[84] = &emojis[843];
			vPeople[85] = &emojis[844];
			vPeople[86] = &emojis[845];
			vPeople[87] = &emojis[846];
			vPeople[88] = &emojis[847];
			vPeople[89] = &emojis[848];
			vPeople[90] = &emojis[453];
			vPeople[91] = &emojis[454];
			vPeople[92] = &emojis[455];
			vPeople[93] = &emojis[457];
			vPeople[94] = &emojis[458];
			vPeople[95] = &emojis[459];
			vPeople[96] = &emojis[460];
			vPeople[97] = &emojis[461];
			vPeople[98] = &emojis[462];
			vPeople[99] = &emojis[463];
			vPeople[100] = &emojis[456];
			vPeople[101] = &emojis[465];
			vPeople[102] = &emojis[466];
			vPeople[103] = &emojis[476];
			vPeople[104] = &emojis[470];
			vPeople[105] = &emojis[274];
			vPeople[106] = &emojis[469];
			vPeople[107] = &emojis[467];
			vPeople[108] = &emojis[468];
			vPeople[109] = &emojis[515];
			vPeople[110] = &emojis[474];
			vPeople[111] = &emojis[471];
			vPeople[112] = &emojis[472];
			vPeople[113] = &emojis[756];
			vPeople[114] = &emojis[475];
			vPeople[115] = &emojis[754];
			vPeople[116] = &emojis[755];
			vPeople[117] = &emojis[760];
			vPeople[118] = &emojis[763];
			vPeople[119] = &emojis[762];
			vPeople[120] = &emojis[480];
			vPeople[121] = &emojis[481];
			vPeople[122] = &emojis[491];
			vPeople[123] = &emojis[849];
			vPeople[124] = &emojis[850];
			vPeople[125] = &emojis[489];
			vPeople[126] = &emojis[851];
			vPeople[127] = &emojis[852];
			vPeople[128] = &emojis[761];
			vPeople[129] = &emojis[425];
			vPeople[130] = &emojis[412];
			vPeople[131] = &emojis[411];
			vPeople[132] = &emojis[413];
			vPeople[133] = &emojis[414];
			vPeople[134] = &emojis[485];
			vPeople[135] = &emojis[415];
			vPeople[136] = &emojis[479];
			vPeople[137] = &emojis[421];
			vPeople[138] = &emojis[423];
			vPeople[139] = &emojis[424];
			vPeople[140] = &emojis[37];
			vPeople[141] = &emojis[416];
			vPeople[142] = &emojis[417];
			vPeople[143] = &emojis[418];
			vPeople[144] = &emojis[419];
			vPeople[145] = &emojis[422];
			vPeople[146] = &emojis[81];
			vPeople[147] = &emojis[420];
			vPeople[148] = &emojis[79];
			vPeople[149] = &emojis[80];
			vPeople[150] = &emojis[516];
			vPeople[151] = &emojis[426];
			vPeople[152] = &emojis[764];
		}
		return vPeople;
	} break;

	case dbietNature: {
		static QVector<EmojiPtr> vNature;
		if (vNature.isEmpty()) {
			vNature.resize(125);
			vNature[0] = &emojis[194];
			vNature[1] = &emojis[195];
			vNature[2] = &emojis[196];
			vNature[3] = &emojis[197];
			vNature[4] = &emojis[198];
			vNature[5] = &emojis[199];
			vNature[6] = &emojis[200];
			vNature[7] = &emojis[201];
			vNature[8] = &emojis[202];
			vNature[9] = &emojis[203];
			vNature[10] = &emojis[204];
			vNature[11] = &emojis[490];
			vNature[12] = &emojis[206];
			vNature[13] = &emojis[207];
			vNature[14] = &emojis[208];
			vNature[15] = &emojis[209];
			vNature[16] = &emojis[210];
			vNature[17] = &emojis[211];
			vNature[18] = &emojis[212];
			vNature[19] = &emojis[193];
			vNature[20] = &emojis[348];
			vNature[21] = &emojis[349];
			vNature[22] = &emojis[393];
			vNature[23] = &emojis[405];
			vNature[24] = &emojis[350];
			vNature[25] = &emojis[351];
			vNature[26] = &emojis[352];
			vNature[27] = &emojis[394];
			vNature[28] = &emojis[353];
			vNature[29] = &emojis[354];
			vNature[30] = &emojis[395];
			vNature[31] = &emojis[355];
			vNature[32] = &emojis[396];
			vNature[33] = &emojis[356];
			vNature[34] = &emojis[397];
			vNature[35] = &emojis[362];
			vNature[36] = &emojis[400];
			vNature[37] = &emojis[363];
			vNature[38] = &emojis[365];
			vNature[39] = &emojis[364];
			vNature[40] = &emojis[367];
			vNature[41] = &emojis[368];
			vNature[42] = &emojis[384];
			vNature[43] = &emojis[383];
			vNature[44] = &emojis[385];
			vNature[45] = &emojis[386];
			vNature[46] = &emojis[387];
			vNature[47] = &emojis[372];
			vNature[48] = &emojis[390];
			vNature[49] = &emojis[391];
			vNature[50] = &emojis[371];
			vNature[51] = &emojis[370];
			vNature[52] = &emojis[403];
			vNature[53] = &emojis[409];
			vNature[54] = &emojis[369];
			vNature[55] = &emojis[389];
			vNature[56] = &emojis[402];
			vNature[57] = &emojis[406];
			vNature[58] = &emojis[407];
			vNature[59] = &emojis[388];
			vNature[60] = &emojis[408];
			vNature[61] = &emojis[401];
			vNature[62] = &emojis[757];
			vNature[63] = &emojis[758];
			vNature[64] = &emojis[759];
			vNature[65] = &emojis[366];
			vNature[66] = &emojis[357];
			vNature[67] = &emojis[398];
			vNature[68] = &emojis[358];
			vNature[69] = &emojis[361];
			vNature[70] = &emojis[382];
			vNature[71] = &emojis[404];
			vNature[72] = &emojis[359];
			vNature[73] = &emojis[399];
			vNature[74] = &emojis[392];
			vNature[75] = &emojis[373];
			vNature[76] = &emojis[379];
			vNature[77] = &emojis[380];
			vNature[78] = &emojis[381];
			vNature[79] = &emojis[374];
			vNature[80] = &emojis[360];
			vNature[81] = &emojis[375];
			vNature[82] = &emojis[376];
			vNature[83] = &emojis[377];
			vNature[84] = &emojis[378];
			vNature[85] = &emojis[410];
			vNature[86] = &emojis[60];
			vNature[87] = &emojis[635];
			vNature[88] = &emojis[185];
			vNature[89] = &emojis[31];
			vNature[90] = &emojis[66];
			vNature[91] = &emojis[32];
			vNature[92] = &emojis[513];
			vNature[93] = &emojis[512];
			vNature[94] = &emojis[35];
			vNature[95] = &emojis[514];
			vNature[96] = &emojis[89];
			vNature[97] = &emojis[191];
			vNature[98] = &emojis[111];
			vNature[99] = &emojis[192];
			vNature[100] = &emojis[164];
			vNature[101] = &emojis[165];
			vNature[102] = &emojis[168];
			vNature[103] = &emojis[170];
			vNature[104] = &emojis[171];
			vNature[105] = &emojis[172];
			vNature[106] = &emojis[684];
			vNature[107] = &emojis[687];
			vNature[108] = &emojis[176];
			vNature[109] = &emojis[173];
			vNature[110] = &emojis[174];
			vNature[111] = &emojis[175];
			vNature[112] = &emojis[177];
			vNature[113] = &emojis[178];
			vNature[114] = &emojis[179];
			vNature[115] = &emojis[180];
			vNature[116] = &emojis[181];
			vNature[117] = &emojis[182];
			vNature[118] = &emojis[183];
			vNature[119] = &emojis[184];
			vNature[120] = &emojis[186];
			vNature[121] = &emojis[189];
			vNature[122] = &emojis[187];
			vNature[123] = &emojis[188];
			vNature[124] = &emojis[190];
		}
		return vNature;
	} break;

	case dbietFood: {
		static QVector<EmojiPtr> vFood;
		if (vFood.isEmpty()) {
			vFood.resize(58);
			vFood[0] = &emojis[213];
			vFood[1] = &emojis[214];
			vFood[2] = &emojis[205];
			vFood[3] = &emojis[240];
			vFood[4] = &emojis[215];
			vFood[5] = &emojis[216];
			vFood[6] = &emojis[217];
			vFood[7] = &emojis[218];
			vFood[8] = &emojis[219];
			vFood[9] = &emojis[220];
			vFood[10] = &emojis[221];
			vFood[11] = &emojis[222];
			vFood[12] = &emojis[223];
			vFood[13] = &emojis[224];
			vFood[14] = &emojis[225];
			vFood[15] = &emojis[226];
			vFood[16] = &emojis[227];
			vFood[17] = &emojis[228];
			vFood[18] = &emojis[229];
			vFood[19] = &emojis[230];
			vFood[20] = &emojis[231];
			vFood[21] = &emojis[232];
			vFood[22] = &emojis[233];
			vFood[23] = &emojis[234];
			vFood[24] = &emojis[235];
			vFood[25] = &emojis[236];
			vFood[26] = &emojis[237];
			vFood[27] = &emojis[238];
			vFood[28] = &emojis[239];
			vFood[29] = &emojis[241];
			vFood[30] = &emojis[242];
			vFood[31] = &emojis[243];
			vFood[32] = &emojis[244];
			vFood[33] = &emojis[245];
			vFood[34] = &emojis[246];
			vFood[35] = &emojis[247];
			vFood[36] = &emojis[248];
			vFood[37] = &emojis[249];
			vFood[38] = &emojis[250];
			vFood[39] = &emojis[251];
			vFood[40] = &emojis[252];
			vFood[41] = &emojis[253];
			vFood[42] = &emojis[254];
			vFood[43] = &emojis[255];
			vFood[44] = &emojis[256];
			vFood[45] = &emojis[257];
			vFood[46] = &emojis[258];
			vFood[47] = &emojis[259];
			vFood[48] = &emojis[260];
			vFood[49] = &emojis[261];
			vFood[50] = &emojis[36];
			vFood[51] = &emojis[262];
			vFood[52] = &emojis[263];
			vFood[53] = &emojis[264];
			vFood[54] = &emojis[265];
			vFood[55] = &emojis[266];
			vFood[56] = &emojis[267];
			vFood[57] = &emojis[268];
		}
		return vFood;
	} break;

	case dbietCelebration: {
		static QVector<EmojiPtr> vCelebration;
		if (vCelebration.isEmpty()) {
			vCelebration.resize(39);
			vCelebration[0] = &emojis[269];
			vCelebration[1] = &emojis[270];
			vCelebration[2] = &emojis[271];
			vCelebration[3] = &emojis[272];
			vCelebration[4] = &emojis[273];
			vCelebration[5] = &emojis[280];
			vCelebration[6] = &emojis[282];
			vCelebration[7] = &emojis[286];
			vCelebration[8] = &emojis[275];
			vCelebration[9] = &emojis[276];
			vCelebration[10] = &emojis[278];
			vCelebration[11] = &emojis[279];
			vCelebration[12] = &emojis[277];
			vCelebration[13] = &emojis[517];
			vCelebration[14] = &emojis[86];
			vCelebration[15] = &emojis[511];
			vCelebration[16] = &emojis[288];
			vCelebration[17] = &emojis[427];
			vCelebration[18] = &emojis[283];
			vCelebration[19] = &emojis[284];
			vCelebration[20] = &emojis[285];
			vCelebration[21] = &emojis[281];
			vCelebration[22] = &emojis[345];
			vCelebration[23] = &emojis[487];
			vCelebration[24] = &emojis[97];
			vCelebration[25] = &emojis[494];
			vCelebration[26] = &emojis[486];
			vCelebration[27] = &emojis[495];
			vCelebration[28] = &emojis[504];
			vCelebration[29] = &emojis[493];
			vCelebration[30] = &emojis[497];
			vCelebration[31] = &emojis[496];
			vCelebration[32] = &emojis[498];
			vCelebration[33] = &emojis[503];
			vCelebration[34] = &emojis[505];
			vCelebration[35] = &emojis[502];
			vCelebration[36] = &emojis[501];
			vCelebration[37] = &emojis[500];
			vCelebration[38] = &emojis[499];
		}
		return vCelebration;
	} break;

	case dbietActivity: {
		static QVector<EmojiPtr> vActivity;
		if (vActivity.isEmpty()) {
			vActivity.resize(53);
			vActivity[0] = &emojis[324];
			vActivity[1] = &emojis[819];
			vActivity[2] = &emojis[477];
			vActivity[3] = &emojis[800];
			vActivity[4] = &emojis[330];
			vActivity[5] = &emojis[325];
			vActivity[6] = &emojis[829];
			vActivity[7] = &emojis[323];
			vActivity[8] = &emojis[320];
			vActivity[9] = &emojis[65];
			vActivity[10] = &emojis[817];
			vActivity[11] = &emojis[818];
			vActivity[12] = &emojis[327];
			vActivity[13] = &emojis[73];
			vActivity[14] = &emojis[292];
			vActivity[15] = &emojis[63];
			vActivity[16] = &emojis[321];
			vActivity[17] = &emojis[328];
			vActivity[18] = &emojis[64];
			vActivity[19] = &emojis[319];
			vActivity[20] = &emojis[329];
			vActivity[21] = &emojis[71];
			vActivity[22] = &emojis[326];
			vActivity[23] = &emojis[318];
			vActivity[24] = &emojis[322];
			vActivity[25] = &emojis[314];
			vActivity[26] = &emojis[313];
			vActivity[27] = &emojis[316];
			vActivity[28] = &emojis[312];
			vActivity[29] = &emojis[315];
			vActivity[30] = &emojis[310];
			vActivity[31] = &emojis[311];
			vActivity[32] = &emojis[317];
			vActivity[33] = &emojis[296];
			vActivity[34] = &emojis[293];
			vActivity[35] = &emojis[302];
			vActivity[36] = &emojis[300];
			vActivity[37] = &emojis[298];
			vActivity[38] = &emojis[299];
			vActivity[39] = &emojis[301];
			vActivity[40] = &emojis[297];
			vActivity[41] = &emojis[304];
			vActivity[42] = &emojis[306];
			vActivity[43] = &emojis[308];
			vActivity[44] = &emojis[305];
			vActivity[45] = &emojis[307];
			vActivity[46] = &emojis[303];
			vActivity[47] = &emojis[309];
			vActivity[48] = &emojis[129];
			vActivity[49] = &emojis[128];
			vActivity[50] = &emojis[289];
			vActivity[51] = &emojis[290];
			vActivity[52] = &emojis[291];
		}
		return vActivity;
	} break;

	case dbietTravel: {
		static QVector<EmojiPtr> vTravel;
		if (vTravel.isEmpty()) {
			vTravel.resize(122);
			vTravel[0] = &emojis[768];
			vTravel[1] = &emojis[795];
			vTravel[2] = &emojis[767];
			vTravel[3] = &emojis[776];
			vTravel[4] = &emojis[794];
			vTravel[5] = &emojis[769];
			vTravel[6] = &emojis[770];
			vTravel[7] = &emojis[771];
			vTravel[8] = &emojis[772];
			vTravel[9] = &emojis[773];
			vTravel[10] = &emojis[774];
			vTravel[11] = &emojis[775];
			vTravel[12] = &emojis[777];
			vTravel[13] = &emojis[778];
			vTravel[14] = &emojis[779];
			vTravel[15] = &emojis[781];
			vTravel[16] = &emojis[782];
			vTravel[17] = &emojis[783];
			vTravel[18] = &emojis[784];
			vTravel[19] = &emojis[785];
			vTravel[20] = &emojis[805];
			vTravel[21] = &emojis[786];
			vTravel[22] = &emojis[787];
			vTravel[23] = &emojis[788];
			vTravel[24] = &emojis[789];
			vTravel[25] = &emojis[790];
			vTravel[26] = &emojis[791];
			vTravel[27] = &emojis[792];
			vTravel[28] = &emojis[793];
			vTravel[29] = &emojis[815];
			vTravel[30] = &emojis[780];
			vTravel[31] = &emojis[74];
			vTravel[32] = &emojis[804];
			vTravel[33] = &emojis[803];
			vTravel[34] = &emojis[802];
			vTravel[35] = &emojis[765];
			vTravel[36] = &emojis[766];
			vTravel[37] = &emojis[77];
			vTravel[38] = &emojis[532];
			vTravel[39] = &emojis[58];
			vTravel[40] = &emojis[799];
			vTravel[41] = &emojis[801];
			vTravel[42] = &emojis[72];
			vTravel[43] = &emojis[798];
			vTravel[44] = &emojis[797];
			vTravel[45] = &emojis[796];
			vTravel[46] = &emojis[831];
			vTravel[47] = &emojis[832];
			vTravel[48] = &emojis[833];
			vTravel[49] = &emojis[834];
			vTravel[50] = &emojis[526];
			vTravel[51] = &emojis[528];
			vTravel[52] = &emojis[529];
			vTravel[53] = &emojis[527];
			vTravel[54] = &emojis[686];
			vTravel[55] = &emojis[688];
			vTravel[56] = &emojis[161];
			vTravel[57] = &emojis[685];
			vTravel[58] = &emojis[70];
			vTravel[59] = &emojis[347];
			vTravel[60] = &emojis[346];
			vTravel[61] = &emojis[167];
			vTravel[62] = &emojis[166];
			vTravel[63] = &emojis[163];
			vTravel[64] = &emojis[169];
			vTravel[65] = &emojis[331];
			vTravel[66] = &emojis[332];
			vTravel[67] = &emojis[333];
			vTravel[68] = &emojis[343];
			vTravel[69] = &emojis[344];
			vTravel[70] = &emojis[334];
			vTravel[71] = &emojis[335];
			vTravel[72] = &emojis[336];
			vTravel[73] = &emojis[337];
			vTravel[74] = &emojis[339];
			vTravel[75] = &emojis[340];
			vTravel[76] = &emojis[492];
			vTravel[77] = &emojis[69];
			vTravel[78] = &emojis[341];
			vTravel[79] = &emojis[342];
			vTravel[80] = &emojis[875];
			vTravel[81] = &emojis[874];
			vTravel[82] = &emojis[876];
			vTravel[83] = &emojis[877];
			vTravel[84] = &emojis[878];
			vTravel[85] = &emojis[880];
			vTravel[86] = &emojis[881];
			vTravel[87] = &emojis[882];
			vTravel[88] = &emojis[884];
			vTravel[89] = &emojis[886];
			vTravel[90] = &emojis[887];
			vTravel[91] = &emojis[883];
			vTravel[92] = &emojis[889];
			vTravel[93] = &emojis[893];
			vTravel[94] = &emojis[890];
			vTravel[95] = &emojis[891];
			vTravel[96] = &emojis[892];
			vTravel[97] = &emojis[894];
			vTravel[98] = &emojis[895];
			vTravel[99] = &emojis[896];
			vTravel[100] = &emojis[897];
			vTravel[101] = &emojis[899];
			vTravel[102] = &emojis[898];
			vTravel[103] = &emojis[900];
			vTravel[104] = &emojis[902];
			vTravel[105] = &emojis[901];
			vTravel[106] = &emojis[903];
			vTravel[107] = &emojis[904];
			vTravel[108] = &emojis[906];
			vTravel[109] = &emojis[905];
			vTravel[110] = &emojis[907];
			vTravel[111] = &emojis[908];
			vTravel[112] = &emojis[910];
			vTravel[113] = &emojis[914];
			vTravel[114] = &emojis[885];
			vTravel[115] = &emojis[909];
			vTravel[116] = &emojis[879];
			vTravel[117] = &emojis[911];
			vTravel[118] = &emojis[888];
			vTravel[119] = &emojis[912];
			vTravel[120] = &emojis[873];
			vTravel[121] = &emojis[913];
		}
		return vTravel;
	} break;

	case dbietObjects: {
		static QVector<EmojiPtr> vObjects;
		if (vObjects.isEmpty()) {
			vObjects.resize(345);
			vObjects[0] = &emojis[14];
			vObjects[1] = &emojis[587];
			vObjects[2] = &emojis[588];
			vObjects[3] = &emojis[533];
			vObjects[4] = &emojis[20];
			vObjects[5] = &emojis[21];
			vObjects[6] = &emojis[15];
			vObjects[7] = &emojis[593];
			vObjects[8] = &emojis[594];
			vObjects[9] = &emojis[294];
			vObjects[10] = &emojis[595];
			vObjects[11] = &emojis[596];
			vObjects[12] = &emojis[569];
			vObjects[13] = &emojis[568];
			vObjects[14] = &emojis[33];
			vObjects[15] = &emojis[570];
			vObjects[16] = &emojis[535];
			vObjects[17] = &emojis[536];
			vObjects[18] = &emojis[537];
			vObjects[19] = &emojis[538];
			vObjects[20] = &emojis[597];
			vObjects[21] = &emojis[609];
			vObjects[22] = &emojis[610];
			vObjects[23] = &emojis[507];
			vObjects[24] = &emojis[636];
			vObjects[25] = &emojis[571];
			vObjects[26] = &emojis[525];
			vObjects[27] = &emojis[530];
			vObjects[28] = &emojis[522];
			vObjects[29] = &emojis[488];
			vObjects[30] = &emojis[162];
			vObjects[31] = &emojis[439];
			vObjects[32] = &emojis[437];
			vObjects[33] = &emojis[438];
			vObjects[34] = &emojis[534];
			vObjects[35] = &emojis[287];
			vObjects[36] = &emojis[478];
			vObjects[37] = &emojis[429];
			vObjects[38] = &emojis[428];
			vObjects[39] = &emojis[443];
			vObjects[40] = &emojis[442];
			vObjects[41] = &emojis[444];
			vObjects[42] = &emojis[440];
			vObjects[43] = &emojis[441];
			vObjects[44] = &emojis[435];
			vObjects[45] = &emojis[433];
			vObjects[46] = &emojis[434];
			vObjects[47] = &emojis[436];
			vObjects[48] = &emojis[431];
			vObjects[49] = &emojis[430];
			vObjects[50] = &emojis[432];
			vObjects[51] = &emojis[807];
			vObjects[52] = &emojis[828];
			vObjects[53] = &emojis[830];
			vObjects[54] = &emojis[826];
			vObjects[55] = &emojis[482];
			vObjects[56] = &emojis[483];
			vObjects[57] = &emojis[484];
			vObjects[58] = &emojis[642];
			vObjects[59] = &emojis[643];
			vObjects[60] = &emojis[644];
			vObjects[61] = &emojis[637];
			vObjects[62] = &emojis[640];
			vObjects[63] = &emojis[639];
			vObjects[64] = &emojis[638];
			vObjects[65] = &emojis[509];
			vObjects[66] = &emojis[809];
			vObjects[67] = &emojis[641];
			vObjects[68] = &emojis[620];
			vObjects[69] = &emojis[586];
			vObjects[70] = &emojis[615];
			vObjects[71] = &emojis[78];
			vObjects[72] = &emojis[579];
			vObjects[73] = &emojis[578];
			vObjects[74] = &emojis[577];
			vObjects[75] = &emojis[575];
			vObjects[76] = &emojis[574];
			vObjects[77] = &emojis[576];
			vObjects[78] = &emojis[585];
			vObjects[79] = &emojis[584];
			vObjects[80] = &emojis[580];
			vObjects[81] = &emojis[581];
			vObjects[82] = &emojis[582];
			vObjects[83] = &emojis[583];
			vObjects[84] = &emojis[542];
			vObjects[85] = &emojis[541];
			vObjects[86] = &emojis[555];
			vObjects[87] = &emojis[546];
			vObjects[88] = &emojis[547];
			vObjects[89] = &emojis[548];
			vObjects[90] = &emojis[543];
			vObjects[91] = &emojis[544];
			vObjects[92] = &emojis[603];
			vObjects[93] = &emojis[604];
			vObjects[94] = &emojis[566];
			vObjects[95] = &emojis[549];
			vObjects[96] = &emojis[560];
			vObjects[97] = &emojis[557];
			vObjects[98] = &emojis[558];
			vObjects[99] = &emojis[556];
			vObjects[100] = &emojis[559];
			vObjects[101] = &emojis[561];
			vObjects[102] = &emojis[562];
			vObjects[103] = &emojis[563];
			vObjects[104] = &emojis[564];
			vObjects[105] = &emojis[545];
			vObjects[106] = &emojis[621];
			vObjects[107] = &emojis[552];
			vObjects[108] = &emojis[550];
			vObjects[109] = &emojis[75];
			vObjects[110] = &emojis[554];
			vObjects[111] = &emojis[551];
			vObjects[112] = &emojis[553];
			vObjects[113] = &emojis[806];
			vObjects[114] = &emojis[539];
			vObjects[115] = &emojis[540];
			vObjects[116] = &emojis[83];
			vObjects[117] = &emojis[82];
			vObjects[118] = &emojis[567];
			vObjects[119] = &emojis[613];
			vObjects[120] = &emojis[614];
			vObjects[121] = &emojis[616];
			vObjects[122] = &emojis[617];
			vObjects[123] = &emojis[573];
			vObjects[124] = &emojis[572];
			vObjects[125] = &emojis[606];
			vObjects[126] = &emojis[607];
			vObjects[127] = &emojis[608];
			vObjects[128] = &emojis[605];
			vObjects[129] = &emojis[510];
			vObjects[130] = &emojis[618];
			vObjects[131] = &emojis[619];
			vObjects[132] = &emojis[519];
			vObjects[133] = &emojis[518];
			vObjects[134] = &emojis[821];
			vObjects[135] = &emojis[611];
			vObjects[136] = &emojis[612];
			vObjects[137] = &emojis[808];
			vObjects[138] = &emojis[68];
			vObjects[139] = &emojis[565];
			vObjects[140] = &emojis[820];
			vObjects[141] = &emojis[812];
			vObjects[142] = &emojis[816];
			vObjects[143] = &emojis[814];
			vObjects[144] = &emojis[591];
			vObjects[145] = &emojis[628];
			vObjects[146] = &emojis[159];
			vObjects[147] = &emojis[158];
			vObjects[148] = &emojis[520];
			vObjects[149] = &emojis[116];
			vObjects[150] = &emojis[115];
			vObjects[151] = &emojis[151];
			vObjects[152] = &emojis[152];
			vObjects[153] = &emojis[149];
			vObjects[154] = &emojis[153];
			vObjects[155] = &emojis[147];
			vObjects[156] = &emojis[155];
			vObjects[157] = &emojis[157];
			vObjects[158] = &emojis[154];
			vObjects[159] = &emojis[156];
			vObjects[160] = &emojis[150];
			vObjects[161] = &emojis[146];
			vObjects[162] = &emojis[145];
			vObjects[163] = &emojis[148];
			vObjects[164] = &emojis[531];
			vObjects[165] = &emojis[90];
			vObjects[166] = &emojis[87];
			vObjects[167] = &emojis[92];
			vObjects[168] = &emojis[76];
			vObjects[169] = &emojis[88];
			vObjects[170] = &emojis[589];
			vObjects[171] = &emojis[590];
			vObjects[172] = &emojis[144];
			vObjects[173] = &emojis[130];
			vObjects[174] = &emojis[131];
			vObjects[175] = &emojis[134];
			vObjects[176] = &emojis[135];
			vObjects[177] = &emojis[132];
			vObjects[178] = &emojis[142];
			vObjects[179] = &emojis[138];
			vObjects[180] = &emojis[133];
			vObjects[181] = &emojis[827];
			vObjects[182] = &emojis[136];
			vObjects[183] = &emojis[137];
			vObjects[184] = &emojis[139];
			vObjects[185] = &emojis[140];
			vObjects[186] = &emojis[141];
			vObjects[187] = &emojis[143];
			vObjects[188] = &emojis[338];
			vObjects[189] = &emojis[39];
			vObjects[190] = &emojis[40];
			vObjects[191] = &emojis[41];
			vObjects[192] = &emojis[42];
			vObjects[193] = &emojis[43];
			vObjects[194] = &emojis[44];
			vObjects[195] = &emojis[45];
			vObjects[196] = &emojis[46];
			vObjects[197] = &emojis[47];
			vObjects[198] = &emojis[48];
			vObjects[199] = &emojis[49];
			vObjects[200] = &emojis[50];
			vObjects[201] = &emojis[824];
			vObjects[202] = &emojis[822];
			vObjects[203] = &emojis[823];
			vObjects[204] = &emojis[825];
			vObjects[205] = &emojis[57];
			vObjects[206] = &emojis[813];
			vObjects[207] = &emojis[810];
			vObjects[208] = &emojis[811];
			vObjects[209] = &emojis[25];
			vObjects[210] = &emojis[26];
			vObjects[211] = &emojis[658];
			vObjects[212] = &emojis[659];
			vObjects[213] = &emojis[16];
			vObjects[214] = &emojis[17];
			vObjects[215] = &emojis[18];
			vObjects[216] = &emojis[19];
			vObjects[217] = &emojis[101];
			vObjects[218] = &emojis[106];
			vObjects[219] = &emojis[107];
			vObjects[220] = &emojis[108];
			vObjects[221] = &emojis[9];
			vObjects[222] = &emojis[10];
			vObjects[223] = &emojis[11];
			vObjects[224] = &emojis[8];
			vObjects[225] = &emojis[7];
			vObjects[226] = &emojis[6];
			vObjects[227] = &emojis[602];
			vObjects[228] = &emojis[13];
			vObjects[229] = &emojis[12];
			vObjects[230] = &emojis[104];
			vObjects[231] = &emojis[105];
			vObjects[232] = &emojis[598];
			vObjects[233] = &emojis[599];
			vObjects[234] = &emojis[600];
			vObjects[235] = &emojis[117];
			vObjects[236] = &emojis[118];
			vObjects[237] = &emojis[119];
			vObjects[238] = &emojis[120];
			vObjects[239] = &emojis[121];
			vObjects[240] = &emojis[122];
			vObjects[241] = &emojis[123];
			vObjects[242] = &emojis[124];
			vObjects[243] = &emojis[125];
			vObjects[244] = &emojis[126];
			vObjects[245] = &emojis[127];
			vObjects[246] = &emojis[629];
			vObjects[247] = &emojis[632];
			vObjects[248] = &emojis[634];
			vObjects[249] = &emojis[631];
			vObjects[250] = &emojis[630];
			vObjects[251] = &emojis[5];
			vObjects[252] = &emojis[592];
			vObjects[253] = &emojis[295];
			vObjects[254] = &emojis[633];
			vObjects[255] = &emojis[98];
			vObjects[256] = &emojis[99];
			vObjects[257] = &emojis[113];
			vObjects[258] = &emojis[100];
			vObjects[259] = &emojis[85];
			vObjects[260] = &emojis[84];
			vObjects[261] = &emojis[601];
			vObjects[262] = &emojis[4];
			vObjects[263] = &emojis[0];
			vObjects[264] = &emojis[1];
			vObjects[265] = &emojis[523];
			vObjects[266] = &emojis[524];
			vObjects[267] = &emojis[102];
			vObjects[268] = &emojis[103];
			vObjects[269] = &emojis[114];
			vObjects[270] = &emojis[96];
			vObjects[271] = &emojis[93];
			vObjects[272] = &emojis[95];
			vObjects[273] = &emojis[94];
			vObjects[274] = &emojis[2];
			vObjects[275] = &emojis[3];
			vObjects[276] = &emojis[91];
			vObjects[277] = &emojis[112];
			vObjects[278] = &emojis[521];
			vObjects[279] = &emojis[624];
			vObjects[280] = &emojis[623];
			vObjects[281] = &emojis[625];
			vObjects[282] = &emojis[627];
			vObjects[283] = &emojis[626];
			vObjects[284] = &emojis[160];
			vObjects[285] = &emojis[22];
			vObjects[286] = &emojis[67];
			vObjects[287] = &emojis[645];
			vObjects[288] = &emojis[646];
			vObjects[289] = &emojis[647];
			vObjects[290] = &emojis[59];
			vObjects[291] = &emojis[55];
			vObjects[292] = &emojis[56];
			vObjects[293] = &emojis[508];
			vObjects[294] = &emojis[506];
			vObjects[295] = &emojis[51];
			vObjects[296] = &emojis[52];
			vObjects[297] = &emojis[53];
			vObjects[298] = &emojis[54];
			vObjects[299] = &emojis[34];
			vObjects[300] = &emojis[61];
			vObjects[301] = &emojis[62];
			vObjects[302] = &emojis[622];
			vObjects[303] = &emojis[650];
			vObjects[304] = &emojis[651];
			vObjects[305] = &emojis[656];
			vObjects[306] = &emojis[657];
			vObjects[307] = &emojis[654];
			vObjects[308] = &emojis[655];
			vObjects[309] = &emojis[652];
			vObjects[310] = &emojis[653];
			vObjects[311] = &emojis[23];
			vObjects[312] = &emojis[24];
			vObjects[313] = &emojis[109];
			vObjects[314] = &emojis[110];
			vObjects[315] = &emojis[28];
			vObjects[316] = &emojis[27];
			vObjects[317] = &emojis[30];
			vObjects[318] = &emojis[29];
			vObjects[319] = &emojis[648];
			vObjects[320] = &emojis[649];
			vObjects[321] = &emojis[660];
			vObjects[322] = &emojis[661];
			vObjects[323] = &emojis[662];
			vObjects[324] = &emojis[663];
			vObjects[325] = &emojis[664];
			vObjects[326] = &emojis[665];
			vObjects[327] = &emojis[666];
			vObjects[328] = &emojis[667];
			vObjects[329] = &emojis[668];
			vObjects[330] = &emojis[669];
			vObjects[331] = &emojis[670];
			vObjects[332] = &emojis[671];
			vObjects[333] = &emojis[672];
			vObjects[334] = &emojis[673];
			vObjects[335] = &emojis[674];
			vObjects[336] = &emojis[675];
			vObjects[337] = &emojis[676];
			vObjects[338] = &emojis[677];
			vObjects[339] = &emojis[678];
			vObjects[340] = &emojis[679];
			vObjects[341] = &emojis[680];
			vObjects[342] = &emojis[681];
			vObjects[343] = &emojis[682];
			vObjects[344] = &emojis[683];
		}
		return vObjects;
	} break;

	};

	EmojiPack result;
	result.reserve(cGetRecentEmojis().size());
	for (RecentEmojiPack::const_iterator i = cGetRecentEmojis().cbegin(), e = cGetRecentEmojis().cend(); i != e; ++i) {
		result.push_back(i->first);
	}
	return result;
}

