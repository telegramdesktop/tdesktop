/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_style.h"

#include "ui/chat/chat_theme.h"
#include "ui/image/image_prepare.h" // ImageRoundRadius
#include "ui/ui_utility.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

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

void RectWithCorners(
		Painter &p,
		QRect rect,
		const style::color &bg,
		const CornersPixmaps &corners,
		RectParts roundCorners) {
	const auto parts = RectPart::Top
		| RectPart::NoTopBottom
		| RectPart::Bottom
		| roundCorners;
	FillRoundRect(p, rect, bg, corners, nullptr, parts);
	if ((roundCorners & RectPart::AllCorners) != RectPart::AllCorners) {
		const auto size = corners.p[0].width() / style::DevicePixelRatio();
		if (!(roundCorners & RectPart::TopLeft)) {
			p.fillRect(rect.x(), rect.y(), size, size, bg);
		}
		if (!(roundCorners & RectPart::TopRight)) {
			p.fillRect(
				rect.x() + rect.width() - size,
				rect.y(),
				size,
				size,
				bg);
		}
		if (!(roundCorners & RectPart::BottomLeft)) {
			p.fillRect(
				rect.x(),
				rect.y() + rect.height() - size,
				size,
				size,
				bg);
		}
		if (!(roundCorners & RectPart::BottomRight)) {
			p.fillRect(
				rect.x() + rect.width() - size,
				rect.y() + rect.height() - size,
				size,
				size,
				bg);
		}
	}
}

} // namespace

not_null<const MessageStyle*> ChatPaintContext::messageStyle() const {
	return &st->messageStyle(outbg, selected());
}

not_null<const MessageImageStyle*> ChatPaintContext::imageStyle() const {
	return &st->imageStyle(selected());
}

int HistoryServiceMsgRadius() {
	static const auto result = [] {
		const auto minMessageHeight = st::msgServicePadding.top()
			+ st::msgServiceFont->height
			+ st::msgServicePadding.bottom();
		return minMessageHeight / 2;
	}();
	return result;
}

int HistoryServiceMsgInvertedRadius() {
	static const auto result = [] {
		const auto minRowHeight = st::msgServiceFont->height;
		return minRowHeight - HistoryServiceMsgRadius();
	}();
	return result;
}

int HistoryServiceMsgInvertedShrink() {
	static const auto result = [] {
		return (HistoryServiceMsgInvertedRadius() * 2) / 3;
	}();
	return result;
}

