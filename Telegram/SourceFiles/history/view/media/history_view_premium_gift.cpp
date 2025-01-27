/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_premium_gift.h"

#include "apiwrap.h"
#include "api/api_credits.h" // InputSavedStarGiftId
#include "api/api_premium.h"
#include "base/unixtime.h"
#include "boxes/gift_premium_box.h" // ResolveGiftCode
#include "chat_helpers/stickers_gift_box_pack.h"
#include "core/click_handler_types.h" // ClickHandlerContext
#include "data/stickers/data_custom_emoji.h"
#include "data/data_channel.h"
#include "data/data_credits.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_credits.h" // Settings::CreditsId
#include "settings/settings_credits_graphics.h" // GiftedCreditsBox
#include "settings/settings_premium.h" // Settings::ShowGiftPremium
#include "ui/chat/chat_style.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {

PremiumGift::PremiumGift(
	not_null<Element*> parent,
	not_null<Data::MediaGiftBox*> gift)
: _parent(parent)
, _gift(gift)
, _data(*gift->gift()) {
}

PremiumGift::~PremiumGift() = default;

int PremiumGift::top() {
	return starGift()
		? st::msgServiceStarGiftStickerTop
		: st::msgServiceGiftBoxStickerTop;
}

int PremiumGift::width() {
	return st::msgServiceStarGiftBoxWidth;
}

QSize PremiumGift::size() {
	return starGift()
		? QSize(
			st::msgServiceStarGiftStickerSize,
			st::msgServiceStarGiftStickerSize)
		: QSize(
			st::msgServiceGiftBoxStickerSize,
			st::msgServiceGiftBoxStickerSize);
}

TextWithEntities PremiumGift::title() {
	using namespace Ui::Text;
	if (starGift()) {
		const auto peer = _parent->history()->peer;
		return peer->isSelf()
			? tr::lng_action_gift_self_subtitle(tr::now, WithEntities)
			: (peer->isServiceUser() && _data.channelFrom)
			? tr::lng_action_gift_got_subtitle(
				tr::now,
				lt_user,
				WithEntities({})
					.append(SingleCustomEmoji(
						peer->owner().customEmojiManager(
							).peerUserpicEmojiData(_data.channelFrom)))
					.append(' ')
					.append(_data.channelFrom->shortName()),
				WithEntities)
			: peer->isServiceUser()
			? tr::lng_gift_link_label_gift(tr::now, WithEntities)
			: (outgoingGift()
				? tr::lng_action_gift_sent_subtitle
				: tr::lng_action_gift_got_subtitle)(
					tr::now,
					lt_user,
					WithEntities({})
						.append(SingleCustomEmoji(
							peer->owner().customEmojiManager(
								).peerUserpicEmojiData(peer)))
						.append(' ')
						.append(peer->shortName()),
					WithEntities);
	} else if (creditsPrize()) {
		return tr::lng_prize_title(tr::now, WithEntities);
	} else if (const auto c = credits()) {
		return tr::lng_gift_stars_title(tr::now, lt_count, c, WithEntities);
	}
	return gift()
		? tr::lng_action_gift_premium_months(
			tr::now,
			lt_count,
			_data.count,
			WithEntities)
		: _data.unclaimed
		? tr::lng_prize_unclaimed_title(tr::now, WithEntities)
		: tr::lng_prize_title(tr::now, WithEntities);
}

