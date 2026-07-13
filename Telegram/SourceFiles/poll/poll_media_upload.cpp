/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "poll/poll_media_upload.h"

#include "api/api_sending.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "lang/lang_keys.h"
#include "layout/layout_document_generic_preview.h"
#include "main/main_session.h"
#include "platform/platform_file_utilities.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/fields/input_field.h"
#include "styles/style_boxes.h"
#include "styles/style_overview.h"
#include "styles/style_polls.h"

#include <QtCore/QMimeData>

namespace PollMediaUpload {

LocalImageThumbnail::LocalImageThumbnail(QImage original)
: _original(std::move(original)) {
}

std::shared_ptr<Ui::DynamicImage> LocalImageThumbnail::clone() {
	return std::make_shared<LocalImageThumbnail>(_original);
}

QImage LocalImageThumbnail::image(int size) {
	return _original;
}

void LocalImageThumbnail::subscribeToUpdates(Fn<void()> callback) {
}

QImage GenerateDocumentFilePreview(
		const QString &filename,
		int size) {
	const auto preview = Layout::DocumentGenericPreview::Create(filename);
	const auto &color = preview.color;
	const auto &ext = preview.ext;

	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(ratio);

	auto p = QPainter(&result);
	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRoundedRect(
		QRect(0, 0, size, size),
		st::pollAttachRadius,
		st::pollAttachRadius);

	if (!ext.isEmpty()) {
		const auto refSize = st::overviewFileLayout.fileThumbSize;
		const auto fontSize = std::max(
			size * st::overviewFileExtFont->f.pixelSize() / refSize,
			8);
		const auto font = style::font(
			fontSize,
			st::overviewFileExtFont->flags(),
			st::overviewFileExtFont->family());
		const auto padding = size * st::overviewFileExtPadding / refSize;
		const auto maxw = size - padding * 2;
		auto extStr = ext;
		auto extw = font->width(extStr);
		if (extw > maxw) {
			extStr = font->elided(extStr, maxw, Qt::ElideMiddle);
			extw = font->width(extStr);
		}
		p.setFont(font);
		p.setPen(st::overviewFileExtFg);
		p.drawText(
			(size - extw) / 2,
			(size - font->height) / 2 + font->ascent,
			extStr);
	}
	p.end();
	return result;
}

bool ValidateFileDragData(not_null<const QMimeData*> data) {
	if (data->hasImage()) {
		return true;
	}
	const auto urls = Core::ReadMimeUrls(data);
	return (urls.size() == 1) && urls.front().isLocalFile();
}

QVector<MTPDocumentAttribute> ExtractAudioAttributes(
		const Ui::PreparedFile &file) {
	auto result = QVector<MTPDocumentAttribute>();
	if (!file.information) {
		return result;
	}
	const auto song = std::get_if<Ui::PreparedFileInformation::Song>(
		&file.information->media);
	if (!song) {
		return result;
	}
	const auto seconds = song->duration / 1000;
	using Flag = MTPDdocumentAttributeAudio::Flag;
	result.push_back(MTP_documentAttributeAudio(
		MTP_flags(Flag::f_title | Flag::f_performer),
		MTP_int(seconds),
		MTP_string(song->title),
		MTP_string(song->performer),
		MTPstring()));
	return result;
}

Ui::PreparedList FileListFromMimeData(
		not_null<const QMimeData*> data,
		bool premium) {
	using Error = Ui::PreparedList::Error;
	const auto urls = Core::ReadMimeUrls(data);
	if (!urls.isEmpty()) {
		return Storage::PrepareMediaList(
			urls.mid(0, 1),
			st::sendMediaPreviewSize,
			premium);
	} else if (auto read = Core::ReadMimeImage(data)) {
		return Storage::PrepareMediaFromImage(
			std::move(read.image),
			std::move(read.content),
			st::sendMediaPreviewSize);
	}
	return Ui::PreparedList(Error::EmptyFile, QString());
}

PollMediaButton::PollMediaButton(
	not_null<QWidget*> parent,
	const style::IconButton &st,
	std::shared_ptr<PollMediaState> state)
: Ui::RippleButton(parent, st.ripple)
, _st(st)
, _state(std::move(state))
, _attach(Ui::MakeIconThumbnail(_st.icon))
, _attachOver(_st.iconOver.empty()
	? _attach
	: Ui::MakeIconThumbnail(_st.iconOver))
, _radial([=](crl::time now) { radialAnimationCallback(now); }) {
	const auto weak = QPointer<PollMediaButton>(this);
	_state->update = [=] {
		if (weak) {
			weak->updateMediaSubscription();
			weak->update();
		}
	};
	resize(_st.width, _st.height);
	setPointerCursor(true);
	updateMediaSubscription();
}

void PollMediaButton::setIconColorOverride(
		std::optional<QColor> colorOverride) {
	_iconColorOverride = colorOverride;
	update();
}

void PollMediaButton::setRippleColorOverride(
		std::optional<QColor> colorOverride) {
	_rippleColorOverride = colorOverride;
	update();
}

PollMediaButton::~PollMediaButton() {
	if (_subscribed) {
		_subscribed->subscribeToUpdates(nullptr);
	}
}

void PollMediaButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	paintRipple(
		p,
		_st.rippleAreaPosition,
		_rippleColorOverride ? &*_rippleColorOverride : nullptr);
	const auto isLinkWithPhoto = _state->media.webpage
		&& _state->media.webpage->photo;
	if (_state->thumbnail) {
		const auto target = rippleRect();
		paintCover(
			p,
			target,
			_state->thumbnail->image(
				std::max(target.width(), target.height())),
			_state->rounded);
		if (isLinkWithPhoto) {
			p.save();
			auto hq = PainterHighQualityEnabler(p);
			auto path = QPainterPath();
			path.addRoundedRect(
				target,
				st::pollAttachRadius,
				st::pollAttachRadius);
			p.setClipPath(path);
			p.fillRect(target, st::songCoverOverlayFg);
			const auto center = QPointF(rect::center(target));
			p.translate(center);
			p.rotate(-45.);
			p.translate(-center);
			st::pollAttachLink.paintInCenter(p, target);
			p.restore();
		}
	} else if (_iconColorOverride) {
		const auto &icon = (isOver() && !_st.iconOver.empty())
			? _st.iconOver
			: _st.icon;
		auto position = _st.iconPosition;
		if (position.x() < 0) {
			position.setX((width() - icon.width()) / 2);
		}
		if (position.y() < 0) {
			position.setY((height() - icon.height()) / 2);
		}
		icon.paint(p, position, width(), *_iconColorOverride);
	} else if (const auto image = currentAttachThumbnail()) {
		const auto target = iconRect();
		p.drawImage(
			target,
			image->image(std::max(target.width(), target.height())));
	}
	if (_state->thumbnail && !_state->uploading && !isLinkWithPhoto) {
		const auto viewOpacity = _viewShown.value(
			(isOver() || isDown()) ? 1. : 0.);
		if (viewOpacity > 0.) {
			p.save();
			p.setOpacity(viewOpacity);
			auto path = QPainterPath();
			path.addRoundedRect(
				rippleRect(),
				st::pollAttachRadius,
				st::pollAttachRadius);
			p.setClipPath(path);
			p.fillRect(rippleRect(), st::songCoverOverlayFg);
			st::pollAttachView.paintInCenter(p, rippleRect());
			p.restore();
		}
	}
	if (_state->uploading && !_radial.animating()) {
		_radial.start(_state->progress);
	}
	if (_state->uploading || _radial.animating()) {
		if (_state->thumbnail) {
			p.save();
			auto path = QPainterPath();
			path.addRoundedRect(
				rippleRect(),
				st::pollAttachRadius,
				st::pollAttachRadius);
			p.setClipPath(path);
			p.fillRect(rippleRect(), st::songCoverOverlayFg);
			p.restore();
		}
		const auto cancelOpacity = _state->uploading
			? _cancelShown.value(
				(isOver() || isDown()) ? 1. : 0.)
			: 0.;
		const auto line = float64(st::lineWidth * 2);
		const auto margin = float64(st::pollAttachProgressMargin);
		const auto arc = QRectF(rippleRect()) - Margins(margin);
		if (cancelOpacity > 0.) {
			p.setOpacity(cancelOpacity);
			st::pollAttachCancel.paintInCenter(p, rippleRect());
			p.setOpacity(1.);
		}
		_radial.draw(p, arc, line, st::historyFileThumbRadialFg);
	}
}

void PollMediaButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	if (!_state->thumbnail) {
		update();
	}
	const auto over = isOver() || isDown();
	const auto wasOver = (was & StateFlag::Over)
		|| (was & StateFlag::Down);
	if (over != wasOver) {
		if (_state->uploading) {
			_cancelShown.start(
				[=] { update(); },
				over ? 0. : 1.,
				over ? 1. : 0.,
				st::universalDuration);
		}
		if (_state->thumbnail && !_state->uploading) {
			_viewShown.start(
				[=] { update(); },
				over ? 0. : 1.,
				over ? 1. : 0.,
				st::universalDuration);
		}
	}
}

QImage PollMediaButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(QSize(
		_st.rippleAreaSize,
		_st.rippleAreaSize));
}

QPoint PollMediaButton::prepareRippleStartPosition() const {
	auto result = mapFromGlobal(QCursor::pos())
		- _st.rippleAreaPosition;
	const auto rect = QRect(
		QPoint(),
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
	return rect.contains(result)
		? result
		: DisabledRippleStartPosition();
}

void PollMediaButton::paintCover(
		Painter &p,
		QRect target,
		QImage image,
		bool rounded) const {
	if (image.isNull() || target.isEmpty()) {
		return;
	}
	const auto source = QRectF(0, 0, image.width(), image.height());
	const auto kx = target.width() / source.width();
	const auto ky = target.height() / source.height();
	const auto scale = std::max(kx, ky);
	const auto size = QSizeF(
		source.width() * scale,
		source.height() * scale);
	const auto geometry = QRectF(
		target.x() + (target.width() - size.width()) / 2.,
		target.y() + (target.height() - size.height()) / 2.,
		size.width(),
		size.height());
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	if (rounded) {
		auto path = QPainterPath();
		path.addRoundedRect(
			target,
			st::pollAttachRadius,
			st::pollAttachRadius);
		p.setClipPath(path);
	}
	p.drawImage(geometry, image, source);
	p.restore();
}

void PollMediaButton::radialAnimationCallback(crl::time now) {
	const auto updated = _radial.update(
		_state->progress,
		!_state->uploading,
		now);
	if (!anim::Disabled() || updated || _radial.animating()) {
		update(rippleRect());
	}
}

QRect PollMediaButton::rippleRect() const {
	return QRect(
		_st.rippleAreaPosition,
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QRect PollMediaButton::iconRect() const {
	const auto over = isOver() || isDown();
	const auto &icon = over && !_st.iconOver.empty()
		? _st.iconOver
		: _st.icon;
	auto position = _st.iconPosition;
	if (position.x() < 0) {
		position.setX((width() - icon.width()) / 2);
	}
	if (position.y() < 0) {
		position.setY((height() - icon.height()) / 2);
	}
	return QRect(position, QSize(icon.width(), icon.height()));
}

std::shared_ptr<Ui::DynamicImage>
PollMediaButton::currentAttachThumbnail() const {
	return (isOver() || isDown()) ? _attachOver : _attach;
}

void PollMediaButton::updateMediaSubscription() {
	if (_subscribed == _state->thumbnail) {
		return;
	}
	if (_subscribed) {
		_subscribed->subscribeToUpdates(nullptr);
	}
	_subscribed = _state->thumbnail;
	if (!_subscribed) {
		return;
	}
	const auto weak = QPointer<PollMediaButton>(this);
	_subscribed->subscribeToUpdates([=] {
		if (weak) {
			weak->update();
		}
	});
}

PreparePollMediaTask::PreparePollMediaTask(
	FileLoadTask::Args &&args,
	Fn<void(std::shared_ptr<FilePrepareResult>)> done)
: _task(std::move(args))
, _done(std::move(done)) {
}

void PreparePollMediaTask::process() {
	_task.process({ .generateGoodThumbnail = false });
}

void PreparePollMediaTask::finish() {
	_done(_task.peekResult());
}

PollMediaUploader::PollMediaUploader(Args &&args)
: _session(args.session)
, _peer(args.peer)
, _showError(std::move(args.showError))
, _prepareQueue(std::make_unique<TaskQueue>()) {
	subscribeToUploader();
}

PollMediaUploader::~PollMediaUploader() = default;

void PollMediaUploader::subscribeToUploader() {
	auto &uploader = _session->uploader();

	uploader.photoReady(
	) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		const auto context = _uploads.take(data.fullId);
		if (!context) {
			return;
		}
		const auto media = context->media.lock();
		if (!media || (media->token != context->token)) {
			return;
		}
		applyUploaded(media, context->token, data.fullId, data.info.file);
	}, _lifetime);

	uploader.photoProgress(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto i = _uploads.find(id);
		if (i == _uploads.end()) {
			return;
		}
		const auto &context = i->second;
		const auto media = context.media.lock();
		if (!media
			|| (media->token != context.token)
			|| !media->uploadDataId) {
			return;
		}
		media->progress = _session->data().photo(
			media->uploadDataId)->progress();
		updateMedia(media);
	}, _lifetime);

	uploader.photoFailed(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto context = _uploads.take(id);
		if (!context) {
			return;
		}
		const auto media = context->media.lock();
		if (!media || (media->token != context->token)) {
			return;
		}
		setMedia(media, PollMedia(), nullptr, false);
		_showError(tr::lng_attach_failed(tr::now));
	}, _lifetime);

	uploader.documentReady(
	) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		const auto context = _uploads.take(data.fullId);
		if (!context) {
			return;
		}
		const auto media = context->media.lock();
		if (!media || (media->token != context->token)) {
			return;
		}
		applyUploadedDocument(
			media,
			context->token,
			data.fullId,
			data.info,
			*context);
	}, _lifetime);

	uploader.documentProgress(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto i = _uploads.find(id);
		if (i == _uploads.end()) {
			return;
		}
		const auto &context = i->second;
		const auto media = context.media.lock();
		if (!media
			|| (media->token != context.token)
			|| !media->uploadDataId) {
			return;
		}
		media->progress = _session->data().document(
			media->uploadDataId)->progress();
		updateMedia(media);
	}, _lifetime);

	uploader.documentFailed(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto context = _uploads.take(id);
		if (!context) {
			return;
		}
		const auto media = context->media.lock();
		if (!media || (media->token != context->token)) {
			return;
		}
		setMedia(media, PollMedia(), nullptr, false);
		_showError(tr::lng_attach_failed(tr::now));
	}, _lifetime);
}

