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
#include "data/data_streaming.h"
#include "data/data_peer_values.h"
#include "data/data_premium_limits.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_domain.h" // kMaxAccounts
#include "ui/chat/chat_theme.h"
#include "ui/chat/chat_style.h"
#include "ui/layers/generic_box.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/effects/gradient.h"
#include "ui/text/text.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/gradient_round_button.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/boxes/confirm_box.h"
#include "settings/settings_premium.h"
#include "lottie/lottie_single_player.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/history_view_element.h"
#include "media/streaming/media_streaming_instance.h"
#include "media/streaming/media_streaming_player.h"
#include "window/window_session_controller.h"
#include "api/api_premium.h"
#include "apiwrap.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "styles/style_settings.h"

#include <QSvgRenderer>

namespace {

constexpr auto kPremiumShift = 21. / 240;
constexpr auto kReactionsPerRow = 5;
constexpr auto kDisabledOpacity = 0.5;
constexpr auto kPreviewsCount = int(PremiumPreview::kCount);
constexpr auto kToggleStickerTimeout = 2 * crl::time(1000);
constexpr auto kStarOpacityOff = 0.1;
constexpr auto kStarOpacityOn = 1.;
constexpr auto kStarPeriod = 3 * crl::time(1000);

using Data::ReactionId;

struct Descriptor {
	PremiumPreview section = PremiumPreview::Stickers;
	DocumentData *requestedSticker = nullptr;
	base::flat_map<ReactionId, ReactionDisableType> disabled;
	bool fromSettings = false;
	Fn<void()> hiddenCallback;
	Fn<void(not_null<Ui::BoxContent*>)> shownCallback;
};

bool operator==(const Descriptor &a, const Descriptor &b) {
	return (a.section == b.section)
		&& (a.requestedSticker == b.requestedSticker)
		&& (a.disabled == b.disabled)
		&& (a.fromSettings == b.fromSettings);
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

[[nodiscard]] rpl::producer<QString> SectionTitle(PremiumPreview section) {
	switch (section) {
	case PremiumPreview::MoreUpload:
		return tr::lng_premium_summary_subtitle_more_upload();
	case PremiumPreview::FasterDownload:
		return tr::lng_premium_summary_subtitle_faster_download();
	case PremiumPreview::VoiceToText:
		return tr::lng_premium_summary_subtitle_voice_to_text();
	case PremiumPreview::NoAds:
		return tr::lng_premium_summary_subtitle_no_ads();
	case PremiumPreview::Reactions:
		return tr::lng_premium_summary_subtitle_unique_reactions();
	case PremiumPreview::Stickers:
		return tr::lng_premium_summary_subtitle_premium_stickers();
	case PremiumPreview::AnimatedEmoji:
		return tr::lng_premium_summary_subtitle_animated_emoji();
	case PremiumPreview::AdvancedChatManagement:
		return tr::lng_premium_summary_subtitle_advanced_chat_management();
	case PremiumPreview::ProfileBadge:
		return tr::lng_premium_summary_subtitle_profile_badge();
	case PremiumPreview::AnimatedUserpics:
		return tr::lng_premium_summary_subtitle_animated_userpics();
	}
	Unexpected("PremiumPreview in SectionTitle.");
}

[[nodiscard]] rpl::producer<QString> SectionAbout(PremiumPreview section) {
	switch (section) {
	case PremiumPreview::MoreUpload:
		return tr::lng_premium_summary_about_more_upload();
	case PremiumPreview::FasterDownload:
		return tr::lng_premium_summary_about_faster_download();
	case PremiumPreview::VoiceToText:
		return tr::lng_premium_summary_about_voice_to_text();
	case PremiumPreview::NoAds:
		return tr::lng_premium_summary_about_no_ads();
	case PremiumPreview::Reactions:
		return tr::lng_premium_summary_about_unique_reactions();
	case PremiumPreview::Stickers:
		return tr::lng_premium_summary_about_premium_stickers();
	case PremiumPreview::AnimatedEmoji:
		return tr::lng_premium_summary_about_animated_emoji();
	case PremiumPreview::AdvancedChatManagement:
		return tr::lng_premium_summary_about_advanced_chat_management();
	case PremiumPreview::ProfileBadge:
		return tr::lng_premium_summary_about_profile_badge();
	case PremiumPreview::AnimatedUserpics:
		return tr::lng_premium_summary_about_animated_userpics();
	}
	Unexpected("PremiumPreview in SectionTitle.");
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
		const std::shared_ptr<Data::DocumentMedia> &media,
		Fn<void()> readyCallback = nullptr) {
	using namespace HistoryView;

	PreloadSticker(media);

	const auto document = media->owner();
	const auto lottieSize = Sticker::Size(document);
	const auto effectSize = Sticker::PremiumEffectSize(document);
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	result->show();

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
		bool readyInvoked = false;
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

		const auto update = [=] {
			if (!state->readyInvoked
				&& readyCallback
				&& state->lottie->ready()
				&& state->effect->ready()) {
				state->readyInvoked = true;
				readyCallback();
			}
			result->update();
		};
		auto &lifetime = result->lifetime();
		state->lottie->updates() | rpl::start_with_next(update, lifetime);
		state->effect->updates() | rpl::start_with_next(update, lifetime);
	};
	createLottieIfReady();
	if (!state->lottie || !state->effect) {
		controller->session().downloaderTaskFinished(
		) | rpl::take_while([=] {
			createLottieIfReady();
			return !state->lottie || !state->effect;
		}) | rpl::start(result->lifetime());
	}
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
		p.drawImage(
			QRect(QPoint(), effect.image.size() / factor),
			effect.image);

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

[[nodiscard]] not_null<Ui::RpWidget*> StickersPreview(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> readyCallback) {
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	result->show();

	parent->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		result->setGeometry(QRect(QPoint(), size));
	}, result->lifetime());
	auto &lifetime = result->lifetime();

