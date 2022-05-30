/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/premium_preview_box.h"

#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_message_reactions.h"
#include "data/data_document_media.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/layers/generic_box.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/text/text.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/padding_wrap.h"
#include "settings/settings_premium.h"
#include "lottie/lottie_single_player.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/history_view_element.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_chat_helpers.h"

namespace {

constexpr auto kPremiumShift = 0.082;
constexpr auto kShiftDuration = crl::time(200);
constexpr auto kReactionsPerRow = 5;
constexpr auto kDisabledOpacity = 0.5;

struct Descriptor {
	PremiumPreview section = PremiumPreview::Stickers;
	DocumentData *requestedSticker = nullptr;
	base::flat_map<QString, ReactionDisableType> disabled;
};

bool operator==(const Descriptor &a, const Descriptor &b) {
	return (a.section == b.section)
		&& (a.requestedSticker == b.requestedSticker)
		&& (a.disabled == b.disabled);
}

bool operator!=(const Descriptor &a, const Descriptor &b) {
	return !(a == b);
}

[[nodiscard]] int ComputeX(int column, int columns) {
	const auto skip = st::premiumReactionWidthSkip;
	const auto fullWidth = columns * skip;
	const auto left = (st::boxWideWidth - fullWidth) / 2;
	return left + column * skip + (skip / 2);
}

[[nodiscard]] int ComputeY(int row, int rows) {
	const auto middle = (rows > 3)
		? (st::premiumReactionInfoTop / 2)
		: st::premiumReactionsMiddle;
	const auto skip = st::premiumReactionHeightSkip;
	const auto fullHeight = rows * skip;
	const auto top = middle - (fullHeight / 2);
	return top + row * skip + (skip / 2);
}

struct Preload {
	Descriptor descriptor;
	std::shared_ptr<Data::DocumentMedia> media;
	base::weak_ptr<Window::SessionController> controller;
};

[[nodiscard]] std::vector<Preload> &Preloads() {
	static auto result = std::vector<Preload>();
	return result;
}

void PreloadSticker(const std::shared_ptr<Data::DocumentMedia> &media) {
	const auto origin = media->owner()->stickerSetOrigin();
	media->automaticLoad(origin, nullptr);
	media->videoThumbnailWanted(origin);
}

[[nodiscard]] object_ptr<Ui::RpWidget> ChatBackPreview(
		QWidget *parent,
		int height,
		const QImage &back) {
	auto result = object_ptr<Ui::FixedHeightWidget>(parent, height);
	const auto raw = result.data();

	raw->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(raw);
		p.drawImage(0, 0, back);
	}, raw->lifetime());

	return result;
}

