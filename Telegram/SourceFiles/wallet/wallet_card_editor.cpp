/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_card_editor.h"

#include "data/data_user_wallet_card.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_card_drag.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_own_card.h"
#include "wallet/wallet_session.h"

#include <QtWidgets/QApplication>

#include "styles/style_editor.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kSnapAngle = 45.;
constexpr auto kSamePose = 1e-6;

enum class Zone : uchar {
	None,
	Card,
	Collectibles,
	Transactions,
};

enum class TravelStage : uchar {
	Held,
	Removing,
	Finished,
};

struct CardPose {
	QPointF center;
	float64 side = 0.;
	float64 rotation = 0.;
};

struct Frame {
	QPolygonF outline;
	std::array<QPointF, 2> handles;
};

[[nodiscard]] bool Finite(QPointF point) {
	return std::isfinite(point.x()) && std::isfinite(point.y());
}

[[nodiscard]] bool Crossed(QPoint origin, QPoint now) {
	return (now - origin).manhattanLength()
		>= QApplication::startDragDistance();
}

[[nodiscard]] bool Presented(const CardFold &fold) {
	return fold.valid && (fold.opacity > 0.);
}

[[nodiscard]] float64 ExactSide(const Data::StoryArea &area, QSize card) {
	return std::min({
		area.geometry.width() * card.width(),
		area.geometry.height() * card.height(),
		float64(card.width()),
		float64(card.height()),
	});
}

[[nodiscard]] CardPose AreaPose(const Data::StoryArea &area, QSize card) {
	return {
		.center = CardStickerCenter(area, card),
		.side = ExactSide(area, card),
		.rotation = CardStickerRotation(area),
	};
}

[[nodiscard]] bool SamePose(const CardPose &a, const CardPose &b) {
	const auto offset = a.center - b.center;
	return (std::hypot(offset.x(), offset.y()) < kSamePose)
		&& (std::abs(a.side - b.side) < kSamePose)
		&& (std::abs(std::remainder(a.rotation - b.rotation, 360.))
			< kSamePose);
}

[[nodiscard]] float64 NormalizedRotation(float64 rotation) {
	const auto turn = std::fmod(rotation, 360.);
	const auto positive = (turn < 0.) ? (turn + 360.) : turn;
	return (positive > 0. && positive < 360.) ? positive : 0.;
}

[[nodiscard]] Data::StoryArea LandedArea(const CardPose &pose, QSize card) {
	return CardStickerArea(
		pose.center,
		pose.side,
		NormalizedRotation(pose.rotation),
		card);
}

[[nodiscard]] Data::StoryArea MovedArea(
		Data::StoryArea area,
		QPointF center,
		QSize card) {
	area.geometry.moveCenter(QPointF(
		center.x() / card.width(),
		center.y() / card.height()));
	return area;
}

[[nodiscard]] Data::StoryArea HandledArea(
		Data::StoryArea area,
		const CardPose &from,
		const CardPose &pose,
		QSize card) {
	if (std::abs(pose.side - from.side) >= kSamePose) {
		return LandedArea(pose, card);
	}
	area.rotation = NormalizedRotation(pose.rotation);
	return area;
}

[[nodiscard]] bool OnCard(const CardFold &fold, QPointF point) {
	return Presented(fold)
		&& CardFoldOutline(fold).containsPoint(point, Qt::OddEvenFill);
}

[[nodiscard]] bool OnChip(const CardFold &fold, QPointF point) {
	const auto local = CardFoldLocal(fold, point);
	return Presented(fold)
		&& local
		&& QRectF(CardQrRect(fold.rest.width())).contains(*local);
}

[[nodiscard]] Zone ChooseZone(
		const CardFold &fold,
		QPointF center,
		const QRectF &lists,
		bool collectibles,
		bool transactions) {
	if (!Finite(center)) {
		return Zone::None;
	} else if (OnCard(fold, center)) {
		return Zone::Card;
	} else if (lists.isEmpty() || !lists.contains(center)) {
		return Zone::None;
	} else if (collectibles) {
		return Zone::Collectibles;
	}
	return transactions ? Zone::Transactions : Zone::None;
}

