/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_box.h"

#include "apiwrap.h"
#include "api/api_credits.h"
#include "api/api_global_privacy.h"
#include "api/api_premium.h"
#include "api/api_text_entities.h"
//#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/qt_signal_producer.h"
#include "base/random.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "boxes/peers/edit_peer_color_box.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "boxes/gift_premium_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/premium_preview_box.h"
#include "boxes/send_credits_box.h"
#include "boxes/star_gift_auction_box.h"
#include "boxes/star_gift_resale_box.h"
#include "boxes/transfer_gift_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/components/gift_auctions.h"
#include "data/components/promo_suggestions.h"
#include "data/data_birthday.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_credits.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_emoji_statuses.h"
#include "data/data_file_origin.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/controls/history_view_suggest_options.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/media/history_view_unique_gift.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "info/profile/info_profile_icon.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "menu/gift_resale_filter.h"
#include "payments/payments_form.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_non_panel_process.h"
#include "settings/settings_credits.h"
#include "settings/settings_credits_graphics.h"
#include "settings/settings_premium.h"
#include "ui/boxes/boost_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/button_labels.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/ton_common.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/premium_bubble.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/new_badges.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/top_background_gradient.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/number_input.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kPriceTabAll = 0;
constexpr auto kPriceTabMy = -1;
constexpr auto kPriceTabCollectibles = -2;
constexpr auto kGiftMessageLimit = 255;
constexpr auto kSentToastDuration = 3 * crl::time(1000);
constexpr auto kSwitchUpgradeCoverInterval = 3 * crl::time(1000);
constexpr auto kCrossfadeDuration = crl::time(400);
constexpr auto kUpgradeDoneToastDuration = 4 * crl::time(1000);
constexpr auto kGiftsPreloadTimeout = 3 * crl::time(1000);
constexpr auto kResellPriceCacheLifetime = 60 * crl::time(1000);
constexpr auto kGradientButtonBgOpacity = 0.6;

constexpr auto kSpinnerBackdrops = 6;
constexpr auto kSpinnerPatterns = 6;
constexpr auto kSpinnerModels = 6;

constexpr auto kBackdropSpinDuration = crl::time(300);
constexpr auto kBackdropStopsAt = crl::time(2.5 * 1000);
constexpr auto kPatternSpinDuration = crl::time(600);
constexpr auto kPatternStopsAt = crl::time(4 * 1000);
constexpr auto kModelSpinDuration = crl::time(160);
constexpr auto kModelStopsAt = crl::time(5.5 * 1000);
constexpr auto kModelScaleFrom = 0.7;

using namespace HistoryView;
using namespace Info::PeerGifts;

using Data::GiftAttributeId;
using Data::GiftAttributeIdType;
using Data::ResaleGiftsSort;
using Data::ResaleGiftsFilter;
using Data::ResaleGiftsDescriptor;
using Data::MyGiftsDescriptor;

enum class PickType {
	Activate,
	SendMessage,
	OpenProfile,
};
using PickCallback = Fn<void(not_null<PeerData*>, PickType)>;

struct PremiumGiftsDescriptor {
	std::vector<GiftTypePremium> list;
	std::shared_ptr<Api::PremiumGiftCodeOptions> api;
};

struct SessionResalePrices {
	explicit SessionResalePrices(not_null<Main::Session*> session)
	: api(std::make_unique<Api::PremiumGiftCodeOptions>(session->user())) {
	}

	std::unique_ptr<Api::PremiumGiftCodeOptions> api;
	base::flat_map<QString, int> prices;
	std::vector<Fn<void()>> waiting;
	rpl::lifetime requestLifetime;
	crl::time lastReceived = 0;
};

[[nodiscard]] not_null<SessionResalePrices*> ResalePrices(
		not_null<Main::Session*> session) {
	static auto result = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<SessionResalePrices>>();
	if (const auto i = result.find(session); i != end(result)) {
		return i->second.get();
	}
	const auto i = result.emplace(
		session,
		std::make_unique<SessionResalePrices>(session)).first;
	session->lifetime().add([session] { result.remove(session); });
	return i->second.get();
}

class PeerRow final : public PeerListRow {
public:
	using PeerListRow::PeerListRow;

	QSize rightActionSize() const override;
	QMargins rightActionMargins() const override;
	void rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

	void rightActionAddRipple(
		QPoint point,
		Fn<void()> updateCallback) override;
	void rightActionStopLastRipple() override;

private:
	std::unique_ptr<Ui::RippleAnimation> _actionRipple;

};

QSize PeerRow::rightActionSize() const {
	return QSize(
		st::inviteLinkThreeDotsIcon.width(),
		st::inviteLinkThreeDotsIcon.height());
}

QMargins PeerRow::rightActionMargins() const {
	return QMargins(
		0,
		(st::inviteLinkList.item.height - rightActionSize().height()) / 2,
		st::inviteLinkThreeDotsSkip,
		0);
}

void PeerRow::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	if (_actionRipple) {
		_actionRipple->paint(p, x, y, outerWidth);
		if (_actionRipple->empty()) {
			_actionRipple.reset();
		}
	}
	(actionSelected
		? st::inviteLinkThreeDotsIconOver
		: st::inviteLinkThreeDotsIcon).paint(p, x, y, outerWidth);
}

void PeerRow::rightActionAddRipple(QPoint point, Fn<void()> updateCallback) {
	if (!_actionRipple) {
		auto mask = Ui::RippleAnimation::EllipseMask(
			Size(st::inviteLinkThreeDotsIcon.height()));
		_actionRipple = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			std::move(mask),
			std::move(updateCallback));
	}
	_actionRipple->add(point);
}

void PeerRow::rightActionStopLastRipple() {
	if (_actionRipple) {
		_actionRipple->lastStop();
	}
}

class PreviewDelegate final : public DefaultElementDelegate {
public:
	PreviewDelegate(
		not_null<QWidget*> parent,
		not_null<ChatStyle*> st,
		Fn<void()> update);

	bool elementAnimationsPaused() override;
	not_null<PathShiftGradient*> elementPathShiftGradient() override;
	Context elementContext() override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<PathShiftGradient> _pathGradient;

};

class PreviewWrap final : public RpWidget {
public:
	PreviewWrap(
		not_null<QWidget*> parent,
		not_null<PeerData*> recipient,
		rpl::producer<GiftSendDetails> details);
	~PreviewWrap();

private:
	void paintEvent(QPaintEvent *e) override;

	void resizeTo(int width);
	void prepare(rpl::producer<GiftSendDetails> details);

	const not_null<History*> _history;
	const not_null<PeerData*> _recipient;
	const std::unique_ptr<ChatTheme> _theme;
	const std::unique_ptr<ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;
	AdminLog::OwnedItem _item;
	QPoint _position;

};

class TextBubblePart final : public MediaGenericTextPart {
public:
	TextBubblePart(
		TextWithEntities text,
		QMargins margins,
		const style::TextStyle &st = st::defaultTextStyle,
		const base::flat_map<uint16, ClickHandlerPtr> &links = {},
		const Ui::Text::MarkedContext &context = {},
		style::align align = style::al_top);

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;

private:
	void setupPen(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const override;
	int elisionLines() const override;

};

TextBubblePart::TextBubblePart(
	TextWithEntities text,
	QMargins margins,
	const style::TextStyle &st,
	const base::flat_map<uint16, ClickHandlerPtr> &links,
	const Ui::Text::MarkedContext &context,
	style::align align)
: MediaGenericTextPart(std::move(text), margins, st, links, context, align) {
}

void TextBubblePart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	p.setPen(Qt::NoPen);
	p.setBrush(context.st->msgServiceBg());
	const auto radius = height() / 2.;
	const auto left = (outerWidth - width()) / 2;
	const auto r = QRect(left, 0, width(), height());
	p.drawRoundedRect(r, radius, radius);

	MediaGenericTextPart::draw(p, owner, context, outerWidth);
}

void TextBubblePart::setupPen(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const {
	auto pen = context.st->msgServiceFg()->c;
	pen.setAlphaF(pen.alphaF() * 0.65);
	p.setPen(pen);
}

int TextBubblePart::elisionLines() const {
	return 1;
}

[[nodiscard]] bool SortForBirthday(not_null<PeerData*> peer) {
	const auto user = peer->asUser();
	if (!user) {
		return false;
	}
	const auto birthday = user->birthday();
	if (!birthday) {
		return false;
	}
	const auto is = [&](const QDate &date) {
		return (date.day() == birthday.day())
			&& (date.month() == birthday.month());
	};
	const auto now = QDate::currentDate();
	return is(now) || is(now.addDays(1)) || is(now.addDays(-1));
}

[[nodiscard]] bool IsSoldOut(const Data::StarGift &info) {
	return info.limitedCount && !info.limitedLeft;
}

struct UpgradePrice {
	TimeId date = 0;
	int stars = 0;
};

[[nodiscard]] std::vector<UpgradePrice> ParsePrices(
		const MTPVector<MTPStarGiftUpgradePrice> &list) {
	return ranges::views::all(
		list.v
	) | ranges::views::transform([](const MTPStarGiftUpgradePrice &price) {
		const auto &data = price.data();
		return UpgradePrice{
			.date = data.vdate().v,
			.stars = int(data.vupgrade_stars().v),
		};
	}) | ranges::to_vector;
}

PreviewDelegate::PreviewDelegate(
	not_null<QWidget*> parent,
	not_null<ChatStyle*> st,
	Fn<void()> update)
: _parent(parent)
, _pathGradient(MakePathShiftGradient(st, update)) {
}

bool PreviewDelegate::elementAnimationsPaused() {
	return _parent->window()->isActiveWindow();
}

auto PreviewDelegate::elementPathShiftGradient()
-> not_null<PathShiftGradient*> {
	return _pathGradient.get();
}

Context PreviewDelegate::elementContext() {
	return Context::History;
}

auto GenerateGiftMedia(
	not_null<Element*> parent,
	Element *replacing,
	not_null<PeerData*> recipient,
	const GiftSendDetails &data)
-> Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](
			not_null<MediaGeneric*> media,
			Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
		const auto &descriptor = data.descriptor;
		auto pushText = [&](
				TextWithEntities text,
				QMargins margins = {},
				const base::flat_map<uint16, ClickHandlerPtr> &links = {},
				Ui::Text::MarkedContext context = {}) {
			if (text.empty()) {
				return;
			}
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				st::defaultTextStyle,
				links,
				std::move(context)));
		};

		const auto sticker = [=] {
			using Tag = ChatHelpers::StickerLottieSize;
			const auto session = &parent->history()->session();
			const auto sticker = LookupGiftSticker(session, descriptor);
			return StickerInBubblePart::Data{
				.sticker = sticker,
				.size = st::chatIntroStickerSize,
				.cacheTag = Tag::ChatIntroHelloSticker,
				.stopOnLastFrame = v::is<GiftTypePremium>(descriptor),
			};
		};
		push(std::make_unique<StickerInBubblePart>(
			parent,
			replacing,
			sticker,
			st::giftBoxPreviewStickerPadding));
		auto title = v::match(descriptor, [&](GiftTypePremium gift) {
			return tr::lng_action_gift_premium_months(
				tr::now,
				lt_count,
				gift.months,
				tr::bold);
		}, [&](const GiftTypeStars &gift) {
			return recipient->isSelf()
				? tr::lng_action_gift_self_subtitle(tr::now, tr::bold)
				: tr::lng_action_gift_got_subtitle(
					tr::now,
					lt_user,
					TextWithEntities()
						.append(Text::SingleCustomEmoji(
							recipient->owner().customEmojiManager(
								).peerUserpicEmojiData(
									recipient->session().user())))
						.append(' ')
						.append(recipient->session().user()->shortName()),
					tr::bold);
		});
		auto textFallback = v::match(descriptor, [&](GiftTypePremium gift) {
			return tr::lng_action_gift_premium_about(
				tr::now,
				tr::rich);
		}, [&](const GiftTypeStars &gift) {
			return data.upgraded
				? tr::lng_action_gift_got_upgradable_text(tr::now, tr::rich)
				: (recipient->isSelf() && gift.info.starsToUpgrade)
				? tr::lng_action_gift_self_about_unique(tr::now, tr::rich)
				: (recipient->isBroadcast() && gift.info.starsToUpgrade)
				? tr::lng_action_gift_channel_about_unique(tr::now, tr::rich)
				: gift.info.auction()
				? (recipient->isBroadcast()
					? tr::lng_action_gift_got_gift_channel(tr::now, tr::rich)
					: tr::lng_action_gift_got_gift_text(tr::now, tr::rich))
				: (recipient->isSelf()
					? tr::lng_action_gift_self_about
					: recipient->isBroadcast()
					? tr::lng_action_gift_channel_about
					: tr::lng_action_gift_got_stars_text)(
						tr::now,
						lt_count,
						gift.info.starsConverted,
						tr::rich);
		});
		auto description = data.text.empty()
			? std::move(textFallback)
			: data.text;
		const auto context = Core::TextContext({
			.session = &parent->history()->session(),
			.repaint = [parent] { parent->repaint(); },
		});
		pushText(
			std::move(title),
			st::giftBoxPreviewTitlePadding,
			{},
			context);

		if (v::is<GiftTypeStars>(descriptor)) {
			const auto &stars = v::get<GiftTypeStars>(descriptor);
			if (const auto by = stars.info.releasedBy) {
				push(std::make_unique<TextBubblePart>(
					tr::lng_gift_released_by(
						tr::now,
						lt_name,
						tr::link('@' + by->username()),
						tr::marked),
					st::giftBoxReleasedByMargin,
					st::uniqueGiftReleasedBy.style));
			}
		}

		pushText(
			std::move(description),
			st::giftBoxPreviewTextPadding,
			{},
			context);

		push(HistoryView::MakeGenericButtonPart(
			(data.upgraded
				? tr::lng_gift_view_unpack(tr::now)
				: tr::lng_sticker_premium_view(tr::now)),
			st::giftBoxButtonMargin,
			[parent] { parent->repaint(); },
			nullptr));
	};
}

PreviewWrap::PreviewWrap(
	not_null<QWidget*> parent,
	not_null<PeerData*> recipient,
	rpl::producer<GiftSendDetails> details)
: RpWidget(parent)
, _history(recipient->owner().history(recipient->session().userPeerId()))
, _recipient(recipient)
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<ChatStyle>(
	_history->session().colorIndicesValue()))
, _delegate(std::make_unique<PreviewDelegate>(
	parent,
	_style.get(),
	[=] { update(); }))
, _position(0, st::msgMargin.bottom()) {
	_style->apply(_theme.get());

	using namespace HistoryView;
	_history->owner().viewRepaintRequest(
	) | rpl::on_next([=](not_null<const Element*> view) {
		if (view == _item.get()) {
			update();
		}
	}, lifetime());

	_history->session().downloaderTaskFinished() | rpl::on_next([=] {
		update();
	}, lifetime());

	prepare(std::move(details));
}

void ShowSentToast(
		not_null<Window::SessionController*> window,
		const GiftDescriptor &descriptor,
		const GiftSendDetails &details) {
	const auto &st = st::historyPremiumToast;
	const auto skip = st.padding.top();
	const auto size = st.style.font->height * 2;
	const auto document = LookupGiftSticker(&window->session(), descriptor);
	const auto leftSkip = document
		? (skip + size + skip - st.padding.left())
		: 0;
	auto text = v::match(descriptor, [&](const GiftTypePremium &gift) {
		return tr::lng_action_gift_premium_about(
			tr::now,
			tr::rich);
	}, [&](const GiftTypeStars &gift) {
		if (gift.info.perUserTotal && gift.info.perUserRemains < 2) {
			return tr::lng_gift_sent_finished(
				tr::now,
				lt_count,
				gift.info.perUserTotal,
				tr::rich);
		} else if (gift.info.perUserTotal) {
			return tr::lng_gift_sent_remains(
				tr::now,
				lt_count,
				gift.info.perUserRemains - 1,
				tr::rich);
		}
		const auto amount = gift.info.stars
			+ (details.upgraded ? gift.info.starsToUpgrade : 0);
		return tr::lng_gift_sent_about(
			tr::now,
			lt_count,
			amount,
			tr::rich);
	});
	const auto strong = window->showToast({
		.title = tr::lng_gift_sent_title(tr::now),
		.text = std::move(text),
		.padding = rpl::single(QMargins(leftSkip, 0, 0, 0)),
		.st = &st,
		.attach = RectPart::Top,
		.duration = kSentToastDuration,
	}).get();
	if (!strong || !document) {
		return;
	}
	const auto widget = strong->widget();
	const auto preview = CreateChild<RpWidget>(widget.get());
	preview->moveToLeft(skip, skip);
	preview->resize(size, size);
	preview->show();

	const auto bytes = document->createMediaView()->bytes();
	const auto filepath = document->filepath();
	const auto ratio = style::DevicePixelRatio();
	const auto player = preview->lifetime().make_state<Lottie::SinglePlayer>(
		Lottie::ReadContent(bytes, filepath),
		Lottie::FrameRequest{ QSize(size, size) * ratio },
		Lottie::Quality::Default);

	preview->paintRequest(
	) | rpl::on_next([=] {
		if (!player->ready()) {
			return;
		}
		const auto image = player->frame();
		QPainter(preview).drawImage(
			QRect(QPoint(), image.size() / ratio),
			image);
		if (player->frameIndex() + 1 != player->framesCount()) {
			player->markFrameShown();
		}
	}, preview->lifetime());

	player->updates(
	) | rpl::on_next([=] {
		preview->update();
	}, preview->lifetime());
}

PreviewWrap::~PreviewWrap() {
	_item = {};
}

void PreviewWrap::prepare(rpl::producer<GiftSendDetails> details) {
	std::move(details) | rpl::on_next([=](GiftSendDetails details) {
		const auto &descriptor = details.descriptor;
		const auto cost = v::match(descriptor, [&](GiftTypePremium data) {
			const auto stars = (details.byStars && data.stars)
				? data.stars
				: (data.currency == kCreditsCurrency)
				? data.cost
				: 0;
			return stars
				? tr::lng_gift_stars_title(tr::now, lt_count, stars)
				: FillAmountAndCurrency(data.cost, data.currency, true);
		}, [&](GiftTypeStars data) {
			const auto stars = data.info.stars
				+ (details.upgraded ? data.info.starsToUpgrade : 0);
			return stars
				? tr::lng_gift_stars_title(tr::now, lt_count, stars)
				: QString();
		});
		const auto name = _history->session().user()->shortName();
		const auto text = cost.isEmpty()
			? tr::lng_action_gift_unique_received(tr::now, lt_user, name)
			: _recipient->isSelf()
			? tr::lng_action_gift_self_bought(tr::now, lt_cost, cost)
			: _recipient->isBroadcast()
			? tr::lng_action_gift_sent_channel(
				tr::now,
				lt_user,
				name,
				lt_name,
				_recipient->name(),
				lt_cost,
				cost)
			: tr::lng_action_gift_received(
				tr::now,
				lt_user,
				name,
				lt_cost,
				cost);
		const auto item = _history->makeMessage({
			.id = _history->nextNonHistoryEntryId(),
			.flags = (MessageFlag::FakeAboutView
				| MessageFlag::FakeHistoryItem
				| MessageFlag::Local),
			.from = _history->peer->id,
		}, PreparedServiceText{ { text } });

		auto owned = AdminLog::OwnedItem(_delegate.get(), item);
		owned->overrideMedia(std::make_unique<MediaGeneric>(
			owned.get(),
			GenerateGiftMedia(owned.get(), _item.get(), _recipient, details),
			MediaGenericDescriptor{
				.maxWidth = st::chatGiftPreviewWidth,
				.service = true,
			}));
		_item = std::move(owned);
		if (width() >= st::msgMinWidth) {
			resizeTo(width());
		}
		update();
	}, lifetime());

	widthValue(
	) | rpl::filter([=](int width) {
		return width >= st::msgMinWidth;
	}) | rpl::on_next([=](int width) {
		resizeTo(width);
	}, lifetime());

	_history->owner().itemResizeRequest(
	) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		if (_item && item == _item->data() && width() >= st::msgMinWidth) {
			resizeTo(width());
		}
	}, lifetime());
}

void PreviewWrap::resizeTo(int width) {
	const auto height = _position.y()
		+ _item->resizeGetHeight(width)
		+ _position.y()
		+ st::msgServiceMargin.top()
		+ st::msgServiceGiftBoxTopSkip
		- st::msgServiceMargin.bottom();
	resize(width, height);
}

void PreviewWrap::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto clip = e->rect();
	if (!clip.isEmpty()) {
		p.setClipRect(clip);
		Window::SectionWidget::PaintBackground(
			p,
			_theme.get(),
			QSize(width(), window()->height()),
			clip);
	}

	auto context = _theme->preparePaintContext(
		_style.get(),
		rect(),
		e->rect(),
		!window()->isActiveWindow());
	p.translate(_position);
	_item->draw(p, context);
}

[[nodiscard]] rpl::producer<PremiumGiftsDescriptor> GiftsPremium(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer) {
	struct Session {
		PremiumGiftsDescriptor last;
	};
	static auto Map = base::flat_map<not_null<Main::Session*>, Session>();
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		auto i = Map.find(session);
		if (i == end(Map)) {
			i = Map.emplace(session, Session()).first;
			session->lifetime().add([=] { Map.remove(session); });
		}
		if (!i->second.last.list.empty()) {
			consumer.put_next_copy(i->second.last);
		}

		using namespace Api;
		const auto api = std::make_shared<PremiumGiftCodeOptions>(peer);
		api->request() | rpl::on_error_done([=](QString error) {
			consumer.put_next({});
		}, [=] {
			const auto &options = api->optionsForPeer();
			auto list = std::vector<GiftTypePremium>();
			list.reserve(options.size());
			auto minMonthsGift = GiftTypePremium();
			for (const auto &option : options) {
				if (option.currency != kCreditsCurrency) {
					list.push_back({
						.cost = option.cost,
						.currency = option.currency,
						.months = option.months,
					});
					if (!minMonthsGift.months
						|| option.months < minMonthsGift.months) {
						minMonthsGift = list.back();
					}
				}
			}
			for (const auto &option : options) {
				if (option.currency == kCreditsCurrency) {
					const auto i = ranges::find(
						list,
						option.months,
						&GiftTypePremium::months);
					if (i != end(list)) {
						i->stars = option.cost;
					}
				}
			}
			for (auto &gift : list) {
				if (gift.months > minMonthsGift.months
					&& gift.currency == minMonthsGift.currency) {
					const auto costPerMonth = gift.cost / (1. * gift.months);
					const auto maxCostPerMonth = minMonthsGift.cost
						/ (1. * minMonthsGift.months);
					const auto costRatio = costPerMonth / maxCostPerMonth;
					const auto discount = 1. - costRatio;
					const auto discountPercent = 100 * discount;
					const auto value = int(base::SafeRound(discountPercent));
					if (value > 0 && value < 100) {
						gift.discountPercent = value;
					}
				}
			}
			ranges::sort(list, ranges::less(), &GiftTypePremium::months);
			auto &map = Map[session];
			if (map.last.list != list || list.empty()) {
				map.last = PremiumGiftsDescriptor{
					std::move(list),
					api,
				};
				consumer.put_next_copy(map.last);
			}
		}, lifetime);

		return lifetime;
	};
}

[[nodiscard]] Text::String TabTextForPrice(int price) {
	const auto simple = [](const QString &text) {
		return Text::String(st::semiboldTextStyle, text);
	};
	if (price == kPriceTabAll) {
		return simple(tr::lng_gift_stars_tabs_all(tr::now));
	} else if (price == kPriceTabMy) {
		return simple(tr::lng_gift_stars_tabs_my(tr::now));
	} else if (price == kPriceTabCollectibles) {
		return simple(tr::lng_gift_stars_tabs_collectibles(tr::now));
	}
	return {};
}

