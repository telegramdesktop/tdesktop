/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_reaction_preview.h"

#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "boxes/sticker_set_box.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "ui/cached_special_layer_shadow_corners.h"
#include "ui/effects/show_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "window/window_media_preview.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace HistoryView {
namespace {

void SetupOverlayHideOnEscape(
		not_null<Ui::AbstractButton*> clickable,
		Fn<void()> hideAll) {
	clickable->setClickedCallback(hideAll);
	base::install_event_filter(QCoreApplication::instance(), [=](
			not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress
			&& clickable->window()->isActiveWindow()) {
			const auto k = static_cast<QKeyEvent*>(e.get());
			if (k->key() == Qt::Key_Escape) {
				hideAll();
				return base::EventFilterResult::Cancel;
			}
		}
		return base::EventFilterResult::Continue;
	}, clickable->lifetime());
}

struct PreviewOverlayState {
	base::unique_qptr<Window::MediaPreviewWidget> mediaPreview;
	base::unique_qptr<Ui::AbstractButton> clickable;
	base::unique_qptr<Ui::FadeWrap<Ui::DropdownMenu>> menuWrap;
	base::unique_qptr<Ui::AbstractButton> background;
	base::unique_qptr<Ui::FlatLabel> label;
	Fn<void()> extraHide;
	rpl::lifetime shutdownGuard;

	void clear() {
		shutdownGuard.destroy();
		menuWrap.reset();
		background.reset();
		label.reset();
		mediaPreview.reset();
		clickable.reset();
	}
};

struct PreviewOverlay {
	std::shared_ptr<PreviewOverlayState> state;
	Fn<void()> hideAll;
};

template <typename MediaData>
[[nodiscard]] PreviewOverlay CreatePreviewOverlay(
		not_null<Window::SessionController*> controller,
		FullMsgId origin,
		MediaData media) {
	const auto state = std::make_shared<PreviewOverlayState>();

	const auto mainwidget = controller->widget()->bodyWidget();
	state->mediaPreview = base::make_unique_q<Window::MediaPreviewWidget>(
		mainwidget,
		controller);
	state->mediaPreview->setCustomDuration(st::defaultToggle.duration);
	state->clickable = base::make_unique_q<Ui::AbstractButton>(mainwidget);
	const auto hideAll = [=] {
		state->clickable->setAttribute(Qt::WA_TransparentForMouseEvents);
		state->mediaPreview->hidePreview();
		if (state->extraHide) {
			state->extraHide();
		}
		base::call_delayed(
			st::defaultToggle.duration,
			[=] { state->clear(); });
	};
	SetupOverlayHideOnEscape(state->clickable.get(), hideAll);
	state->mediaPreview->showPreview(origin, media);
	state->clickable->show();
	const auto clickableRaw = state->clickable.get();

	mainwidget->sizeValue(
	) | rpl::skip(1) | rpl::on_next([=](QSize) {
		hideAll();
	}, clickableRaw->lifetime());

	mainwidget->sizeValue() | rpl::on_next([=](QSize size) {
		clickableRaw->setGeometry(Rect(size));
		clickableRaw->raise();
	}, clickableRaw->lifetime());

	// Prevent running state destructor from within a child widget's
	// destructor, which would trigger a double-delete through unique_qptr.
	mainwidget->death() | rpl::on_next([s = state] {
	}, state->shutdownGuard);

	return { state, hideAll };
}

void SetupPreviewMenu(
		not_null<Window::SessionController*> controller,
		const PreviewOverlay &overlay,
		Fn<void(not_null<Ui::DropdownMenu*>)> fillMenu) {
	const auto &state = overlay.state;
	const auto mainwidget = controller->widget()->bodyWidget();
	if (fillMenu) {
		state->mediaPreview->setHideEmoji(true);
		auto menu = object_ptr<Ui::DropdownMenu>(
			mainwidget,
			st::dropdownMenuWithIcons);
		menu->setAutoHiding(false);
		menu->setHiddenCallback(
			crl::guard(state->clickable.get(), overlay.hideAll));
		fillMenu(menu.data());
		state->menuWrap = base::make_unique_q<Ui::FadeWrap<Ui::DropdownMenu>>(
			mainwidget,
			std::move(menu));
		state->menuWrap->setDuration(st::defaultToggle.duration);
		state->menuWrap->hide(anim::type::instant);
	}
	const auto wrapRaw = state->menuWrap.get();
	state->extraHide = [=] {
		if (wrapRaw) {
			wrapRaw->hide(anim::type::normal);
		}
	};

	const auto mediaPreviewRaw = state->mediaPreview.get();
	mainwidget->sizeValue() | rpl::on_next([=](QSize size) {
		mediaPreviewRaw->setGeometry(Rect(size));

		if (wrapRaw) {
			const auto menuRaw = wrapRaw->entity();
			menuRaw->showFast();
			const auto gap = st::defaultMenu.itemPadding.top();
			const auto menuH = menuRaw->height();
			const auto shift = -(gap + menuH) / 2;
			mediaPreviewRaw->setContentShift(shift);

			const auto menuX = (size.width() - menuRaw->width()) / 2;
			const auto menuY = mediaPreviewRaw->contentBottom() + gap;
			wrapRaw->move(menuX, menuY);
			wrapRaw->show(anim::type::normal);
			wrapRaw->raise();
		}
	}, mediaPreviewRaw->lifetime());
}

} // namespace

