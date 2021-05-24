/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace style {
struct GroupCallLargeVideo;
} // namespace style

namespace Ui {
class AbstractButton;
class RpWidgetWrap;
namespace GL {
struct Capabilities;
struct ChosenRenderer;
} // namespace GL
} // namespace Ui

namespace Calls {
struct VideoEndpoint;
struct VideoPinToggle;
struct VideoQualityRequest;
} // namespace Calls

namespace Webrtc {
class VideoTrack;
} // namespace Webrtc

namespace Calls::Group {

class MembersRow;
enum class PanelMode;
enum class VideoQuality;

struct LargeVideoTrack {
	Webrtc::VideoTrack *track = nullptr;
	MembersRow *row = nullptr;

	[[nodiscard]] explicit operator bool() const {
		return track != nullptr;
	}
};

[[nodiscard]] inline bool operator==(
		LargeVideoTrack a,
		LargeVideoTrack b) noexcept {
	return (a.track == b.track) && (a.row == b.row);
}

[[nodiscard]] inline bool operator!=(
		LargeVideoTrack a,
		LargeVideoTrack b) noexcept {
	return !(a == b);
}

class Viewport final {
public:
	Viewport(not_null<QWidget*> parent, PanelMode mode);
	~Viewport();

	[[nodiscard]] not_null<QWidget*> widget() const;
	[[nodiscard]] not_null<Ui::RpWidgetWrap*> rp() const;

	void setMode(PanelMode mode, not_null<QWidget*> parent);
	void setControlsShown(float64 shown);
	void setGeometry(QRect geometry);
	void resizeToWidth(int newWidth);
	void setScrollTop(int scrollTop);

	void add(
		const VideoEndpoint &endpoint,
		LargeVideoTrack track,
		rpl::producer<bool> pinned);
	void remove(const VideoEndpoint &endpoint);
	void showLarge(const VideoEndpoint &endpoint);

	[[nodiscard]] int fullHeight() const;
	[[nodiscard]] rpl::producer<int> fullHeightValue() const;
	[[nodiscard]] rpl::producer<VideoPinToggle> pinToggled() const;
	[[nodiscard]] rpl::producer<VideoEndpoint> clicks() const;
	[[nodiscard]] rpl::producer<VideoQualityRequest> qualityRequests() const;
	[[nodiscard]] rpl::producer<bool> mouseInsideValue() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct Textures;
	class VideoTile;
	class Renderer;
	class RendererGL;

	struct Selection {
		enum class Element {
			None,
			Tile,
			PinButton,
		};
		VideoTile *tile = nullptr;
		Element element = Element::None;

		inline bool operator==(Selection other) const {
			return (tile == other.tile) && (element == other.element);
		}
	};

	void setup();
	[[nodiscard]] bool wide() const;

	void updateTilesGeometry();
	void updateTilesGeometry(int outerWidth);
	void updateTilesGeometryWide(int outerWidth, int outerHeight);
	void updateTilesGeometryNarrow(int outerWidth);
	void setTileGeometry(not_null<VideoTile*> tile, QRect geometry);

	void setSelected(Selection value);
	void setPressed(Selection value);

	void handleMousePress(QPoint position, Qt::MouseButton button);
	void handleMouseRelease(QPoint position, Qt::MouseButton button);
	void handleMouseMove(QPoint position);
	void updateSelected(QPoint position);
	void updateSelected();

	[[nodiscard]] Ui::GL::ChosenRenderer chooseRenderer(
		Ui::GL::Capabilities capabilities);

	Fn<void(const Textures &)> _freeTextures;
	PanelMode _mode = PanelMode();
	bool _geometryStaleAfterModeChange = false;
	const std::unique_ptr<Ui::RpWidgetWrap> _content;
	std::vector<std::unique_ptr<VideoTile>> _tiles;
	rpl::variable<int> _fullHeight = 0;
	int _scrollTop = 0;
	QImage _shadow;
	rpl::event_stream<VideoEndpoint> _clicks;
	rpl::event_stream<VideoPinToggle> _pinToggles;
	rpl::event_stream<VideoQualityRequest> _qualityRequests;
	float64 _controlsShownRatio = 1.;
	VideoTile *_large = nullptr;
	Selection _selected;
	Selection _pressed;
	rpl::variable<bool> _mouseInside = false;

};

[[nodiscard]] QImage GenerateShadow(
	int height,
	int topAlpha,
	int bottomAlpha,
	QColor color = QColor(0, 0, 0));

} // namespace Calls::Group
