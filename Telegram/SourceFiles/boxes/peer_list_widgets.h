/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

class PeerListWidgets : public Ui::RpWidget {
public:
	PeerListWidgets(
		not_null<Ui::RpWidget*> parent,
		not_null<PeerListController*> controller);

	crl::time paintRow(
		Painter &p,
		crl::time now,
		bool selected,
		not_null<PeerListRow*> row);
	void appendRow(std::unique_ptr<PeerListRow> row);
	PeerListRow* findRow(PeerListRowId id);
	void updateRow(not_null<PeerListRow*> row);
	int fullRowsCount();
	[[nodiscard]] not_null<PeerListRow*> rowAt(int index);
	void refreshRows();

private:
	const not_null<PeerListController*> _controller;
	const style::PeerList &_st;
	base::unique_qptr<Ui::VerticalLayout> _content;

	std::vector<std::unique_ptr<PeerListRow>> _rows;
	std::map<PeerListRowId, not_null<PeerListRow*>> _rowsById;
	std::map<PeerData*, std::vector<not_null<PeerListRow*>>> _rowsByPeer;
};

class PeerListWidgetsDelegate : public PeerListDelegate {
public:
	void setContent(PeerListWidgets *content);
	void setUiShow(std::shared_ptr<Main::SessionShow> uiShow);

	void peerListSetHideEmpty(bool hide) override;
	void peerListAppendRow(std::unique_ptr<PeerListRow> row) override;
	void peerListAppendSearchRow(std::unique_ptr<PeerListRow> row) override;
	void peerListAppendFoundRow(not_null<PeerListRow*> row) override;
	void peerListPrependRow(std::unique_ptr<PeerListRow> row) override;
	void peerListPrependRowFromSearchResult(
		not_null<PeerListRow*> row) override;
	PeerListRow *peerListFindRow(PeerListRowId id) override;
	std::optional<QPoint> peerListLastRowMousePosition() override;
	void peerListUpdateRow(not_null<PeerListRow*> row) override;
	void peerListRemoveRow(not_null<PeerListRow*> row) override;
	void peerListConvertRowToSearchResult(
		not_null<PeerListRow*> row) override;
	void peerListSetRowChecked(
		not_null<PeerListRow*> row,
		bool checked) override;
	void peerListSetRowHidden(
		not_null<PeerListRow*> row,
		bool hidden) override;
	void peerListSetForeignRowChecked(
		not_null<PeerListRow*> row,
		bool checked,
		anim::type animated) override;
	int peerListFullRowsCount() override;
	not_null<PeerListRow*> peerListRowAt(int index) override;
	int peerListSearchRowsCount() override;
	not_null<PeerListRow*> peerListSearchRowAt(int index) override;
	void peerListRefreshRows() override;
	void peerListSetDescription(object_ptr<Ui::FlatLabel>) override;
	void peerListSetSearchNoResults(object_ptr<Ui::FlatLabel>) override;
	void peerListSetAboveWidget(object_ptr<Ui::RpWidget>) override;
	void peerListSetAboveSearchWidget(object_ptr<Ui::RpWidget>) override;
	void peerListSetBelowWidget(object_ptr<Ui::RpWidget>) override;
	void peerListSetSearchMode(PeerListSearchMode mode) override;
	void peerListMouseLeftGeometry() override;
	void peerListSortRows(
		Fn<bool(const PeerListRow &, const PeerListRow &)>) override;
	int peerListPartitionRows(Fn<bool(const PeerListRow &a)> border) override;
	std::unique_ptr<PeerListState> peerListSaveState() const override;
	void peerListRestoreState(std::unique_ptr<PeerListState> state) override;
	void peerListShowRowMenu(
		not_null<PeerListRow*> row,
		bool highlightRow,
		Fn<void(not_null<Ui::PopupMenu*>)> destroyed = nullptr) override;
	void peerListSelectSkip(int direction) override;
	void peerListPressLeftToContextMenu(bool shown) override;
	bool peerListTrackRowPressFromGlobal(QPoint globalPosition) override;
	std::shared_ptr<Main::SessionShow> peerListUiShow() override;
	void peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) override;
	void peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) override;
	void peerListFinishSelectedRowsBunch() override;
	void peerListSetTitle(rpl::producer<QString> title) override;
	void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
	bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
	void peerListScrollToTop() override;
	int peerListSelectedRowsCount() override;

private:
	PeerListWidgets *_content = nullptr;
	std::shared_ptr<Main::SessionShow> _uiShow;

};