struct GiftPriceTabs {
	rpl::producer<int> priceTab;
	object_ptr<RpWidget> widget;
};
[[nodiscard]] GiftPriceTabs MakeGiftsPriceTabs(
		not_null<PeerData*> peer,
		rpl::producer<std::vector<GiftTypeStars>> gifts,
		bool hasMyUnique) {
	auto widget = object_ptr<RpWidget>((QWidget*)nullptr);
	const auto raw = widget.data();

	struct Button {
		QRect geometry;
		Text::String text;
		int price = 0;
		bool active = false;
	};
	struct State {
		rpl::variable<std::vector<int>> prices;
		rpl::variable<int> priceTab = kPriceTabAll;
		rpl::variable<int> fullWidth;
		std::vector<Button> buttons;
		int dragx = 0;
		int pressx = 0;
		float64 dragscroll = 0.;
		float64 scroll = 0.;
		int scrollMax = 0;
		int tabsShift = 0;
		int selected = -1;
		int pressed = -1;
		int active = -1;
	};
	const auto user = peer->asUser();
	const auto disallowed = user
		? user->disallowedGiftTypes()
		: Api::DisallowedGiftType();
	if (disallowed & Api::DisallowedGiftType::Unique) {
		hasMyUnique = false;
	}
	const auto state = raw->lifetime().make_state<State>();
	const auto scroll = [=] {
		return QPoint(int(base::SafeRound(state->scroll)), 0)
			- QPoint(state->tabsShift, 0);
	};

	state->prices = std::move(
		gifts
	) | rpl::map([=](const std::vector<GiftTypeStars> &gifts) {
		auto result = std::vector<int>();
		result.push_back(kPriceTabAll);
		auto hasCollectibles = false;
		if (!(disallowed & Api::DisallowedGiftType::Unique)) {
			for (const auto &gift : gifts) {
				if (gift.resale
					|| (gift.info.limitedCount && gift.info.upgradable)) {
					hasCollectibles = true;
					break;
				}
			}
		}
		if (hasMyUnique && !gifts.empty()) {
			result.push_back(kPriceTabMy);
		}
		if (hasCollectibles) {
			result.push_back(kPriceTabCollectibles);
		}
		return result;
	});

	const auto setSelected = [=](int index) {
		const auto was = (state->selected >= 0);
		const auto now = (index >= 0);
		state->selected = index;
		if (was != now) {
			raw->setCursor(now ? style::cur_pointer : style::cur_default);
		}
	};
	const auto setActive = [=](int index) {
		const auto was = state->active;
		if (was == index) {
			return;
		}
		if (was >= 0 && was < state->buttons.size()) {
			state->buttons[was].active = false;
		}
		state->active = index;
		state->buttons[index].active = true;
		raw->update();

		state->priceTab = state->buttons[index].price;
	};

	state->prices.value(
	) | rpl::on_next([=](const std::vector<int> &prices) {
		auto x = st::giftBoxTabsMargin.left();
		auto y = st::giftBoxTabsMargin.top();

		setSelected(-1);
		state->buttons.resize(prices.size());
		const auto padding = st::giftBoxTabPadding;
		auto currentPrice = state->priceTab.current();
		if (!ranges::contains(prices, currentPrice)) {
			currentPrice = kPriceTabAll;
		}
		state->active = -1;
		for (auto i = 0, count = int(prices.size()); i != count; ++i) {
			const auto price = prices[i];
			auto &button = state->buttons[i];
			if (button.text.isEmpty() || button.price != price) {
				button.price = price;
				button.text = TabTextForPrice(price);
			}
			button.active = (price == currentPrice);
			if (button.active) {
				state->active = i;
			}
			const auto width = button.text.maxWidth();
			const auto height = st::giftBoxTabStyle.font->height;
			const auto r = QRect(0, 0, width, height).marginsAdded(padding);
			button.geometry = QRect(QPoint(x, y), r.size());
			x += r.width() + st::giftBoxTabSkip;
		}
		state->fullWidth = x
			- st::giftBoxTabSkip
			+ st::giftBoxTabsMargin.right();
		const auto height = state->buttons.empty()
			? 0
			: (y
				+ state->buttons.back().geometry.height()
				+ st::giftBoxTabsMargin.bottom());
		raw->resize(raw->width(), height);
		raw->update();
	}, raw->lifetime());

	rpl::combine(
		raw->widthValue(),
		state->fullWidth.value()
	) | rpl::on_next([=](int outer, int inner) {
		state->scrollMax = std::max(0, inner - outer);
		state->tabsShift = (outer - inner) / 2;
	}, raw->lifetime());

	raw->setMouseTracking(true);
	raw->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		switch (type) {
		case QEvent::Leave: setSelected(-1); break;
		case QEvent::MouseMove: {
			const auto me = static_cast<QMouseEvent*>(e.get());
			const auto mousex = me->pos().x() - state->tabsShift;
			const auto drag = QApplication::startDragDistance();
			if (state->dragx > 0) {
				state->scroll = std::clamp(
					state->dragscroll + state->dragx - mousex,
					0.,
					state->scrollMax * 1.);
				raw->update();
				break;
			} else if (state->pressx > 0
				&& std::abs(state->pressx - mousex) > drag) {
				state->dragx = state->pressx;
				state->dragscroll = state->scroll;
			}
			const auto position = me->pos() + scroll();
			for (auto i = 0, c = int(state->buttons.size()); i != c; ++i) {
				if (state->buttons[i].geometry.contains(position)) {
					setSelected(i);
					break;
				}
			}
		} break;
		case QEvent::Wheel: {
			const auto me = static_cast<QWheelEvent*>(e.get());
			state->scroll = std::clamp(
				state->scroll - ScrollDeltaF(me).x(),
				0.,
				state->scrollMax * 1.);
			raw->update();
		} break;
		case QEvent::MouseButtonPress: {
			const auto me = static_cast<QMouseEvent*>(e.get());
			if (me->button() != Qt::LeftButton) {
				break;
			}
			state->pressed = state->selected;
			state->pressx = me->pos().x() - state->tabsShift;
		} break;
		case QEvent::MouseButtonRelease: {
			const auto me = static_cast<QMouseEvent*>(e.get());
			if (me->button() != Qt::LeftButton) {
				break;
			}
			const auto dragx = std::exchange(state->dragx, 0);
			const auto pressed = std::exchange(state->pressed, -1);
			state->pressx = 0;
			if (!dragx && pressed >= 0 && state->selected == pressed) {
				setActive(pressed);
			}
		} break;
		}
	}, raw->lifetime());

	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		const auto padding = st::giftBoxTabPadding;
		const auto shift = -scroll();
		for (const auto &button : state->buttons) {
			const auto geometry = button.geometry.translated(shift);
			if (button.active) {
				p.setBrush(st::giftBoxTabBgActive);
				p.setPen(Qt::NoPen);
				const auto radius = geometry.height() / 2.;
				p.drawRoundedRect(geometry, radius, radius);
				p.setPen(st::giftBoxTabFgActive);
			} else {
				p.setPen(st::giftBoxTabFg);
			}
			button.text.draw(p, {
				.position = geometry.marginsRemoved(padding).topLeft(),
				.availableWidth = button.text.maxWidth(),
			});
		}
		{
			const auto &icon = st::defaultEmojiSuggestions;
			const auto w = icon.fadeRight.width();
			const auto &c = st::boxDividerBg->c;
			const auto r = QRect(0, 0, w, raw->height());
			const auto s = std::abs(float64(shift.x()));
			constexpr auto kF = 0.5;
			const auto opacityRight = (state->scrollMax - s)
				/ (icon.fadeRight.width() * kF);
			p.setOpacity(std::clamp(std::abs(opacityRight), 0., 1.));
			icon.fadeRight.fill(p, r.translated(raw->width() -  w, 0), c);

			const auto opacityLeft = s / (icon.fadeLeft.width() * kF);
			p.setOpacity(std::clamp(std::abs(opacityLeft), 0., 1.));
			icon.fadeLeft.fill(p, r, c);
		}
	}, raw->lifetime());

	return {
		.priceTab = state->priceTab.value(),
		.widget = std::move(widget),
	};
}

[[nodiscard]] int StarGiftMessageLimit(not_null<Main::Session*> session) {
	return session->appConfig().get<int>(
		u"stargifts_message_length_max"_q,
		255);
}

[[nodiscard]] not_null<InputField*> AddPartInput(
		not_null<Window::SessionController*> controller,
		not_null<VerticalLayout*> container,
		not_null<QWidget*> outer,
		rpl::producer<QString> placeholder,
		QString current,
		int limit) {
	const auto field = container->add(
		object_ptr<InputField>(
			container,
			st::giftBoxTextField,
			InputField::Mode::NoNewlines,
			std::move(placeholder),
			current),
		st::giftBoxTextPadding);
	field->setMaxLength(limit);
	AddLengthLimitLabel(field, limit, {
		.limitLabelTop = st::giftBoxLimitTop,
	});

	const auto toggle = CreateChild<EmojiButton>(
		container,
		st::defaultComposeFiles.emoji);
	toggle->show();
	field->geometryValue() | rpl::on_next([=](QRect r) {
		toggle->move(
			r.x() + r.width() - toggle->width(),
			r.y() - st::giftBoxEmojiToggleTop);
	}, toggle->lifetime());

	using namespace ChatHelpers;
	const auto panel = field->lifetime().make_state<TabbedPanel>(
		outer,
		controller,
		object_ptr<TabbedSelector>(
			nullptr,
			controller->uiShow(),
			Window::GifPauseReason::Layer,
			TabbedSelector::Mode::EmojiOnly));
	panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	panel->hide();
	panel->selector()->setAllowEmojiWithoutPremium(true);
	panel->selector()->emojiChosen(
	) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
		InsertEmojiAtCursor(field->textCursor(), data.emoji);
	}, field->lifetime());
	panel->selector()->customEmojiChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		Data::InsertCustomEmoji(field, data.document);
	}, field->lifetime());

	const auto updateEmojiPanelGeometry = [=] {
		const auto parent = panel->parentWidget();
		const auto global = toggle->mapToGlobal({ 0, 0 });
		const auto local = parent->mapFromGlobal(global);
		panel->moveBottomRight(
			local.y(),
			local.x() + toggle->width() * 3);
	};

	const auto filterCallback = [=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::Move || type == QEvent::Resize) {
			// updateEmojiPanelGeometry uses not only container geometry, but
			// also container children geometries that will be updated later.
			crl::on_main(field, updateEmojiPanelGeometry);
		}
		return base::EventFilterResult::Continue;
	};
	for (auto widget = (QWidget*)field, end = (QWidget*)outer->parentWidget()
		; widget && widget != end
		; widget = widget->parentWidget()) {
		base::install_event_filter(field, widget, filterCallback);
	}

	toggle->installEventFilter(panel);
	toggle->addClickHandler([=] {
		panel->toggleAnimated();
	});

	return field;
}

void SendGift(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		std::shared_ptr<Api::PremiumGiftCodeOptions> api,
		const GiftSendDetails &details,
		Fn<void(Payments::CheckoutResult)> done) {
	const auto processNonPanelPaymentFormFactory
		= Payments::ProcessNonPanelPaymentFormFactory(window, done);
	v::match(details.descriptor, [&](const GiftTypePremium &gift) {
		if (details.byStars && gift.stars) {
			auto invoice = Payments::InvoicePremiumGiftCode{
				.purpose = Payments::InvoicePremiumGiftCodeUsers{
					.users = { peer->asUser() },
					.message = details.text,
				},
				.currency = Ui::kCreditsCurrency,
				.randomId = details.randomId,
				.amount = uint64(gift.stars),
				.storeQuantity = 1,
				.users = 1,
				.months = gift.months,
			};
			Payments::CheckoutProcess::Start(
				std::move(invoice),
				done,
				processNonPanelPaymentFormFactory);
		} else {
			auto invoice = api->invoice(1, gift.months);
			invoice.purpose = Payments::InvoicePremiumGiftCodeUsers{
				.users = { peer->asUser() },
				.message = details.text,
			};
			Payments::CheckoutProcess::Start(std::move(invoice), done);
		}
	}, [&](const GiftTypeStars &gift) {
		Payments::CheckoutProcess::Start(Payments::InvoiceStarGift{
			.giftId = gift.info.id,
			.randomId = details.randomId,
			.message = details.text,
			.recipient = peer,
			.limitedCount = gift.info.limitedCount,
			.perUserLimit = gift.info.perUserTotal,
			.anonymous = details.anonymous,
			.upgraded = details.upgraded,
		}, done, processNonPanelPaymentFormFactory);
	});
}

[[nodiscard]] std::shared_ptr<Data::GiftUpgradeResult> FindUniqueGift(
		not_null<Main::Session*> session,
		const MTPUpdates &updates) {
	auto result = std::shared_ptr<Data::GiftUpgradeResult>();
	const auto checkAction = [&](const MTPDmessageService &message) {
		const auto &action = message.vaction();
		action.match([&](const MTPDmessageActionStarGiftUnique &data) {
			if (const auto gift = Api::FromTL(session, data.vgift())) {
				const auto to = data.vpeer()
					? peerFromMTP(*data.vpeer())
					: PeerId();
				const auto service = data.vfrom_id()
					&& session->data().peer(
						peerFromMTP(*data.vfrom_id()))->isServiceUser();
				const auto channel = (service && peerIsChannel(to))
					? session->data().channel(peerToChannel(to)).get()
					: nullptr;
				const auto channelSavedId = channel
					? data.vsaved_id().value_or_empty()
					: uint64();
				const auto realGiftMsgId = (peerIsUser(to) && data.vsaved_id())
					? MsgId(data.vsaved_id().value_or_empty())
					: MsgId(message.vid().v);

				result = std::make_shared<Data::GiftUpgradeResult>(
					Data::GiftUpgradeResult{
						.info = *gift,
						.manageId = (channel && channelSavedId)
							? Data::SavedStarGiftId::Chat(
								channel,
								channelSavedId)
							: Data::SavedStarGiftId::User(realGiftMsgId),
						.date = message.vdate().v,
						.starsForDetailsRemove = int(
							data.vdrop_original_details_stars(
							).value_or_empty()),
						.saved = data.is_saved(),
					});
			}
		}, [](const auto &) {});
	};
	updates.match([&](const MTPDupdates &data) {
		for (const auto &update : data.vupdates().v) {
			update.match([&](const MTPDupdateNewMessage &data) {
				data.vmessage().match([&](const MTPDmessageService &data) {
					checkAction(data);
				}, [](const auto &) {});
			}, [](const auto &) {});
		}
	}, [](const auto &) {});
	return result;
}

void ShowGiftUpgradedToast(
		not_null<Window::SessionController*> window,
		not_null<Main::Session*> session,
		std::shared_ptr<Data::UniqueGift> gift) {
	Expects(gift != nullptr);

	window->showToast({
		.title = tr::lng_gift_upgraded_title(tr::now),
		.text = tr::lng_gift_upgraded_about(
			tr::now,
			lt_name,
			tr::bold(Data::UniqueGiftName(*gift)),
			tr::marked),
		.duration = kUpgradeDoneToastDuration,
	});
}

void ShowUpgradeGiftedToast(
		base::weak_ptr<Window::SessionController> weak,
		not_null<PeerData*> peer) {
	if (const auto strong = weak.get()) {
		strong->showToast({
			.title = tr::lng_gift_upgrade_gifted_title(tr::now),
			.text = { (peer->isBroadcast()
				? tr::lng_gift_upgrade_gifted_about_channel
				: tr::lng_gift_upgrade_gifted_about)(
					tr::now,
					lt_name,
					peer->shortName()) },
			.duration = kUpgradeDoneToastDuration,
		});
	}
}

void SendStarsFormRequest(
		std::shared_ptr<Main::SessionShow> show,
		Settings::SmallBalanceResult result,
		uint64 formId,
		MTPInputInvoice invoice,
		Fn<void(Payments::CheckoutResult, const MTPUpdates *)> done) {
	using BalanceResult = Settings::SmallBalanceResult;
	const auto session = &show->session();
	if (result == BalanceResult::Success
		|| result == BalanceResult::Already) {
		session->api().request(MTPpayments_SendStarsForm(
			MTP_long(formId),
			invoice
		)).done([=](const MTPpayments_PaymentResult &result) {
			result.match([&](const MTPDpayments_paymentResult &data) {
				session->api().applyUpdates(data.vupdates());
				session->credits().tonLoad(true);
				session->credits().load(true);
				done(Payments::CheckoutResult::Paid, &data.vupdates());
			}, [&](const MTPDpayments_paymentVerificationNeeded &data) {
				done(Payments::CheckoutResult::Failed, nullptr);
			});
		}).fail([=](const MTP::Error &error) {
			show->showToast(error.type());
			done(Payments::CheckoutResult::Failed, nullptr);
		}).send();
	} else if (result == BalanceResult::Cancelled) {
		done(Payments::CheckoutResult::Cancelled, nullptr);
	} else {
		done(Payments::CheckoutResult::Failed, nullptr);
	}
}

void UpgradeGift(
		not_null<Window::SessionController*> window,
		Data::SavedStarGiftId savedId,
		bool keepDetails,
		int stars,
		Fn<void(bool, std::shared_ptr<Data::GiftUpgradeResult>)> done) {
	//AssertIsDebug(); return;
	const auto session = &window->session();
	const auto weak = base::make_weak(window);
	auto formDone = [=](
			Payments::CheckoutResult result,
			const MTPUpdates *updates) {
		if (result != Payments::CheckoutResult::Paid) {
			done(false, nullptr);
			return;
		}
		const auto owner = savedId.isUser()
			? session->user().get()
			: savedId.chat();
		if (owner) {
			owner->owner().nextForUpgradeGiftInvalidate(owner);
		}
		const auto gift = updates
			? FindUniqueGift(session, *updates)
			: nullptr;
		done(true, gift);
	};
	if (stars <= 0) {
		using Flag = MTPpayments_UpgradeStarGift::Flag;
		session->api().request(MTPpayments_UpgradeStarGift(
			MTP_flags(keepDetails ? Flag::f_keep_original_details : Flag()),
			Api::InputSavedStarGiftId(savedId)
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
			formDone(Payments::CheckoutResult::Paid, &result);
		}).fail([=](const MTP::Error &error) {
			if (const auto strong = weak.get()) {
				strong->showToast(error.type());
			}
			formDone(Payments::CheckoutResult::Failed, nullptr);
		}).send();
		return;
	}
	using Flag = MTPDinputInvoiceStarGiftUpgrade::Flag;
	RequestStarsFormAndSubmit(
		window->uiShow(),
		MTP_inputInvoiceStarGiftUpgrade(
			MTP_flags(keepDetails ? Flag::f_keep_original_details : Flag()),
			Api::InputSavedStarGiftId(savedId)),
		std::move(formDone));
}

void GiftUpgrade(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		QString giftPrepayUpgradeHash,
		int stars,
		Fn<void(bool)> done) {
	const auto weak = base::make_weak(window);
	auto formDone = [=](
			Payments::CheckoutResult result,
			const MTPUpdates *updates) {
		const auto success = (result == Payments::CheckoutResult::Paid);
		if (success) {
			ShowUpgradeGiftedToast(weak, peer);
		}
		done(success);
	};
	RequestStarsFormAndSubmit(
		window->uiShow(),
		MTP_inputInvoiceStarGiftPrepaidUpgrade(
			peer->input(),
			MTP_string(giftPrepayUpgradeHash)),
		std::move(formDone));
}

void SoldOutBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> window,
		const GiftTypeStars &gift) {
	Settings::ReceiptCreditsBox(
		box,
		window,
		Data::CreditsHistoryEntry{
			.firstSaleDate = base::unixtime::parse(gift.info.firstSaleDate),
			.lastSaleDate = base::unixtime::parse(gift.info.lastSaleDate),
			.credits = CreditsAmount(gift.info.stars),
			.bareGiftStickerId = gift.info.document->id,
			.peerType = Data::CreditsHistoryEntry::PeerType::Peer,
			.limitedCount = gift.info.limitedCount,
			.limitedLeft = gift.info.limitedLeft,
			.soldOutInfo = true,
			.gift = true,
		},
		Data::SubscriptionEntry());
}

void AddUpgradeButton(
		not_null<Ui::VerticalLayout*> container,
		int cost,
		not_null<PeerData*> peer,
		Fn<void(bool)> toggled,
		Fn<void()> preview) {
	const auto button = container->add(
		object_ptr<SettingsButton>(
			container,
			rpl::single(QString()),
			st::settingsButtonNoIcon));
	button->toggleOn(rpl::single(false))->toggledValue(
	) | rpl::on_next(toggled, button->lifetime());

	auto helper = Ui::Text::CustomEmojiHelper();
	auto star = helper.paletteDependent(Ui::Earn::IconCreditsEmoji());
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		tr::lng_gift_send_unique(
			lt_price,
			rpl::single(star.append(' '
				+ Lang::FormatCreditsAmountDecimal(
					CreditsAmount{ cost }))),
			tr::marked),
		st::boxLabel,
		st::defaultPopupMenu,
		helper.context());
	label->show();
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->widthValue() | rpl::on_next([=](int outer) {
		const auto padding = st::settingsButtonNoIcon.padding;
		const auto inner = outer
			- padding.left()
			- padding.right()
			- st::settingsButtonNoIcon.toggleSkip
			- 2 * st::settingsButtonNoIcon.toggle.border
			- 2 * st::settingsButtonNoIcon.toggle.diameter
			- 2 * st::settingsButtonNoIcon.toggle.width;
		label->resizeToWidth(inner);
		label->moveToLeft(padding.left(), padding.top(), outer);
	}, label->lifetime());

	AddSkip(container);
	const auto about = AddDividerText(
		container,
		(peer->isBroadcast()
			? tr::lng_gift_send_unique_about_channel(
				lt_name,
				rpl::single(TextWithEntities{ peer->name() }),
				lt_link,
				tr::lng_gift_send_unique_link(tr::link),
				tr::marked)
			: tr::lng_gift_send_unique_about(
				lt_user,
				rpl::single(TextWithEntities{ peer->shortName() }),
				lt_link,
				tr::lng_gift_send_unique_link(tr::link),
				tr::marked)));
	about->setClickHandlerFilter([=](const auto &...) {
		preview();
		return false;
	});
}

void AddSoldLeftSlider(
		not_null<RpWidget*> above,
		const GiftTypeStars &gift,
		QMargins added = {}) {
	const auto still = gift.info.limitedLeft;
	const auto total = gift.info.limitedCount;
	const auto slider = CreateChild<RpWidget>(above->parentWidget());
	struct State {
		Text::String still;
		Text::String sold;
		int height = 0;
	};
	const auto state = slider->lifetime().make_state<State>();
	const auto sold = total - still;
	state->still.setText(
		st::semiboldTextStyle,
		tr::lng_gift_send_limited_left(tr::now, lt_count_decimal, still));
	state->sold.setText(
		st::semiboldTextStyle,
		tr::lng_gift_send_limited_sold(tr::now, lt_count_decimal, sold));
	state->height = st::giftLimitedPadding.top()
		+ st::semiboldFont->height
		+ st::giftLimitedPadding.bottom();
	above->geometryValue() | rpl::on_next([=](QRect geometry) {
		const auto space = st::giftLimitedBox.buttonPadding.top();
		const auto skip = (space - state->height) / 2;
		slider->setGeometry(
			geometry.x() + added.left(),
			geometry.y() - skip - state->height,
			geometry.width() - added.left() - added.right(),
			state->height);
	}, slider->lifetime());
	slider->paintRequest() | rpl::on_next([=] {
		const auto &padding = st::giftLimitedPadding;
		const auto left = (padding.left() * 2) + state->still.maxWidth();
		const auto right = (padding.right() * 2) + state->sold.maxWidth();
		const auto space = slider->width() - left - right;
		if (space <= 0) {
			return;
		}
		const auto edge = left + ((space * still) / total);

		const auto radius = st::buttonRadius;
		auto p = QPainter(slider);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			edge - (radius * 3),
			0,
			slider->width() - (edge - (radius * 3)),
			state->height,
			radius,
			radius);
		p.setBrush(st::windowBgActive);
		p.drawRoundedRect(0, 0, edge, state->height, radius, radius);

		p.setPen(st::windowFgActive);
		state->still.draw(p, {
			.position = { padding.left(), padding.top() },
			.availableWidth = left,
		});
		p.setPen(st::windowSubTextFg);
		state->sold.draw(p, {
			.position = { left + space + padding.right(), padding.top() },
			.availableWidth = right,
		});
	}, slider->lifetime());
}

