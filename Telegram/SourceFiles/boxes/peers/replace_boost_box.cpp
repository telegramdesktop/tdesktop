/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/replace_boost_box.h"

#include "api/api_peer_colors.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "base/unixtime.h"
#include "data/data_premium_limits.h"
#include "boxes/peer_list_box.h"
#include "data/data_channel.h"
#include "data/data_cloud_themes.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
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
		CreateUserpicsTransfer(
			above.data(),
			_selectedPeers.value(),
			_to,
			UserpicsTransferType::BoostReplace),
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
		st::boxRowPadding,
		style::al_top);
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

object_ptr<Ui::BoxContent> ReassignBoostFloodBox(int seconds, bool group) {
	const auto days = seconds / 86400;
	const auto hours = seconds / 3600;
	const auto minutes = seconds / 60;
	return Ui::MakeInformBox({
		.text = (group
			? tr::lng_boost_error_flood_text_group
			: tr::lng_boost_error_flood_text)(
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
		Fn<void(std::vector<int> slots, int groups, int channels)> reassign,
		Fn<void()> cancel) {
	const auto reassigned = std::make_shared<bool>();
	const auto slot = from.id;
	const auto peer = to->owner().peer(from.peerId);
	const auto group = peer->isMegagroup();
	const auto confirmed = [=](Fn<void()> close) {
		*reassigned = true;
		reassign({ slot }, group ? 1 : 0, group ? 0 : 1);
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
			CreateUserpicsTransfer(
				box,
				rpl::single(std::vector{ peer }),
				to,
				UserpicsTransferType::BoostReplace),
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

Ui::BoostFeatures LookupBoostFeatures(not_null<ChannelData*> channel) {
	auto nameColorsByLevel = base::flat_map<int, int>();
	auto linkStylesByLevel = base::flat_map<int, int>();
	const auto group = channel->isMegagroup();
	const auto peerColors = &channel->session().api().peerColors();
	const auto &list = group
		? peerColors->requiredLevelsGroup()
		: peerColors->requiredLevelsChannel();
	const auto indices = peerColors->indicesCurrent();
	for (const auto &[index, level] : list) {
		if (!Ui::ColorPatternIndex(indices, index, false)) {
			++nameColorsByLevel[level];
		}
		++linkStylesByLevel[level];
	}
	const auto &themes = channel->owner().cloudThemes().chatThemes();
	if (themes.empty()) {
		channel->owner().cloudThemes().refreshChatThemes();
	}
	const auto levelLimits = Data::LevelLimits(&channel->session());
	return Ui::BoostFeatures{
		.nameColorsByLevel = std::move(nameColorsByLevel),
		.linkStylesByLevel = std::move(linkStylesByLevel),
		.linkLogoLevel = group ? 0 : levelLimits.channelBgIconLevelMin(),
		.autotranslateLevel = group ? 0 : levelLimits.channelAutoTranslateLevelMin(),
		.transcribeLevel = group ? levelLimits.groupTranscribeLevelMin() : 0,
		.emojiPackLevel = group ? levelLimits.groupEmojiStickersLevelMin() : 0,
		.emojiStatusLevel = group
			? levelLimits.groupEmojiStatusLevelMin()
			: levelLimits.channelEmojiStatusLevelMin(),
		.wallpaperLevel = group
			? levelLimits.groupWallpaperLevelMin()
			: levelLimits.channelWallpaperLevelMin(),
		.wallpapersCount = themes.empty() ? 8 : int(themes.size()),
		.customWallpaperLevel = group
			? levelLimits.groupCustomWallpaperLevelMin()
			: levelLimits.channelCustomWallpaperLevelMin(),
		.sponsoredLevel = levelLimits.channelRestrictSponsoredLevelMin(),
	};
}

int BoostsForGift(not_null<Main::Session*> session) {
	return session->appConfig().get<int>(u"boosts_per_sent_gift"_q, 0);
}

struct Sources {
	int groups = 0;
	int channels = 0;
};
[[nodiscard]] Sources SourcesCount(
		not_null<ChannelData*> to,
		const std::vector<TakenBoostSlot> &from,
		const std::vector<int> &slots) {
	auto groups = base::flat_set<PeerId>();
	groups.reserve(slots.size());
	auto channels = base::flat_set<PeerId>();
	channels.reserve(slots.size());
	const auto owner = &to->owner();
	for (const auto slot : slots) {
		const auto i = ranges::find(from, slot, &TakenBoostSlot::id);
		Assert(i != end(from));
		const auto id = i->peerId;
		if (!groups.contains(id) && !channels.contains(id)) {
			(owner->peer(id)->isMegagroup() ? groups : channels).insert(id);
		}
	}
	return {
		.groups = int(groups.size()),
		.channels = int(channels.size()),
	};
}

object_ptr<Ui::BoxContent> ReassignBoostsBox(
		not_null<ChannelData*> to,
		std::vector<TakenBoostSlot> from,
		Fn<void(std::vector<int> slots, int groups, int channels)> reassign,
		Fn<void()> cancel) {
	Expects(!from.empty());

	const auto now = base::unixtime::now();
	if (from.size() == 1 && from.front().cooldown > now) {
		cancel();
		return ReassignBoostFloodBox(
			from.front().cooldown - now,
			to->owner().peer(from.front().peerId)->isMegagroup());
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
				const auto sources = SourcesCount(to, from, slots);
				box->addButton(tr::lng_boost_reassign_button(), [=] {
					*reassigned = true;
					reassign(slots, sources.groups, sources.channels);
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

object_ptr<Ui::RpWidget> CreateUserpicsTransfer(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<std::vector<not_null<PeerData*>>> from,
		not_null<PeerData*> to,
		UserpicsTransferType type) {
	struct State {
		std::vector<not_null<PeerData*>> from;
		std::vector<std::unique_ptr<Ui::UserpicButton>> buttons;
		QImage layer;
		rpl::variable<int> count = 0;
		bool painting = false;
	};
	const auto st = &st::boostReplaceUserpicsRow;
	const auto full = st->button.size.height()
		+ st::boostReplaceIconAdd.y()
		+ st::lineWidth;
	auto result = object_ptr<Ui::FixedHeightWidget>(parent, full);
	const auto raw = result.data();
	const auto right = CreateChild<Ui::UserpicButton>(raw, to, st->button);
	const auto overlay = CreateChild<Ui::RpWidget>(raw);

	const auto state = raw->lifetime().make_state<State>();
	std::move(
		from
	) | rpl::start_with_next([=](
			const std::vector<not_null<PeerData*>> &list) {
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
					std::make_unique<Ui::UserpicButton>(raw, peer, st->button));
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
			st->shift,
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
		const auto stroke = st->stroke;
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
		const auto boosting = (type == UserpicsTransferType::BoostReplace);
		const auto last = state->buttons.back().get();
		const auto back = boosting ? last : right;
		const auto add = st::boostReplaceIconAdd;
		const auto &icon = boosting
			? st::boostReplaceIcon
			: st::starrefJoinIcon;
		const auto skip = boosting ? st::boostReplaceIconSkip : 0;
		const auto w = icon.width() + 2 * skip;
		const auto h = icon.height() + 2 * skip;
		const auto x = back->x() + back->width() - w + add.x();
		const auto y = back->y() + back->height() - h + add.y();

		auto brush = QLinearGradient(QPointF(x + w, y + h), QPointF(x, y));
		brush.setStops(Ui::Premium::ButtonGradientStops());
		q.setBrush(brush);
		pen.setWidthF(stroke);
		q.setPen(pen);
		q.drawEllipse(x - half, y - half, w + stroke, h + stroke);
		icon.paint(q, x + skip, y + skip, outerw);

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

object_ptr<Ui::RpWidget> CreateUserpicsWithMoreBadge(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<std::vector<not_null<PeerData*>>> peers,
		const style::UserpicsRow &st,
		int limit) {
	struct State {
		std::vector<not_null<PeerData*>> from;
		std::vector<std::unique_ptr<Ui::UserpicButton>> buttons;
		QImage layer;
		QImage badge;
		rpl::variable<int> count = 0;
		bool painting = false;
	};
	const auto full = st.button.size.height()
		+ (st.complex ? (st::boostReplaceIconAdd.y() + st::lineWidth) : 0);
	auto result = object_ptr<Ui::FixedHeightWidget>(parent, full);
	const auto raw = result.data();
	const auto overlay = CreateChild<Ui::RpWidget>(raw);

	const auto state = raw->lifetime().make_state<State>();
	std::move(
		peers
	) | rpl::start_with_next([=, &st](
			const std::vector<not_null<PeerData*>> &list) {
		auto was = base::take(state->from);
		auto buttons = base::take(state->buttons);
		state->from.reserve(list.size());
		state->buttons.reserve(list.size());
		for (const auto &peer : list | ranges::views::take(limit)) {
			state->from.push_back(peer);
			const auto i = ranges::find(was, peer);
			if (i != end(was)) {
				const auto index = int(i - begin(was));
				Assert(buttons[index] != nullptr);
				state->buttons.push_back(std::move(buttons[index]));
			} else {
				state->buttons.push_back(
					std::make_unique<Ui::UserpicButton>(raw, peer, st.button));
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

	if (const auto count = state->count.current()) {
		const auto single = st.button.size.width();
		const auto used = std::min(count, int(state->buttons.size()));
		const auto shift = st.shift;
		raw->resize(used ? (single + (used - 1) * shift) : 0, raw->height());
	}
	rpl::combine(
		raw->widthValue(),
		state->count.value()
	) | rpl::start_with_next([=, &st](int width, int count) {
		const auto single = st.button.size.width();
		const auto left = width - single;
		const auto used = std::min(count, int(state->buttons.size()));
		const auto shift = std::min(
			st.shift,
			(used > 1 ? (left / (used - 1)) : width));
		const auto total = used ? (single + (used - 1) * shift) : 0;
		auto x = (width - total) / 2;
		for (const auto &single : state->buttons) {
			single->moveToLeft(x, 0);
			x += shift;
		}
		overlay->setGeometry(QRect(0, 0, width, raw->height()));
	}, raw->lifetime());

	overlay->paintRequest(
	) | rpl::filter([=] {
		return !state->buttons.empty();
	}) | rpl::start_with_next([=, &st] {
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
		const auto stroke = st.stroke;
		const auto half = stroke / 2.;
		auto pen = st.bg->p;
		pen.setWidthF(stroke * 2.);
		state->painting = true;
		const auto paintOne = [&](not_null<Ui::UserpicButton*> button) {
			q.setPen(pen);
			q.setBrush(Qt::NoBrush);
			q.drawEllipse(button->geometry());
			const auto position = button->pos();
			button->render(&q, position, QRegion(), QWidget::DrawChildren);
		};
		if (st.invert) {
			for (const auto &button : ranges::views::reverse(state->buttons)) {
				paintOne(button.get());
			}
		} else {
			for (const auto &button : state->buttons) {
				paintOne(button.get());
			}
		}
		state->painting = false;

		const auto text = (state->count.current() > limit)
			? ('+' + QString::number(state->count.current() - limit))
			: QString();
		if (st.complex && !text.isEmpty()) {
			const auto last = state->buttons.back().get();
			const auto add = st::boostReplaceIconAdd;
			const auto skip = st::boostReplaceIconSkip;
			const auto w = st::boostReplaceIcon.width() + 2 * skip;
			const auto h = st::boostReplaceIcon.height() + 2 * skip;
			const auto x = last->x() + last->width() - w + add.x();
			const auto y = last->y() + last->height() - h + add.y();
			const auto &font = st::semiboldFont;
			const auto width = font->width(text);
			const auto padded = std::max(w, width + 2 * font->spacew);
			const auto rect = QRect(x - (padded - w) / 2, y, padded, h);
			auto brush = QLinearGradient(rect.bottomRight(), rect.topLeft());
			brush.setStops(Ui::Premium::ButtonGradientStops());
			q.setBrush(brush);
			pen.setWidthF(stroke);
			q.setPen(pen);
			const auto rectf = QRectF(rect);
			const auto radius = std::min(rect.width(), rect.height()) / 2.;
			q.drawRoundedRect(
				rectf.marginsAdded(QMarginsF{ half, half, half, half }),
				radius,
				radius);
			q.setFont(font);
			q.setPen(st::premiumButtonFg);
			q.drawText(rect, Qt::AlignCenter, text);
		}
		q.end();

		auto p = QPainter(overlay);
		p.drawImage(0, 0, state->layer);
	}, overlay->lifetime());
	return result;
}
