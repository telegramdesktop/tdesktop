/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/send_gif_with_caption_box.h"

#include "base/event_filter.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/field_autocomplete.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "history/view/controls/history_view_characters_limit.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/clip/media_clip_reader.h"
#include "menu/menu_send.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/emoji_button_factory.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

[[nodiscard]] not_null<Ui::RpWidget*> AddGifWidget(
		not_null<Ui::VerticalLayout*> container,
		not_null<DocumentData*> document,
		int width) {
	struct State final {
		std::shared_ptr<Data::DocumentMedia> mediaView;
		::Media::Clip::ReaderPointer gif;
		rpl::lifetime loadingLifetime;
	};

	const auto state = container->lifetime().make_state<State>();
	state->mediaView = document->createMediaView();
	state->mediaView->automaticLoad(Data::FileOriginSavedGifs(), nullptr);
	state->mediaView->thumbnailWanted(Data::FileOriginSavedGifs());
	state->mediaView->videoThumbnailWanted(Data::FileOriginSavedGifs());

	const auto widget = container->add(
		Ui::CreateSkipWidget(
			container,
			document->dimensions.scaled(
				width - rect::m::sum::h(st::boxRowPadding),
				std::numeric_limits<int>::max(),
				Qt::KeepAspectRatio).height()),
		st::boxRowPadding);
	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(widget);
		if (state->gif && state->gif->started()) {
			p.drawImage(
				0,
				0,
				state->gif->current({ .frame = widget->size() }, crl::now()));
		} else if (const auto thumb = state->mediaView->thumbnail()) {
			p.drawImage(
				widget->rect(),
				thumb->pixNoCache(
					widget->size() * style::DevicePixelRatio(),
					{ .outer = widget->size() }).toImage());
		} else if (const auto thumb = state->mediaView->thumbnailInline()) {
			p.drawImage(
				widget->rect(),
				thumb->pixNoCache(
					widget->size() * style::DevicePixelRatio(),
					{
						.options = Images::Option::Blur,
						.outer = widget->size(),
					}).toImage());
		}
	}, widget->lifetime());

	const auto updateThumbnail = [=] {
		if (document->dimensions.isEmpty()) {
			return false;
		}
		if (!state->mediaView->loaded()) {
			return false;
		}
		const auto callback = [=](::Media::Clip::Notification) {
			if (state->gif && state->gif->ready() && !state->gif->started()) {
				state->gif->start({ .frame = widget->size() });
			}
			widget->update();
		};
		state->gif = ::Media::Clip::MakeReader(
			state->mediaView->owner()->location(),
			state->mediaView->bytes(),
			callback);
		return true;
	};
	if (!updateThumbnail()) {
		document->owner().session().downloaderTaskFinished(
		) | rpl::start_with_next([=] {
			if (updateThumbnail()) {
				state->loadingLifetime.destroy();
				widget->update();
			}
		}, state->loadingLifetime);
	}

	return widget;
}

[[nodiscard]] not_null<Ui::InputField*> AddInputField(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	using Limit = HistoryView::Controls::CharactersLimitLabel;

	const auto bottomContainer = box->setPinnedToBottomContent(
		object_ptr<Ui::VerticalLayout>(box));
	const auto wrap = bottomContainer->add(
		object_ptr<Ui::RpWidget>(box),
		st::boxRowPadding);
	const auto input = Ui::CreateChild<Ui::InputField>(
		wrap,
		st::defaultComposeFiles.caption,
		Ui::InputField::Mode::MultiLine,
		tr::lng_photo_caption());
	Ui::ResizeFitChild(wrap, input);

	struct State final {
		base::unique_qptr<ChatHelpers::TabbedPanel> emojiPanel;
		base::unique_qptr<Limit> charsLimitation;
	};
	const auto state = box->lifetime().make_state<State>();

	{
		const auto container = box->getDelegate()->outerContainer();
		using Selector = ChatHelpers::TabbedSelector;
		state->emojiPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
			container,
			controller,
			object_ptr<Selector>(
				nullptr,
				controller->uiShow(),
				Window::GifPauseReason::Layer,
				Selector::Mode::EmojiOnly));
		const auto emojiPanel = state->emojiPanel.get();
		emojiPanel->setDesiredHeightValues(
			1.,
			st::emojiPanMinHeight / 2,
			st::emojiPanMinHeight);
		emojiPanel->hide();
		emojiPanel->selector()->setCurrentPeer(controller->session().user());
		emojiPanel->selector()->emojiChosen(
		) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
			Ui::InsertEmojiAtCursor(input->textCursor(), data.emoji);
		}, input->lifetime());
		emojiPanel->selector()->customEmojiChosen(
		) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
			const auto info = data.document->sticker();
			if (info
				&& info->setType == Data::StickersType::Emoji
				&& !controller->session().premium()) {
				ShowPremiumPreviewBox(
					controller,
					PremiumFeature::AnimatedEmoji);
			} else {
				Data::InsertCustomEmoji(input, data.document);
			}
		}, input->lifetime());
	}

	const auto emojiButton = Ui::AddEmojiToggleToField(
		input,
		box,
		controller,
		state->emojiPanel.get(),
		st::sendGifWithCaptionEmojiPosition);
	emojiButton->show();

	const auto session = &controller->session();
	const auto checkCharsLimitation = [=](auto repeat) -> void {
		const auto remove = Ui::ComputeFieldCharacterCount(input)
			- Data::PremiumLimits(session).captionLengthCurrent();
		if (remove > 0) {
			if (!state->charsLimitation) {
				state->charsLimitation = base::make_unique_q<Limit>(
					input,
					emojiButton,
					style::al_top);
				state->charsLimitation->show();
				Data::AmPremiumValue(session) | rpl::start_with_next([=] {
					repeat(repeat);
				}, state->charsLimitation->lifetime());
			}
			state->charsLimitation->setLeft(remove);
			state->charsLimitation->show();
		} else {
			state->charsLimitation = nullptr;
		}
	};

	input->changes() | rpl::start_with_next([=] {
		checkCharsLimitation(checkCharsLimitation);
	}, input->lifetime());

	return input;
}

} // namespace

