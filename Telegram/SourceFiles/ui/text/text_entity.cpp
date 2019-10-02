/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/text_entity.h"

#include "base/qthelp_url.h"
#include "base/qthelp_regex.h"
#include "base/crc32hash.h"
#include "ui/text/text.h"
#include "ui/widgets/input_fields.h"
#include "ui/emoji_config.h"

#include <QtCore/QStack>
#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace TextUtilities {
namespace {

QString ExpressionMailNameAtEnd() {
	// Matches email first part (before '@') at the end of the string.
	// First we find a domain without protocol (like "gmail.com"), then
	// we find '@' before it and then we look for the name before '@'.
	return QString::fromUtf8("[a-zA-Z\\-_\\.0-9]{1,256}$");
}

QString Quotes() {
	// UTF8 quotes and ellipsis
	return QString::fromUtf8("\xC2\xAB\xC2\xBB\xE2\x80\x9C\xE2\x80\x9D\xE2\x80\x98\xE2\x80\x99\xE2\x80\xA6");
}

QString ExpressionSeparators(const QString &additional) {
	static const auto quotes = Quotes();
	return QString::fromUtf8("\\s\\.,:;<>|'\"\\[\\]\\{\\}\\~\\!\\?\\%\\^\\(\\)\\-\\+=\\x10") + quotes + additional;
}

QString Separators(const QString &additional) {
	static const auto quotes = Quotes();
	return QString::fromUtf8(" \x10\n\r\t.,:;<>|'\"[]{}!?%^()-+=")
		+ QChar(0xfdd0) // QTextBeginningOfFrame
		+ QChar(0xfdd1) // QTextEndOfFrame
		+ QChar(QChar::ParagraphSeparator)
		+ QChar(QChar::LineSeparator)
		+ quotes
		+ additional;
}

QString SeparatorsBold() {
	return Separators(QString::fromUtf8("`~/"));
}

QString SeparatorsItalic() {
	return Separators(QString::fromUtf8("`*~/"));
}

QString SeparatorsStrikeOut() {
	return Separators(QString::fromUtf8("`*~/"));
}

QString SeparatorsMono() {
	return Separators(QString::fromUtf8("*~/"));
}

QString ExpressionHashtag() {
	return QString::fromUtf8("(^|[") + ExpressionSeparators(QString::fromUtf8("`\\*/")) + QString::fromUtf8("])#[\\w]{2,64}([\\W]|$)");
}

QString ExpressionHashtagExclude() {
	return QString::fromUtf8("^#?\\d+$");
}

QString ExpressionMention() {
	return QString::fromUtf8("(^|[") + ExpressionSeparators(QString::fromUtf8("`\\*/")) + QString::fromUtf8("])@[A-Za-z_0-9]{1,32}([\\W]|$)");
}

QString ExpressionBotCommand() {
	return QString::fromUtf8("(^|[") + ExpressionSeparators(QString::fromUtf8("`\\*")) + QString::fromUtf8("])/[A-Za-z_0-9]{1,64}(@[A-Za-z_0-9]{5,32})?([\\W]|$)");
}

QRegularExpression CreateRegExp(const QString &expression) {
	auto result = QRegularExpression(
		expression,
		QRegularExpression::UseUnicodePropertiesOption);
#ifndef OS_MAC_OLD
	result.optimize();
#endif // OS_MAC_OLD
	return result;
}

base::flat_set<int32> CreateValidProtocols() {
	auto result = base::flat_set<int32>();
	const auto addOne = [&](const QString &string) {
		result.insert(base::crc32(string.constData(), string.size() * sizeof(QChar)));
	};
	addOne(QString::fromLatin1("itmss")); // itunes
	addOne(QString::fromLatin1("http"));
	addOne(QString::fromLatin1("https"));
	addOne(QString::fromLatin1("ftp"));
	addOne(QString::fromLatin1("tg")); // local urls
	return result;
}

base::flat_set<int32> CreateValidTopDomains() {
	auto result = base::flat_set<int32>();
	auto addOne = [&result](const QString &string) {
		result.insert(base::crc32(string.constData(), string.size() * sizeof(QChar)));
	};
	addOne(QString::fromLatin1("ac"));
	addOne(QString::fromLatin1("ad"));
	addOne(QString::fromLatin1("ae"));
	addOne(QString::fromLatin1("af"));
	addOne(QString::fromLatin1("ag"));
	addOne(QString::fromLatin1("ai"));
	addOne(QString::fromLatin1("al"));
	addOne(QString::fromLatin1("am"));
	addOne(QString::fromLatin1("an"));
	addOne(QString::fromLatin1("ao"));
	addOne(QString::fromLatin1("aq"));
	addOne(QString::fromLatin1("ar"));
	addOne(QString::fromLatin1("as"));
	addOne(QString::fromLatin1("at"));
	addOne(QString::fromLatin1("au"));
	addOne(QString::fromLatin1("aw"));
	addOne(QString::fromLatin1("ax"));
	addOne(QString::fromLatin1("az"));
	addOne(QString::fromLatin1("ba"));
	addOne(QString::fromLatin1("bb"));
	addOne(QString::fromLatin1("bd"));
	addOne(QString::fromLatin1("be"));
	addOne(QString::fromLatin1("bf"));
	addOne(QString::fromLatin1("bg"));
	addOne(QString::fromLatin1("bh"));
	addOne(QString::fromLatin1("bi"));
	addOne(QString::fromLatin1("bj"));
	addOne(QString::fromLatin1("bm"));
	addOne(QString::fromLatin1("bn"));
	addOne(QString::fromLatin1("bo"));
	addOne(QString::fromLatin1("br"));
	addOne(QString::fromLatin1("bs"));
	addOne(QString::fromLatin1("bt"));
	addOne(QString::fromLatin1("bv"));
	addOne(QString::fromLatin1("bw"));
	addOne(QString::fromLatin1("by"));
	addOne(QString::fromLatin1("bz"));
	addOne(QString::fromLatin1("ca"));
	addOne(QString::fromLatin1("cc"));
	addOne(QString::fromLatin1("cd"));
	addOne(QString::fromLatin1("cf"));
	addOne(QString::fromLatin1("cg"));
	addOne(QString::fromLatin1("ch"));
	addOne(QString::fromLatin1("ci"));
	addOne(QString::fromLatin1("ck"));
	addOne(QString::fromLatin1("cl"));
	addOne(QString::fromLatin1("cm"));
	addOne(QString::fromLatin1("cn"));
	addOne(QString::fromLatin1("co"));
	addOne(QString::fromLatin1("cr"));
	addOne(QString::fromLatin1("cu"));
	addOne(QString::fromLatin1("cv"));
	addOne(QString::fromLatin1("cx"));
	addOne(QString::fromLatin1("cy"));
	addOne(QString::fromLatin1("cz"));
	addOne(QString::fromLatin1("de"));
	addOne(QString::fromLatin1("dj"));
	addOne(QString::fromLatin1("dk"));
	addOne(QString::fromLatin1("dm"));
	addOne(QString::fromLatin1("do"));
	addOne(QString::fromLatin1("dz"));
	addOne(QString::fromLatin1("ec"));
	addOne(QString::fromLatin1("ee"));
	addOne(QString::fromLatin1("eg"));
	addOne(QString::fromLatin1("eh"));
	addOne(QString::fromLatin1("er"));
	addOne(QString::fromLatin1("es"));
	addOne(QString::fromLatin1("et"));
	addOne(QString::fromLatin1("eu"));
	addOne(QString::fromLatin1("fi"));
	addOne(QString::fromLatin1("fj"));
	addOne(QString::fromLatin1("fk"));
	addOne(QString::fromLatin1("fm"));
	addOne(QString::fromLatin1("fo"));
	addOne(QString::fromLatin1("fr"));
	addOne(QString::fromLatin1("ga"));
	addOne(QString::fromLatin1("gd"));
	addOne(QString::fromLatin1("ge"));
	addOne(QString::fromLatin1("gf"));
	addOne(QString::fromLatin1("gg"));
	addOne(QString::fromLatin1("gh"));
	addOne(QString::fromLatin1("gi"));
	addOne(QString::fromLatin1("gl"));
	addOne(QString::fromLatin1("gm"));
	addOne(QString::fromLatin1("gn"));
	addOne(QString::fromLatin1("gp"));
	addOne(QString::fromLatin1("gq"));
	addOne(QString::fromLatin1("gr"));
	addOne(QString::fromLatin1("gs"));
	addOne(QString::fromLatin1("gt"));
	addOne(QString::fromLatin1("gu"));
	addOne(QString::fromLatin1("gw"));
	addOne(QString::fromLatin1("gy"));
	addOne(QString::fromLatin1("hk"));
	addOne(QString::fromLatin1("hm"));
	addOne(QString::fromLatin1("hn"));
	addOne(QString::fromLatin1("hr"));
	addOne(QString::fromLatin1("ht"));
	addOne(QString::fromLatin1("hu"));
	addOne(QString::fromLatin1("id"));
	addOne(QString::fromLatin1("ie"));
	addOne(QString::fromLatin1("il"));
	addOne(QString::fromLatin1("im"));
	addOne(QString::fromLatin1("in"));
	addOne(QString::fromLatin1("io"));
	addOne(QString::fromLatin1("iq"));
	addOne(QString::fromLatin1("ir"));
	addOne(QString::fromLatin1("is"));
	addOne(QString::fromLatin1("it"));
	addOne(QString::fromLatin1("je"));
	addOne(QString::fromLatin1("jm"));
	addOne(QString::fromLatin1("jo"));
	addOne(QString::fromLatin1("jp"));
	addOne(QString::fromLatin1("ke"));
	addOne(QString::fromLatin1("kg"));
	addOne(QString::fromLatin1("kh"));
	addOne(QString::fromLatin1("ki"));
	addOne(QString::fromLatin1("km"));
	addOne(QString::fromLatin1("kn"));
	addOne(QString::fromLatin1("kp"));
	addOne(QString::fromLatin1("kr"));
	addOne(QString::fromLatin1("kw"));
	addOne(QString::fromLatin1("ky"));
	addOne(QString::fromLatin1("kz"));
	addOne(QString::fromLatin1("la"));
	addOne(QString::fromLatin1("lb"));
	addOne(QString::fromLatin1("lc"));
	addOne(QString::fromLatin1("li"));
	addOne(QString::fromLatin1("lk"));
	addOne(QString::fromLatin1("lr"));
	addOne(QString::fromLatin1("ls"));
	addOne(QString::fromLatin1("lt"));
	addOne(QString::fromLatin1("lu"));
	addOne(QString::fromLatin1("lv"));
	addOne(QString::fromLatin1("ly"));
	addOne(QString::fromLatin1("ma"));
	addOne(QString::fromLatin1("mc"));
	addOne(QString::fromLatin1("md"));
	addOne(QString::fromLatin1("me"));
	addOne(QString::fromLatin1("mg"));
	addOne(QString::fromLatin1("mh"));
	addOne(QString::fromLatin1("mk"));
	addOne(QString::fromLatin1("ml"));
	addOne(QString::fromLatin1("mm"));
	addOne(QString::fromLatin1("mn"));
	addOne(QString::fromLatin1("mo"));
	addOne(QString::fromLatin1("mp"));
	addOne(QString::fromLatin1("mq"));
	addOne(QString::fromLatin1("mr"));
	addOne(QString::fromLatin1("ms"));
	addOne(QString::fromLatin1("mt"));
	addOne(QString::fromLatin1("mu"));
	addOne(QString::fromLatin1("mv"));
	addOne(QString::fromLatin1("mw"));
	addOne(QString::fromLatin1("mx"));
	addOne(QString::fromLatin1("my"));
	addOne(QString::fromLatin1("mz"));
	addOne(QString::fromLatin1("na"));
	addOne(QString::fromLatin1("nc"));
	addOne(QString::fromLatin1("ne"));
	addOne(QString::fromLatin1("nf"));
	addOne(QString::fromLatin1("ng"));
	addOne(QString::fromLatin1("ni"));
	addOne(QString::fromLatin1("nl"));
	addOne(QString::fromLatin1("no"));
	addOne(QString::fromLatin1("np"));
	addOne(QString::fromLatin1("nr"));
	addOne(QString::fromLatin1("nu"));
	addOne(QString::fromLatin1("nz"));
	addOne(QString::fromLatin1("om"));
	addOne(QString::fromLatin1("pa"));
	addOne(QString::fromLatin1("pe"));
	addOne(QString::fromLatin1("pf"));
	addOne(QString::fromLatin1("pg"));
	addOne(QString::fromLatin1("ph"));
	addOne(QString::fromLatin1("pk"));
	addOne(QString::fromLatin1("pl"));
	addOne(QString::fromLatin1("pm"));
	addOne(QString::fromLatin1("pn"));
	addOne(QString::fromLatin1("pr"));
	addOne(QString::fromLatin1("ps"));
	addOne(QString::fromLatin1("pt"));
	addOne(QString::fromLatin1("pw"));
	addOne(QString::fromLatin1("py"));
	addOne(QString::fromLatin1("qa"));
	addOne(QString::fromLatin1("re"));
	addOne(QString::fromLatin1("ro"));
	addOne(QString::fromLatin1("ru"));
	addOne(QString::fromLatin1("rs"));
	addOne(QString::fromLatin1("rw"));
	addOne(QString::fromLatin1("sa"));
	addOne(QString::fromLatin1("sb"));
	addOne(QString::fromLatin1("sc"));
	addOne(QString::fromLatin1("sd"));
	addOne(QString::fromLatin1("se"));
	addOne(QString::fromLatin1("sg"));
	addOne(QString::fromLatin1("sh"));
	addOne(QString::fromLatin1("si"));
	addOne(QString::fromLatin1("sj"));
	addOne(QString::fromLatin1("sk"));
	addOne(QString::fromLatin1("sl"));
	addOne(QString::fromLatin1("sm"));
	addOne(QString::fromLatin1("sn"));
	addOne(QString::fromLatin1("so"));
	addOne(QString::fromLatin1("sr"));
	addOne(QString::fromLatin1("ss"));
	addOne(QString::fromLatin1("st"));
	addOne(QString::fromLatin1("su"));
	addOne(QString::fromLatin1("sv"));
	addOne(QString::fromLatin1("sx"));
	addOne(QString::fromLatin1("sy"));
	addOne(QString::fromLatin1("sz"));
	addOne(QString::fromLatin1("tc"));
	addOne(QString::fromLatin1("td"));
	addOne(QString::fromLatin1("tf"));
	addOne(QString::fromLatin1("tg"));
	addOne(QString::fromLatin1("th"));
	addOne(QString::fromLatin1("tj"));
	addOne(QString::fromLatin1("tk"));
	addOne(QString::fromLatin1("tl"));
	addOne(QString::fromLatin1("tm"));
	addOne(QString::fromLatin1("tn"));
	addOne(QString::fromLatin1("to"));
	addOne(QString::fromLatin1("tp"));
	addOne(QString::fromLatin1("tr"));
	addOne(QString::fromLatin1("tt"));
	addOne(QString::fromLatin1("tv"));
	addOne(QString::fromLatin1("tw"));
	addOne(QString::fromLatin1("tz"));
	addOne(QString::fromLatin1("ua"));
	addOne(QString::fromLatin1("ug"));
	addOne(QString::fromLatin1("uk"));
	addOne(QString::fromLatin1("um"));
	addOne(QString::fromLatin1("us"));
	addOne(QString::fromLatin1("uy"));
	addOne(QString::fromLatin1("uz"));
	addOne(QString::fromLatin1("va"));
	addOne(QString::fromLatin1("vc"));
	addOne(QString::fromLatin1("ve"));
	addOne(QString::fromLatin1("vg"));
	addOne(QString::fromLatin1("vi"));
	addOne(QString::fromLatin1("vn"));
	addOne(QString::fromLatin1("vu"));
	addOne(QString::fromLatin1("wf"));
	addOne(QString::fromLatin1("ws"));
	addOne(QString::fromLatin1("ye"));
	addOne(QString::fromLatin1("yt"));
	addOne(QString::fromLatin1("yu"));
	addOne(QString::fromLatin1("za"));
	addOne(QString::fromLatin1("zm"));
	addOne(QString::fromLatin1("zw"));
	addOne(QString::fromLatin1("arpa"));
	addOne(QString::fromLatin1("aero"));
	addOne(QString::fromLatin1("asia"));
	addOne(QString::fromLatin1("biz"));
	addOne(QString::fromLatin1("cat"));
	addOne(QString::fromLatin1("com"));
	addOne(QString::fromLatin1("coop"));
	addOne(QString::fromLatin1("info"));
	addOne(QString::fromLatin1("int"));
	addOne(QString::fromLatin1("jobs"));
	addOne(QString::fromLatin1("mobi"));
	addOne(QString::fromLatin1("museum"));
	addOne(QString::fromLatin1("name"));
	addOne(QString::fromLatin1("net"));
	addOne(QString::fromLatin1("org"));
	addOne(QString::fromLatin1("post"));
	addOne(QString::fromLatin1("pro"));
	addOne(QString::fromLatin1("tel"));
	addOne(QString::fromLatin1("travel"));
	addOne(QString::fromLatin1("xxx"));
	addOne(QString::fromLatin1("edu"));
	addOne(QString::fromLatin1("gov"));
	addOne(QString::fromLatin1("mil"));
	addOne(QString::fromLatin1("local"));
	addOne(QString::fromLatin1("xn--lgbbat1ad8j"));
	addOne(QString::fromLatin1("xn--54b7fta0cc"));
	addOne(QString::fromLatin1("xn--fiqs8s"));
	addOne(QString::fromLatin1("xn--fiqz9s"));
	addOne(QString::fromLatin1("xn--wgbh1c"));
	addOne(QString::fromLatin1("xn--node"));
	addOne(QString::fromLatin1("xn--j6w193g"));
	addOne(QString::fromLatin1("xn--h2brj9c"));
	addOne(QString::fromLatin1("xn--mgbbh1a71e"));
	addOne(QString::fromLatin1("xn--fpcrj9c3d"));
	addOne(QString::fromLatin1("xn--gecrj9c"));
	addOne(QString::fromLatin1("xn--s9brj9c"));
	addOne(QString::fromLatin1("xn--xkc2dl3a5ee0h"));
	addOne(QString::fromLatin1("xn--45brj9c"));
	addOne(QString::fromLatin1("xn--mgba3a4f16a"));
	addOne(QString::fromLatin1("xn--mgbayh7gpa"));
	addOne(QString::fromLatin1("xn--80ao21a"));
	addOne(QString::fromLatin1("xn--mgbx4cd0ab"));
	addOne(QString::fromLatin1("xn--l1acc"));
	addOne(QString::fromLatin1("xn--mgbc0a9azcg"));
	addOne(QString::fromLatin1("xn--mgb9awbf"));
	addOne(QString::fromLatin1("xn--mgbai9azgqp6j"));
	addOne(QString::fromLatin1("xn--ygbi2ammx"));
	addOne(QString::fromLatin1("xn--wgbl6a"));
	addOne(QString::fromLatin1("xn--p1ai"));
	addOne(QString::fromLatin1("xn--mgberp4a5d4ar"));
	addOne(QString::fromLatin1("xn--90a3ac"));
	addOne(QString::fromLatin1("xn--yfro4i67o"));
	addOne(QString::fromLatin1("xn--clchc0ea0b2g2a9gcd"));
	addOne(QString::fromLatin1("xn--3e0b707e"));
	addOne(QString::fromLatin1("xn--fzc2c9e2c"));
	addOne(QString::fromLatin1("xn--xkc2al3hye2a"));
	addOne(QString::fromLatin1("xn--mgbtf8fl"));
	addOne(QString::fromLatin1("xn--kprw13d"));
	addOne(QString::fromLatin1("xn--kpry57d"));
	addOne(QString::fromLatin1("xn--o3cw4h"));
	addOne(QString::fromLatin1("xn--pgbs0dh"));
	addOne(QString::fromLatin1("xn--j1amh"));
	addOne(QString::fromLatin1("xn--mgbaam7a8h"));
	addOne(QString::fromLatin1("xn--mgb2ddes"));
	addOne(QString::fromLatin1("xn--ogbpf8fl"));
	addOne(QString::fromUtf8("\xd1\x80\xd1\x84"));
	return result;
}

// accent char list taken from https://github.com/aristus/accent-folding
inline QChar RemoveOneAccent(uint32 code) {
	switch (code) {
	case 7834: return QChar(97);
	case 193: return QChar(97);
	case 225: return QChar(97);
	case 192: return QChar(97);
	case 224: return QChar(97);
	case 258: return QChar(97);
	case 259: return QChar(97);
	case 7854: return QChar(97);
	case 7855: return QChar(97);
	case 7856: return QChar(97);
	case 7857: return QChar(97);
	case 7860: return QChar(97);
	case 7861: return QChar(97);
	case 7858: return QChar(97);
	case 7859: return QChar(97);
	case 194: return QChar(97);
	case 226: return QChar(97);
	case 7844: return QChar(97);
	case 7845: return QChar(97);
	case 7846: return QChar(97);
	case 7847: return QChar(97);
	case 7850: return QChar(97);
	case 7851: return QChar(97);
	case 7848: return QChar(97);
	case 7849: return QChar(97);
	case 461: return QChar(97);
	case 462: return QChar(97);
	case 197: return QChar(97);
	case 229: return QChar(97);
	case 506: return QChar(97);
	case 507: return QChar(97);
	case 196: return QChar(97);
	case 228: return QChar(97);
	case 478: return QChar(97);
	case 479: return QChar(97);
	case 195: return QChar(97);
	case 227: return QChar(97);
	case 550: return QChar(97);
	case 551: return QChar(97);
	case 480: return QChar(97);
	case 481: return QChar(97);
	case 260: return QChar(97);
	case 261: return QChar(97);
	case 256: return QChar(97);
	case 257: return QChar(97);
	case 7842: return QChar(97);
	case 7843: return QChar(97);
	case 512: return QChar(97);
	case 513: return QChar(97);
	case 514: return QChar(97);
	case 515: return QChar(97);
	case 7840: return QChar(97);
	case 7841: return QChar(97);
	case 7862: return QChar(97);
	case 7863: return QChar(97);
	case 7852: return QChar(97);
	case 7853: return QChar(97);
	case 7680: return QChar(97);
	case 7681: return QChar(97);
	case 570: return QChar(97);
	case 11365: return QChar(97);
	case 508: return QChar(97);
	case 509: return QChar(97);
	case 482: return QChar(97);
	case 483: return QChar(97);
	case 7682: return QChar(98);
	case 7683: return QChar(98);
	case 7684: return QChar(98);
	case 7685: return QChar(98);
	case 7686: return QChar(98);
	case 7687: return QChar(98);
	case 579: return QChar(98);
	case 384: return QChar(98);
	case 7532: return QChar(98);
	case 385: return QChar(98);
	case 595: return QChar(98);
	case 386: return QChar(98);
	case 387: return QChar(98);
	case 262: return QChar(99);
	case 263: return QChar(99);
	case 264: return QChar(99);
	case 265: return QChar(99);
	case 268: return QChar(99);
	case 269: return QChar(99);
	case 266: return QChar(99);
	case 267: return QChar(99);
	case 199: return QChar(99);
	case 231: return QChar(99);
	case 7688: return QChar(99);
	case 7689: return QChar(99);
	case 571: return QChar(99);
	case 572: return QChar(99);
	case 391: return QChar(99);
	case 392: return QChar(99);
	case 597: return QChar(99);
	case 270: return QChar(100);
	case 271: return QChar(100);
	case 7690: return QChar(100);
	case 7691: return QChar(100);
	case 7696: return QChar(100);
	case 7697: return QChar(100);
	case 7692: return QChar(100);
	case 7693: return QChar(100);
	case 7698: return QChar(100);
	case 7699: return QChar(100);
	case 7694: return QChar(100);
	case 7695: return QChar(100);
	case 272: return QChar(100);
	case 273: return QChar(100);
	case 7533: return QChar(100);
	case 393: return QChar(100);
	case 598: return QChar(100);
	case 394: return QChar(100);
	case 599: return QChar(100);
	case 395: return QChar(100);
	case 396: return QChar(100);
	case 545: return QChar(100);
	case 240: return QChar(100);
	case 201: return QChar(101);
	case 399: return QChar(101);
	case 398: return QChar(101);
	case 477: return QChar(101);
	case 233: return QChar(101);
	case 200: return QChar(101);
	case 232: return QChar(101);
	case 276: return QChar(101);
	case 277: return QChar(101);
	case 202: return QChar(101);
	case 234: return QChar(101);
	case 7870: return QChar(101);
	case 7871: return QChar(101);
	case 7872: return QChar(101);
	case 7873: return QChar(101);
	case 7876: return QChar(101);
	case 7877: return QChar(101);
	case 7874: return QChar(101);
	case 7875: return QChar(101);
	case 282: return QChar(101);
	case 283: return QChar(101);
	case 203: return QChar(101);
	case 235: return QChar(101);
	case 7868: return QChar(101);
	case 7869: return QChar(101);
	case 278: return QChar(101);
	case 279: return QChar(101);
	case 552: return QChar(101);
	case 553: return QChar(101);
	case 7708: return QChar(101);
	case 7709: return QChar(101);
	case 280: return QChar(101);
	case 281: return QChar(101);
	case 274: return QChar(101);
	case 275: return QChar(101);
	case 7702: return QChar(101);
	case 7703: return QChar(101);
	case 7700: return QChar(101);
	case 7701: return QChar(101);
	case 7866: return QChar(101);
	case 7867: return QChar(101);
	case 516: return QChar(101);
	case 517: return QChar(101);
	case 518: return QChar(101);
	case 519: return QChar(101);
	case 7864: return QChar(101);
	case 7865: return QChar(101);
	case 7878: return QChar(101);
	case 7879: return QChar(101);
	case 7704: return QChar(101);
	case 7705: return QChar(101);
	case 7706: return QChar(101);
	case 7707: return QChar(101);
	case 582: return QChar(101);
	case 583: return QChar(101);
	case 602: return QChar(101);
	case 605: return QChar(101);
	case 7710: return QChar(102);
	case 7711: return QChar(102);
	case 7534: return QChar(102);
	case 401: return QChar(102);
	case 402: return QChar(102);
	case 500: return QChar(103);
	case 501: return QChar(103);
	case 286: return QChar(103);
	case 287: return QChar(103);
	case 284: return QChar(103);
	case 285: return QChar(103);
	case 486: return QChar(103);
	case 487: return QChar(103);
	case 288: return QChar(103);
	case 289: return QChar(103);
	case 290: return QChar(103);
	case 291: return QChar(103);
	case 7712: return QChar(103);
	case 7713: return QChar(103);
	case 484: return QChar(103);
	case 485: return QChar(103);
	case 403: return QChar(103);
	case 608: return QChar(103);
	case 292: return QChar(104);
	case 293: return QChar(104);
	case 542: return QChar(104);
	case 543: return QChar(104);
	case 7718: return QChar(104);
	case 7719: return QChar(104);
	case 7714: return QChar(104);
	case 7715: return QChar(104);
	case 7720: return QChar(104);
	case 7721: return QChar(104);
	case 7716: return QChar(104);
	case 7717: return QChar(104);
	case 7722: return QChar(104);
	case 7723: return QChar(104);
	case 817: return QChar(104);
	case 7830: return QChar(104);
	case 294: return QChar(104);
	case 295: return QChar(104);
	case 11367: return QChar(104);
	case 11368: return QChar(104);
	case 205: return QChar(105);
	case 237: return QChar(105);
	case 204: return QChar(105);
	case 236: return QChar(105);
	case 300: return QChar(105);
	case 301: return QChar(105);
	case 206: return QChar(105);
	case 238: return QChar(105);
	case 463: return QChar(105);
	case 464: return QChar(105);
	case 207: return QChar(105);
	case 239: return QChar(105);
	case 7726: return QChar(105);
	case 7727: return QChar(105);
	case 296: return QChar(105);
	case 297: return QChar(105);
	case 304: return QChar(105);
	case 302: return QChar(105);
	case 303: return QChar(105);
	case 298: return QChar(105);
	case 299: return QChar(105);
	case 7880: return QChar(105);
	case 7881: return QChar(105);
	case 520: return QChar(105);
	case 521: return QChar(105);
	case 522: return QChar(105);
	case 523: return QChar(105);
	case 7882: return QChar(105);
	case 7883: return QChar(105);
	case 7724: return QChar(105);
	case 7725: return QChar(105);
	case 305: return QChar(105);
	case 407: return QChar(105);
	case 616: return QChar(105);
	case 308: return QChar(106);
	case 309: return QChar(106);
	case 780: return QChar(106);
	case 496: return QChar(106);
	case 567: return QChar(106);
	case 584: return QChar(106);
	case 585: return QChar(106);
	case 669: return QChar(106);
	case 607: return QChar(106);
	case 644: return QChar(106);
	case 7728: return QChar(107);
	case 7729: return QChar(107);
	case 488: return QChar(107);
	case 489: return QChar(107);
	case 310: return QChar(107);
	case 311: return QChar(107);
	case 7730: return QChar(107);
	case 7731: return QChar(107);
	case 7732: return QChar(107);
	case 7733: return QChar(107);
	case 408: return QChar(107);
	case 409: return QChar(107);
	case 11369: return QChar(107);
	case 11370: return QChar(107);
	case 313: return QChar(97);
	case 314: return QChar(108);
	case 317: return QChar(108);
	case 318: return QChar(108);
	case 315: return QChar(108);
	case 316: return QChar(108);
	case 7734: return QChar(108);
	case 7735: return QChar(108);
	case 7736: return QChar(108);
	case 7737: return QChar(108);
	case 7740: return QChar(108);
	case 7741: return QChar(108);
	case 7738: return QChar(108);
	case 7739: return QChar(108);
	case 321: return QChar(108);
	case 322: return QChar(108);
	case 803: return QChar(108);
	case 319: return QChar(108);
	case 320: return QChar(108);
	case 573: return QChar(108);
	case 410: return QChar(108);
	case 11360: return QChar(108);
	case 11361: return QChar(108);
	case 11362: return QChar(108);
	case 619: return QChar(108);
	case 620: return QChar(108);
	case 621: return QChar(108);
	case 564: return QChar(108);
	case 7742: return QChar(109);
	case 7743: return QChar(109);
	case 7744: return QChar(109);
	case 7745: return QChar(109);
	case 7746: return QChar(109);
	case 7747: return QChar(109);
	case 625: return QChar(109);
	case 323: return QChar(110);
	case 324: return QChar(110);
	case 504: return QChar(110);
	case 505: return QChar(110);
	case 327: return QChar(110);
	case 328: return QChar(110);
	case 209: return QChar(110);
	case 241: return QChar(110);
	case 7748: return QChar(110);
	case 7749: return QChar(110);
	case 325: return QChar(110);
	case 326: return QChar(110);
	case 7750: return QChar(110);
	case 7751: return QChar(110);
	case 7754: return QChar(110);
	case 7755: return QChar(110);
	case 7752: return QChar(110);
	case 7753: return QChar(110);
	case 413: return QChar(110);
	case 626: return QChar(110);
	case 544: return QChar(110);
	case 414: return QChar(110);
	case 627: return QChar(110);
	case 565: return QChar(110);
	case 776: return QChar(116);
	case 211: return QChar(111);
	case 243: return QChar(111);
	case 210: return QChar(111);
	case 242: return QChar(111);
	case 334: return QChar(111);
	case 335: return QChar(111);
	case 212: return QChar(111);
	case 244: return QChar(111);
	case 7888: return QChar(111);
	case 7889: return QChar(111);
	case 7890: return QChar(111);
	case 7891: return QChar(111);
	case 7894: return QChar(111);
	case 7895: return QChar(111);
	case 7892: return QChar(111);
	case 7893: return QChar(111);
	case 465: return QChar(111);
	case 466: return QChar(111);
	case 214: return QChar(111);
	case 246: return QChar(111);
	case 554: return QChar(111);
	case 555: return QChar(111);
	case 336: return QChar(111);
	case 337: return QChar(111);
	case 213: return QChar(111);
	case 245: return QChar(111);
	case 7756: return QChar(111);
	case 7757: return QChar(111);
	case 7758: return QChar(111);
	case 7759: return QChar(111);
	case 556: return QChar(111);
	case 557: return QChar(111);
	case 558: return QChar(111);
	case 559: return QChar(111);
	case 560: return QChar(111);
	case 561: return QChar(111);
	case 216: return QChar(111);
	case 248: return QChar(111);
	case 510: return QChar(111);
	case 511: return QChar(111);
	case 490: return QChar(111);
	case 491: return QChar(111);
	case 492: return QChar(111);
	case 493: return QChar(111);
	case 332: return QChar(111);
	case 333: return QChar(111);
	case 7762: return QChar(111);
	case 7763: return QChar(111);
	case 7760: return QChar(111);
	case 7761: return QChar(111);
	case 7886: return QChar(111);
	case 7887: return QChar(111);
	case 524: return QChar(111);
	case 525: return QChar(111);
	case 526: return QChar(111);
	case 527: return QChar(111);
	case 416: return QChar(111);
	case 417: return QChar(111);
	case 7898: return QChar(111);
	case 7899: return QChar(111);
	case 7900: return QChar(111);
	case 7901: return QChar(111);
	case 7904: return QChar(111);
	case 7905: return QChar(111);
	case 7902: return QChar(111);
	case 7903: return QChar(111);
	case 7906: return QChar(111);
	case 7907: return QChar(111);
	case 7884: return QChar(111);
	case 7885: return QChar(111);
	case 7896: return QChar(111);
	case 7897: return QChar(111);
	case 415: return QChar(111);
	case 629: return QChar(111);
	case 7764: return QChar(112);
	case 7765: return QChar(112);
	case 7766: return QChar(112);
	case 7767: return QChar(112);
	case 11363: return QChar(112);
	case 420: return QChar(112);
	case 421: return QChar(112);
	case 771: return QChar(112);
	case 672: return QChar(113);
	case 586: return QChar(113);
	case 587: return QChar(113);
	case 340: return QChar(114);
	case 341: return QChar(114);
	case 344: return QChar(114);
	case 345: return QChar(114);
	case 7768: return QChar(114);
	case 7769: return QChar(114);
	case 342: return QChar(114);
	case 343: return QChar(114);
	case 528: return QChar(114);
	case 529: return QChar(114);
	case 530: return QChar(114);
	case 531: return QChar(114);
	case 7770: return QChar(114);
	case 7771: return QChar(114);
	case 7772: return QChar(114);
	case 7773: return QChar(114);
	case 7774: return QChar(114);
	case 7775: return QChar(114);
	case 588: return QChar(114);
	case 589: return QChar(114);
	case 7538: return QChar(114);
	case 636: return QChar(114);
	case 11364: return QChar(114);
	case 637: return QChar(114);
	case 638: return QChar(114);
	case 7539: return QChar(114);
	case 223: return QChar(115);
	case 346: return QChar(115);
	case 347: return QChar(115);
	case 7780: return QChar(115);
	case 7781: return QChar(115);
	case 348: return QChar(115);
	case 349: return QChar(115);
	case 352: return QChar(115);
	case 353: return QChar(115);
	case 7782: return QChar(115);
	case 7783: return QChar(115);
	case 7776: return QChar(115);
	case 7777: return QChar(115);
	case 7835: return QChar(115);
	case 350: return QChar(115);
	case 351: return QChar(115);
	case 7778: return QChar(115);
	case 7779: return QChar(115);
	case 7784: return QChar(115);
	case 7785: return QChar(115);
	case 536: return QChar(115);
	case 537: return QChar(115);
	case 642: return QChar(115);
	case 809: return QChar(115);
	case 222: return QChar(116);
	case 254: return QChar(116);
	case 356: return QChar(116);
	case 357: return QChar(116);
	case 7831: return QChar(116);
	case 7786: return QChar(116);
	case 7787: return QChar(116);
	case 354: return QChar(116);
	case 355: return QChar(116);
	case 7788: return QChar(116);
	case 7789: return QChar(116);
	case 538: return QChar(116);
	case 539: return QChar(116);
	case 7792: return QChar(116);
	case 7793: return QChar(116);
	case 7790: return QChar(116);
	case 7791: return QChar(116);
	case 358: return QChar(116);
	case 359: return QChar(116);
	case 574: return QChar(116);
	case 11366: return QChar(116);
	case 7541: return QChar(116);
	case 427: return QChar(116);
	case 428: return QChar(116);
	case 429: return QChar(116);
	case 430: return QChar(116);
	case 648: return QChar(116);
	case 566: return QChar(116);
	case 218: return QChar(117);
	case 250: return QChar(117);
	case 217: return QChar(117);
	case 249: return QChar(117);
	case 364: return QChar(117);
	case 365: return QChar(117);
	case 219: return QChar(117);
	case 251: return QChar(117);
	case 467: return QChar(117);
	case 468: return QChar(117);
	case 366: return QChar(117);
	case 367: return QChar(117);
	case 220: return QChar(117);
	case 252: return QChar(117);
	case 471: return QChar(117);
	case 472: return QChar(117);
	case 475: return QChar(117);
	case 476: return QChar(117);
	case 473: return QChar(117);
	case 474: return QChar(117);
	case 469: return QChar(117);
	case 470: return QChar(117);
	case 368: return QChar(117);
	case 369: return QChar(117);
	case 360: return QChar(117);
	case 361: return QChar(117);
	case 7800: return QChar(117);
	case 7801: return QChar(117);
	case 370: return QChar(117);
	case 371: return QChar(117);
	case 362: return QChar(117);
	case 363: return QChar(117);
	case 7802: return QChar(117);
	case 7803: return QChar(117);
	case 7910: return QChar(117);
	case 7911: return QChar(117);
	case 532: return QChar(117);
	case 533: return QChar(117);
	case 534: return QChar(117);
	case 535: return QChar(117);
	case 431: return QChar(117);
	case 432: return QChar(117);
	case 7912: return QChar(117);
	case 7913: return QChar(117);
	case 7914: return QChar(117);
	case 7915: return QChar(117);
	case 7918: return QChar(117);
	case 7919: return QChar(117);
	case 7916: return QChar(117);
	case 7917: return QChar(117);
	case 7920: return QChar(117);
	case 7921: return QChar(117);
	case 7908: return QChar(117);
	case 7909: return QChar(117);
	case 7794: return QChar(117);
	case 7795: return QChar(117);
	case 7798: return QChar(117);
	case 7799: return QChar(117);
	case 7796: return QChar(117);
	case 7797: return QChar(117);
	case 580: return QChar(117);
	case 649: return QChar(117);
	case 7804: return QChar(118);
	case 7805: return QChar(118);
	case 7806: return QChar(118);
	case 7807: return QChar(118);
	case 434: return QChar(118);
	case 651: return QChar(118);
	case 7810: return QChar(119);
	case 7811: return QChar(119);
	case 7808: return QChar(119);
	case 7809: return QChar(119);
	case 372: return QChar(119);
	case 373: return QChar(119);
	case 778: return QChar(121);
	case 7832: return QChar(119);
	case 7812: return QChar(119);
	case 7813: return QChar(119);
	case 7814: return QChar(119);
	case 7815: return QChar(119);
	case 7816: return QChar(119);
	case 7817: return QChar(119);
	case 7820: return QChar(120);
	case 7821: return QChar(120);
	case 7818: return QChar(120);
	case 7819: return QChar(120);
	case 221: return QChar(121);
	case 253: return QChar(121);
	case 7922: return QChar(121);
	case 7923: return QChar(121);
	case 374: return QChar(121);
	case 375: return QChar(121);
	case 7833: return QChar(121);
	case 376: return QChar(121);
	case 255: return QChar(121);
	case 7928: return QChar(121);
	case 7929: return QChar(121);
	case 7822: return QChar(121);
	case 7823: return QChar(121);
	case 562: return QChar(121);
	case 563: return QChar(121);
	case 7926: return QChar(121);
	case 7927: return QChar(121);
	case 7924: return QChar(121);
	case 7925: return QChar(121);
	case 655: return QChar(121);
	case 590: return QChar(121);
	case 591: return QChar(121);
	case 435: return QChar(121);
	case 436: return QChar(121);
	case 377: return QChar(122);
	case 378: return QChar(122);
	case 7824: return QChar(122);
	case 7825: return QChar(122);
	case 381: return QChar(122);
	case 382: return QChar(122);
	case 379: return QChar(122);
	case 380: return QChar(122);
	case 7826: return QChar(122);
	case 7827: return QChar(122);
	case 7828: return QChar(122);
	case 7829: return QChar(122);
	case 437: return QChar(122);
	case 438: return QChar(122);
	case 548: return QChar(122);
	case 549: return QChar(122);
	case 656: return QChar(122);
	case 657: return QChar(122);
	case 11371: return QChar(122);
	case 11372: return QChar(122);
	case 494: return QChar(122);
	case 495: return QChar(122);
	case 442: return QChar(122);
	case 65298: return QChar(50);
	case 65302: return QChar(54);
	case 65314: return QChar(66);
	case 65318: return QChar(70);
	case 65322: return QChar(74);
	case 65326: return QChar(78);
	case 65330: return QChar(82);
	case 65334: return QChar(86);
	case 65338: return QChar(90);
	case 65346: return QChar(98);
	case 65350: return QChar(102);
	case 65354: return QChar(106);
	case 65358: return QChar(110);
	case 65362: return QChar(114);
	case 65366: return QChar(118);
	case 65370: return QChar(122);
	case 65297: return QChar(49);
	case 65301: return QChar(53);
	case 65305: return QChar(57);
	case 65313: return QChar(65);
	case 65317: return QChar(69);
	case 65321: return QChar(73);
	case 65325: return QChar(77);
	case 65329: return QChar(81);
	case 65333: return QChar(85);
	case 65337: return QChar(89);
	case 65345: return QChar(97);
	case 65349: return QChar(101);
	case 65353: return QChar(105);
	case 65357: return QChar(109);
	case 65361: return QChar(113);
	case 65365: return QChar(117);
	case 65369: return QChar(121);
	case 65296: return QChar(48);
	case 65300: return QChar(52);
	case 65304: return QChar(56);
	case 65316: return QChar(68);
	case 65320: return QChar(72);
	case 65324: return QChar(76);
	case 65328: return QChar(80);
	case 65332: return QChar(84);
	case 65336: return QChar(88);
	case 65348: return QChar(100);
	case 65352: return QChar(104);
	case 65356: return QChar(108);
	case 65360: return QChar(112);
	case 65364: return QChar(116);
	case 65368: return QChar(120);
	case 65299: return QChar(51);
	case 65303: return QChar(55);
	case 65315: return QChar(67);
	case 65319: return QChar(71);
	case 65323: return QChar(75);
	case 65327: return QChar(79);
	case 65331: return QChar(83);
	case 65335: return QChar(87);
	case 65347: return QChar(99);
	case 65351: return QChar(103);
	case 65355: return QChar(107);
	case 65359: return QChar(111);
	case 65363: return QChar(115);
	case 65367: return QChar(119);
	case 1105: return QChar(1077);
	default:
	break;
	}
	return QChar(0);
}

const QRegularExpression &RegExpWordSplit() {
	static const auto result = QRegularExpression(QString::fromLatin1("[\\@\\s\\-\\+\\(\\)\\[\\]\\{\\}\\<\\>\\,\\.\\:\\!\\_\\;\\\"\\'\\x0]"));
	return result;
}

[[nodiscard]] QString ExpandCustomLinks(const TextWithTags &text) {
	const auto entities = ConvertTextTagsToEntities(text.tags);
	auto &&urls = ranges::make_subrange(
		entities.begin(),
		entities.end()
	) | ranges::view::filter([](const EntityInText &entity) {
		return entity.type() == EntityType::CustomUrl;
	});
	const auto &original = text.text;
	if (urls.begin() == urls.end()) {
		return original;
	}
	auto result = QString();
	auto offset = 0;
	for (const auto &entity : urls) {
		const auto till = entity.offset() + entity.length();
		if (till > offset) {
			result.append(original.midRef(offset, till - offset));
		}
		result.append(qstr(" (")).append(entity.data()).append(')');
		offset = till;
	}
	if (original.size() > offset) {
		result.append(original.midRef(offset));
	}
	return result;
}

std::unique_ptr<QMimeData> MimeDataFromText(
		TextWithTags &&text,
		const QString &expanded) {
	if (expanded.isEmpty()) {
		return nullptr;
	}

	auto result = std::make_unique<QMimeData>();
	result->setText(expanded);
	if (!text.tags.isEmpty()) {
		for (auto &tag : text.tags) {
			tag.id = Ui::Integration::Instance().convertTagToMimeTag(tag.id);
		}
		result->setData(
			TextUtilities::TagsTextMimeType(),
			text.text.toUtf8());
		result->setData(
			TextUtilities::TagsMimeType(),
			TextUtilities::SerializeTags(text.tags));
	}
	return result;
}

} // namespace

