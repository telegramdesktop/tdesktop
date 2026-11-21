/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_bot_preview_provider.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/media/info_media_list_section.h"
#include "lang/lang_text_entity.h"
#include "layout/layout_selection.h"
#include "main/main_session.h"
#include "overview/overview_layout_delegate.h"
#include "storage/storage_shared_media.h"
#include "ui/painter.h"

#include "styles/style_info_profile_bot_media_previews.h"
#include "styles/style_overview.h"
#include "styles/style_widgets.h"

namespace Info::Profile {
namespace {

class UploadTile final : public Media::BaseLayout {
public:
	UploadTile(
		not_null<Overview::Layout::Delegate*> delegate,
		not_null<HistoryItem*> item,
		QImage preview);
	void initDimensions() override;
	int32 resizeGetHeight(int32 width) override;
	void paint(
		Painter &p,
		const QRect &clip,
		TextSelection selected,
		const Overview::Layout::PaintContext *context) override;
	HistoryView::TextState getState(
		QPoint point,
		HistoryView::StateRequest request) const override;
	bool elementsAnimating() const override;

private:
	QImage _preview;
	QPixmap _thumbnail;
	Ui::InfiniteRadialAnimation _spinner;

};

UploadTile::UploadTile(
	not_null<Overview::Layout::Delegate*> delegate,
	not_null<HistoryItem*> item,
	QImage preview)
: ItemBase(delegate, item)
, _preview(std::move(preview))
, _spinner([=] { delegate->repaintItem(this); }, st::botPreviewUploading) {
	_spinner.start();
}

void UploadTile::initDimensions() {
	_maxw = _preview.width();
	_minh = 1;
}

int32 UploadTile::resizeGetHeight(int32 width) {
	if (_width != width) {
		_width = width;
		_height = width * 5 / 4;
		const auto ratio = style::DevicePixelRatio();
		_thumbnail = QPixmap::fromImage(
			_preview.scaled(
				_width * ratio,
				_height * ratio,
				Qt::KeepAspectRatioByExpanding,
				Qt::SmoothTransformation));
		_thumbnail.setDevicePixelRatio(ratio);
	}
	return _height;
}

void UploadTile::paint(
		Painter &p,
		const QRect &clip,
		TextSelection selected,
		const Overview::Layout::PaintContext *context) {
	const auto rect = QRect(0, 0, _width, _height);
	const auto size = _thumbnail.deviceIndependentSize().toSize();
	const auto topLeft = QPoint(
		(_width - size.width()) / 2,
		(_height - size.height()) / 2);
	p.save();
	p.setClipRect(rect, Qt::IntersectClip);
	p.drawPixmap(topLeft, _thumbnail);
	p.fillRect(rect, st::msgDateImgBg);
	p.restore();
	const auto radial = QRect(QPoint(), st::botPreviewUploading.size);
	_spinner.draw(p, rect.center() - radial.center(), _width);
}

HistoryView::TextState UploadTile::getState(
		QPoint point,
		HistoryView::StateRequest request) const {
	return hasPoint(point)
		? HistoryView::TextState(parent())
		: HistoryView::TextState();
}

bool UploadTile::elementsAnimating() const {
	return true;
}

} // namespace

struct BotPreviewProvider::Row {
	void seedThumbnail(QImage image);

