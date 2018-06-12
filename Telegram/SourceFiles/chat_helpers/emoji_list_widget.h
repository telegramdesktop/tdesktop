/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
