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

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "ui/text/text_entity.h"

#include "auth_session.h"

namespace {

const QRegularExpression _reDomain(QString::fromUtf8("(?<![\\w\\$\\-\\_%=\\.])(?:([a-zA-Z]+)://)?((?:[A-Za-z" "\xd0\x90-\xd0\xaf" "\xd0\xb0-\xd1\x8f" "\xd1\x91\xd0\x81" "0-9\\-\\_]+\\.){1,10}([A-Za-z" "\xd1\x80\xd1\x84" "\\-\\d]{2,22})(\\:\\d+)?)"), QRegularExpression::UseUnicodePropertiesOption);
const QRegularExpression _reExplicitDomain(QString::fromUtf8("(?<![\\w\\$\\-\\_%=\\.])(?:([a-zA-Z]+)://)((?:[A-Za-z" "\xd0\x90-\xd0\xaf\xd0\x81" "\xd0\xb0-\xd1\x8f\xd1\x91" "0-9\\-\\_]+\\.){0,5}([A-Za-z" "\xd1\x80\xd1\x84" "\\-\\d]{2,22})(\\:\\d+)?)"), QRegularExpression::UseUnicodePropertiesOption);
const QRegularExpression _reMailName(qsl("[a-zA-Z\\-_\\.0-9]{1,256}$"));
const QRegularExpression _reMailStart(qsl("^[a-zA-Z\\-_\\.0-9]{1,256}\\@"));
const QRegularExpression _reHashtag(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\%\\^\\*\\(\\)\\-\\+=\\x10])#[\\w]{2,64}([\\W]|$)"), QRegularExpression::UseUnicodePropertiesOption);
const QRegularExpression _reMention(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\%\\^\\*\\(\\)\\-\\+=\\x10])@[A-Za-z_0-9]{1,32}([\\W]|$)"), QRegularExpression::UseUnicodePropertiesOption);
const QRegularExpression _reBotCommand(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\%\\^\\*\\(\\)\\-\\+=\\x10])/[A-Za-z_0-9]{1,64}(@[A-Za-z_0-9]{5,32})?([\\W]|$)"));
const QRegularExpression _rePre(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\?\\%\\^\\*\\(\\)\\-\\+=\\x10])(````?)[\\s\\S]+?(````?)([\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\?\\%\\^\\*\\(\\)\\-\\+=\\x10]|$)"), QRegularExpression::UseUnicodePropertiesOption);
const QRegularExpression _reCode(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\?\\%\\^\\*\\(\\)\\-\\+=\\x10])(`)[^\\n]+?(`)([\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\?\\%\\^\\*\\(\\)\\-\\+=\\x10]|$)"), QRegularExpression::UseUnicodePropertiesOption);
QSet<int32> _validProtocols, _validTopDomains;

} // namespace

const QRegularExpression &reDomain() {
	return _reDomain;
}

const QRegularExpression &reMailName() {
	return _reMailName;
}

const QRegularExpression &reMailStart() {
	return _reMailStart;
}

const QRegularExpression &reHashtag() {
	return _reHashtag;
}

const QRegularExpression &reBotCommand() {
	return _reBotCommand;
}

namespace {

void regOneProtocol(const QString &protocol) {
	_validProtocols.insert(hashCrc32(protocol.constData(), protocol.size() * sizeof(QChar)));
}

void regOneTopDomain(const QString &domain) {
	_validTopDomains.insert(hashCrc32(domain.constData(), domain.size() * sizeof(QChar)));
}

} // namespace

const QSet<int32> &validProtocols() {
	return _validProtocols;
}
const QSet<int32> &validTopDomains() {
	return _validTopDomains;
}

