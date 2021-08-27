/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/cached_round_corners.h"

namespace Ui {

class ChatTheme;

struct MessageStyle {
	CornersPixmaps corners;
	style::icon tailLeft = { Qt::Uninitialized };
	style::icon tailRight = { Qt::Uninitialized };
	style::color msgBg;
	style::color msgShadow;
};

class ChatStyle final : public style::palette {
public:
	ChatStyle();

	void apply(not_null<ChatTheme*> theme);

	[[nodiscard]] const MessageStyle &messageStyle(
		bool outbg,
		bool selected) const;

private:
	void assignPalette(not_null<const style::palette*> palette);

	void icon(style::icon &my, const style::icon &original);

	[[nodiscard]] MessageStyle &messageStyleRaw(
		bool outbg,
		bool selected) const;
	[[nodiscard]] MessageStyle &messageIn();
	[[nodiscard]] MessageStyle &messageInSelected();
	[[nodiscard]] MessageStyle &messageOut();
	[[nodiscard]] MessageStyle &messageOutSelected();
	void messageIcon(
		style::icon MessageStyle::*my,
		const style::icon &originalIn,
		const style::icon &originalInSelected,
		const style::icon &originalOut,
		const style::icon &originalOutSelected);
	void messageColor(
		style::color MessageStyle::*my,
		const style::color &originalIn,
		const style::color &originalInSelected,
		const style::color &originalOut,
		const style::color &originalOutSelected);

	mutable std::array<MessageStyle, 4> _messageStyles;

	rpl::lifetime _defaultPaletteChangeLifetime;

};

} // namespace Ui
