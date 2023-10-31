/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_components.h"

#include "api/api_text_entities.h"
#include "base/qt/qt_key_modifiers.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/image/image.h"
#include "ui/toast/toast.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_message.h" // FromNameFg.
#include "history/view/history_view_service_message.h"
#include "history/view/media/history_view_document.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "layout/layout_position.h"
#include "mainwindow.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_web_page.h"
#include "data/data_file_click_handler.h"
#include "data/data_scheduled_messages.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "api/api_bot.h"
#include "styles/style_widgets.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h" // dialogsMiniReplyStory.

#include <QtGui/QGuiApplication>

namespace {

const auto kPsaForwardedPrefix = "cloud_lng_forwarded_psa_";

void ValidateBackgroundEmoji(
		DocumentId backgroundEmojiId,
		not_null<Ui::BackgroundEmojiData*> data,
		not_null<Ui::BackgroundEmojiCache*> cache,
		not_null<Ui::Text::QuotePaintCache*> quote,
		not_null<const HistoryView::Element*> holder) {
	if (data->firstFrameMask.isNull()) {
		if (!cache->frames[0].isNull()) {
			for (auto &frame : cache->frames) {
				frame = QImage();
			}
		}
		const auto tag = Data::CustomEmojiSizeTag::Isolated;
		if (!data->emoji) {
			const auto owner = &holder->history()->owner();
			const auto repaint = crl::guard(holder, [=] {
				holder->history()->owner().requestViewRepaint(holder);
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

void HistoryMessageVia::create(
		not_null<Data::Session*> owner,
		UserId userId) {
	bot = owner->user(userId);
	maxWidth = st::msgServiceNameFont->width(
		tr::lng_inline_bot_via(
			tr::now,
			lt_inline_bot,
			'@' + bot->username()));
	link = std::make_shared<LambdaClickHandler>([bot = this->bot](
			ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (base::IsCtrlPressed()) {
				controller->showPeerInfo(bot);
				return;
			} else if (!bot->isBot()
				|| bot->botInfo->inlinePlaceholder.isEmpty()) {
				controller->showPeerHistory(
					bot->id,
					Window::SectionShow::Way::Forward);
				return;
			}
		}
		const auto delegate = my.elementDelegate
			? my.elementDelegate()
			: nullptr;
		if (delegate) {
			delegate->elementHandleViaClick(bot);
		}
	});
}

void HistoryMessageVia::resize(int32 availw) const {
	if (availw < 0) {
		text = QString();
		width = 0;
	} else {
		text = tr::lng_inline_bot_via(
			tr::now,
			lt_inline_bot,
			'@' + bot->username());
		if (availw < maxWidth) {
			text = st::msgServiceNameFont->elided(text, availw);
			width = st::msgServiceNameFont->width(text);
		} else if (width < maxWidth) {
			width = maxWidth;
		}
	}
}

HiddenSenderInfo::HiddenSenderInfo(
	const QString &name,
	bool external,
	std::optional<uint8> colorIndex)
: name(name)
, colorIndex(colorIndex.value_or(
	Data::DecideColorIndex(Data::FakePeerIdForJustName(name))))
, emptyUserpic(
	Ui::EmptyUserpic::UserpicColor(this->colorIndex),
	(external
		? Ui::EmptyUserpic::ExternalName()
		: name)) {
	Expects(!name.isEmpty());

	const auto parts = name.trimmed().split(' ', Qt::SkipEmptyParts);
	firstName = parts[0];
	for (const auto &part : parts.mid(1)) {
		if (!lastName.isEmpty()) {
			lastName.append(' ');
		}
		lastName.append(part);
	}
}

const Ui::Text::String &HiddenSenderInfo::nameText() const {
	if (_nameText.isEmpty()) {
		_nameText.setText(st::msgNameStyle, name, Ui::NameTextOptions());
	}
	return _nameText;
}

ClickHandlerPtr HiddenSenderInfo::ForwardClickHandler() {
	static const auto hidden = std::make_shared<LambdaClickHandler>([](
			ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto weak = my.sessionWindow;
		if (const auto strong = weak.get()) {
			strong->showToast(tr::lng_forwarded_hidden(tr::now));
		}
	});
	return hidden;
}

bool HiddenSenderInfo::paintCustomUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		int x,
		int y,
		int outerWidth,
		int size) const {
	Expects(!customUserpic.empty());

	auto valid = true;
	if (!customUserpic.isCurrentView(view.cloud)) {
		view.cloud = customUserpic.createView();
		valid = false;
	}
	const auto image = *view.cloud;
	if (image.isNull()) {
		emptyUserpic.paintCircle(p, x, y, outerWidth, size);
		return valid;
	}
	Ui::ValidateUserpicCache(
		view,
		image.isNull() ? nullptr : &image,
		image.isNull() ? &emptyUserpic : nullptr,
		size * style::DevicePixelRatio(),
		false);
	p.drawImage(QRect(x, y, size, size), view.cached);
	return valid;
}

void HistoryMessageForwarded::create(const HistoryMessageVia *via) const {
	auto phrase = TextWithEntities();
	const auto fromChannel = originalSender
		&& originalSender->isChannel()
		&& !originalSender->isMegagroup();
	const auto name = TextWithEntities{
		.text = (originalSender
			? originalSender->name()
			: hiddenSenderInfo->name)
	};
	if (!originalPostAuthor.isEmpty()) {
		phrase = tr::lng_forwarded_signed(
			tr::now,
			lt_channel,
			name,
			lt_user,
			{ .text = originalPostAuthor },
			Ui::Text::WithEntities);
	} else {
		phrase = name;
	}
	if (story) {
		phrase = tr::lng_forwarded_story(
			tr::now,
			lt_user,
			Ui::Text::Link(phrase.text, QString()), // Link 1.
			Ui::Text::WithEntities);
	} else if (via && psaType.isEmpty()) {
		if (fromChannel) {
			phrase = tr::lng_forwarded_channel_via(
				tr::now,
				lt_channel,
				Ui::Text::Link(phrase.text, 1), // Link 1.
				lt_inline_bot,
				Ui::Text::Link('@' + via->bot->username(), 2),  // Link 2.
				Ui::Text::WithEntities);
		} else {
			phrase = tr::lng_forwarded_via(
				tr::now,
				lt_user,
				Ui::Text::Link(phrase.text, 1), // Link 1.
				lt_inline_bot,
				Ui::Text::Link('@' + via->bot->username(), 2),  // Link 2.
				Ui::Text::WithEntities);
		}
	} else {
		if (fromChannel || !psaType.isEmpty()) {
			auto custom = psaType.isEmpty()
				? QString()
				: Lang::GetNonDefaultValue(
					kPsaForwardedPrefix + psaType.toUtf8());
			if (!custom.isEmpty()) {
				custom = custom.replace("{channel}", phrase.text);
				const auto index = int(custom.indexOf(phrase.text));
				const auto size = int(phrase.text.size());
				phrase = TextWithEntities{
					.text = custom,
					.entities = {{ EntityType::CustomUrl, index, size, {} }},
				};
			} else {
				phrase = (psaType.isEmpty()
					? tr::lng_forwarded_channel
					: tr::lng_forwarded_psa_default)(
						tr::now,
						lt_channel,
						Ui::Text::Link(phrase.text, QString()), // Link 1.
						Ui::Text::WithEntities);
			}
		} else {
			phrase = tr::lng_forwarded(
				tr::now,
				lt_user,
				Ui::Text::Link(phrase.text, QString()), // Link 1.
				Ui::Text::WithEntities);
		}
	}
	text.setMarkedText(st::fwdTextStyle, phrase);

	text.setLink(1, fromChannel
		? JumpToMessageClickHandler(originalSender, originalId)
		: originalSender
		? originalSender->openLink()
		: HiddenSenderInfo::ForwardClickHandler());
	if (via) {
		text.setLink(2, via->link);
	}
}

ReplyFields ReplyFieldsFromMTP(
		not_null<History*> history,
		const MTPMessageReplyHeader &reply) {
	return reply.match([&](const MTPDmessageReplyHeader &data) {
		auto result = ReplyFields();
		if (const auto peer = data.vreply_to_peer_id()) {
			result.externalPeerId = peerFromMTP(*peer);
			if (result.externalPeerId == history->peer->id) {
				result.externalPeerId = 0;
			}
		}
		const auto owner = &history->owner();
		if (const auto id = data.vreply_to_msg_id().value_or_empty()) {
			result.messageId = data.is_reply_to_scheduled()
				? owner->scheduledMessages().localMessageId(id)
				: id;
			result.topMessageId
				= data.vreply_to_top_id().value_or(id);
			result.topicPost = data.is_forum_topic();
		}
		if (const auto header = data.vreply_from()) {
			const auto &data = header->data();
			result.externalPostAuthor
				= qs(data.vpost_author().value_or_empty());
			result.externalSenderId = data.vfrom_id()
				? peerFromMTP(*data.vfrom_id())
				: PeerId();
			result.externalSenderName
				= qs(data.vfrom_name().value_or_empty());
		}
		result.quote = TextWithEntities{
			qs(data.vquote_text().value_or_empty()),
			Api::EntitiesFromMTP(
				&owner->session(),
				data.vquote_entities().value_or_empty()),
		};
		result.manualQuote = data.is_quote();
		return result;
	}, [&](const MTPDmessageReplyStoryHeader &data) {
		return ReplyFields{
			.externalPeerId = peerFromUser(data.vuser_id()),
			.storyId = data.vstory_id().v,
		};
	});
}

FullReplyTo ReplyToFromMTP(
		not_null<History*> history,
		const MTPInputReplyTo &reply) {
	return reply.match([&](const MTPDinputReplyToMessage &data) {
		auto result = FullReplyTo{
			.messageId = { history->peer->id, data.vreply_to_msg_id().v },
		};
		if (const auto peer = data.vreply_to_peer_id()) {
			const auto parsed = Data::PeerFromInputMTP(
				&history->owner(),
				*peer);
			if (!parsed) {
				return FullReplyTo();
			}
			result.messageId.peer = parsed->id;
		}
		result.topicRootId = data.vtop_msg_id().value_or_empty();
		result.quote = TextWithEntities{
			qs(data.vquote_text().value_or_empty()),
			Api::EntitiesFromMTP(
				&history->session(),
				data.vquote_entities().value_or_empty()),
		};
		return result;
	}, [&](const MTPDinputReplyToStory &data) {
		if (const auto parsed = Data::UserFromInputMTP(
				&history->owner(),
				data.vuser_id())) {
			return FullReplyTo{
				.storyId = { parsed->id, data.vstory_id().v },
			};
		}
		return FullReplyTo();
	});
}

HistoryMessageReply::HistoryMessageReply() = default;

HistoryMessageReply &HistoryMessageReply::operator=(
	HistoryMessageReply &&other) = default;

HistoryMessageReply::~HistoryMessageReply() {
	// clearData() should be called by holder.
	Expects(resolvedMessage.empty());
	Expects(originalVia == nullptr);
}

bool HistoryMessageReply::updateData(
		not_null<HistoryItem*> holder,
		bool force) {
	const auto guard = gsl::finally([&] { refreshReplyToMedia(); });
	if (!force) {
		if (resolvedMessage || resolvedStory || _unavailable) {
			return true;
		}
	}
	const auto peerId = _fields.externalPeerId
		? _fields.externalPeerId
		: holder->history()->peer->id;
	if (!resolvedMessage && _fields.messageId) {
		resolvedMessage = holder->history()->owner().message(
			peerId,
			_fields.messageId);
		if (resolvedMessage) {
			if (resolvedMessage->isEmpty()) {
				// Really it is deleted.
				resolvedMessage = nullptr;
				force = true;
			} else {
				holder->history()->owner().registerDependentMessage(
					holder,
					resolvedMessage.get());
			}
		}
	}
	if (!resolvedStory && _fields.storyId) {
		const auto maybe = holder->history()->owner().stories().lookup({
			peerId,
			_fields.storyId,
		});
		if (maybe) {
			resolvedStory = *maybe;
			holder->history()->owner().stories().registerDependentMessage(
				holder,
				resolvedStory.get());
		} else if (maybe.error() == Data::NoStory::Deleted) {
			force = true;
		}
	}

	const auto external = _fields.externalSenderId
		|| !_fields.externalSenderName.isEmpty();
	if (resolvedMessage
		|| resolvedStory
		|| (external && (!_fields.messageId || force))) {
		const auto repaint = [=] { holder->customEmojiRepaint(); };
		const auto context = Core::MarkedTextContext{
			.session = &holder->history()->session(),
			.customEmojiRepaint = repaint,
		};
		const auto text = !_fields.quote.empty()
			? _fields.quote
			: resolvedMessage
			? resolvedMessage->inReplyText()
			: resolvedStory
			? resolvedStory->inReplyText()
			: TextWithEntities{ u"..."_q };
		_text.setMarkedText(
			st::defaultTextStyle,
			text,
			Ui::DialogTextOptions(),
			context);

		updateName(holder);
		setLinkFrom(holder);
		if (resolvedMessage
			&& !resolvedMessage->Has<HistoryMessageForwarded>()) {
			if (const auto bot = resolvedMessage->viaBot()) {
				originalVia = std::make_unique<HistoryMessageVia>();
				originalVia->create(
					&holder->history()->owner(),
					peerToUser(bot->id));
			}
		}

		if (!resolvedMessage && !resolvedStory) {
			_unavailable = 1;
		}

		const auto media = resolvedMessage
			? resolvedMessage->media()
			: nullptr;
		if (!media || !media->hasReplyPreview() || !media->hasSpoiler()) {
			spoiler = nullptr;
		} else if (!spoiler) {
			spoiler = std::make_unique<Ui::SpoilerAnimation>(repaint);
		}
	} else if (force) {
		if (_fields.messageId || _fields.storyId) {
			_unavailable = 1;
		}
		spoiler = nullptr;
	}
	if (force) {
		holder->history()->owner().requestItemResize(holder);
	}
	return resolvedMessage
		|| resolvedStory
		|| (external && !_fields.messageId)
		|| _unavailable;
}

void HistoryMessageReply::set(ReplyFields fields) {
	_fields = std::move(fields);
}

void HistoryMessageReply::updateFields(
		not_null<HistoryItem*> holder,
		MsgId messageId,
		MsgId topMessageId,
		bool topicPost) {
	_fields.topicPost = topicPost;
	if ((_fields.messageId != messageId)
		&& !IsServerMsgId(_fields.messageId)) {
		_fields.messageId = messageId;
		if (!updateData(holder)) {
			RequestDependentMessageItem(
				holder,
				_fields.externalPeerId,
				_fields.messageId);
		}
	}
	if ((_fields.topMessageId != topMessageId)
		&& !IsServerMsgId(_fields.topMessageId)) {
		_fields.topMessageId = topMessageId;
	}
}

void HistoryMessageReply::setLinkFrom(
		not_null<HistoryItem*> holder) {
	const auto externalPeerId = _fields.externalSenderId;
	const auto external = externalPeerId
		|| !_fields.externalSenderName.isEmpty();
	const auto externalLink = [=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (externalPeerId) {
				controller->showPeerInfo(
					controller->session().data().peer(externalPeerId));
			} else {
				controller->showToast(u"External reply"_q);
			}
		}
	};
	_link = resolvedMessage
		? JumpToMessageClickHandler(
			resolvedMessage.get(),
			holder->fullId(),
			_fields.manualQuote ? _fields.quote : TextWithEntities())
		: resolvedStory
		? JumpToStoryClickHandler(resolvedStory.get())
		: (external && !_fields.messageId)
		? std::make_shared<LambdaClickHandler>(externalLink)
		: nullptr;
}

void HistoryMessageReply::setTopMessageId(MsgId topMessageId) {
	_fields.topMessageId = topMessageId;
}

void HistoryMessageReply::clearData(not_null<HistoryItem*> holder) {
	originalVia = nullptr;
	if (resolvedMessage) {
		holder->history()->owner().unregisterDependentMessage(
			holder,
			resolvedMessage.get());
		resolvedMessage = nullptr;
	}
	if (resolvedStory) {
		holder->history()->owner().stories().unregisterDependentMessage(
			holder,
			resolvedStory.get());
		resolvedStory = nullptr;
	}
	_name.clear();
	_text.clear();
	_unavailable = 1;
	refreshReplyToMedia();
}

PeerData *HistoryMessageReply::sender(not_null<HistoryItem*> holder) const {
	if (resolvedStory) {
		return resolvedStory->peer();
	} else if (!resolvedMessage) {
		if (!_externalSender && _fields.externalSenderId) {
			_externalSender = holder->history()->owner().peer(
				_fields.externalSenderId);
		}
		return _externalSender;
	} else if (holder->Has<HistoryMessageForwarded>()) {
		// Forward of a reply. Show reply-to original sender.
		const auto forwarded
			= resolvedMessage->Get<HistoryMessageForwarded>();
		if (forwarded) {
			return forwarded->originalSender;
		}
	}
	if (const auto from = resolvedMessage->displayFrom()) {
		return from;
	}
	return resolvedMessage->author().get();
}

QString HistoryMessageReply::senderName(
		not_null<HistoryItem*> holder) const {
	if (const auto peer = sender(holder)) {
		return senderName(peer);
	} else if (!resolvedMessage) {
		return _fields.externalSenderName;
	} else if (holder->Has<HistoryMessageForwarded>()) {
		// Forward of a reply. Show reply-to original sender.
		const auto forwarded
			= resolvedMessage->Get<HistoryMessageForwarded>();
		if (forwarded) {
			Assert(forwarded->hiddenSenderInfo != nullptr);
			return forwarded->hiddenSenderInfo->name;
		}
	}
	return QString();
}

QString HistoryMessageReply::senderName(not_null<PeerData*> peer) const {
	if (const auto user = originalVia ? peer->asUser() : nullptr) {
		return user->firstName;
	}
	return peer->name();
}

bool HistoryMessageReply::isNameUpdated(
		not_null<HistoryItem*> holder) const {
	if (const auto from = sender(holder)) {
		if (_nameVersion < from->nameVersion()) {
			updateName(holder, from);
			return true;
		}
	}
	return false;
}

void HistoryMessageReply::updateName(
		not_null<HistoryItem*> holder,
		std::optional<PeerData*> resolvedSender) const {
	const auto peer = resolvedSender.value_or(sender(holder));
	const auto name = peer ? senderName(peer) : senderName(holder);
	const auto hasPreview = (resolvedStory
		&& resolvedStory->hasReplyPreview())
		|| (resolvedMessage
			&& resolvedMessage->media()
			&& resolvedMessage->media()->hasReplyPreview());
	const auto textLeft = hasPreview
		? (st::messageQuoteStyle.outline
			+ st::historyReplyPreviewMargin.left()
			+ st::historyReplyPreview
			+ st::historyReplyPreviewMargin.right())
		: st::historyReplyPadding.left();
	if (!name.isEmpty()) {
		_name.setText(st::fwdTextStyle, name, Ui::NameTextOptions());
		if (peer) {
			_nameVersion = peer->nameVersion();
		}
		const auto w = _name.maxWidth()
			+ (originalVia
				? (st::msgServiceFont->spacew + originalVia->maxWidth)
				: 0)
			+ (_fields.quote.empty()
				? 0
				: st::messageTextStyle.blockquote.icon.width());
		_maxWidth = std::max(
			w,
			std::min(_text.maxWidth(), st::maxSignatureSize))
			+ (_fields.storyId
				? (st::dialogsMiniReplyStory.skipText
					+ st::dialogsMiniReplyStory.icon.icon.width())
				: 0);
	} else {
		_maxWidth = st::msgDateFont->width(statePhrase());
	}
	_maxWidth = textLeft
		+ _maxWidth
		+ st::historyReplyPadding.right();
	_minHeight = st::historyReplyPadding.top()
		+ st::msgServiceNameFont->height
		+ st::normalFont->height
		+ st::historyReplyPadding.bottom();
}

int HistoryMessageReply::resizeToWidth(int width) const {
	const auto hasPreview = (resolvedStory
		&& resolvedStory->hasReplyPreview())
		|| (resolvedMessage
			&& resolvedMessage->media()
			&& resolvedMessage->media()->hasReplyPreview());
	const auto textLeft = hasPreview
		? (st::messageQuoteStyle.outline
			+ st::historyReplyPreviewMargin.left()
			+ st::historyReplyPreview
			+ st::historyReplyPreviewMargin.right())
		: st::historyReplyPadding.left();
	if (originalVia) {
		originalVia->resize(width
			- textLeft
			- st::historyReplyPadding.right()
			- _name.maxWidth()
			- st::msgServiceFont->spacew);
	}
	if (width >= _maxWidth) {
		_height = _minHeight;
		return height();
	}
	_height = _minHeight;
	return height();
}

int HistoryMessageReply::height() const {
	return  _height + st::historyReplyTop + st::historyReplyBottom;
}

QMargins HistoryMessageReply::margins() const {
	return QMargins(0, st::historyReplyTop, 0, st::historyReplyBottom);
}

void HistoryMessageReply::itemRemoved(
		not_null<HistoryItem*> holder,
		not_null<HistoryItem*> removed) {
	if (resolvedMessage.get() == removed) {
		clearData(holder);
		holder->history()->owner().requestItemResize(holder);
	}
}

void HistoryMessageReply::storyRemoved(
		not_null<HistoryItem*> holder,
		not_null<Data::Story*> removed) {
	if (resolvedStory.get() == removed) {
		clearData(holder);
		holder->history()->owner().requestItemResize(holder);
	}
}

void HistoryMessageReply::paint(
		Painter &p,
		not_null<const HistoryView::Element*> holder,
		const Ui::ChatPaintContext &context,
		int x,
		int y,
		int w,
		bool inBubble) const {
	const auto st = context.st;
	const auto stm = context.messageStyle();

	y += st::historyReplyTop;
	const auto rect = QRect(x, y, w, _height);
	const auto hasQuote = _fields.manualQuote && !_fields.quote.empty();
	const auto selected = context.selected();
	const auto colorPeer = resolvedMessage
		? resolvedMessage->displayFrom()
		: resolvedStory
		? resolvedStory->peer().get()
		: _externalSender
		? _externalSender
		: nullptr;
	const auto backgroundEmojiId = colorPeer
		? colorPeer->backgroundEmojiId()
		: DocumentId();
	const auto colorIndexPlusOne = colorPeer
		? (colorPeer->colorIndex() + 1)
		: resolvedMessage
		? (resolvedMessage->hiddenSenderInfo()->colorIndex + 1)
		: 0;
	const auto useColorIndex = colorIndexPlusOne && !context.outbg;
	const auto colorPattern = colorIndexPlusOne
		? st->colorPatternIndex(colorIndexPlusOne - 1)
		: 0;
	const auto cache = !inBubble
		? (hasQuote
			? st->serviceQuoteCache(colorPattern)
			: st->serviceReplyCache(colorPattern)).get()
		: useColorIndex
		? (hasQuote
			? st->coloredQuoteCache(selected, colorIndexPlusOne - 1)
			: st->coloredReplyCache(selected, colorIndexPlusOne - 1)).get()
		: (hasQuote
			? stm->quoteCache[colorPattern]
			: stm->replyCache[colorPattern]).get();
	const auto &quoteSt = hasQuote
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
			holder);
		if (!backgroundEmojiCache->frames[0].isNull()) {
			FillBackgroundEmoji(p, rect, hasQuote, *backgroundEmojiCache);
		}
	}
	if (!inBubble) {
		cache->bg = rippleColor;
	}

	if (ripple.animation) {
		ripple.animation->paint(p, x, y, w, &rippleColor);
		if (ripple.animation->empty()) {
			ripple.animation.reset();
		}
	}

	const auto withPreviewLeft = st::messageQuoteStyle.outline
		+ st::historyReplyPreviewMargin.left()
		+ st::historyReplyPreview
		+ st::historyReplyPreviewMargin.right();
	auto textLeft = st::historyReplyPadding.left();
	const auto pausedSpoiler = context.paused
		|| On(PowerSaving::kChatSpoiler);
	if (w > textLeft) {
		if (resolvedMessage || resolvedStory || !_text.isEmpty()) {
			const auto media = resolvedMessage ? resolvedMessage->media() : nullptr;
			auto hasPreview = (media && media->hasReplyPreview())
				|| (resolvedStory && resolvedStory->hasReplyPreview());
			if (hasPreview && w <= withPreviewLeft) {
				hasPreview = false;
			}
			if (hasPreview) {
				textLeft = withPreviewLeft;
				const auto image = media
					? media->replyPreview()
					: resolvedStory->replyPreview();
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
					if (spoiler) {
						holder->clearCustomEmojiRepaint();
						Ui::FillSpoilerRect(
							p,
							to,
							Ui::DefaultImageSpoiler().frame(
								spoiler->index(
									context.now,
									pausedSpoiler)));
					}
				}
			}
			if (w > textLeft + st::historyReplyPadding.right()) {
				w -= textLeft + st::historyReplyPadding.right();
				p.setPen(!inBubble
					? st->msgImgReplyBarColor()->c
					: useColorIndex
					? FromNameFg(context, colorIndexPlusOne - 1)
					: stm->msgServiceFg->c);
				_name.drawLeftElided(p, x + textLeft, y + st::historyReplyPadding.top(), w, w + 2 * x + 2 * textLeft);
				if (originalVia && w > _name.maxWidth() + st::msgServiceFont->spacew) {
					p.setFont(st::msgServiceFont);
					p.drawText(x + textLeft + _name.maxWidth() + st::msgServiceFont->spacew, y + st::historyReplyPadding.top() + st::msgServiceFont->ascent, originalVia->text);
				}

				p.setPen(inBubble
					? stm->historyTextFg
					: st->msgImgReplyBarColor());
				holder->prepareCustomEmojiPaint(p, context, _text);
				auto replyToTextPosition = QPoint(
					x + textLeft,
					y + st::historyReplyPadding.top() + st::msgServiceNameFont->height);
				auto replyToTextPalette = &(!inBubble
					? st->imgReplyTextPalette()
					: useColorIndex
					? st->coloredTextPalette(selected, colorIndexPlusOne - 1)
					: stm->replyTextPalette);
				if (_fields.storyId) {
					st::dialogsMiniReplyStory.icon.icon.paint(
						p,
						replyToTextPosition,
						w + 2 * x + 2 * textLeft,
						replyToTextPalette->linkFg->c);
					replyToTextPosition += QPoint(
						st::dialogsMiniReplyStory.skipText
							+ st::dialogsMiniReplyStory.icon.icon.width(),
						0);
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
					.position = replyToTextPosition,
					.availableWidth = w,
					.palette = replyToTextPalette,
					.spoiler = Ui::Text::DefaultSpoilerCache(),
					.now = context.now,
					.pausedEmoji = (context.paused
						|| On(PowerSaving::kEmojiChat)),
					.pausedSpoiler = pausedSpoiler,
					.elisionOneLine = true,
				});
				p.setTextPalette(stm->textPalette);
			}
		} else {
			p.setFont(st::msgDateFont);
			p.setPen(cache->icon);
			p.drawTextLeft(
				x + textLeft,
				(y + (_height - st::msgDateFont->height) / 2),
				w + 2 * x + 2 * textLeft,
				st::msgDateFont->elided(
					statePhrase(),
					w - textLeft - st::historyReplyPadding.right()));
		}
	}
}

