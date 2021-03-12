/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/calls_box_controller.h"

#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/popup_menu.h"
#include "core/application.h"
#include "calls/calls_instance.h"
#include "history/history.h"
#include "history/history_item.h"
#include "mainwidget.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_media_types.h"
#include "data/data_user.h"
#include "boxes/confirm_box.h"
#include "base/unixtime.h"
#include "api/api_updates.h"
#include "app.h"
#include "apiwrap.h"
#include "styles/style_layers.h" // st::boxLabel.
#include "styles/style_calls.h"
#include "styles/style_boxes.h"

namespace Calls {
namespace {

constexpr auto kFirstPageCount = 20;
constexpr auto kPerPageCount = 100;

} // namespace

class BoxController::Row : public PeerListRow {
public:
	Row(not_null<HistoryItem*> item);

	enum class Type {
		Out,
		In,
		Missed,
	};

	enum class CallType {
		Voice,
		Video,
	};

	bool canAddItem(not_null<const HistoryItem*> item) const {
		return (ComputeType(item) == _type)
			&& (!hasItems() || _items.front()->history() == item->history())
			&& (ItemDateTime(item).date() == _date);
	}
	void addItem(not_null<HistoryItem*> item) {
		Expects(canAddItem(item));

		_items.push_back(item);
		ranges::sort(_items, [](not_null<HistoryItem*> a, auto b) {
			return (a->id > b->id);
		});
		refreshStatus();
	}
	void itemRemoved(not_null<const HistoryItem*> item) {
		if (hasItems() && item->id >= minItemId() && item->id <= maxItemId()) {
			_items.erase(std::remove(_items.begin(), _items.end(), item), _items.end());
			refreshStatus();
		}
	}
	[[nodiscard]] bool hasItems() const {
		return !_items.empty();
	}

	[[nodiscard]] MsgId minItemId() const {
		Expects(hasItems());

		return _items.back()->id;
	}

	[[nodiscard]] MsgId maxItemId() const {
		Expects(hasItems());

		return _items.front()->id;
	}

	[[nodiscard]] const std::vector<not_null<HistoryItem*>> &items() const {
		return _items;
	}

	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;
	void addActionRipple(QPoint point, Fn<void()> updateCallback) override;
	void stopLastActionRipple() override;

	int nameIconWidth() const override {
		return 0;
	}
	QSize actionSize() const override {
		return peer()->isUser() ? QSize(_st->width, _st->height) : QSize();
	}
	QMargins actionMargins() const override {
		return QMargins(
			0,
			0,
			st::defaultPeerListItem.photoPosition.x(),
			0);
	}
	void paintAction(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) override;

private:
	void refreshStatus() override;
	static Type ComputeType(not_null<const HistoryItem*> item);
	static CallType ComputeCallType(not_null<const HistoryItem*> item);

	std::vector<not_null<HistoryItem*>> _items;
	QDate _date;
	Type _type;
	not_null<const style::IconButton*> _st;

