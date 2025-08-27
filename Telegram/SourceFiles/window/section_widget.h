/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "chat_helpers/bot_command.h"
#include "dialogs/dialogs_key.h"
#include "media/player/media_player_float.h" // FloatSectionDelegate
#include "base/object_ptr.h"
#include "window/window_section_common.h"

class PeerData;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct ReactionId;
class ForumTopic;
class WallPaper;
class Session;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class LayerWidget;
class ChatTheme;
} // namespace Ui

namespace Window {

class SessionController;
class SlideAnimation;
struct SectionShow;
enum class SlideDirection;

enum class Column {
	First,
	Second,
	Third,
};

class AbstractSectionWidget
	: public Ui::RpWidget
	, public Media::Player::FloatSectionDelegate {
public:
	AbstractSectionWidget(
		QWidget *parent,
		not_null<SessionController*> controller,
		rpl::producer<PeerData*> peerForBackground);

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<SessionController*> controller() const {
		return _controller;
	}

	// Tabbed selector management.
	virtual bool pushTabbedSelectorToThirdSection(
			not_null<Data::Thread*> thread,
			const SectionShow &params) {
		return false;
	}
	virtual bool returnTabbedSelector() {
		return false;
	}

private:
	const not_null<SessionController*> _controller;

};

class SectionMemento;

struct SectionSlideParams {
	QPixmap oldContentCache;
	int topSkip = 0;
	QPixmap topMask;
	bool withTopBarShadow = false;
	bool withTabs = false;
	bool withFade = false;

	explicit operator bool() const {
		return !oldContentCache.isNull();
	}
};

class SectionWidget : public AbstractSectionWidget {
public:
	SectionWidget(
		QWidget *parent,
		not_null<SessionController*> controller,
		rpl::producer<PeerData*> peerForBackground = nullptr);
	SectionWidget(
		QWidget *parent,
		not_null<SessionController*> controller,
		not_null<PeerData*> peerForBackground);

	virtual Dialogs::RowDescriptor activeChat() const {
		return {};
	}

	// When resizing the widget with top edge moved up or down and we
	// want to add this top movement to the scroll position, so inner
	// content will not move.
	void setGeometryWithTopMoved(const QRect &newGeometry, int topDelta);

	virtual bool hasTopBarShadow() const {
		return false;
	}
	virtual bool forceAnimateBack() const {
		return false;
	}
	void showAnimated(
		SlideDirection direction,
		const SectionSlideParams &params);
	void showFast();
	[[nodiscard]] bool animatingShow() const;

	// This can be used to grab with or without top bar shadow.
	// This will be protected when animation preparation will be done inside.
	virtual QPixmap grabForShowAnimation(const SectionSlideParams &params);

	// Attempt to show the required section inside the existing one.
	// For example if this section already shows exactly the required
	// memento it can simply return true - it is shown already.
	//
	// If this method returns false it is not supposed to modify the memento.
	// If this method returns true it may modify the memento ("take" heavy items).
	virtual bool showInternal(
		not_null<SectionMemento*> memento,
		const SectionShow &params) = 0;
	virtual bool sameTypeAs(not_null<SectionMemento*> memento) {
		return false;
	}

	virtual bool showMessage(
			PeerId peerId,
			const SectionShow &params,
			MsgId messageId) {
		return false;
	}
	virtual bool searchInChatEmbedded(
			QString query,
			Dialogs::Key chat,
			PeerData *searchFrom = nullptr) {
		return false;
	}

	[[nodiscard]] virtual bool preventsClose(
			Fn<void()> &&continueCallback) const {
		return false;
	}

	// Send bot command from peer info or media viewer.
	virtual SectionActionResult sendBotCommand(
			Bot::SendCommandRequest request) {
		return SectionActionResult::Ignore;
	}

	virtual bool confirmSendingFiles(const QStringList &files) {
		return false;
	}
	virtual bool confirmSendingFiles(not_null<const QMimeData*> data) {
		return false;
	}

	// Create a memento of that section to store it in the history stack.
	// This method may modify the section ("take" heavy items).
	virtual std::shared_ptr<SectionMemento> createMemento();

	void setInnerFocus() {
		doSetInnerFocus();
	}
	virtual void checkActivation() {
	}

	[[nodiscard]] virtual rpl::producer<int> desiredHeight() const;
	[[nodiscard]] virtual rpl::producer<> removeRequests() const {
		return rpl::never<>();
	}

	// Some sections convert to layers on some geometry sizes.
	[[nodiscard]] virtual object_ptr<Ui::LayerWidget> moveContentToLayer(
			QRect bodyGeometry) {
		return nullptr;
	}

	virtual void validateSubsectionTabs() {
	}

	static void PaintBackground(
		not_null<SessionController*> controller,
		not_null<Ui::ChatTheme*> theme,
		not_null<QWidget*> widget,
		QRect clip);
	static void PaintBackground(
		not_null<Ui::ChatTheme*> theme,
		not_null<QWidget*> widget,
		int fillHeight,
		int fromy,
		QRect clip);
	static void PaintBackground(
		QPainter &p,
		not_null<Ui::ChatTheme*> theme,
		QSize fill,
		QRect clip);

protected:
	void paintEvent(QPaintEvent *e) override;

	// Temp variable used in resizeEvent() implementation, that is passed
	// to setGeometryWithTopMoved() to adjust the scroll position with the resize.
	int topDelta() const {
		return _topDelta;
	}

	// Called after the hideChildren() call in showAnimated().
	virtual void showAnimatedHook(
		const SectionSlideParams &params) {
	}

	// Called after the showChildren() call in showFinished().
	virtual void showFinishedHook() {
	}

	virtual void doSetInnerFocus() {
		setFocus();
	}

	~SectionWidget();

private:
	void showFinished();

	std::unique_ptr<SlideAnimation> _showAnimation;

	// Saving here topDelta in setGeometryWithTopMoved() to get it passed to resizeEvent().
	int _topDelta = 0;

};

[[nodiscard]] auto ChatThemeValueFromPeer(
	not_null<SessionController*> controller,
	not_null<PeerData*> peer)
-> rpl::producer<std::shared_ptr<Ui::ChatTheme>>;

[[nodiscard]] bool ShowSendPremiumError(
	not_null<SessionController*> controller,
	not_null<DocumentData*> document);
[[nodiscard]] bool ShowSendPremiumError(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<DocumentData*> document);

[[nodiscard]] bool ShowReactPremiumError(
	not_null<SessionController*> controller,
	not_null<HistoryItem*> item,
	const Data::ReactionId &id);

[[nodiscard]] rpl::producer<const Data::WallPaper*> WallPaperResolved(
	not_null<Data::Session*> owner,
	const Data::WallPaper *paper);

} // namespace Window
