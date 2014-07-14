/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "genemoji.h"

#include <QtCore/QtPlugin>

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
#endif

typedef unsigned int uint32;

struct EmojiData {
	uint32 code, code2;
	int x, y;
	QString name, name_200x;
};

// copied from emojibox.cpp
struct EmojiReplace {
	uint32 code;
	const char *replace;
};

EmojiReplace replaces[] = {
	{0xD83DDE0A, ":-)"},
	{0xD83DDE03, ":-D"},
	{0xD83DDE09, ";-)"},
	{0xD83DDE06, "xD"},
	{0xD83DDE1C, ";-P"},
	{0xD83DDE0B, ":-p"},
	{0xD83DDE0D, "8-)"},
	{0xD83DDE0E, "B-)"},
	{0xD83DDE12, ":-("},
	{0xD83DDE0F, ":]"},
	{0xD83DDE14, "3("},
	{0xD83DDE22, ":'("},
	{0xD83DDE2D, ":_("},
	{0xD83DDE29, ":(("},
	{0xD83DDE28, ":o"},
	{0xD83DDE10, ":|"},
	{0xD83DDE0C, "3-)"},
	{0xD83DDE20, ">("},
	{0xD83DDE21, ">(("},
	{0xD83DDE07, "O:)"},
	{0xD83DDE30, ";o"},
	{0xD83DDE33, "8|"},
	{0xD83DDE32, "8o"},
	{0xD83DDE37, ":X"},
	{0xD83DDE1A, ":-*"},
	{0xD83DDE08, "}:)"},
	{0x2764, "<3"},
	{0xD83DDC4D, ":like:"},
	{0xD83DDC4E, ":dislike:"},
	{0x261D, ":up:"},
	{0x270C, ":v:"},
	{0xD83DDC4C, ":ok:"}
};
const uint32 replacesCount = sizeof(replaces) / sizeof(EmojiReplace);
typedef QMap<QString, uint32> ReplaceMap;
ReplaceMap replaceMap;

static const int variantsCount = 4, variants[] = { 0, 2, 3, 4 }, inRow = 40, imSizes[] = { 16, 20, 24, 32 };
static const char *variantPostfix[] = { "", "_125x", "_150x", "_200x" };
static const char *variantNames[] = { "dbisOne", "dbisOneAndQuarter", "dbisOneAndHalf", "dbisTwo" };

typedef QMap<uint32, EmojiData> EmojisData;
EmojisData emojisData;

