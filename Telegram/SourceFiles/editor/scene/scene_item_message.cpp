/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_message.h"

#include "data/data_document.h"
#include "editor/editor_audio_menu.h"
#include "editor/editor_message_render.h"
#include "editor/editor_message_source.h"
#include "editor/editor_message_video.h"
#include "editor/scene/scene.h"
#include "editor/video/video_segment_player.h"
#include "lang/lang_keys.h"
#include "ui/effects/radial_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/popup_menu.h"
#include "window/themes/window_theme.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QCoreApplication>
#include <QGraphicsSceneMouseEvent>

namespace Editor {
namespace {

constexpr auto kMaxRatio = 8;
constexpr auto kPlaybackFrameSide = 1024;

[[nodiscard]] bool OnMainThread() {
	return QThread::currentThread() == QCoreApplication::instance()->thread();
}

[[nodiscard]] int RatioFor(float64 width, int logicalWidth) {
	return std::clamp(int(std::ceil(width / logicalWidth)), 1, kMaxRatio);
}

[[nodiscard]] QRect CoverRect(QRect hole, QSize video) {
	if (video.isEmpty() || hole.isEmpty()) {
		return hole;
	}
	const auto scale = std::max(
		hole.width() / float64(video.width()),
		hole.height() / float64(video.height()));
	const auto size = QSize(
		int(std::ceil(video.width() * scale)),
		int(std::ceil(video.height() * scale)));
	return QRect(
		rect::center(hole) - QPoint(size.width() / 2, size.height() / 2),
		size);
}

[[nodiscard]] QSize PlaybackFrameSize(QSize frames) {
	const auto ratio = style::DevicePixelRatio();
	const auto side = std::max(frames.width(), frames.height());
	const auto fit = std::min(1., kPlaybackFrameSide / float64(side));
	return QSize(
		std::max(int(std::round(frames.width() * fit / ratio)), 1),
		std::max(int(std::round(frames.height() * fit / ratio)), 1));
}

} // namespace

ItemMessage::ItemMessage(
	std::shared_ptr<MessageSource> source,
	std::unique_ptr<MessageRenderer> renderer,
	ItemBase::Data data,
	std::optional<bool> dark,
	MessageVideoOptions video)
: ItemAnimated(std::move(data))
, _source(std::move(source))
, _renderer(std::move(renderer))
, _videoOptions(video)
, _dark(dark) {
	attachRenderer();
	watchVideo();
}

ItemMessage::~ItemMessage() = default;

void ItemMessage::attachRenderer() {
	_renderer->setDark(_dark);
	_renderer->setRepaintCallback([=] { scheduleRefresh(); });
	setImage(_renderer->render(1), 1);
	updateSize();
}

void ItemMessage::scheduleRefresh() {
	if (_refreshScheduled) {
		return;
	}
	_refreshScheduled = true;
	crl::on_main(crl::guard(this, [=] {
		_refreshScheduled = false;
		refresh();
	}));
}

void ItemMessage::refresh() {
	if (!_renderer->ready()) {
		return;
	}
	const auto ratio = std::max(_ratio, 1);
	auto image = _renderer->render(ratio);
	if (image.isNull()) {
		return;
	}
	setImage(std::move(image), ratio);
	updateSize();
	update();
}

void ItemMessage::setImage(QImage image, int ratio) {
	_image = std::move(image);
	_image.setDevicePixelRatio(1.);
	_ratio = _image.isNull() ? 0 : ratio;
	_mediaRect = _renderer->mediaRect();
	_mask = QImage();
	_composite = QImage();
}

void ItemMessage::updateSize() {
	const auto size = _renderer->size();
	if (size.isEmpty() || (size == _size)) {
		return;
	}
	_size = size;
	setAspectRatio(_size.height() / float64(_size.width()));
}

int ItemMessage::neededRatio(not_null<QPainter*> p) const {
	if (_size.isEmpty()) {
		return 1;
	}
	const auto &transform = p->worldTransform();
	const auto device = p->device();
	const auto scale = std::hypot(transform.m11(), transform.m12())
		* (device ? device->devicePixelRatio() : 1.);
	return RatioFor(visibleRect().width() * scale, _size.width());
}

void ItemMessage::ensureRatio(int ratio) {
	if (ratio <= _ratio || !_renderer->ready() || !OnMainThread()) {
		return;
	}
	auto image = _renderer->render(ratio);
	if (!image.isNull()) {
		setImage(std::move(image), ratio);
	}
}

void ItemMessage::watchVideo() {
	_videoLifetime.destroy();
	const auto video = _videoOptions.play ? _source->video() : nullptr;
	if (!video) {
		return;
	}
	video->changes(
	) | rpl::on_next([=] {
		checkVideo();
	}, _videoLifetime);
	video->load();
	checkVideo();
}

void ItemMessage::checkVideo() {
	const auto video = _source->video();
	if (!video) {
		return;
	} else if (!isNormalStatus()) {
		_radial = nullptr;
		return;
	} else if (!_clip) {
		if (auto source = video->source()) {
			createClip(std::move(source));
		}
	}
	const auto loading = !_clip && video->loading();
	if (loading && !_radial) {
		_radial = std::make_unique<Ui::RadialAnimation>([=](crl::time now) {
			updateRadial(now);
		});
		_radial->start(video->progress());
	} else if (loading) {
		_radial->update(video->progress(), false, crl::now());
	} else {
		_radial = nullptr;
	}
	update();
}

void ItemMessage::updateRadial(crl::time now) {
	const auto video = _source->video();
	if (!_radial || !video) {
		return;
	}
	const auto loading = !_clip && video->loading();
	_radial->update(video->progress(), !loading, now);
	update();
}

void ItemMessage::createClip(std::shared_ptr<VideoClipSource> source) {
	auto copy = std::make_shared<VideoClipSource>(*source);
	copy->hasAudio = copy->hasAudio && _videoOptions.sound;
	_clip = std::make_unique<VideoClip>(std::move(copy), [=] {
		checkCutout();
		update();
	});
	if (_playersReleased) {
		_clip->stop();
	}
	notifyVideoClipChanged();
}

void ItemMessage::checkCutout() {
	if (_cutout || !_clip || !_clip->player()->ready()) {
		return;
	}
	_cutout = true;
	update();
}

void ItemMessage::notifyVideoClipChanged() {
	if (const auto owner = static_cast<Scene*>(scene())) {
		owner->videoClipChanged(this);
	}
}

QRect ItemMessage::holeRect() const {
	return QRect(_mediaRect.topLeft() * _ratio, _mediaRect.size() * _ratio);
}

QRect ItemMessage::framesRect() const {
	const auto hole = holeRect();
	if (!_clip) {
		return hole;
	}
	const auto thumbnail = _clip->source()->thumbnail.size();
	const auto video = _source->video();
	return CoverRect(
		hole,
		(!thumbnail.isEmpty()
			? thumbnail
			: video
			? video->document()->dimensions
			: QSize()));
}

Media::Encode::AnimatedEntity::Cutout ItemMessage::cutout() const {
	const auto hole = holeRect();
	if (_mask.isNull()) {
		_mask = _renderer->videoMask(std::max(_ratio, 1));
	}
	return {
		.picture = _image,
		.mask = _mask,
		.hole = hole,
		.frames = framesRect(),
	};
}

const QImage &ItemMessage::composeFrame() {
	Expects(_clip != nullptr);

	const auto hole = holeRect();
	const auto frame = hole.isEmpty()
		? QImage()
		: _clip->frame(PlaybackFrameSize(framesRect().size()));
	if (frame.isNull()) {
		return _composite.isNull() ? _image : _composite;
	}
	return Media::Encode::ComposeCutout(cutout(), frame, _composite);
}

void ItemMessage::paintLoading(QPainter *p) const {
	if (_mediaRect.isEmpty() || _size.isEmpty()) {
		return;
	}
	const auto rect = visibleRect();
	const auto scale = rect.width() / _size.width();
	const auto center = rect.topLeft()
		+ rect::center(QRectF(_mediaRect)) * scale;
	const auto side = st::msgFileLayout.thumbSize * scale;
	const auto line = st::msgFileRadialLine * scale;
	const auto inner = QRectF(
		center.x() - side / 2. + line,
		center.y() - side / 2. + line,
		side - 2 * line,
		side - 2 * line);
	PainterHighQualityEnabler hq(*p);
	_radial->draw(*p, inner, line, st::historyFileThumbRadialFg);
}

void ItemMessage::save(SaveState state) {
	ItemBase::save(state);
	if (!_size.isEmpty()) {
		ensureRatio(RatioFor(visibleRect().width(), _size.width()));
	}
	((state == SaveState::Keep) ? _kept : _saved) = _clip
		? std::make_optional(_clip->state())
		: std::nullopt;
}

void ItemMessage::restore(SaveState state) {
	if (!hasState(state)) {
		return;
	}
	ItemBase::restore(state);
	const auto &saved = (state == SaveState::Keep) ? _kept : _saved;
	if (_clip && saved) {
		_clip->restore(*saved);
	}
}

void ItemMessage::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	ensureRatio(neededRatio(p));
	if (w) {
		_playersReleased = false;
		if (_clip) {
			_clip->resume();
		}
	}
	if (!_image.isNull()) {
		const auto &image = (_cutout && w)
			? composeFrame()
			: (_cutout && !_composite.isNull())
			? _composite
			: _image;
		p->setRenderHint(QPainter::SmoothPixmapTransform);
		p->drawImage(visibleRect(), image);
	}
	if (_radial && w) {
		paintLoading(p);
	}
	ItemBase::paint(p, option, w);
}

