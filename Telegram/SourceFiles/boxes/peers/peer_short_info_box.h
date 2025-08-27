/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_birthday.h"
#include "ui/layers/box_content.h"

namespace style {
struct ShortInfoCover;
struct ShortInfoBox;
} // namespace style

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

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
	Self,
	User,
	Group,
	Channel,
};

struct PeerShortInfoFields {
	QString name;
	QString channelName;
	QString channelLink;
	QString phone;
	QString link;
	TextWithEntities about;
	QString username;
	Data::Birthday birthday;
	bool isBio = false;
};

struct PeerShortInfoUserpic {
	int index = 0;
	int count = 0;

	QImage photo;
	float64 photoLoadingProgress = 0.;
	std::shared_ptr<Media::Streaming::Document> videoDocument;
	crl::time videoStartPosition = 0;
	QString additionalStatus;
};

class PeerShortInfoCover final {
public:
	PeerShortInfoCover(
		not_null<QWidget*> parent,
		const style::ShortInfoCover &st,
		rpl::producer<QString> name,
		rpl::producer<QString> status,
		rpl::producer<PeerShortInfoUserpic> userpic,
		Fn<bool()> videoPaused);
	~PeerShortInfoCover();

	[[nodiscard]] not_null<Ui::RpWidget*> widget() const;
	[[nodiscard]] object_ptr<Ui::RpWidget> takeOwned();

	[[nodiscard]] gsl::span<const QImage, 4> roundMask() const;

	void setScrollTop(int scrollTop);

	[[nodiscard]] rpl::producer<int> moveRequests() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct CustomLabelStyle;
	struct Radial;

	void paint(QPainter &p);
	void paintCoverImage(QPainter &p, const QImage &image);
	void paintBars(QPainter &p);
	void paintShadow(QPainter &p);
	void paintRadial(QPainter &p);

	[[nodiscard]] QImage currentVideoFrame() const;

	void applyUserpic(PeerShortInfoUserpic &&value);
	void applyAdditionalStatus(const QString &status);
	[[nodiscard]] QRect radialRect() const;

	void videoWaiting();
	void checkStreamedIsStarted();
	void handleStreamingUpdate(Media::Streaming::Update &&update);
	void handleStreamingError(Media::Streaming::Error &&error);
	void streamingReady(Media::Streaming::Information &&info);
	void clearVideo();

	void updateRadialState();
	void refreshCoverCursor();
	void refreshBarImages();
	void refreshLabelsGeometry();

	const style::ShortInfoCover &_st;

	object_ptr<Ui::RpWidget> _owned;
	const not_null<Ui::RpWidget*> _widget;
	std::unique_ptr<CustomLabelStyle> _nameStyle;
	object_ptr<Ui::FlatLabel> _name;
	std::unique_ptr<CustomLabelStyle> _statusStyle;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::FlatLabel> _additionalStatus = { nullptr };

	std::array<QImage, 4> _roundMask;
	std::array<QImage, 4> _roundMaskRetina;
	QImage _userpicImage;
	QImage _roundedTopImage;
	QImage _barSmall;
	QImage _barLarge;
	QImage _shadowTop;
	int _scrollTop = 0;
	int _smallWidth = 0;
	int _largeWidth = 0;
	int _index = 0;
	int _count = 0;

	style::cursor _cursor = style::cur_default;

	std::unique_ptr<Media::Streaming::Instance> _videoInstance;
	crl::time _videoStartPosition = 0;
	crl::time _videoPosition = 0;
	crl::time _videoDuration = 0;
	Fn<bool()> _videoPaused;
	QImage _shadowBottom;

	std::unique_ptr<Radial> _radial;
	float64 _photoLoadingProgress = 0.;

	rpl::event_stream<int> _moveRequests;

};

class PeerShortInfoBox final : public Ui::BoxContent {
public:
	PeerShortInfoBox(
		QWidget*,
		PeerShortInfoType type,
		rpl::producer<PeerShortInfoFields> fields,
		rpl::producer<QString> status,
		rpl::producer<PeerShortInfoUserpic> userpic,
		Fn<bool()> videoPaused,
		const style::ShortInfoBox *stOverride);
	~PeerShortInfoBox();

	[[nodiscard]] rpl::producer<> openRequests() const;
	[[nodiscard]] rpl::producer<int> moveRequests() const;
	[[nodiscard]] auto fillMenuRequests() const
	-> rpl::producer<Ui::Menu::MenuCallback>;

protected:
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	void prepare() override;
	void prepareRows();

	void resizeEvent(QResizeEvent *e) override;

	void refreshRoundedTopImage(const QColor &color);
	int fillRoundedTopHeight();

	[[nodiscard]] rpl::producer<QString> nameValue() const;
	[[nodiscard]] rpl::producer<TextWithEntities> channelValue() const;
	[[nodiscard]] rpl::producer<TextWithEntities> linkValue() const;
	[[nodiscard]] rpl::producer<QString> phoneValue() const;
	[[nodiscard]] rpl::producer<QString> usernameValue() const;
	[[nodiscard]] rpl::producer<QString> birthdayLabel() const;
	[[nodiscard]] rpl::producer<QString> birthdayValue() const;
	[[nodiscard]] rpl::producer<TextWithEntities> aboutValue() const;

	const style::ShortInfoBox &_st;
	const PeerShortInfoType _type = PeerShortInfoType::User;

	rpl::variable<PeerShortInfoFields> _fields;

	QColor _roundedTopColor;
	QImage _roundedTop;

	object_ptr<Ui::RpWidget> _topRoundBackground;
	object_ptr<Ui::ScrollArea> _scroll;
	not_null<Ui::VerticalLayout*> _rows;
	PeerShortInfoCover _cover;

	base::unique_qptr<Ui::RpWidget> _menuHolder;
	rpl::event_stream<Ui::Menu::MenuCallback> _fillMenuRequests;

	rpl::event_stream<> _openRequests;

};
