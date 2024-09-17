/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_web_page.h"

#include "core/application.h"
#include "countries/countries_instance.h"
#include "base/qt/qt_key_modifiers.h"
#include "window/window_session_controller.h"
#include "iv/iv_instance.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/components/sponsored_messages.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_file_click_handler.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_reply.h"
#include "history/view/history_view_sponsored_click_handler.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_sponsored.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/power_saving.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kMaxOriginalEntryLines = 8192;
constexpr auto kFactcheckCollapsedLines = 3;
constexpr auto kStickerSetLines = 3;
constexpr auto kFactcheckAboutDuration = 5 * crl::time(1000);

[[nodiscard]] int ArticleThumbWidth(not_null<PhotoData*> thumb, int height) {
	const auto size = thumb->location(Data::PhotoSize::Thumbnail);
	return size.height()
		? std::max(std::min(height * size.width() / size.height(), height), 1)
		: 1;
}

[[nodiscard]] int ArticleThumbHeight(
		not_null<Data::PhotoMedia*> thumb,
		int width) {
	const auto size = thumb->size(Data::PhotoSize::Thumbnail);
	return size.width()
		? std::max(size.height() * width / size.width(), 1)
		: 1;
}

[[nodiscard]] std::vector<std::unique_ptr<Data::Media>> PrepareCollageMedia(
		not_null<HistoryItem*> parent,
		const WebPageCollage &data) {
	auto result = std::vector<std::unique_ptr<Data::Media>>();
	result.reserve(data.items.size());
	const auto spoiler = false;
	for (const auto &item : data.items) {
		if (const auto document = std::get_if<DocumentData*>(&item)) {
			const auto skipPremiumEffect = false;
			result.push_back(std::make_unique<Data::MediaFile>(
				parent,
				*document,
				skipPremiumEffect,
				spoiler,
				/*ttlSeconds = */0));
		} else if (const auto photo = std::get_if<PhotoData*>(&item)) {
			result.push_back(std::make_unique<Data::MediaPhoto>(
				parent,
				*photo,
				spoiler));
		} else {
			return {};
		}
		if (!result.back()->canBeGrouped()) {
			return {};
		}
	}
	return result;
}

[[nodiscard]] QString ExtractHash(
		not_null<WebPageData*> webpage,
		const TextWithEntities &text) {
	const auto simplify = [](const QString &url) {
		auto result = url.split('#')[0].toLower();
		if (result.endsWith('/')) {
			result.chop(1);
		}
		const auto prefixes = { u"http://"_q, u"https://"_q };
		for (const auto &prefix : prefixes) {
			if (result.startsWith(prefix)) {
				result = result.mid(prefix.size());
				break;
			}
		}
		return result;
	};
	const auto simplified = simplify(webpage->url);
	for (const auto &entity : text.entities) {
		const auto link = (entity.type() == EntityType::Url)
			? text.text.mid(entity.offset(), entity.length())
			: (entity.type() == EntityType::CustomUrl)
			? entity.data()
			: QString();
		if (simplify(link) == simplified) {
			const auto i = link.indexOf('#');
			return (i > 0) ? link.mid(i + 1) : QString();
		}
	}
	return QString();
}

[[nodiscard]] ClickHandlerPtr IvClickHandler(
		not_null<WebPageData*> webpage,
		const TextWithEntities &text) {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (const auto iv = webpage->iv.get()) {
				const auto hash = ExtractHash(webpage, text);
				Core::App().iv().show(controller, iv, hash);
				return;
			} else {
				HiddenUrlClickHandler::Open(webpage->url, context.other);
			}
		}
	});
}

[[nodiscard]] ClickHandlerPtr AboutSponsoredClickHandler() {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			Menu::ShowSponsoredAbout(controller->uiShow());
		}
	});
}

[[nodiscard]] QString LookupFactcheckCountryIso2(
		not_null<HistoryItem*> item) {
	const auto info = item->Get<HistoryMessageFactcheck>();
	return info ? info->data.country : QString();
}

[[nodiscard]] QString LookupFactcheckCountryName(const QString &iso2) {
	const auto name = Countries::Instance().countryNameByISO2(iso2);
	return name.isEmpty() ? iso2 : name;
}

[[nodiscard]] ClickHandlerPtr AboutFactcheckClickHandler(QString iso2) {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		const auto show = my.show
			? my.show
			: controller
			? controller->uiShow()
			: nullptr;
		if (show) {
			const auto country = LookupFactcheckCountryName(iso2);
			show->showToast({
				.text = {
					tr::lng_factcheck_about(tr::now, lt_country, country)
				},
				.duration = kFactcheckAboutDuration,
			});
		}
	});
}

[[nodiscard]] ClickHandlerPtr ToggleFactcheckClickHandler(
		not_null<Element*> view) {
	const auto weak = base::make_weak(view);
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		if (const auto strong = weak.get()) {
			if (const auto factcheck = strong->Get<Factcheck>()) {
				factcheck->expanded = factcheck->expanded ? 0 : 1;
				strong->history()->owner().requestViewResize(strong);
			}
		}
	});
}

[[nodiscard]] TextWithEntities PageToPhrase(not_null<WebPageData*> page) {
	const auto type = page->type;
	const auto text = Ui::Text::Upper(page->iv
		? tr::lng_view_button_iv(tr::now)
		: (type == WebPageType::Theme)
		? tr::lng_view_button_theme(tr::now)
		: (type == WebPageType::Story)
		? tr::lng_view_button_story(tr::now)
		: (type == WebPageType::Message)
		? tr::lng_view_button_message(tr::now)
		: (type == WebPageType::Group)
		? tr::lng_view_button_group(tr::now)
		: (type == WebPageType::WallPaper)
		? tr::lng_view_button_background(tr::now)
		: (type == WebPageType::Channel)
		? tr::lng_view_button_channel(tr::now)
		: (type == WebPageType::GroupWithRequest
			|| type == WebPageType::ChannelWithRequest)
		? tr::lng_view_button_request_join(tr::now)
		: (type == WebPageType::GroupBoost
			|| type == WebPageType::ChannelBoost)
		? tr::lng_view_button_boost(tr::now)
		: (type == WebPageType::Giftcode)
		? tr::lng_view_button_giftcode(tr::now)
		: (type == WebPageType::VoiceChat)
		? tr::lng_view_button_voice_chat(tr::now)
		: (type == WebPageType::Livestream)
		? tr::lng_view_button_voice_chat_channel(tr::now)
		: (type == WebPageType::Bot)
		? tr::lng_view_button_bot(tr::now)
		: (type == WebPageType::User)
		? tr::lng_view_button_user(tr::now)
		: (type == WebPageType::BotApp)
		? tr::lng_view_button_bot_app(tr::now)
		: (page->stickerSet && page->stickerSet->isEmoji)
		? tr::lng_view_button_emojipack(tr::now)
		: (type == WebPageType::StickerSet)
		? tr::lng_view_button_stickerset(tr::now)
		: QString());
	if (page->iv) {
		const auto manager = &page->owner().customEmojiManager();
		const auto &icon = st::historyIvIcon;
		const auto padding = st::historyIvIconPadding;
		return Ui::Text::SingleCustomEmoji(
			manager->registerInternalEmoji(icon, padding)
		).append(text);
	}
	return { text };
}

