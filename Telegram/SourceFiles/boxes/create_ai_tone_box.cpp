/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/create_ai_tone_box.h"

#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/data_ai_compose_tones.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_forum_icons.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/sections/settings_premium.h"
#include "ui/abstract_button.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/custom_emoji_toast_icon.h"
#include "ui/controls/warning_tooltip.h"
#include "ui/effects/animations.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kAiComposeToneToastDuration = crl::time(4000);

void ShowToneToast(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const Data::AiComposeTone &tone,
		bool created) {
	const auto size = QSize(
		st::aiComposeToneToastIconSize.width(),
		st::aiComposeToneToastIconSize.height());
	show->showToast(Ui::Toast::Config{
		.title = (created
			? tr::lng_ai_compose_tone_created
			: tr::lng_ai_compose_tone_updated)(
				tr::now,
				lt_title,
				tone.title),
		.text = tr::lng_ai_compose_tone_created_description(
			tr::now,
			Ui::Text::WithEntities),
		.iconContent = Ui::MakeCustomEmojiToastIcon(
			session,
			tone.emojiId,
			size),
		.iconPadding = st::aiComposeToneToastIconPadding,
		.duration = kAiComposeToneToastDuration,
	});
}

void ChooseToneIconBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Fn<void(DocumentId)> chosen) {
	using namespace ChatHelpers;

	box->setTitle(tr::lng_ai_compose_tone_icon_title());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::editTopicMaxHeight);
	box->setScrollStyle(st::reactPanelScroll);

	const auto manager = &controller->session().data().customEmojiManager();
	const auto icons = &controller->session().data().forumIcons();

	auto factory = [=](DocumentId id, Fn<void()> repaint)
	-> std::unique_ptr<Ui::Text::CustomEmoji> {
		return manager->create(
			id,
			std::move(repaint),
			Data::CustomEmojiManager::SizeTag::Large);
	};

	const auto top = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));

	const auto body = box->verticalLayout();
	const auto selector = body->add(
		object_ptr<EmojiListWidget>(body, EmojiListDescriptor{
			.show = controller->uiShow(),
			.mode = EmojiListWidget::Mode::TopicIcon,
			.paused = Window::PausedIn(
				controller,
				Window::GifPauseReason::Layer),
			.customRecentList = DocumentListToRecent(icons->list()),
			.customRecentFactory = std::move(factory),
			.st = &st::reactPanelEmojiPan,
		}),
		st::reactPanelEmojiPan.padding);

	icons->requestDefaultIfUnknown();
	icons->defaultUpdates(
	) | rpl::on_next([=] {
		selector->provideRecent(DocumentListToRecent(icons->list()));
	}, selector->lifetime());

	top->add(selector->createFooter());

	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(box.get());
	shadow->show();

	rpl::combine(
		top->heightValue(),
		selector->widthValue()
	) | rpl::on_next([=](int topHeight, int width) {
		shadow->setGeometry(0, topHeight, width, st::lineWidth);
	}, shadow->lifetime());

	selector->refreshEmoji();

	selector->scrollToRequests(
	) | rpl::on_next([=](int y) {
		box->scrollToY(y);
		shadow->update();
	}, selector->lifetime());

	rpl::combine(
		box->heightValue(),
		top->heightValue(),
		rpl::mappers::_1 - rpl::mappers::_2
	) | rpl::on_next([=](int height) {
		selector->setMinimalHeight(selector->width(), height);
	}, body->lifetime());

	selector->customChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		chosen(data.document->id);
		box->closeBox();
	}, selector->lifetime());

	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