const QRegularExpression &RegExpMailNameAtEnd() {
	static const auto result = CreateRegExp(ExpressionMailNameAtEnd());
	return result;
}

const QRegularExpression &RegExpHashtag() {
	static const auto result = CreateRegExp(ExpressionHashtag());
	return result;
}

const QRegularExpression &RegExpHashtagExclude() {
	static const auto result = CreateRegExp(ExpressionHashtagExclude());
	return result;
}

const QRegularExpression &RegExpMention() {
	static const auto result = CreateRegExp(ExpressionMention());
	return result;
}

const QRegularExpression &RegExpBotCommand() {
	static const auto result = CreateRegExp(ExpressionBotCommand());
	return result;
}

QString MarkdownBoldGoodBefore() {
	return SeparatorsBold();
}

QString MarkdownBoldBadAfter() {
	return QString::fromLatin1("*");
}

QString MarkdownItalicGoodBefore() {
	return SeparatorsItalic();
}

QString MarkdownItalicBadAfter() {
	return QString::fromLatin1("_");
}

QString MarkdownStrikeOutGoodBefore() {
	return SeparatorsStrikeOut();
}

QString MarkdownStrikeOutBadAfter() {
	return QString::fromLatin1("~");
}

QString MarkdownCodeGoodBefore() {
	return SeparatorsMono();
}