[[nodiscard]] bool HasButton(not_null<WebPageData*> webpage) {
	const auto type = webpage->type;
	return webpage->iv
		|| (type == WebPageType::Message)
		|| (type == WebPageType::Group)
		|| (type == WebPageType::GroupWithRequest)
		|| (type == WebPageType::GroupBoost)
		|| (type == WebPageType::Channel)
		|| (type == WebPageType::ChannelBoost)
		|| (type == WebPageType::ChannelWithRequest)
		|| (type == WebPageType::Giftcode)
		// || (type == WebPageType::Bot)
		|| (type == WebPageType::User)
		|| (type == WebPageType::VoiceChat)
		|| (type == WebPageType::Livestream)
		|| (type == WebPageType::BotApp)
		|| ((type == WebPageType::Theme)
			&& webpage->document
			&& webpage->document->isTheme())
		|| ((type == WebPageType::Story)
			&& (webpage->photo || webpage->document))
		|| ((type == WebPageType::WallPaper)
			&& webpage->document
			&& webpage->document->isWallPaper())
		|| (type == WebPageType::StickerSet);
}

} // namespace

WebPage::WebPage(
	not_null<Element*> parent,
	not_null<WebPageData*> data,
	MediaWebPageFlags flags)
: Media(parent)
, _st(data->type == WebPageType::Factcheck
	? st::factcheckPage
	: st::historyPagePreview)
, _data(data)
, _flags(flags)
, _siteName(st::msgMinWidth - _st.padding.left() - _st.padding.right())
, _title(st::msgMinWidth - _st.padding.left() - _st.padding.right())
, _description(st::msgMinWidth - _st.padding.left() - _st.padding.right()) {
	history()->owner().registerWebPageView(_data, _parent);
}

void WebPage::setupAdditionalData() {
	if (_flags & MediaWebPageFlag::Sponsored) {
		_additionalData = std::make_unique<AdditionalData>(SponsoredData());
		const auto raw = sponsoredData();
		const auto session = &_data->session();
		const auto id = _parent->data()->fullId();
		const auto details = session->sponsoredMessages().lookupDetails(id);
		const auto link = details.link;
		raw->buttonText = details.buttonText;
		raw->isLinkInternal = details.isLinkInternal ? 1 : 0;
		raw->backgroundEmojiId = details.backgroundEmojiId;
		raw->colorIndex = details.colorIndex;
		raw->canReport = details.canReport ? 1 : 0;
		raw->hasMedia = (details.mediaPhotoId || details.mediaDocumentId)
			? 1
			: 0;
		raw->link = std::make_shared<LambdaClickHandler>([=] {
			session->sponsoredMessages().clicked(id, false, false);
			UrlClickHandler::Open(link);
		});
		if (!_attach) {
			const auto maybePhoto = details.mediaPhotoId
				? session->data().photo(details.mediaPhotoId).get()
				: nullptr;
			const auto maybeDocument = details.mediaDocumentId
				? session->data().document(
					details.mediaDocumentId).get()
				: nullptr;
			_attach = CreateAttach(
				_parent,
				maybeDocument,
				maybePhoto,
				_collage,
				_data->url);
		}
		if (_attach) {
			if (_attach->getPhoto()) {
				raw->mediaLink = std::make_shared<LambdaClickHandler>([=] {
					session->sponsoredMessages().clicked(id, true, false);
					UrlClickHandler::Open(link);
				});
			} else if (const auto document = _attach->getDocument()) {
				const auto delegate = _parent->delegate();
				raw->mediaLink = document->isVideoFile()
					? std::make_shared<LambdaClickHandler>([=] {
						session->sponsoredMessages().clicked(id, true, false);
						delegate->elementOpenDocument(document, id, true);
					})
					: std::make_shared<LambdaClickHandler>([=] {
						session->sponsoredMessages().clicked(id, true, false);
						UrlClickHandler::Open(link);
					});
			}
		}
	} else if (_data->stickerSet) {
		_additionalData = std::make_unique<AdditionalData>(StickerSetData());
		const auto raw = stickerSetData();
		for (const auto &sticker : _data->stickerSet->items) {
			if (!sticker->sticker()) {
				continue;
			}
			raw->views.push_back(
				std::make_unique<Sticker>(_parent, sticker, true));
		}
		const auto side = std::ceil(std::sqrt(raw->views.size()));
		const auto box = UnitedLineHeight() * kStickerSetLines;
		const auto single = box / side;
		for (const auto &view : raw->views) {
			view->setWebpagePart();
			view->initSize(single);
		}
	} else if (_data->type == WebPageType::Factcheck) {
		_additionalData = std::make_unique<AdditionalData>(FactcheckData());
	}
}

