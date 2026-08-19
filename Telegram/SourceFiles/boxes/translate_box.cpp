/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/translate_box.h"
#include "boxes/translate_box_content.h"
#include "lang/translate_provider.h"

#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "core/ui_integration.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "iv/markdown/iv_markdown_article.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "iv/markdown/iv_markdown_view_widget.h"
#include "iv/iv_cached_media.h"
#include "iv/iv_rich_page.h"
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "spellcheck/platform/platform_language.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/effects/loading_element.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/basic_click_handlers.h"
#include "ui/integration.h"
#include "ui/power_saving.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"

#include "styles/style_boxes.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

constexpr auto kSkipAtLeastOneDuration = 3 * crl::time(1000);

void ActivateRichTranslateLink(
		not_null<Iv::Markdown::MarkdownDocumentWidget*> body,
		const Iv::Markdown::PreparedLink &link,
		Qt::MouseButton button,
		const QVariant &context) {
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return;
	}
	using Kind = Iv::Markdown::PreparedLinkKind;
	switch (link.kind) {
	case Kind::External: {
		if (link.target.isEmpty()) {
			return;
		}
		switch (link.entityType) {
		case EntityType::Url:
		case EntityType::CustomUrl:
		case EntityType::Email:
			break;
		default:
			return;
		}
		const auto handler = Ui::Integration::Instance().createLinkHandler(
			EntityLinkData{
				.text = (!link.copyText.isEmpty()
					? link.copyText
					: link.target),
				.data = link.target,
				.type = link.entityType,
				.shown = link.shown,
			},
			Ui::Text::MarkedContext());
		if (handler) {
			auto click = ClickContext();
			click.button = button;
			click.other = context;
			handler->onClick(std::move(click));
		}
	} break;
	case Kind::InstantViewPage: {
		auto target = link.target;
		if (!link.fragment.isEmpty()) {
			target += u"#"_q + link.fragment;
		}
		UrlClickHandler::Open(target, context);
	} break;
	case Kind::ToggleDetails:
		static_cast<void>(body->toggleDetails(link.target));
		break;
	default:
		break;
	}
}

[[nodiscard]] bool ActivateRichTranslateMedia(
		const Iv::Markdown::MediaActivation &activation,
		Qt::MouseButton button,
		const QVariant &context) {
	if (button != Qt::LeftButton && button != Qt::MiddleButton) {
		return false;
	}
	using Kind = Iv::Markdown::MediaActivationKind;
	switch (activation.kind) {
	case Kind::ExternalUrl:
		if (activation.url.isEmpty()) {
			return false;
		}
		HiddenUrlClickHandler::Open(activation.url, context);
		return true;
	case Kind::Photo:
		if (!activation.photo) {
			return false;
		}
		activation.photo->open(button);
		return true;
	case Kind::Document:
		if (!activation.document) {
			return false;
		}
		activation.document->open(button);
		return true;
	case Kind::OpenChannel:
		if (!activation.channel) {
			return false;
		}
		activation.channel->open(button);
		return true;
	case Kind::JoinChannel:
		if (!activation.channel) {
			return false;
		}
		activation.channel->join(button);
		return true;
	default:
		return false;
	}
}

void SetupRichArticleBody(
		not_null<GenericBox*> box,
		not_null<Iv::Markdown::MarkdownDocumentWidget*> body,
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		FullMsgId itemId) {
	auto clickHandlerContext = ClickHandlerContext();
	clickHandlerContext.itemId = itemId;
	clickHandlerContext.sessionWindow = base::make_weak(
		session->tryResolveWindow(peer));
	clickHandlerContext.show = box->uiShow();
	const auto context = QVariant::fromValue(clickHandlerContext);
	body->setClickHandlerContext(context);
	body->setLinkActivationCallback([=](
			const Iv::Markdown::PreparedLink &link,
			Qt::MouseButton button) {
		ActivateRichTranslateLink(body, link, button, context);
	});
	body->setMediaActivationCallback([=](
			const Iv::Markdown::MediaActivation &activation,
			Qt::MouseButton button) {
		return ActivateRichTranslateMedia(activation, button, context);
	});
	style::PaletteChanged() | rpl::on_next([=] {
		body->refreshPalette();
	}, body->lifetime());
}

