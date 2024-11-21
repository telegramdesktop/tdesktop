/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_list_widgets.h"

#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"

namespace {

using State = std::unique_ptr<PeerListState>;

} // namespace

PeerListWidgets::PeerListWidgets(
	not_null<Ui::RpWidget*> parent,
	not_null<PeerListController*> controller)
: Ui::RpWidget(parent)
, _controller(controller)
, _st(controller->computeListSt()) {
	_content = base::make_unique_q<Ui::VerticalLayout>(this);
	parent->sizeValue() | rpl::start_with_next([this](const QSize &size) {
		_content->resizeToWidth(size.width());
		resize(size.width(), _content->height());
	}, lifetime());
}

crl::time PeerListWidgets::paintRow(
		Painter &p,
		crl::time now,
		bool selected,
		not_null<PeerListRow*> row) {
	const auto &st = row->computeSt(_st.item);

	row->lazyInitialize(st);
	const auto outerWidth = _content->width();
	const auto w = outerWidth;

	auto refreshStatusAt = row->refreshStatusTime();
	if (refreshStatusAt > 0 && now >= refreshStatusAt) {
		row->refreshStatus();
		refreshStatusAt = row->refreshStatusTime();
	}
	const auto refreshStatusIn = (refreshStatusAt > 0)
		? std::max(refreshStatusAt - now, crl::time(1))
		: 0;

	row->paintUserpic(
		p,
		st,
		st.photoPosition.x(),
		st.photoPosition.y(),
		outerWidth);

	p.setPen(st::contactsNameFg);

	const auto skipRight = st.photoPosition.x();
	const auto rightActionSize = row->rightActionSize();
	const auto rightActionMargins = rightActionSize.isEmpty()
		? QMargins()
		: row->rightActionMargins();
	const auto &name = row->name();
	const auto namePosition = st.namePosition;
	const auto namex = namePosition.x();
	const auto namey = namePosition.y();
	auto namew = outerWidth - namex - skipRight;
	if (!rightActionSize.isEmpty()
		&& (namey < rightActionMargins.top() + rightActionSize.height())
		&& (namey + st.nameStyle.font->height
			> rightActionMargins.top())) {
		namew -= rightActionMargins.left()
			+ rightActionSize.width()
			+ rightActionMargins.right()
			- skipRight;
	}
	const auto statusx = st.statusPosition.x();
	const auto statusy = st.statusPosition.y();
	auto statusw = outerWidth - statusx - skipRight;
	if (!rightActionSize.isEmpty()
		&& (statusy < rightActionMargins.top() + rightActionSize.height())
		&& (statusy + st::contactsStatusFont->height
			> rightActionMargins.top())) {
		statusw -= rightActionMargins.left()
			+ rightActionSize.width()
			+ rightActionMargins.right()
			- skipRight;
	}
	namew -= row->paintNameIconGetWidth(
		p,
		[=] { updateRow(row); },
		now,
		namex,
		namey,
		name.maxWidth(),
		namew,
		w,
		selected);
	auto nameCheckedRatio = row->disabled() ? 0. : row->checkedRatio();
	p.setPen(anim::pen(st.nameFg, st.nameFgChecked, nameCheckedRatio));
	name.drawLeftElided(p, namex, namey, namew, w);

	p.setFont(st::contactsStatusFont);
	row->paintStatusText(p, st, statusx, statusy, statusw, w, selected);

	row->elementsPaint(p, outerWidth, selected, 0);

	return refreshStatusIn;
}

void PeerListWidgets::appendRow(std::unique_ptr<PeerListRow> row) {
	Expects(row != nullptr);

	if (_rowsById.find(row->id()) == _rowsById.cend()) {
		const auto raw = row.get();
		const auto &st = raw->computeSt(_st.item);
		raw->setAbsoluteIndex(_rows.size());
		_rows.push_back(std::move(row));

		const auto widget = _content->add(
			object_ptr<Ui::AbstractButton>::fromRaw(
				Ui::CreateSimpleSettingsButton(
					_content.get(),
					st.button.ripple,
					st.button.textBgOver)));
		widget->resize(widget->width(), st.height);
		widget->paintRequest() | rpl::start_with_next([=, this] {
			auto p = Painter(widget);
			const auto selected = widget->isOver() || widget->isDown();
			paintRow(p, crl::now(), selected, raw);
		}, widget->lifetime());

		widget->setClickedCallback([this, raw] {
			_controller->rowClicked(raw);
		});
	}
}

