/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_poll.h"
#include "storage/localimageloader.h"
#include "ui/dynamic_image.h"
#include "ui/effects/radial_animation.h"
#include "ui/widgets/buttons.h"

struct FilePrepareResult;

namespace Api {
struct RemoteFileInfo;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Storage {
struct UploadedMedia;
} // namespace Storage

namespace Ui {
class InputField;
struct PreparedFile;
struct PreparedList;
} // namespace Ui

class PeerData;

namespace PollMediaUpload {

struct PollMediaState {
	PollMedia media;
	std::shared_ptr<Ui::DynamicImage> thumbnail;
	bool rounded = false;
	bool uploading = false;
	float64 progress = 0.;
	uint64 uploadDataId = 0;
	uint64 token = 0;
	crl::time uploadedAt = 0;
	Fn<void()> update;
	Fn<void()> reupload;
};

class LocalImageThumbnail final : public Ui::DynamicImage {
public:
	explicit LocalImageThumbnail(QImage original);

	std::shared_ptr<Ui::DynamicImage> clone() override;
	QImage image(int size) override;
	void subscribeToUpdates(Fn<void()> callback) override;

private:
	QImage _original;

};

[[nodiscard]] QImage GenerateDocumentFilePreview(
	const QString &filename,
	int size);

[[nodiscard]] QVector<MTPDocumentAttribute> ExtractAudioAttributes(
	const Ui::PreparedFile &file);

[[nodiscard]] bool ValidateFileDragData(not_null<const QMimeData*> data);

[[nodiscard]] Ui::PreparedList FileListFromMimeData(
	not_null<const QMimeData*> data,
	bool premium);

class PollMediaButton final : public Ui::RippleButton {
public:
	PollMediaButton(
		not_null<QWidget*> parent,
		const style::IconButton &st,
		std::shared_ptr<PollMediaState> state);
	~PollMediaButton() override;

	void setIconColorOverride(std::optional<QColor> colorOverride);
	void setRippleColorOverride(std::optional<QColor> colorOverride);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void paintCover(
		Painter &p,
		QRect target,
		QImage image,
		bool rounded) const;
	void radialAnimationCallback(crl::time now);
	[[nodiscard]] QRect rippleRect() const;
	[[nodiscard]] QRect iconRect() const;
	std::shared_ptr<Ui::DynamicImage> currentAttachThumbnail() const;
	void updateMediaSubscription();

	const style::IconButton &_st;
	const std::shared_ptr<PollMediaState> _state;
	const std::shared_ptr<Ui::DynamicImage> _attach;
	const std::shared_ptr<Ui::DynamicImage> _attachOver;
	std::shared_ptr<Ui::DynamicImage> _subscribed;
	Ui::RadialAnimation _radial;
	Ui::Animations::Simple _cancelShown;
	Ui::Animations::Simple _viewShown;
	std::optional<QColor> _iconColorOverride;
	std::optional<QColor> _rippleColorOverride;

};

class PreparePollMediaTask final : public Task {
public:
	PreparePollMediaTask(
		FileLoadTask::Args &&args,
		Fn<void(std::shared_ptr<FilePrepareResult>)> done);

	void process() override;
	void finish() override;

private:
	FileLoadTask _task;
	Fn<void(std::shared_ptr<FilePrepareResult>)> _done;

};

struct UploadContext {
	std::weak_ptr<PollMediaState> media;
	uint64 token = 0;
	QString filename;
	QString filemime;
	QVector<MTPDocumentAttribute> attributes;
	bool forceFile = true;
};

class PollMediaUploader final {
public:
	struct Args {
		not_null<Main::Session*> session;
		not_null<PeerData*> peer;
		Fn<void(const QString&)> showError;
	};

	explicit PollMediaUploader(Args &&args);
	~PollMediaUploader();

	void startPhotoUpload(
		std::shared_ptr<PollMediaState> media,
		Ui::PreparedFile file);
	void startVideoUpload(
		std::shared_ptr<PollMediaState> media,
		Ui::PreparedFile file);
	void startDocumentUpload(
		std::shared_ptr<PollMediaState> media,
		Ui::PreparedFile file);
	bool applyPreparedPhotoList(
		std::shared_ptr<PollMediaState> media,
		Ui::PreparedList &&list);

	void setMedia(
		const std::shared_ptr<PollMediaState> &media,
		PollMedia value,
		std::shared_ptr<Ui::DynamicImage> thumbnail,
		bool rounded);
	void clearMedia(std::shared_ptr<PollMediaState> media);

	void choosePhotoOrVideo(
		not_null<QWidget*> parent,
		std::shared_ptr<PollMediaState> media);
	void chooseDocument(
		not_null<QWidget*> parent,
		std::shared_ptr<PollMediaState> media);

	void installDropToWidget(
		not_null<QWidget*> widget,
		std::shared_ptr<PollMediaState> media,
		bool allowDocuments);
	void installDropToField(
		not_null<Ui::InputField*> field,
		std::shared_ptr<PollMediaState> media,
		bool allowDocuments);

private:
	struct UploadedMedia {
		PollMedia input;
		std::shared_ptr<Ui::DynamicImage> thumbnail;
	};

	void updateMedia(const std::shared_ptr<PollMediaState> &media);
	[[nodiscard]] UploadedMedia parseUploaded(
		const MTPMessageMedia &result,
		FullMsgId fullId);
	void applyUploaded(
		const std::shared_ptr<PollMediaState> &media,
		uint64 token,
		FullMsgId fullId,
		const MTPInputFile &file);
	void applyUploadedDocument(
		const std::shared_ptr<PollMediaState> &media,
		uint64 token,
		FullMsgId fullId,
		const Api::RemoteFileInfo &info,
		const UploadContext &context);
	void subscribeToUploader();

	const not_null<Main::Session*> _session;
	const not_null<PeerData*> _peer;
	const Fn<void(const QString&)> _showError;
	std::unique_ptr<TaskQueue> _prepareQueue;
	base::flat_map<FullMsgId, UploadContext> _uploads;
	rpl::lifetime _lifetime;

};

} // namespace PollMediaUpload
