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

#include "chat_helpers/tabbed_selector.h"

namespace Window {
class Controller;
} // namespace Window

namespace ChatHelpers {

constexpr auto kEmojiSectionCount = 8;

class EmojiColorPicker : public TWidget {
	Q_OBJECT

public:
	EmojiColorPicker(QWidget *parent);

	void showEmoji(EmojiPtr emoji);

	void clearSelection();
	void handleMouseMove(QPoint globalPos);
	void handleMouseRelease(QPoint globalPos);
	void setSingleSize(QSize size);

	void hideFast();

public slots:
	void showAnimated();
	void hideAnimated();

signals:
	void emojiSelected(EmojiPtr emoji);
	void hidden();

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void animationCallback();
	void updateSize();

	void drawVariant(Painter &p, int variant);

	void updateSelected();
	void setSelected(int newSelected);

	bool _ignoreShow = false;

	QVector<EmojiPtr> _variants;

	int _selected = -1;
	int _pressedSel = -1;
	QPoint _lastMousePos;
	QSize _singleSize;

	bool _hiding = false;
	QPixmap _cache;
	Animation _a_opacity;

	QTimer _hideTimer;

};

class EmojiListWidget : public TabbedSelector::Inner {
	Q_OBJECT

public:
	EmojiListWidget(QWidget *parent, not_null<Window::Controller*> controller);

	using Section = Ui::Emoji::Section;

	void refreshRecent() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void showEmojiSection(Section section);
	Section currentSection(int yOffset) const;

public slots:
	void onShowPicker();
	void onPickerHidden();
	void onColorSelected(EmojiPtr emoji);

	bool checkPickerHide();

signals:
	void selected(EmojiPtr emoji);
	void switchToStickers();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;
	bool eventHook(QEvent *e) override;

	TabbedSelector::InnerFooter *getFooter() const override;
	void processHideFinished() override;
	int countDesiredHeight(int newWidth) override;

private:
	class Footer;

	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
	};

	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	SectionInfo sectionInfo(int section) const;
	SectionInfo sectionInfoByOffset(int yOffset) const;

	void ensureLoaded(int section);
	int countSectionTop(int section) const;
	void updateSelected();
	void setSelected(int newSelected);

	void selectEmoji(EmojiPtr emoji);

	QRect emojiRect(int section, int sel);

	Footer *_footer = nullptr;

	int _counts[kEmojiSectionCount];
	QVector<EmojiPtr> _emoji[kEmojiSectionCount];

	int _rowsLeft = 0;
	int _columnCount = 1;
	QSize _singleSize;
	int _esize = 0;

	int _selected = -1;
	int _pressedSel = -1;
	int _pickerSel = -1;
	QPoint _lastMousePos;

	object_ptr<EmojiColorPicker> _picker;
	QTimer _showPickerTimer;

};

} // namespace ChatHelpers
