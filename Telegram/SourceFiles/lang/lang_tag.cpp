/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_tag.h"

#include "lang/lang_keys.h"

namespace Lang {
namespace {

//
// http://www.unicode.org/cldr/charts/latest/supplemental/language_plural_rules.html
//

constexpr auto kShiftZero  = ushort(0);
constexpr auto kShiftOne   = ushort(1);
constexpr auto kShiftTwo   = ushort(2);
constexpr auto kShiftFew   = ushort(3);
constexpr auto kShiftMany  = ushort(4);
constexpr auto kShiftOther = ushort(5);

//
// n absolute value of the source number (integer and decimals).
// i integer digits of n.
// v number of visible fraction digits in n, with trailing zeros.
// w number of visible fraction digits in n, without trailing zeros.
// f visible fractional digits in n, with trailing zeros.
// t visible fractional digits in n, without trailing zeros.
//
// Let n be int, being -1 for non-integer numbers and n == i for integer numbers.
// It is fine while in the rules we compare n only to integers.
//
// -123.450: n = -1, i = 123, v = 3, w = 2, f = 450, t = 45
//

using ChoosePluralMethod = ushort (*)(int n, int i, int v, int w, int f, int t);

ushort ChoosePluralAr(int n, int i, int v, int w, int f, int t) {
	if (n == 0) {
		return kShiftZero;
	} else if (n == 1) {
		return kShiftOne;
	} else if (n == 2) {
		return kShiftTwo;
	} else if (n < 0) {
		return kShiftOther;
	}
	auto mod100 = (n % 100);
	if (mod100 >= 3 && mod100 <= 10) {
		return kShiftFew;
	} else if (mod100 >= 11 && mod100 <= 99) {
		return kShiftMany;
	}
	return kShiftOther;
}

ushort ChoosePluralEn(int n, int i, int v, int w, int f, int t) {
	if (i == 1 && v == 0) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePluralPt(int n, int i, int v, int w, int f, int t) {
	if (i == 0 || i == 1) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePluralEs(int n, int i, int v, int w, int f, int t) {
	if (n == 1) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePluralKo(int n, int i, int v, int w, int f, int t) {
	return kShiftOther;
}

ushort ChoosePluralRu(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		auto mod10 = (i % 10);
		auto mod100 = (i % 100);
		if ((mod10 == 1) && (mod100 != 11)) {
			return kShiftOne;
		} else if ((mod10 >= 2) && (mod10 <= 4) && (mod100 < 12 || mod100 > 14)) {
			return kShiftFew;
		} else {
			return kShiftMany;
		}
	}
	return kShiftMany;// kShiftOther;
}

QMap<QString, ChoosePluralMethod> GeneratePluralRulesMap() {
	auto result = QMap<QString, ChoosePluralMethod>();
	result.insert(qsl("ar"), ChoosePluralAr);
//	result.insert(qsl("de"), ChoosePluralEn);
//	result.insert(qsl("en"), ChoosePluralEn); // En is default, so we don't fill it inside the map.
	result.insert(qsl("es"), ChoosePluralEs);
//	result.insert(qsl("it"), ChoosePluralEn);
	result.insert(qsl("ko"), ChoosePluralKo);
//	result.insert(qsl("nl"), ChoosePluralEn);
	result.insert(qsl("pt"), ChoosePluralPt);
	result.insert(qsl("ru"), ChoosePluralRu);
	return result;
}

ChoosePluralMethod ChoosePlural = ChoosePluralEn;

} // namespace

int FindTagReplacementPosition(const QString &original, ushort tag) {
	for (auto s = original.constData(), ch = s, e = ch + original.size(); ch != e;) {
		if (*ch == TextCommand) {
			if (ch + kTagReplacementSize <= e && (ch + 1)->unicode() == TextCommandLangTag && *(ch + 3) == TextCommand) {
				if ((ch + 2)->unicode() == 0x0020 + tag) {
					return ch - s;
				} else {
					ch += kTagReplacementSize;
				}
			} else {
				auto next = textSkipCommand(ch, e);
				if (next == ch) {
					++ch;
				} else {
					ch = next;
				}
			}
		} else {
			++ch;
		}
	}
	return -1;

}

PluralResult Plural(ushort keyBase, float64 value) {
	// Simplified.
	auto n = qAbs(value);
	auto i = qFloor(n);
	auto integer = (qCeil(n) == i);
	auto v = integer ? 0 : 6;
	auto w = v;
	auto f = integer ? 0 : 111111;
	auto t = integer ? 0 : 111111;

	auto &langpack = Lang::Current();
	auto useNonDefaultPlural = (ChoosePlural != ChoosePluralEn && langpack.isNonDefaultPlural(LangKey(keyBase)));
	auto shift = (useNonDefaultPlural ? ChoosePlural : ChoosePluralEn)((integer ? i : -1), i, v, w, f, t);
	auto string = langpack.getValue(LangKey(keyBase + shift));
	if (i == qCeil(n)) {
		return { string, QString::number(qRound(value)) };
	}
	return { string, QString::number(value) };
}

void UpdatePluralRules(const QString &languageId) {
	static auto kMap = GeneratePluralRulesMap();
	ChoosePlural = kMap.value(languageId.toLower(), ChoosePluralEn);
}

QString ReplaceTag<QString>::Replace(QString &&original, const QString &replacement, int replacementPosition) {
	auto result = QString();
	result.reserve(original.size() + replacement.size() - kTagReplacementSize);
	if (replacementPosition > 0) {
		result.append(original.midRef(0, replacementPosition));
	}
	result.append(replacement);
	if (replacementPosition + kTagReplacementSize < original.size()) {
		result.append(original.midRef(replacementPosition + kTagReplacementSize));
	}
	return result;
}

} // namespace Lang
