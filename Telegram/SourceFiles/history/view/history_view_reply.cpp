/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_reply.h"

#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_story.h"
#include "data/data_user.h"
#include "history/view/history_view_item_preview.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace HistoryView {
namespace {

constexpr auto kNonExpandedLinesLimit = 5;

void ValidateBackgroundEmoji(
		DocumentId backgroundEmojiId,
		not_null<Ui::BackgroundEmojiData*> data,
		not_null<Ui::BackgroundEmojiCache*> cache,
		not_null<Ui::Text::QuotePaintCache*> quote,
		not_null<const Element*> view) {
	if (data->firstFrameMask.isNull()) {
		if (!cache->frames[0].isNull()) {
			for (auto &frame : cache->frames) {
				frame = QImage();
			}
		}
		const auto tag = Data::CustomEmojiSizeTag::Isolated;
		if (!data->emoji) {
			const auto owner = &view->history()->owner();
			const auto repaint = crl::guard(view, [=] {
				view->history()->owner().requestViewRepaint(view);
			});
			data->emoji = owner->customEmojiManager().create(
				backgroundEmojiId,
				repaint,
				tag);
		}
		if (!data->emoji->ready()) {
			return;
		}
		const auto size = Data::FrameSizeFromTag(tag);
		data->firstFrameMask = QImage(
			QSize(size, size),
			QImage::Format_ARGB32_Premultiplied);
		data->firstFrameMask.fill(Qt::transparent);
		data->firstFrameMask.setDevicePixelRatio(style::DevicePixelRatio());
		auto p = Painter(&data->firstFrameMask);
		data->emoji->paint(p, {
			.textColor = QColor(255, 255, 255),
			.position = QPoint(0, 0),
			.internal = {
				.forceFirstFrame = true,
			},
		});
		p.end();

		data->emoji = nullptr;
	}
	if (!cache->frames[0].isNull() && cache->color == quote->icon) {
		return;
	}
	cache->color = quote->icon;
	const auto ratio = style::DevicePixelRatio();
	auto colorized = QImage(
		data->firstFrameMask.size(),
		QImage::Format_ARGB32_Premultiplied);
	colorized.setDevicePixelRatio(ratio);
	style::colorizeImage(
		data->firstFrameMask,
		cache->color,
		&colorized,
		QRect(), // src
		QPoint(), // dst
		true); // use alpha
	const auto make = [&](int size) {
		size = style::ConvertScale(size) * ratio;
		auto result = colorized.scaled(
			size,
			size,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		result.setDevicePixelRatio(ratio);
		return result;
	};

	constexpr auto kSize1 = 12;
	constexpr auto kSize2 = 16;
	constexpr auto kSize3 = 20;
	cache->frames[0] = make(kSize1);
	cache->frames[1] = make(kSize2);
	cache->frames[2] = make(kSize3);
}

void FillBackgroundEmoji(
		Painter &p,
		const QRect &rect,
		bool quote,
		const Ui::BackgroundEmojiCache &cache) {
	p.setClipRect(rect);

	const auto &frames = cache.frames;
	const auto right = rect.x() + rect.width();
	const auto paint = [&](int x, int y, int index, float64 opacity) {
		y = style::ConvertScale(y);
		if (y >= rect.height()) {
			return;
		}
		p.setOpacity(opacity);
		p.drawImage(
			right - style::ConvertScale(x + (quote ? 12 : 0)),
			rect.y() + y,
			frames[index]);
	};

	paint(28, 4, 2, 0.32);
	paint(51, 15, 1, 0.32);
	paint(64, -2, 0, 0.28);
	paint(87, 11, 1, 0.24);
	paint(125, -2, 2, 0.16);

	paint(28, 31, 1, 0.24);
	paint(72, 33, 2, 0.2);

	paint(46, 52, 1, 0.24);
	paint(24, 55, 2, 0.18);

	if (quote) {
		paint(4, 23, 1, 0.28);
		paint(0, 48, 0, 0.24);
	}

	p.setClipping(false);
	p.setOpacity(1.);
}

} // namespace

Reply::Reply()
: _name(st::maxSignatureSize / 2)
, _text(st::maxSignatureSize / 2) {
}

Reply &Reply::operator=(Reply &&other) = default;

Reply::~Reply() = default;

void Reply::update(
		not_null<Element*> view,
		not_null<HistoryMessageReply*> data) {
	const auto item = view->data();
	const auto &fields = data->fields();
	const auto message = data->resolvedMessage.get();
	const auto story = data->resolvedStory.get();
	const auto externalMedia = fields.externalMedia.get();
	if (!_externalSender) {
		if (const auto id = fields.externalSenderId) {
			_externalSender = view->history()->owner().peer(id);
		}
	}
	_colorPeer = message
		? message->displayFrom()
		: story
		? story->peer().get()
		: _externalSender
		? _externalSender
		: nullptr;
	_hiddenSenderColorIndexPlusOne = (!_colorPeer && message)
		? (message->hiddenSenderInfo()->colorIndex + 1)
		: 0;

	const auto hasPreview = (story && story->hasReplyPreview())
		|| (message
			&& message->media()
			&& message->media()->hasReplyPreview())
		|| (externalMedia && externalMedia->hasReplyPreview());
	_hasPreview = hasPreview ? 1 : 0;
	_displaying = data->displaying() ? 1 : 0;
	_multiline = data->multiline() ? 1 : 0;
	_replyToStory = (fields.storyId != 0);
	const auto hasQuoteIcon = _displaying
		&& fields.manualQuote
		&& !fields.quote.empty();
	_hasQuoteIcon = hasQuoteIcon ? 1 : 0;

	const auto text = (!_displaying && data->unavailable())
		? TextWithEntities()
		: (message && (fields.quote.empty() || !fields.manualQuote))
		? message->inReplyText()
		: !fields.quote.empty()
		? fields.quote
		: story
		? story->inReplyText()
		: externalMedia
		? externalMedia->toPreview({
			.hideSender = true,
			.hideCaption = true,
			.ignoreMessageText = true,
			.generateImages = false,
			.ignoreGroup = true,
			.ignoreTopic = true,
		}).text
		: TextWithEntities();
	const auto repaint = [=] { item->customEmojiRepaint(); };
	const auto context = Core::MarkedTextContext{
		.session = &view->history()->session(),
		.customEmojiRepaint = repaint,
	};
	_text.setMarkedText(
		st::defaultTextStyle,
		text,
		_multiline ? Ui::ItemTextDefaultOptions() : Ui::DialogTextOptions(),
		context);

	updateName(view, data);

	if (_displaying) {
		setLinkFrom(view, data);
		const auto media = message ? message->media() : nullptr;
		if (!media || !media->hasReplyPreview() || !media->hasSpoiler()) {
			_spoiler = nullptr;
		} else if (!_spoiler) {
			_spoiler = std::make_unique<Ui::SpoilerAnimation>(repaint);
		}
	} else {
		_spoiler = nullptr;
	}
}

bool Reply::expand() {
	if (!_expandable || _expanded) {
		return false;
	}
	_expanded = true;
	return true;
}

void Reply::setLinkFrom(
		not_null<Element*> view,
		not_null<HistoryMessageReply*> data) {
	const auto weak = base::make_weak(view);
	const auto &fields = data->fields();
	const auto externalChannelId = peerToChannel(fields.externalPeerId);
	const auto messageId = fields.messageId;
	const auto quote = fields.manualQuote
		? fields.quote
		: TextWithEntities();
	const auto returnToId = view->data()->fullId();
	const auto externalLink = [=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			auto error = QString();
			const auto owner = &controller->session().data();
			if (const auto view = weak.get()) {
				if (const auto reply = view->Get<Reply>()) {
					if (reply->expand()) {
						owner->requestViewResize(view);
						return;
					}
				}
			}
			if (externalChannelId) {
				const auto channel = owner->channel(externalChannelId);
				if (!channel->isForbidden()) {
					if (messageId) {
						JumpToMessageClickHandler(
							channel,
							messageId,
							returnToId,
							quote
						)->onClick(context);
					} else {
						controller->showPeerInfo(channel);
					}
				} else if (channel->isBroadcast()) {
					error = tr::lng_channel_not_accessible(tr::now);
				} else {
					error = tr::lng_group_not_accessible(tr::now);
				}
			} else {
				error = tr::lng_reply_from_private_chat(tr::now);
			}
			if (!error.isEmpty()) {
				controller->showToast(error);
			}
		}
	};
	const auto message = data->resolvedMessage.get();
	const auto story = data->resolvedStory.get();
	_link = message
		? JumpToMessageClickHandler(message, returnToId, quote)
		: story
		? JumpToStoryClickHandler(story)
		: (data->external()
			&& (!fields.messageId
				|| (data->unavailable() && externalChannelId)))
		? std::make_shared<LambdaClickHandler>(externalLink)
		: nullptr;
}

