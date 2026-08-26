/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/send_gif_with_caption_box.h"

#include "api/api_editing.h"
#include "base/event_filter.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/field_autocomplete.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_groups.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "editor/video/video_editor_common.h"
#include "editor/video/video_editor_layer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/controls/history_view_characters_limit.h"
#include "history/view/controls/history_view_compose_ai_button.h"
#include "history/view/history_view_message.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/clip/media_clip_reader.h"
#include "menu/menu_checked_action.h"
#include "menu/menu_send.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_controls.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/emoji_button_factory.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/text/text_entity.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Ui {

void SetupCaptionFieldInBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<Ui::InputField*> field,
		PeerData *panelPeer,
		Fn<bool(not_null<DocumentData*>)> allowWithoutPremium,
		PremiumFeature premiumFeature) {
	using Limit = HistoryView::Controls::CharactersLimitLabel;
	struct State final {
		base::unique_qptr<ChatHelpers::TabbedPanel> emojiPanel;
		base::unique_qptr<Limit> charsLimitation;
	};
	const auto state = box->lifetime().make_state<State>();
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
	emojiPanel->selector()->setCurrentPeer(
		panelPeer ? panelPeer : controller->session().user());
	emojiPanel->selector()->emojiChosen(
	) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(field->textCursor(), data.emoji);
	}, field->lifetime());
	emojiPanel->selector()->customEmojiChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		const auto info = data.document->sticker();
		if (info
			&& info->setType == Data::StickersType::Emoji
			&& !allowWithoutPremium(data.document)
			&& !controller->session().premium()) {
			ShowPremiumPreviewBox(controller, premiumFeature);
		} else {
			Data::InsertCustomEmoji(field, data.document);
		}
	}, field->lifetime());

	const auto emojiButton = Ui::AddEmojiToggleToField(
		field,
		box,
		controller,
		emojiPanel,
		st::sendGifWithCaptionEmojiPosition);
	emojiButton->show();

	const auto session = &controller->session();
	const auto checkCharsLimitation = [=](auto repeat) -> void {
		const auto remove = Ui::ComputeFieldCharacterCount(field)
			- Data::PremiumLimits(session).captionLengthCurrent();
		if (remove > 0) {
			if (!state->charsLimitation) {
				state->charsLimitation = base::make_unique_q<Limit>(
					field,
					emojiButton,
					style::al_top);
				state->charsLimitation->show();
				Data::AmPremiumValue(session) | rpl::on_next([=] {
					repeat(repeat);
				}, state->charsLimitation->lifetime());
			}
			state->charsLimitation->setLeft(remove);
			state->charsLimitation->show();
		} else {
			state->charsLimitation = nullptr;
		}
	};
	field->changes() | rpl::on_next([=] {
		checkCharsLimitation(checkCharsLimitation);
	}, field->lifetime());
}

