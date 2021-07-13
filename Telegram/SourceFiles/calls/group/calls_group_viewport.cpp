/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_viewport.h"

#include "calls/group/calls_group_viewport_tile.h"
#include "calls/group/calls_group_viewport_opengl.h"
#include "calls/group/calls_group_viewport_raster.h"
#include "calls/group/calls_group_common.h"
#include "calls/group/calls_group_call.h"
#include "calls/group/calls_group_members_row.h"
#include "media/view/media_view_pip.h"
#include "base/platform/base_platform_info.h"
#include "webrtc/webrtc_video_track.h"
#include "ui/painter.h"
#include "ui/abstract_button.h"
#include "ui/gl/gl_surface.h"
#include "ui/effects/animations.h"
#include "ui/effects/cross_line.h"
#include "data/data_group_call.h" // MuteButtonTooltip.
#include "lang/lang_keys.h"
#include "styles/style_calls.h"

#include <QtGui/QtEvents>
#include <QtGui/QOpenGLShader>

namespace Calls::Group {
namespace {

[[nodiscard]] QRect InterpolateRect(QRect a, QRect b, float64 ratio) {
	const auto left = anim::interpolate(a.x(), b.x(), ratio);
	const auto top = anim::interpolate(a.y(), b.y(), ratio);
	const auto right = anim::interpolate(
		a.x() + a.width(),
		b.x() + b.width(),
		ratio);
	const auto bottom = anim::interpolate(
		a.y() + a.height(),
		b.y() + b.height(),
		ratio);
	return { left, top, right - left, bottom - top };
}

} // namespace

Viewport::Viewport(
	not_null<QWidget*> parent,
	PanelMode mode,
	Ui::GL::Backend backend)
: _mode(mode)
, _content(Ui::GL::CreateSurface(parent, chooseRenderer(backend))) {
	setup();
}

Viewport::~Viewport() = default;

not_null<QWidget*> Viewport::widget() const {
	return _content->rpWidget();
}

not_null<Ui::RpWidgetWrap*> Viewport::rp() const {
	return _content.get();
}

void Viewport::setup() {
	const auto raw = widget();

	raw->resize(0, 0);
	raw->setAttribute(Qt::WA_OpaquePaintEvent);
	raw->setMouseTracking(true);

	_content->sizeValue(
	) | rpl::filter([=] {
		return wide();
	}) | rpl::start_with_next([=] {
		updateTilesGeometry();
	}, lifetime());

	_content->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::Enter) {
			Ui::Integration::Instance().registerLeaveSubscription(raw);
			_mouseInside = true;
		} else if (type == QEvent::Leave) {
			Ui::Integration::Instance().unregisterLeaveSubscription(raw);
			setSelected({});
			_mouseInside = false;
		} else if (type == QEvent::MouseButtonPress) {
			handleMousePress(
				static_cast<QMouseEvent*>(e.get())->pos(),
				static_cast<QMouseEvent*>(e.get())->button());
		} else if (type == QEvent::MouseButtonRelease) {
			handleMouseRelease(
				static_cast<QMouseEvent*>(e.get())->pos(),
				static_cast<QMouseEvent*>(e.get())->button());
		} else if (type == QEvent::MouseMove) {
			handleMouseMove(static_cast<QMouseEvent*>(e.get())->pos());
		}
	}, lifetime());
}

void Viewport::setGeometry(QRect geometry) {
	Expects(wide());

	if (widget()->geometry() != geometry) {
		_geometryStaleAfterModeChange = false;
		widget()->setGeometry(geometry);
	} else if (_geometryStaleAfterModeChange) {
		_geometryStaleAfterModeChange = false;
		updateTilesGeometry();
	}
}

void Viewport::resizeToWidth(int width) {
	Expects(!wide());

	updateTilesGeometry(width);
}

void Viewport::setScrollTop(int scrollTop) {
	if (_scrollTop == scrollTop) {
		return;
	}
	_scrollTop = scrollTop;
	updateTilesGeometry();
}

bool Viewport::wide() const {
	return (_mode == PanelMode::Wide);
}