QSize WebPage::countOptimalSize() {
	if (_data->pendingTill || _data->failed) {
		return { 0, 0 };
	}
	setupAdditionalData();

	const auto sponsored = sponsoredData();
	const auto factcheck = factcheckData();

	// Detect _openButtonWidth before counting paddings.
	_openButton = Ui::Text::String();
	if (HasButton(_data)) {
		const auto context = Core::MarkedTextContext{
			.session = &_data->session(),
			.customEmojiRepaint = [] {},
			.customEmojiLoopLimit = 1,
		};
		_openButton.setMarkedText(
			st::semiboldTextStyle,
			PageToPhrase(_data),
			kMarkupTextOptions,
			context);
	} else if (sponsored && !sponsored->buttonText.isEmpty()) {
		_openButton.setText(
			st::semiboldTextStyle,
			Ui::Text::Upper(sponsored->buttonText));
	}

	const auto padding = inBubblePadding() + innerMargin();
	const auto versionChanged = (_dataVersion != _data->version);
	if (versionChanged) {
		_dataVersion = _data->version;
		_openl = nullptr;
		_attach = nullptr;
		const auto item = _parent->data();
		_collage = PrepareCollageMedia(item, _data->collage);
		const auto min = st::msgMinWidth - rect::m::sum::h(_st.padding);
		_siteName = Ui::Text::String(min);
		_title = Ui::Text::String(min);
		_description = Ui::Text::String(min);
		if (factcheck) {
			factcheck->footer = Ui::Text::String(
				st::factcheckFooterStyle,
				tr::lng_factcheck_bottom(
					tr::now,
					lt_country,
					LookupFactcheckCountryName(
						LookupFactcheckCountryIso2(item))),
				kDefaultTextOptions,
				min);
		}
	}
	const auto lineHeight = UnitedLineHeight();

	if (!_openl && (!_data->url.isEmpty() || sponsored || factcheck)) {
		const auto original = _parent->data()->originalText();
		const auto previewOfHiddenUrl = [&] {
			if (_data->type == WebPageType::BotApp) {
				// Bot Web Apps always show confirmation on hidden urls.
				//
				// But from the dedicated "Open App" button we don't want
				// to request users confirmation on non-first app opening.
				return false;
			}
			const auto simplify = [](const QString &url) {
				auto result = url.toLower();
				if (result.endsWith('/')) {
					result.chop(1);
				}
				const auto prefixes = { u"http://"_q, u"https://"_q };
				for (const auto &prefix : prefixes) {
					if (result.startsWith(prefix)) {
						result = result.mid(prefix.size());
						break;
					}
				}
				return result;
			};
			const auto simplified = simplify(_data->url);
			for (const auto &entity : original.entities) {
				if (entity.type() != EntityType::Url) {
					continue;
				}
				const auto link = original.text.mid(
					entity.offset(),
					entity.length());
				if (simplify(link) == simplified) {
					return false;
				}
			}
			return true;
		}();
		if (sponsored) {
			_openl = SponsoredLink(_data->url, sponsored->isLinkInternal);
			if (sponsored->canReport) {
				sponsored->hint.link = AboutSponsoredClickHandler();
			}
		} else if (factcheck) {
			const auto item = _parent->data();
			const auto iso2 = LookupFactcheckCountryIso2(item);
			if (!iso2.isEmpty()) {
				factcheck->hint.link = AboutFactcheckClickHandler(iso2);
			}
		} else {
			_openl = _data->iv
				? IvClickHandler(_data, original)
				: (previewOfHiddenUrl || UrlClickHandler::IsSuspicious(
					_data->url))
				? std::make_shared<HiddenUrlClickHandler>(_data->url)
				: std::make_shared<UrlClickHandler>(_data->url, true);
			if (_data->document
				&& (_data->document->isWallPaper()
					|| _data->document->isTheme())) {
				_openl = std::make_shared<DocumentWrappedClickHandler>(
					std::move(_openl),
					_data->document,
					_parent->data()->fullId());
			}
		}
	}

	// init layout
	const auto title = TextUtilities::SingleLine(_data->title.isEmpty()
		? _data->author
		: _data->title);
	using Flag = MediaWebPageFlag;
	if (_data->hasLargeMedia && (_flags & Flag::ForceLargeMedia)) {
		_asArticle = 0;
	} else if (_data->hasLargeMedia && (_flags & Flag::ForceSmallMedia)) {
		_asArticle = 1;
	} else {
		_asArticle = _data->computeDefaultSmallMedia();
	}
	if (sponsored && sponsored->hasMedia) {
		_asArticle = 0;
	}

	// init attach
	if (!_attach && !_asArticle) {
		_attach = CreateAttach(
			_parent,
			_data->document,
			_data->photo,
			_collage,
			_data->url);
	}

	// init strings
	if (_description.isEmpty() && !_data->description.text.isEmpty()) {
		const auto &text = _data->description;

		if (isLogEntryOriginal()) {
			// Fix layout for small bubbles
			// (narrow media caption edit log entries).
			_description = Ui::Text::String(st::minPhotoSize
				- rect::m::sum::h(padding));
		}
		using MarkedTextContext = Core::MarkedTextContext;
		auto context = MarkedTextContext{
			.session = &history()->session(),
			.customEmojiRepaint = [=] { _parent->customEmojiRepaint(); },
		};
		if (_data->siteName == u"Twitter"_q) {
			context.type = MarkedTextContext::HashtagMentionType::Twitter;
		} else if (_data->siteName == u"Instagram"_q) {
			context.type = MarkedTextContext::HashtagMentionType::Instagram;
		}
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			text,
			Ui::WebpageTextDescriptionOptions(),
			context);
	}
	const auto siteName = _data->displayedSiteName();
	if (!siteName.isEmpty()) {
		_siteNameLines = 1;
		_siteName.setMarkedText(
			st::webPageTitleStyle,
			Ui::Text::Link(siteName, _data->url),
			Ui::WebpageTextTitleOptions());
	}
	if (_title.isEmpty() && !title.isEmpty()) {
		if (!_siteNameLines && !_data->url.isEmpty()) {
			_title.setMarkedText(
				st::webPageTitleStyle,
				Ui::Text::Link(title, _data->url),
				Ui::WebpageTextTitleOptions());

		} else {
			_title.setText(
				st::webPageTitleStyle,
				title,
				Ui::WebpageTextTitleOptions());
		}
	}

	// init dimensions
	const auto skipBlockWidth = _parent->skipBlockWidth();
	auto maxWidth = skipBlockWidth;
	auto minHeight = 0;

	const auto siteNameHeight = _siteName.isEmpty() ? 0 : lineHeight;
	const auto titleMinHeight = _title.isEmpty() ? 0 : lineHeight;
	const auto factcheckMetrics = factcheck
		? computeFactcheckMetrics(_description.minHeight())
		: FactcheckMetrics();
	const auto descMaxLines = factcheck
		? factcheckMetrics.lines
		: isLogEntryOriginal()
		? kMaxOriginalEntryLines
		: (3 + (siteNameHeight ? 0 : 1) + (titleMinHeight ? 0 : 1));
	const auto descriptionMinHeight = _description.isEmpty()
		? 0
		: std::min(_description.minHeight(), descMaxLines * lineHeight);
	const auto articleMinHeight = siteNameHeight
		+ titleMinHeight
		+ descriptionMinHeight;
	const auto articlePhotoMaxWidth = _asArticle
		? st::webPagePhotoDelta
			+ std::max(
				ArticleThumbWidth(_data->photo, articleMinHeight),
				lineHeight)
		: 0;

	if (!_siteName.isEmpty()) {
		accumulate_max(maxWidth, _siteName.maxWidth() + articlePhotoMaxWidth);
		minHeight += lineHeight;
	}
	if (!_title.isEmpty()) {
		accumulate_max(maxWidth, _title.maxWidth() + articlePhotoMaxWidth);
		minHeight += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		accumulate_max(
			maxWidth,
			_description.maxWidth() + articlePhotoMaxWidth);
		minHeight += descriptionMinHeight;
	}
	if (factcheck && factcheck->expanded) {
		accumulate_max(maxWidth, factcheck->footer.maxWidth());
		minHeight += st::factcheckFooterSkip + factcheck->footer.minHeight();
	}
	if (_attach) {
		const auto attachAtTop = (_siteName.isEmpty()
				&& _title.isEmpty()
				&& _description.isEmpty())
			|| (sponsored && sponsored->hasMedia);
		if (!attachAtTop) {
			minHeight += st::mediaInBubbleSkip;
		}

		_attach->initDimensions();
		const auto bubble = _attach->bubbleMargins();
		auto maxMediaWidth = _attach->maxWidth() - rect::m::sum::h(bubble);
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(maxWidth, maxMediaWidth);
		minHeight += _attach->minHeight() - rect::m::sum::v(bubble);
	}
	if (_data->type == WebPageType::Video && _data->duration) {
		_duration = Ui::FormatDurationText(_data->duration);
		_durationWidth = st::msgDateFont->width(_duration);
	}
	if (!_openButton.isEmpty()) {
		maxWidth += rect::m::sum::h(st::historyPageButtonPadding)
			+ _openButton.maxWidth();
	}
	maxWidth += rect::m::sum::h(padding);
	minHeight += rect::m::sum::v(padding);

	if (_asArticle) {
		minHeight = resizeGetHeight(maxWidth);
	}
	if (const auto hint = hintData()) {
		hint->widthBefore = st::webPageTitleStyle.font->width(siteName);
		const auto &font = st::webPageSponsoredHintFont;
		hint->text = sponsored
			? tr::lng_sponsored_message_revenue_button(tr::now)
			: tr::lng_factcheck_whats_this(tr::now);
		hint->size = QSize(
			font->width(hint->text) + font->height,
			font->height);
		maxWidth += hint->size.width();
	}
	return { maxWidth, minHeight };
}

