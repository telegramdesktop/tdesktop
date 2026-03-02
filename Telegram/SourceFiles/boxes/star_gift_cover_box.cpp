/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/star_gift_cover_box.h"

#include "chat_helpers/stickers_lottie.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "main/main_session.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_document_media.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_single_player.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/painter.h"
#include "ui/text/format_values.h"
#include "ui/top_background_gradient.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/labels.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

constexpr auto kCrossfadeDuration = crl::time(400);
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

struct Released {
	Released() : link(QColor(255, 255, 255)) {
	}

	rpl::variable<TextWithEntities> subtitleText;
	std::optional<Premium::ColoredMiniStars> stars;
	style::owned_color link;
	style::FlatLabel st;
	rpl::variable<PeerData*> by;
	base::unique_qptr<FlatLabel> subtitle;
	base::unique_qptr<AbstractButton> subtitleButton;
	rpl::variable<int> subtitleHeight;
	rpl::variable<bool> subtitleCustom;
	bool outlined = false;
	QColor bg;
	QColor fg;
};

} // namespace

struct UniqueGiftCoverWidget::BackdropView {
	Data::UniqueGiftBackdrop colors;
	QImage gradient;
};

struct UniqueGiftCoverWidget::PatternView {
	DocumentData *document = nullptr;
	std::unique_ptr<Text::CustomEmoji> emoji;
	base::flat_map<int, base::flat_map<float64, QImage>> emojis;
};

struct UniqueGiftCoverWidget::ModelView {
	std::shared_ptr<Data::DocumentMedia> media;
	std::unique_ptr<Lottie::SinglePlayer> lottie;
	rpl::lifetime lifetime;
};

struct UniqueGiftCoverWidget::GiftView {
	std::optional<Data::UniqueGift> gift;
	BackdropView backdrop;
	PatternView pattern;
	ModelView model;
	bool forced = false;
};

struct UniqueGiftCoverWidget::State {
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

	Released released;

	FlatLabel *pretitle = nullptr;
	FlatLabel *title = nullptr;
	RpWidget *attrs = nullptr;

	Fn<void(const Data::UniqueGift &)> updateAttrs;
	Fn<void(float64)> updateColors;
	Fn<void(const BackdropView &, const BackdropView &, float64)>
		updateColorsFromBackdrops;
	Fn<void(ModelView &, const Data::UniqueGiftModel &)> setupModel;
	Fn<void(PatternView &, const Data::UniqueGiftPattern &)> setupPattern;

	FlatLabel *number = nullptr;
	rpl::variable<int> numberTextWidth;
	QImage craftedBadge;
	Info::PeerGifts::GiftBadge craftedBadgeKey;
	rpl::variable<CreditsAmount> resaleAmount;
	Fn<void()> resaleClick;

};

UniqueGiftCoverWidget::UniqueGiftCoverWidget(
	QWidget *parent,
	rpl::producer<UniqueGiftCover> data,
	UniqueGiftCoverArgs &&args)
