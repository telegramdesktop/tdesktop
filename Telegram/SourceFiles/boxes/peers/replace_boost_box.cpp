/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/replace_boost_box.h"

#include "base/event_filter.h"
#include "base/unixtime.h"
#include "boxes/peer_list_box.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "ui/boxes/boost_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/premium_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_premium.h"

namespace {

constexpr auto kWaitingOpacity = 0.5;

class Row final : public PeerListRow {
public:
	Row(
		not_null<Main::Session*> session,
		TakenBoostSlot slot,
		TimeId unixtimeNow,
		crl::time preciseNow);

	void updateStatus(TimeId unixtimeNow, crl::time preciseNow);
	[[nodiscard]] TakenBoostSlot data() const {
		return _data;
	}
	[[nodiscard]] bool waiting() const {
		return _waiting;
	}

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;
	float64 opacity() override;

private:
	[[nodiscard]]PaintRoundImageCallback peerPaintUserpicCallback();

	TakenBoostSlot _data;
	PeerData *_peer = nullptr;
	std::shared_ptr<Ui::EmptyUserpic> _empty;
	Ui::PeerUserpicView _userpic;
	crl::time _startPreciseTime = 0;
	TimeId _startUnixtime = 0;
	bool _waiting = false;

};

class Controller final : public PeerListController {
public:
	Controller(not_null<ChannelData*> to, std::vector<TakenBoostSlot> from);

	[[nodiscard]] rpl::producer<std::vector<int>> selectedValue() const {
		return _selected.value();
	}

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	bool trackSelectedList() override {
		return false;
	}

private:
	void updateWaitingState();

