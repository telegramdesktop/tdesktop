/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/cached_round_corners.h"
#include "ui/style/style_core_palette.h"
#include "layout/layout_selection.h"

namespace Ui {

class ChatTheme;
class ChatStyle;
struct BubblePattern;

struct MessageStyle {
	CornersPixmaps corners;
	style::color msgBg;
	style::color msgShadow;
	style::color msgServiceFg;
	style::color msgDateFg;
	style::color msgFileThumbLinkFg;
	style::color msgFileBg;
	style::color historyTextFg;
	style::color msgReplyBarColor;
	style::color webPageTitleFg;
	style::color webPageDescriptionFg;
	style::TextPalette textPalette;
	style::TextPalette fwdTextPalette;
	style::TextPalette replyTextPalette;
	style::icon tailLeft = { Qt::Uninitialized };
	style::icon tailRight = { Qt::Uninitialized };
	style::icon historyRepliesIcon = { Qt::Uninitialized };
	style::icon historyViewsIcon = { Qt::Uninitialized };
	style::icon historyPinIcon = { Qt::Uninitialized };
	style::icon historySentIcon = { Qt::Uninitialized };
	style::icon historyReceivedIcon = { Qt::Uninitialized };
	style::icon historyPsaIcon = { Qt::Uninitialized };
	style::icon historyCommentsOpen = { Qt::Uninitialized };
	style::icon historyComments = { Qt::Uninitialized };
};

struct ChatPaintContext {
	not_null<const ChatStyle*> st;
	const BubblePattern *bubblesPattern = nullptr;
	QRect viewport;
	QRect clip;
	TextSelection selection;
	bool outbg = false;
	crl::time now = 0;

	void translate(int x, int y) {
		viewport.translate(x, y);
		clip.translate(x, y);
	}
	void translate(QPoint point) {
		translate(point.x(), point.y());
	}

	[[nodiscard]] bool selected() const {
		return (selection == FullSelection);
	}
	[[nodiscard]] not_null<const MessageStyle*> messageStyle() const;

	[[nodiscard]] ChatPaintContext translated(int x, int y) const {
		auto result = *this;
		result.translate(x, y);
		return result;
	}
	[[nodiscard]] ChatPaintContext translated(QPoint point) const {
		return translated(point.x(), point.y());
	}
	[[nodiscard]] ChatPaintContext withSelection(
		TextSelection selection) const {
		auto result = *this;
		result.selection = selection;
		return result;
	}

};

class ChatStyle final : public style::palette {
public:
	ChatStyle();

	void apply(not_null<ChatTheme*> theme);

	[[nodiscard]] const MessageStyle &messageStyle(
		bool outbg,
		bool selected) const;

	[[nodiscard]] const CornersPixmaps &msgServiceBgCorners() const;
	[[nodiscard]] const CornersPixmaps &msgServiceBgSelectedCorners() const;
	[[nodiscard]] const CornersPixmaps &msgBotKbOverBgAddCorners() const;
	[[nodiscard]] const CornersPixmaps &msgDateImgBgCorners() const;
	[[nodiscard]] const CornersPixmaps &msgDateImgBgSelectedCorners() const;

