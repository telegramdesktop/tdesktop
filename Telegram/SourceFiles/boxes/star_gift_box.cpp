/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_box.h"

#include "apiwrap.h"
#include "api/api_credits.h"
#include "api/api_premium.h"
#include "base/event_filter.h"
#include "base/random.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "boxes/peers/edit_peer_color_box.h"
#include "boxes/gift_premium_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/premium_preview_box.h"
#include "boxes/send_credits_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/ui_integration.h"
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
#include "history/view/media/history_view_media_generic.h"
#include "history/view/media/history_view_unique_gift.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "info/profile/info_profile_icon.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/payments_form.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_non_panel_process.h"
#include "settings/settings_credits.h"
#include "settings/settings_credits_graphics.h"
#include "settings/settings_premium.h"
#include "ui/boxes/boost_box.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/layers/generic_box.h"
#include "ui/new_badges.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kPriceTabAll = 0;
constexpr auto kPriceTabLimited = -2;
constexpr auto kPriceTabInStock = -1;
constexpr auto kGiftMessageLimit = 255;
constexpr auto kSentToastDuration = 3 * crl::time(1000);
constexpr auto kSwitchUpgradeCoverInterval = 3 * crl::time(1000);
constexpr auto kCrossfadeDuration = crl::time(400);
constexpr auto kUpgradeDoneToastDuration = 4 * crl::time(1000);

using namespace HistoryView;
using namespace Info::PeerGifts;

struct PremiumGiftsDescriptor {
	std::vector<GiftTypePremium> list;
	std::shared_ptr<Api::PremiumGiftCodeOptions> api;
};

struct GiftsDescriptor {
	std::vector<GiftDescriptor> list;
	std::shared_ptr<Api::PremiumGiftCodeOptions> api;
};

struct GiftDetails {
	GiftDescriptor descriptor;
	TextWithEntities text;
	uint64 randomId = 0;
	bool anonymous = false;
	bool upgraded = false;
};

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
		rpl::producer<GiftDetails> details);
	~PreviewWrap();

private:
	void paintEvent(QPaintEvent *e) override;

	void resizeTo(int width);
	void prepare(rpl::producer<GiftDetails> details);

	const not_null<History*> _history;
	const not_null<PeerData*> _recipient;
	const std::unique_ptr<ChatTheme> _theme;
	const std::unique_ptr<ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;
	AdminLog::OwnedItem _item;
	QPoint _position;

};

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
	const GiftDetails &data)
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
				const std::any &context = {}) {
			if (text.empty()) {
				return;
			}
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				st::defaultTextStyle,
				links,
				context));
		};

		const auto sticker = [=] {
			using Tag = ChatHelpers::StickerLottieSize;
			const auto session = &parent->history()->session();
			const auto sticker = LookupGiftSticker(session, descriptor);
			return StickerInBubblePart::Data{
				.sticker = sticker,
				.size = st::chatIntroStickerSize,
				.cacheTag = Tag::ChatIntroHelloSticker,
				.singleTimePlayback = v::is<GiftTypePremium>(descriptor),
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
				Text::Bold);
		}, [&](const GiftTypeStars &gift) {
			return recipient->isSelf()
				? tr::lng_action_gift_self_subtitle(tr::now, Text::Bold)
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
					Text::Bold);
		});
		auto textFallback = v::match(descriptor, [&](GiftTypePremium gift) {
			return tr::lng_action_gift_premium_about(
				tr::now,
				Text::RichLangValue);
		}, [&](const GiftTypeStars &gift) {
			return data.upgraded
				? tr::lng_action_gift_got_upgradable_text(
					tr::now,
					Text::RichLangValue)
				: (recipient->isSelf() && gift.info.starsToUpgrade)
				? tr::lng_action_gift_self_about_unique(
					tr::now,
					Text::RichLangValue)
				: (recipient->isBroadcast() && gift.info.starsToUpgrade)
				? tr::lng_action_gift_channel_about_unique(
					tr::now,
					Text::RichLangValue)
				: (recipient->isSelf()
					? tr::lng_action_gift_self_about
					: recipient->isBroadcast()
					? tr::lng_action_gift_channel_about
					: tr::lng_action_gift_got_stars_text)(
						tr::now,
						lt_count,
						gift.info.starsConverted,
						Text::RichLangValue);
		});
		auto description = data.text.empty()
			? std::move(textFallback)
			: data.text;
		const auto context = Core::MarkedTextContext{
			.session = &parent->history()->session(),
			.customEmojiRepaint = [parent] { parent->repaint(); },
		};
		pushText(
			std::move(title),
			st::giftBoxPreviewTitlePadding,
			{},
			context);
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

[[nodiscard]] QImage CreateGradient(
		QSize size,
		const Data::UniqueGift &gift) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);

	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	auto gradient = QRadialGradient(
		QRect(QPoint(), size).center(),
		size.height() / 2);
	gradient.setStops({
		{ 0., gift.backdrop.centerColor },
		{ 1., gift.backdrop.edgeColor },
	});
	p.setBrush(gradient);
	p.setPen(Qt::NoPen);
	p.drawRect(QRect(QPoint(), size));
	p.end();

	const auto mask = Images::CornersMask(st::boxRadius);
	return Images::Round(std::move(result), mask, RectPart::FullTop);
}

void PrepareImage(
		QImage &image,
		not_null<Text::CustomEmoji*> emoji,
		const PatternPoint &point,
		const Data::UniqueGift &gift) {
	if (!image.isNull() || !emoji->ready()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto size = Emoji::GetSizeNormal() / ratio;
	image = QImage(
		2 * QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	auto p = QPainter(&image);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(point.opacity);
	if (point.scale < 1.) {
		p.translate(size, size);
		p.scale(point.scale, point.scale);
		p.translate(-size, -size);
	}
	const auto shift = (2 * size - (Emoji::GetSizeLarge() / ratio)) / 2;
	emoji->paint(p, {
		.textColor = gift.backdrop.patternColor,
		.position = QPoint(shift, shift),
	});
}

PreviewWrap::PreviewWrap(
	not_null<QWidget*> parent,
	not_null<PeerData*> recipient,
	rpl::producer<GiftDetails> details)
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
	) | rpl::start_with_next([=](not_null<const Element*> view) {
		if (view == _item.get()) {
			update();
		}
	}, lifetime());

	_history->session().downloaderTaskFinished() | rpl::start_with_next([=] {
		update();
	}, lifetime());

	prepare(std::move(details));
}

void ShowSentToast(
		not_null<Window::SessionController*> window,
		const GiftDescriptor &descriptor,
		const GiftDetails &details) {
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
			Text::RichLangValue);
	}, [&](const GiftTypeStars &gift) {
		const auto amount = gift.info.stars
			+ (details.upgraded ? gift.info.starsToUpgrade : 0);
		return tr::lng_gift_sent_about(
			tr::now,
			lt_count,
			amount,
			Text::RichLangValue);
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
	) | rpl::start_with_next([=] {
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
	) | rpl::start_with_next([=] {
		preview->update();
	}, preview->lifetime());
}