void CheckMaybeGiftLocked(
		not_null<Window::SessionController*> window,
		uint64 giftId,
		Fn<void()> send) {
	const auto session = &window->session();
	session->api().request(MTPpayments_CheckCanSendGift(
		MTP_long(giftId)
	)).done(crl::guard(window, [=](
			const MTPpayments_CheckCanSendGiftResult &result) {
		result.match([&](const MTPDpayments_checkCanSendGiftResultOk &) {
			send();
		}, [&](const MTPDpayments_checkCanSendGiftResultFail &data) {
			window->show(Ui::MakeInformBox({
				.text = Api::ParseTextWithEntities(session, data.vreason()),
				.title = tr::lng_gift_locked_title(),
			}));
		});
	})).fail(crl::guard(window, [=] {
	})).send();
}

[[nodiscard]] object_ptr<RpWidget> MakeGiftsList(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		rpl::producer<GiftsDescriptor> gifts,
		Fn<void()> loadMore) {
	auto result = object_ptr<VisibleRangeWidget>((QWidget*)nullptr);
	const auto raw = result.data();

	Data::AmPremiumValue(&window->session()) | rpl::on_next([=] {
		raw->update();
	}, raw->lifetime());

	struct State {
		Delegate delegate;
		std::vector<int> order;
		std::vector<bool> validated;
		std::vector<GiftDescriptor> list;
		std::vector<std::unique_ptr<GiftButton>> buttons;
		std::shared_ptr<Api::PremiumGiftCodeOptions> api;
		std::shared_ptr<Data::UniqueGift> transferRequested;
		rpl::variable<VisibleRange> visibleRange;
		uint64 resaleRequestingId = 0;
		rpl::lifetime resaleLifetime;
		bool sending = false;
		int perRow = 1;
	};
	const auto state = raw->lifetime().make_state<State>(State{
		.delegate = Delegate(&window->session(), GiftButtonMode::Full),
	});
	const auto single = state->delegate.buttonSize();
	const auto shadow = st::defaultDropdownMenu.wrap.shadow;
	const auto extend = shadow.extend;

	auto &packs = window->session().giftBoxStickersPacks();
	packs.updated() | rpl::on_next([=] {
		for (const auto &button : state->buttons) {
			if (const auto raw = button.get()) {
				raw->update();
			}
		}
	}, raw->lifetime());

	const auto rebuild = [=] {
		const auto width = st::boxWideWidth;
		const auto padding = st::giftBoxPadding;
		const auto available = width - padding.left() - padding.right();
		const auto range = state->visibleRange.current();
		const auto count = int(state->list.size());

		auto &buttons = state->buttons;
		if (buttons.size() < count) {
			buttons.resize(count);
		}
		auto &validated = state->validated;
		validated.resize(count);

		auto x = padding.left();
		auto y = padding.top();
		const auto perRow = state->perRow;
		const auto singlew = single.width() + st::giftBoxGiftSkip.x();
		const auto singleh = single.height() + st::giftBoxGiftSkip.y();
		const auto rowFrom = std::max(range.top - y, 0) / singleh;
		const auto rowTill = (std::max(range.bottom - y + st::giftBoxGiftSkip.y(), 0) + singleh - 1)
			/ singleh;
		Assert(rowTill >= rowFrom);
		const auto first = rowFrom * perRow;
		const auto last = std::min(rowTill * perRow, count);
		auto checkedFrom = 0;
		auto checkedTill = int(buttons.size());
		const auto ensureButton = [&](int index) {
			auto &button = buttons[index];
			if (!button) {
				validated[index] = false;
				for (; checkedFrom != first; ++checkedFrom) {
					if (buttons[checkedFrom]) {
						button = std::move(buttons[checkedFrom]);
						break;
					}
				}
			}
			if (!button) {
				for (; checkedTill != last; ) {
					--checkedTill;
					if (buttons[checkedTill]) {
						button = std::move(buttons[checkedTill]);
						break;
					}
				}
			}
			if (!button) {
				button = std::make_unique<GiftButton>(raw, &state->delegate);
			}
			const auto raw = button.get();
			if (validated[index]) {
				return;
			}
			raw->show();
			validated[index] = true;
			const auto &descriptor = state->list[state->order[index]];
			raw->setDescriptor(descriptor, GiftButton::Mode::Full);
			raw->setClickedCallback([=] {
				const auto star = std::get_if<GiftTypeStars>(&descriptor);
				const auto send = crl::guard(raw, [=] {
					window->show(Box(
						SendGiftBox,
						window,
						peer,
						state->api,
						descriptor,
						nullptr));
				});
				const auto unique = star ? star->info.unique : nullptr;
				const auto premiumNeeded = star && star->info.requirePremium;
				if (unique && star->resale) {
					window->show(Box(
						Settings::GlobalStarGiftBox,
						window->uiShow(),
						star->info,
						Settings::StarGiftResaleInfo{
							.recipientId = peer->id,
							.forceTon = star->forceTon,
						},
						Settings::CreditsEntryBoxStyleOverrides()));
				} else if (unique && star->mine && !peer->isSelf()) {
					if (ShowTransferGiftLater(window->uiShow(), unique)) {
						return;
					}
					const auto done = [=] {
						window->session().credits().load(true);
						window->showPeerHistory(peer);
					};
					if (state->transferRequested == unique) {
						return;
					}
					state->transferRequested = unique;
					const auto savedId = star->transferId;
					using Payments::CheckoutResult;
					const auto formReady = [=](
							uint64 formId,
							CreditsAmount price,
							std::optional<CheckoutResult> failure) {
						state->transferRequested = nullptr;
						if (!failure && !price.stars()) {
							LOG(("API Error: "
								"Bad transfer invoice currenct."));
						} else if (!failure
							|| *failure == CheckoutResult::Free) {
							unique->starsForTransfer = failure
								? 0
								: price.whole();
							ShowTransferToBox(
								window,
								peer,
								unique,
								savedId,
								done);
						} else if (*failure == CheckoutResult::Cancelled) {
							done();
						}
					};
					RequestOurForm(
						window->uiShow(),
						MTP_inputInvoiceStarGiftTransfer(
							Api::InputSavedStarGiftId(savedId, unique),
							peer->input()),
						formReady);
				} else if (star && star->resale) {
					const auto id = star->info.id;
					if (state->resaleRequestingId == id) {
						return;
					}
					state->resaleRequestingId = id;
					state->resaleLifetime = ShowStarGiftResale(
						window,
						peer,
						id,
						star->info.resellTitle,
						[=] { state->resaleRequestingId = 0; });
				} else if (star && star->info.auction()) {
					if (!IsSoldOut(star->info)
						&& premiumNeeded
						&& !peer->session().premium()) {
						Settings::ShowPremiumGiftPremium(window, star->info);
					} else {
						const auto id = star->info.id;
						if (state->resaleRequestingId == id) {
							return;
						}
						state->resaleRequestingId = id;
						state->resaleLifetime = ShowStarGiftAuction(
							window,
							peer,
							star->info.auctionSlug,
							[=] { state->resaleRequestingId = 0; },
							crl::guard(raw, [=] {
								state->resaleLifetime.destroy();
							}));
					}
				} else if (star && IsSoldOut(star->info)) {
					window->show(Box(SoldOutBox, window, *star));
				} else if (premiumNeeded && !peer->session().premium()) {
					Settings::ShowPremiumGiftPremium(window, star->info);
				} else if (star
					&& star->info.lockedUntilDate
					&& star->info.lockedUntilDate > base::unixtime::now()) {
					const auto ready = crl::guard(raw, [=] {
						if (premiumNeeded && !peer->session().premium()) {
							Settings::ShowPremiumGiftPremium(
								window,
								v::get<GiftTypeStars>(descriptor).info);
						} else {
							send();
						}
					});
					CheckMaybeGiftLocked(window, star->info.id, ready);
				} else if (star
						&& star->info.perUserTotal
						&& !star->info.perUserRemains) {
					window->showToast({
						.text = tr::lng_gift_sent_finished(
							tr::now,
							lt_count,
							star->info.perUserTotal,
							tr::rich),
					});
				} else {
					send();
				}
			});
			raw->setGeometry(QRect(QPoint(x, y), single), extend);
		};
		y += rowFrom * singleh;
		for (auto row = rowFrom; row != rowTill; ++row) {
			for (auto col = 0; col != perRow; ++col) {
				const auto index = row * perRow + col;
				if (index >= count) {
					break;
				}
				const auto last = !((col + 1) % perRow);
				if (last) {
					x = padding.left() + available - single.width();
				}
				ensureButton(index);
				if (last) {
					x = padding.left();
					y += singleh;
				} else {
					x += singlew;
				}
			}
		}
		const auto till = std::min(int(buttons.size()), rowTill * perRow);
		for (auto i = count; i < till; ++i) {
			if (const auto button = buttons[i].get()) {
				button->hide();
			}
		}

		const auto page = range.bottom - range.top;
		if (loadMore && page > 0 && range.bottom + page > raw->height()) {
			loadMore();
		}
	};

	state->visibleRange = raw->visibleRange();
	state->visibleRange.value(
	) | rpl::on_next(rebuild, raw->lifetime());

	std::move(
		gifts
	) | rpl::on_next([=](const GiftsDescriptor &gifts) {
		const auto width = st::boxWideWidth;
		const auto padding = st::giftBoxPadding;
		const auto available = width - padding.left() - padding.right();
		state->perRow = available / single.width();
		state->list = std::move(gifts.list);
		state->api = gifts.api;

		const auto count = int(state->list.size());
		state->order = ranges::views::ints
			| ranges::views::take(count)
			| ranges::to_vector;
		state->validated.clear();

		if (SortForBirthday(peer)) {
			ranges::stable_partition(state->order, [&](int i) {
				const auto &gift = state->list[i];
				const auto stars = std::get_if<GiftTypeStars>(&gift);
				return stars && stars->info.birthday && !stars->info.unique;
			});
		}

		const auto rows = (count + state->perRow - 1) / state->perRow;
		const auto height = padding.top()
			+ (rows * single.height())
			+ ((rows - 1) * st::giftBoxGiftSkip.y())
			+ padding.bottom();
		raw->resize(raw->width(), height);
		rebuild();
	}, raw->lifetime());

	return result;
}

void FillBg(not_null<RpWidget*> box) {
	box->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(box);
		auto hq = PainterHighQualityEnabler(p);

		const auto radius = st::boxRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxDividerBg);
		p.drawRoundedRect(
			box->rect().marginsAdded({ 0, 0, 0, 2 * radius }),
			radius,
			radius);
	}, box->lifetime());
}

struct AddBlockArgs {
	rpl::producer<QString> subtitle;
	rpl::producer<TextWithEntities> about;
	Fn<bool(const ClickHandlerPtr&, Qt::MouseButton)> aboutFilter;
	object_ptr<RpWidget> content;
};

void AddBlock(
		not_null<VerticalLayout*> content,
		not_null<Window::SessionController*> window,
		AddBlockArgs &&args) {
	content->add(
		object_ptr<FlatLabel>(
			content,
			std::move(args.subtitle),
			st::giftBoxSubtitle),
		st::giftBoxSubtitleMargin,
		style::al_top);
	const auto about = content->add(
		object_ptr<FlatLabel>(
			content,
			std::move(args.about),
			st::giftBoxAbout),
		st::giftBoxAboutMargin,
		style::al_top);
	about->setClickHandlerFilter(std::move(args.aboutFilter));
	content->add(std::move(args.content));
}

[[nodiscard]] object_ptr<RpWidget> MakePremiumGifts(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer) {
	struct State {
		rpl::variable<PremiumGiftsDescriptor> gifts;
	};
	auto state = std::make_unique<State>();

	state->gifts = GiftsPremium(&window->session(), peer);

	auto result = MakeGiftsList(window, peer, state->gifts.value(
	) | rpl::map([=](const PremiumGiftsDescriptor &gifts) {
		return GiftsDescriptor{
			gifts.list | ranges::to<std::vector<GiftDescriptor>>,
			gifts.api,
		};
	}), nullptr);
	result->lifetime().add([state = std::move(state)] {});
	return result;
}

[[nodiscard]] object_ptr<RpWidget> MakeStarsGifts(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		MyGiftsDescriptor my,
		Fn<void(int)> tabSelected) {
	auto result = object_ptr<VerticalLayout>((QWidget*)nullptr);

	struct State {
		rpl::variable<std::vector<GiftTypeStars>> gifts;
		rpl::variable<int> priceTab = kPriceTabAll;
		rpl::event_stream<> myUpdated;
		MyGiftsDescriptor my;
		rpl::lifetime myLoading;
	};
	const auto state = result->lifetime().make_state<State>();
	state->my = std::move(my);

	state->gifts = GiftsStars(&window->session(), peer);

	auto tabs = MakeGiftsPriceTabs(
		peer,
		state->gifts.value(),
		!state->my.list.empty() && !peer->isSelf());
	state->priceTab = std::move(tabs.priceTab);
	state->priceTab.changes() | rpl::on_next([=](int tab) {
		tabSelected(tab);
	}, tabs.widget->lifetime());
	result->add(std::move(tabs.widget));
	result->add(MakeGiftsList(window, peer, rpl::combine(
		state->gifts.value(),
		state->priceTab.value(),
		rpl::single(rpl::empty) | rpl::then(state->myUpdated.events())
	) | rpl::map([=](std::vector<GiftTypeStars> &&gifts, int price, auto) {
		if (price == kPriceTabMy) {
			gifts.clear();
			for (const auto &gift : state->my.list) {
				gifts.push_back({
					.transferId = gift.manageId,
					.info = gift.info,
					.mine = true,
				});
			}
		} else {
			// First, gather information about which gifts are available on resale
			base::flat_set<uint64> resaleGiftIds;
			if (price != kPriceTabCollectibles) {
				// Only need this info when not viewing the resale tab
				for (const auto &gift : gifts) {
					if (gift.resale) {
						resaleGiftIds.insert(gift.info.id);
					}
				}
			}

			const auto pred = [&](const GiftTypeStars &gift) {
				// Skip sold out gifts if they're available on resale
				// (unless we're specifically viewing resale gifts)
				if (price != kPriceTabCollectibles
					&& IsSoldOut(gift.info)
					&& !gift.resale
					&& resaleGiftIds.contains(gift.info.id)) {
					return true; // Remove this gift
				} else if ((price != kPriceTabCollectibles) || gift.resale) {
					return false;
				}
				return !gift.info.limitedCount
					|| !gift.info.upgradable
					|| IsSoldOut(gift.info);
			};
			gifts.erase(ranges::remove_if(gifts, pred), end(gifts));
		}
		return GiftsDescriptor{
			gifts | ranges::to<std::vector<GiftDescriptor>>(),
		};
	}), [=] {
		if (state->priceTab.current() == kPriceTabMy
			&& !state->my.offset.isEmpty()
			&& !state->myLoading) {
			state->myLoading = Data::MyUniqueGiftsSlice(
				&peer->session(),
				Data::MyUniqueType::OnlyOwned,
				state->my.offset
			) | rpl::on_next([=](MyGiftsDescriptor &&descriptor) {
				state->myLoading.destroy();
				state->my.offset = descriptor.list.empty()
					? QString()
					: descriptor.offset;
				state->my.list.insert(
					end(state->my.list),
					std::make_move_iterator(begin(descriptor.list)),
					std::make_move_iterator(end(descriptor.list)));
				state->myUpdated.fire({});
			});
		}
	}));

	return result;
}

void GiftBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		MyGiftsDescriptor my) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::creditsGiftBox);
	box->setNoContentMargin(true);
	box->setCustomCornersFilling(RectPart::FullTop);
	box->addButton(tr::lng_create_group_back(), [=] { box->closeBox(); });

	window->session().credits().load();

	FillBg(box);

	const auto &stUser = st::premiumGiftsUserpicButton;
	const auto content = box->verticalLayout();

	AddSkip(content, st::defaultVerticalListSkip * 5);

	// Check disallowed gift types
	const auto user = peer->asUser();
	using Type = Api::DisallowedGiftType;
	const auto disallowedTypes = user
		? user->disallowedGiftTypes()
		: Type::Premium;
	const auto premiumDisallowed = peer->isSelf()
		|| (disallowedTypes & Type::Premium);
	const auto limitedDisallowed = !peer->isSelf()
		&& (disallowedTypes & Type::Limited);
	const auto unlimitedDisallowed = !peer->isSelf()
		&& (disallowedTypes & Type::Unlimited);
	const auto uniqueDisallowed = !peer->isSelf()
		&& (disallowedTypes & Type::Unique);
	const auto allStarsDisallowed = limitedDisallowed
		&& unlimitedDisallowed
		&& uniqueDisallowed;

	content->add(
		object_ptr<UserpicButton>(content, peer, stUser),
		style::al_top
	)->setClickedCallback([=] { window->showPeerInfo(peer); });
	AddSkip(content);
	AddSkip(content);

	Settings::AddMiniStars(
		content,
		CreateChild<RpWidget>(content),
		stUser.photoSize,
		box->width(),
		2.);
	AddSkip(content);
	AddSkip(box->verticalLayout());

	const auto starsClickHandlerFilter = [=](const auto &...) {
		window->showSettings(Settings::CreditsId());
		return false;
	};
	if (peer->isUser() && !peer->isSelf() && !premiumDisallowed) {
		const auto premiumClickHandlerFilter = [=](const auto &...) {
			Settings::ShowPremium(window, u"gift_send"_q);
			return false;
		};

		AddBlock(content, window, {
			.subtitle = tr::lng_gift_premium_subtitle(),
			.about = tr::lng_gift_premium_about(
				lt_name,
				rpl::single(tr::bold(peer->shortName())),
				lt_features,
				tr::lng_gift_premium_features(tr::link),
				tr::marked),
			.aboutFilter = premiumClickHandlerFilter,
			.content = MakePremiumGifts(window, peer),
		});
	}

	// Only add star gifts if at least one type is allowed
	if (!allStarsDisallowed) {
		const auto collectibles = content->lifetime().make_state<
			rpl::variable<bool>
		>();
		auto tabSelected = [=](int tab) {
			*collectibles = (tab == kPriceTabCollectibles);
		};
		AddBlock(content, window, {
			.subtitle = (peer->isSelf()
				? tr::lng_gift_self_title()
				: peer->isBroadcast()
				? tr::lng_gift_channel_title()
				: tr::lng_gift_stars_subtitle()),
			.about = (peer->isSelf()
				? tr::lng_gift_self_about(tr::marked)
				: peer->isBroadcast()
				? tr::lng_gift_channel_about(
					lt_name,
					rpl::single(tr::bold(peer->name())),
					tr::marked)
				: rpl::conditional(
					collectibles->value(),
					tr::lng_gift_stars_about_collectibles(
						lt_link,
						tr::lng_gift_stars_link(tr::link),
						tr::marked),
					tr::lng_gift_stars_about(
						lt_name,
						rpl::single(tr::bold(peer->shortName())),
						lt_link,
						tr::lng_gift_stars_link(tr::link),
						tr::marked))),
			.aboutFilter = starsClickHandlerFilter,
			.content = MakeStarsGifts(
				window,
				peer,
				std::move(my),
				std::move(tabSelected)),
		});
	}
}

[[nodiscard]] base::unique_qptr<Ui::PopupMenu> CreateRowContextMenu(
		QWidget *parent,
		not_null<PeerData*> peer,
		PickCallback pick) {
	auto result = base::make_unique_q<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	result->addAction(
		tr::lng_context_send_message(tr::now),
		[=] { pick(peer, PickType::SendMessage); },
		&st::menuIconChatBubble);
	result->addAction(
		tr::lng_context_view_profile(tr::now),
		[=] { pick(peer, PickType::OpenProfile); },
		&st::menuIconProfile);
	return result;
}

struct CustomList {
	object_ptr<Ui::RpWidget> content = { nullptr };
	Fn<bool(int, int, int)> overrideKey;
	Fn<void()> activate;
	Fn<bool()> hasSelection;
};

class Controller final : public ContactsBoxController {
public:
	Controller(not_null<Main::Session*> session, PickCallback pick);

	void noSearchSubmit();

	bool overrideKeyboardNavigation(
		int direction,
		int fromIndex,
		int toIndex) override final;

	void rowRightActionClicked(not_null<PeerListRow*> row) override final;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override final;

private:
	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

	void prepareViewHook() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	const PickCallback _pick;
	const std::vector<UserId> _contactBirthdays;
	CustomList _selfOption;
	CustomList _birthdayOptions;

	base::unique_qptr<Ui::PopupMenu> _menu;

	bool _skipUpDirectionSelect = false;

};

[[nodiscard]] CustomList MakeCustomList(
		not_null<Main::Session*> session,
		Fn<void(not_null<PeerListController*>)> fill,
		PickCallback pick,
		rpl::producer<QString> below) {
	class CustomController final : public PeerListController {
	public:
		CustomController(
			not_null<Main::Session*> session,
			Fn<void(not_null<PeerListController*>)> fill,
			PickCallback pick)
		: _session(session)
		, _pick(std::move(pick))
		, _fill(std::move(fill)) {
		}

		void prepare() override {
			if (_fill) {
				_fill(this);
			}
		}
		void loadMoreRows() override {
		}
		void rowClicked(not_null<PeerListRow*> row) override {
			_pick(row->peer(), PickType::Activate);
		}
		Main::Session &session() const override {
			return *_session;
		}

		void rowRightActionClicked(not_null<PeerListRow*> row) override {
			delegate()->peerListShowRowMenu(row, true);
		}

		base::unique_qptr<Ui::PopupMenu> rowContextMenu(
				QWidget *parent,
				not_null<PeerListRow*> row) override {
			auto result = CreateRowContextMenu(parent, row->peer(), _pick);

			if (result) {
				base::take(_menu);

				_menu = base::unique_qptr<Ui::PopupMenu>(result.get());
			}

			return result;
		}

	private:
		const not_null<Main::Session*> _session;
		PickCallback _pick;
		Fn<void(not_null<PeerListController*>)> _fill;

		base::unique_qptr<Ui::PopupMenu> _menu;

	};

	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();

	Ui::AddSkip(container);

	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller
		= container->lifetime().make_state<CustomController>(
			session,
			fill,
			pick);

	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	if (below) {
		Ui::AddSkip(container);
		container->add(CreatePeerListSectionSubtitle(
			container,
			std::move(below)));
	}

	const auto overrideKey = [=](int direction, int from, int to) {
		if (!content->isVisible()) {
			return false;
		} else if (direction > 0 && from < 0 && to >= 0) {
			if (content->hasSelection()) {
				const auto was = content->selectedIndex();
				const auto now = content->selectSkip(1).reallyMovedTo;
				if (was != now) {
					return true;
				}
				content->clearSelection();
			} else {
				content->selectSkip(1);
				return true;
			}
		} else if (direction < 0 && to < 0) {
			if (!content->hasSelection()) {
				content->selectLast();
			} else if (from >= 0 || content->hasSelection()) {
				content->selectSkip(-1);
			}
		}
		return false;
	};
	const auto hasSelection = [=] {
		return content->isVisible() && content->hasSelection();
	};

	return {
		.content = std::move(result),
		.overrideKey = overrideKey,
		.activate = [=] {
			if (content->hasSelection()) {
				pick(
					content->rowAt(content->selectedIndex())->peer(),
					PickType::Activate);
			}
		},
		.hasSelection = hasSelection,
	};
}

