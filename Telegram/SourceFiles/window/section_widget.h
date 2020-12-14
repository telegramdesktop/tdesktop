/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "dialogs/dialogs_key.h"
#include "media/player/media_player_float.h" // FloatSectionDelegate
#include "base/object_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class LayerWidget;
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
	, public Media::Player::FloatSectionDelegate
	, protected base::Subscriber {
public:
	AbstractSectionWidget(
		QWidget *parent,
		not_null<SessionController*> controller)
	: RpWidget(parent)
	, _controller(controller) {
	}

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<SessionController*> controller() const {
		return _controller;
	}

	// Tabbed selector management.
	virtual bool pushTabbedSelectorToThirdSection(
			not_null<PeerData*> peer,
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
		not_null<SessionController*> controller);

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

	virtual bool showMessage(
			PeerId peerId,
			const SectionShow &params,
			MsgId messageId) {
		return false;
	}

	virtual bool replyToMessage(not_null<HistoryItem*> item) {
		return false;
	}

	virtual bool preventsClose(Fn<void()> &&continueCallback) const {
		return false;
	}

	// Create a memento of that section to store it in the history stack.
	// This method may modify the section ("take" heavy items).
	virtual std::shared_ptr<SectionMemento> createMemento();

	void setInnerFocus() {
		doSetInnerFocus();
	}

	virtual rpl::producer<int> desiredHeight() const;

	// Some sections convert to layers on some geometry sizes.
	virtual object_ptr<Ui::LayerWidget> moveContentToLayer(
			QRect bodyGeometry) {
		return nullptr;
	}

	static void PaintBackground(
		not_null<SessionController*> controller,
		not_null<QWidget*> widget,
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

	bool animating() const {
		return _showAnimation != nullptr;
	}

	~SectionWidget();

private:
	void showFinished();

	std::unique_ptr<SlideAnimation> _showAnimation;

	// Saving here topDelta in setGeometryWithTopMoved() to get it passed to resizeEvent().
	int _topDelta = 0;

};

} // namespace Window
