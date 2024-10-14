/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_box.h"

#include "base/event_filter.h"
#include "base/random.h"
#include "api/api_premium.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/send_credits_box.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/ui_integration.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/payments_form.h"
#include "payments/payments_checkout_process.h"
#include "payments/payments_non_panel_process.h"
#include "settings/settings_credits.h"
#include "settings/settings_premium.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

namespace Ui {
namespace {

constexpr auto kPriceTabAll = 0;
constexpr auto kPriceTabLimited = -1;
constexpr auto kGiftMessageLimit = 255;
constexpr auto kSentToastDuration = 3 * crl::time(1000);

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
};

class PreviewDelegate final : public DefaultElementDelegate {
public:
	PreviewDelegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		Fn<void()> update);

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	Context elementContext() override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

};

class PreviewWrap final : public Ui::RpWidget {
public:
	PreviewWrap(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		rpl::producer<GiftDetails> details);
	~PreviewWrap();

private:
	void paintEvent(QPaintEvent *e) override;

	void resizeTo(int width);
	void prepare(rpl::producer<GiftDetails> details);

	const not_null<History*> _history;
	const std::unique_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;
	AdminLog::OwnedItem _item;
	QPoint _position;

};

PreviewDelegate::PreviewDelegate(
	not_null<QWidget*> parent,
	not_null<Ui::ChatStyle*> st,
	Fn<void()> update)
: _parent(parent)
, _pathGradient(MakePathShiftGradient(st, update)) {
}

bool PreviewDelegate::elementAnimationsPaused() {
	return _parent->window()->isActiveWindow();
}

auto PreviewDelegate::elementPathShiftGradient()
-> not_null<Ui::PathShiftGradient*> {
	return _pathGradient.get();
}

Context PreviewDelegate::elementContext() {
	return Context::History;
}

auto GenerateGiftMedia(
	not_null<Element*> parent,
	Element *replacing,
	const GiftDetails &data)
-> Fn<void(Fn<void(std::unique_ptr<MediaGenericPart>)>)> {
	return [=](Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
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
		const auto title = v::match(descriptor, [&](GiftTypePremium gift) {
			return tr::lng_action_gift_premium_months(
				tr::now,
				lt_count,
				gift.months);
		}, [&](const GiftTypeStars &gift) {
			return tr::lng_action_gift_got_subtitle(
				tr::now,
				lt_user,
				parent->history()->session().user()->shortName());
		});
		auto textFallback = v::match(descriptor, [&](GiftTypePremium gift) {
			return tr::lng_action_gift_premium_about(
				tr::now,
				Ui::Text::RichLangValue);
		}, [&](const GiftTypeStars &gift) {
			return tr::lng_action_gift_got_stars_text(
				tr::now,
				lt_count,
				gift.convertStars,
				Ui::Text::RichLangValue);
		});
		auto description = data.text.empty()
			? std::move(textFallback)
			: data.text;
		pushText(Ui::Text::Bold(title), st::giftBoxPreviewTitlePadding);
		pushText(
			std::move(description),
			st::giftBoxPreviewTextPadding,
			{},
			Core::MarkedTextContext{
				.session = &parent->history()->session(),
				.customEmojiRepaint = [parent] { parent->repaint(); },
			});
	};
}

PreviewWrap::PreviewWrap(
	not_null<QWidget*> parent,
	not_null<Main::Session*> session,
	rpl::producer<GiftDetails> details)
: RpWidget(parent)
, _history(session->data().history(session->userPeerId()))
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<Ui::ChatStyle>(
	_history->session().colorIndicesValue()))
, _delegate(std::make_unique<PreviewDelegate>(
	parent,
	_style.get(),
	[=] { update(); }))
, _position(0, st::msgMargin.bottom()) {
	_style->apply(_theme.get());

	using namespace HistoryView;
	session->data().viewRepaintRequest(
	) | rpl::start_with_next([=](not_null<const Element*> view) {
		if (view == _item.get()) {
			update();
		}
	}, lifetime());

	session->downloaderTaskFinished() | rpl::start_with_next([=] {
		update();
	}, lifetime());

	prepare(std::move(details));
}