void HistoryMessageReply::unloadPersistentAnimation() {
	_text.unloadPersistentAnimation();
}

QString HistoryMessageReply::statePhrase() const {
	return ((_fields.messageId || _fields.storyId) && !_unavailable)
		? tr::lng_profile_loading(tr::now)
		: _fields.storyId
		? tr::lng_deleted_story(tr::now)
		: tr::lng_deleted_message(tr::now);
}

void HistoryMessageReply::refreshReplyToMedia() {
	replyToDocumentId = 0;
	replyToWebPageId = 0;
	if (const auto media = resolvedMessage ? resolvedMessage->media() : nullptr) {
		if (const auto document = media->document()) {
			replyToDocumentId = document->id;
		} else if (const auto webpage = media->webpage()) {
			replyToWebPageId = webpage->id;
		}
	}
}

ReplyMarkupClickHandler::ReplyMarkupClickHandler(
	not_null<Data::Session*> owner,
	int row,
	int column,
	FullMsgId context)
: _owner(owner)
, _itemId(context)
, _row(row)
, _column(column) {
}

// Copy to clipboard support.
QString ReplyMarkupClickHandler::copyToClipboardText() const {
	const auto button = getUrlButton();
	return button ? QString::fromUtf8(button->data) : QString();
}

QString ReplyMarkupClickHandler::copyToClipboardContextItemText() const {
	const auto button = getUrlButton();
	return button ? tr::lng_context_copy_link(tr::now) : QString();
}