bool ShowStickerPreview(
		not_null<Window::SessionController*> controller,
		FullMsgId origin,
		not_null<DocumentData*> document,
		Fn<void(not_null<Ui::DropdownMenu*>)> fillMenu) {
	SetupPreviewMenu(
		controller,
		CreatePreviewOverlay(controller, origin, document),
		std::move(fillMenu));
	return true;
}

bool ShowPhotoPreview(
		not_null<Window::SessionController*> controller,
		FullMsgId origin,
		not_null<PhotoData*> photo,
		Fn<void(not_null<Ui::DropdownMenu*>)> fillMenu) {
	SetupPreviewMenu(
		controller,
		CreatePreviewOverlay(controller, origin, photo),
		std::move(fillMenu));
	return true;
}

bool ShowReactionPreview(
		not_null<Window::SessionController*> controller,
		FullMsgId origin,
		Data::ReactionId reactionId,
		bool emojiPreview) {
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
	const auto overlay = CreatePreviewOverlay(controller, origin, document);
	const auto &state = overlay.state;

	const auto mainwidget = controller->widget()->bodyWidget();
	const auto shadowExtend = st::boxRoundShadow.extend;

	if (reactionId.custom() && document->sticker()) {
		const auto setId = document->sticker()->set;
		const auto packName
			= document->owner().customEmojiManager().lookupSetName(setId.id);
		if (!packName.isEmpty()) {
			state->background = base::make_unique_q<Ui::AbstractButton>(
				mainwidget);
			const auto show = controller->uiShow();
			const auto hideAll = overlay.hideAll;
			state->background->setClickedCallback([=] {
				hideAll();
				show->show(Box<StickerSetBox>(
					show,
					setId,
					Data::StickersType::Emoji));
			});
			state->label = base::make_unique_q<Ui::FlatLabel>(
				state->background.get(),
				(emojiPreview
					? tr::lng_context_animated_emoji_preview
					: tr::lng_context_animated_reaction)(
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
	state->extraHide = [=] {
		if (backgroundRaw && labelRaw) {
			Ui::Animations::HideWidgets({
				backgroundRaw,
				labelRaw,
			});
		}
	};

	const auto mediaPreviewRaw = state->mediaPreview.get();
	mainwidget->sizeValue() | rpl::on_next([=](QSize size) {
		mediaPreviewRaw->setGeometry(Rect(size));

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

void ShowWidgetPreview(
		not_null<Window::SessionController*> controller,
		Fn<void(not_null<Ui::RpWidget*>)> setupContent,
		Fn<void(not_null<Ui::DropdownMenu*>)> fillMenu) {
	struct State {
		base::unique_qptr<Ui::RpWidget> preview;
		base::unique_qptr<Ui::AbstractButton> clickable;
		base::unique_qptr<Ui::FadeWrap<Ui::DropdownMenu>> menuWrap;
	};
	const auto state = std::make_shared<State>();
	const auto mainwidget = controller->widget()->bodyWidget();

	state->preview = base::make_unique_q<Ui::RpWidget>(mainwidget);
	const auto previewRaw = state->preview.get();
	setupContent(previewRaw);
	previewRaw->setAttribute(Qt::WA_TransparentForMouseEvents);

	state->clickable = base::make_unique_q<Ui::AbstractButton>(mainwidget);
	state->clickable->paintOn([=](QPainter &p) {
		p.fillRect(state->clickable->rect(), st::stickerPreviewBg);
	});

	const auto hideAll = [=] {
		state->clickable->setAttribute(Qt::WA_TransparentForMouseEvents);
		if (state->menuWrap) {
			state->menuWrap->hide(anim::type::normal);
		}
		base::call_delayed(
			st::defaultToggle.duration,
			[s = state] {
				s->preview.reset();
				s->menuWrap.reset();
				s->clickable.reset();
			});
	};
	SetupOverlayHideOnEscape(state->clickable.get(), hideAll);

	auto menu = object_ptr<Ui::DropdownMenu>(
		mainwidget,
		st::dropdownMenuWithIcons);
	menu->setAutoHiding(false);
	menu->setHiddenCallback(
		crl::guard(state->clickable.get(), hideAll));
	fillMenu(menu.data());
	state->menuWrap = base::make_unique_q<Ui::FadeWrap<Ui::DropdownMenu>>(
		mainwidget,
		std::move(menu));
	state->menuWrap->setDuration(st::defaultToggle.duration);
	state->menuWrap->hide(anim::type::instant);

	const auto wrapRaw = state->menuWrap.get();
	state->clickable->show();
	previewRaw->show();

	mainwidget->sizeValue(
	) | rpl::skip(1) | rpl::on_next([=](QSize) {
		hideAll();
	}, previewRaw->lifetime());

	const auto fullW = previewRaw->width();
	const auto fullH = previewRaw->height();

	mainwidget->sizeValue() | rpl::on_next([=](QSize size) {
		state->clickable->setGeometry(Rect(size));
		state->clickable->raise();

		const auto menuRaw = wrapRaw->entity();
		menuRaw->showFast();
		const auto gap = st::defaultMenu.itemPadding.top();
		const auto totalH = fullH + gap + menuRaw->height();
		const auto previewY = (size.height() - totalH) / 2;
		previewRaw->move((size.width() - fullW) / 2, previewY);
		previewRaw->raise();

		wrapRaw->move(
			(size.width() - menuRaw->width()) / 2,
			previewY + fullH + gap);
		wrapRaw->show(anim::type::normal);
		wrapRaw->raise();
	}, previewRaw->lifetime());
}

} // namespace HistoryView