Controller::Controller(not_null<Main::Session*> session, PickCallback pick)
: ContactsBoxController(session)
, _pick(std::move(pick))
, _contactBirthdays(
	session->promoSuggestions().knownContactBirthdays().value_or(
		std::vector<UserId>{}))
, _selfOption(
	MakeCustomList(
		session,
		[=](not_null<PeerListController*> controller) {
			auto row = std::make_unique<PeerListRow>(session->user());
			row->setCustomStatus(tr::lng_gift_self_status(tr::now));
			controller->delegate()->peerListAppendRow(std::move(row));
			controller->delegate()->peerListRefreshRows();
		},
		_pick,
		_contactBirthdays.empty()
			? tr::lng_contacts_header()
			: tr::lng_gift_subtitle_birthdays()))
, _birthdayOptions(
	MakeCustomList(
		session,
		[=](not_null<PeerListController*> controller) {
			const auto status = [=](const Data::Birthday &date) {
				if (Data::IsBirthdayToday(date)) {
					return tr::lng_gift_list_birthday_status_today(
						tr::now,
						lt_emoji,
						Data::BirthdayCake());
				}
				const auto yesterday = QDate::currentDate().addDays(-1);
				const auto tomorrow = QDate::currentDate().addDays(1);
				if (date.day() == yesterday.day()
						&& date.month() == yesterday.month()) {
					return tr::lng_gift_list_birthday_status_yesterday(
						tr::now);
				} else if (date.day() == tomorrow.day()
						&& date.month() == tomorrow.month()) {
					return tr::lng_gift_list_birthday_status_tomorrow(
						tr::now);
				}
				return QString();
			};

			auto usersWithBirthdays = ranges::views::all(
				_contactBirthdays
			) | ranges::views::transform([&](UserId userId) {
				return session->data().user(userId);
			}) | ranges::to_vector;

			ranges::sort(usersWithBirthdays, [](UserData *a, UserData *b) {
				const auto aBirthday = a->birthday();
				const auto bBirthday = b->birthday();
				const auto aIsToday = Data::IsBirthdayToday(aBirthday);
				const auto bIsToday = Data::IsBirthdayToday(bBirthday);
				if (aIsToday != bIsToday) {
					return aIsToday > bIsToday;
				}
				if (aBirthday.month() != bBirthday.month()) {
					return aBirthday.month() < bBirthday.month();
				}
				return aBirthday.day() < bBirthday.day();
			});

			for (const auto user : usersWithBirthdays) {
				auto row = std::make_unique<PeerRow>(user);
				if (auto s = status(user->birthday()); !s.isEmpty()) {
					row->setCustomStatus(std::move(s));
				}
				controller->delegate()->peerListAppendRow(std::move(row));
			}

			controller->delegate()->peerListRefreshRows();
		},
		_pick,
		_contactBirthdays.empty()
			? rpl::producer<QString>(nullptr)
			: tr::lng_contacts_header())) {
	setStyleOverrides(&st::peerListSmallSkips);
}

void Controller::rowRightActionClicked(not_null<PeerListRow*> row) {
	delegate()->peerListShowRowMenu(row, true);
}

base::unique_qptr<Ui::PopupMenu> Controller::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	auto result = CreateRowContextMenu(parent, row->peer(), _pick);

	if (result) {
		// First clear _menu value, so that we don't check row positions yet.
		base::take(_menu);

		// Here unique_qptr is used like a shared pointer, where
		// not the last destroyed pointer destroys the object, but the first.
		_menu = base::unique_qptr<Ui::PopupMenu>(result.get());
	}

	return result;
}

void Controller::noSearchSubmit() {
	if (const auto onstack = _selfOption.activate) {
		onstack();
	}
	if (const auto onstack = _birthdayOptions.activate) {
		onstack();
	}
}

bool Controller::overrideKeyboardNavigation(
		int direction,
		int from,
		int to) {
	if (direction == -1 && from == -1 && to == -1 && _skipUpDirectionSelect) {
		return true;
	}
	_skipUpDirectionSelect = false;
	if (direction > 0) {
		if (!_selfOption.hasSelection() && !_birthdayOptions.hasSelection()) {
			return _selfOption.overrideKey(direction, from, to);
		}
		if (_selfOption.hasSelection() && !_birthdayOptions.hasSelection()) {
			if (_selfOption.overrideKey(direction, from, to)) {
				return true;
			} else {
				return _birthdayOptions.overrideKey(direction, from, to);
			}
		}
		if (!_selfOption.hasSelection() && _birthdayOptions.hasSelection()) {
			if (_birthdayOptions.overrideKey(direction, from, to)) {
				return true;
			}
		}
	} else if (direction < 0) {
		if (!_selfOption.hasSelection() && !_birthdayOptions.hasSelection()) {
			return _birthdayOptions.overrideKey(direction, from, to);
		}
		if (!_selfOption.hasSelection() && _birthdayOptions.hasSelection()) {
			if (_birthdayOptions.overrideKey(direction, from, to)) {
				return true;
			} else if (!_birthdayOptions.hasSelection()) {
				const auto res = _selfOption.overrideKey(direction, from, to);
				_skipUpDirectionSelect = _selfOption.hasSelection();
				return res;
			}
		}
		if (_selfOption.hasSelection() && !_birthdayOptions.hasSelection()) {
			if (_selfOption.overrideKey(direction, from, to)) {
				_skipUpDirectionSelect = _selfOption.hasSelection();
				return true;
			}
		}
	}
	return false;
}

std::unique_ptr<PeerListRow> Controller::createRow(
		not_null<UserData*> user) {
	if (const auto birthday
			= user->session().promoSuggestions().knownContactBirthdays()) {
		if (ranges::contains(*birthday, peerToUser(user->id))) {
			return nullptr;
		}
	}
	if (user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| user->isInaccessible()) {
		return nullptr;
	}
	return std::make_unique<PeerRow>(user);
}

void Controller::prepareViewHook() {
	auto list = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	list->add(std::move(_selfOption.content));
	list->add(std::move(_birthdayOptions.content));
	delegate()->peerListSetAboveWidget(std::move(list));
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	_pick(row->peer(), PickType::Activate);
}

} // namespace

void ChooseStarGiftRecipient(
		not_null<Window::SessionController*> window) {
	const auto session = &window->session();
	session->promoSuggestions().requestContactBirthdays([=] {
		auto controller = std::make_unique<Controller>(
			session,
			[=](not_null<PeerData*> peer, PickType type) {
				if (type == PickType::Activate) {
					ShowStarGiftBox(window, peer);
				} else if (type == PickType::SendMessage) {
					using Way = Window::SectionShow::Way;
					window->showPeerHistory(peer, Way::Forward);
				} else if (type == PickType::OpenProfile) {
					window->show(PrepareShortInfoBox(peer, window));
				}
			});
		const auto controllerRaw = controller.get();
		auto initBox = [=](not_null<PeerListBox*> box) {
			box->setTitle(tr::lng_gift_premium_or_stars());
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

			box->noSearchSubmits() | rpl::on_next([=] {
				controllerRaw->noSearchSubmit();
			}, box->lifetime());
		};
		window->show(
			Box<PeerListBox>(std::move(controller), std::move(initBox)),
			LayerOption::KeepOther);
	});
}

void ShowStarGiftBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	if (controller->showFrozenError()) {
		return;
	}

	struct Session {
		PeerData *peer = nullptr;
		MyGiftsDescriptor my;
		bool premiumGiftsReady = false;
		bool starsGiftsReady = false;
		bool fullReady = false;
		bool myReady = false;

		bool hasPremium = false;
		bool hasUpgradable = false;
		bool hasLimited = false;
		bool hasUnlimited = false;

		rpl::lifetime lifetime;

		[[nodiscard]] bool ready() const {
			return premiumGiftsReady
				&& starsGiftsReady
				&& fullReady
				&& myReady;
		}
	};
	static auto Map = base::flat_map<not_null<Main::Session*>, Session>();

	const auto session = &controller->session();
	auto i = Map.find(session);
	if (i == end(Map)) {
		i = Map.emplace(session).first;
		session->lifetime().add([=] { Map.remove(session); });
	} else if (i->second.peer == peer) {
		return;
	}
	i->second = Session{ .peer = peer };

	const auto weak = base::make_weak(controller);
	const auto checkReady = [=] {
		auto &entry = Map[session];
		if (!entry.ready()) {
			return;
		}
		auto was = std::move(entry);
		entry = Session();
		if (const auto strong = weak.get()) {
			if (const auto user = peer->asUser()) {
				using Type = Api::DisallowedGiftType;
				const auto disallowedTypes = user->disallowedGiftTypes();
				const auto premium = (disallowedTypes & Type::Premium)
					|| peer->isSelf();
				const auto limited = (disallowedTypes & Type::Limited);
				const auto unlimited = (disallowedTypes & Type::Unlimited);
				const auto unique = (disallowedTypes & Type::Unique);
				if ((unique || (!was.hasUpgradable && was.my.list.empty()))
					&& (premium || !was.hasPremium)
					&& (limited || !was.hasLimited)
					&& (unlimited || !was.hasUnlimited)) {
					strong->showToast(
						tr::lng_edit_privacy_gifts_restricted(tr::now));
					return;
				}
			}
			strong->show(Box(GiftBox, strong, peer, std::move(was.my)));
		}
	};

	const auto user = peer->asUser();
	if (user && !user->isSelf()) {
		GiftsPremium(
			session,
			peer
		) | rpl::on_next([=](PremiumGiftsDescriptor &&gifts) {
			auto &entry = Map[session];
			entry.premiumGiftsReady = true;
			entry.hasPremium = !gifts.list.empty();
			checkReady();
		}, i->second.lifetime);
	} else {
		i->second.hasPremium = false;
		i->second.premiumGiftsReady = true;
	}

	if (peer->isFullLoaded()) {
		i->second.fullReady = true;
	} else {
		peer->updateFull();
		peer->session().changes().peerUpdates(
			peer,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::take(1) | rpl::on_next([=] {
			auto &entry = Map[session];
			entry.fullReady = true;
			checkReady();
		}, i->second.lifetime);
	}

	GiftsStars(
		session,
		peer
	) | rpl::on_next([=](std::vector<GiftTypeStars> &&gifts) {
		auto &entry = Map[session];
		entry.starsGiftsReady = true;
		for (const auto &gift : gifts) {
			if (gift.info.limitedCount) {
				entry.hasLimited = true;
				if (gift.info.starsToUpgrade) {
					entry.hasUpgradable = true;
				}
			} else {
				entry.hasUnlimited = true;
			}
		}
		checkReady();
	}, i->second.lifetime);

	Data::MyUniqueGiftsSlice(
		session,
		Data::MyUniqueType::OnlyOwned
	) | rpl::on_next([=](MyGiftsDescriptor &&gifts) {
		auto &entry = Map[session];
		entry.my = std::move(gifts);
		entry.myReady = true;
		checkReady();
	}, i->second.lifetime);
}

void SetupResalePriceButton(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QColor> background,
		rpl::producer<CreditsAmount> price,
		Fn<void()> click) {
	const auto resale = Ui::CreateChild<
		Ui::FadeWrapScaled<Ui::AbstractButton>
	>(parent, object_ptr<Ui::AbstractButton>(parent));
	resale->move(0, 0);

	const auto button = resale->entity();
	const auto text = Ui::CreateChild<Ui::FlatLabel>(
		button,
		QString(),
		st::uniqueGiftResalePrice);
	text->setAttribute(Qt::WA_TransparentForMouseEvents);
	text->sizeValue() | rpl::on_next([=](QSize size) {
		const auto padding = st::uniqueGiftResalePadding;
		const auto margin = st::uniqueGiftResaleMargin;
		button->resize(size.grownBy(padding + margin));
		text->move((margin + padding).left(), (margin + padding).top());
	}, button->lifetime());
	text->setTextColorOverride(QColor(255, 255, 255, 255));

	std::move(price) | rpl::on_next([=](CreditsAmount value) {
		if (value) {
			text->setMarkedText(value.ton()
				? Ui::Text::IconEmoji(&st::tonIconEmoji).append(
					Lang::FormatCreditsAmountDecimal(value))
				: Ui::Text::IconEmoji(&st::starIconEmoji).append(
					Lang::FormatCountDecimal(value.whole())));
			resale->toggle(true, anim::type::normal);
		} else {
			resale->toggle(false, anim::type::normal);
		}
	}, resale->lifetime());
	resale->finishAnimating();

	const auto bg = button->lifetime().make_state<rpl::variable<QColor>>(
		std::move(background));
	button->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(button);
		auto hq = PainterHighQualityEnabler(p);

		const auto inner = button->rect().marginsRemoved(
			st::uniqueGiftResaleMargin);
		const auto radius = inner.height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(bg->current());
		p.drawRoundedRect(inner, radius, radius);
	}, button->lifetime());
	bg->changes() | rpl::on_next([=] {
		button->update();
	}, button->lifetime());

	if (click) {
		resale->entity()->setClickedCallback(std::move(click));
	} else {
		resale->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
}