void PollMediaUploader::updateMedia(
		const std::shared_ptr<PollMediaState> &media) {
	if (media->update) {
		media->update();
	}
}

void PollMediaUploader::setMedia(
		const std::shared_ptr<PollMediaState> &media,
		PollMedia value,
		std::shared_ptr<Ui::DynamicImage> thumbnail,
		bool rounded) {
	const auto wasUploading = media->uploading;
	media->token++;
	media->media = value;
	media->thumbnail = std::move(thumbnail);
	media->rounded = rounded;
	media->progress = (media->uploading && media->media)
		? 1.
		: 0.;
	media->uploadDataId = 0;
	media->uploading = false;
	if (wasUploading && value) {
		media->uploadedAt = crl::now();
	} else {
		media->uploadedAt = 0;
		media->reupload = nullptr;
	}
	updateMedia(media);
}

auto PollMediaUploader::parseUploaded(
		const MTPMessageMedia &result,
		FullMsgId fullId) -> UploadedMedia {
	auto parsed = UploadedMedia();
	auto &owner = _session->data();
	result.match([&](const MTPDmessageMediaPhoto &media) {
		if (const auto photo = media.vphoto()) {
			photo->match([&](const MTPDphoto &) {
				parsed.input.photo = owner.processPhoto(*photo);
				parsed.thumbnail = Ui::MakePhotoThumbnail(
					parsed.input.photo,
					fullId);
			}, [](const auto &) {
			});
		}
	}, [&](const MTPDmessageMediaDocument &media) {
		if (const auto document = media.vdocument()) {
			document->match([&](const MTPDdocument &) {
				parsed.input.document = owner.processDocument(
					*document);
				parsed.thumbnail
					= Ui::MakeDocumentFilePreviewThumbnail(
						parsed.input.document,
						fullId);
			}, [](const auto &) {
			});
		}
	}, [](const auto &) {
	});
	return parsed;
}