uint32 emojiCategory0[] = {
	0xD83DDE04,
	0xD83DDE03,
	0xD83DDE00,
	0xD83DDE0A,
	0x263A,
	0xD83DDE09,
	0xD83DDE0D,
	0xD83DDE18,
	0xD83DDE1A,
	0xD83DDE17,
	0xD83DDE19,
	0xD83DDE1C,
	0xD83DDE1D,
	0xD83DDE1B,
	0xD83DDE33,
	0xD83DDE01,
	0xD83DDE14,
	0xD83DDE0C,
	0xD83DDE12,
	0xD83DDE1E,
	0xD83DDE23,
	0xD83DDE22,
	0xD83DDE02,
	0xD83DDE2D,
	0xD83DDE2A,
	0xD83DDE25,
	0xD83DDE30,
	0xD83DDE05,
	0xD83DDE13,
	0xD83DDE29,
	0xD83DDE2B,
	0xD83DDE28,
	0xD83DDE31,
	0xD83DDE20,
	0xD83DDE21,
	0xD83DDE24,
	0xD83DDE16,
	0xD83DDE06,
	0xD83DDE0B,
	0xD83DDE37,
	0xD83DDE0E,
	0xD83DDE34,
	0xD83DDE35,
	0xD83DDE32,
	0xD83DDE1F,
	0xD83DDE26,
	0xD83DDE27,
	0xD83DDE08,
	0xD83DDC7F,
	0xD83DDE2E,
	0xD83DDE2C,
	0xD83DDE10,
	0xD83DDE15,
	0xD83DDE2F,
	0xD83DDE36,
	0xD83DDE07,
	0xD83DDE0F,
	0xD83DDE11,
	0xD83DDC72,
	0xD83DDC73,
	0xD83DDC6E,
	0xD83DDC77,
	0xD83DDC82,
	0xD83DDC76,
	0xD83DDC66,
	0xD83DDC67,
	0xD83DDC68,
	0xD83DDC69,
	0xD83DDC74,
	0xD83DDC75,
	0xD83DDC71,
	0xD83DDC7C,
	0xD83DDC78,
	0xD83DDE3A,
	0xD83DDE38,
	0xD83DDE3B,
	0xD83DDE3D,
	0xD83DDE3C,
	0xD83DDE40,
	0xD83DDE3F,
	0xD83DDE39,
	0xD83DDE3E,
	0xD83DDC79,
	0xD83DDC7A,
	0xD83DDE48,
	0xD83DDE49,
	0xD83DDE4A,
	0xD83DDC80,
	0xD83DDC7D,
	0xD83DDCA9,
	0xD83DDD25,
	0x2728,
	0xD83CDF1F,
	0xD83DDCAB,
	0xD83DDCA5,
	0xD83DDCA2,
	0xD83DDCA6,
	0xD83DDCA7,
	0xD83DDCA4,
	0xD83DDCA8,
	0xD83DDC42,
	0xD83DDC40,
	0xD83DDC43,
	0xD83DDC45,
	0xD83DDC44,
	0xD83DDC4D,
	0xD83DDC4E,
	0xD83DDC4C,
	0xD83DDC4A,
	0x270A,
	0x270C,
	0xD83DDC4B,
	0x270B,
	0xD83DDC50,
	0xD83DDC46,
	0xD83DDC47,
	0xD83DDC49,
	0xD83DDC48,
	0xD83DDE4C,
	0xD83DDE4F,
	0x261D,
	0xD83DDC4F,
	0xD83DDCAA,
	0xD83DDEB6,
	0xD83CDFC3,
	0xD83DDC83,
	0xD83DDC6B,
	0xD83DDC6A,
	0xD83DDC6C,
	0xD83DDC6D,
	0xD83DDC8F,
	0xD83DDC91,
	0xD83DDC6F,
	0xD83DDE46,
	0xD83DDE45,
	0xD83DDC81,
	0xD83DDE4B,
	0xD83DDC86,
	0xD83DDC87,
	0xD83DDC85,
	0xD83DDC70,
	0xD83DDE4E,
	0xD83DDE4D,
	0xD83DDE47,
	0xD83CDFA9,
	0xD83DDC51,
	0xD83DDC52,
	0xD83DDC5F,
	0xD83DDC5E,
	0xD83DDC61,
	0xD83DDC60,
	0xD83DDC62,
	0xD83DDC55,
	0xD83DDC54,
	0xD83DDC5A,
	0xD83DDC57,
	0xD83CDFBD,
	0xD83DDC56,
	0xD83DDC58,
	0xD83DDC59,
	0xD83DDCBC,
	0xD83DDC5C,
	0xD83DDC5D,
	0xD83DDC5B,
	0xD83DDC53,
	0xD83CDF80,
	0xD83CDF02,
	0xD83DDC84,
	0xD83DDC9B,
	0xD83DDC99,
	0xD83DDC9C,
	0xD83DDC9A,
	0x2764,
	0xD83DDC94,
	0xD83DDC97,
	0xD83DDC93,
	0xD83DDC95,
	0xD83DDC96,
	0xD83DDC9E,
	0xD83DDC98,
	0xD83DDC8C,
	0xD83DDC8B,
	0xD83DDC8D,
	0xD83DDC8E,
	0xD83DDC64,
	0xD83DDC65,
	0xD83DDCAC,
	0xD83DDC63,
	0xD83DDCAD,
};

