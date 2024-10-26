/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/sponsored_message_bar.h"

#include "core/application.h"
#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/components/sponsored_messages.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image_prepare.h"
#include "ui/rect.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace {

struct Colors final {
	QColor bg;
	QColor fg;
};

using ColorFactory = Fn<Colors()>;

class RemoveButton final : public Ui::RippleButton {
public:
	RemoveButton(
		not_null<Ui::RpWidget*> parent,
		ColorFactory cache)
	: Ui::RippleButton(parent, st::defaultRippleAnimation) {
		tr::lng_sponsored_top_bar_hide(
		) | rpl::start_with_next([this](const QString &t) {
			const auto height = st::stickersHeaderBadgeFont->height;
			resize(
				st::stickersHeaderBadgeFont->width(t) + height,
				height);
			update();
		}, lifetime());
		paintRequest() | rpl::start_with_next([this, cache] {
			auto p = QPainter(this);
			const auto colors = cache();
			const auto r = rect();
			const auto rippleColor = anim::with_alpha(colors.fg, .15);
			Ui::RippleButton::paintRipple(
				p,
				QPoint(),
				&rippleColor);
			p.setBrush(colors.bg);
			p.setPen(Qt::NoPen);
			p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
			p.setFont(st::stickersHeaderBadgeFont);
			p.setPen(colors.fg);
			p.drawText(
				r,
				tr::lng_sponsored_top_bar_hide(tr::now),
				style::al_center);
		}, lifetime());
	}

	QImage prepareRippleMask() const override {
		return Ui::RippleAnimation::RoundRectMask(size(), height() / 2);
	}

};

[[nodiscard]] ColorFactory GenerateReplyColorCallback(
		not_null<RpWidget*> widget,
		FullMsgId fullId,
		int colorIndex) {
	const auto window = Core::App().findWindow(widget);
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return [] -> Colors {
			return { st::windowBgActive->c, st::windowActiveTextFg->c };
		};
	}
	const auto peer = controller->session().data().peer(fullId.peer);
	struct State final {
		std::shared_ptr<Ui::ChatTheme> theme;
	};
	const auto state = widget->lifetime().make_state<State>();
	Window::ChatThemeValueFromPeer(
		controller,
		peer
	) | rpl::start_with_next([=](std::shared_ptr<Ui::ChatTheme> &&theme) {
		state->theme = std::move(theme);
	}, widget->lifetime());

	return [=] -> Colors {
		if (!state->theme) {
			return { st::windowBgActive->c, st::windowActiveTextFg->c };
		}
		const auto context = controller->preparePaintContext({
			.theme = state->theme.get(),
		});
		const auto selected = false;
		const auto cache = context.st->coloredReplyCache(
			selected,
			colorIndex);
		return { cache->bg, cache->icon };
	};
}

} // namespace

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
	const auto &titleSt = st::semiboldTextStyle;
	const auto &contentTitleSt = st::semiboldTextStyle;
	const auto &contentTextSt = st::defaultTextStyle;
	state->title.setText(
		titleSt,
		from.isRecommended
			? tr::lng_recommended_message_title(tr::now)
			: tr::lng_sponsored_message_title(tr::now));
	state->contentTitle.setText(contentTitleSt, from.title);
	state->contentText.setMarkedText(
		contentTextSt,
		textWithEntities,
		kMarkupTextOptions,
		Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [=] { widget->update(); },
		});
	const auto kLinesForPhoto = 3;
	const auto rightPhotoSize = titleSt.font->ascent
		* kLinesForPhoto;
	const auto rightPhotoPlaceholder = titleSt.font->height
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
	const auto removeButton = Ui::CreateChild<RemoveButton>(
		widget,
		GenerateReplyColorCallback(
			widget,
			fullId,
			from.colorIndex ? from.colorIndex : 4/*blue*/));
	removeButton->show();

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
			+ titleSt.font->spacew * 2;
		const auto hasSecondLineTitle = (titleRight
			> (availableWidth
				- state->contentTitle.maxWidth()
				- removeButton->width()));
		p.setPen(st::windowActiveTextFg);
		state->title.draw(p, {
			.position = QPoint(leftPadding, topPadding),
			.outerWidth = availableWidth,
			.availableWidth = availableWidth,
		});
		removeButton->moveToLeft(
			hasSecondLineTitle
				? titleRight
				: std::min(
					titleRight
						+ state->contentTitle.maxWidth()
						+ titleSt.font->spacew * 2,
					r.width()
						- (hasRightPhoto
							? (rightPadding + rightPhotoSize)
							: 0)
						- rightPadding),
			topPadding
				+ (titleSt.font->height - removeButton->height()) / 2);
		p.setPen(st::windowFg);
		{
			const auto left = hasSecondLineTitle ? leftPadding : titleRight;
			const auto top = hasSecondLineTitle
				? (topPadding + titleSt.font->height)
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
					+ titleSt.font->height
					+ contentTitleSt.font->height)
				: topPadding + titleSt.font->height;
			auto lastContentLineAmount = 0;
			const auto lineHeight = contentTextSt.font->height;
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
			+ (lastLines * contentTextSt.font->height)
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
