/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_tag.h"

#include "lang/lang_keys.h"
#include "ui/text/text.h"

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

ushort ChoosePlural1(int n, int i, int v, int w, int f, int t) {
	return kShiftOther;
}

ushort ChoosePlural2fil(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		const auto mod10 = (i % 10);
		if (i == 1 || i == 2 || i == 3) {
			return kShiftOne;
		} else if (mod10 != 4 && mod10 != 6 && mod10 != 9) {
			return kShiftOne;
		}
		return kShiftOther;
	}
	const auto mod10 = (f % 10);
	if (mod10 != 4 && mod10 != 6 && mod10 != 9) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2tzm(int n, int i, int v, int w, int f, int t) {
	if (n == 0 || n == 1) {
		return kShiftOne;
	} else if (n >= 11 && n <= 99) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2is(int n, int i, int v, int w, int f, int t) {
	if (t == 0) {
		const auto mod10 = (i % 10);
		const auto mod100 = (i % 100);
		if (mod10 == 1 && mod100 != 11) {
			return kShiftOne;
		}
		return kShiftOther;
	}
	return kShiftOne;
}

ushort ChoosePlural2mk(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		const auto mod10 = (i % 10);
		const auto mod100 = (i % 100);
		if (mod10 == 1 && mod100 != 11) {
			return kShiftOne;
		}
	}
	const auto mod10 = (f % 10);
	const auto mod100 = (f % 100);
	if ((mod10 == 1) && (mod100 != 11)) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2ak(int n, int i, int v, int w, int f, int t) {
	if (n == 0 || n == 1) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2am(int n, int i, int v, int w, int f, int t) {
	if (i == 0 || n == 1) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2hy(int n, int i, int v, int w, int f, int t) {
	if (i == 0 || i == 1) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2si(int n, int i, int v, int w, int f, int t) {
	if (n == 0 || n == 1) {
		return kShiftOne;
	} else if (i == 0 && f == 1) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2bh(int n, int i, int v, int w, int f, int t) {
	// not documented
	if (n == 0 || n == 1) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2af(int n, int i, int v, int w, int f, int t) {
	if (n == 1) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2ast(int n, int i, int v, int w, int f, int t) {
	if (i == 1 && v == 0) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural2da(int n, int i, int v, int w, int f, int t) {
	if (n == 1) {
		return kShiftOne;
	} else if (t != 0 && ((i == 0) || (i == 1))) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural3lv(int n, int i, int v, int w, int f, int t) {
	const auto nmod10 = (n % 10);
	const auto nmod100 = (n % 100);
	const auto fmod10 = (f % 10);
	const auto fmod100 = (f % 100);
	if (nmod10 == 0) {
		return kShiftZero;
	} else if ((nmod100 >= 11) && (nmod100 <= 19)) {
		return kShiftZero;
	} else if ((v == 2) && (fmod100 >= 11) && (fmod100 <= 19)) {
		return kShiftZero;
	} else if ((nmod10 == 1) && (nmod100 != 11)) {
		return kShiftOne;
	} else if ((v == 2) && (fmod10 == 1) && (fmod100 != 11)) {
		return kShiftOne;
	} else if ((v != 2) && (fmod10 == 1)) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural3ksh(int n, int i, int v, int w, int f, int t) {
	if (n == 0) {
		return kShiftZero;
	} else if (n == 1) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural3lag(int n, int i, int v, int w, int f, int t) {
	if (n == 0) {
		return kShiftZero;
	} else if ((n != 0) && ((i == 0) || (i == 1))) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural3kw(int n, int i, int v, int w, int f, int t) {
	if (n == 1) {
		return kShiftOne;
	} else if (n == 2) {
		return kShiftTwo;
	}
	return kShiftOther;
}

ushort ChoosePlural3bs(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		const auto mod10 = (i % 10);
		const auto mod100 = (i % 100);
		if ((mod10 >= 2) && (mod10 <= 4) && (mod100 < 12 || mod100 > 14)) {
			return kShiftFew;
		} else if ((mod10 == 1) && (mod100 != 11)) {
			return kShiftOne;
		}
		return kShiftOther;
	}
	const auto mod10 = (f % 10);
	const auto mod100 = (f % 100);
	if ((mod10 >= 2) && (mod10 <= 4) && (mod100 < 12 || mod100 > 14)) {
		return kShiftFew;
	} else if ((mod10 == 1) && (mod100 != 11)) {
		return kShiftOne;
	}
	return kShiftOther;
}

ushort ChoosePlural3shi(int n, int i, int v, int w, int f, int t) {
	if (i == 0 || n == 1) {
		return kShiftOne;
	} else if (n >= 2 && n <= 10) {
		return kShiftFew;
	}
	return kShiftOther;
}

ushort ChoosePlural3mo(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		const auto mod100 = (n % 100);
		if (i == 1) {
			return kShiftOne;
		} else if (n == 0) {
			return kShiftFew;
		} else if ((n != 1) && (mod100 >= 1) && (mod100 <= 19)) {
			return kShiftFew;
		}
		return kShiftOther;
	}
	return kShiftFew;
}

ushort ChoosePlural4be(int n, int i, int v, int w, int f, int t) {
	const auto mod10 = (n % 10);
	const auto mod100 = (n % 100);
	if ((mod10 >= 2) && (mod10 <= 4) && (mod100 < 12 || mod100 > 14)) {
		return kShiftFew;
	} else if ((mod10 == 1) && (mod100 != 11)) {
		return kShiftOne;
	} else if (mod10 == 0) {
		return kShiftMany;
	} else if ((mod10 >= 5) && (mod10 <= 9)) {
		return kShiftMany;
	} else if ((mod100 >= 11) && (mod100 <= 14)) {
		return kShiftMany;
	}
	return kShiftOther;
}

ushort ChoosePlural4ru(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		const auto mod10 = (i % 10);
		const auto mod100 = (i % 100);
		if ((mod10 >= 2) && (mod10 <= 4) && (mod100 < 12 || mod100 > 14)) {
			return kShiftFew;
		} else if ((mod10 == 1) && (mod100 != 11)) {
			return kShiftOne;
		}
		return kShiftMany;
	}
	return kShiftOther;
}

ushort ChoosePlural4pl(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		if (i == 1) {
			return kShiftOne;
		}
		const auto mod10 = (i % 10);
		const auto mod100 = (i % 100);
		if ((mod10 >= 2) && (mod10 <= 4) && (mod100 < 12 || mod100 > 14)) {
			return kShiftFew;
		} else {
			return kShiftMany;
		}
	}
	return kShiftOther;
}

ushort ChoosePlural4lt(int n, int i, int v, int w, int f, int t) {
	const auto mod10 = (n % 10);
	const auto mod100 = (n % 100);
	if ((mod10 >= 2) && (mod10 <= 9) && (mod100 < 11 || mod100 > 19)) {
		return kShiftFew;
	} else if ((mod10 == 1) && (mod100 != 11)) {
		return kShiftOne;
	} else if (f != 0) {
		return kShiftMany;
	}
	return kShiftOther;
}

ushort ChoosePlural4cs(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		if (i == 1) {
			return kShiftOne;
		} else if (i >= 2 && i <= 4) {
			return kShiftFew;
		}
		return kShiftOther;
	}
	return kShiftMany;
}

ushort ChoosePlural4gd(int n, int i, int v, int w, int f, int t) {
	if (n == 1 || n == 11) {
		return kShiftOne;
	} else if (n == 2 || n == 12) {
		return kShiftTwo;
	} else if (n >= 3 && n <= 10) {
		return kShiftFew;
	} else if (n >= 13 && n <= 19) {
		return kShiftFew;
	}
	return kShiftOther;
}

ushort ChoosePlural4dsb(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		const auto imod100 = (i % 100);
		if (imod100 == 1) {
			return kShiftOne;
		} else if (imod100 == 2) {
			return kShiftTwo;
		} else if (imod100 == 3 || imod100 == 4) {
			return kShiftFew;
		}
	}
	const auto fmod100 = (f % 100);
	if (fmod100 == 1) {
		return kShiftOne;
	} else if (fmod100 == 2) {
		return kShiftTwo;
	} else if (fmod100 == 3 || fmod100 == 4) {
		return kShiftFew;
	}
	return kShiftOther;
}

ushort ChoosePlural4sl(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		const auto imod100 = (i % 100);
		if (imod100 == 3 || imod100 == 4) {
			return kShiftFew;
		} else if (imod100 == 1) {
			return kShiftOne;
		} else if (imod100 == 2) {
			return kShiftTwo;
		}
		return kShiftOther;
	}
	return kShiftFew;
}

ushort ChoosePlural4he(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		if (i == 1) {
			return kShiftOne;
		} else if (i == 2) {
			return kShiftTwo;
		} else if ((n != 0) && (n != 10) && ((n % 10) == 0)) {
			return kShiftMany;
		}
		return kShiftOther;
	}
	return kShiftOther;
}

ushort ChoosePlural4mt(int n, int i, int v, int w, int f, int t) {
	const auto mod100 = (n % 100);
	if (n == 1) {
		return kShiftOne;
	} else if (n == 0) {
		return kShiftFew;
	} else if (mod100 >= 2 && mod100 <= 10) {
		return kShiftFew;
	} else if (mod100 >= 11 && mod100 <= 19) {
		return kShiftMany;
	}
	return kShiftOther;
}

ushort ChoosePlural5gv(int n, int i, int v, int w, int f, int t) {
	if (v == 0) {
		const auto mod10 = (i % 10);
		const auto mod20 = (i % 20);
		if (mod10 == 1) {
			return kShiftOne;
		} else if (mod10 == 2) {
			return kShiftTwo;
		} else if (mod20 == 0) {
			return kShiftFew;
		}
		return kShiftOther;
	}
	return kShiftMany;
}

ushort ChoosePlural5br(int n, int i, int v, int w, int f, int t) {
	const auto mod10 = (n % 10);
	const auto mod100 = (n % 100);
	if ((mod10 == 1)
		&& (mod100 != 11)
		&& (mod100 != 71)
		&& (mod100 != 91)) {
		return kShiftOne;
	} else if ((mod10 == 2)
		&& (mod100 != 12)
		&& (mod100 != 72)
		&& (mod100 != 92)) {
		return kShiftTwo;
	} else if (((mod10 == 3) || (mod10 == 4) || (mod10 == 9))
		&& ((mod100 < 10) || (mod100 > 19))
		&& ((mod100 < 70) || (mod100 > 79))
		&& ((mod100 < 90) || (mod100 > 99))) {
		return kShiftFew;
	} else if ((n != 0) && (n % 1000000 == 0)) {
		return kShiftMany;
	}
	return kShiftOther;
}

ushort ChoosePlural5ga(int n, int i, int v, int w, int f, int t) {
	if (n == 1) {
		return kShiftOne;
	} else if (n == 2) {
		return kShiftTwo;
	} else if (n >= 3 && n <= 6) {
		return kShiftFew;
	} else if (n >= 7 && n <= 10) {
		return kShiftMany;
	}
	return kShiftOther;
}

ushort ChoosePlural6ar(int n, int i, int v, int w, int f, int t) {
	if (n == 0) {
		return kShiftZero;
	} else if (n == 1) {
		return kShiftOne;
	} else if (n == 2) {
		return kShiftTwo;
	} else if (n < 0) {
		return kShiftOther;
	}
	const auto mod100 = (n % 100);
	if (mod100 >= 3 && mod100 <= 10) {
		return kShiftFew;
	} else if (mod100 >= 11 && mod100 <= 99) {
		return kShiftMany;
	}
	return kShiftOther;
}

ushort ChoosePlural6cy(int n, int i, int v, int w, int f, int t) {
	if (n == 0) {
		return kShiftZero;
	} else if (n == 1) {
		return kShiftOne;
	} else if (n == 2) {
		return kShiftTwo;
	} else if (n == 3) {
		return kShiftFew;
	} else if (n == 6) {
		return kShiftMany;
	}
	return kShiftOther;
}

struct PluralsKey {
	PluralsKey(uint64 key);
	PluralsKey(const char *value);

	inline operator uint64() const {
		return data;
	}

	uint64 data = 0;
};

char ConvertKeyChar(char ch) {
	return (ch == '_') ? '-' : QChar::toLower(ch);
}

PluralsKey::PluralsKey(uint64 key) : data(key) {
}

PluralsKey::PluralsKey(const char *value) {
	for (auto ch = *value; ch; ch = *++value) {
		data = (data << 8) | uint64(ConvertKeyChar(ch));
	}
}

std::map<PluralsKey, ChoosePluralMethod> GeneratePluralRulesMap() {
	return {
		//{ "bm", ChoosePlural1 },
		//{ "my", ChoosePlural1 },
		//{ "yue", ChoosePlural1 },
		//{ "zh", ChoosePlural1 },
		//{ "dz", ChoosePlural1 },
		//{ "ig", ChoosePlural1 },
		//{ "id", ChoosePlural1 },
		//{ "in", ChoosePlural1 }, // same as "id"
		//{ "ja", ChoosePlural1 },
		//{ "jv", ChoosePlural1 },
		//{ "jw", ChoosePlural1 }, // same as "jv"
		//{ "kea", ChoosePlural1 },
		//{ "km", ChoosePlural1 },
		//{ "ko", ChoosePlural1 },
		//{ "ses", ChoosePlural1 },
		//{ "lkt", ChoosePlural1 },
		//{ "lo", ChoosePlural1 },
		//{ "jbo", ChoosePlural1 },
		//{ "kde", ChoosePlural1 },
		//{ "ms", ChoosePlural1 },
		//{ "nqo", ChoosePlural1 },
		//{ "sah", ChoosePlural1 },
		//{ "sg", ChoosePlural1 },
		//{ "ii", ChoosePlural1 },
		//{ "th", ChoosePlural1 },
		//{ "bo", ChoosePlural1 },
		//{ "to", ChoosePlural1 },
		//{ "vi", ChoosePlural1 },
		//{ "wo", ChoosePlural1 },
		//{ "yo", ChoosePlural1 },
		//{ default, ChoosePlural1 },
		{ "fil", ChoosePlural2fil },
		{ "tl", ChoosePlural2fil },
		{ "tzm", ChoosePlural2tzm },
		{ "is", ChoosePlural2is },
		{ "mk", ChoosePlural2mk },
		{ "ak", ChoosePlural2ak },
		{ "guw", ChoosePlural2ak },
		{ "ln", ChoosePlural2ak },
		{ "mg", ChoosePlural2ak },
		{ "nso", ChoosePlural2ak },
		{ "pa", ChoosePlural2ak },
		{ "ti", ChoosePlural2ak },
		{ "wa", ChoosePlural2ak },
		{ "am", ChoosePlural2am },
		{ "as", ChoosePlural2am },
		{ "bn", ChoosePlural2am },
		{ "gu", ChoosePlural2am },
		{ "hi", ChoosePlural2am },
		{ "kn", ChoosePlural2am },
		{ "mr", ChoosePlural2am },
		{ "fa", ChoosePlural2am },
		{ "zu", ChoosePlural2am },
		{ "hy", ChoosePlural2hy },
		{ "fr", ChoosePlural2hy },
		{ "ff", ChoosePlural2hy },
		{ "kab", ChoosePlural2hy },
		{ "pt", ChoosePlural2hy },
		{ "si", ChoosePlural2si },
		{ "bh", ChoosePlural2bh },
		{ "bho", ChoosePlural2bh },
		{ "af", ChoosePlural2af },
		{ "sq", ChoosePlural2af },
		{ "asa", ChoosePlural2af },
		{ "az", ChoosePlural2af },
		{ "eu", ChoosePlural2af },
		{ "bem", ChoosePlural2af },
		{ "bez", ChoosePlural2af },
		{ "brx", ChoosePlural2af },
		{ "bg", ChoosePlural2af },
		{ "ckb", ChoosePlural2af },
		{ "ce", ChoosePlural2af },
		{ "chr", ChoosePlural2af },
		{ "cgg", ChoosePlural2af },
		{ "dv", ChoosePlural2af },
		{ "eo", ChoosePlural2af },
		{ "ee", ChoosePlural2af },
		{ "fo", ChoosePlural2af },
		{ "fur", ChoosePlural2af },
		{ "ka", ChoosePlural2af },
		{ "el", ChoosePlural2af },
		{ "ha", ChoosePlural2af },
		{ "haw", ChoosePlural2af },
		{ "hu", ChoosePlural2af },
		{ "kaj", ChoosePlural2af },
		{ "kkj", ChoosePlural2af },
		{ "kl", ChoosePlural2af },
		{ "ks", ChoosePlural2af },
		{ "kk", ChoosePlural2af },
		{ "ku", ChoosePlural2af },
		{ "ky", ChoosePlural2af },
		{ "lb", ChoosePlural2af },
		{ "jmc", ChoosePlural2af },
		{ "ml", ChoosePlural2af },
		{ "mas", ChoosePlural2af },
		{ "mgo", ChoosePlural2af },
		{ "mn", ChoosePlural2af },
		{ "nah", ChoosePlural2af },
		{ "ne", ChoosePlural2af },
		{ "nnh", ChoosePlural2af },
		{ "jgo", ChoosePlural2af },
		{ "nd", ChoosePlural2af },
		{ "no", ChoosePlural2af },
		{ "nb", ChoosePlural2af },
		{ "nn", ChoosePlural2af },
		{ "ny", ChoosePlural2af },
		{ "nyn", ChoosePlural2af },
		{ "or", ChoosePlural2af },
		{ "om", ChoosePlural2af },
		{ "os", ChoosePlural2af },
		{ "pap", ChoosePlural2af },
		{ "ps", ChoosePlural2af },
		{ "rm", ChoosePlural2af },
		{ "rof", ChoosePlural2af },
		{ "rwk", ChoosePlural2af },
		{ "ssy", ChoosePlural2af },
		{ "saq", ChoosePlural2af },
		{ "seh", ChoosePlural2af },
		{ "ksb", ChoosePlural2af },
		{ "sn", ChoosePlural2af },
		{ "sd", ChoosePlural2af },
		{ "xog", ChoosePlural2af },
		{ "so", ChoosePlural2af },
		{ "nr", ChoosePlural2af },
		{ "sdh", ChoosePlural2af },
		{ "st", ChoosePlural2af },
		{ "es", ChoosePlural2af },
		{ "ss", ChoosePlural2af },
		{ "gsw", ChoosePlural2af },
		{ "syr", ChoosePlural2af },
		{ "ta", ChoosePlural2af },
		{ "te", ChoosePlural2af },
		{ "teo", ChoosePlural2af },
		{ "tig", ChoosePlural2af },
		{ "ts", ChoosePlural2af },
		{ "tn", ChoosePlural2af },
		{ "tr", ChoosePlural2af },
		{ "tk", ChoosePlural2af },
		{ "kcg", ChoosePlural2af },
		{ "ug", ChoosePlural2af },
		{ "uz", ChoosePlural2af },
		{ "ve", ChoosePlural2af },
		{ "vo", ChoosePlural2af },
		{ "vun", ChoosePlural2af },
		{ "wae", ChoosePlural2af },
		{ "xh", ChoosePlural2af },
		{ "", ChoosePlural2ast },
		{ "ast", ChoosePlural2ast },
		{ "ca", ChoosePlural2ast },
		{ "nl", ChoosePlural2ast },
		{ "en", ChoosePlural2ast },
		{ "et", ChoosePlural2ast },
		{ "pt_PT", ChoosePlural2ast },
		{ "fi", ChoosePlural2ast },
		{ "gl", ChoosePlural2ast },
		{ "lg", ChoosePlural2ast },
		{ "de", ChoosePlural2ast },
		{ "io", ChoosePlural2ast },
		{ "ia", ChoosePlural2ast },
		{ "it", ChoosePlural2ast },
		{ "sc", ChoosePlural2ast },
		{ "scn", ChoosePlural2ast },
		{ "sw", ChoosePlural2ast },
		{ "sv", ChoosePlural2ast },
		{ "ur", ChoosePlural2ast },
		{ "fy", ChoosePlural2ast },
		{ "ji", ChoosePlural2ast },
		{ "yi", ChoosePlural2ast }, // same as "ji"
		{ "da", ChoosePlural2da },
		{ "lv", ChoosePlural3lv },
		{ "prg", ChoosePlural3lv },
		{ "ksh", ChoosePlural3ksh },
		{ "lag", ChoosePlural3lag },
		{ "kw", ChoosePlural3kw },
		{ "smn", ChoosePlural3kw },
		{ "iu", ChoosePlural3kw },
		{ "smj", ChoosePlural3kw },
		{ "naq", ChoosePlural3kw },
		{ "se", ChoosePlural3kw },
		{ "smi", ChoosePlural3kw },
		{ "sms", ChoosePlural3kw },
		{ "sma", ChoosePlural3kw },
		{ "bs", ChoosePlural3bs },
		{ "hr", ChoosePlural3bs },
		{ "sr", ChoosePlural3bs },
		{ "sh", ChoosePlural3bs },
		{ "sr_Latn", ChoosePlural3bs }, // same as "sh"
		{ "shi", ChoosePlural3shi },
		{ "mo", ChoosePlural3mo },
		{ "ro_MD", ChoosePlural3mo }, // same as "mo"
		{ "ro", ChoosePlural3mo },
		{ "be", ChoosePlural4be },
		{ "ru", ChoosePlural4ru },
		{ "uk", ChoosePlural4ru },
		{ "pl", ChoosePlural4pl },
		{ "lt", ChoosePlural4lt },
		{ "cs", ChoosePlural4cs },
		{ "sk", ChoosePlural4cs },
		{ "gd", ChoosePlural4gd },
		{ "dsb", ChoosePlural4dsb },
		{ "hsb", ChoosePlural4dsb },
		{ "sl", ChoosePlural4sl },
		{ "he", ChoosePlural4he },
		{ "iw", ChoosePlural4he }, // same as "he"
		{ "mt", ChoosePlural4mt },
		{ "gv", ChoosePlural5gv },
		{ "br", ChoosePlural5br },
		{ "ga", ChoosePlural5ga },
		{ "ar", ChoosePlural6ar },
		{ "ars", ChoosePlural6ar },
		{ "cy", ChoosePlural6cy },
	};
	//return {
	//	//{ "af", ChoosePluralEn },
	//	{ "ak", ChoosePluralPt },
	//	{ "am", ChoosePluralPt },
	//	{ "ar", ChoosePluralAr },
	//	{ "as", ChoosePluralPt },
	//	{ "az", ChoosePluralEs },
	//	{ "be", ChoosePluralRu }, // should be different
	//	{ "bg", ChoosePluralEs },
	//	{ "bh", ChoosePluralPt },
	//	{ "bm", ChoosePluralKo },
	//	{ "bn", ChoosePluralPt },
	//	{ "bo", ChoosePluralKo },
	//	{ "bs", ChoosePluralSr },
	//	//{ "ca", ChoosePluralEn },
	//	//{ "ce", ChoosePluralEn },
	//	{ "cs", ChoosePluralSk },
	//	{ "da", ChoosePluralDa },
	//	//{ "de", ChoosePluralEn },
	//	//{ "dv", ChoosePluralEn },
	//	{ "dz", ChoosePluralKo },
	//	//{ "ee", ChoosePluralEn },
	//	{ "el", ChoosePluralEs },
	//	//{ "en", ChoosePluralEn },
	//	{ "es", ChoosePluralEs },
	//	{ "eo", ChoosePluralEs },
	//	//{ "et", ChoosePluralEn },
	//	{ "eu", ChoosePluralEs },
	//	{ "fa", ChoosePluralPt },
	//	{ "ff", ChoosePluralPt },
	//	//{ "fi", ChoosePluralEn },
	//	//{ "fo", ChoosePluralEn },
	//	{ "fr", ChoosePluralPt },
	//	//{ "fy", ChoosePluralEn },
	//	//{ "gl", ChoosePluralEn },
	//	{ "gu", ChoosePluralPt },
	//	//{ "ha", ChoosePluralEn },
	//	{ "he", ChoosePluralHe },
	//	{ "hi", ChoosePluralPt },
	//	{ "hr", ChoosePluralSr },
	//	{ "hu", ChoosePluralEs },
	//	{ "hy", ChoosePluralPt },
	//	//{ "ia", ChoosePluralEn },
	//	{ "ig", ChoosePluralKo },
	//	{ "ii", ChoosePluralKo },
	//	{ "in", ChoosePluralKo },
	//	//{ "io", ChoosePluralEn },
	//	{ "is", ChoosePluralIs },
	//	//{ "it", ChoosePluralEn },
	//	{ "ja", ChoosePluralKo },
	//	{ "jw", ChoosePluralKo },
	//	//{ "ka", ChoosePluralEn },
	//	//{ "kk", ChoosePluralEn },
	//	//{ "kl", ChoosePluralEn },
	//	{ "km", ChoosePluralKo },
	//	{ "kn", ChoosePluralPt },
	//	{ "ko", ChoosePluralKo },
	//	//{ "ks", ChoosePluralEn },
	//	//{ "ku", ChoosePluralEn },
	//	//{ "ky", ChoosePluralEn },
	//	//{ "lb", ChoosePluralEn },
	//	//{ "lg", ChoosePluralEn },
	//	{ "ln", ChoosePluralPt },
	//	{ "lo", ChoosePluralKo },
	//	{ "mg", ChoosePluralPt },
	//	{ "mk", ChoosePluralIs },
	//	//{ "ml", ChoosePluralEn },
	//	//{ "mn", ChoosePluralEn },
	//	{ "mo", ChoosePluralRo },
	//	{ "mr", ChoosePluralPt },
	//	{ "ms", ChoosePluralKo },
	//	{ "mt", ChoosePluralMt },
	//	{ "my", ChoosePluralKo },
	//	{ "nb", ChoosePluralEs },
	//	//{ "nd", ChoosePluralEn },
	//	//{ "ne", ChoosePluralEn },
	//	//{ "nl", ChoosePluralEn },
	//	//{ "nn", ChoosePluralEn },
	//	//{ "no", ChoosePluralEn },
	//	//{ "nr", ChoosePluralEn },
	//	//{ "ny", ChoosePluralEn },
	//	//{ "om", ChoosePluralEn },
	//	//{ "or", ChoosePluralEn },
	//	//{ "os", ChoosePluralEn },
	//	{ "pa", ChoosePluralPt },
	//	//{ "ps", ChoosePluralEn },
	//	{ "pt", ChoosePluralPt },
	//	//{ "pt_PT", ChoosePluralEn }, // not supported
	//	//{ "rm", ChoosePluralEn },
	//	{ "ro", ChoosePluralRo },
	//	{ "ru", ChoosePluralRu },
	//	//{ "sc", ChoosePluralEn },
	//	//{ "sd", ChoosePluralEn },
	//	{ "sg", ChoosePluralKo },
	//	{ "sk", ChoosePluralSk },
	//	//{ "sn", ChoosePluralEn },
	//	//{ "so", ChoosePluralEn },
	//	{ "sh", ChoosePluralSr },
	//	{ "si", ChoosePluralPt },
	//	{ "sr", ChoosePluralSr },
	//	//{ "ss", ChoosePluralEn },
	//	//{ "st", ChoosePluralEn },
	//	//{ "sv", ChoosePluralEn },
	//	//{ "sw", ChoosePluralEn },
	//	{ "ta", ChoosePluralEs },
	//	//{ "te", ChoosePluralEn },
	//	{ "th", ChoosePluralKo },
	//	{ "ti", ChoosePluralPt },
	//	{ "tk", ChoosePluralEs },
	//	{ "tl", ChoosePluralTl },
	//	//{ "tn", ChoosePluralEn },
	//	{ "to", ChoosePluralKo },
	//	{ "tr", ChoosePluralEs },
	//	//{ "ts", ChoosePluralEn },
	//	//{ "ug", ChoosePluralEn },
	//	{ "uk", ChoosePluralRu },
	//	//{ "ur", ChoosePluralEn },
	//	{ "uz", ChoosePluralEs },
	//	{ "pl", ChoosePluralPl },
	//	{ "sq", ChoosePluralEs },
	//	//{ "ve", ChoosePluralEn },
	//	{ "vi", ChoosePluralKo },
	//	//{ "vo", ChoosePluralEn },
	//	{ "wa", ChoosePluralPt },
	//	{ "wo", ChoosePluralKo },
	//	//{ "xh", ChoosePluralEn },
	//	//{ "yi", ChoosePluralEn },
	//	{ "yo", ChoosePluralKo },
	//	{ "zh", ChoosePluralKo },
	//	{ "zu", ChoosePluralPt },
	//};
}

const auto ChoosePluralDefault = &ChoosePlural2ast;
auto ChoosePlural = ChoosePluralDefault;

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

QString FormatDouble(float64 value) {
	auto result = QString::number(value, 'f', 6);
	while (result.endsWith('0')) {
		result.chop(1);
	}
	if (result.endsWith('.')) {
		result.chop(1);
	}
	return result;
}

int NonZeroPartToInt(QString value) {
	auto zeros = 0;
	for (const auto ch : value) {
		if (ch == '0') {
			++zeros;
		} else {
			break;
		}
	}
	return (zeros > 0)
		? (zeros < value.size() ? value.midRef(zeros).toInt() : 0)
		: (value.isEmpty() ? 0 : value.toInt());
}

ShortenedCount FormatCountToShort(int64 number) {
	auto result = ShortenedCount{ number };
	const auto abs = std::abs(number);
	const auto shorten = [&](int64 divider, char multiplier) {
		const auto sign = (number > 0) ? 1 : -1;
		const auto rounded = abs / (divider / 10);
		result.string = QString::number(sign * rounded / 10);
		if (rounded % 10) {
			result.string += '.' + QString::number(rounded % 10) + multiplier;
		} else {
			result.string += multiplier;
		}
		// Update given number.
		// E.g. 12345 will be 12000.
		result.number = rounded * divider;
	};
	if (abs >= 1'000'000) {
		shorten(1'000'000, 'M');
	} else if (abs >= 10'000) {
		shorten(1'000, 'K');
	} else {
		result.string = QString::number(number);
	}
	return result;
}

PluralResult Plural(
		ushort keyBase,
		float64 value,
		lngtag_count type) {
	// To correctly select a shift for PluralType::Short
	// we must first round the number.
	const auto shortened = (type == lt_count_short)
		? FormatCountToShort(qRound(value))
		: ShortenedCount();

	// Simplified.
	const auto n = std::abs(shortened.number ? float64(shortened.number) : value);
	const auto i = int(std::floor(n));
	const auto integer = (int(std::ceil(n)) == i);
	const auto formatted = integer ? QString() : FormatDouble(n);
	const auto dot = formatted.indexOf('.');
	const auto fraction = (dot >= 0) ? formatted.mid(dot + 1) : QString();
	const auto v = fraction.size();
	const auto w = v;
	const auto f = NonZeroPartToInt(fraction);
	const auto t = f;

	const auto useNonDefaultPlural = (ChoosePlural != ChoosePluralDefault)
		&& Lang::details::IsNonDefaultPlural(keyBase);
	const auto shift = (useNonDefaultPlural ? ChoosePlural : ChoosePluralDefault)(
		(integer ? i : -1),
		i,
		v,
		w,
		f,
		t);
	if (integer) {
		const auto round = qRound(value);
		if (type == lt_count_short) {
			return { shift, shortened.string };
		} else if (type == lt_count_decimal) {
			return { shift, QString("%L1").arg(round) };
		}
		return { shift, QString::number(round) };
	}
	return { shift, FormatDouble(value) };
}

void UpdatePluralRules(const QString &languageId) {
	static auto kMap = GeneratePluralRulesMap();
	auto parent = uint64(0);
	auto key = uint64(0);
	for (const auto ch : languageId) {
		const auto converted = ConvertKeyChar(ch.unicode());
		if (converted == '-' && !parent) {
			parent = key;
		}
		key = (key << 8) | uint64(converted);
	}
	const auto i = [&] {
		const auto result = kMap.find(key);
		return (result != end(kMap) || !parent)
			? result
			: kMap.find(parent);
	}();
	ChoosePlural = (i == end(kMap)) ? ChoosePlural1 : i->second;
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