[[nodiscard]] bool ShowRichArticlePage(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		not_null<Iv::Markdown::MarkdownDocumentWidget*> body,
		not_null<std::shared_ptr<Iv::Markdown::MarkdownArticle>*> article,
		std::shared_ptr<const Iv::RichPage> page) {
	const auto limits = Iv::ResolveRichMessageLimits(session);
	auto prepared = Iv::Markdown::TryPrepareNativeInstantView({
		.richPage = page,
		.mediaRuntime = Iv::CreateMessageMediaRuntime(
			session,
			itemId,
			[](QString) {},
			[](QString) {},
			::Data::FileOrigin()),
		.dimensionsOverride = Iv::Markdown::CaptureMarkdownPrepareDimensions(
			st::translateBoxMarkdown),
		.tableRenderLimits
			= Iv::Markdown::PrepareTableRenderLimitsForRichMessage(limits),
	});
	if (!prepared.supported()) {
		return false;
	}
	if (!*article) {
		*article = std::make_shared<Iv::Markdown::MarkdownArticle>(
			st::translateBoxMarkdown);
		(*article)->setContent(std::move(prepared.content));
		body->setArticle(*article);
	} else {
		(*article)->setContent(std::move(prepared.content));
		body->articleContentChanged();
	}
	return true;
}

[[nodiscard]] bool TranslateRichBox(
		not_null<GenericBox*> box,
		not_null<PeerData*> peer,
		MsgId msgId,
		std::shared_ptr<const Iv::RichPage> page,
		TextWithEntities summaryText,
		bool hasCopyRestriction) {
	struct State {
		State(not_null<Main::Session*> session)
		: api(&session->mtp()) {
		}

		MTP::Sender api;
		rpl::variable<LanguageId> to;
		mtpRequestId requestId = 0;
		std::shared_ptr<Iv::Markdown::MarkdownArticle> original;
		std::shared_ptr<Iv::Markdown::MarkdownArticle> translated;
	};
	const auto session = &peer->session();
	const auto itemId = FullMsgId(peer->id, msgId);
	const auto state = box->lifetime().make_state<State>(session);
	auto originalBody = object_ptr<Iv::Markdown::MarkdownDocumentWidget>(box);
	if (!ShowRichArticlePage(
			session,
			itemId,
			originalBody.data(),
			&state->original,
			page)) {
		return false;
	}
	state->to = ChooseTranslateTo(peer->owner().history(peer));

	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	const auto container = box->verticalLayout();

	const auto textContext = Core::TextContext({ .session = session });

	auto to = state->to.value() | rpl::start_spawning(box->lifetime());
	const auto toTitle = rpl::duplicate(to) | rpl::map(LanguageName);
	const auto toDirection = rpl::duplicate(to) | rpl::map([=](
			LanguageId id) {
		return id.locale().textDirection() == Qt::RightToLeft;
	});

	const auto &stLabel = st::aboutLabel;
	const auto lineHeight = stLabel.style.lineHeight;

	Ui::AddSkip(container);

	const auto animationsPaused = [] {
		using Which = FlatLabel::WhichAnimationsPaused;
		const auto emoji = On(PowerSaving::kEmojiChat);
		const auto spoiler = On(PowerSaving::kChatSpoiler);
		return emoji
			? (spoiler ? Which::All : Which::CustomEmoji)
			: (spoiler ? Which::Spoiler : Which::None);
	};
	const auto summary = box->addRow(object_ptr<SlideWrap<FlatLabel>>(
		box,
		object_ptr<FlatLabel>(box, stLabel)));
	if (hasCopyRestriction) {
		summary->entity()->setContextMenuHook([](auto&&) {
		});
	}
	summary->entity()->setAnimationsPausedCallback(animationsPaused);
	summary->entity()->setMarkedText(summaryText, textContext);
	summary->setMinimalHeight(lineHeight);
	summary->hide(anim::type::instant);

	const auto show = Ui::CreateChild<FadeWrap<TranslateShowButton>>(
		container.get(),
		object_ptr<TranslateShowButton>(container));
	rpl::combine(
		container->widthValue(),
		summary->geometryValue()
	) | rpl::on_next([=](int width, const QRect &rect) {
		show->moveToLeft(
			width - show->width() - st::boxRowPadding.right(),
			rect.y() + std::abs(lineHeight - show->height()) / 2);
	}, show->lifetime());

	const auto original = box->addRow(
		object_ptr<SlideWrap<Iv::Markdown::MarkdownDocumentWidget>>(
			box,
			std::move(originalBody)),
		style::margins());
	original->hide(anim::type::instant);
	SetupRichArticleBody(box, original->entity(), session, peer, itemId);

	show->entity()->clicks() | rpl::on_next([=] {
		show->hide(anim::type::instant);
		summary->setMinimalHeight(0);
		summary->hide(anim::type::normal);
		original->show(anim::type::normal);
	}, show->lifetime());

	Ui::AddSkip(container);
	Ui::AddSkip(container);
	Ui::AddDivider(container);
	Ui::AddSkip(container);

	{
		const auto padding = st::defaultSubsectionTitlePadding;
		const auto subtitle = Ui::AddSubsectionTitle(container, std::move(toTitle));

		rpl::duplicate(to) | rpl::on_next([=] {
			subtitle->resizeToWidth(container->width()
				- padding.left()
				- padding.right());
		}, subtitle->lifetime());
	}

	const auto translated = box->addRow(
		object_ptr<SlideWrap<Iv::Markdown::MarkdownDocumentWidget>>(
			box,
			object_ptr<Iv::Markdown::MarkdownDocumentWidget>(box)),
		style::margins());
	translated->hide(anim::type::instant);
	SetupRichArticleBody(box, translated->entity(), session, peer, itemId);

	const auto error = box->addRow(object_ptr<SlideWrap<FlatLabel>>(
		box,
		object_ptr<FlatLabel>(box, stLabel)));
	error->hide(anim::type::instant);

	constexpr auto kMaxLines = 3;
	const auto loading = box->addRow(object_ptr<SlideWrap<RpWidget>>(
		box,
		CreateLoadingTextWidget(
			box,
			st::aboutLabel.style,
			kMaxLines,
			std::move(toDirection))));

	const auto showError = [=] {
		error->entity()->setMarkedText(
			tr::italic(tr::lng_translate_box_error(tr::now)),
			textContext);
		error->show(anim::type::instant);
		loading->hide(anim::type::instant);
	};
	const auto showResult = [=](std::shared_ptr<const Iv::RichPage> result) {
		if (result
			&& ShowRichArticlePage(
				session,
				itemId,
				translated->entity(),
				&state->translated,
				result)) {
			translated->show(anim::type::instant);
			loading->hide(anim::type::instant);
		} else {
			showError();
		}
	};
	const auto send = [=](LanguageId id) {
		state->api.request(base::take(state->requestId)).cancel();
		loading->show(anim::type::instant);
		translated->hide(anim::type::instant);
		error->hide(anim::type::instant);
		using Flag = MTPmessages_TranslateRichMessage::Flag;
		state->requestId = state->api.request(
			MTPmessages_TranslateRichMessage(
				MTP_flags(Flag::f_peer | Flag::f_id),
				peer->input(),
				MTP_vector<MTPint>(1, MTP_int(msgId)),
				MTPVector<MTPInputRichMessage>(),
				MTP_string(id.twoLetterCode()),
				MTPstring())
		).done([=](const MTPmessages_TranslatedRichMessage &result) {
			state->requestId = 0;
			const auto &list = result.data().vresult().v;
			showResult(list.isEmpty()
				? nullptr
				: Iv::ParseRichPage(session, list.front()));
		}).fail([=](const MTP::Error &) {
			state->requestId = 0;
			showResult(nullptr);
		}).send();
	};
	std::move(to) | rpl::on_next(send, box->lifetime());

	box->addLeftButton(tr::lng_settings_language(), [=] {
		if (loading->toggled()) {
			return;
		}
		box->uiShow()->showBox(ChooseTranslateToBox(
			state->to.current(),
			crl::guard(box, [=](LanguageId id) { state->to = id; })));
	});
	return true;
}

} // namespace

void TranslateBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		MsgId msgId,
		TextWithEntities text,
		bool hasCopyRestriction) {
	struct State {
		State(not_null<Main::Session*> session)
		: provider(CreateTranslateProvider(session)) {
		}

		std::unique_ptr<TranslateProvider> provider;
		rpl::variable<LanguageId> to;
	};
	const auto state = box->lifetime().make_state<State>(&peer->session());
	if (IsServerMsgId(msgId) && state->provider->supportsMessageId()) {
		if (const auto item = peer->owner().message(peer->id, msgId)) {
			if (const auto page = item->richPage()) {
				if (TranslateRichBox(
						box,
						peer,
						msgId,
						page,
						text,
						hasCopyRestriction)) {
					return;
				}
			}
		}
	}
	state->to = ChooseTranslateTo(peer->owner().history(peer));
	const auto request = std::make_shared<TranslateProviderRequest>(
		PrepareTranslateProviderRequest(
			state->provider.get(),
			peer,
			msgId,
			std::move(text)));

	TranslateBoxContent(box, {
		.text = request->text,
		.hasCopyRestriction = hasCopyRestriction,
		.textContext = Core::TextContext({ .session = &peer->session() }),
		.to = state->to.value(),
		.chooseTo = [=] {
			box->uiShow()->showBox(ChooseTranslateToBox(
				state->to.current(),
				crl::guard(box, [=](LanguageId id) { state->to = id; })));
		},
		.request = [=](
				LanguageId to,
				Fn<void(TranslateBoxContentResult)> done) {
			state->provider->request(
				*request,
				to,
				[done = std::move(done)](TranslateProviderResult result) {
					using ProviderError = TranslateProviderError;
					using UiError = TranslateBoxContentError;
					done(TranslateBoxContentResult{
						.text = std::move(result.text),
						.error = (result.error
								== ProviderError::LocalLanguagePackMissing)
							? UiError::LocalLanguagePackMissing
							: (result.error == ProviderError::None)
							? UiError::None
							: UiError::Unknown,
					});
				});
		},
	});
}