[[nodiscard]] not_null<Ui::RpWidget*> StickerPreview(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		const std::shared_ptr<Data::DocumentMedia> &media) {
	using namespace HistoryView;
	const auto document = media->owner();
	const auto lottieSize = Sticker::Size(document);
	const auto effectSize = Sticker::PremiumEffectSize(document);
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	parent->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		result->setGeometry(QRect(
			QPoint(
				(size.width() - effectSize.width()) / 2,
				(size.height() - effectSize.height()) / 2),
			effectSize));
	}, result->lifetime());
	auto &lifetime = result->lifetime();

	struct State {
		std::unique_ptr<Lottie::SinglePlayer> lottie;
		std::unique_ptr<Lottie::SinglePlayer> effect;
		std::unique_ptr<Ui::PathShiftGradient> pathGradient;
	};
	const auto state = lifetime.make_state<State>();
	const auto createLottieIfReady = [=] {
		if (state->lottie) {
			return;
		}
		const auto document = media->owner();
		const auto sticker = document->sticker();
		if (!sticker || !sticker->isLottie() || !media->loaded()) {
			return;
		} else if (media->videoThumbnailContent().isEmpty()) {
			return;
		}

		const auto factor = style::DevicePixelRatio();
		state->lottie = ChatHelpers::LottiePlayerFromDocument(
			media.get(),
			nullptr,
			ChatHelpers::StickerLottieSize::MessageHistory,
			lottieSize * factor,
			Lottie::Quality::High);
		state->effect = document->session().emojiStickersPack().effectPlayer(
			document,
			media->videoThumbnailContent(),
			QString(),
			true);

		const auto update = [=] { result->update(); };
		auto &lifetime = result->lifetime();
		state->lottie->updates() | rpl::start_with_next(update, lifetime);
		state->effect->updates() | rpl::start_with_next(update, lifetime);
	};
	state->pathGradient = MakePathShiftGradient(
		controller->chatStyle(),
		[=] { result->update(); });

	result->paintRequest(
	) | rpl::start_with_next([=] {
		createLottieIfReady();

		auto p = QPainter(result);

		const auto left = effectSize.width()
			- int(lottieSize.width() * (1. + kPremiumShift));
		const auto top = (effectSize.height() - lottieSize.height()) / 2;
		const auto r = QRect(QPoint(left, top), lottieSize);
		if (!state->lottie
			|| !state->lottie->ready()
			|| !state->effect->ready()) {
			p.setBrush(controller->chatStyle()->msgServiceBg());
			ChatHelpers::PaintStickerThumbnailPath(
				p,
				media.get(),
				r,
				state->pathGradient.get());
			return;
		}

		const auto factor = style::DevicePixelRatio();
		const auto frame = state->lottie->frameInfo({ lottieSize * factor });
		const auto effect = state->effect->frameInfo(
			{ effectSize * factor });
		//const auto framesCount = !frame.image.isNull()
		//	? state->lottie->framesCount()
		//	: 1;
		//const auto effectsCount = !effect.image.isNull()
		//	? state->effect->framesCount()
		//	: 1;

		p.drawImage(r, frame.image);
		p.drawImage(result->rect(), effect.image);

		if (!frame.image.isNull()/*
			&& ((frame.index % effectsCount) <= effect.index)*/) {
			state->lottie->markFrameShown();
		}
		if (!effect.image.isNull()/*
			&& ((effect.index % framesCount) <= frame.index)*/) {
			state->effect->markFrameShown();
		}
	}, lifetime);

	return result;
}

class ReactionPreview final {
public:
	ReactionPreview(
		not_null<Window::SessionController*> controller,
		const Data::Reaction &reaction,
		ReactionDisableType type,
		Fn<void()> update,
		QPoint position);

	[[nodiscard]] bool playsEffect() const;
	void paint(Painter &p);
	void paintEffect(QPainter &p);

	void setOver(bool over);
	void startAnimations();
	void cancelAnimations();
	[[nodiscard]] bool disabled() const;
	[[nodiscard]] QRect geometry() const;

private:
	void checkReady();

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _update;
	const QPoint _position;
	Ui::Animations::Simple _scale;
	std::shared_ptr<Data::DocumentMedia> _centerMedia;
	std::shared_ptr<Data::DocumentMedia> _aroundMedia;
	std::unique_ptr<Lottie::SinglePlayer> _center;
	std::unique_ptr<Lottie::SinglePlayer> _around;
	std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	QImage _cache1;
	QImage _cache2;
	bool _over = false;
	bool _disabled = false;
	bool _playRequested = false;
	bool _aroundPlaying = false;
	bool _centerPlaying = false;
	rpl::lifetime _lifetime;

};

[[nodiscard]] QString DisabledText(ReactionDisableType type) {
	switch (type) {
	case ReactionDisableType::Group:
		return tr::lng_premium_reaction_no_group(tr::now);
	case ReactionDisableType::Channel:
		return tr::lng_premium_reaction_no_channel(tr::now);
	}
	return QString();
}

ReactionPreview::ReactionPreview(
	not_null<Window::SessionController*> controller,
	const Data::Reaction &reaction,
	ReactionDisableType type,
	Fn<void()> update,
	QPoint position)
: _controller(controller)
, _update(std::move(update))
, _position(position)
, _centerMedia(reaction.centerIcon->createMediaView())
, _aroundMedia(reaction.aroundAnimation->createMediaView())
, _pathGradient(
	HistoryView::MakePathShiftGradient(
		controller->chatStyle(),
		_update))