not_null<Ui::FlatLabel*> AddAiComposeFieldDecor(
		not_null<Ui::InputField*> field,
		rpl::producer<QString> placeholder) {
	struct FieldDecor {
		not_null<Ui::RpWidget*> bg;
		not_null<Ui::FlatLabel*> placeholder;
		Ui::Animations::Simple anim;
		bool hidden = false;
	};
	const auto parent = field->parentWidget();
	const auto decor = field->lifetime().make_state<FieldDecor>(FieldDecor{
		.bg = Ui::CreateChild<Ui::RpWidget>(parent),
		.placeholder = Ui::CreateChild<Ui::FlatLabel>(
			parent,
			std::move(placeholder),
			st::aiTonePlaceholderLabel),
	});
	decor->bg->setAttribute(Qt::WA_TransparentForMouseEvents);
	decor->placeholder->setAttribute(Qt::WA_TransparentForMouseEvents);
	decor->bg->paintRequest(
	) | rpl::on_next([bg = decor->bg] {
		auto p = QPainter(bg);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::aiToneFieldBg);
		const auto r = st::aiToneFieldRadius;
		p.drawRoundedRect(bg->rect(), r, r);
	}, decor->bg->lifetime());
	decor->bg->lower();
	decor->placeholder->raise();

	const auto applyPosition = [=] {
		const auto pad = st::aiToneFieldPadding;
		const auto progress = decor->anim.value(decor->hidden ? 1. : 0.);
		const auto shift = int(base::SafeRound(
			progress * (-st::defaultInputField.placeholderShift)));
		decor->placeholder->moveToLeft(
			field->x() + pad.left() + shift,
			field->y() + pad.top());
		decor->placeholder->setOpacity(1. - progress);
	};
	field->geometryValue(
	) | rpl::on_next([=](QRect g) {
		if (g.isEmpty()) {
			return;
		}
		const auto pad = st::aiToneFieldPadding;
		decor->bg->setGeometry(g);
		decor->placeholder->resizeToWidth(
			g.width() - pad.left() - pad.right());
		applyPosition();
	}, field->lifetime());

	const auto animate = [=](bool hidden) {
		if (decor->hidden == hidden) {
			return;
		}
		decor->hidden = hidden;
		decor->anim.start(
			applyPosition,
			hidden ? 0. : 1.,
			hidden ? 1. : 0.,
			st::defaultInputField.duration);
	};
	field->changes(
	) | rpl::on_next([=] {
		animate(!field->getLastText().isEmpty());
	}, field->lifetime());
	decor->hidden = !field->getLastText().isEmpty();
	applyPosition();
	return decor->placeholder;
}

not_null<Ui::AbstractButton*> AddAiToneIconPreview(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session,
		rpl::producer<DocumentId> emojiIdValue,
		Fn<void(DocumentId)> emojiIdChosen) {
	using StickerPlayer = HistoryView::StickerPlayer;
	struct State {
		DocumentId emojiId = 0;
		std::shared_ptr<StickerPlayer> player;
		bool playerUsesTextColor = false;
	};

	const auto outer = st::aiToneIconPreviewSize;
	const auto inner = st::aiToneIconPreviewInnerSize;
	const auto top = st::aiToneIconPreviewTopSkip;
	const auto bottom = st::aiToneIconPreviewBottomSkip;
	const auto holder = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container,
			outer + top + bottom));
	const auto button = Ui::CreateChild<Ui::AbstractButton>(holder);
	button->resize(outer, outer);
	button->show();

	holder->widthValue(
	) | rpl::on_next([=](int width) {
		button->move((width - outer) / 2, top);
	}, button->lifetime());

	const auto state = button->lifetime().make_state<State>();
	const auto emojiIdVar = button->lifetime().make_state<
		rpl::variable<DocumentId>>(std::move(emojiIdValue));

	emojiIdVar->value(
	) | rpl::on_next([=](DocumentId id) {
		state->emojiId = id;
	}, button->lifetime());

	emojiIdVar->value(
	) | rpl::map([=](DocumentId id) -> rpl::producer<DocumentData*> {
		if (!id) {
			return rpl::single((DocumentData*)nullptr);
		}
		return session->data().customEmojiManager().resolve(
			id
		) | rpl::map([=](not_null<DocumentData*> document) {
			return document.get();
		}) | rpl::map_error_to_done();
	}) | rpl::flatten_latest(
	) | rpl::map([=](DocumentData *document)
	-> rpl::producer<std::shared_ptr<StickerPlayer>> {
		if (!document) {
			return rpl::single(std::shared_ptr<StickerPlayer>());
		}
		const auto media = document->createMediaView();
		media->checkStickerLarge();
		media->goodThumbnailWanted();

		return rpl::single() | rpl::then(
			document->session().downloaderTaskFinished()
		) | rpl::filter([=] {
			return media->loaded();
		}) | rpl::take(1) | rpl::map([=] {
			auto result = std::shared_ptr<StickerPlayer>();
			const auto sticker = document->sticker();
			const auto size = QSize(inner, inner);
			if (sticker && sticker->isLottie()) {
				result = std::make_shared<HistoryView::LottiePlayer>(
					ChatHelpers::LottiePlayerFromDocument(
						media.get(),
						ChatHelpers::StickerLottieSize::StickerSet,
						size,
						Lottie::Quality::High));
			} else if (sticker && sticker->isWebm()) {
				result = std::make_shared<HistoryView::WebmPlayer>(
					media->owner()->location(),
					media->bytes(),
					size);
			} else {
				result = std::make_shared<HistoryView::StaticStickerPlayer>(
					media->owner()->location(),
					media->bytes(),
					size);
			}
			result->setRepaintCallback([=] { button->update(); });
			state->playerUsesTextColor
				= media->owner()->emojiUsesTextColor();
			return result;
		});
	}) | rpl::flatten_latest(
	) | rpl::on_next([=](std::shared_ptr<StickerPlayer> player) {
		state->player = std::move(player);
		button->update();
	}, button->lifetime());

	button->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(button);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::aiToneIconPreviewBg);
		p.drawEllipse(button->rect());
		if (state->player && state->player->ready()) {
			const auto color = state->playerUsesTextColor
				? st::windowFg->c
				: QColor(0, 0, 0, 0);
			const auto frame = state->player->frame(
				QSize(inner, inner),
				color,
				false,
				crl::now(),
				false).image;
			const auto sz = frame.size() / style::DevicePixelRatio();
			p.drawImage(
				QRect(
					(outer - sz.width()) / 2,
					(outer - sz.height()) / 2,
					sz.width(),
					sz.height()),
				frame);
			state->player->markFrameShown();
		} else if (!state->emojiId) {
			st::aiToneIconPreviewPlaceholder.paintInCenter(
				p,
				button->rect());
		}
	}, button->lifetime());

	if (emojiIdChosen) {
		button->setClickedCallback([=] {
			const auto controller = ChatHelpers::ResolveWindowDefault()(
				session);
			if (!controller) {
				return;
			}
			controller->uiShow()->showBox(Box(
				ChooseToneIconBox,
				controller,
				crl::guard(button, [=](DocumentId id) {
					emojiIdChosen(id);
				})));
		});
	} else {
		button->setAttribute(Qt::WA_TransparentForMouseEvents);
	}

	return button;
}