void Viewport::setMode(PanelMode mode, not_null<QWidget*> parent) {
	if (_mode == mode && widget()->parent() == parent) {
		return;
	}
	_mode = mode;
	_scrollTop = 0;
	setControlsShown(1.);
	if (widget()->parent() != parent) {
		const auto hidden = widget()->isHidden();
		widget()->setParent(parent);
		if (!hidden) {
			widget()->show();
		}
	}
	if (!wide()) {
		for (const auto &tile : _tiles) {
			tile->toggleTopControlsShown(false);
		}
	} else if (_selected.tile) {
		_selected.tile->toggleTopControlsShown(true);
	}
}

void Viewport::handleMousePress(QPoint position, Qt::MouseButton button) {
	handleMouseMove(position);
	setPressed(_selected);
}

void Viewport::handleMouseRelease(QPoint position, Qt::MouseButton button) {
	handleMouseMove(position);
	const auto pressed = _pressed;
	setPressed({});
	if (const auto tile = pressed.tile) {
		if (pressed == _selected) {
			if (button == Qt::RightButton) {
				tile->row()->showContextMenu();
			} else if (!wide()
				|| (_hasTwoOrMore && !_large)
				|| pressed.element != Selection::Element::PinButton) {
				_clicks.fire_copy(tile->endpoint());
			} else if (pressed.element == Selection::Element::PinButton) {
				_pinToggles.fire(!tile->pinned());
			}
		}
	}
}

void Viewport::handleMouseMove(QPoint position) {
	updateSelected(position);
}

void Viewport::updateSelected(QPoint position) {
	if (!widget()->rect().contains(position)) {
		setSelected({});
		return;
	}
	for (const auto &tile : _tiles) {
		const auto geometry = tile->visible()
			? tile->geometry()
			: QRect();
		if (geometry.contains(position)) {
			const auto pin = wide()
				&& tile->pinOuter().contains(position - geometry.topLeft());
			const auto back = wide()
				&& tile->backOuter().contains(position - geometry.topLeft());
			setSelected({
				.tile = tile.get(),
				.element = (pin
					? Selection::Element::PinButton
					: back
					? Selection::Element::BackButton
					: Selection::Element::Tile),
			});
			return;
		}
	}
	setSelected({});
}

void Viewport::updateSelected() {
	updateSelected(widget()->mapFromGlobal(QCursor::pos()));
}

void Viewport::setControlsShown(float64 shown) {
	_controlsShownRatio = shown;
	widget()->update();
}

void Viewport::add(
		const VideoEndpoint &endpoint,
		VideoTileTrack track,
		rpl::producer<QSize> trackSize,
		rpl::producer<bool> pinned) {
	_tiles.push_back(std::make_unique<VideoTile>(
		endpoint,
		track,
		std::move(trackSize),
		std::move(pinned),
		[=] { widget()->update(); }));

	_tiles.back()->trackSizeValue(
	) | rpl::filter([](QSize size) {
		return !size.isEmpty();
	}) | rpl::start_with_next([=] {
		updateTilesGeometry();
	}, _tiles.back()->lifetime());

	_tiles.back()->track()->stateValue(
	) | rpl::start_with_next([=] {
		updateTilesGeometry();
	}, _tiles.back()->lifetime());
}

void Viewport::remove(const VideoEndpoint &endpoint) {
	const auto i = ranges::find(_tiles, endpoint, &VideoTile::endpoint);
	if (i == end(_tiles)) {
		return;
	}
	const auto removing = i->get();
	const auto largeRemoved = (_large == removing);
	if (largeRemoved) {
		prepareLargeChangeAnimation();
		_large = nullptr;
	}
	if (_selected.tile == removing) {
		setSelected({});
	}
	if (_pressed.tile == removing) {
		setPressed({});
	}
	for (auto &geometry : _startTilesLayout.list) {
		if (geometry.tile == removing) {
			geometry.tile = nullptr;
		}
	}
	for (auto &geometry : _finishTilesLayout.list) {
		if (geometry.tile == removing) {
			geometry.tile = nullptr;
		}
	}
	_tiles.erase(i);
	if (largeRemoved) {
		startLargeChangeAnimation();
	} else {
		updateTilesGeometry();
	}
}

