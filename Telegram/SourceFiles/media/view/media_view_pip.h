/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_instance.h"
#include "ui/effects/animations.h"
#include "ui/round_rect.h"
#include "ui/rp_widget.h"

#include <QtCore/QPointer>

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Ui {
class IconButton;
template <typename Widget>
class FadeWrap;
namespace GL {
struct ChosenRenderer;
struct Capabilities;
} // namespace GL
} // namespace Ui

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player

namespace View {

class PlaybackProgress;

[[nodiscard]] QRect RotatedRect(QRect rect, int rotation);
[[nodiscard]] bool UsePainterRotation(int rotation);
[[nodiscard]] QSize FlipSizeByRotation(QSize size, int rotation);
[[nodiscard]] QImage RotateFrameImage(QImage image, int rotation);

class PipPanel final {
public:
	struct Position {
		RectParts attached = RectPart(0);
		RectParts snapped = RectPart(0);
		QRect geometry;
		QRect screen;
	};

	PipPanel(
		QWidget *parent,
		Fn<Ui::GL::ChosenRenderer(Ui::GL::Capabilities)> renderer);
	void init();

	[[nodiscard]] not_null<QWidget*> widget() const;
	[[nodiscard]] not_null<Ui::RpWidgetWrap*> rp() const;

	void update();
	void setGeometry(QRect geometry);

	void setAspectRatio(QSize ratio);
	[[nodiscard]] Position countPosition() const;
	void setPosition(Position position);
	[[nodiscard]] QRect inner() const;
	[[nodiscard]] RectParts attached() const;
	[[nodiscard]] bool useTransparency() const;

	void setDragDisabled(bool disabled);
	[[nodiscard]] bool dragging() const;

	void handleMousePress(QPoint position, Qt::MouseButton button);
	void handleMouseRelease(QPoint position, Qt::MouseButton button);
	void handleMouseMove(QPoint position);

	[[nodiscard]] rpl::producer<> saveGeometryRequests() const;

private:
	void setPositionDefault();
	void setPositionOnScreen(Position position, QRect available);

	QScreen *myScreen() const;
	void startSystemDrag();
	void processDrag(QPoint point);
	void finishDrag(QPoint point);
	void updatePositionAnimated();
	void updateOverState(QPoint point);
	void moveAnimated(QPoint to);
	void updateDecorations();

	const std::unique_ptr<Ui::RpWidgetWrap> _content;
	const QPointer<QWidget> _parent;
	RectParts _attached = RectParts();
	RectParts _snapped = RectParts();
	QSize _ratio;

	bool _useTransparency = true;
	bool _dragDisabled = false;
	style::margins _padding;

	RectPart _overState = RectPart();
	std::optional<RectPart> _pressState;
	QPoint _pressPoint;
	QRect _dragStartGeometry;
	std::optional<RectPart> _dragState;
	rpl::event_stream<> _saveGeometryRequests;

	QPoint _positionAnimationFrom;
	QPoint _positionAnimationTo;
	Ui::Animations::Simple _positionAnimation;

};

class Pip final {
public:
	class Delegate {
	public:
		virtual void pipSaveGeometry(QByteArray geometry) = 0;
		[[nodiscard]] virtual QByteArray pipLoadGeometry() = 0;
		[[nodiscard]] virtual float64 pipPlaybackSpeed() = 0;
		[[nodiscard]] virtual QWidget *pipParentWidget() = 0;
	};

	Pip(
		not_null<Delegate*> delegate,
		not_null<DocumentData*> data,
		FullMsgId contextId,
		std::shared_ptr<Streaming::Document> shared,
		FnMut<void()> closeAndContinue,
		FnMut<void()> destroy);
	~Pip();

private:
	enum class OverState {
		None,
		Close,
		Enlarge,
		Playback,
		VolumeToggle,
		VolumeController,
		Other,
	};
	enum class ThumbState {
		Empty,
		Inline,
		Thumb,
		Good,
		Cover,
	};
	struct Button {
		QRect area;
		QRect icon;
		OverState state = OverState::None;
		Ui::Animations::Simple active;
	};
	struct ContentGeometry {
		QRect inner;
		RectParts attached = RectParts();
		float64 fade = 0.;
		QSize outer;
		int rotation = 0;
		int videoRotation = 0;
		bool useTransparency = false;
	};
	struct StaticContent {
		QImage image;
		bool blurred = false;
	};
	using FrameRequest = Streaming::FrameRequest;
	class Renderer;
	class RendererGL;
	class RendererSW;

