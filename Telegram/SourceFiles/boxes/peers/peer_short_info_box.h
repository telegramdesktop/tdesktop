/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Media::Streaming {
class Document;
class Instance;
struct Update;
enum class Error;
struct Information;
} // namespace Media::Streaming

namespace Ui {
class VerticalLayout;
class RpWidget;
} // namespace Ui

enum class PeerShortInfoType {
	User,
	Group,
	Channel,
};

struct PeerShortInfoFields {
	QString name;
	QString phone;
	QString link;
	TextWithEntities about;
	QString username;
	bool isBio = false;
};

struct PeerShortInfoUserpic {
	int index = 0;
	int count = 0;

	QImage photo;
	float64 photoLoadingProgress = 0.;
	std::shared_ptr<Media::Streaming::Document> videoDocument;
	crl::time videoStartPosition = 0;
};

class PeerShortInfoBox final : public Ui::BoxContent {
public:
	PeerShortInfoBox(
		QWidget*,
		PeerShortInfoType type,
		rpl::producer<PeerShortInfoFields> fields,
		rpl::producer<QString> status,
		rpl::producer<PeerShortInfoUserpic> userpic,
		Fn<bool()> videoPaused);
	~PeerShortInfoBox();

	[[nodiscard]] rpl::producer<> openRequests() const;
	[[nodiscard]] rpl::producer<int> moveRequests() const;

private:
	struct CustomLabelStyle;

	void prepare() override;
	void prepareRows();
	RectParts customCornersFilling() override;

	void resizeEvent(QResizeEvent *e) override;

	void paintCover(QPainter &p);
	void paintCoverImage(QPainter &p, const QImage &image);
	void paintBars(QPainter &p);
	void paintShadow(QPainter &p);

	void refreshRoundedTopImage(const QColor &color);
	int fillRoundedTopHeight();

	[[nodiscard]] QImage currentVideoFrame() const;

	[[nodiscard]] rpl::producer<QString> nameValue() const;
	[[nodiscard]] rpl::producer<TextWithEntities> linkValue() const;
	[[nodiscard]] rpl::producer<QString> phoneValue() const;
	[[nodiscard]] rpl::producer<QString> usernameValue() const;
	[[nodiscard]] rpl::producer<TextWithEntities> aboutValue() const;
	void applyUserpic(PeerShortInfoUserpic &&value);
	QRect radialRect() const;

	void videoWaiting();
	void checkStreamedIsStarted();
	void handleStreamingUpdate(Media::Streaming::Update &&update);
	void handleStreamingError(Media::Streaming::Error &&error);
	void streamingReady(Media::Streaming::Information &&info);

	void refreshCoverCursor();
	void refreshBarImages();

	const PeerShortInfoType _type = PeerShortInfoType::User;

	rpl::variable<PeerShortInfoFields> _fields;

	object_ptr<Ui::RpWidget> _topRoundBackground;
	object_ptr<Ui::ScrollArea> _scroll;
	not_null<Ui::VerticalLayout*> _rows;
	not_null<Ui::RpWidget*> _cover;
	std::unique_ptr<CustomLabelStyle> _nameStyle;
	object_ptr<Ui::FlatLabel> _name;
	std::unique_ptr<CustomLabelStyle> _statusStyle;
	object_ptr<Ui::FlatLabel> _status;

	QImage _userpicImage;
	QImage _roundedTopImage;
	QColor _roundedTopColor;
	QImage _roundedTop;
	QImage _barSmall;
	QImage _barLarge;
	QImage _shadowTop;
	int _smallWidth = 0;
	int _largeWidth = 0;
	int _index = 0;
	int _count = 0;

	style::cursor _cursor = style::cur_default;

	std::unique_ptr<Media::Streaming::Instance> _videoInstance;
	crl::time _videoStartPosition = 0;
	Fn<bool()> _videoPaused;
	QImage _shadowBottom;

	rpl::event_stream<> _openRequests;
	rpl::event_stream<int> _moveRequests;

};
