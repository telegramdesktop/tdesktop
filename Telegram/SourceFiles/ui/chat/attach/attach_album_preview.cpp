/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_album_preview.h"

#include "ui/chat/attach/attach_album_thumbnail.h"
#include "ui/chat/attach/attach_prepare.h"
#include "styles/style_chat.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kDragDuration = crl::time(200);

} // namespace

AlbumPreview::AlbumPreview(
	QWidget *parent,
	gsl::span<Ui::PreparedFile> items,
	SendFilesWay way)
: RpWidget(parent)
, _sendWay(way)
, _dragTimer([=] { switchToDrag(); }) {
	setMouseTracking(true);
	prepareThumbs(items);
	updateSize();
	updateFileRows();
}

AlbumPreview::~AlbumPreview() = default;

void AlbumPreview::setSendWay(SendFilesWay way) {
	if (_sendWay != way) {
		cancelDrag();
		_sendWay = way;
	}
	updateSize();
	updateFileRows();
	update();
}

void AlbumPreview::updateFileRows() {
	Expects(_order.size() == _thumbs.size());

	const auto isFile = !_sendWay.sendImagesAsPhotos();
	for (auto i = 0; i < _order.size(); i++) {
		_thumbs[i]->updateFileRow(isFile ? _order[i] : -1);
	}
}

std::vector<int> AlbumPreview::takeOrder() {
	//Expects(_thumbs.size() == _order.size());
	//Expects(_itemsShownDimensions.size() == _order.size());

	auto reordered = std::vector<std::unique_ptr<AlbumThumbnail>>();
	auto reorderedShownDimensions = std::vector<QSize>();
	reordered.reserve(_thumbs.size());
	reorderedShownDimensions.reserve(_itemsShownDimensions.size());
	for (auto index : _order) {
		reordered.push_back(std::move(_thumbs[index]));
		reorderedShownDimensions.push_back(_itemsShownDimensions[index]);
	}
	_thumbs = std::move(reordered);
	_itemsShownDimensions = std::move(reorderedShownDimensions);
	return std::exchange(_order, defaultOrder());
}

auto AlbumPreview::generateOrderedLayout() const
-> std::vector<GroupMediaLayout> {
	auto layout = LayoutMediaGroup(
		_itemsShownDimensions,
		st::sendMediaPreviewSize,
		st::historyGroupWidthMin / 2,
		st::historyGroupSkip / 2);
	Assert(layout.size() == _order.size());
	return layout;
}

std::vector<int> AlbumPreview::defaultOrder(int count) const {
	if (count < 0) {
		count = _order.size();
	}
	return ranges::views::ints(0, count) | ranges::to_vector;
}

void AlbumPreview::prepareThumbs(gsl::span<Ui::PreparedFile> items) {
	_order = defaultOrder(items.size());
	_itemsShownDimensions = ranges::views::all(
		_order
	) | ranges::views::transform([&](int index) {
		return items[index].shownDimensions;
	}) | ranges::to_vector;

	const auto count = int(_order.size());
	const auto layout = generateOrderedLayout();
	_thumbs.reserve(count);
	for (auto i = 0; i != count; ++i) {
		_thumbs.push_back(std::make_unique<AlbumThumbnail>(
			items[i],
			layout[i],
			this,
			[=] { changeThumbByIndex(thumbIndex(thumbUnderCursor())); },
			[=] { deleteThumbByIndex(thumbIndex(thumbUnderCursor())); }));
	}
	_thumbsHeight = countLayoutHeight(layout);
	_photosHeight = ranges::accumulate(ranges::views::all(
		_thumbs
	) | ranges::views::transform([](const auto &thumb) {
		return thumb->photoHeight();
	}), 0) + (count - 1) * st::sendMediaRowSkip;

	const auto &st = st::attachPreviewThumbLayout;
	_filesHeight = count * st.thumbSize
		+ (count - 1) * st::sendMediaRowSkip;
}

