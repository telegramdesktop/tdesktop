/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media.h"

#include "boxes/send_credits_box.h" // CreditsEmoji.
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_text_helper.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_media_spoiler.h"
#include "history/view/media/history_view_sticker.h"
#include "storage/storage_shared_media.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "lang/lang_keys.h"
#include "ui/item_text_options.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/image/image_prepare.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/text/text_utilities.h"
#include "core/ui_integration.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h" // mediaMenuIconStealth.

namespace HistoryView {
namespace {

[[nodiscard]] TimeId TimeFromMatch(
		QStringView hours,
		QStringView minutes1,
		QStringView minutes2,
		QStringView seconds) {
	auto ok1 = true;
	auto ok2 = true;
	auto ok3 = true;
	auto minutes = minutes1.toString();
	minutes += minutes2;
	const auto value1 = (hours.isEmpty() ? 0 : hours.toInt(&ok1));
	const auto value2 = minutes.toInt(&ok2);
	const auto value3 = seconds.toInt(&ok3);
	const auto ok = ok1 && ok2 && ok3;
	return (ok && value3 < 60 && (hours.isEmpty() || value2 < 60))
		? (value1 * 3600 + value2 * 60 + value3)
		: -1;
}

} // namespace

TimeId DurationForTimestampLinks(not_null<DocumentData*> document) {
	if (!document->isVideoFile()
		&& !document->isSong()
		&& !document->isVoiceMessage()) {
		return TimeId(0);
	}
	return std::max(document->duration(), crl::time(0)) / 1000;
}

QString TimestampLinkBase(
		not_null<DocumentData*> document,
		FullMsgId context) {
	return QString(
		"media_timestamp?base=doc%1_%2_%3&t="
	).arg(document->id).arg(context.peer.value).arg(context.msg.bare);
}

TimeId DurationForTimestampLinks(not_null<WebPageData*> webpage) {
	if (!webpage->collage.items.empty()) {
		return 0;
	} else if (const auto document = webpage->document) {
		return DurationForTimestampLinks(document);
	} else if (webpage->type != WebPageType::Video
		|| webpage->siteName != u"YouTube"_q) {
		return TimeId(0);
	} else if (webpage->duration > 0) {
		return webpage->duration;
	}
	constexpr auto kMaxYouTubeTimestampDuration = 100 * 60 * TimeId(60);
	return kMaxYouTubeTimestampDuration;
}

QString TimestampLinkBase(
		not_null<WebPageData*> webpage,
		FullMsgId context) {
	const auto url = webpage->url;
	if (url.isEmpty()) {
		return QString();
	}
	auto parts = url.split(QChar('#'));
	const auto base = parts[0];
	parts.pop_front();
	const auto use = [&] {
		const auto query = base.indexOf(QChar('?'));
		if (query < 0) {
			return base + QChar('?');
		}
		auto params = base.mid(query + 1).split(QChar('&'));
		for (auto i = params.begin(); i != params.end();) {
			if (i->startsWith("t=")) {
				i = params.erase(i);
			} else {
				++i;
			}
		}
		return base.mid(0, query)
			+ (params.empty() ? "?" : ("?" + params.join(QChar('&')) + "&"));
	}();
	return "url:"
		+ use
		+ "t="
		+ (parts.empty() ? QString() : ("#" + parts.join(QChar('#'))));
}

TextWithEntities AddTimestampLinks(
		TextWithEntities text,
		TimeId duration,
		const QString &base) {
	if (base.isEmpty()) {
		return text;
	}
	static const auto expression = QRegularExpression(
		"(?<![^\\s\\(\\)\"\\,\\.\\-])"
		"(?:(?:(\\d{1,2}):)?(\\d))?(\\d):(\\d\\d)"
		"(?![^\\s\\(\\)\",\\.\\-\\+])");
	const auto &string = text.text;
	auto offset = 0;
	while (true) {
		const auto m = expression.match(string, offset);
		if (!m.hasMatch()) {
			break;
		}

		const auto from = m.capturedStart();
		const auto till = from + m.capturedLength();
		offset = till;

		const auto time = TimeFromMatch(
			m.capturedView(1),
			m.capturedView(2),
			m.capturedView(3),
			m.capturedView(4));
		if (time < 0 || time > duration) {
			continue;
		}

		auto &entities = text.entities;
		auto i = ranges::lower_bound(
			entities,
			from,
			std::less<>(),
			&EntityInText::offset);
		while (i != entities.end()
			&& i->offset() < till
			&& i->type() == EntityType::Spoiler) {
			++i;
		}
		if (i != entities.end() && i->offset() < till) {
			continue;
		}

		const auto intersects = [&](const EntityInText &entity) {
			return (entity.offset() + entity.length() > from)
				&& (entity.type() != EntityType::Spoiler);
		};
		auto j = std::make_reverse_iterator(i);
		const auto e = std::make_reverse_iterator(entities.begin());
		if (std::find_if(j, e, intersects) != e) {
			continue;
		}

		entities.insert(
			i,
			EntityInText(
				EntityType::CustomUrl,
				from,
				till - from,
				("internal:" + base + QString::number(time))));
	}
	return text;
}

Storage::SharedMediaTypesMask Media::sharedMediaTypes() const {
	return {};
}

bool Media::allowTextSelectionByHandler(
		const ClickHandlerPtr &handler) const {
	return false;
}

not_null<Element*> Media::parent() const {
	return _parent;
}

not_null<History*> Media::history() const {
	return _parent->history();
}

SelectedQuote Media::selectedQuote(TextSelection selection) const {
	return {};
}

QSize Media::countCurrentSize(int newWidth) {
	return QSize(qMin(newWidth, maxWidth()), minHeight());
}

bool Media::hasPurchasedTag() const {
	if (const auto media = parent()->data()->media()) {
		if (const auto invoice = media->invoice()) {
			if (invoice->isPaidMedia && !invoice->extendedMedia.empty()) {
				const auto photo = invoice->extendedMedia.front()->photo();
				return !photo || !photo->extendedMediaPreview();
			}
		}
	}
	return false;
}

void Media::drawPurchasedTag(
		Painter &p,
		QRect outer,
		const PaintContext &context) const {
	const auto purchased = parent()->enforcePurchasedTag();
	if (purchased->text.isEmpty()) {
		const auto item = parent()->data();
		const auto media = item->media();
		const auto invoice = media ? media->invoice() : nullptr;
		const auto amount = invoice ? invoice->amount : 0;
		if (!amount) {
			return;
		}
		const auto session = &item->history()->session();
		auto text = Ui::Text::Colorized(Ui::CreditsEmojiSmall(session));
		text.append(Lang::FormatCountDecimal(amount));
		purchased->text.setMarkedText(st::defaultTextStyle, text, kMarkupTextOptions, Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = [] {},
		});
	}