	struct State {
		std::vector<std::shared_ptr<Data::DocumentMedia>> medias;
		Ui::RpWidget *previous = nullptr;
		Ui::RpWidget *current = nullptr;
		Ui::RpWidget *next = nullptr;
		Ui::Animations::Simple slide;
		base::Timer toggleTimer;
		bool toggleTimerPending = false;
		Fn<void()> singleReadyCallback;
		bool readyInvoked = false;
		bool timerFired = false;
		bool nextReady = false;
		int index = 0;
	};
	const auto premium = &controller->session().api().premium();
	const auto state = lifetime.make_state<State>();
	const auto create = [=](std::shared_ptr<Data::DocumentMedia> media) {
		const auto outer = Ui::CreateChild<Ui::RpWidget>(result);
		outer->show();

		result->sizeValue(
		) | rpl::start_with_next([=](QSize size) {
			outer->resize(size);
		}, outer->lifetime());

		[[maybe_unused]] const auto sticker = StickerPreview(
			outer,
			controller,
			media,
			state->singleReadyCallback);

		return outer;
	};
	const auto createNext = [=] {
		state->nextReady = false;
		state->next = create(state->medias[state->index]);
		state->next->move(0, state->current->height());
	};
	const auto check = [=] {
		if (!state->timerFired || !state->nextReady) {
			return;
		}
		const auto animationCallback = [=] {
			const auto top = int(base::SafeRound(state->slide.value(0.)));
			state->previous->move(0, top - state->current->height());
			state->current->move(0, top);
			if (!state->slide.animating()) {
				delete base::take(state->previous);
				state->timerFired = false;
				state->toggleTimer.callOnce(kToggleStickerTimeout);
			}
		};
		state->timerFired = false;
		++state->index;
		state->index %= state->medias.size();
		delete std::exchange(state->previous, state->current);
		state->current = state->next;
		createNext();
		state->slide.stop();
		state->slide.start(
			animationCallback,
			state->current->height(),
			0,
			st::premiumSlideDuration,
			anim::sineInOut);
	};
	state->toggleTimer.setCallback([=] {
		state->timerFired = true;
		check();
	});
	state->singleReadyCallback = [=] {
		if (!state->readyInvoked && readyCallback) {
			state->readyInvoked = true;
			readyCallback();
		}
		if (!state->next) {
			createNext();
			if (result->isHidden()) {
				state->toggleTimerPending = true;
			} else {
				state->toggleTimer.callOnce(kToggleStickerTimeout);
			}
		} else {
			state->nextReady = true;
			check();
		}
	};

	result->shownValue(
	) | rpl::filter([=](bool shown) {
		return shown && state->toggleTimerPending;
	}) | rpl::start_with_next([=] {
		state->toggleTimerPending = false;
		state->toggleTimer.callOnce(kToggleStickerTimeout);
	}, result->lifetime());

	const auto fill = [=] {
		const auto &list = premium->stickers();
		for (const auto &document : list) {
			state->medias.push_back(document->createMediaView());
		}
		if (!state->medias.empty()) {
			state->current = create(state->medias.front());
			state->index = 1 % state->medias.size();
			state->current->move(0, 0);
		}
	};

	fill();
	if (state->medias.empty()) {
		premium->stickersUpdated(
		) | rpl::take(1) | rpl::start_with_next(fill, lifetime);
	}

	return result;
}