void PollMediaUploader::applyUploaded(
		const std::shared_ptr<PollMediaState> &media,
		uint64 token,
		FullMsgId fullId,
		const MTPInputFile &file) {
	const auto uploaded = MTP_inputMediaUploadedPhoto(
		MTP_flags(0),
		file,
		MTP_vector<MTPInputDocument>(QVector<MTPInputDocument>()),
		MTPint(),
		MTPInputDocument());
	_session->api().request(MTPmessages_UploadMedia(
		MTP_flags(0),
		MTPstring(),
		_peer->input(),
		uploaded
	)).done([=](const MTPMessageMedia &result) {
		if (media->token != token) {
			return;
		}
		auto parsed = parseUploaded(result, fullId);
		if (!parsed.input) {
			setMedia(media, PollMedia(), nullptr, false);
			_showError(tr::lng_attach_failed(tr::now));
			return;
		}
		setMedia(
			media,
			parsed.input,
			media->thumbnail
				? media->thumbnail
				: std::move(parsed.thumbnail),
			true);
	}).fail([=](const MTP::Error &) {
		if (media->token != token) {
			return;
		}
		setMedia(media, PollMedia(), nullptr, false);
		_showError(tr::lng_attach_failed(tr::now));
	}).send();
}

void PollMediaUploader::applyUploadedDocument(
		const std::shared_ptr<PollMediaState> &media,
		uint64 token,
		FullMsgId fullId,
		const Api::RemoteFileInfo &info,
		const UploadContext &context) {
	using Flag = MTPDinputMediaUploadedDocument::Flag;
	const auto flags = (context.forceFile ? Flag::f_force_file : Flag())
		| (info.thumb ? Flag::f_thumb : Flag());
	auto attributes = !context.attributes.isEmpty()
		? context.attributes
		: QVector<MTPDocumentAttribute>{
			MTP_documentAttributeFilename(
				MTP_string(context.filename)),
		};
	const auto uploaded = MTP_inputMediaUploadedDocument(
		MTP_flags(flags),
		info.file,
		info.thumb.value_or(MTPInputFile()),
		MTP_string(context.filemime),
		MTP_vector<MTPDocumentAttribute>(std::move(attributes)),
		MTP_vector<MTPInputDocument>(),
		MTPInputPhoto(),
		MTP_int(0),
		MTP_int(0));
	_session->api().request(MTPmessages_UploadMedia(
		MTP_flags(0),
		MTPstring(),
		_peer->input(),
		uploaded
	)).done([=](const MTPMessageMedia &result) {
		if (media->token != token) {
			return;
		}
		auto parsed = parseUploaded(result, fullId);
		if (!parsed.input) {
			setMedia(media, PollMedia(), nullptr, false);
			_showError(tr::lng_attach_failed(tr::now));
			return;
		}
		const auto isVideo = parsed.input.document
			&& parsed.input.document->isVideoFile();
		setMedia(
			media,
			parsed.input,
			isVideo
				? (media->thumbnail
					? media->thumbnail
					: std::move(parsed.thumbnail))
				: std::move(parsed.thumbnail),
			isVideo);
	}).fail([=](const MTP::Error &) {
		if (media->token != token) {
			return;
		}
		setMedia(media, PollMedia(), nullptr, false);
		_showError(tr::lng_attach_failed(tr::now));
	}).send();
}

