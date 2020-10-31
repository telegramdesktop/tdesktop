/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_drag_area.h"

#include "base/event_filter.h"
#include "boxes/confirm_box.h"
#include "boxes/sticker_set_box.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "dialogs/dialogs_layout.h"
#include "history/history_widget.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "ui/widgets/shadow.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "storage/storage_media_prepare.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kDragAreaEvents = {
	QEvent::DragEnter,
	QEvent::DragLeave,
	QEvent::Drop,
	QEvent::MouseButtonRelease,
	QEvent::Leave,
};

inline auto InnerRect(not_null<Ui::RpWidget*> widget) {
	return QRect(
		st::dragPadding.left(),
		st::dragPadding.top(),
		widget->width() - st::dragPadding.left() - st::dragPadding.right(),
		widget->height() - st::dragPadding.top() - st::dragPadding.bottom());
}

} // namespace

DragArea::Areas DragArea::SetupDragAreaToContainer(
		not_null<Ui::RpWidget*> container,
		Fn<bool(not_null<const QMimeData*>)> &&dragEnterFilter,
		Fn<void(bool)> &&setAcceptDropsField,
		Fn<void()> &&updateControlsGeometry,
		DragArea::CallbackComputeState &&computeState) {

	using DragState = Storage::MimeDataState;

	auto &lifetime = container->lifetime();
	container->setAcceptDrops(true);

	const auto attachDragDocument =
		Ui::CreateChild<DragArea>(container.get());
	const auto attachDragPhoto = Ui::CreateChild<DragArea>(container.get());

	attachDragDocument->hide();
	attachDragPhoto->hide();

	attachDragDocument->raise();
	attachDragPhoto->raise();

	const auto attachDragState =
		lifetime.make_state<DragState>(DragState::None);

	const auto width = [=] {
		return container->width();
	};
	const auto height = [=] {
		return container->height();
	};

	const auto horizontalMargins = st::dragMargin.left()
		+ st::dragMargin.right();
	const auto verticalMargins = st::dragMargin.top()
		+ st::dragMargin.bottom();
	const auto resizeToFull = [=](not_null<DragArea*> w) {
		w->resize(width() - horizontalMargins, height() - verticalMargins);
	};
	const auto moveToTop = [=](not_null<DragArea*> w) {
		w->move(st::dragMargin.left(), st::dragMargin.top());
	};
	const auto updateAttachGeometry = crl::guard(container, [=] {
		if (updateControlsGeometry) {
			updateControlsGeometry();
		}

		switch (*attachDragState) {
		case DragState::Files:
			resizeToFull(attachDragDocument);
			moveToTop(attachDragDocument);
		break;
		case DragState::PhotoFiles:
			attachDragDocument->resize(
				width() - horizontalMargins,
				(height() - verticalMargins) / 2);
			moveToTop(attachDragDocument);
			attachDragPhoto->resize(
				attachDragDocument->width(),
				attachDragDocument->height());
			attachDragPhoto->move(
				st::dragMargin.left(),
				height()
					- attachDragPhoto->height()
					- st::dragMargin.bottom());
		break;
		case DragState::Image:
			resizeToFull(attachDragPhoto);
			moveToTop(attachDragPhoto);
		break;
		}
	});

	const auto updateDragAreas = [=] {
		if (setAcceptDropsField) {
			setAcceptDropsField(*attachDragState == DragState::None);
		}
		updateAttachGeometry();

		switch (*attachDragState) {
		case DragState::None:
			attachDragDocument->otherLeave();
			attachDragPhoto->otherLeave();
		break;
		case DragState::Files:
			attachDragDocument->setText(
				tr::lng_drag_files_here(tr::now),
				tr::lng_drag_to_send_files(tr::now));
			attachDragDocument->otherEnter();
			attachDragPhoto->hideFast();
		break;
		case DragState::PhotoFiles:
			attachDragDocument->setText(
				tr::lng_drag_images_here(tr::now),
				tr::lng_drag_to_send_no_compression(tr::now));
			attachDragPhoto->setText(
				tr::lng_drag_photos_here(tr::now),
				tr::lng_drag_to_send_quick(tr::now));
			attachDragDocument->otherEnter();
			attachDragPhoto->otherEnter();
		break;
		case DragState::Image:
			attachDragPhoto->setText(
				tr::lng_drag_images_here(tr::now),
				tr::lng_drag_to_send_quick(tr::now));
			attachDragDocument->hideFast();
			attachDragPhoto->otherEnter();
		break;
		};
	};

	container->sizeValue(
	) | rpl::start_with_next(updateAttachGeometry, lifetime);

	const auto resetDragStateIfNeeded = [=] {
		if (*attachDragState != DragState::None
			|| !attachDragPhoto->isHidden()
			|| !attachDragDocument->isHidden()) {
			*attachDragState = DragState::None;
			updateDragAreas();
		}
	};

	const auto dragEnterEvent = [=](QDragEnterEvent *e) {
		if (dragEnterFilter && !dragEnterFilter(e->mimeData())) {
			return;
		}

		*attachDragState = computeState
			? computeState(e->mimeData())
			: Storage::ComputeMimeDataState(e->mimeData());
		updateDragAreas();

		if (*attachDragState != DragState::None) {
			e->setDropAction(Qt::IgnoreAction);
			e->accept();
		}
	};

	const auto dragLeaveEvent = [=](QDragLeaveEvent *e) {
		resetDragStateIfNeeded();
	};

	const auto dropEvent = [=](QDropEvent *e) {
		// Hide fast to avoid visual bugs in resizable boxes.
		attachDragDocument->hideFast();
		attachDragPhoto->hideFast();

		*attachDragState = DragState::None;
		updateDragAreas();
		e->acceptProposedAction();
	};

	const auto processDragEvents = [=](not_null<QEvent*> event) {
		switch (event->type()) {
		case QEvent::DragEnter:
			dragEnterEvent(static_cast<QDragEnterEvent*>(event.get()));
			return true;
		case QEvent::DragLeave:
			dragLeaveEvent(static_cast<QDragLeaveEvent*>(event.get()));
			return true;
		case QEvent::Drop:
			dropEvent(static_cast<QDropEvent*>(event.get()));
			return true;
		};
		return false;
	};

	container->events(
	) | rpl::filter([=](not_null<QEvent*> event) {
		return ranges::contains(kDragAreaEvents, event->type());
	}) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();

		if (processDragEvents(event)) {
			return;
		} else if (type == QEvent::Leave
			|| type == QEvent::MouseButtonRelease) {
			resetDragStateIfNeeded();
		}
	}, lifetime);

	const auto eventFilter = [=](not_null<QEvent*> event) {
		processDragEvents(event);
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(attachDragDocument, eventFilter);
	base::install_event_filter(attachDragPhoto, eventFilter);

	updateDragAreas();

	return {
		.document = attachDragDocument,
		.photo = attachDragPhoto,
	};
}

