/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_style.h"

#include "ui/chat/chat_theme.h"
#include "styles/style_chat.h"

namespace Ui {

ChatStyle::ChatStyle() {
	finalize();
	messageIcon(
		&MessageStyle::tailLeft,
		st::historyBubbleTailInLeft,
		st::historyBubbleTailInLeftSelected,
		st::historyBubbleTailOutLeft,
		st::historyBubbleTailOutLeftSelected);
	messageIcon(
		&MessageStyle::tailRight,
		st::historyBubbleTailInRight,
		st::historyBubbleTailInRightSelected,
		st::historyBubbleTailOutRight,
		st::historyBubbleTailOutRightSelected);
	messageColor(
		&MessageStyle::msgBg,
		msgInBg(),
		msgInBgSelected(),
		msgOutBg(),
		msgOutBgSelected());
	messageColor(
		&MessageStyle::msgShadow,
		msgInShadow(),
		msgInShadowSelected(),
		msgOutShadow(),
		msgOutShadowSelected());
}

void ChatStyle::apply(not_null<ChatTheme*> theme) {
	const auto themePalette = theme->palette();
	assignPalette(themePalette
		? themePalette
		: style::main_palette::get().get());
	if (themePalette) {
		_defaultPaletteChangeLifetime.destroy();
	} else {
		style::PaletteChanged(
		) | rpl::start_with_next([=] {
			assignPalette(style::main_palette::get());
		}, _defaultPaletteChangeLifetime);
	}
}

void ChatStyle::assignPalette(not_null<const style::palette*> palette) {
	*static_cast<style::palette*>(this) = *palette;
	style::internal::resetIcons();
	for (auto &style : _messageStyles) {
		style.corners = {};
	}
}

const MessageStyle &ChatStyle::messageStyle(bool outbg, bool selected) const {
	auto &result = messageStyleRaw(outbg, selected);
	if (result.corners.p[0].isNull()) {
		result.corners = Ui::PrepareCornerPixmaps(
			st::historyMessageRadius,
			result.msgBg,
			&result.msgShadow);
	}
	return result;
}

MessageStyle &ChatStyle::messageStyleRaw(bool outbg, bool selected) const {
	return _messageStyles[(outbg ? 2 : 0) + (selected ? 1 : 0)];
}

void ChatStyle::icon(style::icon &my, const style::icon &original) {
	my = original.withPalette(*this);
}

MessageStyle &ChatStyle::messageIn() {
	return messageStyleRaw(false, false);
}

MessageStyle &ChatStyle::messageInSelected() {
	return messageStyleRaw(false, true);
}

MessageStyle &ChatStyle::messageOut() {
	return messageStyleRaw(true, false);
}

MessageStyle &ChatStyle::messageOutSelected() {
	return messageStyleRaw(true, true);
}

void ChatStyle::messageIcon(
		style::icon MessageStyle::*my,
		const style::icon &originalIn,
		const style::icon &originalInSelected,
		const style::icon &originalOut,
		const style::icon &originalOutSelected) {
	icon(messageIn().*my, originalIn);
	icon(messageInSelected().*my, originalInSelected);
	icon(messageOut().*my, originalOut);
	icon(messageOutSelected().*my, originalOutSelected);
}

void ChatStyle::messageColor(
		style::color MessageStyle::*my,
		const style::color &originalIn,
		const style::color &originalInSelected,
		const style::color &originalOut,
		const style::color &originalOutSelected) {
	messageIn().*my = originalIn;
	messageInSelected().*my = originalInSelected;
	messageOut().*my = originalOut;
	messageOutSelected().*my = originalOutSelected;
}

} // namespace Ui