// Finds the corresponding button in the items markup struct.
// If the button is not found it returns nullptr.
// Note: it is possible that we will point to the different button
// than the one was used when constructing the handler, but not a big deal.
const HistoryMessageMarkupButton *ReplyMarkupClickHandler::getButton() const {
	return HistoryMessageMarkupButton::Get(_owner, _itemId, _row, _column);
}

auto ReplyMarkupClickHandler::getUrlButton() const
-> const HistoryMessageMarkupButton* {
	if (const auto button = getButton()) {
		using Type = HistoryMessageMarkupButton::Type;
		if (button->type == Type::Url || button->type == Type::Auth) {
			return button;
		}
	}
	return nullptr;
}

void ReplyMarkupClickHandler::onClick(ClickContext context) const {
	if (context.button != Qt::LeftButton) {
		return;
	}
	auto my = context.other.value<ClickHandlerContext>();
	my.itemId = _itemId;
	Api::ActivateBotCommand(my, _row, _column);
}

// Returns the full text of the corresponding button.
QString ReplyMarkupClickHandler::buttonText() const {
	if (const auto button = getButton()) {
		return button->text;
	}
	return QString();
}

QString ReplyMarkupClickHandler::tooltip() const {
	const auto button = getUrlButton();
	const auto url = button ? QString::fromUtf8(button->data) : QString();
	const auto text = _fullDisplayed ? QString() : buttonText();
	if (!url.isEmpty() && !text.isEmpty()) {
		return QString("%1\n\n%2").arg(text, url);
	} else if (url.isEmpty() != text.isEmpty()) {
		return text + url;
	} else {
		return QString();
	}
}

