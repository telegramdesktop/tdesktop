/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_birthday_effect.h"

#include "apiwrap.h"
#include "base/random.h"
#include "base/weak_qptr.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/data_birthday.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lottie/lottie_common.h"
#include "main/main_session.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "ui/effects/fireworks_animation.h"
#include "ui/emoji_config.h"
#include "ui/power_saving.h"
#include "ui/qt_object_factory.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

constexpr auto kDuration = crl::time(4200);
constexpr auto kMaxFrameStep = crl::time(20);
constexpr auto kFadeOutFrom = 0.9;
constexpr auto kCascadeWaveLength = 1.8;
constexpr auto kCascadeScalePart = 0.4;
constexpr auto kDigitPitchFactor = 0.88;
constexpr auto kDigitSourceBelowFactor = 2;

const auto kConfettiSetShortName = u"EmojiAnimations"_q;
const auto kDigitsSetShortName = u"FestiveFontEmoji"_q;

const auto kConfettiEmoji = {
	QString::fromUtf8("\xf0\x9f\x8e\x89"),
	QString::fromUtf8("\xf0\x9f\x8e\x86"),
	QString::fromUtf8("\xf0\x9f\x8e\x88"),
};

[[nodiscard]] float64 Cascade(
		float64 fullProgress,
		float64 position,
		float64 count,
		float64 waveLength) {
	if (count <= 0) {
		return fullProgress;
	}
	const auto waveDuration = (1. / count) * std::min(waveLength, count);
	const auto waveOffset = (position / count) * (1. - waveDuration);
	return std::clamp(
		(fullProgress - waveOffset) / waveDuration,
		0.,
		1.);
}

[[nodiscard]] std::vector<QString> DigitEmojiVariants(QChar digit) {
	const auto keycap = QChar(0x20E3);
	const auto selector = QChar(0xFE0F);
	return {
		QString() + digit + selector + keycap,
		QString() + digit + keycap,
	};
}

[[nodiscard]] bool AdvanceFrame(
		not_null<HistoryView::StickerPlayer*> player,
		int index,
		bool frozen) {
	if (frozen) {
		return false;
	}
	const auto frames = player->framesCount();
	return (frames <= 0) || (index + 1 < frames);
}

} // namespace

class BirthdayEffect final : public Ui::RpWidget {
public:
	BirthdayEffect(
		not_null<Ui::RpWidget*> parent,
		not_null<Main::Session*> session,
		int age,
		Fn<QRect()> userpicGeometry,
		Fn<bool()> paused);

	void start();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	using StickerPlayer = HistoryView::StickerPlayer;
	using EmojiDocuments = base::flat_map<EmojiPtr, DocumentData*>;
	struct Sticker {
		std::shared_ptr<Data::DocumentMedia> media;
		std::unique_ptr<StickerPlayer> player;
		bool usesTextColor = false;
	};

	void resolveSet(const QString &shortName, Fn<void(EmojiDocuments)> done);

	void loadConfetti();
	void useFireworksFallback();

	void loadDigits();
	void startDigits(const EmojiDocuments &byEmoji);

	void loadInto(
		not_null<Sticker*> slot,
		not_null<DocumentData*> document,
		ChatHelpers::StickerLottieSize sizeTag,
		QSize box,
		Fn<void()> ready);

	void startAnimation();
	void tick(crl::time now);
	void paintConfetti(QPainter &p);
	void paintDigits(QPainter &p);
	void destroy();

	[[nodiscard]] bool paused() const {
		return _paused && _paused();
	}

	const not_null<Main::Session*> _session;
	const int _age = 0;
	const Fn<QRect()> _userpicGeometry;
	const Fn<bool()> _paused;

	Ui::Animations::Basic _animation;
	crl::time _lastTickTime = 0;
	float64 _progress = 0.;
	bool _started = false;
	bool _destroying = false;

	Sticker _confetti;
	int _confettiSide = 0;
	std::optional<Ui::FireworksAnimation> _fireworks;

	std::vector<Sticker> _digits;
	int _digitSide = 0;

};

BirthdayEffect::BirthdayEffect(
	not_null<Ui::RpWidget*> parent,
	not_null<Main::Session*> session,
	int age,
	Fn<QRect()> userpicGeometry,
	Fn<bool()> paused)
: RpWidget(parent)
, _session(session)
, _age(age)
, _userpicGeometry(std::move(userpicGeometry))
, _paused(std::move(paused))
, _animation([=](crl::time now) { tick(now); }) {
}

void BirthdayEffect::start() {
	_confettiSide = std::min(width(), st::infoProfileBirthdayConfettiSize);
	_digitSide = st::infoProfileBirthdayDigitSize;
	loadConfetti();
	if (_age > 0) {
		loadDigits();
	}
}

