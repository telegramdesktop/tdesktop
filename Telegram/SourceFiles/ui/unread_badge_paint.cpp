/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/unread_badge_paint.h"

#include "ui/ui_utility.h"
#include "styles/style_dialogs.h"

namespace Ui {
namespace {

struct UnreadBadgeSizeData {
	QImage circle;
	QPixmap left[6], right[6];
};
class UnreadBadgeStyleData {
public:
	UnreadBadgeStyleData();

	UnreadBadgeSizeData sizes[static_cast<int>(UnreadBadgeSize::kCount)];
	style::color bg[6] = {
		st::dialogsUnreadBg,
		st::dialogsUnreadBgOver,
		st::dialogsUnreadBgActive,
		st::dialogsUnreadBgMuted,
		st::dialogsUnreadBgMutedOver,
		st::dialogsUnreadBgMutedActive
	};
	style::color reactionBg[6] = {
		st::dialogsDraftFg,
		st::dialogsDraftFgOver,
		st::dialogsDraftFgActive,
		st::dialogsUnreadBgMuted,
		st::dialogsUnreadBgMutedOver,
		st::dialogsUnreadBgMutedActive
	};
	rpl::lifetime lifetime;
};

UnreadBadgeStyleData::UnreadBadgeStyleData() {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		for (auto &data : sizes) {
			for (auto &left : data.left) {
				left = QPixmap();
			}
			for (auto &right : data.right) {
				right = QPixmap();
			}
		}
	}, lifetime);
}

UnreadBadgeStyleData &UnreadBadgeStyles() {
	static auto result = UnreadBadgeStyleData();
	return result;
}

void CreateCircleMask(UnreadBadgeSizeData *data, int size) {
	if (!data->circle.isNull()) {
		return;
	}
	data->circle = style::createCircleMask(size);
}

[[nodiscard]] QImage ColorizeCircleHalf(
		UnreadBadgeSizeData *data,
		int size,
		int half,
		int xoffset,
		style::color color) {
	auto result = style::colorizeImage(data->circle, color, QRect(xoffset, 0, half, size));
	result.setDevicePixelRatio(style::DevicePixelRatio());
	return result;
}

[[nodiscard]] QString ComputeUnreadBadgeText(
		const QString &unreadCount,
		int allowDigits) {
	return (allowDigits > 0) && (unreadCount.size() > allowDigits + 1)
		? u".."_q + unreadCount.mid(unreadCount.size() - allowDigits)
		: unreadCount;
}

void PaintUnreadBadge(QPainter &p, const QRect &rect, const UnreadBadgeStyle &st) {
	Assert(rect.height() == st.size);

	int index = (st.muted ? 0x03 : 0x00) + (st.active ? 0x02 : (st.selected ? 0x01 : 0x00));
	int size = st.size, sizehalf = size / 2;

	auto &styles = UnreadBadgeStyles();
	auto badgeData = styles.sizes;
	if (st.sizeId > UnreadBadgeSize()) {
		Assert(st.sizeId < UnreadBadgeSize::kCount);
		badgeData = &styles.sizes[static_cast<int>(st.sizeId)];
	}
	const auto bg = (st.sizeId == UnreadBadgeSize::ReactionInDialogs)
		? styles.reactionBg[index]
		: styles.bg[index];
	if (badgeData->left[index].isNull()) {
		const auto ratio = style::DevicePixelRatio();
		int imgsize = size * ratio, imgsizehalf = sizehalf * ratio;
		CreateCircleMask(badgeData, size);
		badgeData->left[index] = PixmapFromImage(
			ColorizeCircleHalf(badgeData, imgsize, imgsizehalf, 0, bg));
		badgeData->right[index] = PixmapFromImage(ColorizeCircleHalf(
			badgeData,
			imgsize,
			imgsizehalf,
			imgsize - imgsizehalf,
			bg));
	}

	int bar = rect.width() - 2 * sizehalf;
	p.drawPixmap(rect.x(), rect.y(), badgeData->left[index]);
	if (bar) {
		p.fillRect(rect.x() + sizehalf, rect.y(), bar, rect.height(), bg);
	}
	p.drawPixmap(rect.x() + sizehalf + bar, rect.y(), badgeData->right[index]);
}

} // namespace

UnreadBadgeStyle::UnreadBadgeStyle()
: size(st::dialogsUnreadHeight)
, padding(st::dialogsUnreadPadding)
, font(st::dialogsUnreadFont) {
}

QSize CountUnreadBadgeSize(
		const QString &unreadCount,
		const UnreadBadgeStyle &st,
		int allowDigits) {
	const auto text = ComputeUnreadBadgeText(unreadCount, allowDigits);
	const auto unreadRectHeight = st.size;
	const auto unreadWidth = st.font->width(text);
	return {
		std::max(unreadWidth + 2 * st.padding, unreadRectHeight),
		unreadRectHeight,
	};
}

QRect PaintUnreadBadge(
		QPainter &p,
		const QString &unreadCount,
		int x,
		int y,
		const UnreadBadgeStyle &st,
		int allowDigits) {
	const auto text = ComputeUnreadBadgeText(unreadCount, allowDigits);
	const auto unreadRectHeight = st.size;
	const auto unreadWidth = st.font->width(text);
	const auto unreadRectWidth = std::max(
		unreadWidth + 2 * st.padding,
		unreadRectHeight);

	const auto unreadRectLeft = ((st.align & Qt::AlignHorizontal_Mask) & style::al_center)
		? (x - unreadRectWidth) / 2
		: ((st.align & Qt::AlignHorizontal_Mask) & style::al_right)
		? (x - unreadRectWidth)
		: x;
	const auto unreadRectTop = y;

	const auto badge = QRect(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight);
	PaintUnreadBadge(p, badge, st);

	const auto textTop = st.textTop ? st.textTop : (unreadRectHeight - st.font->height) / 2;
	p.setFont(st.font);
	p.setPen(st.active
		? st::dialogsUnreadFgActive
		: st.selected
		? st::dialogsUnreadFgOver
		: st::dialogsUnreadFg);
	p.drawText(unreadRectLeft + (unreadRectWidth - unreadWidth) / 2, unreadRectTop + textTop + st.font->ascent, text);

	return badge;
}

} // namespace Ui