void initLinkSets() {
	if (!_validProtocols.isEmpty() || !_validTopDomains.isEmpty()) return;

	regOneProtocol(qsl("itmss")); // itunes
	regOneProtocol(qsl("http"));
	regOneProtocol(qsl("https"));
	regOneProtocol(qsl("ftp"));
	regOneProtocol(qsl("tg")); // local urls

	regOneTopDomain(qsl("ac"));
	regOneTopDomain(qsl("ad"));
	regOneTopDomain(qsl("ae"));
	regOneTopDomain(qsl("af"));
	regOneTopDomain(qsl("ag"));
	regOneTopDomain(qsl("ai"));
	regOneTopDomain(qsl("al"));
	regOneTopDomain(qsl("am"));
	regOneTopDomain(qsl("an"));
	regOneTopDomain(qsl("ao"));
	regOneTopDomain(qsl("aq"));
	regOneTopDomain(qsl("ar"));
	regOneTopDomain(qsl("as"));
	regOneTopDomain(qsl("at"));
	regOneTopDomain(qsl("au"));
	regOneTopDomain(qsl("aw"));
	regOneTopDomain(qsl("ax"));
	regOneTopDomain(qsl("az"));
	regOneTopDomain(qsl("ba"));
	regOneTopDomain(qsl("bb"));
	regOneTopDomain(qsl("bd"));
	regOneTopDomain(qsl("be"));
	regOneTopDomain(qsl("bf"));
	regOneTopDomain(qsl("bg"));
	regOneTopDomain(qsl("bh"));
	regOneTopDomain(qsl("bi"));
	regOneTopDomain(qsl("bj"));
	regOneTopDomain(qsl("bm"));
	regOneTopDomain(qsl("bn"));
	regOneTopDomain(qsl("bo"));
	regOneTopDomain(qsl("br"));
	regOneTopDomain(qsl("bs"));
	regOneTopDomain(qsl("bt"));
	regOneTopDomain(qsl("bv"));
	regOneTopDomain(qsl("bw"));
	regOneTopDomain(qsl("by"));
	regOneTopDomain(qsl("bz"));
	regOneTopDomain(qsl("ca"));
	regOneTopDomain(qsl("cc"));
	regOneTopDomain(qsl("cd"));
	regOneTopDomain(qsl("cf"));
	regOneTopDomain(qsl("cg"));
	regOneTopDomain(qsl("ch"));
	regOneTopDomain(qsl("ci"));
	regOneTopDomain(qsl("ck"));
	regOneTopDomain(qsl("cl"));
	regOneTopDomain(qsl("cm"));
	regOneTopDomain(qsl("cn"));
	regOneTopDomain(qsl("co"));
	regOneTopDomain(qsl("cr"));
	regOneTopDomain(qsl("cu"));
	regOneTopDomain(qsl("cv"));
	regOneTopDomain(qsl("cx"));
	regOneTopDomain(qsl("cy"));
	regOneTopDomain(qsl("cz"));
	regOneTopDomain(qsl("de"));
	regOneTopDomain(qsl("dj"));
	regOneTopDomain(qsl("dk"));
	regOneTopDomain(qsl("dm"));
	regOneTopDomain(qsl("do"));
	regOneTopDomain(qsl("dz"));
	regOneTopDomain(qsl("ec"));
	regOneTopDomain(qsl("ee"));
	regOneTopDomain(qsl("eg"));
	regOneTopDomain(qsl("eh"));
	regOneTopDomain(qsl("er"));
	regOneTopDomain(qsl("es"));
	regOneTopDomain(qsl("et"));
	regOneTopDomain(qsl("eu"));
	regOneTopDomain(qsl("fi"));
	regOneTopDomain(qsl("fj"));
	regOneTopDomain(qsl("fk"));
	regOneTopDomain(qsl("fm"));
	regOneTopDomain(qsl("fo"));
	regOneTopDomain(qsl("fr"));
	regOneTopDomain(qsl("ga"));
	regOneTopDomain(qsl("gd"));
	regOneTopDomain(qsl("ge"));
	regOneTopDomain(qsl("gf"));
	regOneTopDomain(qsl("gg"));
	regOneTopDomain(qsl("gh"));
	regOneTopDomain(qsl("gi"));
	regOneTopDomain(qsl("gl"));
	regOneTopDomain(qsl("gm"));
	regOneTopDomain(qsl("gn"));
	regOneTopDomain(qsl("gp"));
	regOneTopDomain(qsl("gq"));
	regOneTopDomain(qsl("gr"));
	regOneTopDomain(qsl("gs"));
	regOneTopDomain(qsl("gt"));
	regOneTopDomain(qsl("gu"));
	regOneTopDomain(qsl("gw"));
	regOneTopDomain(qsl("gy"));
	regOneTopDomain(qsl("hk"));
	regOneTopDomain(qsl("hm"));
	regOneTopDomain(qsl("hn"));
	regOneTopDomain(qsl("hr"));
	regOneTopDomain(qsl("ht"));
	regOneTopDomain(qsl("hu"));
	regOneTopDomain(qsl("id"));
	regOneTopDomain(qsl("ie"));
	regOneTopDomain(qsl("il"));
	regOneTopDomain(qsl("im"));
	regOneTopDomain(qsl("in"));
	regOneTopDomain(qsl("io"));
	regOneTopDomain(qsl("iq"));
	regOneTopDomain(qsl("ir"));
	regOneTopDomain(qsl("is"));
	regOneTopDomain(qsl("it"));
	regOneTopDomain(qsl("je"));
	regOneTopDomain(qsl("jm"));
	regOneTopDomain(qsl("jo"));
	regOneTopDomain(qsl("jp"));
	regOneTopDomain(qsl("ke"));
	regOneTopDomain(qsl("kg"));
	regOneTopDomain(qsl("kh"));
	regOneTopDomain(qsl("ki"));
	regOneTopDomain(qsl("km"));
	regOneTopDomain(qsl("kn"));
	regOneTopDomain(qsl("kp"));
	regOneTopDomain(qsl("kr"));
	regOneTopDomain(qsl("kw"));
	regOneTopDomain(qsl("ky"));
	regOneTopDomain(qsl("kz"));
	regOneTopDomain(qsl("la"));
	regOneTopDomain(qsl("lb"));
	regOneTopDomain(qsl("lc"));
	regOneTopDomain(qsl("li"));
	regOneTopDomain(qsl("lk"));
	regOneTopDomain(qsl("lr"));
	regOneTopDomain(qsl("ls"));
	regOneTopDomain(qsl("lt"));
	regOneTopDomain(qsl("lu"));
	regOneTopDomain(qsl("lv"));
	regOneTopDomain(qsl("ly"));
	regOneTopDomain(qsl("ma"));
	regOneTopDomain(qsl("mc"));
	regOneTopDomain(qsl("md"));
	regOneTopDomain(qsl("me"));
	regOneTopDomain(qsl("mg"));
	regOneTopDomain(qsl("mh"));
	regOneTopDomain(qsl("mk"));
	regOneTopDomain(qsl("ml"));
	regOneTopDomain(qsl("mm"));
	regOneTopDomain(qsl("mn"));
	regOneTopDomain(qsl("mo"));
	regOneTopDomain(qsl("mp"));
	regOneTopDomain(qsl("mq"));
	regOneTopDomain(qsl("mr"));
	regOneTopDomain(qsl("ms"));
	regOneTopDomain(qsl("mt"));
	regOneTopDomain(qsl("mu"));
	regOneTopDomain(qsl("mv"));
	regOneTopDomain(qsl("mw"));
	regOneTopDomain(qsl("mx"));
	regOneTopDomain(qsl("my"));
	regOneTopDomain(qsl("mz"));
	regOneTopDomain(qsl("na"));
	regOneTopDomain(qsl("nc"));
	regOneTopDomain(qsl("ne"));
	regOneTopDomain(qsl("nf"));
	regOneTopDomain(qsl("ng"));
	regOneTopDomain(qsl("ni"));
	regOneTopDomain(qsl("nl"));
	regOneTopDomain(qsl("no"));
	regOneTopDomain(qsl("np"));
	regOneTopDomain(qsl("nr"));
	regOneTopDomain(qsl("nu"));
	regOneTopDomain(qsl("nz"));
	regOneTopDomain(qsl("om"));
	regOneTopDomain(qsl("pa"));
	regOneTopDomain(qsl("pe"));
	regOneTopDomain(qsl("pf"));
	regOneTopDomain(qsl("pg"));
	regOneTopDomain(qsl("ph"));
	regOneTopDomain(qsl("pk"));
	regOneTopDomain(qsl("pl"));
	regOneTopDomain(qsl("pm"));
	regOneTopDomain(qsl("pn"));
	regOneTopDomain(qsl("pr"));
	regOneTopDomain(qsl("ps"));
	regOneTopDomain(qsl("pt"));
	regOneTopDomain(qsl("pw"));
	regOneTopDomain(qsl("py"));
	regOneTopDomain(qsl("qa"));
	regOneTopDomain(qsl("re"));
	regOneTopDomain(qsl("ro"));
	regOneTopDomain(qsl("ru"));
	regOneTopDomain(qsl("rs"));
	regOneTopDomain(qsl("rw"));
	regOneTopDomain(qsl("sa"));
	regOneTopDomain(qsl("sb"));
	regOneTopDomain(qsl("sc"));
	regOneTopDomain(qsl("sd"));
	regOneTopDomain(qsl("se"));
	regOneTopDomain(qsl("sg"));
	regOneTopDomain(qsl("sh"));
	regOneTopDomain(qsl("si"));
	regOneTopDomain(qsl("sj"));
	regOneTopDomain(qsl("sk"));
	regOneTopDomain(qsl("sl"));
	regOneTopDomain(qsl("sm"));
	regOneTopDomain(qsl("sn"));
	regOneTopDomain(qsl("so"));
	regOneTopDomain(qsl("sr"));
	regOneTopDomain(qsl("ss"));
	regOneTopDomain(qsl("st"));
	regOneTopDomain(qsl("su"));
	regOneTopDomain(qsl("sv"));
	regOneTopDomain(qsl("sx"));
	regOneTopDomain(qsl("sy"));
	regOneTopDomain(qsl("sz"));
	regOneTopDomain(qsl("tc"));
	regOneTopDomain(qsl("td"));
	regOneTopDomain(qsl("tf"));
	regOneTopDomain(qsl("tg"));
	regOneTopDomain(qsl("th"));
	regOneTopDomain(qsl("tj"));
	regOneTopDomain(qsl("tk"));
	regOneTopDomain(qsl("tl"));
	regOneTopDomain(qsl("tm"));
	regOneTopDomain(qsl("tn"));
	regOneTopDomain(qsl("to"));
	regOneTopDomain(qsl("tp"));
	regOneTopDomain(qsl("tr"));
	regOneTopDomain(qsl("tt"));
	regOneTopDomain(qsl("tv"));
	regOneTopDomain(qsl("tw"));
	regOneTopDomain(qsl("tz"));
	regOneTopDomain(qsl("ua"));
	regOneTopDomain(qsl("ug"));
	regOneTopDomain(qsl("uk"));
	regOneTopDomain(qsl("um"));
	regOneTopDomain(qsl("us"));
	regOneTopDomain(qsl("uy"));
	regOneTopDomain(qsl("uz"));
	regOneTopDomain(qsl("va"));
	regOneTopDomain(qsl("vc"));
	regOneTopDomain(qsl("ve"));
	regOneTopDomain(qsl("vg"));
	regOneTopDomain(qsl("vi"));
	regOneTopDomain(qsl("vn"));
	regOneTopDomain(qsl("vu"));
	regOneTopDomain(qsl("wf"));
	regOneTopDomain(qsl("ws"));
	regOneTopDomain(qsl("ye"));
	regOneTopDomain(qsl("yt"));
	regOneTopDomain(qsl("yu"));
	regOneTopDomain(qsl("za"));
	regOneTopDomain(qsl("zm"));
	regOneTopDomain(qsl("zw"));
	regOneTopDomain(qsl("arpa"));
	regOneTopDomain(qsl("aero"));
	regOneTopDomain(qsl("asia"));
	regOneTopDomain(qsl("biz"));
	regOneTopDomain(qsl("cat"));
	regOneTopDomain(qsl("com"));
	regOneTopDomain(qsl("coop"));
	regOneTopDomain(qsl("info"));
	regOneTopDomain(qsl("int"));
	regOneTopDomain(qsl("jobs"));
	regOneTopDomain(qsl("mobi"));
	regOneTopDomain(qsl("museum"));
	regOneTopDomain(qsl("name"));
	regOneTopDomain(qsl("net"));
	regOneTopDomain(qsl("org"));
	regOneTopDomain(qsl("post"));
	regOneTopDomain(qsl("pro"));
	regOneTopDomain(qsl("tel"));
	regOneTopDomain(qsl("travel"));
	regOneTopDomain(qsl("xxx"));
	regOneTopDomain(qsl("edu"));
	regOneTopDomain(qsl("gov"));
	regOneTopDomain(qsl("mil"));
	regOneTopDomain(qsl("local"));
	regOneTopDomain(qsl("xn--lgbbat1ad8j"));
	regOneTopDomain(qsl("xn--54b7fta0cc"));
	regOneTopDomain(qsl("xn--fiqs8s"));
	regOneTopDomain(qsl("xn--fiqz9s"));
	regOneTopDomain(qsl("xn--wgbh1c"));
	regOneTopDomain(qsl("xn--node"));
	regOneTopDomain(qsl("xn--j6w193g"));
	regOneTopDomain(qsl("xn--h2brj9c"));
	regOneTopDomain(qsl("xn--mgbbh1a71e"));
	regOneTopDomain(qsl("xn--fpcrj9c3d"));
	regOneTopDomain(qsl("xn--gecrj9c"));
	regOneTopDomain(qsl("xn--s9brj9c"));
	regOneTopDomain(qsl("xn--xkc2dl3a5ee0h"));
	regOneTopDomain(qsl("xn--45brj9c"));
	regOneTopDomain(qsl("xn--mgba3a4f16a"));
	regOneTopDomain(qsl("xn--mgbayh7gpa"));
	regOneTopDomain(qsl("xn--80ao21a"));
	regOneTopDomain(qsl("xn--mgbx4cd0ab"));
	regOneTopDomain(qsl("xn--l1acc"));
	regOneTopDomain(qsl("xn--mgbc0a9azcg"));
	regOneTopDomain(qsl("xn--mgb9awbf"));
	regOneTopDomain(qsl("xn--mgbai9azgqp6j"));
	regOneTopDomain(qsl("xn--ygbi2ammx"));
	regOneTopDomain(qsl("xn--wgbl6a"));
	regOneTopDomain(qsl("xn--p1ai"));
	regOneTopDomain(qsl("xn--mgberp4a5d4ar"));
	regOneTopDomain(qsl("xn--90a3ac"));
	regOneTopDomain(qsl("xn--yfro4i67o"));
	regOneTopDomain(qsl("xn--clchc0ea0b2g2a9gcd"));
	regOneTopDomain(qsl("xn--3e0b707e"));
	regOneTopDomain(qsl("xn--fzc2c9e2c"));
	regOneTopDomain(qsl("xn--xkc2al3hye2a"));
	regOneTopDomain(qsl("xn--mgbtf8fl"));
	regOneTopDomain(qsl("xn--kprw13d"));
	regOneTopDomain(qsl("xn--kpry57d"));
	regOneTopDomain(qsl("xn--o3cw4h"));
	regOneTopDomain(qsl("xn--pgbs0dh"));
	regOneTopDomain(qsl("xn--j1amh"));
	regOneTopDomain(qsl("xn--mgbaam7a8h"));
	regOneTopDomain(qsl("xn--mgb2ddes"));
	regOneTopDomain(qsl("xn--ogbpf8fl"));
	regOneTopDomain(QString::fromUtf8("\xd1\x80\xd1\x84"));
}