bool SkipTranslate(TextWithEntities textWithEntities) {
	const auto &text = textWithEntities.text;
	if (text.isEmpty()) {
		return true;
	}
	if (!Core::App().settings().translateButtonEnabled()) {
		return true;
	}
	constexpr auto kFirstChunk = size_t(100);
	auto hasLetters = (text.size() >= kFirstChunk);
	for (auto i = 0; i < kFirstChunk; i++) {
		if (i >= text.size()) {
			break;
		}
		if (text.at(i).isLetter()) {
			hasLetters = true;
			break;
		}
	}
	if (!hasLetters) {
		return true;
	}
#ifndef TDESKTOP_DISABLE_SPELLCHECK
	const auto result = Platform::Language::Recognize(text);
	const auto skip = Core::App().settings().skipTranslationLanguages();
	return result.known() && ranges::contains(skip, result);
#else
	return false;
#endif
}

object_ptr<BoxContent> EditSkipTranslationLanguages() {
	auto title = tr::lng_translate_settings_choose();
	const auto selected = std::make_shared<std::vector<LanguageId>>(
		Core::App().settings().skipTranslationLanguages());
	const auto weak = std::make_shared<base::weak_qptr<BoxContent>>();
	const auto check = [=](LanguageId id) {
		const auto already = ranges::contains(*selected, id);
		if (already) {
			selected->erase(ranges::remove(*selected, id), selected->end());
		} else {
			selected->push_back(id);
		}
		if (already && selected->empty()) {
			if (const auto strong = weak->get()) {
				strong->showToast(
					tr::lng_translate_settings_one(tr::now),
					kSkipAtLeastOneDuration);
			}
			return false;
		}
		return true;
	};
	auto result = Box(ChooseLanguageBox, std::move(title), [=](
			std::vector<LanguageId> &&list) {
		Core::App().settings().setSkipTranslationLanguages(
			std::move(list));
		Core::App().saveSettingsDelayed();
	}, *selected, true, check);
	*weak = result.data();
	return result;
}

object_ptr<BoxContent> ChooseTranslateToBox(
		LanguageId bringUp,
		Fn<void(LanguageId)> callback) {
	auto &settings = Core::App().settings();
	auto selected = std::vector<LanguageId>{
		settings.translateTo(),
	};
	for (const auto &id : settings.skipTranslationLanguages()) {
		if (id != selected.front()) {
			selected.push_back(id);
		}
	}
	if (bringUp && ranges::contains(selected, bringUp)) {
		selected.push_back(bringUp);
	}
	return Box(ChooseLanguageBox, tr::lng_languages(), [=](
			const std::vector<LanguageId> &ids) {
		Expects(!ids.empty());

		const auto id = ids.front();
		Core::App().settings().setTranslateTo(id);
		Core::App().saveSettingsDelayed();
		callback(id);
	}, selected, false, nullptr);
}

LanguageId ChooseTranslateTo(not_null<History*> history) {
	return ChooseTranslateTo(history->translateOfferedFrom());
}

LanguageId ChooseTranslateTo(LanguageId offeredFrom) {
	auto &settings = Core::App().settings();
	return ChooseTranslateTo(
		offeredFrom,
		settings.translateTo(),
		settings.skipTranslationLanguages());
}

LanguageId ChooseTranslateTo(
		not_null<History*> history,
		LanguageId savedTo,
		const std::vector<LanguageId> &skip) {
	return ChooseTranslateTo(history->translateOfferedFrom(), savedTo, skip);
}

LanguageId ChooseTranslateTo(
		LanguageId offeredFrom,
		LanguageId savedTo,
		const std::vector<LanguageId> &skip) {
	return (offeredFrom != savedTo) ? savedTo : skip.front();
}

} // namespace Ui