	const auto st = context.st;
	const auto sti = context.imageStyle();
	const auto &padding = st::purchasedTagPadding;
	auto right = outer.x() + outer.width();
	auto top = outer.y();
	right -= st::msgDateImgDelta + padding.right();
	top += st::msgDateImgDelta + padding.top();

	const auto size = QSize(
		purchased->text.maxWidth(),
		st::normalFont->height);
	const auto tagX = right - size.width();
	const auto tagY = top;
	const auto tagW = padding.left() + size.width() + padding.right();
	const auto tagH = padding.top() + size.height() + padding.bottom();
	Ui::FillRoundRect(
		p,
		tagX - padding.left(),
		tagY - padding.top(),
		tagW,
		tagH,
		sti->msgDateImgBg,
		sti->msgDateImgBgCorners);

	p.setPen(st->msgDateImgFg());
	purchased->text.draw(p, {
		.position = { tagX, tagY },
		.outerWidth = width(),
		.availableWidth = size.width(),
		.palette = &st->priceTagTextPalette(),
	});
}

void Media::fillImageShadow(
		QPainter &p,
		QRect rect,
		Ui::BubbleRounding rounding,
		const PaintContext &context) const {
	const auto sti = context.imageStyle();
	auto corners = Ui::CornersPixmaps();
	const auto choose = [&](int index) -> QPixmap {
		using Corner = Ui::BubbleCornerRounding;
		switch (rounding[index]) {
		case Corner::Large: return sti->msgShadowCornersLarge.p[index];
		case Corner::Small: return sti->msgShadowCornersSmall.p[index];
		}
		return QPixmap();
	};
	corners.p[2] = choose(2);
	corners.p[3] = choose(3);
	Ui::FillRoundShadow(p, rect, sti->msgShadow, corners);
}

