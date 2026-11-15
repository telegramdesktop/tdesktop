/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_look.h"

#include "data/stickers/data_custom_emoji.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user_wallet_card.h"
#include "main/main_session.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/emoji_config.h"
#include "ui/top_background_gradient.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_own_card.h"
#include "wallet/wallet_session.h"

namespace Wallet {
namespace {

constexpr auto kPatternTop = 0.125;
constexpr auto kPatternHeight = 0.79;
constexpr auto kPatternInkAlpha = 153; // As faint as the calls panel pattern.
constexpr auto kPatternSizeTag = Data::CustomEmojiSizeTag::Large;

class ScaledPatternEmoji final : public Ui::Text::CustomEmoji {
public:
	explicit ScaledPatternEmoji(not_null<Ui::Text::CustomEmoji*> emoji);

	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	const not_null<Ui::Text::CustomEmoji*> _emoji;

};

ScaledPatternEmoji::ScaledPatternEmoji(
	not_null<Ui::Text::CustomEmoji*> emoji)
: _emoji(emoji) {
}

int ScaledPatternEmoji::width() {
	return _emoji->width();
}

QString ScaledPatternEmoji::entityData() {
	return _emoji->entityData();
}

void ScaledPatternEmoji::paint(QPainter &p, const Context &context) {
	const auto scale = Ui::Emoji::GetSizeLarge()
		/ float64(Data::FrameSizeFromTag(kPatternSizeTag));
	p.save();
	p.translate(context.position);
	p.scale(scale, scale);
	p.translate(-context.position);
	_emoji->paint(p, context);
	p.restore();
}

void ScaledPatternEmoji::unload() {
	_emoji->unload();
}

bool ScaledPatternEmoji::ready() {
	return _emoji->ready();
}

bool ScaledPatternEmoji::readyInDefaultState() {
	return _emoji->readyInDefaultState();
}

[[nodiscard]] QRect PatternRect(const QRect &card) {
	return QRect(
		card.x(),
		card.y() + int(base::SafeRound(card.height() * kPatternTop)),
		card.width(),
		int(base::SafeRound(card.height() * kPatternHeight)));
}

[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> MakePatternEmoji(
		not_null<DocumentData*> document,
		Fn<void()> repaint) {
	auto result = document->owner().customEmojiManager().create(
		document,
		std::move(repaint),
		kPatternSizeTag);
	[[maybe_unused]] const auto preload = result->ready();
	return result;
}

} // namespace

CardLook ResolveCardLook(
		const std::vector<Data::WalletCardSticker> &stickers,
		const QString &background) {
	const auto canonical = CanonicalAddress(background);
	if (canonical.isEmpty()) {
		return {};
	}
	const auto i = ranges::find_if(stickers, [&](const auto &sticker) {
		return (CanonicalAddress(sticker.address) == canonical);
	});
	const auto unique = (i != end(stickers) && i->gift)
		? i->gift->unique.get()
		: nullptr;
	if (!unique
		|| !unique->backdrop.centerColor.isValid()
		|| !unique->backdrop.edgeColor.isValid()) {
		return {};
	}
	return {
		.source = canonical,
		.center = unique->backdrop.centerColor,
		.edge = unique->backdrop.edgeColor,
		.pattern = unique->pattern.document.get(),
	};
}

rpl::producer<CardLook> OwnCardLookValue(not_null<Main::Session*> session) {
	return rpl::combine(
		session->wallet().ownCard().shownValue(),
		rpl::single(false) | rpl::then(
			session->data().sessionDataAboutToBeCleared(
			) | rpl::map_to(true))
	) | rpl::map([](const OwnCardState &state, bool cleared) {
		return cleared
			? CardLook()
			: ResolveCardLook(state.stickers, state.background);
	}) | rpl::distinct_until_changed();
}

CardLookPainter::CardLookPainter(Fn<void()> repaint)
: _repaint(std::move(repaint)) {
}

CardLookPainter::~CardLookPainter() = default;

bool CardLookPainter::setLook(CardLook look) {
	if (_look == look) {
		return false;
	}
	const auto patternChanged = (_look.pattern != look.pattern);
	_look = std::move(look);
	_tintValid = false;
	if (_look.brand()) {
		_background.setTint(std::nullopt);
	}
	if (patternChanged) {
		_symbols.clear();
		_emoji = _look.pattern
			? MakePatternEmoji(_look.pattern, _repaint)
			: nullptr;
	}
	return true;
}

void CardLookPainter::paint(QPainter &p, const QRect &card, float64 angle) {
	if (!_look.brand()) {
		validateTint();
	}
	_background.paint(p, card, angle);
	if (_emoji) {
		paintPattern(p, card);
	}
}

QColor CardLookPainter::stripInk() {
	if (_look.brand()) {
		return st::windowActiveTextFg->c;
	}
	validateTint();
	return _ink;
}

void CardLookPainter::validateTint() {
	// WHY: the surfaces are read inside the paint, where the Wallet window
	// palette is active, so a theme or accent change re-adjusts the card.
	const auto bg = st::windowBg->c;
	const auto over = st::windowBgOver->c;
	if (_tintValid
		&& _surfaces.size() == 2
		&& _surfaces[0] == bg
		&& _surfaces[1] == over) {
		return;
	}
	_surfaces = { bg, over };
	_tintValid = true;
	const auto tint = ComputeCardTint(_look.center, _look.edge, _surfaces);
	_ink = CardStripInk(tint);
	_markHue = CardReferenceColor(tint);
	_background.setTint(tint);
}

CardMarkTint CardLookPainter::markTint() {
	if (_look.brand()) {
		return {};
	}
	validateTint();
	return { .hue = _markHue, .amount = 1. };
}

void CardLookPainter::paintPattern(QPainter &p, const QRect &card) {
	const auto ratio = style::DevicePixelRatio();
	if (_symbolsRatio != ratio) {
		_symbols.clear();
		_symbolsRatio = ratio;
	}
	auto symbol = ScaledPatternEmoji(_emoji.get());
	Ui::PaintBgPoints(
		p,
		Ui::PatternBgPoints(),
		_symbols,
		&symbol,
		QColor(0, 0, 0, kPatternInkAlpha),
		PatternRect(card));
}

} // namespace Wallet
