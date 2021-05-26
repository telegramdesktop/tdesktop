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
#include "lang/lang_keys.h"
#include "styles/style_calls.h"

#include <QtGui/QtEvents>
#include <QtGui/QOpenGLShader>

namespace Calls::Group {

Viewport::Viewport(not_null<QWidget*> parent, PanelMode mode)
: _mode(mode)
, _content(Ui::GL::CreateSurface(
	parent,
	[=](Ui::GL::Capabilities capabilities) {
		return chooseRenderer(capabilities);
	})) {
	setup();
}

Viewport::~Viewport() {
	for (const auto &tile : base::take(_tiles)) {
		if (auto textures = tile->takeTextures()) {
			_freeTextures(base::take(textures));
		}
	}
}

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
			tile->togglePinShown(false);
		}
	} else if (_selected.tile) {
		_selected.tile->togglePinShown(true);
	}
}

void Viewport::handleMousePress(QPoint position, Qt::MouseButton button) {
	handleMouseMove(position);

	if (button == Qt::LeftButton) {
		setPressed(_selected);
	}
}

void Viewport::handleMouseRelease(QPoint position, Qt::MouseButton button) {
	handleMouseMove(position);
	const auto pressed = _pressed;
	setPressed({});
	if (const auto tile = pressed.tile) {
		if (pressed == _selected) {
			if (!wide()) {
				_clicks.fire_copy(tile->endpoint());
			} else if (pressed.element == Selection::Element::PinButton) {
				_pinToggles.fire({
					.endpoint = tile->endpoint(),
					.pinned = !tile->pinned(),
				});
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
		const auto geometry = tile->geometry();
		if (geometry.contains(position)) {
			const auto pin = wide()
				&& tile->pinOuter().contains(position - geometry.topLeft());
			setSelected({
				.tile = tile.get(),
				.element = (pin
					? Selection::Element::PinButton
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
		LargeVideoTrack track,
		rpl::producer<bool> pinned) {
	_tiles.push_back(std::make_unique<VideoTile>(
		endpoint,
		track,
		std::move(pinned),
		[=] { widget()->update(); }));

	_tiles.back()->trackSizeValue(
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
	if (_large == removing) {
		_large = nullptr;
	}
	if (_selected.tile == removing) {
		setSelected({});
	}
	if (_pressed.tile == removing) {
		setPressed({});
	}
	if (const auto textures = removing->takeTextures()) {

	}
	_tiles.erase(i);
	updateTilesGeometry();
}

void Viewport::showLarge(const VideoEndpoint &endpoint) {
	const auto i = ranges::find(_tiles, endpoint, &VideoTile::endpoint);
	const auto large = (i != end(_tiles)) ? i->get() : nullptr;
	if (_large != large) {
		_large = large;
		if (wide()) {
			updateTilesGeometry();
		}
	}
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
		_fullHeight = 0;
	} else {
		updateTilesGeometryNarrow(outerWidth);
	}
}

void Viewport::updateTilesGeometryWide(int outerWidth, int outerHeight) {
	if (!outerHeight) {
		return;
	}

	struct Geometry {
		QSize size;
		QRect columns;
		QRect rows;
	};
	auto sizes = base::flat_map<not_null<VideoTile*>, Geometry>();
	sizes.reserve(_tiles.size());
	for (const auto &tile : _tiles) {
		const auto video = tile.get();
		const auto size = (_large && video != _large)
			? QSize()
			: video->trackSize();
		if (size.isEmpty()) {
			setTileGeometry(video, { 0, 0, outerWidth, 0 });
		} else {
			sizes.emplace(video, Geometry{ size });
		}
	}
	if (sizes.size() == 1) {
		setTileGeometry(
			sizes.front().first,
			{ 0, 0, outerWidth, outerHeight });
		return;
	}
	if (sizes.empty()) {
		return;
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
				auto &geometry = (sizes.begin() + index)->second;
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
				auto &geometry = (sizes.begin() + index)->second;
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
	const auto layout = (columnsBlack < rowsBlack)
		? &Geometry::columns
		: &Geometry::rows;
	for (const auto &[video, geometry] : sizes) {
		setTileGeometry(video, geometry.*layout);
	}
}

void Viewport::updateTilesGeometryNarrow(int outerWidth) {
	const auto y = -_scrollTop;

	auto sizes = base::flat_map<not_null<VideoTile*>, QSize>();
	sizes.reserve(_tiles.size());
	for (const auto &tile : _tiles) {
		const auto video = tile.get();
		const auto size = video->trackSize();
		if (size.isEmpty()) {
			video->setGeometry({ 0, y, outerWidth, 0 });
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
		sizes.front().first->setGeometry({ 0, y, outerWidth, height });
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
		tile->setGeometry({
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

void Viewport::setTileGeometry(not_null<VideoTile*> tile, QRect geometry) {
	tile->setGeometry(geometry);

	const auto min = std::min(geometry.width(), geometry.height());
	const auto kMedium = style::ConvertScale(480);
	const auto kSmall = style::ConvertScale(240);
	const auto quality = (min >= kMedium)
		? VideoQuality::Full
		: (min >= kSmall)
		? VideoQuality::Medium
		: VideoQuality::Thumbnail;
	if (tile->updateRequestedQuality(quality)) {
		_qualityRequests.fire(VideoQualityRequest{
			.endpoint = tile->endpoint(),
			.quality = quality,
		});
	}
}

void Viewport::setSelected(Selection value) {
	if (_selected == value) {
		return;
	}
	if (_selected.tile) {
		_selected.tile->togglePinShown(false);
	}
	_selected = value;
	if (_selected.tile && wide()) {
		_selected.tile->togglePinShown(true);
	}
	const auto pointer = _selected.tile
		&& (!wide() || _selected.element == Selection::Element::PinButton);
	widget()->setCursor(pointer ? style::cur_pointer : style::cur_default);
}

void Viewport::setPressed(Selection value) {
	if (_pressed == value) {
		return;
	}
	_pressed = value;
}

Ui::GL::ChosenRenderer Viewport::chooseRenderer(
		Ui::GL::Capabilities capabilities) {
	const auto use = Platform::IsMac()
		? true
		: Platform::IsWindows()
		? capabilities.supported
		: capabilities.transparency;
	LOG(("OpenGL: %1 (Calls::Group::Viewport)").arg(Logs::b(use)));
	if (use) {
		auto renderer = std::make_unique<RendererGL>(this);
		_freeTextures = [raw = renderer.get()](const Textures &textures) {
			raw->free(textures);
		};
		return {
			.renderer = std::move(renderer),
			.backend = Ui::GL::Backend::OpenGL,
		};
	}
	return {
		.renderer = std::make_unique<Renderer>(this),
		.backend = Ui::GL::Backend::Raster,
	};
}

bool Viewport::requireARGB32() const {
	return !_freeTextures;
}

int Viewport::fullHeight() const {
	return _fullHeight.current();
}

rpl::producer<int> Viewport::fullHeightValue() const {
	return _fullHeight.value();
}

rpl::producer<VideoPinToggle> Viewport::pinToggled() const {
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

} // namespace Calls::Group