ReplyKeyboard::Button::Button() = default;
ReplyKeyboard::Button::Button(Button &&other) = default;
ReplyKeyboard::Button &ReplyKeyboard::Button::operator=(
	Button &&other) = default;
ReplyKeyboard::Button::~Button() = default;

ReplyKeyboard::ReplyKeyboard(
	not_null<const HistoryItem*> item,
	std::unique_ptr<Style> &&s)
: _item(item)
, _selectedAnimation([=](crl::time now) {
	return selectedAnimationCallback(now);
})
, _st(std::move(s)) {
	if (const auto markup = _item->Get<HistoryMessageReplyMarkup>()) {
		const auto owner = &_item->history()->owner();
		const auto context = _item->fullId();
		const auto rowCount = int(markup->data.rows.size());
		_rows.reserve(rowCount);
		for (auto i = 0; i != rowCount; ++i) {
			const auto &row = markup->data.rows[i];
			const auto rowSize = int(row.size());
			auto newRow = std::vector<Button>();
			newRow.reserve(rowSize);
			for (auto j = 0; j != rowSize; ++j) {
				auto button = Button();
				const auto text = row[j].text;
				button.type = row.at(j).type;
				button.link = std::make_shared<ReplyMarkupClickHandler>(
					owner,
					i,
					j,
					context);
				button.text.setText(
					_st->textStyle(),
					TextUtilities::SingleLine(text),
					kPlainTextOptions);
				button.characters = text.isEmpty() ? 1 : text.size();
				newRow.push_back(std::move(button));
			}
			_rows.push_back(std::move(newRow));
		}
	}
}

