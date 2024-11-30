/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/sponsored_message_bar.h"

#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/components/sponsored_messages.h"
#include "data/data_session.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_sponsored.h"
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
#include "styles/style_dialogs.h"

namespace Ui {
namespace {

struct Colors final {
	QColor bg;
	QColor fg;
};

using ColorFactory = Fn<Colors()>;

class BadgeButton final : public Ui::RippleButton {
public:
	BadgeButton(
		not_null<Ui::RpWidget*> parent,
		tr::phrase<> text,
		ColorFactory cache)
	: Ui::RippleButton(parent, st::defaultRippleAnimation) {
		text(
		) | rpl::start_with_next([this](const QString &t) {
			const auto height = st::stickersHeaderBadgeFont->height;
			resize(
				st::stickersHeaderBadgeFont->width(t) + height,
				height);
			update();
		}, lifetime());
		paintRequest() | rpl::start_with_next([this, cache, text] {
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
			p.drawText(r, text(tr::now), style::al_center);
		}, lifetime());
	}

	QImage prepareRippleMask() const override {
		return Ui::RippleAnimation::RoundRectMask(size(), height() / 2);
	}

};

[[nodiscard]] Window::SessionController *FindSessionController(
		not_null<RpWidget*> widget) {
	const auto window = Core::App().findWindow(widget);
	return window ? window->sessionController() : nullptr;
}

[[nodiscard]] ColorFactory GenerateReplyColorCallback(
		not_null<Window::SessionController*> controller,
		not_null<RpWidget*> widget,
		FullMsgId fullId,
		int colorIndex) {
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

	return [=]() -> Colors {
		if (!state->theme) {
			return {
				anim::with_alpha(st::windowBgActive->c, .15),
				st::windowActiveTextFg->c,
			};
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

[[nodiscard]] ColorFactory GenerateReplyColorCallback(
		not_null<RpWidget*> widget,
		FullMsgId fullId,
		int colorIndex) {
	if (const auto window = FindSessionController(widget)) {
		return GenerateReplyColorCallback(window, widget, fullId, colorIndex);
	}
	const auto window
		= widget->lifetime().make_state<Window::SessionController*>();
	const auto callback = widget->lifetime().make_state<ColorFactory>();
	return [=, color = colorIndex]() -> Colors {
		if (*callback) {
			return (*callback)();
		}
		*window = FindSessionController(widget);
		if (const auto w = (*window)) {
			*callback = GenerateReplyColorCallback(w, widget, fullId, color);
			return (*callback)();
		} else {
			return {
				anim::with_alpha(st::windowBgActive->c, .15),
				st::windowActiveTextFg->c,
			};
		}
	};
}

} // namespace

void FillSponsoredMessageBar(
		not_null<RpWidget*> container,
		not_null<Main::Session*> session,
		FullMsgId fullId,
		Data::SponsoredFrom from,
		const TextWithEntities &textWithEntities) {
	const auto widget = CreateSimpleRectButton(
		container,
		st::defaultRippleAnimationBgOver);
	widget->show();
	container->sizeValue() | rpl::start_with_next([=](const QSize &s) {
		widget->resize(s);
	}, widget->lifetime());
	widget->setAcceptBoth();

	widget->addClickHandler([=](Qt::MouseButton button) {
		if (button == Qt::RightButton) {
			if (const auto controller = FindSessionController(widget)) {
				::Menu::ShowSponsored(widget, controller->uiShow(), fullId);
			}
		} else if (button == Qt::LeftButton) {
			session->sponsoredMessages().clicked(fullId, false, false);
			UrlClickHandler::Open(from.link);
		}
	});

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
	const auto hostedClick = [=](ClickHandlerPtr handler) {
		return [=] {
			if (const auto controller = FindSessionController(widget)) {
				ActivateClickHandler(widget, handler, {
					.other = QVariant::fromValue(ClickHandlerContext{
						.itemId = fullId,
						.sessionWindow = base::make_weak(controller),
						.show = controller->uiShow(),
					})
				});
			}
		};
	};
	const auto kLinesForPhoto = 3;
	const auto rightPhotoSize = titleSt.font->ascent * kLinesForPhoto;
	const auto rightPhotoPlaceholder = titleSt.font->height * kLinesForPhoto;
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
	const auto rightHide = hasRightPhoto
		? nullptr
		: Ui::CreateChild<Ui::IconButton>(
			container,
			st::dialogsCancelSearchInPeer);
	if (rightHide) {
		container->sizeValue(
		) | rpl::start_with_next([=](const QSize &s) {
			rightHide->moveToRight(st::buttonRadius, st::lineWidth);
		}, rightHide->lifetime());
		rightHide->setClickedCallback(
			hostedClick(HideSponsoredClickHandler()));
	}
	const auto badgeButton = Ui::CreateChild<BadgeButton>(
		widget,
		from.canReport
			? tr::lng_sponsored_message_revenue_button
			: tr::lng_sponsored_top_bar_hide,
		GenerateReplyColorCallback(
			widget,
			fullId,
			from.colorIndex ? from.colorIndex : 4/*blue*/));
	badgeButton->setClickedCallback(
		hostedClick(from.canReport
			? AboutSponsoredClickHandler()
			: HideSponsoredClickHandler()));
	badgeButton->show();

	const auto draw = [=](QPainter &p) {
		const auto r = widget->rect();
		p.fillRect(r, st::historyPinnedBg);
		widget->paintRipple(p, 0, 0);
		const auto leftPadding = st::msgReplyBarSkip + st::msgReplyBarSkip;
		const auto rightPadding = st::msgReplyBarSkip;
		const auto topPadding = st::msgReplyPadding.top();
		const auto availableWidthNoPhoto = r.width()
			- leftPadding
			- rightPadding;
		const auto availableWidth = availableWidthNoPhoto
			- (hasRightPhoto ? (rightPadding + rightPhotoSize) : 0)
			- (rightHide ? rightHide->width() : 0);
		const auto titleRight = leftPadding
			+ state->title.maxWidth()
			+ titleSt.font->spacew * 2;
		const auto hasSecondLineTitle = (titleRight
			> (availableWidth
				- state->contentTitle.maxWidth()
				- badgeButton->width()));
		p.setPen(st::windowActiveTextFg);
		state->title.draw(p, {
			.position = QPoint(leftPadding, topPadding),
			.outerWidth = availableWidth,
			.availableWidth = availableWidth,
		});
		badgeButton->moveToLeft(
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
						- (rightHide ? rightHide->width() : 0)
						- rightPadding),
			topPadding
				+ (titleSt.font->height - badgeButton->height()) / 2);
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
				line += (hasSecondLineTitle ? 2 : 1)
					+ (hasRightPhoto ? 0 : 1);
				return {
					.width = (line > kLinesForPhoto)
						? availableWidthNoPhoto
						: availableWidth,
				};
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
	};
	widget->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(widget);
		draw(p);
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
		container->resize(
			widget->width(),
			std::clamp(
				desiredHeight,
				minHeight,
				st::sponsoredMessageBarMaxHeight));
	}, widget->lifetime());
	{ // Calculate a good size for container.
		auto dummy = QImage(1, 1, QImage::Format_ARGB32);
		auto p = QPainter(&dummy);
		draw(p);
	}

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