struct VideoPreviewDocument {
	DocumentData *document = nullptr;
	RectPart align = RectPart::Bottom;
};

[[nodiscard]] bool VideoAlignToTop(PremiumPreview section) {
	return (section == PremiumPreview::MoreUpload)
		|| (section == PremiumPreview::NoAds)
		|| (section == PremiumPreview::AnimatedEmoji);
}

[[nodiscard]] DocumentData *LookupVideo(
		not_null<Main::Session*> session,
		PremiumPreview section) {
	const auto name = [&] {
		switch (section) {
		case PremiumPreview::MoreUpload: return "more_upload";
		case PremiumPreview::FasterDownload: return "faster_download";
		case PremiumPreview::VoiceToText: return "voice_to_text";
		case PremiumPreview::NoAds: return "no_ads";
		case PremiumPreview::AnimatedEmoji: return "animated_emoji";
		case PremiumPreview::AdvancedChatManagement:
			return "advanced_chat_management";
		case PremiumPreview::ProfileBadge: return "profile_badge";
		case PremiumPreview::AnimatedUserpics: return "animated_userpics";
		}
		return "";
	}();
	const auto &videos = session->api().premium().videos();
	const auto i = videos.find(name);
	return (i != end(videos)) ? i->second.get() : nullptr;
}

[[nodiscard]] QPainterPath GenerateFrame(
		int left,
		int top,
		int width,
		int height,
		bool alignToBottom) {
	const auto radius = style::ConvertScaleExact(20.);
	const auto thickness = style::ConvertScaleExact(6.);
	const auto skip = thickness / 2.;
	auto path = QPainterPath();
	if (alignToBottom) {
		path.moveTo(left - skip, top + height);
		path.lineTo(left - skip, top - skip + radius);
		path.arcTo(
			left - skip,
			top - skip,
			radius * 2,
			radius * 2,
			180,
			-90);
		path.lineTo(left + width + skip - radius, top - skip);
		path.arcTo(
			left + width + skip - 2 * radius,
			top - skip,
			radius * 2,
			radius * 2,
			90,
			-90);
		path.lineTo(left + width + skip, top + height);
	} else {
		path.moveTo(left - skip, top);
		path.lineTo(left - skip, top + height + skip - radius);
		path.arcTo(
			left - skip,
			top + height + skip - 2 * radius,
			radius * 2,
			radius * 2,
			180,
			90);
		path.lineTo(left + width + skip - radius, top + height + skip);
		path.arcTo(
			left + width + skip - 2 * radius,
			top + height + skip - 2 * radius,
			radius * 2,
			radius * 2,
			270,
			90);
		path.lineTo(left + width + skip, top);
	}
	return path;
}