int ItemMessage::type() const {
	return Type;
}

bool ItemMessage::animated() const {
	return _cutout && _clip && _clip->animated();
}

bool ItemMessage::hasContent() const {
	return _clip && _clip->hasContent();
}

QByteArray ItemMessage::content() const {
	return _clip ? _clip->content() : QByteArray();
}

crl::time ItemMessage::loopDuration() const {
	return _clip ? _clip->loopDuration() : 0;
}

VideoTrim ItemMessage::trim() const {
	return _clip ? _clip->trim() : VideoTrim();
}

void ItemMessage::releasePlayers() {
	_playersReleased = true;
	if (_clip) {
		_clip->stop();
	}
}

void ItemMessage::setStatus(Status status) {
	if (status != Status::Normal) {
		releasePlayers();
	}
	ItemBase::setStatus(status);
	if (status == Status::Normal) {
		checkVideo();
	}
}

VideoClip *ItemMessage::videoClip() {
	return _clip.get();
}

Media::Encode::AnimatedEntity ItemMessage::animatedEntity(
		const QTransform &sceneToCanvas) const {
	auto result = ItemAnimated::animatedEntity(sceneToCanvas);
	if (_cutout && !_image.isNull()) {
		result.cutout = cutout();
	}
	return result;
}

Media::Encode::AnimatedEntity::Kind ItemMessage::entityKind() const {
	return Media::Encode::AnimatedEntity::Kind::Webm;
}