, _disabled(type != ReactionDisableType::None) {
	_centerMedia->checkStickerLarge();
	_aroundMedia->checkStickerLarge();
	checkReady();
}

QRect ReactionPreview::geometry() const {
	const auto xsize = st::premiumReactionWidthSkip;
	const auto ysize = st::premiumReactionHeightSkip;
	return { _position - QPoint(xsize / 2, ysize / 2), QSize(xsize, ysize) };
}

void ReactionPreview::checkReady() {
	const auto make = [&](
			const std::shared_ptr<Data::DocumentMedia> &media,
			int size) {
		const auto bytes = media->bytes();
		const auto filepath = media->owner()->filepath();
		auto result = ChatHelpers::LottiePlayerFromDocument(
			media.get(),
			nullptr,
			ChatHelpers::StickerLottieSize::PremiumReactionPreview,
			QSize(size, size) * style::DevicePixelRatio(),
			Lottie::Quality::Default);
		result->updates() | rpl::start_with_next(_update, _lifetime);
		return result;
	};
	if (!_center && _centerMedia->loaded()) {
		_center = make(_centerMedia, st::premiumReactionSize);
	}
	if (!_around && _aroundMedia->loaded()) {
		_around = make(_aroundMedia, st::premiumReactionAround);
	}
}

void ReactionPreview::setOver(bool over) {
	if (_over == over || _disabled) {
		return;
	}
	_over = over;
	const auto from = st::premiumReactionScale;
	_scale.start(
		_update,
		over ? from : 1.,
		over ? 1. : from,
		st::slideWrapDuration);
}

void ReactionPreview::startAnimations() {
	if (_disabled) {
		return;
	}
	_playRequested = true;
	if (!_center || !_center->ready() || !_around || !_around->ready()) {
		return;
	}
	_update();
}

void ReactionPreview::cancelAnimations() {
	_playRequested = false;
}

bool ReactionPreview::disabled() const {
	return _disabled;
}

void ReactionPreview::paint(Painter &p) {
	const auto size = st::premiumReactionAround;
	const auto center = st::premiumReactionSize;
	const auto scale = _scale.value(_over ? 1. : st::premiumReactionScale);
	const auto inner = QRect(
		-center / 2,
		-center / 2,
		center,
		center
	).translated(_position);
	auto hq = PainterHighQualityEnabler(p);
	const auto centerReady = _center && _center->ready();
	const auto staticCenter = centerReady && !_centerPlaying;
	const auto use1 = staticCenter && scale == 1.;
	const auto use2 = staticCenter && scale == st::premiumReactionScale;
	const auto useScale = (!use1 && !use2 && scale != 1.);
	if (useScale) {
		p.save();
		p.translate(inner.center());
		p.scale(scale, scale);
		p.translate(-inner.center());
	}
	if (_disabled) {
		p.setOpacity(kDisabledOpacity);
	}
	checkReady();
	if (centerReady) {
		if (use1 || use2) {
			auto &cache = use1 ? _cache1 : _cache2;
			const auto use = int(std::round(center * scale));
			const auto rect = QRect(-use / 2, -use / 2, use, use).translated(
				_position);
			if (cache.isNull()) {
				cache = _center->frame().scaledToWidth(
					use * style::DevicePixelRatio(),
					Qt::SmoothTransformation);
			}
			p.drawImage(rect, cache);
		} else {
			p.drawImage(inner, _center->frame());
		}
		if (_centerPlaying) {
			const auto almost = (_center->frameIndex() + 1)
				== _center->framesCount();
			const auto marked = _center->markFrameShown();
			if (almost && marked) {
				_centerPlaying = false;
			}
		}
		if (_around
			&& _around->ready()
			&& !_aroundPlaying
			&& !_centerPlaying
			&& _playRequested) {
			_aroundPlaying = _centerPlaying = true;
			_playRequested = false;
		}
	} else {
		p.setBrush(_controller->chatStyle()->msgServiceBg());
		ChatHelpers::PaintStickerThumbnailPath(
			p,
			_centerMedia.get(),
			inner,
			_pathGradient.get());
	}
	if (useScale) {
		p.restore();
	} else if (_disabled) {
		p.setOpacity(1.);
	}
}