QSize WebPage::countCurrentSize(int newWidth) {
	if (_data->pendingTill || _data->failed) {
		return { newWidth, minHeight() };
	}

	const auto padding = inBubblePadding() + innerMargin();
	const auto innerWidth = newWidth - rect::m::sum::h(padding);
	auto newHeight = 0;

	const auto stickerSet = stickerSetData();
	const auto factcheck = factcheckData();
	const auto sponsored = sponsoredData();
	const auto specialRightPix = ((sponsored && !sponsored->hasMedia)
		|| stickerSet);
	const auto lineHeight = UnitedLineHeight();
	const auto factcheckMetrics = factcheck
		? computeFactcheckMetrics(_description.countHeight(innerWidth))
		: FactcheckMetrics();
	if (factcheck) {
		factcheck->expandable = factcheckMetrics.expandable ? 1 : 0;
		factcheck->expanded = factcheckMetrics.expanded ? 1 : 0;
		_openl = factcheck->expandable
			? ToggleFactcheckClickHandler(_parent)
			: nullptr;
	}
	const auto linesMax = factcheck
		? (factcheckMetrics.lines + 1)
		: (specialRightPix || isLogEntryOriginal())
		? kMaxOriginalEntryLines
		: 5;
	const auto siteNameHeight = _siteNameLines ? lineHeight : 0;
	const auto twoTitleLines = 2 * st::webPageTitleFont->height;
	const auto descriptionLineHeight = st::webPageDescriptionFont->height;
	if (asArticle() || specialRightPix) {
		constexpr auto kSponsoredUserpicLines = 2;
		_pixh = lineHeight
			* (stickerSet
				? kStickerSetLines
				: specialRightPix
				? kSponsoredUserpicLines
				: linesMax);
		do {
			_pixw = specialRightPix
				? _pixh
				: ArticleThumbWidth(_data->photo, _pixh);
			const auto wleft = innerWidth
				- st::webPagePhotoDelta
				- std::max(_pixw, lineHeight);

			newHeight = siteNameHeight;

			if (_title.isEmpty()) {
				_titleLines = 0;
			} else {
				_titleLines = (_title.countHeight(wleft) < twoTitleLines)
					? 1
					: 2;
				newHeight += _titleLines * lineHeight;
			}

			const auto descriptionHeight = _description.countHeight(wleft);
			const auto restLines = (linesMax - _siteNameLines - _titleLines);
			if (descriptionHeight < restLines * descriptionLineHeight) {
				// We have height for all the lines.
				_descriptionLines = -1;
				newHeight += descriptionHeight;
			} else {
				_descriptionLines = restLines;
				newHeight += _descriptionLines * lineHeight;
			}

			if (newHeight >= _pixh) {
				break;
			}

			_pixh -= lineHeight;
		} while (_pixh > lineHeight);
	} else {
		newHeight = siteNameHeight;

		if (_title.isEmpty()) {
			_titleLines = 0;
		} else {
			_titleLines = (_title.countHeight(innerWidth) < twoTitleLines)
				? 1
				: 2;
			newHeight += _titleLines * lineHeight;
		}

		if (_description.isEmpty()) {
			_descriptionLines = 0;
		} else {
			const auto restLines = (linesMax - _siteNameLines - _titleLines);
			const auto descriptionHeight = _description.countHeight(
				innerWidth);
			if (descriptionHeight < restLines * descriptionLineHeight) {
				// We have height for all the lines.
				_descriptionLines = -1;
				newHeight += descriptionHeight;
			} else {
				_descriptionLines = restLines;
				newHeight += _descriptionLines * lineHeight;
			}
		}
		if (factcheck && factcheck->expanded) {
			factcheck->footerHeight = st::factcheckFooterSkip
				+ factcheck->footer.countHeight(innerWidth);
			newHeight += factcheck->footerHeight;
		}

		if (_attach) {
			const auto attachAtTop = (!_siteNameLines
					&& !_titleLines
					&& !_descriptionLines)
				|| (sponsored && sponsored->hasMedia);
			if (!attachAtTop) {
				newHeight += st::mediaInBubbleSkip;
			}

			const auto bubble = _attach->bubbleMargins();
			_attach->resizeGetHeight(innerWidth + rect::m::sum::h(bubble));
			newHeight += _attach->height() - rect::m::sum::v(bubble);
		}
	}
	newHeight += rect::m::sum::v(padding);

	return { newWidth, newHeight };
}

TextSelection WebPage::toTitleSelection(TextSelection selection) const {
	return UnshiftItemSelection(selection, _siteName);
}

TextSelection WebPage::fromTitleSelection(TextSelection selection) const {
	return ShiftItemSelection(selection, _siteName);
}

TextSelection WebPage::toDescriptionSelection(TextSelection selection) const {
	return UnshiftItemSelection(toTitleSelection(selection), _title);
}

TextSelection WebPage::fromDescriptionSelection(
		TextSelection selection) const {
	return ShiftItemSelection(fromTitleSelection(selection), _title);
}

void WebPage::refreshParentId(not_null<HistoryItem*> realParent) {
	if (_attach) {
		_attach->refreshParentId(realParent);
	}
}