[[nodiscard]] std::optional<FlightPose> StickerPose(
		const CardFold &fold,
		const Data::StoryArea &area,
		int ratio) {
	const auto card = fold.rest.size();
	const auto side = CardStickerSide(area, card);
	const auto center = CardStickerCenter(area, card);
	if (!Presented(fold) || side <= 0 || !Finite(center)) {
		return std::nullopt;
	}
	const auto scale = CardFoldScale(fold, center);
	const auto mapped = CardFoldPoint(fold, center);
	if (!std::isfinite(scale) || scale <= 0. || !Finite(mapped)) {
		return std::nullopt;
	}
	return FlightPose{
		.center = mapped,
		.side = CardStickerOuterSide(side, ratio) * scale,
		.rotation = CardStickerRotation(area),
	};
}

[[nodiscard]] float64 OuterRatio(int side, int ratio) {
	return (side > 0) ? (CardStickerOuterSide(side, ratio) / side) : 1.;
}

[[nodiscard]] std::optional<CardPose> HandlePose(
		const CardPose &from,
		QPointF pressed,
		QPointF cursor,
		float64 least,
		float64 most,
		std::optional<float64> frozen) {
	const auto was = pressed - from.center;
	const auto now = cursor - from.center;
	const auto distance = std::hypot(was.x(), was.y());
	const auto reached = std::hypot(now.x(), now.y());
	if (!std::isfinite(distance)
		|| !std::isfinite(reached)
		|| !std::isfinite(from.side)
		|| !std::isfinite(from.rotation)
		|| distance <= 0.) {
		return std::nullopt;
	}
	const auto turn = (reached > 0.)
		? qRadiansToDegrees(
			std::atan2(now.y(), now.x()) - std::atan2(was.y(), was.x()))
		: 0.;
	const auto rotation = from.rotation + std::remainder(turn, 360.);
	if (frozen) {
		return CardPose{
			.center = from.center,
			.side = *frozen,
			.rotation = base::SafeRound(rotation / kSnapAngle) * kSnapAngle,
		};
	}
	const auto side = from.side * reached / distance;
	return CardPose{
		.center = from.center,
		.side = std::max(least, std::min(side, most)),
		.rotation = rotation,
	};
}

[[nodiscard]] QRectF HandleRect(QPointF center, float64 size) {
	return QRectF(center - QPointF(size, size) / 2., QSizeF(size, size));
}

[[nodiscard]] bool HandleContains(
		QPointF center,
		QPointF point,
		float64 size) {
	return (std::abs(point.x() - center.x()) <= size / 2.)
		&& (std::abs(point.y() - center.y()) <= size / 2.);
}

[[nodiscard]] std::optional<Frame> ComputeFrame(
		const CardFold &fold,
		const Data::StoryArea &area,
		float64 handle) {
	const auto card = fold.rest.size();
	const auto side = CardStickerSide(area, card);
	const auto center = CardStickerCenter(area, card);
	if (!Presented(fold) || side <= 0 || !Finite(center)) {
		return std::nullopt;
	}
	const auto scale = CardFoldScale(fold, center);
	if (!std::isfinite(scale) || scale <= 0.) {
		return std::nullopt;
	}
	const auto half = (side + handle / scale) / 2.;
	const auto rotation = QTransform().rotate(CardStickerRotation(area));
	const auto map = [&](float64 x, float64 y) {
		return CardFoldPoint(fold, center + rotation.map(QPointF(x, y)));
	};
	auto result = Frame{
		.outline = QPolygonF({
			map(-half, -half),
			map(half, -half),
			map(half, half),
			map(-half, half),
		}),
		.handles = { map(-half, 0.), map(half, 0.) },
	};
	if (!ranges::all_of(result.outline, Finite)
		|| !ranges::all_of(result.handles, Finite)) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] QRect FrameBounds(
		const Frame &frame,
		float64 handle,
		float64 stroke) {
	auto result = frame.outline.boundingRect();
	for (const auto &center : frame.handles) {
		result = result.united(HandleRect(center, handle));
	}
	return result.marginsAdded(
		QMarginsF(stroke, stroke, stroke, stroke)
	).toAlignedRect();
}

} // namespace