	not_null<ChannelData*> _to;
	std::vector<TakenBoostSlot> _from;
	rpl::variable<std::vector<int>> _selected;
	rpl::variable<std::vector<not_null<PeerData*>>> _selectedPeers;
	base::Timer _waitingTimer;
	bool _hasWaitingRows = false;

};

Row::Row(
	not_null<Main::Session*> session,
	TakenBoostSlot slot,
	TimeId unixtimeNow,
	crl::time preciseNow)
: PeerListRow(PeerListRowId(slot.id))
, _data(slot)
, _peer(session->data().peerLoaded(_data.peerId))
, _startPreciseTime(preciseNow)
, _startUnixtime(unixtimeNow) {
	updateStatus(unixtimeNow, preciseNow);
}

void Row::updateStatus(TimeId unixtimeNow, crl::time preciseNow) {
	_waiting = (_data.cooldown > unixtimeNow);
	if (_waiting) {
		const auto initial = crl::time(_data.cooldown - _startUnixtime);
		const auto elapsed = (preciseNow + 500 - _startPreciseTime) / 1000;
		const auto seconds = initial
			- std::clamp(elapsed, crl::time(), initial);
		const auto hours = seconds / 3600;
		const auto minutes = seconds / 60;
		const auto duration = (hours > 0)
			? u"%1:%2:%3"_q.arg(
				hours
			).arg(minutes % 60, 2, 10, QChar('0')
			).arg(seconds % 60, 2, 10, QChar('0'))
			: u"%1:%2"_q.arg(
				minutes
			).arg(seconds % 60, 2, 10, QChar('0'));
		setCustomStatus(
			tr::lng_boost_available_in(tr::now, lt_duration, duration));
	} else {
		const auto date = base::unixtime::parse(_data.expires);
		setCustomStatus(tr::lng_boosts_list_status(
			tr::now,
			lt_date,
			langDayOfMonth(date.date())));
	}
}

QString Row::generateName() {
	return _peer ? _peer->name() : u" "_q;
}

QString Row::generateShortName() {
	return _peer ? _peer->shortName() : generateName();
}

PaintRoundImageCallback Row::generatePaintUserpicCallback(
		bool forceRound) {
	if (_peer) {
		return (forceRound && _peer->isForum())
			? ForceRoundUserpicCallback(_peer)
			: peerPaintUserpicCallback();
	} else if (!_empty) {
		const auto colorIndex = _data.id % Ui::kColorIndexCount;
		_empty = std::make_shared<Ui::EmptyUserpic>(
			Ui::EmptyUserpic::UserpicColor(colorIndex),
			u" "_q);
	}
	const auto empty = _empty;
	return [=](Painter &p, int x, int y, int outerWidth, int size) {
		empty->paintCircle(p, x, y, outerWidth, size);
	};
}

float64 Row::opacity() {
	return _waiting ? kWaitingOpacity : 1.;
}

PaintRoundImageCallback Row::peerPaintUserpicCallback() {
	const auto peer = _peer;
	if (!_userpic.cloud && peer->hasUserpic()) {
		_userpic = peer->createUserpicView();
	}
	auto userpic = _userpic;
	return [=](Painter &p, int x, int y, int outerWidth, int size) mutable {
		peer->paintUserpicLeft(p, userpic, x, y, outerWidth, size);
	};
}

Controller::Controller(
	not_null<ChannelData*> to,
	std::vector<TakenBoostSlot> from)
: _to(to)
, _from(std::move(from))
, _waitingTimer([=] { updateWaitingState(); }) {
}

Main::Session &Controller::session() const {
	return _to->session();
}

void Controller::prepare() {
	delegate()->peerListSetTitle(tr::lng_boost_reassign_title());

	const auto session = &_to->session();
	auto above = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	above->add(
		CreateBoostReplaceUserpics(
			above.data(),
			_selectedPeers.value(),
			_to),
		st::boxRowPadding + st::boostReplaceUserpicsPadding);
	above->add(
		object_ptr<Ui::FlatLabel>(
			above.data(),
			tr::lng_boost_reassign_text(
				lt_channel,
				rpl::single(Ui::Text::Bold(_to->name())),
				lt_gift,
				tr::lng_boost_reassign_gift(
					lt_count,
					rpl::single(1. * BoostsForGift(session)),
					Ui::Text::RichLangValue),
				Ui::Text::RichLangValue),
			st::boostReassignText),
		st::boxRowPadding);
	delegate()->peerListSetAboveWidget(std::move(above));

	const auto now = base::unixtime::now();
	const auto precise = crl::now();
	ranges::stable_sort(_from, ranges::less(), [&](TakenBoostSlot slot) {
		return (slot.cooldown > now) ? slot.cooldown : -slot.cooldown;
	});
	for (const auto &slot : _from) {
		auto row = std::make_unique<Row>(session, slot, now, precise);
		if (row->waiting()) {
			_hasWaitingRows = true;
		}
		delegate()->peerListAppendRow(std::move(row));
	}

	if (_hasWaitingRows) {
		_waitingTimer.callEach(1000);
	}

	delegate()->peerListRefreshRows();
}

void Controller::updateWaitingState() {
	_hasWaitingRows = false;
	const auto now = base::unixtime::now();
	const auto precise = crl::now();
	const auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		const auto bare = delegate()->peerListRowAt(i);
		const auto row = static_cast<Row*>(bare.get());
		if (row->waiting()) {
			row->updateStatus(now, precise);
			delegate()->peerListUpdateRow(row);
			if (row->waiting()) {
				_hasWaitingRows = true;
			}
		}
	}
	if (!_hasWaitingRows) {
		_waitingTimer.cancel();
	}
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	const auto slot = static_cast<Row*>(row.get())->data();
	if (slot.cooldown > base::unixtime::now()) {
		delegate()->peerListUiShow()->showToast({
			.text = tr::lng_boost_available_in_toast(
				tr::now,
				lt_count,
				BoostsForGift(&session()),
				Ui::Text::RichLangValue),
			.adaptive = true,
		});
		return;
	}
	auto now = _selected.current();
	const auto id = slot.id;
	const auto checked = !row->checked();
	delegate()->peerListSetRowChecked(row, checked);
	const auto peer = slot.peerId
		? _to->owner().peerLoaded(slot.peerId)
		: nullptr;
	auto peerRemoved = false;
	if (checked) {
		now.push_back(id);
	} else {
		now.erase(ranges::remove(now, id), end(now));

		peerRemoved = true;
		for (const auto left : now) {
			const auto i = ranges::find(_from, left, &TakenBoostSlot::id);
			Assert(i != end(_from));
			if (i->peerId == slot.peerId) {
				peerRemoved = false;
				break;
			}
		}
	}
	_selected = std::move(now);

