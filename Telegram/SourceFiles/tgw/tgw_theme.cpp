/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_theme.h"

#include "tgw/tgw_runtime.h"
#include "twidget/widget_host.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/effects/animation_value.h"
#include "ui/color_contrast.h"

#include "styles/style_chat.h"
#include "styles/style_widgets.h"

namespace Tgw {
namespace {

constexpr auto kFontDecimals = 2;
constexpr auto kLengthDecimals = 3;
constexpr auto kLineHeightDecimals = 3;
constexpr auto kMinFontSize = 8.;
constexpr auto kMaxFontSize = 64.;
constexpr auto kMinLineHeight = 1.;
constexpr auto kMaxLineHeight = 3.;
constexpr auto kSeparatorOpacity = 0.3;
constexpr auto kNormalWeight = 400;
constexpr auto kSemiboldWeight = 600;
constexpr auto kPressedOpacity = 1.;
constexpr auto kDisabledOpacity = 1.;

[[nodiscard]] QColor Flatten(const QColor &color, const QColor &under) {
	const auto alpha = color.alphaF();
	return QColor(
		anim::interpolate(under.red(), color.red(), alpha),
		anim::interpolate(under.green(), color.green(), alpha),
		anim::interpolate(under.blue(), color.blue(), alpha));
}

[[nodiscard]] bool IsDark(const QColor &color) {
	return Ui::CountContrast(color, QColor(255, 255, 255))
		> Ui::CountContrast(color, QColor(0, 0, 0));
}

[[nodiscard]] std::string Hex(const QColor &color) {
	return color.name(QColor::HexRgb).toStdString();
}

[[nodiscard]] double Rounded(double value, int decimals) {
	const auto factor = std::pow(10., decimals);
	return std::round(value * factor) / factor;
}

[[nodiscard]] QString Number(double value, int decimals) {
	auto result = QString::number(value, 'f', decimals);
	if (result.contains('.')) {
		while (result.endsWith('0')) {
			result.chop(1);
		}
		if (result.endsWith('.')) {
			result.chop(1);
		}
	}
	return result;
}

[[nodiscard]] QString Length(double css) {
	return Number(std::max(0., css), kLengthDecimals) + u"px"_q;
}

[[nodiscard]] std::string Box(
		double top,
		double right,
		double bottom,
		double left) {
	return (Length(top)
		+ ' '
		+ Length(right)
		+ ' '
		+ Length(bottom)
		+ ' '
		+ Length(left)).toStdString();
}

[[nodiscard]] std::string FontToken(const style::font &font) {
	const auto weight = (font->flags() & style::FontFlag::Bold)
		? kSemiboldWeight
		: kNormalWeight;
	const auto size = CssFromLogical(font->f.pixelSize());
	const auto height = CssFromLogical(font->height);
	const auto token = u"%1 %2px/%3px system-ui, sans-serif"_q.arg(
		QString::number(weight),
		Number(size, kFontDecimals),
		Number(height, kFontDecimals));
	return token.toStdString();
}

[[nodiscard]] twidget::WidgetButtonTokens ButtonTokens(
		const QColor &bg,
		const QColor &text) {
	const auto &button = st::defaultActiveButton;
	const auto &font = button.style.font;
	const auto radius = button.radius ? button.radius : st::buttonRadius;
	const auto sides = CssFromLogical(-button.width) / 2.;
	const auto bottom = button.height - button.textTop - font->height;
	return {
		.bg = Hex(bg),
		.text = Hex(text),
		.radius = Length(CssFromLogical(radius)).toStdString(),
		.font = FontToken(font),
		.padding = Box(
			CssFromLogical(button.textTop),
			sides,
			CssFromLogical(bottom),
			sides),
		.height = Length(CssFromLogical(button.height)).toStdString(),
		.pressedOpacity = kPressedOpacity,
		.disabledOpacity = kDisabledOpacity,
	};
}

[[nodiscard]] twidget::WidgetTextTokens TextTokens() {
	const auto &font = st::msgFont;
	const auto px = std::max(font->f.pixelSize(), 1);
	const auto line = st::messageTextStyle.lineHeight
		? st::messageTextStyle.lineHeight
		: font->height;
	const auto &padding = st::msgPadding;
	return {
		.fontSize = Rounded(
			std::clamp(CssFromLogical(px), kMinFontSize, kMaxFontSize),
			kFontDecimals),
		.lineHeight = Rounded(
			std::clamp(line / double(px), kMinLineHeight, kMaxLineHeight),
			kLineHeightDecimals),
		.padding = Box(
			CssFromLogical(padding.top()),
			CssFromLogical(padding.right()),
			CssFromLogical(padding.bottom()),
			CssFromLogical(padding.left())),
	};
}

[[nodiscard]] QColor BubbleColor(const Ui::ChatPaintContext &context) {
	const auto pattern = context.bubblesPattern;
	const auto &average = context.bubblesPatternAverage;
	if (!context.outbg
		|| !pattern
		|| !average
		|| context.viewport.isEmpty()
		|| pattern->pixmap.size().isEmpty()) {
		return context.st->messageStyle(context.outbg, false).msgBg->c;
	}
	auto result = *average;
	result.setAlphaF(result.alphaF() * context.st->msgOutBg()->c.alphaF());
	return result;
}

[[nodiscard]] QColor UnderColor(const Ui::ChatPaintContext &context) {
	const auto base = QColor::fromRgb(st::windowBg->c.rgb());
	const auto &wallpaper = context.backgroundAverage;
	return wallpaper ? Flatten(*wallpaper, base) : base;
}

} // namespace

ThemeKey BubbleThemeKey(const Ui::ChatPaintContext &context) {
	const auto bubble = BubbleColor(context);
	const auto opaque = (bubble.alpha() == 255);
	return {
		.style = context.st.get(),
		.bubble = bubble.rgba(),
		.under = opaque ? QRgb() : UnderColor(context).rgb(),
		.paletteVersion = context.st->paletteVersion(),
		.outbg = context.outbg,
	};
}

std::shared_ptr<const twidget::WidgetTheme> BubbleTheme(
		const Ui::ChatPaintContext &context) {
	const auto key = BubbleThemeKey(context);
	const auto chat = context.st;
	const auto &stm = chat->messageStyle(key.outbg, false);
	const auto bg = Flatten(
		QColor::fromRgba(key.bubble),
		QColor::fromRgb(key.under));
	const auto over = [&](const QColor &color) {
		return Hex(Flatten(color, bg));
	};
	const auto button = Flatten(
		chat->value(st::defaultActiveButton.textBg)->c,
		bg);
	const auto buttonText = Flatten(
		chat->value(st::defaultActiveButton.textFg)->c,
		button);
	auto secondary = stm.msgReplyBarColor->c;
	secondary.setAlpha(int(Ui::kDefaultBgOpacity * 255));
	auto separator = stm.msgDateFg->c;
	separator.setAlphaF(separator.alphaF() * kSeparatorOpacity);
	const auto surface = Hex(bg);
	const auto accent = over(stm.msgServiceFg->c);
	auto result = std::make_shared<twidget::WidgetTheme>();
	result->colorScheme = IsDark(bg) ? "dark" : "light";
	result->params = {
		{ "bg_color", surface },
		{ "text_color", over(stm.historyTextFg->c) },
		{ "hint_color", over(stm.msgDateFg->c) },
		{ "link_color", over(stm.textPalette.linkFg->c) },
		{ "button_color", Hex(button) },
		{ "button_text_color", Hex(buttonText) },
		{ "secondary_bg_color", over(secondary) },
		{ "accent_text_color", accent },
		{ "subtitle_text_color", over(stm.mediaFg->c) },
		{ "destructive_text_color", over(chat->attentionButtonFg()->c) },
		{ "section_bg_color", surface },
		{ "section_header_text_color", accent },
		{ "section_separator_color", over(separator) },
		{ "header_bg_color", surface },
		{ "bottom_bar_bg_color", surface },
	};
	result->button = ButtonTokens(button, buttonText);
	result->text = TextTokens();
	return result;
}

bool SameTheme(
		const twidget::WidgetTheme &a,
		const twidget::WidgetTheme &b) {
	const auto fields = [](const twidget::WidgetTheme &theme) {
		const auto &button = theme.button;
		const auto &text = theme.text;
		return std::tie(
			theme.colorScheme,
			theme.params,
			button.bg,
			button.text,
			button.radius,
			button.font,
			button.padding,
			button.height,
			button.pressedOpacity,
			button.disabledOpacity,
			text.fontSize,
			text.lineHeight,
			text.padding);
	};
	return fields(a) == fields(b);
}

} // namespace Tgw