	std::unique_ptr<Ui::RippleAnimation> _actionRipple;

};

BoxController::Row::Row(not_null<HistoryItem*> item)
: PeerListRow(item->history()->peer, item->id)
, _items(1, item)
, _date(ItemDateTime(item).date())
, _type(ComputeType(item))
, _st(ComputeCallType(item) == CallType::Voice
		? &st::callReDial
		: &st::callCameraReDial) {
	refreshStatus();
}

void BoxController::Row::paintStatusText(Painter &p, const style::PeerListItem &st, int x, int y, int availableWidth, int outerWidth, bool selected) {
	auto icon = ([this] {
		switch (_type) {
		case Type::In: return &st::callArrowIn;
		case Type::Out: return &st::callArrowOut;
		case Type::Missed: return &st::callArrowMissed;
		}
		Unexpected("_type in Calls::BoxController::Row::paintStatusText().");
	})();
	icon->paint(p, x + st::callArrowPosition.x(), y + st::callArrowPosition.y(), outerWidth);
	auto shift = st::callArrowPosition.x() + icon->width() + st::callArrowSkip;
	x += shift;
	availableWidth -= shift;

	PeerListRow::paintStatusText(p, st, x, y, availableWidth, outerWidth, selected);
}

void BoxController::Row::paintAction(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	auto size = actionSize();
	if (_actionRipple) {
		_actionRipple->paint(
			p,
			x + _st->rippleAreaPosition.x(),
			y + _st->rippleAreaPosition.y(),
			outerWidth);
		if (_actionRipple->empty()) {
			_actionRipple.reset();
		}
	}
	_st->icon.paintInCenter(
		p,
		style::rtlrect(x, y, size.width(), size.height(), outerWidth));
}

void BoxController::Row::refreshStatus() {
	if (!hasItems()) {
		return;
	}
	auto text = [this] {
		auto time = ItemDateTime(_items.front()).time().toString(cTimeFormat());
		auto today = QDateTime::currentDateTime().date();
		if (_date == today) {
			return tr::lng_call_box_status_today(tr::now, lt_time, time);
		} else if (_date.addDays(1) == today) {
			return tr::lng_call_box_status_yesterday(tr::now, lt_time, time);
		}
		return tr::lng_call_box_status_date(tr::now, lt_date, langDayOfMonthFull(_date), lt_time, time);
	};
	setCustomStatus((_items.size() > 1)
		? tr::lng_call_box_status_group(
			tr::now,
			lt_amount,
			QString::number(_items.size()),
			lt_status,
			text())
		: text());
}

BoxController::Row::Type BoxController::Row::ComputeType(
		not_null<const HistoryItem*> item) {
	if (item->out()) {
		return Type::Out;
	} else if (auto media = item->media()) {
		if (const auto call = media->call()) {
			const auto reason = call->finishReason;
			if (reason == Data::Call::FinishReason::Busy
				|| reason == Data::Call::FinishReason::Missed) {
				return Type::Missed;
			}
		}
	}
	return Type::In;
}

BoxController::Row::CallType BoxController::Row::ComputeCallType(
		not_null<const HistoryItem*> item) {
	if (auto media = item->media()) {
		if (const auto call = media->call()) {
			if (call->video) {
				return CallType::Video;
			}
		}
	}
	return CallType::Voice;
}

void BoxController::Row::addActionRipple(QPoint point, Fn<void()> updateCallback) {
	if (!_actionRipple) {
		auto mask = Ui::RippleAnimation::ellipseMask(
			QSize(_st->rippleAreaSize, _st->rippleAreaSize));
		_actionRipple = std::make_unique<Ui::RippleAnimation>(
			_st->ripple,
			std::move(mask),
			std::move(updateCallback));
	}
	_actionRipple->add(point - _st->rippleAreaPosition);
}

void BoxController::Row::stopLastActionRipple() {
	if (_actionRipple) {
		_actionRipple->lastStop();
	}
}

BoxController::BoxController(not_null<Window::SessionController*> window)
: _window(window)
, _api(&_window->session().mtp()) {
}

Main::Session &BoxController::session() const {
	return _window->session();
}

void BoxController::prepare() {
	session().data().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		if (const auto row = rowForItem(item)) {
			row->itemRemoved(item);
			if (!row->hasItems()) {
				delegate()->peerListRemoveRow(row);
				if (!delegate()->peerListFullRowsCount()) {
					refreshAbout();
				}
			}
			delegate()->peerListRefreshRows();
		}
	}, lifetime());

	session().changes().messageUpdates(
		Data::MessageUpdate::Flag::NewAdded
	) | rpl::filter([=](const Data::MessageUpdate &update) {
		const auto media = update.item->media();
		return (media != nullptr) && (media->call() != nullptr);
	}) | rpl::start_with_next([=](const Data::MessageUpdate &update) {
		insertRow(update.item, InsertWay::Prepend);
	}, lifetime());

	delegate()->peerListSetTitle(tr::lng_call_box_title());
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	delegate()->peerListRefreshRows();

	loadMoreRows();
}