void AddUniqueGiftCover(
		not_null<VerticalLayout*> container,
		rpl::producer<UniqueGiftCover> data,
		UniqueGiftCoverArgs &&args) {
	using SpinnerState = Data::GiftUpgradeSpinner::State;

	const auto cover = container->add(object_ptr<RpWidget>(container));

	struct Released {
		Released() : link(QColor(255, 255, 255)) {
		}

		rpl::variable<TextWithEntities> subtitleText;
		std::optional<Ui::Premium::ColoredMiniStars> stars;
		style::owned_color link;
		style::FlatLabel st;
		rpl::variable<PeerData*> by;
		base::unique_qptr<FlatLabel> subtitle;
		base::unique_qptr<AbstractButton> subtitleButton;
		rpl::variable<int> subtitleHeight;
		QColor bg;
		QColor fg;
	};
	const auto released = cover->lifetime().make_state<Released>();

	struct BackdropView {
		Data::UniqueGiftBackdrop colors;
		QImage gradient;
	};
	struct PatternView {
		DocumentData *document = nullptr;
		std::unique_ptr<Text::CustomEmoji> emoji;
		base::flat_map<int, base::flat_map<float64, QImage>> emojis;
	};
	struct ModelView {
		std::shared_ptr<Data::DocumentMedia> media;
		std::unique_ptr<Lottie::SinglePlayer> lottie;
		rpl::lifetime lifetime;
	};
	struct GiftView {
		std::optional<Data::UniqueGift> gift;
		BackdropView backdrop;
		PatternView pattern;
		ModelView model;
		bool forced = false;
	};
	struct AttributeSpin {
		AttributeSpin(crl::time duration) : duration(duration) {
		}

		Animations::Simple animation;
		crl::time duration = 0;
		int wasIndex = -1;
		int nowIndex = -1;
		int willIndex = -1;

		[[nodiscard]] float64 progress() const {
			return animation.value(1.);
		}
		void startWithin(int count, Fn<void()> update) {
			Expects(count > 0);

			wasIndex = nowIndex;
			nowIndex = willIndex;
			willIndex = (willIndex < 0 ? 1 : (willIndex + 1)) % count;
			animation.start(update, 0., 1., duration);
		}
		void startToTarget(Fn<void()> update, int slowdown = 1) {
			if (willIndex != 0) {
				wasIndex = nowIndex;
				nowIndex = willIndex;
				willIndex = 0;
				animation.start(
					update,
					0.,
					1.,
					duration * 3 * slowdown,
					anim::easeOutCubic);
			}
		}
	};
	struct State {
		std::shared_ptr<Data::GiftUpgradeSpinner> spinner;
		Fn<void()> checkSpinnerStart;
		GiftView now;
		GiftView next;
		Animations::Simple crossfade;
		Animations::Simple heightAnimation;
		std::vector<BackdropView> spinnerBackdrops;
		std::vector<PatternView> spinnerPatterns;
		std::vector<ModelView> spinnerModels;
		AttributeSpin backdropSpin = kBackdropSpinDuration;
		AttributeSpin patternSpin = kPatternSpinDuration;
		AttributeSpin modelSpin = kModelSpinDuration;
		crl::time spinStarted = 0;
		int heightFinal = 0;
		bool crossfading = false;
		bool updateAttributesPending = false;
	};
	const auto state = cover->lifetime().make_state<State>();
	state->spinner = std::move(args.upgradeSpinner);

	const auto setupModel = [=](
			ModelView &to,
			const Data::UniqueGiftModel &model) {
		to.lifetime.destroy();

		const auto document = model.document;
		to.media = document->createMediaView();
		to.media->automaticLoad({}, nullptr);
		rpl::single() | rpl::then(
			document->session().downloaderTaskFinished()
		) | rpl::filter([&to] {
			return to.media->loaded();
		}) | rpl::on_next([=, &to] {
			const auto lottieSize = st::creditsHistoryEntryStarGiftSize;
			to.lottie = ChatHelpers::LottiePlayerFromDocument(
				to.media.get(),
				ChatHelpers::StickerLottieSize::MessageHistory,
				QSize(lottieSize, lottieSize),
				Lottie::Quality::High);

			to.lifetime.destroy();
			const auto lottie = to.lottie.get();
			lottie->updates() | rpl::on_next([=] {
				if (state->now.model.lottie.get() == lottie
					|| state->crossfade.animating()) {
					cover->update();
				}
				if (const auto onstack = state->checkSpinnerStart) {
					onstack();
				}
			}, to.lifetime);
		}, to.lifetime);
	};
	const auto setupPattern = [=](
			PatternView &to,
			const Data::UniqueGiftPattern &pattern) {
		const auto document = pattern.document;
		const auto callback = [=] {
			if (state->now.pattern.document == document
				|| state->crossfade.animating()) {
				cover->update();
			}
			if (const auto onstack = state->checkSpinnerStart) {
				onstack();
			}
		};
		to.document = document;
		to.emoji = document->owner().customEmojiManager().create(
			document,
			callback,
			Data::CustomEmojiSizeTag::Large);
		[[maybe_unused]] const auto preload = to.emoji->ready();
	};

	if (const auto spinner = state->spinner.get()) {
		const auto fillBackdrops = [=] {
			state->spinnerBackdrops.clear();
			state->spinnerBackdrops.reserve(kSpinnerBackdrops);
			const auto push = [&](const Data::UniqueGiftBackdrop &backdrop) {
				if (state->spinnerBackdrops.size() >= kSpinnerBackdrops) {
					return false;
				}
				const auto already = ranges::contains(
					state->spinnerBackdrops,
					backdrop,
					&BackdropView::colors);
				if (!already) {
					state->spinnerBackdrops.push_back({ backdrop });
				}
				return true;
			};
			push(spinner->target->backdrop);
			for (const auto &backdrop : spinner->attributes.backdrops) {
				if (!push(backdrop)) {
					break;
				}
			}
		};
		const auto fillPatterns = [=] {
			state->spinnerPatterns.clear();
			state->spinnerPatterns.reserve(kSpinnerPatterns);
			const auto push = [&](const Data::UniqueGiftPattern &pattern) {
				if (state->spinnerPatterns.size() >= kSpinnerPatterns) {
					return false;
				}
				const auto already = ranges::contains(
					state->spinnerPatterns,
					pattern.document.get(),
					&PatternView::document);
				if (!already) {
					setupPattern(
						state->spinnerPatterns.emplace_back(),
						pattern);
				}
				return true;
			};
			push(spinner->target->pattern);
			for (const auto &pattern : spinner->attributes.patterns) {
				if (!push(pattern)) {
					break;
				}
			}
		};
		const auto fillModels = [=] {
			state->spinnerModels.clear();
			state->spinnerModels.reserve(kSpinnerModels);
			const auto push = [&](const Data::UniqueGiftModel &model) {
				if (state->spinnerModels.size() >= kSpinnerModels) {
					return false;
				}
				const auto already = ranges::contains(
					state->spinnerModels,
					model.document,
					[](const ModelView &view) {
						return view.media->owner();
					});
				if (!already) {
					setupModel(
						state->spinnerModels.emplace_back(),
						model);
				}
				return true;
			};
			push(spinner->target->model);
			for (const auto &model : spinner->attributes.models) {
				if (!push(model)) {
					break;
				}
			}
		};
		state->checkSpinnerStart = [=] {
			if (spinner->state.current() != SpinnerState::Loading
				|| state->crossfading) {
				return;
			}
			for (const auto &pattern : state->spinnerPatterns) {
				if (!pattern.emoji->ready()) {
					return;
				}
			}
			for (const auto &model : state->spinnerModels) {
				if (!model.lottie || !model.lottie->ready()) {
					return;
				}
			}
			spinner->state = SpinnerState::Prepared;
			state->checkSpinnerStart = nullptr;
		};
		spinner->state.value() | rpl::on_next([=](SpinnerState now) {
			if (now == SpinnerState::Preparing) {
				fillBackdrops();
				fillPatterns();
				fillModels();
				spinner->state = SpinnerState::Loading;
				if (!state->crossfading) {
					state->next = {};
				}
				state->checkSpinnerStart();
			} else if (now == SpinnerState::Started) {
				const auto repaint = [=] { cover->update(); };
				state->backdropSpin.startWithin(
					state->spinnerBackdrops.size(),
					repaint);
				state->patternSpin.startWithin(
					state->spinnerPatterns.size(),
					repaint);
				state->modelSpin.startWithin(
					state->spinnerModels.size(),
					repaint);
				state->spinStarted = crl::now();
			}
		}, cover->lifetime());
	}

	const auto lottieSize = st::creditsHistoryEntryStarGiftSize;
	rpl::duplicate(
		data
	) | rpl::on_next([=](const UniqueGiftCover &now) {
		const auto setup = [&](GiftView &to) {
			Expects(!now.spinner);

			to = {};
			to.gift = now.values;
			to.forced = now.force;
			to.backdrop.colors = now.values.backdrop;
			setupModel(to.model, now.values.model);
			setupPattern(to.pattern, now.values.pattern);
		};

		const auto spinner = state->spinner.get();
		if (!state->now.gift) {
			setup(state->now);
			cover->update();
		} else if (now.spinner) {
			Assert(spinner != nullptr);
			Assert(spinner->state.current() == SpinnerState::Prepared);

			spinner->state = SpinnerState::Started;
		} else if (!state->next.gift || now.force) {
			const auto spinnerState = spinner
				? spinner->state.current()
				: SpinnerState::Initial;
			if (spinnerState == SpinnerState::Initial) {
				setup(state->next);
			}
		}
	}, cover->lifetime());

	const auto repaintedHook = args.repaintedHook;
	const auto updateLinkFg = args.subtitleLinkColored;
	const auto updateColorsFromBackdrops = [=](
			const BackdropView &from,
			const BackdropView &to,
			float64 progress) {
		released->bg = (progress == 0.)
			? from.colors.patternColor
			: (progress == 1.)
			? to.colors.patternColor
			: anim::color(
				from.colors.patternColor,
				to.colors.patternColor,
				progress);
		const auto color = (progress == 0.)
			? from.colors.textColor
			: (progress == 1.)
			? to.colors.textColor
			: anim::color(
				from.colors.textColor,
				to.colors.textColor,
				progress);
		if (updateLinkFg) {
			released->link.update(color);
		}
		released->fg = color;
		released->subtitle->setTextColorOverride(color);
	};
	const auto updateColors = [=](float64 progress) {
		if (repaintedHook) {
			repaintedHook(state->now.gift, state->next.gift, progress);
		}
		updateColorsFromBackdrops(
			state->now.backdrop,
			state->next.backdrop,
			progress);
	};
	if (args.resalePrice) {
		auto background = rpl::duplicate(
			data
		) | rpl::map([=](const UniqueGiftCover &cover) {
			auto result = cover.values.backdrop.patternColor;
			result.setAlphaF(kGradientButtonBgOpacity * result.alphaF());
			return result;
		});
		SetupResalePriceButton(
			cover,
			std::move(background),
			std::move(args.resalePrice),
			std::move(args.resaleClick));
	}

	const auto pretitle = args.pretitle
		? CreateChild<FlatLabel>(
			cover,
			std::move(args.pretitle),
			st::uniqueGiftPretitle)
		: nullptr;
	if (pretitle) {
		released->stars.emplace(
			cover,
			true,
			Ui::Premium::MiniStarsType::SlowStars);
		const auto white = QColor(255, 255, 255);
		released->stars->setColorOverride(QGradientStops{
			{ 0., anim::with_alpha(white, .3) },
			{ 1., white },
		});
		pretitle->geometryValue() | rpl::on_next([=](QRect rect) {
			const auto half = rect.height() / 2;
			released->stars->setCenter(rect - QMargins(half, 0, half, 0));
		}, pretitle->lifetime());
		pretitle->setAttribute(Qt::WA_TransparentForMouseEvents);
		pretitle->setTextColorOverride(QColor(255, 255, 255));
		pretitle->paintOn([=](QPainter &p) {
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = pretitle->height() / 2.;
			p.setPen(Qt::NoPen);
			auto bg = released->bg;
			bg.setAlphaF(kGradientButtonBgOpacity * bg.alphaF());
			p.setBrush(bg);
			p.drawRoundedRect(pretitle->rect(), radius, radius);
			p.translate(-pretitle->pos());
			released->stars->paint(p);
		});
	}
	const auto title = CreateChild<FlatLabel>(
		cover,
		rpl::duplicate(
			data
		) | rpl::map([](const UniqueGiftCover &now) {
			return now.values.title;
		}),
		st::uniqueGiftTitle);
	title->setTextColorOverride(QColor(255, 255, 255));
	released->by = rpl::duplicate(
		data
	) | rpl::map([](const UniqueGiftCover &cover) {
		return cover.values.releasedBy;
	});
	released->subtitleText = rpl::combine(
		args.subtitle ? std::move(args.subtitle) : rpl::single(tr::marked()),
		rpl::duplicate(data)
	) | rpl::map([=](TextWithEntities custom, const UniqueGiftCover &cover) {
		if (!custom.empty()) {
			return custom;
		}
		const auto &gift = cover.values;
		return gift.releasedBy
			? tr::lng_gift_unique_number_by(
				tr::now,
				lt_index,
				tr::marked(QString::number(gift.number)),
				lt_name,
				tr::link('@' + gift.releasedBy->username()),
				tr::marked)
			: tr::lng_gift_unique_number(
				tr::now,
				lt_index,
				tr::marked(QString::number(gift.number)),
				tr::marked);
	});
	released->by.value() | rpl::on_next([=](PeerData *by) {
		released->st = by
			? st::uniqueGiftReleasedBy
			: st::uniqueGiftSubtitle;
		released->st.palette.linkFg = released->link.color();

		released->subtitle = base::make_unique_q<FlatLabel>(
			cover,
			released->subtitleText.value(),
			released->st);
		const auto subtitle = released->subtitle.get();
		subtitle->show();

		cover->widthValue(
		) | rpl::on_next([=](int width) {
			const auto skip = st::uniqueGiftBottom;
			if (width <= 3 * skip) {
				return;
			}
			const auto available = width - 2 * skip;
			subtitle->resizeToWidth(available);
		}, subtitle->lifetime());
		released->subtitleHeight = subtitle->heightValue();

		if (const auto handler = args.subtitleClick) {
			subtitle->setClickHandlerFilter([=](const auto &...) {
				handler();
				return false;
			});
		} else if (by) {
			released->subtitleButton = base::make_unique_q<AbstractButton>(
				cover);
			const auto button = released->subtitleButton.get();

			button->show();
			subtitle->raise();
			subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);

			button->setClickedCallback([=] {
				GiftReleasedByHandler(by);
			});
			subtitle->geometryValue(
			) | rpl::on_next([=](QRect geometry) {
				button->setGeometry(
					geometry.marginsAdded(st::giftBoxReleasedByMargin));
			}, button->lifetime());
			button->paintRequest() | rpl::on_next([=] {
				auto p = QPainter(button);
				auto hq = PainterHighQualityEnabler(p);
				const auto use = subtitle->textMaxWidth();
				const auto add = button->width() - subtitle->width();
				const auto full = use + add;
				const auto left = (button->width() - full) / 2;
				const auto height = button->height();
				const auto radius = height / 2.;
				p.setPen(Qt::NoPen);
				p.setBrush(released->bg);
				p.setOpacity(0.5);
				p.drawRoundedRect(left, 0, full, height, radius, radius);
			}, button->lifetime());
		} else {
			released->subtitleButton = nullptr;
		}
		updateColors(state->crossfade.value(state->crossfading ? 1. : 0.));
	}, title->lifetime());

	const auto attrs = args.attributesInfo
		? CreateChild<RpWidget>(cover)
		: nullptr;
	auto updateAttrs = Fn<void(const Data::UniqueGift &)>([](const auto &) {
	});
	if (attrs) {
		struct AttributeState {
			Ui::Text::String name;
			Ui::Text::String type;
			Ui::Text::String percent;
		};
		struct AttributesState {
			AttributeState model;
			AttributeState pattern;
			AttributeState backdrop;
		};
		const auto astate = cover->lifetime().make_state<AttributesState>();
		const auto setType = [&](AttributeState &state, tr::phrase<> text) {
			state.type = Ui::Text::String(
				st::uniqueAttributeType,
				text(tr::now));
		};
		setType(astate->model, tr::lng_auction_preview_model);
		setType(astate->pattern, tr::lng_auction_preview_symbol);
		setType(astate->backdrop, tr::lng_auction_preview_backdrop);

		updateAttrs = [=](const Data::UniqueGift &gift) {
			const auto set = [&](
					AttributeState &state,
					const Data::UniqueGiftAttribute &value) {
				state.name = Ui::Text::String(
					st::uniqueAttributeName,
					value.name);
				state.percent = Ui::Text::String(
					st::uniqueAttributePercent,
					QString::number(value.rarityPermille / 10.) + '%');
			};
			set(astate->model, gift.model);
			set(astate->pattern, gift.pattern);
			set(astate->backdrop, gift.backdrop);
			attrs->update();
		};
		const auto height = st::uniqueAttributeTop
			+ st::uniqueAttributePadding.top()
			+ st::uniqueAttributeName.font->height
			+ st::uniqueAttributeType.font->height
			+ st::uniqueAttributePadding.bottom();
		attrs->resize(attrs->width(), height);
		attrs->paintOn([=](QPainter &p) {
			const auto skip = st::giftBoxGiftSkip.x();
			const auto available = attrs->width() - 2 * skip;
			const auto single = available / 3;
			if (single <= 0) {
				return;
			}
			auto hq = PainterHighQualityEnabler(p);
			auto bg = released->bg;
			bg.setAlphaF(kGradientButtonBgOpacity * bg.alphaF());
			const auto innert = st::uniqueAttributeTop;
			const auto innerh = height - innert;
			const auto radius = innerh / 3.;
			const auto paint = [&](int x, const AttributeState &state) {
				p.setPen(Qt::NoPen);
				p.setBrush(bg);
				p.drawRoundedRect(x, innert, single, innerh, radius, radius);
				p.setPen(QColor(255, 255, 255));
				const auto padding = st::uniqueAttributePadding;
				const auto inner = single - padding.left() - padding.right();
				const auto namew = std::min(inner, state.name.maxWidth());
				state.name.draw(p, {
					.position = QPoint(
						x + (single - namew) / 2,
						innert + padding.top()),
					.availableWidth = namew,
					.elisionLines = 1,
				});
				p.setPen(released->fg);
				const auto typew = std::min(inner, state.type.maxWidth());
				state.type.draw(p, {
					.position = QPoint(
						x + (single - typew) / 2,
						innert + padding.top() + state.name.minHeight()),
					.availableWidth = typew,
				});
				p.setPen(Qt::NoPen);
				p.setBrush(anim::color(released->bg, released->fg, 0.3));
				const auto r = st::uniqueAttributePercent.font->height / 2.;
				const auto left = x + single - state.percent.maxWidth();
				const auto top = st::uniqueAttributePercentPadding.top();
				const auto percent = QRect(
					left,
					top,
					state.percent.maxWidth(),
					st::uniqueAttributeType.font->height);
				p.drawRoundedRect(
					percent.marginsAdded(st::uniqueAttributePercentPadding),
					r,
					r);
				p.setPen(QColor(255, 255, 255));
				state.percent.draw(p, {
					.position = percent.topLeft(),
				});
			};
			auto left = 0;
			paint(left, astate->model);
			paint(left + single + skip, astate->backdrop);
			paint(attrs->width() - single - left, astate->pattern);
		});
	}
	updateAttrs(*state->now.gift);

	rpl::combine(
		cover->widthValue(),
		released->subtitleHeight.value()
	) | rpl::on_next([=](int width, int subtitleHeight) {
		const auto skip = st::uniqueGiftBottom;
		if (width <= 3 * skip) {
			return;
		}
		const auto available = width - 2 * skip;
		title->resizeToWidth(available);

		auto top = st::uniqueGiftTitleTop;
		if (pretitle) {
			pretitle->move((width - pretitle->width()) / 2, top);
			top += pretitle->height()
				+ (st::uniqueGiftSubtitleTop - st::uniqueGiftTitleTop)
				- title->height();
		}

		title->moveToLeft(skip, top);
		if (pretitle) {
			top += title->height() + st::defaultVerticalListSkip;
		} else {
			top += st::uniqueGiftSubtitleTop - st::uniqueGiftTitleTop;
		}

		released->subtitle->moveToLeft(skip, top);
		top += subtitleHeight + (skip / 2);

		if (attrs) {
			attrs->resizeToWidth(width
				- st::giftBoxPadding.left()
				- st::giftBoxPadding.right());
			attrs->moveToLeft(st::giftBoxPadding.left(), top);
			top += attrs->height() + (skip / 2);
		} else {
			top += (skip / 2);
		}
		if (!cover->height() || cover->height() == top) {
			cover->resize(width, top);
		} else {
			state->heightFinal = top;
			state->heightAnimation.start([=] {
				cover->resize(
					width,
					int(base::SafeRound(state->heightAnimation.value(top))));
			}, cover->height(), top, st::slideWrapDuration);
		}
	}, cover->lifetime());

	cover->paintOn([=](QPainter &p) {
		auto progress = state->crossfade.value(state->crossfading ? 1. : 0.);
		if (state->updateAttributesPending && progress >= 0.5) {
			state->updateAttributesPending = false;
			updateAttrs(*state->next.gift);
		} else if (state->updateAttributesPending
			&& !state->crossfade.animating()) {
			state->updateAttributesPending = false;
			updateAttrs(*state->now.gift);
		}
		if (state->crossfading) {
			updateColors(progress);
		}
		if (progress == 1.) {
			state->crossfading = false;
			state->now = base::take(state->next);
			progress = 0.;
		}
		const auto width = cover->width();
		const auto getBackdrop = [&](BackdropView &backdrop) {
			const auto ratio = style::DevicePixelRatio();
			const auto gradientSize = QSize(
				width,
				std::max(cover->height(), state->heightFinal));
			auto &gradient = backdrop.gradient;
			if (gradient.size() != gradientSize * ratio) {
				gradient = Ui::CreateTopBgGradient(
					gradientSize,
					backdrop.colors);
			}
			return gradient;
		};
		const auto paintPattern = [&](
				QPainter &p,
				PatternView &pattern,
				const BackdropView &backdrop,
				float64 shown) {
			const auto color = backdrop.colors.patternColor;
			const auto key = (color.red() << 16)
				| (color.green() << 8)
				| color.blue();
			Ui::PaintBgPoints(
				p,
				Ui::PatternBgPoints(),
				pattern.emojis[key],
				pattern.emoji.get(),
				color,
				QRect(0, 0, width, st::uniqueGiftSubtitleTop),
				shown);
		};
		const auto paintModel = [&](
				QPainter &p,
				ModelView &model,
				float64 scale = 1.,
				bool paused = false) {
			const auto lottie = model.lottie.get();
			const auto factor = style::DevicePixelRatio();
			const auto request = Lottie::FrameRequest{
				.box = Size(lottieSize) * factor,
			};
			const auto frame = (lottie && lottie->ready())
				? lottie->frameInfo(request)
				: Lottie::Animation::FrameInfo();
			if (frame.image.isNull()) {
				return false;
			}
			const auto size = frame.image.size() / factor;
			const auto rect = QRect(
				QPoint((width - size.width()) / 2, st::uniqueGiftModelTop),
				size);
			if (scale < 1.) {
				const auto origin = rect.center();
				p.translate(origin);
				p.scale(scale, scale);
				p.translate(-origin);
			}
			p.drawImage(rect, frame.image);
			const auto count = lottie->framesCount();
			const auto finished = lottie->frameIndex() == (count - 1);
			if (!paused) {
				lottie->markFrameShown();
			}
			return finished;
		};
		const auto paint = [&](GiftView &gift, float64 shown) {
			Expects(gift.gift.has_value());

			p.drawImage(0, 0, getBackdrop(gift.backdrop));
			if (gift.gift->pattern.document != gift.gift->model.document) {
				paintPattern(p, gift.pattern, gift.backdrop, shown);
			}
			return paintModel(p, gift.model);
		};

		if (state->spinStarted) {
			const auto select = [&](auto &&list, int index, auto &fallback)
			-> decltype(auto) {
				return (index >= 0) ? list[index] : fallback;
			};

			const auto now = crl::now();
			const auto elapsed = now - state->spinStarted;
			auto current = state->spinner->state.current();
			const auto stateTo = [&](SpinnerState to) {
				if (int(current) < int(to)) {
					state->spinner->state = current = to;
				}
			};
			const auto switchTo = [&](SpinnerState to, crl::time at) {
				if (elapsed >= at) {
					stateTo(to);
				}
			};
			const auto actualize = [&](
					AttributeSpin &spin,
					auto &&list,
					SpinnerState checkState,
					SpinnerState finishState,
					int slowdown = 1) {
				if (spin.progress() < 1.) {
					return;
				} else if (current >= checkState) {
					spin.startToTarget([=] { cover->update(); }, slowdown);
					stateTo(finishState);
				} else {
					spin.startWithin(list.size(), [=] { cover->update(); });
				}
			};
			if (anim::Disabled() && current < SpinnerState::FinishedModel) {
				stateTo(SpinnerState::FinishedModel);
				state->backdropSpin.startToTarget([=] { cover->update(); });
				state->patternSpin.startToTarget([=] { cover->update(); });
				state->modelSpin.startToTarget([=] { cover->update(); });
			}
			if (state->backdropSpin.willIndex != 0) {
				switchTo(SpinnerState::FinishBackdrop, kBackdropStopsAt);
			}
			actualize(
				state->backdropSpin,
				state->spinnerBackdrops,
				SpinnerState::FinishBackdrop,
				SpinnerState::FinishedBackdrop);

			if (current == SpinnerState::FinishedBackdrop
				&& state->patternSpin.willIndex != 0) {
				switchTo(SpinnerState::FinishPattern, kPatternStopsAt);
			}
			actualize(
				state->patternSpin,
				state->spinnerPatterns,
				SpinnerState::FinishPattern,
				SpinnerState::FinishedPattern);

			if (current == SpinnerState::FinishedPattern
				&& state->modelSpin.willIndex != 0) {
				// We want to start final model move not from the middle.
				switchTo(SpinnerState::FinishModel, kModelStopsAt);
			}
			actualize(
				state->modelSpin,
				state->spinnerModels,
				SpinnerState::FinishModel,
				SpinnerState::FinishedModel,
				2);

			auto &backdropNow = select(
				state->spinnerBackdrops,
				state->backdropSpin.nowIndex,
				state->now.backdrop);
			auto &backdropWill = select(
				state->spinnerBackdrops,
				state->backdropSpin.willIndex,
				state->now.backdrop);
			const auto backdropProgress = state->backdropSpin.progress();
			updateColorsFromBackdrops(
				backdropNow,
				backdropWill,
				backdropProgress);

			auto &patternNow = select(
				state->spinnerPatterns,
				state->patternSpin.nowIndex,
				state->now.pattern);
			auto &patternWill = select(
				state->spinnerPatterns,
				state->patternSpin.willIndex,
				state->now.pattern);
			const auto patternProgress = state->patternSpin.progress();
			const auto paintPatterns = [&](
					QPainter &p,
					const BackdropView &backdrop) {
				if (patternProgress >= 1.) {
					paintPattern(p, patternWill, backdrop, 1.);
				} else {
					paintPattern(p, patternNow, backdrop, 1. - patternProgress);
					if (patternProgress > 0.) {
						paintPattern(p, patternWill, backdrop, patternProgress);
					}
				}
			};

			if (backdropProgress >= 1.) {
				p.drawImage(0, 0, getBackdrop(backdropWill));
				paintPatterns(p, backdropWill);
			} else {
				p.drawImage(0, 0, getBackdrop(backdropNow));
				paintPatterns(p, backdropNow);

				const auto fade = width / 2;
				const auto from = anim::interpolate(
					-fade,
					width,
					backdropProgress);
				if (const auto till = from + fade; till > 0) {
					auto faded = getBackdrop(backdropWill);
					auto q = QPainter(&faded);
					paintPatterns(q, backdropWill);

					q.setCompositionMode(
						QPainter::CompositionMode_DestinationIn);
					auto brush = QLinearGradient(from, 0, till, 0);
					brush.setStops({
						{ 0., QColor(255, 255, 255, 255) },
						{ 1., QColor(255, 255, 255, 0) },
					});
					const auto ratio = int(faded.devicePixelRatio());
					const auto height = faded.height() / ratio;
					q.fillRect(from, 0, fade, height, brush);
					q.end();

					p.drawImage(
						QRect(0, 0, till, height),
						faded,
						QRect(0, 0, till * ratio, faded.height()));
				}
			}

			auto &modelWas = select(
				state->spinnerModels,
				state->modelSpin.wasIndex,
				state->now.model);
			auto &modelNow = select(
				state->spinnerModels,
				state->modelSpin.nowIndex,
				state->now.model);
			auto &modelWill = select(
				state->spinnerModels,
				state->modelSpin.willIndex,
				state->now.model);
			const auto modelProgress = state->modelSpin.progress();
			const auto paintOne = [&](ModelView &view, float64 progress) {
				if (progress >= 1. || progress <= -1.) {
					return;
				}
				auto scale = 1.;
				if (progress != 0.) {
					const auto shift = progress * width / 2.;
					const auto shown = 1. - std::abs(progress);
					scale = kModelScaleFrom + shown * (1. - kModelScaleFrom);
					p.save();
					p.setOpacity(shown);
					p.translate(int(base::SafeRound(shift)), 0);
				}
				paintModel(p, view, scale, true);
				if (progress != 0.) {
					p.restore();
				}
			};
			const auto initial = (state->modelSpin.nowIndex < 0);
			const auto ending = (current == SpinnerState::FinishedModel)
				&& !state->modelSpin.willIndex;
			const auto willProgress = ending
				? (modelProgress - 1.)
				: (-1. + modelProgress * 2 / 3.);
			const auto nowProgress = ending
				? (modelProgress * 4 / 3. - 1 / 3.)
				: initial
				? (modelProgress * 1 / 3.)
				: (willProgress + 2 / 3.);
			const auto wasProgress = ending
				? std::min(nowProgress + 2 / 3., 1.)
				: 1. + (modelProgress - 1.) * 2 / 3.;
			if (!initial) {
				paintOne(modelWas, wasProgress);
			}
			paintOne(modelNow, nowProgress);
			paintOne(modelWill, willProgress);

			if (anim::Disabled()
				|| (ending
					&& modelProgress >= 1.
					&& backdropProgress >= 1.
					&& patternProgress >= 1.)) {
				const auto take = [&](auto &&list) {
					auto result = std::move(list.front());
					list.clear();
					return result;
				};
				state->now.gift = *state->spinner->target;
				state->now.backdrop = take(state->spinnerBackdrops);
				state->now.pattern = take(state->spinnerPatterns);
				state->now.model = take(state->spinnerModels);
				state->now.forced = true;
				state->spinStarted = 0;
				state->spinner->state = SpinnerState::Finished;
			}
		} else {
			if (progress < 1.) {
				const auto finished = paint(state->now, 1. - progress)
					|| (state->next.forced
						&& (!state->crossfading
							|| !state->crossfade.animating()));
				const auto next = finished
					? state->next.model.lottie.get()
					: nullptr;
				if (next && next->ready()) {
					state->crossfading = true;
					state->updateAttributesPending = true;
					state->crossfade.start([=] {
						cover->update();
					}, 0., 1., kCrossfadeDuration);
				} else if (finished) {
					if (const auto onstack = state->checkSpinnerStart) {
						onstack();
					}
				}
			}
			if (progress > 0.) {
				p.setOpacity(progress);
				paint(state->next, progress);
			}
		}
	});
}

void AddWearGiftCover(
		not_null<VerticalLayout*> container,
		const Data::UniqueGift &data,
		not_null<PeerData*> peer) {
	const auto cover = container->add(object_ptr<RpWidget>(container));

	const auto title = CreateChild<FlatLabel>(
		cover,
		rpl::single(peer->name()),
		st::uniqueGiftTitle);
	title->setTextColorOverride(QColor(255, 255, 255));
	const auto subtitle = CreateChild<FlatLabel>(
		cover,
		(peer->isChannel()
			? tr::lng_chat_status_subscribers(
				lt_count,
				rpl::single(peer->asChannel()->membersCount() * 1.))
			: tr::lng_status_online()),
		st::uniqueGiftSubtitle);
	subtitle->setTextColorOverride(data.backdrop.textColor);

	struct State {
		QImage gradient;
		Data::UniqueGift gift;
		Ui::PeerUserpicView view;
		std::unique_ptr<Text::CustomEmoji> emoji;
		base::flat_map<float64, QImage> emojis;
		rpl::lifetime lifetime;
	};
	const auto state = cover->lifetime().make_state<State>(State{
		.gift = data,
	});
	state->emoji = peer->owner().customEmojiManager().create(
		state->gift.pattern.document,
		[=] { cover->update(); },
		Data::CustomEmojiSizeTag::Large);

	cover->widthValue() | rpl::on_next([=](int width) {
		const auto skip = st::uniqueGiftBottom;
		if (width <= 3 * skip) {
			return;
		}
		const auto available = width - 2 * skip;
		title->resizeToWidth(available);
		title->moveToLeft(skip, st::uniqueGiftTitleTop);

		subtitle->resizeToWidth(available);
		subtitle->moveToLeft(skip, st::uniqueGiftSubtitleTop);

		cover->resize(width, subtitle->y() + subtitle->height() + skip);
	}, cover->lifetime());

	cover->paintRequest() | rpl::on_next([=] {
		auto p = Painter(cover);

		const auto width = cover->width();
		const auto pointsHeight = st::uniqueGiftSubtitleTop;
		const auto ratio = style::DevicePixelRatio();
		if (state->gradient.size() != cover->size() * ratio) {
			state->gradient = Ui::CreateTopBgGradient(
				cover->size(),
				state->gift);
		}
		p.drawImage(0, 0, state->gradient);

		Ui::PaintBgPoints(
			p,
			Ui::PatternBgPoints(),
			state->emojis,
			state->emoji.get(),
			state->gift,
			QRect(0, 0, width, pointsHeight),
			1.);

		peer->paintUserpic(
			p,
			state->view,
			(width - st::uniqueGiftUserpicSize) / 2,
			st::uniqueGiftUserpicTop,
			st::uniqueGiftUserpicSize);
	}, cover->lifetime());
}

