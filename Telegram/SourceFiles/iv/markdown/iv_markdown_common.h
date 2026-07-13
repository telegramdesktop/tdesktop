/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QVariant>

#include <rpl/never.h>
#include <rpl/producer.h>

#include <functional>
#include <memory>

#include "ui/text/text.h"
#include "webview/webview_common.h"

namespace Ui {
class DynamicImage;
class Show;
} // namespace Ui

namespace Iv {
class Delegate;
} // namespace Iv

namespace Iv::Markdown {

class MediaBlock;
class HostedMediaBlockFactory;
class PhotoRuntime {
public:
	virtual ~PhotoRuntime() = default;

	[[nodiscard]] virtual std::shared_ptr<Ui::DynamicImage> thumbnail(
		QSize size) const = 0;
	[[nodiscard]] virtual std::shared_ptr<Ui::DynamicImage> full(
		QSize size) const = 0;
	[[nodiscard]] virtual bool loaded() const = 0;
	[[nodiscard]] virtual bool loading() const = 0;
	[[nodiscard]] virtual double progress() const = 0;
	virtual void open(Qt::MouseButton button) const = 0;
};

class DocumentRuntime {
public:
	virtual ~DocumentRuntime() = default;

	[[nodiscard]] virtual std::shared_ptr<Ui::DynamicImage> thumbnail(
		QSize size) const = 0;
	[[nodiscard]] virtual std::shared_ptr<Ui::DynamicImage> full(
		QSize size) const = 0;
	[[nodiscard]] virtual bool loaded() const = 0;
	[[nodiscard]] virtual bool loading() const = 0;
	[[nodiscard]] virtual double progress() const = 0;
	virtual void open(Qt::MouseButton button) const = 0;
};

class MapRuntime {
public:
	virtual ~MapRuntime() = default;

	[[nodiscard]] virtual std::shared_ptr<Ui::DynamicImage> thumbnail(
		QSize size) const = 0;
	[[nodiscard]] virtual std::shared_ptr<Ui::DynamicImage> full(
		QSize size) const = 0;
	[[nodiscard]] virtual bool loaded() const = 0;
	[[nodiscard]] virtual bool loading() const = 0;
	[[nodiscard]] virtual double progress() const = 0;
};

class ChannelRuntime {
public:
	virtual ~ChannelRuntime() = default;

	[[nodiscard]] virtual bool joinVisible() const = 0;
	virtual void open(Qt::MouseButton button) const = 0;
	virtual void join(Qt::MouseButton button) const = 0;
};

struct PreparedMediaBlockId {
	uint64 value = 0;

	[[nodiscard]] explicit operator bool() const {
		return (value != 0);
	}
};

struct PreparedPlaceholderBlockId {
	uint64 value = 0;

	[[nodiscard]] explicit operator bool() const {
		return (value != 0);
	}
};

struct PreparedPhotoBlockData;
struct PreparedVideoBlockData;
struct PreparedAudioBlockData;
struct PreparedMapBlockData;
struct PreparedGroupedMediaBlockData;

class HostedMediaBlockFactory {
public:
	virtual ~HostedMediaBlockFactory() = default;

	[[nodiscard]] virtual std::shared_ptr<MediaBlock> createPhoto(
		const PreparedPhotoBlockData &prepared) const {
		return nullptr;
	}
	[[nodiscard]] virtual std::shared_ptr<MediaBlock> createVideo(
		const PreparedVideoBlockData &prepared) const {
		return nullptr;
	}
	[[nodiscard]] virtual std::shared_ptr<MediaBlock> createAudio(
		const PreparedAudioBlockData &prepared) const {
		return nullptr;
	}
	[[nodiscard]] virtual std::shared_ptr<MediaBlock> createMap(
		const PreparedMapBlockData &prepared) const {
		return nullptr;
	}
	[[nodiscard]] virtual std::shared_ptr<MediaBlock> createGroupedMedia(
		const PreparedGroupedMediaBlockData &prepared) const {
		return nullptr;
	}
};

class MediaRuntime {
public:
	virtual ~MediaRuntime() = default;