void WebPage::ensurePhotoMediaCreated() const {
	Expects(_data->photo != nullptr);

	if (_photoMedia) {
		return;
	}
	_photoMedia = _data->photo->createMediaView();
	const auto contextId = _parent->data()->fullId();
	_photoMedia->wanted(Data::PhotoSize::Thumbnail, contextId);
	history()->owner().registerHeavyViewPart(_parent);
}

bool WebPage::hasHeavyPart() const {
	if (const auto stickerSet = stickerSetData()) {
		for (const auto &part : stickerSet->views) {
			if (part->hasHeavyPart()) {
				return true;
			}
		}
	}
	return _photoMedia
		|| (_attach ? _attach->hasHeavyPart() : false);
}

void WebPage::unloadHeavyPart() {
	if (_attach) {
		_attach->unloadHeavyPart();
	}
	_description.unloadPersistentAnimation();
	_photoMedia = nullptr;
	if (const auto stickerSet = stickerSetData()) {
		for (const auto &part : stickerSet->views) {
			part->unloadHeavyPart();
		}
	}
}

void WebPage::draw(Painter &p, const PaintContext &context) const {
	if (width() < rect::m::sum::h(st::msgPadding) + 1) {
		return;
	}
	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();

	const auto bubble = _attach ? _attach->bubbleMargins() : QMargins();
	const auto full = Rect(currentSize());
	const auto outer = full - inBubblePadding();
	const auto inner = outer - innerMargin();
	const auto attachAdditionalInfoText = _attach
		? _attach->additionalInfoString()
		: QString();
	auto tshift = inner.top();
	auto paintw = inner.width();

	const auto sponsored = sponsoredData();
	const auto factcheck = factcheckData();

	const auto hasSponsoredMedia = sponsored && sponsored->hasMedia;
	if (hasSponsoredMedia && _attach) {
		tshift += _attach->height() + st::mediaInBubbleSkip;
	}

	const auto selected = context.selected();
	const auto view = parent();
	const auto from = view->data()->contentColorsFrom();
	const auto colorIndex = factcheck
		? 0 // red
		: (sponsored && sponsored->colorIndex)
		? sponsored->colorIndex
		: from
		? from->colorIndex()
		: view->colorIndex();
	const auto cache = context.outbg
		? stm->replyCache[st->colorPatternIndex(colorIndex)].get()
		: st->coloredReplyCache(selected, colorIndex).get();
	const auto backgroundEmojiId = factcheck
		? DocumentId()
		: (sponsored && sponsored->backgroundEmojiId)
		? sponsored->backgroundEmojiId
		: from
		? from->backgroundEmojiId()
		: DocumentId();
	const auto backgroundEmoji = backgroundEmojiId
		? st->backgroundEmojiData(backgroundEmojiId).get()
		: nullptr;
	const auto backgroundEmojiCache = backgroundEmoji
		? &backgroundEmoji->caches[Ui::BackgroundEmojiData::CacheIndex(
			selected,
			context.outbg,
			true,
			colorIndex + 1)]
		: nullptr;
	Ui::Text::ValidateQuotePaintCache(*cache, _st);
	Ui::Text::FillQuotePaint(p, outer, *cache, _st);
	if (backgroundEmoji) {
		ValidateBackgroundEmoji(
			backgroundEmojiId,
			backgroundEmoji,
			backgroundEmojiCache,
			cache,
			view);
		if (!backgroundEmojiCache->frames[0].isNull()) {
			FillBackgroundEmoji(p, outer, false, *backgroundEmojiCache);
		}
	} else if (factcheck && factcheck->expandable) {
		const auto &icon = factcheck->expanded ? _st.collapse : _st.expand;
		const auto &position = factcheck->expanded
			? _st.collapsePosition
			: _st.expandPosition;
		icon.paint(
			p,
			outer.x() + outer.width() - icon.width() - position.x(),
			outer.y() + outer.height() - icon.height() - position.y(),
			width());
	}

	if (_ripple) {
		_ripple->paint(p, outer.x(), outer.y(), width(), &cache->bg);
		if (_ripple->empty()) {
			_ripple = nullptr;
		}
	}

	auto lineHeight = UnitedLineHeight();
	if (const auto stickerSet = stickerSetData()) {
		const auto viewsCount = stickerSet->views.size();
		const auto box = _pixh;
		const auto topLeft = QPoint(inner.left() + paintw - box, tshift);
		const auto side = std::ceil(std::sqrt(viewsCount));
		const auto single = box / side;
		for (auto i = 0; i < side; i++) {
			for (auto j = 0; j < side; j++) {
				const auto index = i * side + j;
				if (viewsCount <= index) {
					break;
				}
				const auto &view = stickerSet->views[index];
				const auto size = view->countOptimalSize();
				const auto offsetX = (single - size.width()) / 2.;
				const auto offsetY = (single - size.height()) / 2.;
				const auto x = j * single + offsetX;
				const auto y = i * single + offsetY;
				view->draw(p, context, QRect(QPoint(x, y) + topLeft, size));
			}
		}
		paintw -= box;
	} else if (asArticle()) {
		ensurePhotoMediaCreated();

		auto pix = QPixmap();
		const auto pw = qMax(_pixw, lineHeight);
		const auto ph = _pixh;
		auto pixw = _pixw;
		auto pixh = ArticleThumbHeight(_photoMedia.get(), _pixw);
		const auto maxsize = _photoMedia->size(Data::PhotoSize::Thumbnail);
		const auto maxw = style::ConvertScale(maxsize.width());
		const auto maxh = style::ConvertScale(maxsize.height());
		if (pixw * ph != pixh * pw) {
			const auto coef = (pixw * ph > pixh * pw)
				? std::min(ph / float64(pixh), maxh / float64(pixh))
				: std::min(pw / float64(pixw), maxw / float64(pixw));
			pixh = std::round(pixh * coef);
			pixw = std::round(pixw * coef);
		}
		const auto size = QSize(pixw, pixh);
		const auto args = Images::PrepareArgs{
			.options = Images::Option::RoundSmall,
			.outer = { pw, ph },
		};
		using namespace Data;
		if (const auto thumbnail = _photoMedia->image(PhotoSize::Thumbnail)) {
			pix = thumbnail->pixSingle(size, args);
		} else if (const auto small = _photoMedia->image(PhotoSize::Small)) {
			pix = small->pixSingle(size, args.blurred());
		} else if (const auto blurred = _photoMedia->thumbnailInline()) {
			pix = blurred->pixSingle(size, args.blurred());
		}
		p.drawPixmapLeft(inner.left() + paintw - pw, tshift, width(), pix);
		if (context.selected()) {
			const auto st = context.st;
			Ui::FillRoundRect(
				p,
				style::rtlrect(
					inner.left() + paintw - pw,
					tshift,
					pw,
					_pixh,
					width()),
				st->msgSelectOverlay(),
				st->msgSelectOverlayCorners(Ui::CachedCornerRadius::Small));
		}
		if (!sponsored) {
			// Ignore photo width in sponsored messages,
			// as its width only affects the title.
			paintw -= pw + st::webPagePhotoDelta;
		}
	}
	if (_siteNameLines) {
		p.setPen(cache->icon);
		p.setTextPalette(context.outbg
			? stm->semiboldPalette
			: st->coloredTextPalette(selected, colorIndex));

		const auto endskip = _siteName.hasSkipBlock()
			? _parent->skipBlockWidth()
			: 0;
		_siteName.drawLeftElided(
			p,
			inner.left(),
			tshift,
			paintw,
			width(),
			_siteNameLines,
			style::al_left,
			0,
			-1,
			endskip,
			false,
			context.selection);
		const auto hint = hintData();
		if (hint && (paintw > hint->widthBefore + hint->size.width())) {
			auto color = cache->icon;
			color.setAlphaF(color.alphaF() * 0.15);

			const auto height = st::webPageSponsoredHintFont->height;
			const auto radius = height / 2;

			hint->lastPosition = QPointF(
				radius + inner.left() + hint->widthBefore,
				tshift + (_siteName.style()->font->height - height) / 2.);

			if (hint->ripple) {
				hint->ripple->paint(
					p,
					hint->lastPosition.x(),
					hint->lastPosition.y(),
					width(),
					&cache->bg);
				if (hint->ripple->empty()) {
					hint->ripple = nullptr;
				}
			}

			const auto rect = QRectF(hint->lastPosition, hint->size);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawRoundedRect(rect, radius, radius);

			p.setPen(cache->icon);
			p.setBrush(Qt::NoBrush);
			p.setFont(st::webPageSponsoredHintFont);
			p.drawText(rect, hint->text, style::al_center);
		}
		tshift += lineHeight;

		p.setTextPalette(stm->textPalette);
	}
	p.setPen(stm->historyTextFg);
	if (_titleLines) {
		const auto endskip = _title.hasSkipBlock()
			? _parent->skipBlockWidth()
			: 0;
		const auto titleWidth = sponsored
			? (paintw - _pixh - st::webPagePhotoDelta)
			: paintw;
		_title.drawLeftElided(
			p,
			inner.left(),
			tshift,
			titleWidth,
			width(),
			_titleLines,
			style::al_left,
			0,
			-1,
			endskip,
			false,
			toTitleSelection(context.selection));
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		const auto endskip = _description.hasSkipBlock()
			? _parent->skipBlockWidth()
			: 0;
		_parent->prepareCustomEmojiPaint(p, context, _description);
		_description.draw(p, {
			.position = { inner.left(), tshift },
			.outerWidth = width(),
			.availableWidth = paintw,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.selection = toDescriptionSelection(context.selection),
			.elisionHeight = ((_descriptionLines > 0)
				? (_descriptionLines * lineHeight)
				: 0),
			.elisionRemoveFromEnd = (_descriptionLines > 0) ? endskip : 0,
			.useFullWidth = true,
		});
		tshift += (_descriptionLines > 0)
			? (_descriptionLines * lineHeight)
			: _description.countHeight(paintw);
	}
	if (factcheck && factcheck->expanded) {
		const auto skip = st::factcheckFooterSkip;
		const auto line = st::lineWidth;
		const auto separatorTop = tshift + skip / 2;

		auto color = cache->icon;
		color.setAlphaF(color.alphaF() * 0.3);
		p.fillRect(inner.left(), separatorTop, paintw, line, color);

		p.setPen(cache->icon);
		factcheck->footer.draw(p, {
			.position = { inner.left(), tshift + skip },
			.outerWidth = width(),
			.availableWidth = paintw,
		});
		tshift += factcheck->footerHeight;
	}
	if (_attach) {
		const auto attachAtTop = hasSponsoredMedia
			|| (!_siteNameLines && !_titleLines && !_descriptionLines);
		if (!attachAtTop) {
			tshift += st::mediaInBubbleSkip;
		}

		const auto attachLeft = rtl()
			? (width() - (inner.left() - bubble.left()) - _attach->width())
			: (inner.left() - bubble.left());
		const auto attachTop = hasSponsoredMedia
			? inner.top()
			: (tshift - bubble.top());

		p.translate(attachLeft, attachTop);

		_attach->draw(p, context.translated(
			-attachLeft,
			-attachTop
		).withSelection(context.selected()
			? FullSelection
			: TextSelection()));
		const auto pixwidth = _attach->width();
		const auto pixheight = _attach->height();

		if (_data->type == WebPageType::Video
			&& _collage.empty()
			&& _data->photo
			&& !_data->document) {
			if (_attach->isReadyForOpen()) {
				if (_data->siteName == u"YouTube"_q) {
					st->youtubeIcon().paint(
						p,
						(pixwidth - st::youtubeIcon.width()) / 2,
						(pixheight - st::youtubeIcon.height()) / 2,
						width());
				} else {
					st->videoIcon().paint(
						p,
						(pixwidth - st::videoIcon.width()) / 2,
						(pixheight - st::videoIcon.height()) / 2,
						width());
				}
			}
			if (_durationWidth) {
				const auto dateX = pixwidth
					- _durationWidth
					- st::msgDateImgDelta
					- 2 * st::msgDateImgPadding.x();
				const auto dateY = pixheight
					- st::msgDateFont->height
					- 2 * st::msgDateImgPadding.y()
					- st::msgDateImgDelta;
				const auto dateW = pixwidth - dateX - st::msgDateImgDelta;
				const auto dateH = pixheight - dateY - st::msgDateImgDelta;

				Ui::FillRoundRect(
					p,
					dateX,
					dateY,
					dateW,
					dateH,
					sti->msgDateImgBg,
					sti->msgDateImgBgCorners);

				p.setFont(st::msgDateFont);
				p.setPen(st->msgDateImgFg());
				p.drawTextLeft(
					dateX + st::msgDateImgPadding.x(),
					dateY + st::msgDateImgPadding.y(),
					pixwidth,
					_duration);
			}
		}

		p.translate(-attachLeft, -attachTop);

		if (!attachAdditionalInfoText.isEmpty()) {
			p.setFont(st::msgDateFont);
			p.setPen(stm->msgDateFg);
			p.drawTextLeft(
				st::msgPadding.left(),
				outer.y() + outer.height() + st::mediaInBubbleSkip,
				width(),
				attachAdditionalInfoText);
		}
	}

	if (!_openButton.isEmpty()) {
		p.setFont(st::semiboldFont);
		p.setPen(cache->icon);
		const auto end = inner.y() + inner.height() + _st.padding.bottom();
		const auto line = st::historyPageButtonLine;
		auto color = cache->icon;
		color.setAlphaF(color.alphaF() * 0.3);
		p.fillRect(inner.x(), end, inner.width(), line, color);
		_openButton.draw(p, {
			.position = QPoint(
				inner.x() + (inner.width() - _openButton.maxWidth()) / 2,
				end + st::historyPageButtonPadding.top()),
			.availableWidth = paintw,
			.now = context.now,
		});
	}
}