void AttachGiftSenderBadge(
		not_null<GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> from,
		const QDateTime &date) {
	const auto parent = box->getDelegate()->outerContainer();

	const auto dateText = tr::bold(langDayOfMonth(date.date()));
	const auto badge = CreateChild<FlatLabel>(
		parent,
		(from->isSelf()
			? tr::lng_gift_unique_sender_you(
				lt_date,
				rpl::single(dateText),
				tr::marked)
			: tr::lng_gift_unique_sender(
				lt_from,
				rpl::single(tr::link(tr::bold(from->shortName()), 1)),
				lt_date,
				rpl::single(dateText),
				tr::marked)),
		st::uniqueGiftSenderBadge);

	badge->paintOn([=](QPainter &p) {
		auto hq = PainterHighQualityEnabler(p);
		p.setBrush(st::radialBg);
		p.setPen(Qt::NoPen);
		const auto radius = badge->height() / 2.;
		p.drawRoundedRect(badge->rect(), radius, radius);
	});
	badge->setLink(1, std::make_shared<LambdaClickHandler>([=] {
		if (const auto window = show->resolveWindow()) {
			window->showPeerHistory(from);
		}
	}));

	auto widget = static_cast<QWidget*>(box);
	while (widget->parentWidget() && widget->parentWidget() != parent) {
		widget = widget->parentWidget();
	}

	const auto updateGeometry = [=] {
		const auto outer = parent->rect();
		const auto margin = st::msgServiceMargin;
		const auto padding = st::msgServicePadding;
		const auto available = outer.marginsRemoved(margin).width();
		badge->resizeToWidth(std::min(
			available - padding.left() - padding.right(),
			badge->textMaxWidth()));
		const auto inner = Ui::MapFrom(parent, box, box->rect());
		const auto top = std::max(
			inner.y() - badge->height() - margin.top(),
			outer.y() + (margin.top() * 2));
		const auto left = margin.left() + (available - badge->width()) / 2;
		badge->move(left, top);
	};
	badge->naturalWidthValue(
	) | rpl::on_next(updateGeometry, badge->lifetime());
	for (auto w = static_cast<QWidget*>(box)
		; w != widget
		; w = w->parentWidget()) {
		base::install_event_filter(w, [=](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type == QEvent::Move || type == QEvent::Resize) {
				PostponeCall(badge, updateGeometry);
			}
			return base::EventFilterResult::Continue;
		});
	}

	base::install_event_filter(widget, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Show) {
			badge->show();
		} else if (type == QEvent::Hide) {
			badge->hide();
		} else if (type == QEvent::Move || type == QEvent::Resize) {
			PostponeCall(badge, updateGeometry);
		} else if (type == QEvent::ZOrderChange) {
			PostponeCall(badge, [=] { badge->raise(); });
		}
		return base::EventFilterResult::Continue;
	});
	badge->setVisible(!widget->isHidden());

	base::install_event_filter(parent, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::ChildAdded) {
			PostponeCall(badge, [=] { badge->raise(); });
		} else if (e->type() == QEvent::Resize) {
			PostponeCall(badge, updateGeometry);
		}
		return base::EventFilterResult::Continue;
	});
	badge->raise();

	box->boxClosing() | rpl::on_next([=] {
		delete badge;
	}, badge->lifetime());
}

void ShowUniqueGiftWearBox(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<PeerData*> peer,
		const Data::UniqueGift &gift,
		Settings::GiftWearBoxStyleOverride st) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setNoContentMargin(true);

		box->setWidth((st::boxWidth + st::boxWideWidth) / 2); // =)
		box->setStyle(st.box ? *st.box : st::upgradeGiftBox);

		const auto channel = peer->isChannel();
		const auto content = box->verticalLayout();
		AddWearGiftCover(content, gift, peer);

		AddSkip(content, st::defaultVerticalListSkip * 2);

		const auto infoRow = [&](
				rpl::producer<QString> title,
				rpl::producer<QString> text,
				not_null<const style::icon*> icon) {
			auto raw = content->add(
				object_ptr<Ui::VerticalLayout>(content));
			raw->add(
				object_ptr<Ui::FlatLabel>(
					raw,
					std::move(title) | rpl::map(tr::bold),
					st.infoTitle ? *st.infoTitle : st::defaultFlatLabel),
				st::settingsPremiumRowTitlePadding);
			raw->add(
				object_ptr<Ui::FlatLabel>(
					raw,
					std::move(text),
					st.infoAbout ? *st.infoAbout : st::upgradeGiftSubtext),
				st::settingsPremiumRowAboutPadding);
			object_ptr<Info::Profile::FloatingIcon>(
				raw,
				*icon,
				st::starrefInfoIconPosition);
		};

		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_gift_wear_title(
					lt_name,
					rpl::single(UniqueGiftName(gift))),
				st.title ? *st.title : st::uniqueGiftTitle),
			st::settingsPremiumRowTitlePadding,
			style::al_top);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_gift_wear_about(),
				st.subtitle ? *st.subtitle : st::uniqueGiftSubtitle),
			st::settingsPremiumRowAboutPadding,
			style::al_top);
		infoRow(
			tr::lng_gift_wear_badge_title(),
			(channel
				? tr::lng_gift_wear_badge_about_channel()
				: tr::lng_gift_wear_badge_about()),
			st.radiantIcon ? st.radiantIcon : &st::menuIconUnique);
		infoRow(
			tr::lng_gift_wear_design_title(),
			(channel
				? tr::lng_gift_wear_design_about_channel()
				: tr::lng_gift_wear_design_about()),
			st.profileIcon ? st.profileIcon : &st::menuIconUniqueProfile);
		infoRow(
			tr::lng_gift_wear_proof_title(),
			(channel
				? tr::lng_gift_wear_proof_about_channel()
				: tr::lng_gift_wear_proof_about()),
			st.proofIcon ? st.proofIcon : &st::menuIconFactcheck);

		const auto session = &show->session();
		const auto checking = std::make_shared<bool>();
		const auto button = box->addButton(rpl::single(QString()), [=] {
			const auto emojiStatuses = &session->data().emojiStatuses();
			const auto id = emojiStatuses->fromUniqueGift(gift);
			if (!peer->isSelf()) {
				if (*checking) {
					return;
				}
				*checking = true;
				const auto weak = base::make_weak(box);
				CheckBoostLevel(show, peer, [=](int level) {
					const auto limits = Data::LevelLimits(&peer->session());
					const auto wanted = limits.channelEmojiStatusLevelMin();
					if (level >= wanted) {
						if (const auto strong = weak.get()) {
							strong->closeBox();
						}
						emojiStatuses->set(peer, id);
						return std::optional<Ui::AskBoostReason>();
					}
					const auto reason = [&]() -> Ui::AskBoostReason {
						return { Ui::AskBoostWearCollectible{
							wanted
						} };
					}();
					return std::make_optional(reason);
				}, [=] { *checking = false; });
			} else if (session->premium()) {
				box->closeBox();
				emojiStatuses->set(peer, id);
			} else {
				const auto link = tr::bold(
					tr::lng_send_as_premium_required_link(tr::now));
				Settings::ShowPremiumPromoToast(
					show,
					tr::lng_gift_wear_subscribe(
						tr::now,
						lt_link,
						tr::link(link),
						tr::marked),
					u"wear_collectibles"_q);
			}
		});
		const auto lock = Ui::Text::IconEmoji(&st::giftBoxLock);
		auto label = rpl::combine(
			tr::lng_gift_wear_start(),
			Data::AmPremiumValue(&show->session())
		) | rpl::map([=](const QString &text, bool premium) {
			auto result = TextWithEntities();
			if (!premium && peer->isSelf()) {
				result.append(lock);
			}
			result.append(text);
			return result;
		});
		SetButtonMarkedLabel(
			button,
			std::move(label),
			session,
			st::creditsBoxButtonLabel,
			&st::giftBox.button.textFg);
		AddUniqueCloseButton(box, {});
	}));
}

void PreloadUniqueGiftResellPrices(not_null<Main::Session*> session) {
	const auto entry = ResalePrices(session);
	const auto now = crl::now();
	const auto makeRequest = entry->prices.empty()
		|| (now - entry->lastReceived >= kResellPriceCacheLifetime);
	if (!makeRequest || entry->requestLifetime) {
		return;
	}
	const auto finish = [=] {
		entry->requestLifetime.destroy();
		entry->lastReceived = crl::now();
		for (const auto &callback : base::take(entry->waiting)) {
			callback();
		}
	};
	entry->requestLifetime = entry->api->requestStarGifts(
	) | rpl::on_error_done(finish, [=] {
		const auto &gifts = entry->api->starGifts();
		entry->prices.reserve(gifts.size());
		for (auto &gift : gifts) {
			if (!gift.resellTitle.isEmpty() && gift.starsResellMin > 0) {
				entry->prices[gift.resellTitle] = gift.starsResellMin;
			}
		}
		finish();
	});
}

void InvokeWithUniqueGiftResellPrice(
		not_null<Main::Session*> session,
		const QString &title,
		Fn<void(int)> callback) {
	PreloadUniqueGiftResellPrices(session);

	const auto finish = [=] {
		const auto entry = ResalePrices(session);
		Assert(entry->lastReceived != 0);

		const auto i = entry->prices.find(title);
		callback((i != end(entry->prices)) ? i->second : 0);
	};
	const auto entry = ResalePrices(session);
	if (entry->lastReceived) {
		finish();
	} else {
		entry->waiting.push_back(finish);
	}
}

void UpdateGiftSellPrice(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> unique,
		Data::SavedStarGiftId savedId,
		CreditsAmount price) {
	const auto wasOnResale = (unique->starsForResale > 0);
	const auto session = &show->session();
	session->api().request(MTPpayments_UpdateStarGiftPrice(
		Api::InputSavedStarGiftId(savedId, unique),
		(price
			? StarsAmountToTL(price)
			: MTP_starsAmount(MTP_long(0), MTP_int(0)))
	)).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		show->showToast((!price
			? tr::lng_gift_sell_removed
			: wasOnResale
			? tr::lng_gift_sell_updated
			: tr::lng_gift_sell_toast)(
				tr::now,
				lt_name,
				Data::UniqueGiftName(*unique)));
		const auto setStars = [&](CreditsAmount amount) {
			unique->starsForResale = amount.whole();
		};
		const auto setTon = [&](CreditsAmount amount) {
			unique->nanoTonForResale = amount.whole() * Ui::kNanosInOne
				+ amount.nano();
		};
		if (!price) {
			setStars({});
			setTon({});
			unique->onlyAcceptTon = false;
		} else if (price.ton()) {
			setStars(StarsFromTon(session, price));
			setTon(price);
			unique->onlyAcceptTon = true;
		} else {
			setStars(price);
			setTon(TonFromStars(session, price));
			unique->onlyAcceptTon = false;
		}
		session->data().notifyGiftUpdate({
			.id = savedId,
			.slug = unique->slug,
			.action = Data::GiftUpdate::Action::ResaleChange,
		});
	}).fail([=](const MTP::Error &error) {
		const auto earlyPrefix = u"STARGIFT_RESELL_TOO_EARLY_"_q;
		const auto type = error.type();
		if (type.startsWith(earlyPrefix)) {
			const auto seconds = type.mid(earlyPrefix.size()).toInt();
			const auto newAvailableAt = base::unixtime::now() + seconds;
			unique->canResellAt = newAvailableAt;
			ShowResaleGiftLater(show, unique);
		} else {
			show->showToast(type);
		}
	}).send();
}

void UniqueGiftSellBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> unique,
		Data::SavedStarGiftId savedId,
		int price,
		Settings::GiftWearBoxStyleOverride st) {
	const auto session = &show->session();
	const auto &appConfig = session->appConfig();
	const auto starsMin = appConfig.giftResaleStarsMin();
	const auto nanoTonMin = appConfig.giftResaleNanoTonMin();
	const auto starsThousandths = appConfig.giftResaleStarsThousandths();
	const auto nanoTonThousandths = appConfig.giftResaleNanoTonThousandths();

	struct State {
		rpl::variable<bool> onlyTon;
		rpl::variable<CreditsAmount> price;
		Fn<std::optional<CreditsAmount>()> computePrice;
		rpl::event_stream<> errors;
	};
	const auto state = box->lifetime().make_state<State>();
	state->onlyTon = unique->onlyAcceptTon;
	const auto priceNow = Data::UniqueGiftResaleAsked(*unique);
	state->price = priceNow
		? priceNow
		: price
		? CreditsAmount(price)
		: CreditsAmount(starsMin);

	box->setTitle(rpl::conditional(
		state->onlyTon.value(),
		tr::lng_gift_sell_title_ton(),
		tr::lng_gift_sell_title()));
	box->setStyle(st.box ? *st.box : st::upgradeGiftBox);
	box->setWidth(st::boxWideWidth);

	box->addTopButton(st.close ? *st.close : st::boxTitleClose, [=] {
		box->closeBox();
	});
	const auto name = Data::UniqueGiftName(*unique);
	const auto slug = unique->slug;

	const auto container = box->verticalLayout();
	auto priceInput = HistoryView::AddStarsTonPriceInput(container, {
		.session = session,
		.showTon = state->onlyTon.value(),
		.price = state->price.current(),
		.starsMin = starsMin,
		.starsMax = appConfig.giftResaleStarsMax(),
		.nanoTonMin = nanoTonMin,
		.nanoTonMax = appConfig.giftResaleNanoTonMax(),
		.allowEmpty = true,
	});
	state->price = std::move(priceInput.result);
	state->computePrice = std::move(priceInput.computeResult);
	box->setFocusCallback(std::move(priceInput.focusCallback));

	auto goods = rpl::merge(
		rpl::single(rpl::empty) | rpl::map_to(true),
		std::move(priceInput.updates) | rpl::map_to(true),
		state->errors.events() | rpl::map_to(false)
	) | rpl::start_spawning(box->lifetime());
	auto text = rpl::duplicate(goods) | rpl::map([=](bool good) {
		const auto value = state->computePrice();
		const auto amount = value ? value->value() : 0.;
		const auto tonMin = nanoTonMin / float64(Ui::kNanosInOne);
		const auto enough = value
			&& (amount >= (value->ton() ? tonMin : starsMin));
		const auto receive = !value
			? 0
			: value->ton()
			? ((amount * nanoTonThousandths) / 1000.)
			: ((int64(amount) * starsThousandths) / 1000);
		const auto thousandths = state->onlyTon.current()
			? nanoTonThousandths
			: starsThousandths;
		return (!good || !value)
			? (state->onlyTon.current()
				? tr::lng_gift_sell_min_price_ton(
					tr::now,
					lt_count,
					nanoTonMin / float64(Ui::kNanosInOne),
					tr::rich)
				: tr::lng_gift_sell_min_price(
					tr::now,
					lt_count,
					starsMin,
					tr::rich))
			: enough
			? (value->ton()
				? tr::lng_gift_sell_amount_ton(
					tr::now,
					lt_count,
					receive,
					tr::rich)
				: tr::lng_gift_sell_amount(
					tr::now,
					lt_count,
					receive,
					tr::rich))
			: tr::lng_gift_sell_about(
				tr::now,
				lt_percent,
				TextWithEntities{ u"%1%"_q.arg(thousandths / 10.) },
				tr::rich);
	});
	const auto details = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		std::move(text) | rpl::after_next([=] {
			box->verticalLayout()->resizeToWidth(box->width());
		}),
		st::boxLabel));

	Ui::AddSkip(container);
	Ui::AddSkip(container);
	box->addRow(object_ptr<Ui::PlainShadow>(box));
	Ui::AddSkip(container);
	Ui::AddSkip(container);

	const auto onlyTon = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_gift_sell_only_ton(tr::now),
			state->onlyTon.current(),
			st::defaultCheckbox));
	state->onlyTon = onlyTon->checkedValue();

	Ui::AddSkip(container);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_gift_sell_only_ton_about(tr::rich),
			st::boxDividerLabel));
	Ui::AddSkip(container);

	rpl::duplicate(goods) | rpl::on_next([=](bool good) {
		details->setTextColorOverride(
			good ? st::windowSubTextFg->c : st::boxTextFgError->c);
	}, details->lifetime());

	const auto submit = [=] {
		const auto value = state->computePrice();
		if (!value) {
			state->errors.fire({});
			return;
		}
		box->closeBox();
		UpdateGiftSellPrice(show, unique, savedId, *value);
	};
	std::move(
		priceInput.submits
	) | rpl::on_next(submit, details->lifetime());
	auto submitText = priceNow
		? tr::lng_gift_sell_update()
		: tr::lng_gift_sell_put();
	box->addButton(std::move(submitText), submit);
}

void ShowUniqueGiftSellBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> unique,
		Data::SavedStarGiftId savedId,
		Settings::GiftWearBoxStyleOverride st) {
	if (ShowResaleGiftLater(show, unique)) {
		return;
	}
	const auto session = &show->session();
	const auto &title = unique->title;
	InvokeWithUniqueGiftResellPrice(session, title, [=](int price) {
		show->show(Box(UniqueGiftSellBox, show, unique, savedId, price, st));
	});
}

void SendOfferBuyGift(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> unique,
		SuggestOptions options,
		int starsPerMessage,
		Fn<void(bool)> done) {
	const auto randomId = base::RandomValue<uint64>();
	const auto owner = show->session().data().peer(unique->ownerId);

	using Flag = MTPpayments_SendStarGiftOffer::Flag;
	show->session().api().request(MTPpayments_SendStarGiftOffer(
		MTP_flags(starsPerMessage ? Flag::f_allow_paid_stars : Flag()),
		owner->input(),
		MTP_string(unique->slug),
		StarsAmountToTL(options.price()),
		MTP_int(options.offerDuration),
		MTP_long(randomId),
		MTP_long(starsPerMessage)
	)).done([=](const MTPUpdates &result) {
		show->session().api().applyUpdates(result);
		done(true);
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u""_q) {
		} else {
			show->showToast(error.type());
		}
		done(false);
	}).send();
}

void ConfirmOfferBuyGift(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> unique,
		SuggestOptions options,
		Fn<void()> done) {
	const auto owner = show->session().data().peer(unique->ownerId);
	const auto fee = owner->starsPerMessageChecked();
	const auto price = options.price();
	const auto sent = std::make_shared<bool>();
	const auto send = [=](Fn<void()> close) {
		if (*sent) {
			return;
		}
		*sent = true;
		SendOfferBuyGift(show, unique, options, fee, [=](bool ok) {
			*sent = false;
			if (ok) {
				if (const auto window = show->resolveWindow()) {
					window->showPeerHistory(owner->id);
				}
				done();
				close();
			}
		});
	};

	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		Ui::ConfirmBox(box, {
			.text = tr::lng_gift_offer_confirm_text(
				tr::now,
				lt_cost,
				tr::bold(PrepareCreditsAmountText(options.price())),
				lt_user,
				tr::bold(owner->shortName()),
				lt_name,
				tr::bold(Data::UniqueGiftName(*unique)),
				tr::marked),
			.confirmed = send,
			.confirmText = tr::lng_payments_pay_amount(
				tr::now,
				lt_amount,
				Ui::Text::IconEmoji(price.ton()
					? &st::buttonTonIconEmoji
					: &st::buttonStarIconEmoji
				).append(Lang::FormatCreditsAmountDecimal(price.ton()
					? price
					: CreditsAmount(price.whole() + fee))),
				tr::marked),
			.title = tr::lng_gift_offer_confirm_title(),
		});

		auto helper = Ui::Text::CustomEmojiHelper();
		const auto starIcon = helper.paletteDependent(
			Ui::Earn::IconCreditsEmoji());
		const auto tonIcon = helper.paletteDependent(
			Ui::Earn::IconCurrencyEmoji());
		const auto context = helper.context();
		const auto table = box->addRow(
			object_ptr<Ui::TableLayout>(box, st::defaultTable),
			st::boxPadding);
		const auto add = [&](tr::phrase<> label, TextWithEntities value) {
			table->addRow(
				object_ptr<Ui::FlatLabel>(
					table,
					label(),
					st::defaultTable.defaultLabel),
				object_ptr<Ui::FlatLabel>(
					table,
					rpl::single(value),
					st::defaultTable.defaultValue,
					st::defaultPopupMenu,
					context),
				st::giveawayGiftCodeLabelMargin,
				st::giveawayGiftCodeValueMargin);
		};
		add(tr::lng_gift_offer_table_offer, tr::marked(price.ton()
			? tonIcon
			: starIcon).append(Lang::FormatCreditsAmountDecimal(price)));
		if (fee) {
			add(tr::lng_gift_offer_table_fee, tr::marked(starIcon).append(
				Lang::FormatCreditsAmountDecimal(CreditsAmount(fee))));
		}
		const auto hours = options.offerDuration / 3600;
		const auto duration = hours
			? tr::lng_hours(tr::now, lt_count, hours)
			: tr::lng_minutes(tr::now, lt_count, options.offerDuration / 60);
		add(tr::lng_gift_offer_table_duration, tr::marked(duration));
	}));
}

void ShowOfferBuyBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::shared_ptr<Data::UniqueGift> unique) {
	Expects(unique->starsMinOffer >= 0);

	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto done = [=](SuggestOptions result) {
		ConfirmOfferBuyGift(show, unique, result, [=] {
			if (const auto strong = weak->get()) {
				strong->closeBox();
			}
		});
	};
	using namespace HistoryView;
	const auto options = SuggestOptions{
		.exists = 1,
		.priceWhole = uint32(unique->starsMinOffer),
	};
	auto priceBox = Box(ChooseSuggestPriceBox, SuggestPriceBoxArgs{
		.peer = show->session().data().peer(unique->ownerId),
		.done = done,
		.value = options,
		.mode = SuggestMode::Gift,
		.giftName = UniqueGiftName(*unique),
	});
	*weak = priceBox.data();
	show->show(std::move(priceBox));
}

void GiftReleasedByHandler(not_null<PeerData*> peer) {
	const auto session = &peer->session();
	const auto window = session->tryResolveWindow(peer);
	if (window) {
		window->showPeerHistory(peer);
		return;
	}
	const auto account = not_null(&session->account());
	if (const auto window = Core::App().windowFor(account)) {
		window->invokeForSessionController(
			&session->account(),
			peer,
			[=](not_null<Window::SessionController*> window) {
				window->showPeerHistory(peer);
			});
	}
}

struct UpgradeArgs : StarGiftUpgradeArgs {
	std::vector<Data::UniqueGiftModel> models;
	std::vector<Data::UniqueGiftPattern> patterns;
	std::vector<Data::UniqueGiftBackdrop> backdrops;
	std::vector<UpgradePrice> prices;
	std::vector<UpgradePrice> nextPrices;
};

[[nodiscard]] rpl::producer<UniqueGiftCover> MakeUpgradeGiftStream(
		const UpgradeArgs &args) {
	if (args.models.empty()
		|| args.patterns.empty()
		|| args.backdrops.empty()) {
		return rpl::never<UniqueGiftCover>();
	}
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		struct State {
			UpgradeArgs data;
			std::vector<int> modelIndices;
			std::vector<int> patternIndices;
			std::vector<int> backdropIndices;
		};
		const auto state = lifetime.make_state<State>(State{
			.data = args,
		});

		const auto put = [=] {
			const auto index = [](std::vector<int> &indices, const auto &v) {
				const auto fill = [&] {
					if (!indices.empty()) {
						return;
					}
					indices = ranges::views::ints(0) | ranges::views::take(
						v.size()
					) | ranges::to_vector;
					ranges::shuffle(indices);
				};
				fill();
				const auto result = indices.back();
				indices.pop_back();
				fill();
				if (indices.back() == result) {
					std::swap(indices.front(), indices.back());
				}
				return result;
			};
			auto &models = state->data.models;
			auto &patterns = state->data.patterns;
			auto &backdrops = state->data.backdrops;
			consumer.put_next(UniqueGiftCover{ Data::UniqueGift{
				.title = (state->data.savedId
					? tr::lng_gift_upgrade_title(tr::now)
					: tr::lng_gift_upgrade_preview_title(tr::now)),
				.model = models[index(state->modelIndices, models)],
				.pattern = patterns[index(state->patternIndices, patterns)],
				.backdrop = backdrops[index(state->backdropIndices, backdrops)],
			} });
		};

		put();
		base::timer_each(
			kSwitchUpgradeCoverInterval / 3
		) | rpl::on_next(put, lifetime);

		return lifetime;
	};
}