DragArea::DragArea(QWidget *parent) : Ui::RpWidget(parent) {
	setMouseTracking(true);
	setAcceptDrops(true);
}

bool DragArea::overlaps(const QRect &globalRect) {
	if (isHidden() || _a_opacity.animating()) {
		return false;
	}

	const auto inner = InnerRect(this);
	const auto testRect = QRect(
		mapFromGlobal(globalRect.topLeft()),
		globalRect.size());
	const auto h = QMargins(st::boxRadius, 0, st::boxRadius, 0);
	const auto v = QMargins(0, st::boxRadius, 0, st::boxRadius);
	return inner.marginsRemoved(h).contains(testRect)
		|| inner.marginsRemoved(v).contains(testRect);
}


void DragArea::mouseMoveEvent(QMouseEvent *e) {
	if (_hiding) {
		return;
	}

	setIn(InnerRect(this).contains(e->pos()));
}

void DragArea::dragMoveEvent(QDragMoveEvent *e) {
	setIn(InnerRect(this).contains(e->pos()));
	e->setDropAction(_in ? Qt::CopyAction : Qt::IgnoreAction);
	e->accept();
}

void DragArea::setIn(bool in) {
	if (_in != in) {
		_in = in;
		_a_in.start(
			[=] { update(); },
			_in ? 0. : 1.,
			_in ? 1. : 0.,
			st::boxDuration);
	}
}

void DragArea::setText(const QString &text, const QString &subtext) {
	_text = text;
	_subtext = subtext;
	update();
}

void DragArea::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto opacity = _a_opacity.value(_hiding ? 0. : 1.);
	if (!_a_opacity.animating() && _hiding) {
		return;
	}
	p.setOpacity(opacity);
	const auto inner = InnerRect(this);

	if (!_cache.isNull()) {
		p.drawPixmapLeft(
			inner.x() - st::boxRoundShadow.extend.left(),
			inner.y() - st::boxRoundShadow.extend.top(),
			width(),
			_cache);
		return;
	}

	Ui::Shadow::paint(p, inner, width(), st::boxRoundShadow);
	Ui::FillRoundRect(p, inner, st::boxBg, Ui::BoxCorners);

	p.setPen(anim::pen(
		st::dragColor,
		st::dragDropColor,
		_a_in.value(_in ? 1. : 0.)));

	p.setFont(st::dragFont);
	const auto rText = QRect(
		0,
		(height() - st::dragHeight) / 2,
		width(),
		st::dragFont->height);
	p.drawText(rText, _text, QTextOption(style::al_top));

	p.setFont(st::dragSubfont);
	const auto rSubtext = QRect(
		0,
		(height() + st::dragHeight) / 2 - st::dragSubfont->height,
		width(),
		st::dragSubfont->height * 2);
	p.drawText(rSubtext, _subtext, QTextOption(style::al_top));
}

void DragArea::dragEnterEvent(QDragEnterEvent *e) {
	e->setDropAction(Qt::IgnoreAction);
	e->accept();
}

void DragArea::dragLeaveEvent(QDragLeaveEvent *e) {
	setIn(false);
}

void DragArea::dropEvent(QDropEvent *e) {
	if (e->isAccepted() && _droppedCallback) {
		_droppedCallback(e->mimeData());
	}
}

void DragArea::otherEnter() {
	showStart();
}

void DragArea::otherLeave() {
	hideStart();
}

void DragArea::hideFast() {
	_a_opacity.stop();
	hide();
}

void DragArea::hideStart() {
	if (_hiding || isHidden()) {
		return;
	}
	if (_cache.isNull()) {
		_cache = Ui::GrabWidget(
			this,
			InnerRect(this).marginsAdded(st::boxRoundShadow.extend));
	}
	_hiding = true;
	setIn(false);
	_a_opacity.start(
		[=] { opacityAnimationCallback(); },
		1.,
		0.,
		st::boxDuration);
}

void DragArea::hideFinish() {
	hide();
	_in = false;
	_a_in.stop();
}

void DragArea::showStart() {
	if (!_hiding && !isHidden()) {
		return;
	}
	_hiding = false;
	if (_cache.isNull()) {
		_cache = Ui::GrabWidget(
			this,
			InnerRect(this).marginsAdded(st::boxRoundShadow.extend));
	}
	show();
	_a_opacity.start(
		[=] { opacityAnimationCallback(); },
		0.,
		1.,
		st::boxDuration);
}

void DragArea::opacityAnimationCallback() {
	update();
	if (!_a_opacity.animating()) {
		_cache = QPixmap();
		if (_hiding) {
			hideFinish();
		}
	}
}