namespace {

void SetToneSubmitPending(
		const QPointer<Ui::RoundButton> &button,
		bool pending) {
	if (!button) {
		return;
	}
	if (pending) {
		button->clearState();
	}
	button->setDisabled(pending);
	button->setAttribute(Qt::WA_TransparentForMouseEvents, pending);
	button->setTextFgOverride(pending
		? anim::color(st::activeButtonBg, st::activeButtonFg, 0.5)
		: std::optional<QColor>());
}

QPointer<Ui::RoundButton> SetupToneBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		DocumentId initialEmojiId,
		const QString &initialName,
		const QString &initialPrompt,
		bool initialDisplayAuthor,
		rpl::producer<QString> title,
		rpl::producer<QString> submitLabel,
		Fn<void(DocumentId, QString, QString, bool)> submit,
		Fn<void()> requestDelete = nullptr) {
	box->setStyle(st::aiComposeBox);
	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);
	box->addTopButton(st::aiComposeBoxClose, [=] { box->closeBox(); });
	box->setTitle(std::move(title));

	const auto container = box->verticalLayout();
	const auto emojiId = container->lifetime().make_state<
		rpl::variable<DocumentId>>(initialEmojiId);

	const auto iconButton = AddAiToneIconPreview(
		container,
		session,
		emojiId->value(),
		[=](DocumentId id) { *emojiId = id; });

	const auto name = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::aiToneNameField,
			Ui::InputField::Mode::SingleLine,
			rpl::producer<QString>(),
			initialName),
		st::aiToneFieldsMargin);
	name->setMaxLength(session->appConfig().get<int>(
		u"aicompose_tone_title_length_max"_q,
		12));

	Ui::AddSkip(container, st::aiToneFieldsSkip);

	const auto promptSt = box->lifetime().make_state<style::InputField>(
		st::aiTonePromptField);
	{
		const auto &placeholderStyle = st::aiTonePlaceholderLabel.style;
		const auto fieldsMargin = st::aiToneFieldsMargin;
		const auto contentWidth = st::boxWideWidth
			- fieldsMargin.left() - fieldsMargin.right()
			- promptSt->textMargins.left() - promptSt->textMargins.right();
		auto measure = Ui::Text::String{ contentWidth / 2 };
		measure.setText(
			placeholderStyle,
			tr::lng_ai_compose_tone_prompt_placeholder(tr::now));
		const auto desiredMin = measure.countHeight(contentWidth)
			+ promptSt->textMargins.top()
			+ promptSt->textMargins.bottom();
		if (promptSt->heightMin < desiredMin) {
			promptSt->heightMin = desiredMin;
		}
		if (promptSt->heightMax < promptSt->heightMin) {
			promptSt->heightMax = promptSt->heightMin;
		}
	}

	const auto prompt = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			*promptSt,
			Ui::InputField::Mode::MultiLine,
			rpl::producer<QString>(),
			initialPrompt),
		st::aiToneFieldsMargin);
	prompt->setSubmitSettings(Ui::InputField::SubmitSettings::None);
	prompt->setMaxLength(session->appConfig().get<int>(
		u"aicompose_tone_prompt_length_max"_q,
		1024));

	AddAiComposeFieldDecor(name, tr::lng_ai_compose_tone_name_placeholder());
	const auto promptPlaceholder = AddAiComposeFieldDecor(
		prompt,
		tr::lng_ai_compose_tone_prompt_placeholder());

	const auto authorCheckbox = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_ai_compose_tone_author(tr::now),
			st::aiComposeEmojifyCheckbox,
			std::make_unique<Ui::RoundCheckView>(
				st::defaultCheck,
				initialDisplayAuthor)),
		st::aiToneAuthorCheckboxMargin,
		style::al_top);

	const auto deleteButton = requestDelete
		? box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				tr::lng_ai_compose_tone_delete(),
				st::aiToneDeleteButton),
			st::aiToneDeleteButtonMargin)
		: nullptr;
	if (deleteButton) {
		deleteButton->setFullRadius(true);
		deleteButton->setClickedCallback(std::move(requestDelete));
		box->widthValue(
		) | rpl::on_next([=](int width) {
			const auto &margin = st::aiToneDeleteButtonMargin;
			deleteButton->setFullWidth(
				width - margin.left() - margin.right());
		}, deleteButton->lifetime());
	}

	rpl::combine(
		prompt->topValue(),
		promptPlaceholder->heightValue(),
		box->getDelegate()->contentHeightMaxValue()
	) | rpl::on_next([=](int top, int phHeight, int contentHeight) {
		const auto pad = st::aiToneFieldPadding;
		const auto deleteBlock = deleteButton
			? (deleteButton->heightNoMargins()
				+ st::aiToneDeleteButtonMargin.top()
				+ st::aiToneDeleteButtonMargin.bottom())
			: 0;
		prompt->setMaxHeight(contentHeight
			- top
			- st::aiToneFieldsMargin.bottom()
			- authorCheckbox->heightNoMargins()
			- st::aiToneAuthorCheckboxMargin.top()
			- st::aiToneAuthorCheckboxMargin.bottom()
			- deleteBlock);
		prompt->setMinHeight(phHeight + pad.top() + pad.bottom());
	}, prompt->lifetime());

	box->setFocusCallback([=] {
		name->setFocusFast();
	});

	const auto warning = box->lifetime().make_state<Ui::WarningTooltip>();
	const auto save = [=] {
		const auto nameText = name->getLastText().trimmed();
		const auto promptText = prompt->getLastText().trimmed();
		const auto showWarning = [=](
				not_null<QWidget*> target,
				rpl::producer<TextWithEntities> text) {
			warning->show({
				.parent = box,
				.target = target,
				.text = std::move(text),
			});
		};
		if (!emojiId->current()) {
			showWarning(
				iconButton,
				tr::lng_ai_compose_tone_warn_icon(tr::marked));
			return;
		} else if (nameText.isEmpty()) {
			name->showError();
			showWarning(
				name,
				tr::lng_ai_compose_tone_warn_name(tr::marked));
			return;
		} else if (promptText.isEmpty()) {
			prompt->showError();
			showWarning(
				prompt,
				tr::lng_ai_compose_tone_warn_prompt(tr::marked));
			return;
		}
		warning->hide(anim::type::normal);
		submit(
			emojiId->current(),
			nameText,
			promptText,
			authorCheckbox->checked());
	};

	const auto submitBtn = box->addButton(std::move(submitLabel), save);
	submitBtn->setFullRadius(true);
	return submitBtn;
}

} // namespace

void CreateAiToneBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		Fn<void(Data::AiComposeTone)> saved) {
	struct State {
		QPointer<Ui::RoundButton> submitButton;
		bool pending = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->submitButton = SetupToneBox(
		box,
		session,
		DocumentId(0),
		QString(),
		QString(),
		false,
		tr::lng_ai_compose_create_tone_title(),
		tr::lng_ai_compose_tone_create(),
		[=](DocumentId emojiId,
				const QString &name,
				const QString &prompt,
				bool displayAuthor) {
			if (state->pending) {
				return;
			}
			state->pending = true;
			SetToneSubmitPending(state->submitButton, true);
			session->data().aiComposeTones().create(
				name,
				prompt,
				emojiId,
				displayAuthor,
				crl::guard(box, [=](Data::AiComposeTone tone) {
					const auto show = box->uiShow();
					box->closeBox();
					ShowToneToast(show, session, tone, true);
					if (saved) {
						saved(tone);
					}
				}),
				crl::guard(box, [=](const MTP::Error &error) {
					state->pending = false;
					SetToneSubmitPending(state->submitButton, false);
					if (error.type() == u"TONES_SAVED_TOO_MANY"_q) {
						ShowAiComposeToneLimitError(box->uiShow(), session);
					} else if (!MTP::IgnoreError(error)) {
						box->showToast(error.type());
					}
				}));
		},
		nullptr);
}

void EditAiToneBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const Data::AiComposeTone &tone,
		Fn<void(Data::AiComposeTone)> saved) {
	const auto toneCopy = tone;
	SetupToneBox(
		box,
		session,
		tone.emojiId,
		tone.title,
		tone.prompt,
		tone.authorId != 0,
		tr::lng_ai_compose_edit_tone_title(),
		tr::lng_ai_compose_tone_save(),
		[=](DocumentId emojiId,
				const QString &name,
				const QString &prompt,
				bool displayAuthor) {
			session->data().aiComposeTones().update(
				toneCopy,
				name,
				prompt,
				std::make_optional(emojiId),
				std::make_optional(displayAuthor),
				crl::guard(box, [=](Data::AiComposeTone updated) {
					const auto show = box->uiShow();
					box->closeBox();
					ShowToneToast(show, session, updated, false);
					if (saved) {
						saved(updated);
					}
				}));
		},
		[=] {
			ConfirmDeleteAiTone(
				box->uiShow(),
				session,
				toneCopy,
				[=] { box->closeBox(); });
		});
}