void Media::fillImageOverlay(
		QPainter &p,
		QRect rect,
		std::optional<Ui::BubbleRounding> rounding,
		const PaintContext &context) const {
	using Radius = Ui::CachedCornerRadius;
	const auto &st = context.st;
	if (!rounding) {
		Ui::FillComplexOverlayRect(
			p,
			rect,
			st->msgSelectOverlay(),
			st->msgSelectOverlayCorners(Radius::Small));
		return;
	}
	using Corner = Ui::BubbleCornerRounding;
	auto corners = Ui::CornersPixmaps();
	const auto lookup = [&](Corner corner) {
		switch (corner) {
		case Corner::None: return Radius::kCount;
		case Corner::Small: return Radius::BubbleSmall;
		case Corner::Large: return Radius::BubbleLarge;
		}
		Unexpected("Corner value in Document::fillThumbnailOverlay.");
	};
	for (auto i = 0; i != 4; ++i) {
		const auto radius = lookup((*rounding)[i]);
		corners.p[i] = (radius == Radius::kCount)
			? QPixmap()
			: st->msgSelectOverlayCorners(radius).p[i];
	}
	Ui::FillComplexOverlayRect(p, rect, st->msgSelectOverlay(), corners);
}

void Media::fillImageSpoiler(
		QPainter &p,
		not_null<MediaSpoiler*> spoiler,
		QRect rect,
		const PaintContext &context) const {
	if (!spoiler->animation) {
		spoiler->animation = std::make_unique<Ui::SpoilerAnimation>([=] {
			_parent->customEmojiRepaint();
		});
		history()->owner().registerHeavyViewPart(_parent);
	}
	_parent->clearCustomEmojiRepaint();
	const auto pausedSpoiler = context.paused
		|| On(PowerSaving::kChatSpoiler);
	Ui::FillSpoilerRect(
		p,
		rect,
		MediaRoundingMask(spoiler->backgroundRounding),
		Ui::DefaultImageSpoiler().frame(
			spoiler->animation->index(context.now, pausedSpoiler)),
		spoiler->cornerCache);
}

