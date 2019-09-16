/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "base/object_ptr.h"
#include "base/flags.h"

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

class BoxContent;

enum class LayerOption {
	CloseOther = (1 << 0),
	KeepOther = (1 << 1),
	ShowAfterOther = (1 << 2),
};
using LayerOptions = base::flags<LayerOption>;
inline constexpr auto is_flag_type(LayerOption) { return true; };

namespace Window {

class MainMenu;
class SessionController;
class SectionMemento;
struct SectionShow;

class LayerWidget : public Ui::RpWidget {
public:
	using RpWidget::RpWidget;

	virtual void parentResized() = 0;
	virtual void showFinished() {
	}
	void setInnerFocus();
	bool setClosing() {
		if (!_closing) {
			_closing = true;
			closeHook();
			return true;
		}
		return false;
	}

	bool overlaps(const QRect &globalRect);

	void setClosedCallback(Fn<void()> callback) {
		_closedCallback = std::move(callback);
	}
	void setResizedCallback(Fn<void()> callback) {
		_resizedCallback = std::move(callback);
	}
	virtual bool takeToThirdSection() {
		return false;
	}
	virtual bool showSectionInternal(
			not_null<SectionMemento*> memento,
			const SectionShow &params) {
		return false;
	}
	virtual bool closeByOutsideClick() const {
		return true;
	}

protected:
	void closeLayer() {
		if (const auto callback = base::take(_closedCallback)) {
			callback();
		}
	}
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	virtual void doSetInnerFocus() {
		setFocus();
	}
	virtual void closeHook() {
	}

private:
	bool _closing = false;
	Fn<void()> _closedCallback;
	Fn<void()> _resizedCallback;

};

class LayerStackWidget : public Ui::RpWidget {
public:
	LayerStackWidget(QWidget *parent);

	void finishAnimating();
	rpl::producer<> hideFinishEvents() const;

	void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated);
	void showSpecialLayer(
		object_ptr<LayerWidget> layer,
		anim::type animated);
	void showMainMenu(
		not_null<Window::SessionController*> controller,
		anim::type animated);
	bool takeToThirdSection();

	bool canSetFocus() const;
	void setInnerFocus();

	bool contentOverlapped(const QRect &globalRect);

	void hideSpecialLayer(anim::type animated);
	void hideLayers(anim::type animated);
	void hideAll(anim::type animated);
	void hideTopLayer(anim::type animated);
	void setHideByBackgroundClick(bool hide);
	void removeBodyCache();

	bool showSectionInternal(
		not_null<SectionMemento*> memento,
		const SectionShow &params);

	bool layerShown() const;

	~LayerStackWidget();

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void appendBox(
		object_ptr<BoxContent> box,
		anim::type animated);
	void prependBox(
		object_ptr<BoxContent> box,
		anim::type animated);
	void replaceBox(
		object_ptr<BoxContent> box,
		anim::type animated);
	void backgroundClicked();

	LayerWidget *pushBox(
		object_ptr<BoxContent> box,
		anim::type animated);
	void showFinished();
	void hideCurrent(anim::type animated);
	void closeLayer(not_null<LayerWidget*> layer);

	enum class Action {
		ShowMainMenu,
		ShowSpecialLayer,
		ShowLayer,
		HideSpecialLayer,
		HideLayer,
		HideAll,
	};
	template <typename SetupNew, typename ClearOld>
	void startAnimation(
		SetupNew setupNewWidgets,
		ClearOld clearOldWidgets,
		Action action,
		anim::type animated);

	void prepareForAnimation();
	void animationDone();

	void setCacheImages();
	void clearLayers();
	void clearSpecialLayer();
	void initChildLayer(LayerWidget *layer);
	void updateLayerBoxes();
	void fixOrder();
	void sendFakeMouseEvent();
	void clearClosingLayers();

	LayerWidget *currentLayer() {
		return _layers.empty() ? nullptr : _layers.back().get();
	}
	const LayerWidget *currentLayer() const {
		return const_cast<LayerStackWidget*>(this)->currentLayer();
	}

	std::vector<std::unique_ptr<LayerWidget>> _layers;
	std::vector<std::unique_ptr<LayerWidget>> _closingLayers;

	object_ptr<LayerWidget> _specialLayer = { nullptr };
	object_ptr<MainMenu> _mainMenu = { nullptr };

	class BackgroundWidget;
	object_ptr<BackgroundWidget> _background;
	bool _hideByBackgroundClick = true;

	rpl::event_stream<> _hideFinishStream;

};

} // namespace Window

class GenericBox;

template <typename BoxType = GenericBox, typename ...Args>
inline object_ptr<BoxType> Box(Args&&... args) {
	const auto parent = static_cast<QWidget*>(nullptr);
	return object_ptr<BoxType>(parent, std::forward<Args>(args)...);
}