QRectF ItemMessage::entityRect() const {
	return visibleRect();
}

const std::shared_ptr<MessageSource> &ItemMessage::source() const {
	return _source;
}

void ItemMessage::setSource(std::shared_ptr<MessageSource> source) {
	_videoLifetime.destroy();
	_clip = nullptr;
	_radial = nullptr;
	_cutout = false;
	_source = std::move(source);
	_renderer = std::make_unique<MessageRenderer>(_source);
	_ratio = 0;
	attachRenderer();
	watchVideo();
	notifyVideoClipChanged();
	update();
}

std::optional<bool> ItemMessage::dark() const {
	return _dark;
}

void ItemMessage::setDark(std::optional<bool> dark) {
	if (_dark == dark) {
		return;
	}
	_dark = dark;
	_ratio = 0;
	_renderer->setDark(dark);
	refresh();
}

void ItemMessage::setEditCallback(EditCallback callback) {
	_edit = std::move(callback);
}

bool ItemMessage::editable() const {
	return _edit && _source->link().has_value();
}

QRectF ItemMessage::visibleRect() const {
	return fittedRect(_size);
}

bool ItemMessage::flippable() const {
	return false;
}

void ItemMessage::fillContextMenu(not_null<Ui::PopupMenu*> menu) {
	const auto dark = _dark.value_or(Window::Theme::IsNightMode());
	menu->addAction(
		(dark
			? tr::lng_settings_theme_day(tr::now)
			: tr::lng_settings_theme_night(tr::now)),
		[=] { setDark(!dark); },
		&st::mediaMenuIconNightMode);
	if (editable()) {
		menu->addAction(
			tr::lng_menu_formatting_link_edit(tr::now),
			[=] { _edit(this); },
			&st::mediaMenuIconEdit);
	}
	if (_clip && _clip->hasAudio()) {
		AddVolumeAction(menu, _clip->volume(), [=](float64 volume) {
			_clip->setVolume(volume);
		});
	}
}

void ItemMessage::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
	if (editable()) {
		_edit(this);
	} else {
		ItemBase::mouseDoubleClickEvent(event);
	}
}

std::shared_ptr<ItemBase> ItemMessage::duplicate(ItemBase::Data data) const {
	auto result = std::make_shared<ItemMessage>(
		_source,
		std::make_unique<MessageRenderer>(_source),
		std::move(data),
		_dark,
		_videoOptions);
	result->_edit = _edit;
	if (_clip && result->_clip) {
		result->_clip->restore(_clip->state());
	}
	return result;
}

} // namespace Editor