void Viewport::prepareLargeChangeAnimation() {
	if (!wide()) {
		return;
	} else if (_largeChangeAnimation.animating()) {
		updateTilesAnimated();
		const auto field = _finishTilesLayout.useColumns
			? &Geometry::columns
			: &Geometry::rows;
		for (auto &finish : _finishTilesLayout.list) {
			const auto tile = finish.tile;
			if (!tile) {
				continue;
			}
			finish.*field = tile->geometry();
		}
		_startTilesLayout = std::move(_finishTilesLayout);
		_largeChangeAnimation.stop();

		_startTilesLayout.list.erase(
			ranges::remove(_startTilesLayout.list, nullptr, &Geometry::tile),
			end(_startTilesLayout.list));
	} else {
		_startTilesLayout = applyLarge(std::move(_startTilesLayout));
	}
}

void Viewport::startLargeChangeAnimation() {
	Expects(!_largeChangeAnimation.animating());

	if (!wide()
		|| anim::Disabled()
		|| (_startTilesLayout.list.size() < 2)
		|| !_opengl
		|| widget()->size().isEmpty()) {
		updateTilesGeometry();
		return;
	}
	_finishTilesLayout = applyLarge(
		countWide(widget()->width(), widget()->height()));
	if (_finishTilesLayout.list.empty()
		|| _finishTilesLayout.outer != _startTilesLayout.outer) {
		updateTilesGeometry();
		return;
	}
	_largeChangeAnimation.start(
		[=] { updateTilesAnimated(); },
		0.,
		1.,
		st::slideDuration);
}

Viewport::Layout Viewport::applyLarge(Layout layout) const {
	auto &list = layout.list;
	if (!_large) {
		return layout;
	}
	const auto i = ranges::find(list, _large, &Geometry::tile);
	if (i == end(list)) {
		return layout;
	}
	const auto field = layout.useColumns
		? &Geometry::columns
		: &Geometry::rows;
	const auto fullWidth = layout.outer.width();
	const auto fullHeight = layout.outer.height();
	const auto largeRect = (*i).*field;
	const auto largeLeft = largeRect.x();
	const auto largeTop = largeRect.y();
	const auto largeRight = largeLeft + largeRect.width();
	const auto largeBottom = largeTop + largeRect.height();
	for (auto &geometry : list) {
		if (geometry.tile == _large) {
			geometry.*field = { QPoint(), layout.outer };
		} else if (layout.useColumns) {
			auto &rect = geometry.columns;
			const auto center = rect.center();
			if (center.x() < largeLeft) {
				rect = rect.translated(-largeLeft, 0);
			} else if (center.x() > largeRight) {
				rect = rect.translated(fullWidth - largeRight, 0);
			} else if (center.y() < largeTop) {
				rect = QRect(
					0,
					rect.y() - largeTop,
					fullWidth,
					rect.height());
			} else if (center.y() > largeBottom) {
				rect = QRect(
					0,
					rect.y() + (fullHeight - largeBottom),
					fullWidth,
					rect.height());
			}
		} else {
			auto &rect = geometry.rows;
			const auto center = rect.center();
			if (center.y() < largeTop) {
				rect = rect.translated(0, -largeTop);
			} else if (center.y() > largeBottom) {
				rect = rect.translated(0, fullHeight - largeBottom);
			} else if (center.x() < largeLeft) {
				rect = QRect(
					rect.x() - largeLeft,
					0,
					rect.width(),
					fullHeight);
			} else {
				rect = QRect(
					rect.x() + (fullWidth - largeRight),
					0,
					rect.width(),
					fullHeight);
			}
		}
	}
	return layout;
}

void Viewport::updateTilesAnimated() {
	if (!_largeChangeAnimation.animating()) {
		updateTilesGeometry();
		return;
	}
	const auto ratio = _largeChangeAnimation.value(1.);
	const auto field = _finishTilesLayout.useColumns
		? &Geometry::columns
		: &Geometry::rows;
	for (const auto &finish : _finishTilesLayout.list) {
		const auto tile = finish.tile;
		if (!tile) {
			continue;
		}
		const auto i = ranges::find(
			_startTilesLayout.list,
			tile,
			&Geometry::tile);
		if (i == end(_startTilesLayout.list)) {
			LOG(("Tiles Animation Error 1!"));
			_largeChangeAnimation.stop();
			updateTilesGeometry();
			return;
		}
		const auto from = (*i).*field;
		const auto to = finish.*field;
		tile->setGeometry(
			InterpolateRect(from, to, ratio),
			TileAnimation{ from.size(), to.size(), ratio });
	}
	widget()->update();
}

