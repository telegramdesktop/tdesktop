/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_style.h"

#include "ui/chat/chat_theme.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace Ui {
namespace {

void EnsureCorners(
		CornersPixmaps &corners,
		int radius,
		const style::color &color,
		const style::color *shadow = nullptr) {
	if (corners.p[0].isNull()) {
		corners = Ui::PrepareCornerPixmaps(radius, color, shadow);
	}
}

} // namespace

not_null<const MessageStyle*> ChatPaintContext::messageStyle() const {
	return &st->messageStyle(outbg, selected());
}

ChatStyle::ChatStyle() {
	finalize();
	make(_historyPsaForwardPalette, st::historyPsaForwardPalette);
	make(_imgReplyTextPalette, st::imgReplyTextPalette);
	make(_historyRepliesInvertedIcon, st::historyRepliesInvertedIcon);
	make(_historyViewsInvertedIcon, st::historyViewsInvertedIcon);
	make(_historyViewsSendingIcon, st::historyViewsSendingIcon);
	make(
		_historyViewsSendingInvertedIcon,
		st::historyViewsSendingInvertedIcon);
	make(_historyPinInvertedIcon, st::historyPinInvertedIcon);
	make(_historySendingIcon, st::historySendingIcon);
	make(_historySendingInvertedIcon, st::historySendingInvertedIcon);
	make(_historySentInvertedIcon, st::historySentInvertedIcon);
	make(_historyReceivedInvertedIcon, st::historyReceivedInvertedIcon);
	make(_msgBotKbUrlIcon, st::msgBotKbUrlIcon);
	make(_msgBotKbPaymentIcon, st::msgBotKbPaymentIcon);
	make(_msgBotKbSwitchPmIcon, st::msgBotKbSwitchPmIcon);
	make(_historyFastCommentsIcon, st::historyFastCommentsIcon);
	make(_historyFastShareIcon, st::historyFastShareIcon);
	make(_historyGoToOriginalIcon, st::historyGoToOriginalIcon);
	make(
		&MessageStyle::msgBg,
		st::msgInBg,
		st::msgInBgSelected,
		st::msgOutBg,
		st::msgOutBgSelected);
	make(
		&MessageStyle::msgShadow,
		st::msgInShadow,
		st::msgInShadowSelected,
		st::msgOutShadow,
		st::msgOutShadowSelected);
	make(
		&MessageStyle::msgServiceFg,
		st::msgInServiceFg,
		st::msgInServiceFgSelected,
		st::msgOutServiceFg,
		st::msgOutServiceFgSelected);
	make(
		&MessageStyle::msgDateFg,
		st::msgInDateFg,
		st::msgInDateFgSelected,
		st::msgOutDateFg,
		st::msgOutDateFgSelected);
	make(
		&MessageStyle::msgFileThumbLinkFg,
		st::msgFileThumbLinkInFg,
		st::msgFileThumbLinkInFgSelected,
		st::msgFileThumbLinkOutFg,
		st::msgFileThumbLinkOutFgSelected);
	make(
		&MessageStyle::msgFileBg,
		st::msgFileInBg,
		st::msgFileInBgSelected,
		st::msgFileOutBg,
		st::msgFileOutBgSelected);
	make(
		&MessageStyle::historyTextFg,
		st::historyTextInFg,
		st::historyTextInFgSelected,
		st::historyTextOutFg,
		st::historyTextOutFgSelected);
	make(
		&MessageStyle::msgReplyBarColor,
		st::msgInReplyBarColor,
		st::msgInReplyBarSelColor,
		st::msgOutReplyBarColor,
		st::msgOutReplyBarSelColor);
	make(
		&MessageStyle::webPageTitleFg,
		st::webPageTitleInFg,
		st::webPageTitleInFg,
		st::webPageTitleOutFg,
		st::webPageTitleOutFg);
	make(
		&MessageStyle::webPageDescriptionFg,
		st::webPageDescriptionInFg,
		st::webPageDescriptionInFg,
		st::webPageDescriptionOutFg,
		st::webPageDescriptionOutFg);
	make(
		&MessageStyle::textPalette,
		st::inTextPalette,
		st::inTextPaletteSelected,
		st::outTextPalette,
		st::outTextPaletteSelected);
	make(
		&MessageStyle::fwdTextPalette,
		st::inFwdTextPalette,
		st::inFwdTextPaletteSelected,
		st::outFwdTextPalette,
		st::outFwdTextPaletteSelected);
	make(
		&MessageStyle::replyTextPalette,
		st::inReplyTextPalette,
		st::inReplyTextPaletteSelected,
		st::outReplyTextPalette,
		st::outReplyTextPaletteSelected);
	make(
		&MessageStyle::tailLeft,
		st::historyBubbleTailInLeft,
		st::historyBubbleTailInLeftSelected,
		st::historyBubbleTailOutLeft,
		st::historyBubbleTailOutLeftSelected);
	make(
		&MessageStyle::tailRight,
		st::historyBubbleTailInRight,
		st::historyBubbleTailInRightSelected,
		st::historyBubbleTailOutRight,
		st::historyBubbleTailOutRightSelected);
	make(
		&MessageStyle::historyRepliesIcon,
		st::historyRepliesInIcon,
		st::historyRepliesInSelectedIcon,
		st::historyRepliesOutIcon,
		st::historyRepliesOutSelectedIcon);
	make(
		&MessageStyle::historyViewsIcon,
		st::historyViewsInIcon,
		st::historyViewsInSelectedIcon,
		st::historyViewsOutIcon,
		st::historyViewsOutSelectedIcon);
	make(
		&MessageStyle::historyPinIcon,
		st::historyPinInIcon,
		st::historyPinInSelectedIcon,
		st::historyPinOutIcon,
		st::historyPinOutSelectedIcon);
	make(
		&MessageStyle::historySentIcon,
		st::historySentIcon,
		st::historySentSelectedIcon,
		st::historySentIcon,
		st::historySentSelectedIcon);
	make(
		&MessageStyle::historyReceivedIcon,
		st::historyReceivedIcon,
		st::historyReceivedSelectedIcon,
		st::historyReceivedIcon,
		st::historyReceivedSelectedIcon);
	make(
		&MessageStyle::historyPsaIcon,
		st::historyPsaIconIn,
		st::historyPsaIconInSelected,
		st::historyPsaIconOut,
		st::historyPsaIconOutSelected);
	make(
		&MessageStyle::historyCommentsOpen,
		st::historyCommentsOpenIn,
		st::historyCommentsOpenInSelected,
		st::historyCommentsOpenOut,
		st::historyCommentsOpenOutSelected);
	make(
		&MessageStyle::historyComments,
		st::historyCommentsIn,
		st::historyCommentsInSelected,
		st::historyCommentsOut,
		st::historyCommentsOutSelected);
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
	_msgServiceBgCorners = {};
	_msgServiceBgSelectedCorners = {};
	_msgBotKbOverBgAddCorners = {};
}