bool ReactionPreview::playsEffect() const {
	return _aroundPlaying;
}

void ReactionPreview::paintEffect(QPainter &p) {
	if (!_aroundPlaying) {
		return;
	}
	const auto size = st::premiumReactionAround;
	const auto outer = QRect(-size/2, -size/2, size, size).translated(
		_position);
	const auto scale = _scale.value(_over ? 1. : st::premiumReactionScale);
	auto hq = PainterHighQualityEnabler(p);
	if (scale != 1.) {
		p.save();
		p.translate(outer.center());
		p.scale(scale, scale);
		p.translate(-outer.center());
	}
	p.drawImage(outer, _around->frame());
	if (scale != 1.) {
		p.restore();
	}
	if (_aroundPlaying) {
		const auto almost = (_around->frameIndex() + 1)
			== _around->framesCount();
		const auto marked = _around->markFrameShown();
		if (almost && marked) {
			_aroundPlaying = false;
		}
	}
}

[[nodiscard]] not_null<Ui::RpWidget*> ReactionsPreview(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		const base::flat_map<QString, ReactionDisableType> &disabled) {
	struct State {
		std::vector<std::unique_ptr<ReactionPreview>> entries;
		Ui::Text::String bottom;
		int selected = -1;
	};
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	auto &lifetime = result->lifetime();
	const auto state = lifetime.make_state<State>();

	result->setMouseTracking(true);

	parent->sizeValue(
	) | rpl::start_with_next([=] {
		result->setGeometry(parent->rect());
	}, result->lifetime());

	using namespace HistoryView;
	const auto list = controller->session().data().reactions().list(
		Data::Reactions::Type::Active);
	const auto count = ranges::count(list, true, &Data::Reaction::premium);
	const auto rows = (count + kReactionsPerRow - 1) / kReactionsPerRow;
	const auto inrowmax = (count + rows - 1) / rows;
	const auto inrowless = (inrowmax * rows - count);
	const auto inrowmore = rows - inrowless;
	const auto inmaxrows = inrowmore * inrowmax;
	auto index = 0;
	auto disableType = ReactionDisableType::None;
	for (const auto &reaction : list) {
		if (!reaction.premium) {
			continue;
		}
		const auto inrow = (index < inmaxrows) ? inrowmax : (inrowmax - 1);
		const auto row = (index < inmaxrows)
			? (index / inrow)
			: (inrowmore + ((index - inmaxrows) / inrow));
		const auto column = (index < inmaxrows)
			? (index % inrow)
			: ((index - inmaxrows) % inrow);
		++index;
		if (!reaction.centerIcon || !reaction.aroundAnimation) {
			continue;
		}
		const auto i = disabled.find(reaction.emoji);
		const auto disable = (i != end(disabled))
			? i->second
			: ReactionDisableType::None;
		if (disable != ReactionDisableType::None) {
			disableType = disable;
		}
		state->entries.push_back(std::make_unique<ReactionPreview>(
			controller,
			reaction,
			disable,
			[=] { result->update(); },
			QPoint(ComputeX(column, inrow), ComputeY(row, rows))));
	}

	const auto bottom1 = tr::lng_reaction_premium_info(tr::now);
	const auto bottom2 = (disableType == ReactionDisableType::None)
		? QString()
		: (disableType == ReactionDisableType::Group)
		? tr::lng_reaction_premium_no_group(tr::now)
		: tr::lng_reaction_premium_no_channel(tr::now);
	state->bottom.setText(
		st::defaultTextStyle,
		(bottom1 + '\n' + bottom2).trimmed());

	result->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(result);
		auto effects = std::vector<Fn<void()>>();
		for (const auto &entry : state->entries) {
			entry->paint(p);
			if (entry->playsEffect()) {
				effects.push_back([&] {
					entry->paintEffect(p);
				});
			}
		}
		const auto padding = st::boxRowPadding;
		const auto available = parent->width()
			- padding.left()
			- padding.right();
		const auto top = st::premiumReactionInfoTop
			+ ((state->bottom.maxWidth() > available)
				? st::normalFont->height
				: 0);
		p.setPen(st::premiumButtonFg);
		state->bottom.draw(
			p,
			padding.left(),
			top,
			available,
			style::al_top);
		for (const auto &paint : effects) {
			paint();
		}
	}, lifetime);

	const auto lookup = [=](QPoint point) {
		auto index = 0;
		for (const auto &entry : state->entries) {
			if (entry->geometry().contains(point) && !entry->disabled()) {
				return index;
			}
			++index;
		}
		return -1;
	};
	result->events(
	) | rpl::start_with_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::MouseButtonPress) {
			const auto point = static_cast<QMouseEvent*>(event.get())->pos();
			if (state->selected >= 0) {
				state->entries[state->selected]->cancelAnimations();
			}
			if (const auto index = lookup(point); index >= 0) {
				state->entries[index]->startAnimations();
			}
		} else if (event->type() == QEvent::MouseMove) {
			const auto point = static_cast<QMouseEvent*>(event.get())->pos();
			const auto index = lookup(point);
			const auto wasInside = (state->selected >= 0);
			const auto nowInside = (index >= 0);
			if (state->selected != index) {
				if (wasInside) {
					state->entries[state->selected]->setOver(false);
				}
				if (nowInside) {
					state->entries[index]->setOver(true);
				}
				state->selected = index;
			}
			if (wasInside != nowInside) {
				result->setCursor(nowInside
					? style::cur_pointer
					: style::cur_default);
			}
		}
	}, lifetime);

	return result;
}