struct CardStickerEditor::Press {
	QString address;
	QPoint origin;
	QPointF local;
	CardPose from;
	CardPose pose;
	std::optional<Data::StoryArea> preview;
	bool handle = false;
	bool crossed = false;
};

struct CardStickerEditor::Travel {
	QString address;
	std::unique_ptr<DraggedGift> gift;
	uint64 generation = 0;
	TravelStage stage = TravelStage::Held;
};

CardStickerEditor::CardStickerEditor(CardEditorArgs &&args)
: _show(std::move(args.show))
, _host(std::move(args.host))
, _input(args.input)
, _header(args.header)
, _squares(std::move(args.squares))
, _repaint(std::move(args.repaint)) {
	base::install_event_filter(_input, [=](not_null<QEvent*> e) {
		return filterInput(e);
	}, _lifetime);
	base::install_event_filter(_input->window(), [=](not_null<QEvent*> e) {
		return filterPanel(e);
	}, _lifetime);

	OwnCardStickersValue(
		&_show->session()
	) | rpl::on_next([=](const std::vector<Data::WalletCardSticker> &list) {
		reconcile(list);
	}, _lifetime);

	_show->session().wallet().collectiblesTabValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		clear();
	}, _lifetime);

	TransactionsShownValue(
		&_show->session()
	) | rpl::on_next([=](bool shown) {
		_transactions = shown;
	}, _lifetime);
}

CardStickerEditor::~CardStickerEditor() {
	_lifetime.destroy();
	const auto travel = base::take(_travel);
	abandon(base::take(_press));
	updateGrip();
	if (!travel) {
		return;
	} else if (travel->stage != TravelStage::Finished) {
		_squares.leaving(travel->address, false);
	}
	if (travel->stage == TravelStage::Held) {
		if (const auto painter = _host.painter.get()) {
			painter->release(travel->address);
		}
	}
	if (travel->gift->following()) {
		travel->gift->settle(nullptr, nullptr);
	}
	travel->gift->finish();
}

void CardStickerEditor::updateInput() {
	refresh();
}

