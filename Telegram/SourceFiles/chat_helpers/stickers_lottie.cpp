/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_lottie.h"

#include "lottie/lottie_single_player.h"
#include "lottie/lottie_multi_player.h"
#include "data/stickers/data_stickers_set.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "storage/cache/storage_cache_database.h"
#include "ui/effects/path_shift_gradient.h"
#include "main/main_session.h"

namespace ChatHelpers {
namespace {

constexpr auto kDontCacheLottieAfterArea = 512 * 512;

} // namespace

template <typename Method>
auto LottieCachedFromContent(
		Method &&method,
		Storage::Cache::Key baseKey,
		uint8 keyShift,
		not_null<Main::Session*> session,
		const QByteArray &content,
		QSize box) {
	const auto key = Storage::Cache::Key{
		baseKey.high,
		baseKey.low + keyShift
	};
	const auto get = [=](FnMut<void(QByteArray &&cached)> handler) {
		session->data().cacheBigFile().get(
			key,
			std::move(handler));
	};
	const auto weak = base::make_weak(session.get());
	const auto put = [=](QByteArray &&cached) {
		crl::on_main(weak, [=, data = std::move(cached)]() mutable {
			weak->data().cacheBigFile().put(key, std::move(data));
		});
	};
	return method(
		get,
		put,
		content,
		Lottie::FrameRequest{ box });
}

template <typename Method>
auto LottieFromDocument(
		Method &&method,
		not_null<Data::DocumentMedia*> media,
		uint8 keyShift,
		QSize box) {
	const auto document = media->owner();
	const auto data = media->bytes();
	const auto filepath = document->filepath();
	if (box.width() * box.height() > kDontCacheLottieAfterArea) {
		// Don't use frame caching for large stickers.
		return method(
			Lottie::ReadContent(data, filepath),
			Lottie::FrameRequest{ box });
	}
	if (const auto baseKey = document->bigFileBaseCacheKey()) {
		return LottieCachedFromContent(
			std::forward<Method>(method),
			baseKey,
			keyShift,
			&document->session(),
			Lottie::ReadContent(data, filepath),
			box);
	}
	return method(
		Lottie::ReadContent(data, filepath),
		Lottie::FrameRequest{ box });
}

std::unique_ptr<Lottie::SinglePlayer> LottiePlayerFromDocument(
		not_null<Data::DocumentMedia*> media,
		StickerLottieSize sizeTag,
		QSize box,
		Lottie::Quality quality,
		std::shared_ptr<Lottie::FrameRenderer> renderer) {
	return LottiePlayerFromDocument(
		media,
		nullptr,
		sizeTag,
		box,
		quality,
		std::move(renderer));
}

std::unique_ptr<Lottie::SinglePlayer> LottiePlayerFromDocument(
		not_null<Data::DocumentMedia*> media,
		const Lottie::ColorReplacements *replacements,
		StickerLottieSize sizeTag,
		QSize box,
		Lottie::Quality quality,
		std::shared_ptr<Lottie::FrameRenderer> renderer) {
	const auto method = [&](auto &&...args) {
		return std::make_unique<Lottie::SinglePlayer>(
			std::forward<decltype(args)>(args)...,
			quality,
			replacements,
			std::move(renderer));
	};
	const auto tag = replacements ? replacements->tag : uint8(0);
	const auto keyShift = ((tag << 4) & 0xF0) | (uint8(sizeTag) & 0x0F);
	return LottieFromDocument(method, media, uint8(keyShift), box);
}

not_null<Lottie::Animation*> LottieAnimationFromDocument(
		not_null<Lottie::MultiPlayer*> player,
		not_null<Data::DocumentMedia*> media,
		StickerLottieSize sizeTag,
		QSize box) {
	const auto method = [&](auto &&...args) {
		return player->append(std::forward<decltype(args)>(args)...);
	};
	return LottieFromDocument(method, media, uint8(sizeTag), box);
}

bool HasLottieThumbnail(
		Data::StickersSetThumbnailView *thumb,
		Data::DocumentMedia *media) {
	if (thumb) {
		return !thumb->content().isEmpty();
	} else if (!media) {
		return false;
	}
	const auto document = media->owner();
	if (const auto info = document->sticker()) {
		if (!info->animated) {
			return false;
		}
		media->automaticLoad(document->stickerSetOrigin(), nullptr);
		if (!media->loaded()) {
			return false;
		}
		return document->bigFileBaseCacheKey().valid();
	}
	return false;
}

std::unique_ptr<Lottie::SinglePlayer> LottieThumbnail(
		Data::StickersSetThumbnailView *thumb,
		Data::DocumentMedia *media,
		StickerLottieSize sizeTag,
		QSize box,
		std::shared_ptr<Lottie::FrameRenderer> renderer) {
	const auto baseKey = thumb
		? thumb->owner()->thumbnailLocation().file().bigFileBaseCacheKey()
		: media
		? media->owner()->bigFileBaseCacheKey()
		: Storage::Cache::Key();
	if (!baseKey) {
		return nullptr;
	}
	const auto content = thumb
		? thumb->content()
		: Lottie::ReadContent(media->bytes(), media->owner()->filepath());
	if (content.isEmpty()) {
		return nullptr;
	}
	const auto method = [](auto &&...args) {
		return std::make_unique<Lottie::SinglePlayer>(
			std::forward<decltype(args)>(args)...);
	};
	const auto session = thumb
		? &thumb->owner()->session()
		: media
		? &media->owner()->session()
		: nullptr;
	return LottieCachedFromContent(
		method,
		baseKey,
		uint8(sizeTag),
		session,
		content,
		box);
}

bool PaintStickerThumbnailPath(
		QPainter &p,
		not_null<Data::DocumentMedia*> media,
		QRect target,
		QLinearGradient *gradient) {
	const auto &path = media->thumbnailPath();
	const auto dimensions = media->owner()->dimensions;
	if (path.isEmpty() || dimensions.isEmpty() || target.isEmpty()) {
		return false;
	}
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.translate(target.topLeft());
	if (gradient) {
		const auto scale = dimensions.width() / float64(target.width());
		const auto shift = p.worldTransform().dx();
		gradient->setStart((gradient->start().x() - shift) * scale, 0);
		gradient->setFinalStop(
			(gradient->finalStop().x() - shift) * scale,
			0);
		p.setBrush(*gradient);
	}
	p.scale(
		target.width() / float64(dimensions.width()),
		target.height() / float64(dimensions.height()));
	p.drawPath(path);
	p.restore();
	return true;
}

bool PaintStickerThumbnailPath(
		QPainter &p,
		not_null<Data::DocumentMedia*> media,
		QRect target,
		not_null<Ui::PathShiftGradient*> gradient) {
	return gradient->paint([&](const Ui::PathShiftGradient::Background &bg) {
		if (const auto color = std::get_if<style::color>(&bg)) {
			p.setBrush(*color);
			return PaintStickerThumbnailPath(p, media, target);
		}
		const auto gradient = v::get<QLinearGradient*>(bg);
		return PaintStickerThumbnailPath(p, media, target, gradient);
	});
}

} // namespace ChatHelpers