namespace {

struct State final {
	std::shared_ptr<Data::DocumentMedia> mediaView;
	::Media::Clip::ReaderPointer gif;
	std::unique_ptr<Ui::SpoilerAnimation> spoiler;
	base::unique_qptr<Ui::AttachControlsWidget> controls;
	QImage firstFrame;
	QImage blurredFrame;
	QImage edited;
	Ui::PreparedList list;
	bool hasSpoiler = false;
	bool preparing = false;
	bool pressed = false;
	rpl::lifetime loadingLifetime;
};

[[nodiscard]] Ui::PreparedFileInformation::Video *PreparedVideo(
		not_null<State*> state) {
	if (state->list.files.empty()) {
		return nullptr;
	}
	const auto &file = state->list.files.front();
	return file.information
		? std::get_if<Ui::PreparedFileInformation::Video>(
			&file.information->media)
		: nullptr;
}

[[nodiscard]] bool VideoModified(not_null<State*> state) {
	const auto video = PreparedVideo(state);
	if (!video) {
		return false;
	}
	const auto &modifications = video->modifications;
	return (modifications.cover > 0)
		|| Editor::VideoEdited(
			modifications,
			video->thumbnail.size(),
			video->duration,
			video->hasAudio);
}

[[nodiscard]] not_null<State*> AddGifWidget(
		not_null<Ui::GenericBox*> box,
		Window::SessionController *controller,
		not_null<DocumentData*> document,
		int width) {
	const auto container = box->verticalLayout();
	const auto state = container->lifetime().make_state<State>();
	state->mediaView = document->createMediaView();
	state->mediaView->automaticLoad(Data::FileOriginSavedGifs(), nullptr);
	state->mediaView->thumbnailWanted(Data::FileOriginSavedGifs());
	state->mediaView->videoThumbnailWanted(Data::FileOriginSavedGifs());

	const auto heightFor = [=](QSize dimensions) {
		// An empty size makes QSize::scaled return the unbounded box itself.
		return dimensions.isEmpty()
			? 0
			: dimensions.scaled(
				width - rect::m::sum::h(st::boxRowPadding),
				std::numeric_limits<int>::max(),
				Qt::KeepAspectRatio).height();
	};
	const auto widget = container->add(
		Ui::CreateSkipWidget(container, heightFor(document->dimensions)),
		st::boxRowPadding);
	state->controls = base::make_unique_q<Ui::AttachControlsWidget>(
		widget,
		Ui::AttachControls::Type::EditOnly);
	const auto controls = state->controls.get();
	controls->hide();
	widget->sizeValue(
	) | rpl::on_next([=](QSize size) {
		controls->moveToRight(
			st::sendBoxAlbumGroupSkipRight,
			st::sendBoxAlbumGroupSkipTop,
			size.width());
	}, controls->lifetime());

	widget->paintOn([=](QPainter &p) {
		if (state->hasSpoiler) {
			if (state->firstFrame.isNull()) {
				if (!state->edited.isNull()) {
					state->firstFrame = state->edited;
				} else if (state->gif && state->gif->ready()) {
					state->firstFrame = state->gif->current(
						{ .frame = widget->size() },
						crl::now());
				}
				if (!state->firstFrame.isNull()) {
					state->blurredFrame = Images::BlurLargeImage(
						base::duplicate(state->firstFrame),
						24);
				}
			}
			if (!state->blurredFrame.isNull()) {
				p.drawImage(widget->rect(), state->blurredFrame);
			} else if (const auto thumb = state->mediaView->thumbnail()) {
				p.drawImage(
					widget->rect(),
					thumb->pixNoCache(
						widget->size() * style::DevicePixelRatio(),
						{
							.options = Images::Option::Blur,
							.outer = widget->size(),
						}).toImage());
			}
			if (!state->spoiler) {
				state->spoiler = std::make_unique<Ui::SpoilerAnimation>(
					[=] { widget->update(); });
			}
			const auto now = crl::now();
			const auto index = state->spoiler->index(now, false);
			const auto frame = Ui::DefaultImageSpoiler().frame(index);
			Ui::FillSpoilerRect(p, widget->rect(), frame);
			return;
		}
		if (!state->edited.isNull()) {
			p.drawImage(widget->rect(), state->edited);
		} else if (state->gif && state->gif->started()) {
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
	});

	const auto prepareFile = [=] {
		if (state->preparing || !state->list.files.empty()) {
			return;
		}
		const auto bytes = state->mediaView->bytes();
		if (bytes.isEmpty()) {
			return;
		}
		state->preparing = true;
		// The cached bytes may be a view into a buffer that is freed soon.
		auto content = QByteArray(bytes.constData(), bytes.size());
		const auto previewWidth = st::sendMediaPreviewSize;
		const auto sideLimit = PhotoSideLimit();
		crl::async([
			=,
			content = std::move(content),
			weak = base::make_weak(widget)
		]() mutable {
			auto file = Ui::PreparedFile(QString());
			file.size = content.size();
			file.content = std::move(content);
			Storage::PrepareDetails(file, previewWidth, sideLimit);
			crl::on_main(weak, [=, file = std::move(file)]() mutable {
				state->preparing = false;
				if (!file.canEditVideo()) {
					return;
				}
				state->list.files.push_back(std::move(file));
				widget->setCursor(style::cur_pointer);
				controls->show();
			});
		});
	};

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
			if (state->edited.isNull()) {
				widget->update();
			}
		};
		state->gif = ::Media::Clip::MakeReader(
			state->mediaView->owner()->location(),
			state->mediaView->bytes(),
			callback);
		prepareFile();
		return true;
	};
	if (!updateThumbnail()) {
		document->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			if (updateThumbnail()) {
				state->loadingLifetime.destroy();
				widget->update();
			}
		}, state->loadingLifetime);
	}

	const auto refreshEdited = [=] {
		const auto video = VideoModified(state)
			? PreparedVideo(state)
			: nullptr;
		const auto ratio = style::DevicePixelRatio();
		auto edited = (video && !video->thumbnail.isNull())
			? Editor::ImageModified(
				base::duplicate(video->thumbnail),
				video->modifications.geometry)
			: QImage();
		if (!edited.isNull()) {
			edited = edited.scaledToWidth(
				std::max(widget->width(), 1) * ratio,
				Qt::SmoothTransformation);
			edited.setDevicePixelRatio(ratio);
		}
		state->edited = std::move(edited);
		state->firstFrame = QImage();
		state->blurredFrame = QImage();
		widget->resize(widget->width(), state->edited.isNull()
			? heightFor(document->dimensions)
			: (state->edited.height() / ratio));
		if (state->edited.isNull() && state->gif && state->gif->ready()) {
			state->gif->start({ .frame = widget->size() });
		}
		widget->update();
	};

	const auto openEditor = [=] {
		if (!controller || state->list.files.empty()) {
			return;
		}
		Editor::OpenWithPreparedVideoFile(
			box,
			controller->uiShow(),
			&state->list,
			0,
			st::sendMediaPreviewSize,
			crl::guard(box, [=](bool ok) {
				if (ok) {
					refreshEdited();
				}
			}),
			PhotoSideLimit(true));
	};

	const auto editable = [=] {
		return (controller != nullptr) && !state->list.files.empty();
	};
	const auto showMenu = [=](bool fromControls) {
		const auto menu = Ui::CreateChild<Ui::PopupMenu>(
			widget,
			st::popupMenuWithIcons);
		if (fromControls) {
			menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		}
		if (editable()) {
			menu->addAction(
				tr::lng_context_edit_gif(tr::now),
				openEditor,
				&st::menuIconDraw);
		}
		::Menu::AddCheckedAction(
			menu,
			tr::lng_context_spoiler_effect(tr::now),
			[=] {
				state->hasSpoiler = !state->hasSpoiler;
				if (!state->hasSpoiler) {
					state->spoiler = nullptr;
					state->firstFrame = QImage();
					state->blurredFrame = QImage();
					if (state->gif && state->gif->ready()) {
						state->gif->start({ .frame = widget->size() });
					}
				}
				widget->update();
			},
			&st::menuIconSpoiler,
			state->hasSpoiler);
		menu->popup(QCursor::pos());
	};
	controls->editRequests(
	) | rpl::on_next([=] {
		showMenu(true);
	}, controls->lifetime());

	base::install_event_filter(widget, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::ContextMenu) {
			showMenu(false);
			return base::EventFilterResult::Cancel;
		} else if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (!editable() || mouse->button() != Qt::LeftButton) {
				return base::EventFilterResult::Continue;
			}
			state->pressed = true;
			return base::EventFilterResult::Cancel;
		} else if (type == QEvent::MouseButtonRelease) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (!base::take(state->pressed)
				|| mouse->button() != Qt::LeftButton) {
				return base::EventFilterResult::Continue;
			}
			if (widget->rect().contains(mouse->pos())) {
				openEditor();
			}
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	return state;
}