void PollMediaUploader::startPhotoUpload(
		std::shared_ptr<PollMediaState> media,
		Ui::PreparedFile file) {
	const auto token = ++media->token;
	media->media = PollMedia();
	media->thumbnail = std::make_shared<LocalImageThumbnail>(
		std::move(file.preview));
	media->rounded = true;
	media->uploading = true;
	media->progress = 0.;
	media->uploadDataId = 0;
	updateMedia(media);
	_prepareQueue->addTask(std::make_unique<PreparePollMediaTask>(
		FileLoadTask::Args{
			.session = _session,
			.filepath = file.path,
			.content = file.content,
			.information = std::move(file.information),
			.videoCover = nullptr,
			.type = SendMediaType::Photo,
			.to = FileLoadTo(
				_peer->id,
				Api::SendOptions(),
				FullReplyTo(),
				MsgId()),
			.caption = TextWithTags(),
			.spoiler = false,
			.album = nullptr,
			.forceFile = false,
			.idOverride = 0,
			.displayName = file.displayName,
		},
		[=](std::shared_ptr<FilePrepareResult> prepared) {
			if ((media->token != token)
				|| !prepared
				|| (prepared->type != SendMediaType::Photo)) {
				if (media->token == token) {
					setMedia(media, PollMedia(), nullptr, false);
					_showError(tr::lng_attach_failed(tr::now));
				}
				return;
			}
			const auto uploadId = FullMsgId(
				_peer->id,
				_session->data().nextLocalMessageId());
			_uploads.emplace(uploadId, UploadContext{
				.media = media,
				.token = token,
			});
			media->uploadDataId = prepared->id;
			_session->uploader().upload(uploadId, prepared);
		}));
}