	Api::BotPreviewMedia media;
	std::unique_ptr<HistoryItem, HistoryItem::Destroyer> item;
	std::unique_ptr<Media::BaseLayout> layout;
	std::shared_ptr<Data::PhotoMedia> photoThumbnail;
	std::shared_ptr<Data::DocumentMedia> videoThumbnail;
};

struct BotPreviewProvider::Pending {
	QImage preview;
	std::optional<int> replacing;
	std::unique_ptr<HistoryItem, HistoryItem::Destroyer> item;
	std::unique_ptr<UploadTile> layout;
};

void BotPreviewProvider::Row::seedThumbnail(QImage image) {
	constexpr auto side = 900;
	if (image.width() > side || image.height() > side) {
		image = image.scaled(
			side,
			side,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	if (const auto photo = std::get_if<not_null<PhotoData*>>(&media)) {
		photoThumbnail = (*photo)->createMediaView();
		photoThumbnail->set(
			Data::PhotoSize::Thumbnail,
			Data::PhotoSize::Thumbnail,
			std::move(image),
			{});
	} else {
		const auto video = std::get<not_null<DocumentData*>>(media);
		videoThumbnail = video->createMediaView();
		videoThumbnail->setThumbnail(std::move(image));
	}
}

BotPreviewProvider::BotPreviewProvider(
	not_null<UserData*> bot,
	bool editable,
	Fn<void(int)> open)
: _bot(bot)
, _editable(editable)
, _open(std::move(open)) {
}

BotPreviewProvider::~BotPreviewProvider() = default;

void BotPreviewProvider::setMedia(
		const std::vector<Api::BotPreviewMedia> &media,
		std::shared_ptr<const Data::FileOrigin> origin,
		std::optional<Api::BotPreviewMedia> published) {
	auto old = base::take(_rows);
	const auto pending = base::take(_pending);
	const auto sameOrigin = (origin == _origin);
	_origin = std::move(origin);
	_rows.reserve(media.size());
	for (const auto &item : media) {
		const auto matches = [&](const auto &row) {
			return row && row->media == item;
		};
		const auto i = sameOrigin
			? ranges::find_if(old, matches)
			: end(old);
		_rows.push_back((i == end(old))
			? makeRow(item)
			: std::move(*i));
	}
	if (pending && published) {
		const auto at = ranges::find(media, *published);
		if (at != end(media)) {
			const auto index = at - begin(media);
			_rows[index]->seedThumbnail(pending->preview);
		}
	}
	refreshViewer();
	if (pending && pending->layout) {
		_layoutRemoved.fire_copy(pending->layout.get());
	}
	for (const auto &row : old) {
		if (row && row->layout) {
			_layoutRemoved.fire_copy(row->layout.get());
		}
	}
}

void BotPreviewProvider::setPending(
		QImage image,
		std::optional<int> replacing) {
	auto old = base::take(_pending);
	if (!image.isNull()) {
		const auto history = _bot->owner().history(_bot);
		const auto item = history->makeMessage(
			{
				.id = history->nextNonHistoryEntryId(),
				.flags = MessageFlag::FakeHistoryItem,
				.from = _bot->id,
			},
			tr::marked(),
			MTP_messageMediaEmpty());
		_pending = std::make_unique<Pending>();
		_pending->item.reset(item.get());
		_pending->preview = std::move(image);
		_pending->replacing = replacing;
	}
	refreshViewer();
	if (old && old->layout) {
		_layoutRemoved.fire_copy(old->layout.get());
	}
}

void BotPreviewProvider::move(int from, int to) {
	base::reorder(_rows, from, to);
}

int BotPreviewProvider::indexOf(MsgId id) const {
	const auto matches = [&](const auto &row) {
		return row->item->id == id;
	};
	const auto i = ranges::find_if(_rows, matches);
	return (i == end(_rows)) ? -1 : int(i - begin(_rows));
}

std::unique_ptr<BotPreviewProvider::Row> BotPreviewProvider::makeRow(
		Api::BotPreviewMedia media) {
	const auto history = _bot->owner().history(_bot);
	const auto make = [&](auto data) {
		return history->makeMessage(
			{
				.id = history->nextNonHistoryEntryId(),
				.flags = MessageFlag::FakeHistoryItem,
				.from = _bot->id,
			},
			data,
			tr::marked());
	};
	using ItemPtr = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
	return std::make_unique<Row>(
		Row{
			.media = media,
			.item = ItemPtr(v::match(media, make).get()),
		});
}

Media::Type BotPreviewProvider::type() {
	return Media::Type::PhotoVideo;
}

bool BotPreviewProvider::hasSelectRestriction() {
	return !_editable || _pending || _bot->session().frozen();
}

rpl::producer<bool> BotPreviewProvider::hasSelectRestrictionChanges() {
	return rpl::never<bool>();
}

bool BotPreviewProvider::isPossiblyMyItem(not_null<const HistoryItem*> item) {
	return isMyItem(item);
}

std::optional<int> BotPreviewProvider::fullCount() {
	const auto adding = _pending && !_pending->replacing;
	return int(_rows.size()) + (adding ? 1 : 0);
}

void BotPreviewProvider::restart() {
	refreshViewer();
}

void BotPreviewProvider::checkPreload(
	QSize viewport,
	not_null<Media::BaseLayout*> top,
	not_null<Media::BaseLayout*> bottom,
	bool preloadTop,
	bool preloadBottom) {
}

void BotPreviewProvider::refreshViewer() {
	_refreshed.fire({});
}

rpl::producer<> BotPreviewProvider::refreshed() {
	return _refreshed.events();
}

std::vector<Media::ListSection> BotPreviewProvider::fillSections(
		not_null<Overview::Layout::Delegate*> delegate) {
	using namespace Overview::Layout;
	auto sections = std::vector<Media::ListSection>();
	if (_rows.empty() && !_pending) {
		return sections;
	}
	auto section = Media::ListSection(type(), sectionDelegate());
	section.setMaxGridColumns(std::min(*fullCount(), 4));
	if (_pending) {
		if (!_pending->layout) {
			_pending->layout = std::make_unique<UploadTile>(
				delegate,
				_pending->item.get(),
				_pending->preview);
			_pending->layout->initDimensions();
		}
		if (!_pending->replacing) {
			section.addItem(_pending->layout.get());
		}
	}
	auto options = MediaOptions{ .aspectRatio = 1.25, .origin = _origin };
	for (auto i = 0, count = int(_rows.size()); i != count; ++i) {
		if (_pending && _pending->replacing == i) {
			section.addItem(_pending->layout.get());
			continue;
		}
		const auto &row = _rows[i];
		if (!row->layout) {
			const auto item = row->item.get();
			const auto photo = [&](not_null<PhotoData*> data)
			-> std::unique_ptr<Media::BaseLayout> {
				return std::make_unique<Photo>(
					delegate,
					item,
					data,
					options);
			};
			const auto video = [&](not_null<DocumentData*> data)
			-> std::unique_ptr<Media::BaseLayout> {
				return std::make_unique<Video>(
					delegate,
					item,
					data,
					options);
			};
			row->layout = v::match(row->media, photo, video);
			row->layout->initDimensions();
		}
		section.addItem(row->layout.get());
	}
	section.finishSection();
	sections.push_back(std::move(section));
	return sections;
}

auto BotPreviewProvider::layoutRemoved()
-> rpl::producer<not_null<Media::BaseLayout*>> {
	return _layoutRemoved.events();
}

Media::BaseLayout *BotPreviewProvider::lookupLayout(const HistoryItem *item) {
	if (_pending && item == _pending->item.get()) {
		return _pending->layout.get();
	}
	const auto index = item ? indexOf(item->id) : -1;
	return (index >= 0) ? _rows[index]->layout.get() : nullptr;
}

bool BotPreviewProvider::isMyItem(not_null<const HistoryItem*> item) {
	return item->history()->peer == _bot
		&& ((indexOf(item->id) >= 0)
			|| (_pending && item == _pending->item.get()));
}

bool BotPreviewProvider::isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) {
	return indexOf(a->id) >= indexOf(b->id);
}

Media::ListItemSelectionData BotPreviewProvider::computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) {
	auto data = Media::ListItemSelectionData(selection);
	data.canDelete = !hasSelectRestriction();
	return data;
}

void BotPreviewProvider::applyDragSelection(
		Media::ListSelectedMap &selected,
		not_null<const HistoryItem*> from,
		bool skipFrom,
		not_null<const HistoryItem*> till,
		bool skipTill) {
	const auto first = indexOf(from->id) + (skipFrom ? 1 : 0);
	const auto last = indexOf(till->id) - (skipTill ? 1 : 0);
	selected.clear();
	for (auto i = first; i >= 0 && i <= last; ++i) {
		const auto item = _rows[i]->item.get();
		Media::ChangeItemSelection(
			selected,
			item,
			computeSelectionData(item, FullSelection));
	}
}

bool BotPreviewProvider::allowSaveFileAs(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	return false;
}

QString BotPreviewProvider::showInFolderPath(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	return {};
}

void BotPreviewProvider::setSearchQuery(QString query) {
}

void BotPreviewProvider::jumpToMessage(MsgId id, Fn<void(FullMsgId)> done) {
	done({});
}

bool BotPreviewProvider::orderedByItemId() const {
	return false;
}

bool BotPreviewProvider::showScrollDateBadge() const {
	return false;
}

bool BotPreviewProvider::open(FullMsgId id) {
	if (const auto index = indexOf(id.msg); index >= 0) {
		_open(index);
	}
	return true;
}

int64 BotPreviewProvider::scrollTopStatePosition(
		not_null<HistoryItem*> item) {
	return item->id.bare;
}

HistoryItem *BotPreviewProvider::scrollTopStateItem(
		Media::ListScrollTopState state) {
	const auto index = indexOf(MsgId(state.position));
	return (index >= 0) ? _rows[index]->item.get() : nullptr;
}

void BotPreviewProvider::saveState(
	not_null<Media::Memento*> memento,
	Media::ListScrollTopState state) {
}

void BotPreviewProvider::restoreState(
	not_null<Media::Memento*> memento,
	Fn<void(Media::ListScrollTopState)> restore) {
}

bool BotPreviewProvider::sectionHasFloatingHeader() {
	return false;
}

QString BotPreviewProvider::sectionTitle(
		not_null<const Media::BaseLayout*> item) {
	return {};
}

bool BotPreviewProvider::sectionItemBelongsHere(
		not_null<const Media::BaseLayout*> item,
		not_null<const Media::BaseLayout*> previous) {
	return true;
}

} // namespace Info::Profile
