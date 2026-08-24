/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_common.h"

#include "editor/scene/scene.h"
#include "editor/scene/scene_item_sticker.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"

namespace Editor {
namespace {

constexpr auto kAnimatedMaxSide = 854;
constexpr auto kAnimatedFps = 30.;
constexpr auto kAnimatedMinDuration = crl::time(1000);
constexpr auto kAnimatedMaxDuration = crl::time(3000);

} // namespace

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

Media::Encode::Job ComposeAnimatedJob(
		const QImage &image,
		const PhotoModifications &mods) {
	Expects(mods.paint != nullptr);
	Expects(!image.isNull());

	const auto scene = mods.paint.get();
	const auto crop = mods.crop.isValid()
		? mods.crop
		: QRect(QPoint(), image.size());
	auto transform = QTransform();
	if (mods.flipped) {
		transform.scale(-1, 1);
	}
	if (mods.angle) {
		transform.rotate(mods.angle);
	}
	const auto matrix = QImage::trueMatrix(
		transform,
		crop.width(),
		crop.height());
	const auto rotated = matrix.mapRect(
		QRectF(QPointF(), QSizeF(crop.size()))).size();
	if (rotated.isEmpty()) {
		return {};
	}
	const auto fit = std::min({
		kAnimatedMaxSide / rotated.width(),
		kAnimatedMaxSide / rotated.height(),
		1.,
	});
	const auto target = QSize(
		std::max(int(rotated.width() * fit) & ~1, 2),
		std::max(int(rotated.height() * fit) & ~1, 2));
	const auto sceneToCanvas = QTransform::fromTranslate(
		-crop.x(),
		-crop.y()
	) * matrix * QTransform::fromScale(
		target.width() / rotated.width(),
		target.height() / rotated.height());

	const auto bake = [&](const QImage &source) {
		auto cropped = source.copy(crop);
		return cropped.transformed(transform, Qt::SmoothTransformation)
			.scaled(
				target,
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
	};

	auto job = Media::Encode::Job();
	const auto items = scene->items(Qt::AscendingOrder);
	auto normal = std::vector<NumberedItem*>();
	for (const auto &item : items) {
		if (item->isNormalStatus()) {
			normal.push_back(item.get());
		}
	}
	ranges::stable_sort(normal, ranges::less(), &QGraphicsItem::zValue);
	auto run = std::vector<NumberedItem*>();
	const auto flushRun = [&] {
		if (run.empty()) {
			return;
		}
		for (const auto item : normal) {
			item->setVisible(false);
		}
		for (const auto item : run) {
			item->setVisible(true);
		}
		auto layer = QImage(
			image.size(),
			QImage::Format_ARGB32_Premultiplied);
		layer.fill(Qt::transparent);
		{
			auto p = Painter(&layer);
			PainterHighQualityEnabler hq(p);
			scene->render(&p, layer.rect());
		}
		for (const auto item : normal) {
			item->setVisible(true);
		}
		job.overlay.push_back(bake(layer));
		run.clear();
	};

	auto longest = crl::time(0);
	for (const auto item : normal) {
		const auto sticker = (item->type() == ItemSticker::Type)
			? static_cast<ItemSticker*>(item)
			: nullptr;
		if (!sticker || !sticker->animated()) {
			run.push_back(item);
			continue;
		}
		auto entity = sticker->animatedEntity(sceneToCanvas);
		if (entity.bytes.isEmpty()) {
			run.push_back(item);
			continue;
		}
		flushRun();
		job.overlay.push_back(std::move(entity));
		const auto duration = sticker->loopDuration();
		longest = std::max(
			longest,
			duration ? duration : kAnimatedMaxDuration);
	}
	flushRun();

	job.source = Media::Encode::StillSource{
		.base = bake(image),
		.duration = std::clamp(
			longest,
			kAnimatedMinDuration,
			kAnimatedMaxDuration),
		.fps = kAnimatedFps,
	};
	job.silentLoop = true;
	return job;
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