void Media::drawSpoilerTag(
		Painter &p,
		not_null<MediaSpoiler*> spoiler,
		std::unique_ptr<MediaSpoilerTag> &tag,
		QRect rthumb,
		const PaintContext &context,
		Fn<QImage()> generateBackground) const {
	if (!tag) {
		setupSpoilerTag(tag);
		if (!tag) {
			return;
		}
	}
	const auto revealed = spoiler->revealAnimation.value(
		spoiler->revealed ? 1. : 0.);
	if (revealed == 1.) {
		return;
	}
	p.setOpacity(1. - revealed);
	const auto st = context.st;
	const auto darken = st->msgDateImgBg()->c;
	const auto fg = st->msgDateImgFg()->c;
	const auto star = st->creditsBg1()->c;
	if (tag->cache.isNull()
		|| tag->darken != darken
		|| tag->fg != fg
		|| tag->star != star) {
		const auto ratio = style::DevicePixelRatio();
		auto bg = generateBackground();
		if (bg.isNull()) {
			bg = QImage(ratio, ratio, QImage::Format_ARGB32_Premultiplied);
			bg.fill(Qt::black);
		}

		auto text = Ui::Text::String();
		auto iconSkip = 0;
		if (tag->sensitive) {
			text.setText(
				st::semiboldTextStyle,
				tr::lng_sensitive_tag(tr::now));
			iconSkip = st::mediaMenuIconStealth.width() * 1.4;
		} else {
			const auto session = &history()->session();
			auto price = Ui::Text::Colorized(Ui::CreditsEmoji(session));
			price.append(Lang::FormatCountDecimal(tag->price));
			text.setMarkedText(
				st::semiboldTextStyle,
				tr::lng_paid_price(
					tr::now,
					lt_price,
					price,
					Ui::Text::WithEntities),
				kMarkupTextOptions,
				Core::MarkedTextContext{
					.session = session,
					.customEmojiRepaint = [] {},
				});
		}
		const auto width = iconSkip + text.maxWidth();
		const auto inner = QRect(0, 0, width, text.minHeight());
		const auto outer = inner.marginsAdded(st::paidTagPadding);
		const auto size = outer.size();
		const auto radius = std::min(size.width(), size.height()) / 2;
		auto cache = QImage(
			size * ratio,
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(ratio);
		cache.fill(Qt::black);
		auto p = Painter(&cache);
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(
			QRect(
				(size.width() - rthumb.width()) / 2,
				(size.height() - rthumb.height()) / 2,
				rthumb.width(),
				rthumb.height()),
			bg);
		p.fillRect(QRect(QPoint(), size), darken);
		p.setPen(fg);
		p.setTextPalette(st->priceTagTextPalette());
		if (iconSkip) {
			st::mediaMenuIconStealth.paint(
				p,
				-outer.x(),
				(size.height() - st::mediaMenuIconStealth.height()) / 2,
				size.width(),
				fg);
		}
		text.draw(p, iconSkip - outer.x(), -outer.y(), width);
		p.end();

		tag->darken = darken;
		tag->fg = fg;
		tag->cache = Images::Round(
			std::move(cache),
			Images::CornersMask(radius));
	}
	const auto &cache = tag->cache;
	const auto size = cache.size() / cache.devicePixelRatio();
	const auto left = rthumb.x() + (rthumb.width() - size.width()) / 2;
	const auto top = rthumb.y() + (rthumb.height() - size.height()) / 2;
	p.drawImage(left, top, cache);
	if (context.selected()) {
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = std::min(size.width(), size.height()) / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(st->msgSelectOverlay());
		p.drawRoundedRect(
			QRect(left, top, size.width(), size.height()),
			radius,
			radius);
	}
	p.setOpacity(1.);
}

void Media::setupSpoilerTag(std::unique_ptr<MediaSpoilerTag> &tag) const {
	const auto item = parent()->data();
	if (item->isMediaSensitive()) {
		tag = std::make_unique<MediaSpoilerTag>();
		tag->sensitive = 1;
		return;
	}
	const auto media = parent()->data()->media();
	const auto invoice = media ? media->invoice() : nullptr;
	if (const auto price = invoice->isPaidMedia ? invoice->amount : 0) {
		tag = std::make_unique<MediaSpoilerTag>();
		tag->price = price;
	}
}

ClickHandlerPtr Media::spoilerTagLink(
		not_null<MediaSpoiler*> spoiler,
		std::unique_ptr<MediaSpoilerTag> &tag) const {
	const auto item = parent()->data();
	if (!item->isRegular() || spoiler->revealed) {
		return nullptr;
	} else if (!tag) {
		setupSpoilerTag(tag);
		if (!tag) {
			return nullptr;
		}
	}
	if (!tag->link) {
		tag->link = tag->sensitive
			? MakeSensitiveMediaLink(spoiler->link, item)
			: MakePaidMediaLink(item);
	}
	return tag->link;
}

void Media::createSpoilerLink(not_null<MediaSpoiler*> spoiler) {
	const auto weak = base::make_weak(this);
	spoiler->link = std::make_shared<LambdaClickHandler>([weak, spoiler](
			const ClickContext &context) {
		const auto button = context.button;
		const auto media = weak.get();
		if (button != Qt::LeftButton || !media || spoiler->revealed) {
			return;
		}
		const auto view = media->parent();
		spoiler->revealed = true;
		spoiler->revealAnimation.start([=] {
			view->repaint();
		}, 0., 1., st::fadeWrapDuration);
		view->repaint();
		media->history()->owner().registerShownSpoiler(view);
	});
}

void Media::repaint() const {
	_parent->repaint();
}

Ui::Text::String Media::createCaption(not_null<HistoryItem*> item) const {
	if (item->emptyText()) {
		return {};
	}
	const auto minResizeWidth = st::minPhotoSize
		- st::msgPadding.left()
		- st::msgPadding.right();
	auto result = Ui::Text::String(minResizeWidth);
	const auto context = Core::MarkedTextContext{
		.session = &history()->session(),
		.customEmojiRepaint = [=] { _parent->customEmojiRepaint(); },
	};
	result.setMarkedText(
		st::messageTextStyle,
		item->translatedTextWithLocalEntities(),
		Ui::ItemTextOptions(item),
		context);
	InitElementTextPart(_parent, result);
	if (const auto width = _parent->skipBlockWidth()) {
		result.updateSkipBlock(width, _parent->skipBlockHeight());
	}
	return result;
}

TextSelection Media::skipSelection(TextSelection selection) const {
	return UnshiftItemSelection(selection, fullSelectionLength());
}

TextSelection Media::unskipSelection(TextSelection selection) const {
	return ShiftItemSelection(selection, fullSelectionLength());
}

auto Media::getBubbleSelectionIntervals(
	TextSelection selection) const
-> std::vector<Ui::BubbleSelectionInterval> {
	return {};
}

bool Media::usesBubblePattern(const PaintContext &context) const {
	return _parent->usesBubblePattern(context);
}

PointState Media::pointState(QPoint point) const {
	return QRect(0, 0, width(), height()).contains(point)
		? PointState::Inside
		: PointState::Outside;
}

std::unique_ptr<StickerPlayer> Media::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return nullptr;
}

