/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/tabs/info_profile_tab_skeleton.h"

#include "ui/effects/animations.h"
#include "ui/effects/glare.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

using Type = Storage::SharedMediaType;

void PaintGridSkeleton(QPainter &p, int width, int height) {
	const auto minSize = st::infoMediaMinGridSize;
	const auto columns = std::max(width / minSize, 1);
	const auto size = (width - (columns - 1)) / float64(columns);
	for (auto y = 0.; y < height; y += size + 1) {
		for (auto i = 0; i != columns; ++i) {
			p.drawRect(QRectF(i * (size + 1), y, size, size));
		}
	}
}

void PaintRowsSkeleton(
		QPainter &p,
		int width,
		int height,
		int rowHeight,
		int thumbSize,
		bool round,
		int lines) {
	const auto padding = st::infoMediaSkeletonRowPadding;
	const auto bar = st::infoMediaSkeletonBarHeight;
	const auto widths = std::array{ 3, 2, 1 };
	for (auto y = 0; y < height; y += rowHeight) {
		const auto thumbTop = y + (rowHeight - thumbSize) / 2;
		if (round) {
			p.drawEllipse(padding, thumbTop, thumbSize, thumbSize);
		} else {
			p.drawRoundedRect(
				QRect(padding, thumbTop, thumbSize, thumbSize),
				st::roundRadiusSmall,
				st::roundRadiusSmall);
		}
		const auto left = padding + thumbSize + padding;
		const auto available = width - left - padding;
		if (available <= 0) {
			continue;
		}
		const auto linesHeight = lines * bar + (lines - 1) * (bar / 2);
		auto barTop = y + (rowHeight - linesHeight) / 2;
		for (auto line = 0; line != lines; ++line) {
			p.drawRoundedRect(
				left,
				barTop,
				available * widths[line % widths.size()] / 5,
				bar,
				bar / 2.,
				bar / 2.);
			barTop += bar + (bar / 2);
		}
	}
}

} // namespace

object_ptr<Ui::RpWidget> CreateTabSkeleton(
		not_null<QWidget*> parent,
		Storage::SharedMediaType type) {
	auto widget = object_ptr<Ui::RpWidget>(parent);
	const auto raw = widget.data();
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);

	struct State {
		Ui::GlareEffect glare;
	};
	const auto state = raw->lifetime().make_state<State>();

	raw->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto width = raw->width();
		const auto height = raw->height();
		switch (type) {
		case Type::Photo:
		case Type::Video:
		case Type::PhotoVideo:
		case Type::GIF:
			PaintGridSkeleton(p, width, height);
			break;
		case Type::File:
			PaintRowsSkeleton(
				p,
				width,
				height,
				st::infoMediaSkeletonRowHeight,
				st::infoMediaSkeletonFileThumb,
				false,
				3);
			break;
		case Type::Link:
			PaintRowsSkeleton(
				p,
				width,
				height,
				st::infoMediaSkeletonLinkRow,
				st::infoMediaSkeletonLinkThumb,
				false,
				3);
			break;
		default:
			PaintRowsSkeleton(
				p,
				width,
				height,
				st::infoMediaSkeletonLinkRow,
				st::infoMediaSkeletonSoundSize,
				true,
				2);
			break;
		}
		auto &glare = state->glare;
		if (glare.glare.birthTime) {
			const auto progress = glare.progress(crl::now());
			const auto x = (-glare.width)
				+ (width + glare.width * 2) * progress;
			p.drawTiledPixmap(x, 0, glare.width, height, glare.pixmap, 0, 0);
		}
	}, raw->lifetime());

	raw->widthValue(
	) | rpl::on_next([=](int width) {
		state->glare.width = width;
		state->glare.validate(
			st::windowBg->c,
			[=] { raw->update(); },
			crl::time(1000),
			crl::time(1000));
	}, raw->lifetime());

	return widget;
}

} // namespace Info::Profile
