/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/earn/earn_icons.h"

#include "ui/effects/credits_graphics.h"
#include "ui/effects/premium_graphics.h"
#include "ui/text/custom_emoji_instance.h"
#include "ui/rect.h"
#include "styles/style_credits.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "styles/style_info.h" // infoIconReport.

#include <QFile>
#include <QtSvg/QSvgRenderer>

namespace Ui::Earn {
namespace {

[[nodiscard]] QByteArray CurrencySvg(const QColor &c) {
	const auto color = u"rgb(%1,%2,%3)"_q
		.arg(c.red())
		.arg(c.green())
		.arg(c.blue())
		.toUtf8();
	return R"(
<svg width="72px" height="72px" viewBox="0 0 72 72">
    <g stroke="none" fill="none" fill-rule="evenodd">
        <path fill=")" + color + R"(" d="M26.0148187,12.0018928 L45.6469002,12.0007985
 C48.1777253,12.0066542 49.5893649,12.0554519 50.8804395,12.462099
 C52.179101,12.873841 53.3833619,13.5432922 54.423511,14.4349062
 C55.5981446,15.4450073 56.4762152,16.8342664 58.2300327,19.6157469
 L62.7287285,26.754584 C64.0735736,28.8873486 64.7445438,29.956693
 64.9272453,31.0793566 C65.0878715,32.0687224 64.9815619,33.0847478
 64.6179018,34.0178323 C64.2054439,35.0753281 63.3244687,35.9728665
 61.5657134,37.7649812 L40.9297474,58.8112207 C39.2046857,60.5707515
 38.3420097,61.4505169 37.3471729,61.7822803 C36.4722974,62.0725732
 35.5277107,62.0725732 34.6525447,61.7822803 C33.6579983,61.4505169
 32.7950318,60.5707515 31.0699702,58.8112207 L10.4340042,37.7649812
 C8.67524887,35.9728665 7.79427365,35.0753281 7.38181574,34.0178323
 C7.01844614,33.0847478 6.91213657,32.0687224 7.07276278,31.0793566
 C7.2554642,29.9537309 7.92875816,28.8873486 9.27389376,26.754584
 L13.7699754,19.6157469 C15.5237928,16.8342664 16.4018634,15.4450073
 17.5764971,14.4349062 C18.6163557,13.5432922 19.8209071,12.873841
 21.1192781,12.462099 C22.3516674,12.0739358 23.6941154,12.0118297
 26.0148187,12.0018928 L26.0148187,12.0018928 Z M39.7838696,26.8434492
 L31.9493683,29.8016705 C31.4490624,29.9905805 31.4492603,30.6983702
 31.9496718,30.8870004 L39.7838696,33.8401019 L42.6683153,41.7951143
 C42.8531134,42.3047691 43.5738691,42.3048571 43.7587917,41.7952475
 L46.6454844,33.8401019 L54.4772842,30.8869792 C54.9776209,30.6983182
 54.9778188,29.9906327 54.4775878,29.8016918 L46.6454844,26.8434492
 L43.7587718,18.8907402 C43.5738178,18.3812036 42.8531647,18.3812916
 42.6683351,18.8908734 L39.7838696,26.8434492 Z"></path>
    </g>
</svg>)";
}

} // namespace

QImage IconCurrencyColored(int size, const QColor &c) {
	const auto s = Size(size);
	auto svg = QSvgRenderer(CurrencySvg(c));
	auto image = QImage(
		s * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		svg.render(&p, Rect(s));
	}
	return image;
}

QImage IconCurrencyColored(
		const style::font &font,
		const QColor &c) {
	return IconCurrencyColored(font->ascent, c);
}

QByteArray CurrencySvgColored(const QColor &c) {
	return CurrencySvg(c);
}

QImage MenuIconCurrency(const QSize &size) {
	auto image = QImage(
		size * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	auto p = QPainter(&image);
	st::infoIconReport.paintInCenter(
		p,
		Rect(size),
		st::infoIconFg->c);
	p.setCompositionMode(QPainter::CompositionMode_Clear);
	const auto w = st::lineWidth * 6;
	p.fillRect(
		QRect(
			rect::center(Rect(size)).x() - w / 2,
			rect::center(Rect(size)).y() - w,
			w,
			w * 2),
		Qt::white);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);

	const auto s = Size(st::inviteLinkSubscribeBoxTerms.style.font->ascent);
	auto svg = QSvgRenderer(CurrencySvg(st::infoIconFg->c));
	svg.render(
		&p,
		QRectF(
			(size.width() - s.width()) / 2.,
			(size.height() - s.height()) / 2.,
			s.width(),
			s.height()));
	return image;
}

QImage MenuIconCredits() {
	constexpr auto kStrokeWidth = 5;
	const auto sizeShift = st::lineWidth * 1.5;

	auto colorized = [&] {
		auto f = QFile(Ui::Premium::Svg());
		if (!f.open(QIODevice::ReadOnly)) {
			return QString();
		}
		return QString::fromUtf8(f.readAll()).replace(
			u"#fff"_q,
			u"#ffffff00"_q);
	}();
	colorized.replace(
		u"stroke=\"none\""_q,
		u"stroke=\"%1\""_q.arg(st::menuIconColor->c.name()));
	colorized.replace(
		u"stroke-width=\"1\""_q,
		u"stroke-width=\"%1\""_q.arg(kStrokeWidth));
	auto svg = QSvgRenderer(colorized.toUtf8());
	svg.setViewBox(svg.viewBox()
		+ Margins(style::ConvertScale(kStrokeWidth)));

	auto image = QImage(
		st::menuIconLinks.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(style::DevicePixelRatio());
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		svg.render(&p, Rect(st::menuIconLinks.size()) - Margins(sizeShift));
	}
	return image;
}

std::unique_ptr<Ui::Text::CustomEmoji> MakeCurrencyIconEmoji(
		const style::font &font,
		const QColor &c) {
	return std::make_unique<Ui::CustomEmoji::Internal>(
		u"currency_icon:%1:%2"_q.arg(font->height).arg(c.name()),
		IconCurrencyColored(font, c));
}

Ui::Text::PaletteDependentEmoji IconCreditsEmoji(
		IconDescriptor descriptor) {
	return { .factory = [=] {
		return Ui::GenerateStars(
			(descriptor.size
				? descriptor.size
				: st::defaultTableLabel.style.font->height),
			1);
	}, .margin = descriptor.margin.value_or(
		QMargins{ 0, st::giftBoxByStarsSkip, 0, 0 }) };
}

Ui::Text::PaletteDependentEmoji IconCurrencyEmoji(
		IconDescriptor descriptor) {
	return { .factory = [=] {
		return IconCurrencyColored(
			descriptor.size ? descriptor.size : st::earnTonIconSize,
			st::currencyFg->c);
	}, .margin = descriptor.margin.value_or(st::earnTonIconMargin) };
}

Ui::Text::PaletteDependentEmoji IconCreditsEmojiSmall() {
	return IconCreditsEmoji({
		.size = st::giftBoxByStarsStyle.font->height,
		.margin = QMargins{ 0, st::giftBoxByStarsStarTop, 0, 0 },
	});
}

Ui::Text::PaletteDependentEmoji IconCurrencyEmojiSmall() {
	return IconCreditsEmoji({});
}

} // namespace Ui::Earn