QString MarkdownCodeBadAfter() {
	return QString::fromLatin1("`\n\r");
}

QString MarkdownPreGoodBefore() {
	return SeparatorsMono();
}

QString MarkdownPreBadAfter() {
	return QString::fromLatin1("`");
}

bool IsValidProtocol(const QString &protocol) {
	static const auto list = CreateValidProtocols();
	return list.contains(base::crc32(protocol.constData(), protocol.size() * sizeof(QChar)));
}

bool IsValidTopDomain(const QString &protocol) {
	static const auto list = CreateValidTopDomains();
	return list.contains(base::crc32(protocol.constData(), protocol.size() * sizeof(QChar)));
}

QString Clean(const QString &text) {
	auto result = text;
	for (auto s = text.unicode(), ch = s, e = text.unicode() + text.size(); ch != e; ++ch) {
		if (*ch == TextCommand) {
			result[int(ch - s)] = QChar::Space;
		}
	}
	return result;
}

QString EscapeForRichParsing(const QString &text) {
	QString result;
	result.reserve(text.size());
	auto s = text.constData(), ch = s;
	for (const QChar *e = s + text.size(); ch != e; ++ch) {
		if (*ch == TextCommand) {
			if (ch > s) result.append(s, ch - s);
			result.append(QChar::Space);
			s = ch + 1;
			continue;
		}
		if (ch->unicode() == '\\' || ch->unicode() == '[') {
			if (ch > s) result.append(s, ch - s);
			result.append('\\');
			s = ch;
			continue;
		}
	}
	if (ch > s) result.append(s, ch - s);
	return result;
}