	if (peer) {
		auto selectedPeers = _selectedPeers.current();
		const auto i = ranges::find(selectedPeers, not_null(peer));
		if (peerRemoved) {
			Assert(i != end(selectedPeers));
			selectedPeers.erase(i);
			_selectedPeers = std::move(selectedPeers);
		} else if (i == end(selectedPeers) && checked) {
			selectedPeers.insert(begin(selectedPeers), peer);
			_selectedPeers = std::move(selectedPeers);
		}
	}
}

object_ptr<Ui::BoxContent> ReassignBoostFloodBox(int seconds) {
	const auto days = seconds / 86400;
	const auto hours = seconds / 3600;
	const auto minutes = seconds / 60;
	return Ui::MakeInformBox({
		.text = tr::lng_boost_error_flood_text(
			lt_left,
			rpl::single(Ui::Text::Bold((days > 1)
				? tr::lng_days(tr::now, lt_count, days)
				: (hours > 1)
				? tr::lng_hours(tr::now, lt_count, hours)
				: (minutes > 1)
				? tr::lng_minutes(tr::now, lt_count, minutes)
				: tr::lng_seconds(tr::now, lt_count, seconds))),
			Ui::Text::RichLangValue),
		.title = tr::lng_boost_error_flood_title(),
	});
}

object_ptr<Ui::BoxContent> ReassignBoostSingleBox(
		not_null<ChannelData*> to,
		TakenBoostSlot from,
		Fn<void(std::vector<int> slots, int sources)> reassign,
		Fn<void()> cancel) {
	const auto reassigned = std::make_shared<bool>();
	const auto slot = from.id;
	const auto peer = to->owner().peer(from.peerId);
	const auto confirmed = [=](Fn<void()> close) {
		*reassigned = true;
		reassign({ slot }, 1);
		close();
	};

	auto result = Box([=](not_null<Ui::GenericBox*> box) {
		Ui::ConfirmBox(box, {
			.text = tr::lng_boost_now_instead(
				lt_channel,
				rpl::single(Ui::Text::Bold(peer->name())),
				lt_other,
				rpl::single(Ui::Text::Bold(to->name())),
				Ui::Text::WithEntities),
			.confirmed = confirmed,
			.confirmText = tr::lng_boost_now_replace(),
			.labelPadding = st::boxRowPadding,
		});
		box->verticalLayout()->insert(
			0,
			CreateBoostReplaceUserpics(
				box,
				rpl::single(std::vector{ peer }),
				to),
			st::boxRowPadding + st::boostReplaceUserpicsPadding);
	});

	result->boxClosing() | rpl::filter([=] {
		return !*reassigned;
	}) | rpl::start_with_next(cancel, result->lifetime());

	return result;
}

} // namespace

ForChannelBoostSlots ParseForChannelBoostSlots(
		not_null<ChannelData*> channel,
		const QVector<MTPMyBoost> &boosts) {
	auto result = ForChannelBoostSlots();
	const auto now = base::unixtime::now();
	for (const auto &my : boosts) {
		const auto &data = my.data();
		const auto id = data.vslot().v;
		const auto cooldown = data.vcooldown_until_date().value_or(0);
		const auto peerId = data.vpeer()
			? peerFromMTP(*data.vpeer())
			: PeerId();
		if (!peerId && cooldown <= now) {
			result.free.push_back(id);
		} else if (peerId == channel->id) {
			result.already.push_back(id);
		} else {
			result.other.push_back({
				.id = id,
				.expires = data.vexpires().v,
				.peerId = peerId,
				.cooldown = cooldown,
			});
		}
	}
	return result;
}