uint32 emojiCategory1[] = {
	0xD83DDC36,
	0xD83DDC3A,
	0xD83DDC31,
	0xD83DDC2D,
	0xD83DDC39,
	0xD83DDC30,
	0xD83DDC38,
	0xD83DDC2F,
	0xD83DDC28,
	0xD83DDC3B,
	0xD83DDC37,
	0xD83DDC3D,
	0xD83DDC2E,
	0xD83DDC17,
	0xD83DDC35,
	0xD83DDC12,
	0xD83DDC34,
	0xD83DDC11,
	0xD83DDC18,
	0xD83DDC3C,
	0xD83DDC27,
	0xD83DDC26,
	0xD83DDC24,
	0xD83DDC25,
	0xD83DDC23,
	0xD83DDC14,
	0xD83DDC0D,
	0xD83DDC22,
	0xD83DDC1B,
	0xD83DDC1D,
	0xD83DDC1C,
	0xD83DDC1E,
	0xD83DDC0C,
	0xD83DDC19,
	0xD83DDC1A,
	0xD83DDC20,
	0xD83DDC1F,
	0xD83DDC2C,
	0xD83DDC33,
	0xD83DDC0B,
	0xD83DDC04,
	0xD83DDC0F,
	0xD83DDC00,
	0xD83DDC03,
	0xD83DDC05,
	0xD83DDC07,
	0xD83DDC09,
	0xD83DDC0E,
	0xD83DDC10,
	0xD83DDC13,
	0xD83DDC15,
	0xD83DDC16,
	0xD83DDC01,
	0xD83DDC02,
	0xD83DDC32,
	0xD83DDC21,
	0xD83DDC0A,
	0xD83DDC2B,
	0xD83DDC2A,
	0xD83DDC06,
	0xD83DDC08,
	0xD83DDC29,
	0xD83DDC3E,
	0xD83DDC90,
	0xD83CDF38,
	0xD83CDF37,
	0xD83CDF40,
	0xD83CDF39,
	0xD83CDF3B,
	0xD83CDF3A,
	0xD83CDF41,
	0xD83CDF43,
	0xD83CDF42,
	0xD83CDF3F,
	0xD83CDF3E,
	0xD83CDF44,
	0xD83CDF35,
	0xD83CDF34,
	0xD83CDF32,
	0xD83CDF33,
	0xD83CDF30,
	0xD83CDF31,
	0xD83CDF3C,
	0xD83CDF10,
	0xD83CDF1E,
	0xD83CDF1D,
	0xD83CDF1A,
	0xD83CDF11,
	0xD83CDF12,
	0xD83CDF13,
	0xD83CDF14,
	0xD83CDF15,
	0xD83CDF16,
	0xD83CDF17,
	0xD83CDF18,
	0xD83CDF1C,
	0xD83CDF1B,
	0xD83CDF19,
	0xD83CDF0D,
	0xD83CDF0E,
	0xD83CDF0F,
	0xD83CDF0B,
	0xD83CDF0C,
	0xD83CDF0D,
	0x2B50,
	0x2600,
	0x26C5,
	0x2601,
	0x26A1,
	0x2614,
	0x2744,
	0x26C4,
	0xD83CDF00,
	0xD83CDF01,
	0xD83CDF08,
	0xD83CDF0A,
};

