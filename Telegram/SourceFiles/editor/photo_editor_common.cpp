/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_common.h"

#include "editor/scene/scene.h"
#include "editor/scene/scene_item_animated.h"
#include "editor/video/video_clip.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"

namespace Editor {
namespace {

constexpr auto kAnimatedMaxSide = 854;
constexpr auto kAnimatedFps = 30.;
constexpr auto kAnimatedMinDuration = crl::time(1000);

[[nodiscard]] QImage ExpandCanvas(
		const QImage &image,
		QRect crop,
		Scene *scene = nullptr) {
	auto result = QImage(crop.size(), QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	auto p = Painter(&result);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.drawImage(-crop.topLeft(), image);
	if (scene) {
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		PainterHighQualityEnabler hq(p);
		scene->render(&p, QRectF(result.rect()), QRectF(crop));
	}
	return result;
}

void FillCanvasBackground(
		QImage &canvas,
		const Media::Encode::CanvasBackground &background,
		QRect keep = QRect()) {
	auto p = QPainter(&canvas);
	p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
	if (!keep.isEmpty()) {
		p.setClipRegion(QRegion(canvas.rect()) - QRegion(keep));
	}
	Media::Encode::PaintCanvasBackground(p, canvas.rect(), background);
}

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
	const auto expanded = mods.crop.isValid()
		&& !image.rect().contains(mods.crop);
	if (mods.paint && !expanded) {
		if (image.format() != QImage::Format_ARGB32_Premultiplied) {
			image = image.convertToFormat(
				QImage::Format_ARGB32_Premultiplied);
		}

		Painter p(&image);
		PainterHighQualityEnabler hq(p);

		mods.paint->render(&p, image.rect());
	}
	auto cropped = expanded
		? ExpandCanvas(image, mods.crop, mods.paint.get())
		: mods.crop.isValid()
		? image.copy(mods.crop)
		: image;
	QTransform transform;
	if (mods.flipped) {
		transform.scale(-1, 1);
	}
	if (mods.angle) {
		transform.rotate(mods.angle);
	}
	auto result = cropped.transformed(transform);
	if (expanded) {
		const auto matrix = QImage::trueMatrix(
			transform,
			cropped.width(),
			cropped.height());
		FillCanvasBackground(
			result,
			Media::Encode::DominantCanvasBackground(image),
			matrix.mapRect(QRect(-mods.crop.topLeft(), image.size())));
	}
	return result;
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

	const auto bake = [&](const QImage &canvas) {
		return canvas.transformed(transform, Qt::SmoothTransformation)
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
			crop.size(),
			QImage::Format_ARGB32_Premultiplied);
		layer.fill(Qt::transparent);
		{
			auto p = Painter(&layer);
			PainterHighQualityEnabler hq(p);
			scene->render(&p, QRectF(layer.rect()), QRectF(crop));
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
		const auto clip = item->videoClip();
		const auto loop = animated->loopDuration();
		if (clip && (loop > 0)) {
			entity.till = entity.from + loop;
		}
		if (clip && clip->sounding()) {
			const auto trim = clip->trim();
			music.push_back({
				.bytes = entity.bytes,
				.from = trim.from,
				.till = trim.from + loop,
				.volume = clip->volume(),
				.loop = true,
			});
		}
		job.overlay.push_back(std::move(entity));
		longest = std::max(longest, loop);
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
	auto base = bake(ExpandCanvas(image, crop));
	FillCanvasBackground(
		base,
		Media::Encode::DominantCanvasBackground(image));
	job.source = Media::Encode::StillSource{
		.base = std::move(base),
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