ChatStyle::ChatStyle() {
	finalize();
	make(_historyPsaForwardPalette, st::historyPsaForwardPalette);
	make(_imgReplyTextPalette, st::imgReplyTextPalette);
	make(_serviceTextPalette, st::serviceTextPalette);
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
	make(_historyMapPoint, st::historyMapPoint);
	make(_historyMapPointInner, st::historyMapPointInner);
	make(_youtubeIcon, st::youtubeIcon);
	make(_videoIcon, st::videoIcon);
	make(_historyPollChoiceRight, st::historyPollChoiceRight);
	make(_historyPollChoiceWrong, st::historyPollChoiceWrong);
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
		&MessageStyle::msgReplyBarColor,
		st::msgInReplyBarColor,
		st::msgInReplyBarSelColor,
		st::msgOutReplyBarColor,
		st::msgOutReplyBarSelColor);
	make(
		&MessageStyle::msgWaveformActive,
		st::msgWaveformInActive,
		st::msgWaveformInActiveSelected,
		st::msgWaveformOutActive,
		st::msgWaveformOutActiveSelected);
	make(
		&MessageStyle::msgWaveformInactive,
		st::msgWaveformInInactive,
		st::msgWaveformInInactiveSelected,
		st::msgWaveformOutInactive,
		st::msgWaveformOutInactiveSelected);
	make(
		&MessageStyle::historyTextFg,
		st::historyTextInFg,
		st::historyTextInFgSelected,
		st::historyTextOutFg,
		st::historyTextOutFgSelected);
	make(
		&MessageStyle::historyFileNameFg,
		st::historyFileNameInFg,
		st::historyFileNameInFgSelected,
		st::historyFileNameOutFg,
		st::historyFileNameOutFgSelected);
	make(
		&MessageStyle::historyFileRadialFg,
		st::historyFileInRadialFg,
		st::historyFileInRadialFgSelected,
		st::historyFileOutRadialFg,
		st::historyFileOutRadialFgSelected);
	make(
		&MessageStyle::mediaFg,
		st::mediaInFg,
		st::mediaInFgSelected,
		st::mediaOutFg,
		st::mediaOutFgSelected);
	make(
		&MessageStyle::textPalette,
		st::inTextPalette,
		st::inTextPaletteSelected,
		st::outTextPalette,
		st::outTextPaletteSelected);
	make(
		&MessageStyle::semiboldPalette,
		st::inSemiboldPalette,
		st::inTextPaletteSelected,
		st::outSemiboldPalette,
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
	make(
		&MessageStyle::historyCallArrow,
		st::historyCallArrowIn,
		st::historyCallArrowInSelected,
		st::historyCallArrowOut,
		st::historyCallArrowOutSelected);
	make(
		&MessageStyle::historyCallArrowMissed,
		st::historyCallArrowMissedIn,
		st::historyCallArrowMissedInSelected,
		st::historyCallArrowMissedIn,
		st::historyCallArrowMissedInSelected);
	make(
		&MessageStyle::historyCallIcon,
		st::historyCallInIcon,
		st::historyCallInIconSelected,
		st::historyCallOutIcon,
		st::historyCallOutIconSelected);
	make(
		&MessageStyle::historyCallCameraIcon,
		st::historyCallCameraInIcon,
		st::historyCallCameraInIconSelected,
		st::historyCallCameraOutIcon,
		st::historyCallCameraOutIconSelected);
	make(
		&MessageStyle::historyFilePlay,
		st::historyFileInPlay,
		st::historyFileInPlaySelected,
		st::historyFileOutPlay,
		st::historyFileOutPlaySelected);
	make(
		&MessageStyle::historyFileWaiting,
		st::historyFileInWaiting,
		st::historyFileInWaitingSelected,
		st::historyFileOutWaiting,
		st::historyFileOutWaitingSelected);
	make(
		&MessageStyle::historyFileDownload,
		st::historyFileInDownload,
		st::historyFileInDownloadSelected,
		st::historyFileOutDownload,
		st::historyFileOutDownloadSelected);
	make(
		&MessageStyle::historyFileCancel,
		st::historyFileInCancel,
		st::historyFileInCancelSelected,
		st::historyFileOutCancel,
		st::historyFileOutCancelSelected);
	make(
		&MessageStyle::historyFilePause,
		st::historyFileInPause,
		st::historyFileInPauseSelected,
		st::historyFileOutPause,
		st::historyFileOutPauseSelected);
	make(
		&MessageStyle::historyFileImage,
		st::historyFileInImage,
		st::historyFileInImageSelected,
		st::historyFileOutImage,
		st::historyFileOutImageSelected);
	make(
		&MessageStyle::historyFileDocument,
		st::historyFileInDocument,
		st::historyFileInDocumentSelected,
		st::historyFileOutDocument,
		st::historyFileOutDocumentSelected);
	make(
		&MessageStyle::historyAudioDownload,
		st::historyAudioInDownload,
		st::historyAudioInDownloadSelected,
		st::historyAudioOutDownload,
		st::historyAudioOutDownloadSelected);
	make(
		&MessageStyle::historyAudioCancel,
		st::historyAudioInCancel,
		st::historyAudioInCancelSelected,
		st::historyAudioOutCancel,
		st::historyAudioOutCancelSelected);
	make(
		&MessageStyle::historyQuizTimer,
		st::historyQuizTimerIn,
		st::historyQuizTimerInSelected,
		st::historyQuizTimerOut,
		st::historyQuizTimerOutSelected);
	make(
		&MessageStyle::historyQuizExplain,
		st::historyQuizExplainIn,
		st::historyQuizExplainInSelected,
		st::historyQuizExplainOut,
		st::historyQuizExplainOutSelected);
	make(
		&MessageStyle::historyPollChosen,
		st::historyPollInChosen,
		st::historyPollInChosenSelected,
		st::historyPollOutChosen,
		st::historyPollOutChosenSelected);
	make(
		&MessageStyle::historyPollChoiceRight,
		st::historyPollInChoiceRight,
		st::historyPollInChoiceRightSelected,
		st::historyPollOutChoiceRight,
		st::historyPollOutChoiceRightSelected);
	make(
		&MessageImageStyle::msgDateImgBg,
		st::msgDateImgBg,
		st::msgDateImgBgSelected);
	make(
		&MessageImageStyle::msgServiceBg,
		st::msgServiceBg,
		st::msgServiceBgSelected);
	make(
		&MessageImageStyle::msgShadow,
		st::msgInShadow,
		st::msgInShadowSelected);
	make(
		&MessageImageStyle::historyFileThumbRadialFg,
		st::historyFileThumbRadialFg,
		st::historyFileThumbRadialFgSelected);
	make(
		&MessageImageStyle::historyFileThumbPlay,
		st::historyFileThumbPlay,
		st::historyFileThumbPlaySelected);
	make(
		&MessageImageStyle::historyFileThumbWaiting,
		st::historyFileThumbWaiting,
		st::historyFileThumbWaitingSelected);
	make(
		&MessageImageStyle::historyFileThumbDownload,
		st::historyFileThumbDownload,
		st::historyFileThumbDownloadSelected);
	make(
		&MessageImageStyle::historyFileThumbCancel,
		st::historyFileThumbCancel,
		st::historyFileThumbCancelSelected);
	make(
		&MessageImageStyle::historyFileThumbPause,
		st::historyFileThumbPause,
		st::historyFileThumbPauseSelected);
	make(
		&MessageImageStyle::historyVideoDownload,
		st::historyVideoDownload,
		st::historyVideoDownloadSelected);
	make(
		&MessageImageStyle::historyVideoCancel,
		st::historyVideoCancel,
		st::historyVideoCancelSelected);
	make(
		&MessageImageStyle::historyVideoMessageMute,
		st::historyVideoMessageMute,
		st::historyVideoMessageMuteSelected);
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
		style.msgBgCorners = {};
	}
	for (auto &style : _imageStyles) {
		style.msgDateImgBgCorners = {};
		style.msgServiceBgCorners = {};
		style.msgShadowCorners = {};
	}
	_serviceBgCornersNormal = {};
	_serviceBgCornersInverted = {};
	_msgBotKbOverBgAddCorners = {};
	_msgSelectOverlayCornersSmall = {};
	_msgSelectOverlayCornersLarge = {};

	for (auto &stm : _messageStyles) {
		const auto same = (stm.textPalette.linkFg->c == stm.historyTextFg->c);
		stm.textPalette.linkAlwaysActive = same ? 1 : 0;
		stm.semiboldPalette.linkAlwaysActive = same ? 1 : 0;
	}

	_paletteChanged.fire({});
}