bool WebPage::asArticle() const {
	return _asArticle && (_data->photo != nullptr);
}

WebPage::StickerSetData *WebPage::stickerSetData() const {
	return std::get_if<StickerSetData>(_additionalData.get());
}

WebPage::SponsoredData *WebPage::sponsoredData() const {
	return std::get_if<SponsoredData>(_additionalData.get());
}

WebPage::FactcheckData *WebPage::factcheckData() const {
	return std::get_if<FactcheckData>(_additionalData.get());
}

WebPage::HintData *WebPage::hintData() const {
	if (const auto sponsored = sponsoredData()) {
		return sponsored->hint.link ? &sponsored->hint : nullptr;
	} else if (const auto factcheck = factcheckData()) {
		return factcheck->hint.link ? &factcheck->hint : nullptr;
	}
	return nullptr;
}

TextState WebPage::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < rect::m::sum::h(st::msgPadding) + 1) {
		return result;
	}
	const auto sponsored = sponsoredData();
	const auto bubble = _attach ? _attach->bubbleMargins() : QMargins();
	const auto full = Rect(currentSize());
	auto outer = full - inBubblePadding();
	if (sponsored) {
		outer.translate(0, st::msgDateFont->height);
	}
	const auto inner = outer - innerMargin();
	auto tshift = inner.top();
	auto paintw = inner.width();

	const auto hasSponsoredMedia = sponsored && sponsored->hasMedia;
	if (hasSponsoredMedia && _attach) {
		tshift += _attach->height() + st::mediaInBubbleSkip;
	}

	const auto lineHeight = UnitedLineHeight();
	auto inThumb = false;
	if (asArticle()) {
		const auto pw = std::max(_pixw, lineHeight);
		inThumb = style::rtlrect(
			inner.left() + paintw - pw,
			tshift,
			pw,
			_pixh,
			width()).contains(point);
		paintw -= pw + st::webPagePhotoDelta;
	}
	auto symbolAdd = int(0);
	if (_siteNameLines) {
		if (point.y() >= tshift && point.y() < tshift + lineHeight) {
			auto siteNameRequest = Ui::Text::StateRequestElided(
				request.forText());
			siteNameRequest.lines = _siteNameLines;
			result = TextState(
				_parent,
				_siteName.getStateElidedLeft(
					point - QPoint(inner.left(), tshift),
					paintw,
					width(),
					siteNameRequest));
		} else if (point.y() >= tshift + lineHeight) {
			symbolAdd += _siteName.length();
		}
		tshift += lineHeight;
	}
	if (_titleLines) {
		if (point.y() >= tshift
			&& point.y() < tshift + _titleLines * lineHeight) {
			auto titleRequest = Ui::Text::StateRequestElided(
				request.forText());
			titleRequest.lines = _titleLines;
			result = TextState(
				_parent,
				_title.getStateElidedLeft(
					point - QPoint(inner.left(), tshift),
					paintw,
					width(),
					titleRequest));
		} else if (point.y() >= tshift + _titleLines * lineHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		const auto descriptionHeight = (_descriptionLines > 0)
			? _descriptionLines * lineHeight
			: _description.countHeight(paintw);
		if (point.y() >= tshift && point.y() < tshift + descriptionHeight) {
			if (_descriptionLines > 0) {
				auto descriptionRequest = Ui::Text::StateRequestElided(
					request.forText());
				descriptionRequest.lines = _descriptionLines;
				result = TextState(
					_parent,
					_description.getStateElidedLeft(
						point - QPoint(inner.left(), tshift),
						paintw,
						width(),
						descriptionRequest));
			} else {
				result = TextState(
					_parent,
					_description.getStateLeft(
						point - QPoint(inner.left(), tshift),
						paintw,
						width(),
						request.forText()));
			}
		} else if (point.y() >= tshift + descriptionHeight) {
			symbolAdd += _description.length();
		}
		tshift += descriptionHeight;
	}
	auto isWithinSponsoredMedia = false;
	if (inThumb) {
		result.link = _openl;
	} else if (_attach) {
		const auto attachAtTop = hasSponsoredMedia
			|| (!_siteNameLines && !_titleLines && !_descriptionLines);
		if (!attachAtTop) {
			tshift += st::mediaInBubbleSkip;
		}
		if (hasSponsoredMedia) {
			tshift -= _attach->height();
		}

		const auto rect = hasSponsoredMedia
			? QRect(
				inner.left(),
				inner.top(),
				_attach->width(),
				_attach->height())
			: QRect(
				inner.left(),
				tshift,
				paintw,
				inner.top() + inner.height() - tshift);
		if (rect.contains(point)) {
			const auto attachLeft = rtl()
				? width() - (inner.left() - bubble.left()) - _attach->width()
				: (inner.left() - bubble.left());
			const auto attachTop = hasSponsoredMedia
				? inner.top()
				: (tshift - bubble.top());
			result = _attach->textState(
				point - QPoint(attachLeft, attachTop),
				request);
			if (hasSponsoredMedia) {
				isWithinSponsoredMedia = true;
			} else if (result.cursor == CursorState::Enlarge) {
				result.cursor = CursorState::None;
			} else {
				result.link = replaceAttachLink(result.link);
			}
		}
		if (hasSponsoredMedia) {
			tshift += _attach->height();
		}
	}
	if (isWithinSponsoredMedia) {
		result.link = sponsored->mediaLink;
	} else if (sponsored && outer.contains(point)) {
		result.link = sponsored->link;
	}
	if (!result.link && outer.contains(point)) {
		result.link = _openl;
	}
	if (const auto hint = hintData()) {
		const auto check = point
			- QPoint(0, sponsored ? st::msgDateFont->height : 0);
		const auto hintRect = QRectF(hint->lastPosition, hint->size);
		if (hintRect.contains(check)) {
			result.link = hint->link;
		}
	}
	_lastPoint = point - outer.topLeft();

	result.symbol += symbolAdd;
	return result;
}

