/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/qt/qt_compare.h"
#include "ui/rp_widget.h"

class QPainter;

namespace Dialogs::Stories {

class Userpic {
public:
	[[nodiscard]] virtual QImage image(int size) = 0;
	virtual void subscribeToUpdates(Fn<void()> callback) = 0;
};

struct User {
	uint64 id = 0;
	QString name;
	std::shared_ptr<Userpic> userpic;
	bool unread = false;

	friend inline bool operator==(const User &a, const User &b) = default;
};

struct Content {
	std::vector<User> users;

	friend inline bool operator==(
		const Content &a,
		const Content &b) = default;
};

class List final : public Ui::RpWidget {
public:
	List(
		not_null<QWidget*> parent,
		rpl::producer<Content> content,
		Fn<int()> shownHeight);

	[[nodiscard]] rpl::producer<uint64> clicks() const;
	[[nodiscard]] rpl::producer<> expandRequests() const;
	[[nodiscard]] rpl::producer<> entered() const;

private:
	struct Item {
		User user;
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
	static void Populate(Summary &summary);
	static void Populate(Summaries &summaries);
	[[nodiscard]] static Summary &ChooseSummary(
		Summaries &summaries,
		int totalItems,
		int fullWidth);
	static void PrerenderSummary(Summary &summary);

	void showContent(Content &&content);
	void enterEventHook(QEnterEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void validateUserpic(not_null<Item*> item);
	void validateName(not_null<Item*> item);
	void updateScrollMax();
	void updateSummary(Data &data);
	void checkDragging();
	bool finishDragging();

	void updateHeight();
	void toggleAnimated(bool shown);
	void paintSummary(
		QPainter &p,
		Data &data,
		float64 summaryTop,
		float64 hidden);

	Content _content;
	Data _data;
	Data _hidingData;
	Fn<int()> _shownHeight = 0;
	rpl::event_stream<uint64> _clicks;
	rpl::event_stream<> _expandRequests;
	rpl::event_stream<> _entered;

	Ui::Animations::Simple _shownAnimation;

	QPoint _lastMousePosition;
	std::optional<QPoint> _mouseDownPosition;
	int _startDraggingLeft = 0;
	int _scrollLeft = 0;
	int _scrollLeftMax = 0;
	bool _dragging = false;

};

} // namespace Dialogs::Stories