TextWithEntities PremiumGift::subtitle() {
	if (starGift()) {
		const auto toChannel = _data.channel
			&& _parent->history()->peer->isServiceUser();
		return !_data.message.empty()
			? _data.message
			: _data.refunded
			? tr::lng_action_gift_refunded(tr::now, Ui::Text::RichLangValue)
			: outgoingGift()
			? (_data.starsUpgradedBySender
				? tr::lng_action_gift_sent_upgradable(
					tr::now,
					lt_user,
					Ui::Text::Bold(_parent->history()->peer->shortName()),
					Ui::Text::RichLangValue)
				: tr::lng_action_gift_sent_text(
					tr::now,
					lt_count,
					_data.starsConverted,
					lt_user,
					Ui::Text::Bold(_parent->history()->peer->shortName()),
					Ui::Text::RichLangValue))
			: _data.starsUpgradedBySender
			? tr::lng_action_gift_got_upgradable_text(
				tr::now,
				Ui::Text::RichLangValue)
			: (_data.starsToUpgrade
				&& !_data.converted
				&& _parent->history()->peer->isSelf())
			? tr::lng_action_gift_self_about_unique(
				tr::now,
				Ui::Text::RichLangValue)
			: (_data.starsToUpgrade
				&& !_data.converted
				&& _parent->history()->peer->isServiceUser()
				&& _data.channel)
			? tr::lng_action_gift_channel_about_unique(
				tr::now,
				Ui::Text::RichLangValue)
			: (!_data.converted && !_data.starsConverted)
			? (_data.saved
				? (toChannel
					? tr::lng_action_gift_can_remove_channel
					: tr::lng_action_gift_can_remove_text)
				: (toChannel
					? tr::lng_action_gift_got_gift_channel
					: tr::lng_action_gift_got_gift_text))(
						tr::now,
						Ui::Text::RichLangValue)
			: (_data.converted
				? (toChannel
					? tr::lng_gift_channel_got
					: tr::lng_gift_got_stars)
				: _parent->history()->peer->isSelf()
				? tr::lng_action_gift_self_about
				: toChannel
				? tr::lng_action_gift_channel_about
				: tr::lng_action_gift_got_stars_text)(
					tr::now,
					lt_count,
					_data.starsConverted,
					Ui::Text::RichLangValue);
	}
	const auto isCreditsPrize = creditsPrize();
	if (const auto count = credits(); count && !isCreditsPrize) {
		return outgoingGift()
			? tr::lng_gift_stars_outgoing(
				tr::now,
				lt_user,
				Ui::Text::Bold(_parent->history()->peer->shortName()),
				Ui::Text::RichLangValue)
			: tr::lng_gift_stars_incoming(tr::now, Ui::Text::WithEntities);
	} else if (gift()) {
		return !_data.message.empty()
			? _data.message
			: tr::lng_action_gift_premium_about(
				tr::now,
				Ui::Text::RichLangValue);
	}
	const auto name = _data.channel ? _data.channel->name() : "channel";
	auto result = (_data.unclaimed
		? tr::lng_prize_unclaimed_about
		: _data.viaGiveaway
		? tr::lng_prize_about
		: tr::lng_prize_gift_about)(
			tr::now,
			lt_channel,
			Ui::Text::Bold(name),
			Ui::Text::RichLangValue);
	result.append("\n\n");
	result.append(isCreditsPrize
		? tr::lng_prize_credits(
			tr::now,
			lt_amount,
			tr::lng_prize_credits_amount(
				tr::now,
				lt_count,
				credits(),
				Ui::Text::RichLangValue),
			Ui::Text::RichLangValue)
		: (_data.unclaimed
			? tr::lng_prize_unclaimed_duration
			: _data.viaGiveaway
			? tr::lng_prize_duration
			: tr::lng_prize_gift_duration)(
				tr::now,
				lt_duration,
				Ui::Text::Bold(GiftDuration(_data.count)),
				Ui::Text::RichLangValue));
	return result;
}

rpl::producer<QString> PremiumGift::button() {
	return (starGift() && outgoingGift())
		? tr::lng_sticker_premium_view()
		: creditsPrize()
		? tr::lng_view_button_giftcode()
		: (starGift() && _data.starsUpgradedBySender && !_data.upgraded)
		? tr::lng_gift_view_unpack()
		: (gift() && (outgoingGift() || !_data.unclaimed))
		? tr::lng_sticker_premium_view()
		: tr::lng_prize_open();
}

bool PremiumGift::buttonMinistars() {
	return true;
}

ClickHandlerPtr PremiumGift::createViewLink() {
	if (auto link = OpenStarGiftLink(_parent->data())) {
		return link;
	}
	const auto from = _gift->from();
	const auto peer = _parent->history()->peer;
	const auto date = _parent->data()->date();
	const auto data = *_gift->gift();
	const auto showForWeakWindow = [=](
			base::weak_ptr<Window::SessionController> weak) {
		const auto controller = weak.get();
		if (!controller) {
			return;
		}
		const auto selfId = controller->session().userPeerId();
		const auto sent = (from->id == selfId);
		if (creditsPrize()) {
			controller->show(Box(
				Settings::CreditsPrizeBox,
				controller,
				data,
				date));
		} else if (data.type == Data::GiftType::Credits) {
			const auto to = sent ? peer : peer->session().user();
			controller->show(Box(
				Settings::GiftedCreditsBox,
				controller,
				from,
				to,
				data.count,
				date));
		} else if (data.slug.isEmpty()) {
			const auto months = data.count;
			Settings::ShowGiftPremium(controller, peer, months, sent);
		} else {
			const auto fromId = from->id;
			const auto toId = sent ? peer->id : selfId;
			ResolveGiftCode(controller, data.slug, fromId, toId);
		}
	};
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		showForWeakWindow(
			context.other.value<ClickHandlerContext>().sessionWindow);
	});
}

int PremiumGift::buttonSkip() {
	return st::msgServiceGiftBoxButtonMargins.top();
}

void PremiumGift::draw(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) {
	if (_sticker) {
		_sticker->draw(p, context, geometry);
	} else {
		ensureStickerCreated();
	}
}

