/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_style.h"

#include "ui/chat/chat_theme.h"
#include "ui/image/image_prepare.h" // ImageRoundRadius
#include "ui/painter.h"
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
	make(_msgBotKbWebviewIcon, st::msgBotKbWebviewIcon);
	make(_historyFastCommentsIcon, st::historyFastCommentsIcon);
	make(_historyFastShareIcon, st::historyFastShareIcon);
	make(_historyFastTranscribeIcon, st::historyFastTranscribeIcon);
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
		&MessageStyle::historyTranscribeIcon,
		st::historyTranscribeInIcon,
		st::historyTranscribeInIconSelected,
		st::historyTranscribeOutIcon,
		st::historyTranscribeOutIconSelected);
	make(
		&MessageStyle::historyTranscribeHide,
		st::historyTranscribeInHide,
		st::historyTranscribeInHideSelected,
		st::historyTranscribeOutHide,
		st::historyTranscribeOutHideSelected);
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

ChatStyle::ChatStyle(not_null<const style::palette*> isolated)
: ChatStyle() {
	assignPalette(isolated);
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
		style.msgBgCornersSmall = {};
		style.msgBgCornersLarge = {};
	}
	for (auto &style : _imageStyles) {
		style.msgDateImgBgCorners = {};
		style.msgServiceBgCornersSmall = {};
		style.msgServiceBgCornersLarge = {};
		style.msgShadowCornersSmall = {};
		style.msgShadowCornersLarge = {};
	}
	_serviceBgCornersNormal = {};
	_serviceBgCornersInverted = {};
	_msgBotKbOverBgAddCornersSmall = {};
	_msgBotKbOverBgAddCornersLarge = {};
	for (auto &corners : _msgSelectOverlayCorners) {
		corners = {};
	}

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
		result.msgBgCornersSmall,
		st::bubbleRadiusSmall,
		result.msgBg,
		&result.msgShadow);
	EnsureCorners(
		result.msgBgCornersLarge,
		st::bubbleRadiusLarge,
		result.msgBg,
		&result.msgShadow);
	return result;
}

const MessageImageStyle &ChatStyle::imageStyle(bool selected) const {
	auto &result = imageStyleRaw(selected);
	EnsureCorners(
		result.msgDateImgBgCorners,
		(st::msgDateImgPadding.y() * 2 + st::normalFont->height) / 2,
		result.msgDateImgBg);
	EnsureCorners(
		result.msgServiceBgCornersSmall,
		st::bubbleRadiusSmall,
		result.msgServiceBg);
	EnsureCorners(
		result.msgServiceBgCornersLarge,
		st::bubbleRadiusLarge,
		result.msgServiceBg);
	EnsureCorners(
		result.msgShadowCornersSmall,
		st::bubbleRadiusSmall,
		result.msgShadow);
	EnsureCorners(
		result.msgShadowCornersLarge,
		st::bubbleRadiusLarge,
		result.msgShadow);
	return result;
}

const CornersPixmaps &ChatStyle::msgBotKbOverBgAddCornersSmall() const {
	EnsureCorners(
		_msgBotKbOverBgAddCornersSmall,
		st::bubbleRadiusSmall,
		msgBotKbOverBgAdd());
	return _msgBotKbOverBgAddCornersSmall;
}

const CornersPixmaps &ChatStyle::msgBotKbOverBgAddCornersLarge() const {
	EnsureCorners(
		_msgBotKbOverBgAddCornersLarge,
		st::bubbleRadiusLarge,
		msgBotKbOverBgAdd());
	return _msgBotKbOverBgAddCornersLarge;
}

const CornersPixmaps &ChatStyle::msgSelectOverlayCorners(
		CachedCornerRadius radius) const {
	const auto index = static_cast<int>(radius);
	Assert(index >= 0 && index < int(CachedCornerRadius::kCount));

	EnsureCorners(
		_msgSelectOverlayCorners[index],
		CachedCornerRadiusValue(radius),
		msgSelectOverlay());
	return _msgSelectOverlayCorners[index];
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
	make(my.spoilerFg, original.spoilerFg);
	make(my.selectBg, original.selectBg);
	make(my.selectFg, original.selectFg);
	make(my.selectLinkFg, original.selectLinkFg);
	make(my.selectMonoFg, original.selectMonoFg);
	make(my.selectSpoilerFg, original.selectSpoilerFg);
	make(my.selectOverlay, original.selectOverlay);
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
		QPainter &p,
		QRect rect,
		const style::color &color,
		const CornersPixmaps &corners) {
	using namespace Images;

	const auto pix = corners.p;
	const auto fillRect = [&](QRect rect) {
		p.fillRect(rect, color);
	};
	if (pix[kTopLeft].isNull()
		&& pix[kTopRight].isNull()
		&& pix[kBottomLeft].isNull()
		&& pix[kBottomRight].isNull()) {
		fillRect(rect);
		return;
	}

	const auto ratio = style::DevicePixelRatio();
	const auto fillCorner = [&](int left, int top, int index) {
		p.drawPixmap(left, top, pix[index]);
	};
	const auto cornerSize = [&](int index) {
		const auto &p = pix[index];
		return p.isNull() ? 0 : p.width() / ratio;
	};
	const auto verticalSkip = [&](int left, int right) {
		return std::max(cornerSize(left), cornerSize(right));
	};
	const auto top = verticalSkip(kTopLeft, kTopRight);
	const auto bottom = verticalSkip(kBottomLeft, kBottomRight);
	if (top) {
		const auto left = cornerSize(kTopLeft);
		const auto right = cornerSize(kTopRight);
		if (left) {
			fillCorner(rect.left(), rect.top(), kTopLeft);
			if (const auto add = top - left) {
				fillRect({ rect.left(), rect.top() + left, left, add });
			}
		}
		if (const auto fill = rect.width() - left - right; fill > 0) {
			fillRect({ rect.left() + left, rect.top(), fill, top });
		}
		if (right) {
			fillCorner(
				rect.left() + rect.width() - right,
				rect.top(),
				kTopRight);
			if (const auto add = top - right) {
				fillRect({
					rect.left() + rect.width() - right,
					rect.top() + right,
					right,
					add,
				});
			}
		}
	}
	if (const auto h = rect.height() - top - bottom; h > 0) {
		fillRect({ rect.left(), rect.top() + top, rect.width(), h });
	}
	if (bottom) {
		const auto left = cornerSize(kBottomLeft);
		const auto right = cornerSize(kBottomRight);
		if (left) {
			fillCorner(
				rect.left(),
				rect.top() + rect.height() - left,
				kBottomLeft);
			if (const auto add = bottom - left) {
				fillRect({
					rect.left(),
					rect.top() + rect.height() - bottom,
					left,
					add,
				});
			}
		}
		if (const auto fill = rect.width() - left - right; fill > 0) {
			fillRect({
				rect.left() + left,
				rect.top() + rect.height() - bottom,
				fill,
				bottom,
			});
		}
		if (right) {
			fillCorner(
				rect.left() + rect.width() - right,
				rect.top() + rect.height() - right,
				kBottomRight);
			if (const auto add = bottom - right) {
				fillRect({
					rect.left() + rect.width() - right,
					rect.top() + rect.height() - bottom,
					right,
					add,
				});
			}
		}
	}
}

void FillComplexEllipse(
		QPainter &p,
		not_null<const ChatStyle*> st,
		QRect rect) {
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st->msgSelectOverlay());
	p.drawEllipse(rect);
}

} // namespace Ui
