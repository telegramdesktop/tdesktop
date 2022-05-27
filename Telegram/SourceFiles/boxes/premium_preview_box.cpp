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
constexpr auto kEnumerateCount = 3;

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
		Fn<void()> update);

	[[nodiscard]] bool playsEffect() const;
	void paint(Painter &p, int x, int y, float64 scale);
	void paintEffect(QPainter &p, int x, int y, float64 scale);
	void paintRestricted(Painter &p, int x, int bottom, float64 scale);

	void startAnimations();
	void cancelAnimations();

private:
	void checkReady();
	void paintTitle(Painter &p, int x, int y, float64 scale);

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _update;
	std::shared_ptr<Data::DocumentMedia> _centerMedia;
	std::shared_ptr<Data::DocumentMedia> _aroundMedia;
	std::unique_ptr<Lottie::SinglePlayer> _center;
	std::unique_ptr<Lottie::SinglePlayer> _around;
	std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	Ui::Text::String _name;
	Ui::Text::String _disabled;
	QImage _cache1;
	QImage _cache2;
	QImage _cache3;
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
	Fn<void()> update)
: _controller(controller)
, _update(std::move(update))
, _centerMedia(reaction.centerIcon->createMediaView())
, _aroundMedia(reaction.aroundAnimation->createMediaView())
, _pathGradient(
	HistoryView::MakePathShiftGradient(
		controller->chatStyle(),
		_update))