[[nodiscard]] not_null<Ui::InputField*> AddInputField(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
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
	SetupCaptionFieldInBox(
		box,
		controller,
		input,
		controller->session().user(),
		[](not_null<DocumentData*>) {
			return false;
		},
		PremiumFeature::AnimatedEmoji);

	return input;
}

void CaptionBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> confirmText,
		TextWithTags initialText,
		not_null<PeerData*> peer,
		const SendMenu::Details &details,
		Fn<void(Api::SendOptions, TextWithTags)> done,
		Fn<void(TextWithTags)> cancelled = nullptr) {
	const auto window = Core::App().findWindow(box);
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return;
	}
	box->setWidth(st::boxWidth);
	box->getDelegate()->setStyle(st::sendGifBox);

	const auto input = AddInputField(box, controller);
	box->setFocusCallback([=] {
		input->setFocus();
	});

	input->setTextWithTags(std::move(initialText));
	input->setSubmitSettings(Core::App().settings().sendSubmitWay());
	const auto chatStyle = InitMessageField(
		controller,
		input,
		[=](not_null<DocumentData*>) { return true; });

	const auto aiButton = Ui::SetupCaptionAiButton({
		.parent = input->parentWidget(),
		.field = input,
		.session = &controller->session(),
		.show = controller->uiShow(),
		.chatStyle = chatStyle,
	});
	rpl::combine(
		box->sizeValue(),
		input->geometryValue()
	) | rpl::on_next([=](QSize, QRect) {
		Ui::UpdateCaptionAiButtonGeometry(aiButton, input);
		aiButton->raise();
	}, aiButton->lifetime());

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

	const auto confirmed = box->lifetime().make_state<bool>(false);
	const auto send = [=, show = controller->uiShow()](
			Api::SendOptions options) {
		const auto textWithTags = input->getTextWithTags();
		const auto remove = Ui::ComputeFieldCharacterCount(input)
			- Data::PremiumLimits(&show->session()).captionLengthCurrent();
		if (remove > 0) {
			show->showToast(
				tr::lng_edit_limit_reached(tr::now, lt_count, remove));
			return;
		}
		*confirmed = true;
		done(std::move(options), textWithTags);
	};
	if (cancelled) {
		box->boxClosing(
		) | rpl::on_next([=] {
			if (!*confirmed) {
				cancelled(input->getTextWithTags());
			}
		}, box->lifetime());
	}
	const auto confirm = box->addButton(
		std::move(confirmText),
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
	) | rpl::on_next([=] { send({}); }, input->lifetime());
}

} // namespace