QImage PremiumGift::cornerTag(const PaintContext &context) {
	auto badge = Info::PeerGifts::GiftBadge();
	if (_data.unique) {
		badge = {
			.text = tr::lng_gift_collectible_tag(tr::now),
			.bg1 = _data.unique->backdrop.edgeColor,
			.bg2 = _data.unique->backdrop.patternColor,
			.fg = QColor(255, 255, 255),
		};
	} else if (const auto count = _data.limitedCount) {
		badge = {
			.text = ((count == 1)
				? tr::lng_gift_limited_of_one(tr::now)
				: tr::lng_gift_limited_of_count(
					tr::now,
					lt_amount,
					(((count % 1000) && (count < 10'000))
						? Lang::FormatCountDecimal(count)
						: Lang::FormatCountToShort(count).string))),
			.bg1 = context.st->msgServiceBg()->c,
			.fg = context.st->msgServiceFg()->c,
		};
	} else {
		return {};
	}
	if (_badgeCache.isNull() || _badgeKey != badge) {
		_badgeKey = badge;
		_badgeCache = ValidateRotatedBadge(badge, 0);
	}
	return _badgeCache;
}

bool PremiumGift::hideServiceText() {
	return !gift();
}

void PremiumGift::stickerClearLoopPlayed() {
	if (_sticker) {
		_sticker->stickerClearLoopPlayed();
	}
}

std::unique_ptr<StickerPlayer> PremiumGift::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _sticker
		? _sticker->stickerTakePlayer(data, replacements)
		: nullptr;
}

bool PremiumGift::hasHeavyPart() {
	return (_sticker ? _sticker->hasHeavyPart() : false);
}

void PremiumGift::unloadHeavyPart() {
	if (_sticker) {
		_sticker->unloadHeavyPart();
	}
}

bool PremiumGift::incomingGift() const {
	const auto out = _parent->data()->out();
	return gift() && (starGiftUpgrade() ? out : !out);
}

bool PremiumGift::outgoingGift() const {
	const auto out = _parent->data()->out();
	return gift() && (starGiftUpgrade() ? !out : out);
}

bool PremiumGift::gift() const {
	return _data.slug.isEmpty() || !_data.channel;
}

bool PremiumGift::starGift() const {
	return (_data.type == Data::GiftType::StarGift);
}

bool PremiumGift::starGiftUpgrade() const {
	return (_data.type == Data::GiftType::StarGift) && _data.upgrade;
}

bool PremiumGift::creditsPrize() const {
	return _data.viaGiveaway
		&& (_data.type == Data::GiftType::Credits)
		&& !_data.slug.isEmpty();
}

int PremiumGift::credits() const {
	return (_data.type == Data::GiftType::Credits) ? _data.count : 0;
}

void PremiumGift::ensureStickerCreated() const {
	if (_sticker) {
		return;
	} else if (const auto document = _data.document) {
		const auto sticker = document->sticker();
		Assert(sticker != nullptr);
		_sticker.emplace(_parent, document, false, _parent);
		_sticker->setPlayingOnce(true);
		_sticker->initSize(st::msgServiceStarGiftStickerSize);
		_parent->repaint();
		return;
	}
	const auto &session = _parent->history()->session();
	auto &packs = session.giftBoxStickersPacks();
	const auto count = credits();
	const auto months = count ? packs.monthsForStars(count) : _data.count;
	if (const auto document = packs.lookup(months)) {
		if (const auto sticker = document->sticker()) {
			const auto skipPremiumEffect = false;
			_sticker.emplace(_parent, document, skipPremiumEffect, _parent);
			_sticker->setPlayingOnce(true);
			_sticker->initSize(st::msgServiceGiftBoxStickerSize);
		}
	}
}

ClickHandlerPtr OpenStarGiftLink(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto gift = media ? media->gift() : nullptr;
	if (!gift || gift->type != Data::GiftType::StarGift) {
		return nullptr;
	}
	const auto data = *gift;
	const auto itemId = item->fullId();
	const auto openInsteadId = data.upgradeMsgId
		? Data::SavedStarGiftId::User(data.upgradeMsgId)
		: (data.channel && data.channelSavedId)
		? Data::SavedStarGiftId::Chat(data.channel, data.channelSavedId)
		: Data::SavedStarGiftId();
	const auto requesting = std::make_shared<bool>();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto weak = my.sessionWindow;
		const auto controller = weak.get();
		if (!controller) {
			return;
		}
		const auto quick = [=](not_null<Window::SessionController*> window) {
			const auto item = window->session().data().message(itemId);
			if (item) {
				window->show(Box(
					Settings::StarGiftViewBox,
					window,
					data,
					item));
			}
		};
		if (!openInsteadId) {
			quick(controller);
			return;
		} else if (*requesting) {
			return;
		}
		*requesting = true;
		controller->session().api().request(MTPpayments_GetSavedStarGift(
			MTP_vector<MTPInputSavedStarGift>(
				1,
				Api::InputSavedStarGiftId(openInsteadId))
		)).done([=](const MTPpayments_SavedStarGifts &result) {
			*requesting = false;
			if (const auto window = weak.get()) {
				const auto &data = result.data();
				window->session().data().processUsers(data.vusers());
				window->session().data().processChats(data.vchats());
				const auto owner = openInsteadId.chat()
					? openInsteadId.chat()
					: window->session().user();
				const auto &list = data.vgifts().v;
				if (list.empty()) {
					quick(window);
				} else if (auto parsed = Api::FromTL(owner, list[0])) {
					window->show(Box(
						Settings::SavedStarGiftBox,
						window,
						owner,
						*parsed));
				}
			}
		}).fail([=](const MTP::Error &error) {
			*requesting = false;
			if (const auto window = weak.get()) {
				window->showToast(error.type());
				quick(window);
			}
		}).send();
	});
}

} // namespace HistoryView
