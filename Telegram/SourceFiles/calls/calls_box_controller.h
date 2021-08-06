/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "ui/layers/generic_box.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Calls {

class BoxController : public PeerListController {
public:
	explicit BoxController(not_null<Window::SessionController*> window);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void rowActionClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;

	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

private:
	void receivedCalls(const QVector<MTPMessage> &result);
	void refreshAbout();

	class Row;
	Row *rowForItem(not_null<const HistoryItem*> item);

	enum class InsertWay {
		Append,
		Prepend,
	};
	bool insertRow(not_null<HistoryItem*> item, InsertWay way);
	std::unique_ptr<PeerListRow> createRow(
		not_null<HistoryItem*> item) const;

	const not_null<Window::SessionController*> _window;
	MTP::Sender _api;

	MsgId _offsetId = 0;
	int _loadRequestId = 0; // Not a real mtpRequestId.
	bool _allLoaded = false;

};

void ClearCallsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> window);

} // namespace Calls