PreviewWrap::~PreviewWrap() {
	_item = {};
}

void PreviewWrap::prepare(rpl::producer<GiftDetails> details) {
	std::move(details) | rpl::start_with_next([=](GiftDetails details) {
		const auto &descriptor = details.descriptor;
		const auto cost = v::match(descriptor, [&](GiftTypePremium data) {
			return FillAmountAndCurrency(data.cost, data.currency, true);
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
				.maxWidth = st::chatIntroWidth,
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
	}) | rpl::start_with_next([=](int width) {
		resizeTo(width);
	}, lifetime());

	_history->owner().itemResizeRequest(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
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
		api->request() | rpl::start_with_error_done([=](QString error) {
			consumer.put_next({});
		}, [=] {
			const auto &options = api->optionsForPeer();
			auto list = std::vector<GiftTypePremium>();
			list.reserve(options.size());
			auto minMonthsGift = GiftTypePremium();
			for (const auto &option : options) {
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
			if (map.last.list != list) {
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

[[nodiscard]] rpl::producer<std::vector<GiftTypeStars>> GiftsStars(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer) {
	struct Session {
		std::vector<GiftTypeStars> last;
	};
	static auto Map = base::flat_map<not_null<Main::Session*>, Session>();

	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		auto i = Map.find(session);
		if (i == end(Map)) {
			i = Map.emplace(session, Session()).first;
			session->lifetime().add([=] { Map.remove(session); });
		}
		if (!i->second.last.empty()) {
			consumer.put_next_copy(i->second.last);
		}

		using namespace Api;
		const auto api = lifetime.make_state<PremiumGiftCodeOptions>(peer);
		api->requestStarGifts(
		) | rpl::start_with_error_done([=](QString error) {
			consumer.put_next({});
		}, [=] {
			auto list = std::vector<GiftTypeStars>();
			const auto &gifts = api->starGifts();
			list.reserve(gifts.size());
			for (auto &gift : gifts) {
				list.push_back({ .info = gift });
			}
			ranges::sort(list, [](
					const GiftTypeStars &a,
					const GiftTypeStars &b) {
				if (!a.info.limitedCount && !b.info.limitedCount) {
					return a.info.stars <= b.info.stars;
				} else if (!a.info.limitedCount) {
					return true;
				} else if (!b.info.limitedCount) {
					return false;
				} else if (a.info.limitedLeft != b.info.limitedLeft) {
					return a.info.limitedLeft > b.info.limitedLeft;
				}
				return a.info.stars <= b.info.stars;
			});

			auto &map = Map[session];
			if (map.last != list) {
				map.last = list;
				consumer.put_next_copy(list);
			}
		}, lifetime);

		return lifetime;
	};
}

[[nodiscard]] Text::String TabTextForPrice(
		not_null<Main::Session*> session,
		int price) {
	const auto simple = [](const QString &text) {
		return Text::String(st::semiboldTextStyle, text);
	};
	if (price == kPriceTabAll) {
		return simple(tr::lng_gift_stars_tabs_all(tr::now));
	} else if (price == kPriceTabLimited) {
		return simple(tr::lng_gift_stars_tabs_limited(tr::now));
	} else if (price == kPriceTabInStock) {
		return simple(tr::lng_gift_stars_tabs_in_stock(tr::now));
	}
	auto &manager = session->data().customEmojiManager();
	auto result = Text::String();
	const auto context = Core::MarkedTextContext{
		.session = session,
		.customEmojiRepaint = [] {},
	};
	result.setMarkedText(
		st::semiboldTextStyle,
		manager.creditsEmoji().append(QString::number(price)),
		kMarkupTextOptions,
		context);
	return result;
}

struct GiftPriceTabs {
	rpl::producer<int> priceTab;
	object_ptr<RpWidget> widget;
};
[[nodiscard]] GiftPriceTabs MakeGiftsPriceTabs(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		rpl::producer<std::vector<GiftTypeStars>> gifts) {
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
		int selected = -1;
		int pressed = -1;
		int active = -1;
	};
	const auto state = raw->lifetime().make_state<State>();
	const auto scroll = [=] {
		return QPoint(int(base::SafeRound(state->scroll)), 0);
	};

	state->prices = std::move(
		gifts
	) | rpl::map([](const std::vector<GiftTypeStars> &gifts) {
		auto result = std::vector<int>();
		result.push_back(kPriceTabAll);
		auto special = 1;
		auto same = true;
		auto sameKey = 0;
		auto hasNonSoldOut = false;
		auto hasSoldOut = false;
		auto hasLimited = false;
		for (const auto &gift : gifts) {
			if (same) {
				const auto key = gift.info.stars
					* (gift.info.limitedCount ? -1 : 1);
				if (!sameKey) {
					sameKey = key;
				} else if (sameKey != key) {
					same = false;
				}
			}
			if (IsSoldOut(gift.info)) {
				hasSoldOut = true;
			} else {
				hasNonSoldOut = true;
			}
			if (gift.info.limitedCount) {
				hasLimited = true;
			}
			if (!ranges::contains(result, gift.info.stars)) {
				result.push_back(gift.info.stars);
			}
		}
		if (same) {
			return std::vector<int>();
		}
		if (hasSoldOut && hasNonSoldOut) {
			result.insert(begin(result) + (special++), kPriceTabInStock);
		}
		if (hasLimited) {
			result.insert(begin(result) + (special++), kPriceTabLimited);
		}
		ranges::sort(begin(result) + 1, end(result));
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

	const auto session = &peer->session();
	state->prices.value(
	) | rpl::start_with_next([=](const std::vector<int> &prices) {
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
				button.text = TabTextForPrice(session, price);
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
	) | rpl::start_with_next([=](int outer, int inner) {
		state->scrollMax = std::max(0, inner - outer);
	}, raw->lifetime());

	raw->setMouseTracking(true);
	raw->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		switch (type) {
		case QEvent::Leave: setSelected(-1); break;
		case QEvent::MouseMove: {
			const auto me = static_cast<QMouseEvent*>(e.get());
			const auto mousex = me->pos().x();
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
			state->pressx = me->pos().x();
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

	raw->paintRequest() | rpl::start_with_next([=] {
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
	AddLengthLimitLabel(field, limit, std::nullopt, st::giftBoxLimitTop);

	const auto toggle = CreateChild<EmojiButton>(
		container,
		st::defaultComposeFiles.emoji);
	toggle->show();
	field->geometryValue() | rpl::start_with_next([=](QRect r) {
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
	) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
		InsertEmojiAtCursor(field->textCursor(), data.emoji);
	}, field->lifetime());
	panel->selector()->customEmojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
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
		const GiftDetails &details,
		Fn<void(Payments::CheckoutResult)> done) {
	v::match(details.descriptor, [&](const GiftTypePremium &gift) {
		auto invoice = api->invoice(1, gift.months);
		invoice.purpose = Payments::InvoicePremiumGiftCodeUsers{
			.users = { peer->asUser() },
			.message = details.text,
		};
		Payments::CheckoutProcess::Start(std::move(invoice), done);
	}, [&](const GiftTypeStars &gift) {
		const auto processNonPanelPaymentFormFactory
			= Payments::ProcessNonPanelPaymentFormFactory(window, done);
		Payments::CheckoutProcess::Start(Payments::InvoiceStarGift{
			.giftId = gift.info.id,
			.randomId = details.randomId,
			.message = details.text,
			.recipient = peer,
			.limitedCount = gift.info.limitedCount,
			.anonymous = details.anonymous,
			.upgraded = details.upgraded,
		}, done, processNonPanelPaymentFormFactory);
	});
}

[[nodiscard]] std::shared_ptr<Data::UniqueGift> FindUniqueGift(
		not_null<Main::Session*> session,
		const MTPUpdates &updates) {
	auto result = std::shared_ptr<Data::UniqueGift>();
	const auto checkAction = [&](const MTPMessageAction &action) {
		action.match([&](const MTPDmessageActionStarGiftUnique &data) {
			if (const auto gift = Api::FromTL(session, data.vgift())) {
				result = gift->unique;
			}
		}, [](const auto &) {});
	};
	updates.match([&](const MTPDupdates &data) {
		for (const auto &update : data.vupdates().v) {
			update.match([&](const MTPDupdateNewMessage &data) {
				data.vmessage().match([&](const MTPDmessageService &data) {
					checkAction(data.vaction());
				}, [](const auto &) {});
			}, [](const auto &) {});
		}
	}, [](const auto &) {});
	return result;
}

void ShowGiftUpgradedToast(
		base::weak_ptr<Window::SessionController> weak,
		not_null<Main::Session*> session,
		const MTPUpdates &result) {
	const auto gift = FindUniqueGift(session, result);
	if (const auto strong = gift ? weak.get() : nullptr) {
		strong->showToast({
			.title = tr::lng_gift_upgraded_title(tr::now),
			.text = tr::lng_gift_upgraded_about(
				tr::now,
				lt_name,
				Text::Bold(Data::UniqueGiftName(*gift)),
				Ui::Text::WithEntities),
			.duration = kUpgradeDoneToastDuration,
		});
	}
}

void SendStarsFormRequest(
		not_null<Window::SessionController*> controller,
		Settings::SmallBalanceResult result,
		uint64 formId,
		MTPInputInvoice invoice,
		Fn<void(Payments::CheckoutResult, const MTPUpdates *)> done) {
	using BalanceResult = Settings::SmallBalanceResult;
	const auto session = &controller->session();
	if (result == BalanceResult::Success
		|| result == BalanceResult::Already) {
		const auto weak = base::make_weak(controller);
		session->api().request(MTPpayments_SendStarsForm(
			MTP_long(formId),
			invoice
		)).done([=](const MTPpayments_PaymentResult &result) {
			result.match([&](const MTPDpayments_paymentResult &data) {
				session->api().applyUpdates(data.vupdates());
				done(Payments::CheckoutResult::Paid, &data.vupdates());
			}, [&](const MTPDpayments_paymentVerificationNeeded &data) {
				done(Payments::CheckoutResult::Failed, nullptr);
			});
		}).fail([=](const MTP::Error &error) {
			if (const auto strong = weak.get()) {
				strong->showToast(error.type());
			}
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
		Fn<void(Payments::CheckoutResult)> done) {
	const auto session = &window->session();
	const auto weak = base::make_weak(window);
	auto formDone = [=](
			Payments::CheckoutResult result,
			const MTPUpdates *updates) {
		if (result == Payments::CheckoutResult::Paid && updates) {
			if (const auto strong = weak.get()) {
				ShowGiftUpgradedToast(strong, session, *updates);
			}
		}
		done(result);
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
		window,
		MTP_inputInvoiceStarGiftUpgrade(
			MTP_flags(keepDetails ? Flag::f_keep_original_details : Flag()),
			Api::InputSavedStarGiftId(savedId)),
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
			.credits = StarsAmount(gift.info.stars),
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
		not_null<Main::Session*> session,
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
	) | rpl::start_with_next(toggled, button->lifetime());

	const auto makeContext = [session](Fn<void()> update) {
		return Core::MarkedTextContext{
			.session = session,
			.customEmojiRepaint = std::move(update),
		};
	};
	auto star = session->data().customEmojiManager().creditsEmoji();
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button,
		tr::lng_gift_send_unique(
			lt_price,
			rpl::single(star.append(' '
				+ Lang::FormatStarsAmountDecimal(
					StarsAmount{ cost }))),
			Text::WithEntities),
		st::boxLabel,
		st::defaultPopupMenu,
		std::move(makeContext));
	label->show();
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->widthValue() | rpl::start_with_next([=](int outer) {
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
				tr::lng_gift_send_unique_link() | Text::ToLink(),
				Text::WithEntities)
			: tr::lng_gift_send_unique_about(
				lt_user,
				rpl::single(TextWithEntities{ peer->shortName() }),
				lt_link,
				tr::lng_gift_send_unique_link() | Text::ToLink(),
				Text::WithEntities)));
	about->setClickHandlerFilter([=](const auto &...) {
		preview();
		return false;
	});
}

void AddSoldLeftSlider(
		not_null<RoundButton*> button,
		const GiftTypeStars &gift) {
	const auto still = gift.info.limitedLeft;
	const auto total = gift.info.limitedCount;
	const auto slider = CreateChild<RpWidget>(button->parentWidget());
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
	button->geometryValue() | rpl::start_with_next([=](QRect geometry) {
		const auto space = st::giftLimitedBox.buttonPadding.top();
		const auto skip = (space - state->height) / 2;
		slider->setGeometry(
			geometry.x(),
			geometry.y() - skip - state->height,
			geometry.width(),
			state->height);
	}, slider->lifetime());
	slider->paintRequest() | rpl::start_with_next([=] {
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

void SendGiftBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		std::shared_ptr<Api::PremiumGiftCodeOptions> api,
		const GiftDescriptor &descriptor) {
	const auto stars = std::get_if<GiftTypeStars>(&descriptor);
	const auto limited = stars
		&& (stars->info.limitedCount > stars->info.limitedLeft)
		&& (stars->info.limitedLeft > 0);
	box->setStyle(limited ? st::giftLimitedBox : st::giftBox);
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_gift_send_title());
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	const auto session = &window->session();

	struct State {
		rpl::variable<GiftDetails> details;
		std::shared_ptr<Data::DocumentMedia> media;
		bool submitting = false;
	};
	const auto state = box->lifetime().make_state<State>();
	state->details = GiftDetails{
		.descriptor = descriptor,
		.randomId = base::RandomValue<uint64>(),
	};

	auto cost = state->details.value(
	) | rpl::map([session](const GiftDetails &details) {
		return v::match(details.descriptor, [&](const GiftTypePremium &data) {
			if (data.currency == kCreditsCurrency) {
				return CreditsEmojiSmall(session).append(
					Lang::FormatCountDecimal(std::abs(data.cost)));
			}
			return TextWithEntities{
				FillAmountAndCurrency(data.cost, data.currency),
			};
		}, [&](const GiftTypeStars &data) {
			const auto amount = std::abs(data.info.stars)
				+ (details.upgraded ? data.info.starsToUpgrade : 0);
			return CreditsEmojiSmall(session).append(
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

	const auto limit = StarGiftMessageLimit(session);
	const auto text = AddPartInput(
		window,
		container,
		box->getDelegate()->outerContainer(),
		tr::lng_gift_send_message(),
		QString(),
		limit);
	text->changes() | rpl::start_with_next([=] {
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
		const auto cost = stars->info.starsToUpgrade;
		if (cost > 0 && !peer->isSelf()) {
			const auto id = stars->info.id;
			const auto showing = std::make_shared<bool>();
			AddDivider(container);
			AddSkip(container);
			AddUpgradeButton(container, session, cost, peer, [=](bool on) {
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
					.stargiftId = id,
					.ready = [=](bool) { *showing = false; },
					.peer = peer,
					.cost = int(cost),
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
		) | rpl::start_with_next([=](bool toggled) {
			auto now = state->details.current();
			now.anonymous = toggled;
			state->details = std::move(now);
		}, container->lifetime());
		AddSkip(container);
	}
	v::match(descriptor, [&](const GiftTypePremium &) {
		AddDividerText(container, tr::lng_gift_send_premium_about(
			lt_user,
			rpl::single(peer->shortName())));
	}, [&](const GiftTypeStars &) {
		AddDividerText(container, peer->isSelf()
			? tr::lng_gift_send_anonymous_self()
			: peer->isBroadcast()
			? tr::lng_gift_send_anonymous_about_channel()
			: tr::lng_gift_send_anonymous_about(
				lt_user,
				rpl::single(peer->shortName()),
				lt_recipient,
				rpl::single(peer->shortName())));
	});

	const auto buttonWidth = st::boxWideWidth
		- st::giftBox.buttonPadding.left()
		- st::giftBox.buttonPadding.right();
	const auto button = box->addButton(rpl::single(QString()), [=] {
		if (state->submitting) {
			return;
		}
		state->submitting = true;
		const auto details = state->details.current();
		const auto weak = MakeWeak(box);
		const auto done = [=](Payments::CheckoutResult result) {
			if (result == Payments::CheckoutResult::Paid) {
				const auto copy = state->media;
				window->showPeerHistory(peer);
				ShowSentToast(window, descriptor, details);
			}
			if (const auto strong = weak.data()) {
				box->closeBox();
			}
		};
		SendGift(window, peer, api, details, done);
	});
	if (limited) {
		AddSoldLeftSlider(button, *stars);
	}
	SetButtonMarkedLabel(
		button,
		(peer->isSelf()
			? tr::lng_gift_send_button_self
			: tr::lng_gift_send_button)(
				lt_cost,
				std::move(cost),
				Text::WithEntities),
		session,
		st::creditsBoxButtonLabel,
		&st::giftBox.button.textFg);
	button->resizeToWidth(buttonWidth);
	button->widthValue() | rpl::start_with_next([=](int width) {
		if (width != buttonWidth) {
			button->resizeToWidth(buttonWidth);
		}
	}, button->lifetime());
}

[[nodiscard]] object_ptr<RpWidget> MakeGiftsList(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		rpl::producer<GiftsDescriptor> gifts) {
	auto result = object_ptr<RpWidget>((QWidget*)nullptr);
	const auto raw = result.data();

	struct State {
		Delegate delegate;
		std::vector<std::unique_ptr<GiftButton>> buttons;
		bool sending = false;
	};
	const auto state = raw->lifetime().make_state<State>(State{
		.delegate = Delegate(window, GiftButtonMode::Full),
	});
	const auto single = state->delegate.buttonSize();
	const auto shadow = st::defaultDropdownMenu.wrap.shadow;
	const auto extend = shadow.extend;

	auto &packs = window->session().giftBoxStickersPacks();
	packs.updated() | rpl::start_with_next([=] {
		for (const auto &button : state->buttons) {
			button->update();
		}
	}, raw->lifetime());

	std::move(
		gifts
	) | rpl::start_with_next([=](const GiftsDescriptor &gifts) {
		const auto width = st::boxWideWidth;
		const auto padding = st::giftBoxPadding;
		const auto available = width - padding.left() - padding.right();
		const auto perRow = available / single.width();
		const auto count = int(gifts.list.size());

		auto order = ranges::views::ints
			| ranges::views::take(count)
			| ranges::to_vector;

		if (SortForBirthday(peer)) {
			ranges::stable_partition(order, [&](int i) {
				const auto &gift = gifts.list[i];
				const auto stars = std::get_if<GiftTypeStars>(&gift);
				return stars && stars->info.birthday;
			});
		}

		auto x = padding.left();
		auto y = padding.top();
		state->buttons.resize(count);
		for (auto &button : state->buttons) {
			if (!button) {
				button = std::make_unique<GiftButton>(raw, &state->delegate);
				button->show();
			}
		}
		const auto api = gifts.api;
		for (auto i = 0; i != count; ++i) {
			const auto button = state->buttons[i].get();
			const auto &descriptor = gifts.list[order[i]];
			button->setDescriptor(descriptor, GiftButton::Mode::Full);

			const auto last = !((i + 1) % perRow);
			if (last) {
				x = padding.left() + available - single.width();
			}
			button->setGeometry(QRect(QPoint(x, y), single), extend);
			if (last) {
				x = padding.left();
				y += single.height() + st::giftBoxGiftSkip.y();
			} else {
				x += single.width() + st::giftBoxGiftSkip.x();
			}

			button->setClickedCallback([=] {
				const auto star = std::get_if<GiftTypeStars>(&descriptor);
				if (star && IsSoldOut(star->info)) {
					window->show(Box(SoldOutBox, window, *star));
				} else {
					window->show(
						Box(SendGiftBox, window, peer, api, descriptor));
				}
			});
		}
		if (count % perRow) {
			y += padding.bottom() + single.height();
		} else {
			y += padding.bottom() - st::giftBoxGiftSkip.y();
		}
		raw->resize(raw->width(), count ? y : 0);
	}, raw->lifetime());

	return result;
}

void FillBg(not_null<RpWidget*> box) {
	box->paintRequest() | rpl::start_with_next([=] {
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
		st::giftBoxSubtitleMargin);
	const auto about = content->add(
		object_ptr<FlatLabel>(
			content,
			std::move(args.about),
			st::giftBoxAbout),
		st::giftBoxAboutMargin);
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
	}));
	result->lifetime().add([state = std::move(state)] {});
	return result;
}

[[nodiscard]] object_ptr<RpWidget> MakeStarsGifts(
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer) {
	auto result = object_ptr<VerticalLayout>((QWidget*)nullptr);

	struct State {
		rpl::variable<std::vector<GiftTypeStars>> gifts;
		rpl::variable<int> priceTab = kPriceTabAll;
	};
	const auto state = result->lifetime().make_state<State>();

	state->gifts = GiftsStars(&window->session(), peer);

	auto tabs = MakeGiftsPriceTabs(window, peer, state->gifts.value());
	state->priceTab = std::move(tabs.priceTab);
	result->add(std::move(tabs.widget));
	result->add(MakeGiftsList(window, peer, rpl::combine(
		state->gifts.value(),
		state->priceTab.value()
	) | rpl::map([=](std::vector<GiftTypeStars> &&gifts, int price) {
		gifts.erase(ranges::remove_if(gifts, [&](const GiftTypeStars &gift) {
			return (price == kPriceTabLimited)
				? (!gift.info.limitedCount)
				: (price == kPriceTabInStock)
				? IsSoldOut(gift.info)
				: (price && gift.info.stars != price);
		}), end(gifts));
		return GiftsDescriptor{
			gifts | ranges::to<std::vector<GiftDescriptor>>(),
		};
	})));

	return result;
}

void GiftBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::creditsGiftBox);
	box->setNoContentMargin(true);
	box->setCustomCornersFilling(RectPart::FullTop);
	box->addButton(tr::lng_create_group_back(), [=] { box->closeBox(); });

	FillBg(box);

	const auto &stUser = st::premiumGiftsUserpicButton;
	const auto content = box->verticalLayout();

	AddSkip(content, st::defaultVerticalListSkip * 5);

	content->add(
		object_ptr<CenterWrap<>>(
			content,
			object_ptr<UserpicButton>(content, peer, stUser))
	)->setAttribute(Qt::WA_TransparentForMouseEvents);
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
	if (peer->isUser() && !peer->isSelf()) {
		const auto premiumClickHandlerFilter = [=](const auto &...) {
			Settings::ShowPremium(window, u"gift_send"_q);
			return false;
		};
		AddBlock(content, window, {
			.subtitle = tr::lng_gift_premium_subtitle(),
			.about = tr::lng_gift_premium_about(
				lt_name,
				rpl::single(Text::Bold(peer->shortName())),
				lt_features,
				tr::lng_gift_premium_features() | Text::ToLink(),
				Text::WithEntities),
			.aboutFilter = premiumClickHandlerFilter,
			.content = MakePremiumGifts(window, peer),
		});
	}
	AddBlock(content, window, {
		.subtitle = (peer->isSelf()
			? tr::lng_gift_self_title()
			: peer->isBroadcast()
			? tr::lng_gift_channel_title()
			: tr::lng_gift_stars_subtitle()),
		.about = (peer->isSelf()
			? tr::lng_gift_self_about(Text::WithEntities)
			: peer->isBroadcast()
			? tr::lng_gift_channel_about(
				lt_name,
				rpl::single(Text::Bold(peer->name())),
				Text::WithEntities)
			: tr::lng_gift_stars_about(
				lt_name,
				rpl::single(Text::Bold(peer->shortName())),
				lt_link,
				tr::lng_gift_stars_link() | Text::ToLink(),
				Text::WithEntities)),
		.aboutFilter = starsClickHandlerFilter,
		.content = MakeStarsGifts(window, peer),
	});
}

struct SelfOption {
	object_ptr<Ui::RpWidget> content = { nullptr };
	Fn<bool(int, int, int)> overrideKey;
	Fn<void()> activate;
};

class Controller final : public ContactsBoxController {
public:
	Controller(
		not_null<Main::Session*> session,
		Fn<void(not_null<PeerData*>)> choose);

	void noSearchSubmit();

	bool overrideKeyboardNavigation(
		int direction,
		int fromIndex,
		int toIndex) override;

private:
	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

	void prepareViewHook() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	const Fn<void(not_null<PeerData*>)> _choose;
	SelfOption _selfOption;

};

[[nodiscard]] SelfOption MakeSelfOption(
		not_null<Main::Session*> session,
		Fn<void()> activate) {
	class SelfController final : public PeerListController {
	public:
		SelfController(
			not_null<Main::Session*> session,
			Fn<void()> activate)
		: _session(session)
		, _activate(std::move(activate)) {
		}

		void prepare() override {
			auto row = std::make_unique<PeerListRow>(_session->user());
			row->setCustomStatus(tr::lng_gift_self_status(tr::now));
			delegate()->peerListAppendRow(std::move(row));
			delegate()->peerListRefreshRows();
		}
		void loadMoreRows() override {
		}
		void rowClicked(not_null<PeerListRow*> row) override {
			_activate();
		}
		Main::Session &session() const override {
			return *_session;
		}

	private:
		const not_null<Main::Session*> _session;
		Fn<void()> _activate;

	};

	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();

	Ui::AddSkip(container);

	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime().make_state<
		SelfController
	>(session, activate);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	Ui::AddSkip(container);
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_contacts_header()));

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

	return {
		.content = std::move(result),
		.overrideKey = overrideKey,
		.activate = activate,
	};
}

Controller::Controller(
	not_null<Main::Session*> session,
	Fn<void(not_null<PeerData*>)> choose)
: ContactsBoxController(session)
, _choose(std::move(choose))
, _selfOption(MakeSelfOption(session, [=] { _choose(session->user()); })) {
	setStyleOverrides(&st::peerListSmallSkips);
}

void Controller::noSearchSubmit() {
	if (const auto onstack = _selfOption.activate) {
		onstack();
	}
}

bool Controller::overrideKeyboardNavigation(
		int direction,
		int fromIndex,
		int toIndex) {
	return _selfOption.overrideKey
		&& _selfOption.overrideKey(direction, fromIndex, toIndex);
}

std::unique_ptr<PeerListRow> Controller::createRow(
		not_null<UserData*> user) {
	if (user->isSelf()
		|| user->isBot()
		|| user->isServiceUser()
		|| user->isInaccessible()) {
		return nullptr;
	}
	return ContactsBoxController::createRow(user);
}

void Controller::prepareViewHook() {
	delegate()->peerListSetAboveWidget(std::move(_selfOption.content));
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	_choose(row->peer());
}

} // namespace

void ChooseStarGiftRecipient(
		not_null<Window::SessionController*> window) {
	auto controller = std::make_unique<Controller>(
		&window->session(),
		[=](not_null<PeerData*> peer) {
			ShowStarGiftBox(window, peer);
		});
	const auto controllerRaw = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->setTitle(tr::lng_gift_premium_or_stars());
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

		box->noSearchSubmits() | rpl::start_with_next([=] {
			controllerRaw->noSearchSubmit();
		}, box->lifetime());
	};
	window->show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		LayerOption::KeepOther);
}

void ShowStarGiftBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(GiftBox, controller, peer));
}

void AddUniqueGiftCover(
		not_null<VerticalLayout*> container,
		rpl::producer<Data::UniqueGift> data,
		rpl::producer<QString> subtitleOverride) {
	const auto cover = container->add(object_ptr<RpWidget>(container));

	const auto title = CreateChild<FlatLabel>(
		cover,
		rpl::duplicate(
			data
		) | rpl::map([](const Data::UniqueGift &now) { return now.title; }),
		st::uniqueGiftTitle);
	title->setTextColorOverride(QColor(255, 255, 255));
	auto subtitleText = subtitleOverride
		? std::move(subtitleOverride)
		: rpl::duplicate(data) | rpl::map([](const Data::UniqueGift &gift) {
			return tr::lng_gift_unique_number(
				tr::now,
				lt_index,
				QString::number(gift.number));
		});
	const auto subtitle = CreateChild<FlatLabel>(
		cover,
		std::move(subtitleText),
		st::uniqueGiftSubtitle);

	struct GiftView {
		QImage gradient;
		std::optional<Data::UniqueGift> gift;
		std::shared_ptr<Data::DocumentMedia> media;
		std::unique_ptr<Lottie::SinglePlayer> lottie;
		std::unique_ptr<Text::CustomEmoji> emoji;
		base::flat_map<float64, QImage> emojis;
		rpl::lifetime lifetime;
	};
	struct State {
		GiftView now;
		GiftView next;
		Animations::Simple crossfade;
		bool animating = false;
	};
	const auto state = cover->lifetime().make_state<State>();
	const auto lottieSize = st::creditsHistoryEntryStarGiftSize;
	const auto updateColors = [=](float64 progress) {
		subtitle->setTextColorOverride((progress == 0.)
			? state->now.gift->backdrop.textColor
			: (progress == 1.)
			? state->next.gift->backdrop.textColor
			: anim::color(
				state->now.gift->backdrop.textColor,
				state->next.gift->backdrop.textColor,
				progress));
	};
	std::move(
		data
	) | rpl::start_with_next([=](const Data::UniqueGift &gift) {
		const auto setup = [&](GiftView &to) {
			to.gift = gift;
			const auto document = gift.model.document;
			to.media = document->createMediaView();
			to.media->automaticLoad({}, nullptr);
			rpl::single() | rpl::then(
				document->session().downloaderTaskFinished()
			) | rpl::filter([&to] {
				return to.media->loaded();
			}) | rpl::start_with_next([=, &to] {
				const auto lottieSize = st::creditsHistoryEntryStarGiftSize;
				to.lottie = ChatHelpers::LottiePlayerFromDocument(
					to.media.get(),
					ChatHelpers::StickerLottieSize::MessageHistory,
					QSize(lottieSize, lottieSize),
					Lottie::Quality::High);

				to.lifetime.destroy();
				const auto lottie = to.lottie.get();
				lottie->updates() | rpl::start_with_next([=] {
					if (state->now.lottie.get() == lottie
						|| state->crossfade.animating()) {
						cover->update();
					}
				}, to.lifetime);
			}, to.lifetime);
			to.emoji = document->owner().customEmojiManager().create(
				gift.pattern.document,
				[=] { cover->update(); },
				Data::CustomEmojiSizeTag::Large);
			[[maybe_unused]] const auto preload = to.emoji->ready();
		};

		if (!state->now.gift) {
			setup(state->now);
			cover->update();
			updateColors(0.);
		} else if (!state->next.gift) {
			setup(state->next);
		}
	}, cover->lifetime());

	cover->widthValue() | rpl::start_with_next([=](int width) {
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

	cover->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(cover);

		auto progress = state->crossfade.value(state->animating ? 1. : 0.);
		if (state->animating) {
			updateColors(progress);
		}
		if (progress == 1.) {
			state->animating = false;
			state->now = base::take(state->next);
			progress = 0.;
		}
		const auto paint = [&](GiftView &gift, float64 shown) {
			Expects(gift.gift.has_value());

			const auto width = cover->width();
			const auto pointsHeight = st::uniqueGiftSubtitleTop;
			const auto ratio = style::DevicePixelRatio();
			if (gift.gradient.size() != cover->size() * ratio) {
				gift.gradient = CreateGradient(cover->size(), *gift.gift);
			}
			p.drawImage(0, 0, gift.gradient);

			PaintPoints(
				p,
				PatternPoints(),
				gift.emojis,
				gift.emoji.get(),
				*gift.gift,
				QRect(0, 0, width, pointsHeight),
				shown);

			const auto lottie = gift.lottie.get();
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
			const auto left = (width - size.width()) / 2;
			p.drawImage(
				QRect(QPoint(left, st::uniqueGiftModelTop), size),
				frame.image);
			const auto count = lottie->framesCount();
			const auto finished = lottie->frameIndex() == (count - 1);
			lottie->markFrameShown();
			return finished;
		};

		if (progress < 1.) {
			const auto finished = paint(state->now, 1. - progress);
			const auto next = finished ? state->next.lottie.get() : nullptr;
			if (next && next->ready()) {
				state->animating = true;
				state->crossfade.start([=] {
					cover->update();
				}, 0., 1., kCrossfadeDuration);
			}
		}
		if (progress > 0.) {
			p.setOpacity(progress);
			paint(state->next, progress);
		}
	}, cover->lifetime());
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

	cover->widthValue() | rpl::start_with_next([=](int width) {
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

	cover->paintRequest() | rpl::start_with_next([=] {
		auto p = Painter(cover);

		const auto width = cover->width();
		const auto pointsHeight = st::uniqueGiftSubtitleTop;
		const auto ratio = style::DevicePixelRatio();
		if (state->gradient.size() != cover->size() * ratio) {
			state->gradient = CreateGradient(cover->size(), state->gift);
		}
		p.drawImage(0, 0, state->gradient);

		PaintPoints(
			p,
			PatternPoints(),
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
					std::move(title) | Ui::Text::ToBold(),
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
			st::settingsPremiumRowTitlePadding);
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_gift_wear_about(),
				st.subtitle ? *st.subtitle : st::uniqueGiftSubtitle),
			st::settingsPremiumRowAboutPadding);
		infoRow(
			tr::lng_gift_wear_badge_title(),
			(channel
				? tr::lng_gift_wear_badge_about_channel()
				: tr::lng_gift_wear_badge_about()),
			st.radiantIcon ? st.radiantIcon : &st::menuIconUnique);
		//infoRow(
		//	tr::lng_gift_wear_design_title(),
		//	tr::lng_gift_wear_design_about(),
		//	&st::menuIconUniqueProfile);
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
				const auto weak = Ui::MakeWeak(box);
				CheckBoostLevel(show, peer, [=](int level) {
					const auto limits = Data::LevelLimits(&peer->session());
					const auto wanted = limits.channelEmojiStatusLevelMin();
					if (level >= wanted) {
						if (const auto strong = weak.data()) {
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
				const auto link = Ui::Text::Bold(
					tr::lng_send_as_premium_required_link(tr::now));
				Settings::ShowPremiumPromoToast(
					show,
					tr::lng_gift_wear_subscribe(
						tr::now,
						lt_link,
						Ui::Text::Link(link),
						Ui::Text::WithEntities),
					u"wear_collectibles"_q);
			}
		});
		const auto lock = Ui::Text::SingleCustomEmoji(
			session->data().customEmojiManager().registerInternalEmoji(
				st::historySendDisabledIcon,
				st::giftBoxLockMargins,
				true));
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

		rpl::combine(
			box->widthValue(),
			button->widthValue()
		) | rpl::start_with_next([=](int outer, int inner) {
			const auto padding = st::giftBox.buttonPadding;
			const auto wanted = outer - padding.left() - padding.right();
			if (inner != wanted) {
				button->resizeToWidth(wanted);
				button->moveToLeft(padding.left(), padding.top());
			}
		}, box->lifetime());

		AddUniqueCloseButton(box, {});
	}));
}

struct UpgradeArgs : StarGiftUpgradeArgs {
	std::vector<Data::UniqueGiftModel> models;
	std::vector<Data::UniqueGiftPattern> patterns;
	std::vector<Data::UniqueGiftBackdrop> backdrops;
};

[[nodiscard]] rpl::producer<Data::UniqueGift> MakeUpgradeGiftStream(
		const UpgradeArgs &args) {
	if (args.models.empty()
		|| args.patterns.empty()
		|| args.backdrops.empty()) {
		return rpl::never<Data::UniqueGift>();
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
			consumer.put_next(Data::UniqueGift{
				.title = (state->data.savedId
					? tr::lng_gift_upgrade_title(tr::now)
					: tr::lng_gift_upgrade_preview_title(tr::now)),
				.model = models[index(state->modelIndices, models)],
				.pattern = patterns[index(state->patternIndices, patterns)],
				.backdrop = backdrops[index(state->backdropIndices, backdrops)],
			});
		};

		put();
		base::timer_each(
			kSwitchUpgradeCoverInterval / 3
		) | rpl::start_with_next(put, lifetime);

		return lifetime;
	};
}

void AddUpgradeGiftCover(
		not_null<VerticalLayout*> container,
		const UpgradeArgs &args) {
	AddUniqueGiftCover(
		container,
		MakeUpgradeGiftStream(args),
		(args.savedId
			? tr::lng_gift_upgrade_about()
			: (args.peer->isBroadcast()
				? tr::lng_gift_upgrade_preview_about_channel
				: tr::lng_gift_upgrade_preview_about)(
					lt_name,
					rpl::single(args.peer->shortName()))));
}

void UpgradeBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> controller,
		UpgradeArgs &&args) {
	box->setNoContentMargin(true);

	const auto container = box->verticalLayout();
	AddUpgradeGiftCover(container, args);

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
				std::move(title) | Ui::Text::ToBold(),
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
		tr::lng_gift_upgrade_unique_about(),
		&st::menuIconUnique);
	infoRow(
		tr::lng_gift_upgrade_transferable_title(),
		tr::lng_gift_upgrade_transferable_about(),
		&st::menuIconReplace);
	infoRow(
		tr::lng_gift_upgrade_tradable_title(),
		tr::lng_gift_upgrade_tradable_about(),
		&st::menuIconTradable);

	struct State {
		bool sent = false;
		bool preserveDetails = false;
	};
	const auto state = std::make_shared<State>();
	const auto preview = !args.savedId;

	if (!preview) {
		const auto skip = st::defaultVerticalListSkip;
		container->add(
			object_ptr<PlainShadow>(container),
			st::boxRowPadding + QMargins(0, skip, 0, skip));
		const auto checkbox = container->add(
			object_ptr<CenterWrap<Checkbox>>(
				container,
				object_ptr<Checkbox>(
					container,
					(args.canAddComment
						? tr::lng_gift_upgrade_add_comment(tr::now)
						: args.canAddSender
						? tr::lng_gift_upgrade_add_sender(tr::now)
						: args.canAddMyComment
						? tr::lng_gift_upgrade_add_my_comment(tr::now)
						: tr::lng_gift_upgrade_add_my(tr::now)),
					args.addDetailsDefault)),
			st::defaultCheckbox.margin)->entity();
		checkbox->checkedChanges() | rpl::start_with_next([=](bool checked) {
			state->preserveDetails = checked;
		}, checkbox->lifetime());
	}

	box->setStyle(preview ? st::giftBox : st::upgradeGiftBox);

	const auto cost = args.cost;
	const auto session = &controller->session();
	auto buttonText = preview ? tr::lng_box_ok() : rpl::single(QString());
	const auto button = box->addButton(std::move(buttonText), [=] {
		if (preview) {
			box->closeBox();
			return;
		} else if (state->sent) {
			return;
		}
		state->sent = true;
		const auto keepDetails = state->preserveDetails;
		const auto weak = Ui::MakeWeak(box);
		const auto done = [=](Payments::CheckoutResult result) {
			if (result != Payments::CheckoutResult::Paid) {
				state->sent = false;
			} else {
				controller->showPeerHistory(args.peer);
				if (const auto strong = weak.data()) {
					strong->closeBox();
				}
			}
		};
		UpgradeGift(controller, args.savedId, keepDetails, cost, done);
	});
	if (!preview) {
		auto star = session->data().customEmojiManager().creditsEmoji();
		SetButtonMarkedLabel(
			button,
			(cost
				? tr::lng_gift_upgrade_button(
					lt_price,
					rpl::single(star.append(
						' ' + Lang::FormatStarsAmountDecimal(
							StarsAmount{ cost }))),
					Ui::Text::WithEntities)
				: tr::lng_gift_upgrade_confirm(Ui::Text::WithEntities)),
			&controller->session(),
			st::creditsBoxButtonLabel,
			&st::giftBox.button.textFg);
	}
	rpl::combine(
		box->widthValue(),
		button->widthValue()
	) | rpl::start_with_next([=](int outer, int inner) {
		const auto padding = st::giftBox.buttonPadding;
		const auto wanted = outer - padding.left() - padding.right();
		if (inner != wanted) {
			button->resizeToWidth(wanted);
			button->moveToLeft(padding.left(), padding.top());
		}
	}, box->lifetime());

	AddUniqueCloseButton(box, {});
}

const std::vector<PatternPoint> &PatternPoints() {
	static const auto kSmall = 0.7;
	static const auto kFaded = 0.2;
	static const auto kLarge = 0.85;
	static const auto kOpaque = 0.3;
	static const auto result = std::vector<PatternPoint>{
		{ { 0.5, 0.066 }, kSmall, kFaded },

		{ { 0.177, 0.168 }, kSmall, kFaded },
		{ { 0.822, 0.168 }, kSmall, kFaded },

		{ { 0.37, 0.168 }, kLarge, kOpaque },
		{ { 0.63, 0.168 }, kLarge, kOpaque },

		{ { 0.277, 0.308 }, kSmall, kOpaque },
		{ { 0.723, 0.308 }, kSmall, kOpaque },

		{ { 0.13, 0.42 }, kSmall, kFaded },
		{ { 0.87, 0.42 }, kSmall, kFaded },

		{ { 0.27, 0.533 }, kLarge, kOpaque },
		{ { 0.73, 0.533 }, kLarge, kOpaque },

		{ { 0.2, 0.73 }, kSmall, kFaded },
		{ { 0.8, 0.73 }, kSmall, kFaded },

		{ { 0.302, 0.825 }, kLarge, kOpaque },
		{ { 0.698, 0.825 }, kLarge, kOpaque },

		{ { 0.5, 0.876 }, kLarge, kFaded },

		{ { 0.144, 0.936 }, kSmall, kFaded },
		{ { 0.856, 0.936 }, kSmall, kFaded },
	};
	return result;
}

const std::vector<PatternPoint> &PatternPointsSmall() {
	static const auto kSmall = 0.45;
	static const auto kFaded = 0.2;
	static const auto kLarge = 0.55;
	static const auto kOpaque = 0.3;
	static const auto result = std::vector<PatternPoint>{
		{ { 0.5, 0.066 }, kSmall, kFaded },

		{ { 0.177, 0.168 }, kSmall, kFaded },
		{ { 0.822, 0.168 }, kSmall, kFaded },

		{ { 0.37, 0.168 }, kLarge, kOpaque },
		{ { 0.63, 0.168 }, kLarge, kOpaque },

		{ { 0.277, 0.308 }, kSmall, kOpaque },
		{ { 0.723, 0.308 }, kSmall, kOpaque },

		{ { 0.13, 0.42 }, kSmall, kFaded },
		{ { 0.87, 0.42 }, kSmall, kFaded },

		{ { 0.27, 0.533 }, kLarge, kOpaque },
		{ { 0.73, 0.533 }, kLarge, kOpaque },

		{ { 0.2, 0.73 }, kSmall, kFaded },
		{ { 0.8, 0.73 }, kSmall, kFaded },

		{ { 0.302, 0.825 }, kLarge, kOpaque },
		{ { 0.698, 0.825 }, kLarge, kOpaque },

		{ { 0.5, 0.876 }, kLarge, kFaded },

		{ { 0.144, 0.936 }, kSmall, kFaded },
		{ { 0.856, 0.936 }, kSmall, kFaded },
	};
	return result;
}

void PaintPoints(
		QPainter &p,
		const std::vector<PatternPoint> &points,
		base::flat_map<float64, QImage> &cache,
		not_null<Text::CustomEmoji*> emoji,
		const Data::UniqueGift &gift,
		const QRect &rect,
		float64 shown) {
	const auto origin = rect.topLeft();
	const auto width = rect.width();
	const auto height = rect.height();
	const auto ratio = style::DevicePixelRatio();
	const auto paintPoint = [&](const PatternPoint &point) {
		const auto key = (1. + point.opacity) * 10. + point.scale;
		auto &image = cache[key];
		PrepareImage(image, emoji, point, gift);
		if (!image.isNull()) {
			const auto position = origin + QPoint(
				int(point.position.x() * width),
				int(point.position.y() * height));
			if (shown < 1.) {
				p.save();
				p.translate(position);
				p.scale(shown, shown);
				p.translate(-position);
			}
			const auto size = image.size() / ratio;
			p.drawImage(
				position - QPoint(size.width() / 2, size.height() / 2),
				image);
			if (shown < 1.) {
				p.restore();
			}
		}
	};
	for (const auto &point : points) {
		paintPoint(point);
	}
}

void ShowStarGiftUpgradeBox(StarGiftUpgradeArgs &&args) {
	const auto weak = base::make_weak(args.controller);
	const auto session = &args.peer->session();
	session->api().request(MTPpayments_GetStarGiftUpgradePreview(
		MTP_long(args.stargiftId)
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
	box->widthValue() | rpl::start_with_next([=](int width) {
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

void RequestStarsFormAndSubmit(
		not_null<Window::SessionController*> window,
		MTPInputInvoice invoice,
		Fn<void(Payments::CheckoutResult, const MTPUpdates *)> done) {
	const auto weak = base::make_weak(window);
	window->session().api().request(MTPpayments_GetPaymentForm(
		MTP_flags(0),
		invoice,
		MTPDataJSON() // theme_params
	)).done([=](const MTPpayments_PaymentForm &result) {
		result.match([&](const MTPDpayments_paymentFormStarGift &data) {
			const auto formId = data.vform_id().v;
			const auto prices = data.vinvoice().data().vprices().v;
			const auto strong = weak.get();
			if (!strong) {
				done(Payments::CheckoutResult::Failed, nullptr);
				return;
			}
			const auto ready = [=](Settings::SmallBalanceResult result) {
				SendStarsFormRequest(strong, result, formId, invoice, done);
			};
			Settings::MaybeRequestBalanceIncrease(
				strong->uiShow(),
				prices.front().data().vamount().v,
				Settings::SmallBalanceDeepLink{},
				ready);
		}, [&](const auto &) {
			done(Payments::CheckoutResult::Failed, nullptr);
		});
	}).fail([=](const MTP::Error &error) {
		const auto type = error.type();
		if (type == u"STARGIFT_EXPORT_IN_PROGRESS"_q) {
			done(Payments::CheckoutResult::Cancelled, nullptr);
		} else {
			if (const auto strong = weak.get()) {
				strong->showToast(type);
			}
			done(Payments::CheckoutResult::Failed, nullptr);
		}
	}).send();
}

void ShowGiftTransferredToast(
		base::weak_ptr<Window::SessionController> weak,
		not_null<PeerData*> to,
		const Data::UniqueGift &gift) {
	if (const auto strong = weak.get()) {
		strong->showToast({
			.title = tr::lng_gift_transferred_title(tr::now),
			.text = tr::lng_gift_transferred_about(
				tr::now,
				lt_name,
				Text::Bold(Data::UniqueGiftName(gift)),
				lt_recipient,
				Text::Bold(to->shortName()),
				Ui::Text::WithEntities),
			.duration = kUpgradeDoneToastDuration,
		});
	}
}

} // namespace Ui
