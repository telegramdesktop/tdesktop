/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Image;

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct FileOrigin;

class WallPaper {
public:
	explicit WallPaper(WallPaperId id);

	void setLocalImageAsThumbnail(std::shared_ptr<Image> image);

	[[nodiscard]] WallPaperId id() const;
	[[nodiscard]] std::optional<QColor> backgroundColor() const;
	[[nodiscard]] DocumentData *document() const;
	[[nodiscard]] Image *localThumbnail() const;
	[[nodiscard]] bool isPattern() const;
	[[nodiscard]] bool isDefault() const;
	[[nodiscard]] bool isCreator() const;
	[[nodiscard]] bool isDark() const;
	[[nodiscard]] bool isLocal() const;
	[[nodiscard]] bool isBlurred() const;
	[[nodiscard]] int patternIntensity() const;
	[[nodiscard]] bool hasShareUrl() const;
	[[nodiscard]] QString shareUrl(not_null<Main::Session*> session) const;

	void loadDocument() const;
	void loadDocumentThumbnail() const;
	[[nodiscard]] FileOrigin fileOrigin() const;

	[[nodiscard]] UserId ownerId() const;
	[[nodiscard]] MTPInputWallPaper mtpInput(
		not_null<Main::Session*> session) const;
	[[nodiscard]] MTPWallPaperSettings mtpSettings() const;

	[[nodiscard]] WallPaper withUrlParams(
		const QMap<QString, QString> &params) const;
	[[nodiscard]] WallPaper withBlurred(bool blurred) const;
	[[nodiscard]] WallPaper withPatternIntensity(int intensity) const;
	[[nodiscard]] WallPaper withBackgroundColor(QColor color) const;
	[[nodiscard]] WallPaper withParamsFrom(const WallPaper &other) const;
	[[nodiscard]] WallPaper withoutImageData() const;

	[[nodiscard]] static std::optional<WallPaper> Create(
		not_null<Main::Session*> session,
		const MTPWallPaper &data);
	[[nodiscard]] static std::optional<WallPaper> Create(
		not_null<Main::Session*> session,
		const MTPDwallPaper &data);

	[[nodiscard]] QByteArray serialize() const;
	[[nodiscard]] static std::optional<WallPaper> FromSerialized(
		const QByteArray &serialized);
	[[nodiscard]] static std::optional<WallPaper> FromLegacySerialized(
		quint64 id,
		quint64 accessHash,
		quint32 flags,
		QString slug);
	[[nodiscard]] static std::optional<WallPaper> FromLegacyId(
		qint32 legacyId);
	[[nodiscard]] static std::optional<WallPaper> FromColorSlug(
		const QString &slug);

private:
	static constexpr auto kDefaultIntensity = 40;

	WallPaperId _id = WallPaperId();
	uint64 _accessHash = 0;
	UserId _ownerId = 0;
	MTPDwallPaper::Flags _flags;
	QString _slug;

	MTPDwallPaperSettings::Flags _settings;
	std::optional<QColor> _backgroundColor;
	int _intensity = kDefaultIntensity;

	DocumentData *_document = nullptr;
	std::shared_ptr<Image> _thumbnail;

};

[[nodiscard]] WallPaper ThemeWallPaper();
[[nodiscard]] bool IsThemeWallPaper(const WallPaper &paper);
[[nodiscard]] WallPaper CustomWallPaper();
[[nodiscard]] bool IsCustomWallPaper(const WallPaper &paper);
[[nodiscard]] WallPaper Legacy1DefaultWallPaper();
[[nodiscard]] bool IsLegacy1DefaultWallPaper(const WallPaper &paper);
[[nodiscard]] bool IsLegacy2DefaultWallPaper(const WallPaper &paper);
[[nodiscard]] WallPaper DefaultWallPaper();
[[nodiscard]] bool IsDefaultWallPaper(const WallPaper &paper);
[[nodiscard]] bool IsCloudWallPaper(const WallPaper &paper);

QColor PatternColor(QColor background);
QImage PreparePatternImage(
	QImage image,
	QColor bg,
	QColor fg,
	int intensity);
QImage PrepareBlurredBackground(QImage image);

namespace details {

[[nodiscard]] WallPaper UninitializedWallPaper();
[[nodiscard]] bool IsUninitializedWallPaper(const WallPaper &paper);
[[nodiscard]] WallPaper TestingThemeWallPaper();
[[nodiscard]] bool IsTestingThemeWallPaper(const WallPaper &paper);
[[nodiscard]] WallPaper TestingDefaultWallPaper();
[[nodiscard]] bool IsTestingDefaultWallPaper(const WallPaper &paper);
[[nodiscard]] WallPaper TestingEditorWallPaper();
[[nodiscard]] bool IsTestingEditorWallPaper(const WallPaper &paper);

} // namespace details
} // namespace Data
