/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/qt/qt_compare.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/rp_widget.h"

class QPainter;

namespace style {
struct DialogsStories;
struct DialogsStoriesList;
} // namespace style

namespace Ui {
class PopupMenu;
class DynamicImage;
struct OutlineSegment;
class ImportantTooltip;
} // namespace Ui

namespace Dialogs::Stories {

struct Element {
	uint64 id = 0;
	QString name;
	std::shared_ptr<Ui::DynamicImage> thumbnail;
	uint32 count : 15 = 0;
	uint32 unreadCount : 15 = 0;
	uint32 hasVideoStream : 1 = 0;
	uint32 skipSmall : 1 = 0;
	uint32 mine : 1 = 0;
	uint32 mineUnread : 1 = 0;
	uint32 uploading : 1 = 0;
	uint32 uploadFailed : 1 = 0;
	uint32 uploadProgress : 7 = 0;

	friend inline bool operator==(
		const Element &a,
		const Element &b) = default;
};

struct Content {
	std::vector<Element> elements;
	int total = 0;

	friend inline bool operator==(
		const Content &a,
		const Content &b) = default;
};

struct ShowMenuRequest {
	uint64 id = 0;
	Ui::Menu::MenuCallback callback;
};

class List final : public Ui::RpWidget {
public:
	List(
		not_null<QWidget*> parent,
		const style::DialogsStoriesList &st,
		rpl::producer<Content> content);
	~List();

	void setExpandedHeight(int height, bool momentum = false);
	void setLayoutConstraints(
		QPoint positionSmall,
		style::align alignSmall,
		QRect geometryFull = QRect());
	void setShowTooltip(
		not_null<Ui::RpWidget*> tooltipParent,
		rpl::producer<bool> shown,
		Fn<void()> hide);
	void raiseTooltip();

	struct CollapsedGeometry {
		QRect geometry;
		float64 expanded = 0.;
		float64 singleWidth = 0.;
	};
	[[nodiscard]] CollapsedGeometry collapsedGeometryCurrent() const;
	[[nodiscard]] rpl::producer<> collapsedGeometryChanged() const;

	[[nodiscard]] bool empty() const {
		return _empty.current();
	}
	[[nodiscard]] rpl::producer<bool> emptyValue() const {
		return _empty.value();
	}
	void setShowTitle(bool shown);
	[[nodiscard]] rpl::producer<uint64> clicks() const;
	[[nodiscard]] rpl::producer<uint64> addStoryClicks() const;
	[[nodiscard]] rpl::producer<ShowMenuRequest> showMenuRequests() const;
	[[nodiscard]] rpl::producer<bool> toggleExpandedRequests() const;
	//[[nodiscard]] rpl::producer<> entered() const;
	[[nodiscard]] rpl::producer<> loadMoreRequests() const;

	[[nodiscard]] auto verticalScrollEvents() const
		-> rpl::producer<not_null<QWheelEvent*>>;

	[[nodiscard]] bool toggledHidden() const;
	void setToggledHidden(bool hiddenInstant, bool hiddenAnimated);
	void updateFromParentDrag(QPoint globalPos);
	void dragLeft();
	[[nodiscard]] uint64 finishDropAt(QPoint globalPos);

private:
	struct Layout;
	enum class State {
		Small,
		Changing,
		Full,
	};
	struct Item {
		Element element;
		QImage nameCache;
		QColor nameCacheColor;
		std::vector<Ui::OutlineSegment> segments;
		bool subscribed = false;
	};
	struct Data {
		std::vector<Item> items;

		[[nodiscard]] bool empty() const {
			return items.empty();
		}
	};

