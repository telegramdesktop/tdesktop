/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "base/timer.h"
#include "mtproto/sender.h"
#include "data/stickers/data_stickers_set.h"
#include "ui/effects/animations.h"

namespace style {
struct RippleAnimation;
struct PeerListItem;
} // namespace style

namespace Ui {
class PlainShadow;
class RippleAnimation;
class SettingsSlider;
class SlideAnimation;
class CrossButton;
class BoxContentDivider;
} // namespace Ui

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Data {
class DocumentMedia;
enum class StickersType : uchar;
} // namespace Data

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace Stickers {
class Set;
} // namespace Stickers

class StickersBox final : public Ui::BoxContent {
public:
	enum class Section {
		Installed,
		Featured,
		Archived,
		Attached,
		Masks,
	};

	StickersBox(
		QWidget*,
		std::shared_ptr<ChatHelpers::Show> show,
		Section section,
		bool masks = false);
	StickersBox(
		QWidget*,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<ChannelData*> megagroup,
		bool isEmoji);
	StickersBox(
		QWidget*,
		std::shared_ptr<ChatHelpers::Show> show,
		const QVector<MTPStickerSetCovered> &attachedSets);
	StickersBox(
		QWidget*,
		std::shared_ptr<ChatHelpers::Show> show,
		const std::vector<StickerSetIdentifier> &emojiSets);
	~StickersBox();

	[[nodiscard]] Main::Session &session() const;

	void setInnerFocus() override;

protected:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	class Inner;
	class Tab {
	public:
		Tab() = default;

		template <typename ...Args>
		Tab(int index, Args&&... args);

		object_ptr<Inner> takeWidget();
		void returnWidget(object_ptr<Inner> widget);

		[[nodiscard]] Inner *widget() const;
		[[nodiscard]] int index() const;

		void saveScrollTop();
		int scrollTop() const {
			return _scrollTop;
		}

	private:
		const int _index = 0;
		object_ptr<Inner> _widget = { nullptr };
		QPointer<Inner> _weak;
		int _scrollTop = 0;

	};

	void handleStickersUpdated();
	void refreshTabs();
	void rebuildList(Tab *tab = nullptr);
	void updateTabsGeometry();
	void switchTab();
	void installSet(uint64 setId);
	int topSkip() const;
	void saveChanges();

	QPixmap grabContentCache();

	void installDone(const MTPmessages_StickerSetInstallResult &result) const;
	void installFail(const MTP::Error &error, uint64 setId);

	void preloadArchivedSets();
	void requestArchivedSets();
	void loadMoreArchived();
	void getArchivedDone(
		const MTPmessages_ArchivedStickers &result,
		uint64 offsetId);
	void showAttachedStickers();

	const Data::StickersSetsOrder &archivedSetsOrder() const;
	Data::StickersSetsOrder &archivedSetsOrderRef() const;

	std::array<Inner*, 5> widgets() const;

	const style::PeerListItem &_st;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	object_ptr<Ui::SettingsSlider> _tabs = { nullptr };
	QList<Section> _tabIndices;
	bool _ignoreTabActivation = false;

	class CounterWidget;
	object_ptr<CounterWidget> _unreadBadge = { nullptr };

	Section _section;
	const bool _isMasks;
	const bool _isEmoji;

	Tab _installed;
	Tab _masks;
	Tab _featured;
	Tab _archived;
	Tab _attached;
	Tab *_tab = nullptr;

	const Data::StickersType _attachedType = {};
	const QVector<MTPStickerSetCovered> _attachedSets;
	const std::vector<StickerSetIdentifier> _emojiSets;

	ChannelData *_megagroupSet = nullptr;

	std::unique_ptr<Ui::SlideAnimation> _slideAnimation;
	object_ptr<Ui::PlainShadow> _titleShadow = { nullptr };

	mtpRequestId _archivedRequestId = 0;
	bool _archivedLoaded = false;
	bool _allArchivedLoaded = false;
	bool _someArchivedLoaded = false;

	Data::StickersSetsOrder _localOrder;
	Data::StickersSetsOrder _localRemoved;

};