PeerData *Reply::sender(
		not_null<const Element*> view,
		not_null<HistoryMessageReply*> data) const {
	const auto message = data->resolvedMessage.get();
	if (const auto story = data->resolvedStory.get()) {
		return story->peer();
	} else if (!message) {
		return _externalSender;
	} else if (view->data()->Has<HistoryMessageForwarded>()) {
		// Forward of a reply. Show reply-to original sender.
		const auto forwarded = message->Get<HistoryMessageForwarded>();
		if (forwarded) {
			return forwarded->originalSender;
		}
	}
	if (const auto from = message->displayFrom()) {
		return from;
	}
	return message->author().get();
}

QString Reply::senderName(
		not_null<const Element*> view,
		not_null<HistoryMessageReply*> data,
		bool shorten) const {
	if (const auto peer = sender(view, data)) {
		return senderName(peer, shorten);
	} else if (!data->resolvedMessage) {
		return data->fields().externalSenderName;
	} else if (view->data()->Has<HistoryMessageForwarded>()) {
		// Forward of a reply. Show reply-to original sender.
		const auto forwarded
			= data->resolvedMessage->Get<HistoryMessageForwarded>();
		if (forwarded) {
			Assert(forwarded->hiddenSenderInfo != nullptr);
			return forwarded->hiddenSenderInfo->name;
		}
	}
	return QString();
}