	void showContent(Content &&content);
	void paintAddBadge(QPainter &p, uint64 id, const QRectF &userpic) const;
	void updateUploadAnimation();
	void stepUploadAnimation(crl::time now);
	[[nodiscard]] float64 uploadShownProgress(uint64 id, crl::time now) const;
	[[nodiscard]] float64 finishMorphProgress(uint64 id, crl::time now) const;
	[[nodiscard]] float64 failFadeProgress(uint64 id, crl::time now) const;
	[[nodiscard]] float64 badgeFadeProgress(uint64 id, crl::time now) const;
	void validateTitle();
	//void enterEventHook(QEnterEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	void paint(
		QPainter &p,
		const Layout &layout,
		float64 photo,
		float64 line,
		bool layered);
	void ensureLayer();
	void validateThumbnail(not_null<Item*> item);
	void validateName(not_null<Item*> item);
	void updateScrollMax();
	void updateSelected();
	void checkDragging();
	bool finishDragging();
	void checkLoadMore();
	void requestExpanded(bool expanded);

	void updateTooltipGeometry();
	[[nodiscard]] TextWithEntities computeTooltipText() const;
	void toggleTooltip(bool fast);
	[[nodiscard]] bool overMineElement();

	bool checkForFullState();
	void setState(State state);
	void updateGeometry();
	[[nodiscard]] QRect countSmallGeometry() const;
	void updateExpanding();
	void updateExpanding(int expandingHeight, int expandedHeight);
	void validateSegments(
		not_null<Item*> item,
		const QBrush &brush,
		float64 line,
		bool forUnread);

	[[nodiscard]] Layout computeLayout();
	[[nodiscard]] Layout computeLayout(float64 expanded) const;

	const style::DialogsStoriesList &_st;
	Content _content;
	Data _data;
	rpl::event_stream<uint64> _clicks;
	rpl::event_stream<uint64> _addStoryClicks;
	mutable std::optional<std::pair<uint64, QRectF>> _addBadge;
	rpl::event_stream<ShowMenuRequest> _showMenuRequests;
	rpl::event_stream<bool> _toggleExpandedRequests;
	//rpl::event_stream<> _entered;
	rpl::event_stream<> _loadMoreRequests;
	rpl::event_stream<> _collapsedGeometryChanged;

	QImage _layer;
	QPoint _positionSmall;
	style::align _alignSmall = {};
	QRect _geometryFull;
	QRect _changingGeometryFrom;
	State _state = State::Small;
	rpl::variable<bool> _empty = true;
	QString _title;
	int _titleWidth = 0;
	bool _showTitle = false;

	QPoint _lastMousePosition;
	std::optional<QPoint> _mouseDownPosition;
	int _startDraggingLeft = 0;
	int _scrollLeft = 0;
	int _scrollLeftMax = 0;
	bool _dragging = false;
	Qt::Orientation _scrollingLock = {};

	Ui::Animations::Simple _expandedAnimation;
	Ui::Animations::Simple _expandCatchUpAnimation;
	Ui::Animations::Simple _hiddenAnimation;

	struct UploadProgress {
		float64 from = 0.;
		float64 to = 0.;
		crl::time started = 0;
		bool finishing = false;
		float64 startFrom = 0.;
	};
	base::flat_map<uint64, UploadProgress> _uploadProgress;
	base::flat_map<uint64, crl::time> _failFade;
	base::flat_map<uint64, crl::time> _badgeFade;
	Ui::Animations::Basic _uploadAnimation;
	crl::time _uploadStarted = 0;
	float64 _lastRatio = 0.;
	int _lastExpandedHeight = 0;
	bool _hiddenAnimated : 1 = false;
	bool _hiddenInstant : 1 = false;
	bool _expandIgnored : 1 = false;
	bool _expanded : 1 = false;
	bool _changingFromTop : 1 = false;

	mutable CollapsedGeometry _lastCollapsedGeometry;
	mutable float64 _lastCollapsedRatio = 0.;

	int _selected = -1;
	int _pressed = -1;
	bool _dragOverBadge = false;

	rpl::event_stream<not_null<QWheelEvent*>> _verticalScrollEvents;

	rpl::variable<TextWithEntities> _tooltipText;
	rpl::variable<bool> _tooltipNotHidden;
	Fn<void()> _tooltipHide;
	std::unique_ptr<Ui::ImportantTooltip> _tooltip;
	bool _tooltipWindowActive = false;

	base::unique_qptr<Ui::PopupMenu> _menu;
	base::has_weak_ptr _menuGuard;

};

} // namespace Dialogs::Stories
