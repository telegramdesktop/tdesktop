/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_style.h"

#include "ui/chat/chat_theme.h"
#include "ui/image/image_prepare.h" // ImageRoundRadius
#include "ui/text/text_custom_emoji.h"
#include "ui/color_contrast.h"
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
		corners = PrepareCornerPixmaps(radius, color, shadow);
	}
}

void EnsureBlockquoteCache(
		std::unique_ptr<Text::QuotePaintCache> &cache,
		Fn<ColorIndexValues()> values) {
	if (cache) {
		return;
	}
	cache = std::make_unique<Text::QuotePaintCache>();
	const auto &colors = values();
	cache->bg = colors.bg;
	cache->outlines = colors.outlines;
	cache->icon = colors.name;
}

void EnsurePreCache(
		std::unique_ptr<Text::QuotePaintCache> &cache,
		const style::color &color,
		Fn<std::optional<QColor>()> bgOverride) {
	if (cache) {
		return;
	}
	cache = std::make_unique<Text::QuotePaintCache>();
	const auto bg = bgOverride();
	cache->bg = bg.value_or(color->c);
	if (!bg) {
		cache->bg.setAlpha(kDefaultBgOpacity * 255);
	}
	cache->outlines[0] = color->c;
	cache->outlines[0].setAlpha(kDefaultOutline1Opacity * 255);
	cache->outlines[1] = cache->outlines[2] = QColor(0, 0, 0, 0);
	cache->header = color->c;
	cache->header.setAlpha(kDefaultOutline2Opacity * 255);
	cache->icon = cache->outlines[0];
	cache->icon.setAlpha(kDefaultOutline3Opacity * 255);
}

} // namespace

not_null<const MessageStyle*> ChatPaintContext::messageStyle() const {
	return &st->messageStyle(outbg, selected());
}

not_null<const MessageImageStyle*> ChatPaintContext::imageStyle() const {
	return &st->imageStyle(selected());
}

not_null<Text::QuotePaintCache*> ChatPaintContext::quoteCache(
		uint8 colorIndex) const {
	return !outbg
		? st->coloredQuoteCache(selected(), colorIndex).get()
		: messageStyle()->quoteCache[
			st->colorPatternIndex(colorIndex)].get();
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

ColorIndexValues SimpleColorIndexValues(QColor color, int patternIndex) {
	auto bg = color;
	bg.setAlpha(kDefaultBgOpacity * 255);
	auto result = ColorIndexValues{
		.name = color,
		.bg = bg,
	};
	result.outlines[0] = color;
	result.outlines[0].setAlpha(kDefaultOutline1Opacity * 255);
	if (patternIndex > 1) {
		result.outlines[1] = result.outlines[0];
		result.outlines[1].setAlpha(kDefaultOutline2Opacity * 255);
		result.outlines[2] = result.outlines[0];
		result.outlines[2].setAlpha(kDefaultOutline3Opacity * 255);
	} else if (patternIndex > 0) {
		result.outlines[1] = result.outlines[0];
		result.outlines[1].setAlpha(kDefaultOutlineOpacitySecond * 255);
		result.outlines[2] = QColor(0, 0, 0, 0);
	} else {
		result.outlines[1] = result.outlines[2] = QColor(0, 0, 0, 0);
	}
	return result;
}

int BackgroundEmojiData::CacheIndex(
		bool selected,
		bool outbg,
		bool inbubble,
		uint8 colorIndexPlusOne) {
	const auto base = colorIndexPlusOne
		? (colorIndexPlusOne - 1)
		: (kColorIndexCount + (!inbubble ? 0 : outbg ? 1 : 2));
	return (base * 2) + (selected ? 1 : 0);
};

int ColorPatternIndex(
		const ColorIndicesCompressed &indices,
		uint8 colorIndex,
		bool dark) {
	Expects(colorIndex >= 0 && colorIndex < kColorIndexCount);

	if (!indices.colors
		|| colorIndex < kSimpleColorIndexCount) {
		return 0;
	}
	auto &data = (*indices.colors)[colorIndex];
	auto &colors = dark ? data.dark : data.light;
	return colors[2] ? 2 : colors[1] ? 1 : 0;
}

ChatStyle::ChatStyle(rpl::producer<ColorIndicesCompressed> colorIndices) {
	if (colorIndices) {
		_colorIndicesLifetime = std::move(
			colorIndices
		) | rpl::start_with_next([=](ColorIndicesCompressed &&indices) {
			_colorIndices = std::move(indices);
		});
	}

	finalize();
	make(_historyPsaForwardPalette, st::historyPsaForwardPalette);
	make(_imgReplyTextPalette, st::imgReplyTextPalette);
	make(_serviceTextPalette, st::serviceTextPalette);
	make(_priceTagTextPalette, st::priceTagTextPalette);
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
	make(_msgBotKbCopyIcon, st::msgBotKbCopyIcon);
	make(_historyFastCommentsIcon, st::historyFastCommentsIcon);
	make(_historyFastShareIcon, st::historyFastShareIcon);
	make(_historyFastTranscribeIcon, st::historyFastTranscribeIcon);
	make(_historyFastTranscribeLock, st::historyFastTranscribeLock);
	make(_historyGoToOriginalIcon, st::historyGoToOriginalIcon);
	make(_historyFastCloseIcon, st::historyFastCloseIcon);
	make(_historyFastMoreIcon, st::historyFastMoreIcon);
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
		&MessageStyle::historyTranscribeLock,
		st::historyTranscribeInLock,
		st::historyTranscribeInLockSelected,
		st::historyTranscribeOutLock,
		st::historyTranscribeOutLockSelected);
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
	make(
		&MessageImageStyle::historyVideoMessageTtlIcon,
		st::historyVideoMessageTtlIcon,
		st::historyVideoMessageTtlIconSelected);
	make(
		&MessageImageStyle::historyPageEnlarge,
		st::historyPageEnlarge,
		st::historyPageEnlargeSelected);
	make(
		&MessageStyle::historyVoiceMessageTTL,
		st::historyVoiceMessageInTTL,
		st::historyVoiceMessageInTTLSelected,
		st::historyVoiceMessageOutTTL,
		st::historyVoiceMessageOutTTLSelected);
	make(
		&MessageStyle::liveLocationLongIcon,
		st::liveLocationLongInIcon,
		st::liveLocationLongInIconSelected,
		st::liveLocationLongOutIcon,
		st::liveLocationLongOutIconSelected);

	updateDarkValue();
}

