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

#include "stickers/emoji_panel.h"

namespace ChatHelpers {

class GifsListWidget : public EmojiPanel::Inner, public InlineBots::Layout::Context, private base::Subscriber {
	Q_OBJECT

public:
	GifsListWidget(QWidget *parent);

	void refreshRecent() override;
	void preloadImages() override;
	void hideFinish(bool completely) override;
	void clearSelection() override;
	object_ptr<TWidget> createController() override;

	void refreshSavedGifs();
	int refreshInlineRows(UserData *bot, const InlineCacheEntry *results, bool resultsDeleted);
	void inlineBotChanged();
	void hideInlineRowsPanel();
	void clearInlineRowsPanel();

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void inlineItemLayoutChanged(const InlineItem *layout) override;
	void inlineItemRepaint(const InlineItem *layout) override;
	bool inlineItemVisible(const InlineItem *layout) override;

	~GifsListWidget();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;
	int countHeight() override;

private slots:
	void onPreview();
	void onUpdateInlineItems();
	void onSwitchPm();

signals:
	void selected(DocumentData *sticker);
	void selected(PhotoData *photo);
	void selected(InlineBots::Result *result, UserData *bot);

	void emptyInlineRows();
	void scrollUpdated();

private:
	enum class Section {
		Inlines,
		Gifs,
	};
	class Controller;

	void updateSelected();
	void paintInlineItems(Painter &p, QRect clip);
	void refreshSwitchPmButton(const InlineCacheEntry *entry);

	Section _section = Section::Gifs;
	UserData *_inlineBot;
	QString _inlineBotTitle;
	TimeMs _lastScrolled = 0;
	QTimer _updateInlineItems;
	bool _inlineWithThumb = false;

	object_ptr<Ui::RoundButton> _switchPmButton = { nullptr };
	QString _switchPmStartToken;

	typedef QVector<InlineItem*> InlineItems;
	struct InlineRow {
		int height = 0;
		InlineItems items;
	};
	typedef QVector<InlineRow> InlineRows;
	InlineRows _inlineRows;
	void clearInlineRows(bool resultsDeleted);

	std::map<DocumentData*, std::unique_ptr<InlineItem>> _gifLayouts;
	InlineItem *layoutPrepareSavedGif(DocumentData *doc, int32 position);

	std::map<InlineResult*, std::unique_ptr<InlineItem>> _inlineLayouts;
	InlineItem *layoutPrepareInlineResult(InlineResult *result, int32 position);

	bool inlineRowsAddItem(DocumentData *savedGif, InlineResult *result, InlineRow &row, int32 &sumWidth);
	bool inlineRowFinalize(InlineRow &row, int32 &sumWidth, bool force = false);

	InlineRow &layoutInlineRow(InlineRow &row, int32 sumWidth = 0);
	void deleteUnusedGifLayouts();

	void deleteUnusedInlineLayouts();

	int validateExistingInlineRows(const InlineResults &results);
	void selectInlineResult(int row, int column);

	int _selected = -1;
	int _pressed = -1;
	QPoint _lastMousePos;

	QTimer _previewTimer;
	bool _previewShown = false;

};

} // namespace ChatHelpers