void BirthdayEffect::resolveSet(
		const QString &shortName,
		Fn<void(EmojiDocuments)> done) {
	const auto build = [](not_null<Data::StickersSet*> set) {
		auto byEmoji = EmojiDocuments();
		for (const auto &[emoji, pack] : set->emoji) {
			if (!pack.isEmpty()) {
				byEmoji.emplace(emoji, pack.front());
			}
		}
		return byEmoji;
	};
	auto &stickers = _session->data().stickers();
	for (const auto &[id, set] : stickers.sets()) {
		if (!set->emoji.empty()
			&& !set->shortName.compare(shortName, Qt::CaseInsensitive)) {
			done(build(set.get()));
			return;
		}
	}
	auto identifier = StickerSetIdentifier();
	identifier.shortName = shortName;
	_session->api().request(MTPmessages_GetStickerSet(
		Data::InputStickerSet(identifier),
		MTP_int(0) // hash
	)).done(crl::guard(this, [=](const MTPmessages_StickerSet &result) {
		auto byEmoji = EmojiDocuments();
		result.match([&](const MTPDmessages_stickerSet &data) {
			byEmoji = build(_session->data().stickers().feedSetFull(data));
		}, [](const MTPDmessages_stickerSetNotModified &) {
		});
		done(std::move(byEmoji));
	})).fail(crl::guard(this, [=] {
		done(EmojiDocuments());
	})).send();
}

void BirthdayEffect::loadConfetti() {
	resolveSet(kConfettiSetShortName, [=](EmojiDocuments byEmoji) {
		const auto count = int(kConfettiEmoji.size());
		const auto from = count ? base::RandomIndex(count) : 0;
		auto document = (DocumentData*)nullptr;
		for (auto k = 0; k != count; ++k) {
			const auto &emoticon = *(kConfettiEmoji.begin() + ((from + k) % count));
			if (const auto emoji = Ui::Emoji::Find(emoticon)) {
				const auto i = byEmoji.find(emoji->original());
				if (i != byEmoji.end()) {
					document = i->second;
					break;
				}
			}
		}
		if (!document) {
			useFireworksFallback();
			return;
		}
		loadInto(
			&_confetti,
			document,
			ChatHelpers::StickerLottieSize::EmojiInteraction,
			Size(_confettiSide),
			[=] { startAnimation(); });
	});
}

void BirthdayEffect::useFireworksFallback() {
	if (_started || _fireworks) {
		return;
	}
	_started = true;
	_fireworks.emplace([=] { update(); });
	update();
}

void BirthdayEffect::loadDigits() {
	resolveSet(kDigitsSetShortName, [=](EmojiDocuments byEmoji) {
		startDigits(byEmoji);
	});
}

void BirthdayEffect::startDigits(const EmojiDocuments &byEmoji) {
	const auto text = QString::number(_age);
	_digits.clear();
	_digits.resize(text.size());
	for (auto i = 0; i != int(text.size()); ++i) {
		auto found = (DocumentData*)nullptr;
		for (const auto &variant : DigitEmojiVariants(text[i])) {
			const auto emoji = Ui::Emoji::Find(variant);
			if (!emoji) {
				continue;
			}
			const auto j = byEmoji.find(emoji->original());
			if (j != byEmoji.end()) {
				found = j->second;
				break;
			}
		}
		if (found) {
			loadInto(
				&_digits[i],
				found,
				ChatHelpers::StickerLottieSize::StickerSet,
				Size(_digitSide),
				[=] { update(); });
		}
	}
}

void BirthdayEffect::loadInto(
		not_null<Sticker*> slot,
		not_null<DocumentData*> document,
		ChatHelpers::StickerLottieSize sizeTag,
		QSize box,
		Fn<void()> ready) {
	slot->media = document->createMediaView();
	slot->media->checkStickerLarge();
	slot->media->goodThumbnailWanted();
	const auto media = slot->media;
	rpl::single() | rpl::then(
		document->session().downloaderTaskFinished()
	) | rpl::filter([=] {
		return media->loaded();
	}) | rpl::take(1) | rpl::on_next([=] {
		const auto sticker = document->sticker();
		if (!sticker) {
			return;
		}
		auto player = std::unique_ptr<StickerPlayer>();
		if (sticker->isLottie()) {
			player = std::make_unique<HistoryView::LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
					media.get(),
					sizeTag,
					box,
					Lottie::Quality::High));
		} else if (sticker->isWebm()) {
			player = std::make_unique<HistoryView::WebmPlayer>(
				media->owner()->location(),
				media->bytes(),
				box);
		} else {
			player = std::make_unique<HistoryView::StaticStickerPlayer>(
				media->owner()->location(),
				media->bytes(),
				box);
		}
		player->setRepaintCallback([=] { update(); });
		slot->usesTextColor = media->owner()->emojiUsesTextColor();
		slot->player = std::move(player);
		ready();
	}, lifetime());
}

