/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_emoji.h"

#include "ui/emoji_config.h"

#include <QtGui/QFontMetricsF>

namespace Tgw {
namespace {

struct CodeRange {
	char32_t first = 0;
	char32_t last = 0;
};

struct CodePoint {
	char32_t value = 0;
	int length = 0;
};

// Calibrated on Chromium's Apple Color Emoji in the conformance references.
constexpr auto kEmojiAdvance = 1.;
constexpr auto kEmojiRise = 0.35;
constexpr auto kFirstNonAscii = char16_t(0x80);
constexpr auto kFirstWideStart = char16_t(0x2000);
constexpr auto kTextPresentation = char16_t(0xFE0E);
constexpr auto kEmojiPresentationSelector = char32_t(0xFE0F);
constexpr auto kEnclosingKeycap = char32_t(0x20E3);
constexpr auto kFirstPictograph = char32_t(0x1F000);
constexpr auto kLastPictograph = char32_t(0x1FAFF);
constexpr auto kZeroWidthJoiner = char32_t(0x200D);
constexpr auto kFirstRegional = char32_t(0x1F1E6);
constexpr auto kLastRegional = char32_t(0x1F1FF);
constexpr auto kFirstTag = char32_t(0xE0020);
constexpr auto kLastTag = char32_t(0xE007F);
constexpr auto kNarrowStarts = std::array<char16_t, 4>{
	u'#',
	u'*',
	u'©',
	u'®',
};
constexpr auto kRightToLeft = std::array{
	QChar::DirR,
	QChar::DirAL,
	QChar::DirRLE,
	QChar::DirRLO,
	QChar::DirRLI,
};

// Emoji_Presentation of Unicode emoji-data.txt 17.0, as lib_ui's emoji.txt.
constexpr auto kEmojiPresentation = std::array<CodeRange, 81>{ {
	{ 0x231A, 0x231B },
	{ 0x23E9, 0x23EC },
	{ 0x23F0, 0x23F0 },
	{ 0x23F3, 0x23F3 },
	{ 0x25FD, 0x25FE },
	{ 0x2614, 0x2615 },
	{ 0x2648, 0x2653 },
	{ 0x267F, 0x267F },
	{ 0x2693, 0x2693 },
	{ 0x26A1, 0x26A1 },
	{ 0x26AA, 0x26AB },
	{ 0x26BD, 0x26BE },
	{ 0x26C4, 0x26C5 },
	{ 0x26CE, 0x26CE },
	{ 0x26D4, 0x26D4 },
	{ 0x26EA, 0x26EA },
	{ 0x26F2, 0x26F3 },
	{ 0x26F5, 0x26F5 },
	{ 0x26FA, 0x26FA },
	{ 0x26FD, 0x26FD },
	{ 0x2705, 0x2705 },
	{ 0x270A, 0x270B },
	{ 0x2728, 0x2728 },
	{ 0x274C, 0x274C },
	{ 0x274E, 0x274E },
	{ 0x2753, 0x2755 },
	{ 0x2757, 0x2757 },
	{ 0x2795, 0x2797 },
	{ 0x27B0, 0x27B0 },
	{ 0x27BF, 0x27BF },
	{ 0x2B1B, 0x2B1C },
	{ 0x2B50, 0x2B50 },
	{ 0x2B55, 0x2B55 },
	{ 0x1F004, 0x1F004 },
	{ 0x1F0CF, 0x1F0CF },
	{ 0x1F18E, 0x1F18E },
	{ 0x1F191, 0x1F19A },
	{ 0x1F1E6, 0x1F1FF },
	{ 0x1F201, 0x1F201 },
	{ 0x1F21A, 0x1F21A },
	{ 0x1F22F, 0x1F22F },
	{ 0x1F232, 0x1F236 },
	{ 0x1F238, 0x1F23A },
	{ 0x1F250, 0x1F251 },
	{ 0x1F300, 0x1F320 },
	{ 0x1F32D, 0x1F335 },
	{ 0x1F337, 0x1F37C },
	{ 0x1F37E, 0x1F393 },
	{ 0x1F3A0, 0x1F3CA },
	{ 0x1F3CF, 0x1F3D3 },
	{ 0x1F3E0, 0x1F3F0 },
	{ 0x1F3F4, 0x1F3F4 },
	{ 0x1F3F8, 0x1F43E },
	{ 0x1F440, 0x1F440 },
	{ 0x1F442, 0x1F4FC },
	{ 0x1F4FF, 0x1F53D },
	{ 0x1F54B, 0x1F54E },
	{ 0x1F550, 0x1F567 },
	{ 0x1F57A, 0x1F57A },
	{ 0x1F595, 0x1F596 },
	{ 0x1F5A4, 0x1F5A4 },
	{ 0x1F5FB, 0x1F64F },
	{ 0x1F680, 0x1F6C5 },
	{ 0x1F6CC, 0x1F6CC },
	{ 0x1F6D0, 0x1F6D2 },
	{ 0x1F6D5, 0x1F6D8 },
	{ 0x1F6DC, 0x1F6DF },
	{ 0x1F6EB, 0x1F6EC },
	{ 0x1F6F4, 0x1F6FC },
	{ 0x1F7E0, 0x1F7EB },
	{ 0x1F7F0, 0x1F7F0 },
	{ 0x1F90C, 0x1F93A },
	{ 0x1F93C, 0x1F945 },
	{ 0x1F947, 0x1F9FF },
	{ 0x1FA70, 0x1FA7C },
	{ 0x1FA80, 0x1FA8A },
	{ 0x1FA8E, 0x1FAC6 },
	{ 0x1FAC8, 0x1FAC8 },
	{ 0x1FACD, 0x1FADC },
	{ 0x1FADF, 0x1FAEA },
	{ 0x1FAEF, 0x1FAF8 },
} };

[[nodiscard]] bool IsEmojiPresentation(char32_t code) {
	const auto i = ranges::upper_bound(
		kEmojiPresentation,
		code,
		ranges::less(),
		&CodeRange::first);
	return (i != begin(kEmojiPresentation)) && (code <= (i - 1)->last);
}

[[nodiscard]] bool IsRegional(char32_t code) {
	return (code >= kFirstRegional) && (code <= kLastRegional);
}

[[nodiscard]] bool IsJoining(char32_t code) {
	return (code == kZeroWidthJoiner)
		|| ((code >= kFirstTag) && (code <= kLastTag));
}

[[nodiscard]] bool MayStartEmoji(char16_t unit) {
	return (unit >= kFirstWideStart)
		|| (unit >= u'0' && unit <= u'9')
		|| ranges::contains(kNarrowStarts, unit);
}

[[nodiscard]] bool IsAscii(QStringView text) {
	return ranges::all_of(text, [](QChar unit) {
		return (unit.unicode() < kFirstNonAscii);
	});
}

[[nodiscard]] CodePoint CodePointAt(QStringView text, int i) {
	const auto unit = text[i];
	if (unit.isHighSurrogate() && (i + 1 < int(text.size()))) {
		const auto low = text[i + 1];
		if (low.isLowSurrogate()) {
			return { char32_t(QChar::surrogateToUcs4(unit, low)), 2 };
		}
	}
	return { char32_t(unit.unicode()), 1 };
}

[[nodiscard]] int JoinedTill(QStringView text, int till) {
	const auto size = int(text.size());
	while (till < size) {
		const auto point = CodePointAt(text, till);
		if (!IsJoining(point.value)) {
			break;
		}
		till += point.length;
	}
	return till;
}

[[nodiscard]] bool HasRightToLeft(QStringView text) {
	const auto size = int(text.size());
	for (auto i = 0; i < size;) {
		const auto point = CodePointAt(text, i);
		if (ranges::contains(kRightToLeft, QChar::direction(point.value))) {
			return true;
		}
		i += point.length;
	}
	return false;
}

[[nodiscard]] int AddCell(
		std::vector<EmojiCell> &cells,
		QStringView text,
		int from,
		int till,
		EmojiPtr emoji) {
	if ((till < int(text.size()))
		&& (text[till].unicode() == kTextPresentation)) {
		return till;
	}
	const auto joined = emoji ? JoinedTill(text, till) : till;
	cells.push_back({ from, joined, emoji });
	return joined;
}

} // namespace

std::vector<EmojiCell> FindEmojiCells(QStringView text) {
	if (IsAscii(text)) {
		return {};
	}
	const auto size = int(text.size());
	auto result = std::vector<EmojiCell>();
	auto regionals = 0;
	for (auto i = 0; i < size;) {
		const auto point = CodePointAt(text, i);
		auto matched = 0;
		if (IsRegional(point.value)) {
			const auto flag = (regionals % 2)
				? nullptr
				: Ui::Emoji::Find(text.mid(i), &matched);
			if (flag && (matched > point.length)) {
				regionals += 2;
				i = AddCell(result, text, i, i + matched, flag);
			} else {
				++regionals;
				i = AddCell(result, text, i, i + point.length, nullptr);
			}
			continue;
		}
		regionals = 0;
		const auto emoji = MayStartEmoji(text[i].unicode())
			? Ui::Emoji::Find(text.mid(i), &matched)
			: nullptr;
		if (!emoji || (matched <= 0)) {
			i += point.length;
		} else if ((matched == point.length)
			&& !IsEmojiPresentation(point.value)) {
			i += matched;
		} else {
			i = AddCell(result, text, i, i + matched, emoji);
		}
	}
	if (!result.empty() && HasRightToLeft(text)) {
		return {};
	}
	return result;
}

int ColorGlyphCandidates(QStringView text) {
	if (IsAscii(text)) {
		return 0;
	}
	const auto size = int(text.size());
	auto result = 0;
	for (auto i = 0; i < size;) {
		const auto point = CodePointAt(text, i);
		const auto code = point.value;
		auto matched = 0;
		if ((code >= kFirstPictograph && code <= kLastPictograph)
			|| (code == kEmojiPresentationSelector)
			|| (code == kEnclosingKeycap)
			|| IsEmojiPresentation(code)
			|| (MayStartEmoji(text[i].unicode())
				&& Ui::Emoji::Find(text.mid(i), &matched))) {
			++result;
		}
		i += point.length;
	}
	return result;
}

float64 EmojiAdvance(float64 fontSize) {
	return fontSize * kEmojiAdvance;
}

QRectF EmojiBox(float64 x, float64 fontSize) {
	const auto side = EmojiAdvance(fontSize);
	return QRectF(x, -fontSize * kEmojiRise - side / 2., side, side);
}

float64 WalkTextWithEmoji(
		const QFontMetricsF &metrics,
		const QString &text,
		const std::vector<EmojiCell> &cells,
		float64 fontSize,
		Fn<void(float64, const QString &, EmojiPtr)> visit) {
	auto x = 0.;
	auto from = 0;
	const auto gap = [&](int till) {
		if (till > from) {
			const auto piece = text.mid(from, till - from);
			if (visit) {
				visit(x, piece, nullptr);
			}
			x += metrics.horizontalAdvance(piece);
		}
	};
	for (const auto &cell : cells) {
		gap(cell.from);
		if (visit) {
			const auto piece = cell.emoji
				? QString()
				: text.mid(cell.from, cell.till - cell.from);
			visit(x, piece, cell.emoji);
		}
		x += EmojiAdvance(fontSize);
		from = cell.till;
	}
	gap(int(text.size()));
	return x;
}

} // namespace Tgw
