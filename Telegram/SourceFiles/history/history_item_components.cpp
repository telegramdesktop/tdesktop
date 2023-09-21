/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_components.h"

#include "base/qt/qt_key_modifiers.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
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
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_web_page.h"
#include "data/data_file_click_handler.h"
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

HiddenSenderInfo::HiddenSenderInfo(const QString &name, bool external)
: name(name)
, colorPeerId(Data::FakePeerIdForJustName(name))
, emptyUserpic(
	Ui::EmptyUserpic::UserpicColor(Data::PeerColorIndex(colorPeerId)),
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
	if (!originalAuthor.isEmpty()) {
		phrase = tr::lng_forwarded_signed(
			tr::now,
			lt_channel,
			name,
			lt_user,
			{ .text = originalAuthor },
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

bool HistoryMessageReply::updateData(
		not_null<HistoryItem*> holder,
		bool force) {
	const auto guard = gsl::finally([&] { refreshReplyToMedia(); });
	if (!force) {
		if ((replyToMsg || !replyToMsgId)
			&& (replyToStory || !replyToStoryId)) {
			return true;
		}
	}
	const auto peerId = replyToPeerId
		? replyToPeerId
		: holder->history()->peer->id;
	if (!replyToMsg && replyToMsgId) {
		replyToMsg = holder->history()->owner().message(
			peerId,
			replyToMsgId);
		if (replyToMsg) {
			if (replyToMsg->isEmpty()) {
				// Really it is deleted.
				replyToMsg = nullptr;
				force = true;
			} else {
				holder->history()->owner().registerDependentMessage(
					holder,
					replyToMsg.get());
			}
		}
	}
	if (!replyToStory && replyToStoryId) {
		const auto maybe = holder->history()->owner().stories().lookup({
			peerId,
			replyToStoryId,
		});
		if (maybe) {
			replyToStory = *maybe;
			holder->history()->owner().stories().registerDependentMessage(
				holder,
				replyToStory.get());
		} else if (maybe.error() == Data::NoStory::Deleted) {
			force = true;
		}
	}

	if (replyToMsg || replyToStory) {
		const auto repaint = [=] { holder->customEmojiRepaint(); };
		const auto context = Core::MarkedTextContext{
			.session = &holder->history()->session(),
			.customEmojiRepaint = repaint,
		};
		replyToText.setMarkedText(
			st::messageTextStyle,
			(replyToMsg
				? replyToMsg->inReplyText()
				: replyToStory->inReplyText()),
			Ui::DialogTextOptions(),
			context);

		updateName(holder);

		setReplyToLinkFrom(holder);
		if (replyToMsg && !replyToMsg->Has<HistoryMessageForwarded>()) {
			if (auto bot = replyToMsg->viaBot()) {
				replyToVia = std::make_unique<HistoryMessageVia>();
				replyToVia->create(
					&holder->history()->owner(),
					peerToUser(bot->id));
			}
		}

		if (replyToMsg) {
			const auto peer = replyToMsg->history()->peer;
			replyToColorKey = (!holder->out()
					&& (peer->isMegagroup() || peer->isChat()))
				? replyToMsg->from()->id
				: PeerId(0);
		} else {
			replyToColorKey = PeerId(0);
		}

		const auto media = replyToMsg ? replyToMsg->media() : nullptr;
		if (!media || !media->hasReplyPreview() || !media->hasSpoiler()) {
			spoiler = nullptr;
		} else if (!spoiler) {
			spoiler = std::make_unique<Ui::SpoilerAnimation>(repaint);
		}
	} else if (force) {
		replyToMsgId = 0;
		replyToStoryId = 0;
		replyToColorKey = PeerId(0);
		spoiler = nullptr;
	}
	if (force) {
		holder->history()->owner().requestItemResize(holder);
	}
	return (replyToMsg || !replyToMsgId)
		&& (replyToStory || !replyToStoryId);
}

void HistoryMessageReply::setReplyToLinkFrom(
		not_null<HistoryItem*> holder) {
	replyToLnk = replyToMsg
		? JumpToMessageClickHandler(replyToMsg.get(), holder->fullId())
		: replyToStory
		? JumpToStoryClickHandler(replyToStory.get())
		: nullptr;
}

void HistoryMessageReply::clearData(not_null<HistoryItem*> holder) {
	replyToVia = nullptr;
	if (replyToMsg) {
		holder->history()->owner().unregisterDependentMessage(
			holder,
			replyToMsg.get());
		replyToMsg = nullptr;
	}
	if (replyToStory) {
		holder->history()->owner().stories().unregisterDependentMessage(
			holder,
			replyToStory.get());
		replyToStory = nullptr;
	}
	replyToMsgId = 0;
	replyToStoryId = 0;
	refreshReplyToMedia();
}

PeerData *HistoryMessageReply::replyToFrom(
		not_null<HistoryItem*> holder) const {
	if (!replyToMsg) {
		return nullptr;
	} else if (holder->Has<HistoryMessageForwarded>()) {
		if (const auto fwd = replyToMsg->Get<HistoryMessageForwarded>()) {
			return fwd->originalSender;
		}
	}
	if (const auto from = replyToMsg->displayFrom()) {
		return from;
	}
	return replyToMsg->author().get();
}

QString HistoryMessageReply::replyToFromName(
		not_null<HistoryItem*> holder) const {
	if (replyToStory) {
		return replyToFromName(replyToStory->peer());
	} else if (!replyToMsg) {
		return QString();
	} else if (holder->Has<HistoryMessageForwarded>()) {
		if (const auto fwd = replyToMsg->Get<HistoryMessageForwarded>()) {
			return fwd->originalSender
				? replyToFromName(fwd->originalSender)
				: fwd->hiddenSenderInfo->name;
		}
	}
	if (const auto from = replyToMsg->displayFrom()) {
		return replyToFromName(from);
	}
	return replyToFromName(replyToMsg->author());
}

QString HistoryMessageReply::replyToFromName(
		not_null<PeerData*> peer) const {
	if (const auto user = replyToVia ? peer->asUser() : nullptr) {
		return user->firstName;
	}
	return peer->name();
}

bool HistoryMessageReply::isNameUpdated(
		not_null<HistoryItem*> holder) const {
	if (const auto from = replyToFrom(holder)) {
		if (replyToVersion < from->nameVersion()) {
			updateName(holder);
			return true;
		}
	}
	return false;
}

void HistoryMessageReply::updateName(
		not_null<HistoryItem*> holder) const {
	if (const auto name = replyToFromName(holder); !name.isEmpty()) {
		replyToName.setText(st::fwdTextStyle, name, Ui::NameTextOptions());
		if (const auto from = replyToFrom(holder)) {
			replyToVersion = from->nameVersion();
		} else if (replyToMsg) {
			replyToVersion = replyToMsg->author()->nameVersion();
		} else {
			replyToVersion = replyToStory->peer()->nameVersion();
		}
		bool hasPreview = (replyToStory && replyToStory->hasReplyPreview())
			|| (replyToMsg
				&& replyToMsg->media()
				&& replyToMsg->media()->hasReplyPreview());
		int32 previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		int32 w = replyToName.maxWidth();
		if (replyToVia) {
			w += st::msgServiceFont->spacew + replyToVia->maxWidth;
		}

		maxReplyWidth = previewSkip
			+ std::max(
				w,
				std::min(replyToText.maxWidth(), st::maxSignatureSize))
			+ (storyReply
				? (st::dialogsMiniReplyStory.skipText
					+ st::dialogsMiniReplyStory.icon.icon.width())
				: 0);
	} else {
		maxReplyWidth = st::msgDateFont->width(statePhrase());
	}
	maxReplyWidth = st::msgReplyPadding.left() + st::msgReplyBarSkip + maxReplyWidth + st::msgReplyPadding.right();
}

void HistoryMessageReply::resize(int width) const {
	if (replyToVia) {
		bool hasPreview = (replyToStory && replyToStory->hasReplyPreview())
			|| (replyToMsg
				&& replyToMsg->media()
				&& replyToMsg->media()->hasReplyPreview());
		int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		replyToVia->resize(width - st::msgReplyBarSkip - previewSkip - replyToName.maxWidth() - st::msgServiceFont->spacew);
	}
}

void HistoryMessageReply::itemRemoved(
		not_null<HistoryItem*> holder,
		not_null<HistoryItem*> removed) {
	if (replyToMsg.get() == removed) {
		clearData(holder);
		holder->history()->owner().requestItemResize(holder);
	}
}

void HistoryMessageReply::storyRemoved(
		not_null<HistoryItem*> holder,
		not_null<Data::Story*> removed) {
	if (replyToStory.get() == removed) {
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

	{
		const auto opacity = p.opacity();
		const auto outerWidth = w + 2 * x;
		const auto &bar = !inBubble
			? st->msgImgReplyBarColor()
			: replyToColorKey
			? HistoryView::FromNameFg(context, replyToColorKey)
			: stm->msgReplyBarColor;
		const auto rbar = style::rtlrect(
			x + st::msgReplyBarPos.x(),
			y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(),
			st::msgReplyBarSize.width(),
			st::msgReplyBarSize.height(),
			outerWidth);

		if (ripple.animation) {
			const auto colorOverride = &stm->msgWaveformInactive->c;
			p.setOpacity(st::historyPollRippleOpacity);
			ripple.animation->paint(
				p,
				x - st::msgReplyPadding.left(),
				y,
				outerWidth,
				colorOverride);
			if (ripple.animation->empty()) {
				ripple.animation.reset();
			}
		}

		p.setOpacity(opacity * kBarAlpha);
		p.fillRect(rbar, bar);
		p.setOpacity(opacity);
	}

	const auto pausedSpoiler = context.paused
		|| On(PowerSaving::kChatSpoiler);
	if (w > st::msgReplyBarSkip) {
		if (replyToMsg || replyToStory) {
			const auto media = replyToMsg ? replyToMsg->media() : nullptr;
			auto hasPreview = (replyToStory && replyToStory->hasReplyPreview()) || (media && media->hasReplyPreview());
			if (hasPreview && w < st::msgReplyBarSkip + st::msgReplyBarSize.height()) {
				hasPreview = false;
			}
			auto previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;

			if (hasPreview) {
				const auto image = media
					? media->replyPreview()
					: replyToStory->replyPreview();
				if (image) {
					auto to = style::rtlrect(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height(), w + 2 * x);
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
			if (w > st::msgReplyBarSkip + previewSkip) {
				p.setPen(!inBubble
					? st->msgImgReplyBarColor()
					: replyToColorKey
					? HistoryView::FromNameFg(context, replyToColorKey)
					: stm->msgServiceFg);
				replyToName.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top(), w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
				if (replyToVia && w > st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew) {
					p.setFont(st::msgServiceFont);
					p.drawText(x + st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew, y + st::msgReplyPadding.top() + st::msgServiceFont->ascent, replyToVia->text);
				}

				p.setPen(inBubble
					? stm->historyTextFg
					: st->msgImgReplyBarColor());
				holder->prepareCustomEmojiPaint(p, context, replyToText);
				auto replyToTextPosition = QPoint(
					x + st::msgReplyBarSkip + previewSkip,
					y + st::msgReplyPadding.top() + st::msgServiceNameFont->height);
				const auto replyToTextPalette = &(inBubble
					? stm->replyTextPalette
					: st->imgReplyTextPalette());
				if (storyReply) {
					st::dialogsMiniReplyStory.icon.icon.paint(
						p,
						replyToTextPosition,
						w - st::msgReplyBarSkip - previewSkip,
						replyToTextPalette->linkFg->c);
					replyToTextPosition += QPoint(
						st::dialogsMiniReplyStory.skipText
							+ st::dialogsMiniReplyStory.icon.icon.width(),
						0);
				}
				replyToText.draw(p, {
					.position = replyToTextPosition,
					.availableWidth = w - st::msgReplyBarSkip - previewSkip,
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
			p.setPen(inBubble
				? stm->msgDateFg
				: st->msgDateImgFg());
			p.drawTextLeft(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2, w + 2 * x, st::msgDateFont->elided(statePhrase(), w - st::msgReplyBarSkip));
		}
	}
}

QString HistoryMessageReply::statePhrase() const {
	return (replyToMsgId || replyToStoryId)
		? tr::lng_profile_loading(tr::now)
		: storyReply
		? tr::lng_deleted_story(tr::now)
		: tr::lng_deleted_message(tr::now);
}

void HistoryMessageReply::refreshReplyToMedia() {
	replyToDocumentId = 0;
	replyToWebPageId = 0;
	if (const auto media = replyToMsg ? replyToMsg->media() : nullptr) {
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
