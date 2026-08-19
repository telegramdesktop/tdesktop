/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_common.h"

#include "editor/scene/scene.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"

namespace Editor {

void ApplyShapeMask(QImage &image, const PhotoModifications &mods) {
	if (mods.cropMode != EditorData::CropMode::Mask) {
		return;
	}
	const auto type = mods.cropType;
	if (type == EditorData::CropType::Rect) {
		return;
	}
	const auto multiplier = (type == EditorData::CropType::RoundedRect)
		? RoundedCornersMultiplier(mods.cornersLevel)
		: Ui::ForumUserpicRadiusMultiplier();
	if (type == EditorData::CropType::RoundedRect && multiplier <= 0.) {
		return;
	}
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	}
	auto mask = QImage(image.size(), QImage::Format_ARGB32_Premultiplied);
	mask.fill(Qt::transparent);
	{
		auto p = QPainter(&mask);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		const auto rect = QRectF(QPointF(), QSizeF(image.size()));
		if (type == EditorData::CropType::Ellipse) {
			p.drawEllipse(rect);
		} else {
			const auto radius = std::min(rect.width(), rect.height())
				* multiplier;
			p.drawRoundedRect(rect, radius, radius);
		}
	}
	auto p = QPainter(&image);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawImage(0, 0, mask);
}

float64 RoundedCornersMultiplier(RoundedCornersLevel level) {
	switch (level) {
	case RoundedCornersLevel::Large: return Ui::ForumUserpicRadiusMultiplier();
	case RoundedCornersLevel::Medium: return 0.2;
	case RoundedCornersLevel::Small: return 0.12;
	case RoundedCornersLevel::None: return 0.;
	}
	Unexpected("Unknown RoundedCornersLevel in RoundedCornersMultiplier.");
}

QImage ImageModified(QImage image, const PhotoModifications &mods) {
	Expects(!image.isNull());

	if (!mods) {
		return image;
	}
	if (mods.paint) {
		if (image.format() != QImage::Format_ARGB32_Premultiplied) {
			image = image.convertToFormat(
				QImage::Format_ARGB32_Premultiplied);
		}

		Painter p(&image);
		PainterHighQualityEnabler hq(p);

		mods.paint->render(&p, image.rect());
	}
	auto cropped = mods.crop.isValid()
		? image.copy(mods.crop)
		: image;
	QTransform transform;
	if (mods.flipped) {
		transform.scale(-1, 1);
	}
	if (mods.angle) {
		transform.rotate(mods.angle);
	}
	return cropped.transformed(transform);
}

bool PhotoModifications::empty() const {
	return !angle && !flipped && !crop.isValid() && !paint;
}

PhotoModifications::operator bool() const {
	return !empty();
}

PhotoModifications::~PhotoModifications() {
	if (paint && (paint.use_count() == 1)) {
		paint->deleteLater();
	}
}

} // namespace Editor