void BoxController::loadMoreRows() {
	if (_loadRequestId || _allLoaded) {
		return;
	}

	_loadRequestId = _api.request(MTPmessages_Search(
		MTP_flags(0),
		MTP_inputPeerEmpty(),
		MTP_string(),
		MTP_inputPeerEmpty(),
		MTPint(), // top_msg_id
		MTP_inputMessagesFilterPhoneCalls(MTP_flags(0)),
		MTP_int(0),
		MTP_int(0),
		MTP_int(_offsetId),
		MTP_int(0),
		MTP_int(_offsetId ? kFirstPageCount : kPerPageCount),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0)
	)).done([this](const MTPmessages_Messages &result) {
		_loadRequestId = 0;

		auto handleResult = [&](auto &data) {
			session().data().processUsers(data.vusers());
			session().data().processChats(data.vchats());
			receivedCalls(data.vmessages().v);
		};

		switch (result.type()) {
		case mtpc_messages_messages: handleResult(result.c_messages_messages()); _allLoaded = true; break;
		case mtpc_messages_messagesSlice: handleResult(result.c_messages_messagesSlice()); break;
		case mtpc_messages_channelMessages: {
			LOG(("API Error: received messages.channelMessages! (Calls::BoxController::preloadRows)"));
			handleResult(result.c_messages_channelMessages());
		} break;
		case mtpc_messages_messagesNotModified: {
			LOG(("API Error: received messages.messagesNotModified! (Calls::BoxController::preloadRows)"));
		} break;
		default: Unexpected("Type of messages.Messages (Calls::BoxController::preloadRows)");
		}
	}).fail([this](const MTP::Error &error) {
		_loadRequestId = 0;
	}).send();
}

base::unique_qptr<Ui::PopupMenu> BoxController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	const auto &items = static_cast<Row*>(row.get())->items();
	const auto session = &this->session();
	const auto ids = session->data().itemsToIds(items);

	auto result = base::make_unique_q<Ui::PopupMenu>(parent);
	result->addAction(tr::lng_context_delete_selected(tr::now), [=] {
		Ui::show(
			Box<DeleteMessagesBox>(session, base::duplicate(ids)),
			Ui::LayerOption::KeepOther);
	});
	return result;
}

void BoxController::refreshAbout() {
	setDescriptionText(delegate()->peerListFullRowsCount() ? QString() : tr::lng_call_box_about(tr::now));
}

void BoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto itemsRow = static_cast<Row*>(row.get());
	const auto itemId = itemsRow->maxItemId();
	const auto window = _window;
	crl::on_main(window, [=, peer = row->peer()] {
		window->showPeerHistory(
			peer,
			Window::SectionShow::Way::ClearStack,
			itemId);
	});
}

void BoxController::rowActionClicked(not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	Assert(user != nullptr);

	Core::App().calls().startOutgoingCall(user, false);
}

void BoxController::receivedCalls(const QVector<MTPMessage> &result) {
	if (result.empty()) {
		_allLoaded = true;
	}

	for (const auto &message : result) {
		const auto msgId = IdFromMessage(message);
		const auto peerId = PeerFromMessage(message);
		if (const auto peer = session().data().peerLoaded(peerId)) {
			const auto item = session().data().addNewMessage(
				message,
				MTPDmessage_ClientFlags(),
				NewMessageType::Existing);
			insertRow(item, InsertWay::Append);
		} else {
			LOG(("API Error: a search results with not loaded peer %1").arg(peerId));
		}
		_offsetId = msgId;
	}

	refreshAbout();
	delegate()->peerListRefreshRows();
}