QString SingleLine(const QString &text) {
	auto result = text;
	auto s = text.unicode(), ch = s, e = text.unicode() + text.size();

	// Trim.
	while (s < e && chIsTrimmed(*s)) {
		++s;
	}
	while (s < e && chIsTrimmed(*(e - 1))) {
		--e;
	}
	if (e - s != text.size()) {
		result = text.mid(s - text.unicode(), e - s);
	}

	for (auto ch = s; ch != e; ++ch) {
		if (chIsNewline(*ch) || *ch == TextCommand) {
			result[int(ch - s)] = QChar::Space;
		}
	}
	return result;
}

QString RemoveAccents(const QString &text) {
	auto result = text;
	auto copying = false;
	auto i = 0;
	for (auto s = text.unicode(), ch = s, e = text.unicode() + text.size(); ch != e; ++ch, ++i) {
		if (ch->unicode() < 128) {
			if (copying) result[i] = *ch;
			continue;
		}
		if (chIsDiac(*ch)) {
			copying = true;
			--i;
			continue;
		}
		if (ch->isHighSurrogate() && ch + 1 < e && (ch + 1)->isLowSurrogate()) {
			auto noAccent = RemoveOneAccent(QChar::surrogateToUcs4(*ch, *(ch + 1)));
			if (noAccent.unicode() > 0) {
				copying = true;
				result[i] = noAccent;
			} else {
				if (copying) result[i] = *ch;
				++ch, ++i;
				if (copying) result[i] = *ch;
			}
		} else {
			auto noAccent = RemoveOneAccent(ch->unicode());
			if (noAccent.unicode() > 0 && noAccent != *ch) {
				result[i] = noAccent;
			} else if (copying) {
				result[i] = *ch;
			}
		}
	}
	return (i < result.size()) ? result.mid(0, i) : result;
}