uint32 emojiCategory2[] = {
	0xD83CDF8D,
	0xD83DDC9D,
	0xD83CDF8E,
	0xD83CDF92,
	0xD83CDF93,
	0xD83CDF8F,
	0xD83CDF86,
	0xD83CDF87,
	0xD83CDF90,
	0xD83CDF91,
	0xD83CDF83,
	0xD83DDC7B,
	0xD83CDF85,
	0xD83CDF84,
	0xD83CDF81,
	0xD83CDF8B,
	0xD83CDF89,
	0xD83CDF8A,
	0xD83CDF88,
	0xD83CDF8C,
	0xD83DDD2E,
	0xD83CDFA5,
	0xD83DDCF7,
	0xD83DDCF9,
	0xD83DDCFC,
	0xD83DDCBF,
	0xD83DDCC0,
	0xD83DDCBD,
	0xD83DDCBE,
	0xD83DDCBB,
	0xD83DDCF1,
	0x260E,
	0xD83DDCDE,
	0xD83DDCDF,
	0xD83DDCE0,
	0xD83DDCE1,
	0xD83DDCFA,
	0xD83DDCFB,
	0xD83DDD0A,
	0xD83DDD09,
	0xD83DDD09,
	0xD83DDD07,
	0xD83DDD14,
	0xD83DDD14,
	0xD83DDCE2,
	0xD83DDCE3,
	0x23F3,
	0x231B,
	0x23F0,
	0x231A,
	0xD83DDD13,
	0xD83DDD12,
	0xD83DDD0F,
	0xD83DDD10,
	0xD83DDD11,
	0xD83DDD0E,
	0xD83DDCA1,
	0xD83DDD26,
	0xD83DDD06,
	0xD83DDD05,
	0xD83DDD0C,
	0xD83DDD0B,
	0xD83DDD0D,
	0xD83DDEC0,
	0xD83DDEBF,
	0xD83DDEBD,
	0xD83DDD27,
	0xD83DDD29,
	0xD83DDD28,
	0xD83DDEAA,
	0xD83DDEAC,
	0xD83DDCA3,
	0xD83DDD2B,
	0xD83DDD2A,
	0xD83DDC8A,
	0xD83DDC89,
	0xD83DDCB0,
	0xD83DDCB4,
	0xD83DDCB5,
	0xD83DDCB7,
	0xD83DDCB6,
	0xD83DDCB3,
	0xD83DDCB8,
	0xD83DDCF2,
	0xD83DDCE7,
	0xD83DDCE5,
	0xD83DDCE4,
	0x2709,
	0xD83DDCE9,
	0xD83DDCE8,
	0xD83DDCEF,
	0xD83DDCEB,
	0xD83DDCEA,
	0xD83DDCEC,
	0xD83DDCED,
	0xD83DDCEE,
	0xD83DDCE6,
	0xD83DDCDD,
	0xD83DDCC4,
	0xD83DDCC3,
	0xD83DDCD1,
	0xD83DDCCA,
	0xD83DDCC8,
	0xD83DDCC9,
	0xD83DDCDC,
	0xD83DDCCB,
	0xD83DDCC5,
	0xD83DDCC6,
	0xD83DDCC7,
	0xD83DDCC1,
	0xD83DDCC2,
	0x2702,
	0xD83DDCCC,
	0xD83DDCCE,
	0x2712,
	0x270F,
	0xD83DDCCF,
	0xD83DDCD0,
	0xD83DDCD5,
	0xD83DDCD7,
	0xD83DDCD8,
	0xD83DDCD9,
	0xD83DDCD3,
	0xD83DDCD4,
	0xD83DDCD2,
	0xD83DDCDA,
	0xD83DDCD6,
	0xD83DDD16,
	0xD83DDCDB,
	0xD83DDD2C,
	0xD83DDD2D,
	0xD83DDCF0,
	0xD83CDFA8,
	0xD83CDFAC,
	0xD83CDFA4,
	0xD83CDFA7,
	0xD83CDFBC,
	0xD83CDFB5,
	0xD83CDFB6,
	0xD83CDFB9,
	0xD83CDFBB,
	0xD83CDFBA,
	0xD83CDFB7,
	0xD83CDFB8,
	0xD83DDC7E,
	0xD83CDFAE,
	0xD83CDCCF,
	0xD83CDFB4,
	0xD83CDC04,
	0xD83CDFB2,
	0xD83CDFAF,
	0xD83CDFC8,
	0xD83CDFC0,
	0x26BD,
	0x26BE,
	0xD83CDFBE,
	0xD83CDFB1,
	0xD83CDFC9,
	0xD83CDFB3,
	0x26F3,
	0xD83DDEB5,
	0xD83DDEB4,
	0xD83CDFC1,
	0xD83CDFC7,
	0xD83CDFC6,
	0xD83CDFBF,
	0xD83CDFC2,
	0xD83CDFCA,
	0xD83CDFC4,
	0xD83CDFA3,
	0x2615,
	0xD83CDF75,
	0xD83CDF76,
	0xD83CDF7C,
	0xD83CDF7A,
	0xD83CDF7B,
	0xD83CDF78,
	0xD83CDF79,
	0xD83CDF77,
	0xD83CDF74,
	0xD83CDF55,
	0xD83CDF54,
	0xD83CDF5F,
	0xD83CDF57,
	0xD83CDF56,
	0xD83CDF5D,
	0xD83CDF5B,
	0xD83CDF64,
	0xD83CDF71,
	0xD83CDF63,
	0xD83CDF65,
	0xD83CDF59,
	0xD83CDF58,
	0xD83CDF5A,
	0xD83CDF5C,
	0xD83CDF72,
	0xD83CDF62,
	0xD83CDF61,
	0xD83CDF73,
	0xD83CDF5E,
	0xD83CDF69,
	0xD83CDF6E,
	0xD83CDF66,
	0xD83CDF68,
	0xD83CDF67,
	0xD83CDF82,
	0xD83CDF70,
	0xD83CDF6A,
	0xD83CDF6B,
	0xD83CDF6C,
	0xD83CDF6D,
	0xD83CDF6F,
	0xD83CDF4E,
	0xD83CDF4F,
	0xD83CDF4A,
	0xD83CDF4B,
	0xD83CDF52,
	0xD83CDF47,
	0xD83CDF49,
	0xD83CDF53,
	0xD83CDF51,
	0xD83CDF48,
	0xD83CDF4C,
	0xD83CDF50,
	0xD83CDF4D,
	0xD83CDF60,
	0xD83CDF46,
	0xD83CDF45,
	0xD83CDF3D,
};