namespace {
// accent char list taken from https://github.com/aristus/accent-folding
inline QChar chNoAccent(int32 code) {
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
}

QString textClean(const QString &text) {
	QString result(text);
	for (const QChar *s = text.unicode(), *ch = s, *e = text.unicode() + text.size(); ch != e; ++ch) {
		if (*ch == TextCommand) {
			result[int(ch - s)] = QChar::Space;
		}
	}
	return result;
}

QString textRichPrepare(const QString &text) {
	QString result;
	result.reserve(text.size());
	const QChar *s = text.constData(), *ch = s;
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

QString textOneLine(const QString &text, bool trim, bool rich) {
	QString result(text);
	const QChar *s = text.unicode(), *ch = s, *e = text.unicode() + text.size();
	if (trim) {
		while (s < e && chIsTrimmed(*s)) {
			++s;
		}
		while (s < e && chIsTrimmed(*(e - 1))) {
			--e;
		}
		if (e - s != text.size()) {
			result = text.mid(s - text.unicode(), e - s);
		}
	}
	for (const QChar *ch = s; ch != e; ++ch) {
		if (chIsNewline(*ch)) {
			result[int(ch - s)] = QChar::Space;
		}
	}
	return result;
}

QString textAccentFold(const QString &text) {
	QString result(text);
	bool copying = false;
	int32 i = 0;
	for (const QChar *s = text.unicode(), *ch = s, *e = text.unicode() + text.size(); ch != e; ++ch, ++i) {
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
			QChar noAccent = chNoAccent(QChar::surrogateToUcs4(*ch, *(ch + 1)));
			if (noAccent.unicode() > 0) {
				copying = true;
				result[i] = noAccent;
			} else {
				if (copying) result[i] = *ch;
				++ch, ++i;
				if (copying) result[i] = *ch;
			}
		} else {
			QChar noAccent = chNoAccent(ch->unicode());
			if (noAccent.unicode() > 0 && noAccent != *ch) {
				result[i] = noAccent;
			} else if (copying) {
				result[i] = *ch;
			}
		}
	}
	return (i < result.size()) ? result.mid(0, i) : result;
}

QString textSearchKey(const QString &text) {
	return textAccentFold(text.trimmed().toLower());
}

bool textSplit(QString &sendingText, EntitiesInText &sendingEntities, QString &leftText, EntitiesInText &leftEntities, int32 limit) {
	if (leftText.isEmpty() || !limit) return false;

	int32 currentEntity = 0, goodEntity = currentEntity, entityCount = leftEntities.size();
	bool goodInEntity = false, goodCanBreakEntity = false;

	int32 s = 0, half = limit / 2, goodLevel = 0;
	for (const QChar *start = leftText.constData(), *ch = start, *end = leftText.constEnd(), *good = ch; ch != end; ++ch, ++s) {
		while (currentEntity < entityCount && ch >= start + leftEntities.at(currentEntity).offset() + leftEntities.at(currentEntity).length()) {
			++currentEntity;
		}

		if (s > half) {
			bool inEntity = (currentEntity < entityCount) && (ch > start + leftEntities.at(currentEntity).offset()) && (ch < start + leftEntities.at(currentEntity).offset() + leftEntities.at(currentEntity).length());
			EntityInTextType entityType = (currentEntity < entityCount) ? leftEntities.at(currentEntity).type() : EntityInTextInvalid;
			bool canBreakEntity = (entityType == EntityInTextPre || entityType == EntityInTextCode);
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
					} else if (currentEntity < entityCount && ch + 1 == start + leftEntities.at(currentEntity).offset() && leftEntities.at(currentEntity).type() == EntityInTextPre) {
						markGoodAsLevel(14);
					} else if (currentEntity > 0 && ch == start + leftEntities.at(currentEntity - 1).offset() + leftEntities.at(currentEntity - 1).length() && leftEntities.at(currentEntity - 1).type() == EntityInTextPre) {
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
			sendingText = leftText.mid(0, good - start);
			leftText = leftText.mid(good - start);
			if (goodInEntity) {
				if (goodCanBreakEntity) {
					sendingEntities = leftEntities.mid(0, goodEntity + 1);
					sendingEntities.back().updateTextEnd(good - start);
					leftEntities = leftEntities.mid(goodEntity);
					for (auto &entity : leftEntities) {
						entity.shiftLeft(good - start);
					}
				} else {
					sendingEntities = leftEntities.mid(0, goodEntity);
					leftEntities = leftEntities.mid(goodEntity + 1);
				}
			} else {
				sendingEntities = leftEntities.mid(0, goodEntity);
				leftEntities = leftEntities.mid(goodEntity);
				for (auto &entity : leftEntities) {
					entity.shiftLeft(good - start);
				}
			}
			return true;
		}
	}
	sendingText = leftText;
	leftText = QString();
	sendingEntities = leftEntities;
	leftEntities = EntitiesInText();
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

EntitiesInText entitiesFromMTP(const QVector<MTPMessageEntity> &entities) {
	EntitiesInText result;
	if (!entities.isEmpty()) {
		result.reserve(entities.size());
		for_const (const auto &entity, entities) {
			switch (entity.type()) {
			case mtpc_messageEntityUrl: { const auto &d(entity.c_messageEntityUrl()); result.push_back(EntityInText(EntityInTextUrl, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityTextUrl: { const auto &d(entity.c_messageEntityTextUrl()); result.push_back(EntityInText(EntityInTextCustomUrl, d.voffset.v, d.vlength.v, textClean(qs(d.vurl)))); } break;
			case mtpc_messageEntityEmail: { const auto &d(entity.c_messageEntityEmail()); result.push_back(EntityInText(EntityInTextEmail, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityHashtag: { const auto &d(entity.c_messageEntityHashtag()); result.push_back(EntityInText(EntityInTextHashtag, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityMention: { const auto &d(entity.c_messageEntityMention()); result.push_back(EntityInText(EntityInTextMention, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityMentionName: {
				const auto &d(entity.c_messageEntityMentionName());
				auto data = QString::number(d.vuser_id.v);
				if (auto user = App::userLoaded(peerFromUser(d.vuser_id))) {
					data += '.' + QString::number(user->access);
				}
				result.push_back(EntityInText(EntityInTextMentionName, d.voffset.v, d.vlength.v, data));
			} break;
			case mtpc_inputMessageEntityMentionName: {
				const auto &d(entity.c_inputMessageEntityMentionName());
				auto data = ([&d]() -> QString {
					if (d.vuser_id.type() == mtpc_inputUserSelf) {
						return QString::number(AuthSession::CurrentUserId());
					} else if (d.vuser_id.type() == mtpc_inputUser) {
						const auto &user(d.vuser_id.c_inputUser());
						return QString::number(user.vuser_id.v) + '.' + QString::number(user.vaccess_hash.v);
					}
					return QString();
				})();
				if (!data.isEmpty()) {
					result.push_back(EntityInText(EntityInTextMentionName, d.voffset.v, d.vlength.v, data));
				}
			} break;
			case mtpc_messageEntityBotCommand: { const auto &d(entity.c_messageEntityBotCommand()); result.push_back(EntityInText(EntityInTextBotCommand, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityBold: { const auto &d(entity.c_messageEntityBold()); result.push_back(EntityInText(EntityInTextBold, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityItalic: { const auto &d(entity.c_messageEntityItalic()); result.push_back(EntityInText(EntityInTextItalic, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityCode: { const auto &d(entity.c_messageEntityCode()); result.push_back(EntityInText(EntityInTextCode, d.voffset.v, d.vlength.v)); } break;
			case mtpc_messageEntityPre: { const auto &d(entity.c_messageEntityPre()); result.push_back(EntityInText(EntityInTextPre, d.voffset.v, d.vlength.v, textClean(qs(d.vlanguage)))); } break;
			}
		}
	}
	return result;
}

MTPVector<MTPMessageEntity> linksToMTP(const EntitiesInText &links, bool sending) {
	auto v = QVector<MTPMessageEntity>();
	v.reserve(links.size());
	for_const (auto &link, links) {
		if (link.length() <= 0) continue;
		if (sending
			&& link.type() != EntityInTextCode
			&& link.type() != EntityInTextPre
			&& link.type() != EntityInTextMentionName) {
			continue;
		}

		auto offset = MTP_int(link.offset()), length = MTP_int(link.length());
		switch (link.type()) {
		case EntityInTextUrl: v.push_back(MTP_messageEntityUrl(offset, length)); break;
		case EntityInTextCustomUrl: v.push_back(MTP_messageEntityTextUrl(offset, length, MTP_string(link.data()))); break;
		case EntityInTextEmail: v.push_back(MTP_messageEntityEmail(offset, length)); break;
		case EntityInTextHashtag: v.push_back(MTP_messageEntityHashtag(offset, length)); break;
		case EntityInTextMention: v.push_back(MTP_messageEntityMention(offset, length)); break;
		case EntityInTextMentionName: {
			auto inputUser = ([](const QString &data) -> MTPInputUser {
				UserId userId = 0;
				uint64 accessHash = 0;
				if (mentionNameToFields(data, &userId, &accessHash)) {
					if (userId == AuthSession::CurrentUserId()) {
						return MTP_inputUserSelf();
					}
					return MTP_inputUser(MTP_int(userId), MTP_long(accessHash));
				}
				return MTP_inputUserEmpty();
			})(link.data());
			if (inputUser.type() != mtpc_inputUserEmpty) {
				v.push_back(MTP_inputMessageEntityMentionName(offset, length, inputUser));
			}
		} break;
		case EntityInTextBotCommand: v.push_back(MTP_messageEntityBotCommand(offset, length)); break;
		case EntityInTextBold: v.push_back(MTP_messageEntityBold(offset, length)); break;
		case EntityInTextItalic: v.push_back(MTP_messageEntityItalic(offset, length)); break;
		case EntityInTextCode: v.push_back(MTP_messageEntityCode(offset, length)); break;
		case EntityInTextPre: v.push_back(MTP_messageEntityPre(offset, length, MTP_string(link.data()))); break;
		}
	}
	return MTP_vector<MTPMessageEntity>(std::move(v));
}

// Some code is duplicated in flattextarea.cpp!
void textParseEntities(QString &text, int32 flags, EntitiesInText *inOutEntities, bool rich) {
	EntitiesInText result;

	bool withHashtags = (flags & TextParseHashtags);
	bool withMentions = (flags & TextParseMentions);
	bool withBotCommands = (flags & TextParseBotCommands);
	bool withMono = (flags & TextParseMono);

	if (withMono) { // parse mono entities (code and pre)
		int existingEntityIndex = 0, existingEntitiesCount = inOutEntities->size();
		int existingEntityShiftLeft = 0;

		QString newText;

		int32 offset = 0, matchOffset = offset, len = text.size(), commandOffset = rich ? 0 : len;
		bool inLink = false, commandIsLink = false;
		const QChar *start = text.constData();
		for (; matchOffset < len;) {
			if (commandOffset <= matchOffset) {
				for (commandOffset = matchOffset; commandOffset < len; ++commandOffset) {
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
			}
			auto mPre = _rePre.match(text, matchOffset);
			auto mCode = _reCode.match(text, matchOffset);
			if (!mPre.hasMatch() && !mCode.hasMatch()) break;

			int preStart = mPre.hasMatch() ? mPre.capturedStart() : INT_MAX,
				preEnd = mPre.hasMatch() ? mPre.capturedEnd() : INT_MAX,
				codeStart = mCode.hasMatch() ? mCode.capturedStart() : INT_MAX,
				codeEnd = mCode.hasMatch() ? mCode.capturedEnd() : INT_MAX,
				tagStart, tagEnd;
			if (mPre.hasMatch()) {
				if (!mPre.capturedRef(1).isEmpty()) {
					++preStart;
				}
				if (!mPre.capturedRef(4).isEmpty()) {
					--preEnd;
				}
			}
			if (mCode.hasMatch()) {
				if (!mCode.capturedRef(1).isEmpty()) {
					++codeStart;
				}
				if (!mCode.capturedRef(4).isEmpty()) {
					--codeEnd;
				}
			}

			bool pre = (preStart <= codeStart);
			auto mTag = pre ? mPre : mCode;
			if (pre) {
				tagStart = preStart;
				tagEnd = preEnd;
			} else {
				tagStart = codeStart;
				tagEnd = codeEnd;
			}

			bool inCommand = checkTagStartInCommand(start, len, tagStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				matchOffset = commandOffset;
				continue;
			}

			bool addNewlineBefore = false, addNewlineAfter = false;
			int32 outerStart = tagStart, outerEnd = tagEnd;
			int32 innerStart = tagStart + mTag.capturedLength(2), innerEnd = tagEnd - mTag.capturedLength(3);

			// Check if start or end sequences intersect any existing entity.
			int intersectedEntityEnd = 0;
			for_const (auto &entity, *inOutEntities) {
				if (qMin(innerStart, entity.offset() + entity.length()) > qMax(outerStart, entity.offset()) ||
					qMin(outerEnd, entity.offset() + entity.length()) > qMax(innerEnd, entity.offset())) {
					intersectedEntityEnd = entity.offset() + entity.length();
					break;
				}
			}
			if (intersectedEntityEnd > 0) {
				matchOffset = qMax(innerStart, intersectedEntityEnd);
				continue;
			}

			if (newText.isEmpty()) newText.reserve(text.size());
			if (pre) {
				while (outerStart > 0 && chIsSpace(*(start + outerStart - 1), rich) && !chIsNewline(*(start + outerStart - 1))) {
					--outerStart;
				}
				addNewlineBefore = (outerStart > 0 && !chIsNewline(*(start + outerStart - 1)));

				for (int32 testInnerStart = innerStart; testInnerStart < innerEnd; ++testInnerStart) {
					if (chIsNewline(*(start + testInnerStart))) {
						innerStart = testInnerStart + 1;
						break;
					} else if (!chIsSpace(*(start + testInnerStart))) {
						break;
					}
				}
				for (int32 testInnerEnd = innerEnd; innerStart < testInnerEnd;) {
					--testInnerEnd;
					if (chIsNewline(*(start + testInnerEnd))) {
						innerEnd = testInnerEnd;
						break;
					} else if (!chIsSpace(*(start + testInnerEnd))) {
						break;
					}
				}

				while (outerEnd < len && chIsSpace(*(start + outerEnd)) && !chIsNewline(*(start + outerEnd))) {
					++outerEnd;
				}
				addNewlineAfter = (outerEnd < len && !chIsNewline(*(start + outerEnd)));
			}

			for (; existingEntityIndex < existingEntitiesCount && inOutEntities->at(existingEntityIndex).offset() < innerStart; ++existingEntityIndex) {
				auto &entity = inOutEntities->at(existingEntityIndex);
				result.push_back(entity);
				result.back().shiftLeft(existingEntityShiftLeft);
			}
			if (outerStart > offset) newText.append(start + offset, outerStart - offset);
			if (addNewlineBefore) newText.append('\n');
			existingEntityShiftLeft += (innerStart - outerStart) - (addNewlineBefore ? 1 : 0);

			int entityStart = newText.size(), entityLength = innerEnd - innerStart;
			result.push_back(EntityInText(pre ? EntityInTextPre : EntityInTextCode, entityStart, entityLength));

			for (; existingEntityIndex < existingEntitiesCount && inOutEntities->at(existingEntityIndex).offset() <= innerEnd; ++existingEntityIndex) {
				auto &entity = inOutEntities->at(existingEntityIndex);
				result.push_back(entity);
				result.back().shiftLeft(existingEntityShiftLeft);
			}
			newText.append(start + innerStart, entityLength);
			if (addNewlineAfter) newText.append('\n');
			existingEntityShiftLeft += (outerEnd - innerEnd) - (addNewlineAfter ? 1 : 0);

			offset = matchOffset = outerEnd;
		}
		if (!newText.isEmpty()) {
			newText.append(start + offset, len - offset);
			text = newText;
		}
		if (!result.isEmpty()) {
			for (; existingEntityIndex < existingEntitiesCount; ++existingEntityIndex) {
				auto &entity = inOutEntities->at(existingEntityIndex);
				result.push_back(entity);
				result.back().shiftLeft(existingEntityShiftLeft);
			}
			*inOutEntities = result;
			result = EntitiesInText();
		}
	}

	int existingEntityIndex = 0, existingEntitiesCount = inOutEntities->size();
	int existingEntityEnd = 0;

	initLinkSets();
	int32 len = text.size(), commandOffset = rich ? 0 : len;
	bool inLink = false, commandIsLink = false;
	const QChar *start = text.constData(), *end = start + text.size();
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
		auto mDomain = _reDomain.match(text, matchOffset);
		auto mExplicitDomain = _reExplicitDomain.match(text, matchOffset);
		auto mHashtag = withHashtags ? _reHashtag.match(text, matchOffset) : QRegularExpressionMatch();
		auto mMention = withMentions ? _reMention.match(text, qMax(mentionSkip, matchOffset)) : QRegularExpressionMatch();
		auto mBotCommand = withBotCommands ? _reBotCommand.match(text, matchOffset) : QRegularExpressionMatch();

		EntityInTextType lnkType = EntityInTextUrl;
		int32 lnkStart = 0, lnkLength = 0;
		int32 domainStart = mDomain.hasMatch() ? mDomain.capturedStart() : INT_MAX,
			domainEnd = mDomain.hasMatch() ? mDomain.capturedEnd() : INT_MAX,
			explicitDomainStart = mExplicitDomain.hasMatch() ? mExplicitDomain.capturedStart() : INT_MAX,
			explicitDomainEnd = mExplicitDomain.hasMatch() ? mExplicitDomain.capturedEnd() : INT_MAX,
			hashtagStart = mHashtag.hasMatch() ? mHashtag.capturedStart() : INT_MAX,
			hashtagEnd = mHashtag.hasMatch() ? mHashtag.capturedEnd() : INT_MAX,
			mentionStart = mMention.hasMatch() ? mMention.capturedStart() : INT_MAX,
			mentionEnd = mMention.hasMatch() ? mMention.capturedEnd() : INT_MAX,
			botCommandStart = mBotCommand.hasMatch() ? mBotCommand.capturedStart() : INT_MAX,
			botCommandEnd = mBotCommand.hasMatch() ? mBotCommand.capturedEnd() : INT_MAX;
		if (mHashtag.hasMatch()) {
			if (!mHashtag.capturedRef(1).isEmpty()) {
				++hashtagStart;
			}
			if (!mHashtag.capturedRef(2).isEmpty()) {
				--hashtagEnd;
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
				mMention = _reMention.match(text, qMax(mentionSkip, matchOffset));
				if (mMention.hasMatch()) {
					mentionStart = mMention.capturedStart();
					mentionEnd = mMention.capturedEnd();
				} else {
					mentionStart = INT_MAX;
					mentionEnd = INT_MAX;
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
		if (!mDomain.hasMatch() && !mExplicitDomain.hasMatch() && !mHashtag.hasMatch() && !mMention.hasMatch() && !mBotCommand.hasMatch()) {
			break;
		}

		if (explicitDomainStart < domainStart) {
			domainStart = explicitDomainStart;
			domainEnd = explicitDomainEnd;
			mDomain = mExplicitDomain;
		}
		if (mentionStart < hashtagStart && mentionStart < domainStart && mentionStart < botCommandStart) {
			bool inCommand = checkTagStartInCommand(start, len, mentionStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			lnkType = EntityInTextMention;
			lnkStart = mentionStart;
			lnkLength = mentionEnd - mentionStart;
		} else if (hashtagStart < domainStart && hashtagStart < botCommandStart) {
			bool inCommand = checkTagStartInCommand(start, len, hashtagStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			lnkType = EntityInTextHashtag;
			lnkStart = hashtagStart;
			lnkLength = hashtagEnd - hashtagStart;
		} else if (botCommandStart < domainStart) {
			bool inCommand = checkTagStartInCommand(start, len, botCommandStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			lnkType = EntityInTextBotCommand;
			lnkStart = botCommandStart;
			lnkLength = botCommandEnd - botCommandStart;
		} else {
			bool inCommand = checkTagStartInCommand(start, len, domainStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			QString protocol = mDomain.captured(1).toLower();
			QString topDomain = mDomain.captured(3).toLower();

			bool isProtocolValid = protocol.isEmpty() || _validProtocols.contains(hashCrc32(protocol.constData(), protocol.size() * sizeof(QChar)));
			bool isTopDomainValid = !protocol.isEmpty() || _validTopDomains.contains(hashCrc32(topDomain.constData(), topDomain.size() * sizeof(QChar)));

			if (protocol.isEmpty() && domainStart > offset + 1 && *(start + domainStart - 1) == QChar('@')) {
				QString forMailName = text.mid(offset, domainStart - offset - 1);
				QRegularExpressionMatch mMailName = _reMailName.match(forMailName);
				if (mMailName.hasMatch()) {
					int32 mailStart = offset + mMailName.capturedStart();
					if (mailStart < offset) {
						mailStart = offset;
					}
					lnkType = EntityInTextEmail;
					lnkStart = mailStart;
					lnkLength = domainEnd - mailStart;
				}
			}
			if (lnkType == EntityInTextUrl && !lnkLength) {
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
		for (; existingEntityIndex < existingEntitiesCount && inOutEntities->at(existingEntityIndex).offset() <= lnkStart; ++existingEntityIndex) {
			auto &entity = inOutEntities->at(existingEntityIndex);
			accumulate_max(existingEntityEnd, entity.offset() + entity.length());
			result.push_back(entity);
		}
		if (lnkStart >= existingEntityEnd) {
			inOutEntities->push_back(EntityInText(lnkType, lnkStart, lnkLength));
		}

		offset = matchOffset = lnkStart + lnkLength;
	}
	if (!result.isEmpty()) {
		for (; existingEntityIndex < existingEntitiesCount; ++existingEntityIndex) {
			auto &entity = inOutEntities->at(existingEntityIndex);
			result.push_back(entity);
		}
		*inOutEntities = result;
	}
}

QString textApplyEntities(const QString &text, const EntitiesInText &entities) {
	if (entities.isEmpty()) return text;

	QMultiMap<int32, QString> closingTags;
	QString code(qsl("`")), pre(qsl("```"));

	QString result;
	int32 size = text.size();
	const QChar *b = text.constData(), *already = b, *e = b + size;
	auto entity = entities.cbegin(), end = entities.cend();
	while (entity != end && ((entity->type() != EntityInTextCode && entity->type() != EntityInTextPre) || entity->length() <= 0 || entity->offset() >= size)) {
		++entity;
	}
	while (entity != end || !closingTags.isEmpty()) {
		int32 nextOpenEntity = (entity == end) ? (size + 1) : entity->offset();
		int32 nextCloseEntity = closingTags.isEmpty() ? (size + 1) : closingTags.cbegin().key();
		if (nextOpenEntity <= nextCloseEntity) {
			QString tag = (entity->type() == EntityInTextCode) ? code : pre;
			if (result.isEmpty()) result.reserve(text.size() + entities.size() * pre.size() * 2);

			const QChar *offset = b + nextOpenEntity;
			if (offset > already) {
				result.append(already, offset - already);
				already = offset;
			}
			result.append(tag);
			closingTags.insert(qMin(entity->offset() + entity->length(), size), tag);

			++entity;
			while (entity != end && ((entity->type() != EntityInTextCode && entity->type() != EntityInTextPre) || entity->length() <= 0 || entity->offset() >= size)) {
				++entity;
			}
		} else {
			const QChar *offset = b + nextCloseEntity;
			if (offset > already) {
				result.append(already, offset - already);
				already = offset;
			}
			result.append(closingTags.cbegin().value());
			closingTags.erase(closingTags.begin());
		}
	}
	if (result.isEmpty()) {
		return text;
	}
	const QChar *offset = b + size;
	if (offset > already) {
		result.append(already, offset - already);
	}
	return result;
}

void moveStringPart(QChar *start, int32 &to, int32 &from, int32 count, EntitiesInText *inOutEntities) {
	if (count > 0) {
		if (to < from) {
			memmove(start + to, start + from, count * sizeof(QChar));
			for (auto &entity : *inOutEntities) {
				if (entity.offset() >= from + count) break;
				if (entity.offset() + entity.length() < from) continue;
				if (entity.offset() >= from) {
					entity.extendToLeft(from - to);
				}
				if (entity.offset() + entity.length() < from + count) {
					entity.shrinkFromRight(from - to);
				}
			}
		}
		to += count;
		from += count;
	}
}

void replaceStringWithEntities(const QLatin1String &from, QChar to, QString &result, EntitiesInText *inOutEntities, bool checkSpace = false) {
	int32 len = from.size(), s = result.size(), offset = 0, length = 0;
	EntitiesInText::iterator i = inOutEntities->begin(), e = inOutEntities->end();
	for (QChar *start = result.data(); offset < s;) {
		int32 nextOffset = result.indexOf(from, offset);
		if (nextOffset < 0) {
			moveStringPart(start, length, offset, s - offset, inOutEntities);
			break;
		}

		if (checkSpace) {
			bool spaceBefore = (nextOffset > 0) && (start + nextOffset - 1)->isSpace();
			bool spaceAfter = (nextOffset + len < s) && (start + nextOffset + len)->isSpace();
			if (!spaceBefore && !spaceAfter) {
				moveStringPart(start, length, offset, nextOffset - offset + len + 1, inOutEntities);
				continue;
			}
		}

		bool skip = false;
		for (; i != e; ++i) { // find and check next finishing entity
			if (i->offset() + i->length() > nextOffset) {
				skip = (i->offset() < nextOffset + len);
				break;
			}
		}
		if (skip) {
			moveStringPart(start, length, offset, nextOffset - offset + len, inOutEntities);
			continue;
		}

		moveStringPart(start, length, offset, nextOffset - offset, inOutEntities);

		*(start + length) = to;
		++length;
		offset += len;
	}
	if (length < s) result.resize(length);
}

QString prepareTextWithEntities(QString result, int32 flags, EntitiesInText *inOutEntities) {
	cleanTextWithEntities(result, inOutEntities);

	if (flags) {
		textParseEntities(result, flags, inOutEntities);
	}

	replaceStringWithEntities(qstr("--"), QChar(8212), result, inOutEntities, true);
	replaceStringWithEntities(qstr("<<"), QChar(171), result, inOutEntities);
	replaceStringWithEntities(qstr(">>"), QChar(187), result, inOutEntities);

	if (cReplaceEmojis()) {
		result = Ui::Emoji::ReplaceInText(result, inOutEntities);
	}

	trimTextWithEntities(result, inOutEntities);

	return result;
}

// replace bad symbols with space and remove \r
void cleanTextWithEntities(QString &result, EntitiesInText *inOutEntities) {
	result = result.replace('\t', qstr("  "));
	int32 len = result.size(), to = 0, from = 0;
	QChar *start = result.data();
	for (QChar *ch = start, *end = start + len; ch < end; ++ch) {
		if (ch->unicode() == '\r') {
			moveStringPart(start, to, from, (ch - start) - from, inOutEntities);
			++from;
		} else if (chReplacedBySpace(*ch)) {
			*ch = ' ';
		}
	}
	moveStringPart(start, to, from, len - from, inOutEntities);
	if (to < len) result.resize(to);
}

void trimTextWithEntities(QString &result, EntitiesInText *inOutEntities) {
	bool foundNotTrimmedChar = false;

	// right trim
	for (QChar *s = result.data(), *e = s + result.size(), *ch = e; ch != s;) {
		--ch;
		if (!chIsTrimmed(*ch)) {
			if (ch + 1 < e) {
				int32 l = ch + 1 - s;
				for (auto &entity : *inOutEntities) {
					entity.updateTextEnd(l);
				}
				result.resize(l);
			}
			foundNotTrimmedChar = true;
			break;
		}
	}
	if (!foundNotTrimmedChar) {
		result.clear();
		inOutEntities->clear();
		return;
	}

	int firstMonospaceOffset = EntityInText::firstMonospaceOffset(*inOutEntities, result.size());

	// left trim
	for (QChar *s = result.data(), *ch = s, *e = s + result.size(); ch != e; ++ch) {
		if (!chIsTrimmed(*ch) || (ch - s) == firstMonospaceOffset) {
			if (ch > s) {
				int32 l = ch - s;
				for (auto &entity : *inOutEntities) {
					entity.shiftLeft(l);
				}
				result = result.mid(l);
			}
			break;
		}
	}
}