int AlbumPreview::contentLeft() const {
	return (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
}

int AlbumPreview::contentTop() const {
	return 0;
}

AlbumThumbnail *AlbumPreview::findThumb(QPoint position) const {
	position -= QPoint(contentLeft(), contentTop());

	auto top = 0;
	const auto isPhotosWay = _sendWay.sendImagesAsPhotos();
	const auto skip = st::sendMediaRowSkip;
	auto find = [&](const auto &thumb) {
		if (_sendWay.groupFiles() && _sendWay.sendImagesAsPhotos()) {
			return thumb->containsPoint(position);
		} else {
			const auto bottom = top + (isPhotosWay
				? thumb->photoHeight()
				: st::attachPreviewThumbLayout.thumbSize);
			const auto isUnderTop = (position.y() > top);
			top = bottom + skip;
			return isUnderTop && (position.y() < bottom);
		}
		return false;
	};

	const auto i = ranges::find_if(_thumbs, std::move(find));
	return (i == _thumbs.end()) ? nullptr : i->get();
}

not_null<AlbumThumbnail*> AlbumPreview::findClosestThumb(
	QPoint position) const {
	Expects(_draggedThumb != nullptr);

	if (const auto exact = findThumb(position)) {
		return exact;
	}
	auto result = _draggedThumb;
	auto distance = _draggedThumb->distanceTo(position);
	for (const auto &thumb : _thumbs) {
		const auto check = thumb->distanceTo(position);
		if (check < distance) {
			distance = check;
			result = thumb.get();
		}
	}
	return result;
}

int AlbumPreview::orderIndex(
	not_null<AlbumThumbnail*> thumb) const {
	const auto i = ranges::find_if(_order, [&](int index) {
		return (_thumbs[index].get() == thumb);
	});
	Assert(i != _order.end());
	return int(i - _order.begin());
}

void AlbumPreview::cancelDrag() {
	_thumbsHeightAnimation.stop();
	_finishDragAnimation.stop();
	_shrinkAnimation.stop();
	if (_draggedThumb) {
		_draggedThumb->moveInAlbum({ 0, 0 });
		_draggedThumb = nullptr;
	}
	if (_suggestedThumb) {
		const auto suggestedIndex = orderIndex(_suggestedThumb);
		if (suggestedIndex > 0) {
			_thumbs[_order[suggestedIndex - 1]]->suggestMove(0., [] {});
		}
		if (suggestedIndex < int(_order.size() - 1)) {
			_thumbs[_order[suggestedIndex + 1]]->suggestMove(0., [] {});
		}
		_suggestedThumb->suggestMove(0., [] {});
		_suggestedThumb->finishAnimations();
		_suggestedThumb = nullptr;
	}
	_paintedAbove = nullptr;
	update();
}

void AlbumPreview::finishDrag() {
	Expects(_draggedThumb != nullptr);
	Expects(_suggestedThumb != nullptr);

	if (_suggestedThumb != _draggedThumb) {
		const auto currentIndex = orderIndex(_draggedThumb);
		const auto newIndex = orderIndex(_suggestedThumb);
		const auto delta = (currentIndex < newIndex) ? 1 : -1;
		const auto realIndex = _order[currentIndex];
		for (auto i = currentIndex; i != newIndex; i += delta) {
			_order[i] = _order[i + delta];
		}
		_order[newIndex] = realIndex;
		const auto layout = generateOrderedLayout();
		for (auto i = 0, count = int(_order.size()); i != count; ++i) {
			_thumbs[_order[i]]->moveToLayout(layout[i]);
		}
		_finishDragAnimation.start([=] { update(); }, 0., 1., kDragDuration);

		updateSizeAnimated(layout);
	} else {
		for (const auto &thumb : _thumbs) {
			thumb->resetLayoutAnimation();
		}
		_draggedThumb->animateLayoutToInitial();
		_finishDragAnimation.start([=] { update(); }, 0., 1., kDragDuration);
	}
}

int AlbumPreview::countLayoutHeight(
	const std::vector<GroupMediaLayout> &layout) const {
	const auto accumulator = [](int current, const auto &item) {
		return std::max(current, item.geometry.y() + item.geometry.height());
	};
	return ranges::accumulate(layout, 0, accumulator);
}

void AlbumPreview::updateSizeAnimated(
	const std::vector<GroupMediaLayout> &layout) {
	const auto newHeight = countLayoutHeight(layout);
	if (newHeight != _thumbsHeight) {
		_thumbsHeightAnimation.start(
			[=] { updateSize(); },
			_thumbsHeight,
			newHeight,
			kDragDuration);
		_thumbsHeight = newHeight;
	}
}

void AlbumPreview::updateSize() {
	const auto newHeight = [&] {
		if (!_sendWay.sendImagesAsPhotos()) {
			return _filesHeight;
		} else if (!_sendWay.groupFiles()) {
			return _photosHeight;
		} else {
			return int(std::round(_thumbsHeightAnimation.value(
				_thumbsHeight)));
		}
	}();
	if (height() != newHeight) {
		resize(st::boxWideWidth, newHeight);
	}
}

void AlbumPreview::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_sendWay.sendImagesAsPhotos()) {
		paintFiles(p, e->rect());
	} else if (!_sendWay.groupFiles()) {
		paintPhotos(p, e->rect());
	} else {
		paintAlbum(p);
	}
}