PeerListRow *PeerListWidgets::findRow(PeerListRowId id) {
	const auto it = _rowsById.find(id);
	return (it == _rowsById.cend()) ? nullptr : it->second.get();
}

void PeerListWidgets::updateRow(not_null<PeerListRow*> row) {
	const auto it = ranges::find_if(
		_rows,
		[row](const auto &r) { return r.get() == row; });
	if (it != _rows.end()) {
		const auto index = std::distance(_rows.begin(), it);
		if (const auto widget = _content->widgetAt(index)) {
			widget->update();
		}
	}
}

int PeerListWidgets::fullRowsCount() {
	return _rows.size();
}

[[nodiscard]] not_null<PeerListRow*> PeerListWidgets::rowAt(int index) {
	Expects(index >= 0 && index < _rows.size());

	return _rows[index].get();
}

void PeerListWidgets::refreshRows() {
	_content->resizeToWidth(width());
	resize(width(), _content->height());
}

void PeerListWidgetsDelegate::setContent(PeerListWidgets *content) {
	_content = content;
}

void PeerListWidgetsDelegate::setUiShow(
		std::shared_ptr<Main::SessionShow> uiShow) {
	_uiShow = std::move(uiShow);
}

void PeerListWidgetsDelegate::peerListSetHideEmpty(bool hide) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetHideEmpty");
}

void PeerListWidgetsDelegate::peerListAppendRow(
		std::unique_ptr<PeerListRow> row) {
	_content->appendRow(std::move(row));
}

void PeerListWidgetsDelegate::peerListAppendSearchRow(
		std::unique_ptr<PeerListRow> row) {
	Unexpected("...PeerListWidgetsDelegate::peerListAppendSearchRow");
}

void PeerListWidgetsDelegate::peerListAppendFoundRow(
		not_null<PeerListRow*> row) {
	Unexpected("...PeerListWidgetsDelegate::peerListAppendFoundRow");
}

void PeerListWidgetsDelegate::peerListPrependRow(
		std::unique_ptr<PeerListRow> row) {
	Unexpected("...PeerListWidgetsDelegate::peerListPrependRow");
}

void PeerListWidgetsDelegate::peerListPrependRowFromSearchResult(
		not_null<PeerListRow*> row) {
	Unexpected(
		"...PeerListWidgetsDelegate::peerListPrependRowFromSearchResult");
}

PeerListRow* PeerListWidgetsDelegate::peerListFindRow(PeerListRowId id) {
	return _content->findRow(id);
}

auto PeerListWidgetsDelegate::peerListLastRowMousePosition()
-> std::optional<QPoint> {
	Unexpected("...PeerListWidgetsDelegate::peerListLastRowMousePosition");
}

void PeerListWidgetsDelegate::peerListUpdateRow(not_null<PeerListRow*> row) {
	_content->updateRow(row);
}

void PeerListWidgetsDelegate::peerListRemoveRow(not_null<PeerListRow*> row) {
	Unexpected("...PeerListWidgetsDelegate::peerListRemoveRow");
}

void PeerListWidgetsDelegate::peerListConvertRowToSearchResult(
		not_null<PeerListRow*> row) {
	Unexpected(
		"...PeerListWidgetsDelegate::peerListConvertRowToSearchResult");
}

void PeerListWidgetsDelegate::peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetRowChecked");
}

void PeerListWidgetsDelegate::peerListSetRowHidden(
		not_null<PeerListRow*> row,
		bool hidden) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetRowHidden");
}

void PeerListWidgetsDelegate::peerListSetForeignRowChecked(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) {
}

int PeerListWidgetsDelegate::peerListFullRowsCount() {
	return _content->fullRowsCount();
}

not_null<PeerListRow*> PeerListWidgetsDelegate::peerListRowAt(int index) {
	return _content->rowAt(index);
}

int PeerListWidgetsDelegate::peerListSearchRowsCount() {
	Unexpected("...PeerListWidgetsDelegate::peerListSearchRowsCount");
}