[[nodiscard]] object_ptr<Ui::AbstractButton> CreateGradientButton(
		QWidget *parent,
		QGradientStops stops) {
	return object_ptr<Ui::GradientButton>(parent, std::move(stops));
}

[[nodiscard]] object_ptr<Ui::AbstractButton> CreatePremiumButton(
		QWidget *parent) {
	return CreateGradientButton(parent, Ui::Premium::ButtonGradientStops());
}

[[nodiscard]] object_ptr<Ui::AbstractButton> CreateUnlockButton(
		QWidget *parent,
		int width) {
	auto result = CreatePremiumButton(parent);
	const auto &st = st::premiumPreviewBox.button;
	result->resize(width, st.height);

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		result.data(),
		tr::lng_premium_more_about(),
		st::premiumPreviewButtonLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		result->widthValue(),
		label->widthValue()
	) | rpl::start_with_next([=](int outer, int width) {
		label->moveToLeft(
			(outer - width) / 2,
			st::premiumPreviewBox.button.textTop,
			outer);
	}, label->lifetime());

	return result;
}

void StickerBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const Descriptor &descriptor,
		const std::shared_ptr<Data::DocumentMedia> &media,
		const QImage &back) {
	const auto size = QSize(st::boxWideWidth, st::premiumPreviewHeight);
	box->setWidth(size.width());
	box->setNoContentMargin(true);

	const auto outer = box->addRow(
		ChatBackPreview(box, size.height(), back),
		{});
	struct State {
		Ui::RpWidget *content = nullptr;
	};
	const auto state = outer->lifetime().make_state<State>();

	auto text = rpl::producer<QString>();
	auto title = rpl::producer<QString>();
	switch (descriptor.section) {
	case PremiumPreview::Stickers:
		Assert(media != nullptr);
		state->content = StickerPreview(outer, controller, media);
		text = tr::lng_premium_summary_about_premium_stickers();
		title = tr::lng_premium_summary_subtitle_premium_stickers();
		break;
	case PremiumPreview::Reactions:
		state->content = ReactionsPreview(
			outer,
			controller,
			descriptor.disabled);
		text = tr::lng_premium_summary_about_unique_reactions();
		title = tr::lng_premium_summary_subtitle_unique_reactions();
		break;
	case PremiumPreview::Avatars:
		break;
	}

	const auto padding = st::premiumPreviewAboutPadding;
	const auto available = size.width() - padding.left() - padding.right();
	auto titleLabel = object_ptr<Ui::FlatLabel>(
		box,
		std::move(title),
		st::premiumPreviewAboutTitle);
	titleLabel->resizeToWidth(available);
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			box,
			std::move(titleLabel)),
		st::premiumPreviewAboutTitlePadding);
	auto textLabel = object_ptr<Ui::FlatLabel>(
		box,
		std::move(text),
		st::premiumPreviewAbout);
	textLabel->resizeToWidth(available);
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(box, std::move(textLabel)),
		padding);
	box->setStyle(st::premiumPreviewBox);
	const auto buttonPadding = st::premiumPreviewBox.buttonPadding;
	const auto width = size.width()
		- buttonPadding.left()
		- buttonPadding.right();
	auto button = CreateUnlockButton(box, width);
	button->setClickedCallback([=] {
		Settings::ShowPremium(controller, "premium_stickers");
	});
	box->addButton(std::move(button));
}