Viewport::Layout Viewport::countWide(int outerWidth, int outerHeight) const {
	auto result = Layout{ .outer = QSize(outerWidth, outerHeight) };
	auto &sizes = result.list;
	sizes.reserve(_tiles.size());
	for (const auto &tile : _tiles) {
		const auto video = tile.get();
		const auto size = video->trackOrUserpicSize();
		if (!size.isEmpty()) {
			sizes.push_back(Geometry{ video, size });
		}
	}
	if (sizes.empty()) {
		return result;
	} else if (sizes.size() == 1) {
		sizes.front().rows = { 0, 0, outerWidth, outerHeight };
		return result;
	}

	auto columnsBlack = uint64();
	auto rowsBlack = uint64();
	const auto count = int(sizes.size());
	const auto skip = st::groupCallVideoLargeSkip;
	const auto slices = int(std::ceil(std::sqrt(float64(count))));
	{
		auto index = 0;
		const auto columns = slices;
		const auto sizew = (outerWidth + skip) / float64(columns);
		for (auto column = 0; column != columns; ++column) {
			const auto left = int(std::round(column * sizew));
			const auto width = int(std::round(column * sizew + sizew - skip))
				- left;
			const auto rows = int(std::round((count - index)
				/ float64(columns - column)));
			const auto sizeh = (outerHeight + skip) / float64(rows);
			for (auto row = 0; row != rows; ++row) {
				const auto top = int(std::round(row * sizeh));
				const auto height = int(std::round(
					row * sizeh + sizeh - skip)) - top;
				auto &geometry = sizes[index];
				geometry.columns = {
					left,
					top,
					width,
					height };
				const auto scaled = geometry.size.scaled(
					width,
					height,
					Qt::KeepAspectRatio);
				columnsBlack += (scaled.width() < width)
					? (width - scaled.width()) * height
					: (height - scaled.height()) * width;
				++index;
			}
		}
	}
	{
		auto index = 0;
		const auto rows = slices;
		const auto sizeh = (outerHeight + skip) / float64(rows);
		for (auto row = 0; row != rows; ++row) {
			const auto top = int(std::round(row * sizeh));
			const auto height = int(std::round(row * sizeh + sizeh - skip))
				- top;
			const auto columns = int(std::round((count - index)
				/ float64(rows - row)));
			const auto sizew = (outerWidth + skip) / float64(columns);
			for (auto column = 0; column != columns; ++column) {
				const auto left = int(std::round(column * sizew));
				const auto width = int(std::round(
					column * sizew + sizew - skip)) - left;
				auto &geometry = sizes[index];
				geometry.rows = {
					left,
					top,
					width,
					height };
				const auto scaled = geometry.size.scaled(
					width,
					height,
					Qt::KeepAspectRatio);
				rowsBlack += (scaled.width() < width)
					? (width - scaled.width()) * height
					: (height - scaled.height()) * width;
				++index;
			}
		}
	}
	result.useColumns = (columnsBlack < rowsBlack);
	return result;
}

void Viewport::showLarge(const VideoEndpoint &endpoint) {
	// If a video get's switched off, GroupCall first unpins it,
	// then removes it from Large endpoint, then removes from active tracks.
	//
	// If we want to animate large video removal properly, we need to
	// delay this update and start animation directly from removing of the
	// track from the active list. Otherwise final state won't be correct.
	_updateLargeScheduled = [=] {
		const auto i = ranges::find(_tiles, endpoint, &VideoTile::endpoint);
		const auto large = (i != end(_tiles)) ? i->get() : nullptr;
		if (_large != large) {
			prepareLargeChangeAnimation();
			_large = large;
			updateTopControlsVisibility();
			startLargeChangeAnimation();
		}

		Ensures(!_large || !_large->trackOrUserpicSize().isEmpty());
	};
	crl::on_main(widget(), [=] {
		if (!_updateLargeScheduled) {
			return;
		}
		base::take(_updateLargeScheduled)();
	});
}

void Viewport::updateTilesGeometry() {
	updateTilesGeometry(widget()->width());
}

