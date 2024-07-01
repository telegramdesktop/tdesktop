/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_common.h"

#include "api/api_views.h"
#include "apiwrap.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "core/click_handler_types.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_wall_paper.h"
#include "data/data_media_types.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media_grouped.h"
#include "history/view/media/history_view_photo.h"
#include "history/view/media/history_view_gif.h"
#include "history/view/media/history_view_document.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_theme_document.h"
#include "history/history_item.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "media/streaming/media_streaming_utility.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_non_panel_process.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kMediaUnlockedTooltipDuration = 5 * crl::time(1000);

} // namespace

void PaintInterpolatedIcon(
		QPainter &p,
		const style::icon &a,
		const style::icon &b,
		float64 b_ratio,
		QRect rect) {
	PainterHighQualityEnabler hq(p);
	p.save();
	p.translate(rect.center());
	p.setOpacity(b_ratio);
	p.scale(b_ratio, b_ratio);
	b.paintInCenter(p, rect.translated(-rect.center()));
	p.restore();

	p.save();
	p.translate(rect.center());
	p.setOpacity(1. - b_ratio);
	p.scale(1. - b_ratio, 1. - b_ratio);
	a.paintInCenter(p, rect.translated(-rect.center()));
	p.restore();
}

std::unique_ptr<Media> CreateAttach(
		not_null<Element*> parent,
		DocumentData *document,
		PhotoData *photo) {
	return CreateAttach(parent, document, photo, {}, {});
}

std::unique_ptr<Media> CreateAttach(
		not_null<Element*> parent,
		DocumentData *document,
		PhotoData *photo,
		const std::vector<std::unique_ptr<Data::Media>> &collage,
		const QString &webpageUrl) {
	if (!collage.empty()) {
		return std::make_unique<GroupedMedia>(parent, collage);
	} else if (document) {
		const auto spoiler = false;
		if (document->sticker()) {
			const auto skipPremiumEffect = true;
			return std::make_unique<UnwrappedMedia>(
				parent,
				std::make_unique<Sticker>(
					parent,
					document,
					skipPremiumEffect));
		} else if (document->isAnimation() || document->isVideoFile()) {
			return std::make_unique<Gif>(
				parent,
				parent->data(),
				document,
				spoiler);
		} else if (document->isWallPaper() || document->isTheme()) {
			return std::make_unique<ThemeDocument>(
				parent,
				document,
				ThemeDocument::ParamsFromUrl(webpageUrl));
		}
		return std::make_unique<Document>(parent, parent->data(), document);
	} else if (photo) {
		const auto spoiler = false;
		return std::make_unique<Photo>(
			parent,
			parent->data(),
			photo,
			spoiler);
	} else if (const auto params = ThemeDocument::ParamsFromUrl(webpageUrl)) {
		return std::make_unique<ThemeDocument>(parent, nullptr, params);
	}
	return nullptr;
}

int UnitedLineHeight() {
	return std::max(st::semiboldFont->height, st::normalFont->height);
}

QImage PrepareWithBlurredBackground(
		QSize outer,
		::Media::Streaming::ExpandDecision resize,
		Image *large,
		Image *blurred) {
	return PrepareWithBlurredBackground(
		outer,
		resize,
		large ? large->original() : QImage(),
		blurred ? blurred->original() : QImage());
}

QImage PrepareWithBlurredBackground(
		QSize outer,
		::Media::Streaming::ExpandDecision resize,
		QImage large,
		QImage blurred) {
	const auto ratio = style::DevicePixelRatio();
	if (resize.expanding) {
		return Images::Prepare(std::move(large), resize.result * ratio, {
			.outer = outer,
		});
	}
	auto background = QImage(
		outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
	background.setDevicePixelRatio(ratio);
	if (blurred.isNull()) {
		background.fill(Qt::black);
		if (large.isNull()) {
			return background;
		}
	}
	auto p = QPainter(&background);
	if (!blurred.isNull()) {
		using namespace ::Media::Streaming;
		FillBlurredBackground(p, outer, std::move(blurred));
	}
	if (!large.isNull()) {
		auto image = large.scaled(
			resize.result * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		image.setDevicePixelRatio(ratio);
		p.drawImage(
			(outer.width() - resize.result.width()) / 2,
			(outer.height() - resize.result.height()) / 2,
			image);
	}
	p.end();
	return background;
}

QSize CountDesiredMediaSize(QSize original) {
	return DownscaledSize(
		style::ConvertScale(original),
		{ st::maxMediaSize, st::maxMediaSize });
}

QSize CountMediaSize(QSize desired, int newWidth) {
	Expects(!desired.isEmpty());

	return (desired.width() <= newWidth)
		? desired
		: NonEmptySize(
			desired.scaled(newWidth, desired.height(), Qt::KeepAspectRatio));
}

QSize CountPhotoMediaSize(
		QSize desired,
		int newWidth,
		int maxWidth) {
	const auto media = CountMediaSize(desired, qMin(newWidth, maxWidth));
	return (media.height() <= newWidth)
		? media
		: NonEmptySize(
			media.scaled(media.width(), newWidth, Qt::KeepAspectRatio));
}

void ShowPaidMediaUnlockedToast(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto invoice = media ? media->invoice() : nullptr;
	if (!invoice || !invoice->isPaidMedia) {
		return;
	}
	const auto sender = item->originalSender();
	const auto broadcast = (sender && sender->isBroadcast())
		? sender
		: item->history()->peer.get();
	auto text = tr::lng_credits_media_done_title(
		tr::now,
		Ui::Text::Bold
	).append('\n').append(tr::lng_credits_media_done_text(
		tr::now,
		lt_count,
		invoice->amount,
		lt_chat,
		Ui::Text::Bold(broadcast->name()),
		Ui::Text::RichLangValue));
	controller->showToast(std::move(text), kMediaUnlockedTooltipDuration);
}

ClickHandlerPtr MakePaidMediaLink(not_null<HistoryItem*> item) {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		const auto weak = my.sessionWindow;
		const auto itemId = item->fullId();
		const auto session = &item->history()->session();
		using Result = Payments::CheckoutResult;
		const auto done = crl::guard(session, [=](Result result) {
			if (result != Result::Paid) {
				return;
			} else if (const auto item = session->data().message(itemId)) {
				session->api().views().pollExtendedMedia(item, true);
				if (const auto strong = weak.get()) {
					ShowPaidMediaUnlockedToast(strong, item);
				}
			}
		});
		Payments::CheckoutProcess::Start(
			item,
			Payments::Mode::Payment,
			(controller
				? crl::guard(
					controller,
					[=](auto) { controller->widget()->activate(); })
				: Fn<void(Payments::CheckoutResult)>()),
			((controller && Payments::IsCreditsInvoice(item))
				? Payments::ProcessNonPanelPaymentFormFactory(
					controller,
					done)
				: nullptr));
	});
}

} // namespace HistoryView
