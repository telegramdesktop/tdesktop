/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/gift_credits_box.h"

#include "api/api_credits.h"
#include "api/api_premium.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/send_credits_box.h"
#include "chat_helpers/stickers_gift_box_pack.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/media/history_view_sticker_player.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "settings/settings_credits.h"
#include "settings/settings_premium.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/label_with_custom_emoji.h"
#include "ui/widgets/shadow.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

#include "data/stickers/data_stickers.h"
#include "data/data_document.h"
#include "data/data_document_media.h"

namespace Ui {
namespace {

constexpr auto kPriceTabAll = 0;
constexpr auto kPriceTabLimited = -1;
constexpr auto kGiftsPerRow = 3;
constexpr auto kGiftMessageLimit = 256;

using namespace HistoryView;

struct GiftTypePremium {
	int64 cost = 0;
	QString currency;
	int months = 0;
	int discountPercent = 0;

	[[nodiscard]] friend inline bool operator==(
		const GiftTypePremium &,
		const GiftTypePremium &) = default;
};

struct GiftTypeStars {
	uint64 id = 0;
	int64 stars = 0;
	DocumentData *document = nullptr;
	bool limited = false;

	[[nodiscard]] friend inline bool operator==(
		const GiftTypeStars&,
		const GiftTypeStars&) = default;
};

struct GiftDescriptor : std::variant<GiftTypePremium, GiftTypeStars> {
	using variant::variant;

