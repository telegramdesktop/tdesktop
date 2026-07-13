/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_common.h"

#include "ui/click_handler.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"

#include <memory>
#include <vector>

#include <QtCore/QPoint>
#include <QtCore/QRect>

namespace style {
struct Markdown;
} // namespace style

namespace Iv::Markdown {

struct MarkdownArticlePaintContext;
class MediaRuntime;
struct PreparedAudioBlockData;
struct PreparedChannelBlockData;
struct PreparedGroupedMediaBlockData;
struct PreparedMapBlockData;
struct PreparedPhotoBlockData;
struct PreparedVideoBlockData;

class MediaBlockHost {
public:
	virtual ~MediaBlockHost() = default;

	virtual void requestRepaint(QRect articleRect) = 0;
	virtual void requestRelayout(QRect articleRect) = 0;
};

struct MediaBlockSelectionData {
	QString copyText;
	TextWithEntities caption;
};

class MediaBlock : public std::enable_shared_from_this<MediaBlock> {
public:
	virtual ~MediaBlock();

	void setHost(MediaBlockHost *host);
	[[nodiscard]] MediaBlockHost *host() const;
	void setMediaPixelScale(double scale);

	[[nodiscard]] virtual uint64 stableId() const = 0;
	[[nodiscard]] virtual bool alive() const;
	[[nodiscard]] virtual int resizeGetHeight(int width) = 0;
	virtual void setGeometry(QRect geometry) = 0;
	[[nodiscard]] virtual QRect geometry() const = 0;
	[[nodiscard]] virtual int firstLineBaseline() const = 0;
	void setLayoutStyle(const style::Markdown &st);
	virtual void paint(
		Painter &p,
		const MarkdownArticlePaintContext &context) const = 0;
	[[nodiscard]] virtual ClickHandlerPtr linkAt(QPoint point) const = 0;
	[[nodiscard]] virtual MediaActivation activationAt(QPoint point) const = 0;
	[[nodiscard]] virtual MediaBlockSelectionData selectionData() const = 0;
	[[nodiscard]] virtual bool hasHeavyPart() const;
	virtual void unloadHeavyPart();
	virtual void hideSpoilers();
	[[nodiscard]] virtual std::vector<QRect> itemRects() const {
		return {};
	}
	[[nodiscard]] virtual int activeItemIndex() const {
		return -1;
	}
	virtual void setActiveItemIndex(int index) {
	}

protected:
	void requestRepaint(QRect articleRect) const;
	void requestRelayout(QRect articleRect) const;
	[[nodiscard]] const style::Markdown &layoutStyle() const;
	[[nodiscard]] double mediaPixelScale() const;
	virtual void layoutStyleUpdated();
	virtual void mediaPixelScaleUpdated();
	virtual void hostUpdated();

private:
	MediaBlockHost *_host = nullptr;
	const style::Markdown *_st = nullptr;
	double _mediaPixelScale = 1.;

};

[[nodiscard]] std::shared_ptr<MediaBlock> CreatePhotoMediaBlock(
	const PreparedPhotoBlockData &prepared,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st);
[[nodiscard]] std::shared_ptr<MediaBlock> CreateVideoMediaBlock(
	const PreparedVideoBlockData &prepared,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st);
[[nodiscard]] std::shared_ptr<MediaBlock> CreateAudioMediaBlock(
	const PreparedAudioBlockData &prepared,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st);
[[nodiscard]] std::shared_ptr<MediaBlock> CreateMapMediaBlock(
	const PreparedMapBlockData &prepared,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st);
[[nodiscard]] std::shared_ptr<MediaBlock> CreateChannelMediaBlock(
	const PreparedChannelBlockData &prepared,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st);
[[nodiscard]] std::shared_ptr<MediaBlock> CreateGroupedMediaBlock(
	const PreparedGroupedMediaBlockData &prepared,
	const std::shared_ptr<MediaRuntime> &mediaRuntime,
	const style::Markdown &st);

} // namespace Iv::Markdown