const CornersPixmaps &ChatStyle::serviceBgCornersNormal() const {
	EnsureCorners(
		_serviceBgCornersNormal,
		HistoryServiceMsgRadius(),
		msgServiceBg());
	return _serviceBgCornersNormal;
}

const CornersPixmaps &ChatStyle::serviceBgCornersInverted() const {
	if (_serviceBgCornersInverted.p[0].isNull()) {
		const auto radius = HistoryServiceMsgInvertedRadius();
		const auto size = radius * style::DevicePixelRatio();
		auto circle = style::colorizeImage(
			style::createInvertedCircleMask(radius * 2),
			msgServiceBg());
		circle.setDevicePixelRatio(style::DevicePixelRatio());
		const auto fill = [&](int index, int xoffset, int yoffset) {
			_serviceBgCornersInverted.p[index] = PixmapFromImage(
				circle.copy(QRect(xoffset, yoffset, size, size)));
		};
		fill(0, 0, 0);
		fill(1, size, 0);
		fill(2, size, size);
		fill(3, 0, size);
	}
	return _serviceBgCornersInverted;
}

const MessageStyle &ChatStyle::messageStyle(bool outbg, bool selected) const {
	auto &result = messageStyleRaw(outbg, selected);
	EnsureCorners(
		result.msgBgCorners,
		st::historyMessageRadius,
		result.msgBg,
		&result.msgShadow);
	return result;
}

const MessageImageStyle &ChatStyle::imageStyle(bool selected) const {
	auto &result = imageStyleRaw(selected);
	EnsureCorners(
		result.msgDateImgBgCorners,
		st::dateRadius,
		result.msgDateImgBg);
	EnsureCorners(
		result.msgServiceBgCorners,
		st::dateRadius,
		result.msgServiceBg);
	EnsureCorners(
		result.msgShadowCorners,
		st::historyMessageRadius,
		result.msgShadow);
	return result;
}

