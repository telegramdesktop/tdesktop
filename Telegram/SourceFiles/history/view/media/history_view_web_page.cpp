/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_web_page.h"

#include "core/application.h"
#include "base/qt/qt_key_modifiers.h"
#include "window/window_session_controller.h"
#include "iv/iv_instance.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_file_click_handler.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_sponsored_messages.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_reply.h"
#include "history/view/history_view_sponsored_click_handler.h"
#include "history/view/media/history_view_media_common.h"
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
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kMaxOriginalEntryLines = 8192;

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
		: (type == WebPageType::ChannelBoost)
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
		|| (type == WebPageType::Channel)
		|| (type == WebPageType::ChannelBoost)
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
			&& webpage->document->isWallPaper());
}

} // namespace

WebPage::WebPage(
	not_null<Element*> parent,
	not_null<WebPageData*> data,
	MediaWebPageFlags flags)
: Media(parent)
, _st(st::historyPagePreview)
, _data(data)
, _sponsoredData([&]() -> std::optional<SponsoredData> {
	if (!(flags & MediaWebPageFlag::Sponsored)) {
		return std::nullopt;
	}
	const auto &data = _parent->data()->history()->owner();
	const auto details = data.sponsoredMessages().lookupDetails(
		_parent->data()->fullId());
	auto result = std::make_optional<SponsoredData>();
	result->buttonText = details.buttonText;
	result->hasExternalLink = (details.externalLink == _data->url);
	result->canReport = details.canReport;
#ifdef _DEBUG
	if (details.peer) {
#else
	if (details.isForceUserpicDisplay && details.peer) {
#endif
		result->peer = details.peer;
		result->userpicView = details.peer->createUserpicView();
		details.peer->loadUserpic();
	}
	return result;
}())
, _siteName(st::msgMinWidth - _st.padding.left() - _st.padding.right())
, _title(st::msgMinWidth - _st.padding.left() - _st.padding.right())
, _description(st::msgMinWidth - _st.padding.left() - _st.padding.right())
, _flags(flags) {
	history()->owner().registerWebPageView(_data, _parent);
}

QSize WebPage::countOptimalSize() {
	if (_data->pendingTill || _data->failed) {
		return { 0, 0 };
	}

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
	} else if (_sponsoredData) {
		if (!_sponsoredData->buttonText.isEmpty()) {
			_openButton.setText(
				st::semiboldTextStyle,
				Ui::Text::Upper(_sponsoredData->buttonText));
		}
	}

	const auto padding = inBubblePadding() + innerMargin();
	const auto versionChanged = (_dataVersion != _data->version);
	if (versionChanged) {
		_dataVersion = _data->version;
		_openl = nullptr;
		_attach = nullptr;
		_collage = PrepareCollageMedia(_parent->data(), _data->collage);
		const auto min = st::msgMinWidth - rect::m::sum::h(_st.padding);
		_siteName = Ui::Text::String(min);
		_title = Ui::Text::String(min);
		_description = Ui::Text::String(min);
	}
	const auto lineHeight = UnitedLineHeight();

	if (!_openl && (!_data->url.isEmpty() || _sponsoredData)) {
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
		if (_sponsoredData) {
			_openl = SponsoredLink(_sponsoredData->hasExternalLink
				? _data->url
				: QString());

			if (_sponsoredData->canReport) {
				_sponsoredData->hintLink = AboutSponsoredClickHandler();
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
	const auto descMaxLines = isLogEntryOriginal()
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
	if (_attach) {
		const auto attachAtTop = _siteName.isEmpty()
			&& _title.isEmpty()
			&& _description.isEmpty();
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
	if (_sponsoredData && _sponsoredData->canReport) {
		_sponsoredData->widthBeforeHint =
			st::webPageTitleStyle.font->width(siteName);
		const auto &font = st::webPageSponsoredHintFont;
		_sponsoredData->hintSize = QSize(
			font->width(tr::lng_sponsored_message_revenue_button(tr::now))
				+ font->height,
			font->height);
		maxWidth += _sponsoredData->hintSize.width();
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

	const auto lineHeight = UnitedLineHeight();
	const auto linesMax = (_sponsoredData || isLogEntryOriginal())
		? kMaxOriginalEntryLines
		: 5;
	const auto siteNameHeight = _siteNameLines ? lineHeight : 0;
	const auto twoTitleLines = 2 * st::webPageTitleFont->height;
	const auto descriptionLineHeight = st::webPageDescriptionFont->height;
	const auto asSponsored = (!!_sponsoredData);
	if (asArticle() || asSponsored) {
		const auto sponsoredUserpic = (asSponsored && _sponsoredData->peer);
		constexpr auto kSponsoredUserpicLines = 2;
		_pixh = lineHeight
			* (asSponsored ? kSponsoredUserpicLines : linesMax);
		do {
			_pixw = asSponsored
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

			const auto descriptionHeight = _description.countHeight(
				sponsoredUserpic ? innerWidth : wleft);
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

		if (_attach) {
			const auto attachAtTop = !_siteNameLines
				&& !_titleLines
				&& !_descriptionLines;
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
	return _photoMedia
		|| (_sponsoredData && !_sponsoredData->userpicView.null())
		|| (_attach ? _attach->hasHeavyPart() : false);
}

void WebPage::unloadHeavyPart() {
	if (_attach) {
		_attach->unloadHeavyPart();
	}
	_description.unloadPersistentAnimation();
	_photoMedia = nullptr;
	if (_sponsoredData) {
		_sponsoredData->userpicView = Ui::PeerUserpicView();
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

	const auto selected = context.selected();
	const auto view = parent();
	const auto from = view->data()->contentColorsFrom();
	const auto colorIndex = from ? from->colorIndex() : view->colorIndex();
	const auto cache = context.outbg
		? stm->replyCache[st->colorPatternIndex(colorIndex)].get()
		: st->coloredReplyCache(selected, colorIndex).get();
	const auto backgroundEmojiId = from
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
	}

	if (_ripple) {
		_ripple->paint(p, outer.x(), outer.y(), width(), &cache->bg);
		if (_ripple->empty()) {
			_ripple = nullptr;
		}
	}

	const auto asSponsored = (!!_sponsoredData);

	auto lineHeight = UnitedLineHeight();
	if (asArticle()) {
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
		if (!asSponsored) {
			// Ignore photo width in sponsored messages,
			// as its width only affects the title.
			paintw -= pw + st::webPagePhotoDelta;
		}
	} else if (asSponsored && _sponsoredData->peer) {
		const auto size = _pixh;
		const auto sizeHq = size * style::DevicePixelRatio();
		const auto userpicPos = QPoint(inner.left() + paintw - size, tshift);
		const auto &peer = _sponsoredData->peer;
		auto &view = _sponsoredData->userpicView;
		if (const auto cloud = peer->userpicCloudImage(view)) {
			Ui::ValidateUserpicCache(view, cloud, nullptr, sizeHq, true);
			p.drawImage(QRect(userpicPos, QSize(size, size)), view.cached);
		} else {
			const auto r = sizeHq * Ui::ForumUserpicRadiusMultiplier();
			const auto empty = peer->generateUserpicImage(view, sizeHq, r);
			p.drawImage(QRect(userpicPos, QSize(size, size)), empty);
		}
		// paintw -= size + st::webPagePhotoDelta;
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
		if (asSponsored
			&& _sponsoredData->canReport
			&& (paintw >
					_sponsoredData->widthBeforeHint
						+ _sponsoredData->hintSize.width())) {
			if (_sponsoredData->hintRipple) {
				_sponsoredData->hintRipple->paint(
					p,
					_sponsoredData->lastHintPos.x(),
					_sponsoredData->lastHintPos.y(),
					width(),
					&cache->bg);
				if (_sponsoredData->hintRipple->empty()) {
					_sponsoredData->hintRipple = nullptr;
				}
			}

			auto color = cache->icon;
			color.setAlphaF(color.alphaF() * 0.15);

			const auto height = st::webPageSponsoredHintFont->height;
			const auto radius = height / 2;

			_sponsoredData->lastHintPos = QPoint(
				radius + inner.left() + _sponsoredData->widthBeforeHint,
				tshift
					+ (_siteName.style()->font->height - height) / 2
					+ st::webPageSponsoredHintFont->descent / 2);
			const auto rect = QRect(
				_sponsoredData->lastHintPos,
				_sponsoredData->hintSize);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			p.drawRoundedRect(rect, radius, radius);

			p.setPen(cache->icon);
			p.setBrush(Qt::NoBrush);
			p.setFont(st::webPageSponsoredHintFont);
			p.drawText(
				rect,
				tr::lng_sponsored_message_revenue_button(tr::now),
				style::al_center);
		}
		tshift += lineHeight;

		p.setTextPalette(stm->textPalette);
	}
	p.setPen(stm->historyTextFg);
	if (_titleLines) {
		const auto endskip = _title.hasSkipBlock()
			? _parent->skipBlockWidth()
			: 0;
		const auto titleWidth = asSponsored
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
		});
		tshift += (_descriptionLines > 0)
			? (_descriptionLines * lineHeight)
			: _description.countHeight(paintw);
	}
	if (_attach) {
		const auto attachAtTop = !_siteNameLines
			&& !_titleLines
			&& !_descriptionLines;
		if (!attachAtTop) {
			tshift += st::mediaInBubbleSkip;
		}

		const auto attachLeft = rtl()
			? (width() - (inner.left() - bubble.left()) - _attach->width())
			: (inner.left() - bubble.left());
		const auto attachTop = tshift - bubble.top();

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

TextState WebPage::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < rect::m::sum::h(st::msgPadding) + 1) {
		return result;
	}
	const auto bubble = _attach ? _attach->bubbleMargins() : QMargins();
	const auto full = Rect(currentSize());
	auto outer = full - inBubblePadding();
	if (_sponsoredData) {
		outer.translate(0, st::msgDateFont->height);
	}
	const auto inner = outer - innerMargin();
	auto tshift = inner.top();
	auto paintw = inner.width();

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
	if (inThumb) {
		result.link = _openl;
	} else if (_attach) {
		const auto attachAtTop = !_siteNameLines
			&& !_titleLines
			&& !_descriptionLines;
		if (!attachAtTop) {
			tshift += st::mediaInBubbleSkip;
		}

		const auto rect = QRect(
			inner.left(),
			tshift,
			paintw,
			inner.top() + inner.height() - tshift);
		if (rect.contains(point)) {
			const auto attachLeft = rtl()
				? width() - (inner.left() - bubble.left()) - _attach->width()
				: (inner.left() - bubble.left());
			const auto attachTop = tshift - bubble.top();
			result = _attach->textState(
				point - QPoint(attachLeft, attachTop),
				request);
			if (result.cursor == CursorState::Enlarge) {
				result.cursor = CursorState::None;
			} else {
				result.link = replaceAttachLink(result.link);
			}
		}
	}
	if ((!result.link || _sponsoredData) && outer.contains(point)) {
		result.link = _openl;
	}
	if (_sponsoredData && _sponsoredData->canReport) {
		const auto contains = QRect(
			_sponsoredData->lastHintPos,
			_sponsoredData->hintSize).contains(point
				- QPoint(0, st::msgDateFont->height));
		if (contains) {
			result.link = _sponsoredData->hintLink;
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
	if (_sponsoredData && _sponsoredData->hintLink == p) {
		if (pressed) {
			if (!_sponsoredData->hintRipple) {
				const auto owner = &parent()->history()->owner();
				auto ripple = std::make_unique<Ui::RippleAnimation>(
					st::defaultRippleAnimation,
					Ui::RippleAnimation::RoundRectMask(
						_sponsoredData->hintSize,
						_st.radius),
					[=] { owner->requestViewRepaint(parent()); });
				_sponsoredData->hintRipple = std::move(ripple);
			}
			const auto full = Rect(currentSize());
			const auto outer = full - inBubblePadding();
			_sponsoredData->hintRipple->add(_lastPoint
				+ outer.topLeft()
				- _sponsoredData->lastHintPos);
		} else if (_sponsoredData->hintRipple) {
			_sponsoredData->hintRipple->lastStop();
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