QString RemoveEmoji(const QString &text) {
	auto result = QString();
	result.reserve(text.size());

	auto begin = text.data();
	const auto end = begin + text.size();
	while (begin != end) {
		auto length = 0;
		if (Ui::Emoji::Find(begin, end, &length)) {
			begin += length;
		} else {
			result.append(*begin++);
		}
	}
	return result;
}

QStringList PrepareSearchWords(
		const QString &query,
		const QRegularExpression *SplitterOverride) {
	auto clean = RemoveAccents(query.trimmed().toLower());
	auto result = QStringList();
	if (!clean.isEmpty()) {
		auto list = clean.split(SplitterOverride
			? *SplitterOverride
			: RegExpWordSplit(),
			QString::SkipEmptyParts);
		auto size = list.size();
		result.reserve(list.size());
		for (const auto &word : std::as_const(list)) {
			auto trimmed = word.trimmed();
			if (!trimmed.isEmpty()) {
				result.push_back(trimmed);
			}
		}
	}
	return result;
}

bool CutPart(TextWithEntities &sending, TextWithEntities &left, int32 limit) {
	if (left.text.isEmpty() || !limit) return false;

	int32 currentEntity = 0, goodEntity = currentEntity, entityCount = left.entities.size();
	bool goodInEntity = false, goodCanBreakEntity = false;

	int32 s = 0, half = limit / 2, goodLevel = 0;
	for (const QChar *start = left.text.constData(), *ch = start, *end = left.text.constEnd(), *good = ch; ch != end; ++ch, ++s) {
		while (currentEntity < entityCount && ch >= start + left.entities[currentEntity].offset() + left.entities[currentEntity].length()) {
			++currentEntity;
		}

		if (s > half) {
			bool inEntity = (currentEntity < entityCount) && (ch > start + left.entities[currentEntity].offset()) && (ch < start + left.entities[currentEntity].offset() + left.entities[currentEntity].length());
			EntityType entityType = (currentEntity < entityCount) ? left.entities[currentEntity].type() : EntityType::Invalid;
			bool canBreakEntity = (entityType == EntityType::Pre || entityType == EntityType::Code); // #TODO entities
			int32 noEntityLevel = inEntity ? 0 : 1;

			auto markGoodAsLevel = [&](int newLevel) {
				if (goodLevel > newLevel) {
					return;
				}
				goodLevel = newLevel;
				good = ch;
				goodEntity = currentEntity;
				goodInEntity = inEntity;
				goodCanBreakEntity = canBreakEntity;
			};

			if (inEntity && !canBreakEntity) {
				markGoodAsLevel(0);
			} else {
				if (chIsNewline(*ch)) {
					if (inEntity) {
						if (ch + 1 < end && chIsNewline(*(ch + 1))) {
							markGoodAsLevel(12);
						} else {
							markGoodAsLevel(11);
						}
					} else if (ch + 1 < end && chIsNewline(*(ch + 1))) {
						markGoodAsLevel(15);
					} else if (currentEntity < entityCount && ch + 1 == start + left.entities[currentEntity].offset() && left.entities[currentEntity].type() == EntityType::Pre) {
						markGoodAsLevel(14);
					} else if (currentEntity > 0 && ch == start + left.entities[currentEntity - 1].offset() + left.entities[currentEntity - 1].length() && left.entities[currentEntity - 1].type() == EntityType::Pre) {
						markGoodAsLevel(14);
					} else {
						markGoodAsLevel(13);
					}
				} else if (chIsSpace(*ch)) {
					if (chIsSentenceEnd(*(ch - 1))) {
						markGoodAsLevel(9 + noEntityLevel);
					} else if (chIsSentencePartEnd(*(ch - 1))) {
						markGoodAsLevel(7 + noEntityLevel);
					} else {
						markGoodAsLevel(5 + noEntityLevel);
					}
				} else if (chIsWordSeparator(*(ch - 1))) {
					markGoodAsLevel(3 + noEntityLevel);
				} else {
					markGoodAsLevel(1 + noEntityLevel);
				}
			}
		}

		int elen = 0;
		if (auto e = Ui::Emoji::Find(ch, end, &elen)) {
			for (int i = 0; i < elen; ++i, ++ch, ++s) {
				if (ch->isHighSurrogate() && i + 1 < elen && (ch + 1)->isLowSurrogate()) {
					++ch;
					++i;
				}
			}
			--ch;
			--s;
		} else if (ch->isHighSurrogate() && ch + 1 < end && (ch + 1)->isLowSurrogate()) {
			++ch;
		}
		if (s >= limit) {
			sending.text = left.text.mid(0, good - start);
			left.text = left.text.mid(good - start);
			if (goodInEntity) {
				if (goodCanBreakEntity) {
					sending.entities = left.entities.mid(0, goodEntity + 1);
					sending.entities.back().updateTextEnd(good - start);
					left.entities = left.entities.mid(goodEntity);
					for (auto &entity : left.entities) {
						entity.shiftLeft(good - start);
					}
				} else {
					sending.entities = left.entities.mid(0, goodEntity);
					left.entities = left.entities.mid(goodEntity + 1);
				}
			} else {
				sending.entities = left.entities.mid(0, goodEntity);
				left.entities = left.entities.mid(goodEntity);
				for (auto &entity : left.entities) {
					entity.shiftLeft(good - start);
				}
			}
			return true;
		}
	}
	sending.text = left.text;
	left.text = QString();
	sending.entities = left.entities;
	left.entities = EntitiesInText();
	return true;
}