void AlbumPreview::paintAlbum(Painter &p) const {
	const auto shrink = _shrinkAnimation.value(_draggedThumb ? 1. : 0.);
	const auto moveProgress = _finishDragAnimation.value(1.);
	const auto left = contentLeft();
	const auto top = contentTop();
	for (const auto &thumb : _thumbs) {
		if (thumb.get() != _paintedAbove) {
			thumb->paintInAlbum(p, left, top, shrink, moveProgress);
		}
	}
	if (_paintedAbove) {
		_paintedAbove->paintInAlbum(p, left, top, shrink, moveProgress);
	}
}

void AlbumPreview::paintPhotos(Painter &p, QRect clip) const {
	const auto left = (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
	auto top = 0;
	const auto outerWidth = width();
	for (const auto &thumb : _thumbs) {
		const auto bottom = top + thumb->photoHeight();
		const auto guard = gsl::finally([&] {
			top = bottom + st::sendMediaRowSkip;
		});
		if (top >= clip.y() + clip.height()) {
			break;
		} else if (bottom <= clip.y()) {
			continue;
		}
		thumb->paintPhoto(p, left, top, outerWidth);
	}
}

void AlbumPreview::paintFiles(Painter &p, QRect clip) const {
	const auto fileHeight = st::attachPreviewThumbLayout.thumbSize
		+ st::sendMediaRowSkip;
	const auto bottom = clip.y() + clip.height();
	const auto from = std::clamp(clip.y() / fileHeight, 0, int(_thumbs.size()));
	const auto till = std::clamp((bottom + fileHeight - 1) / fileHeight, 0, int(_thumbs.size()));
	const auto left = (st::boxWideWidth - st::sendMediaPreviewSize) / 2;
	const auto outerWidth = width();

	auto top = from * fileHeight;
	for (auto i = from; i != till; ++i) {
		_thumbs[i]->paintFile(p, left, top, outerWidth);
		top += fileHeight;
	}
}

int AlbumPreview::thumbIndex(AlbumThumbnail *thumb) {
	if (!thumb) {
		return -1;
	}
	const auto thumbIt = ranges::find_if(_thumbs, [&](auto &t) {
		return t.get() == thumb;
	});
	Expects(thumbIt != _thumbs.end());
	return std::distance(_thumbs.begin(), thumbIt);
}

AlbumThumbnail *AlbumPreview::thumbUnderCursor() {
	return findThumb(mapFromGlobal(QCursor::pos()));
}

void AlbumPreview::deleteThumbByIndex(int index) {
	if (index < 0) {
		return;
	}
	const auto orderIt = ranges::find(_order, index);
	Expects(orderIt != _order.end());

	_order.erase(orderIt);
	ranges::for_each(_order, [=](auto &i) {
		if (i > index) {
			i--;
		}
	});
	_thumbDeleted.fire(std::move(index));
}

void AlbumPreview::changeThumbByIndex(int index) {
	if (index < 0) {
		return;
	}
	_thumbChanged.fire(std::move(index));
}

void AlbumPreview::modifyThumbByIndex(int index) {
	if (index < 0) {
		return;
	}
	_thumbModified.fire(std::move(index));
}

void AlbumPreview::thumbButtonsCallback(
		not_null<AlbumThumbnail*> thumb,
		AttachButtonType type) {
	const auto index = thumbIndex(thumb);

	switch (type) {
	case AttachButtonType::None: return;
	case AttachButtonType::Edit: changeThumbByIndex(index); break;
	case AttachButtonType::Delete: deleteThumbByIndex(index); break;
	case AttachButtonType::Modify:
		cancelDrag();
		modifyThumbByIndex(index);
		break;
	}
}

void AlbumPreview::mousePressEvent(QMouseEvent *e) {
	if (_finishDragAnimation.animating()) {
		return;
	}
	const auto position = e->pos();
	cancelDrag();
	if (const auto thumb = findThumb(position)) {
		_draggedStartPosition = position;
		_pressedThumb = thumb;
		_pressedButtonType = thumb->buttonTypeFromPoint(position);

		const auto isAlbum = _sendWay.sendImagesAsPhotos()
			&& _sendWay.groupFiles();
		if (!isAlbum) {
			return;
		}

		if (_pressedButtonType == AttachButtonType::None) {
			switchToDrag();
		} else if (_pressedButtonType == AttachButtonType::Modify) {
			_dragTimer.callOnce(QApplication::startDragTime());
		}
	}
}

void AlbumPreview::mouseMoveEvent(QMouseEvent *e) {
	if (!_sendWay.sendImagesAsPhotos()) {
		applyCursor(style::cur_default);
		return;
	}
	if (_dragTimer.isActive()) {
		_dragTimer.cancel();
		switchToDrag();
	}
	const auto isAlbum = _sendWay.sendImagesAsPhotos()
		&& _sendWay.groupFiles();
	if (isAlbum && _draggedThumb) {
		const auto position = e->pos();
		_draggedThumb->moveInAlbum(position - _draggedStartPosition);
		updateSuggestedDrag(_draggedThumb->center());
		update();
	} else {
		const auto thumb = findThumb(e->pos());
		const auto regularCursor = isAlbum
			? style::cur_pointer
			: style::cur_default;
		const auto cursor = thumb
			? (thumb->buttonsContainPoint(e->pos())
				? style::cur_pointer
				: regularCursor)
			: style::cur_default;
		applyCursor(cursor);
	}
}

void AlbumPreview::applyCursor(style::cursor cursor) {
	if (_cursor != cursor) {
		_cursor = cursor;
		setCursor(_cursor);
	}
}

void AlbumPreview::updateSuggestedDrag(QPoint position) {
	auto closest = findClosestThumb(position);
	auto closestIndex = orderIndex(closest);

	const auto draggedIndex = orderIndex(_draggedThumb);
	const auto closestIsBeforePoint = closest->isPointAfter(position);
	if (closestIndex < draggedIndex && closestIsBeforePoint) {
		closest = _thumbs[_order[++closestIndex]].get();
	} else if (closestIndex > draggedIndex && !closestIsBeforePoint) {
		closest = _thumbs[_order[--closestIndex]].get();
	}

	if (_suggestedThumb == closest) {
		return;
	}

	const auto last = int(_order.size()) - 1;
	if (_suggestedThumb) {
		const auto suggestedIndex = orderIndex(_suggestedThumb);
		if (suggestedIndex < draggedIndex && suggestedIndex > 0) {
			const auto previous = _thumbs[_order[suggestedIndex - 1]].get();
			previous->suggestMove(0., [=] { update(); });
		} else if (suggestedIndex > draggedIndex && suggestedIndex < last) {
			const auto next = _thumbs[_order[suggestedIndex + 1]].get();
			next->suggestMove(0., [=] { update(); });
		}
		_suggestedThumb->suggestMove(0., [=] { update(); });
	}
	_suggestedThumb = closest;
	const auto suggestedIndex = closestIndex;
	if (_suggestedThumb != _draggedThumb) {
		const auto delta = (suggestedIndex < draggedIndex) ? 1. : -1.;
		if (delta > 0. && suggestedIndex > 0) {
			const auto previous = _thumbs[_order[suggestedIndex - 1]].get();
			previous->suggestMove(-delta, [=] { update(); });
		} else if (delta < 0. && suggestedIndex < last) {
			const auto next = _thumbs[_order[suggestedIndex + 1]].get();
			next->suggestMove(-delta, [=] { update(); });
		}
		_suggestedThumb->suggestMove(delta, [=] { update(); });
	}
}

void AlbumPreview::mouseReleaseEvent(QMouseEvent *e) {
	if (_draggedThumb) {
		finishDrag();
		_shrinkAnimation.start(
			[=] { update(); },
			1.,
			0.,
			AlbumThumbnail::kShrinkDuration);
		_draggedThumb = nullptr;
		_suggestedThumb = nullptr;
		update();
	} else if (const auto thumb = base::take(_pressedThumb)) {
		const auto was = _pressedButtonType;
		const auto now = thumb->buttonTypeFromPoint(e->pos());
		if (was == now) {
			thumbButtonsCallback(thumb, now);
		}
	}
	_pressedButtonType = AttachButtonType::None;
}

void AlbumPreview::switchToDrag() {
	_paintedAbove
		= _suggestedThumb
		= _draggedThumb
		= base::take(_pressedThumb);
	_shrinkAnimation.start(
		[=] { update(); },
		0.,
		1.,
		AlbumThumbnail::kShrinkDuration);
	applyCursor(style::cur_sizeall);
	update();
}

rpl::producer<int> AlbumPreview::thumbModified() const {
	return _thumbModified.events();
}

} // namespace Ui