uint32 emojiCategory3[] = {
	0xD83CDFE0,
	0xD83CDFE1,
	0xD83CDFEB,
	0xD83CDFE2,
	0xD83CDFE3,
	0xD83CDFE5,
	0xD83CDFE6,
	0xD83CDFEA,
	0xD83CDFE9,
	0xD83CDFE8,
	0xD83DDC92,
	0x26EA,
	0xD83CDFEC,
	0xD83CDFE4,
	0xD83CDF07,
	0xD83CDF06,
	0xD83CDFEF,
	0xD83CDFF0,
	0x26FA,
	0xD83CDFED,
	0xD83DDDFC,
	0xD83DDDFE,
	0xD83DDDFB,
	0xD83CDF04,
	0xD83CDF05,
	0xD83CDF03,
	0xD83DDDFD,
	0xD83CDF09,
	0xD83CDFA0,
	0xD83CDFA1,
	0x26F2,
	0xD83CDFA2,
	0xD83DDEA2,
	0x26F5,
	0xD83DDEA4,
	0xD83DDEA3,
	0x2693,
	0xD83DDE80,
	0x2708,
	0xD83DDCBA,
	0xD83DDE81,
	0xD83DDE82,
	0xD83DDE8A,
	0xD83DDE89,
	0xD83DDE9E,
	0xD83DDE86,
	0xD83DDE84,
	0xD83DDE85,
	0xD83DDE88,
	0xD83DDE87,
	0xD83DDE9D,
	0xD83DDE9D,
	0xD83DDE83,
	0xD83DDE8E,
	0xD83DDE8C,
	0xD83DDE8D,
	0xD83DDE99,
	0xD83DDE98,
	0xD83DDE97,
	0xD83DDE95,
	0xD83DDE96,
	0xD83DDE9B,
	0xD83DDE9A,
	0xD83DDEA8,
	0xD83DDE93,
	0xD83DDE94,
	0xD83DDE92,
	0xD83DDE91,
	0xD83DDE90,
	0xD83DDEB2,
	0xD83DDEA1,
	0xD83DDE9F,
	0xD83DDEA0,
	0xD83DDE9C,
	0xD83DDC88,
	0xD83DDE8F,
	0xD83CDFAB,
	0xD83DDEA6,
	0xD83DDEA5,
	0x26A0,
	0xD83DDEA7,
	0xD83DDD30,
	0x26FD,
	0xD83CDFEE,
	0xD83CDFB0,
	0x2668,
	0xD83DDDFF,
	0xD83CDFAA,
	0xD83CDFAD,
	0xD83DDCCD,
	0xD83DDEA9,
	0xD83CDDEF,
	0xD83CDDF0,
	0xD83CDDE9,
	0xD83CDDE8,
	0xD83CDDFA,
	0xD83CDDEB,
	0xD83CDDEA,
	0xD83CDDEE,
	0xD83CDDF7,
	0xD83CDDEC,
};