	[[nodiscard]] virtual Ui::Text::MarkedContext textContext() const;
	[[nodiscard]] virtual QString mentionNameEntityData(uint64 userId) const;
	[[nodiscard]] virtual std::shared_ptr<Ui::DynamicImage> resolveInlineImage(
		uint64 documentId,
		QSize size) const = 0;
	[[nodiscard]] virtual std::shared_ptr<PhotoRuntime> resolvePhoto(
		uint64 photoId) const = 0;
	[[nodiscard]] virtual std::shared_ptr<DocumentRuntime> resolveDocument(
		uint64 documentId) const;
	virtual void registerPhoto(
		uint64 photoId,
		TextWithEntities caption = {}) const;
	virtual void registerDocument(
		uint64 documentId,
		TextWithEntities caption = {}) const;
	[[nodiscard]] virtual std::shared_ptr<MapRuntime> resolveMap(
		double latitude,
		double longitude,
		uint64 accessHash,
		QSize size,
		int zoom) const;
	[[nodiscard]] virtual std::shared_ptr<ChannelRuntime> resolveChannel(
		uint64 channelId,
		const QString &username) const;
	[[nodiscard]] virtual rpl::producer<uint64> channelJoinedChanges() const;
	[[nodiscard]] virtual std::shared_ptr<HostedMediaBlockFactory>
	hostedMediaBlockFactory() const;
};

inline Ui::Text::MarkedContext MediaRuntime::textContext() const {
	return {};
}

inline QString MediaRuntime::mentionNameEntityData(uint64) const {
	return QString();
}

inline std::shared_ptr<DocumentRuntime> MediaRuntime::resolveDocument(
		uint64) const {
	return nullptr;
}

inline void MediaRuntime::registerPhoto(uint64, TextWithEntities) const {
}

inline void MediaRuntime::registerDocument(uint64, TextWithEntities) const {
}

inline std::shared_ptr<MapRuntime> MediaRuntime::resolveMap(
		double,
		double,
		uint64,
		QSize,
		int) const {
	return nullptr;
}

inline std::shared_ptr<ChannelRuntime> MediaRuntime::resolveChannel(
		uint64,
		const QString &) const {
	return nullptr;
}

inline rpl::producer<uint64> MediaRuntime::channelJoinedChanges() const {
	return rpl::never<uint64>();
}

inline auto MediaRuntime::hostedMediaBlockFactory() const
-> std::shared_ptr<HostedMediaBlockFactory> {
	return nullptr;
}

enum class MediaActivationKind {
	None,
	ExternalUrl,
	Embed,
	Photo,
	Document,
	OpenChannel,
	JoinChannel,
};

struct EmbedRequest {
	QByteArray html;
	QString url;
	int width = 0;
	int height = 0;
	bool fullWidth = false;
	bool fixedHeight = false;
	bool allowScrolling = false;

	[[nodiscard]] explicit operator bool() const {
		return !html.isEmpty() || !url.isEmpty();
	}
};

struct MediaActivation {
	MediaActivationKind kind = MediaActivationKind::None;
	int itemIndex = -1;
	QString url;
	EmbedRequest embed;
	PreparedPlaceholderBlockId placeholderId;
	std::shared_ptr<PhotoRuntime> photo;
	std::shared_ptr<DocumentRuntime> document;
	std::shared_ptr<ChannelRuntime> channel;
};

enum class ViewerKind {
	Auto,
	LocalFile,
	InstantView,
};

struct OpenOptions {
	QString sourceName;
	QString sourcePath;
	QString sourceUrl;
	QString initialFragment;
	uint64 currentPageId = 0;
	ViewerKind viewerKind = ViewerKind::Auto;
	Iv::Delegate *delegate = nullptr;
	QVariant clickHandlerContext;
	std::shared_ptr<QVariant> clickHandlerContextRef;
	std::function<void()> openSource;
	std::function<void(std::shared_ptr<Ui::Show>)> share;
	Webview::StorageId ivWebviewStorageId;
	std::function<bool(
		const MediaActivation &,
		Qt::MouseButton)> activateMedia;
	std::function<void()> zoomActivated;
	rpl::producer<> downloadTaskFinished;
};

struct ParseOptions {
	QString sourceName;
};

[[nodiscard]] bool LooksLikeMarkdownFile(
	const QString &fileName,
	const QString &mimeType = QString());

struct Event {
	enum class Type {
		Close,
		Quit,
		OpenPage,
		OpenFile,
		Report,
	};
	Type type = Type::Close;
	uint64 webpageId = 0;
	QString url;
	QVariant context;
};

} // namespace Iv::Markdown
