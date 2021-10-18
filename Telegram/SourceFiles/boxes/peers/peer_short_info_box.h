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

private:
	void prepare() override;
	void prepareRows();
	RectParts customCornersFilling() override;

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	[[nodiscard]] QImage currentVideoFrame() const;

	[[nodiscard]] rpl::producer<QString> nameValue() const;
	[[nodiscard]] rpl::producer<TextWithEntities> linkValue() const;
	[[nodiscard]] rpl::producer<QString> phoneValue() const;
	[[nodiscard]] rpl::producer<QString> usernameValue() const;
	[[nodiscard]] rpl::producer<TextWithEntities> aboutValue() const;
	void applyUserpic(PeerShortInfoUserpic &&value);
	QRect coverRect() const;
	QRect radialRect() const;

	void videoWaiting();
	void checkStreamedIsStarted();
	void handleStreamingUpdate(Media::Streaming::Update &&update);
	void handleStreamingError(Media::Streaming::Error &&error);
	void streamingReady(Media::Streaming::Information &&info);

	const PeerShortInfoType _type = PeerShortInfoType::User;

	rpl::variable<PeerShortInfoFields> _fields;

	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::ScrollArea> _scroll;
	not_null<Ui::VerticalLayout*> _rows;

	QImage _userpicImage;
	std::unique_ptr<Media::Streaming::Instance> _videoInstance;
	crl::time _videoStartPosition = 0;
	Fn<bool()> _videoPaused;
	QImage _shadow;

	rpl::event_stream<> _openRequests;

};