QImage Media::locationTakeImage() {
	return QImage();
}

TextState Media::getStateGrouped(
		const QRect &geometry,
		RectParts sides,
		QPoint point,
		StateRequest request) const {
	Unexpected("Grouping method call.");
}

Ui::BubbleRounding Media::adjustedBubbleRounding(RectParts square) const {
	auto result = bubbleRounding();
	using Corner = Ui::BubbleCornerRounding;
	const auto adjust = [&](bool round, Corner already, RectPart corner) {
		return (already == Corner::Tail || !round || (square & corner))
			? Corner::None
			: already;
	};
	const auto top = isBubbleTop();
	const auto bottom = isRoundedInBubbleBottom();
	result.topLeft = adjust(top, result.topLeft, RectPart::TopLeft);
	result.topRight = adjust(top, result.topRight, RectPart::TopRight);
	result.bottomLeft = adjust(
		bottom,
		result.bottomLeft,
		RectPart::BottomLeft);
	result.bottomRight = adjust(
		bottom,
		result.bottomRight,
		RectPart::BottomRight);
	return result;
}

HistoryItem *Media::itemForText() const {
	return _parent->data();
}

bool Media::isRoundedInBubbleBottom() const {
	return isBubbleBottom()
		&& !_parent->data()->repliesAreComments()
		&& !_parent->data()->externalReply();
}

Images::CornersMaskRef MediaRoundingMask(
		std::optional<Ui::BubbleRounding> rounding) {
	using Radius = Ui::CachedCornerRadius;
	if (!rounding) {
		return Images::CornersMaskRef(Ui::CachedCornersMasks(Radius::Small));
	}
	using Corner = Ui::BubbleCornerRounding;
	auto result = Images::CornersMaskRef();
	const auto &small = Ui::CachedCornersMasks(Radius::BubbleSmall);
	const auto &large = Ui::CachedCornersMasks(Radius::BubbleLarge);
	for (auto i = 0; i != 4; ++i) {
		switch ((*rounding)[i]) {
		case Corner::Small: result.p[i] = &small[i]; break;
		case Corner::Large: result.p[i] = &large[i]; break;
		}
	}
	return result;

}

} // namespace HistoryView