ChatStyle::ChatStyle(not_null<const style::palette*> isolated)
: ChatStyle(rpl::producer<ColorIndicesCompressed>()) {
	assignPalette(isolated);
}

ChatStyle::~ChatStyle() = default;

void ChatStyle::apply(not_null<ChatTheme*> theme) {
	applyCustomPalette(theme->palette());
}

void ChatStyle::updateDarkValue() {
	const auto withBg = [&](const QColor &color) {
		return CountContrast(windowBg()->c, color);
	};
	_dark = (withBg({ 0, 0, 0 }) < withBg({ 255, 255, 255 }));
}

void ChatStyle::applyCustomPalette(const style::palette *palette) {
	assignPalette(palette ? palette : style::main_palette::get().get());
	if (palette) {
		_defaultPaletteChangeLifetime.destroy();
	} else {
		style::PaletteChanged(
		) | rpl::start_with_next([=] {
			assignPalette(style::main_palette::get());
		}, _defaultPaletteChangeLifetime);
	}
}

void ChatStyle::applyAdjustedServiceBg(QColor serviceBg) {
	auto r = 0, g = 0, b = 0, a = 0;
	serviceBg.getRgb(&r, &g, &b, &a);
	msgServiceBg().set(uchar(r), uchar(g), uchar(b), uchar(a));
}

std::span<Text::SpecialColor> ChatStyle::highlightColors() const {
	if (_highlightColors.empty()) {
		const auto push = [&](const style::color &color) {
			_highlightColors.push_back({ &color->p, &color->p });
		};

		// comment, block-comment, prolog, doctype, cdata
		push(statisticsChartLineLightblue());

		// punctuation
		push(statisticsChartLineRed());

		// property, tag, boolean, number,
		// constant, symbol, deleted
		push(statisticsChartLineRed());

		// selector, attr-name, string, char, builtin, inserted
		push(statisticsChartLineOrange());

		// operator, entity, url
		push(statisticsChartLineRed());

		// atrule, attr-value, keyword, function
		push(statisticsChartLineBlue());

		// class-name
		push(statisticsChartLinePurple());

		//push(statisticsChartLineLightgreen());
		//push(statisticsChartLineGreen());
		//push(statisticsChartLineGolden());
	}
	return _highlightColors;
}

void ChatStyle::clearColorIndexCaches() {
	for (auto &style : _messageStyles) {
		for (auto &cache : style.quoteCache) {
			cache = nullptr;
		}
		for (auto &cache : style.replyCache) {
			cache = nullptr;
		}
	}
	for (auto &values : _coloredValues) {
		values.reset();
	}
	for (auto &palette : _coloredTextPalettes) {
		palette.linkFg.reset();
	}
	for (auto &cache : _coloredReplyCaches) {
		cache = nullptr;
	}
	for (auto &cache : _coloredQuoteCaches) {
		cache = nullptr;
	}
}