void AddUpgradeGiftCover(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<VerticalLayout*> container,
		const UpgradeArgs &args,
		std::shared_ptr<Data::GiftUpgradeSpinner> upgradeSpinner,
		rpl::producer<std::shared_ptr<Data::GiftUpgradeResult>> upgraded) {
	struct State {
		rpl::variable<std::shared_ptr<Data::GiftUpgradeResult>> upgraded;
	};
	const auto state = container->lifetime().make_state<State>();
	state->upgraded = std::move(upgraded);

	auto resalePrice = rpl::single(
		CreditsAmount()
	) | rpl::then(state->upgraded.changes(
	) | rpl::map([=](std::shared_ptr<Data::GiftUpgradeResult> gift) {
		return Settings::UniqueGiftResalePrice(gift->info.unique);
	}) | rpl::flatten_latest());
	auto resaleClick = [=] {
		if (const auto upgraded = state->upgraded.current()) {
			ShowUniqueGiftSellBox(
				show,
				upgraded->info.unique,
				upgraded->manageId,
				{});
		}
	};
	auto gifts = state->upgraded.value(
	) | rpl::map([=](std::shared_ptr<Data::GiftUpgradeResult> upgraded) {
		return upgraded
			? rpl::single(Ui::UniqueGiftCover{
				.values = *upgraded->info.unique,
				.spinner = true,
			})
			: MakeUpgradeGiftStream(args);
	}) | rpl::flatten_latest();
	auto subtitle = state->upgraded.value(
	) | rpl::map([=](std::shared_ptr<Data::GiftUpgradeResult> upgraded) {
		return upgraded
			? rpl::single(tr::marked())
			: args.savedId
			? tr::lng_gift_upgrade_about(tr::marked)
			: (args.peer->isBroadcast()
				? tr::lng_gift_upgrade_preview_about_channel
				: tr::lng_gift_upgrade_preview_about)(
					lt_name,
					rpl::single(tr::marked(args.peer->shortName())),
					tr::marked);
	}) | rpl::flatten_latest();
	AddUniqueGiftCover(container, std::move(gifts), {
		.subtitle = std::move(subtitle),
		.resalePrice = std::move(resalePrice),
		.resaleClick = std::move(resaleClick),
		.upgradeSpinner = upgradeSpinner,
	});
}

void SwitchToUpgradedAnimation(
		not_null<Window::SessionController*> window,
		std::shared_ptr<Data::GiftUpgradeResult> gift,
		std::shared_ptr<Data::GiftUpgradeSpinner> spinner,
		not_null<GenericBox*> box,
		int contentStartIndex,
		std::optional<Data::SavedStarGift> upgradeNext) {
	Expects(gift != nullptr);
	Expects(gift->info.unique != nullptr);

	auto oldContent = QImage();
	const auto content = box->verticalLayout();
	const auto oldContentTill = content->height();
	const auto oldContentFrom = (content->count() > contentStartIndex)
		? content->widgetAt(contentStartIndex)->y()
		: oldContentTill;
	const auto oldContentHeight = oldContentTill - oldContentFrom;
	if (oldContentHeight > 0) {
		oldContent = GrabWidgetToImage(
			content,
			QRect(0, oldContentFrom, content->width(), oldContentHeight),
			st::giveawayGiftCodeBox.bg->c);
	}

	box->setStyle(st::giveawayGiftCodeBox);
	box->clearButtons();
	box->addButton(tr::lng_create_group_skip(), [=] {
		box->closeBox();
	});

	while (content->count() > contentStartIndex) {
		delete content->widgetAt(contentStartIndex);
	}

	const auto show = window->uiShow();
	const auto unique = gift->info.unique;
	const auto selfId = show->session().userPeerId();
	const auto chatGiftPeer = gift->manageId.chat();
	const auto nextToUpgradeStickerId = upgradeNext
		? upgradeNext->info.document->id
		: uint64();
	const auto nextToUpgradeShow = upgradeNext
		? [=] {
			Settings::ShowSavedStarGiftBox(
				window,
				window->session().data().peer(unique->ownerId),
				*upgradeNext);
		}
		: Fn<void()>();
	const auto entry = Data::CreditsHistoryEntry{
		//.description = data.message,
		.date = base::unixtime::parse(gift->date),
		.credits = CreditsAmount(gift->info.stars),
		.bareMsgId = uint64(gift->manageId.userMessageId().bare),
		//.barePeerId = data.fromId.value,
		.bareGiftStickerId = gift->info.document->id,
		.bareGiftOwnerId = unique->ownerId.value,
		.bareGiftHostId = unique->hostId.value,
		//.bareActorId = data.fromId.value,
		.bareEntryOwnerId = chatGiftPeer ? chatGiftPeer->id.value : 0,
		.giftChannelSavedId = gift->manageId.chatSavedId(),
		.stargiftId = gift->info.id,
		.giftTitle = gift->info.resellTitle,
		.uniqueGift = unique,
		.nextToUpgradeStickerId = nextToUpgradeStickerId,
		.nextToUpgradeShow = nextToUpgradeShow,
		.peerType = Data::CreditsHistoryEntry::PeerType::Peer,
		.limitedCount = gift->info.limitedCount,
		.limitedLeft = gift->info.limitedLeft,
		.starsToUpgrade = int(gift->info.starsToUpgrade),
		.starsForDetailsRemove = int(gift->starsForDetailsRemove),
		.giftNumber = unique->number,
		.converted = false,
		.stargift = true,
		.savedToProfile = gift->saved,
		.in = (unique->ownerId == selfId),
		.gift = true,
	};

	Settings::GenericCreditsEntryBody(
		box,
		show,
		entry,
		{},
		spinner);

	content->resizeToWidth(st::boxWideWidth);

	const auto nowContentTill = content->height();
	const auto nowContentFrom = (content->count() > contentStartIndex)
		? content->widgetAt(contentStartIndex)->y()
		: nowContentTill;
	if (const auto height = nowContentTill - nowContentFrom; height > 0) {
		const auto ratio = style::DevicePixelRatio();
		const auto fadeOutSize = QSize(content->width(), height);
		auto fadeOutImage = QImage(
			fadeOutSize * ratio,
			QImage::Format_ARGB32_Premultiplied);
		fadeOutImage.fill(st::giveawayGiftCodeBox.bg->c);
		if (oldContentHeight > 0) {
			QPainter p(&fadeOutImage);
			p.drawImage(0, 0, oldContent);
		}
		struct FadeOut {
			RpWidget *widget = nullptr;
			Animations::Simple animation;
		};
		const auto fadeOut = content->lifetime().make_state<FadeOut>();
		fadeOut->widget = CreateChild<RpWidget>(content);
		fadeOut->widget->show();
		fadeOut->widget->setGeometry(
			{ 0, nowContentFrom, content->width(), height });
		fadeOut->widget->paintOn([=](QPainter &p) {
			if (!fadeOut->animation.animating()) {
				crl::on_main(fadeOut->widget, [widget = fadeOut->widget] {
					delete widget;
				});
				return;
			}
			const auto progress = fadeOut->animation.value(1.);
			p.setOpacity(1. - progress);

			const auto shift = int(base::SafeRound(
				st::defaultVerticalListSkip * progress));
			if (shift > 0) {
				p.fillRect(
					QRect{ 0, 0, content->width(), shift },
					st::giveawayGiftCodeBox.bg);
			}
			p.drawImage(0, shift, fadeOutImage);
		});
		fadeOut->animation.start([=] {
			fadeOut->widget->update();
		}, 0., 1., st::slideWrapDuration);
	}
}

class UpgradePriceValue final {
public:
	UpgradePriceValue(
		not_null<Main::Session*> session,
		uint64 stargiftId,
		int cost,
		std::vector<UpgradePrice> all,
		std::vector<UpgradePrice> next);

	[[nodiscard]] int cost() const;
	[[nodiscard]] rpl::producer<int> costValue() const;
	[[nodiscard]] rpl::producer<TimeId> tillNextValue() const;

private:
	void update();
	void refresh();

	MTP::Sender _api;
	uint64 _stargiftId = 0;
	rpl::variable<int> _cost;
	rpl::variable<TimeId> _tillNext;
	std::vector<UpgradePrice> _all;
	std::vector<UpgradePrice> _next;
	base::Timer _timer;
	bool _refreshing = false;
	bool _finished = false;

};

UpgradePriceValue::UpgradePriceValue(
	not_null<Main::Session*> session,
	uint64 stargiftId,
	int cost,
	std::vector<UpgradePrice> all,
	std::vector<UpgradePrice> next)
: _api(&session->mtp())
, _stargiftId(stargiftId)
, _cost(cost)
, _all(std::move(all))
, _next(std::move(next))
, _timer([=] { update(); })
, _finished(_next.size() < 2) {
	update();
	_timer.callEach(1000);
}

int UpgradePriceValue::cost() const {
	return _cost.current();
}

rpl::producer<int> UpgradePriceValue::costValue() const {
	return _cost.value();
}

rpl::producer<TimeId> UpgradePriceValue::tillNextValue() const {
	return _tillNext.value();
}

void UpgradePriceValue::update() {
	if (_all.empty() || _next.empty()) {
		_timer.cancel();
		return;
	}
	const auto now = base::unixtime::now();
	const auto i = ranges::upper_bound(
		_all,
		now,
		ranges::less(),
		&UpgradePrice::date);
	const auto j = ranges::upper_bound(
		_next,
		now,
		ranges::less(),
		&UpgradePrice::date);
	const auto full = (i == begin(_all)) ? i->stars : (i - 1)->stars;
	const auto part = (j == begin(_next)) ? j->stars : (j - 1)->stars;
	const auto fullDate = (i != end(_all)) ? i->date : TimeId();
	const auto partDate = (j != end(_next)) ? j->date : TimeId();
	_cost = std::min({ _cost.current(), part, full});
	if (int(end(_next) - j) < 3) {
		refresh();
	}

	const auto next = std::min({
		partDate ? partDate : TimeId(INT_MAX),
		fullDate ? fullDate : TimeId(INT_MAX),
	});
	if (next != TimeId(INT_MAX)) {
		_tillNext = next - now;
	} else {
		_tillNext = 0;
		_timer.cancel();
	}
}

void UpgradePriceValue::refresh() {
	if (_refreshing || _finished) {
		return;
	}
	_api.request(MTPpayments_GetStarGiftUpgradePreview(
		MTP_long(_stargiftId)
	)).done([=](const MTPpayments_StarGiftUpgradePreview &result) {
		const auto &data = result.data();
		_all = ParsePrices(data.vprices());
		_next = ParsePrices(data.vnext_prices());
	}).fail([=] {
		_finished = true;
	}).send();
}

void PricesBox(
		not_null<Ui::GenericBox*> box,
		const std::vector<UpgradePrice> &list,
		rpl::producer<int> cost) {
	const auto top = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::boostBox);

	struct State {
		rpl::variable<int> cost;
	};
	const auto state = box->lifetime().make_state<State>();
	state->cost = std::move(cost);
	const auto min = list.back().stars;
	const auto max = std::max(min + 2, list.front().stars);
	const auto from = 0;
	const auto till = max - min;

	const auto addSkip = [&](int skip) {
		top->add(object_ptr<Ui::FixedHeightWidget>(top, skip));
	};

	const auto ratio = [=](int current) {
		current = std::clamp(current, from, till);
		const auto count = (till - from);
		const auto index = (current - from);
		if (count <= 2) {
			return 0.5;
		}
		const auto available = st::boxWideWidth
			- st::boxPadding.left()
			- st::boxPadding.right();
		const auto average = available / float64(count);
		const auto levelWidth = [&](int stars) {
			return st::normalFont->width(Lang::FormatCountDecimal(stars));
		};
		const auto paddings = 2 * st::premiumLineTextSkip;
		const auto labelLeftWidth = paddings + levelWidth(min);
		const auto labelRightWidth = paddings + levelWidth(max);
		const auto first = std::max(average, labelLeftWidth * 1.);
		const auto last = std::max(average, labelRightWidth * 1.);
		const auto other = (available - first - last) / (count - 2);
		return (first + (index - 1) * other) / available;
	};

	auto bubbleRowState = state->cost.value(
	) | rpl::map([=](int value) {
		return Premium::BubbleRowState{
			.counter = max - value,
			.ratio = ratio(max - value),
			.dynamic = true,
		};
	});
	Premium::AddBubbleRow(
		top,
		st::starRatingBubble,
		box->showFinishes(),
		rpl::duplicate(bubbleRowState),
		Ui::Premium::BubbleType::StarRating,
		[=](int value) {
			return Premium::BubbleText{
				.counter = Lang::FormatCountDecimal(max - value),
			};
		},
		&st::paidReactBubbleIcon,
		st::boxRowPadding);
	addSkip(st::premiumLineTextSkip);

	auto limitState = std::move(
		bubbleRowState
	) | rpl::map([=](const Premium::BubbleRowState &state) {
		return Premium::LimitRowState{
			.ratio = state.ratio,
			.dynamic = state.dynamic
		};
	});
	auto left = rpl::single(Lang::FormatCountDecimal(max));
	auto right = rpl::single(Lang::FormatCountDecimal(min));
	Premium::AddLimitRow(
		top,
		st::upgradePriceLimits,
		Premium::LimitRowLabels{
			.leftLabel = std::move(left),
			.rightLabel = std::move(right),
			.activeLineBg = [=] { return st::windowBgActive->b; },
		},
		std::move(limitState),
		st::boxRowPadding);

	top->add(
		object_ptr<Ui::FlatLabel>(
			top,
			tr::lng_gift_upgrade_prices_title(),
			st::infoStarsTitle),
		st::boxRowPadding + QMargins(0, st::boostTitleSkip / 2, 0, 0),
		style::al_top);
	top->add(
		object_ptr<Ui::FlatLabel>(
			top,
			tr::lng_gift_upgrade_prices_subtitle(),
			st::boostText),
		(st::boxRowPadding
			+ QMargins(0, st::boostTextSkip, 0, st::boostBottomSkip)),
		style::al_top
	)->setTryMakeSimilarLines(true);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto creditsIcon = helper.paletteDependent(
		Ui::Earn::IconCreditsEmoji());
	const auto context = helper.context();
	const auto table = box->addRow(
		object_ptr<Ui::TableLayout>(box, st::defaultTable),
		st::boxPadding);
	for (const auto &price : list) {
		const auto parsed = base::unixtime::parse(price.date);
		const auto time = QLocale().toString(
			parsed.time(),
			QLocale::ShortFormat);
		const auto date = tr::lng_month_day(
			tr::now,
			lt_month,
			Lang::MonthSmall(parsed.date().month())(tr::now),
			lt_day,
			QString::number(parsed.date().day()));
		table->addRow(
			object_ptr<Ui::FlatLabel>(
				table,
				time + u", "_q + date,
				st::defaultTable.defaultLabel),
			object_ptr<Ui::FlatLabel>(
				table,
				rpl::single(
					base::duplicate(creditsIcon).append(' ').append(
						Lang::FormatCountDecimal(price.stars))),
				st::defaultTable.defaultValue,
				st::defaultPopupMenu,
				context),
			st::giveawayGiftCodeLabelMargin,
			st::giveawayGiftCodeValueMargin);
	}
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_gift_upgrade_prices_about(),
			st::resalePriceAbout),
		style::al_top);
	box->addSkip(st::boxPadding.top());

	box->setMaxHeight(st::boxWideWidth);

	box->addButton(rpl::single(QString()), [=] {
		box->closeBox();
	})->setText(rpl::single(Ui::Text::IconEmoji(
		&st::infoStarsUnderstood
	).append(' ').append(tr::lng_stars_rating_understood(tr::now))));
}

void UpgradeBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> controller,
		UpgradeArgs &&args) {
	struct State {
		base::Timer timer;
		std::unique_ptr<UpgradePriceValue> cost;
		rpl::variable<bool> upgrading = false;
		rpl::event_stream<std::shared_ptr<Data::GiftUpgradeResult>> upgraded;
		Ui::FlatLabel *pricesLink = nullptr;
		bool preserveDetails = false;
		bool sent = false;
	};
	const auto state = std::make_shared<State>();

	box->setNoContentMargin(true);
	box->setWidth(st::boxWideWidth);

	const auto show = controller->uiShow();
	const auto container = box->verticalLayout();
	const auto spinner = std::make_shared<Data::GiftUpgradeSpinner>();
	spinner->attributes = Data::UniqueGiftAttributes{
		.models = args.models,
		.backdrops = args.backdrops,
		.patterns = args.patterns,
	};

	AddUpgradeGiftCover(
		show,
		container,
		args,
		spinner,
		state->upgraded.events());
	const auto contentIndex = container->count();

	AddSkip(container, st::defaultVerticalListSkip * 2);

	const auto infoRow = [&](
			rpl::producer<QString> title,
			rpl::producer<QString> text,
			not_null<const style::icon*> icon) {
		auto raw = container->add(
			object_ptr<Ui::VerticalLayout>(container));
		raw->add(
			object_ptr<Ui::FlatLabel>(
				raw,
				std::move(title) | rpl::map(tr::bold),
				st::defaultFlatLabel),
			st::settingsPremiumRowTitlePadding);
		raw->add(
			object_ptr<Ui::FlatLabel>(
				raw,
				std::move(text),
				st::upgradeGiftSubtext),
			st::settingsPremiumRowAboutPadding);
		object_ptr<Info::Profile::FloatingIcon>(
			raw,
			*icon,
			st::starrefInfoIconPosition);
	};

	infoRow(
		tr::lng_gift_upgrade_unique_title(),
		(args.savedId
			? tr::lng_gift_upgrade_unique_about()
			: (args.peer->isBroadcast()
				? tr::lng_gift_upgrade_unique_about_channel
				: tr::lng_gift_upgrade_unique_about_user)(
					lt_name,
					rpl::single(args.peer->shortName()))),
		&st::menuIconUnique);
	infoRow(
		tr::lng_gift_upgrade_transferable_title(),
		(args.savedId
			? tr::lng_gift_upgrade_transferable_about()
			: (args.peer->isBroadcast()
				? tr::lng_gift_upgrade_transferable_about_channel
				: tr::lng_gift_upgrade_transferable_about_user)(
					lt_name,
					rpl::single(args.peer->shortName()))),
		&st::menuIconReplace);
	infoRow(
		tr::lng_gift_upgrade_tradable_title(),
		(args.savedId
			? tr::lng_gift_upgrade_tradable_about()
			: (args.peer->isBroadcast()
				? tr::lng_gift_upgrade_tradable_about_channel
				: tr::lng_gift_upgrade_tradable_about_user)(
					lt_name,
					rpl::single(args.peer->shortName()))),
		&st::menuIconTradable);

	const auto gifting = !args.savedId
		&& !args.giftPrepayUpgradeHash.isEmpty();
	const auto preview = !args.savedId && !gifting;
	const auto showPrices = !preview
		&& (args.cost > 0)
		&& (args.prices.size() > 1)
		&& (args.nextPrices.size() > 1);
	auto prices = args.prices;
	state->cost = std::make_unique<UpgradePriceValue>(
		&controller->session(),
		args.stargift.id,
		args.cost,
		std::move(args.prices),
		std::move(args.nextPrices));
	if (!preview && !gifting) {
		const auto skip = st::defaultVerticalListSkip;
		container->add(
			object_ptr<PlainShadow>(container),
			st::boxRowPadding + QMargins(0, skip, 0, skip));
		const auto checkbox = container->add(
			object_ptr<Checkbox>(
				container,
				(args.canAddComment
					? tr::lng_gift_upgrade_add_comment(tr::now)
					: args.canAddSender
					? tr::lng_gift_upgrade_add_sender(tr::now)
					: args.canAddMyComment
					? tr::lng_gift_upgrade_add_my_comment(tr::now)
					: tr::lng_gift_upgrade_add_my(tr::now)),
				args.addDetailsDefault),
			st::defaultCheckbox.margin,
			style::al_top);
		checkbox->checkedChanges() | rpl::on_next([=](bool checked) {
			state->preserveDetails = checked;
		}, checkbox->lifetime());
	}

	box->setStyle(preview
		? st::giftBox
		: showPrices
		? st::upgradeGiftWithPricesBox
		: st::upgradeGiftBox);
	if (gifting) {
		box->setWidth(st::boxWideWidth);
	}
	auto buttonText = preview ? tr::lng_box_ok() : rpl::single(QString());
	const auto button = box->addButton(std::move(buttonText), [=] {
		if (preview) {
			box->closeBox();
			return;
		} else if (state->sent) {
			return;
		}
		state->sent = true;
		const auto weakBox = base::make_weak(box);
		const auto session = &controller->session();
		const auto weakWindow = base::make_weak(controller);
		const auto cost = state->cost->cost();
		const auto keepDetails = state->preserveDetails;
		if (gifting) {
			const auto done = [=](bool success) {
				state->upgrading = false;
				if (!success) {
					state->sent = false;
				} else {
					if (const auto window = weakWindow.get()) {
						window->showPeerHistory(args.peer);
					}
					if (const auto strong = weakBox.get()) {
						strong->closeBox();
					}
				}
			};
			GiftUpgrade(
				controller,
				args.peer,
				args.giftPrepayUpgradeHash,
				cost,
				done);
		} else {
			const auto done = [=](
					bool success,
					std::shared_ptr<Data::GiftUpgradeResult> gift) {
				const auto ownerId = (gift && gift->info.unique)
					? gift->info.unique->ownerId
					: PeerId();
				const auto owner = ownerId
					? session->data().peer(ownerId).get()
					: nullptr;
				if (!success) {
					if (weakBox) {
						state->upgrading = false;
						state->sent = false;
					}
				} else if (owner) {
					using State = Data::GiftUpgradeSpinner::State;
					if (const auto onstack = args.upgraded) {
						onstack();
					}
					if (weakBox.get()) {
						spinner->target = gift->info.unique;
						spinner->state = State::Preparing;
					}
					if (weakWindow.get()) {
						session->data().nextForUpgradeGiftRequest(
							owner,
							[=](std::optional<Data::SavedStarGift> next) {
								if (const auto box = weakBox.get()) {
									spinner->state.value() | rpl::filter(
										rpl::mappers::_1 == State::Prepared
									) | rpl::take(1) | rpl::on_next([=] {
										state->upgrading = false;
										state->upgraded.fire_copy(gift);

										delete base::take(state->pricesLink);
										SwitchToUpgradedAnimation(
											weakWindow.get(),
											gift,
											spinner,
											box,
											contentIndex,
											next);
									}, box->lifetime());
								} else if (const auto w = weakWindow.get()) {
									ShowGiftUpgradedToast(
										w,
										session,
										gift->info.unique);
								}
							});
					}
				} else {
					if (const auto window = weakWindow.get()) {
						window->showPeerHistory(args.peer);
					}
					if (const auto strong = weakBox.get()) {
						strong->closeBox();
					}
				}
			};
			state->upgrading = true;
			UpgradeGift(controller, args.savedId, keepDetails, cost, done);

			//AssertIsDebug();
			//const auto modelIndex = base::RandomIndex(args.models.size());
			//const auto patternIndex = base::RandomIndex(args.patterns.size());
			//const auto backdropIndex = base::RandomIndex(args.backdrops.size());
			//const auto result = std::make_shared<Data::GiftUpgradeResult>(Data::GiftUpgradeResult{
			//	.info = args.stargift,
			//	.manageId = Data::SavedStarGiftId::User(123),
			//	.date = base::unixtime::now(),
			//	.starsForDetailsRemove = 119,
			//	.saved = true,
			//});
			//result->info.unique = std::make_shared<Data::UniqueGift>(Data::UniqueGift{
			//	.id = 1,
			//	.initialGiftId = args.stargift.id,
			//	.slug = u"hello_world"_q,
			//	.title = u"Shard Test"_q,
			//	.ownerId = session->userPeerId(),
			//	.releasedBy = show->session().user(),
			//	.model = args.models[modelIndex],
			//	.pattern = args.patterns[patternIndex],
			//	.backdrop = args.backdrops[backdropIndex],
			//});
			//base::call_delayed(3000, [=] {
			//	done(true, result);
			//});
		}
	});
	if (!preview) {
		using namespace Info::Statistics;
		const auto loadingAnimation = InfiniteRadialAnimationWidget(
			button,
			st::giveawayGiftCodeStartButton.height / 2);
		AddChildToWidgetCenter(button.data(), loadingAnimation);
		loadingAnimation->showOn(state->upgrading.value());

		auto costText = [=] {
			return state->cost->costValue(
			) | rpl::map([](int cost) {
				if (!cost) {
					return tr::lng_gift_upgrade_confirm(
						tr::marked);
				}
				return tr::lng_gift_upgrade_button(
					lt_price,
					rpl::single(Ui::Text::IconEmoji(
						&st::starIconEmoji
					).append(Lang::FormatCreditsAmountDecimal(
						CreditsAmount{ cost }))),
					tr::marked);
			}) | rpl::flatten_latest();
		};
		auto subtext = state->cost->tillNextValue(
		) | rpl::map([](TimeId left) {
			if (!left) {
				return QString();
			}
			const auto hours = left / 3600;
			const auto minutes = (left % 3600) / 60;
			const auto seconds = left % 60;
			const auto padded = [](int value) {
				return u"%1"_q.arg(value, 2, 10, QChar('0'));
			};
			const auto text = hours
				? u"%1:%2:%3"_q
				.arg(hours).arg(padded(minutes)).arg(padded(seconds))
				: u"%2:%3"_q.arg(padded(minutes)).arg(padded(seconds));
			return tr::lng_gift_upgrade_decreases(tr::now, lt_time, text);
		}) | rpl::map(tr::marked);
		Ui::SetButtonTwoLabels(
			button,
			rpl::conditional(
				state->upgrading.value(),
				rpl::single(tr::marked()),
				costText()),
			rpl::conditional(
				(showPrices
					? state->upgrading.value()
					: (rpl::single(true) | rpl::type_erased)),
				rpl::single(tr::marked()),
				std::move(subtext)),
			st::resaleButtonTitle,
			st::resaleButtonSubtitle);
	}
	if (showPrices) {
		const auto link = state->pricesLink = Ui::CreateChild<Ui::FlatLabel>(
			button->parentWidget(),
			tr::lng_gift_upgrade_see_table(
				lt_arrow,
				rpl::single(Ui::Text::IconEmoji(&st::textMoreIconEmoji)),
				[](QString text) { return tr::link(text); }),
			st::resalePriceTableLink);
		link->setTryMakeSimilarLines(true);
		button->geometryValue() | rpl::on_next([=](QRect geometry) {
			const auto outer = button->parentWidget()->height();
			const auto top = geometry.y() + geometry.height();
			const auto available = outer - top - st::boxRadius;
			link->resizeToWidth(geometry.width());
			link->move(geometry.x(), top + (available - link->height()) / 2);
		}, link->lifetime());
		link->setClickHandlerFilter([=, list = prices](const auto &...) {
			controller->show(Box(PricesBox, list, state->cost->costValue()));
			return false;
		});
	}

	AddUniqueCloseButton(box, {});

	spinner->state.value() | rpl::filter(
		rpl::mappers::_1 == Data::GiftUpgradeSpinner::State::Finished
	) | rpl::take(1) | rpl::on_next([=] {
		Ui::StartFireworks(box->parentWidget());
	}, box->lifetime());
}