void ReplyKeyboard::updateMessageId() {
	const auto msgId = _item->fullId();
	for (const auto &row : _rows) {
		for (const auto &button : row) {
			button.link->setMessageId(msgId);
		}
	}

}

void ReplyKeyboard::resize(int width, int height) {
	_width = width;

	auto y = 0.;
	auto buttonHeight = _rows.empty()
		? float64(_st->buttonHeight())
		: (float64(height + _st->buttonSkip()) / _rows.size());
	for (auto &row : _rows) {
		int s = row.size();

		int widthForButtons = _width - ((s - 1) * _st->buttonSkip());
		int widthForText = widthForButtons;
		int widthOfText = 0;
		int maxMinButtonWidth = 0;
		for (const auto &button : row) {
			widthOfText += qMax(button.text.maxWidth(), 1);
			int minButtonWidth = _st->minButtonWidth(button.type);
			widthForText -= minButtonWidth;
			accumulate_max(maxMinButtonWidth, minButtonWidth);
		}
		bool exact = (widthForText == widthOfText);
		bool enough = (widthForButtons - s * maxMinButtonWidth) >= widthOfText;

		float64 x = 0;
		for (auto &button : row) {
			int buttonw = qMax(button.text.maxWidth(), 1);
			float64 textw = buttonw, minw = _st->minButtonWidth(button.type);
			float64 w = textw;
			if (exact) {
				w += minw;
			} else if (enough) {
				w = (widthForButtons / float64(s));
				textw = w - minw;
			} else {
				textw = (widthForText / float64(s));
				w = minw + textw;
				accumulate_max(w, 2 * float64(_st->buttonPadding()));
			}

			int rectx = static_cast<int>(std::floor(x));
			int rectw = static_cast<int>(std::floor(x + w)) - rectx;
			button.rect = QRect(rectx, qRound(y), rectw, qRound(buttonHeight - _st->buttonSkip()));
			if (rtl()) button.rect.setX(_width - button.rect.x() - button.rect.width());
			x += w + _st->buttonSkip();

			button.link->setFullDisplayed(textw >= buttonw);
		}
		y += buttonHeight;
	}
}