void Viewport::updateTilesGeometry(int outerWidth) {
	const auto mouseInside = _mouseInside.current();
	const auto guard = gsl::finally([&] {
		if (mouseInside) {
			updateSelected();
		}
		widget()->update();
	});

	const auto outerHeight = widget()->height();
	if (_tiles.empty() || !outerWidth) {
		_fullHeight = 0;
		return;
	}

	if (wide()) {
		updateTilesGeometryWide(outerWidth, outerHeight);
		refreshHasTwoOrMore();
		_fullHeight = 0;
	} else {
		updateTilesGeometryNarrow(outerWidth);
	}
}

void Viewport::refreshHasTwoOrMore() {
	auto hasTwoOrMore = false;
	auto oneFound = false;
	for (const auto &tile : _tiles) {
		if (!tile->trackOrUserpicSize().isEmpty()) {
			if (oneFound) {
				hasTwoOrMore = true;
				break;
			}
			oneFound = true;
		}
	}
	if (_hasTwoOrMore == hasTwoOrMore) {
		return;
	}
	_hasTwoOrMore = hasTwoOrMore;
	updateCursor();
	updateTopControlsVisibility();
}

void Viewport::updateTopControlsVisibility() {
	if (_selected.tile) {
		_selected.tile->toggleTopControlsShown(
			_hasTwoOrMore && wide() && _large && _large == _selected.tile);
	}
}

void Viewport::updateTilesGeometryWide(int outerWidth, int outerHeight) {
	if (!outerHeight) {
		return;
	} else if (_largeChangeAnimation.animating()) {
		if (_startTilesLayout.outer == QSize(outerWidth, outerHeight)) {
			return;
		}
		_largeChangeAnimation.stop();
	}

	_startTilesLayout = countWide(outerWidth, outerHeight);
	if (_large && !_large->trackOrUserpicSize().isEmpty()) {
		for (const auto &geometry : _startTilesLayout.list) {
			if (geometry.tile == _large) {
				setTileGeometry(_large, { 0, 0, outerWidth, outerHeight });
			} else {
				geometry.tile->hide();
			}
		}
	} else {
		const auto field = _startTilesLayout.useColumns
			? &Geometry::columns
			: &Geometry::rows;
		for (const auto &geometry : _startTilesLayout.list) {
			if (const auto video = geometry.tile) {
				setTileGeometry(video, geometry.*field);
			}
		}
	}
}

void Viewport::updateTilesGeometryNarrow(int outerWidth) {
	if (outerWidth <= st::groupCallNarrowMembersWidth) {
		updateTilesGeometryColumn(outerWidth);
		return;
	}

	const auto y = -_scrollTop;
	auto sizes = base::flat_map<not_null<VideoTile*>, QSize>();
	sizes.reserve(_tiles.size());
	for (const auto &tile : _tiles) {
		const auto video = tile.get();
		const auto size = video->trackOrUserpicSize();
		if (size.isEmpty()) {
			video->hide();
		} else {
			sizes.emplace(video, size);
		}
	}
	if (sizes.empty()) {
		_fullHeight = 0;
		return;
	} else if (sizes.size() == 1) {
		const auto size = sizes.front().second;
		const auto heightMin = (outerWidth * 9) / 16;
		const auto heightMax = (outerWidth * 3) / 4;
		const auto scaled = size.scaled(
			QSize(outerWidth, heightMax),
			Qt::KeepAspectRatio);
		const auto height = std::max(scaled.height(), heightMin);
		const auto skip = st::groupCallVideoSmallSkip;
		setTileGeometry(sizes.front().first, { 0, y, outerWidth, height });
		_fullHeight = height + skip;
		return;
	}
	const auto min = (st::groupCallWidth
		- st::groupCallMembersMargin.left()
		- st::groupCallMembersMargin.right()
		- st::groupCallVideoSmallSkip) / 2;
	const auto square = (outerWidth - st::groupCallVideoSmallSkip) / 2;
	const auto skip = (outerWidth - 2 * square);
	const auto put = [&](not_null<VideoTile*> tile, int column, int row) {
		setTileGeometry(tile, {
			(column == 2) ? 0 : column ? (outerWidth - square) : 0,
			y + row * (min + skip),
			(column == 2) ? outerWidth : square,
			min,
		});
	};
	const auto rows = (sizes.size() + 1) / 2;
	if (sizes.size() == 3) {
		put(sizes.front().first, 2, 0);
		put((sizes.begin() + 1)->first, 0, 1);
		put((sizes.begin() + 2)->first, 1, 1);
	} else {
		auto row = 0;
		auto column = 0;
		for (const auto &[video, endpoint] : sizes) {
			put(video, column, row);
			if (column) {
				++row;
				column = (row + 1 == rows && sizes.size() % 2) ? 2 : 0;
			} else {
				column = 1;
			}
		}
	}
	_fullHeight = rows * (min + skip);
}