void ChatStyle::assignPalette(not_null<const style::palette*> palette) {
	*static_cast<style::palette*>(this) = *palette;
	style::internal::ResetIcons();

	clearColorIndexCaches();
	for (auto &style : _messageStyles) {
		style.msgBgCornersSmall = {};
		style.msgBgCornersLarge = {};
		style.preCache = nullptr;
		style.textPalette.linkAlwaysActive
			= style.semiboldPalette.linkAlwaysActive
			= (style.textPalette.linkFg->c == style.historyTextFg->c);
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
	updateDarkValue();

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
		_serviceBgCornersInverted = PrepareInvertedCornerPixmaps(
			HistoryServiceMsgInvertedRadius(),
			msgServiceBg());
	}
	return _serviceBgCornersInverted;
}

const MessageStyle &ChatStyle::messageStyle(bool outbg, bool selected) const {
	auto &result = messageStyleRaw(outbg, selected);
	EnsureCorners(
		result.msgBgCornersSmall,
		BubbleRadiusSmall(),
		result.msgBg,
		&result.msgShadow);
	EnsureCorners(
		result.msgBgCornersLarge,
		BubbleRadiusLarge(),
		result.msgBg,
		&result.msgShadow);
	const auto &replyBar = result.msgReplyBarColor->c;
	for (auto i = 0; i != kColorPatternsCount; ++i) {
		EnsureBlockquoteCache(
			result.replyCache[i],
			[&] { return SimpleColorIndexValues(replyBar, i); });
		if (!result.quoteCache[i]) {
			result.quoteCache[i] = std::make_unique<Text::QuotePaintCache>(
				*result.replyCache[i]);
		}
	}

	const auto preBgOverride = [&] {
		return _dark ? QColor(0, 0, 0, 192) : std::optional<QColor>();
	};
	EnsurePreCache(
		result.preCache,
		(selected
			? result.textPalette.selectMonoFg
			: result.textPalette.monoFg),
		preBgOverride);
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
		BubbleRadiusSmall(),
		result.msgServiceBg);
	EnsureCorners(
		result.msgServiceBgCornersLarge,
		BubbleRadiusLarge(),
		result.msgServiceBg);
	EnsureCorners(
		result.msgShadowCornersSmall,
		BubbleRadiusSmall(),
		result.msgShadow);
	EnsureCorners(
		result.msgShadowCornersLarge,
		BubbleRadiusLarge(),
		result.msgShadow);

	return result;
}

int ChatStyle::colorPatternIndex(uint8 colorIndex) const {
	Expects(colorIndex >= 0 && colorIndex < kColorIndexCount);

	if (!_colorIndices.colors
		|| colorIndex < kSimpleColorIndexCount) {
		return 0;
	}
	auto &data = (*_colorIndices.colors)[colorIndex];
	auto &colors = _dark ? data.dark : data.light;
	return colors[2] ? 2 : colors[1] ? 1 : 0;
}