[[nodiscard]] not_null<Ui::RpWidget*> VideoPreview(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document,
		bool alignToBottom,
		Fn<void()> readyCallback) {
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	result->show();

	parent->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		result->setGeometry(parent->rect());
	}, result->lifetime());
	auto &lifetime = result->lifetime();

	auto shared = document->owner().streaming().sharedDocument(
		document,
		Data::FileOriginPremiumPreviews());
	if (!shared) {
		return result;
	}

	struct State {
		State(
			std::shared_ptr<Media::Streaming::Document> shared,
			Fn<void()> waitingCallback)
		: instance(shared, std::move(waitingCallback))
		, star(u":/gui/icons/settings/star.svg"_q) {
		}
		QImage blurred;
		Media::Streaming::Instance instance;
		std::shared_ptr<Data::DocumentMedia> media;
		Ui::Animations::Basic loading;
		QPainterPath frame;
		QSvgRenderer star;
		bool readyInvoked = false;
	};
	const auto state = lifetime.make_state<State>(std::move(shared), [] {});
	state->media = document->createMediaView();
	if (const auto image = state->media->thumbnailInline()) {
		if (image->width() > 0) {
			const auto width = st::premiumVideoWidth;
			const auto height = std::max(
				int(base::SafeRound(
					float64(width) * image->height() / image->width())),
				1);
			using Option = Images::Option;
			const auto corners = alignToBottom
				? (Option::RoundSkipBottomLeft
					| Option::RoundSkipBottomRight)
				: (Option::RoundSkipTopLeft
					| Option::RoundSkipTopRight);
			state->blurred = Images::Prepare(
				image->original(),
				QSize(width, height) * style::DevicePixelRatio(),
				{ .options = (Option::Blur | Option::RoundLarge | corners) });
		}
	}
	const auto width = st::premiumVideoWidth;
	const auto height = state->blurred.height()
		? (state->blurred.height() / state->blurred.devicePixelRatio())
		: width;
	const auto left = (st::boxWideWidth - width) / 2;
	const auto top = alignToBottom ? (st::premiumPreviewHeight - height) : 0;
	state->frame = GenerateFrame(left, top, width, height, alignToBottom);
	const auto check = [=] {
		if (state->instance.playerLocked()) {
			return;
		} else if (state->instance.paused()) {
			state->instance.resume();
		}
		if (!state->instance.active() && !state->instance.failed()) {
			auto options = Media::Streaming::PlaybackOptions();
			options.waitForMarkAsShown = true;
			options.mode = ::Media::Streaming::Mode::Video;
			options.loop = true;
			state->instance.play(options);
		}
	};
	state->instance.player().updates(
	) | rpl::start_with_next_error([=](Media::Streaming::Update &&update) {
		if (v::is<Media::Streaming::Information>(update.data)
			|| v::is<Media::Streaming::UpdateVideo>(update.data)) {
			if (!state->readyInvoked && readyCallback) {
				state->readyInvoked = true;
				readyCallback();
			}
			result->update();
		}
	}, [=](::Media::Streaming::Error &&error) {
		result->update();
	}, state->instance.lifetime());

	state->loading.init([=] {
		if (!anim::Disabled()) {
			result->update();
		}
	});

	result->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(result);
		const auto paintFrame = [&](QColor color, float64 thickness) {
			auto hq = PainterHighQualityEnabler(p);
			auto pen = QPen(color);
			pen.setWidthF(style::ConvertScaleExact(thickness));
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawPath(state->frame);
		};

		check();
		const auto corners = alignToBottom
			? (RectPart::TopLeft | RectPart::TopRight)
			: (RectPart::BottomLeft | RectPart::BottomRight);
		const auto ready = state->instance.player().ready()
			&& !state->instance.player().videoSize().isEmpty();
		const auto size = QSize(width, height) * style::DevicePixelRatio();
		const auto frame = !ready
			? state->blurred
			: state->instance.frame({
				.resize = size,
				.outer = size,
				.radius = ImageRoundRadius::Large,
				.corners = corners,
			});
		paintFrame(QColor(0, 0, 0, 128), 12.);
		p.drawImage(QRect(left, top, width, height), frame);
		paintFrame(Qt::black, 6.6);
		if (ready) {
			state->loading.stop();
			state->instance.markFrameShown();
		} else {
			if (!state->loading.animating()) {
				state->loading.start();
			}
			const auto progress = anim::Disabled()
				? 1.
				: ((crl::now() % kStarPeriod) / float64(kStarPeriod));
			const auto ratio = anim::Disabled()
				? 1.
				: (1. + cos(progress * 2 * M_PI)) / 2.;
			const auto opacity = kStarOpacityOff
				+ (kStarOpacityOn - kStarOpacityOff) * ratio;
			p.setOpacity(opacity);

			const auto starSize = st::premiumVideoStarSize;
			state->star.render(&p, QRectF(
				QPointF(
					left + (width - starSize.width()) / 2.,
					top + (height - starSize.height()) / 2.),
				starSize));
		}
	}, lifetime);

	return result;
}

[[nodiscard]] not_null<Ui::RpWidget*> GenericPreview(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		PremiumPreview section,
		Fn<void()> readyCallback) {
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	result->show();

	parent->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		result->setGeometry(QRect(QPoint(), size));
	}, result->lifetime());
	auto &lifetime = result->lifetime();

	struct State {
		std::vector<std::shared_ptr<Data::DocumentMedia>> medias;
		Ui::RpWidget *single = nullptr;
	};
	const auto session = &controller->session();
	const auto state = lifetime.make_state<State>();
	const auto create = [=] {
		const auto document = LookupVideo(session, section);
		if (!document) {
			return;
		}
		state->single = VideoPreview(
			result,
			controller,
			document,
			!VideoAlignToTop(section),
			readyCallback);
	};
	create();
	if (!state->single) {
		session->api().premium().videosUpdated(
		) | rpl::take(1) | rpl::start_with_next(create, lifetime);
	}

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
	[[nodiscard]] bool ready() const;
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
	if (!_center || !_around) {
		_controller->session().downloaderTaskFinished(
		) | rpl::take_while([=] {
			checkReady();
			return !_center || !_around;
		}) | rpl::start(_lifetime);
	}
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

bool ReactionPreview::ready() const {
	return _center && _center->ready();
}

bool ReactionPreview::disabled() const {
	return _disabled;
}