uint32 emojiCategory4[] = {
	0x3120E3,
	0x3220E3,
	0x3320E3,
	0x3420E3,
	0x3520E3,
	0x3620E3,
	0x3720E3,
	0x3820E3,
	0x3920E3,
	0x3020E3,
	0xD83DDD1F,
	0xD83DDD22,
	0x2320E3,
	0xD83DDD23,
	0x2B06,
	0x2B07,
	0x2B05,
	0x27A1,
	0xD83DDD20,
	0xD83DDD21,
	0xD83DDD24,
	0x2197,
	0x2196,
	0x2198,
	0x2199,
	0x2194,
	0x2195,
	0xD83DDD04,
	0x25C0,
	0x25B6,
	0xD83DDD3C,
	0xD83DDD3D,
	0x21A9,
	0x21AA,
	0x2139,
	0x23EA,
	0x23E9,
	0x23EB,
	0x23EC,
	0x2935,
	0x2934,
	0xD83CDD97,
	0xD83DDD00,
	0xD83DDD01,
	0xD83DDD02,
	0xD83CDD95,
	0xD83CDD99,
	0xD83CDD92,
	0xD83CDD93,
	0xD83CDD96,
	0xD83DDCF6,
	0xD83CDFA6,
	0xD83CDE01,
	0xD83CDE2F,
	0xD83CDE33,
	0xD83CDE35,
	0xD83CDE32,
	0xD83CDE34,
	0xD83CDE32,
	0xD83CDE50,
	0xD83CDE39,
	0xD83CDE3A,
	0xD83CDE36,
	0xD83CDE1A,
	0xD83DDEBB,
	0xD83DDEB9,
	0xD83DDEBA,
	0xD83DDEBC,
	0xD83DDEBE,
	0xD83DDEB0,
	0xD83DDEAE,
	0xD83CDD7F,
	0x267F,
	0xD83DDEAD,
	0xD83CDE37,
	0xD83CDE38,
	0xD83CDE02,
	0x24C2,
	0xD83CDE51,
	0x3299,
	0x3297,
	0xD83CDD91,
	0xD83CDD98,
	0xD83CDD94,
	0xD83DDEAB,
	0xD83DDD1E,
	0xD83DDCF5,
	0xD83DDEAF,
	0xD83DDEB1,
	0xD83DDEB3,
	0xD83DDEB7,
	0xD83DDEB8,
	0x26D4,
	0x2733,
	0x2747,
	0x274E,
	0x2705,
	0x2734,
	0xD83DDC9F,
	0xD83CDD9A,
	0xD83DDCF3,
	0xD83DDCF4,
	0xD83CDD70,
	0xD83CDD71,
	0xD83CDD8E,
	0xD83CDD7E,
	0xD83DDCA0,
	0x27BF,
	0x267B,
	0x2648,
	0x2649,
	0x264A,
	0x264B,
	0x264C,
	0x264D,
	0x264E,
	0x264F,
	0x2650,
	0x2651,
	0x2652,
	0x2653,
	0x26CE,
	0xD83DDD2F,
	0xD83CDFE7,
	0xD83DDCB9,
	0xD83DDCB2,
	0xD83DDCB1,
	0xA9,
	0xAE,
	0x2122,
	0x303D,
	0x3030,
	0xD83DDD1D,
	0xD83DDD1A,
	0xD83DDD19,
	0xD83DDD1B,
	0xD83DDD1C,
	0x274C,
	0x2B55,
	0x2757,
	0x2753,
	0x2755,
	0x2754,
	0xD83DDD03,
	0xD83DDD5B,
	0xD83DDD67,
	0xD83DDD50,
	0xD83DDD5C,
	0xD83DDD51,
	0xD83DDD5D,
	0xD83DDD52,
	0xD83DDD5E,
	0xD83DDD53,
	0xD83DDD5F,
	0xD83DDD54,
	0xD83DDD60,
	0xD83DDD55,
	0xD83DDD56,
	0xD83DDD57,
	0xD83DDD58,
	0xD83DDD59,
	0xD83DDD5A,
	0xD83DDD61,
	0xD83DDD62,
	0xD83DDD63,
	0xD83DDD64,
	0xD83DDD65,
	0xD83DDD66,
	0x2716,
	0x2795,
	0x2796,
	0x2797,
	0x2660,
	0x2665,
	0x2663,
	0x2666,
	0xD83DDCAE,
	0xD83DDCAF,
	0x2714,
	0x2611,
	0xD83DDD18,
	0xD83DDD17,
	0x27B0,
	0xD83DDD31,
	0xD83DDD32,
	0xD83DDD33,
	0x25FC,
	0x25FB,
	0x25FE,
	0x25FD,
	0x25AA,
	0x25AB,
	0xD83DDD3A,
	0x2B1C,
	0x2B1B,
	0x26AB,
	0x26AA,
	0xD83DDD34,
	0xD83DDD35,
	0xD83DDD3B,
	0xD83DDD36,
	0xD83DDD37,
	0xD83DDD38,
	0xD83DDD39,
};

void writeEmojiCategory(QTextStream &tcpp, uint32 *emojiCategory, uint32 size, const char *name) {
	tcpp << "\tcase dbiet" << name << ": {\n";
	tcpp << "\t\tstatic QVector<EmojiPtr> v" << name << ";\n";
	tcpp << "\t\tif (v" << name << ".isEmpty()) {\n";
	tcpp << "\t\t\tv" << name << ".resize(" << size << ");\n";
	for (uint32 i = 0; i < size; ++i) {
		int index = 0;
		for (EmojisData::const_iterator j = emojisData.cbegin(), e = emojisData.cend(); j != e; ++j) {
			if (j->code == emojiCategory[i]) {
				break;
			}
			++index;
		}
		if (index == emojisData.size()) {
			throw Exception(QString("Could not find emoji from category '%1' with index %2, code %3").arg(name).arg(i).arg(emojiCategory[i]).toUtf8().constData());
		}
		tcpp << "\t\t\tv" << name << "[" << i << "] = &emojis[" << index << "];\n";
	}
	tcpp << "\t\t}\n";
	tcpp << "\t\treturn v" << name << ";\n";
	tcpp << "\t} break;\n\n";
}