void PollMediaUploader::startDocumentUpload(
		std::shared_ptr<PollMediaState> media,
		Ui::PreparedFile file) {
	const auto displayName = file.displayName.isEmpty()
		? QFileInfo(file.path).fileName()
		: file.displayName;
	auto audioAttributes = ExtractAudioAttributes(file);
	const auto isAudio = !audioAttributes.isEmpty();
	const auto token = ++media->token;
	media->media = PollMedia();
	media->thumbnail = std::make_shared<LocalImageThumbnail>(
		GenerateDocumentFilePreview(
			displayName,
			st::pollAttach.rippleAreaSize));
	media->rounded = false;
	media->uploading = true;
	media->progress = 0.;
	media->uploadDataId = 0;
	updateMedia(media);
	_prepareQueue->addTask(std::make_unique<PreparePollMediaTask>(
		FileLoadTask::Args{
			.session = _session,
			.filepath = file.path,
			.content = file.content,
			.information = std::move(file.information),
			.videoCover = nullptr,
			.type = SendMediaType::File,
			.to = FileLoadTo(
				_peer->id,
				Api::SendOptions(),
				FullReplyTo(),
				MsgId()),
			.caption = TextWithTags(),
			.spoiler = false,
			.album = nullptr,
			.forceFile = !isAudio,
			.idOverride = 0,
			.displayName = displayName,
		},
		[=, attributes = std::move(audioAttributes)](
				std::shared_ptr<FilePrepareResult> prepared) {
			if ((media->token != token) || !prepared) {
				if (media->token == token) {
					setMedia(media, PollMedia(), nullptr, false);
					_showError(tr::lng_attach_failed(tr::now));
				}
				return;
			}
			const auto uploadId = FullMsgId(
				_peer->id,
				_session->data().nextLocalMessageId());
			_uploads.emplace(uploadId, UploadContext{
				.media = media,
				.token = token,
				.filename = prepared->filename,
				.filemime = prepared->filemime,
				.attributes = attributes,
				.forceFile = !isAudio,
			});
			media->uploadDataId = prepared->id;
			_session->uploader().upload(uploadId, prepared);
		}));
}