Ui::BoostCounters ParseBoostCounters(
		const MTPpremium_BoostsStatus &status) {
	const auto &data = status.data();
	const auto slots = data.vmy_boost_slots();
	return {
		.level = data.vlevel().v,
		.boosts = data.vboosts().v,
		.thisLevelBoosts = data.vcurrent_level_boosts().v,
		.nextLevelBoosts = data.vnext_level_boosts().value_or_empty(),
		.mine = slots ? int(slots->v.size()) : 0,
	};
}

int BoostsForGift(not_null<Main::Session*> session) {
	const auto key = u"boosts_per_sent_gift"_q;
	return session->account().appConfig().get<int>(key, 0);
}

[[nodiscard]] int SourcesCount(
		const std::vector<TakenBoostSlot> &from,
		const std::vector<int> &slots) {
	auto checked = base::flat_set<PeerId>();
	checked.reserve(slots.size());
	for (const auto slot : slots) {
		const auto i = ranges::find(from, slot, &TakenBoostSlot::id);
		Assert(i != end(from));
		checked.emplace(i->peerId);
	}
	return checked.size();
}

object_ptr<Ui::BoxContent> ReassignBoostsBox(
		not_null<ChannelData*> to,
		std::vector<TakenBoostSlot> from,
		Fn<void(std::vector<int> slots, int sources)> reassign,
		Fn<void()> cancel) {
	Expects(!from.empty());

	const auto now = base::unixtime::now();
	if (from.size() == 1 && from.front().cooldown > now) {
		cancel();
		return ReassignBoostFloodBox(from.front().cooldown - now);
	} else if (from.size() == 1 && from.front().peerId) {
		return ReassignBoostSingleBox(to, from.front(), reassign, cancel);
	}
	const auto reassigned = std::make_shared<bool>();
	auto controller = std::make_unique<Controller>(to, from);
	const auto raw = controller.get();
	auto initBox = [=](not_null<Ui::BoxContent*> box) {
		raw->selectedValue(
		) | rpl::start_with_next([=](std::vector<int> slots) {
			box->clearButtons();
			if (!slots.empty()) {
				const auto sources = SourcesCount(from, slots);
				box->addButton(tr::lng_boost_reassign_button(), [=] {
					*reassigned = true;
					reassign(slots, sources);
				});
			}
			box->addButton(tr::lng_cancel(), [=] {
				box->closeBox();
			});
		}, box->lifetime());

		box->boxClosing() | rpl::filter([=] {
			return !*reassigned;
		}) | rpl::start_with_next(cancel, box->lifetime());
	};
	return Box<PeerListBox>(std::move(controller), std::move(initBox));
}

