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

#include "layerwidget.h"
#include "ui/widgets/shadow.h"

namespace Ui {
class RoundButton;
class IconButton;
class ScrollArea;
class FlatLabel;
template <typename Widget>
class WidgetFadeWrap;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

class BoxLayerTitleShadow;

class BoxContentDelegate {
public:
	virtual Window::Controller *controller() const = 0;

	virtual void setLayerType(bool layerType) = 0;
	virtual void setTitle(base::lambda<TextWithEntities()> titleFactory) = 0;
	virtual void setAdditionalTitle(base::lambda<QString()> additionalFactory) = 0;

	virtual void clearButtons() = 0;
	virtual QPointer<Ui::RoundButton> addButton(base::lambda<QString()> textFactory, base::lambda<void()> clickCallback, const style::RoundButton &st) = 0;
	virtual QPointer<Ui::RoundButton> addLeftButton(base::lambda<QString()> textFactory, base::lambda<void()> clickCallback, const style::RoundButton &st) = 0;
	virtual void updateButtonsPositions() = 0;

	virtual void setDimensions(int newWidth, int maxHeight) = 0;
	virtual void setNoContentMargin(bool noContentMargin) = 0;
	virtual bool isBoxShown() const = 0;
	virtual void closeBox() = 0;

};

class BoxContent : public TWidget, protected base::Subscriber {
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

	void setTitle(base::lambda<QString()> titleFactory) {
		if (titleFactory) {
			getDelegate()->setTitle([titleFactory] { return TextWithEntities { titleFactory(), EntitiesInText() }; });
		} else {
			getDelegate()->setTitle(base::lambda<TextWithEntities()>());
		}
	}
	void setTitle(base::lambda<TextWithEntities()> titleFactory) {
		getDelegate()->setTitle(std::move(titleFactory));
	}
	void setAdditionalTitle(base::lambda<QString()> additional) {
		getDelegate()->setAdditionalTitle(std::move(additional));
	}

	void clearButtons() {
		getDelegate()->clearButtons();
	}
	QPointer<Ui::RoundButton> addButton(base::lambda<QString()> textFactory, base::lambda<void()> clickCallback);
	QPointer<Ui::RoundButton> addLeftButton(base::lambda<QString()> textFactory, base::lambda<void()> clickCallback);
	QPointer<Ui::RoundButton> addButton(base::lambda<QString()> textFactory, base::lambda<void()> clickCallback, const style::RoundButton &st) {
		return getDelegate()->addButton(std::move(textFactory), std::move(clickCallback), st);
	}
	void updateButtonsGeometry() {
		getDelegate()->updateButtonsPositions();
	}

	virtual void setInnerFocus() {
		setFocus();
	}

	base::Observable<void> boxClosing;

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

	Window::Controller *controller() {
		return getDelegate()->controller();
	}

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

	template <typename Widget>
	QPointer<Widget> setInnerWidget(object_ptr<Widget> inner, const style::ScrollArea &st, int topSkip = 0) {
		auto result = QPointer<Widget>(inner.data());
		setInnerTopSkip(topSkip);
		setInner(std::move(inner), st);
		return result;
	}

	template <typename Widget>
	QPointer<Widget> setInnerWidget(object_ptr<Widget> inner, int topSkip = 0) {
		auto result = QPointer<Widget>(inner.data());
		setInnerTopSkip(topSkip);
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

	BoxContentDelegate *getDelegate() const {
		Expects(_delegate != nullptr);
		return _delegate;
	}
	BoxContentDelegate *_delegate = nullptr;

	bool _preparing = false;
	bool _noContentMargin = false;
	int _innerTopSkip = 0;
	object_ptr<Ui::ScrollArea> _scroll = { nullptr };
	object_ptr<Ui::WidgetFadeWrap<BoxLayerTitleShadow>> _topShadow = { nullptr };
	object_ptr<Ui::WidgetFadeWrap<BoxLayerTitleShadow>> _bottomShadow = { nullptr };

	object_ptr<QTimer> _draggingScrollTimer = { nullptr };
	int _draggingScrollDelta = 0;

};

class AbstractBox : public LayerWidget, public BoxContentDelegate, protected base::Subscriber {
public:
	AbstractBox(QWidget *parent, Window::Controller *controller, object_ptr<BoxContent> content);

	Window::Controller *controller() const override {
		return _controller;
	}
	void parentResized() override;

	void setLayerType(bool layerType) override;
	void setTitle(base::lambda<TextWithEntities()> titleFactory) override;
	void setAdditionalTitle(base::lambda<QString()> additionalFactory) override;

	void clearButtons() override;
	QPointer<Ui::RoundButton> addButton(base::lambda<QString()> textFactory, base::lambda<void()> clickCallback, const style::RoundButton &st) override;
	QPointer<Ui::RoundButton> addLeftButton(base::lambda<QString()> textFactory, base::lambda<void()> clickCallback, const style::RoundButton &st) override;
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

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void doSetInnerFocus() override {
		_content->setInnerFocus();
	}
	void closeHook() override {
		_content->boxClosing.notify(true);
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

	Window::Controller *_controller = nullptr;
	int _fullHeight = 0;

	bool _noContentMargin = false;
	int _maxContentHeight = 0;
	object_ptr<BoxContent> _content;

	object_ptr<Ui::FlatLabel> _title = { nullptr };
	base::lambda<TextWithEntities()> _titleFactory;
	QString _additionalTitle;
	base::lambda<QString()> _additionalTitleFactory;
	int _titleLeft = 0;
	int _titleTop = 0;
	bool _layerType = false;

	std::vector<object_ptr<Ui::RoundButton>> _buttons;
	object_ptr<Ui::RoundButton> _leftButton = { nullptr };

};

class BoxLayerTitleShadow : public Ui::PlainShadow {
public:
	BoxLayerTitleShadow(QWidget *parent);

};

class BoxContentDivider : public TWidget {
public:
	BoxContentDivider(QWidget *parent);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

};

enum CreatingGroupType {
	CreatingGroupNone,
	CreatingGroupGroup,
	CreatingGroupChannel,
};
