/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"

class PeerListsBox : public Ui::BoxContent {
public:
	PeerListsBox(
		QWidget*,
		std::vector<std::unique_ptr<PeerListController>> controllers,
		Fn<void(not_null<PeerListsBox*>)> init);

	[[nodiscard]] std::vector<not_null<PeerData*>> collectSelectedRows();

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	class Delegate final : public PeerListContentDelegate {
	public:
		Delegate(
			not_null<PeerListsBox*> box,
			not_null<PeerListController*> controller);

		void peerListSetTitle(rpl::producer<QString> title) override;
		void peerListSetAdditionalTitle(rpl::producer<QString> title) override;
		void peerListSetSearchMode(PeerListSearchMode mode) override;
		void peerListSetRowChecked(
			not_null<PeerListRow*> row,
			bool checked) override;
		void peerListSetForeignRowChecked(
			not_null<PeerListRow*> row,
			bool checked,
			anim::type animated) override;
		bool peerListIsRowChecked(not_null<PeerListRow*> row) override;
		int peerListSelectedRowsCount() override;
		void peerListScrollToTop() override;

		void peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) override {
			_box->addSelectItem(peer, anim::type::instant);
		}
		void peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) override {
			_box->addSelectItem(row, anim::type::instant);
		}
		void peerListFinishSelectedRowsBunch() override;

	private:
		const not_null<PeerListsBox*> _box;
		const not_null<PeerListController*> _controller;

	};
	struct List {
		std::unique_ptr<PeerListController> controller;
		std::unique_ptr<Delegate> delegate;
		PeerListContent *content = nullptr;
	};

	friend class Delegate;

	[[nodiscard]] List makeList(
		std::unique_ptr<PeerListController> controller);
	[[nodiscard]] std::vector<List> makeLists(
		std::vector<std::unique_ptr<PeerListController>> controllers);

	[[nodiscard]] not_null<PeerListController*> firstController() const;

	void addSelectItem(
		not_null<PeerData*> peer,
		anim::type animated);
	void addSelectItem(
		not_null<PeerListRow*> row,
		anim::type animated);
	void addSelectItem(
		uint64 itemId,
		const QString &text,
		PaintRoundImageCallback paintUserpic,
		anim::type animated);
	void setSearchMode(PeerListSearchMode mode);
	void createMultiSelect();
	int getTopScrollSkip() const;
	void updateScrollSkips();
	void searchQueryChanged(const QString &query);

	object_ptr<Ui::SlideWrap<Ui::MultiSelect>> _select = { nullptr };

	std::vector<List> _lists;
	Fn<void(PeerListsBox*)> _init;
	bool _scrollBottomFixed = false;

};