bool ReplyKeyboard::isEnoughSpace(int width, const style::BotKeyboardButton &st) const {
	for (const auto &row : _rows) {
		int s = row.size();
		int widthLeft = width - ((s - 1) * st.margin + s * 2 * st.padding);
		for (const auto &button : row) {
			widthLeft -= qMax(button.text.maxWidth(), 1);
			if (widthLeft < 0) {
				if (row.size() > 3) {
					return false;
				} else {
					break;
				}
			}
		}
	}
	return true;
}

void ReplyKeyboard::setStyle(std::unique_ptr<Style> &&st) {
	_st = std::move(st);
}

int ReplyKeyboard::naturalWidth() const {
	auto result = 0;
	for (const auto &row : _rows) {
		auto maxMinButtonWidth = 0;
		for (const auto &button : row) {
			accumulate_max(
				maxMinButtonWidth,
				_st->minButtonWidth(button.type));
		}
		auto rowMaxButtonWidth = 0;
		for (const auto &button : row) {
			accumulate_max(
				rowMaxButtonWidth,
				qMax(button.text.maxWidth(), 1) + maxMinButtonWidth);
		}

		const auto rowSize = int(row.size());
		accumulate_max(
			result,
			rowSize * rowMaxButtonWidth + (rowSize - 1) * _st->buttonSkip());
	}
	return result;
}

