/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/variable.h>
#include "base/flags.h"
#include "dialogs/dialogs_key.h"

class MainWidget;
class HistoryMessage;
class HistoryService;

namespace Media {
namespace Player {
class RoundController;
} // namespace Player
} // namespace Media

namespace Passport {
struct FormRequest;
class FormController;
} // namespace Passport

namespace Window {

class LayerWidget;
class MainWindow;
class SectionMemento;

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

class DateClickHandler : public ClickHandler {
public:
	DateClickHandler(Dialogs::Key chat, QDate date);

	void setDate(QDate date);
	void onClick(Qt::MouseButton) const override;

private:
	Dialogs::Key _chat;
	QDate _date;

};

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
	SectionShow withThirdColumn() const {
		auto copy = *this;
		copy.thirdColumn = true;
		return copy;
	}

	Way way = Way::Forward;
	anim::type animated = anim::type::normal;
	anim::activation activation = anim::activation::normal;
	bool thirdColumn = false;

};

class Controller;

class Navigation {
public:
	virtual void showSection(
		SectionMemento &&memento,
		const SectionShow &params = SectionShow()) = 0;
	virtual void showBackFromStack(
		const SectionShow &params = SectionShow()) = 0;
	virtual not_null<Controller*> parentController() = 0;

	void showPeerInfo(
		PeerId peerId,
		const SectionShow &params = SectionShow());
	void showPeerInfo(
		not_null<PeerData*> peer,
		const SectionShow &params = SectionShow());
	void showPeerInfo(
		not_null<History*> history,
		const SectionShow &params = SectionShow());

	virtual ~Navigation() = default;

};

class Controller : public Navigation {
public:
	Controller(not_null<MainWindow*> window);

	not_null<MainWindow*> window() const {
		return _window;
	}

	// This is needed for History TopBar updating when searchInChat
	// is changed in the DialogsWidget of the current window.
	rpl::variable<Dialogs::Key> searchInChat;

	void setActiveChatEntry(Dialogs::RowDescriptor row);
	void setActiveChatEntry(Dialogs::Key key);
	Dialogs::RowDescriptor activeChatEntryCurrent() const;
	Dialogs::Key activeChatCurrent() const;
	rpl::producer<Dialogs::RowDescriptor> activeChatEntryChanges() const;
	rpl::producer<Dialogs::Key> activeChatChanges() const;
	rpl::producer<Dialogs::RowDescriptor> activeChatEntryValue() const;
	rpl::producer<Dialogs::Key> activeChatValue() const;

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
		const SectionShow &params = SectionShow()) override;
	void showBackFromStack(
		const SectionShow &params = SectionShow()) override;

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
		Dialogs::Key chat,
		QDate requestedDate);

	void showPassportForm(const Passport::FormRequest &request);
	void clearPassportForm();

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

	not_null<Controller*> parentController() override {
		return this;
	}

	using RoundController = Media::Player::RoundController;
	bool startRoundVideo(not_null<HistoryItem*> context);
	RoundController *currentRoundVideo() const;
	RoundController *roundVideo(not_null<const HistoryItem*> context) const;
	RoundController *roundVideo(FullMsgId contextId) const;
	void roundVideoFinished(not_null<RoundController*> video);

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

	~Controller();

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

	std::unique_ptr<Passport::FormController> _passportForm;

	GifPauseReasons _gifPauseReasons = 0;
	base::Observable<void> _gifPauseLevelChanged;
	base::Observable<void> _floatPlayerAreaUpdated;

	rpl::variable<Dialogs::RowDescriptor> _activeChatEntry;
	base::Variable<bool> _dialogsListFocused = { false };
	base::Variable<bool> _dialogsListDisplayForced = { false };

	std::unique_ptr<RoundController> _roundVideo;

	rpl::lifetime _lifetime;

};

} // namespace Window