void CardStickerEditor::paint(QPainter &p, const CardFold &fold) {
	const auto area = decorationArea();
	const auto handle = float64(st::photoEditorItemHandleSize);
	const auto frame = area ? ComputeFrame(fold, *area, handle) : std::nullopt;
	if (!frame) {
		return;
	}
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(p.opacity() * fold.opacity);
	auto pen = QPen(
		QColor(255, 255, 255),
		st::walletCardStickerRingWidth,
		Qt::DashLine,
		Qt::SquareCap,
		Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawPolygon(frame->outline);
	pen.setStyle(Qt::SolidLine);
	p.setPen(pen);
	p.setBrush(st::photoEditorItemBaseHandleFg);
	for (const auto &center : frame->handles) {
		p.drawEllipse(HandleRect(center, handle));
	}
	p.restore();
}

base::EventFilterResult CardStickerEditor::filterInput(
		not_null<QEvent*> e) {
	switch (e->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick:
		return mousePress(static_cast<QMouseEvent*>(e.get()));
	case QEvent::MouseMove:
		return mouseMove(static_cast<QMouseEvent*>(e.get()));
	case QEvent::MouseButtonRelease:
		return mouseRelease(static_cast<QMouseEvent*>(e.get()));
	case QEvent::UngrabMouse:
		cancelPress();
		break;
	default:
		break;
	}
	return base::EventFilterResult::Continue;
}

base::EventFilterResult CardStickerEditor::filterPanel(
		not_null<QEvent*> e) {
	const auto type = e->type();
	if (type == QEvent::KeyPress) {
		return escape(static_cast<QKeyEvent*>(e.get()));
	} else if (type == QEvent::WindowDeactivate) {
		cancelPress();
	} else if (type == QEvent::Hide) {
		clear();
		finishTravel();
		if (const auto painter = _host.painter.get()) {
			painter->stopPlayback();
		}
	}
	return base::EventFilterResult::Continue;
}

base::EventFilterResult CardStickerEditor::mousePress(
		not_null<QMouseEvent*> e) {
	using Result = base::EventFilterResult;
	if (e->button() != Qt::LeftButton) {
		return _press ? Result::Cancel : Result::Continue;
	}
	cancelPress();
	finishTravel();
	const auto fold = _host.fold();
	const auto global = e->globalPosition();
	const auto point = bodyPoint(global);
	if (OnChip(fold, point)) {
		// Deselecting may move the input, so let it take this press first.
		crl::on_main(this, [=] { select(QString()); });
		return Result::Continue;
	} else if (handleAt(fold, point)) {
		startPress(global.toPoint(), point, _selected, true);
		return Result::Cancel;
	}
	const auto address = stickerAt(fold, point);
	if (!address.isEmpty()) {
		select(address);
		startPress(global.toPoint(), point, address, false);
		return Result::Cancel;
	} else if (!_selected.isEmpty()) {
		select(QString());
		return Result::Cancel;
	}
	return Result::Continue;
}

base::EventFilterResult CardStickerEditor::mouseMove(
		not_null<QMouseEvent*> e) {
	using Result = base::EventFilterResult;
	if (!_press) {
		return Result::Continue;
	} else if (!(e->buttons() & Qt::LeftButton)
		|| QWidget::mouseGrabber() != _input.get()) {
		cancelPress();
		return Result::Cancel;
	}
	const auto global = e->globalPosition();
	if (Crossed(_press->origin, global.toPoint())) {
		_press->crossed = true;
	}
	if (!_press->crossed) {
		return Result::Cancel;
	} else if (_press->handle) {
		if (!updateHandle(global, e->modifiers())) {
			cancelPress();
		}
		return Result::Cancel;
	} else if (!moving()) {
		startMove(global);
	}
	if (moving() && !updateMove(global)) {
		cancelPress();
	}
	return Result::Cancel;
}

base::EventFilterResult CardStickerEditor::mouseRelease(
		not_null<QMouseEvent*> e) {
	using Result = base::EventFilterResult;
	if (!_press) {
		return Result::Continue;
	} else if (e->button() != Qt::LeftButton) {
		if (!(e->buttons() & Qt::LeftButton)) {
			cancelPress();
		}
		return Result::Cancel;
	} else if (QWidget::mouseGrabber() != _input.get()) {
		cancelPress();
		return Result::Cancel;
	}
	const auto global = e->globalPosition();
	if (Crossed(_press->origin, global.toPoint())) {
		_press->crossed = true;
	}
	const auto handle = _press->handle && _press->crossed;
	const auto body = !_press->handle && _press->crossed;
	if (body && !moving()) {
		startMove(global);
	}
	const auto valid = !handle || updateHandle(global, e->modifiers());
	auto press = base::take(_press);
	releaseGrab();
	if (!valid || !press) {
		abandon(std::move(press));
	} else if (handle) {
		finishHandle(*press);
	} else if (body) {
		finishMove(*press, global);
	} else if (!press->handle) {
		finishClick(*press, bodyPoint(global));
	}
	refresh();
	return Result::Cancel;
}

base::EventFilterResult CardStickerEditor::escape(not_null<QKeyEvent*> e) {
	using Result = base::EventFilterResult;
	if (e->key() != Qt::Key_Escape) {
		return Result::Continue;
	} else if (_press || !_selected.isEmpty()) {
		clear();
		_escapeHeld = true;
		return Result::Cancel;
	}
	const auto repeat = e->isAutoRepeat();
	if (!repeat) {
		_escapeHeld = false;
	}
	return (repeat && _escapeHeld) ? Result::Cancel : Result::Continue;
}

void CardStickerEditor::startPress(
		QPoint origin,
		QPointF point,
		const QString &address,
		bool handle) {
	const auto fold = _host.fold();
	const auto local = CardFoldLocal(fold, point);
	const auto area = shownArea(address);
	if (!local || !area) {
		return;
	}
	const auto from = AreaPose(*area, fold.rest.size());
	_press = std::make_unique<Press>(Press{
		.address = address,
		.origin = origin,
		.local = *local,
		.from = from,
		.pose = from,
		.handle = handle,
	});
	_input->grabMouse();
	updateGrip();
}

bool CardStickerEditor::updateHandle(
		QPointF global,
		Qt::KeyboardModifiers modifiers) {
	const auto fold = _host.fold();
	const auto cursor = CardFoldLocal(fold, bodyPoint(global));
	const auto painter = _host.painter.get();
	if (!_press || !Presented(fold) || !cursor || !painter) {
		return false;
	}
	auto &press = *_press;
	const auto card = fold.rest.size();
	const auto shift = modifiers.testFlag(Qt::ShiftModifier);
	const auto pose = HandlePose(
		press.from,
		press.local,
		*cursor,
		CardStickerMinimumSide(press.from.side, card),
		CardStickerPreferredSide(card),
		shift ? std::make_optional(press.pose.side) : std::nullopt);
	if (!pose) {
		return false;
	}
	press.pose = *pose;
	press.preview = CardStickerArea(
		pose->center,
		pose->side,
		pose->rotation,
		card);
	painter->preview(press.address, *press.preview);
	refresh();
	return true;
}

void CardStickerEditor::finishHandle(const Press &press) {
	const auto card = _host.fold().rest.size();
	const auto area = shownArea(press.address);
	const auto clearPreview = [&] {
		if (const auto painter = _host.painter.get()) {
			painter->clearPreview();
		}
	};
	if (!area || SamePose(press.pose, AreaPose(*area, card))) {
		clearPreview();
		return;
	}
	const auto change = saveArea(
		press.address,
		HandledArea(*area, press.from, press.pose, card));
	if (change == OwnCardChange::Applied) {
		if (const auto painter = _host.painter.get()) {
			painter->land(press.address);
		}
	}
	clearPreview();
	showRefusal(change);
}

void CardStickerEditor::finishClick(const Press &press, QPointF point) {
	if (stickerAt(_host.fold(), point) != press.address) {
		return;
	} else if (const auto painter = _host.painter.get()) {
		painter->play(press.address);
	}
}

void CardStickerEditor::startMove(QPointF global) {
	finishTravel();
	const auto painter = _host.painter.get();
	if (!_press || !painter) {
		return;
	}
	const auto address = _press->address;
	const auto fold = _host.fold();
	const auto area = shownArea(address);
	const auto source = area
		? StickerPose(fold, *area, style::DevicePixelRatio())
		: std::nullopt;
	auto image = painter->image(address);
	if (!source || image.isNull() || !painter->shown(address)) {
		return;
	}
	auto gift = std::make_unique<DraggedGift>(DraggedGiftArgs{
		.body = _host.body,
		.titleExtension = st::separatePanelTitleHeight,
		.image = std::move(image),
		.source = *source,
		.cursor = bodyPoint(global),
		.grab = CardFoldPoint(fold, _press->local) - source->center,
		.origin = DraggedGiftSource::Card,
		.moved = [=](const FlightPose &pose) {
			showLeaving(pose.center);
		},
	});
	_travel = std::make_unique<Travel>(Travel{
		.address = address,
		.gift = std::move(gift),
		.generation = ++_lastGeneration,
	});
	painter->clearPreview();
	painter->hold(address);
	refresh();
}

bool CardStickerEditor::updateMove(QPointF global) {
	if (!_press || !moving() || !Presented(_host.fold())) {
		return false;
	}
	_travel->gift->move(bodyPoint(global));
	return true;
}

void CardStickerEditor::finishMove(const Press &press, QPointF global) {
	if (!moving()) {
		return;
	}
	const auto point = bodyPoint(global);
	_travel->gift->move(point);
	const auto zone = ChooseZone(
		_host.fold(),
		_travel->gift->pose().center,
		listsArea(),
		_show->session().wallet().collectiblesTab(),
		_transactions);
	if (zone == Zone::Card) {
		dropMoved(press, point);
	} else if (zone == Zone::None) {
		returnMoved();
	} else {
		removeMoved(zone == Zone::Collectibles);
	}
}

void CardStickerEditor::dropMoved(const Press &press, QPointF point) {
	const auto fold = _host.fold();
	const auto card = fold.rest.size();
	const auto local = CardFoldLocal(fold, point);
	const auto area = shownArea(press.address);
	const auto center = local
		? CardStickerClampedCenter(
			press.from.center + (*local - press.local),
			card)
		: std::nullopt;
	if (!center || !area) {
		returnMoved();
		return;
	}
	const auto shown = AreaPose(*area, card);
	const auto landed = CardPose{
		.center = *center,
		.side = shown.side,
		.rotation = shown.rotation,
	};
	if (SamePose(landed, shown)) {
		returnMoved();
		return;
	}
	const auto change = saveArea(
		press.address,
		MovedArea(*area, *center, card));
	if (change == OwnCardChange::Applied) {
		settleMoved(true);
	} else {
		showRefusal(change);
		returnMoved();
	}
}

void CardStickerEditor::removeMoved(bool collectibles) {
	const auto generation = _travel->generation;
	const auto address = _travel->address;
	const auto area = shownArea(address);
	const auto ratio = area
		? OuterRatio(
			CardStickerSide(*area, _host.fold().rest.size()),
			style::DevicePixelRatio())
		: 1.;
	// WHY: the save emits the shown set synchronously, and the reconcile
	// it runs would abort a travel whose sticker vanished unless it already
	// reads as removing.
	_travel->stage = TravelStage::Removing;
	auto desired = ownCard().shown();
	const auto change = RemoveOwnCardSticker(desired, address)
		? saveStacked(std::move(desired))
		: OwnCardChange::Unchanged;
	if (!current(generation)) {
		return;
	} else if (change != OwnCardChange::Applied) {
		_travel->stage = TravelStage::Held;
		showRefusal(change);
		returnMoved();
		return;
	}
	if (const auto painter = _host.painter.get()) {
		painter->release(address);
	}
	select(QString());
	const auto square = [=]() -> std::optional<FlightPose> {
		auto result = _squares.pose(address);
		if (result) {
			result->side *= ratio;
		}
		return result;
	};
	if (collectibles && square()) {
		_travel->gift->returnTo(square, [=](bool) {
			removed(generation);
		});
	} else {
		_travel->gift->scaleOut([=] {
			removed(generation);
		});
	}
}

void CardStickerEditor::returnMoved() {
	settleMoved(false);
}

void CardStickerEditor::settleMoved(bool land) {
	if (!_travel || !_travel->gift->following()) {
		return;
	}
	const auto generation = _travel->generation;
	const auto address = _travel->address;
	_squares.leaving(address, false);
	_travel->gift->settle([=] {
		return cardPose(address);
	}, [=](bool arrived) {
		settled(generation, land && arrived);
	});
}

void CardStickerEditor::settled(uint64 generation, bool land) {
	if (!current(generation)) {
		return;
	}
	const auto address = _travel->address;
	retireTravel();
	if (const auto painter = _host.painter.get()) {
		if (land && cardPose(address)) {
			painter->land(address);
		} else {
			painter->release(address);
		}
	}
	refresh();
}

void CardStickerEditor::removed(uint64 generation) {
	if (!current(generation)) {
		return;
	}
	const auto address = _travel->address;
	retireTravel();
	updateGrip();
	_squares.leaving(address, false);
}

void CardStickerEditor::retireTravel() {
	const auto generation = _travel->generation;
	_travel->stage = TravelStage::Finished;
	crl::on_main(this, [=] {
		if (_travel && _travel->generation == generation) {
			_travel = nullptr;
		}
	});
}

void CardStickerEditor::finishTravel() {
	returnMoved();
	if (_travel) {
		_travel->gift->finish();
	}
}

void CardStickerEditor::showLeaving(QPointF center) {
	if (!moving()) {
		return;
	}
	const auto zone = ChooseZone(
		_host.fold(),
		center,
		listsArea(),
		_show->session().wallet().collectiblesTab(),
		_transactions);
	_squares.leaving(_travel->address, zone == Zone::Collectibles);
}

void CardStickerEditor::showRefusal(OwnCardChange change) {
	const auto text = OwnCardRefusalText(change);
	if (!text.isEmpty()) {
		_show->showToast(text);
	}
}

OwnCardChange CardStickerEditor::saveArea(
		const QString &address,
		Data::StoryArea area) {
	auto desired = ownCard().shown();
	const auto same = [&](const Data::WalletCardSticker &sticker) {
		return (CanonicalAddress(sticker.address) == address);
	};
	const auto i = ranges::find_if(desired.stickers, same);
	if (i == end(desired.stickers)) {
		return OwnCardChange::Unchanged;
	}
	area.radius = i->area.radius;
	if (i->area == area) {
		return OwnCardChange::Unchanged;
	}
	i->area = area;
	return saveStacked(std::move(desired));
}

OwnCardChange CardStickerEditor::saveStacked(OwnCardState desired) {
	if (const auto painter = _host.painter.get()) {
		StackCardStickers(desired.stickers, painter->stack());
	}
	return ownCard().save(std::move(desired));
}

void CardStickerEditor::reconcile(
		const std::vector<Data::WalletCardSticker> &stickers) {
	_placed.clear();
	for (const auto &sticker : stickers) {
		_placed.push_back({
			.address = CanonicalAddress(sticker.address),
			.area = sticker.area,
		});
	}
	const auto present = [&](const QString &address) {
		return ranges::contains(_placed, address, &Placed::address);
	};
	if (_press && !present(_press->address)) {
		abandon(base::take(_press));
	}
	if (_travel) {
		const auto stage = _travel->stage;
		const auto there = present(_travel->address);
		if ((stage == TravelStage::Held && !there)
			|| (stage == TravelStage::Removing && there)) {
			finishTravel();
		}
	}
	if (!_selected.isEmpty() && !present(_selected)) {
		_selected = QString();
	}
	refresh();
}

void CardStickerEditor::select(const QString &address) {
	if (_selected != address) {
		_selected = address;
		refresh();
	}
}

void CardStickerEditor::cancelPress() {
	if (_press) {
		abandon(base::take(_press));
		refresh();
	}
}

void CardStickerEditor::clear() {
	_selected = QString();
	abandon(base::take(_press));
	refresh();
}

void CardStickerEditor::abandon(std::unique_ptr<Press> press) {
	releaseGrab();
	if (press && press->preview) {
		if (const auto painter = _host.painter.get()) {
			painter->clearPreview();
		}
	}
	returnMoved();
}

void CardStickerEditor::releaseGrab() {
	if (QWidget::mouseGrabber() == _input.get()) {
		_input->releaseMouse();
	}
}

void CardStickerEditor::updateGrip() {
	const auto desired = _press
		? _press->address
		: (_travel && _travel->stage == TravelStage::Held)
		? _travel->address
		: QString();
	const auto was = std::exchange(_gripped, desired);
	if (was == desired) {
		return;
	} else if (const auto painter = _host.painter.get()) {
		if (!desired.isEmpty()) {
			painter->grip(desired);
		}
		if (!was.isEmpty()) {
			painter->ungrip(was);
		}
	}
}

void CardStickerEditor::refresh() {
	const auto fold = _host.fold();
	const auto presented = Presented(fold);
	if (!presented) {
		_selected = QString();
		abandon(base::take(_press));
	} else if (moving()) {
		showLeaving(_travel->gift->pose().center);
	}
	updateGrip();
	const auto handle = float64(st::photoEditorItemHandleSize);
	const auto area = presented ? decorationArea() : std::nullopt;
	const auto frame = area ? ComputeFrame(fold, *area, handle) : std::nullopt;
	if (presented) {
		const auto outline = CardFoldOutline(fold);
		auto bounds = outline.boundingRect().toAlignedRect();
		auto handles = QRegion();
		if (frame) {
			for (const auto &center : frame->handles) {
				handles += HandleRect(center, handle).toAlignedRect();
			}
			bounds = bounds.united(handles.boundingRect());
		}
		const auto origin = bounds.topLeft();
		const auto mask = QRegion(outline.translated(-origin).toPolygon());
		_input->setGeometry(bounds);
		_input->setMask(mask.united(handles.translated(-origin)));
	}
	_input->setVisible(presented);
	const auto painted = frame
		? FrameBounds(*frame, handle, st::walletCardStickerRingWidth)
		: QRect();
	if (_painted != painted) {
		const auto dirty = _painted.united(painted);
		_painted = painted;
		_repaint(dirty);
	}
}

bool CardStickerEditor::moving() const {
	return _travel
		&& (_travel->stage == TravelStage::Held)
		&& _travel->gift->following();
}

bool CardStickerEditor::current(uint64 generation) const {
	return _travel
		&& (_travel->generation == generation)
		&& (_travel->stage != TravelStage::Finished);
}

bool CardStickerEditor::handleAt(const CardFold &fold, QPointF point) const {
	const auto area = decorationArea();
	const auto handle = float64(st::photoEditorItemHandleSize);
	const auto frame = area ? ComputeFrame(fold, *area, handle) : std::nullopt;
	return frame && ranges::any_of(frame->handles, [&](QPointF center) {
		return HandleContains(center, point, handle);
	});
}

QString CardStickerEditor::stickerAt(
		const CardFold &fold,
		QPointF point) const {
	const auto painter = _host.painter.get();
	const auto local = CardFoldLocal(fold, point);
	if (!painter || !local || !OnCard(fold, point)) {
		return QString();
	}
	const auto card = fold.rest.size();
	const auto ratio = style::DevicePixelRatio();
	const auto stack = painter->stack();
	for (const auto &address : stack | ranges::views::reverse) {
		const auto area = shownArea(address);
		if (area
			&& painter->shown(address)
			&& CardStickerContains(*area, card, ratio, *local)) {
			return address;
		}
	}
	return QString();
}

std::optional<Data::StoryArea> CardStickerEditor::shownArea(
		const QString &address) const {
	const auto i = ranges::find(_placed, address, &Placed::address);
	return (i != end(_placed))
		? std::make_optional(i->area)
		: std::nullopt;
}

std::optional<Data::StoryArea> CardStickerEditor::decorationArea() const {
	const auto painter = _host.painter.get();
	if (_selected.isEmpty() || !painter || !painter->shown(_selected)) {
		return std::nullopt;
	} else if (_press && _press->preview) {
		return _press->preview;
	}
	return shownArea(_selected);
}

std::optional<FlightPose> CardStickerEditor::cardPose(
		const QString &address) const {
	const auto area = shownArea(address);
	return area
		? StickerPose(_host.fold(), *area, style::DevicePixelRatio())
		: std::nullopt;
}

QRectF CardStickerEditor::listsArea() const {
	const auto body = _host.body;
	const auto bottom = QPoint(0, _header->height());
	const auto top = Ui::MapFrom(body, _header, bottom).y();
	return QRectF(0., top, body->width(), body->height() - top);
}

QPointF CardStickerEditor::bodyPoint(QPointF global) const {
	const auto body = static_cast<const QWidget*>(_host.body.get());
	return body->mapFromGlobal(global);
}

OwnCard &CardStickerEditor::ownCard() const {
	return _show->session().wallet().ownCard();
}

} // namespace Wallet