void ShowStarGiftUpgradeBox(StarGiftUpgradeArgs &&args) {
	const auto weak = base::make_weak(args.controller);
	const auto session = &args.peer->session();
	session->api().request(MTPpayments_GetStarGiftUpgradePreview(
		MTP_long(args.stargift.id)
	)).done([=](const MTPpayments_StarGiftUpgradePreview &result) {
		const auto strong = weak.get();
		if (!strong) {
			if (const auto onstack = args.ready) {
				onstack(false);
			}
			return;
		}
		const auto &data = result.data();
		auto upgrade = UpgradeArgs{ args };
		upgrade.prices = ParsePrices(data.vprices());
		upgrade.nextPrices = ParsePrices(data.vnext_prices());
		for (const auto &attribute : data.vsample_attributes().v) {
			attribute.match([&](const MTPDstarGiftAttributeModel &data) {
				upgrade.models.push_back(Api::FromTL(session, data));
			}, [&](const MTPDstarGiftAttributePattern &data) {
				upgrade.patterns.push_back(Api::FromTL(session, data));
			}, [&](const MTPDstarGiftAttributeBackdrop &data) {
				upgrade.backdrops.push_back(Api::FromTL(data));
			}, [](const auto &) {});
		}
		strong->show(Box(UpgradeBox, strong, std::move(upgrade)));
		if (const auto onstack = args.ready) {
			onstack(true);
		}
	}).fail([=](const MTP::Error &error) {
		if (const auto strong = weak.get()) {
			strong->showToast(error.type());
		}
		if (const auto onstack = args.ready) {
			onstack(false);
		}
	}).send();
}

void AddUniqueCloseButton(
		not_null<GenericBox*> box,
		Settings::CreditsEntryBoxStyleOverrides st,
		Fn<void(not_null<Ui::PopupMenu*>)> fillMenu) {
	const auto close = Ui::CreateChild<IconButton>(
		box,
		st::uniqueCloseButton);
	const auto menu = fillMenu
		? Ui::CreateChild<IconButton>(box, st::uniqueMenuButton)
		: nullptr;
	close->show();
	close->raise();
	if (menu) {
		menu->show();
		menu->raise();
	}
	box->widthValue() | rpl::on_next([=](int width) {
		close->moveToRight(0, 0, width);
		close->raise();
		if (menu) {
			menu->moveToRight(close->width(), 0, width);
			menu->raise();
		}
	}, close->lifetime());
	close->setClickedCallback([=] {
		box->closeBox();
	});
	if (menu) {
		const auto state = menu->lifetime().make_state<
			base::unique_qptr<Ui::PopupMenu>
		>();
		menu->setClickedCallback([=] {
			if (*state) {
				*state = nullptr;
				return;
			}
			*state = base::make_unique_q<Ui::PopupMenu>(
				menu,
				st.menu ? *st.menu : st::popupMenuWithIcons);
			fillMenu(state->get());
			if (!(*state)->empty()) {
				(*state)->popup(QCursor::pos());
			}
		});
	}
}

void SubmitStarsForm(
		std::shared_ptr<Main::SessionShow> show,
		MTPInputInvoice invoice,
		uint64 formId,
		uint64 price,
		Fn<void(Payments::CheckoutResult, const MTPUpdates *)> done) {
	const auto ready = [=](Settings::SmallBalanceResult result) {
		SendStarsFormRequest(show, result, formId, invoice, done);
	};
	Settings::MaybeRequestBalanceIncrease(
		show,
		price,
		Settings::SmallBalanceDeepLink{},
		ready);
}

void SubmitTonForm(
		std::shared_ptr<Main::SessionShow> show,
		MTPInputInvoice invoice,
		uint64 formId,
		CreditsAmount ton,
		Fn<void(Payments::CheckoutResult, const MTPUpdates *)> done) {
	const auto ready = [=] {
		SendStarsFormRequest(
			show,
			Settings::SmallBalanceResult::Already,
			formId,
			invoice,
			done);
	};
	struct State {
		rpl::lifetime lifetime;
		bool success = false;
	};
	const auto state = std::make_shared<State>();

	const auto session = &show->session();
	session->credits().tonLoad();
	session->credits().tonLoadedValue(
	) | rpl::filter(rpl::mappers::_1) | rpl::on_next([=] {
		state->lifetime.destroy();

		if (session->credits().tonBalance() < ton) {
			show->show(Box(InsufficientTonBox, session->user(), ton));
		} else {
			ready();
		}
	}, state->lifetime);
}

void RequestOurForm(
	std::shared_ptr<Main::SessionShow> show,
	MTPInputInvoice invoice,
	Fn<void(
		uint64 formId,
		CreditsAmount price,
		std::optional<Payments::CheckoutResult> failure)> done) {
	const auto fail = [=](Payments::CheckoutResult failure) {
		done(0, {}, failure);
	};
	show->session().api().request(MTPpayments_GetPaymentForm(
		MTP_flags(0),
		invoice,
		MTPDataJSON() // theme_params
	)).done([=](const MTPpayments_PaymentForm &result) {
		result.match([&](const MTPDpayments_paymentFormStarGift &data) {
			const auto &invoice = data.vinvoice().data();
			const auto prices = invoice.vprices().v;
			if (show->valid() && !prices.isEmpty()) {
				const auto price = prices.front().data().vamount().v;
				const auto currency = qs(invoice.vcurrency());
				const auto amount = (currency == Ui::kCreditsCurrency)
					? CreditsAmount(price)
					: (currency == u"TON"_q)
					? CreditsAmount(
						price / Ui::kNanosInOne,
						price % Ui::kNanosInOne,
						CreditsType::Ton)
					: std::optional<CreditsAmount>();
				if (amount) {
					done(data.vform_id().v, *amount, std::nullopt);
				} else {
					fail(Payments::CheckoutResult::Failed);
				}
			} else {
				fail(Payments::CheckoutResult::Failed);
			}
		}, [&](const MTPDpayments_paymentFormStars &data) {
			show->session().data().processUsers(data.vusers());
			const auto currency = qs(data.vinvoice().data().vcurrency());
			const auto &prices = data.vinvoice().data().vprices().v;
			if (!prices.empty()) {
				const auto price = prices.front().data().vamount().v;
				const auto amount = (currency == Ui::kCreditsCurrency)
					? CreditsAmount(price)
					: (currency == u"TON"_q)
					? CreditsAmount(
						price / Ui::kNanosInOne,
						price % Ui::kNanosInOne,
						CreditsType::Ton)
					: std::optional<CreditsAmount>();
				if (amount) {
					done(data.vform_id().v, *amount, std::nullopt);
				} else {
					fail(Payments::CheckoutResult::Failed);
				}
			} else {
				fail(Payments::CheckoutResult::Failed);
			}
		}, [&](const auto &) {
			fail(Payments::CheckoutResult::Failed);
		});
	}).fail([=](const MTP::Error &error) {
		const auto type = error.type();
		if (type == u"STARGIFT_EXPORT_IN_PROGRESS"_q) {
			fail(Payments::CheckoutResult::Cancelled);
		} else if (type == u"NO_PAYMENT_NEEDED"_q) {
			fail(Payments::CheckoutResult::Free);
		} else if (type == u"USER_DISALLOWED_STARGIFTS"_q) {
			show->showToast(tr::lng_edit_privacy_gifts_restricted(tr::now));
			fail(Payments::CheckoutResult::Cancelled);
		} else {
			show->showToast(type);
			fail(Payments::CheckoutResult::Failed);
		}
	}).send();
}

void RequestStarsFormAndSubmit(
		std::shared_ptr<Main::SessionShow> show,
		MTPInputInvoice invoice,
		Fn<void(Payments::CheckoutResult, const MTPUpdates *)> done) {
	RequestOurForm(show, invoice, [=](
			uint64 formId,
			CreditsAmount price,
			std::optional<Payments::CheckoutResult> failure) {
		if (failure) {
			done(*failure, nullptr);
		} else if (!price.stars()) {
			done(Payments::CheckoutResult::Failed, nullptr);
		} else {
			SubmitStarsForm(show, invoice, formId, price.whole(), done);
		}
	});
}

void ShowGiftTransferredToast(
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> to,
		const Data::UniqueGift &gift) {
	show->showToast({
		.title = tr::lng_gift_transferred_title(tr::now),
		.text = tr::lng_gift_transferred_about(
			tr::now,
				lt_name,
				tr::bold(Data::UniqueGiftName(gift)),
				lt_recipient,
				tr::bold(to->shortName()),
				tr::marked),
		.duration = kUpgradeDoneToastDuration,
	});
}

CreditsAmount StarsFromTon(
		not_null<Main::Session*> session,
		CreditsAmount ton) {
	const auto appConfig = &session->appConfig();
	const auto starsRate = appConfig->starsSellRate() / 100.;
	const auto tonRate = appConfig->currencySellRate();
	if (!starsRate) {
		return {};
	}
	const auto count = (ton.value() * tonRate) / starsRate;
	return CreditsAmount(int(base::SafeRound(count)));
}

CreditsAmount TonFromStars(
		not_null<Main::Session*> session,
		CreditsAmount stars) {
	const auto appConfig = &session->appConfig();
	const auto starsRate = appConfig->starsSellRate() / 100.;
	const auto tonRate = appConfig->currencySellRate();
	if (!tonRate) {
		return {};
	}
	const auto count = (stars.value() * starsRate) / tonRate;
	const auto whole = int(std::floor(count));
	const auto cents = int(base::SafeRound((count - whole) * 100));
	return CreditsAmount(
		whole,
		cents * (Ui::kNanosInOne / 100),
		CreditsType::Ton);
}

object_ptr<RpWidget> MakeGiftsSendList(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		rpl::producer<GiftsDescriptor> gifts,
		Fn<void()> loadMore) {
	return MakeGiftsList(
		window,
		peer,
		std::move(gifts),
		std::move(loadMore));
}

void SendGiftBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		std::shared_ptr<Api::PremiumGiftCodeOptions> api,
		const GiftDescriptor &descriptor,
		rpl::producer<Data::GiftAuctionState> auctionState) {
	const auto stars = std::get_if<GiftTypeStars>(&descriptor);
	const auto auction = !!auctionState;
	const auto limited = stars
		&& (stars->info.limitedCount > stars->info.limitedLeft)
		&& (stars->info.limitedLeft > 0);
	const auto costToUpgrade = stars ? stars->info.starsToUpgrade : 0;
	const auto user = peer->asUser();
	const auto disallowed = user
		? user->disallowedGiftTypes()
		: Api::DisallowedGiftTypes();
	const auto disallowLimited = !peer->isSelf()
		&& (disallowed & Api::DisallowedGiftType::Limited);
	box->setStyle((limited && !auction) ? st::giftLimitedBox : st::giftBox);
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_gift_send_title());
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	const auto session = &window->session();

	struct State {
		rpl::variable<GiftSendDetails> details;
		rpl::variable<bool> messageAllowed;
		std::shared_ptr<Data::DocumentMedia> media;
		rpl::variable<Data::GiftAuctionState> auction;
		bool submitting = false;
	};
	const auto state = box->lifetime().make_state<State>();
	if (auctionState) {
		state->auction = std::move(auctionState);
	}
	state->details = GiftSendDetails{
		.descriptor = descriptor,
		.randomId = base::RandomValue<uint64>(),
		.upgraded = disallowLimited && (costToUpgrade > 0),
	};
	peer->updateFull();
	state->messageAllowed = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::StarsPerMessage
	) | rpl::map([=] {
		return peer->starsPerMessageChecked() == 0;
	});

	auto cost = state->details.value(
	) | rpl::map([](const GiftSendDetails &details) {
		return v::match(details.descriptor, [&](const GiftTypePremium &data) {
			const auto stars = (details.byStars && data.stars)
				? data.stars
				: (data.currency == kCreditsCurrency)
				? data.cost
				: 0;
			if (stars) {
				return CreditsEmojiSmall().append(
					Lang::FormatCountDecimal(std::abs(stars)));
			}
			return TextWithEntities{
				FillAmountAndCurrency(data.cost, data.currency),
			};
		}, [&](const GiftTypeStars &data) {
			const auto amount = std::abs(data.info.stars)
				+ (details.upgraded ? data.info.starsToUpgrade : 0);
			return CreditsEmojiSmall().append(
				Lang::FormatCountDecimal(amount));
		});
	});

	const auto document = LookupGiftSticker(session, descriptor);
	if ((state->media = document ? document->createMediaView() : nullptr)) {
		state->media->checkStickerLarge();
	}

	const auto container = box->verticalLayout();
	container->add(object_ptr<PreviewWrap>(
		container,
		peer,
		state->details.value()));

	const auto messageWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	messageWrap->toggleOn(state->messageAllowed.value());
	messageWrap->finishAnimating();
	const auto messageInner = messageWrap->entity();
	const auto limit = StarGiftMessageLimit(session);
	const auto text = AddPartInput(
		window,
		messageInner,
		box->getDelegate()->outerContainer(),
		tr::lng_gift_send_message(),
		QString(),
		limit);
	text->changes() | rpl::on_next([=] {
		auto now = state->details.current();
		auto textWithTags = text->getTextWithAppliedMarkdown();
		now.text = TextWithEntities{
			std::move(textWithTags.text),
			TextUtilities::ConvertTextTagsToEntities(textWithTags.tags)
		};
		state->details = std::move(now);
	}, text->lifetime());

	box->setFocusCallback([=] {
		text->setFocusFast();
	});

	const auto allow = [=](not_null<DocumentData*> emoji) {
		return true;
	};
	InitMessageFieldHandlers({
		.session = session,
		.show = window->uiShow(),
		.field = text,
		.customEmojiPaused = [=] {
			using namespace Window;
			return window->isGifPausedAtLeastFor(GifPauseReason::Layer);
		},
		.allowPremiumEmoji = allow,
		.allowMarkdownTags = {
			InputField::kTagBold,
			InputField::kTagItalic,
			InputField::kTagUnderline,
			InputField::kTagStrikeOut,
			InputField::kTagSpoiler,
		}
	});
	Emoji::SuggestionsController::Init(
		box->getDelegate()->outerContainer(),
		text,
		session,
		{ .suggestCustomEmoji = true, .allowCustomWithoutPremium = allow });
	if (stars) {
		if (costToUpgrade > 0 && !peer->isSelf() && !disallowLimited) {
			const auto stargiftInfo = stars->info;
			const auto showing = std::make_shared<bool>();
			AddDivider(container);
			AddSkip(container);
			AddUpgradeButton(container, costToUpgrade, peer, [=](bool on) {
				auto now = state->details.current();
				now.upgraded = on;
				state->details = std::move(now);
			}, [=] {
				if (*showing) {
					return;
				}
				*showing = true;
				ShowStarGiftUpgradeBox({
					.controller = window,
					.stargift = stargiftInfo,
					.ready = [=](bool) { *showing = false; },
					.peer = peer,
					.cost = int(costToUpgrade),
				});
			});
		} else {
			AddDivider(container);
		}
		AddSkip(container);
		container->add(
			object_ptr<SettingsButton>(
				container,
				tr::lng_gift_send_anonymous(),
				st::settingsButtonNoIcon)
		)->toggleOn(rpl::single(peer->isSelf()))->toggledValue(
		) | rpl::on_next([=](bool toggled) {
			auto now = state->details.current();
			now.anonymous = toggled;
			state->details = std::move(now);
		}, container->lifetime());
		AddSkip(container);
	}
	v::match(descriptor, [&](const GiftTypePremium &data) {
		AddDividerText(messageInner, tr::lng_gift_send_premium_about(
			lt_user,
			rpl::single(peer->shortName())));

		if (const auto byStars = data.stars) {
			const auto star = Ui::Text::IconEmoji(&st::starIconEmojiColored);
			AddSkip(container);
			container->add(
				object_ptr<SettingsButton>(
					container,
					tr::lng_gift_send_pay_with_stars(
						lt_amount,
						rpl::single(base::duplicate(star).append(Lang::FormatCountDecimal(byStars))),
						tr::marked),
						st::settingsButtonNoIcon)
			)->toggleOn(rpl::single(false))->toggledValue(
			) | rpl::on_next([=](bool toggled) {
				auto now = state->details.current();
				now.byStars = toggled;
				state->details = std::move(now);
			}, container->lifetime());
			AddSkip(container);

			const auto balance = AddDividerText(
				container,
				tr::lng_gift_send_stars_balance(
					lt_amount,
					peer->session().credits().balanceValue(
					) | rpl::map([=](CreditsAmount amount) {
						return base::duplicate(star).append(
							Lang::FormatCreditsAmountDecimal(amount));
					}),
					lt_link,
					tr::lng_gift_send_stars_balance_link(tr::link),
					tr::marked));
			struct State {
				Settings::BuyStarsHandler buyStars;
				rpl::variable<bool> loading;
			};
			const auto state = balance->lifetime().make_state<State>();
			state->loading = state->buyStars.loadingValue();
			balance->setClickHandlerFilter([=](const auto &...) {
				if (!state->loading.current()) {
					state->buyStars.handler(window->uiShow())();
				}
				return false;
			});
		}
	}, [&](const GiftTypeStars &) {
		AddDividerText(container, peer->isSelf()
			? tr::lng_gift_send_anonymous_self()
			: peer->isBroadcast()
			? tr::lng_gift_send_anonymous_about_channel()
			: rpl::conditional(
				state->messageAllowed.value(),
				tr::lng_gift_send_anonymous_about(
					lt_user,
					rpl::single(peer->shortName()),
					lt_recipient,
					rpl::single(peer->shortName())),
				tr::lng_gift_send_anonymous_about_paid(
					lt_user,
					rpl::single(peer->shortName()),
					lt_recipient,
					rpl::single(peer->shortName()))));
	});

	const auto button = box->addButton(rpl::single(QString()), [=] {
		if (state->submitting) {
			return;
		}
		state->submitting = true;
		auto details = state->details.current();
		if (!state->messageAllowed.current()) {
			details.text = {};
		}
		const auto stars = std::get_if<GiftTypeStars>(&details.descriptor);
		if (stars && stars->info.auction()) {
			const auto bidBox = window->show(MakeAuctionBidBox({
				.peer = peer,
				.show = window->uiShow(),
				.state = state->auction.value(),
				.details = std::make_unique<GiftSendDetails>(details),
			}));
			bidBox->boxClosing(
			) | rpl::on_next([=] {
				box->closeBox();
			}, box->lifetime());
			return;
		}
		const auto copy = state->media; // Let media outlive the box.
		const auto weak = base::make_weak(box);
		const auto done = [=](Payments::CheckoutResult result) {
			if (result == Payments::CheckoutResult::Paid) {
				if (details.byStars
					|| v::is<GiftTypeStars>(details.descriptor)) {
					window->session().credits().load(true);
				}
				const auto another = copy; // Let media outlive the box.
				window->showPeerHistory(peer);
				ShowSentToast(window, details.descriptor, details);
			}
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		};
		SendGift(window, peer, api, details, done);
	});
	if (limited) {
		if (auction) {
			const auto &now = state->auction.current();
			const auto rounds = now.totalRounds;
			const auto perRound = now.gift->auctionGiftsPerRound;
			auto owned = object_ptr<Ui::FlatLabel>(
				container,
				rpl::single(tr::lng_auction_about_top_short(
					tr::now,
					lt_count,
					perRound,
					lt_bidders,
					tr::lng_auction_about_top_bidders(
						tr::now,
						lt_count,
						perRound,
						tr::rich),
					lt_link,
					tr::lng_auction_text_link(
						tr::now,
						lt_arrow,
						Text::IconEmoji(&st::textMoreIconEmoji),
						tr::link),
					tr::rich)),
				st::defaultDividerLabel.label);
			const auto label = owned.data();
			const auto about = container->add(
				object_ptr<Ui::DividerLabel>(
					container,
					std::move(owned),
					st::defaultBoxDividerLabelPadding),
				{ 0, st::giftLimitedBox.buttonPadding.top(), 0, 0 });
			AddSoldLeftSlider(about, *stars, st::boxRowPadding);

			const auto show = window->uiShow();
			label->setClickHandlerFilter([=](const auto &...) {
				show->show(Box(AuctionAboutBox, rounds, perRound, nullptr));
				return false;
			});
		} else {
			AddSoldLeftSlider(button, *stars);
		}
	}
	if (stars && stars->info.auction()) {
		SetAuctionButtonCountdownText(
			button,
			AuctionButtonCountdownType::Place,
			state->auction.value());
	} else {
		SetButtonMarkedLabel(
			button,
			(peer->isSelf()
				? tr::lng_gift_send_button_self
				: tr::lng_gift_send_button)(
					lt_cost,
					std::move(cost),
					tr::marked),
			session,
			st::creditsBoxButtonLabel,
			&st::giftBox.button.textFg);
	}
}

} // namespace Ui
