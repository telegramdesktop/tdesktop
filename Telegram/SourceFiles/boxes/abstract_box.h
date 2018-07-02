/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/layer_widget.h"
#include "ui/rp_widget.h"

namespace style {
struct RoundButton;
struct ScrollArea;
} // namespace style

namespace Ui {
class RoundButton;
class IconButton;
class ScrollArea;
class FlatLabel;
class FadeShadow;
} // namespace Ui

class BoxContent;

class BoxContentDelegate {
public:
	virtual void setLayerType(bool layerType) = 0;
	virtual void setTitle(Fn<TextWithEntities()> titleFactory) = 0;
	virtual void setAdditionalTitle(Fn<QString()> additionalFactory) = 0;
	virtual void setCloseByOutsideClick(bool close) = 0;

	virtual void clearButtons() = 0;
	virtual QPointer<Ui::RoundButton> addButton(Fn<QString()> textFactory, Fn<void()> clickCallback, const style::RoundButton &st) = 0;
	virtual QPointer<Ui::RoundButton> addLeftButton(Fn<QString()> textFactory, Fn<void()> clickCallback, const style::RoundButton &st) = 0;
	virtual void updateButtonsPositions() = 0;

	virtual void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) = 0;
	virtual void setDimensions(int newWidth, int maxHeight) = 0;
	virtual void setNoContentMargin(bool noContentMargin) = 0;
	virtual bool isBoxShown() const = 0;
	virtual void closeBox() = 0;

	template <typename BoxType>
	QPointer<BoxType> show(
			object_ptr<BoxType> content,
			LayerOptions options = LayerOption::KeepOther,
			anim::type animated = anim::type::normal) {
		auto result = QPointer<BoxType>(content.data());
		showBox(std::move(content), options, animated);
		return result;
	}

};

class BoxContent : public Ui::RpWidget, protected base::Subscriber {
	Q_OBJECT

public:
	BoxContent() {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	bool isBoxShown() const {
		return getDelegate()->isBoxShown();
	}
	void closeBox() {
		getDelegate()->closeBox();
	}

	void setTitle(Fn<QString()> titleFactory) {
		if (titleFactory) {
			getDelegate()->setTitle([titleFactory] { return TextWithEntities { titleFactory(), EntitiesInText() }; });
		} else {
			getDelegate()->setTitle(Fn<TextWithEntities()>());
		}
	}
	void setTitle(Fn<TextWithEntities()> titleFactory) {
		getDelegate()->setTitle(std::move(titleFactory));
	}
	void setAdditionalTitle(Fn<QString()> additional) {
		getDelegate()->setAdditionalTitle(std::move(additional));
	}
	void setCloseByEscape(bool close) {
		_closeByEscape = close;
	}
	void setCloseByOutsideClick(bool close) {
		getDelegate()->setCloseByOutsideClick(close);
	}

	void scrollToWidget(not_null<QWidget*> widget);

	void clearButtons() {
		getDelegate()->clearButtons();
	}
	QPointer<Ui::RoundButton> addButton(Fn<QString()> textFactory, Fn<void()> clickCallback);
	QPointer<Ui::RoundButton> addLeftButton(Fn<QString()> textFactory, Fn<void()> clickCallback);
	QPointer<Ui::RoundButton> addButton(Fn<QString()> textFactory, Fn<void()> clickCallback, const style::RoundButton &st) {
		return getDelegate()->addButton(std::move(textFactory), std::move(clickCallback), st);
	}
	void updateButtonsGeometry() {
		getDelegate()->updateButtonsPositions();
	}

	virtual void setInnerFocus() {
		setFocus();
	}

	rpl::producer<> boxClosing() const {
		return _boxClosingStream.events();
	}
	void notifyBoxClosing() {
		_boxClosingStream.fire({});
	}

	void setDelegate(BoxContentDelegate *newDelegate) {
		_delegate = newDelegate;
		_preparing = true;
		prepare();
		finishPrepare();
	}

public slots:
	void onScrollToY(int top, int bottom = -1);

	void onDraggingScrollDelta(int delta);

protected:
	virtual void prepare() = 0;

	void setLayerType(bool layerType) {
		getDelegate()->setLayerType(layerType);
	}

	void setNoContentMargin(bool noContentMargin) {
		if (_noContentMargin != noContentMargin) {
			_noContentMargin = noContentMargin;
			setAttribute(Qt::WA_OpaquePaintEvent, !_noContentMargin);
		}
		getDelegate()->setNoContentMargin(noContentMargin);
	}
	void setDimensions(int newWidth, int maxHeight) {
		getDelegate()->setDimensions(newWidth, maxHeight);
	}
	void setInnerTopSkip(int topSkip, bool scrollBottomFixed = false);
	void setInnerBottomSkip(int bottomSkip);

	template <typename Widget>
	QPointer<Widget> setInnerWidget(
			object_ptr<Widget> inner,
			const style::ScrollArea &st,
			int topSkip = 0,
			int bottomSkip = 0) {
		auto result = QPointer<Widget>(inner.data());
		setInnerTopSkip(topSkip);
		setInnerBottomSkip(bottomSkip);
		setInner(std::move(inner), st);
		return result;
	}

	template <typename Widget>
	QPointer<Widget> setInnerWidget(
			object_ptr<Widget> inner,
			int topSkip = 0,
			int bottomSkip = 0) {
		auto result = QPointer<Widget>(inner.data());
		setInnerTopSkip(topSkip);
		setInnerBottomSkip(bottomSkip);
		setInner(std::move(inner));
		return result;
	}

	template <typename Widget>
	object_ptr<Widget> takeInnerWidget() {
		return static_object_cast<Widget>(doTakeInnerWidget());
	}

	void setInnerVisible(bool scrollAreaVisible);
	QPixmap grabInnerCache();

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	not_null<BoxContentDelegate*> getDelegate() const {
		return _delegate;
	}

private slots:
	void onScroll();
	void onInnerResize();

	void onDraggingScrollTimer();

private:
	void finishPrepare();
	void finishScrollCreate();
	void setInner(object_ptr<TWidget> inner);
	void setInner(object_ptr<TWidget> inner, const style::ScrollArea &st);
	void updateScrollAreaGeometry();
	void updateInnerVisibleTopBottom();
	void updateShadowsVisibility();
	object_ptr<TWidget> doTakeInnerWidget();

	BoxContentDelegate *_delegate = nullptr;

	bool _preparing = false;
	bool _noContentMargin = false;
	bool _closeByEscape = true;
	int _innerTopSkip = 0;
	int _innerBottomSkip = 0;
	object_ptr<Ui::ScrollArea> _scroll = { nullptr };
	object_ptr<Ui::FadeShadow> _topShadow = { nullptr };
	object_ptr<Ui::FadeShadow> _bottomShadow = { nullptr };

	object_ptr<QTimer> _draggingScrollTimer = { nullptr };
	int _draggingScrollDelta = 0;

	rpl::event_stream<> _boxClosingStream;

};

class AbstractBox
	: public Window::LayerWidget
	, public BoxContentDelegate
	, protected base::Subscriber {
public:
	AbstractBox(
		not_null<Window::LayerStackWidget*> layer,
		object_ptr<BoxContent> content);

	void parentResized() override;

	void setLayerType(bool layerType) override;
	void setTitle(Fn<TextWithEntities()> titleFactory) override;
	void setAdditionalTitle(Fn<QString()> additionalFactory) override;
	void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) override;