const CornersPixmaps &ChatStyle::msgBotKbOverBgAddCorners() const {
	EnsureCorners(
		_msgBotKbOverBgAddCorners,
		st::dateRadius,
		msgBotKbOverBgAdd());
	return _msgBotKbOverBgAddCorners;
}

const CornersPixmaps &ChatStyle::msgSelectOverlayCornersSmall() const {
	EnsureCorners(
		_msgSelectOverlayCornersSmall,
		st::roundRadiusSmall,
		msgSelectOverlay());
	return _msgSelectOverlayCornersSmall;
}

const CornersPixmaps &ChatStyle::msgSelectOverlayCornersLarge() const {
	EnsureCorners(
		_msgSelectOverlayCornersLarge,
		st::historyMessageRadius,
		msgSelectOverlay());
	return _msgSelectOverlayCornersLarge;
}

MessageStyle &ChatStyle::messageStyleRaw(bool outbg, bool selected) const {
	return _messageStyles[(outbg ? 2 : 0) + (selected ? 1 : 0)];
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

MessageImageStyle &ChatStyle::imageStyleRaw(bool selected) const {
	return _imageStyles[selected ? 1 : 0];
}

MessageImageStyle &ChatStyle::image() {
	return imageStyleRaw(false);
}

MessageImageStyle &ChatStyle::imageSelected() {
	return imageStyleRaw(true);
}

void ChatStyle::make(style::color &my, const style::color &original) const {
	my = _colors[style::main_palette::indexOfColor(original)];
}

void ChatStyle::make(style::icon &my, const style::icon &original) const {
	my = original.withPalette(*this);
}

void ChatStyle::make(
		style::TextPalette &my,
		const style::TextPalette &original) const {
	my.linkAlwaysActive = original.linkAlwaysActive;
	make(my.linkFg, original.linkFg);
	make(my.monoFg, original.monoFg);
	make(my.selectBg, original.selectBg);
	make(my.selectFg, original.selectFg);
	make(my.selectLinkFg, original.selectLinkFg);
	make(my.selectMonoFg, original.selectMonoFg);
	make(my.selectOverlay, original.selectOverlay);
	make(my.spoilerBg, original.spoilerBg);
	make(my.spoilerActiveBg, original.spoilerActiveBg);
	make(my.spoilerActiveFg, original.spoilerActiveFg);
}

void ChatStyle::make(
		style::TwoIconButton &my,
		const style::TwoIconButton &original) const {
	my = original;
	make(my.iconBelow, original.iconBelow);
	make(my.iconAbove, original.iconAbove);
	make(my.iconBelowOver, original.iconBelowOver);
	make(my.iconAboveOver, original.iconAboveOver);
	make(my.ripple.color, original.ripple.color);
}

void ChatStyle::make(
		style::ScrollArea &my,
		const style::ScrollArea &original) const {
	my = original;
	make(my.bg, original.bg);
	make(my.bgOver, original.bgOver);
	make(my.barBg, original.barBg);
	make(my.barBgOver, original.barBgOver);
	make(my.shColor, original.shColor);
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

template <typename Type>
void ChatStyle::make(
		Type MessageImageStyle::*my,
		const Type &original,
		const Type &originalSelected) {
	make(image().*my, original);
	make(imageSelected().*my, originalSelected);
}

void FillComplexOverlayRect(
		Painter &p,
		not_null<const ChatStyle*> st,
		QRect rect,
		ImageRoundRadius radius,
		RectParts roundCorners) {
	const auto bg = st->msgSelectOverlay();
	if (radius == ImageRoundRadius::Ellipse) {
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawEllipse(rect);
	} else {
		const auto &corners = (radius == ImageRoundRadius::Small)
			? st->msgSelectOverlayCornersSmall()
			: st->msgSelectOverlayCornersLarge();
		RectWithCorners(p, rect, bg, corners, roundCorners);
	}
}

void FillComplexLocationRect(
		Painter &p,
		not_null<const ChatStyle*> st,
		QRect rect,
		ImageRoundRadius radius,
		RectParts roundCorners) {
	const auto stm = &st->messageStyle(false, false);
	RectWithCorners(p, rect, stm->msgBg, stm->msgBgCorners, roundCorners);
}

} // namespace Ui