void Show(
		not_null<Window::SessionController*> controller,
		const Descriptor &descriptor,
		const std::shared_ptr<Data::DocumentMedia> &media,
		QImage back) {
	controller->show(Box(StickerBox, controller, descriptor, media, back));
}

void Show(not_null<Window::SessionController*> controller, QImage back) {
	auto &list = Preloads();
	for (auto i = begin(list); i != end(list);) {
		const auto already = i->controller.get();
		if (!already) {
			i = list.erase(i);
		} else if (already == controller) {
			Show(controller, i->descriptor, i->media, back);
			i = list.erase(i);
			return;
		} else {
			++i;
		}
	}
}

[[nodiscard]] QImage SolidColorImage(QSize size, QColor color) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(color);
	return result;
}

void Show(
		not_null<Window::SessionController*> controller,
		Descriptor &&descriptor) {
	auto &list = Preloads();
	for (auto i = begin(list); i != end(list);) {
		const auto already = i->controller.get();
		if (!already) {
			i = list.erase(i);
		} else if (already == controller) {
			if (i->descriptor == descriptor) {
				return;
			}
			i->descriptor = descriptor;
			i->media = descriptor.requestedSticker
				? descriptor.requestedSticker->createMediaView()
				: nullptr;
			if (const auto &media = i->media) {
				PreloadSticker(media);
			}
			return;
		} else {
			++i;
		}
	}

	const auto weak = base::make_weak(controller.get());
	list.push_back({
		.descriptor = descriptor,
		.media = (descriptor.requestedSticker
			? descriptor.requestedSticker->createMediaView()
			: nullptr),
		.controller = weak,
	});
	if (const auto &media = list.back().media) {
		PreloadSticker(media);
	}

	const auto fill = QSize(st::boxWideWidth, st::boxWideWidth);
	const auto theme = controller->currentChatTheme();
	const auto color = theme->background().colorForFill;
	const auto area = QSize(fill.width(), fill.height() * 2);
	const auto request = theme->cacheBackgroundRequest(area);
	crl::async([=] {
		using Option = Images::Option;
		auto back = color
			? SolidColorImage(area, *color)
			: request.background.waitingForNegativePattern()
			? SolidColorImage(area, Qt::black)
			: Ui::CacheBackground(request).image;
		const auto factor = style::DevicePixelRatio();
		auto cropped = back.copy(QRect(
			QPoint(0, fill.height() * factor / 2),
			fill * factor));
		cropped.setDevicePixelRatio(factor);
		const auto options = Images::Options()
			| Option::RoundSkipBottomLeft
			| Option::RoundSkipBottomRight
			| Option::RoundLarge;
		const auto result = Images::Round(
			std::move(cropped),
			Images::CornersMask(st::boxRadius),
			RectPart::TopLeft | RectPart::TopRight);
		crl::on_main([=] {
			if (const auto strong = weak.get()) {
				Show(strong, result);
			}
		});
	});
}

} // namespace

void ShowStickerPreviewBox(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document) {
	Show(controller, Descriptor{
		.section = PremiumPreview::Stickers,
		.requestedSticker = document,
	});
}

void ShowPremiumPreviewBox(
		not_null<Window::SessionController*> controller,
		PremiumPreview section,
		const base::flat_map<QString, ReactionDisableType> &disabled) {
	Show(controller, Descriptor{
		.section = section,
		.disabled = disabled,
	});
}
