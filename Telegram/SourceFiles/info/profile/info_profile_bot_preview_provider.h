/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_bot_media_previews.h"
#include "info/media/info_media_common.h"

namespace Info::Profile {

class BotPreviewProvider final
	: public Media::ListProvider
	, private Media::ListSectionDelegate {
public:
	BotPreviewProvider(
		not_null<UserData*> bot,
		bool editable,
		Fn<void(int)> open);
	~BotPreviewProvider();

	void setMedia(
		const std::vector<Api::BotPreviewMedia> &media,
		std::shared_ptr<const Data::FileOrigin> origin,
		std::optional<Api::BotPreviewMedia> published = std::nullopt);
	void setPending(
		QImage image,
		std::optional<int> replacing = std::nullopt);
	void move(int from, int to);
	[[nodiscard]] int indexOf(MsgId id) const;

	Media::Type type() override;
	bool hasSelectRestriction() override;
	rpl::producer<bool> hasSelectRestrictionChanges() override;
	bool isPossiblyMyItem(not_null<const HistoryItem*> item) override;
	std::optional<int> fullCount() override;
	void restart() override;
	void checkPreload(
		QSize viewport,
		not_null<Media::BaseLayout*> top,
		not_null<Media::BaseLayout*> bottom,
		bool preloadTop,
		bool preloadBottom) override;
	void refreshViewer() override;
	rpl::producer<> refreshed() override;
	std::vector<Media::ListSection> fillSections(
		not_null<Overview::Layout::Delegate*> delegate) override;
	rpl::producer<not_null<Media::BaseLayout*>> layoutRemoved() override;
	Media::BaseLayout *lookupLayout(const HistoryItem *item) override;
	bool isMyItem(not_null<const HistoryItem*> item) override;
	bool isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) override;
	Media::ListItemSelectionData computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) override;
	void applyDragSelection(
		Media::ListSelectedMap &selected,
		not_null<const HistoryItem*> from,
		bool skipFrom,
		not_null<const HistoryItem*> till,
		bool skipTill) override;
	bool allowSaveFileAs(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) override;
	QString showInFolderPath(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) override;
	void setSearchQuery(QString query) override;
	void jumpToMessage(MsgId id, Fn<void(FullMsgId)> done) override;
	bool orderedByItemId() const override;
	bool showScrollDateBadge() const override;
	bool open(FullMsgId id) override;
	int64 scrollTopStatePosition(not_null<HistoryItem*> item) override;
	HistoryItem *scrollTopStateItem(
		Media::ListScrollTopState state) override;
	void saveState(
		not_null<Media::Memento*> memento,
		Media::ListScrollTopState state) override;
	void restoreState(
		not_null<Media::Memento*> memento,
		Fn<void(Media::ListScrollTopState)> restore) override;

private:
	struct Row;
	struct Pending;

	bool sectionHasFloatingHeader() override;
	QString sectionTitle(
		not_null<const Media::BaseLayout*> item) override;
	bool sectionItemBelongsHere(
		not_null<const Media::BaseLayout*> item,
		not_null<const Media::BaseLayout*> previous) override;

	[[nodiscard]] std::unique_ptr<Row> makeRow(
		Api::BotPreviewMedia media);

	const not_null<UserData*> _bot;
	const bool _editable;
	Fn<void(int)> _open;
	std::vector<std::unique_ptr<Row>> _rows;
	std::unique_ptr<Pending> _pending;
	std::shared_ptr<const Data::FileOrigin> _origin;
	rpl::event_stream<not_null<Media::BaseLayout*>> _layoutRemoved;
	rpl::event_stream<> _refreshed;

};

} // namespace Info::Profile