	[[nodiscard]] friend inline bool operator==(
		const GiftDescriptor&,
		const GiftDescriptor&) = default;
};

struct GiftDetails {
	GiftDescriptor descriptor;
	QString text;
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
				const base::flat_map<uint16, ClickHandlerPtr> &links = {}) {
			if (text.empty()) {
				return;
			}
			push(std::make_unique<MediaGenericTextPart>(
				std::move(text),
				margins,
				links));
		};
		const auto sticker = [=] {
			using Tag = ChatHelpers::StickerLottieSize;
			const auto &session = parent->history()->session();
			auto &packs = session.giftBoxStickersPacks();
			packs.load();
			auto sticker = v::match(descriptor, [&](GiftTypePremium data) {
				return packs.lookup(data.months);
			}, [&](GiftTypeStars data) {
				return data.document
					? data.document
					: packs.lookup(packs.monthsForStars(data.stars));
			});
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
		const auto title = data.anonymous
			? tr::lng_action_gift_anonymous(tr::now)
			: tr::lng_action_gift_got_subtitle(
				tr::now,
				lt_user,
				parent->data()->history()->session().user()->shortName());
		auto textFallback = v::match(descriptor, [&](GiftTypePremium data) {
			return TextWithEntities{
				u"Use all those premium features with joy!"_q
			};
		}, [&](GiftTypeStars data) {
			return tr::lng_action_gift_got_stars_text(
				tr::now,
				lt_cost,
				tr::lng_gift_stars_title(
					tr::now,
					lt_count,
					data.stars,
					Ui::Text::Bold),
				Ui::Text::WithEntities);
		});
		auto description = data.text.isEmpty()
			? std::move(textFallback)
			: TextWithEntities{ data.text };
		pushText(Ui::Text::Bold(title), st::giftBoxPreviewTitlePadding);
		pushText(std::move(description), st::giftBoxPreviewTextPadding);
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
		const auto text = details.anonymous
			? tr::lng_action_gift_received_anonymous(tr::now, lt_cost, cost)
			: tr::lng_action_gift_received(
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

[[nodiscard]] rpl::producer<std::vector<GiftTypePremium>> GiftsPremium(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer) {
	struct Session {
		std::vector<GiftTypePremium> last;
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
			if (map.last != list) {
				map.last = list;
				consumer.put_next_copy(list);
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
					.document = gift.document,
					.limited = (gift.limitedCount > 0),
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
				const auto key = gift.stars * (gift.limited ? -1 : 1);
				if (!sameKey) {
					sameKey = key;
				} else if (sameKey != key) {
					same = false;
				}
			}

			if (gift.limited
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

class GiftButtonDelegate {
public:
	[[nodiscard]] virtual TextWithEntities star() = 0;
	[[nodiscard]] virtual std::any textContext() = 0;
	[[nodiscard]] virtual QSize buttonSize() = 0;
	[[nodiscard]] virtual QImage background() = 0;
	[[nodiscard]] virtual DocumentData *lookupSticker(
		const GiftDescriptor &descriptor) = 0;
};

class GiftButton final : public AbstractButton {
public:
	GiftButton(QWidget *parent, not_null<GiftButtonDelegate*> delegate);
	using AbstractButton::AbstractButton;

	void setDescriptor(const GiftDescriptor &descriptor);
	void setGeometry(QRect inner, QMargins extend);

private:
	void paintEvent(QPaintEvent *e) override;

	void setDocument(not_null<DocumentData*> document);

	const not_null<GiftButtonDelegate*> _delegate;
	GiftDescriptor _descriptor;
	Text::String _text;
	Text::String _price;
	QRect _button;
	QMargins _extend;

	std::unique_ptr<StickerPlayer> _player;
	rpl::lifetime _mediaLifetime;

};

GiftButton::GiftButton(
	QWidget *parent,
	not_null<GiftButtonDelegate*> delegate)
: AbstractButton(parent)
, _delegate(delegate) {
}

void GiftButton::setDescriptor(const GiftDescriptor &descriptor) {
	if (_descriptor == descriptor) {
		return;
	}
	auto player = base::take(_player);
	_mediaLifetime.destroy();
	_descriptor = descriptor;
	v::match(descriptor, [&](const GiftTypePremium &data) {
		const auto months = data.months;
		const auto years = (months % 12) ? 0 : months / 12;
		_text = Text::String(st::giftBoxGiftHeight / 4);
		_text.setMarkedText(
			st::defaultTextStyle,
			Text::Bold(years
				? tr::lng_years(tr::now, lt_count, years)
				: tr::lng_months(tr::now, lt_count, months)
			).append('\n').append(
				tr::lng_gift_premium_label(tr::now)
			));
		_price.setText(
			st::semiboldTextStyle,
			FillAmountAndCurrency(
				data.cost,
				data.currency,
				true));
	}, [&](const GiftTypeStars &data) {
		_price.setMarkedText(
			st::semiboldTextStyle,
			_delegate->star().append(QString::number(data.stars)),
			kMarkupTextOptions,
			_delegate->textContext());
	});
	if (const auto document = _delegate->lookupSticker(descriptor)) {
		setDocument(document);
	}

	const auto buttonw = _price.maxWidth();
	const auto buttonh = st::semiboldFont->height;
	const auto inner = QRect(
		QPoint(),
		QSize(buttonw, buttonh)
	).marginsAdded(st::giftBoxButtonPadding);
	const auto single = _delegate->buttonSize();
	const auto skipx = (single.width() - inner.width()) / 2;
	const auto skipy = single.height()
		- st::giftBoxButtonBottom
		- inner.height();
	const auto outer = (single.width() - 2 * skipx);
	_button = QRect(skipx, skipy, outer, inner.height());
}

void GiftButton::setDocument(not_null<DocumentData*> document) {
	const auto media = document->createMediaView();
	media->checkStickerLarge();
	media->goodThumbnailWanted();

	rpl::single() | rpl::then(
		document->owner().session().downloaderTaskFinished()
	) | rpl::filter([=] {
		return media->loaded();
	}) | rpl::start_with_next([=] {
		_mediaLifetime.destroy();

		auto result = std::unique_ptr<StickerPlayer>();
		const auto sticker = document->sticker();
		if (sticker->isLottie()) {
			result = std::make_unique<HistoryView::LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
					media.get(),
					ChatHelpers::StickerLottieSize::InlineResults,
					st::giftBoxStickerSize,
					Lottie::Quality::High));
		} else if (sticker->isWebm()) {
			result = std::make_unique<HistoryView::WebmPlayer>(
				media->owner()->location(),
				media->bytes(),
				st::giftBoxStickerSize);
		} else {
			result = std::make_unique<HistoryView::StaticStickerPlayer>(
				media->owner()->location(),
				media->bytes(),
				st::giftBoxStickerSize);
		}
		result->setRepaintCallback([=] { update(); });
		_player = std::move(result);
		update();
	}, _mediaLifetime);
}

void GiftButton::setGeometry(QRect inner, QMargins extend) {
	_extend = extend;
	AbstractButton::setGeometry(inner.marginsAdded(extend));
}

void GiftButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto position = QPoint(_extend.left(), _extend.top());
	p.drawImage(0, 0, _delegate->background());

	if (_player && _player->ready()) {
		const auto paused = !isOver();
		auto info = _player->frame(
			st::giftBoxStickerSize,
			QColor(0, 0, 0, 0),
			false,
			crl::now(),
			paused);
		const auto finished = (info.index + 1 == _player->framesCount());
		if (!finished || !paused) {
			_player->markFrameShown();
		}
		const auto size = info.image.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				(width() - size.width()) / 2,
				(_text.isEmpty()
					? st::giftBoxStickerStarTop
					: st::giftBoxStickerTop),
				size.width(),
				size.height()),
			info.image);
	}

	auto hq = PainterHighQualityEnabler(p);
	const auto premium = v::is<GiftTypePremium>(_descriptor);
	const auto singlew = _delegate->buttonSize().width();
	const auto font = st::semiboldFont;
	p.setFont(font);
	const auto text = v::match(_descriptor, [&](GiftTypePremium data) {
		if (data.discountPercent > 0) {
			p.setBrush(st::attentionBoxButton.textFg);
			const auto kMinus = QChar(0x2212);
			return kMinus + QString::number(data.discountPercent) + '%';
		}
		return QString();
	}, [&](const GiftTypeStars &data) {
		if (data.limited) {
			p.setBrush(st::windowActiveTextFg);
			return tr::lng_gift_stars_limited(tr::now);
		}
		return QString();
	});
	if (!text.isEmpty()) {
		p.setPen(Qt::NoPen);
		const auto twidth = font->width(text);
		const auto pos = position + QPoint(singlew - twidth, font->height);
		p.save();
		p.translate(pos);
		p.rotate(45.);
		p.translate(-pos);
		p.drawRect(-5 * twidth, position.y(), twidth * 12, font->height);
		p.setPen(st::windowBg);
		p.drawText(pos - QPoint(0, font->descent), text);
		p.restore();
	}
	p.setBrush(premium ? st::lightButtonBgOver : st::creditsBg3);
	p.setPen(Qt::NoPen);
	if (!premium) {
		p.setOpacity(0.12);
	}
	const auto geometry = _button.translated(position);
	const auto radius = geometry.height() / 2.;
	p.drawRoundedRect(geometry, radius, radius);
	if (!premium) {
		p.setOpacity(1.);
	}

	if (!_text.isEmpty()) {
		p.setPen(st::windowFg);
		_text.draw(p, {
			.position = (position
				+ QPoint(0, st::giftBoxPremiumTextTop)),
			.availableWidth = singlew,
			.align = style::al_top,
		});
	}

	const auto padding = st::giftBoxButtonPadding;
	p.setPen(premium ? st::windowActiveTextFg : st::creditsFg);
	_price.draw(p, {
		.position = (geometry.topLeft()
			+ QPoint(padding.left(), padding.top())),
		.availableWidth = _price.maxWidth(),
	});
}

[[nodiscard]] not_null<Ui::InputField*> AddPartInput(
		not_null<Ui::VerticalLayout*> container,
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
	Ui::AddLengthLimitLabel(field, limit);
	return field;
}

void SendGiftBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		const GiftDescriptor &descriptor) {
	box->setStyle(st::giftBox);
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_gift_send_title());
	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	const auto session = &window->session();
	const auto context = Core::MarkedTextContext{
		.session = session,
		.customEmojiRepaint = [] {},
	};
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
	const auto button = box->addButton(rpl::single(QString()), [=] {
		box->closeBox();
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

	struct State {
		rpl::variable<GiftDetails> details;
	};
	const auto state = box->lifetime().make_state<State>();
	state->details = GiftDetails{
		.descriptor = descriptor,
	};

	const auto container = box->verticalLayout();
	container->add(object_ptr<PreviewWrap>(
		container,
		session,
		state->details.value()));

	const auto text = AddPartInput(
		container,
		tr::lng_gift_send_message(),
		QString(),
		kGiftMessageLimit);
	text->changes() | rpl::start_with_next([=] {
		auto now = state->details.current();
		now.text = text->getLastText();
		state->details = std::move(now);
	}, text->lifetime());

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
	AddDividerText(container, tr::lng_gift_send_anonymous_about(
		lt_user,
		rpl::single(peer->shortName()),
		lt_recipient,
		rpl::single(peer->shortName())));

	const auto buttonWidth = st::boxWideWidth
		- st::giftBox.buttonPadding.left()
		- st::giftBox.buttonPadding.right();
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
		rpl::producer<std::vector<GiftDescriptor>> gifts) {
	auto result = object_ptr<RpWidget>((QWidget*)nullptr);
	const auto raw = result.data();

	class Delegate final : public GiftButtonDelegate {
	public:
		Delegate(
			not_null<Window::SessionController*> window,
			not_null<PeerData*> peer)
		: _window(window)
		, _peer(peer) {
		}

		TextWithEntities star() override {
			return _peer->owner().customEmojiManager().creditsEmoji();
		}
		std::any textContext() override {
			return Core::MarkedTextContext{
				.session = &_peer->session(),
				.customEmojiRepaint = [] {},
			};
		}
		QSize buttonSize() override {
			if (!_single.isEmpty()) {
				return _single;
			}
			const auto width = st::boxWideWidth;
			const auto padding = st::giftBoxPadding;
			const auto available = width - padding.left() - padding.right();
			const auto singlew = (available - 2 * st::giftBoxGiftSkip.x())
				/ kGiftsPerRow;
			_single = QSize(singlew, st::giftBoxGiftHeight);
			return _single;
		}
		void setBackground(QImage bg) {
			_bg = std::move(bg);
		}
		QImage background() override {
			return _bg;
		}
		DocumentData *lookupSticker(
				const GiftDescriptor &descriptor) {
			const auto &session = _window->session();
			auto &packs = session.giftBoxStickersPacks();
			packs.load();
			return v::match(descriptor, [&](GiftTypePremium data) {
				return packs.lookup(data.months);
			}, [&](GiftTypeStars data) {
				return data.document
					? data.document
					: packs.lookup(packs.monthsForStars(data.stars));
			});
		}

	private:
		const not_null<Window::SessionController*> _window;
		const not_null<PeerData*> _peer;
		QSize _single;
		QImage _bg;

	};

	struct State {
		Delegate delegate;
		std::vector<std::unique_ptr<GiftButton>> buttons;
	};
	const auto state = raw->lifetime().make_state<State>(State{
		.delegate = Delegate(window, peer),
	});
	const auto single = state->delegate.buttonSize();
	const auto shadow = st::defaultDropdownMenu.wrap.shadow;
	const auto extend = shadow.extend;

	const auto bgSize = QRect(QPoint(), single ).marginsAdded(extend).size();
	const auto ratio = style::DevicePixelRatio();
	auto bg = QImage(
		bgSize * ratio,
		QImage::Format_ARGB32_Premultiplied);
	bg.setDevicePixelRatio(ratio);
	bg.fill(Qt::transparent);

	const auto radius = st::giftBoxGiftRadius;
	const auto rect = QRect(QPoint(), bgSize).marginsRemoved(extend);

	{
		auto p = QPainter(&bg);
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(0.3);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowShadowFg);
		p.drawRoundedRect(
			QRectF(rect).translated(0, radius / 12.),
			radius,
			radius);
	}
	bg = bg.scaled(
		(bgSize * ratio) / 2,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	bg = Images::Blur(std::move(bg), true);
	bg = bg.scaled(
		bgSize * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	{
		auto p = QPainter(&bg);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(rect, radius, radius);
	}

	state->delegate.setBackground(std::move(bg));
	std::move(
		gifts
	) | rpl::start_with_next([=](const std::vector<GiftDescriptor> &gifts) {
		const auto width = st::boxWideWidth;
		const auto padding = st::giftBoxPadding;
		const auto available = width - padding.left() - padding.right();

		auto x = padding.left();
		auto y = padding.top();
		state->buttons.resize(gifts.size());
		for (auto &button : state->buttons) {
			if (!button) {
				button = std::make_unique<GiftButton>(raw, &state->delegate);
				button->show();
			}
		}
		for (auto i = 0, count = int(gifts.size()); i != count; ++i) {
			const auto button = state->buttons[i].get();
			const auto &descriptor = gifts[i];
			button->setDescriptor(descriptor);

			const auto last = !((i + 1) % kGiftsPerRow);
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
				window->show(Box(SendGiftBox, window, peer, descriptor));
			});
		}
		if (gifts.size() % kGiftsPerRow) {
			y += padding.bottom() + single.height();
		} else {
			y += padding.bottom() - st::giftBoxGiftSkip.y();
		}
		raw->resize(raw->width(), gifts.empty() ? 0 : y);
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
		rpl::variable<std::vector<GiftDescriptor>> gifts;
	};
	auto state = std::make_unique<State>();

	state->gifts = GiftsPremium(&window->session(), peer) | rpl::map([=](
			const std::vector<GiftTypePremium> &gifts) {
		return gifts | ranges::to<std::vector<GiftDescriptor>>;
	});

	auto result = MakeGiftsList(window, peer, state->gifts.value());
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
				? (!gift.limited)
				: (price && gift.stars != price);
		}), end(gifts));
		return gifts | ranges::to<std::vector<GiftDescriptor>>();
	})));

	return result;
}

void GiftBox(
		not_null<GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<PeerData*> peer,
		Fn<void()> gifted) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::creditsGiftBox);
	box->setNoContentMargin(true);
	box->setCustomCornersFilling(RectPart::FullTop);
	box->addButton(tr::lng_create_group_back(), [=] { box->closeBox(); });

	FillBg(box);

	const auto &stUser = st::premiumGiftsUserpicButton;
	const auto content = box->verticalLayout();

	AddSkip(content);
	AddSkip(content);

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
			widget->moveToLeft(stUser.photoSize / 2, 0);
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

void ShowGiftCreditsBox(
		not_null<Window::SessionController*> controller,
		Fn<void()> gifted) {

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
		peersBox->setTitle(tr::lng_credits_gift_title());
		peersBox->addButton(tr::lng_cancel(), [=] { peersBox->closeBox(); });
	};

	const auto show = controller->uiShow();
	auto listController = std::make_unique<Controller>(
		&controller->session(),
		[=](not_null<PeerData*> peer) {
			show->showBox(Box(GiftBox, controller, peer, gifted));
		});
	show->showBox(
		Box<PeerListBox>(std::move(listController), std::move(initBox)),
		LayerOption::KeepOther);
}

} // namespace Ui
