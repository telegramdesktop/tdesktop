/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_viewport.h"

#include "calls/group/calls_group_large_video.h" // LargeVideoTrack.
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

Viewport::Viewport(QWidget *parent, PanelMode mode)
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
		if (const auto textures = tile->takeTextures()) {
			_freeTextures(textures);
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
	) | rpl::start_with_next([=] {
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

bool Viewport::wide() const {
	return (_mode.current() == PanelMode::Wide);
}

void Viewport::setMode(PanelMode mode) {
	if (_mode.current() == mode) {
		return;
	}
	_mode = mode;
	widget()->setVisible(wide()); // #TODO calls
	setControlsShown(1.);
	updateTilesGeometry();
	if (_mouseInside.current()) {
		handleMouseMove(widget()->mapFromGlobal(QCursor::pos()));
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

	//video->pinToggled( // #TODO calls
	//) | rpl::start_with_next([=](bool pinned) {
	//	_call->pinVideoEndpoint(pinned ? endpoint : VideoEndpoint{});
	//}, video->lifetime());

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
		updateTilesGeometry();
	}
}

void Viewport::updateTilesGeometry() {
	const auto outer = widget()->size();
	if (_tiles.empty() || outer.isEmpty()) {
		return;
	}

	const auto guard = gsl::finally([&] { widget()->update(); });

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
			setTileGeometry(video, { 0, 0, outer.width(), 0 });
		} else {
			sizes.emplace(video, Geometry{ size });
		}
	}
	if (sizes.size() == 1) {
		setTileGeometry(
			sizes.front().first,
			{ 0, 0, outer.width(), outer.height() });
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
		const auto sizew = (outer.width() + skip) / float64(columns);
		for (auto column = 0; column != columns; ++column) {
			const auto left = int(std::round(column * sizew));
			const auto width = int(std::round(column * sizew + sizew - skip))
				- left;
			const auto rows = int(std::round((count - index)
				/ float64(columns - column)));
			const auto sizeh = (outer.height() + skip) / float64(rows);
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
		const auto sizeh = (outer.height() + skip) / float64(rows);
		for (auto row = 0; row != rows; ++row) {
			const auto top = int(std::round(row * sizeh));
			const auto height = int(std::round(row * sizeh + sizeh - skip))
				- top;
			const auto columns = int(std::round((count - index)
				/ float64(rows - row)));
			const auto sizew = (outer.width() + skip) / float64(columns);
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

[[nodiscard]] rpl::producer<VideoPinToggle> Viewport::pinToggled() const {
	return _pinToggles.events();
}

[[nodiscard]] rpl::producer<VideoEndpoint> Viewport::clicks() const {
	return _clicks.events();
}

[[nodiscard]] rpl::producer<VideoQualityRequest> Viewport::qualityRequests() const {
	return _qualityRequests.events();
}

[[nodiscard]] rpl::producer<bool> Viewport::mouseInsideValue() const {
	return _mouseInside.value();
}

[[nodiscard]] rpl::lifetime &Viewport::lifetime() {
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