void ReactionPreview::paint(Painter &p) {
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
		const base::flat_map<ReactionId, ReactionDisableType> &disabled,
		Fn<void()> readyCallback) {
	struct State {
		std::vector<std::unique_ptr<ReactionPreview>> entries;
		Ui::Text::String bottom;
		int selected = -1;
		bool readyInvoked = false;
	};
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	result->show();

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
	const auto inrowmax = rows ? ((count + rows - 1) / rows) : 1;
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
		const auto i = disabled.find(reaction.id);
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
		auto ready = 0;
		for (const auto &entry : state->entries) {
			entry->paint(p);
			if (entry->ready()) {
				++ready;
			}
			if (entry->playsEffect()) {
				effects.push_back([&] {
					entry->paintEffect(p);
				});
			}
		}
		if (!state->readyInvoked
			&& readyCallback
			&& ready > 0
			&& ready == state->entries.size()) {
			state->readyInvoked = true;
			readyCallback();

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
	const auto select = [=](int index) {
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
			select(lookup(point));
		} else if (event->type() == QEvent::Leave) {
			select(-1);
		}
	}, lifetime);

	return result;
}

[[nodiscard]] not_null<Ui::RpWidget*> GenerateDefaultPreview(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		PremiumPreview section,
		Fn<void()> readyCallback) {
	switch (section) {
	case PremiumPreview::Reactions:
		return ReactionsPreview(parent, controller, {}, readyCallback);
	case PremiumPreview::Stickers:
		return StickersPreview(parent, controller, readyCallback);
	default:
		return GenericPreview(parent, controller, section, readyCallback);
	}
}

[[nodiscard]] object_ptr<Ui::GradientButton> CreateGradientButton(
		QWidget *parent,
		QGradientStops stops) {
	return object_ptr<Ui::GradientButton>(parent, std::move(stops));
}

[[nodiscard]] object_ptr<Ui::GradientButton> CreatePremiumButton(
		QWidget *parent) {
	return CreateGradientButton(parent, Ui::Premium::ButtonGradientStops());
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateSwitch(
		not_null<Ui::RpWidget*> parent,
		not_null<rpl::variable<PremiumPreview>*> selected) {
	const auto padding = st::premiumDotPadding;
	const auto width = padding.left() + st::premiumDot + padding.right();
	const auto height = padding.top() + st::premiumDot + padding.bottom();
	const auto stops = Ui::Premium::ButtonGradientStops();
	auto result = object_ptr<Ui::FixedHeightWidget>(parent.get(), height);
	const auto raw = result.data();
	for (auto i = 0; i != kPreviewsCount; ++i) {
		const auto section = PremiumPreview(i);
		const auto button = Ui::CreateChild<Ui::AbstractButton>(raw);
		parent->widthValue(
		) | rpl::start_with_next([=](int outer) {
			const auto full = width * kPreviewsCount;
			const auto left = (outer - full) / 2 + (i * width);
			button->setGeometry(left, 0, width, height);
		}, button->lifetime());
		button->setClickedCallback([=] {
			*selected = section;
		});
		button->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(button);
			auto hq = PainterHighQualityEnabler(p);
			p.setBrush((selected->current() == section)
				? anim::gradient_color_at(
					stops,
					float64(i) / (kPreviewsCount - 1))
				: st::windowBgRipple->c);
			p.setPen(Qt::NoPen);
			p.drawEllipse(
				button->rect().marginsRemoved(st::premiumDotPadding));
		}, button->lifetime());
		selected->changes(
		) | rpl::start_with_next([=] {
			button->update();
		}, button->lifetime());
	}
	return result;
}

void PreviewBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		const Descriptor &descriptor,
		const std::shared_ptr<Data::DocumentMedia> &media,
		const QImage &back) {
	const auto single = st::boxWideWidth;
	const auto size = QSize(single, st::premiumPreviewHeight);
	box->setWidth(size.width());
	box->setNoContentMargin(true);

	const auto outer = box->addRow(
		ChatBackPreview(box, size.height(), back),
		{});

	struct Hiding {
		not_null<Ui::RpWidget*> widget;
		int leftFrom = 0;
		int leftTill = 0;
	};
	struct State {
		int leftFrom = 0;
		Ui::RpWidget *content = nullptr;
		Ui::RpWidget *stickersPreload = nullptr;
		bool stickersPreloadReady = false;
		Ui::RpWidget *reactionsPreload = nullptr;
		bool reactionsPreloadReady = false;
		bool preloadScheduled = false;
		bool showFinished = false;
		Ui::Animations::Simple animation;
		Fn<void()> preload;
		std::vector<Hiding> hiding;
		rpl::variable<PremiumPreview> selected;
	};
	const auto state = outer->lifetime().make_state<State>();
	state->selected = descriptor.section;

	const auto move = [=](int delta) {
		using Type = PremiumPreview;
		const auto count = int(Type::kCount);
		const auto now = state->selected.current();
		state->selected = Type((int(now) + count + delta) % count);
	};

	const auto buttonsParent = box->verticalLayout().get();
	const auto close = Ui::CreateChild<Ui::IconButton>(
		buttonsParent,
		st::settingsPremiumTopBarClose);
	close->setClickedCallback([=] { box->closeBox(); });

	const auto left = Ui::CreateChild<Ui::IconButton>(
		buttonsParent,
		st::settingsPremiumMoveLeft);
	left->setClickedCallback([=] { move(-1); });

	const auto right = Ui::CreateChild<Ui::IconButton>(
		buttonsParent,
		st::settingsPremiumMoveRight);
	right->setClickedCallback([=] { move(1); });

	buttonsParent->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto outerHeight = st::premiumPreviewHeight;
		close->moveToRight(0, 0, width);
		left->moveToLeft(0, (outerHeight - left->height()) / 2, width);
		right->moveToRight(0, (outerHeight - right->height()) / 2, width);
	}, close->lifetime());

	state->preload = [=] {
		if (!state->showFinished) {
			state->preloadScheduled = true;
			return;
		}
		const auto now = state->selected.current();
		if (now != PremiumPreview::Stickers && !state->stickersPreload) {
			const auto ready = [=] {
				if (state->stickersPreload) {
					state->stickersPreloadReady = true;
				} else {
					state->preload();
				}
			};
			state->stickersPreload = GenerateDefaultPreview(
				outer,
				controller,
				PremiumPreview::Stickers,
				ready);
			state->stickersPreload->hide();
		}
		if (now != PremiumPreview::Reactions && !state->reactionsPreload) {
			const auto ready = [=] {
				if (state->reactionsPreload) {
					state->reactionsPreloadReady = true;
				} else {
					state->preload();
				}
			};
			state->reactionsPreload = GenerateDefaultPreview(
				outer,
				controller,
				PremiumPreview::Reactions,
				ready);
			state->reactionsPreload->hide();
		}
	};

	switch (descriptor.section) {
	case PremiumPreview::Stickers:
		state->content = media
			? StickerPreview(outer, controller, media, state->preload)
			: StickersPreview(outer, controller, state->preload);
		break;
	case PremiumPreview::Reactions:
		state->content = ReactionsPreview(
			outer,
			controller,
			descriptor.disabled,
			state->preload);
		break;
	default:
		state->content = GenericPreview(
			outer,
			controller,
			descriptor.section,
			state->preload);
		break;
	}

	state->selected.value(
	) | rpl::combine_previous(
	) | rpl::start_with_next([=](PremiumPreview was, PremiumPreview now) {
		const auto animationCallback = [=] {
			if (!state->animation.animating()) {
				for (const auto &hiding : base::take(state->hiding)) {
					delete hiding.widget;
				}
				state->leftFrom = 0;
				state->content->move(0, 0);
			} else {
				const auto progress = state->animation.value(1.);
				state->content->move(
					anim::interpolate(state->leftFrom, 0, progress),
					0);
				for (const auto &hiding : state->hiding) {
					hiding.widget->move(anim::interpolate(
						hiding.leftFrom,
						hiding.leftTill,
						progress), 0);
				}
			}
		};
		animationCallback();
		const auto toLeft = int(now) > int(was);
		auto start = state->content->x() + (toLeft ? single : -single);
		for (const auto &hiding : state->hiding) {
			const auto left = hiding.widget->x();
			if (toLeft && left + single > start) {
				start = left + single;
			} else if (!toLeft && left - single < start) {
				start = left - single;
			}
		}
		for (auto &hiding : state->hiding) {
			hiding.leftFrom = hiding.widget->x();
			hiding.leftTill = hiding.leftFrom - start;
		}
		state->hiding.push_back({
			.widget = state->content,
			.leftFrom = state->content->x(),
			.leftTill = state->content->x() - start,
		});
		state->leftFrom = start;
		if (now == PremiumPreview::Stickers && state->stickersPreload) {
			state->content = base::take(state->stickersPreload);
			state->content->show();
			if (base::take(state->stickersPreloadReady)) {
				state->preload();
			}
		} else if (now == PremiumPreview::Reactions
			&& state->reactionsPreload) {
			state->content = base::take(state->reactionsPreload);
			state->content->show();
			if (base::take(state->reactionsPreloadReady)) {
				state->preload();
			}
		} else {
			state->content = GenerateDefaultPreview(
				outer,
				controller,
				now,
				state->preload);
		}
		state->animation.stop();
		state->animation.start(
			animationCallback,
			0.,
			1.,
			st::premiumSlideDuration,
			anim::sineInOut);
	}, outer->lifetime());

	auto title = state->selected.value(
	) | rpl::map([=](PremiumPreview section) {
		return SectionTitle(section);
	}) | rpl::flatten_latest();

	auto text = state->selected.value(
	) | rpl::map([=](PremiumPreview section) {
		return SectionAbout(section);
	}) | rpl::flatten_latest();

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
	box->addRow(
		CreateSwitch(box->verticalLayout(), &state->selected),
		st::premiumDotsMargin);
	const auto showFinished = [=] {
		state->showFinished = true;
		if (base::take(state->preloadScheduled)) {
			state->preload();
		}
	};
	if (descriptor.fromSettings && controller->session().premium()) {
		box->setShowFinishedCallback(showFinished);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	} else {
		box->setStyle(st::premiumPreviewBox);
		const auto buttonPadding = st::premiumPreviewBox.buttonPadding;
		const auto width = size.width()
			- buttonPadding.left()
			- buttonPadding.right();
		const auto computeRef = [=] {
			return Settings::LookupPremiumRef(state->selected.current());
		};
		auto unlock = state->selected.value(
		) | rpl::map([=](PremiumPreview section) {
			return (section == PremiumPreview::Reactions)
				? tr::lng_premium_unlock_reactions()
				: (section == PremiumPreview::Stickers)
				? tr::lng_premium_unlock_stickers()
				: (section == PremiumPreview::AnimatedEmoji)
				? tr::lng_premium_unlock_emoji()
				: tr::lng_premium_more_about();
		}) | rpl::flatten_latest();
		auto button = descriptor.fromSettings
			? object_ptr<Ui::GradientButton>::fromRaw(
				Settings::CreateSubscribeButton({
					controller,
					box,
					computeRef,
				}))
			: CreateUnlockButton(box, std::move(unlock));
		button->resizeToWidth(width);
		if (!descriptor.fromSettings) {
			button->setClickedCallback([=] {
				Settings::ShowPremium(
					controller,
					Settings::LookupPremiumRef(state->selected.current()));
			});
		}
		box->setShowFinishedCallback([=, raw = button.data()]{
			showFinished();
			raw->startGlareAnimation();
		});
		box->addButton(std::move(button));
	}

	if (descriptor.fromSettings) {
		Data::AmPremiumValue(
			&controller->session()
		) | rpl::skip(1) | rpl::start_with_next([=] {
			box->closeBox();
		}, box->lifetime());
	}

	box->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get())->key();
			if (key == Qt::Key_Left) {
				move(-1);
			} else if (key == Qt::Key_Right) {
				move(1);
			}
		}
	}, box->lifetime());

	if (const auto &hidden = descriptor.hiddenCallback) {
		box->boxClosing() | rpl::start_with_next(hidden, box->lifetime());
	}
}

