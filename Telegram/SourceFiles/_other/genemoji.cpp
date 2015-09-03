/*
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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "genemoji.h"

#include <QtCore/QtPlugin>
#include <QtGui/QFontDatabase>

#ifdef Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

#ifdef Q_OS_MAC
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
Q_IMPORT_PLUGIN(QDDSPlugin)
Q_IMPORT_PLUGIN(QICNSPlugin)
Q_IMPORT_PLUGIN(QICOPlugin)
Q_IMPORT_PLUGIN(QJp2Plugin)
Q_IMPORT_PLUGIN(QMngPlugin)
Q_IMPORT_PLUGIN(QTgaPlugin)
Q_IMPORT_PLUGIN(QTiffPlugin)
Q_IMPORT_PLUGIN(QWbmpPlugin)
Q_IMPORT_PLUGIN(QWebpPlugin)
#else
//#error Only Mac OS X is supported
#endif

typedef quint32 uint32;
typedef quint64 uint64;

struct EmojiData {
	uint32 code, code2;
	int x, y;
	int category, index;
	uint32 color;
};

// copied from emojibox.cpp
struct EmojiReplace {
	uint32 code;
	const char *replace;
};

EmojiReplace replaces[] = {
	{ 0xD83DDE0AU, ":-)" },
	{ 0xD83DDE0DU, "8-)" },
	{ 0x2764U, "<3" },
	{ 0xD83DDC8BU, ":kiss:" },
	{ 0xD83DDE01U, ":grin:" },
	{ 0xD83DDE02U, ":joy:" },
	{ 0xD83DDE1AU, ":-*" },
	{ 0xD83DDE06U, "xD" },
	{ 0xD83DDC4DU, ":like:" },
	{ 0xD83DDC4EU, ":dislike:" },
	{ 0x261DU, ":up:" },
	{ 0x270CU, ":v:" },
	{ 0xD83DDC4CU, ":ok:" },
	{ 0xD83DDE0EU, "B-)" },
	{ 0xD83DDE03U, ":-D" },
	{ 0xD83DDE09U, ";-)" },
	{ 0xD83DDE1CU, ";-P" },
	{ 0xD83DDE0BU, ":-p" },
	{ 0xD83DDE14U, "3(" },
	{ 0xD83DDE1EU, ":-(" },
	{ 0xD83DDE0FU, ":]" },
	{ 0xD83DDE22U, ":'(" },
	{ 0xD83DDE2DU, ":_(" },
	{ 0xD83DDE29U, ":((" },
	{ 0xD83DDE28U, ":o" },
	{ 0xD83DDE10U, ":|" },
	{ 0xD83DDE0CU, "3-)" },
	{ 0xD83DDE20U, ">(" },
	{ 0xD83DDE21U, ">((" },
	{ 0xD83DDE07U, "O:)" },
	{ 0xD83DDE30U, ";o" },
	{ 0xD83DDE33U, "8|" },
	{ 0xD83DDE32U, "8o" },
	{ 0xD83DDE37U, ":X" },
	{ 0xD83DDE08U, "}:)" },
};
const uint32 replacesCount = sizeof(replaces) / sizeof(EmojiReplace);
typedef QMap<QString, uint32> ReplaceMap;
ReplaceMap replaceMap;

static const int variantsCount = 5, inRow = 40, imSizes[] = { 18, 22, 27, 36, 45, 180 }, badSizes[] = { 1, 1, 0, 0, 0 };
static const int emojiFontSizes[] = { 14, 20, 27, 36, 45, 180 };
static const int emojiDeltas[] = { 15, 20, 25, 34, 42, 167 };
static const char *variantPostfix[] = { "", "_125x", "_150x", "_200x", "_250x" };
static const char *variantNames[] = { "dbisOne", "dbisOneAndQuarter", "dbisOneAndHalf", "dbisTwo" };

uint64 emojiColors[] = {
	0xD83CDFFBLLU,
	0xD83CDFFCLLU,
	0xD83CDFFDLLU,
	0xD83CDFFELLU,
	0xD83CDFFFLLU,
};
uint64 emojiColored[] = {
	0xD83DDC76LLU,
	0xD83DDC66LLU,
	0xD83DDC67LLU,
	0xD83DDC68LLU,
	0xD83DDC69LLU,

	0xD83DDC70LLU,
	0xD83DDC71LLU,
	0xD83DDC72LLU,
	0xD83DDC73LLU,
	0xD83DDC74LLU,
	0xD83DDC75LLU,
	0xD83DDC6ELLU,
	0xD83DDC77LLU,
	0xD83DDC78LLU,
	0xD83DDC82LLU,
	0xD83DDC7CLLU,
	0xD83CDF85LLU,

	0xD83DDE47LLU,
	0xD83DDC81LLU,
	0xD83DDE45LLU,
	0xD83DDE46LLU,
	0xD83DDE4BLLU,
	0xD83DDE4ELLU,
	0xD83DDE4DLLU,
	0xD83DDC86LLU,
	0xD83DDC87LLU,

	0xD83DDE4CLLU,
	0xD83DDC4FLLU,
	0xD83DDC42LLU,

	0xD83DDC43LLU,

	0xD83DDC85LLU,
	0xD83DDC4BLLU,
	0xD83DDC4DLLU,
	0xD83DDC4ELLU,
	0x261DLLU,
	0xD83DDC46LLU,
	0xD83DDC47LLU,
	0xD83DDC48LLU,
	0xD83DDC49LLU,
	0xD83DDC4CLLU,
	0x270CLLU,
	0xD83DDC4ALLU,
	0x270ALLU,
	0x270BLLU,
	0xD83DDCAALLU,
	0xD83DDC50LLU,
	0xD83DDE4FLLU,

	0xD83CDFC3LLU,
	0xD83DDEB6LLU,
	0xD83DDC83LLU,
	0xD83DDEA3LLU,
	0xD83CDFCALLU,
	0xD83CDFC4LLU,
	0xD83DDEC0LLU,

	0xD83DDEB4LLU,
	0xD83DDEB5LLU,
	0xD83CDFC7LLU,
};
const char *emojiSequences[] = { // getter of sequence emojis is done manually
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7",
	"\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6",
	"\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7",
	"\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6",
	"\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6",
	"\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa6",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa6",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa6\xe2\x80\x8d\xf0\x9f\x91\xa6",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa7\xe2\x80\x8d\xf0\x9f\x91\xa7",

	"\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x91\xa9",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x91\xa8",

	"\xf0\x9f\x91\xa9\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa9",
	"\xf0\x9f\x91\xa8\xe2\x80\x8d\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x92\x8b\xe2\x80\x8d\xf0\x9f\x91\xa8",
};

typedef QMap<uint64, EmojiData> EmojisData;
EmojisData emojisData;

uint64 emojiCategory1[] = {
	0xD83DDE00LLU,
	0xD83DDE01LLU,
	0xD83DDE02LLU,
	0xD83DDE03LLU,
	0xD83DDE04LLU,
	0xD83DDE05LLU,
	0xD83DDE06LLU,
	0xD83DDE07LLU,
	0xD83DDE08LLU,
	0xD83DDC7FLLU,
	0xD83DDE09LLU,
	0xD83DDE0ALLU,
	0x263ALLU,
	0xD83DDE0BLLU,
	0xD83DDE0CLLU,
	0xD83DDE0DLLU,
	0xD83DDE0ELLU,
	0xD83DDE0FLLU,
	0xD83DDE10LLU,
	0xD83DDE11LLU,
	0xD83DDE12LLU,
	0xD83DDE13LLU,
	0xD83DDE14LLU,
	0xD83DDE15LLU,
	0xD83DDE16LLU,
	0xD83DDE17LLU,
	0xD83DDE18LLU,
	0xD83DDE19LLU,
	0xD83DDE1ALLU,
	0xD83DDE1BLLU,
	0xD83DDE1CLLU,
	0xD83DDE1DLLU,
	0xD83DDE1ELLU,
	0xD83DDE1FLLU,
	0xD83DDE20LLU,
	0xD83DDE21LLU,
	0xD83DDE22LLU,
	0xD83DDE23LLU,
	0xD83DDE24LLU,
	0xD83DDE25LLU,
	0xD83DDE26LLU,
	0xD83DDE27LLU,
	0xD83DDE28LLU,
	0xD83DDE29LLU,
	0xD83DDE2ALLU,
	0xD83DDE2BLLU,
	0xD83DDE2CLLU,
	0xD83DDE2DLLU,
	0xD83DDE2ELLU,
	0xD83DDE2FLLU,
	0xD83DDE30LLU,
	0xD83DDE31LLU,
	0xD83DDE32LLU,
	0xD83DDE33LLU,
	0xD83DDE34LLU,
	0xD83DDE35LLU,
	0xD83DDE36LLU,
	0xD83DDE37LLU,
	0xD83DDE38LLU,
	0xD83DDE39LLU,
	0xD83DDE3ALLU,
	0xD83DDE3BLLU,
	0xD83DDE3CLLU,
	0xD83DDE3DLLU,
	0xD83DDE3ELLU,
	0xD83DDE3FLLU,
	0xD83DDE40LLU,
	0xD83DDC63LLU,
	0xD83DDC64LLU,
	0xD83DDC65LLU,
	0xD83DDC76LLU,
	0xD83DDC66LLU,
	0xD83DDC67LLU,
	0xD83DDC68LLU,
	0xD83DDC69LLU,
	0xD83DDC6ALLU,
	0xFFFF0000LLU,//0xD83DDC68LLU,0x200DLLU,0xD83DDC69LLU,0x200DLLU,0xD83DDC67LLU,
	0xFFFF0001LLU,//0xD83DDC68LLU,0x200DLLU,0xD83DDC69LLU,0x200DLLU,0xD83DDC67LLU,0x200DLLU,0xD83DDC66LLU,
	0xFFFF0002LLU,//0xD83DDC68LLU,0x200DLLU,0xD83DDC69LLU,0x200DLLU,0xD83DDC66LLU,0x200DLLU,0xD83DDC66LLU,
	0xFFFF0003LLU,//0xD83DDC68LLU,0x200DLLU,0xD83DDC69LLU,0x200DLLU,0xD83DDC67LLU,0x200DLLU,0xD83DDC67LLU,
	0xFFFF0004LLU,//0xD83DDC69LLU,0x200DLLU,0xD83DDC69LLU,0x200DLLU,0xD83DDC66LLU,
	0xFFFF0005LLU,//0xD83DDC69LLU,0x200DLLU,0xD83DDC69LLU,0x200DLLU,0xD83DDC67LLU,
	0xFFFF0006LLU,//0xD83DDC69LLU,0x200DLLU,0xD83DDC69LLU,0x200DLLU,0xD83DDC67LLU,0x200DLLU,0xD83DDC66LLU,
	0xFFFF0007LLU,//0xD83DDC69LLU,0x200DLLU,0xD83DDC69LLU,0x200DLLU,0xD83DDC66LLU,0x200DLLU,0xD83DDC66LLU,
	0xFFFF0008LLU,//0xD83DDC69LLU,0x200DLLU,0xD83DDC69LLU,0x200DLLU,0xD83DDC67LLU,0x200DLLU,0xD83DDC67LLU,
	0xFFFF0009LLU,//0xD83DDC68LLU,0x200DLLU,0xD83DDC68LLU,0x200DLLU,0xD83DDC66LLU,
	0xFFFF000ALLU,//0xD83DDC68LLU,0x200DLLU,0xD83DDC68LLU,0x200DLLU,0xD83DDC67LLU,
	0xFFFF000BLLU,//0xD83DDC68LLU,0x200DLLU,0xD83DDC68LLU,0x200DLLU,0xD83DDC67LLU,0x200DLLU,0xD83DDC66LLU,
	0xFFFF000CLLU,//0xD83DDC68LLU,0x200DLLU,0xD83DDC68LLU,0x200DLLU,0xD83DDC66LLU,0x200DLLU,0xD83DDC66LLU,
	0xFFFF000DLLU,//0xD83DDC68LLU,0x200DLLU,0xD83DDC68LLU,0x200DLLU,0xD83DDC67LLU,0x200DLLU,0xD83DDC67LLU,
	0xD83DDC6BLLU,
	0xD83DDC6CLLU,
	0xD83DDC6DLLU,
	0xD83DDC6FLLU,
	0xD83DDC70LLU,
	0xD83DDC71LLU,
	0xD83DDC72LLU,
	0xD83DDC73LLU,
	0xD83DDC74LLU,
	0xD83DDC75LLU,
	0xD83DDC6ELLU,
	0xD83DDC77LLU,
	0xD83DDC78LLU,
	0xD83DDC82LLU,
	0xD83DDC7CLLU,
	0xD83CDF85LLU,
	0xD83DDC7BLLU,
	0xD83DDC79LLU,
	0xD83DDC7ALLU,
	0xD83DDCA9LLU,
	0xD83DDC80LLU,
	0xD83DDC7DLLU,
	0xD83DDC7ELLU,
	0xD83DDE47LLU,
	0xD83DDC81LLU,
	0xD83DDE45LLU,
	0xD83DDE46LLU,
	0xD83DDE4BLLU,
	0xD83DDE4ELLU,
	0xD83DDE4DLLU,
	0xD83DDC86LLU,
	0xD83DDC87LLU,
	0xD83DDC91LLU,
	0xFFFF000ELLU,//0xD83DDC69LLU,0x200DLLU,0x2764LLU,0xFE0FLLU,0x200DLLU,0xD83DDC69LLU,
	0xFFFF000FLLU,//0xD83DDC68LLU,0x200DLLU,0x2764LLU,0xFE0FLLU,0x200DLLU,0xD83DDC68LLU,
	0xD83DDC8FLLU,
	0xFFFF0010LLU,//0xD83DDC69LLU,0x200DLLU,0x2764LLU,0xFE0FLLU,0x200DLLU,0xD83DDC8BLLU,0x200DLLU,0xD83DDC69LLU,
	0xFFFF0011LLU,//0xD83DDC68LLU,0x200DLLU,0x2764LLU,0xFE0FLLU,0x200DLLU,0xD83DDC8BLLU,0x200DLLU,0xD83DDC68LLU,
	0xD83DDE4CLLU,
	0xD83DDC4FLLU,
	0xD83DDC42LLU,
	0xD83DDC40LLU,
	0xD83DDC43LLU,
	0xD83DDC44LLU,
	0xD83DDC8BLLU,
	0xD83DDC45LLU,
	0xD83DDC85LLU,
	0xD83DDC4BLLU,
	0xD83DDC4DLLU,
	0xD83DDC4ELLU,
	0x261DLLU,
	0xD83DDC46LLU,
	0xD83DDC47LLU,
	0xD83DDC48LLU,
	0xD83DDC49LLU,
	0xD83DDC4CLLU,
	0x270CLLU,
	0xD83DDC4ALLU,
	0x270ALLU,
	0x270BLLU,
	0xD83DDCAALLU,
	0xD83DDC50LLU,
	0xD83DDE4FLLU,
};

uint64 emojiCategory2[] = {
	0xD83CDF31LLU,
	0xD83CDF32LLU,
	0xD83CDF33LLU,
	0xD83CDF34LLU,
	0xD83CDF35LLU,
	0xD83CDF37LLU,
	0xD83CDF38LLU,
	0xD83CDF39LLU,
	0xD83CDF3ALLU,
	0xD83CDF3BLLU,
	0xD83CDF3CLLU,
	0xD83DDC90LLU,
	0xD83CDF3ELLU,
	0xD83CDF3FLLU,
	0xD83CDF40LLU,
	0xD83CDF41LLU,
	0xD83CDF42LLU,
	0xD83CDF43LLU,
	0xD83CDF44LLU,
	0xD83CDF30LLU,
	0xD83DDC00LLU,
	0xD83DDC01LLU,
	0xD83DDC2DLLU,
	0xD83DDC39LLU,
	0xD83DDC02LLU,
	0xD83DDC03LLU,
	0xD83DDC04LLU,
	0xD83DDC2ELLU,
	0xD83DDC05LLU,
	0xD83DDC06LLU,
	0xD83DDC2FLLU,
	0xD83DDC07LLU,
	0xD83DDC30LLU,
	0xD83DDC08LLU,
	0xD83DDC31LLU,
	0xD83DDC0ELLU,
	0xD83DDC34LLU,
	0xD83DDC0FLLU,
	0xD83DDC11LLU,
	0xD83DDC10LLU,
	0xD83DDC13LLU,
	0xD83DDC14LLU,
	0xD83DDC24LLU,
	0xD83DDC23LLU,
	0xD83DDC25LLU,
	0xD83DDC26LLU,
	0xD83DDC27LLU,
	0xD83DDC18LLU,
	0xD83DDC2ALLU,
	0xD83DDC2BLLU,
	0xD83DDC17LLU,
	0xD83DDC16LLU,
	0xD83DDC37LLU,
	0xD83DDC3DLLU,
	0xD83DDC15LLU,
	0xD83DDC29LLU,
	0xD83DDC36LLU,
	0xD83DDC3ALLU,
	0xD83DDC3BLLU,
	0xD83DDC28LLU,
	0xD83DDC3CLLU,
	0xD83DDC35LLU,
	0xD83DDE48LLU,
	0xD83DDE49LLU,
	0xD83DDE4ALLU,
	0xD83DDC12LLU,
	0xD83DDC09LLU,
	0xD83DDC32LLU,
	0xD83DDC0ALLU,
	0xD83DDC0DLLU,
	0xD83DDC22LLU,
	0xD83DDC38LLU,
	0xD83DDC0BLLU,
	0xD83DDC33LLU,
	0xD83DDC2CLLU,
	0xD83DDC19LLU,
	0xD83DDC1FLLU,
	0xD83DDC20LLU,
	0xD83DDC21LLU,
	0xD83DDC1ALLU,
	0xD83DDC0CLLU,
	0xD83DDC1BLLU,
	0xD83DDC1CLLU,
	0xD83DDC1DLLU,
	0xD83DDC1ELLU,
	0xD83DDC3ELLU,
	0x26A1LLU,
	0xD83DDD25LLU,
	0xD83CDF19LLU,
	0x2600LLU,
	0x26C5LLU,
	0x2601LLU,
	0xD83DDCA7LLU,
	0xD83DDCA6LLU,
	0x2614LLU,
	0xD83DDCA8LLU,
	0x2744LLU,
	0xD83CDF1FLLU,
	0x2B50LLU,
	0xD83CDF20LLU,
	0xD83CDF04LLU,
	0xD83CDF05LLU,
	0xD83CDF08LLU,
	0xD83CDF0ALLU,
	0xD83CDF0BLLU,
	0xD83CDF0CLLU,
	0xD83DDDFBLLU,
	0xD83DDDFELLU,
	0xD83CDF10LLU,
	0xD83CDF0DLLU,
	0xD83CDF0ELLU,
	0xD83CDF0FLLU,
	0xD83CDF11LLU,
	0xD83CDF12LLU,
	0xD83CDF13LLU,
	0xD83CDF14LLU,
	0xD83CDF15LLU,
	0xD83CDF16LLU,
	0xD83CDF17LLU,
	0xD83CDF18LLU,
	0xD83CDF1ALLU,
	0xD83CDF1DLLU,
	0xD83CDF1BLLU,
	0xD83CDF1CLLU,
	0xD83CDF1ELLU,
};

uint64 emojiCategory3[] = {
	0xD83CDF45LLU,
	0xD83CDF46LLU,
	0xD83CDF3DLLU,
	0xD83CDF60LLU,
	0xD83CDF47LLU,
	0xD83CDF48LLU,
	0xD83CDF49LLU,
	0xD83CDF4ALLU,
	0xD83CDF4BLLU,
	0xD83CDF4CLLU,
	0xD83CDF4DLLU,
	0xD83CDF4ELLU,
	0xD83CDF4FLLU,
	0xD83CDF50LLU,
	0xD83CDF51LLU,
	0xD83CDF52LLU,
	0xD83CDF53LLU,
	0xD83CDF54LLU,
	0xD83CDF55LLU,
	0xD83CDF56LLU,
	0xD83CDF57LLU,
	0xD83CDF58LLU,
	0xD83CDF59LLU,
	0xD83CDF5ALLU,
	0xD83CDF5BLLU,
	0xD83CDF5CLLU,
	0xD83CDF5DLLU,
	0xD83CDF5ELLU,
	0xD83CDF5FLLU,
	0xD83CDF61LLU,
	0xD83CDF62LLU,
	0xD83CDF63LLU,
	0xD83CDF64LLU,
	0xD83CDF65LLU,
	0xD83CDF66LLU,
	0xD83CDF67LLU,
	0xD83CDF68LLU,
	0xD83CDF69LLU,
	0xD83CDF6ALLU,
	0xD83CDF6BLLU,
	0xD83CDF6CLLU,
	0xD83CDF6DLLU,
	0xD83CDF6ELLU,
	0xD83CDF6FLLU,
	0xD83CDF70LLU,
	0xD83CDF71LLU,
	0xD83CDF72LLU,
	0xD83CDF73LLU,
	0xD83CDF74LLU,
	0xD83CDF75LLU,
	0x2615LLU,
	0xD83CDF76LLU,
	0xD83CDF77LLU,
	0xD83CDF78LLU,
	0xD83CDF79LLU,
	0xD83CDF7ALLU,
	0xD83CDF7BLLU,
	0xD83CDF7CLLU,
};

uint64 emojiCategory4[] = {
	0xD83CDF80LLU,
	0xD83CDF81LLU,
	0xD83CDF82LLU,
	0xD83CDF83LLU,
	0xD83CDF84LLU,
	0xD83CDF8BLLU,
	0xD83CDF8DLLU,
	0xD83CDF91LLU,
	0xD83CDF86LLU,
	0xD83CDF87LLU,
	0xD83CDF89LLU,
	0xD83CDF8ALLU,
	0xD83CDF88LLU,
	0xD83DDCABLLU,
	0x2728LLU,
	0xD83DDCA5LLU,
	0xD83CDF93LLU,
	0xD83DDC51LLU,
	0xD83CDF8ELLU,
	0xD83CDF8FLLU,
	0xD83CDF90LLU,
	0xD83CDF8CLLU,
	0xD83CDFEELLU,
	0xD83DDC8DLLU,
	0x2764LLU,
	0xD83DDC94LLU,
	0xD83DDC8CLLU,
	0xD83DDC95LLU,
	0xD83DDC9ELLU,
	0xD83DDC93LLU,
	0xD83DDC97LLU,
	0xD83DDC96LLU,
	0xD83DDC98LLU,
	0xD83DDC9DLLU,
	0xD83DDC9FLLU,
	0xD83DDC9CLLU,
	0xD83DDC9BLLU,
	0xD83DDC9ALLU,
	0xD83DDC99LLU,
};

uint64 emojiCategory5[] = {
	0xD83CDFC3LLU,
	0xD83DDEB6LLU,
	0xD83DDC83LLU,
	0xD83DDEA3LLU,
	0xD83CDFCALLU,
	0xD83CDFC4LLU,
	0xD83DDEC0LLU,
	0xD83CDFC2LLU,
	0xD83CDFBFLLU,
	0x26C4LLU,
	0xD83DDEB4LLU,
	0xD83DDEB5LLU,
	0xD83CDFC7LLU,
	0x26FALLU,
	0xD83CDFA3LLU,
	0x26BDLLU,
	0xD83CDFC0LLU,
	0xD83CDFC8LLU,
	0x26BELLU,
	0xD83CDFBELLU,
	0xD83CDFC9LLU,
	0x26F3LLU,
	0xD83CDFC6LLU,
	0xD83CDFBDLLU,
	0xD83CDFC1LLU,
	0xD83CDFB9LLU,
	0xD83CDFB8LLU,
	0xD83CDFBBLLU,
	0xD83CDFB7LLU,
	0xD83CDFBALLU,
	0xD83CDFB5LLU,
	0xD83CDFB6LLU,
	0xD83CDFBCLLU,
	0xD83CDFA7LLU,
	0xD83CDFA4LLU,
	0xD83CDFADLLU,
	0xD83CDFABLLU,
	0xD83CDFA9LLU,
	0xD83CDFAALLU,
	0xD83CDFACLLU,
	0xD83CDFA8LLU,
	0xD83CDFAFLLU,
	0xD83CDFB1LLU,
	0xD83CDFB3LLU,
	0xD83CDFB0LLU,
	0xD83CDFB2LLU,
	0xD83CDFAELLU,
	0xD83CDFB4LLU,
	0xD83CDCCFLLU,
	0xD83CDC04LLU,
	0xD83CDFA0LLU,
	0xD83CDFA1LLU,
	0xD83CDFA2LLU,
};

uint64 emojiCategory6[] = {
	0xD83DDE83LLU,
	0xD83DDE9ELLU,
	0xD83DDE82LLU,
	0xD83DDE8BLLU,
	0xD83DDE9DLLU,
	0xD83DDE84LLU,
	0xD83DDE85LLU,
	0xD83DDE86LLU,
	0xD83DDE87LLU,
	0xD83DDE88LLU,
	0xD83DDE89LLU,
	0xD83DDE8ALLU,
	0xD83DDE8CLLU,
	0xD83DDE8DLLU,
	0xD83DDE8ELLU,
	0xD83DDE90LLU,
	0xD83DDE91LLU,
	0xD83DDE92LLU,
	0xD83DDE93LLU,
	0xD83DDE94LLU,
	0xD83DDEA8LLU,
	0xD83DDE95LLU,
	0xD83DDE96LLU,
	0xD83DDE97LLU,
	0xD83DDE98LLU,
	0xD83DDE99LLU,
	0xD83DDE9ALLU,
	0xD83DDE9BLLU,
	0xD83DDE9CLLU,
	0xD83DDEB2LLU,
	0xD83DDE8FLLU,
	0x26FDLLU,
	0xD83DDEA7LLU,
	0xD83DDEA6LLU,
	0xD83DDEA5LLU,
	0xD83DDE80LLU,
	0xD83DDE81LLU,
	0x2708LLU,
	0xD83DDCBALLU,
	0x2693LLU,
	0xD83DDEA2LLU,
	0xD83DDEA4LLU,
	0x26F5LLU,
	0xD83DDEA1LLU,
	0xD83DDEA0LLU,
	0xD83DDE9FLLU,
	0xD83DDEC2LLU,
	0xD83DDEC3LLU,
	0xD83DDEC4LLU,
	0xD83DDEC5LLU,
	0xD83DDCB4LLU,
	0xD83DDCB6LLU,
	0xD83DDCB7LLU,
	0xD83DDCB5LLU,
	0xD83DDDFDLLU,
	0xD83DDDFFLLU,
	0xD83CDF01LLU,
	0xD83DDDFCLLU,
	0x26F2LLU,
	0xD83CDFF0LLU,
	0xD83CDFEFLLU,
	0xD83CDF07LLU,
	0xD83CDF06LLU,
	0xD83CDF03LLU,
	0xD83CDF09LLU,
	0xD83CDFE0LLU,
	0xD83CDFE1LLU,
	0xD83CDFE2LLU,
	0xD83CDFECLLU,
	0xD83CDFEDLLU,
	0xD83CDFE3LLU,
	0xD83CDFE4LLU,
	0xD83CDFE5LLU,
	0xD83CDFE6LLU,
	0xD83CDFE8LLU,
	0xD83CDFE9LLU,
	0xD83DDC92LLU,
	0x26EALLU,
	0xD83CDFEALLU,
	0xD83CDFEBLLU,
	0xD83CDDE6D83CDDFALLU,
	0xD83CDDE6D83CDDF9LLU,
	0xD83CDDE7D83CDDEALLU,
	0xD83CDDE7D83CDDF7LLU,
	0xD83CDDE8D83CDDE6LLU,
	0xD83CDDE8D83CDDF1LLU,
	0xD83CDDE8D83CDDF3LLU,
	0xD83CDDE8D83CDDF4LLU,
	0xD83CDDE9D83CDDF0LLU,
	0xD83CDDEBD83CDDEELLU,
	0xD83CDDEBD83CDDF7LLU,
	0xD83CDDE9D83CDDEALLU,
	0xD83CDDEDD83CDDF0LLU,
	0xD83CDDEED83CDDF3LLU,
	0xD83CDDEED83CDDE9LLU,
	0xD83CDDEED83CDDEALLU,
	0xD83CDDEED83CDDF1LLU,
	0xD83CDDEED83CDDF9LLU,
	0xD83CDDEFD83CDDF5LLU,
	0xD83CDDF0D83CDDF7LLU,
	0xD83CDDF2D83CDDF4LLU,
	0xD83CDDF2D83CDDFELLU,
	0xD83CDDF2D83CDDFDLLU,
	0xD83CDDF3D83CDDF1LLU,
	0xD83CDDF3D83CDDFFLLU,
	0xD83CDDF3D83CDDF4LLU,
	0xD83CDDF5D83CDDEDLLU,
	0xD83CDDF5D83CDDF1LLU,
	0xD83CDDF5D83CDDF9LLU,
	0xD83CDDF5D83CDDF7LLU,
	0xD83CDDF7D83CDDFALLU,
	0xD83CDDF8D83CDDE6LLU,
	0xD83CDDF8D83CDDECLLU,
	0xD83CDDFFD83CDDE6LLU,
	0xD83CDDEAD83CDDF8LLU,
	0xD83CDDF8D83CDDEALLU,
	0xD83CDDE8D83CDDEDLLU,
	0xD83CDDF9D83CDDF7LLU,
	0xD83CDDECD83CDDE7LLU,
	0xD83CDDFAD83CDDF8LLU,
	0xD83CDDE6D83CDDEALLU,
	0xD83CDDFBD83CDDF3LLU,
};

uint64 emojiCategory7[] = {
	0x231ALLU,
	0xD83DDCF1LLU,
	0xD83DDCF2LLU,
	0xD83DDCBBLLU,
	0x23F0LLU,
	0x23F3LLU,
	0x231BLLU,
	0xD83DDCF7LLU,
	0xD83DDCF9LLU,
	0xD83CDFA5LLU,
	0xD83DDCFALLU,
	0xD83DDCFBLLU,
	0xD83DDCDFLLU,
	0xD83DDCDELLU,
	0x260ELLU,
	0xD83DDCE0LLU,
	0xD83DDCBDLLU,
	0xD83DDCBELLU,
	0xD83DDCBFLLU,
	0xD83DDCC0LLU,
	0xD83DDCFCLLU,
	0xD83DDD0BLLU,
	0xD83DDD0CLLU,
	0xD83DDCA1LLU,
	0xD83DDD26LLU,
	0xD83DDCE1LLU,
	0xD83DDCB3LLU,
	0xD83DDCB8LLU,
	0xD83DDCB0LLU,
	0xD83DDC8ELLU,
	0xD83CDF02LLU,
	0xD83DDC5DLLU,
	0xD83DDC5BLLU,
	0xD83DDC5CLLU,
	0xD83DDCBCLLU,
	0xD83CDF92LLU,
	0xD83DDC84LLU,
	0xD83DDC53LLU,
	0xD83DDC52LLU,
	0xD83DDC61LLU,
	0xD83DDC60LLU,
	0xD83DDC62LLU,
	0xD83DDC5ELLU,
	0xD83DDC5FLLU,
	0xD83DDC59LLU,
	0xD83DDC57LLU,
	0xD83DDC58LLU,
	0xD83DDC5ALLU,
	0xD83DDC55LLU,
	0xD83DDC54LLU,
	0xD83DDC56LLU,
	0xD83DDEAALLU,
	0xD83DDEBFLLU,
	0xD83DDEC1LLU,
	0xD83DDEBDLLU,
	0xD83DDC88LLU,
	0xD83DDC89LLU,
	0xD83DDC8ALLU,
	0xD83DDD2CLLU,
	0xD83DDD2DLLU,
	0xD83DDD2ELLU,
	0xD83DDD27LLU,
	0xD83DDD2ALLU,
	0xD83DDD29LLU,
	0xD83DDD28LLU,
	0xD83DDCA3LLU,
	0xD83DDEACLLU,
	0xD83DDD2BLLU,
	0xD83DDD16LLU,
	0xD83DDCF0LLU,
	0xD83DDD11LLU,
	0x2709LLU,
	0xD83DDCE9LLU,
	0xD83DDCE8LLU,
	0xD83DDCE7LLU,
	0xD83DDCE5LLU,
	0xD83DDCE4LLU,
	0xD83DDCE6LLU,
	0xD83DDCEFLLU,
	0xD83DDCEELLU,
	0xD83DDCEALLU,
	0xD83DDCEBLLU,
	0xD83DDCECLLU,
	0xD83DDCEDLLU,
	0xD83DDCC4LLU,
	0xD83DDCC3LLU,
	0xD83DDCD1LLU,
	0xD83DDCC8LLU,
	0xD83DDCC9LLU,
	0xD83DDCCALLU,
	0xD83DDCC5LLU,
	0xD83DDCC6LLU,
	0xD83DDD05LLU,
	0xD83DDD06LLU,
	0xD83DDCDCLLU,
	0xD83DDCCBLLU,
	0xD83DDCD6LLU,
	0xD83DDCD3LLU,
	0xD83DDCD4LLU,
	0xD83DDCD2LLU,
	0xD83DDCD5LLU,
	0xD83DDCD7LLU,
	0xD83DDCD8LLU,
	0xD83DDCD9LLU,
	0xD83DDCDALLU,
	0xD83DDCC7LLU,
	0xD83DDD17LLU,
	0xD83DDCCELLU,
	0xD83DDCCCLLU,
	0x2702LLU,
	0xD83DDCD0LLU,
	0xD83DDCCDLLU,
	0xD83DDCCFLLU,
	0xD83DDEA9LLU,
	0xD83DDCC1LLU,
	0xD83DDCC2LLU,
	0x2712LLU,
	0x270FLLU,
	0xD83DDCDDLLU,
	0xD83DDD0FLLU,
	0xD83DDD10LLU,
	0xD83DDD12LLU,
	0xD83DDD13LLU,
	0xD83DDCE3LLU,
	0xD83DDCE2LLU,
	0xD83DDD08LLU,
	0xD83DDD09LLU,
	0xD83DDD0ALLU,
	0xD83DDD07LLU,
	0xD83DDCA4LLU,
	0xD83DDD14LLU,
	0xD83DDD15LLU,
	0xD83DDCADLLU,
	0xD83DDCACLLU,
	0xD83DDEB8LLU,
	0xD83DDD0DLLU,
	0xD83DDD0ELLU,
	0xD83DDEABLLU,
	0x26D4LLU,
	0xD83DDCDBLLU,
	0xD83DDEB7LLU,
	0xD83DDEAFLLU,
	0xD83DDEB3LLU,
	0xD83DDEB1LLU,
	0xD83DDCF5LLU,
	0xD83DDD1ELLU,
	0xD83CDE51LLU,
	0xD83CDE50LLU,
	0xD83DDCAELLU,
	0x3299LLU,
	0x3297LLU,
	0xD83CDE34LLU,
	0xD83CDE35LLU,
	0xD83CDE32LLU,
	0xD83CDE36LLU,
	0xD83CDE1ALLU,
	0xD83CDE38LLU,
	0xD83CDE3ALLU,
	0xD83CDE37LLU,
	0xD83CDE39LLU,
	0xD83CDE33LLU,
	0xD83CDE02LLU,
	0xD83CDE01LLU,
	0xD83CDE2FLLU,
	0xD83DDCB9LLU,
	0x2747LLU,
	0x2733LLU,
	0x274ELLU,
	0x2705LLU,
	0x2734LLU,
	0xD83DDCF3LLU,
	0xD83DDCF4LLU,
	0xD83CDD9ALLU,
	0xD83CDD70LLU,
	0xD83CDD71LLU,
	0xD83CDD8ELLU,
	0xD83CDD91LLU,
	0xD83CDD7ELLU,
	0xD83CDD98LLU,
	0xD83CDD94LLU,
	0xD83CDD7FLLU,
	0xD83DDEBELLU,
	0xD83CDD92LLU,
	0xD83CDD93LLU,
	0xD83CDD95LLU,
	0xD83CDD96LLU,
	0xD83CDD97LLU,
	0xD83CDD99LLU,
	0xD83CDFE7LLU,
	0x2648LLU,
	0x2649LLU,
	0x264ALLU,
	0x264BLLU,
	0x264CLLU,
	0x264DLLU,
	0x264ELLU,
	0x264FLLU,
	0x2650LLU,
	0x2651LLU,
	0x2652LLU,
	0x2653LLU,
	0xD83DDEBBLLU,
	0xD83DDEB9LLU,
	0xD83DDEBALLU,
	0xD83DDEBCLLU,
	0x267FLLU,
	0xD83DDEB0LLU,
	0xD83DDEADLLU,
	0xD83DDEAELLU,
	0x25B6LLU,
	0x25C0LLU,
	0xD83DDD3CLLU,
	0xD83DDD3DLLU,
	0x23E9LLU,
	0x23EALLU,
	0x23EBLLU,
	0x23ECLLU,
	0x27A1LLU,
	0x2B05LLU,
	0x2B06LLU,
	0x2B07LLU,
	0x2197LLU,
	0x2198LLU,
	0x2199LLU,
	0x2196LLU,
	0x2195LLU,
	0x2194LLU,
	0xD83DDD04LLU,
	0x21AALLU,
	0x21A9LLU,
	0x2934LLU,
	0x2935LLU,
	0xD83DDD00LLU,
	0xD83DDD01LLU,
	0xD83DDD02LLU,
	0x2320E3LLU,
	0x3020E3LLU,
	0x3120E3LLU,
	0x3220E3LLU,
	0x3320E3LLU,
	0x3420E3LLU,
	0x3520E3LLU,
	0x3620E3LLU,
	0x3720E3LLU,
	0x3820E3LLU,
	0x3920E3LLU,
	0xD83DDD1FLLU,
	0xD83DDD22LLU,
	0xD83DDD24LLU,
	0xD83DDD21LLU,
	0xD83DDD20LLU,
	0x2139LLU,
	0xD83DDCF6LLU,
	0xD83CDFA6LLU,
	0xD83DDD23LLU,
	0x2795LLU,
	0x2796LLU,
	0x3030LLU,
	0x2797LLU,
	0x2716LLU,
	0x2714LLU,
	0xD83DDD03LLU,
	0x2122LLU,
	0xA9LLU,
	0xAELLU,
	0xD83DDCB1LLU,
	0xD83DDCB2LLU,
	0x27B0LLU,
	0x27BFLLU,
	0x303DLLU,
	0x2757LLU,
	0x2753LLU,
	0x2755LLU,
	0x2754LLU,
	0x203CLLU,
	0x2049LLU,
	0x274CLLU,
	0x2B55LLU,
	0xD83DDCAFLLU,
	0xD83DDD1ALLU,
	0xD83DDD19LLU,
	0xD83DDD1BLLU,
	0xD83DDD1DLLU,
	0xD83DDD1CLLU,
	0xD83CDF00LLU,
	0x24C2LLU,
	0x26CELLU,
	0xD83DDD2FLLU,
	0xD83DDD30LLU,
	0xD83DDD31LLU,
	0x26A0LLU,
	0x2668LLU,
	0x267BLLU,
	0xD83DDCA2LLU,
	0xD83DDCA0LLU,
	0x2660LLU,
	0x2663LLU,
	0x2665LLU,
	0x2666LLU,
	0x2611LLU,
	0x26AALLU,
	0x26ABLLU,
	0xD83DDD18LLU,
	0xD83DDD34LLU,
	0xD83DDD35LLU,
	0xD83DDD3ALLU,
	0xD83DDD3BLLU,
	0xD83DDD38LLU,
	0xD83DDD39LLU,
	0xD83DDD36LLU,
	0xD83DDD37LLU,
	0x25AALLU,
	0x25ABLLU,
	0x2B1BLLU,
	0x2B1CLLU,
	0x25FCLLU,
	0x25FBLLU,
	0x25FELLU,
	0x25FDLLU,
	0xD83DDD32LLU,
	0xD83DDD33LLU,
	0xD83DDD50LLU,
	0xD83DDD51LLU,
	0xD83DDD52LLU,
	0xD83DDD53LLU,
	0xD83DDD54LLU,
	0xD83DDD55LLU,
	0xD83DDD56LLU,
	0xD83DDD57LLU,
	0xD83DDD58LLU,
	0xD83DDD59LLU,
	0xD83DDD5ALLU,
	0xD83DDD5BLLU,
	0xD83DDD5CLLU,
	0xD83DDD5DLLU,
	0xD83DDD5ELLU,
	0xD83DDD5FLLU,
	0xD83DDD60LLU,
	0xD83DDD61LLU,
	0xD83DDD62LLU,
	0xD83DDD63LLU,
	0xD83DDD64LLU,
	0xD83DDD65LLU,
	0xD83DDD66LLU,
	0xD83DDD67LLU,
};

uint64 emojiPostfixed[] = {
	0x263ALLU, // category 1
	0x261DLLU,
	0x270CLLU,

	0x26A1LLU, // category 2
	0x2600LLU,
	0x26C5LLU,
	0x2601LLU,
	0x2614LLU,
	0x2744LLU,
	0x2B50LLU,

	0x2615LLU, // category 3

	0x2764LLU, // category 4

	0x26C4LLU, // category 5
	0x26FALLU,
	0x26BDLLU,
	0x26BELLU,
	0x26F3LLU,
	0xD83CDC04LLU,

	0x26FDLLU, // category 6
	0x2708LLU,
	0x2693LLU,
	0x26F5LLU,
	0x26F2LLU,
	0x26EALLU,

	0x231ALLU, // category 7
	0x231BLLU,
	0x260ELLU,
	0x2709LLU,
	0x2702LLU,
	0x2712LLU,
	0x270FLLU,
	0x26D4LLU,
	0x3299LLU,
	0x3297LLU,
	0xD83CDE1ALLU,
	0xD83CDE2FLLU,
	0x2747LLU,
	0x2733LLU,
	0x2734LLU,
	0xD83CDD7FLLU,
	0x2648LLU,
	0x2649LLU,
	0x264ALLU,
	0x264BLLU,
	0x264CLLU,
	0x264DLLU,
	0x264ELLU,
	0x264FLLU,
	0x2650LLU,
	0x2651LLU,
	0x2652LLU,
	0x2653LLU,
	0x267FLLU,
	0x25B6LLU,
	0x25C0LLU,
	0x27A1LLU,
	0x2B05LLU,
	0x2B06LLU,
	0x2B07LLU,
	0x2197LLU,
	0x2198LLU,
	0x2199LLU,
	0x2196LLU,
	0x2195LLU,
	0x2194LLU,
	0x21AALLU,
	0x21A9LLU,
	0x2934LLU,
	0x2935LLU,
	0x2139LLU,
	0x2716LLU,
	0x2714LLU,
	0x303DLLU,
	0x2757LLU,
	0x203CLLU,
	0x2049LLU,
	0x2B55LLU,
	0x24C2LLU,
	0x26A0LLU,
	0x2668LLU,
	0x267BLLU,
	0x2660LLU,
	0x2663LLU,
	0x2665LLU,
	0x2666LLU,
	0x2611LLU,
	0x26AALLU,
	0x26ABLLU,
	0x25AALLU,
	0x25ABLLU,
	0x2B1BLLU,
	0x2B1CLLU,
	0x25FCLLU,
	0x25FBLLU,
	0x25FELLU,
	0x25FDLLU,
};
QMap<uint64, bool> emojiWithPostfixes, emojiWithColors;

uint32 firstCode(uint64 fullCode) {
	return (fullCode > 0xFFFFFFFFLLU) ? uint32(fullCode >> 32) : (fullCode & 0xFFFFFFFFU);
}

uint32 secondCode(uint64 fullCode) {
	return (fullCode > 0xFFFFFFFFLLU) ? (fullCode & 0xFFFFFFFFU) : 0;
}

void writeEmojiCategory(QTextStream &tcpp, uint64 *emojiCategory, uint32 size, const char *name) {
	tcpp << "\tcase dbiet" << name << ": {\n";
	tcpp << "\t\tstatic QVector<EmojiPtr> v" << name << ";\n";
	tcpp << "\t\tif (v" << name << ".isEmpty()) {\n";
	tcpp << "\t\t\tv" << name << ".resize(" << size << ");\n";
	for (uint32 i = 0; i < size; ++i) {
		int index = 0;
		for (EmojisData::const_iterator j = emojisData.cbegin(), e = emojisData.cend(); j != e; ++j) {
			if (emojiCategory[i] == (j->code2 ? ((uint64(j->code) << 32) | j->code2) : j->code)) {
				break;
			}
			++index;
		}
		if (index == emojisData.size()) {
			throw Exception(QString("Could not find emoji from category '%1' with index %2, code %3").arg(name).arg(i).arg(emojiCategory[i], 0, 16).toUtf8().constData());
		}
		tcpp << "\t\t\tv" << name << "[" << i << "] = &emojis[" << index << "];\n";
	}
	tcpp << "\t\t}\n";
	tcpp << "\t\treturn v" << name << ";\n";
	tcpp << "\t} break;\n\n";
}

QString textEmojiString(const EmojiData *emoji) {
	QString result;
	if ((emoji->code & 0xFFFF0000U) == 0xFFFF0000U) { // sequence
		return QString::fromUtf8(emojiSequences[(emoji->code & 0xFFFF)]);
	}
	int len = emoji->code2 ? 4 : ((emoji->code >> 16) ? 2 : 1);
	bool withPostfix = emojiWithPostfixes.constFind(emoji->code) != emojiWithPostfixes.constEnd();
	result.reserve(len + (withPostfix ? 1 : 0));
	switch (len) {
		case 1: result.append(QChar(emoji->code & 0xFFFF)); break;
		case 2:
			result.append(QChar((emoji->code >> 16) & 0xFFFF));
			result.append(QChar(emoji->code & 0xFFFF));
			break;
		case 4:
			result.append(QChar((emoji->code >> 16) & 0xFFFF));
			result.append(QChar(emoji->code & 0xFFFF));
			result.append(QChar((emoji->code2 >> 16) & 0xFFFF));
			result.append(QChar(emoji->code2 & 0xFFFF));
			break;
	}
	if (withPostfix) result.append(QChar(0xFE0F));
	if (emoji->color && ((emoji->color & 0xFFFF0000U) != 0xFFFF0000U)) {
		result.append(QChar((emoji->color >> 16) & 0xFFFF));
		result.append(QChar(emoji->color & 0xFFFF));
	}
	return result;
}

bool genEmoji(QString, const QString &emoji_out, const QString &emoji_png) {
	int currentRow = 0, currentColumn = 0;
	uint32 min1 = 0xFFFFFFFFU, max1 = 0, min2 = 0xFFFFFFFFU, max2 = 0;

	for (int i = 0, l = sizeof(emojiPostfixed) / sizeof(emojiPostfixed[0]); i < l; ++i) {
		emojiWithPostfixes.insert(emojiPostfixed[i], true);
	}
	for (int i = 0, l = sizeof(emojiColored) / sizeof(emojiColored[0]); i < l; ++i) {
		emojiWithColors.insert(emojiColored[i], true);
	}

	for (int i = 1; i < 8; ++i) {
		uint64 *k;
		int cnt = 0;
		switch (i) {
		case 1: k = emojiCategory1; cnt = sizeof(emojiCategory1) / sizeof(emojiCategory1[0]); break;
		case 2: k = emojiCategory2; cnt = sizeof(emojiCategory2) / sizeof(emojiCategory2[0]); break;
		case 3: k = emojiCategory3; cnt = sizeof(emojiCategory3) / sizeof(emojiCategory3[0]); break;
		case 4: k = emojiCategory4; cnt = sizeof(emojiCategory4) / sizeof(emojiCategory4[0]); break;
		case 5: k = emojiCategory5; cnt = sizeof(emojiCategory5) / sizeof(emojiCategory5[0]); break;
		case 6: k = emojiCategory6; cnt = sizeof(emojiCategory6) / sizeof(emojiCategory6[0]); break;
		case 7: k = emojiCategory7; cnt = sizeof(emojiCategory7) / sizeof(emojiCategory7[0]); break;
		}
		for (int j = 0; j < cnt; ++j) {
			EmojiData data;
			uint64 fullCode = k[j];
			data.code = firstCode(fullCode);
			data.code2 = secondCode(fullCode);
			data.category = i;
			data.index = j;
			data.x = currentColumn;
			data.y = currentRow;
			data.color = 0;

			++currentColumn;
			if (currentColumn == inRow) {
				++currentRow;
				currentColumn = 0;
			}

			uint32 high = data.code >> 16;
			if (high == 0xFFFF) { // sequences

			} else if (!high) { // small codes
				if (data.code == 169 || data.code == 174) { // two small
				} else {
					if (data.code < min1) min1 = data.code;
					if (data.code > max1) max1 = data.code;
				}
			} else if (high == 35 || (high >= 48 && high < 58)) { // digits
			} else {
				if (data.code < min2) min2 = data.code;
				if (data.code > max2) max2 = data.code;
			}
			EmojisData::const_iterator key = emojisData.constFind(fullCode);
			if (key != emojisData.cend()) {
				cout << QString("Bad emoji code (duplicate) %1 %2 and %3 %4").arg(data.code).arg(data.code2).arg(key->code).arg(key->code2).toUtf8().constData() << "\n";
				continue;
			}
			emojisData.insert(fullCode, data);
			if (emojiWithColors.constFind(fullCode) != emojiWithColors.cend()) {
				if (fullCode > 0xFFFFFFFFLLU) {
					cout << QString("Bad colored emoji: %1 %2").arg(data.code).arg(data.code2).toUtf8().constData() << "\n";
					continue;
				}
				for (uint32 l = 0; l < sizeof(emojiColors) / sizeof(emojiColors[0]); ++l) {
					uint32 color = emojiColors[l];
					EmojiData colored = data;
					colored.x = currentColumn;
					colored.y = currentRow;
					colored.color = color;

					++currentColumn;
					if (currentColumn == inRow) {
						++currentRow;
						currentColumn = 0;
					}

					fullCode = (uint64(k[j]) << 32) | uint64(color);
					EmojisData::const_iterator key = emojisData.constFind(fullCode);
					if (key != emojisData.cend()) {
						cout << QString("Bad emoji code (duplicate) %1 %2 and %3 %4").arg(data.code).arg(data.code2).arg(key->code).arg(key->code2).toUtf8().constData() << "\n";
						continue;
					}
					emojisData.insert(fullCode, colored);
				}
			}
		}
	}

	if (currentColumn) ++currentRow;
	if (!currentRow) {
		cout << "No emojis written..\n";
		return true;
	}

	QStringList str = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(QStringLiteral("/System/Library/Fonts/Apple Color Emoji.ttf")));

	for (int variantIndex = 0; variantIndex < variantsCount; variantIndex++) {
		int imSize = imSizes[variantIndex], bad = badSizes[variantIndex], badSize = (bad ? imSizes[5] : imSize);

		QFont f(QGuiApplication::font());
		f.setFamily(QStringLiteral("Apple Color Emoji"));
		f.setPixelSize(emojiFontSizes[bad ? 5 : variantIndex]);

		int s = 4 + badSize;
		QImage badImg(inRow * badSize, currentRow * badSize, QImage::Format_ARGB32);
		QImage emojisImg(inRow * imSize, currentRow * imSize, QImage::Format_ARGB32), emojiImg(s, s, QImage::Format_ARGB32);
		{
			QPainter p(&emojisImg), q(&badImg);
			QPainter::CompositionMode m = p.compositionMode();
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.fillRect(0, 0, emojisImg.width(), emojisImg.height(), Qt::transparent);
			p.setCompositionMode(m);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			if (bad) {
				QPainter::CompositionMode m = q.compositionMode();
				q.setCompositionMode(QPainter::CompositionMode_Source);
				q.fillRect(0, 0, emojisImg.width(), emojisImg.height(), Qt::transparent);
				q.setCompositionMode(m);
				q.setRenderHint(QPainter::SmoothPixmapTransform);
			}
			for (EmojisData::const_iterator it = emojisData.cbegin(), e = emojisData.cend(); it != e; ++it) {
				QRect drawFrom(2, 2, badSize, badSize);
				QString es = textEmojiString(&it.value());
				{
					QPainter q(&emojiImg);
					q.setPen(QColor(0, 0, 0, 255));
					q.setFont(f);
					QPainter::CompositionMode m = q.compositionMode();
					q.setCompositionMode(QPainter::CompositionMode_Source);
					q.fillRect(0, 0, emojiImg.width(), emojiImg.height(), Qt::transparent);
					q.setCompositionMode(m);
					q.drawText(2, 2 + emojiDeltas[bad ? 5 : variantIndex], es);
				}
				int top = 1, bottom = 1, left = 1, right = 1;
				QRgb *b = (QRgb*)emojiImg.bits();
				for (int i = 0; i < s; ++i) {
					if (b[i] > 0 || b[(s - 1) * s + i] > 0 || b[i * s] > 0 || b[i * s + (s - 1)] > 0) {
						cout << "Bad border!\n";
						return false;
					}
					if (b[1 * s + i] > 0) top = -1;
					else if (top > 0 && b[2 * s + i] > 0) top = 0;
					if (b[(s - 2) * s + i] > 0) bottom = -1;
					else if (bottom > 0 && b[(s - 3) * s + i] > 0) bottom = 0;
					if (b[i * s + 1] > 0) left = -1;
					else if (left > 0 && b[i * s + 2] > 0) left = 0;
					if (b[i * s + (s - 2)] > 0) right = -1;
					else if (right > 0 && b[i * s + (s - 3)] > 0) right = 0;
				}
				if (top < 0) {
					if (bottom <= 0) {
						cout << "Bad vertical :(\n";
						return false;
					} else {
						drawFrom.setY(drawFrom.y() + 1);
					}
				} else if (bottom < 0) {
					if (top <= 0) {
						cout << "Bad vertical :(\n";
						return false;
					} else {
						drawFrom.setY(drawFrom.y() - 1);
					}
				}
				if (left < 0) {
					if (right <= 0) {
						cout << "Bad horizontal :(\n";
						return false;
					} else {
						drawFrom.setX(drawFrom.x() + 1);
					}
				} else if (right < 0) {
					if (left <= 0) {
						cout << "Bad horizontal :(\n";
						return false;
					} else {
						drawFrom.setX(drawFrom.x() - 1);
					}
				}
				if (bad) {
					p.drawImage(QRect(it->x * imSize, it->y * imSize, imSize, imSize), emojiImg.copy(2, 2, badSize, badSize).scaled(imSize, imSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
					q.drawImage(QRect(it->x * badSize, it->y * badSize, badSize, badSize), emojiImg, QRect(2, 2, badSize, badSize));
				} else {
					p.drawImage(QRect(it->x * imSize, it->y * imSize, imSize, imSize), emojiImg, drawFrom);
				}
			}
		}
		QString postfix = variantPostfix[variantIndex], emojif = emoji_png + postfix + ".webp";
//		emojisImg.save(emoji_png + postfix + ".png");
//		if (bad) badImg.save(emoji_png + "_bad.png");
		QByteArray emojib;
		{
			QBuffer ebuf(&emojib);
			if (!emojisImg.save(&ebuf, "WEBP", (variantIndex < 3) ? 100 : 99)) {
				cout << "Could not save 'emoji" << postfix.toUtf8().constData() << ".webp'!\n";
				return false;
			}
		}
		bool needResave = !QFileInfo(emojif).exists();
		if (!needResave) {
			QFile ef(emojif);
			if (!ef.open(QIODevice::ReadOnly)) {
				needResave = true;
			} else {
				QByteArray already(ef.readAll());
				if (already.size() != emojib.size() || memcmp(already.constData(), emojib.constData(), already.size())) {
					needResave = true;
				}
			}
		}
        if (needResave) {
			QFile ef(emojif);
			if (!ef.open(QIODevice::WriteOnly)) {
				cout << "Could not save 'emoji" << postfix.toUtf8().constData() << ".png'!\n";
				return false;
			} else {
				if (ef.write(emojib) != emojib.size()) {
					cout << "Could not save 'emoji" << postfix.toUtf8().constData() << ".png'!\n";
					return false;
				}
			}
		}
	}

	try {

		QByteArray cppText;
		{
			QTextStream tcpp(&cppText);
			tcpp << "\
/*\n\
Created from emoji config by \'/MetaEmoji\' project\n\
\n\
WARNING! All changes made in this file will be lost!\n\
\n\
This file is part of Telegram Desktop, \n\
the official desktop version of Telegram messaging app, see https://telegram.org\n\
\n\
Telegram Desktop is free software: you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation, either version 3 of the License, or\n\
(at your option) any later version.\n\
\n\
It is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n\
GNU General Public License for more details.\n\
\n\
Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n\
Copyright (c) 2014 John Preston, https://desktop.telegram.org\n\
*/\n";
			tcpp << "#include \"stdafx.h\"\n#include \"gui/emoji_config.h\"\n\n";

			tcpp << "namespace {\n"; // namespace with data
			tcpp << "\tEmojiData *emojis = 0;\n";
			tcpp << "\tchar emojisData[sizeof(EmojiData) * " << emojisData.size() << "];\n";
			tcpp << "}\n\n";
			tcpp << "int EmojiSizes[] = { ";
			for (int variantIndex = 0; variantIndex < variantsCount; ++variantIndex) {
				int imSize = imSizes[variantIndex];
				tcpp << imSize;
				if (variantIndex + 1 < variantsCount) tcpp << ", ";
			}
			tcpp << " }, EIndex = -1, ESize = 0;\n";
			tcpp << "const char *EmojiNames[] = { ";
			for (int variantIndex = 0; variantIndex < variantsCount; ++variantIndex) {
				tcpp << "\":/gui/art/emoji" << variantPostfix[variantIndex] << ".webp\"";
				if (variantIndex + 1 < variantsCount) tcpp << ", ";
			}
			tcpp << " }, *EName = 0;\n";

			int ind = 0;
			for (EmojisData::const_iterator i = emojisData.cbegin(), e = emojisData.cend(); i != e; ++i) {
				if (i->color && ((i->color & 0xFFFF0000U) != 0xFFFF0000U)) {
					uint32 fullCode = uint32(i.key() >> 32);
					EmojisData::iterator it = emojisData.find(fullCode);
					if (it == e) {
						cout << "Could not find original emoji 0x" << QString("%1").arg(fullCode, 0, 16).toUpper().toUtf8().constData() << "\n";
						++ind;
						continue;
					}
					if (!it->color) {
						it->color = 0xFFFF0000U | uint32(ind);
					}
				}
				++ind;
			}

			tcpp << "void emojiInit() {\n";
			tcpp << "\tDBIScale emojiForScale = cRetina() ? dbisTwo : cScale();\n\n";
            tcpp << "\tswitch (emojiForScale) {\n";
            for (int variantIndex = 0; variantIndex < variantsCount - 1; ++variantIndex) {
                tcpp << "\t\tcase " << variantNames[variantIndex] << ": EIndex = " << variantIndex << "; break;\n";
            }
            tcpp << "\t};\n\tESize = EmojiSizes[EIndex];\n\tEName = EmojiNames[EIndex];\n\n";
			tcpp << "\tEmojiData *toFill = emojis = (EmojiData*)emojisData;\n\n";
			uint32 index = 0;
			int sequenceOffset = 0;
			for (EmojisData::const_iterator i = emojisData.cbegin(), e = emojisData.cend(); i != e; ++i) {
				int len = 1;
				if (i->code2) {
					len = 4;
				} else if (i->code >> 16) {
					if ((i->code >> 16) == 0xFFFF) { // sequence
						len = textEmojiString(&i.value()).size();
						if (!sequenceOffset) sequenceOffset = index;
					} else {
						len = 2;
					}
				}
				if (i->color && ((i->color & 0xFFFF0000U) != 0xFFFF0000U)) {
					len += 2;
				}
				bool withPostfix = emojiWithPostfixes.constFind(i->code) != emojiWithPostfixes.cend();
				tcpp << "\tnew (toFill++) EmojiData(" << i->x << ", " << i->y << ", 0x" << QString("%1").arg(i->code, 0, 16).toUpper().toUtf8().constData() << "U, 0" << (i->code2 ? ('x' + QString("%1U").arg(i->code2, 0, 16).toUpper()).toUtf8().constData() : "") << ", " << len << (withPostfix ? ", 0xFE0F, 0" : ", 0, 0") << (i->color ? ('x' + QString("%1U").arg(i->color, 0, 16).toUpper()).toUtf8().constData() : "") << ");\n";
				++index;
			}
			tcpp << "};\n\n";

			// getter of one symbol emojis
			tcpp << "EmojiPtr emojiGet(uint32 code) {\n";
			tcpp << "\tif (!emojis) return 0;\n\n";
			tcpp << "\tuint32 highCode = code >> 16;\n";

			EmojisData::const_iterator i = emojisData.cbegin(), e = emojisData.cend();

			tcpp << "\tif (!highCode) {\n"; // small codes
			tcpp << "\t\tswitch (code) {\n";
			for (index = 0; i != e; ++i) { // two small
				if (i->code2) break;
				if (i->code != 169 && i->code != 174) break;

                tcpp << "\t\t\tcase 0x" << QString("%1").arg(i->code, 0, 16).toUpper().toUtf8().constData() << "U: return &emojis[" << (index++) << "];\n";
			}
			tcpp << "\t\t}\n\n";
            tcpp << "\t\tif (code < 0x" << QString("%1").arg(min1, 0, 16).toUpper().toUtf8().constData() << "U || code > 0x" << QString("%1").arg(max1, 0, 16).toUpper().toUtf8().constData() << "U) return 0;\n\n";
			tcpp << "\t\tswitch (code) {\n";
			for (; i != e; ++i) {
				if (i->code2 || (i->code >> 16)) break;
                tcpp << "\t\t\tcase 0x" << QString("%1").arg(i->code, 0, 16).toUpper().toUtf8().constData() << "U: return &emojis[" << (index++) << "];\n";
			}
			tcpp << "\t\t}\n\n";
			tcpp << "\t\treturn 0;\n";
			tcpp << "\t}\n\n";

			tcpp << "\tif (highCode == 35 || (highCode >= 48 && highCode < 58)) {\n"; // digits
            tcpp << "\t\tif ((code & 0xFFFFU) != 0x20E3U) return 0;\n\n";
			tcpp << "\t\tswitch (code) {\n";
			for (; i != e; ++i) {
				if (i->code2) break;
				uint32 high = i->code >> 16;
				if (high != 35 && (high < 48 || high >= 58)) break;

                tcpp << "\t\t\tcase 0x" << QString("%1").arg(i->code, 0, 16).toUpper().toUtf8().constData() << "U: return &emojis[" << (index++) << "];\n";
			}
			tcpp << "\t\t}\n\n";
			tcpp << "\t\treturn 0;\n";
			tcpp << "\t}\n\n";

			tcpp << "\tif (highCode == 0xFFFFU) {\n"; // sequences
			tcpp << "\t\tstatic const int sequenceOffset = " << sequenceOffset << ";\n\n";
			tcpp << "\t\tuint32 index = (code & 0xFFFFU);\n";
			tcpp << "\t\treturn (index < " << (sizeof(emojiSequences) / sizeof(emojiSequences[0])) << ") ? &emojis[sequenceOffset + index] : 0;\n";
			tcpp << "\t}\n\n";

            tcpp << "\tif (code < 0x" << QString("%1").arg(min2, 0, 16).toUpper().toUtf8().constData() << "U || code > 0x" << QString("%1").arg(max2, 0, 16).toUpper().toUtf8().constData() << "U) return 0;\n\n";
			tcpp << "\tswitch (code) {\n";
			uint32 minTwoSymbol = 0, maxTwoSymbol = 0;
			for (; i != e; ++i) {
				if (i->code2) {
					if (!minTwoSymbol) minTwoSymbol = i->code;
					if (i->code > maxTwoSymbol) {
						tcpp << "\t\tcase 0x" << QString("%1").arg(i->code, 0, 16).toUpper().toUtf8().constData() << "U: return TwoSymbolEmoji;\n";
						maxTwoSymbol = i->code;
					}
					index++;
					continue;
				}
				if ((i->code & 16) == 0xFFFF) { // sequence
					index++;
					continue;
				}
				if (i->color && ((i->color & 0xFFFF0000U) != 0xFFFF0000U)) {
					index++;
					continue;
				}
                tcpp << "\t\tcase 0x" << QString("%1").arg(i->code, 0, 16).toUpper().toUtf8().constData() << "U: return &emojis[" << (index++) << "];\n";
			}
			tcpp << "\t}\n\n";

			tcpp << "\treturn 0;\n";
			tcpp << "}\n\n";

			// getter of two symbol emojis
			tcpp << "EmojiPtr emojiGet(uint32 code, uint32 code2) {\n";
			tcpp << "\tif (code < 0x" << QString("%1").arg(minTwoSymbol, 0, 16).toUpper().toUtf8().constData() << "U || code > 0x" << QString("%1").arg(maxTwoSymbol, 0, 16).toUpper().toUtf8().constData() << "U) return 0;\n\n";
			tcpp << "\tswitch (code) {\n";
			maxTwoSymbol = 0;
			index = 0;
			for (i = emojisData.cbegin(); i != e; ++i) {
				if (i->code2) {
					if (i->code > maxTwoSymbol) {
						if (maxTwoSymbol) {
							tcpp << "\t\t\tdefault: return 0;\n";
							tcpp << "\t\t} break;\n";
						}
						tcpp << "\t\tcase 0x" << QString("%1").arg(i->code, 0, 16).toUpper().toUtf8().constData() << "U: switch (code2) {\n";
						maxTwoSymbol = i->code;
					}
					tcpp << "\t\t\tcase 0x" << QString("%1").arg(i->code2, 0, 16).toUpper().toUtf8().constData() << "U: return &emojis[" << index << "];\n";
				}
				index++;
			}
			if (maxTwoSymbol) {
				tcpp << "\t\t\tdefault: return 0;\n";
				tcpp << "\t\t} break;\n";
			}
			tcpp << "\t}\n\n";
			tcpp << "\treturn 0;\n";
			tcpp << "}\n\n";

			// getter of colored emojis
			tcpp << "EmojiPtr emojiGet(EmojiPtr emoji, uint32 color) {\n";
			tcpp << "\tif (!emoji || ((emoji->color & 0xFFFF0000U) != 0xFFFF0000U)) return emoji;\n\n";
			tcpp << "\tint index = 0;\n";
			tcpp << "\tswitch (color) {\n";
			for (int j = 0, l = sizeof(emojiColors) / sizeof(emojiColors[0]); j < l; ++j) {
				tcpp << "\t\tcase 0x" << QString("%1").arg(emojiColors[j], 0, 16).toUpper().toUtf8().constData() << ": index = " << j << "; break;\n";
			}
			tcpp << "\t\tdefault: return emoji;\n";
			tcpp << "\t}\n\n";
			tcpp << "\treturn &emojis[(emoji->color & 0xFFFFU) + index];\n";
			tcpp << "}\n\n";

			// getter of sequence emojis
			QString seqs[sizeof(emojiSequences) / sizeof(emojiSequences[0])];
			for (int j = 0, l = sizeof(emojiSequences) / sizeof(emojiSequences[0]); j < l; ++j) {
				seqs[j] = QString::fromUtf8(emojiSequences[j]);
			}
			tcpp << "EmojiPtr emojiGet(const QChar *from, const QChar *end) {\n";
			tcpp << "\tstatic const int sequenceOffset = " << sequenceOffset << ";\n\n";
			tcpp << "\tif (end < from + 8 || (from + 2)->unicode() != 0x200D || (from + 5)->unicode() != 0x200D) return 0;\n\n";
			tcpp << "\tstatic const uint32 ";
			tcpp << "man = 0x" << QString("%1").arg((uint32(seqs[0].at(0).unicode()) << 16) | uint32(seqs[0].at(1).unicode()), 0, 16).toUpper().toUtf8().constData() << ", ";
			tcpp << "woman = 0x" << QString("%1").arg((uint32(seqs[0].at(3).unicode()) << 16) | uint32(seqs[0].at(4).unicode()), 0, 16).toUpper().toUtf8().constData() << ", ";
			tcpp << "boy = 0x" << QString("%1").arg((uint32(seqs[4].at(6).unicode()) << 16) | uint32(seqs[4].at(7).unicode()), 0, 16).toUpper().toUtf8().constData() << ", ";
			tcpp << "girl = 0x" << QString("%1").arg((uint32(seqs[0].at(6).unicode()) << 16) | uint32(seqs[0].at(7).unicode()), 0, 16).toUpper().toUtf8().constData() << ", ";
			tcpp << "heart = 0x" << QString("%1").arg((uint32(seqs[16].at(3).unicode()) << 16) | uint32(seqs[16].at(4).unicode()), 0, 16).toUpper().toUtf8().constData() << ", ";
			tcpp << "kiss = 0x" << QString("%1").arg((uint32(seqs[16].at(6).unicode()) << 16) | uint32(seqs[16].at(7).unicode()), 0, 16).toUpper().toUtf8().constData() << ";\n";
			tcpp << "\tuint32 one = (uint32(from->unicode()) << 16) | uint32((from + 1)->unicode()), two = (uint32((from + 3)->unicode()) << 16) | uint32((from + 4)->unicode()), three = (uint32((from + 6)->unicode()) << 16) | uint32((from + 7)->unicode());\n\n";
			tcpp << "\tif (one != man && one != woman) return 0;\n\n";
			tcpp << "\tif (end > from + 10 && (from + 8)->unicode() == 0x200D) {\n";
			tcpp << "\t\tuint32 four = (uint32((from + 9)->unicode()) << 16) | uint32((from + 10)->unicode());\n\n";
			tcpp << "\t\tif (one == man) {\n";
			tcpp << "\t\t\tif (two == man) {\n";

			tcpp << "\t\t\t\tif (three == girl) {\n";
			tcpp << "\t\t\t\t\tif (four == girl) return &emojis[sequenceOffset + 13];\n";
			tcpp << "\t\t\t\t\tif (four == boy) return &emojis[sequenceOffset + 11];\n";
			tcpp << "\t\t\t\t} else if (three == boy) {\n";
			tcpp << "\t\t\t\t\tif (four == boy) return &emojis[sequenceOffset + 12];\n";
			tcpp << "\t\t\t\t}\n";

			tcpp << "\t\t\t} else if (two == woman) {\n";

			tcpp << "\t\t\t\tif (three == girl) {\n";
			tcpp << "\t\t\t\t\tif (four == girl) return &emojis[sequenceOffset + 3];\n";
			tcpp << "\t\t\t\t\tif (four == boy) return &emojis[sequenceOffset + 1];\n";
			tcpp << "\t\t\t\t} else if (three == boy) {\n";
			tcpp << "\t\t\t\t\tif (four == boy) return &emojis[sequenceOffset + 2];\n";
			tcpp << "\t\t\t\t}\n";

			tcpp << "\t\t\t} else if (two == heart) {\n";
			tcpp << "\t\t\t\tif (three == kiss && four == man) return &emojis[sequenceOffset + 17];\n";
			tcpp << "\t\t\t}\n";
			tcpp << "\t\t} else {\n"; // one == woman
			tcpp << "\t\t\tif (two == woman) {\n";

			tcpp << "\t\t\t\tif (three == girl) {\n";
			tcpp << "\t\t\t\t\tif (four == girl) return &emojis[sequenceOffset + 8];\n";
			tcpp << "\t\t\t\t\tif (four == boy) return &emojis[sequenceOffset + 6];\n";
			tcpp << "\t\t\t\t} else if (three == boy) {\n";
			tcpp << "\t\t\t\t\tif (four == boy) return &emojis[sequenceOffset + 7];\n";
			tcpp << "\t\t\t\t}\n";

			tcpp << "\t\t\t} else if (two == heart) {\n";
			tcpp << "\t\t\t\tif (three == kiss && four == woman) return &emojis[sequenceOffset + 16];\n";
			tcpp << "\t\t\t}\n";
			tcpp << "\t\t}\n";
			tcpp << "\t}\n";

			tcpp << "\tif (one == man) {\n";
			tcpp << "\t\tif (two == man) {\n";

			tcpp << "\t\t\tif (three == girl) return &emojis[sequenceOffset + 10];\n";
			tcpp << "\t\t\tif (three == boy) return &emojis[sequenceOffset + 9];\n";

			tcpp << "\t\t} else if (two == woman) {\n";
			tcpp << "\t\t\tif (three == girl) return &emojis[sequenceOffset + 0];\n";
			tcpp << "\t\t} else if (two == heart) {\n";
			tcpp << "\t\t\tif (three == man) return &emojis[sequenceOffset + 15];\n";
			tcpp << "\t\t}\n";
			tcpp << "\t} else {\n"; // one == woman
			tcpp << "\t\tif (two == woman) {\n";

			tcpp << "\t\t\tif (three == girl) return &emojis[sequenceOffset + 5];\n";
			tcpp << "\t\t\tif (three == boy) return &emojis[sequenceOffset + 4];\n";

			tcpp << "\t\t} else if (two == heart) {\n";
			tcpp << "\t\t\tif (three == woman) return &emojis[sequenceOffset + 14];\n";
			tcpp << "\t\t}\n";
			tcpp << "\t}\n";

			tcpp << "\treturn 0;\n";
			tcpp << "}\n\n";

			tcpp << "QString emojiGetSequence(int index) {\n";
			tcpp << "\tstatic QVector<QString> sequences;\n";
			tcpp << "\tif (sequences.isEmpty()) {\n";
			tcpp << "\t\tsequences.reserve(" << (sizeof(seqs) / sizeof(seqs[0])) << ");\n\n";
			for (uint32 j = 0; j < (sizeof(emojiSequences) / sizeof(emojiSequences[0])); ++j) {
				uint32 len = QByteArray(emojiSequences[j]).size();
				QString str;
				str.reserve(4 * len);
				for (uint32 k = 0; k < len; ++k) {
					str.append(QString("\\x%1").arg(uint32((unsigned char)(emojiSequences[j][k])), 2, 16, QChar('0')));
				}
				tcpp << "\t\tsequences.push_back(QString::fromUtf8(\"" << str.toUtf8().constData() << "\"));\n";
			}
			tcpp << "\t}\n\n";
			tcpp << "\treturn (index >= 0 && index < sequences.size()) ? sequences.at(index) : QString();\n";
			tcpp << "}\n\n";

			// emoji autoreplace
			tcpp << "void emojiFind(const QChar *ch, const QChar *e, const QChar *&newEmojiEnd, uint32 &emojiCode) {\n";
			tcpp << "\tswitch (ch->unicode()) {\n";

			QString tab("\t");
			for (uint32 i = 0; i < replacesCount; ++i) {
				QString key = QString::fromUtf8(replaces[i].replace);
				replaceMap[key] = replaces[i].code;
			}
			QString chars;
			for (ReplaceMap::const_iterator i = replaceMap.cend(), e = replaceMap.cbegin(); i != e;) {
				--i;
				QString key = i.key();
				if (key == chars) {
					tcpp << tab.repeated(1 + chars.size()) << "}\n";
				}
				bool needSwitch = chars.size();
				while (chars.size() && key.midRef(0, chars.size()) != chars) {
					needSwitch = false;
					chars.resize(chars.size() - 1);
					tcpp << tab.repeated(1 + chars.size()) << "break;\n";
					if (chars.size() && (key.midRef(0, chars.size()) != chars || key == chars)) {
						tcpp << tab.repeated(1 + chars.size()) << "}\n";
					}
				}
				for (int j = chars.size(); j < key.size(); ++j) {
					if (needSwitch) {
						tcpp << tab.repeated(1 + chars.size()) << "if (ch + " << chars.size() << " != e) switch ((ch + " << chars.size() << ")->unicode()) {\n";
					}
					tcpp << tab.repeated(1 + chars.size()) << "case '" << ((key.at(j) == '\\' || key.at(j) == '\'') ? "\\" : "") << key.at(j) << "':\n";
					chars.push_back(key.at(j));
					needSwitch = true;
				}
				tcpp << tab.repeated(1 + chars.size()) << "newEmojiEnd = ch + " << chars.size() << ";\n";
				tcpp << tab.repeated(1 + chars.size()) << "if (newEmojiEnd == e || emojiEdge(newEmojiEnd) || newEmojiEnd->unicode() == ' ') {\n";
                tcpp << tab.repeated(1 + chars.size()) << "\temojiCode = 0x" << QString("%1").arg(i.value(), 0, 16).toUpper().toUtf8().constData() << "U;\n";
				tcpp << tab.repeated(1 + chars.size()) << "\treturn;\n";
				tcpp << tab.repeated(1 + chars.size()) << "}\n";
			}
			while (chars.size()) {
				chars.resize(chars.size() - 1);
				tcpp << tab.repeated(1 + chars.size()) << "break;\n";
				if (chars.size()) {
					tcpp << tab.repeated(1 + chars.size()) << "}\n";
				}
			}
					
			tcpp << "\t}\n";
			tcpp << "}\n\n";

			tcpp << "int emojiPackCount(DBIEmojiTab tab) {\n";
			tcpp << "\tswitch (tab) {\n";
			tcpp << "\t\tcase dbietRecent     : return cGetRecentEmojis().size();\n";
			tcpp << "\t\tcase dbietPeople     : return " << sizeof(emojiCategory1) / sizeof(emojiCategory1[0]) << ";\n";
			tcpp << "\t\tcase dbietNature     : return " << sizeof(emojiCategory2) / sizeof(emojiCategory2[0]) << ";\n";
			tcpp << "\t\tcase dbietFood       : return " << sizeof(emojiCategory3) / sizeof(emojiCategory3[0]) << ";\n";
			tcpp << "\t\tcase dbietCelebration: return " << sizeof(emojiCategory4) / sizeof(emojiCategory4[0]) << ";\n";
			tcpp << "\t\tcase dbietActivity   : return " << sizeof(emojiCategory5) / sizeof(emojiCategory5[0]) << ";\n";
			tcpp << "\t\tcase dbietTravel     : return " << sizeof(emojiCategory6) / sizeof(emojiCategory6[0]) << ";\n";
			tcpp << "\t\tcase dbietObjects    : return " << sizeof(emojiCategory7) / sizeof(emojiCategory7[0]) << ";\n";
			tcpp << "\t};\n";
			tcpp << "\treturn 0;\n";
			tcpp << "}\n\n";
			tcpp << "EmojiPack emojiPack(DBIEmojiTab tab) {\n";
			tcpp << "\tswitch (tab) {\n\n";
			writeEmojiCategory(tcpp, emojiCategory1, sizeof(emojiCategory1) / sizeof(emojiCategory1[0]), "People");
			writeEmojiCategory(tcpp, emojiCategory2, sizeof(emojiCategory2) / sizeof(emojiCategory2[0]), "Nature");
			writeEmojiCategory(tcpp, emojiCategory3, sizeof(emojiCategory3) / sizeof(emojiCategory3[0]), "Food");
			writeEmojiCategory(tcpp, emojiCategory4, sizeof(emojiCategory4) / sizeof(emojiCategory4[0]), "Celebration");
			writeEmojiCategory(tcpp, emojiCategory5, sizeof(emojiCategory5) / sizeof(emojiCategory5[0]), "Activity");
			writeEmojiCategory(tcpp, emojiCategory6, sizeof(emojiCategory6) / sizeof(emojiCategory6[0]), "Travel");
			writeEmojiCategory(tcpp, emojiCategory7, sizeof(emojiCategory7) / sizeof(emojiCategory7[0]), "Objects");
			tcpp << "\t};\n\n";
			tcpp << "\tEmojiPack result;\n";
			tcpp << "\tresult.reserve(cGetRecentEmojis().size());\n";
			tcpp << "\tfor (RecentEmojiPack::const_iterator i = cGetRecentEmojis().cbegin(), e = cGetRecentEmojis().cend(); i != e; ++i) {\n";
			tcpp << "\t\tresult.push_back(i->first);\n";
			tcpp << "\t}\n";
			tcpp << "\treturn result;\n";
			tcpp << "}\n\n";
		}
		QFile cpp(emoji_out);
		bool write_cpp = true;
		if (cpp.open(QIODevice::ReadOnly)) {
			QByteArray wasCpp = cpp.readAll();
			if (wasCpp.size() == cppText.size()) {
				if (!memcmp(wasCpp.constData(), cppText.constData(), cppText.size())) {
					write_cpp = false;
				}
			}
			cpp.close();
		}
		if (write_cpp) {
			cout << "Emoji updated, writing " << currentRow << " rows, full count " << emojisData.size() << " emojis.\n";
			if (!cpp.open(QIODevice::WriteOnly)) throw Exception("Could not open style_auto.cpp for writing!");
			if (cpp.write(cppText) != cppText.size()) throw Exception("Could not open style_auto.cpp for writing!");
		}/**/
	} catch (exception &e) {
		cout << e.what() << "\n";
		QCoreApplication::exit(1);
		return false;
	}
	return true;
}