bool textcmdStartsLink(const QChar *start, int32 len, int32 commandOffset) {
	if (commandOffset + 2 < len) {
		if (*(start + commandOffset + 1) == TextCommandLinkIndex) {
			return (*(start + commandOffset + 2) != 0);
		}
		return (*(start + commandOffset + 1) != TextCommandLinkText);
	}
	return false;
}

bool checkTagStartInCommand(const QChar *start, int32 len, int32 tagStart, int32 &commandOffset, bool &commandIsLink, bool &inLink) {
	bool inCommand = false;
	const QChar *commandEnd = start + commandOffset;
	while (commandOffset < len && tagStart > commandOffset) { // skip commands, evaluating are we in link or not
		commandEnd = textSkipCommand(start + commandOffset, start + len);
		if (commandEnd > start + commandOffset) {
			if (tagStart < (commandEnd - start)) {
				inCommand = true;
				break;
			}
			for (commandOffset = commandEnd - start; commandOffset < len; ++commandOffset) {
				if (*(start + commandOffset) == TextCommand) {
					inLink = commandIsLink;
					commandIsLink = textcmdStartsLink(start, len, commandOffset);
					break;
				}
			}
			if (commandOffset >= len) {
				inLink = commandIsLink;
				commandIsLink = false;
			}
		} else {
			break;
		}
	}
	if (inCommand) {
		commandOffset = commandEnd - start;
	}
	return inCommand;
}