ColorIndexValues ChatStyle::computeColorIndexValues(
		bool selected,
		uint8 colorIndex) const {
	if (!_colorIndices.colors) {
		colorIndex %= kSimpleColorIndexCount;
	}
	if (colorIndex < kSimpleColorIndexCount) {
		const auto list = std::array{
			&historyPeer1NameFg(),
			&historyPeer2NameFg(),
			&historyPeer3NameFg(),
			&historyPeer4NameFg(),
			&historyPeer5NameFg(),
			&historyPeer6NameFg(),
			&historyPeer7NameFg(),
			&historyPeer8NameFg(),
		};
		const auto listSelected = std::array{
			&historyPeer1NameFgSelected(),
			&historyPeer2NameFgSelected(),
			&historyPeer3NameFgSelected(),
			&historyPeer4NameFgSelected(),
			&historyPeer5NameFgSelected(),
			&historyPeer6NameFgSelected(),
			&historyPeer7NameFgSelected(),
			&historyPeer8NameFgSelected(),
		};
		const auto paletteIndex = ColorIndexToPaletteIndex(colorIndex);
		auto result = ColorIndexValues{
			.name = (*(selected ? listSelected : list)[paletteIndex])->c,
		};
		result.bg = result.name;
		result.bg.setAlpha(kDefaultBgOpacity * 255);
		result.outlines[0] = result.name;
		result.outlines[0].setAlpha(kDefaultOutline1Opacity * 255);
		result.outlines[1] = result.outlines[2] = QColor(0, 0, 0, 0);
		return result;
	}
	auto &data = (*_colorIndices.colors)[colorIndex];
	auto &colors = _dark ? data.dark : data.light;
	if (!colors[0]) {
		return computeColorIndexValues(
			selected,
			colorIndex % kSimpleColorIndexCount);
	}
	const auto color = [&](int index) {
		const auto v = colors[index];
		return v
			? QColor((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)
			: QColor(0, 0, 0, 0);
	};
	auto result = ColorIndexValues{
		.outlines = { color(0), color(1), color(2) }
	};
	result.bg = result.outlines[0];
	result.bg.setAlpha(kDefaultBgOpacity * 255);
	result.name = result.outlines[0];
	return result;
}

not_null<Text::QuotePaintCache*> ChatStyle::serviceQuoteCache(
		bool twoColored) const {
	const auto index = (twoColored ? 1 : 0);
	const auto &service = msgServiceFg()->c;
	EnsureBlockquoteCache(
		_serviceQuoteCache[index],
		[&] { return SimpleColorIndexValues(service, twoColored); });
	return _serviceQuoteCache[index].get();
}

not_null<Text::QuotePaintCache*> ChatStyle::serviceReplyCache(
		bool twoColored) const {
	const auto index = (twoColored ? 1 : 0);
	const auto &service = msgServiceFg()->c;
	EnsureBlockquoteCache(
		_serviceReplyCache[index],
		[&] { return SimpleColorIndexValues(service, twoColored); });
	return _serviceReplyCache[index].get();
}

const ColorIndexValues &ChatStyle::coloredValues(
		bool selected,
		uint8 colorIndex) const {
	Expects(colorIndex >= 0 && colorIndex < kColorIndexCount);

	const auto shift = (selected ? kColorIndexCount : 0);
	auto &result = _coloredValues[shift + colorIndex];
	if (!result) {
		result.emplace(computeColorIndexValues(selected, colorIndex));
	}
	return *result;
}

const style::TextPalette &ChatStyle::coloredTextPalette(
		bool selected,
		uint8 colorIndex) const {
	Expects(colorIndex >= 0 && colorIndex < kColorIndexCount);

	const auto shift = (selected ? kColorIndexCount : 0);
	auto &result = _coloredTextPalettes[shift + colorIndex];
	if (!result.linkFg) {
		result.linkFg.emplace(coloredValues(selected, colorIndex).name);
		make(
			result.data,
			(selected
				? st::inReplyTextPaletteSelected
				: st::inReplyTextPalette));
		result.data.linkFg = result.linkFg->color();
		result.data.selectLinkFg = result.data.linkFg;
	}
	return result.data;
}

not_null<BackgroundEmojiData*> ChatStyle::backgroundEmojiData(
		uint64 id) const {
	return &_backgroundEmojis[id];
}

not_null<Text::QuotePaintCache*> ChatStyle::coloredQuoteCache(
		bool selected,
		uint8 colorIndex) const {
	return coloredCache(_coloredQuoteCaches, selected, colorIndex);
}

not_null<Text::QuotePaintCache*> ChatStyle::coloredReplyCache(
		bool selected,
		uint8 colorIndex) const {
	return coloredCache(_coloredReplyCaches, selected, colorIndex);
}

not_null<Text::QuotePaintCache*> ChatStyle::coloredCache(
		ColoredQuotePaintCaches &caches,
		bool selected,
		uint8 colorIndex) const {
	Expects(colorIndex >= 0 && colorIndex < kColorIndexCount);

	const auto shift = (selected ? kColorIndexCount : 0);
	auto &cache = caches[shift + colorIndex];
	EnsureBlockquoteCache(cache, [&] {
		return coloredValues(selected, colorIndex);
	});
	return cache.get();
}

const CornersPixmaps &ChatStyle::msgBotKbOverBgAddCornersSmall() const {
	EnsureCorners(
		_msgBotKbOverBgAddCornersSmall,
		BubbleRadiusSmall(),
		msgBotKbOverBgAdd());
	return _msgBotKbOverBgAddCornersSmall;
}

const CornersPixmaps &ChatStyle::msgBotKbOverBgAddCornersLarge() const {
	EnsureCorners(
		_msgBotKbOverBgAddCornersLarge,
		BubbleRadiusLarge(),
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

uint8 DecideColorIndex(uint64 id) {
	return id % kSimpleColorIndexCount;
}

uint8 ColorIndexToPaletteIndex(uint8 colorIndex) {
	Expects(colorIndex >= 0 && colorIndex < kColorIndexCount);

	const int8 map[] = { 0, 7, 4, 1, 6, 3, 5 };
	return map[colorIndex % kSimpleColorIndexCount];
}

QColor FromNameFg(
		not_null<const ChatStyle*> st,
		bool selected,
		uint8 colorIndex) {
	return st->coloredValues(selected, colorIndex).name;
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