QString Reply::senderName(
		not_null<PeerData*> peer,
		bool shorten) const {
	const auto user = shorten ? peer->asUser() : nullptr;
	return user ? user->firstName : peer->name();
}

bool Reply::isNameUpdated(
		not_null<const Element*> view,
		not_null<HistoryMessageReply*> data) const {
	if (const auto from = sender(view, data)) {
		if (_nameVersion < from->nameVersion()) {
			updateName(view, data, from);
			return true;
		}
	}
	return false;
}

void Reply::updateName(
		not_null<const Element*> view,
		not_null<HistoryMessageReply*> data,
		std::optional<PeerData*> resolvedSender) const {
	auto viaBotUsername = QString();
	const auto message = data->resolvedMessage.get();
	if (message && !message->Has<HistoryMessageForwarded>()) {
		if (const auto bot = message->viaBot()) {
			viaBotUsername = bot->username();
		}
	}
	const auto history = view->history();
	const auto &fields = data->fields();
	const auto sender = resolvedSender.value_or(this->sender(view, data));
	const auto externalPeer = fields.externalPeerId
		? history->owner().peer(fields.externalPeerId).get()
		: nullptr;
	const auto displayAsExternal = data->displayAsExternal(view->data());
	const auto groupNameAdded = displayAsExternal
		&& externalPeer
		&& (externalPeer != sender)
		&& (externalPeer->isChat() || externalPeer->isMegagroup());
	const auto shorten = !viaBotUsername.isEmpty() || groupNameAdded;
	const auto name = sender
		? senderName(sender, shorten)
		: senderName(view, data, shorten);
	const auto previewSkip = _hasPreview
		? (st::messageQuoteStyle.outline
			+ st::historyReplyPreviewMargin.left()
			+ st::historyReplyPreview
			+ st::historyReplyPreviewMargin.right()
			- st::historyReplyPadding.left())
		: 0;
	auto nameFull = TextWithEntities();
	if (displayAsExternal && !groupNameAdded && !fields.storyId) {
		nameFull.append(PeerEmoji(history, sender));
	}
	nameFull.append(name);
	if (groupNameAdded) {
		nameFull.append(' ').append(PeerEmoji(history, externalPeer));
		nameFull.append(externalPeer->name());
	}
	if (!viaBotUsername.isEmpty()) {
		nameFull.append(u" @"_q).append(viaBotUsername);
	}
	const auto context = Core::MarkedTextContext{
		.session = &history->session(),
		.customEmojiRepaint = [] {},
		.customEmojiLoopLimit = 1,
	};
	_name.setMarkedText(
		st::fwdTextStyle,
		nameFull,
		Ui::NameTextOptions(),
		context);
	if (sender) {
		_nameVersion = sender->nameVersion();
	}
	const auto nameMaxWidth = previewSkip
		+ _name.maxWidth()
		+ (_hasQuoteIcon
			? st::messageTextStyle.blockquote.icon.width()
			: 0);
	const auto storySkip = fields.storyId
		? (st::dialogsMiniReplyStory.skipText
			+ st::dialogsMiniReplyStory.icon.icon.width())
		: 0;
	const auto optimalTextSize = _multiline
		? countMultilineOptimalSize(previewSkip)
		: QSize(
			(previewSkip
				+ storySkip
				+ std::min(_text.maxWidth(), st::maxSignatureSize)),
			st::normalFont->height);
	_maxWidth = std::max(nameMaxWidth, optimalTextSize.width());
	if (!data->displaying()) {
		const auto unavailable = data->unavailable();
		_stateText = ((fields.messageId || fields.storyId) && !unavailable)
			? tr::lng_profile_loading(tr::now)
			: fields.storyId
			? tr::lng_deleted_story(tr::now)
			: tr::lng_deleted_message(tr::now);
		const auto phraseWidth = st::msgDateFont->width(_stateText);
		_maxWidth = unavailable
			? phraseWidth
			: std::max(_maxWidth, phraseWidth);
	} else {
		_stateText = QString();
	}
	_maxWidth = st::historyReplyPadding.left()
		+ _maxWidth
		+ st::historyReplyPadding.right();
	_minHeight = st::historyReplyPadding.top()
		+ st::msgServiceNameFont->height
		+ optimalTextSize.height()
		+ st::historyReplyPadding.bottom();
}