void Viewport::updateTilesGeometryColumn(int outerWidth) {
	const auto y = -_scrollTop;
	auto top = 0;
	const auto layoutNext = [&](not_null<VideoTile*> tile) {
		const auto size = tile->trackOrUserpicSize();
		const auto shown = !size.isEmpty() && _large && tile != _large;
		const auto height = st::groupCallNarrowVideoHeight;
		if (!shown) {
			tile->hide();
		} else {
			setTileGeometry(tile, { 0, y + top, outerWidth, height });
			top += height + st::groupCallVideoSmallSkip;
		}
	};
	const auto topPeer = _large ? _large->row()->peer().get() : nullptr;
	const auto reorderNeeded = [&] {
		if (!_large) {
			return false;
		}
		for (const auto &tile : _tiles) {
			if (tile.get() != _large && tile->row()->peer() == topPeer) {
				return (tile.get() != _tiles.front().get())
					&& !tile->trackOrUserpicSize().isEmpty();
			}
		}
		return false;
	}();
	if (reorderNeeded) {
		_tilesForOrder.clear();
		_tilesForOrder.reserve(_tiles.size());
		for (const auto &tile : _tiles) {
			_tilesForOrder.push_back(tile.get());
		}
		ranges::stable_partition(
			_tilesForOrder,
			[&](not_null<VideoTile*> tile) {
				return (tile->row()->peer() == topPeer);
			});
		for (const auto &tile : _tilesForOrder) {
			layoutNext(tile);
		}
	} else {
		for (const auto &tile : _tiles) {
			layoutNext(tile.get());
		}
	}
	_fullHeight = top;
}

void Viewport::setTileGeometry(not_null<VideoTile*> tile, QRect geometry) {
	tile->setGeometry(geometry);

	const auto min = std::min(geometry.width(), geometry.height());
	const auto kMedium = style::ConvertScale(540);
	const auto kSmall = style::ConvertScale(240);
	const auto &endpoint = tile->endpoint();
	const auto forceThumbnailQuality = !wide()
		&& (ranges::count(_tiles, false, &VideoTile::hidden) > 1);
	const auto forceFullQuality = wide() && (tile.get() == _large);
	const auto quality = forceThumbnailQuality
		? VideoQuality::Thumbnail
		: (forceFullQuality || min >= kMedium)
		? VideoQuality::Full
		: (min >= kSmall)
		? VideoQuality::Medium
		: VideoQuality::Thumbnail;
	if (tile->updateRequestedQuality(quality)) {
		_qualityRequests.fire(VideoQualityRequest{
			.endpoint = endpoint,
			.quality = quality,
		});
	}
}

void Viewport::setSelected(Selection value) {
	if (_selected == value) {
		return;
	}
	if (_selected.tile) {
		_selected.tile->toggleTopControlsShown(false);
	}
	_selected = value;
	updateTopControlsVisibility();
	updateCursor();
}

void Viewport::updateCursor() {
	const auto pointer = _selected.tile && (!wide() || _hasTwoOrMore);
	widget()->setCursor(pointer ? style::cur_pointer : style::cur_default);
}

void Viewport::setPressed(Selection value) {
	if (_pressed == value) {
		return;
	}
	_pressed = value;
}

Ui::GL::ChosenRenderer Viewport::chooseRenderer(Ui::GL::Backend backend) {
	_opengl = (backend == Ui::GL::Backend::OpenGL);
	return {
		.renderer = (_opengl
			? std::unique_ptr<Ui::GL::Renderer>(
				std::make_unique<RendererGL>(this))
			: std::make_unique<RendererSW>(this)),
		.backend = backend,
	};
}