void Show(
		not_null<Window::SessionController*> controller,
		const Descriptor &descriptor,
		const std::shared_ptr<Data::DocumentMedia> &media,
		QImage back) {
	const auto box = controller->show(
		Box(PreviewBox, controller, descriptor, media, back));
	if (descriptor.shownCallback) {
		descriptor.shownCallback(box);
	}
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

void Show(
		not_null<Window::SessionController*> controller,
		Descriptor &&descriptor) {
	if (!controller->session().premiumPossible()) {
		const auto box = controller->show(Box(PremiumUnavailableBox));
		if (descriptor.shownCallback) {
			descriptor.shownCallback(box);
		}
		return;
	}
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
	const auto stops = Ui::Premium::LimitGradientStops();
	crl::async([=] {
		const auto factor = style::DevicePixelRatio();
		auto cropped = QImage(
			fill * factor,
			QImage::Format_ARGB32_Premultiplied);
		cropped.setDevicePixelRatio(factor);
		auto p = QPainter(&cropped);
		auto gradient = QLinearGradient(0, fill.height(), fill.width(), 0);
		gradient.setStops(stops);
		p.fillRect(QRect(QPoint(), fill), gradient);
		p.end();

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
		Fn<void(not_null<Ui::BoxContent*>)> shown) {
	ShowPremiumPreviewBox(controller, section, {}, std::move(shown));
}

void ShowPremiumPreviewBox(
		not_null<Window::SessionController*> controller,
		PremiumPreview section,
		const base::flat_map<ReactionId, ReactionDisableType> &disabled,
		Fn<void(not_null<Ui::BoxContent*>)> shown) {
	Show(controller, Descriptor{
		.section = section,
		.disabled = disabled,
		.shownCallback = std::move(shown),
	});
}

void ShowPremiumPreviewToBuy(
		not_null<Window::SessionController*> controller,
		PremiumPreview section,
		Fn<void()> hiddenCallback) {
	Show(controller, Descriptor{
		.section = section,
		.fromSettings = true,
		.hiddenCallback = std::move(hiddenCallback),
	});
}

void PremiumUnavailableBox(not_null<Ui::GenericBox*> box) {
	Ui::ConfirmBox(box, {
		.text = tr::lng_premium_unavailable(
			tr::now,
			Ui::Text::RichLangValue),
		.inform = true,
	});
}

void DoubledLimitsPreviewBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	const auto limits = Data::PremiumLimits(session);
	auto entries = std::vector<Ui::Premium::ListEntry>();
	{
		const auto premium = limits.channelsPremium();
		entries.push_back(Ui::Premium::ListEntry{
			tr::lng_premium_double_limits_subtitle_channels(),
			tr::lng_premium_double_limits_about_channels(
				lt_count,
				rpl::single(float64(premium)),
				Ui::Text::RichLangValue),
			limits.channelsDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.dialogsPinnedPremium();
		entries.push_back(Ui::Premium::ListEntry{
			tr::lng_premium_double_limits_subtitle_pins(),
			tr::lng_premium_double_limits_about_pins(
				lt_count,
				rpl::single(float64(premium)),
				Ui::Text::RichLangValue),
			limits.dialogsPinnedDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.channelsPublicPremium();
		entries.push_back(Ui::Premium::ListEntry{
			tr::lng_premium_double_limits_subtitle_links(),
			tr::lng_premium_double_limits_about_links(
				lt_count,
				rpl::single(float64(premium)),
				Ui::Text::RichLangValue),
			limits.channelsPublicDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.gifsPremium();
		entries.push_back(Ui::Premium::ListEntry{
			tr::lng_premium_double_limits_subtitle_gifs(),
			tr::lng_premium_double_limits_about_gifs(
				lt_count,
				rpl::single(float64(premium)),
				Ui::Text::RichLangValue),
			limits.gifsDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.stickersFavedPremium();
		entries.push_back(Ui::Premium::ListEntry{
			tr::lng_premium_double_limits_subtitle_stickers(),
			tr::lng_premium_double_limits_about_stickers(
				lt_count,
				rpl::single(float64(premium)),
				Ui::Text::RichLangValue),
			limits.stickersFavedDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.aboutLengthPremium();
		entries.push_back(Ui::Premium::ListEntry{
			tr::lng_premium_double_limits_subtitle_bio(),
			tr::lng_premium_double_limits_about_bio(
				Ui::Text::RichLangValue),
			limits.aboutLengthDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.captionLengthPremium();
		entries.push_back(Ui::Premium::ListEntry{
			tr::lng_premium_double_limits_subtitle_captions(),
			tr::lng_premium_double_limits_about_captions(
				Ui::Text::RichLangValue),
			limits.captionLengthDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.dialogFiltersPremium();
		entries.push_back(Ui::Premium::ListEntry{
			tr::lng_premium_double_limits_subtitle_folders(),
			tr::lng_premium_double_limits_about_folders(
				lt_count,
				rpl::single(float64(premium)),
				Ui::Text::RichLangValue),
			limits.dialogFiltersDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.dialogFiltersChatsPremium();
		entries.push_back(Ui::Premium::ListEntry{
			tr::lng_premium_double_limits_subtitle_folder_chats(),
			tr::lng_premium_double_limits_about_folder_chats(
				lt_count,
				rpl::single(float64(premium)),
				Ui::Text::RichLangValue),
			limits.dialogFiltersChatsDefault(),
			premium,
		});
	}
	const auto nextMax = session->domain().maxAccounts() + 1;
	const auto till = (nextMax >= Main::Domain::kPremiumMaxAccounts)
		? QString::number(Main::Domain::kPremiumMaxAccounts)
		: (QString::number(nextMax) + QChar('+'));
	entries.push_back(Ui::Premium::ListEntry{
		tr::lng_premium_double_limits_subtitle_accounts(),
		tr::lng_premium_double_limits_about_accounts(
			lt_count,
			rpl::single(float64(Main::Domain::kPremiumMaxAccounts)),
			Ui::Text::RichLangValue),
		Main::Domain::kMaxAccounts,
		Main::Domain::kPremiumMaxAccounts,
		till,
	});
	Ui::Premium::ShowListBox(box, std::move(entries));
}

object_ptr<Ui::GradientButton> CreateUnlockButton(
		QWidget *parent,
		rpl::producer<QString> text) {
	auto result = CreatePremiumButton(parent);
	const auto &st = st::premiumPreviewBox.button;
	result->resize(result->width(), st.height);

	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		result.data(),
		std::move(text),
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