: RpWidget(parent)
, _state(std::make_unique<State>()) {
	using SpinnerState = Data::GiftUpgradeSpinner::State;

	_state->spinner = std::move(args.upgradeSpinner);

	_state->setupModel = [this](
			ModelView &to,
			const Data::UniqueGiftModel &model) {
		to.lifetime.destroy();

		const auto document = model.document;
		to.media = document->createMediaView();
		to.media->automaticLoad(document->stickerSetOrigin(), nullptr);
		rpl::single() | rpl::then(
			document->session().downloaderTaskFinished()
		) | rpl::filter([&to] {
			return to.media->loaded();
		}) | rpl::on_next([this, &to] {
			const auto lottieSize = st::creditsHistoryEntryStarGiftSize;
			to.lottie = ChatHelpers::LottiePlayerFromDocument(
				to.media.get(),
				ChatHelpers::StickerLottieSize::MessageHistory,
				QSize(lottieSize, lottieSize),
				Lottie::Quality::High);

			to.lifetime.destroy();
			const auto lottie = to.lottie.get();
			lottie->updates() | rpl::on_next([this, lottie] {
				if (_state->now.model.lottie.get() == lottie
					|| _state->crossfade.animating()) {
					update();
				}
				if (const auto onstack = _state->checkSpinnerStart) {
					onstack();
				}
			}, to.lifetime);
		}, to.lifetime);
	};

	_state->setupPattern = [this](
			PatternView &to,
			const Data::UniqueGiftPattern &pattern) {
		const auto document = pattern.document;
		const auto callback = [this, document] {
			if (_state->now.pattern.document == document
				|| _state->crossfade.animating()) {
				update();
			}
			if (const auto onstack = _state->checkSpinnerStart) {
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

	if (const auto spinner = _state->spinner.get()) {
		const auto fillBackdrops = [this, spinner] {
			_state->spinnerBackdrops.clear();
			_state->spinnerBackdrops.reserve(kSpinnerBackdrops);
			const auto push = [this](const Data::UniqueGiftBackdrop &backdrop) {
				if (_state->spinnerBackdrops.size() >= kSpinnerBackdrops) {
					return false;
				}
				const auto already = ranges::contains(
					_state->spinnerBackdrops,
					backdrop,
					&BackdropView::colors);
				if (!already) {
					_state->spinnerBackdrops.push_back({ backdrop });
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
		const auto fillPatterns = [this, spinner] {
			_state->spinnerPatterns.clear();
			_state->spinnerPatterns.reserve(kSpinnerPatterns);
			const auto push = [this](const Data::UniqueGiftPattern &pattern) {
				if (_state->spinnerPatterns.size() >= kSpinnerPatterns) {
					return false;
				}
				const auto already = ranges::contains(
					_state->spinnerPatterns,
					pattern.document.get(),
					&PatternView::document);
				if (!already) {
					_state->setupPattern(
						_state->spinnerPatterns.emplace_back(),
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
		const auto fillModels = [this, spinner] {
			_state->spinnerModels.clear();
			_state->spinnerModels.reserve(kSpinnerModels);
			const auto push = [this](const Data::UniqueGiftModel &model) {
				if (_state->spinnerModels.size() >= kSpinnerModels) {
					return false;
				}
				const auto already = ranges::contains(
					_state->spinnerModels,
					model.document,
					[](const ModelView &view) {
						return view.media->owner();
					});
				if (!already) {
					_state->setupModel(
						_state->spinnerModels.emplace_back(),
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
		_state->checkSpinnerStart = [this, spinner] {
			if (spinner->state.current() != SpinnerState::Loading
				|| _state->crossfading) {
				return;
			}
			for (const auto &pattern : _state->spinnerPatterns) {
				if (!pattern.emoji->ready()) {
					return;
				}
			}
			for (const auto &model : _state->spinnerModels) {
				if (!model.lottie || !model.lottie->ready()) {
					return;
				}
			}
			spinner->state = SpinnerState::Prepared;
			_state->checkSpinnerStart = nullptr;
		};
		spinner->state.value() | rpl::on_next([=](SpinnerState now) {
			if (now == SpinnerState::Preparing) {
				fillBackdrops();
				fillPatterns();
				fillModels();
				spinner->state = SpinnerState::Loading;
				if (!_state->crossfading) {
					_state->next = {};
				}
				_state->checkSpinnerStart();
			} else if (now == SpinnerState::Started) {
				const auto repaint = [this] { update(); };
				_state->backdropSpin.startWithin(
					_state->spinnerBackdrops.size(),
					repaint);
				_state->patternSpin.startWithin(
					_state->spinnerPatterns.size(),
					repaint);
				_state->modelSpin.startWithin(
					_state->spinnerModels.size(),
					repaint);
				_state->spinStarted = crl::now();
			}
		}, lifetime());
	}

	rpl::duplicate(
		data
	) | rpl::on_next([this](const UniqueGiftCover &now) {
		const auto setup = [&](GiftView &to) {
			Expects(!now.spinner);

			to = {};
			to.gift = now.values;
			to.forced = now.force;
			to.backdrop.colors = now.values.backdrop;
			_state->setupModel(to.model, now.values.model);
			_state->setupPattern(to.pattern, now.values.pattern);
		};

		const auto spinner = _state->spinner.get();
		if (!_state->now.gift) {
			setup(_state->now);
			update();
		} else if (now.spinner) {
			Assert(spinner != nullptr);
			Assert(spinner->state.current() == SpinnerState::Prepared);

			spinner->state = SpinnerState::Started;
		} else if (!_state->next.gift || now.force) {
			const auto spinnerState = spinner
				? spinner->state.current()
				: SpinnerState::Initial;
			if (spinnerState == SpinnerState::Initial) {
				setup(_state->next);
			}
		}
	}, lifetime());

	auto subtitleCustomText = args.subtitle
		? std::move(args.subtitle)
		: rpl::single(tr::marked());
	_state->released.subtitleCustom = rpl::duplicate(
		subtitleCustomText
	) | rpl::map([](const TextWithEntities &custom) {
		return !custom.empty();
	});

	const auto repaintedHook = args.repaintedHook;
	const auto updateLinkFg = args.subtitleLinkColored;
	_state->updateColorsFromBackdrops = [this, updateLinkFg](
			const BackdropView &from,
			const BackdropView &to,
			float64 progress) {
		_state->released.bg = (progress == 0.)
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
			const auto &subtitleCustom = _state->released.subtitleCustom;
			_state->released.link.update(subtitleCustom.current()
				? color
				: QColor(255, 255, 255));
		}
		_state->released.fg = color;
		const auto onSale = !_state->released.subtitleCustom.current()
			&& _state->resaleAmount.current()
			&& _state->resaleAmount.current().value() > 0;
		_state->released.subtitle->setTextColorOverride(
			onSale ? QColor(255, 255, 255) : color);
		if (_state->number) {
			_state->number->setTextColorOverride(color);
		}
	};
	_state->updateColors = [this, repaintedHook](float64 progress) {
		if (repaintedHook) {
			repaintedHook(_state->now.gift, _state->next.gift, progress);
		}
		_state->updateColorsFromBackdrops(
			_state->now.backdrop,
			_state->next.backdrop,
			progress);
	};

	if (args.resalePrice) {
		std::move(args.resalePrice) | rpl::on_next([this](CreditsAmount value) {
			_state->resaleAmount = value;
		}, lifetime());
	}
	_state->resaleClick = std::move(args.resaleClick);

	_state->pretitle = args.pretitle
		? CreateChild<FlatLabel>(
			this,
			std::move(args.pretitle),
			st::uniqueGiftPretitle)
		: nullptr;
	if (_state->pretitle) {
		_state->released.stars.emplace(
			this,
			true,
			Premium::MiniStarsType::SlowStars);
		const auto white = QColor(255, 255, 255);
		_state->released.stars->setColorOverride(QGradientStops{
			{ 0., anim::with_alpha(white, .3) },
			{ 1., white },
		});
		_state->pretitle->geometryValue() | rpl::on_next([this](QRect rect) {
			const auto half = rect.height() / 2;
			_state->released.stars->setCenter(rect - QMargins(half, 0, half, 0));
		}, _state->pretitle->lifetime());
		_state->pretitle->setAttribute(Qt::WA_TransparentForMouseEvents);
		_state->pretitle->setTextColorOverride(QColor(255, 255, 255));
		_state->pretitle->paintOn([this](QPainter &p) {
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = _state->pretitle->height() / 2.;
			p.setPen(Qt::NoPen);
			auto bg = _state->released.bg;
			bg.setAlphaF(kGradientButtonBgOpacity * bg.alphaF());
			p.setBrush(bg);
			p.drawRoundedRect(_state->pretitle->rect(), radius, radius);
			p.translate(-_state->pretitle->pos());
			_state->released.stars->paint(p);
		});
	}

	_state->title = CreateChild<FlatLabel>(
		this,
		rpl::duplicate(
			data
		) | rpl::map([](const UniqueGiftCover &now) {
			return now.values.title;
		}),
		st::uniqueGiftTitle);
	_state->title->setTextColorOverride(QColor(255, 255, 255));

	auto numberTextDup = args.numberText
		? rpl::duplicate(args.numberText)
		: rpl::producer<QString>();
	_state->number = args.numberText
		? CreateChild<FlatLabel>(
			this,
			std::move(args.numberText),
			st::uniqueGiftNumber)
		: nullptr;
	if (_state->number) {
		_state->number->setTextColorOverride(QColor(255, 255, 255));
		std::move(
			numberTextDup
		) | rpl::on_next([this](const QString &text) {
			_state->numberTextWidth = text.isEmpty() ? 0 : 1;
		}, lifetime());
	}

	_state->released.by = rpl::duplicate(
		data
	) | rpl::map([](const UniqueGiftCover &cover) {
		return cover.values.releasedBy;
	});
	_state->released.subtitleText = rpl::combine(
		std::move(subtitleCustomText),
		_state->resaleAmount.value(),
		rpl::duplicate(data)
	) | rpl::map([](
			TextWithEntities custom,
			CreditsAmount resalePrice,
			const UniqueGiftCover &cover) {
		if (!custom.empty()) {
			return custom;
		}
		const auto &gift = cover.values;
		if (resalePrice && resalePrice.value() > 0) {
			auto priceText = resalePrice.ton()
				? Text::IconEmoji(&st::tonIconEmojiInSmall).append(
					Lang::FormatCreditsAmountDecimal(resalePrice))
				: Text::IconEmoji(&st::starIconEmojiSmall).append(
					Lang::FormatCountDecimal(resalePrice.whole()));
			return tr::lng_gift_on_sale_for(
				tr::now,
				lt_price,
				priceText,
				tr::marked);
		}
		if (gift.releasedBy) {
			return tr::lng_gift_released_by(
				tr::now,
				lt_name,
				tr::link('@' + gift.releasedBy->username()),
				tr::marked);
		}
		return tr::marked(gift.model.name);
	});

	const auto subtitleOutlined = args.subtitleOutlined;
	const auto subtitleClick = args.subtitleClick;
	rpl::combine(
		_state->released.by.value(),
		_state->released.subtitleCustom.value(),
		_state->resaleAmount.value()
	) | rpl::on_next([=](PeerData *by, bool subtitleCustom, CreditsAmount resale) {
		const auto hasResale = resale && resale.value() > 0;
		_state->released.outlined = (subtitleCustom && subtitleOutlined)
			|| (!subtitleCustom && (hasResale || by));
		_state->released.st = !_state->released.outlined
			? st::uniqueGiftSubtitle
			: (!subtitleCustom && hasResale)
			? st::uniqueGiftOnSale
			: st::uniqueGiftReleasedBy;
		_state->released.st.palette.linkFg = _state->released.link.color();

		const auto session = &_state->now.gift->model.document->session();
		_state->released.subtitle = base::make_unique_q<FlatLabel>(
			this,
			_state->released.subtitleText.value(),
			_state->released.st,
			st::defaultPopupMenu,
			Core::TextContext({ .session = session }));
		const auto subtitle = _state->released.subtitle.get();
		subtitle->show();

		widthValue(
		) | rpl::on_next([=](int width) {
			const auto skip = st::uniqueGiftBottom;
			if (width <= 3 * skip) {
				return;
			}
			const auto available = width - 2 * skip;
			subtitle->resizeToWidth(available);
		}, subtitle->lifetime());
		_state->released.subtitleHeight.force_assign(subtitle->height());
		_state->released.subtitleHeight = subtitle->heightValue();

		const auto handler = subtitleCustom
			? (subtitleClick ? subtitleClick : Fn<void()>())
			: hasResale
			? _state->resaleClick
			: by
			? Fn<void()>([=] { GiftReleasedByHandler(by); })
			: Fn<void()>();

		if (!_state->released.outlined && handler) {
			subtitle->setClickHandlerFilter([=](const auto &...) {
				handler();
				return false;
			});
		} else if (_state->released.outlined) {
			_state->released.subtitleButton = base::make_unique_q<AbstractButton>(
				this);
			const auto button = _state->released.subtitleButton.get();

			button->show();
			subtitle->raise();
			subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);

			if (handler) {
				button->setClickedCallback(handler);
			} else {
				button->setAttribute(Qt::WA_TransparentForMouseEvents);
			}
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
				p.setBrush(_state->released.bg);
				p.setOpacity(0.5);
				p.drawRoundedRect(left, 0, full, height, radius, radius);
			}, button->lifetime());
		} else {
			_state->released.subtitleButton = nullptr;
		}
		_state->updateColors(_state->crossfade.value(
			_state->crossfading ? 1. : 0.));
	}, _state->title->lifetime());

	_state->attrs = args.attributesInfo
		? CreateChild<RpWidget>(this)
		: nullptr;
	_state->updateAttrs = [](const Data::UniqueGift &) {};
	if (_state->attrs) {
		struct AttributeState {
			Text::String name;
			Text::String type;
			Text::String percent;
		};
		struct AttributesState {
			AttributeState model;
			AttributeState pattern;
			AttributeState backdrop;
		};
		const auto astate = lifetime().make_state<AttributesState>();
		const auto setType = [&](AttributeState &state, tr::phrase<> text) {
			state.type = Text::String(
				st::uniqueAttributeType,
				text(tr::now));
		};
		setType(astate->model, tr::lng_auction_preview_model);
		setType(astate->pattern, tr::lng_auction_preview_symbol);
		setType(astate->backdrop, tr::lng_auction_preview_backdrop);

		_state->updateAttrs = [this, astate](const Data::UniqueGift &gift) {
			const auto set = [&](
					AttributeState &state,
					const Data::UniqueGiftAttribute &value) {
				state.name = Text::String(
					st::uniqueAttributeName,
					value.name);
				state.percent = Text::String(
					st::uniqueAttributePercent,
					Data::UniqueGiftAttributeText(value));
			};
			set(astate->model, gift.model);
			set(astate->pattern, gift.pattern);
			set(astate->backdrop, gift.backdrop);
			_state->attrs->update();
		};
		const auto attrsHeight = st::uniqueAttributeTop
			+ st::uniqueAttributePadding.top()
			+ st::uniqueAttributeName.font->height
			+ st::uniqueAttributeType.font->height
			+ st::uniqueAttributePadding.bottom();
		_state->attrs->resize(_state->attrs->width(), attrsHeight);
		_state->attrs->paintOn([this, astate, attrsHeight](QPainter &p) {
			const auto boxPadding = st::giftBoxPadding;
			const auto skip = st::giftBoxGiftSkip.x();
			const auto available = _state->attrs->width()
				- boxPadding.left()
				- boxPadding.right()
				- 2 * skip;
			const auto single = available / 3;
			if (single <= 0) {
				return;
			}
			auto hq = PainterHighQualityEnabler(p);
			auto bg = _state->released.bg;
			bg.setAlphaF(kGradientButtonBgOpacity * bg.alphaF());
			const auto innert = st::uniqueAttributeTop;
			const auto innerh = attrsHeight - innert;
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
				p.setPen(_state->released.fg);
				const auto typew = std::min(inner, state.type.maxWidth());
				state.type.draw(p, {
					.position = QPoint(
						x + (single - typew) / 2,
						innert + padding.top() + state.name.minHeight()),
					.availableWidth = typew,
				});
				p.setPen(Qt::NoPen);
				p.setBrush(anim::color(_state->released.bg, _state->released.fg, 0.3));
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
			auto left = boxPadding.left();
			paint(left, astate->model);
			paint(left + single + skip, astate->backdrop);
			paint(_state->attrs->width() - single - boxPadding.right(), astate->pattern);
		});
	}
	_state->updateAttrs(*_state->now.gift);

	rpl::combine(
		widthValue(),
		_state->released.subtitleHeight.value(),
		_state->numberTextWidth.value()
	) | rpl::on_next([this](int width, int subtitleHeight, int) {
		const auto skip = st::uniqueGiftBottom;
		if (width <= 3 * skip) {
			return;
		}
		const auto available = width - 2 * skip;
		auto top = st::uniqueGiftTitleTop;
		if (_state->pretitle) {
			_state->title->resizeToWidth(available);
			_state->pretitle->move((width - _state->pretitle->width()) / 2, top);
			top += _state->pretitle->height()
				+ (st::uniqueGiftSubtitleTop - st::uniqueGiftTitleTop)
				- _state->title->height();
		}

		if (_state->number && _state->number->textMaxWidth() > 0) {
			const auto titleWidth = _state->title->textMaxWidth();
			_state->title->resizeToWidth(titleWidth);
			const auto numberWidth = _state->number->textMaxWidth();
			_state->number->resizeToWidth(numberWidth);
			const auto gap = st::normalFont->spacew;
			const auto totalWidth = titleWidth + gap + numberWidth;
			const auto groupLeft = (width - totalWidth) / 2;
			_state->title->moveToLeft(groupLeft, top);
			const auto &stTitle = _state->title->st();
			const auto &stNumber = _state->number->st();
			_state->number->moveToLeft(
				groupLeft + titleWidth + gap,
				(top
					+ stTitle.style.font->ascent
					- stNumber.style.font->ascent));
		} else {
			_state->title->resizeToWidth(available);
			_state->title->moveToLeft(skip, top);
		}
		if (_state->pretitle) {
			top += _state->title->height() + st::defaultVerticalListSkip;
		} else {
			top += st::uniqueGiftSubtitleTop - st::uniqueGiftTitleTop;
		}

		_state->released.subtitle->moveToLeft(skip, top);
		top += subtitleHeight + (skip / 2);

		if (_state->attrs) {
			_state->attrs->resizeToWidth(width);
			_state->attrs->moveToLeft(0, top);
			top += _state->attrs->height() + (skip / 2);
		} else {
			top += (skip / 2);
		}
		if (!height() || height() == top) {
			resize(width, top);
		} else {
			_state->heightFinal = top;
			_state->heightAnimation.start([this, width, top] {
				resize(
					width,
					int(base::SafeRound(_state->heightAnimation.value(top))));
			}, height(), top, st::slideWrapDuration);
		}
	}, lifetime());
}

void UniqueGiftCoverWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto progress = _state->crossfade.value(_state->crossfading ? 1. : 0.);
	if (_state->updateAttributesPending && progress >= 0.5) {
		_state->updateAttributesPending = false;
		_state->updateAttrs(*_state->next.gift);
	} else if (_state->updateAttributesPending
		&& !_state->crossfade.animating()) {
		_state->updateAttributesPending = false;
		_state->updateAttrs(*_state->now.gift);
	}
	if (_state->crossfading) {
		_state->updateColors(progress);
	}
	if (progress == 1.) {
		_state->crossfading = false;
		_state->now = base::take(_state->next);
		progress = 0.;
	}

	const auto context = PaintContext{
		.width = width(),
		.patternAreaHeight = st::uniqueGiftSubtitleTop,
	};
	if (_state->spinStarted) {
		paintSpinnerAnimation(p, context);
	} else {
		paintNormalAnimation(p, context, progress);
	}
}

UniqueGiftCoverWidget::~UniqueGiftCoverWidget() = default;

QImage UniqueGiftCoverWidget::prepareBackdrop(
		BackdropView &backdrop,
		const PaintContext &context) {
	const auto ratio = style::DevicePixelRatio();
	const auto gradientSize = QSize(
		context.width,
		std::max(height(), _state->heightFinal));
	if (backdrop.gradient.size() != gradientSize * ratio) {
		backdrop.gradient = CreateTopBgGradient(
			gradientSize,
			backdrop.colors);
	}
	return backdrop.gradient;
}

void UniqueGiftCoverWidget::paintBackdrop(
		QPainter &p,
		BackdropView &backdrop,
		const PaintContext &context) {
	p.drawImage(0, 0, prepareBackdrop(backdrop, context));
}

void UniqueGiftCoverWidget::paintPattern(
		QPainter &p,
		PatternView &pattern,
		const BackdropView &backdrop,
		const PaintContext &context,
		float64 shown) {
	const auto color = backdrop.colors.patternColor;
	const auto key = (color.red() << 16)
		| (color.green() << 8)
		| color.blue();
	PaintBgPoints(
		p,
		PatternBgPoints(),
		pattern.emojis[key],
		pattern.emoji.get(),
		color,
		QRect(0, 0, context.width, context.patternAreaHeight),
		shown);
}

bool UniqueGiftCoverWidget::paintModel(
		QPainter &p,
		ModelView &model,
		const PaintContext &context,
		float64 scale,
		bool paused) {
	const auto lottieSize = st::creditsHistoryEntryStarGiftSize;
	const auto lottie = model.lottie.get();
	const auto factor = style::DevicePixelRatio();
	const auto request = Lottie::FrameRequest{
		.box = QSize(lottieSize, lottieSize) * factor,
	};
	const auto frame = (lottie && lottie->ready())
		? lottie->frameInfo(request)
		: Lottie::Animation::FrameInfo();
	if (frame.image.isNull()) {
		return false;
	}
	const auto size = frame.image.size() / factor;
	const auto rect = QRect(
		QPoint((context.width - size.width()) / 2, st::uniqueGiftModelTop),
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
}

QRect UniqueGiftCoverWidget::prepareCraftFrame(
		QImage &canvas,
		const CraftContext &context) {
	const auto full = this->size();
	const auto ratio = style::DevicePixelRatio();

	if (canvas.size() != full * ratio) {
		canvas = QImage(full * ratio, QImage::Format_ARGB32_Premultiplied);
		canvas.setDevicePixelRatio(ratio);
	}

	LOG(("FULL: %1x%2").arg(full.width()).arg(full.height()));

	const auto expand = context.expandProgress;
	const auto size = QSize(
		anim::interpolate(context.initial.width(), full.width(), expand),
		anim::interpolate(context.initial.height(), full.height(), expand));
	const auto rect = QRect(QPoint(), size);
	auto p = QPainter(&canvas);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::boxRadius * expand;
	const auto bgProgress = context.fadeInProgress;

	auto gradient = QRadialGradient(
		rect.center(),
		size.height() / 2);
	const auto center = anim::color(
		context.initialBg,
		_state->now.backdrop.colors.centerColor,
		bgProgress);
	const auto edge = anim::color(
		context.initialBg,
		_state->now.backdrop.colors.edgeColor,
		bgProgress);
	gradient.setStops({ { 0., center }, { 1., edge } });
	p.setPen(Qt::NoPen);
	if (radius > 0.) {
		const auto more = qCeil(radius * 2);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, more, more, Qt::transparent);
		p.fillRect(rect.width() - more, 0, more, more, Qt::transparent);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		p.setBrush(gradient);
		p.drawRoundedRect(
			rect.marginsAdded({ 0, 0, 0, more }),
			radius,
			radius);
	} else {
		p.fillRect(rect, gradient);
	}

	const auto lottieSize = st::creditsHistoryEntryStarGiftSize;
	const auto paused = (expand == 0.);
	const auto modelScale = expand
		+ ((st::giftBoxStickerTiny.width() / float64(lottieSize))
			* (1. - expand));
	const auto finalShift = (size.height() - lottieSize) / 2;
	const auto shift = (1. - expand) * (finalShift - st::uniqueGiftModelTop);
	p.translate(0, shift);
	paintModel(p, _state->now.model, {
		.width = size.width(),
	}, modelScale, paused);
	p.translate(0, -shift);

	const auto skip = anim::interpolate(size.width() / 3, 0, expand);
	const auto pointsHeight = anim::interpolate(
		context.initial.height(),
		st::uniqueGiftSubtitleTop,
		expand);
	p.translate(-skip, 0);
	paintPattern(p, _state->now.pattern, _state->now.backdrop, {
		.width = size.width() + 2 * skip,
		.patternAreaHeight = pointsHeight,
	}, bgProgress);

	return QRect(QPoint(), size * ratio);
}

bool UniqueGiftCoverWidget::paintGift(
		QPainter &p,
		GiftView &gift,
		const PaintContext &context,
		float64 shown) {
	Expects(gift.gift.has_value());

	paintBackdrop(p, gift.backdrop, context);
	if (gift.gift->pattern.document != gift.gift->model.document) {
		paintPattern(p, gift.pattern, gift.backdrop, context, shown);
	}
	const auto finished = paintModel(p, gift.model, context);
	if (gift.gift->crafted) {
		const auto padding = st::chatUniqueGiftBadgePadding;
		auto badge = Info::PeerGifts::GiftBadge{
			.text = tr::lng_gift_crafted_tag(tr::now),
			.bg1 = gift.gift->backdrop.edgeColor,
			.bg2 = gift.gift->backdrop.patternColor,
			.fg = gift.gift->backdrop.textColor,
		};
		if (_state->craftedBadge.isNull()
			|| _state->craftedBadgeKey != badge) {
			_state->craftedBadgeKey = badge;
			_state->craftedBadge = Info::PeerGifts::ValidateRotatedBadge(
				badge, padding, true);
		}
		p.drawImage(0, 0, _state->craftedBadge);
	}
	return finished;
}

QColor UniqueGiftCoverWidget::backgroundColor() const {
	return _state->released.bg;
}

QColor UniqueGiftCoverWidget::foregroundColor() const {
	return _state->released.fg;
}

void UniqueGiftCoverWidget::paintSpinnerAnimation(
		QPainter &p,
		const PaintContext &context) {
	using SpinnerState = Data::GiftUpgradeSpinner::State;

	const auto select = [&](auto &&list, int index, auto &fallback)
	-> decltype(auto) {
		return (index >= 0) ? list[index] : fallback;
	};

	const auto now = crl::now();
	const auto elapsed = now - _state->spinStarted;
	auto current = _state->spinner->state.current();
	const auto stateTo = [&](SpinnerState to) {
		if (int(current) < int(to)) {
			_state->spinner->state = current = to;
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
			spin.startToTarget([this] { update(); }, slowdown);
			stateTo(finishState);
		} else {
			spin.startWithin(list.size(), [this] { update(); });
		}
	};
	if (anim::Disabled() && current < SpinnerState::FinishedModel) {
		stateTo(SpinnerState::FinishedModel);
		_state->backdropSpin.startToTarget([this] { update(); });
		_state->patternSpin.startToTarget([this] { update(); });
		_state->modelSpin.startToTarget([this] { update(); });
	}
	if (_state->backdropSpin.willIndex != 0) {
		switchTo(SpinnerState::FinishBackdrop, kBackdropStopsAt);
	}
	actualize(
		_state->backdropSpin,
		_state->spinnerBackdrops,
		SpinnerState::FinishBackdrop,
		SpinnerState::FinishedBackdrop);

	if (current == SpinnerState::FinishedBackdrop
		&& _state->patternSpin.willIndex != 0) {
		switchTo(SpinnerState::FinishPattern, kPatternStopsAt);
	}
	actualize(
		_state->patternSpin,
		_state->spinnerPatterns,
		SpinnerState::FinishPattern,
		SpinnerState::FinishedPattern);

	if (current == SpinnerState::FinishedPattern
		&& _state->modelSpin.willIndex != 0) {
		switchTo(SpinnerState::FinishModel, kModelStopsAt);
	}
	actualize(
		_state->modelSpin,
		_state->spinnerModels,
		SpinnerState::FinishModel,
		SpinnerState::FinishedModel,
		2);

	auto &backdropNow = select(
		_state->spinnerBackdrops,
		_state->backdropSpin.nowIndex,
		_state->now.backdrop);
	auto &backdropWill = select(
		_state->spinnerBackdrops,
		_state->backdropSpin.willIndex,
		_state->now.backdrop);
	const auto backdropProgress = _state->backdropSpin.progress();
	_state->updateColorsFromBackdrops(
		backdropNow,
		backdropWill,
		backdropProgress);

	auto &patternNow = select(
		_state->spinnerPatterns,
		_state->patternSpin.nowIndex,
		_state->now.pattern);
	auto &patternWill = select(
		_state->spinnerPatterns,
		_state->patternSpin.willIndex,
		_state->now.pattern);
	const auto patternProgress = _state->patternSpin.progress();
	const auto paintPatterns = [&](
			QPainter &p,
			const BackdropView &backdrop) {
		if (patternProgress >= 1.) {
			paintPattern(p, patternWill, backdrop, context, 1.);
		} else {
			paintPattern(p, patternNow, backdrop, context, 1. - patternProgress);
			if (patternProgress > 0.) {
				paintPattern(p, patternWill, backdrop, context, patternProgress);
			}
		}
	};

	if (backdropProgress >= 1.) {
		p.drawImage(0, 0, prepareBackdrop(backdropWill, context));
		paintPatterns(p, backdropWill);
	} else {
		p.drawImage(0, 0, prepareBackdrop(backdropNow, context));
		paintPatterns(p, backdropNow);

		const auto fade = context.width / 2;
		const auto from = anim::interpolate(
			-fade,
			context.width,
			backdropProgress);
		if (const auto till = from + fade; till > 0) {
			auto faded = prepareBackdrop(backdropWill, context);
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
			const auto imgHeight = faded.height() / ratio;
			q.fillRect(from, 0, fade, imgHeight, brush);
			q.end();

			p.drawImage(
				QRect(0, 0, till, imgHeight),
				faded,
				QRect(0, 0, till * ratio, faded.height()));
		}
	}

	auto &modelWas = select(
		_state->spinnerModels,
		_state->modelSpin.wasIndex,
		_state->now.model);
	auto &modelNow = select(
		_state->spinnerModels,
		_state->modelSpin.nowIndex,
		_state->now.model);
	auto &modelWill = select(
		_state->spinnerModels,
		_state->modelSpin.willIndex,
		_state->now.model);
	const auto modelProgress = _state->modelSpin.progress();
	const auto paintOneModel = [&](ModelView &view, float64 progress) {
		if (progress >= 1. || progress <= -1.) {
			return;
		}
		auto scale = 1.;
		if (progress != 0.) {
			const auto shift = progress * context.width / 2.;
			const auto shown = 1. - std::abs(progress);
			scale = kModelScaleFrom + shown * (1. - kModelScaleFrom);
			p.save();
			p.setOpacity(shown);
			p.translate(int(base::SafeRound(shift)), 0);
		}
		paintModel(p, view, context, scale, true);
		if (progress != 0.) {
			p.restore();
		}
	};
	const auto initial = (_state->modelSpin.nowIndex < 0);
	const auto ending = (current == SpinnerState::FinishedModel)
		&& !_state->modelSpin.willIndex;
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
		paintOneModel(modelWas, wasProgress);
	}
	paintOneModel(modelNow, nowProgress);
	paintOneModel(modelWill, willProgress);

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
		_state->now.gift = *_state->spinner->target;
		_state->now.backdrop = take(_state->spinnerBackdrops);
		_state->now.pattern = take(_state->spinnerPatterns);
		_state->now.model = take(_state->spinnerModels);
		_state->now.forced = true;
		_state->spinStarted = 0;
		_state->spinner->state = SpinnerState::Finished;
	}
}

void UniqueGiftCoverWidget::paintNormalAnimation(
		QPainter &p,
		const PaintContext &context,
		float64 progress) {
	if (progress < 1.) {
		const auto finished = paintGift(p, _state->now, context, 1. - progress)
			|| (_state->next.forced
				&& (!_state->crossfading
					|| !_state->crossfade.animating()));
		const auto next = finished
			? _state->next.model.lottie.get()
			: nullptr;
		if (next && next->ready()) {
			_state->crossfading = true;
			_state->updateAttributesPending = true;
			_state->crossfade.start([this] {
				update();
			}, 0., 1., kCrossfadeDuration);
		} else if (finished) {
			if (const auto onstack = _state->checkSpinnerStart) {
				onstack();
			}
		}
	}
	if (progress > 0.) {
		p.setOpacity(progress);
		paintGift(p, _state->next, context, progress);
	}
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

object_ptr<UniqueGiftCoverWidget> MakeUniqueGiftCover(
		QWidget *parent,
		rpl::producer<UniqueGiftCover> data,
		UniqueGiftCoverArgs &&args) {
	return object_ptr<UniqueGiftCoverWidget>(
		parent,
		std::move(data),
		std::move(args));
}

void AddUniqueGiftCover(
		not_null<VerticalLayout*> container,
		rpl::producer<UniqueGiftCover> data,
		UniqueGiftCoverArgs &&args) {
	container->add(
		MakeUniqueGiftCover(container, std::move(data), std::move(args)));
}

} // namespace Ui