int Reply::resizeToWidth(int width) const {
	_ripple.animation = nullptr;

	const auto previewSkip = _hasPreview
		? (st::messageQuoteStyle.outline
			+ st::historyReplyPreviewMargin.left()
			+ st::historyReplyPreview
			+ st::historyReplyPreviewMargin.right()
			- st::historyReplyPadding.left())
		: 0;
	if (width >= _maxWidth || !_multiline) {
		_nameTwoLines = 0;
		_expandable = _minHeightExpandable;
		_height = _minHeight;
		return height();
	}
	const auto innerw = width
		- st::historyReplyPadding.left()
		- st::historyReplyPadding.right();
	const auto namew = innerw - previewSkip;
	const auto desiredNameHeight = _name.countHeight(namew);
	_nameTwoLines = (desiredNameHeight > st::semiboldFont->height) ? 1 : 0;
	const auto nameh = (_nameTwoLines ? 2 : 1) * st::semiboldFont->height;
	const auto firstLineSkip = _nameTwoLines ? 0 : previewSkip;
	auto elided = false;
	const auto texth = _text.countDimensions(
		textGeometry(innerw, firstLineSkip, &elided)).height;
	_expandable = elided ? 1 : 0;
	_height = st::historyReplyPadding.top()
		+ nameh
		+ std::max(texth, st::normalFont->height)
		+ st::historyReplyPadding.bottom();
	return height();
}