void ShowSentToast(
		not_null<Window::SessionController*> window,
		const GiftDescriptor &descriptor) {
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
			Ui::Text::RichLangValue);
	}, [&](const GiftTypeStars &gift) {
		return tr::lng_gift_sent_about(
			tr::now,
			lt_count,
			gift.stars,
			Ui::Text::RichLangValue);
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
	const auto preview = Ui::CreateChild<Ui::RpWidget>(widget.get());
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
			return tr::lng_gift_stars_title(tr::now, lt_count, data.stars);
		});
		const auto text = tr::lng_action_gift_received(
			tr::now,
			lt_user,
			_history->session().user()->shortName(),
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
			GenerateGiftMedia(owned.get(), _item.get(), details),
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
				list.push_back({
					.id = gift.id,
					.stars = gift.stars,
					.convertStars = gift.convertStars,
					.document = gift.document,
					.limitedCount = gift.limitedCount,
					.limitedLeft = gift.limitedLeft,
				});
			}
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
		std::vector<Button> buttons;
		int selected = -1;
		int active = -1;
	};
	const auto state = raw->lifetime().make_state<State>();
	state->prices = std::move(
		gifts
	) | rpl::map([](const std::vector<GiftTypeStars> &gifts) {
		auto result = std::vector<int>();
		result.push_back(kPriceTabAll);
		auto same = true;
		auto sameKey = 0;
		for (const auto &gift : gifts) {
			if (same) {
				const auto key = gift.stars * (gift.limitedCount ? -1 : 1);
				if (!sameKey) {
					sameKey = key;
				} else if (sameKey != key) {
					same = false;
				}
			}

			if (gift.limitedCount
				&& (result.size() < 2 || result[1] != kPriceTabLimited)) {
				result.insert(begin(result) + 1, kPriceTabLimited);
			}
			if (!ranges::contains(result, gift.stars)) {
				result.push_back(gift.stars);
			}
		}
		if (same) {
			return std::vector<int>();
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
		const auto height = state->buttons.empty()
			? 0
			: (y
				+ state->buttons.back().geometry.height()
				+ st::giftBoxTabsMargin.bottom());
		raw->resize(raw->width(), height);
		raw->update();
	}, raw->lifetime());

	raw->setMouseTracking(true);
	raw->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		switch (type) {
		case QEvent::Leave: setSelected(-1); break;
		case QEvent::MouseMove: {
			const auto position = static_cast<QMouseEvent*>(e.get())->pos();
			for (auto i = 0, c = int(state->buttons.size()); i != c; ++i) {
				if (state->buttons[i].geometry.contains(position)) {
					setSelected(i);
					break;
				}
			}
		} break;
		case QEvent::MouseButtonPress: {
			const auto me = static_cast<QMouseEvent*>(e.get());
			if (me->button() != Qt::LeftButton) {
				break;
			}
			const auto position = me->pos();
			for (auto i = 0, c = int(state->buttons.size()); i != c; ++i) {
				if (state->buttons[i].geometry.contains(position)) {
					setActive(i);
					break;
				}
			}
		} break;
		}
	}, raw->lifetime());

	raw->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		const auto padding = st::giftBoxTabPadding;
		for (const auto &button : state->buttons) {
			const auto geometry = button.geometry;
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

[[nodiscard]] not_null<Ui::InputField*> AddPartInput(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		not_null<QWidget*> outer,
		rpl::producer<QString> placeholder,
		QString current,
		int limit) {
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::giftBoxTextField,
			Ui::InputField::Mode::NoNewlines,
			std::move(placeholder),
			current),
		st::giftBoxTextPadding);
	field->setMaxLength(limit);
	Ui::AddLengthLimitLabel(field, limit, std::nullopt, st::giftBoxLimitTop);

	const auto toggle = Ui::CreateChild<Ui::EmojiButton>(
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
		Ui::InsertEmojiAtCursor(field->textCursor(), data.emoji);
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
			.giftId = gift.id,
			.randomId = details.randomId,
			.message = details.text,
			.user = peer->asUser(),
			.limitedCount = gift.limitedCount,
			.anonymous = details.anonymous,
		}, done, processNonPanelPaymentFormFactory);
	});
}

void SendGiftBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		std::shared_ptr<Api::PremiumGiftCodeOptions> api,
		const GiftDescriptor &descriptor) {
	box->setStyle(st::giftBox);
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_gift_send_title());
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	const auto session = &window->session();
	auto cost = rpl::single([&] {
		return v::match(descriptor, [&](const GiftTypePremium &data) {
			if (data.currency == Ui::kCreditsCurrency) {
				return Ui::CreditsEmojiSmall(session).append(
					Lang::FormatCountDecimal(std::abs(data.cost)));
			}
			return TextWithEntities{
				FillAmountAndCurrency(data.cost, data.currency),
			};
		}, [&](const GiftTypeStars &data) {
			return Ui::CreditsEmojiSmall(session).append(
				Lang::FormatCountDecimal(std::abs(data.stars)));
		});
	}());

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
	const auto document = LookupGiftSticker(&window->session(), descriptor);
	if ((state->media = document ? document->createMediaView() : nullptr)) {
		state->media->checkStickerLarge();
	}

	const auto container = box->verticalLayout();
	container->add(object_ptr<PreviewWrap>(
		container,
		session,
		state->details.value()));

	const auto limit = StarGiftMessageLimit(&window->session());
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
		.session = &window->session(),
		.show = window->uiShow(),
		.field = text,
		.customEmojiPaused = [=] {
			using namespace Window;
			return window->isGifPausedAtLeastFor(GifPauseReason::Layer);
		},
		.allowPremiumEmoji = allow,
		.allowMarkdownTags = {
			Ui::InputField::kTagBold,
			Ui::InputField::kTagItalic,
			Ui::InputField::kTagUnderline,
			Ui::InputField::kTagStrikeOut,
			Ui::InputField::kTagSpoiler,
		}
	});
	Ui::Emoji::SuggestionsController::Init(
		box->getDelegate()->outerContainer(),
		text,
		&window->session(),
		{ .suggestCustomEmoji = true, .allowCustomWithoutPremium = allow });

	if (v::is<GiftTypeStars>(descriptor)) {
		AddDivider(container);
		AddSkip(container);
		container->add(
			object_ptr<Ui::SettingsButton>(
				container,
				tr::lng_gift_send_anonymous(),
				st::settingsButtonNoIcon)
		)->toggleOn(rpl::single(false))->toggledValue(
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
		AddDividerText(container, tr::lng_gift_send_anonymous_about(
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
		const auto weak = Ui::MakeWeak(box);
		const auto done = [=](Payments::CheckoutResult result) {
			if (result == Payments::CheckoutResult::Paid) {
				const auto copy = state->media;
				window->showPeerHistory(peer);
				ShowSentToast(window, descriptor);
			}
			if (const auto strong = weak.data()) {
				box->closeBox();
			}
		};
		SendGift(window, peer, api, details, done);
	});
	SetButtonMarkedLabel(
		button,
		tr::lng_gift_send_button(
			lt_cost,
			std::move(cost),
			Ui::Text::WithEntities),
		session,
		st::creditsBoxButtonLabel,
		st::giftBox.button.textFg->c);
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
		.delegate = Delegate(window),
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

		auto x = padding.left();
		auto y = padding.top();
		state->buttons.resize(gifts.list.size());
		for (auto &button : state->buttons) {
			if (!button) {
				button = std::make_unique<GiftButton>(raw, &state->delegate);
				button->show();
			}
		}
		const auto api = gifts.api;
		for (auto i = 0, count = int(gifts.list.size()); i != count; ++i) {
			const auto button = state->buttons[i].get();
			const auto &descriptor = gifts.list[i];
			button->setDescriptor(descriptor);

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
				if (star && star->limitedCount && !star->limitedLeft) {
					window->showToast({
						.title = tr::lng_gift_sold_out_title(tr::now),
						.text = tr::lng_gift_sold_out_text(
							tr::now,
							lt_count_decimal,
							star->limitedCount,
							Ui::Text::RichLangValue),
					});
				} else {
					window->show(
						Box(SendGiftBox, window, peer, api, descriptor));
				}
			});
		}
		if (gifts.list.size() % perRow) {
			y += padding.bottom() + single.height();
		} else {
			y += padding.bottom() - st::giftBoxGiftSkip.y();
		}
		raw->resize(raw->width(), gifts.list.empty() ? 0 : y);
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
				? (!gift.limitedCount)
				: (price && gift.stars != price);
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

	{
		const auto widget = CreateChild<RpWidget>(content);
		using ColoredMiniStars = Premium::ColoredMiniStars;
		const auto stars = widget->lifetime().make_state<ColoredMiniStars>(
			widget,
			false,
			Premium::MiniStars::Type::BiStars);
		stars->setColorOverride(Premium::CreditsIconGradientStops());
		widget->resize(
			st::boxWidth - stUser.photoSize,
			stUser.photoSize * 2);
		content->sizeValue(
		) | rpl::start_with_next([=](const QSize &size) {
			widget->moveToLeft((size.width() - widget->width()) / 2, 0);
			const auto starsRect = Rect(widget->size());
			stars->setPosition(starsRect.topLeft());
			stars->setSize(starsRect.size());
			widget->lower();
		}, widget->lifetime());
		widget->paintRequest(
		) | rpl::start_with_next([=](const QRect &r) {
			auto p = QPainter(widget);
			p.fillRect(r, Qt::transparent);
			stars->paint(p);
		}, widget->lifetime());
	}
	AddSkip(content);
	AddSkip(box->verticalLayout());

	const auto premiumClickHandlerFilter = [=](const auto &...) {
		Settings::ShowPremium(window, u"gift_send"_q);
		return false;
	};
	const auto starsClickHandlerFilter = [=](const auto &...) {
		window->showSettings(Settings::CreditsId());
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
	AddBlock(content, window, {
		.subtitle = tr::lng_gift_stars_subtitle(),
		.about = tr::lng_gift_stars_about(
			lt_name,
			rpl::single(Text::Bold(peer->shortName())),
			lt_link,
			tr::lng_gift_stars_link() | Text::ToLink(),
			Text::WithEntities),
		.aboutFilter = starsClickHandlerFilter,
		.content = MakeStarsGifts(window, peer),
	});
}

} // namespace