void ConfirmDeleteAiTone(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const Data::AiComposeTone &tone,
		Fn<void()> done) {
	if (!tone.creator) {
		show->show(Ui::MakeConfirmBox({
			.text = tr::lng_ai_compose_tone_remove_sure(),
			.confirmed = [=](Fn<void()> &&close) {
				close();
				session->data().aiComposeTones().save(
					tone,
					true,
					done);
			},
			.confirmText = tr::lng_box_remove(),
		}));
		return;
	}
	show->show(Ui::MakeConfirmBox({
		.text = tr::lng_ai_compose_tone_delete_sure(),
		.confirmed = [=](Fn<void()> &&close) {
			close();
			session->data().aiComposeTones().remove(tone, done);
		},
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_ai_compose_tone_delete(),
	}));
}

void ShowAiComposeToneLimitError(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session) {
	const auto limits = Data::PremiumLimits(session);
	const auto premium = session->premium();
	const auto premiumPossible = session->premiumPossible();
	const auto defaultLimit = limits.aiComposeSavedTonesDefault();
	const auto premiumLimit = limits.aiComposeSavedTonesPremium();
	const auto current = premium ? premiumLimit : defaultLimit;
	if (premium || !premiumPossible) {
		show->showToast(tr::lng_ai_compose_tone_saved_limit_final(
			tr::now,
			lt_count,
			current,
			tr::rich));
	} else {
		Settings::ShowPremiumPromoToast(
			Main::MakeSessionShow(show, session),
			ChatHelpers::ResolveWindowDefault(),
			tr::lng_ai_compose_tone_saved_limit(
				tr::now,
				lt_count,
				defaultLimit,
				lt_link,
				tr::bold(tr::lng_ai_compose_tone_saved_limit_link(
					tr::now,
					tr::link)),
				lt_premium_count,
				tr::bold(QString::number(premiumLimit)),
				tr::rich),
			u"ai_compose_tones"_q);
	}
}