const MessageStyle &ChatStyle::messageStyle(bool outbg, bool selected) const {
	auto &result = messageStyleRaw(outbg, selected);
	EnsureCorners(
		result.corners,
		st::historyMessageRadius,
		result.msgBg,
		&result.msgShadow);
	return result;
}

const CornersPixmaps &ChatStyle::msgServiceBgCorners() const {
	EnsureCorners(_msgServiceBgCorners, st::dateRadius, msgServiceBg());
	return _msgServiceBgCorners;
}

const CornersPixmaps &ChatStyle::msgServiceBgSelectedCorners() const {
	EnsureCorners(
		_msgServiceBgSelectedCorners,
		st::dateRadius,
		msgServiceBgSelected());
	return _msgServiceBgSelectedCorners;
}

const CornersPixmaps &ChatStyle::msgBotKbOverBgAddCorners() const {
	EnsureCorners(
		_msgBotKbOverBgAddCorners,
		st::dateRadius,
		msgBotKbOverBgAdd());
	return _msgBotKbOverBgAddCorners;
}

const CornersPixmaps &ChatStyle::msgDateImgBgCorners() const {
	EnsureCorners(
		_msgDateImgBgCorners,
		st::dateRadius,
		msgDateImgBg());
	return _msgDateImgBgCorners;
}

const CornersPixmaps &ChatStyle::msgDateImgBgSelectedCorners() const {
	EnsureCorners(
		_msgDateImgBgSelectedCorners,
		st::dateRadius,
		msgDateImgBgSelected());
	return _msgDateImgBgSelectedCorners;
}

MessageStyle &ChatStyle::messageStyleRaw(bool outbg, bool selected) const {
	return _messageStyles[(outbg ? 2 : 0) + (selected ? 1 : 0)];
}

void ChatStyle::make(style::color &my, const style::color &original) {
	my = _colors[style::main_palette::indexOfColor(original)];
}

void ChatStyle::make(style::icon &my, const style::icon &original) {
	my = original.withPalette(*this);
}

void ChatStyle::make(
		style::TextPalette &my,
		const style::TextPalette &original) {
	make(my.linkFg, original.linkFg);
	make(my.monoFg, original.monoFg);
	make(my.selectBg, original.selectBg);
	make(my.selectFg, original.selectFg);
	make(my.selectLinkFg, original.selectLinkFg);
	make(my.selectMonoFg, original.selectMonoFg);
	make(my.selectOverlay, original.selectOverlay);
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

template <typename Type>
void ChatStyle::make(
		Type MessageStyle::*my,
		const Type &originalIn,
		const Type &originalInSelected,
		const Type &originalOut,
		const Type &originalOutSelected) {
	make(messageIn().*my, originalIn);
	make(messageInSelected().*my, originalInSelected);
	make(messageOut().*my, originalOut);
	make(messageOutSelected().*my, originalOutSelected);
}

} // namespace Ui
