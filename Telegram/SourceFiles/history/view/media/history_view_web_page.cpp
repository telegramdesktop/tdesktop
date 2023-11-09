/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_web_page.h"

#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "lang/lang_keys.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_view_button.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_theme_document.h"
#include "ui/image/image.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h"
#include "ui/chat/chat_style.h"
#include "ui/cached_round_corners.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "data/data_session.h"
#include "data/data_wall_paper.h"
#include "data/data_media_types.h"
#include "data/data_web_page.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kMaxOriginalEntryLines = 8192;

int articleThumbWidth(not_null<PhotoData*> thumb, int height) {
	const auto size = thumb->location(Data::PhotoSize::Thumbnail);
	return size.height()
		? qMax(qMin(height * size.width() / size.height(), height), 1)
		: 1;
}

int articleThumbHeight(not_null<Data::PhotoMedia*> thumb, int width) {
	const auto size = thumb->size(Data::PhotoSize::Thumbnail);
	return size.width()
		? std::max(size.height() * width / size.width(), 1)
		: 1;
}

std::vector<std::unique_ptr<Data::Media>> PrepareCollageMedia(
		not_null<HistoryItem*> parent,
		const WebPageCollage &data) {
	auto result = std::vector<std::unique_ptr<Data::Media>>();
	result.reserve(data.items.size());
	for (const auto &item : data.items) {
		const auto spoiler = false;
		if (const auto document = std::get_if<DocumentData*>(&item)) {
			const auto skipPremiumEffect = false;
			result.push_back(std::make_unique<Data::MediaFile>(
				parent,
				*document,
				skipPremiumEffect,
				spoiler));
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

[[nodiscard]] QString PageToPhrase(not_null<WebPageData*> webpage) {
	const auto type = webpage->type;
	return Ui::Text::Upper((type == WebPageType::Theme)
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
}

} // namespace

WebPage::WebPage(
	not_null<Element*> parent,
	not_null<WebPageData*> data,
	MediaWebPageFlags flags)
: Media(parent)
, _st(st::historyPagePreview)
, _data(data)
, _siteName(st::msgMinWidth - _st.padding.left() - _st.padding.right())
, _title(st::msgMinWidth - _st.padding.left() - _st.padding.right())
, _description(st::msgMinWidth - _st.padding.left() - _st.padding.right())
, _flags(flags) {
	history()->owner().registerWebPageView(_data, _parent);
}

bool WebPage::HasButton(not_null<WebPageData*> webpage) {
	const auto type = webpage->type;
	return (type == WebPageType::Message)
		|| (type == WebPageType::Group)
		|| (type == WebPageType::Channel)
		|| (type == WebPageType::ChannelBoost)
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

QSize WebPage::countOptimalSize() {
	if (_data->pendingTill || _data->failed) {
		return { 0, 0 };
	}

	// Detect _openButtonWidth before counting paddings.
	_openButton = QString();
	_openButtonWidth = 0;
	if (HasButton(_data)) {
		_openButton = PageToPhrase(_data);
		_openButtonWidth = st::semiboldFont->width(_openButton);
	}

	const auto padding = inBubblePadding() + innerMargin();
	const auto versionChanged = (_dataVersion != _data->version);
	if (versionChanged) {
		_dataVersion = _data->version;
		_openl = nullptr;
		_attach = nullptr;
		_collage = PrepareCollageMedia(_parent->data(), _data->collage);
		const auto min = st::msgMinWidth
			- _st.padding.left()
			- _st.padding.right();
		_siteName = Ui::Text::String(min);
		_title = Ui::Text::String(min);
		_description = Ui::Text::String(min);
	}
	auto lineHeight = UnitedLineHeight();

	if (!_openl && !_data->url.isEmpty()) {
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
			const auto full = _parent->data()->originalText();
			for (const auto &entity : full.entities) {
				if (entity.type() != EntityType::Url) {
					continue;
				}
				const auto link = full.text.mid(
					entity.offset(),
					entity.length());
				if (simplify(link) == simplified) {
					return false;
				}
			}
			return true;
		}();
		_openl = (previewOfHiddenUrl
			|| UrlClickHandler::IsSuspicious(_data->url))
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

	// init layout
	auto title = TextUtilities::SingleLine(_data->title.isEmpty()
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
		auto text = _data->description;

		if (isLogEntryOriginal()) {
			// Fix layout for small bubbles (narrow media caption edit log entries).
			_description = Ui::Text::String(st::minPhotoSize
				- padding.left()
				- padding.right());
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
		auto titleWithEntities = Ui::Text::Link(title, _data->url);
		if (!_siteNameLines && !_data->url.isEmpty()) {
			_title.setMarkedText(
				st::webPageTitleStyle,
				std::move(titleWithEntities),
				Ui::WebpageTextTitleOptions());

		} else {
			_title.setText(
				st::webPageTitleStyle,
				title,
				Ui::WebpageTextTitleOptions());
		}
	}

	// init dimensions
	auto skipBlockWidth = _parent->skipBlockWidth();
	auto maxWidth = skipBlockWidth;
	auto minHeight = 0;

	auto siteNameHeight = _siteName.isEmpty() ? 0 : lineHeight;
	auto titleMinHeight = _title.isEmpty() ? 0 : lineHeight;
	auto descMaxLines = isLogEntryOriginal() ? kMaxOriginalEntryLines : (3 + (siteNameHeight ? 0 : 1) + (titleMinHeight ? 0 : 1));
	auto descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * lineHeight);
	auto articleMinHeight = siteNameHeight + titleMinHeight + descriptionMinHeight;
	auto articlePhotoMaxWidth = 0;
	if (_asArticle) {
		articlePhotoMaxWidth = st::webPagePhotoDelta + qMax(articleThumbWidth(_data->photo, articleMinHeight), lineHeight);
	}

	if (!_siteName.isEmpty()) {
		accumulate_max(maxWidth, _siteName.maxWidth() + articlePhotoMaxWidth);
		minHeight += lineHeight;
	}
	if (!_title.isEmpty()) {
		accumulate_max(maxWidth, _title.maxWidth() + articlePhotoMaxWidth);
		minHeight += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		accumulate_max(maxWidth, _description.maxWidth() + articlePhotoMaxWidth);
		minHeight += descriptionMinHeight;
	}
	if (_attach) {
		auto attachAtTop = _siteName.isEmpty() && _title.isEmpty() && _description.isEmpty();
		if (!attachAtTop) minHeight += st::mediaInBubbleSkip;

		_attach->initDimensions();
		auto bubble = _attach->bubbleMargins();
		auto maxMediaWidth = _attach->maxWidth() - bubble.left() - bubble.right();
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(maxWidth, maxMediaWidth);
		minHeight += _attach->minHeight() - bubble.top() - bubble.bottom();
	}
	if (_data->type == WebPageType::Video && _data->duration) {
		_duration = Ui::FormatDurationText(_data->duration);
		_durationWidth = st::msgDateFont->width(_duration);
	}
	if (_openButtonWidth) {
		const auto &margins = st::historyPageButtonPadding;
		maxWidth += margins.left() + _openButtonWidth + margins.right();
	}
	maxWidth += padding.left() + padding.right();
	minHeight += padding.top() + padding.bottom();

	if (_asArticle) {
		minHeight = resizeGetHeight(maxWidth);
	}
	return { maxWidth, minHeight };
}

QSize WebPage::countCurrentSize(int newWidth) {
	if (_data->pendingTill || _data->failed) {
		return { newWidth, minHeight() };
	}

	auto padding = inBubblePadding() + innerMargin();
	auto innerWidth = newWidth - padding.left() - padding.right();
	auto newHeight = 0;

	auto lineHeight = UnitedLineHeight();
	auto linesMax = isLogEntryOriginal() ? kMaxOriginalEntryLines : 5;
	auto siteNameHeight = _siteNameLines ? lineHeight : 0;
	if (asArticle()) {
		_pixh = linesMax * lineHeight;
		do {
			_pixw = articleThumbWidth(_data->photo, _pixh);
			auto wleft = innerWidth - st::webPagePhotoDelta - qMax(_pixw, lineHeight);

			newHeight = siteNameHeight;

			if (_title.isEmpty()) {
				_titleLines = 0;
			} else {
				if (_title.countHeight(wleft) < 2 * st::webPageTitleFont->height) {
					_titleLines = 1;
				} else {
					_titleLines = 2;
				}
				newHeight += _titleLines * lineHeight;
			}

			auto descriptionHeight = _description.countHeight(wleft);
			if (descriptionHeight < (linesMax - _siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				// We have height for all the lines.
				_descriptionLines = -1;
				newHeight += descriptionHeight;
			} else {
				_descriptionLines = (linesMax - _siteNameLines - _titleLines);
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
			if (_title.countHeight(innerWidth) < 2 * st::webPageTitleFont->height) {
				_titleLines = 1;
			} else {
				_titleLines = 2;
			}
			newHeight += _titleLines * lineHeight;
		}

		if (_description.isEmpty()) {
			_descriptionLines = 0;
		} else {
			auto descriptionHeight = _description.countHeight(innerWidth);
			if (descriptionHeight < (linesMax - _siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				// We have height for all the lines.
				_descriptionLines = -1;
				newHeight += descriptionHeight;
			} else {
				_descriptionLines = (linesMax - _siteNameLines - _titleLines);
				newHeight += _descriptionLines * lineHeight;
			}
		}

		if (_attach) {
			auto attachAtTop = !_siteNameLines && !_titleLines && !_descriptionLines;
			if (!attachAtTop) newHeight += st::mediaInBubbleSkip;

			auto bubble = _attach->bubbleMargins();

			_attach->resizeGetHeight(innerWidth + bubble.left() + bubble.right());
			newHeight += _attach->height() - bubble.top() - bubble.bottom();
		}
	}
	newHeight += padding.top() + padding.bottom();

	return { newWidth, newHeight };
}

TextSelection WebPage::toTitleSelection(
		TextSelection selection) const {
	return UnshiftItemSelection(selection, _siteName);
}

TextSelection WebPage::fromTitleSelection(
		TextSelection selection) const {
	return ShiftItemSelection(selection, _siteName);
}

TextSelection WebPage::toDescriptionSelection(
		TextSelection selection) const {
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
	return _photoMedia || (_attach ? _attach->hasHeavyPart() : false);
}

void WebPage::unloadHeavyPart() {
	if (_attach) {
		_attach->unloadHeavyPart();
	}
	_description.unloadPersistentAnimation();
	_photoMedia = nullptr;
}

void WebPage::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	}
	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto stm = context.messageStyle();

	const auto bubble = _attach ? _attach->bubbleMargins() : QMargins();
	const auto full = QRect(0, 0, width(), height());
	auto outer = full.marginsRemoved(inBubblePadding());
	auto inner = outer.marginsRemoved(innerMargin());
	auto tshift = inner.top();
	auto paintw = inner.width();
	auto attachAdditionalInfoText = _attach ? _attach->additionalInfoString() : QString();

	const auto selected = context.selected();
	const auto colorIndex = parent()->colorIndex();
	const auto cache = context.outbg
		? stm->replyCache[st->colorPatternIndex(colorIndex)].get()
		: st->coloredReplyCache(selected, colorIndex).get();
	Ui::Text::ValidateQuotePaintCache(*cache, _st);
	Ui::Text::FillQuotePaint(p, outer, *cache, _st);

	if (_ripple) {
		_ripple->paint(p, outer.x(), outer.y(), width(), &cache->bg);
		if (_ripple->empty()) {
			_ripple = nullptr;
		}
	}

	auto lineHeight = UnitedLineHeight();
	if (asArticle()) {
		ensurePhotoMediaCreated();

		QPixmap pix;
		auto pw = qMax(_pixw, lineHeight);
		auto ph = _pixh;
		auto pixw = _pixw, pixh = articleThumbHeight(_photoMedia.get(), _pixw);
		const auto maxsize = _photoMedia->size(Data::PhotoSize::Thumbnail);
		const auto maxw = style::ConvertScale(maxsize.width());
		const auto maxh = style::ConvertScale(maxsize.height());
		if (pixw * ph != pixh * pw) {
			float64 coef = (pixw * ph > pixh * pw) ? qMin(ph / float64(pixh), maxh / float64(pixh)) : qMin(pw / float64(pixw), maxw / float64(pixw));
			pixh = qRound(pixh * coef);
			pixw = qRound(pixw * coef);
		}
		const auto size = QSize(pixw, pixh);
		const auto args = Images::PrepareArgs{
			.options = Images::Option::RoundSmall,
			.outer = { pw, ph },
		};
		if (const auto thumbnail = _photoMedia->image(
				Data::PhotoSize::Thumbnail)) {
			pix = thumbnail->pixSingle(size, args);
		} else if (const auto small = _photoMedia->image(
				Data::PhotoSize::Small)) {
			pix = small->pixSingle(size, args.blurred());
		} else if (const auto blurred = _photoMedia->thumbnailInline()) {
			pix = blurred->pixSingle(size, args.blurred());
		}
		p.drawPixmapLeft(inner.left() + paintw - pw, tshift, width(), pix);
		if (context.selected()) {
			const auto st = context.st;
			Ui::FillRoundRect(
				p,
				style::rtlrect(inner.left() + paintw - pw, tshift, pw, _pixh, width()),
				st->msgSelectOverlay(),
				st->msgSelectOverlayCorners(Ui::CachedCornerRadius::Small));
		}
		paintw -= pw + st::webPagePhotoDelta;
	}
	if (_siteNameLines) {
		p.setPen(cache->icon);
		p.setTextPalette(context.outbg
			? stm->semiboldPalette
			: st->coloredTextPalette(selected, colorIndex));

		auto endskip = 0;
		if (_siteName.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_siteName.drawLeftElided(p, inner.left(), tshift, paintw, width(), _siteNameLines, style::al_left, 0, -1, endskip, false, context.selection);
		tshift += lineHeight;

		p.setTextPalette(stm->textPalette);
	}
	p.setPen(stm->historyTextFg);
	if (_titleLines) {
		auto endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, inner.left(), tshift, paintw, width(), _titleLines, style::al_left, 0, -1, endskip, false, toTitleSelection(context.selection));
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		auto endskip = 0;
		if (_description.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
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
		auto attachAtTop = !_siteNameLines && !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = inner.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		p.translate(attachLeft, attachTop);

		_attach->draw(p, context.translated(
			-attachLeft,
			-attachTop
		).withSelection(context.selected()
			? FullSelection
			: TextSelection()));
		auto pixwidth = _attach->width();
		auto pixheight = _attach->height();

		if (_data->type == WebPageType::Video
			&& _collage.empty()
			&& _data->photo
			&& !_data->document) {
			if (_attach->isReadyForOpen()) {
				if (_data->siteName == u"YouTube"_q) {
					st->youtubeIcon().paint(p, (pixwidth - st::youtubeIcon.width()) / 2, (pixheight - st::youtubeIcon.height()) / 2, width());
				} else {
					st->videoIcon().paint(p, (pixwidth - st::videoIcon.width()) / 2, (pixheight - st::videoIcon.height()) / 2, width());
				}
			}
			if (_durationWidth) {
				auto dateX = pixwidth - _durationWidth - st::msgDateImgDelta - 2 * st::msgDateImgPadding.x();
				auto dateY = pixheight - st::msgDateFont->height - 2 * st::msgDateImgPadding.y() - st::msgDateImgDelta;
				auto dateW = pixwidth - dateX - st::msgDateImgDelta;
				auto dateH = pixheight - dateY - st::msgDateImgDelta;

				Ui::FillRoundRect(p, dateX, dateY, dateW, dateH, sti->msgDateImgBg, sti->msgDateImgBgCorners);

				p.setFont(st::msgDateFont);
				p.setPen(st->msgDateImgFg());
				p.drawTextLeft(dateX + st::msgDateImgPadding.x(), dateY + st::msgDateImgPadding.y(), pixwidth, _duration);
			}
		}

		p.translate(-attachLeft, -attachTop);

		if (!attachAdditionalInfoText.isEmpty()) {
			p.setFont(st::msgDateFont);
			p.setPen(stm->msgDateFg);
			p.drawTextLeft(st::msgPadding.left(), outer.y() + outer.height() + st::mediaInBubbleSkip, width(), attachAdditionalInfoText);
		}
	}

	if (_openButtonWidth) {
		p.setFont(st::semiboldFont);
		p.setPen(cache->icon);
		const auto end = inner.y() + inner.height() + _st.padding.bottom();
		const auto line = st::historyPageButtonLine;
		auto color = cache->icon;
		color.setAlphaF(color.alphaF() * 0.3);
		p.fillRect(inner.x(), end, inner.width(), line, color);
		const auto top = end + st::historyPageButtonPadding.top();
		p.drawText(
			inner.x() + (inner.width() - _openButtonWidth) / 2,
			top + st::semiboldFont->ascent,
			_openButton);
	}
}

bool WebPage::asArticle() const {
	return _asArticle && (_data->photo != nullptr);
}

TextState WebPage::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}
	const auto bubble = _attach ? _attach->bubbleMargins() : QMargins();
	const auto full = QRect(0, 0, width(), height());
	auto outer = full.marginsRemoved(inBubblePadding());
	auto inner = outer.marginsRemoved(innerMargin());
	auto tshift = inner.top();
	auto paintw = inner.width();

	auto lineHeight = UnitedLineHeight();
	auto inThumb = false;
	if (asArticle()) {
		auto pw = qMax(_pixw, lineHeight);
		if (style::rtlrect(inner.left() + paintw - pw, tshift, pw, _pixh, width()).contains(point)) {
			inThumb = true;
		}
		paintw -= pw + st::webPagePhotoDelta;
	}
	int symbolAdd = 0;
	if (_siteNameLines) {
		if (point.y() >= tshift && point.y() < tshift + lineHeight) {
			Ui::Text::StateRequestElided siteNameRequest = request.forText();
			siteNameRequest.lines = _siteNameLines;
			result = TextState(_parent, _siteName.getStateElidedLeft(
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
		if (point.y() >= tshift && point.y() < tshift + _titleLines * lineHeight) {
			Ui::Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleLines;
			result = TextState(_parent, _title.getStateElidedLeft(
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
		auto descriptionHeight = (_descriptionLines > 0) ? _descriptionLines * lineHeight : _description.countHeight(paintw);
		if (point.y() >= tshift && point.y() < tshift + descriptionHeight) {
			if (_descriptionLines > 0) {
				Ui::Text::StateRequestElided descriptionRequest = request.forText();
				descriptionRequest.lines = _descriptionLines;
				result = TextState(_parent, _description.getStateElidedLeft(
					point - QPoint(inner.left(), tshift),
					paintw,
					width(),
					descriptionRequest));
			} else {
				result = TextState(_parent, _description.getStateLeft(
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
		auto attachAtTop = !_siteNameLines && !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		if (QRect(inner.left(), tshift, paintw, inner.top() + inner.height() - tshift).contains(point)) {
			auto attachLeft = inner.left() - bubble.left();
			auto attachTop = tshift - bubble.top();
			if (rtl()) attachLeft = width() - attachLeft - _attach->width();
			result = _attach->textState(point - QPoint(attachLeft, attachTop), request);
			if (result.cursor == CursorState::Enlarge) {
				result.cursor = CursorState::None;
			} else {
				result.link = replaceAttachLink(result.link);
			}
		}
	}
	if (!result.link && outer.contains(point)) {
		result.link = _openl;
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

TextSelection WebPage::adjustSelection(TextSelection selection, TextSelectType type) const {
	if ((!_titleLines && !_descriptionLines) || selection.to <= _siteName.length()) {
		return _siteName.adjustSelection(selection, type);
	}

	auto titlesLength = _siteName.length() + _title.length();
	auto titleSelection = _title.adjustSelection(toTitleSelection(selection), type);
	if ((!_siteNameLines && !_descriptionLines) || (selection.from >= _siteName.length() && selection.to <= titlesLength)) {
		return fromTitleSelection(titleSelection);
	}

	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if ((!_siteNameLines && !_titleLines) || selection.from >= titlesLength) {
		return fromDescriptionSelection(descriptionSelection);
	}

	auto siteNameSelection = _siteName.adjustSelection(selection, type);
	if (!_descriptionLines || selection.to <= titlesLength) {
		return { siteNameSelection.from, fromTitleSelection(titleSelection).to };
	}
	return { siteNameSelection.from, fromDescriptionSelection(descriptionSelection).to };
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
	if (p == _openl) {
		if (pressed) {
			if (!_ripple) {
				const auto full = QRect(0, 0, width(), height());
				const auto outer = full.marginsRemoved(inBubblePadding());
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
	const auto item = _parent->data();
	return !_data->pendingTill
		&& !_data->failed
		&& !item->Has<HistoryMessageLogEntryOriginal>();
}

QString WebPage::additionalInfoString() const {
	return _attach ? _attach->additionalInfoString() : QString();
}

bool WebPage::toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const {
	return _attach && _attach->toggleSelectionByHandlerClick(p);
}

bool WebPage::allowTextSelectionByHandler(
		const ClickHandlerPtr &p) const {
	return (p == _openl);
}

bool WebPage::dragItemByHandler(const ClickHandlerPtr &p) const {
	return _attach && _attach->dragItemByHandler(p);
}

TextForMimeData WebPage::selectedText(TextSelection selection) const {
	auto siteNameResult = _siteName.toTextForMimeData(selection);
	auto titleResult = _title.toTextForMimeData(
		toTitleSelection(selection));
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
		return siteNameResult.append('\n').append(std::move(descriptionResult));
	} else if (descriptionResult.empty()) {
		return siteNameResult.append('\n').append(std::move(titleResult));
	}

	return siteNameResult.append('\n').append(std::move(titleResult)).append('\n').append(std::move(descriptionResult));
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
	const auto button = _openButtonWidth ? st::historyPageButtonHeight : 0;
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