int ReplyKeyboard::naturalHeight() const {
	return (_rows.size() - 1) * _st->buttonSkip() + _rows.size() * _st->buttonHeight();
}

void ReplyKeyboard::paint(
		Painter &p,
		const Ui::ChatStyle *st,
		Ui::BubbleRounding rounding,
		int outerWidth,
		const QRect &clip) const {
	Assert(_st != nullptr);
	Assert(_width > 0);

	_st->startPaint(p, st);
	for (auto y = 0, rowsCount = int(_rows.size()); y != rowsCount; ++y) {
		for (auto x = 0, count = int(_rows[y].size()); x != count; ++x) {
			const auto &button = _rows[y][x];
			const auto rect = button.rect;
			if (rect.y() >= clip.y() + clip.height()) return;
			if (rect.y() + rect.height() < clip.y()) continue;

			// just ignore the buttons that didn't layout well
			if (rect.x() + rect.width() > _width) break;

			auto buttonRounding = Ui::BubbleRounding();
			using Corner = Ui::BubbleCornerRounding;
			buttonRounding.topLeft = buttonRounding.topRight = Corner::Small;
			buttonRounding.bottomLeft = ((y + 1 == rowsCount)
				&& !x
				&& (rounding.bottomLeft == Corner::Large))
				? Corner::Large
				: Corner::Small;
			buttonRounding.bottomRight = ((y + 1 == rowsCount)
				&& (x + 1 == count)
				&& (rounding.bottomRight == Corner::Large))
				? Corner::Large
				: Corner::Small;
			_st->paintButton(p, st, outerWidth, button, buttonRounding);
		}
	}
}

ClickHandlerPtr ReplyKeyboard::getLink(QPoint point) const {
	Assert(_width > 0);

	for (const auto &row : _rows) {
		for (const auto &button : row) {
			QRect rect(button.rect);

			// just ignore the buttons that didn't layout well
			if (rect.x() + rect.width() > _width) break;

			if (rect.contains(point)) {
				_savedCoords = point;
				return button.link;
			}
		}
	}
	return ClickHandlerPtr();
}

void ReplyKeyboard::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!p) return;

	_savedActive = active ? p : ClickHandlerPtr();
	auto coords = findButtonCoordsByClickHandler(p);
	if (coords.i >= 0 && _savedPressed != p) {
		startAnimation(coords.i, coords.j, active ? 1 : -1);
	}
}

ReplyKeyboard::ButtonCoords ReplyKeyboard::findButtonCoordsByClickHandler(const ClickHandlerPtr &p) {
	for (int i = 0, rows = _rows.size(); i != rows; ++i) {
		auto &row = _rows[i];
		for (int j = 0, cols = row.size(); j != cols; ++j) {
			if (row[j].link == p) {
				return { i, j };
			}
		}
	}
	return { -1, -1 };
}

void ReplyKeyboard::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed,
		Ui::BubbleRounding rounding) {
	if (!handler) return;

	_savedPressed = pressed ? handler : ClickHandlerPtr();
	auto coords = findButtonCoordsByClickHandler(handler);
	if (coords.i >= 0) {
		auto &button = _rows[coords.i][coords.j];
		if (pressed) {
			if (!button.ripple) {
				const auto sides = RectPart()
					| (!coords.i ? RectPart::Top : RectPart())
					| (!coords.j ? RectPart::Left : RectPart())
					| ((coords.i + 1 == _rows.size())
						? RectPart::Bottom
						: RectPart())
					| ((coords.j + 1 == _rows[coords.i].size())
						? RectPart::Right
						: RectPart());
				auto mask = Ui::RippleAnimation::RoundRectMask(
					button.rect.size(),
					_st->buttonRounding(rounding, sides));
				button.ripple = std::make_unique<Ui::RippleAnimation>(
					_st->_st->ripple,
					std::move(mask),
					[=] { _st->repaint(_item); });
			}
			button.ripple->add(_savedCoords - button.rect.topLeft());
		} else {
			if (button.ripple) {
				button.ripple->lastStop();
			}
			if (_savedActive != handler) {
				startAnimation(coords.i, coords.j, -1);
			}
		}
	}
}

void ReplyKeyboard::startAnimation(int i, int j, int direction) {
	auto notStarted = _animations.empty();

	int indexForAnimation = Layout::PositionToIndex(i, j + 1) * direction;

	_animations.remove(-indexForAnimation);
	if (!_animations.contains(indexForAnimation)) {
		_animations.emplace(indexForAnimation, crl::now());
	}

	if (notStarted && !_selectedAnimation.animating()) {
		_selectedAnimation.start();
	}
}