object_ptr<Ui::RpWidget> CreateBoostReplaceUserpics(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<std::vector<not_null<PeerData*>>> from,
		not_null<PeerData*> to) {
	struct State {
		std::vector<not_null<PeerData*>> from;
		std::vector<std::unique_ptr<Ui::UserpicButton>> buttons;
		QImage layer;
		rpl::variable<int> count = 0;
		bool painting = false;
	};
	const auto full = st::boostReplaceUserpic.size.height()
		+ st::boostReplaceIconAdd.y()
		+ st::lineWidth;
	auto result = object_ptr<Ui::FixedHeightWidget>(parent, full);
	const auto raw = result.data();
	const auto &st = st::boostReplaceUserpic;
	const auto right = CreateChild<Ui::UserpicButton>(raw, to, st);
	const auto overlay = CreateChild<Ui::RpWidget>(raw);

	const auto state = raw->lifetime().make_state<State>();
	std::move(
		from
	) | rpl::start_with_next([=](
			const std::vector<not_null<PeerData*>> &list) {
		const auto &st = st::boostReplaceUserpic;
		auto was = base::take(state->from);
		auto buttons = base::take(state->buttons);
		state->from.reserve(list.size());
		state->buttons.reserve(list.size());
		for (const auto &peer : list) {
			state->from.push_back(peer);
			const auto i = ranges::find(was, peer);
			if (i != end(was)) {
				const auto index = int(i - begin(was));
				Assert(buttons[index] != nullptr);
				state->buttons.push_back(std::move(buttons[index]));
			} else {
				state->buttons.push_back(
					std::make_unique<Ui::UserpicButton>(raw, peer, st));
				const auto raw = state->buttons.back().get();
				base::install_event_filter(raw, [=](not_null<QEvent*> e) {
					return (e->type() == QEvent::Paint && !state->painting)
						? base::EventFilterResult::Cancel
						: base::EventFilterResult::Continue;
				});
			}
		}
		state->count.force_assign(int(list.size()));
		overlay->update();
	}, raw->lifetime());

	rpl::combine(
		raw->widthValue(),
		state->count.value()
	) | rpl::start_with_next([=](int width, int count) {
		const auto skip = st::boostReplaceUserpicsSkip;
		const auto left = width - 2 * right->width() - skip;
		const auto shift = std::min(
			st::boostReplaceUserpicsShift,
			(count > 1 ? (left / (count - 1)) : width));
		const auto total = right->width()
			+ (count ? (skip + right->width() + (count - 1) * shift) : 0);
		auto x = (width - total) / 2;
		for (const auto &single : state->buttons) {
			single->moveToLeft(x, 0);
			x += shift;
		}
		if (count) {
			x += right->width() - shift + skip;
		}
		right->moveToLeft(x, 0);
		overlay->setGeometry(QRect(0, 0, width, raw->height()));
	}, raw->lifetime());

	overlay->paintRequest(
	) | rpl::filter([=] {
		return !state->buttons.empty();
	}) | rpl::start_with_next([=] {
		const auto outerw = overlay->width();
		const auto ratio = style::DevicePixelRatio();
		if (state->layer.size() != QSize(outerw, full) * ratio) {
			state->layer = QImage(
				QSize(outerw, full) * ratio,
				QImage::Format_ARGB32_Premultiplied);
			state->layer.setDevicePixelRatio(ratio);
		}
		state->layer.fill(Qt::transparent);

		auto q = QPainter(&state->layer);
		auto hq = PainterHighQualityEnabler(q);
		const auto stroke = st::boostReplaceIconOutline;
		const auto half = stroke / 2.;
		auto pen = st::windowBg->p;
		pen.setWidthF(stroke * 2.);
		state->painting = true;
		for (const auto &button : state->buttons) {
			q.setPen(pen);
			q.setBrush(Qt::NoBrush);
			q.drawEllipse(button->geometry());
			const auto position = button->pos();
			button->render(&q, position, QRegion(), QWidget::DrawChildren);
		}
		state->painting = false;
		const auto last = state->buttons.back().get();
		const auto add = st::boostReplaceIconAdd;
		const auto skip = st::boostReplaceIconSkip;
		const auto w = st::boostReplaceIcon.width() + 2 * skip;
		const auto h = st::boostReplaceIcon.height() + 2 * skip;
		const auto x = last->x() + last->width() - w + add.x();
		const auto y = last->y() + last->height() - h + add.y();

		auto brush = QLinearGradient(QPointF(x + w, y + h), QPointF(x, y));
		brush.setStops(Ui::Premium::ButtonGradientStops());
		q.setBrush(brush);
		pen.setWidthF(stroke);
		q.setPen(pen);
		q.drawEllipse(x - half, y - half, w + stroke, h + stroke);
		st::boostReplaceIcon.paint(q, x + skip, y + skip, outerw);

		const auto size = st::boostReplaceArrow.size();
		st::boostReplaceArrow.paint(
			q,
			(last->x()
				+ last->width()
				+ (st::boostReplaceUserpicsSkip - size.width()) / 2),
			(last->height() - size.height()) / 2,
			outerw);

		q.end();

		auto p = QPainter(overlay);
		p.drawImage(0, 0, state->layer);
	}, overlay->lifetime());
	return result;
}
