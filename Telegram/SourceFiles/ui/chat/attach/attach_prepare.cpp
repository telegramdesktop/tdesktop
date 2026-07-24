/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_prepare.h"

#include "ui/rp_widget.h"
#include "ui/widgets/popup_menu.h"

#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "core/mime_type.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_media_player.h"

#include <QFileInfo>

namespace Ui {
namespace {

constexpr auto kMaxAlbumCount = 10;
constexpr auto kStandardPhotoSideLimit = 1280;

struct GroupRange {
	int from = 0;
	int till = 0;
	AlbumType type = AlbumType::None;

	[[nodiscard]] int size() const {
		return till - from;
	}
};

struct HighQualityBadgeCache {
	QRgb bg = 0;
	QRgb fg = 0;
	qreal ratio = 0.;
	int width = 0;
	int height = 0;
	QImage image;
};

[[nodiscard]] const QImage &HighQualityBadgeImage(
		const style::ComposeControls &st) {
	static auto cache = HighQualityBadgeCache();
	const auto text = u"HD"_q;
	const auto &font = st::mediaPlayerSpeedButton.font;
	const auto xpadding = style::ConvertScale(2.);
	const auto ypadding = 0;
	const auto stroke = style::ConvertScaleExact(1.);
	const auto width = font->width(text);
	const auto height = font->height;
	const auto ratio = style::DevicePixelRatio();
	const auto bg = st::roundedBg->c.rgba();
	const auto fg = st::roundedFg->c.rgba();
	if (cache.image.isNull()
		|| (cache.bg != bg)
		|| (cache.fg != fg)
		|| (cache.ratio != ratio)
		|| (cache.width != width)
		|| (cache.height != height)) {
		cache.bg = bg;
		cache.fg = fg;
		cache.ratio = ratio;
		cache.width = width;
		cache.height = height;
		cache.image = QImage(
			(width + 2 * xpadding + stroke) * ratio,
			(height + 2 * ypadding + stroke) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		cache.image.setDevicePixelRatio(ratio);
		cache.image.fill(Qt::transparent);
		auto painter = QPainter(&cache.image);
		auto hq = PainterHighQualityEnabler(painter);
		painter.setCompositionMode(QPainter::CompositionMode_Source);
		painter.setPen(QPen(Qt::transparent, stroke));
		painter.setBrush(st::roundedBg);
		painter.setFont(font);
		painter.drawRoundedRect(
			QRectF(
				0,
				0,
				width + 2 * xpadding + stroke,
				height + 2 * ypadding + stroke),
			height / 3.,
			height / 3.);
		painter.setPen(st::roundedFg);
		painter.drawText(
			QPointF(
				xpadding + stroke / 2.,
				ypadding + font->ascent + stroke / 2.),
			text);
	}
	return cache.image;
}

[[nodiscard]] AlbumType GroupTypeForFile(
		PreparedFile::Type type,
		bool groupFiles,
		bool sendImagesAsPhotos) {
	using Type = PreparedFile::Type;
	return (type == Type::Music)
		? (groupFiles ? AlbumType::Music : AlbumType::None)
		: (type == Type::Video || type == Type::Photo)
		? ((groupFiles && sendImagesAsPhotos)
			? AlbumType::PhotoVideo
			: (groupFiles && !sendImagesAsPhotos)
			? AlbumType::File
			: AlbumType::None)
		: (type == Type::File)
		? (groupFiles ? AlbumType::File : AlbumType::None)
		: AlbumType::None;
}

[[nodiscard]] std::vector<GroupRange> GroupRanges(
		const std::vector<PreparedFile> &files,
		SendFilesWay way,
		bool slowmode) {
	const auto sendImagesAsPhotos = way.sendImagesAsPhotos();
	const auto groupFiles = way.groupFiles() || slowmode;

	auto result = std::vector<GroupRange>();
	if (files.empty()) {
		return result;
	}
	auto from = 0;
	auto groupType = AlbumType::None;
	for (auto i = 0; i != int(files.size()); ++i) {
		const auto fileGroupType = GroupTypeForFile(
			files[i].type,
			groupFiles,
			sendImagesAsPhotos);
		const auto count = (i - from);
		if ((i > from && groupType != fileGroupType)
			|| ((groupType != AlbumType::None) && (count == kMaxAlbumCount))) {
			result.push_back(GroupRange{
				.from = from,
				.till = i,
				.type = (count > 1) ? groupType : AlbumType::None,
			});
			from = i;
		}
		groupType = fileGroupType;
	}
	const auto till = int(files.size());
	const auto count = (till - from);
	result.push_back(GroupRange{
		.from = from,
		.till = till,
		.type = (count > 1) ? groupType : AlbumType::None,
	});
	return result;
}

} // namespace

PreparedFile::PreparedFile(const QString &path) : path(path) {
	const auto fileInfo = QFileInfo(path);
	displayName = fileInfo.fileName();
}

PreparedFile::PreparedFile(PreparedFile &&other) = default;

PreparedFile &PreparedFile::operator=(PreparedFile &&other) = default;

PreparedFile::~PreparedFile() = default;

bool PreparedFile::canBeInAlbumType(AlbumType album) const {
	return CanBeInAlbumType(type, album);
}

bool PreparedFile::isSticker() const {
	Expects(information != nullptr);

	return (type == PreparedFile::Type::Photo)
		&& Core::IsMimeSticker(information->filemime);
}

bool PreparedFile::isVideoFile() const {
	Expects(information != nullptr);

	using Video = Ui::PreparedFileInformation::Video;
	return (type == PreparedFile::Type::Video)
		&& v::is<Video>(information->media)
		&& !v::get<Video>(information->media).isGifv;
}

bool PreparedFile::isGifv() const {
	Expects(information != nullptr);

	using Video = Ui::PreparedFileInformation::Video;
	return (type == PreparedFile::Type::Video)
		&& v::is<Video>(information->media)
		&& v::get<Video>(information->media).isGifv;
}

bool PreparedFile::canUseHighQualityPhoto() const {
	return (type == PreparedFile::Type::Photo)
		&& (information != nullptr)
		&& !isSticker()
		&& ((originalDimensions.width() > kStandardPhotoSideLimit)
			|| (originalDimensions.height() > kStandardPhotoSideLimit));
}

AlbumType PreparedFile::albumType(bool sendImagesAsPhotos) const {
	switch (type) {
	case Type::Photo:
		return sendImagesAsPhotos ? AlbumType::PhotoVideo : AlbumType::File;
	case Type::Video:
		return AlbumType::PhotoVideo;
	case Type::Music:
		return AlbumType::Music;
	case Type::File:
		return AlbumType::File;
	case Type::None:
		return AlbumType::None;
	}
	Unexpected("PreparedFile::type in PreparedFile::albumType().");
}

bool CanBeInAlbumType(PreparedFile::Type type, AlbumType album) {
	Expects(album != AlbumType::None);

	using Type = PreparedFile::Type;
	switch (album) {
	case AlbumType::PhotoVideo:
		return (type == Type::Photo) || (type == Type::Video);
	case AlbumType::Music:
		return (type == Type::Music);
	case AlbumType::File:
		return (type == Type::Photo)
			|| (type == Type::Video)
			|| (type == Type::File);
	}
	Unexpected("AlbumType in CanBeInAlbumType.");
}

bool InsertTextOnImageCancel(const QString &text) {
	return !text.isEmpty() && !text.startsWith(u"data:image"_q);
}

PreparedList PreparedList::Reordered(
		PreparedList &&list,
		std::vector<int> order) {
	Expects(list.error == PreparedList::Error::None);
	Expects(list.files.size() == order.size());

	auto result = PreparedList(list.error, list.errorData);
	result.files.reserve(list.files.size());
	for (auto index : order) {
		result.files.push_back(std::move(list.files[index]));
	}
	return result;
}

void PreparedList::mergeToEnd(PreparedList &&other, bool cutToAlbumSize) {
	if (error != Error::None) {
		return;
	}
	if (other.error != Error::None) {
		error = other.error;
		errorData = other.errorData;
		return;
	}
	files.reserve(std::min(
		size_t(cutToAlbumSize ? kMaxAlbumCount : INT_MAX),
		files.size() + other.files.size()));
	for (auto &file : other.files) {
		if (cutToAlbumSize && files.size() == kMaxAlbumCount) {
			break;
		}
		files.push_back(std::move(file));
	}
}

bool PreparedList::canBeSentInSlowmode() const {
	return canBeSentInSlowmodeWith(PreparedList());
}

bool PreparedList::canBeSentInSlowmodeWith(const PreparedList &other) const {
	if (!filesToProcess.empty() || !other.filesToProcess.empty()) {
		return false;
	} else if (files.size() + other.files.size() < 2) {
		return true;
	} else if (files.size() + other.files.size() > kMaxAlbumCount) {
		return false;
	}

	using Type = PreparedFile::Type;
	auto &&all = ranges::views::concat(files, other.files);
	const auto has = [&](Type type) {
		return ranges::contains(all, type, &PreparedFile::type);
	};
	const auto hasNonGrouping = has(Type::None);
	const auto hasPhotos = has(Type::Photo);
	const auto hasFiles = has(Type::File);
	const auto hasVideos = has(Type::Video);
	const auto hasMusic = has(Type::Music);

	// File-s and Video-s never can be grouped.
	// Music-s can be grouped only with themselves.
	if (hasNonGrouping) {
		return false;
	} else if (hasFiles) {
		return !hasMusic && !hasVideos;
	} else if (hasVideos) {
		return !hasMusic && !hasFiles;
	} else if (hasMusic) {
		return !hasVideos && !hasFiles && !hasPhotos;
	}
	return !hasNonGrouping && (!hasFiles || !hasVideos);
}

bool PreparedList::canAddCaption(bool compress) const {
	if (files.empty()) {
		return false;
	}
	const auto &last = files.back();
	const auto isSticker = last.path.endsWith(u".tgs"_q, Qt::CaseInsensitive)
		|| (!compress
			&& last.information
			&& Core::IsMimeSticker(last.information->filemime));
	return !isSticker;
}

bool PreparedList::canMoveCaption(bool sendingAlbum, bool compress) const {
	if (!canAddCaption(compress)) {
		return false;
	} else if (files.size() > kMaxAlbumCount) {
		return false;
	} else if (!sendingAlbum || !compress) {
		return (files.size() == 1);
	}
	for (const auto &file : files) {
		if (file.type != PreparedFile::Type::Photo
			&& file.type != PreparedFile::Type::Video) {
			return false;
		}
	}
	return true;
}

bool PreparedList::canChangePrice(bool sendingAlbum, bool compress) const {
	return canMoveCaption(sendingAlbum, compress);
}

bool PreparedList::hasGroupOption(bool slowmode) const {
	if (slowmode || files.size() < 2) {
		return false;
	}
	using Type = PreparedFile::Type;
	auto lastType = Type::None;
	for (const auto &file : files) {
		if ((file.type == lastType)
			|| (file.type == Type::Video && lastType == Type::Photo)
			|| (file.type == Type::Photo && lastType == Type::Video)
			|| (file.type == Type::File && lastType == Type::Photo)
			|| (file.type == Type::Photo && lastType == Type::File)) {
			if (lastType != Type::None) {
				return true;
			}
		}
		lastType = file.type;
	}
	return false;
}

bool PreparedList::hasSendImagesAsPhotosOption(bool slowmode) const {
	using Type = PreparedFile::Type;
	if (slowmode) {
		const auto t = files.front().type;
		return (files.size() == 1)
			&& (t == Type::Photo || t == Type::Video);
	}
	return ranges::contains(files, Type::Photo, &PreparedFile::type)
		|| ranges::contains(files, Type::Video, &PreparedFile::type);
}

bool PreparedList::canHaveEditorHintLabel() const {
	for (const auto &file : files) {
		if ((file.type == PreparedFile::Type::Photo)
			&& !Core::IsMimeSticker(file.information->filemime)) {
			return true;
		}
	}
	return false;
}

bool PreparedList::hasSticker() const {
	return ranges::any_of(files, &PreparedFile::isSticker);
}

bool PreparedList::hasSpoilerMenu(bool compress) const {
	const auto allAreVideo = !ranges::any_of(files, [](const auto &f) {
		using Type = Ui::PreparedFile::Type;
		return (f.type != Type::Video);
	});
	const auto allAreMedia = !ranges::any_of(files, [](const auto &f) {
		using Type = Ui::PreparedFile::Type;
		return (f.type != Type::Photo) && (f.type != Type::Video);
	});
	return allAreVideo || (allAreMedia && compress);
}

bool PreparedList::hasSendLargePhotosOption(bool compress) const {
	return compress
		&& ranges::any_of(files, &PreparedFile::canUseHighQualityPhoto);
}

std::shared_ptr<PreparedBundle> PrepareFilesBundle(
		std::vector<PreparedGroup> groups,
		SendFilesWay way,
		bool ctrlShiftEnter) {
	auto totalCount = 0;
	for (const auto &group : groups) {
		totalCount += group.list.files.size();
	}
	return std::make_shared<PreparedBundle>(PreparedBundle{
		.groups = std::move(groups),
		.way = way,
		.totalCount = totalCount,
		.ctrlShiftEnter = ctrlShiftEnter,
	});
}

int MaxAlbumItems() {
	return kMaxAlbumCount;
}

bool ValidateThumbDimensions(int width, int height) {
	return (width > 0)
		&& (height > 0)
		&& (width <= 20 * height)
		&& (height <= 20 * width);
}

std::vector<PreparedGroup> DivideByGroups(
		PreparedList &&list,
		SendFilesWay way,
		bool slowmode) {
	const auto ranges = GroupRanges(list.files, way, slowmode);
	auto result = std::vector<PreparedGroup>();
	result.reserve(ranges.size());
	for (const auto &range : ranges) {
		auto grouped = Ui::PreparedList();
		grouped.files.reserve(range.size());
		for (auto i = range.from; i != range.till; ++i) {
			grouped.files.push_back(std::move(list.files[i]));
		}
		result.push_back(PreparedGroup{
			.list = std::move(grouped),
			.type = range.type,
		});
	}
	return result;
}

QPixmap PrepareSongCoverForThumbnail(QImage image, int size) {
	const auto scaledSize = image.size().scaled(
		size,
		size,
		Qt::KeepAspectRatioByExpanding);
	using Option = Images::Option;
	const auto ratio = style::DevicePixelRatio();
	return PixmapFromImage(Images::Prepare(
		std::move(image),
		scaledSize * ratio,
		{
			.colored = &st::songCoverOverlayFg,
			.options = Option::RoundCircle,
			.outer = { size, size },
		}));
}

QPixmap BlurredPreviewFromPixmap(QPixmap pixmap, RectParts corners) {
	const auto image = pixmap.toImage();
	const auto skip = st::roundRadiusLarge * image.devicePixelRatio();
	auto small = image.copy(
		skip,
		skip,
		image.width() - 2 * skip,
		image.height() - 2 * skip
	).scaled(
		40,
		40,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);

	using namespace Images;
	return PixmapFromImage(Prepare(
		Blur(std::move(small), true),
		image.size(),
		{ .options = RoundOptions(ImageRoundRadius::Large, corners) }));
}

void PaintHighQualityBadge(
		QPainter &p,
		const style::ComposeControls &st,
		QRect rect,
		RectPart origin) {
	const auto outerSkip = st.photoQualityBadgeOuterSkip;
	const auto &badge = HighQualityBadgeImage(st);
	const auto size = badge.size() / badge.devicePixelRatio();
	const auto left = (origin == RectPart::TopLeft)
		|| (origin == RectPart::BottomLeft);
	const auto top = (origin == RectPart::TopLeft)
		|| (origin == RectPart::TopRight);
	const auto x = left
		? (rect.x() + outerSkip)
		: (rect.x() + rect.width() - size.width() - outerSkip);
	const auto y = top
		? (rect.y() + outerSkip)
		: (rect.y() + rect.height() - size.height() - outerSkip);
	p.drawImage(QPointF(x, y), badge);
}

} // namespace Ui
