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
struct LargeVideoTrack;

class Viewport final {
public:
	Viewport(QWidget *parent, PanelMode mode);
	~Viewport();

	[[nodiscard]] not_null<QWidget*> widget() const;
	[[nodiscard]] not_null<Ui::RpWidgetWrap*> rp() const;

	void setMode(PanelMode mode);
	void setControlsShown(float64 shown);

	void add(
		const VideoEndpoint &endpoint,
		LargeVideoTrack track,
		rpl::producer<bool> pinned);
	void remove(const VideoEndpoint &endpoint);
	void showLarge(const VideoEndpoint &endpoint);

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
	void setTileGeometry(not_null<VideoTile*> tile, QRect geometry);

	void setSelected(Selection value);
	void setPressed(Selection value);

	void handleMousePress(QPoint position, Qt::MouseButton button);
	void handleMouseRelease(QPoint position, Qt::MouseButton button);
	void handleMouseMove(QPoint position);

	[[nodiscard]] Ui::GL::ChosenRenderer chooseRenderer(
		Ui::GL::Capabilities capabilities);

	Fn<void(const Textures &)> _freeTextures;
	rpl::variable<PanelMode> _mode;
	const std::unique_ptr<Ui::RpWidgetWrap> _content;
	std::vector<std::unique_ptr<VideoTile>> _tiles;
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