void PollMediaUploader::startVideoUpload(
		std::shared_ptr<PollMediaState> media,
		Ui::PreparedFile file) {
	const auto token = ++media->token;
	media->media = PollMedia();
	media->thumbnail = std::make_shared<LocalImageThumbnail>(
		std::move(file.preview));
	media->rounded = true;
	media->uploading = true;
	media->progress = 0.;
	media->uploadDataId = 0;
	updateMedia(media);
	_prepareQueue->addTask(std::make_unique<PreparePollMediaTask>(
		FileLoadTask::Args{
			.session = _session,
			.filepath = file.path,
			.content = file.content,
			.information = std::move(file.information),
			.videoCover = nullptr,
			.type = SendMediaType::File,
			.to = FileLoadTo(
				_peer->id,
				Api::SendOptions(),
				FullReplyTo(),
				MsgId()),
			.caption = TextWithTags(),
			.spoiler = false,
			.album = nullptr,
			.forceFile = false,
			.idOverride = 0,
			.displayName = file.displayName,
		},
		[=](std::shared_ptr<FilePrepareResult> prepared) {
			if ((media->token != token) || !prepared) {
				if (media->token == token) {
					setMedia(media, PollMedia(), nullptr, false);
					_showError(tr::lng_attach_failed(tr::now));
				}
				return;
			}
			auto attributes = QVector<MTPDocumentAttribute>();
			prepared->document.match([&](const MTPDdocument &data) {
				attributes = data.vattributes().v;
			}, [](const auto &) {
			});
			const auto uploadId = FullMsgId(
				_peer->id,
				_session->data().nextLocalMessageId());
			_uploads.emplace(uploadId, UploadContext{
				.media = media,
				.token = token,
				.filename = prepared->filename,
				.filemime = prepared->filemime,
				.attributes = std::move(attributes),
				.forceFile = false,
			});
			media->uploadDataId = prepared->id;
			_session->uploader().upload(uploadId, prepared);
		}));
}

bool PollMediaUploader::applyPreparedPhotoList(
		std::shared_ptr<PollMediaState> media,
		Ui::PreparedList &&list) {
	if (list.error != Ui::PreparedList::Error::None
		|| (list.files.size() != 1)
		|| (list.files.front().type != Ui::PreparedFile::Type::Photo)) {
		return false;
	}
	startPhotoUpload(media, std::move(list.files.front()));
	return true;
}

void PollMediaUploader::clearMedia(std::shared_ptr<PollMediaState> media) {
	auto toCancel = std::vector<FullMsgId>();
	for (auto i = _uploads.begin(); i != _uploads.end();) {
		if (i->second.media.lock() == media) {
			toCancel.push_back(i->first);
			i = _uploads.erase(i);
		} else {
			++i;
		}
	}
	for (const auto &id : toCancel) {
		_session->uploader().cancel(id);
	}
	setMedia(media, PollMedia(), nullptr, false);
}