, _name(st::premiumReactionName, reaction.title)
, _disabled(st::defaultTextStyle, DisabledText(type)) {
	_centerMedia->checkStickerLarge();
	_aroundMedia->checkStickerLarge();
	checkReady();
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

void ReactionPreview::startAnimations() {
	_playRequested = true;
	if (!_center || !_center->ready() || !_around || !_around->ready()) {
		return;
	}
	_update();
}

void ReactionPreview::cancelAnimations() {
	_playRequested = false;
}

void ReactionPreview::paint(Painter &p, int x, int y, float64 scale) {
	const auto size = st::premiumReactionAround;
	const auto center = st::premiumReactionSize;
	const auto inner = QRect(
		x + (size - center) / 2,
		y + (size - center) / 2,
		center,
		center);
	auto hq = PainterHighQualityEnabler(p);
	const auto centerReady = _center && _center->ready();
	const auto staticCenter = centerReady && !_centerPlaying;
	const auto use1 = staticCenter && scale == st::premiumReactionScale1;
	const auto use2 = staticCenter && scale == st::premiumReactionScale2;
	const auto use3 = staticCenter && scale == st::premiumReactionScale3;
	const auto useScale = (!use1 && !use2 && !use3 && scale != 1.);
	if (useScale) {
		p.save();
		p.translate(inner.center());
		p.scale(scale, scale);
		p.translate(-inner.center());
	}
	checkReady();
	if (centerReady) {
		if (use1 || use2 || use3) {
			auto &cache = use1 ? _cache1 : use2 ? _cache2 : _cache3;
			const auto use = int(std::round(center * scale));
			const auto rect = QRect(
				x + (size - use) / 2,
				y + (size - use) / 2,
				use,
				use);
			if (cache.isNull()) {
				cache = _center->frame().scaledToWidth(
					use * style::DevicePixelRatio(),
					Qt::SmoothTransformation);
			}
			p.drawImage(rect, cache);
		} else {
			p.drawImage(inner, _center->frame());
		}
		if (_aroundPlaying) {
			const auto almost = (_around->frameIndex() + 1)
				== _around->framesCount();
			const auto marked = _around->markFrameShown();
			if (almost && marked) {
				_aroundPlaying = false;
			}
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
	}
	paintTitle(p, x, y, scale);
}

void ReactionPreview::paintTitle(Painter &p, int x, int y, float64 scale) {
	const auto first = st::premiumReactionScale1;
	if (scale <= first) {
		return;
	}
	const auto opacity = (scale - first) / (1. - first);
	p.setOpacity(opacity * 0.2);
	auto hq = PainterHighQualityEnabler(p);
	const auto width = _name.maxWidth();
	const auto sticker = st::premiumReactionAround;
	const auto inner = QRect(
		x + (sticker - width) / 2,
		y + (sticker / 2) + st::premiumReactionNameTop,
		width,
		st::premiumReactionName.font->height);
	const auto outer = inner.marginsAdded(st::premiumReactionNamePadding);
	const auto radius = outer.height() / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(st::premiumButtonFg);
	p.drawRoundedRect(outer, radius, radius);
	p.setOpacity(opacity);
	p.setPen(st::premiumButtonFg);
	_name.draw(p, inner.x(), inner.y(), width);
	if (!_disabled.isEmpty()) {
		const auto left = x + (sticker / 2) - (_disabled.maxWidth() / 2);
	}
	p.setOpacity(1.);
}

void ReactionPreview::paintRestricted(
		Painter &p,
		int x,
		int bottom,
		float64 scale) {
	const auto first = st::premiumReactionScale1;
	if (scale <= first || _disabled.isEmpty()) {
		return;
	}
	const auto sticker = st::premiumReactionAround;
	const auto opacity = (scale - first) / (1. - first);
	p.setOpacity(opacity);
	p.setPen(st::premiumButtonFg);
	const auto left = x + (sticker / 2) - (_disabled.maxWidth() / 2);
	_disabled.draw(p, left, bottom - 2.5 * st::normalFont->height, _disabled.maxWidth());
	p.setOpacity(1.);
}

bool ReactionPreview::playsEffect() const {
	return _aroundPlaying;
}

void ReactionPreview::paintEffect(QPainter &p, int x, int y, float64 scale) {
	if (!_aroundPlaying) {
		return;
	}
	const auto size = st::premiumReactionAround;
	const auto outer = QRect(x, y, size, size);
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
	if (_aroundPlaying
		&& (_around->frameIndex() + 1 == _around->framesCount())
		&& _around->markFrameShown()) {
		_aroundPlaying = false;
	}
}

[[nodiscard]] not_null<Ui::RpWidget*> ReactionsPreview(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		const base::flat_map<QString, ReactionDisableType> &disabled) {
	struct State {
		std::vector<std::unique_ptr<ReactionPreview>> entries;
		Ui::Animations::Simple shifting;
		int shift = 2;
		bool played = false;
		bool inside = false;
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
	for (const auto &reaction : list) {
		if (!reaction.premium
			|| !reaction.centerIcon
			|| !reaction.aroundAnimation) {
			continue;
		}
		const auto i = disabled.find(reaction.emoji);
		state->entries.push_back(std::make_unique<ReactionPreview>(
			controller,
			reaction,
			(i != end(disabled)) ? i->second : ReactionDisableType::None,
			[=] { result->update(); }));
	}
	const auto enumerate = [=](
			Fn<void(not_null<ReactionPreview*>,int,float64,int)> callback) {
		const auto count = int(state->entries.size());
		if (!count) {
			return;
		}
		const auto computeLeft = [](int index) {
			const auto skips = std::array{
				st::premiumReactionSkip1,
				st::premiumReactionSkip2,
				st::premiumReactionSkip3,
			};
			const auto id = std::abs(index);
			const auto delta = !id
				? 0
				: (id <= skips.size())
				? std::accumulate(begin(skips), begin(skips) + id, 0)
				: (ranges::accumulate(skips, 0)
					+ skips.back() * int(id - skips.size()));
			return (st::boxWideWidth / 2)
				+ (index < 0 ? -delta : delta)
				- st::premiumReactionAround / 2;
		};
		const auto computeScale = [](int index) {
			const auto id = std::abs(index);
			const auto scales = std::array{
				st::premiumReactionScale1,
				st::premiumReactionScale2,
				st::premiumReactionScale3,
			};
			return !id
				? 1.
				: scales[std::min(id, int(scales.size())) - 1];
		};
		const auto shift = state->shifting.value(state->shift);
		const auto delta = !state->shifting.animating()
			? state->shift
			: (shift < 0)
			? -(int(std::floor(-shift)) + 1)
			: int(std::floor(shift));
		const auto progress = shift - delta;
		const auto start = delta - kEnumerateCount;
		const auto from = ((start % count) + count) % count;
		const auto till = from + kEnumerateCount * 2 + 1;
		const auto outerSize = st::premiumReactionAround;
		for (auto i = from; i != till; ++i) {
			const auto index = (i - from) - kEnumerateCount;
			auto left = computeLeft(index);
			auto scale = computeScale(index);
			if (progress > 0.) {
				left = anim::interpolate(
					left,
					computeLeft(index - 1),
					progress);
				scale = scale + (computeScale(index - 1) - scale) * progress;
			}
			const auto entry = state->entries[i % count].get();
			const auto scaledSize = scale * st::premiumReactionSize;
			const auto paintedLeft = left + (outerSize - scaledSize) / 2;
			const auto paintedRight = left + (outerSize + scaledSize) / 2;
			if (entry->playsEffect()
				|| (paintedRight > 0 && paintedLeft < st::boxWideWidth)) {
				callback(entry, left, scale, delta + index);
			}
		}
	};

	result->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = Painter(result);
		const auto bottom = result->height();
		const auto top = (bottom / 2) - st::premiumReactionTop;
		auto effects = std::vector<Fn<void()>>();
		if (!state->played && !state->shifting.animating()) {
			state->played = true;
			if (const auto count = state->entries.size()) {
				const auto index = ((state->shift % count) + count) % count;
				state->entries[index]->startAnimations();
			}
		}
		enumerate([&](
				not_null<ReactionPreview*> entry,
				int left,
				float64 scale,
				int index) {
			entry->paint(p, left, top, scale);
			entry->paintRestricted(p, left, bottom, scale);
			if (entry->playsEffect()) {
				effects.push_back([=, &p] {
					entry->paintEffect(p, left, top, scale);
				});
			}
		});
		for (const auto &paint : effects) {
			paint();
		}
	}, lifetime);

	const auto lookup = [=](QPoint point) -> std::optional<int> {
		auto found = std::optional<int>();
		const auto top = result->height() / 2 - st::premiumReactionTop;
		enumerate([&](auto, int left, float64 scale, int index) {
			const auto size = int(st::premiumReactionSize * scale) / 2;
			const auto outer = st::premiumReactionAround / 2;
			const auto rect = QRect(
				left + outer - (size / 2),
				top + outer - (size / 2),
				size,
				size);
			if (rect.contains(point)) {
				found = index;
			}
		});
		return found;
	};
	result->events(
	) | rpl::start_with_next([=](not_null<QEvent*> event) {
		if (event->type() == QEvent::MouseButtonPress) {
			const auto point = static_cast<QMouseEvent*>(event.get())->pos();
			if (const auto index = lookup(point)) {
				state->shifting.start(
					[=] { result->update(); },
					state->shift,
					*index,
					kShiftDuration,
					anim::sineInOut);
				state->shift = *index;
				state->played = false;
			}

		} else if (event->type() == QEvent::MouseMove) {
			const auto point = static_cast<QMouseEvent*>(event.get())->pos();
			const auto inside = lookup(point).has_value();
			if (state->inside != inside) {
				state->inside = inside;
				result->setCursor(inside
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
		tr::lng_sticker_premium_button(),
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
	const auto size = QSize(
		st::boxWideWidth,
		HistoryView::Sticker::UsualPremiumEffectSize().height());
	box->setWidth(size.width());
	box->setNoContentMargin(true);

	const auto outer = box->addRow(
		ChatBackPreview(box, size.height(), back),
		{});
	struct State {
		Ui::RpWidget *content = nullptr;
	};
	const auto state = outer->lifetime().make_state<State>();

	switch (descriptor.section) {
	case PremiumPreview::Stickers:
		Assert(media != nullptr);
		state->content = StickerPreview(outer, controller, media);
		break;
	case PremiumPreview::Reactions:
		state->content = ReactionsPreview(
			outer,
			controller,
			descriptor.disabled);
		break;
	case PremiumPreview::Avatars:
		break;
	}

	const auto padding = st::premiumPreviewAboutPadding;
	auto label = object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_sticker_premium_about(),
		st::premiumPreviewAbout);
	label->resizeToWidth(size.width() - padding.left() - padding.right());
	box->addRow(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			box,
			std::move(label)),
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