void ChooseStarGiftRecipient(
		not_null<Window::SessionController*> controller) {
	class Controller final : public ContactsBoxController {
	public:
		Controller(
			not_null<Main::Session*> session,
			Fn<void(not_null<PeerData*>)> choose)
		: ContactsBoxController(session)
		, _choose(std::move(choose)) {
		}

	protected:
		std::unique_ptr<PeerListRow> createRow(
				not_null<UserData*> user) override {
			if (user->isSelf()
				|| user->isBot()
				|| user->isServiceUser()
				|| user->isInaccessible()) {
				return nullptr;
			}
			return ContactsBoxController::createRow(user);
		}

		void rowClicked(not_null<PeerListRow*> row) override {
			_choose(row->peer());
		}

	private:
		const Fn<void(not_null<PeerData*>)> _choose;

	};
	auto initBox = [=](not_null<PeerListBox*> peersBox) {
		peersBox->setTitle(tr::lng_gift_premium_or_stars());
		peersBox->addButton(tr::lng_cancel(), [=] { peersBox->closeBox(); });
	};

	auto listController = std::make_unique<Controller>(
		&controller->session(),
		[=](not_null<PeerData*> peer) {
			ShowStarGiftBox(controller, peer);
		});
	controller->show(
		Box<PeerListBox>(std::move(listController), std::move(initBox)),
		LayerOption::KeepOther);
}

void ShowStarGiftBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box(GiftBox, controller, peer));
}

} // namespace Ui
