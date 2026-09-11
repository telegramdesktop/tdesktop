/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_common.h"

#include "editor/scene/scene.h"
#include "editor/scene/scene_item_animated.h"
#include "editor/scene/scene_item_video.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"

namespace Editor {
namespace {

constexpr auto kAnimatedMaxSide = 854;
constexpr auto kAnimatedFps = 30.;
constexpr auto kAnimatedMinDuration = crl::time(1000);

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
	auto music = std::vector<Media::Encode::MusicTrack>();
	for (const auto item : normal) {
		const auto animated = item->asAnimated();
		if (!animated || !animated->animated()) {
			run.push_back(item);
			continue;
		}
		auto entity = animated->animatedEntity(sceneToCanvas);
		if (entity.bytes.isEmpty()) {
			run.push_back(item);
			continue;
		}
		flushRun();
		const auto video = (item->type() == ItemVideo::Type)
			? static_cast<ItemVideo*>(item)
			: nullptr;
		const auto loop = animated->loopDuration();
		if (video && (loop > 0)) {
			entity.till = entity.from + loop;
		}
		job.overlay.push_back(std::move(entity));
		longest = std::max(longest, loop);
		if (video && video->sounding()) {
			const auto &source = video->source();
			const auto trim = video->trim();
			music.push_back({
				.path = source.path,
				.bytes = source.content,
				.from = trim.from,
				.till = trim.from + loop,
				.volume = video->volume(),
				.loop = true,
			});
		}
	}
	flushRun();

	if (const auto audio = scene->audio()) {
		if (audio->volume > 0.) {
			music.push_back({
				.path = audio->path,
				.bytes = audio->content,
				.from = audio->from,
				.till = (audio->till > audio->from) ? audio->till : 0,
				.volume = audio->volume,
			});
		}
		longest = std::max(longest, audio->length());
	}
	job.source = Media::Encode::StillSource{
		.base = bake(image),
		.duration = std::max(longest, kAnimatedMinDuration),
		.fps = kAnimatedFps,
		.music = std::move(music),
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
