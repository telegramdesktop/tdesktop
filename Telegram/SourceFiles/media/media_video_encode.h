/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media::Encode {

struct AnimatedEntity {
	enum class Kind {
		Lottie,
		Webm,
	};

	// Frames show through the hole cut by mask in the picture at geometry.
	struct Cutout {
		QImage picture;
		QImage mask;
		QRect hole;
		QRect frames;
	};

	Kind kind = Kind::Lottie;
	QByteArray bytes;
	QRectF geometry;
	float64 rotation = 0.;
	bool flipped = false;
	std::optional<Cutout> cutout;

	crl::time from = 0;
	crl::time till = 0;
};

using Layer = std::variant<QImage, AnimatedEntity>;

// The part [from, till) of the file plays from the result position.
struct MusicTrack {
	QString path;
	QByteArray bytes;
	crl::time position = 0;
	crl::time from = 0;
	// Zero means the end of the music.
	crl::time till = 0;
	float64 volume = 1.;
	// Repeats the part till the result ends, a shorter music
	// is padded with silence to keep the part length.
	bool loop = false;

	[[nodiscard]] bool empty() const {
		return path.isEmpty() && bytes.isEmpty();
	}
};

struct StillSource {
	QImage base;
	crl::time duration = 0;
	float64 fps = 30.;
	std::vector<MusicTrack> music;
};

struct VideoSource {
	enum class Mode {
		Mp4,
		WebmSticker,
	};

	QString path;
	QByteArray bytes;

	// Output size: exactSize wins, otherwise the shorter side is capped.
	int targetShorterSide = 0;
	QSize exactSize;

	// Applied like Editor::ImageModified: display matrix, then crop in those
	// pixels, then flip, then rotation.
	QRect crop;
	int rotation = 0;
	bool flipped = false;

	crl::time from = 0;
	crl::time till = 0;

	Mode mode = Mode::Mp4;
	std::optional<int> webmCrf;

	bool removeAudio = false;

	bool silentAudio = false;
	float64 fpsLimit = 0.;

	crl::time coverPosition = -1;

	std::vector<MusicTrack> music;
};

struct Job {
	std::variant<VideoSource, StillSource> source;
	std::vector<Layer> overlay;
	std::vector<uint64> attachedStickerIds;
	int bitrate = 0;
	bool silentLoop = false;
};

struct Result {
	QByteArray bytes;
	QSize dimensions;
	crl::time duration = 0;

	[[nodiscard]] bool empty() const {
		return bytes.isEmpty();
	}
};

struct SourceInfo {
	QSize coded;
	QSize display;
	int rotation = 0;
	crl::time duration = 0;
	float64 fps = 0.;
	int64 fileSize = 0;
	int64 videoBitrate = 0;
	int64 audioBitrate = 0;
	int audioChannels = 0;
	bool videoRemuxable = false;
	bool audioRemuxable = false;
	bool hasAudio = false;

	[[nodiscard]] bool empty() const {
		return coded.isEmpty();
	}
};

struct TranscodeResult {
	QString path;
	QImage cover;
	QSize dimensions;
	crl::time duration = 0;
	crl::time coverOffset = 0;

	[[nodiscard]] bool empty() const {
		return path.isEmpty();
	}
};

[[nodiscard]] Result Run(Job &&job, Fn<bool(float64)> progress = nullptr);

// Retries with harder quality ceilings until the result fits maxBytes.
[[nodiscard]] Result RunWebmSticker(
	VideoSource source,
	int64 maxBytes,
	Fn<bool(float64)> progress = nullptr);

// Writes a temporary mp4 the caller owns and must remove.
[[nodiscard]] TranscodeResult TranscodeVideo(
	const VideoSource &source,
	Fn<bool(float64)> progress = nullptr);

[[nodiscard]] SourceInfo ProbeSource(const QString &path);

// Zero when nothing can be guessed.
[[nodiscard]] int64 EstimateTranscodedSize(
	const VideoSource &source,
	const SourceInfo &info);

[[nodiscard]] int64 MaxTranscodeSourceSize();

[[nodiscard]] QSize DownscaledSize(QSize original, int targetShorterSide);

[[nodiscard]] QSize TranscodedSize(
	const VideoSource &source,
	QSize displaySize);

[[nodiscard]] crl::time TranscodedDuration(
	const VideoSource &source,
	crl::time duration);

void ClearStaleTempFiles();

[[nodiscard]] QString TempFileTemplate(const QString &extension);

[[nodiscard]] const QImage &ComposeCutout(
	const AnimatedEntity::Cutout &cutout,
	const QImage &frame,
	QImage &composite);

} // namespace Media::Encode
