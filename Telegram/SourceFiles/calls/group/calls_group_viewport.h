/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace Ui {
class AbstractButton;
class RpWidgetWrap;
namespace GL {
enum class Backend;
struct Capabilities;
struct ChosenRenderer;
} // namespace GL
} // namespace Ui

namespace Calls {
class GroupCall;
struct VideoEndpoint;
struct VideoQualityRequest;
} // namespace Calls

namespace Webrtc {
class VideoTrack;
} // namespace Webrtc

namespace Calls::Group {

class MembersRow;
enum class PanelMode;
enum class VideoQuality;

struct VideoTileTrack {
	Webrtc::VideoTrack *track = nullptr;
	MembersRow *row = nullptr;
	rpl::variable<QSize> trackSize;

	[[nodiscard]] explicit operator bool() const {
		return track != nullptr;
	}
};

[[nodiscard]] inline bool operator==(
		VideoTileTrack a,
		VideoTileTrack b) noexcept {
	return (a.track == b.track) && (a.row == b.row);
}

[[nodiscard]] inline bool operator!=(
		VideoTileTrack a,
		VideoTileTrack b) noexcept {
	return !(a == b);
}

class Viewport final {
public:
	Viewport(
		not_null<QWidget*> parent,
		PanelMode mode,
		Ui::GL::Backend backend);
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
		VideoTileTrack track,
		rpl::producer<QSize> trackSize,
		rpl::producer<bool> pinned);
	void remove(const VideoEndpoint &endpoint);
	void showLarge(const VideoEndpoint &endpoint);

	[[nodiscard]] bool requireARGB32() const;
	[[nodiscard]] int fullHeight() const;
	[[nodiscard]] rpl::producer<int> fullHeightValue() const;
	[[nodiscard]] rpl::producer<bool> pinToggled() const;
	[[nodiscard]] rpl::producer<VideoEndpoint> clicks() const;
	[[nodiscard]] rpl::producer<VideoQualityRequest> qualityRequests() const;
	[[nodiscard]] rpl::producer<bool> mouseInsideValue() const;

	[[nodiscard]] rpl::lifetime &lifetime();

	static constexpr auto kShadowMaxAlpha = 80;

private:
	struct Textures;
	class VideoTile;
	class RendererSW;
	class RendererGL;
	using TileId = quintptr;

	struct Geometry {
		VideoTile *tile = nullptr;
		QSize size;
		QRect rows;
		QRect columns;
	};

	struct Layout {
		std::vector<Geometry> list;
		QSize outer;
		bool useColumns = false;
	};

	struct TileAnimation {
		QSize from;
		QSize to;
		float64 ratio = -1.;
	};

	struct Selection {
		enum class Element {
			None,
			Tile,
			PinButton,
			BackButton,
		};
		VideoTile *tile = nullptr;
		Element element = Element::None;

		inline bool operator==(Selection other) const {
			return (tile == other.tile) && (element == other.element);
		}
	};

	void setup();
	[[nodiscard]] bool wide() const;

	void updateCursor();
	void updateTilesGeometry();
	void updateTilesGeometry(int outerWidth);
	void updateTilesGeometryWide(int outerWidth, int outerHeight);
	void updateTilesGeometryNarrow(int outerWidth);
	void updateTilesGeometryColumn(int outerWidth);
	void setTileGeometry(not_null<VideoTile*> tile, QRect geometry);
	void refreshHasTwoOrMore();
	void updateTopControlsVisibility();

	void prepareLargeChangeAnimation();
	void startLargeChangeAnimation();
	void updateTilesAnimated();
	[[nodiscard]] Layout countWide(int outerWidth, int outerHeight) const;
	[[nodiscard]] Layout applyLarge(Layout layout) const;

	void setSelected(Selection value);
	void setPressed(Selection value);

	void handleMousePress(QPoint position, Qt::MouseButton button);
	void handleMouseRelease(QPoint position, Qt::MouseButton button);
	void handleMouseMove(QPoint position);
	void updateSelected(QPoint position);
	void updateSelected();

	[[nodiscard]] Ui::GL::ChosenRenderer chooseRenderer(
		Ui::GL::Backend backend);

	PanelMode _mode = PanelMode();
	bool _opengl = false;
	bool _geometryStaleAfterModeChange = false;
	const std::unique_ptr<Ui::RpWidgetWrap> _content;
	std::vector<std::unique_ptr<VideoTile>> _tiles;
	std::vector<not_null<VideoTile*>> _tilesForOrder;
	rpl::variable<int> _fullHeight = 0;
	bool _hasTwoOrMore = false;
	int _scrollTop = 0;
	QImage _shadow;
	rpl::event_stream<VideoEndpoint> _clicks;
	rpl::event_stream<bool> _pinToggles;
	rpl::event_stream<VideoQualityRequest> _qualityRequests;
	float64 _controlsShownRatio = 1.;
	VideoTile *_large = nullptr;
	Fn<void()> _updateLargeScheduled;
	Ui::Animations::Simple _largeChangeAnimation;
	Layout _startTilesLayout;
	Layout _finishTilesLayout;
	Selection _selected;
	Selection _pressed;
	rpl::variable<bool> _mouseInside = false;

};

[[nodiscard]] QImage GenerateShadow(
	int height,
	int topAlpha,
	int bottomAlpha,
	QColor color = QColor(0, 0, 0));

[[nodiscard]] rpl::producer<QString> MuteButtonTooltip(
	not_null<GroupCall*> call);

} // namespace Calls::Group
