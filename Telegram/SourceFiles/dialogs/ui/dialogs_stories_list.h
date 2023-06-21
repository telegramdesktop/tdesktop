/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/qt/qt_compare.h"
#include "base/weak_ptr.h"
#include "ui/rp_widget.h"

class QPainter;

namespace style {
struct DialogsStories;
struct DialogsStoriesList;
} // namespace style

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Dialogs::Stories {

class Thumbnail {
public:
	[[nodiscard]] virtual QImage image(int size) = 0;
	virtual void subscribeToUpdates(Fn<void()> callback) = 0;
};

struct Element {
	uint64 id = 0;
	QString name;
	std::shared_ptr<Thumbnail> thumbnail;
	bool unread : 1 = false;
	bool suggestHide : 1 = false;
	bool suggestUnhide : 1 = false;
	bool profile : 1 = false;
	bool skipSmall : 1 = false;

	friend inline bool operator==(
		const Element &a,
		const Element &b) = default;
};

struct Content {
	std::vector<Element> elements;
	bool hidden = false;

	friend inline bool operator==(
		const Content &a,
		const Content &b) = default;
};

struct ToggleShownRequest {
	uint64 id = 0;
	bool shown = false;
};

class List final : public Ui::RpWidget {
public:
	List(
		not_null<QWidget*> parent,
		const style::DialogsStoriesList &st,
		rpl::producer<Content> content,
		Fn<int()> shownHeight);

	[[nodiscard]] rpl::producer<uint64> clicks() const;
	[[nodiscard]] rpl::producer<uint64> showProfileRequests() const;
	[[nodiscard]] rpl::producer<ToggleShownRequest> toggleShown() const;
	[[nodiscard]] rpl::producer<> expandRequests() const;
	[[nodiscard]] rpl::producer<> entered() const;
	[[nodiscard]] rpl::producer<> loadMoreRequests() const;

private:
	struct Layout;
	struct Item {
		Element element;
		QImage nameCache;
		QColor nameCacheColor;
		bool subscribed = false;
	};
	struct Summary {
		QString string;
		Ui::Text::String text;
		int available = 0;
		QImage cache;
		QColor cacheColor;
		int cacheForWidth = 0;

		[[nodiscard]] bool empty() const {
			return string.isEmpty();
		}
	};
	struct Summaries {
		Summary total;
		Summary allNames;
		Summary unreadNames;
		bool skipOne = false;
	};
	struct Data {
		std::vector<Item> items;
		Summaries summaries;

		[[nodiscard]] bool empty() const {
			return items.empty();
		}
	};

	[[nodiscard]] static Summaries ComposeSummaries(Data &data);
	[[nodiscard]] static bool StringsEqual(
		const Summaries &a,
		const Summaries &b);
	static void Populate(
		const style::DialogsStories &st,
		Summary &summary);
	static void Populate(
		const style::DialogsStories &st,
		Summaries &summaries);
	[[nodiscard]] static Summary &ChooseSummary(
		const style::DialogsStories &st,
		Summaries &summaries,
		int totalItems,
		int fullWidth);
	static void PrerenderSummary(
		const style::DialogsStories &st,
		Summary &summary);

	void showContent(Content &&content);
	void enterEventHook(QEnterEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	void validateThumbnail(not_null<Item*> item);
	void validateName(not_null<Item*> item);
	void updateScrollMax();
	void updateSummary(Data &data);
	void updateSelected();
	void checkDragging();
	bool finishDragging();
	void checkLoadMore();

	void updateHeight();
	void toggleAnimated(bool shown);
	void paintSummary(
		QPainter &p,
		Data &data,
		float64 summaryTop,
		float64 hidden);

	[[nodiscard]] Layout computeLayout() const;

	const style::DialogsStoriesList &_st;
	Content _content;
	Data _data;
	Data _hidingData;
	Fn<int()> _shownHeight = 0;
	rpl::event_stream<uint64> _clicks;
	rpl::event_stream<uint64> _showProfileRequests;
	rpl::event_stream<ToggleShownRequest> _toggleShown;
	rpl::event_stream<> _expandRequests;
	rpl::event_stream<> _entered;
	rpl::event_stream<> _loadMoreRequests;

	Ui::Animations::Simple _shownAnimation;

	QPoint _lastMousePosition;
	std::optional<QPoint> _mouseDownPosition;
	int _startDraggingLeft = 0;
	int _scrollLeft = 0;
	int _scrollLeftMax = 0;
	bool _dragging = false;

	int _selected = -1;
	int _pressed = -1;

	base::unique_qptr<Ui::PopupMenu> _menu;
	base::has_weak_ptr _menuGuard;

};

} // namespace Dialogs::Stories