TextWithEntities ParseEntities(const QString &text, int32 flags) {
	const auto rich = ((flags & TextParseRichText) != 0);
	auto result = TextWithEntities{ text, EntitiesInText() };
	ParseEntities(result, flags, rich);
	return result;
}

// Some code is duplicated in message_field.cpp!
void ParseEntities(TextWithEntities &result, int32 flags, bool rich) {
	constexpr auto kNotFound = std::numeric_limits<int>::max();

	auto newEntities = EntitiesInText();
	bool withHashtags = (flags & TextParseHashtags);
	bool withMentions = (flags & TextParseMentions);
	bool withBotCommands = (flags & TextParseBotCommands);

	int existingEntityIndex = 0, existingEntitiesCount = result.entities.size();
	int existingEntityEnd = 0;

	int32 len = result.text.size(), commandOffset = rich ? 0 : len;
	bool inLink = false, commandIsLink = false;
	const QChar *start = result.text.constData(), *end = start + result.text.size();
	for (int32 offset = 0, matchOffset = offset, mentionSkip = 0; offset < len;) {
		if (commandOffset <= offset) {
			for (commandOffset = offset; commandOffset < len; ++commandOffset) {
				if (*(start + commandOffset) == TextCommand) {
					inLink = commandIsLink;
					commandIsLink = textcmdStartsLink(start, len, commandOffset);
					break;
				}
			}
		}
		auto mDomain = qthelp::RegExpDomain().match(result.text, matchOffset);
		auto mExplicitDomain = qthelp::RegExpDomainExplicit().match(result.text, matchOffset);
		auto mHashtag = withHashtags ? RegExpHashtag().match(result.text, matchOffset) : QRegularExpressionMatch();
		auto mMention = withMentions ? RegExpMention().match(result.text, qMax(mentionSkip, matchOffset)) : QRegularExpressionMatch();
		auto mBotCommand = withBotCommands ? RegExpBotCommand().match(result.text, matchOffset) : QRegularExpressionMatch();

		auto lnkType = EntityType::Url;
		int32 lnkStart = 0, lnkLength = 0;
		auto domainStart = mDomain.hasMatch() ? mDomain.capturedStart() : kNotFound,
			domainEnd = mDomain.hasMatch() ? mDomain.capturedEnd() : kNotFound,
			explicitDomainStart = mExplicitDomain.hasMatch() ? mExplicitDomain.capturedStart() : kNotFound,
			explicitDomainEnd = mExplicitDomain.hasMatch() ? mExplicitDomain.capturedEnd() : kNotFound,
			hashtagStart = mHashtag.hasMatch() ? mHashtag.capturedStart() : kNotFound,
			hashtagEnd = mHashtag.hasMatch() ? mHashtag.capturedEnd() : kNotFound,
			mentionStart = mMention.hasMatch() ? mMention.capturedStart() : kNotFound,
			mentionEnd = mMention.hasMatch() ? mMention.capturedEnd() : kNotFound,
			botCommandStart = mBotCommand.hasMatch() ? mBotCommand.capturedStart() : kNotFound,
			botCommandEnd = mBotCommand.hasMatch() ? mBotCommand.capturedEnd() : kNotFound;
		auto hashtagIgnore = false;
		auto mentionIgnore = false;

		if (mHashtag.hasMatch()) {
			if (!mHashtag.capturedRef(1).isEmpty()) {
				++hashtagStart;
			}
			if (!mHashtag.capturedRef(2).isEmpty()) {
				--hashtagEnd;
			}
			if (RegExpHashtagExclude().match(
				result.text.mid(
					hashtagStart + 1,
					hashtagEnd - hashtagStart - 1)).hasMatch()) {
				hashtagIgnore = true;
			}
		}
		while (mMention.hasMatch()) {
			if (!mMention.capturedRef(1).isEmpty()) {
				++mentionStart;
			}
			if (!mMention.capturedRef(2).isEmpty()) {
				--mentionEnd;
			}
			if (!(start + mentionStart + 1)->isLetter() || !(start + mentionEnd - 1)->isLetterOrNumber()) {
				mentionSkip = mentionEnd;
				mMention = RegExpMention().match(result.text, qMax(mentionSkip, matchOffset));
				if (mMention.hasMatch()) {
					mentionStart = mMention.capturedStart();
					mentionEnd = mMention.capturedEnd();
				} else {
					mentionIgnore = true;
				}
			} else {
				break;
			}
		}
		if (mBotCommand.hasMatch()) {
			if (!mBotCommand.capturedRef(1).isEmpty()) {
				++botCommandStart;
			}
			if (!mBotCommand.capturedRef(3).isEmpty()) {
				--botCommandEnd;
			}
		}
		if (!mDomain.hasMatch()
			&& !mExplicitDomain.hasMatch()
			&& !mHashtag.hasMatch()
			&& !mMention.hasMatch()
			&& !mBotCommand.hasMatch()) {
			break;
		}

		if (explicitDomainStart < domainStart) {
			domainStart = explicitDomainStart;
			domainEnd = explicitDomainEnd;
			mDomain = mExplicitDomain;
		}
		if (mentionStart < hashtagStart
			&& mentionStart < domainStart
			&& mentionStart < botCommandStart) {
			if (mentionIgnore) {
				offset = matchOffset = mentionEnd;
				continue;
			}
			const auto inCommand = checkTagStartInCommand(
				start,
				len,
				mentionStart,
				commandOffset,
				commandIsLink,
				inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			lnkType = EntityType::Mention;
			lnkStart = mentionStart;
			lnkLength = mentionEnd - mentionStart;
		} else if (hashtagStart < domainStart
			&& hashtagStart < botCommandStart) {
			if (hashtagIgnore) {
				offset = matchOffset = hashtagEnd;
				continue;
			}
			const auto inCommand = checkTagStartInCommand(
				start,
				len,
				hashtagStart,
				commandOffset,
				commandIsLink,
				inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			lnkType = EntityType::Hashtag;
			lnkStart = hashtagStart;
			lnkLength = hashtagEnd - hashtagStart;
		} else if (botCommandStart < domainStart) {
			const auto inCommand = checkTagStartInCommand(
				start,
				len,
				botCommandStart,
				commandOffset,
				commandIsLink,
				inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			lnkType = EntityType::BotCommand;
			lnkStart = botCommandStart;
			lnkLength = botCommandEnd - botCommandStart;
		} else {
			const auto inCommand = checkTagStartInCommand(
				start,
				len,
				domainStart,
				commandOffset,
				commandIsLink,
				inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			auto protocol = mDomain.captured(1).toLower();
			auto topDomain = mDomain.captured(3).toLower();
			auto isProtocolValid = protocol.isEmpty() || IsValidProtocol(protocol);
			auto isTopDomainValid = !protocol.isEmpty() || IsValidTopDomain(topDomain);

			if (protocol.isEmpty() && domainStart > offset + 1 && *(start + domainStart - 1) == QChar('@')) {
				auto forMailName = result.text.mid(offset, domainStart - offset - 1);
				auto mMailName = RegExpMailNameAtEnd().match(forMailName);
				if (mMailName.hasMatch()) {
					auto mailStart = offset + mMailName.capturedStart();
					if (mailStart < offset) {
						mailStart = offset;
					}
					lnkType = EntityType::Email;
					lnkStart = mailStart;
					lnkLength = domainEnd - mailStart;
				}
			}
			if (lnkType == EntityType::Url && !lnkLength) {
				if (!isProtocolValid || !isTopDomainValid) {
					matchOffset = domainEnd;
					continue;
				}
				lnkStart = domainStart;

				QStack<const QChar*> parenth;
				const QChar *domainEnd = start + mDomain.capturedEnd(), *p = domainEnd;
				for (; p < end; ++p) {
					QChar ch(*p);
					if (chIsLinkEnd(ch)) break; // link finished
					if (chIsAlmostLinkEnd(ch)) {
						const QChar *endTest = p + 1;
						while (endTest < end && chIsAlmostLinkEnd(*endTest)) {
							++endTest;
						}
						if (endTest >= end || chIsLinkEnd(*endTest)) {
							break; // link finished at p
						}
						p = endTest;
						ch = *p;
					}
					if (ch == '(' || ch == '[' || ch == '{' || ch == '<') {
						parenth.push(p);
					} else if (ch == ')' || ch == ']' || ch == '}' || ch == '>') {
						if (parenth.isEmpty()) break;
						const QChar *q = parenth.pop(), open(*q);
						if ((ch == ')' && open != '(') || (ch == ']' && open != '[') || (ch == '}' && open != '{') || (ch == '>' && open != '<')) {
							p = q;
							break;
						}
					}
				}
				if (p > domainEnd) { // check, that domain ended
					if (domainEnd->unicode() != '/' && domainEnd->unicode() != '?') {
						matchOffset = domainEnd - start;
						continue;
					}
				}
				lnkLength = (p - start) - lnkStart;
			}
		}
		for (; existingEntityIndex < existingEntitiesCount && result.entities[existingEntityIndex].offset() <= lnkStart; ++existingEntityIndex) {
			auto &entity = result.entities[existingEntityIndex];
			accumulate_max(existingEntityEnd, entity.offset() + entity.length());
			newEntities.push_back(entity);
		}
		if (lnkStart >= existingEntityEnd) {
			result.entities.push_back({ lnkType, lnkStart, lnkLength });
		}

		offset = matchOffset = lnkStart + lnkLength;
	}
	if (!newEntities.isEmpty()) {
		for (; existingEntityIndex < existingEntitiesCount; ++existingEntityIndex) {
			auto &entity = result.entities[existingEntityIndex];
			newEntities.push_back(entity);
		}
		result.entities = newEntities;
	}
}

void MoveStringPart(TextWithEntities &result, int to, int from, int count) {
	if (!count) return;
	if (to != from) {
		auto start = result.text.data();
		memmove(start + to, start + from, count * sizeof(QChar));

		for (auto &entity : result.entities) {
			if (entity.offset() >= from + count) break;
			if (entity.offset() + entity.length() <= from) continue;
			if (entity.offset() >= from) {
				entity.extendToLeft(from - to);
			}
			if (entity.offset() + entity.length() <= from + count) {
				entity.shrinkFromRight(from - to);
			}
		}
	}
}

void MovePartAndGoForward(TextWithEntities &result, int &to, int &from, int count) {
	if (!count) return;
	MoveStringPart(result, to, from, count);
	to += count;
	from += count;
}

void PrepareForSending(TextWithEntities &result, int32 flags) {
	ApplyServerCleaning(result);

	if (flags) {
		ParseEntities(result, flags);
	}

	Trim(result);
}

// Replace bad symbols with space and remove '\r'.
void ApplyServerCleaning(TextWithEntities &result) {
	auto len = result.text.size();

	// Replace tabs with two spaces.
	if (auto tabs = std::count(result.text.cbegin(), result.text.cend(), '\t')) {
		auto replacement = QString::fromLatin1("  ");
		auto replacementLength = replacement.size();
		auto shift = (replacementLength - 1);
		result.text.resize(len + shift * tabs);
		for (auto i = len, movedTill = len, to = result.text.size(); i > 0; --i) {
			if (result.text[i - 1] == '\t') {
				auto toMove = movedTill - i;
				to -= toMove;
				MoveStringPart(result, to, i, toMove);
				to -= replacementLength;
				memcpy(result.text.data() + to, replacement.constData(), replacementLength * sizeof(QChar));
				movedTill = i - 1;
			}
		}
		len = result.text.size();
	}

	auto to = 0;
	auto from = 0;
	auto start = result.text.data();
	for (auto ch = start, end = start + len; ch < end; ++ch) {
		if (ch->unicode() == '\r') {
			MovePartAndGoForward(result, to, from, (ch - start) - from);
			++from;
		} else if (chReplacedBySpace(*ch)) {
			*ch = ' ';
		}
	}
	MovePartAndGoForward(result, to, from, len - from);
	if (to < len) result.text.resize(to);
}

void Trim(TextWithEntities &result) {
	auto foundNotTrimmedChar = false;

	// right trim
	for (auto s = result.text.data(), e = s + result.text.size(), ch = e; ch != s;) {
		--ch;
		if (!chIsTrimmed(*ch)) {
			if (ch + 1 < e) {
				auto l = ch + 1 - s;
				for (auto &entity : result.entities) {
					entity.updateTextEnd(l);
				}
				result.text.resize(l);
			}
			foundNotTrimmedChar = true;
			break;
		}
	}
	if (!foundNotTrimmedChar) {
		result = TextWithEntities();
		return;
	}

	const auto firstMonospaceOffset = EntityInText::FirstMonospaceOffset(
		result.entities,
		result.text.size());

	// left trim
	for (auto s = result.text.data(), ch = s, e = s + result.text.size(); ch != e; ++ch) {
		if (!chIsTrimmed(*ch) || (ch - s) == firstMonospaceOffset) {
			if (ch > s) {
				auto l = ch - s;
				for (auto &entity : result.entities) {
					entity.shiftLeft(l);
				}
				result.text = result.text.mid(l);
			}
			break;
		}
	}
}

QByteArray SerializeTags(const TextWithTags::Tags &tags) {
	if (tags.isEmpty()) {
		return QByteArray();
	}

	QByteArray tagsSerialized;
	{
		QDataStream stream(&tagsSerialized, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(tags.size());
		for (const auto &tag : tags) {
			stream << qint32(tag.offset) << qint32(tag.length) << tag.id;
		}
	}
	return tagsSerialized;
}

TextWithTags::Tags DeserializeTags(QByteArray data, int textLength) {
	auto result = TextWithTags::Tags();
	if (data.isEmpty()) {
		return result;
	}

	QDataStream stream(data);
	stream.setVersion(QDataStream::Qt_5_1);

	qint32 tagCount = 0;
	stream >> tagCount;
	if (stream.status() != QDataStream::Ok) {
		return result;
	}
	if (tagCount <= 0 || tagCount > textLength) {
		return result;
	}

	for (auto i = 0; i != tagCount; ++i) {
		qint32 offset = 0, length = 0;
		QString id;
		stream >> offset >> length >> id;
		if (stream.status() != QDataStream::Ok) {
			return result;
		}
		if (offset < 0 || length <= 0 || offset + length > textLength) {
			return result;
		}
		result.push_back({ offset, length, id });
	}
	return result;
}

QString TagsMimeType() {
	return QString::fromLatin1("application/x-td-field-tags");
}

QString TagsTextMimeType() {
	return QString::fromLatin1("application/x-td-field-text");
}

bool IsMentionLink(const QString &link) {
	return link.startsWith(kMentionTagStart);
}

EntitiesInText ConvertTextTagsToEntities(const TextWithTags::Tags &tags) {
	EntitiesInText result;
	if (tags.isEmpty()) {
		return result;
	}

	result.reserve(tags.size());
	for (const auto &tag : tags) {
		const auto push = [&](
				EntityType type,
				const QString &data = QString()) {
			result.push_back(
				EntityInText(type, tag.offset, tag.length, data));
		};
		if (IsMentionLink(tag.id)) {
			if (auto match = qthelp::regex_match("^(\\d+\\.\\d+)(/|$)", tag.id.midRef(kMentionTagStart.size()))) {
				push(EntityType::MentionName, match->captured(1));
			}
		} else if (tag.id == Ui::InputField::kTagBold) {
			push(EntityType::Bold);
		} else if (tag.id == Ui::InputField::kTagItalic) {
			push(EntityType::Italic);
		} else if (tag.id == Ui::InputField::kTagUnderline) {
			push(EntityType::Underline);
		} else if (tag.id == Ui::InputField::kTagStrikeOut) {
			push(EntityType::StrikeOut);
		} else if (tag.id == Ui::InputField::kTagCode) {
			push(EntityType::Code);
		} else if (tag.id == Ui::InputField::kTagPre) { // #TODO entities
			push(EntityType::Pre);
		} else /*if (ValidateUrl(tag.id)) */{ // We validate when we insert.
			push(EntityType::CustomUrl, tag.id);
		}
	}
	return result;
}

TextWithTags::Tags ConvertEntitiesToTextTags(const EntitiesInText &entities) {
	TextWithTags::Tags result;
	if (entities.isEmpty()) {
		return result;
	}

	result.reserve(entities.size());
	for (const auto &entity : entities) {
		const auto push = [&](const QString &tag) {
			result.push_back({ entity.offset(), entity.length(), tag });
		};
		switch (entity.type()) {
		case EntityType::MentionName: {
			auto match = QRegularExpression(R"(^(\d+\.\d+)$)").match(entity.data());
			if (match.hasMatch()) {
				push(kMentionTagStart + entity.data());
			}
		} break;
		case EntityType::CustomUrl: {
			const auto url = entity.data();
			if (Ui::InputField::IsValidMarkdownLink(url)
				&& !IsMentionLink(url)) {
				push(url);
			}
		} break;
		case EntityType::Bold: push(Ui::InputField::kTagBold); break;
		case EntityType::Italic: push(Ui::InputField::kTagItalic); break;
		case EntityType::Underline:
			push(Ui::InputField::kTagUnderline);
			break;
		case EntityType::StrikeOut:
			push(Ui::InputField::kTagStrikeOut);
			break;
		case EntityType::Code: push(Ui::InputField::kTagCode); break; // #TODO entities
		case EntityType::Pre: push(Ui::InputField::kTagPre); break;
		}
	}
	return result;
}

std::unique_ptr<QMimeData> MimeDataFromText(const TextForMimeData &text) {
	return MimeDataFromText(
		{ text.rich.text, ConvertEntitiesToTextTags(text.rich.entities) },
		text.expanded);
}

std::unique_ptr<QMimeData> MimeDataFromText(TextWithTags &&text) {
	const auto expanded = ExpandCustomLinks(text);
	return MimeDataFromText(std::move(text), expanded);
}

void SetClipboardText(
		const TextForMimeData &text,
		QClipboard::Mode mode) {
	if (auto data = MimeDataFromText(text)) {
		QGuiApplication::clipboard()->setMimeData(data.release(), mode);
	}
}

} // namespace TextUtilities

EntityInText::EntityInText(
	EntityType type,
	int offset,
	int length,
	const QString &data)
: _type(type)
, _offset(offset)
, _length(length)
, _data(data) {
}

int EntityInText::FirstMonospaceOffset(
		const EntitiesInText &entities,
		int textLength) {
	auto &&monospace = ranges::make_subrange(
		entities.begin(),
		entities.end()
	) | ranges::view::filter([](const EntityInText & entity) {
		return (entity.type() == EntityType::Pre)
			|| (entity.type() == EntityType::Code);
	});
	const auto i = ranges::max_element(
		monospace,
		std::greater<>(),
		&EntityInText::offset);
	return (i == monospace.end()) ? textLength : i->offset();
}
