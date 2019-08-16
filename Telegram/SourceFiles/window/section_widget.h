/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "dialogs/dialogs_key.h"

namespace Main {
class Session;
} // namespace Main

namespace Window {

class SessionController;
class LayerWidget;
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
	, protected base::Subscriber {
public:
	AbstractSectionWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: RpWidget(parent)
	, _controller(controller) {
	}

	[[nodiscard]] Main::Session &session() const;

	// Tabbed selector management.
	virtual void pushTabbedSelectorToThirdSection(
		const Window::SectionShow &params) {
	}
	virtual bool returnTabbedSelector() {
		return false;
	}

	// Float player interface.
	virtual bool wheelEventFromFloatPlayer(QEvent *e) {
		return false;
	}
	[[nodiscard]] virtual QRect rectForFloatPlayer() const {
		return mapToGlobal(rect());
	}

protected:
	[[nodiscard]] not_null<Window::SessionController*> controller() const {
		return _controller;
	}

private:
	const not_null<Window::SessionController*> _controller;

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
	SectionWidget(QWidget *parent, not_null<Window::SessionController*> controller);

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
	virtual QPixmap grabForShowAnimation(const SectionSlideParams &params) {
		return Ui::GrabWidget(this);
	}

	// Attempt to show the required section inside the existing one.
	// For example if this section already shows exactly the required
	// memento it can simply return true - it is shown already.
	//
	// If this method returns false it is not supposed to modify the memento.
	// If this method returns true it may modify the memento ("take" heavy items).
	virtual bool showInternal(
		not_null<SectionMemento*> memento,
		const SectionShow &params) = 0;

	// Create a memento of that section to store it in the history stack.
	// This method may modify the section ("take" heavy items).
	virtual std::unique_ptr<SectionMemento> createMemento();

	void setInnerFocus() {
		doSetInnerFocus();
	}

	virtual rpl::producer<int> desiredHeight() const;

	// Some sections convert to layers on some geometry sizes.
	virtual object_ptr<LayerWidget> moveContentToLayer(
			QRect bodyGeometry) {
		return nullptr;
	}

	static void PaintBackground(not_null<QWidget*> widget, QRect clip);

protected:
	void paintEvent(QPaintEvent *e) override;

	// Temp variable used in resizeEvent() implementation, that is passed
	// to setGeometryWithTopMoved() to adjust the scroll position with the resize.
	int topDelta() const {
		return _topDelta;
	}

	// Called after the hideChildren() call in showAnimated().
	virtual void showAnimatedHook(
		const Window::SectionSlideParams &params) {
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