void PollMediaUploader::choosePhotoOrVideo(
		not_null<QWidget*> parent,
		std::shared_ptr<PollMediaState> media) {
	const auto weak = QPointer<QWidget>(parent.get());
	const auto callback = crl::guard(parent.get(), [=](
			FileDialog::OpenResult &&result) {
		const auto checkResult = [&](const Ui::PreparedList &list) {
			using namespace Ui;
			if (list.files.size() != 1) {
				return false;
			}
			const auto type = list.files.front().type;
			return (type == PreparedFile::Type::Photo)
				|| (type == PreparedFile::Type::Video);
		};
		const auto showError = [=](tr::phrase<> text) {
			_showError(text(tr::now));
		};
		auto list = Storage::PreparedFileFromFilesDialog(
			std::move(result),
			checkResult,
			showError,
			st::sendMediaPreviewSize,
			_session->premium());
		if (!list) {
			return;
		}
		auto &file = list->files.front();
		if (file.type == Ui::PreparedFile::Type::Photo) {
			applyPreparedPhotoList(media, std::move(*list));
		} else {
			startVideoUpload(media, std::move(file));
		}
	});
	FileDialog::GetOpenPath(
		QPointer<QWidget>(parent.get()),
		tr::lng_attach_photo_or_video(tr::now),
		FileDialog::PhotoVideoFilesFilter(),
		callback);
}

void PollMediaUploader::chooseDocument(
		not_null<QWidget*> parent,
		std::shared_ptr<PollMediaState> media) {
	const auto callback = crl::guard(parent.get(), [=](
			FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty()) {
			return;
		}
		auto list = Storage::PrepareMediaList(
			result.paths.mid(0, 1),
			st::sendMediaPreviewSize,
			_session->premium());
		if (list.error != Ui::PreparedList::Error::None
			|| list.files.empty()) {
			return;
		}
		startDocumentUpload(
			media,
			std::move(list.files.front()));
	});
	FileDialog::GetOpenPath(
		QPointer<QWidget>(parent.get()),
		tr::lng_attach_file(tr::now),
		FileDialog::AllFilesFilter(),
		callback);
}

void PollMediaUploader::installDropToWidget(
		not_null<QWidget*> widget,
		std::shared_ptr<PollMediaState> media,
		bool allowDocuments) {
	widget->setAcceptDrops(true);
	base::install_event_filter(widget, [=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type != QEvent::DragEnter
			&& type != QEvent::DragMove
			&& type != QEvent::Drop) {
			return base::EventFilterResult::Continue;
		}
		const auto drop = static_cast<QDropEvent*>(event.get());
		const auto data = drop->mimeData();
		if (!data || !ValidateFileDragData(data)) {
			return base::EventFilterResult::Continue;
		}
		if (type == QEvent::Drop) {
			auto list = FileListFromMimeData(data, _session->premium());
			if (list.error != Ui::PreparedList::Error::None
				|| list.files.empty()) {
				return base::EventFilterResult::Continue;
			}
			auto &file = list.files.front();
			if (file.type == Ui::PreparedFile::Type::Photo) {
				startPhotoUpload(media, std::move(file));
			} else if (file.type == Ui::PreparedFile::Type::Video) {
				startVideoUpload(media, std::move(file));
			} else if (allowDocuments) {
				startDocumentUpload(media, std::move(file));
			} else {
				return base::EventFilterResult::Continue;
			}
		}
		drop->acceptProposedAction();
		return base::EventFilterResult::Cancel;
	});
}

void PollMediaUploader::installDropToField(
		not_null<Ui::InputField*> field,
		std::shared_ptr<PollMediaState> media,
		bool allowDocuments) {
	field->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return ValidateFileDragData(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			auto list = FileListFromMimeData(data, _session->premium());
			if (list.error != Ui::PreparedList::Error::None
				|| list.files.empty()) {
				return false;
			}
			auto &file = list.files.front();
			if (file.type == Ui::PreparedFile::Type::Photo) {
				startPhotoUpload(media, std::move(file));
				return true;
			} else if (file.type == Ui::PreparedFile::Type::Video) {
				startVideoUpload(media, std::move(file));
				return true;
			} else if (allowDocuments) {
				startDocumentUpload(media, std::move(file));
				return true;
			}
			return false;
		}
		Unexpected("Polls: action in MimeData hook.");
	});
}

} // namespace PollMediaUpload
