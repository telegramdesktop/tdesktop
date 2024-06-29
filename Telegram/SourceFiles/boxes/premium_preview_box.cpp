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
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "settings/settings_business.h"
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
constexpr auto kToggleStickerTimeout = 2 * crl::time(1000);
constexpr auto kStarOpacityOff = 0.1;
constexpr auto kStarOpacityOn = 1.;
constexpr auto kStarPeriod = 3 * crl::time(1000);

using Data::ReactionId;

struct Descriptor {
	PremiumFeature section = PremiumFeature::Stickers;
	DocumentData *requestedSticker = nullptr;
	bool fromSettings = false;
	Fn<void()> hiddenCallback;
	Fn<void(not_null<Ui::BoxContent*>)> shownCallback;
	bool hideSubscriptionButton = false;
};

bool operator==(const Descriptor &a, const Descriptor &b) {
	return (a.section == b.section)
		&& (a.requestedSticker == b.requestedSticker)
		&& (a.fromSettings == b.fromSettings);
}

struct Preload {
	Descriptor descriptor;
	std::shared_ptr<Data::DocumentMedia> media;
	std::weak_ptr<ChatHelpers::Show> show;
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

[[nodiscard]] rpl::producer<QString> SectionTitle(PremiumFeature section) {
	switch (section) {
	case PremiumFeature::Wallpapers:
		return tr::lng_premium_summary_subtitle_wallpapers();
	case PremiumFeature::Stories:
		return tr::lng_premium_summary_subtitle_stories();
	case PremiumFeature::DoubleLimits:
		return tr::lng_premium_summary_subtitle_double_limits();
	case PremiumFeature::MoreUpload:
		return tr::lng_premium_summary_subtitle_more_upload();
	case PremiumFeature::FasterDownload:
		return tr::lng_premium_summary_subtitle_faster_download();
	case PremiumFeature::VoiceToText:
		return tr::lng_premium_summary_subtitle_voice_to_text();
	case PremiumFeature::NoAds:
		return tr::lng_premium_summary_subtitle_no_ads();
	case PremiumFeature::EmojiStatus:
		return tr::lng_premium_summary_subtitle_emoji_status();
	case PremiumFeature::InfiniteReactions:
		return tr::lng_premium_summary_subtitle_infinite_reactions();
	case PremiumFeature::TagsForMessages:
		return tr::lng_premium_summary_subtitle_tags_for_messages();
	case PremiumFeature::LastSeen:
		return tr::lng_premium_summary_subtitle_last_seen();
	case PremiumFeature::MessagePrivacy:
		return tr::lng_premium_summary_subtitle_message_privacy();
	case PremiumFeature::Stickers:
		return tr::lng_premium_summary_subtitle_premium_stickers();
	case PremiumFeature::AnimatedEmoji:
		return tr::lng_premium_summary_subtitle_animated_emoji();
	case PremiumFeature::AdvancedChatManagement:
		return tr::lng_premium_summary_subtitle_advanced_chat_management();
	case PremiumFeature::ProfileBadge:
		return tr::lng_premium_summary_subtitle_profile_badge();
	case PremiumFeature::AnimatedUserpics:
		return tr::lng_premium_summary_subtitle_animated_userpics();
	case PremiumFeature::RealTimeTranslation:
		return tr::lng_premium_summary_subtitle_translation();
	case PremiumFeature::Business:
		return tr::lng_premium_summary_subtitle_business();
	case PremiumFeature::Effects:
		return tr::lng_premium_summary_subtitle_effects();

	case PremiumFeature::BusinessLocation:
		return tr::lng_business_subtitle_location();
	case PremiumFeature::BusinessHours:
		return tr::lng_business_subtitle_opening_hours();
	case PremiumFeature::QuickReplies:
		return tr::lng_business_subtitle_quick_replies();
	case PremiumFeature::GreetingMessage:
		return tr::lng_business_subtitle_greeting_messages();
	case PremiumFeature::AwayMessage:
		return tr::lng_business_subtitle_away_messages();
	case PremiumFeature::BusinessBots:
		return tr::lng_business_subtitle_chatbots();
	case PremiumFeature::ChatIntro:
		return tr::lng_business_subtitle_chat_intro();
	case PremiumFeature::ChatLinks:
		return tr::lng_business_subtitle_chat_links();
	}
	Unexpected("PremiumFeature in SectionTitle.");
}

[[nodiscard]] rpl::producer<QString> SectionAbout(PremiumFeature section) {
	switch (section) {
	case PremiumFeature::Wallpapers:
		return tr::lng_premium_summary_about_wallpapers();
	case PremiumFeature::Stories:
		return tr::lng_premium_summary_about_stories();
	case PremiumFeature::DoubleLimits:
		return tr::lng_premium_summary_about_double_limits();
	case PremiumFeature::MoreUpload:
		return tr::lng_premium_summary_about_more_upload();
	case PremiumFeature::FasterDownload:
		return tr::lng_premium_summary_about_faster_download();
	case PremiumFeature::VoiceToText:
		return tr::lng_premium_summary_about_voice_to_text();
	case PremiumFeature::NoAds:
		return tr::lng_premium_summary_about_no_ads();
	case PremiumFeature::EmojiStatus:
		return tr::lng_premium_summary_about_emoji_status();
	case PremiumFeature::InfiniteReactions:
		return tr::lng_premium_summary_about_infinite_reactions();
	case PremiumFeature::TagsForMessages:
		return tr::lng_premium_summary_about_tags_for_messages();
	case PremiumFeature::LastSeen:
		return tr::lng_premium_summary_about_last_seen();
	case PremiumFeature::MessagePrivacy:
		return tr::lng_premium_summary_about_message_privacy();
	case PremiumFeature::Stickers:
		return tr::lng_premium_summary_about_premium_stickers();
	case PremiumFeature::AnimatedEmoji:
		return tr::lng_premium_summary_about_animated_emoji();
	case PremiumFeature::AdvancedChatManagement:
		return tr::lng_premium_summary_about_advanced_chat_management();
	case PremiumFeature::ProfileBadge:
		return tr::lng_premium_summary_about_profile_badge();
	case PremiumFeature::AnimatedUserpics:
		return tr::lng_premium_summary_about_animated_userpics();
	case PremiumFeature::RealTimeTranslation:
		return tr::lng_premium_summary_about_translation();
	case PremiumFeature::Business:
		return tr::lng_premium_summary_about_business();
	case PremiumFeature::Effects:
		return tr::lng_premium_summary_about_effects();

	case PremiumFeature::BusinessLocation:
		return tr::lng_business_about_location();
	case PremiumFeature::BusinessHours:
		return tr::lng_business_about_opening_hours();
	case PremiumFeature::QuickReplies:
		return tr::lng_business_about_quick_replies();
	case PremiumFeature::GreetingMessage:
		return tr::lng_business_about_greeting_messages();
	case PremiumFeature::AwayMessage:
		return tr::lng_business_about_away_messages();
	case PremiumFeature::BusinessBots:
		return tr::lng_business_about_chatbots();
	case PremiumFeature::ChatIntro:
		return tr::lng_business_about_chat_intro();
	case PremiumFeature::ChatLinks:
		return tr::lng_business_about_chat_links();
	}
	Unexpected("PremiumFeature in SectionTitle.");
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
		std::shared_ptr<ChatHelpers::Show> show,
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
		style::owned_color pathFg = style::owned_color(
			QColor(255, 255, 255, 64));
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
			Stickers::EffectType::PremiumSticker);

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
		show->session().downloaderTaskFinished(
		) | rpl::take_while([=] {
			createLottieIfReady();
			return !state->lottie || !state->effect;
		}) | rpl::start(result->lifetime());
	}
	state->pathGradient = std::make_unique<Ui::PathShiftGradient>(
		st::shadowFg,
		state->pathFg.color(),
		[=] { result->update(); },
		rpl::never<>());

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
			p.setBrush(st::shadowFg);
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
		std::shared_ptr<ChatHelpers::Show> show,
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
	const auto premium = &show->session().api().premium();
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
			show,
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

