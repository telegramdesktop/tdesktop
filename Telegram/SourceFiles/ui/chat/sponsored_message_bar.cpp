/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/sponsored_message_bar.h"

#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/components/sponsored_messages.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/image/image_prepare.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "ui/widgets/shadow.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace Ui {

void FillSponsoredMessageBar(
		not_null<RpWidget*> widget,
		not_null<Main::Session*> session,
		FullMsgId fullId,
		Data::SponsoredFrom from,
		const TextWithEntities &textWithEntities) {
	struct State final {
		Ui::Text::String title;
		Ui::Text::String contentTitle;
		Ui::Text::String contentText;
		rpl::variable<int> lastPaintedContentLineAmount = 0;
		rpl::variable<int> lastPaintedContentTop = 0;

		std::shared_ptr<Ui::DynamicImage> rightPhoto;
		QImage rightPhotoImage;
	};
	const auto state = widget->lifetime().make_state<State>();
	state->title.setText(
		st::semiboldTextStyle,
		from.isRecommended
			? tr::lng_recommended_message_title(tr::now)
			: tr::lng_sponsored_message_title(tr::now));
	state->contentTitle.setText(st::semiboldTextStyle, from.title);
	state->contentText.setMarkedText(
		st::defaultTextStyle,
		textWithEntities,
		kMarkupTextOptions,
		Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [=] { widget->update(); },
		});
	const auto kLinesForPhoto = 3;
	const auto rightPhotoSize = state->title.style()->font->ascent
		* kLinesForPhoto;
	const auto rightPhotoPlaceholder = state->title.style()->font->height
		* kLinesForPhoto;
	const auto hasRightPhoto = from.photoId > 0;
	if (hasRightPhoto) {
		state->rightPhoto = Ui::MakePhotoThumbnail(
			session->data().photo(from.photoId),
			fullId);
		const auto callback = [=] {
			state->rightPhotoImage = Images::Round(
				state->rightPhoto->image(rightPhotoSize),
				ImageRoundRadius::Small);
			widget->update();
		};
		state->rightPhoto->subscribeToUpdates(callback);
		callback();
	}
	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(widget);
		const auto r = widget->rect();
		p.fillRect(r, st::historyPinnedBg);
		const auto leftPadding = st::msgReplyBarSkip + st::msgReplyBarSkip;
		const auto rightPadding = st::msgReplyBarSkip;
		const auto topPadding = st::msgReplyPadding.top();
		const auto availableWidthNoPhoto = r.width()
			- leftPadding
			- rightPadding;
		const auto availableWidth = availableWidthNoPhoto
			- (hasRightPhoto ? (rightPadding + rightPhotoSize) : 0);
		const auto titleRight = leftPadding
			+ state->title.maxWidth()
			+ state->title.style()->font->spacew * 2;
		const auto hasSecondLineTitle
			= (availableWidth - state->contentTitle.maxWidth() < titleRight);
		p.setPen(st::windowActiveTextFg);
		state->title.draw(p, {
			.position = QPoint(leftPadding, topPadding),
			.outerWidth = availableWidth,
			.availableWidth = availableWidth,
		});
		p.setPen(st::windowFg);
		{
			const auto left = hasSecondLineTitle ? leftPadding : titleRight;
			const auto top = hasSecondLineTitle
				? (topPadding + state->title.style()->font->height)
				: topPadding;
			state->contentTitle.draw(p, {
				.position = QPoint(left, top),
				.outerWidth = hasSecondLineTitle
					? availableWidth
					: (availableWidth - titleRight),
				.availableWidth = availableWidth,
				.elisionLines = 1,
			});
		}
		{
			const auto left = leftPadding;
			const auto top = hasSecondLineTitle
				? (topPadding
					+ state->title.style()->font->height
					+ state->contentTitle.style()->font->height)
				: topPadding + state->title.style()->font->height;
			auto lastContentLineAmount = 0;
			const auto lineHeight = state->contentText.style()->font->height;
			const auto lineLayout = [&](int line) -> Ui::Text::LineGeometry {
				line++;
				lastContentLineAmount = line;
				const auto diff = (st::sponsoredMessageBarMaxHeight)
					- line * lineHeight;
				if (diff < 3 * lineHeight) {
					return {
						.width = availableWidthNoPhoto,
						.elided = true,
					};
				} else if (diff < 2 * lineHeight) {
					return {};
				}
				if (hasRightPhoto) {
					line += (hasSecondLineTitle ? 2 : 1);
					return {
						.width = (line > kLinesForPhoto)
							? availableWidthNoPhoto
							: availableWidth,
					};
				} else {
					return { .width = availableWidth };
				}
			};
			state->contentText.draw(p, {
				.position = QPoint(left, top),
				.outerWidth = availableWidth,
				.availableWidth = availableWidth,
				.geometry = Ui::Text::GeometryDescriptor{
					.layout = std::move(lineLayout),
				},
			});
			state->lastPaintedContentTop = top;
			state->lastPaintedContentLineAmount = lastContentLineAmount;
		}
		if (hasRightPhoto) {
			p.drawImage(
				r.width() - rightPadding - rightPhotoSize,
				topPadding + (rightPhotoPlaceholder - rightPhotoSize) / 2,
				state->rightPhotoImage);
		}
	}, widget->lifetime());
	rpl::combine(
		state->lastPaintedContentTop.value(),
		state->lastPaintedContentLineAmount.value()
	) | rpl::distinct_until_changed() | rpl::start_with_next([=](
			int lastTop,
			int lastLines) {
		const auto bottomPadding = st::msgReplyPadding.top();
		const auto desiredHeight = lastTop
			+ (lastLines * state->contentText.style()->font->height)
			+ bottomPadding;
		const auto minHeight = hasRightPhoto
			? (rightPhotoPlaceholder + bottomPadding * 2)
			: desiredHeight;
		widget->resize(
			widget->width(),
			std::clamp(
				desiredHeight,
				minHeight,
				st::sponsoredMessageBarMaxHeight));
	}, widget->lifetime());
	widget->resize(widget->width(), 1);

	{
		const auto top = Ui::CreateChild<PlainShadow>(widget);
		const auto bottom = Ui::CreateChild<PlainShadow>(widget);
		widget->sizeValue() | rpl::start_with_next([=] (const QSize &s) {
			top->show();
			top->raise();
			top->resizeToWidth(s.width());
			bottom->show();
			bottom->raise();
			bottom->resizeToWidth(s.width());
			bottom->moveToLeft(0, s.height() - bottom->height());
		}, top->lifetime());
	}
}

} // namespace Ui