bool genEmoji(QString emoji_in, const QString &emoji_out, const QString &emoji_png) {
	QDir d(emoji_in);
	if (!d.exists()) {
		cout << "Could not open emoji input dir '" << emoji_in.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}
	emoji_in = d.absolutePath() + '/';
	QString emoji_in_200x = d.absolutePath() + "_200x/";
	QDir d_200x(emoji_in_200x);
	if (!d.exists()) {
		cout << "Could not open emoji _200x input dir '" << emoji_in_200x.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}

	int currentRow = 0, currentColumn = 0;
	uint32 min1 = 0xFFFFFFFF, max1 = 0, min2 = 0xFFFFFFFF, max2 = 0;

	QStringList filters;
    filters << "*.png" << "*.gif";
	QFileInfoList emojis = d.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
	for (QFileInfoList::const_iterator i = emojis.cbegin(), e = emojis.cend(); i != e; ++i) {
		QString s = i->fileName();
		QRegularExpressionMatch m = QRegularExpression("^([A-F0-9]+)\\.(png|gif)$").match(s);
		if (m.hasMatch()) {
			EmojiData data;
			QString num = m.captured(1);
			if (num.size() != 4 && num.size() != 8 && num.size() != 16) {
				cout << "Bad name found '" << s.toUtf8().constData() << "'!\n";
				continue;
			}

			data.code = ("0x" + num.mid(0, 8)).toUInt(0, 16);
			if (num.size() > 8) {
				data.code2 = ("0x" + num.mid(8)).toUInt(0, 16);
			} else {
				data.code2 = 0;
			}
			data.name = emoji_in + s;
			data.name_200x = emoji_in_200x + s;
			data.x = currentColumn;
			data.y = currentRow;
			++currentColumn;

			if (currentColumn == inRow) {
				++currentRow;
				currentColumn = 0;
			}
			uint32 high = data.code >> 16;
			if (!high) { // small codes
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
			EmojisData::const_iterator i = emojisData.constFind(data.code);
			if (i != emojisData.cend()) {
				cout << QString("Bad emoji code (duplicate) %1 %2 and %3 %4").arg(data.code).arg(data.code2).arg(i->code).arg(i->code2).toUtf8().constData() << "\n";
				continue;
			}
			emojisData.insert(data.code, data);
		} else {
			cout << QString("Bad emoji file found: %1").arg(s).toUtf8().constData() << "\n";
		}
	}

	if (currentColumn) ++currentRow;
	if (!currentRow) {
		cout << "No emojis written..\n";
		return true;
	}

	for (int variantIndex = 0; variantIndex < variantsCount; variantIndex++) {
		int variant = variants[variantIndex], imSize = imSizes[variantIndex];

		QImage emojisImg(inRow * imSize, currentRow * imSize, QImage::Format_ARGB32);
		QPainter p(&emojisImg);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, emojisImg.width(), emojisImg.height(), Qt::transparent);
		for (EmojisData::const_iterator i = emojisData.cbegin(), e = emojisData.cend(); i != e; ++i) {
			QString name = variant ? i->name_200x : i->name;
			int emojiSize = variant ? imSizes[3] : imSizes[0];

			QPixmap emoji(name);
			if (emoji.width() == emojiSize && emoji.height() == emojiSize) {
				if (emojiSize != imSize) {
					emoji = QPixmap::fromImage(emoji.toImage().scaled(imSize, imSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
				}
				p.drawPixmap(i->x * imSize, i->y * imSize, emoji);
			} else {
				cout << "Could not read image '" << name.toUtf8().constData() << "'!\n";
			}
		}
		QString postfix = variantPostfix[variantIndex], emojif = emoji_png + postfix + ".png";
		QByteArray emojib;
		{
			QBuffer ebuf(&emojib);
			if (!emojisImg.save(&ebuf, "PNG")) {
				cout << "Could not save 'emoji" << postfix.toUtf8().constData() << ".png'!\n";
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
an unofficial desktop messaging app, see https://telegram.org\n\
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
Copyright (c) 2014 John Preston, https://tdesktop.com\n\
*/\n";
			tcpp << "#include \"stdafx.h\"\n#include \"gui/emoji_config.h\"\n\n";

			tcpp << "namespace {\n"; // namespace with data
			tcpp << "\tEmojiData *emojis = 0;\n";
			tcpp << "\tchar emojisData[sizeof(EmojiData) * " << emojisData.size() << "];\n";
			tcpp << "}\n\n";

			tcpp << "void initEmoji() {\n";
			tcpp << "\tEmojiData *toFill = emojis = (EmojiData*)emojisData;\n\n";
			tcpp << "\tDBIScale emojiForScale = cRetina() ? dbisTwo : cScale();\n\n";
			tcpp << "\tswitch (emojiForScale) {\n\n";
			for (int variantIndex = 0; variantIndex < variantsCount; ++variantIndex) {
				int imSize = imSizes[variantIndex];
				tcpp << "\tcase " << variantNames[variantIndex] << ":\n";
				for (EmojisData::const_iterator i = emojisData.cbegin(), e = emojisData.cend(); i != e; ++i) {
					int len = i->code2 ? 4 : ((i->code >> 16) ? 2 : 1);
					tcpp << "\t\tnew (toFill++) EmojiData(" << (i->x * imSize) << ", " << (i->y * imSize) << ", " << i->code << ", " << i->code2 << ", " << len << ");\n";
				}
				tcpp << "\tbreak;\n\n";
			}
			tcpp << "\t};\n";
			tcpp << "};\n\n";

			tcpp << "const EmojiData *getEmoji(uint32 code) {\n"; // getter
			tcpp << "\tif (!emojis) return 0;\n\n";
			tcpp << "\tuint32 highCode = code >> 16;\n";

			uint32 index = 0;
			EmojisData::const_iterator i = emojisData.cbegin(), e = emojisData.cend();

			tcpp << "\tif (!highCode) {\n"; // small codes
			tcpp << "\t\tswitch (code) {\n";
			for (; i != e; ++i) { // two small
				if (i->code2) break;
				if (i->code != 169 && i->code != 174) break;

				tcpp << "\t\t\tcase " << i->code << ": return &emojis[" << (index++) << "];\n";
			}
			tcpp << "\t\t}\n\n";
			tcpp << "\t\tif (code < " << min1 << " || code > " << max1 << ") return 0;\n\n";
			tcpp << "\t\tswitch (code) {\n";
			for (; i != e; ++i) {
				if (i->code2 || (i->code >> 16)) break;
				tcpp << "\t\t\tcase " << i->code << ": return &emojis[" << (index++) << "];\n";
			}
			tcpp << "\t\t}\n\n";
			tcpp << "\t\treturn 0;\n";
			tcpp << "\t}\n\n";

			tcpp << "\tif (highCode == 35 || (highCode >= 48 && highCode < 58)) {\n"; // digits
			tcpp << "\t\tif ((code & 0xFFFF) != 0x20E3) return 0;\n\n";
			tcpp << "\t\tswitch (code) {\n";
			for (; i != e; ++i) {
				if (i->code2) break;
				uint32 high = i->code >> 16;
				if (high != 35 && (high < 48 || high >= 58)) break;

				tcpp << "\t\t\tcase " << i->code << ": return &emojis[" << (index++) << "];\n";
			}
			tcpp << "\t\t}\n\n";
			tcpp << "\t\treturn 0;\n";
			tcpp << "\t}\n\n";

			tcpp << "\tif (code < " << min2 << " || code > " << max2 << ") return 0;\n\n";
			tcpp << "\tswitch (code) {\n";
			for (; i != e; ++i) {
				tcpp << "\tcase " << i->code << ": return &emojis[" << (index++) << "];\n";
			}
			tcpp << "\t}\n\n";

			tcpp << "\treturn 0;\n";
			tcpp << "}\n\n";

			// emoji autoreplace
			tcpp << "void findEmoji(const QChar *ch, const QChar *e, const QChar *&newEmojiEnd, uint32 &emojiCode) {\n";
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
				tcpp << tab.repeated(1 + chars.size()) << "\temojiCode = " << i.value() << ";\n";
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

			tcpp << "EmojiPack emojiPack(DBIEmojiTab tab) {\n";
			tcpp << "\tswitch (tab) {\n\n";
			writeEmojiCategory(tcpp, emojiCategory0, sizeof(emojiCategory0) / sizeof(emojiCategory0[0]), "People");
			writeEmojiCategory(tcpp, emojiCategory1, sizeof(emojiCategory1) / sizeof(emojiCategory1[0]), "Nature");
			writeEmojiCategory(tcpp, emojiCategory2, sizeof(emojiCategory2) / sizeof(emojiCategory2[0]), "Objects");
			writeEmojiCategory(tcpp, emojiCategory3, sizeof(emojiCategory3) / sizeof(emojiCategory3[0]), "Places");
			writeEmojiCategory(tcpp, emojiCategory4, sizeof(emojiCategory4) / sizeof(emojiCategory4[0]), "Symbols");
			tcpp << "\t};\n\n";
			tcpp << "\tEmojiPack result;\n";
			tcpp << "\tresult.reserve(cGetRecentEmojis().size());\n";
			tcpp << "\tfor (RecentEmojiPack::const_iterator i = cGetRecentEmojis().cbegin(), e = cGetRecentEmojis().cend(); i != e; ++i) {\n";
			tcpp << "\t\tresult.push_back(i->first);\n";
			tcpp << "\t}\n";
			tcpp << "\treturn result;";
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