	void clearButtons() override;
	QPointer<Ui::RoundButton> addButton(Fn<QString()> textFactory, Fn<void()> clickCallback, const style::RoundButton &st) override;
	QPointer<Ui::RoundButton> addLeftButton(Fn<QString()> textFactory, Fn<void()> clickCallback, const style::RoundButton &st) override;
	void updateButtonsPositions() override;

	void setDimensions(int newWidth, int maxHeight) override;

	void setNoContentMargin(bool noContentMargin) override {
		if (_noContentMargin != noContentMargin) {
			_noContentMargin = noContentMargin;
			updateSize();
		}
	}

	bool isBoxShown() const override {
		return !isHidden();
	}
	void closeBox() override {
		closeLayer();
	}

	void setCloseByOutsideClick(bool close) override;
	bool closeByOutsideClick() const override;

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void doSetInnerFocus() override {
		_content->setInnerFocus();
	}
	void closeHook() override {
		_content->notifyBoxClosing();
	}

private:
	void paintAdditionalTitle(Painter &p);
	void updateTitlePosition();
	void refreshTitle();
	void refreshAdditionalTitle();
	void refreshLang();

	bool hasTitle() const;
	int titleHeight() const;
	int buttonsHeight() const;
	int buttonsTop() const;
	int contentTop() const;
	int countFullHeight() const;
	int countRealHeight() const;
	void updateSize();

	not_null<Window::LayerStackWidget*> _layer;
	int _fullHeight = 0;

	bool _noContentMargin = false;
	int _maxContentHeight = 0;
	object_ptr<BoxContent> _content;

	object_ptr<Ui::FlatLabel> _title = { nullptr };
	Fn<TextWithEntities()> _titleFactory;
	QString _additionalTitle;
	Fn<QString()> _additionalTitleFactory;
	int _titleLeft = 0;
	int _titleTop = 0;
	bool _layerType = false;
	bool _closeByOutsideClick = true;

	std::vector<object_ptr<Ui::RoundButton>> _buttons;
	object_ptr<Ui::RoundButton> _leftButton = { nullptr };

};

class BoxContentDivider : public Ui::RpWidget {
public:
	BoxContentDivider(QWidget *parent);
	BoxContentDivider(QWidget *parent, int height);

protected:
	void paintEvent(QPaintEvent *e) override;

};

enum CreatingGroupType {
	CreatingGroupNone,
	CreatingGroupGroup,
	CreatingGroupChannel,
};