bool Viewport::requireARGB32() const {
	return !_opengl;
}

int Viewport::fullHeight() const {
	return _fullHeight.current();
}

rpl::producer<int> Viewport::fullHeightValue() const {
	return _fullHeight.value();
}

rpl::producer<bool> Viewport::pinToggled() const {
	return _pinToggles.events();
}

rpl::producer<VideoEndpoint> Viewport::clicks() const {
	return _clicks.events();
}

rpl::producer<VideoQualityRequest> Viewport::qualityRequests() const {
	return _qualityRequests.events();
}

rpl::producer<bool> Viewport::mouseInsideValue() const {
	return _mouseInside.value();
}

rpl::lifetime &Viewport::lifetime() {
	return _content->lifetime();
}

QImage GenerateShadow(
		int height,
		int topAlpha,
		int bottomAlpha,
		QColor color) {
	Expects(topAlpha >= 0 && topAlpha < 256);
	Expects(bottomAlpha >= 0 && bottomAlpha < 256);
	Expects(height * style::DevicePixelRatio() < 65536);

	const auto base = (uint32(color.red()) << 16)
		| (uint32(color.green()) << 8)
		| uint32(color.blue());
	const auto premultiplied = (topAlpha == bottomAlpha) || !base;
	auto result = QImage(
		QSize(1, height * style::DevicePixelRatio()),
		(premultiplied
			? QImage::Format_ARGB32_Premultiplied
			: QImage::Format_ARGB32));
	if (topAlpha == bottomAlpha) {
		color.setAlpha(topAlpha);
		result.fill(color);
		return result;
	}
	constexpr auto kShift = 16;
	constexpr auto kMultiply = (1U << kShift);
	const auto values = std::abs(topAlpha - bottomAlpha);
	const auto rows = uint32(result.height());
	const auto step = (values * kMultiply) / (rows - 1);
	const auto till = rows * uint32(step);
	Assert(result.bytesPerLine() == sizeof(uint32));
	auto ints = reinterpret_cast<uint32*>(result.bits());
	if (topAlpha < bottomAlpha) {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = base | ((topAlpha + (i >> kShift)) << 24);
		}
	} else {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = base | ((topAlpha - (i >> kShift)) << 24);
		}
	}
	if (!premultiplied) {
		result = std::move(result).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	return result;
}

rpl::producer<QString> MuteButtonTooltip(not_null<GroupCall*> call) {
	//return rpl::single(std::make_tuple(
	//	(Data::GroupCall*)nullptr,
	//	call->scheduleDate()
	//)) | rpl::then(call->real(
	//) | rpl::map([](not_null<Data::GroupCall*> real) {
	//	using namespace rpl::mappers;
	//	return real->scheduleDateValue(
	//	) | rpl::map([=](TimeId scheduleDate) {
	//		return std::make_tuple(real.get(), scheduleDate);
	//	});
	//}) | rpl::flatten_latest(
	//)) | rpl::map([=](
	//		Data::GroupCall *real,
	//		TimeId scheduleDate) -> rpl::producer<QString> {
	//	if (scheduleDate) {
	//		return rpl::combine(
	//			call->canManageValue(),
	//			(real
	//				? real->scheduleStartSubscribedValue()
	//				: rpl::single(false))
	//		) | rpl::map([](bool canManage, bool subscribed) {
	//			return canManage
	//				? tr::lng_group_call_start_now()
	//				: subscribed
	//				? tr::lng_group_call_cancel_reminder()
	//				: tr::lng_group_call_set_reminder();
	//		}) | rpl::flatten_latest();
	//	}
		return call->mutedValue(
		) | rpl::map([](MuteState muted) {
			switch (muted) {
			case MuteState::Active:
			case MuteState::PushToTalk:
				return tr::lng_group_call_you_are_live();
			case MuteState::ForceMuted:
				return tr::lng_group_call_tooltip_force_muted();
			case MuteState::RaisedHand:
				return tr::lng_group_call_tooltip_raised_hand();
			case MuteState::Muted:
				return tr::lng_group_call_tooltip_microphone();
			}
			Unexpected("Value in MuteState in showNiceTooltip.");
		}) | rpl::flatten_latest();
	//}) | rpl::flatten_latest();
}

} // namespace Calls::Group