Ui::Text::GeometryDescriptor Reply::textGeometry(
		int available,
		int firstLineSkip,
		bool *outElided) const {
	return { .layout = [=](int line) {
		const auto skip = (line ? 0 : firstLineSkip);
		const auto elided = !_multiline
			|| (!_expanded && (line + 1 >= kNonExpandedLinesLimit));
		return Ui::Text::LineGeometry{
			.left = skip,
			.width = available - skip,
			.elided = elided,
		};
	}, .outElided = outElided };
}

int Reply::height() const {
	return _height + st::historyReplyTop + st::historyReplyBottom;
}

QMargins Reply::margins() const {
	return QMargins(0, st::historyReplyTop, 0, st::historyReplyBottom);
}

QSize Reply::countMultilineOptimalSize(
		int previewSkip) const {
	auto elided = false;
	const auto max = previewSkip + _text.maxWidth();
	const auto result = _text.countDimensions(
		textGeometry(max, previewSkip, &elided));
	_minHeightExpandable = elided ? 1 : 0;
	return {
		result.width,
		std::max(result.height, st::normalFont->height),
	};
}

void Reply::paint(
		Painter &p,
		not_null<const Element*> view,
		const Ui::ChatPaintContext &context,
		int x,
		int y,
		int w,
		bool inBubble) const {
	const auto st = context.st;
	const auto stm = context.messageStyle();

	y += st::historyReplyTop;
	const auto rect = QRect(x, y, w, _height);
	const auto selected = context.selected();
	const auto backgroundEmojiId = _colorPeer
		? _colorPeer->backgroundEmojiId()
		: DocumentId();
	const auto colorIndexPlusOne = _colorPeer
		? (_colorPeer->colorIndex() + 1)
		: _hiddenSenderColorIndexPlusOne;
	const auto useColorIndex = colorIndexPlusOne && !context.outbg;
	const auto colorPattern = colorIndexPlusOne
		? st->colorPatternIndex(colorIndexPlusOne - 1)
		: 0;
	const auto cache = !inBubble
		? (_hasQuoteIcon
			? st->serviceQuoteCache(colorPattern)
			: st->serviceReplyCache(colorPattern)).get()
		: useColorIndex
		? (_hasQuoteIcon
			? st->coloredQuoteCache(selected, colorIndexPlusOne - 1)
			: st->coloredReplyCache(selected, colorIndexPlusOne - 1)).get()
		: (_hasQuoteIcon
			? stm->quoteCache[colorPattern]
			: stm->replyCache[colorPattern]).get();
	const auto &quoteSt = _hasQuoteIcon
		? st::messageTextStyle.blockquote
		: st::messageQuoteStyle;
	const auto backgroundEmoji = backgroundEmojiId
		? st->backgroundEmojiData(backgroundEmojiId).get()
		: nullptr;
	const auto backgroundEmojiCache = backgroundEmoji
		? &backgroundEmoji->caches[Ui::BackgroundEmojiData::CacheIndex(
			selected,
			context.outbg,
			inBubble,
			colorIndexPlusOne)]
		: nullptr;
	const auto rippleColor = cache->bg;
	if (!inBubble) {
		cache->bg = QColor(0, 0, 0, 0);
	}
	Ui::Text::ValidateQuotePaintCache(*cache, quoteSt);
	Ui::Text::FillQuotePaint(p, rect, *cache, quoteSt);
	if (backgroundEmoji) {
		ValidateBackgroundEmoji(
			backgroundEmojiId,
			backgroundEmoji,
			backgroundEmojiCache,
			cache,
			view);
		if (!backgroundEmojiCache->frames[0].isNull()) {
			FillBackgroundEmoji(p, rect, _hasQuoteIcon, *backgroundEmojiCache);
		}
	}
	if (!inBubble) {
		cache->bg = rippleColor;
	}

	if (_ripple.animation) {
		_ripple.animation->paint(p, x, y, w, &rippleColor);
		if (_ripple.animation->empty()) {
			_ripple.animation.reset();
		}
	}

	auto hasPreview = (_hasPreview != 0);
	auto previewSkip = hasPreview
		? (st::messageQuoteStyle.outline
			+ st::historyReplyPreviewMargin.left()
			+ st::historyReplyPreview
			+ st::historyReplyPreviewMargin.right()
			- st::historyReplyPadding.left())
		: 0;
	if (hasPreview && w <= st::historyReplyPadding.left() + previewSkip) {
		hasPreview = false;
		previewSkip = 0;
	}

	const auto pausedSpoiler = context.paused
		|| On(PowerSaving::kChatSpoiler);
	auto textLeft = x + st::historyReplyPadding.left();
	auto textTop = y
		+ st::historyReplyPadding.top()
		+ (st::msgServiceNameFont->height * (_nameTwoLines ? 2 : 1));
	if (w > st::historyReplyPadding.left()) {
		if (_displaying) {
			if (hasPreview) {
				const auto data = view->data()->Get<HistoryMessageReply>();
				const auto message = data
					? data->resolvedMessage.get()
					: nullptr;
				const auto media = message ? message->media() : nullptr;
				const auto image = media
					? media->replyPreview()
					: !data
					? nullptr
					: data->resolvedStory
					? data->resolvedStory->replyPreview()
					: data->fields().externalMedia
					? data->fields().externalMedia->replyPreview()
					: nullptr;
				if (image) {
					auto to = style::rtlrect(
						x + st::historyReplyPreviewMargin.left(),
						y + st::historyReplyPreviewMargin.top(),
						st::historyReplyPreview,
						st::historyReplyPreview,
						w + 2 * x);
					const auto preview = image->pixSingle(
						image->size() / style::DevicePixelRatio(),
						{
							.colored = (context.selected()
								? &st->msgStickerOverlay()
								: nullptr),
							.options = Images::Option::RoundSmall,
							.outer = to.size(),
						});
					p.drawPixmap(to.x(), to.y(), preview);
					if (_spoiler) {
						view->clearCustomEmojiRepaint();
						Ui::FillSpoilerRect(
							p,
							to,
							Ui::DefaultImageSpoiler().frame(
								_spoiler->index(
									context.now,
									pausedSpoiler)));
					}
				}
			}
			const auto textw = w
				- st::historyReplyPadding.left()
				- st::historyReplyPadding.right();
			const auto namew = textw - previewSkip;
			auto firstLineSkip = _nameTwoLines ? 0 : previewSkip;
			if (namew > 0) {
				p.setPen(!inBubble
					? st->msgImgReplyBarColor()->c
					: useColorIndex
					? FromNameFg(context, colorIndexPlusOne - 1)
					: stm->msgServiceFg->c);
				_name.drawLeftElided(
					p,
					x + st::historyReplyPadding.left() + previewSkip,
					y + st::historyReplyPadding.top(),
					namew,
					w + 2 * x,
					_nameTwoLines ? 2 : 1);

				p.setPen(inBubble
					? stm->historyTextFg
					: st->msgImgReplyBarColor());
				view->prepareCustomEmojiPaint(p, context, _text);
				auto replyToTextPalette = &(!inBubble
					? st->imgReplyTextPalette()
					: useColorIndex
					? st->coloredTextPalette(selected, colorIndexPlusOne - 1)
					: stm->replyTextPalette);
				if (_replyToStory) {
					st::dialogsMiniReplyStory.icon.icon.paint(
						p,
						textLeft + firstLineSkip,
						textTop,
						w + 2 * x,
						replyToTextPalette->linkFg->c);
					firstLineSkip += st::dialogsMiniReplyStory.skipText
						+ st::dialogsMiniReplyStory.icon.icon.width();
				}
				auto owned = std::optional<style::owned_color>();
				auto copy = std::optional<style::TextPalette>();
				if (inBubble && colorIndexPlusOne) {
					copy.emplace(*replyToTextPalette);
					owned.emplace(cache->icon);
					copy->linkFg = owned->color();
					replyToTextPalette = &*copy;
				}
				_text.draw(p, {
					.position = { textLeft, textTop },
					.geometry = textGeometry(textw, firstLineSkip),
					.palette = replyToTextPalette,
					.spoiler = Ui::Text::DefaultSpoilerCache(),
					.now = context.now,
					.pausedEmoji = (context.paused
						|| On(PowerSaving::kEmojiChat)),
					.pausedSpoiler = pausedSpoiler,
					.elisionLines = 1,
				});
				p.setTextPalette(stm->textPalette);
			}
		} else {
			p.setFont(st::msgDateFont);
			p.setPen(cache->icon);
			p.drawTextLeft(
				textLeft,
				(y
					+ st::historyReplyPadding.top()
					+ (st::msgDateFont->height / 2)),
				w + 2 * x,
				st::msgDateFont->elided(
					_stateText,
					x + w - textLeft - st::historyReplyPadding.right()));
		}
	}
}