void BirthdayEffect::startAnimation() {
	if (_started) {
		return;
	}
	_started = true;
	_lastTickTime = 0;
	_progress = 0.;
	_animation.start();
	update();
}

void BirthdayEffect::tick(crl::time now) {
	const auto delta = _lastTickTime
		? std::min(now - _lastTickTime, kMaxFrameStep)
		: crl::time(0);
	_lastTickTime = now;
	if (!paused()) {
		_progress = std::clamp(
			_progress + (delta / float64(kDuration)),
			0.,
			1.);
	}
	update();
	if (_progress >= 1.) {
		_animation.stop();
		destroy();
	}
}

void BirthdayEffect::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_fireworks) {
		if (!_fireworks->paint(p, rect())) {
			destroy();
		}
		return;
	} else if (!_started) {
		return;
	}
	paintConfetti(p);
	if (_age > 0) {
		paintDigits(p);
	}
}

void BirthdayEffect::paintConfetti(QPainter &p) {
	if (!_confetti.player || !_confetti.player->ready()) {
		return;
	}
	const auto frozen = paused();
	const auto colored = _confetti.usesTextColor
		? st::windowFgActive->c
		: QColor(0, 0, 0, 0);
	const auto info = _confetti.player->frame(
		Size(_confettiSide),
		colored,
		true,
		crl::now(),
		frozen);
	const auto image = info.image;
	const auto size = image.size() / style::DevicePixelRatio();
	const auto center = rect::center(_userpicGeometry());
	const auto x = center.x() - (size.width() / 2);
	const auto y = std::max(0, center.y() - (size.height() / 2));
	const auto opacity = (_progress < kFadeOutFrom)
		? 1.
		: std::max(0., 1. - (_progress - kFadeOutFrom) / (1. - kFadeOutFrom));

	p.setOpacity(opacity);
	p.drawImage(Rect(x, y, size), image);
	p.setOpacity(1.);

	if (AdvanceFrame(_confetti.player.get(), info.index, frozen)) {
		_confetti.player->markFrameShown();
	}
}

void BirthdayEffect::paintDigits(QPainter &p) {
	const auto count = int(_digits.size());
	if (!count) {
		return;
	}
	const auto frozen = paused();
	const auto sz = float64(_digitSide);
	const auto pitch = sz * kDigitPitchFactor;
	const auto userpic = _userpicGeometry();
	const auto sourceX = float64(rect::center(userpic).x());
	const auto sourceY = float64(
		userpic.y() + userpic.height() * kDigitSourceBelowFactor);
	const auto rowLeft = (width() - pitch * (count - 1)) / 2.;
	const auto travelX = rowLeft - sourceX;
	const auto travelY = sourceY + sz;
	const auto t = _progress;

	for (auto i = count - 1; i >= 0; --i) {
		auto &digit = _digits[i];
		if (!digit.player || !digit.player->ready()) {
			continue;
		}
		const auto local = Cascade(t, i, count, kCascadeWaveLength);
		const auto scale = anim::easeOutQuint(
			1.,
			std::clamp(local / kCascadeScalePart, 0., 1.));
		const auto centerX = sourceX + (pitch * i) + (local * travelX);
		const auto centerY = sourceY - (travelY * t * t);
		const auto drawSide = sz * scale;
		const auto colored = digit.usesTextColor
			? st::windowFgActive->c
			: QColor(0, 0, 0, 0);
		const auto info = digit.player->frame(
			Size(_digitSide),
			colored,
			false,
			crl::now(),
			frozen);
		p.drawImage(
			Rect(
				centerX - (drawSide / 2.),
				centerY - (drawSide / 2.),
				Size(drawSide)),
			info.image);
		if (AdvanceFrame(digit.player.get(), info.index, frozen)) {
			digit.player->markFrameShown();
		}
	}
}

void BirthdayEffect::destroy() {
	if (_destroying) {
		return;
	}
	_destroying = true;
	crl::on_main(this, [=] { delete this; });
}

void StartProfileBirthdayEffect(
		not_null<Ui::RpWidget*> cover,
		not_null<UserData*> user,
		Fn<QRect()> userpicGeometry,
		Fn<bool()> paused) {
	if (PowerSaving::On(PowerSaving::kChatEffects)) {
		return;
	}
	const auto effect = Ui::CreateChild<BirthdayEffect>(
		cover.get(),
		&user->session(),
		Data::BirthdayAge(user->birthday()),
		std::move(userpicGeometry),
		std::move(paused));
	effect->setAttribute(Qt::WA_TransparentForMouseEvents);
	effect->setGeometry(cover->rect());
	cover->sizeValue(
	) | rpl::on_next([=](QSize size) {
		effect->setGeometry(Rect(size));
	}, effect->lifetime());
	effect->show();
	effect->raise();
	effect->start();
}

} // namespace Info::Profile