	[[nodiscard]] const style::TextPalette &historyPsaForwardPalette() const {
		return _historyPsaForwardPalette;
	}
	[[nodiscard]] const style::TextPalette &imgReplyTextPalette() const {
		return _imgReplyTextPalette;
	}
	[[nodiscard]] const style::icon &historyRepliesInvertedIcon() const {
		return _historyRepliesInvertedIcon;
	}
	[[nodiscard]] const style::icon &historyViewsInvertedIcon() const {
		return _historyViewsInvertedIcon;
	}
	[[nodiscard]] const style::icon &historyViewsSendingIcon() const {
		return _historyViewsSendingIcon;
	}
	[[nodiscard]] const style::icon &historyViewsSendingInvertedIcon() const {
		return _historyViewsSendingInvertedIcon;
	}
	[[nodiscard]] const style::icon &historyPinInvertedIcon() const {
		return _historyPinInvertedIcon;
	}
	[[nodiscard]] const style::icon &historySendingIcon() const {
		return _historySendingIcon;
	}
	[[nodiscard]] const style::icon &historySendingInvertedIcon() const {
		return _historySendingInvertedIcon;
	}
	[[nodiscard]] const style::icon &historySentInvertedIcon() const {
		return _historySentInvertedIcon;
	}
	[[nodiscard]] const style::icon &historyReceivedInvertedIcon() const {
		return _historyReceivedInvertedIcon;
	}
	[[nodiscard]] const style::icon &msgBotKbUrlIcon() const {
		return _msgBotKbUrlIcon;
	}
	[[nodiscard]] const style::icon &msgBotKbPaymentIcon() const {
		return _msgBotKbPaymentIcon;
	}
	[[nodiscard]] const style::icon &msgBotKbSwitchPmIcon() const {
		return _msgBotKbSwitchPmIcon;
	}
	[[nodiscard]] const style::icon &historyFastCommentsIcon() const {
		return _historyFastCommentsIcon;
	}
	[[nodiscard]] const style::icon &historyFastShareIcon() const {
		return _historyFastShareIcon;
	}
	[[nodiscard]] const style::icon &historyGoToOriginalIcon() const {
		return _historyGoToOriginalIcon;
	}

private:
	void assignPalette(not_null<const style::palette*> palette);

	void make(style::color &my, const style::color &original);
	void make(style::icon &my, const style::icon &original);
	void make(style::TextPalette &my, const style::TextPalette &original);

	[[nodiscard]] MessageStyle &messageStyleRaw(
		bool outbg,
		bool selected) const;
	[[nodiscard]] MessageStyle &messageIn();
	[[nodiscard]] MessageStyle &messageInSelected();
	[[nodiscard]] MessageStyle &messageOut();
	[[nodiscard]] MessageStyle &messageOutSelected();

	template <typename Type>
	void make(
		Type MessageStyle::*my,
		const Type &originalIn,
		const Type &originalInSelected,
		const Type &originalOut,
		const Type &originalOutSelected);

	mutable std::array<MessageStyle, 4> _messageStyles;

	mutable CornersPixmaps _msgServiceBgCorners;
	mutable CornersPixmaps _msgServiceBgSelectedCorners;
	mutable CornersPixmaps _msgBotKbOverBgAddCorners;
	mutable CornersPixmaps _msgDateImgBgCorners;
	mutable CornersPixmaps _msgDateImgBgSelectedCorners;

	style::TextPalette _historyPsaForwardPalette;
	style::TextPalette _imgReplyTextPalette;
	style::icon _historyRepliesInvertedIcon = { Qt::Uninitialized };
	style::icon _historyViewsInvertedIcon = { Qt::Uninitialized };
	style::icon _historyViewsSendingIcon = { Qt::Uninitialized };
	style::icon _historyViewsSendingInvertedIcon = { Qt::Uninitialized };
	style::icon _historyPinInvertedIcon = { Qt::Uninitialized };
	style::icon _historySendingIcon = { Qt::Uninitialized };
	style::icon _historySendingInvertedIcon = { Qt::Uninitialized };
	style::icon _historySentInvertedIcon = { Qt::Uninitialized };
	style::icon _historyReceivedInvertedIcon = { Qt::Uninitialized };
	style::icon _msgBotKbUrlIcon = { Qt::Uninitialized };
	style::icon _msgBotKbPaymentIcon = { Qt::Uninitialized };
	style::icon _msgBotKbSwitchPmIcon = { Qt::Uninitialized };
	style::icon _historyFastCommentsIcon = { Qt::Uninitialized };
	style::icon _historyFastShareIcon = { Qt::Uninitialized };
	style::icon _historyGoToOriginalIcon = { Qt::Uninitialized };

	rpl::lifetime _defaultPaletteChangeLifetime;

};

} // namespace Ui
