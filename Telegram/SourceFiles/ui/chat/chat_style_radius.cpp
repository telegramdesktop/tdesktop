/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chat_style_radius.h"
#include "ui/chat/chat_style.h"
#include "base/options.h"

#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_chat.h"

namespace Ui {
namespace {

base::options::toggle UseSmallMsgBubbleRadius({
	.id = kOptionUseSmallMsgBubbleRadius,
	.name = "Use small message bubble radius",
	.description = "Makes most message bubbles square-ish.",
	.restartRequired = true,
});

} // namespace

const char kOptionUseSmallMsgBubbleRadius[] = "use-small-msg-bubble-radius";

int BubbleRadiusSmall() {
	return st::bubbleRadiusSmall;
}

int BubbleRadiusLarge() {
	static const auto result = [] {
		if (UseSmallMsgBubbleRadius.value()) {
			return st::bubbleRadiusSmall;
		} else {
			return st::bubbleRadiusLarge;
		}
	}();
	return result;
}

int MsgFileThumbRadiusSmall() {
	return st::msgFileThumbRadiusSmall;
}

int MsgFileThumbRadiusLarge() {
	static const auto result = [] {
		if (UseSmallMsgBubbleRadius.value()) {
			return st::msgFileThumbRadiusSmall;
		} else {
			return st::msgFileThumbRadiusLarge;
		}
	}();
	return result;
}

}