	void setupPanel();
	void setupButtons();
	void setupStreaming();
	void playbackPauseResume();
	void volumeChanged(float64 volume);
	void volumeToggled();
	void volumeControllerUpdate(QPoint position);
	void waitingAnimationCallback();
	void handleStreamingUpdate(Streaming::Update &&update);
	void handleStreamingError(Streaming::Error &&error);
	void saveGeometry();

	void updatePlaybackState();
	void updatePlayPauseResumeState(const Player::TrackState &state);
	void restartAtSeekPosition(crl::time position);

	[[nodiscard]] bool canUseVideoFrame() const;
	[[nodiscard]] QImage videoFrame(const FrameRequest &request) const;
	[[nodiscard]] Streaming::FrameWithInfo videoFrameWithInfo() const; // YUV
	[[nodiscard]] QImage staticContent() const;
	[[nodiscard]] OverState computeState(QPoint position) const;
	void setOverState(OverState state);
	void setPressedState(std::optional<OverState> state);
	[[nodiscard]] OverState shownActiveState() const;
	[[nodiscard]] float64 activeValue(const Button &button) const;
	void updateActiveState(OverState was);
	void updatePlaybackTexts(int64 position, int64 length, int64 frequency);

	[[nodiscard]] static OverState ResolveShownOver(OverState state);

	[[nodiscard]] Ui::GL::ChosenRenderer chooseRenderer(
		Ui::GL::Capabilities capabilities);
	void paint(not_null<Renderer*> renderer) const;

	void handleMouseMove(QPoint position);
	void handleMousePress(QPoint position, Qt::MouseButton button);
	void handleMouseRelease(QPoint position, Qt::MouseButton button);
	void handleDoubleClick(Qt::MouseButton button);
	void handleLeave();
	void handleClose();

	void paintRadialLoadingContent(
		QPainter &p,
		const QRect &inner,
		QColor fg) const;
	void paintButtons(not_null<Renderer*> renderer, float64 shown) const;
	void paintPlayback(not_null<Renderer*> renderer, float64 shown) const;
	void paintPlaybackContent(QPainter &p, QRect outer, float64 shown) const;
	void paintPlaybackProgress(QPainter &p, QRect outer) const;
	void paintProgressBar(
		QPainter &p,
		const QRect &rect,
		float64 progress,
		int radius,
		float64 active) const;
	void paintPlaybackTexts(QPainter &p, QRect outer) const;
	void paintVolumeController(
		not_null<Renderer*> renderer,
		float64 shown) const;
	void paintVolumeControllerContent(
		QPainter &p,
		QRect outer,
		float64 shown) const;
	[[nodiscard]] QRect countRadialRect() const;

	void seekUpdate(QPoint position);
	void seekProgress(float64 value);
	void seekFinish(float64 value);

	const not_null<Delegate*> _delegate;
	not_null<DocumentData*> _data;
	FullMsgId _contextId;
	Streaming::Instance _instance;
	bool _opengl = false;
	PipPanel _panel;
	QSize _size;
	std::unique_ptr<PlaybackProgress> _playbackProgress;
	std::shared_ptr<Data::DocumentMedia> _dataMedia;

	bool _showPause = false;
	bool _startPaused = false;
	bool _pausedBySeek = false;
	QString _timeAlready, _timeLeft;
	int _timeLeftWidth = 0;
	int _rotation = 0;
	float64 _lastPositiveVolume = 1.;
	crl::time _seekPositionMs = -1;
	crl::time _lastDurationMs = 0;
	OverState _over = OverState::None;
	std::optional<OverState> _pressed;
	std::optional<OverState> _lastHandledPress;
	Button _close;
	Button _enlarge;
	Button _playback;
	Button _play;
	Button _volumeToggle;
	Button _volumeController;
	Ui::Animations::Simple _controlsShown;

	FnMut<void()> _closeAndContinue;
	FnMut<void()> _destroy;

	mutable QImage _preparedCoverStorage;
	mutable ThumbState _preparedCoverState = ThumbState::Empty;

};

} // namespace View
} // namespace Media