void SendGifWithCaptionBox(
		not_null<Ui::GenericBox*> box,
		not_null<DocumentData*> document,
		not_null<PeerData*> peer,
		const SendMenu::Details &details,
		Fn<void(Api::SendOptions, TextWithTags)> done) {
	const auto window = Core::App().findWindow(box);
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return;
	}
	box->setTitle(tr::lng_send_gif_with_caption());
	box->setWidth(st::boxWidth);
	box->getDelegate()->setStyle(st::sendGifBox);

	const auto container = box->verticalLayout();
	[[maybe_unused]] const auto gifWidget = AddGifWidget(
		container,
		document,
		st::boxWidth);

	Ui::AddSkip(container);

	const auto input = AddInputField(box, controller);
	box->setFocusCallback([=] {
		input->setFocus();
	});

	input->setSubmitSettings(Core::App().settings().sendSubmitWay());
	InitMessageField(controller, input, [=](not_null<DocumentData*>) {
		return true;
	});

	const auto sendMenuDetails = [=] { return details; };
	struct Autocomplete {
		std::unique_ptr<ChatHelpers::FieldAutocomplete> dropdown;
		bool geometryUpdateScheduled = false;
	};
	const auto autocomplete = box->lifetime().make_state<Autocomplete>();
	const auto outer = box->getDelegate()->outerContainer();
	ChatHelpers::InitFieldAutocomplete(autocomplete->dropdown, {
		.parent = outer,
		.show = controller->uiShow(),
		.field = input,
		.peer = peer,
		.features = [=] {
			auto result = ChatHelpers::ComposeFeatures();
			result.autocompleteCommands = false;
			result.suggestStickersByEmoji = false;
			return result;
		},
		.sendMenuDetails = sendMenuDetails,
	});
	const auto raw = autocomplete->dropdown.get();
	const auto recountPostponed = [=] {
		if (autocomplete->geometryUpdateScheduled) {
			return;
		}
		autocomplete->geometryUpdateScheduled = true;
		Ui::PostponeCall(raw, [=] {
			autocomplete->geometryUpdateScheduled = false;

			const auto from = input->parentWidget();
			auto field = Ui::MapFrom(outer, from, input->geometry());
			const auto &st = st::defaultComposeFiles;
			autocomplete->dropdown->setBoundings(QRect(
				field.x() - input->x(),
				st::defaultBox.margin.top(),
				input->width(),
				(field.y()
					+ st.caption.textMargins.top()
					+ st.caption.placeholderShift
					+ st.caption.placeholderFont->height
					- st::defaultBox.margin.top())));
		});
	};
	for (auto w = (QWidget*)input; w; w = w->parentWidget()) {
		base::install_event_filter(raw, w, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Move || e->type() == QEvent::Resize) {
				recountPostponed();
			}
			return base::EventFilterResult::Continue;
		});
		if (w == outer) {
			break;
		}
	}

	const auto send = [=](Api::SendOptions options) {
		done(std::move(options), input->getTextWithTags());
	};
	const auto confirm = box->addButton(
		tr::lng_send_button(),
		[=] { send({}); });
	SendMenu::SetupMenuAndShortcuts(
		confirm,
		controller->uiShow(),
		sendMenuDetails,
		SendMenu::DefaultCallback(controller->uiShow(), send));
	box->setShowFinishedCallback([=] {
		if (const auto raw = autocomplete->dropdown.get()) {
			InvokeQueued(raw, [=] {
				raw->raise();
			});
		}
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
	input->submits(
	) | rpl::start_with_next([=] { send({}); }, input->lifetime());
}

} // namespace Ui