[[nodiscard]] bool VideoAlignToTop(PremiumFeature section) {
	return (section == PremiumFeature::MoreUpload)
		|| (section == PremiumFeature::NoAds)
		|| (section == PremiumFeature::AnimatedEmoji);
}

[[nodiscard]] DocumentData *LookupVideo(
		not_null<Main::Session*> session,
		PremiumFeature section) {
	const auto name = [&] {
		switch (section) {
		case PremiumFeature::MoreUpload: return "more_upload";
		case PremiumFeature::FasterDownload: return "faster_download";
		case PremiumFeature::VoiceToText: return "voice_to_text";
		case PremiumFeature::NoAds: return "no_ads";
		case PremiumFeature::AnimatedEmoji: return "animated_emoji";
		case PremiumFeature::AdvancedChatManagement:
			return "advanced_chat_management";
		case PremiumFeature::EmojiStatus: return "emoji_status";
		case PremiumFeature::InfiniteReactions: return "infinite_reactions";
		case PremiumFeature::TagsForMessages: return "saved_tags";
		case PremiumFeature::ProfileBadge: return "profile_badge";
		case PremiumFeature::AnimatedUserpics: return "animated_userpics";
		case PremiumFeature::RealTimeTranslation: return "translations";
		case PremiumFeature::Wallpapers: return "wallpapers";
		case PremiumFeature::LastSeen: return "last_seen";
		case PremiumFeature::MessagePrivacy: return "message_privacy";
		case PremiumFeature::Effects: return "effects";

		case PremiumFeature::BusinessLocation: return "business_location";
		case PremiumFeature::BusinessHours: return "business_hours";
		case PremiumFeature::QuickReplies: return "quick_replies";
		case PremiumFeature::GreetingMessage: return "greeting_message";
		case PremiumFeature::AwayMessage: return "away_message";
		case PremiumFeature::BusinessBots: return "business_bots";
		case PremiumFeature::ChatIntro: return "business_intro";
		case PremiumFeature::ChatLinks: return "business_links";
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
		std::shared_ptr<ChatHelpers::Show> show,
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
		const auto ready = state->instance.player().ready()
			&& !state->instance.player().videoSize().isEmpty();
		const auto size = QSize(width, height) * style::DevicePixelRatio();

		using namespace Images;
		auto rounding = CornersMaskRef(
			Images::CornersMask(ImageRoundRadius::Large));
		if (alignToBottom) {
			rounding.p[kBottomLeft] = rounding.p[kBottomRight] = nullptr;
		} else {
			rounding.p[kTopLeft] = rounding.p[kTopRight] = nullptr;
		}
		const auto frame = !ready
			? state->blurred
			: state->instance.frame({
				.resize = size,
				.outer = size,
				.rounding = rounding,
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
		std::shared_ptr<ChatHelpers::Show> show,
		PremiumFeature section,
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
	const auto session = &show->session();
	const auto state = lifetime.make_state<State>();
	const auto create = [=] {
		const auto document = LookupVideo(session, section);
		if (!document) {
			return;
		}
		state->single = VideoPreview(
			result,
			show,
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

[[nodiscard]] not_null<Ui::RpWidget*> GenerateDefaultPreview(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		PremiumFeature section,
		Fn<void()> readyCallback) {
	switch (section) {
	case PremiumFeature::Stickers:
		return StickersPreview(parent, std::move(show), readyCallback);
	default:
		return GenericPreview(
			parent,
			std::move(show),
			section,
			readyCallback);
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
		not_null<rpl::variable<PremiumFeature>*> selected,
		std::vector<PremiumFeature> order) {
	const auto padding = st::premiumDotPadding;
	const auto width = padding.left() + st::premiumDot + padding.right();
	const auto height = padding.top() + st::premiumDot + padding.bottom();
	const auto stops = Ui::Premium::ButtonGradientStops();
	auto result = object_ptr<Ui::FixedHeightWidget>(parent.get(), height);
	const auto raw = result.data();
	const auto count = order.size();
	for (auto i = 0; i != count; ++i) {
		const auto section = order[i];
		const auto button = Ui::CreateChild<Ui::AbstractButton>(raw);
		parent->widthValue(
		) | rpl::start_with_next([=](int outer) {
			const auto full = width * count;
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
					float64(i) / (count - 1))
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
		std::shared_ptr<ChatHelpers::Show> show,
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
		bool preloadScheduled = false;
		bool showFinished = false;
		Ui::Animations::Simple animation;
		Fn<void()> preload;
		std::vector<Hiding> hiding;
		rpl::variable<PremiumFeature> selected;
		std::vector<PremiumFeature> order;
	};
	const auto state = outer->lifetime().make_state<State>();
	state->selected = descriptor.section;
	auto premiumOrder = Settings::PremiumFeaturesOrder(&show->session());
	auto businessOrder = Settings::BusinessFeaturesOrder(&show->session());
	state->order = ranges::contains(businessOrder, descriptor.section)
		? std::move(businessOrder)
		: ranges::contains(premiumOrder, descriptor.section)
		? std::move(premiumOrder)
		: std::vector{ descriptor.section };

	const auto index = [=](PremiumFeature section) {
		const auto it = ranges::find(state->order, section);
		return (it == end(state->order))
			? 0
			: std::distance(begin(state->order), it);
	};

	const auto move = [=](int delta) {
		const auto count = int(state->order.size());
		const auto now = state->selected.current();
		state->selected = state->order[(index(now) + count + delta) % count];
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
		if (now != PremiumFeature::Stickers && !state->stickersPreload) {
			const auto ready = [=] {
				if (state->stickersPreload) {
					state->stickersPreloadReady = true;
				} else {
					state->preload();
				}
			};
			state->stickersPreload = GenerateDefaultPreview(
				outer,
				show,
				PremiumFeature::Stickers,
				ready);
			state->stickersPreload->hide();
		}
	};

	switch (descriptor.section) {
	case PremiumFeature::Stickers:
		state->content = media
			? StickerPreview(outer, show, media, state->preload)
			: StickersPreview(outer, show, state->preload);
		break;
	default:
		state->content = GenericPreview(
			outer,
			show,
			descriptor.section,
			state->preload);
		break;
	}

	state->selected.value(
	) | rpl::combine_previous(
	) | rpl::start_with_next([=](PremiumFeature was, PremiumFeature now) {
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
		const auto toLeft = index(now) > index(was);
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
		if (now == PremiumFeature::Stickers && state->stickersPreload) {
			state->content = base::take(state->stickersPreload);
			state->content->show();
			if (base::take(state->stickersPreloadReady)) {
				state->preload();
			}
		} else {
			state->content = GenerateDefaultPreview(
				outer,
				show,
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
	) | rpl::map(SectionTitle) | rpl::flatten_latest();

	auto text = state->selected.value(
	) | rpl::map(SectionAbout) | rpl::flatten_latest();

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
		CreateSwitch(box->verticalLayout(), &state->selected, state->order),
		st::premiumDotsMargin);
	const auto showFinished = [=] {
		state->showFinished = true;
		if (base::take(state->preloadScheduled)) {
			state->preload();
		}
	};
	if ((descriptor.fromSettings && show->session().premium())
		|| descriptor.hideSubscriptionButton) {
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
		) | rpl::map([=](PremiumFeature section) {
			return (section == PremiumFeature::InfiniteReactions)
				? tr::lng_premium_unlock_reactions()
				: (section == PremiumFeature::Stickers)
				? tr::lng_premium_unlock_stickers()
				: (section == PremiumFeature::AnimatedEmoji)
				? tr::lng_premium_unlock_emoji()
				: (section == PremiumFeature::EmojiStatus)
				? tr::lng_premium_unlock_status()
				: tr::lng_premium_more_about();
		}) | rpl::flatten_latest();
		auto button = descriptor.fromSettings
			? object_ptr<Ui::GradientButton>::fromRaw(
				Settings::CreateSubscribeButton({
					.parent = box,
					.computeRef = computeRef,
					.show = show,
				}))
			: CreateUnlockButton(box, std::move(unlock));
		button->resizeToWidth(width);
		if (!descriptor.fromSettings) {
			button->setClickedCallback([=] {
				const auto window = show->resolveWindow(
					ChatHelpers::WindowUsage::PremiumPromo);
				if (!window) {
					return;
				}
				Settings::ShowPremium(
					window,
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
			&show->session()
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
		std::shared_ptr<ChatHelpers::Show> show,
		const Descriptor &descriptor,
		const std::shared_ptr<Data::DocumentMedia> &media,
		QImage back) {
	auto box = Box(PreviewBox, show, descriptor, media, back);
	const auto raw = box.data();
	show->showBox(std::move(box));
	if (descriptor.shownCallback) {
		descriptor.shownCallback(raw);
	}
}

void Show(std::shared_ptr<ChatHelpers::Show> show, QImage back) {
	auto &list = Preloads();
	for (auto i = begin(list); i != end(list);) {
		const auto already = i->show.lock();
		if (!already) {
			i = list.erase(i);
		} else if (already == show) {
			Show(std::move(show), i->descriptor, i->media, back);
			i = list.erase(i);
			return;
		} else {
			++i;
		}
	}
}

void DecorateListPromoBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		const Descriptor &descriptor) {
	const auto session = &show->session();

	box->addTopButton(st::boxTitleClose, [=] {
		box->closeBox();
	});

	if (!descriptor.hideSubscriptionButton) {
		Data::AmPremiumValue(
			session
		) | rpl::skip(1) | rpl::start_with_next([=] {
			box->closeBox();
		}, box->lifetime());
	}

	if (const auto &hidden = descriptor.hiddenCallback) {
		box->boxClosing() | rpl::start_with_next(hidden, box->lifetime());
	}

	if (session->premium() || descriptor.hideSubscriptionButton) {
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	} else {
		const auto button = Settings::CreateSubscribeButton({
			.parent = box,
			.computeRef = [] { return u"double_limits"_q; },
			.show = show,
		});

		box->setShowFinishedCallback([=] {
			button->startGlareAnimation();
		});

		box->setStyle(st::premiumPreviewDoubledLimitsBox);
		box->widthValue(
		) | rpl::start_with_next([=](int width) {
			const auto &padding
				= st::premiumPreviewDoubledLimitsBox.buttonPadding;
			button->resizeToWidth(width
				- padding.left()
				- padding.right());
			button->moveToLeft(padding.left(), padding.top());
		}, button->lifetime());
		box->addButton(
			object_ptr<Ui::AbstractButton>::fromRaw(button));
	}
}

void Show(
		std::shared_ptr<ChatHelpers::Show> show,
		Descriptor &&descriptor) {
	if (!show->session().premiumPossible()) {
		auto box = Box(PremiumUnavailableBox);
		const auto raw = box.data();
		show->showBox(std::move(box));
		if (descriptor.shownCallback) {
			descriptor.shownCallback(raw);
		}
		return;
	} else if (descriptor.section == PremiumFeature::DoubleLimits) {
		show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
			DoubledLimitsPreviewBox(box, &show->session());
			DecorateListPromoBox(box, show, descriptor);
		}));
		return;
	} else if (descriptor.section == PremiumFeature::Stories) {
		show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
			UpgradedStoriesPreviewBox(box, &show->session());
			DecorateListPromoBox(box, show, descriptor);
		}));
		return;
	} else if (descriptor.section == PremiumFeature::Business) {
		show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
			TelegramBusinessPreviewBox(box, &show->session());
			DecorateListPromoBox(box, show, descriptor);
		}));
		return;
	}
	auto &list = Preloads();
	for (auto i = begin(list); i != end(list);) {
		const auto already = i->show.lock();
		if (!already) {
			i = list.erase(i);
		} else if (already == show) {
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

	const auto weak = std::weak_ptr(show);
	list.push_back({
		.descriptor = descriptor,
		.media = (descriptor.requestedSticker
			? descriptor.requestedSticker->createMediaView()
			: nullptr),
		.show = weak,
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
			if (auto strong = weak.lock()) {
				Show(std::move(strong), result);
			}
		});
	});
}

} // namespace

void ShowStickerPreviewBox(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document) {
	Show(std::move(show), Descriptor{
		.section = PremiumFeature::Stickers,
		.requestedSticker = document,
	});
}

void ShowPremiumPreviewBox(
		not_null<Window::SessionController*> controller,
		PremiumFeature section,
		Fn<void(not_null<Ui::BoxContent*>)> shown) {
	ShowPremiumPreviewBox(controller->uiShow(), section, std::move(shown));
}

void ShowPremiumPreviewBox(
		std::shared_ptr<ChatHelpers::Show> show,
		PremiumFeature section,
		Fn<void(not_null<Ui::BoxContent*>)> shown,
		bool hideSubscriptionButton) {
	Show(std::move(show), Descriptor{
		.section = section,
		.shownCallback = std::move(shown),
		.hideSubscriptionButton = hideSubscriptionButton,
	});
}

void ShowPremiumPreviewToBuy(
		not_null<Window::SessionController*> controller,
		PremiumFeature section,
		Fn<void()> hiddenCallback) {
	Show(controller->uiShow(), Descriptor{
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
	box->setTitle(tr::lng_premium_summary_subtitle_double_limits());

	const auto limits = Data::PremiumLimits(session);
	auto entries = std::vector<Ui::Premium::ListEntry>();
	{
		const auto premium = limits.channelsPremium();
		entries.push_back({
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
		entries.push_back({
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
		entries.push_back({
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
		entries.push_back({
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
		entries.push_back({
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
		entries.push_back({
			tr::lng_premium_double_limits_subtitle_bio(),
			tr::lng_premium_double_limits_about_bio(
				Ui::Text::RichLangValue),
			limits.aboutLengthDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.captionLengthPremium();
		entries.push_back({
			tr::lng_premium_double_limits_subtitle_captions(),
			tr::lng_premium_double_limits_about_captions(
				Ui::Text::RichLangValue),
			limits.captionLengthDefault(),
			premium,
		});
	}
	{
		const auto premium = limits.dialogFiltersPremium();
		entries.push_back({
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
		entries.push_back({
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
	entries.push_back({
		tr::lng_premium_double_limits_subtitle_accounts(),
		tr::lng_premium_double_limits_about_accounts(
			lt_count,
			rpl::single(float64(Main::Domain::kPremiumMaxAccounts)),
			Ui::Text::RichLangValue),
		Main::Domain::kMaxAccounts,
		Main::Domain::kPremiumMaxAccounts,
		till,
	});
	Ui::Premium::ShowListBox(
		box,
		st::defaultPremiumLimits,
		std::move(entries));
}

void UpgradedStoriesPreviewBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	using namespace Ui::Text;

	box->setTitle(tr::lng_premium_summary_subtitle_stories());

	auto entries = std::vector<Ui::Premium::ListEntry>();
	entries.push_back({
		.title = tr::lng_premium_stories_subtitle_order(),
		.about = tr::lng_premium_stories_about_order(WithEntities),
		.icon = &st::settingsStoriesIconOrder,
	});
	entries.push_back({
		.title = tr::lng_premium_stories_subtitle_stealth(),
		.about = tr::lng_premium_stories_about_stealth(WithEntities),
		.icon = &st::settingsStoriesIconStealth,
	});
	entries.push_back({
		.title = tr::lng_premium_stories_subtitle_views(),
		.about = tr::lng_premium_stories_about_views(WithEntities),
		.icon = &st::settingsStoriesIconViews,
	});
	entries.push_back({
		.title = tr::lng_premium_stories_subtitle_expiration(),
		.about = tr::lng_premium_stories_about_expiration(WithEntities),
		.icon = &st::settingsStoriesIconExpiration,
	});
	entries.push_back({
		.title = tr::lng_premium_stories_subtitle_download(),
		.about = tr::lng_premium_stories_about_download(WithEntities),
		.icon = &st::settingsStoriesIconDownload,
	});
	entries.push_back({
		.title = tr::lng_premium_stories_subtitle_caption(),
		.about = tr::lng_premium_stories_about_caption(WithEntities),
		.icon = &st::settingsStoriesIconCaption,
	});
	entries.push_back({
		.title = tr::lng_premium_stories_subtitle_links(),
		.about = tr::lng_premium_stories_about_links(WithEntities),
		.icon = &st::settingsStoriesIconLinks,
	});

	Ui::Premium::ShowListBox(
		box,
		st::defaultPremiumLimits,
		std::move(entries));

	Ui::AddDividerText(
		box->verticalLayout(),
		tr::lng_premium_stories_about_mobile());
}

void TelegramBusinessPreviewBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	using namespace Ui::Text;

	box->setTitle(tr::lng_business_title());

	auto entries = std::vector<Ui::Premium::ListEntry>();
	const auto push = [&](
			tr::phrase<> title,
			tr::phrase<> description,
			const style::icon &icon) {
		entries.push_back({
			.title = title(),
			.about = description(WithEntities),
			.icon = &icon,
		});
	};
	for (const auto feature : Settings::BusinessFeaturesOrder(session)) {
		switch (feature) {
		case PremiumFeature::GreetingMessage: push(
			tr::lng_business_subtitle_greeting_messages,
			tr::lng_business_about_greeting_messages,
			st::settingsBusinessPromoGreeting);
			break;
		case PremiumFeature::AwayMessage: push(
			tr::lng_business_subtitle_away_messages,
			tr::lng_business_about_away_messages,
			st::settingsBusinessPromoAway);
			break;
		case PremiumFeature::QuickReplies: push(
			tr::lng_business_subtitle_quick_replies,
			tr::lng_business_about_quick_replies,
			st::settingsBusinessPromoReplies);
			break;
		case PremiumFeature::BusinessHours: push(
			tr::lng_business_subtitle_opening_hours,
			tr::lng_business_about_opening_hours,
			st::settingsBusinessPromoHours);
			break;
		case PremiumFeature::BusinessLocation: push(
			tr::lng_business_subtitle_location,
			tr::lng_business_about_location,
			st::settingsBusinessPromoLocation);
			break;
		case PremiumFeature::BusinessBots: push(
			tr::lng_business_subtitle_chatbots,
			tr::lng_business_about_chatbots,
			st::settingsBusinessPromoChatbots);
			break;
		case PremiumFeature::ChatIntro: push(
			tr::lng_business_subtitle_chat_intro,
			tr::lng_business_about_chat_intro,
			st::settingsBusinessPromoChatIntro);
			break;
		case PremiumFeature::ChatLinks: push(
			tr::lng_business_subtitle_chat_links,
			tr::lng_business_about_chat_links,
			st::settingsBusinessPromoChatLinks);
			break;
		}
	}

	Ui::Premium::ShowListBox(
		box,
		st::defaultPremiumLimits,
		std::move(entries));
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
