/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/sticker_preview_box.h"

#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/layers/generic_box.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/padding_wrap.h"
#include "settings/settings_premium.h"
#include "lottie/lottie_single_player.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/history_view_element.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_chat_helpers.h"

namespace {

constexpr auto kPremiumShift = 0.082;

struct Preload {
	not_null<DocumentData*> document;
	std::shared_ptr<Data::DocumentMedia> media;
	base::weak_ptr<Window::SessionController> controller;
};

[[nodiscard]] std::vector<Preload> &Preloads() {
	static auto result = std::vector<Preload>();
	return result;
}

void PreloadSticker(const std::shared_ptr<Data::DocumentMedia> &media) {
	const auto origin = media->owner()->stickerSetOrigin();
	media->automaticLoad(origin, nullptr);
	media->videoThumbnailWanted(origin);
}

[[nodiscard]] object_ptr<Ui::RpWidget> StickerPreview(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const std::shared_ptr<Data::DocumentMedia> &media,
		const QImage &back,
		QSize size) {
	auto result = object_ptr<Ui::FixedHeightWidget>(parent, size.height());
	const auto raw = result.data();
	auto &lifetime = raw->lifetime();

	struct State {
		std::unique_ptr<Lottie::SinglePlayer> lottie;
		std::unique_ptr<Lottie::SinglePlayer> effect;
		std::unique_ptr<Ui::PathShiftGradient> pathGradient;
	};
	const auto state = lifetime.make_state<State>();

	using namespace HistoryView;
	const auto document = media->owner();
	const auto lottieSize = Sticker::Size(document);
	const auto effectSize = Sticker::PremiumEffectSize(document);
	const auto createLottieIfReady = [=] {
		if (state->lottie) {
			return;
		}
		const auto document = media->owner();
		const auto sticker = document->sticker();
		if (!sticker || !sticker->isLottie() || !media->loaded()) {
			return;
		} else if (media->videoThumbnailContent().isEmpty()) {
			return;
		}

		const auto factor = style::DevicePixelRatio();
		state->lottie = ChatHelpers::LottiePlayerFromDocument(
			media.get(),
			nullptr,
			ChatHelpers::StickerLottieSize::MessageHistory,
			lottieSize * factor,
			Lottie::Quality::High);
		state->effect = document->session().emojiStickersPack().effectPlayer(
			document,
			media->videoThumbnailContent(),
			QString(),
			true);

		const auto update = [=] { raw->update(); };
		auto &lifetime = raw->lifetime();
		state->lottie->updates() | rpl::start_with_next(update, lifetime);
		state->effect->updates() | rpl::start_with_next(update, lifetime);
	};
	state->pathGradient = MakePathShiftGradient(
		controller->chatStyle(),
		[=] { raw->update(); });

	raw->paintRequest(
	) | rpl::start_with_next([=] {
		createLottieIfReady();

		auto p = QPainter(raw);
		p.drawImage(0, 0, back);

		const auto zero = (size.width() - effectSize.width()) / 2;
		const auto left = zero
			+ effectSize.width()
			- int(lottieSize.width() * (1. + kPremiumShift));
		const auto top = (effectSize.height() - lottieSize.height()) / 2;
		const auto r = QRect(QPoint(left, top), lottieSize);
		if (!state->lottie
			|| !state->lottie->ready()
			|| !state->effect->ready()) {
			p.setBrush(controller->chatStyle()->msgServiceBg());
			ChatHelpers::PaintStickerThumbnailPath(
				p,
				media.get(),
				r,
				state->pathGradient.get());
			return;
		}

		const auto factor = style::DevicePixelRatio();
		const auto frame = state->lottie->frameInfo({ lottieSize * factor });
		const auto effect = state->effect->frameInfo(
			{ effectSize * factor });
		//const auto framesCount = !frame.image.isNull()
		//	? state->lottie->framesCount()
		//	: 1;
		//const auto effectsCount = !effect.image.isNull()
		//	? state->effect->framesCount()
		//	: 1;

		p.drawImage(r, frame.image);
		p.drawImage(QRect(QPoint(zero, 0), effectSize), effect.image);

		if (!frame.image.isNull()/*
			&& ((frame.index % effectsCount) <= effect.index)*/) {
			state->lottie->markFrameShown();
		}
		if (!effect.image.isNull()/*
			&& ((effect.index % framesCount) <= frame.index)*/) {
			state->effect->markFrameShown();
		}
	}, lifetime);

	return result;
}

[[nodiscard]] object_ptr<Ui::AbstractButton> CreateGradientButton(
		QWidget *parent,
		QGradientStops stops) {
	return object_ptr<Ui::GradientButton>(parent, std::move(stops));
}

[[nodiscard]] object_ptr<Ui::AbstractButton> CreatePremiumButton(
		QWidget *parent) {
	return CreateGradientButton(parent, Ui::Premium::ButtonGradientStops());
}

[[nodiscard]] object_ptr<Ui::AbstractButton> CreateUnlockButton(
		QWidget *parent,
		int width) {
	auto result = CreatePremiumButton(parent);
	const auto &st = st::premiumPreviewBox.button;
	result->resize(width, st.height);

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		result.data(),
		tr::lng_sticker_premium_button(),
		st::premiumPreviewButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		result->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=](int outer, int width) {
		label->moveToLeft(
			(outer - width) / 2,
			st::premiumPreviewBox.button.textTop,
			outer);
	}, label->lifetime());

	return result;
}

void StickerBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const std::shared_ptr<Data::DocumentMedia> &media,
		const QImage &back) {
	const auto size = QSize(
		st::boxWideWidth,
		HistoryView::Sticker::PremiumEffectSize(media->owner()).height());
	box->setWidth(size.width());
	box->setNoContentMargin(true);
	box->addRow(StickerPreview(box, controller, media, back, size), {});
	const auto padding = st::premiumPreviewAboutPadding;
	auto label = object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_sticker_premium_about(),
		st::premiumPreviewAbout);
	label->resizeToWidth(size.width() - padding.left() - padding.right());
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			box,
			std::move(label)),
		padding);
	box->setStyle(st::premiumPreviewBox);
	const auto buttonPadding = st::premiumPreviewBox.buttonPadding;
	const auto width = size.width()
		- buttonPadding.left()
		- buttonPadding.right();
	auto button = CreateUnlockButton(box, width);
	button->setClickedCallback([=] {
		Settings::ShowPremium(controller, "premium_stickers");
	});
	box->addButton(std::move(button));
}

void Show(
		not_null<Window::SessionController*> controller,
		const std::shared_ptr<Data::DocumentMedia> &media,
		QImage back) {
	controller->show(Box(StickerBox, controller, media, back));
}

void Show(not_null<Window::SessionController*> controller, QImage back) {
	auto &list = Preloads();
	for (auto i = begin(list); i != end(list);) {
		const auto already = i->controller.get();
		if (!already) {
			i = list.erase(i);
		} else if (already == controller) {
			Show(controller, i->media, back);
			i = list.erase(i);
			return;
		} else {
			++i;
		}
	}
}

[[nodiscard]] QImage SolidColorImage(QSize size, QColor color) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(color);
	return result;
}

} // namespace

void ShowStickerPreviewBox(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document) {
	auto &list = Preloads();
	for (auto i = begin(list); i != end(list);) {
		const auto already = i->controller.get();
		if (!already) {
			i = list.erase(i);
		} else if (already == controller) {
			if (i->document == document) {
				return;
			}
			i->document = document;
			i->media = document->createMediaView();
			PreloadSticker(i->media);
			return;
		} else {
			++i;
		}
	}

	const auto weak = base::make_weak(controller.get());
	list.push_back({
		.document = document,
		.media = document->createMediaView(),
		.controller = weak,
	});
	PreloadSticker(list.back().media);

	const auto fill = QSize(st::boxWideWidth, st::boxWideWidth);
	const auto theme = controller->currentChatTheme();
	const auto color = theme->background().colorForFill;
	const auto area = QSize(fill.width(), fill.height() * 2);
	const auto request = theme->cacheBackgroundRequest(area);
	crl::async([=] {
		using Option = Images::Option;
		auto back = color
			? SolidColorImage(area, *color)
			: request.background.waitingForNegativePattern()
			? SolidColorImage(area, Qt::black)
			: Ui::CacheBackground(request).image;
		const auto factor = style::DevicePixelRatio();
		auto cropped = back.copy(QRect(
			QPoint(0, fill.height() * factor / 2),
			fill * factor));
		cropped.setDevicePixelRatio(factor);
		const auto options = Images::Options()
			| Option::RoundSkipBottomLeft
			| Option::RoundSkipBottomRight
			| Option::RoundLarge;
		const auto result = Images::Round(
			std::move(cropped),
			Images::CornersMask(st::boxRadius),
			RectPart::TopLeft | RectPart::TopRight);
		crl::on_main([=] {
			if (const auto strong = weak.get()) {
				Show(strong, result);
			}
		});
	});
}