not_null<PeerListRow*> PeerListWidgetsDelegate::peerListSearchRowAt(int) {
	Unexpected("...PeerListWidgetsDelegate::peerListSearchRowAt");
}

void PeerListWidgetsDelegate::peerListRefreshRows() {
	_content->refreshRows();
}

void PeerListWidgetsDelegate::peerListSetDescription(
		object_ptr<Ui::FlatLabel>) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetDescription");
}

void PeerListWidgetsDelegate::peerListSetSearchNoResults(
		object_ptr<Ui::FlatLabel>) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetSearchNoResults");
}

void PeerListWidgetsDelegate::peerListSetAboveWidget(
		object_ptr<Ui::RpWidget>) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetAboveWidget");
}

void PeerListWidgetsDelegate::peerListSetAboveSearchWidget(
		object_ptr<Ui::RpWidget>) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetAboveSearchWidget");
}

void PeerListWidgetsDelegate::peerListSetBelowWidget(
		object_ptr<Ui::RpWidget>) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetBelowWidget");
}

void PeerListWidgetsDelegate::peerListSetSearchMode(PeerListSearchMode mode) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetSearchMode");
}

void PeerListWidgetsDelegate::peerListMouseLeftGeometry() {
	Unexpected("...PeerListWidgetsDelegate::peerListMouseLeftGeometry");
}

void PeerListWidgetsDelegate::peerListSortRows(
		Fn<bool(const PeerListRow &, const PeerListRow &)>) {
	Unexpected("...PeerListWidgetsDelegate::peerListSortRows");
}

int PeerListWidgetsDelegate::peerListPartitionRows(
		Fn<bool(const PeerListRow &a)> border) {
	Unexpected("...PeerListWidgetsDelegate::peerListPartitionRows");
}

State PeerListWidgetsDelegate::peerListSaveState() const {
	Unexpected("...PeerListWidgetsDelegate::peerListSaveState");
	return nullptr;
}

void PeerListWidgetsDelegate::peerListRestoreState(
		std::unique_ptr<PeerListState> state) {
	Unexpected("...PeerListWidgetsDelegate::peerListRestoreState");
}

void PeerListWidgetsDelegate::peerListShowRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed) {
}

void PeerListWidgetsDelegate::peerListSelectSkip(int direction) {
	// _content->selectSkip(direction);
}

void PeerListWidgetsDelegate::peerListPressLeftToContextMenu(bool shown) {
	Unexpected("...PeerListWidgetsDelegate::peerListPressLeftToContextMenu");
}

bool PeerListWidgetsDelegate::peerListTrackRowPressFromGlobal(QPoint) {
	return false;
}

std::shared_ptr<Main::SessionShow> PeerListWidgetsDelegate::peerListUiShow() {
	Expects(_uiShow != nullptr);
	return _uiShow;
}

void PeerListWidgetsDelegate::peerListAddSelectedPeerInBunch(
		not_null<PeerData*> peer) {
	Unexpected("...PeerListWidgetsDelegate::peerListAddSelectedPeerInBunch");
}

void PeerListWidgetsDelegate::peerListAddSelectedRowInBunch(
		not_null<PeerListRow*> row) {
	Unexpected("...PeerListWidgetsDelegate::peerListAddSelectedRowInBunch");
}

void PeerListWidgetsDelegate::peerListFinishSelectedRowsBunch() {
	Unexpected("...PeerListWidgetsDelegate::peerListFinishSelectedRowsBunch");
}

void PeerListWidgetsDelegate::peerListSetTitle(rpl::producer<QString> title) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetTitle");
}

void PeerListWidgetsDelegate::peerListSetAdditionalTitle(
		rpl::producer<QString> title) {
	Unexpected("...PeerListWidgetsDelegate::peerListSetAdditionalTitle");
}

bool PeerListWidgetsDelegate::peerListIsRowChecked(
		not_null<PeerListRow*> row) {
	Unexpected("...PeerListWidgetsDelegate::peerListIsRowChecked");
	return false;
}

void PeerListWidgetsDelegate::peerListScrollToTop() {
	Unexpected("...PeerListWidgetsDelegate::peerListScrollToTop");
}

int PeerListWidgetsDelegate::peerListSelectedRowsCount() {
	Unexpected("...PeerListWidgetsDelegate::peerListSelectedRowsCount");
	return 0;
}
