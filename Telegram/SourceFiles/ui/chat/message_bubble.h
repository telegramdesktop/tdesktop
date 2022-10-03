/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class ChatTheme;
class ChatStyle;

enum class BubbleCornerRounding : uchar {
	None,
	Tail,
	Small,
	Large,
};

struct BubbleRounding {
	BubbleCornerRounding topLeft : 2 = BubbleCornerRounding();
	BubbleCornerRounding topRight : 2 = BubbleCornerRounding();
	BubbleCornerRounding bottomLeft : 2 = BubbleCornerRounding();
	BubbleCornerRounding bottomRight : 2 = BubbleCornerRounding();

	struct ConstProxy {
		constexpr ConstProxy(
			not_null<const BubbleRounding*> that,
			int index) noexcept
		: that(that)
		, index(index) {
			Expects(index >= 0 && index < 4);
		}

		constexpr operator BubbleCornerRounding() const noexcept {
			switch (index) {
			case 0: return that->topLeft;
			case 1: return that->topRight;
			case 2: return that->bottomLeft;
			case 3: return that->bottomRight;
			}
			Unexpected("Index value in BubbleRounding::ConstProxy.");
		}

		not_null<const BubbleRounding*> that;
		int index = 0;
	};
	struct Proxy : ConstProxy {
		constexpr Proxy(not_null<BubbleRounding*> that, int index) noexcept
		: ConstProxy(that, index) {
		}

		using ConstProxy::operator BubbleCornerRounding;

		constexpr Proxy &operator=(BubbleCornerRounding value) noexcept {
			const auto nonconst = const_cast<BubbleRounding*>(that.get());
			switch (index) {
			case 0: nonconst->topLeft = value; break;
			case 1: nonconst->topRight = value; break;
			case 2: nonconst->bottomLeft = value; break;
			case 3: nonconst->bottomRight = value; break;
			}
			return *this;
		}
	};
	[[nodiscard]] constexpr ConstProxy operator[](int index) const {
		return { this, index };
	}
	[[nodiscard]] constexpr Proxy operator[](int index) {
		return { this, index };
	}

	[[nodiscard]] uchar key() const {
		static_assert(sizeof(*this) == sizeof(uchar));
		return uchar(*reinterpret_cast<const std::byte*>(this));
	}

	inline friend constexpr auto operator<=>(
		BubbleRounding,
		BubbleRounding) = default;
};

struct BubbleSelectionInterval {
	int top = 0;
	int height = 0;
};

struct BubblePattern {
	QPixmap pixmap;
	std::array<QImage, 4> cornersSmall;
	std::array<QImage, 4> cornersLarge;
	QImage tailLeft;
	QImage tailRight;
	mutable QImage cornerTopSmallCache;
	mutable QImage cornerTopLargeCache;
	mutable QImage cornerBottomSmallCache;
	mutable QImage cornerBottomLargeCache;
	mutable QImage tailCache;
};

[[nodiscard]] std::unique_ptr<BubblePattern> PrepareBubblePattern(
	not_null<const style::palette*> st);
void FinishBubblePatternOnMain(not_null<BubblePattern*> pattern);

struct SimpleBubble {
	not_null<const ChatStyle*> st;
	QRect geometry;
	const BubblePattern *pattern = nullptr;
	QRect patternViewport;
	int outerWidth = 0;
	bool selected = false;
	bool shadowed = true;
	bool outbg = false;
	BubbleRounding rounding;
};

struct ComplexBubble {
	SimpleBubble simple;
	const std::vector<BubbleSelectionInterval> &selection;
};

void PaintBubble(QPainter &p, const SimpleBubble &args);
void PaintBubble(QPainter &p, const ComplexBubble &args);

void PaintPatternBubblePart(
	QPainter &p,
	const QRect &viewport,
	const QPixmap &pixmap,
	const QRect &target);

void PaintPatternBubblePart(
	QPainter &p,
	const QRect &viewport,
	const QPixmap &pixmap,
	const QRect &target,
	const QImage &mask,
	QImage &cache);

void PaintPatternBubblePart(
	QPainter &p,
	const QRect &viewport,
	const QPixmap &pixmap,
	const QRect &target,
	Fn<void(QPainter&)> paintContent,
	QImage &cache);

} // namespace Ui