void Reply::createRippleAnimation(
		not_null<const Element*> view,
		QSize size) {
	_ripple.animation = std::make_unique<Ui::RippleAnimation>(
		st::defaultRippleAnimation,
		Ui::RippleAnimation::RoundRectMask(
			size,
			st::messageQuoteStyle.radius),
		[=] { view->history()->owner().requestViewRepaint(view); });
}

void Reply::saveRipplePoint(QPoint point) const {
	_ripple.lastPoint = point;
}

void Reply::addRipple() {
	if (_ripple.animation) {
		_ripple.animation->add(_ripple.lastPoint);
	}
}

void Reply::stopLastRipple() {
	if (_ripple.animation) {
		_ripple.animation->lastStop();
	}
}

TextWithEntities Reply::PeerEmoji(
		not_null<History*> history,
		PeerData *peer) {
	using namespace std;
	const auto icon = !peer
		? pair(&st::historyReplyUser, st::historyReplyUserPadding)
		: peer->isBroadcast()
		? pair(&st::historyReplyChannel, st::historyReplyChannelPadding)
		: (peer->isChannel() || peer->isChat())
		? pair(&st::historyReplyGroup, st::historyReplyGroupPadding)
		: pair(&st::historyReplyUser, st::historyReplyUserPadding);
	const auto owner = &history->owner();
	return Ui::Text::SingleCustomEmoji(
		owner->customEmojiManager().registerInternalEmoji(
			*icon.first,
			icon.second));
}