bool BoxController::insertRow(
		not_null<HistoryItem*> item,
		InsertWay way) {
	if (auto row = rowForItem(item)) {
		if (row->canAddItem(item)) {
			row->addItem(item);
			return false;
		}
	}
	(way == InsertWay::Append)
		? delegate()->peerListAppendRow(createRow(item))
		: delegate()->peerListPrependRow(createRow(item));
	delegate()->peerListSortRows([](
			const PeerListRow &a,
			const PeerListRow &b) {
		return static_cast<const Row&>(a).maxItemId()
			> static_cast<const Row&>(b).maxItemId();
	});
	return true;
}

BoxController::Row *BoxController::rowForItem(not_null<const HistoryItem*> item) {
	auto v = delegate();
	if (auto fullRowsCount = v->peerListFullRowsCount()) {
		auto itemId = item->id;
		auto lastRow = static_cast<Row*>(v->peerListRowAt(fullRowsCount - 1).get());
		if (itemId < lastRow->minItemId()) {
			return lastRow;
		}
		auto firstRow = static_cast<Row*>(v->peerListRowAt(0).get());
		if (itemId > firstRow->maxItemId()) {
			return firstRow;
		}

		// Binary search. Invariant:
		// 1. rowAt(left)->maxItemId() >= itemId.
		// 2. (left + 1 == fullRowsCount) OR rowAt(left + 1)->maxItemId() < itemId.
		auto left = 0;
		auto right = fullRowsCount;
		while (left + 1 < right) {
			auto middle = (right + left) / 2;
			auto middleRow = static_cast<Row*>(v->peerListRowAt(middle).get());
			if (middleRow->maxItemId() >= itemId) {
				left = middle;
			} else {
				right = middle;
			}
		}
		auto result = static_cast<Row*>(v->peerListRowAt(left).get());
		// Check for rowAt(left)->minItemId > itemId > rowAt(left + 1)->maxItemId.
		// In that case we sometimes need to return rowAt(left + 1), not rowAt(left).
		if (result->minItemId() > itemId && left + 1 < fullRowsCount) {
			auto possibleResult = static_cast<Row*>(v->peerListRowAt(left + 1).get());
			Assert(possibleResult->maxItemId() < itemId);
			if (possibleResult->canAddItem(item)) {
				return possibleResult;
			}
		}
		return result;
	}
	return nullptr;
}

std::unique_ptr<PeerListRow> BoxController::createRow(
		not_null<HistoryItem*> item) const {
	return std::make_unique<Row>(item);
}

void ClearCallsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window) {
	const auto weak = Ui::MakeWeak(box);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_call_box_clear_sure(),
			st::boxLabel),
		st::boxPadding);
	const auto revokeCheckbox = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			tr::lng_delete_for_everyone_check(tr::now),
			false,
			st::defaultBoxCheckbox),
		style::margins(
			st::boxPadding.left(),
			st::boxPadding.bottom(),
			st::boxPadding.right(),
			st::boxPadding.bottom()));

	const auto api = &window->session().api();
	const auto sendRequest = [=](bool revoke, auto self) -> void {
		using Flag = MTPmessages_DeletePhoneCallHistory::Flag;
		api->request(MTPmessages_DeletePhoneCallHistory(
			MTP_flags(revoke ? Flag::f_revoke : Flag(0))
		)).done([=](const MTPmessages_AffectedFoundMessages &result) {
			result.match([&](
					const MTPDmessages_affectedFoundMessages &data) {
				api->applyUpdates(MTP_updates(
					MTP_vector<MTPUpdate>(
						1,
						MTP_updateDeleteMessages(
							data.vmessages(),
							data.vpts(),
							data.vpts_count())),
					MTP_vector<MTPUser>(),
					MTP_vector<MTPChat>(),
					MTP_int(base::unixtime::now()),
					MTP_int(0)));
				const auto offset = data.voffset().v;
				if (offset > 0) {
					self(revoke, self);
				} else {
					api->session().data().destroyAllCallItems();
					if (const auto strong = weak.data()) {
						strong->closeBox();
					}
				}
			});
		}).send();
	};

	box->addButton(tr::lng_call_box_clear_button(), [=] {
		sendRequest(revokeCheckbox->checked(), sendRequest);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Calls
