/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "window/window_controller.h"

#include "window/main_window.h"
#include "mainwidget.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"

namespace Window {

void Controller::enableGifPauseReason(GifPauseReason reason) {
	if (!(_gifPauseReasons & reason)) {
		auto notify = (static_cast<int>(_gifPauseReasons) < static_cast<int>(reason));
		_gifPauseReasons |= reason;
		if (notify) {
			_gifPauseLevelChanged.notify();
		}
	}
}

void Controller::disableGifPauseReason(GifPauseReason reason) {
	if (_gifPauseReasons & reason) {
		_gifPauseReasons &= ~qFlags(reason);
		if (static_cast<int>(_gifPauseReasons) < static_cast<int>(reason)) {
			_gifPauseLevelChanged.notify();
		}
	}
}

bool Controller::isGifPausedAtLeastFor(GifPauseReason reason) const {
	if (reason == GifPauseReason::Any) {
		return (_gifPauseReasons != 0) || !window()->isActive();
	}
	return (static_cast<int>(_gifPauseReasons) >= 2 * static_cast<int>(reason)) || !window()->isActive();
}

Controller::ColumnLayout Controller::computeColumnLayout() {
	auto layout = Adaptive::WindowLayout::OneColumn;

	auto bodyWidth = window()->bodyWidget()->width();
	auto dialogsWidth = qRound(bodyWidth * dialogsWidthRatio().value());
	auto historyWidth = bodyWidth - dialogsWidth;
	accumulate_max(historyWidth, st::windowMinWidth);
	dialogsWidth = bodyWidth - historyWidth;

	auto useOneColumnLayout = [this, bodyWidth, dialogsWidth] {
		auto someSectionShown = !App::main()->selectingPeer() && App::main()->isSectionShown();
		if (dialogsWidth < st::dialogsPadding.x() && (Adaptive::OneColumn() || someSectionShown)) {
			return true;
		}
		if (bodyWidth < st::windowMinWidth + st::dialogsWidthMin) {
			return true;
		}
		return false;
	};
	auto useSmallColumnLayout = [this, dialogsWidth] {
		// used if useOneColumnLayout() == false.
		if (dialogsWidth < st::dialogsWidthMin / 2) {
			return true;
		}
		return false;
	};
	if (useOneColumnLayout()) {
		dialogsWidth = bodyWidth;
	} else if (useSmallColumnLayout()) {
		layout = Adaptive::WindowLayout::SmallColumn;
		auto forceWideDialogs = [this] {
			if (dialogsListDisplayForced().value()) {
				return true;
			} else if (dialogsListFocused().value()) {
				return true;
			}
			return !App::main()->isSectionShown();
		};
		if (forceWideDialogs()) {
			dialogsWidth = st::dialogsWidthMin;
		} else {
			dialogsWidth = st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPadding.x();
		}
	} else {
		layout = Adaptive::WindowLayout::Normal;
		accumulate_max(dialogsWidth, st::dialogsWidthMin);
	}
	return { bodyWidth, dialogsWidth, layout };
}

bool Controller::provideChatWidth(int requestedWidth) {
	auto currentLayout = computeColumnLayout();
	auto chatWidth = currentLayout.bodyWidth - currentLayout.dialogsWidth;
	if (currentLayout.windowLayout == Adaptive::WindowLayout::OneColumn) {
		chatWidth = currentLayout.bodyWidth;
	}
	if (chatWidth >= requestedWidth) {
		return true;
	}
	if (!window()->canExtendWidthBy(requestedWidth - chatWidth)) {
		return false;
	}
	window()->tryToExtendWidthBy(requestedWidth - chatWidth);
	auto newLayout = computeColumnLayout();
	if (newLayout.windowLayout != Adaptive::WindowLayout::OneColumn) {
		dialogsWidthRatio().set(float64(newLayout.bodyWidth - requestedWidth) / newLayout.bodyWidth, true);
	}
	return true;
}

} // namespace Window
