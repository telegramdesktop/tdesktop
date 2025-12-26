/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_reaction_preview.h"

#include "base/call_delayed.h"
#include "boxes/sticker_set_box.h"
#include "data/data_document.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "ui/cached_special_layer_shadow_corners.h"
#include "ui/effects/show_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "window/window_media_preview.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace HistoryView {

bool ShowReactionPreview(
		not_null<Window::SessionController*> controller,
		FullMsgId origin,
		Data::ReactionId reactionId) {
	auto document = (DocumentData*)(nullptr);
	if (const auto custom = reactionId.custom()) {
		document = controller->session().data().document(custom);
	} else if (const auto resolved
			= controller->session().data().reactions().lookupTemporary(
				reactionId)) {
		document = resolved->selectAnimation;
	}
	if (!document) {
		return false;
	}
	struct State {
		base::unique_qptr<Window::MediaPreviewWidget> mediaPreview;
		base::unique_qptr<Ui::AbstractButton> clickable;
		base::unique_qptr<Ui::AbstractButton> background;
		base::unique_qptr<Ui::FlatLabel> label;
	};
	const auto state = std::make_shared<State>();

	const auto mainwidget = controller->widget();
	state->mediaPreview = base::make_unique_q<Window::MediaPreviewWidget>(
		mainwidget,
		controller);
	state->mediaPreview->setCustomDuration(st::defaultToggle.duration);
	state->clickable = base::make_unique_q<Ui::AbstractButton>(mainwidget);
	const auto hideAll = [=] {
		state->clickable->setAttribute(Qt::WA_TransparentForMouseEvents);
		state->mediaPreview->hidePreview();
		if (state->label && state->background) {
			Ui::Animations::HideWidgets({
				state->background.get(),
				state->label.get(),
			});
		}
		base::call_delayed(
			st::defaultToggle.duration,
			crl::guard(state->clickable.get(), [=] {
				state->clickable.reset();
			}));
	};
	state->clickable->setClickedCallback(hideAll);
	state->mediaPreview->showPreview(origin, document);
	state->clickable->show();
	const auto mediaPreviewRaw = state->mediaPreview.get();
	const auto clickableRaw = state->clickable.get();
	const auto shadowExtend = st::boxRoundShadow.extend;

	if (reactionId.custom() && document->sticker()) {
		const auto setId = document->sticker()->set;
		const auto packName
			= document->owner().customEmojiManager().lookupSetName(setId.id);
		if (!packName.isEmpty()) {
			state->background = base::make_unique_q<Ui::AbstractButton>(
				mainwidget);
			const auto show = controller->uiShow();
			state->background->setClickedCallback([=] {
				hideAll();
				show->show(Box<StickerSetBox>(
					show,
					setId,
					Data::StickersType::Emoji));
			});
			state->label = base::make_unique_q<Ui::FlatLabel>(
				state->background.get(),
				tr::lng_context_animated_reaction(
					lt_name,
					rpl::single(Ui::Text::Colorized(packName)),
					tr::rich));
			state->label->setAttribute(Qt::WA_TransparentForMouseEvents);
			const auto backgroundRaw = state->background.get();
			const auto labelRaw = state->label.get();

			backgroundRaw->paintOn([=](QPainter &p) {
				const auto innerRect = backgroundRaw->rect() - shadowExtend;
				Ui::Shadow::paint(
					p,
					innerRect,
					backgroundRaw->width(),
					st::boxRoundShadow,
					Ui::SpecialLayerShadowCorners(),
					RectPart::Full);
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBg);
				p.drawRoundedRect(
					innerRect,
					st::boxRadius,
					st::boxRadius);
			});

			labelRaw->show();
			Ui::Animations::ShowWidgets({ backgroundRaw });
		}
	}
	const auto backgroundRaw = state->background.get();
	const auto labelRaw = state->label.get();

	mainwidget->sizeValue() | rpl::on_next([=](QSize size) {
		mediaPreviewRaw->setGeometry(Rect(size));
		clickableRaw->setGeometry(Rect(size));
		clickableRaw->raise();

		if (backgroundRaw && labelRaw) {
			const auto maxLabelWidth = labelRaw->textMaxWidth() / 2;
			labelRaw->resizeToWidth(maxLabelWidth);
			const auto labelHeight = labelRaw->height() * 2;
			const auto padding = st::msgServicePadding;
			const auto innerWidth = maxLabelWidth + rect::m::sum::h(padding);
			const auto innerHeight = labelHeight + rect::m::sum::v(padding);
			const auto bgWidth = innerWidth + rect::m::sum::h(shadowExtend);
			const auto bgHeight = innerHeight + rect::m::sum::v(shadowExtend);
			const auto bgX = (size.width() - bgWidth) / 2;
			const auto bgY = (size.height() * 3 / 4) - (bgHeight / 2);

			backgroundRaw->setGeometry(bgX, bgY, bgWidth, bgHeight);
			labelRaw->setGeometry(
				shadowExtend.left() + padding.left(),
				shadowExtend.top() + padding.top(),
				maxLabelWidth,
				labelHeight);
			backgroundRaw->raise();
		}
	}, mediaPreviewRaw->lifetime());
	return true;
}

} // namespace HistoryView
