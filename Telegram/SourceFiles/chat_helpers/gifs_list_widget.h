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
#include "inline_bots/inline_bot_layout_item.h"

namespace InlineBots {
namespace Layout {
class ItemBase;
} // namespace Layout
class Result;
} // namespace InlineBots

namespace Ui {
class RoundButton;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace ChatHelpers {

class GifsListWidget
	: public TabbedSelector::Inner
	, public InlineBots::Layout::Context
	, private base::Subscriber
	, private MTP::Sender {
	Q_OBJECT

public:
	GifsListWidget(QWidget *parent, not_null<Window::Controller*> controller);

	void refreshRecent() override;
	void preloadImages() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout) override;
	void inlineItemRepaint(const InlineBots::Layout::ItemBase *layout) override;
	bool inlineItemVisible(const InlineBots::Layout::ItemBase *layout) override;

	void afterShown() override;
	void beforeHiding() override;

	void setInlineQueryPeer(PeerData *peer) {
		_inlineQueryPeer = peer;
	}
	void searchForGifs(const QString &query);
	void sendInlineRequest();

	~GifsListWidget();

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

	TabbedSelector::InnerFooter *getFooter() const override;
	void processHideFinished() override;
	void processPanelHideFinished() override;
	int countDesiredHeight(int newWidth) override;

private slots:
	void onPreview();
	void onUpdateInlineItems();

signals:
	void selected(DocumentData *sticker);
	void selected(PhotoData *photo);
	void selected(InlineBots::Result *result, UserData *bot);
	void cancelled();

	void emptyInlineRows();
	void scrollUpdated();

private:
	enum class Section {
		Inlines,
		Gifs,
	};
	class Footer;

	using InlineResult = InlineBots::Result;
	using InlineResults = std::vector<std::unique_ptr<InlineResult>>;
	using LayoutItem = InlineBots::Layout::ItemBase;

	struct InlineCacheEntry {
		QString nextOffset;
		InlineResults results;
	};

	void cancelGifsSearch();
	void switchToSavedGifs();
	void refreshSavedGifs();
	int refreshInlineRows(const InlineCacheEntry *results, bool resultsDeleted);
	void checkLoadMore();

	int32 showInlineRows(bool newResults);
	bool refreshInlineRows(int32 *added = 0);
	void inlineResultsDone(const MTPmessages_BotResults &result);

	void updateSelected();
	void paintInlineItems(Painter &p, QRect clip);

	Section _section = Section::Gifs;
	TimeMs _lastScrolled = 0;
	QTimer _updateInlineItems;
	bool _inlineWithThumb = false;

	struct Row {
		int maxWidth = 0;
		int height = 0;
		QVector<LayoutItem*> items;
	};
	QVector<Row> _rows;
	void clearInlineRows(bool resultsDeleted);

	std::map<DocumentData*, std::unique_ptr<LayoutItem>> _gifLayouts;
	LayoutItem *layoutPrepareSavedGif(DocumentData *doc, int32 position);

	std::map<InlineResult*, std::unique_ptr<LayoutItem>> _inlineLayouts;
	LayoutItem *layoutPrepareInlineResult(InlineResult *result, int32 position);

	bool inlineRowsAddItem(DocumentData *savedGif, InlineResult *result, Row &row, int32 &sumWidth);
	bool inlineRowFinalize(Row &row, int32 &sumWidth, bool force = false);

	void layoutInlineRow(Row &row, int fullWidth);
	void deleteUnusedGifLayouts();

	void deleteUnusedInlineLayouts();

	int validateExistingInlineRows(const InlineResults &results);
	void selectInlineResult(int row, int column);

	Footer *_footer = nullptr;

	int _selected = -1;
	int _pressed = -1;
	QPoint _lastMousePos;

	QTimer _previewTimer;
	bool _previewShown = false;

	std::map<QString, std::unique_ptr<InlineCacheEntry>> _inlineCache;
	QTimer _inlineRequestTimer;

	UserData *_searchBot = nullptr;
	mtpRequestId _searchBotRequestId = 0;
	PeerData *_inlineQueryPeer = nullptr;
	QString _inlineQuery, _inlineNextQuery, _inlineNextOffset;
	mtpRequestId _inlineRequestId = 0;

};

} // namespace ChatHelpers