ClickHandlerPtr WebPage::replaceAttachLink(
		const ClickHandlerPtr &link) const {
	if (!_attach->isReadyForOpen()
		|| (_siteName.isEmpty()
			&& _title.isEmpty()
			&& _description.isEmpty())
		|| (_data->document
			&& !_data->document->isWallPaper()
			&& !_data->document->isTheme())
		|| !_data->collage.items.empty()) {
		return link;
	}
	return _openl;
}

TextSelection WebPage::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	if ((!_titleLines && !_descriptionLines)
		|| selection.to <= _siteName.length()) {
		return _siteName.adjustSelection(selection, type);
	}

	const auto titlesLength = _siteName.length() + _title.length();
	const auto titleSelection = _title.adjustSelection(
		toTitleSelection(selection),
		type);
	if ((!_siteNameLines && !_descriptionLines)
		|| (selection.from >= _siteName.length()
			&& selection.to <= titlesLength)) {
		return fromTitleSelection(titleSelection);
	}

	const auto descriptionSelection = _description.adjustSelection(
		toDescriptionSelection(selection),
		type);
	if ((!_siteNameLines && !_titleLines) || selection.from >= titlesLength) {
		return fromDescriptionSelection(descriptionSelection);
	}

	return {
		_siteName.adjustSelection(selection, type).from,
		(!_descriptionLines || selection.to <= titlesLength)
			? fromTitleSelection(titleSelection).to
			: fromDescriptionSelection(descriptionSelection).to,
	};
}

