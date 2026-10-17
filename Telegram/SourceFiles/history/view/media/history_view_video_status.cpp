/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_video_status.h"

#include "ui/chat/chat_style.h"
#include "ui/effects/radial_animation.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_chat.h"
#include "styles/style_chat_style.h"

namespace HistoryView {

QRect VideoCornerDownloadRect(QPoint position) {
	const auto padding = st::msgDateImgPadding;
	const auto statusX = position.x() + st::msgDateImgDelta + padding.x();
	const auto statusY = position.y() + st::msgDateImgDelta + padding.y();
	return Rect(
		statusX + padding.y() - padding.x(),
		statusY,
		Size(st::historyVideoDownloadSize));
}

void PaintVideoCornerStatus(
		Painter &p,
		const Ui::ChatPaintContext &context,
		const VideoCornerStatus &status) {
	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto &font = st::normalFont;
	const auto position = status.position;
	const auto outerWidth = status.outerWidth;
	const auto padding = st::msgDateImgPadding;
	const auto addLeft = status.download
		? (st::historyVideoDownloadSize + 2 * padding.y())
		: 0;
	const auto addRight = status.mute ? st::historyVideoMuteSize : 0;
	const auto downloadWidth = status.download
		? font->width(status.downloadSize)
		: 0;
	const auto statusWidth = std::max(downloadWidth, font->width(status.text))
		+ 2 * padding.x()
		+ addLeft
		+ addRight;
	const auto statusHeight = 2 * padding.y()
		+ (status.download ? st::historyVideoDownloadSize : font->height);
	const auto statusX = position.x() + st::msgDateImgDelta + padding.x();
	const auto statusY = position.y() + st::msgDateImgDelta + padding.y();
	const auto textWidth = statusWidth - 2 * padding.x();
	const auto freeHeight = statusHeight - 2 * font->height;
	const auto statusTextTop = status.download
		? (statusY + (freeHeight / 3) - padding.y())
		: statusY;
	const auto around = style::rtlrect(
		statusX - padding.x(),
		statusY - padding.y(),
		statusWidth,
		statusHeight,
		outerWidth);
	Ui::FillRoundRect(p, around, sti->msgDateImgBg, sti->msgDateImgBgCorners);
	p.setFont(font);
	p.setPen(st->msgDateImgFg());
	p.drawTextLeft(
		statusX + addLeft,
		statusTextTop,
		outerWidth,
		status.text,
		textWidth);
	if (status.download) {
		const auto downloadTextTop = statusY
			+ font->height
			+ (2 * freeHeight / 3)
			- padding.y();
		p.drawTextLeft(
			statusX + addLeft,
			downloadTextTop,
			outerWidth,
			status.downloadSize,
			textWidth);
		const auto inner = VideoCornerDownloadRect(position);
		const auto &icon = status.loading
			? sti->historyVideoCancel
			: sti->historyVideoDownload;
		icon.paintInCenter(p, inner);
		if (status.radial) {
			const auto line = st::historyVideoRadialLine;
			status.radial->draw(
				p,
				inner - Margins(line),
				line,
				sti->historyFileThumbRadialFg);
		}
	} else if (status.mute) {
		const auto &icon = sti->historyVideoMessageMute;
		icon.paint(
			p,
			statusX - padding.x() - padding.y() + statusWidth - addRight,
			statusY - padding.y() + (statusHeight - icon.height()) / 2,
			outerWidth);
	}
}

void PaintVideoTimestampMark(
		Painter &p,
		QRect rthumb,
		std::optional<Ui::BubbleRounding> rounding,
		crl::time position,
		crl::time duration) {
	const auto cornerRadius = [](Ui::BubbleCornerRounding corner) {
		return (corner == Ui::BubbleCornerRounding::Small)
			? Ui::BubbleRadiusSmall()
			: (corner == Ui::BubbleCornerRounding::Large)
			? Ui::BubbleRadiusLarge()
			: 0;
	};
	const auto radiusLeft = rounding
		? cornerRadius(rounding->bottomLeft)
		: st::roundRadiusSmall;
	const auto radiusRight = rounding
		? cornerRadius(rounding->bottomRight)
		: st::roundRadiusSmall;
	const auto line = st::historyVideoTimestampProgressLine;
	if (rthumb.height() <= line
		|| rthumb.width() <= radiusLeft + radiusRight
		|| position > duration) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto used = rthumb.width() - radiusLeft - radiusRight;
	const auto progress = position / float64(duration);
	const auto edge = radiusLeft + int(base::SafeRound(used * progress));
	const auto top = rthumb.y() + rthumb.height() - line;
	const auto rest = rthumb.width() - edge;
	p.save();
	p.setPen(Qt::NoPen);
	if (edge > 0) {
		p.save();
		p.setBrush(st::windowBgActive);
		p.setClipRect(rthumb.x(), top, edge, line, Qt::IntersectClip);
		p.drawRoundedRect(
			rthumb.x(),
			top - 2 * radiusLeft,
			edge + radiusLeft,
			line + 2 * radiusLeft,
			radiusLeft,
			radiusLeft);
		p.restore();
	}
	if (rest > 0) {
		const auto left = rthumb.x() + edge;
		p.save();
		p.setBrush(st::mediaviewPlaybackProgressFg);
		p.setClipRect(left, top, rest, line, Qt::IntersectClip);
		p.drawRoundedRect(
			left - radiusRight,
			top - 2 * radiusRight,
			rest + radiusRight,
			line + 2 * radiusRight,
			radiusRight,
			radiusRight);
		p.restore();
	}
	p.restore();
}

} // namespace HistoryView
