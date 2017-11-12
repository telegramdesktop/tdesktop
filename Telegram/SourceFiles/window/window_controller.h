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
#pragma once

#include <rpl/variable.h>
#include "base/flags.h"

class MainWidget;

namespace Window {

class LayerWidget;

enum class GifPauseReason {
	Any           = 0,
	InlineResults = (1 << 0),
	SavedGifs     = (1 << 1),
	Layer         = (1 << 2),
	RoundPlaying  = (1 << 3),
	MediaPreview  = (1 << 4),
};
using GifPauseReasons = base::flags<GifPauseReason>;
inline constexpr bool is_flag_type(GifPauseReason) { return true; };

struct SectionShow {
	enum class Way {
		Forward,
		Backward,
		ClearStack,
	};
	SectionShow(
		Way way = Way::Forward,
		anim::type animated = anim::type::normal,
		anim::activation activation = anim::activation::normal)
	: way(way)
	, animated(animated)
	, activation(activation) {
	}
	SectionShow(
		anim::type animated,
		anim::activation activation = anim::activation::normal)
	: animated(animated)
	, activation(activation) {
	}

	SectionShow withWay(Way newWay) const {
		return SectionShow(newWay, animated, activation);
	}

	Way way = Way::Forward;
	anim::type animated = anim::type::normal;
	anim::activation activation = anim::activation::normal;

};

class MainWindow;
class SectionMemento;

class Controller {
public:
	Controller(not_null<MainWindow*> window) : _window(window) {
	}

	not_null<MainWindow*> window() const {
		return _window;
	}

	// This is needed for History TopBar updating when searchInPeer
	// is changed in the DialogsWidget of the current window.
	rpl::variable<PeerData*> searchInPeer;

	// This is needed while we have one HistoryWidget and one TopBarWidget
	// for all histories we show in a window. Once each history is shown
	// in its own HistoryWidget with its own TopBarWidget this can be removed.
	//
	// Also used in the Info::Profile to toggle Send Message button.
	rpl::variable<PeerData*> historyPeer;

	// This is used for auto-switch in third column Info::Profile.
	rpl::variable<PeerData*> activePeer;

	void enableGifPauseReason(GifPauseReason reason);
	void disableGifPauseReason(GifPauseReason reason);
	base::Observable<void> &gifPauseLevelChanged() {
		return _gifPauseLevelChanged;
	}
	bool isGifPausedAtLeastFor(GifPauseReason reason) const;
	base::Observable<void> &floatPlayerAreaUpdated() {
		return _floatPlayerAreaUpdated;
	}

	struct ColumnLayout {
		int bodyWidth;
		int dialogsWidth;
		int chatWidth;
		int thirdWidth;
		Adaptive::WindowLayout windowLayout;
	};
	ColumnLayout computeColumnLayout() const;
	int dialogsSmallColumnWidth() const;
	bool forceWideDialogs() const;
	void updateColumnLayout();
	bool canShowThirdSection() const;
	bool canShowThirdSectionWithoutResize() const;
	bool takeThirdSectionFromLayer();
	void resizeForThirdSection();
	void closeThirdSection();

	void showSection(
		SectionMemento &&memento,
		const SectionShow &params = SectionShow());
	void showBackFromStack(
		const SectionShow &params = SectionShow());

	void showPeerHistory(
		PeerId peerId,
		const SectionShow &params = SectionShow::Way::ClearStack,
		MsgId msgId = ShowAtUnreadMsgId);
	void showPeerHistory(
		not_null<PeerData*> peer,
		const SectionShow &params = SectionShow::Way::ClearStack,
		MsgId msgId = ShowAtUnreadMsgId);
	void showPeerHistory(
		not_null<History*> history,
		const SectionShow &params = SectionShow::Way::ClearStack,
		MsgId msgId = ShowAtUnreadMsgId);

	void showPeerInfo(
		PeerId peerId,
		const SectionShow &params = SectionShow());
	void showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params = SectionShow());
	void showPeerInfo(
		not_null<History*> history,
		const SectionShow &params = SectionShow());

	void clearSectionStack(
			const SectionShow &params = SectionShow::Way::ClearStack) {
		showPeerHistory(
			PeerId(0),
			params,
			ShowAtUnreadMsgId);
	}

	void showSpecialLayer(
		object_ptr<LayerWidget> &&layer,
		anim::type animated = anim::type::normal);
	void hideSpecialLayer(
			anim::type animated = anim::type::normal) {
		showSpecialLayer(nullptr, animated);
	}

	void showJumpToDate(
		not_null<PeerData*> peer,
		QDate requestedDate);

	base::Variable<bool> &dialogsListFocused() {
		return _dialogsListFocused;
	}
	const base::Variable<bool> &dialogsListFocused() const {
		return _dialogsListFocused;
	}
	base::Variable<bool> &dialogsListDisplayForced() {
		return _dialogsListDisplayForced;
	}
	const base::Variable<bool> &dialogsListDisplayForced() const {
		return _dialogsListDisplayForced;
	}

private:
	int minimalThreeColumnWidth() const;
	not_null<MainWidget*> chats() const;
	int countDialogsWidthFromRatio(int bodyWidth) const;
	int countThirdColumnWidthFromRatio(int bodyWidth) const;
	struct ShrinkResult {
		int dialogsWidth;
		int thirdWidth;
	};
	ShrinkResult shrinkDialogsAndThirdColumns(
		int dialogsWidth,
		int thirdWidth,
		int bodyWidth) const;

	not_null<MainWindow*> _window;

	GifPauseReasons _gifPauseReasons = 0;
	base::Observable<void> _gifPauseLevelChanged;
	base::Observable<void> _floatPlayerAreaUpdated;

	base::Variable<bool> _dialogsListFocused = { false };
	base::Variable<bool> _dialogsListDisplayForced = { false };

};

} // namespace Window