TextWithEntities Reply::ComposePreviewName(
		not_null<History*> history,
		not_null<HistoryItem*> to,
		bool quote) {
	const auto sender = [&] {
		if (const auto from = to->displayFrom()) {
			return not_null(from);
		}
		return to->author();
	}();
	const auto toPeer = to->history()->peer;
	const auto displayAsExternal = (to->history() != history);
	const auto groupNameAdded = displayAsExternal
		&& (toPeer != sender)
		&& (toPeer->isChat() || toPeer->isMegagroup());
	const auto shorten = groupNameAdded || quote;

	auto nameFull = TextWithEntities();
	using namespace HistoryView;
	if (displayAsExternal && !groupNameAdded) {
		nameFull.append(Reply::PeerEmoji(history, sender));
	}
	nameFull.append(shorten ? sender->shortName() : sender->name());
	if (groupNameAdded) {
		nameFull.append(' ').append(Reply::PeerEmoji(history, toPeer));
		nameFull.append(toPeer->name());
	}
	return (quote
		? tr::lng_preview_reply_to_quote
		: tr::lng_preview_reply_to)(
			tr::now,
			lt_name,
			nameFull,
			Ui::Text::WithEntities);

}

void Reply::unloadPersistentAnimation() {
	_text.unloadPersistentAnimation();
}

} // namespace HistoryView