bool ReplyKeyboard::selectedAnimationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::botKbDuration;
	}
	for (auto i = _animations.begin(); i != _animations.end();) {
		const auto index = std::abs(i->first) - 1;
		const auto &[row, col] = Layout::IndexToPosition(index);
		const auto dt = float64(now - i->second) / st::botKbDuration;
		if (dt >= 1) {
			_rows[row][col].howMuchOver = (i->first > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			_rows[row][col].howMuchOver = (i->first > 0) ? dt : (1 - dt);
			++i;
		}
	}
	_st->repaint(_item);
	return !_animations.empty();
}

void ReplyKeyboard::clearSelection() {
	for (const auto &[relativeIndex, time] : _animations) {
		const auto index = std::abs(relativeIndex) - 1;
		const auto &[row, col] = Layout::IndexToPosition(index);
		_rows[row][col].howMuchOver = 0;
	}
	_animations.clear();
	_selectedAnimation.stop();
}

int ReplyKeyboard::Style::buttonSkip() const {
	return _st->margin;
}

int ReplyKeyboard::Style::buttonPadding() const {
	return _st->padding;
}

int ReplyKeyboard::Style::buttonHeight() const {
	return _st->height;
}

void ReplyKeyboard::Style::paintButton(
		Painter &p,
		const Ui::ChatStyle *st,
		int outerWidth,
		const ReplyKeyboard::Button &button,
		Ui::BubbleRounding rounding) const {
	const QRect &rect = button.rect;
	paintButtonBg(p, st, rect, rounding, button.howMuchOver);
	if (button.ripple) {
		const auto color = st ? &st->msgBotKbRippleBg()->c : nullptr;
		button.ripple->paint(p, rect.x(), rect.y(), outerWidth, color);
		if (button.ripple->empty()) {
			button.ripple.reset();
		}
	}
	paintButtonIcon(p, st, rect, outerWidth, button.type);
	if (button.type == HistoryMessageMarkupButton::Type::CallbackWithPassword
		|| button.type == HistoryMessageMarkupButton::Type::Callback
		|| button.type == HistoryMessageMarkupButton::Type::Game) {
		if (const auto data = button.link->getButton()) {
			if (data->requestId) {
				paintButtonLoading(p, st, rect, outerWidth, rounding);
			}
		}
	}

	int tx = rect.x(), tw = rect.width();
	if (tw >= st::botKbStyle.font->elidew + _st->padding * 2) {
		tx += _st->padding;
		tw -= _st->padding * 2;
	} else if (tw > st::botKbStyle.font->elidew) {
		tx += (tw - st::botKbStyle.font->elidew) / 2;
		tw = st::botKbStyle.font->elidew;
	}
	button.text.drawElided(p, tx, rect.y() + _st->textTop + ((rect.height() - _st->height) / 2), tw, 1, style::al_top);
}

void HistoryMessageReplyMarkup::createForwarded(
		const HistoryMessageReplyMarkup &original) {
	Expects(!inlineKeyboard);

	data.fillForwardedData(original.data);
}

void HistoryMessageReplyMarkup::updateData(
		HistoryMessageMarkupData &&markup) {
	data = std::move(markup);
	inlineKeyboard = nullptr;
}

bool HistoryMessageReplyMarkup::hiddenBy(Data::Media *media) const {
	if (media && (data.flags & ReplyMarkupFlag::OnlyBuyButton)) {
		if (const auto invoice = media->invoice()) {
			if (invoice->extendedPreview
				&& (!invoice->extendedMedia || !invoice->receiptMsgId)) {
				return true;
			}
		}
	}
	return false;
}

HistoryMessageLogEntryOriginal::HistoryMessageLogEntryOriginal() = default;

HistoryMessageLogEntryOriginal::HistoryMessageLogEntryOriginal(
	HistoryMessageLogEntryOriginal &&other)
: page(std::move(other.page)) {
}

HistoryMessageLogEntryOriginal &HistoryMessageLogEntryOriginal::operator=(
		HistoryMessageLogEntryOriginal &&other) {
	page = std::move(other.page);
	return *this;
}

HistoryMessageLogEntryOriginal::~HistoryMessageLogEntryOriginal() = default;

HistoryDocumentCaptioned::HistoryDocumentCaptioned()
: caption(st::msgFileMinWidth - st::msgPadding.left() - st::msgPadding.right()) {
}

HistoryDocumentVoicePlayback::HistoryDocumentVoicePlayback(
	const HistoryView::Document *that)
: progress(0., 0.)
, progressAnimation([=](crl::time now) {
	const auto nonconst = const_cast<HistoryView::Document*>(that);
	return nonconst->voiceProgressAnimationCallback(now);
}) {
}

void HistoryDocumentVoice::ensurePlayback(
		const HistoryView::Document *that) const {
	if (!playback) {
		playback = std::make_unique<HistoryDocumentVoicePlayback>(that);
	}
}

void HistoryDocumentVoice::checkPlaybackFinished() const {
	if (playback && !playback->progressAnimation.animating()) {
		playback.reset();
	}
}

void HistoryDocumentVoice::startSeeking() {
	_seeking = true;
	_seekingCurrent = _seekingStart;
	Media::Player::instance()->startSeeking(AudioMsgId::Type::Voice);
}

void HistoryDocumentVoice::stopSeeking() {
	_seeking = false;
	Media::Player::instance()->cancelSeeking(AudioMsgId::Type::Voice);
}

bool HistoryDocumentVoice::seeking() const {
	return _seeking;
}

float64 HistoryDocumentVoice::seekingStart() const {
	return _seekingStart / kFloatToIntMultiplier;
}

void HistoryDocumentVoice::setSeekingStart(float64 seekingStart) const {
	_seekingStart = qRound(seekingStart * kFloatToIntMultiplier);
}

float64 HistoryDocumentVoice::seekingCurrent() const {
	return _seekingCurrent / kFloatToIntMultiplier;
}

void HistoryDocumentVoice::setSeekingCurrent(float64 seekingCurrent) {
	_seekingCurrent = qRound(seekingCurrent * kFloatToIntMultiplier);
}