void SendGifWithCaptionBox(
		not_null<Ui::GenericBox*> box,
		not_null<DocumentData*> document,
		not_null<PeerData*> peer,
		const SendMenu::Details &details,
		TextWithTags initialText,
		Fn<void(Api::SendOptions, TextWithTags, Ui::PreparedList&&)> c,
		Fn<void(TextWithTags)> cancelled) {
	box->setTitle(tr::lng_send_gif_with_caption());
	const auto window = Core::App().findWindow(box);
	const auto state = AddGifWidget(
		box,
		window ? window->sessionController() : nullptr,
		document,
		st::boxWidth);
	Ui::AddSkip(box->verticalLayout());
	const auto d = [=](Api::SendOptions o, TextWithTags t) {
		auto edited = Ui::PreparedList();
		if (VideoModified(state)) {
			edited = base::take(state->list);
			auto &file = edited.files.front();
			file.spoiler = state->hasSpoiler;
			file.caption = t;
		} else {
			o.mediaSpoiler = state->hasSpoiler;
		}
		document->owner().stickers().notifyGifWithCaptionSent();
		c(std::move(o), std::move(t), std::move(edited));
	};
	CaptionBox(
		box,
		tr::lng_send_button(),
		std::move(initialText),
		peer,
		details,
		std::move(d),
		std::move(cancelled));
}

void SendGifWithCaption(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<Ui::InputField*> field,
		not_null<DocumentData*> document,
		not_null<PeerData*> peer,
		const SendMenu::Details &details,
		Fn<void(Api::SendOptions, TextWithTags, Ui::PreparedList&&)> send) {
	show->show(Box(
		SendGifWithCaptionBox,
		document,
		peer,
		details,
		field->getTextWithTags(),
		crl::guard(field, [=](
				Api::SendOptions options,
				TextWithTags caption,
				Ui::PreparedList &&edited) {
			field->setTextWithTags({});
			show->hideLayer(anim::type::normal);
			send(std::move(options), std::move(caption), std::move(edited));
		}),
		crl::guard(field, [=](TextWithTags caption) {
			if (!caption.text.isEmpty()) {
				field->setTextWithTags(std::move(caption));
			}
		})));
}

void EditCaptionBox(
		not_null<Ui::GenericBox*> box,
		not_null<HistoryView::Element*> view) {
	using namespace TextUtilities;

	box->setTitle(tr::lng_context_upload_edit_caption());

	const auto data = &view->data()->history()->peer->owner();

	struct State {
		FullMsgId fullId;
	};
	const auto state = box->lifetime().make_state<State>();
	state->fullId = view->data()->fullId();

	data->itemIdChanged(
	) | rpl::on_next([=](Data::Session::IdChange event) {
		if (event.oldId == state->fullId.msg) {
			state->fullId = event.newId;
		}
	}, box->lifetime());

	auto done = [=, show = box->uiShow()](
			Api::SendOptions,
			TextWithTags textWithTags) {
		const auto item = data->message(state->fullId);
		if (!item) {
			show->showToast(tr::lng_message_not_found(tr::now));
			return;
		}
		if (!(item->media() && item->media()->allowsEditCaption())) {
			show->showToast(tr::lng_edit_error(tr::now));
			return;
		}
		const auto textLength = textWithTags.text.size();
		const auto limit = Data::PremiumLimits(
			&item->history()->session()).captionLengthCurrent();
		if (textLength > limit) {
			show->showToast(tr::lng_edit_limit_reached(
				tr::now,
				lt_count,
				textLength - limit));
			return;
		}
		auto text = TextWithEntities{
			std::move(textWithTags.text),
			ConvertTextTagsToEntities(std::move(textWithTags.tags)),
		};
		if (item->isUploading()) {
			item->setText(std::move(text));
			data->requestViewResize(view);
			if (item->groupId()) {
				data->groups().refreshMessage(item, true);
			}
			box->closeBox();
		} else {
			Api::EditCaption(
				item,
				std::move(text),
				{ .invertCaption = item->invertMedia() },
				[=] { box->closeBox(); },
				[=](const QString &e) { box->uiShow()->showToast(e); });
		}
	};

	const auto item = view->data();
	CaptionBox(
		box,
		tr::lng_settings_save(),
		TextWithTags{
			.text = item->originalText().text,
			.tags = ConvertEntitiesToTextTags(item->originalText().entities),
		},
		item->history()->peer,
		{},
		std::move(done));
}

} // namespace Ui