uint16 WebPage::fullSelectionLength() const {
	return _siteName.length() + _title.length() + _description.length();
}

void WebPage::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void WebPage::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	const auto hint = hintData();
	if (hint && hint->link == p) {
		if (pressed) {
			if (!hint->ripple) {
				const auto owner = &parent()->history()->owner();
				hint->ripple = std::make_unique<Ui::RippleAnimation>(
					st::defaultRippleAnimation,
					Ui::RippleAnimation::RoundRectMask(
						hint->size,
						_st.radius),
					[=] { owner->requestViewRepaint(parent()); });
			}
			const auto full = Rect(currentSize());
			const auto outer = full - inBubblePadding();
			hint->ripple->add(_lastPoint
				+ outer.topLeft()
				- hint->lastPosition.toPoint());
		} else if (hint->ripple) {
			hint->ripple->lastStop();
		}
		return;
	}
	if (p == _openl) {
		if (pressed) {
			if (!_ripple) {
				const auto full = Rect(currentSize());
				const auto outer = full - inBubblePadding();
				const auto owner = &parent()->history()->owner();
				_ripple = std::make_unique<Ui::RippleAnimation>(
					st::defaultRippleAnimation,
					Ui::RippleAnimation::RoundRectMask(
						outer.size(),
						_st.radius),
					[=] { owner->requestViewRepaint(parent()); });
			}
			_ripple->add(_lastPoint);
		} else if (_ripple) {
			_ripple->lastStop();
		}
	}
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}

bool WebPage::enforceBubbleWidth() const {
	return (_attach != nullptr)
		&& (_data->document != nullptr)
		&& (_data->document->isWallPaper() || _data->document->isTheme());
}

void WebPage::playAnimation(bool autoplay) {
	if (_attach) {
		if (autoplay) {
			_attach->autoplayAnimation();
		} else {
			_attach->playAnimation();
		}
	}
}

bool WebPage::isDisplayed() const {
	return !_data->pendingTill
		&& !_data->failed
		&& !_parent->data()->Has<HistoryMessageLogEntryOriginal>();
}

QString WebPage::additionalInfoString() const {
	return _attach ? _attach->additionalInfoString() : QString();
}

bool WebPage::toggleSelectionByHandlerClick(const ClickHandlerPtr &p) const {
	return _attach && _attach->toggleSelectionByHandlerClick(p);
}

bool WebPage::allowTextSelectionByHandler(const ClickHandlerPtr &p) const {
	return (p == _openl);
}

bool WebPage::dragItemByHandler(const ClickHandlerPtr &p) const {
	return _attach && _attach->dragItemByHandler(p);
}

TextForMimeData WebPage::selectedText(TextSelection selection) const {
	auto siteNameResult = _siteName.toTextForMimeData(selection);
	auto titleResult = _title.toTextForMimeData(toTitleSelection(selection));
	auto descriptionResult = _description.toTextForMimeData(
		toDescriptionSelection(selection));
	if (titleResult.empty() && descriptionResult.empty()) {
		return siteNameResult;
	} else if (siteNameResult.empty() && descriptionResult.empty()) {
		return titleResult;
	} else if (siteNameResult.empty() && titleResult.empty()) {
		return descriptionResult;
	} else if (siteNameResult.empty()) {
		return titleResult.append('\n').append(std::move(descriptionResult));
	} else if (titleResult.empty()) {
		return siteNameResult
			.append('\n')
			.append(std::move(descriptionResult));
	} else if (descriptionResult.empty()) {
		return siteNameResult.append('\n').append(std::move(titleResult));
	}

	return siteNameResult
		.append('\n')
		.append(std::move(titleResult))
		.append('\n')
		.append(std::move(descriptionResult));
}

QMargins WebPage::inBubblePadding() const {
	return {
		st::msgPadding.left(),
		isBubbleTop() ? st::msgPadding.left() : 0,
		st::msgPadding.right(),
		isBubbleBottom() ? (st::msgPadding.left() + bottomInfoPadding()) : 0
	};
}

QMargins WebPage::innerMargin() const {
	const auto button = _openButton.isEmpty()
		? 0
		: st::historyPageButtonHeight;
	return _st.padding + QMargins(0, 0, 0, button);
}

bool WebPage::isLogEntryOriginal() const {
	return _parent->data()->isAdminLogEntry() && _parent->media() != this;
}

WebPage::FactcheckMetrics WebPage::computeFactcheckMetrics(
		int fullHeight) const {
	const auto possible = fullHeight / st::normalFont->height;
	//const auto expandable = (possible > kFactcheckCollapsedLines + 1);
	// Now always expandable because of the footer.
	const auto expandable = true;
	const auto check = _parent->Get<Factcheck>();
	const auto expanded = check && check->expanded;
	const auto allowExpanding = (expanded || !expandable);
	return {
		.lines = allowExpanding ? possible : kFactcheckCollapsedLines,
		.expandable = expandable,
		.expanded = expanded,
	};
}

int WebPage::bottomInfoPadding() const {
	if (!isBubbleBottom()) {
		return 0;
	}

	auto result = st::msgDateFont->height;

	// We use padding greater than st::msgPadding.bottom() in the
	// bottom of the bubble so that the left line looks pretty.
	// but if we have bottom skip because of the info display
	// we don't need that additional padding so we replace it
	// back with st::msgPadding.bottom() instead of left().
	result += st::msgPadding.bottom() - st::msgPadding.left();
	return result;
}

WebPage::~WebPage() {
	history()->owner().unregisterWebPageView(_data, _parent);
	if (_photoMedia) {
		history()->owner().keepAlive(base::take(_photoMedia));
		_parent->checkHeavyPart();
	}
}

} // namespace HistoryView
