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
#include "history/view/media/history_view_media_common.h"
#include "ui/image/image.h"
#include "ui/text/text_options.h"
#include "ui/text/format_values.h"
#include "ui/cached_round_corners.h"
#include "layout.h" // FullSelection
#include "data/data_session.h"
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
	for (const auto item : data.items) {
		if (const auto document = std::get_if<DocumentData*>(&item)) {
			result.push_back(std::make_unique<Data::MediaFile>(
				parent,
				*document));
		} else if (const auto photo = std::get_if<PhotoData*>(&item)) {
			result.push_back(std::make_unique<Data::MediaPhoto>(
				parent,
				*photo));
		} else {
			return {};
		}
		if (!result.back()->canBeGrouped()) {
			return {};
		}
	}
	return result;
}

} // namespace

WebPage::WebPage(
	not_null<Element*> parent,
	not_null<WebPageData*> data)
: Media(parent)
, _data(data)
, _siteName(st::msgMinWidth - st::webPageLeft)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft) {
	history()->owner().registerWebPageView(_data, _parent);
}

QSize WebPage::countOptimalSize() {
	if (_data->pendingTill) {
		return { 0, 0 };
	}
	const auto versionChanged = (_dataVersion != _data->version);
	if (versionChanged) {
		_dataVersion = _data->version;
		_openl = nullptr;
		_attach = nullptr;
		_collage = PrepareCollageMedia(_parent->data(), _data->collage);
		_siteName = Ui::Text::String(st::msgMinWidth - st::webPageLeft);
		_title = Ui::Text::String(st::msgMinWidth - st::webPageLeft);
		_description = Ui::Text::String(st::msgMinWidth - st::webPageLeft);
	}
	auto lineHeight = unitedLineHeight();

	if (!_openl && !_data->url.isEmpty()) {
		const auto previewOfHiddenUrl = [&] {
			const auto simplify = [](const QString &url) {
				auto result = url.toLower();
				if (result.endsWith('/')) {
					result.chop(1);
				}
				const auto prefixes = { qstr("http://"), qstr("https://") };
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
	if (!_collage.empty()) {
		_asArticle = false;
	} else if (!_data->document
		&& _data->photo
		&& _data->type != WebPageType::Photo
		&& _data->type != WebPageType::Video) {
		if (_data->type == WebPageType::Profile) {
			_asArticle = true;
		} else if (_data->siteName == qstr("Twitter")
			|| _data->siteName == qstr("Facebook")
			|| _data->type == WebPageType::ArticleWithIV) {
			_asArticle = false;
		} else {
			_asArticle = true;
		}
		if (_asArticle
			&& _data->description.text.isEmpty()
			&& title.isEmpty()
			&& _data->siteName.isEmpty()) {
			_asArticle = false;
		}
	} else {
		_asArticle = false;
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

	auto textFloatsAroundInfo = !_asArticle && !_attach && isBubbleBottom();

	// init strings
	if (_description.isEmpty() && !_data->description.text.isEmpty()) {
		auto text = _data->description;

		if (textFloatsAroundInfo) {
			text.text += _parent->skipBlock();
		}
		if (isLogEntryOriginal()) {
			// Fix layout for small bubbles (narrow media caption edit log entries).
			_description = Ui::Text::String(st::minPhotoSize
				- st::msgPadding.left()
				- st::msgPadding.right()
				- st::webPageLeft);
		}
		auto context = Core::MarkedTextContext();
		using MarkedTextContext = Core::MarkedTextContext;
		if (_data->siteName == qstr("Twitter")) {
			context.type = MarkedTextContext::HashtagMentionType::Twitter;
		} else if (_data->siteName == qstr("Instagram")) {
			context.type = MarkedTextContext::HashtagMentionType::Instagram;
		}
		_description.setMarkedText(
			st::webPageDescriptionStyle,
			text,
			Ui::WebpageTextDescriptionOptions(),
			context);
	}
	if (!displayedSiteName().isEmpty()) {
		_siteNameLines = 1;
		_siteName.setRichText(
			st::webPageTitleStyle,
			textcmdLink(_data->url, displayedSiteName()),
			Ui::WebpageTextTitleOptions());
	}
	if (_title.isEmpty() && !title.isEmpty()) {
		if (textFloatsAroundInfo && _description.isEmpty()) {
			title += _parent->skipBlock();
		}
		if (!_siteNameLines && !_data->url.isEmpty()) {
			_title.setRichText(
				st::webPageTitleStyle,
				textcmdLink(_data->url, title),
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
		if (_title.isEmpty() && _description.isEmpty() && textFloatsAroundInfo) {
			accumulate_max(maxWidth, _siteName.maxWidth() + _parent->skipBlockWidth());
		} else {
			accumulate_max(maxWidth, _siteName.maxWidth() + articlePhotoMaxWidth);
		}
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
		if (!_attach->additionalInfoString().isEmpty()) {
			minHeight += bottomInfoPadding();
		}
	}
	if (_data->type == WebPageType::Video && _data->duration) {
		_duration = Ui::FormatDurationText(_data->duration);
		_durationWidth = st::msgDateFont->width(_duration);
	}
	maxWidth += st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();
	auto padding = inBubblePadding();
	minHeight += padding.top() + padding.bottom();

	if (_asArticle) {
		minHeight = resizeGetHeight(maxWidth);
	}
	return { maxWidth, minHeight };
}

QSize WebPage::countCurrentSize(int newWidth) {
	if (_data->pendingTill) {
		return { newWidth, minHeight() };
	}

	auto innerWidth = newWidth - st::msgPadding.left() - st::webPageLeft - st::msgPadding.right();
	auto newHeight = 0;

	auto lineHeight = unitedLineHeight();
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
		newHeight += bottomInfoPadding();
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
			if (!_attach->additionalInfoString().isEmpty()) {
				newHeight += bottomInfoPadding();
			} else if (isBubbleBottom() && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > innerWidth + bubble.left() + bubble.right()) {
				newHeight += bottomInfoPadding();
			}
		}
	}
	auto padding = inBubblePadding();
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
	_photoMedia = nullptr;
}

void WebPage::draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	auto &barfg = selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	paintw -= padding.left() + padding.right();
	auto attachAdditionalInfoText = _attach ? _attach->additionalInfoString() : QString();
	if (asArticle()) {
		bshift += bottomInfoPadding();
	} else if (!attachAdditionalInfoText.isEmpty()) {
		bshift += bottomInfoPadding();
	} else if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}

	QRect bar(style::rtlrect(st::msgPadding.left(), tshift, st::webPageBar, height() - tshift - bshift, width()));
	p.fillRect(bar, barfg);

	auto lineHeight = unitedLineHeight();
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
		if (const auto thumbnail = _photoMedia->image(
				Data::PhotoSize::Thumbnail)) {
			pix = thumbnail->pixSingle(pixw, pixh, pw, ph, ImageRoundRadius::Small);
		} else if (const auto small = _photoMedia->image(
				Data::PhotoSize::Small)) {
			pix = small->pixBlurredSingle(pixw, pixh, pw, ph, ImageRoundRadius::Small);
		} else if (const auto blurred = _photoMedia->thumbnailInline()) {
			pix = blurred->pixBlurredSingle(pixw, pixh, pw, ph, ImageRoundRadius::Small);
		}
		p.drawPixmapLeft(padding.left() + paintw - pw, tshift, width(), pix);
		if (selected) {
			Ui::FillRoundRect(p, style::rtlrect(padding.left() + paintw - pw, tshift, pw, _pixh, width()), p.textPalette().selectOverlay, Ui::SelectedOverlaySmallCorners);
		}
		paintw -= pw + st::webPagePhotoDelta;
	}
	if (_siteNameLines) {
		p.setPen(semibold);
		auto endskip = 0;
		if (_siteName.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_siteName.drawLeftElided(p, padding.left(), tshift, paintw, width(), _siteNameLines, style::al_left, 0, -1, endskip, false, selection);
		tshift += lineHeight;
	}
	if (_titleLines) {
		p.setPen(outbg ? st::webPageTitleOutFg : st::webPageTitleInFg);
		auto endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, padding.left(), tshift, paintw, width(), _titleLines, style::al_left, 0, -1, endskip, false, toTitleSelection(selection));
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		auto endskip = 0;
		if (_description.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		if (_descriptionLines > 0) {
			_description.drawLeftElided(p, padding.left(), tshift, paintw, width(), _descriptionLines, style::al_left, 0, -1, endskip, false, toDescriptionSelection(selection));
			tshift += _descriptionLines * lineHeight;
		} else {
			_description.drawLeft(p, padding.left(), tshift, paintw, width(), style::al_left, 0, -1, toDescriptionSelection(selection));
			tshift += _description.countHeight(paintw);
		}
	}
	if (_attach) {
		auto attachAtTop = !_siteNameLines && !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = width() - attachLeft - _attach->width();

		p.translate(attachLeft, attachTop);

		auto attachSelection = selected ? FullSelection : TextSelection { 0, 0 };
		_attach->draw(p, r.translated(-attachLeft, -attachTop), attachSelection, ms);
		auto pixwidth = _attach->width();
		auto pixheight = _attach->height();

		if (_data->type == WebPageType::Video
			&& _collage.empty()
			&& _data->photo
			&& !_data->document) {
			if (_attach->isReadyForOpen()) {
				if (_data->siteName == qstr("YouTube")) {
					st::youtubeIcon.paint(p, (pixwidth - st::youtubeIcon.width()) / 2, (pixheight - st::youtubeIcon.height()) / 2, width());
				} else {
					st::videoIcon.paint(p, (pixwidth - st::videoIcon.width()) / 2, (pixheight - st::videoIcon.height()) / 2, width());
				}
			}
			if (_durationWidth) {
				auto dateX = pixwidth - _durationWidth - st::msgDateImgDelta - 2 * st::msgDateImgPadding.x();
				auto dateY = pixheight - st::msgDateFont->height - 2 * st::msgDateImgPadding.y() - st::msgDateImgDelta;
				auto dateW = pixwidth - dateX - st::msgDateImgDelta;
				auto dateH = pixheight - dateY - st::msgDateImgDelta;

				Ui::FillRoundRect(p, dateX, dateY, dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? Ui::DateSelectedCorners : Ui::DateCorners);

				p.setFont(st::msgDateFont);
				p.setPen(st::msgDateImgFg);
				p.drawTextLeft(dateX + st::msgDateImgPadding.x(), dateY + st::msgDateImgPadding.y(), pixwidth, _duration);
			}
		}

		p.translate(-attachLeft, -attachTop);

		if (!attachAdditionalInfoText.isEmpty()) {
			p.setFont(st::msgDateFont);
			p.setPen(selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg));
			p.drawTextLeft(st::msgPadding.left(), bar.y() + bar.height() + st::mediaInBubbleSkip, width(), attachAdditionalInfoText);
		}
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
	auto paintw = width();

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	if (asArticle() || (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->width() + _parent->skipBlockWidth() > paintw + bubble.left() + bubble.right())) {
		bshift += bottomInfoPadding();
	}
	paintw -= padding.left() + padding.right();

	auto lineHeight = unitedLineHeight();
	auto inThumb = false;
	if (asArticle()) {
		auto pw = qMax(_pixw, lineHeight);
		if (style::rtlrect(padding.left() + paintw - pw, 0, pw, _pixh, width()).contains(point)) {
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
				point - QPoint(padding.left(), tshift),
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
				point - QPoint(padding.left(), tshift),
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
					point - QPoint(padding.left(), tshift),
					paintw,
					width(),
					descriptionRequest));
			} else {
				result = TextState(_parent, _description.getStateLeft(
					point - QPoint(padding.left(), tshift),
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

		if (QRect(padding.left(), tshift, paintw, height() - tshift - bshift).contains(point)) {
			auto attachLeft = padding.left() - bubble.left();
			auto attachTop = tshift - bubble.top();
			if (rtl()) attachLeft = width() - attachLeft - _attach->width();
			result = _attach->textState(point - QPoint(attachLeft, attachTop), request);
			result.link = replaceAttachLink(result.link);
		}
	}

	result.symbol += symbolAdd;
	return result;
}

ClickHandlerPtr WebPage::replaceAttachLink(
		const ClickHandlerPtr &link) const {
	if (!link || !_attach->isReadyForOpen() || !_collage.empty()) {
		return link;
	}
	if (_data->document) {
		if (_data->document->isWallPaper() || _data->document->isTheme()) {
			return _openl;
		}
	} else if (_data->photo) {
		if (_data->type == WebPageType::Profile
			|| _data->type == WebPageType::Video) {
			return _openl;
		} else if (_data->type == WebPageType::Photo
			|| _data->siteName == qstr("Twitter")
			|| _data->siteName == qstr("Facebook")) {
			// leave photo link
		} else {
			return _openl;
		}
	}
	return link;
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

void WebPage::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void WebPage::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
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
		&& !item->Has<HistoryMessageLogEntryOriginal>();
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
	auto lshift = st::msgPadding.left() + st::webPageLeft;
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

bool WebPage::isLogEntryOriginal() const {
	return _parent->data()->isAdminLogEntry() && _parent->media() != this;
}

int WebPage::bottomInfoPadding() const {
	if (!isBubbleBottom()) return 0;

	auto result = st::msgDateFont->height;

	// We use padding greater than st::msgPadding.bottom() in the
	// bottom of the bubble so that the left line looks pretty.
	// but if we have bottom skip because of the info display
	// we don't need that additional padding so we replace it
	// back with st::msgPadding.bottom() instead of left().
	result += st::msgPadding.bottom() - st::msgPadding.left();
	return result;
}

QString WebPage::displayedSiteName() const {
	return (_data->document && _data->document->isWallPaper())
		? tr::lng_media_chat_background(tr::now)
		: (_data->document && _data->document->isTheme())
		? tr::lng_media_color_theme(tr::now)
		: _data->siteName;
}

WebPage::~WebPage() {
	history()->owner().unregisterWebPageView(_data, _parent);
	if (_photoMedia) {
		history()->owner().keepAlive(base::take(_photoMedia));
		_parent->checkHeavyPart();
	}
}

} // namespace HistoryView
