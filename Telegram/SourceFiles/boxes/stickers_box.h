/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/timer.h"
#include "mtproto/sender.h"
#include "data/stickers/data_stickers_set.h"
#include "ui/effects/animations.h"
#include "ui/special_fields.h"

class ConfirmBox;

namespace style {
struct RippleAnimation;
} // namespace style

namespace Ui {
class PlainShadow;
class RippleAnimation;
class SettingsSlider;
class SlideAnimation;
class CrossButton;
class BoxContentDivider;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Main {
class Session;
} // namespace Main

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace Stickers {
class Set;
} // namespace Stickers

class StickersBox final : public Ui::BoxContent, private base::Subscriber {
public:
	enum class Section {
		Installed,
		Featured,
		Archived,
		Attached,
	};

	StickersBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		Section section);
	StickersBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> megagroup);
	StickersBox(
		QWidget*,
		not_null<Window::SessionController*> controller,
		const MTPVector<MTPStickerSetCovered> &attachedSets);
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

		[[nodiscard]] Inner *widget();
		[[nodiscard]] int index() const;

		void saveScrollTop();
		int getScrollTop() const {
			return _scrollTop;
		}

	private:
		int _index = 0;
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
	int getTopSkip() const;
	void saveChanges();

	QPixmap grabContentCache();

	void installDone(const MTPmessages_StickerSetInstallResult &result);
	void installFail(const MTP::Error &error, uint64 setId);

	void preloadArchivedSets();
	void requestArchivedSets();
	void loadMoreArchived();
	void getArchivedDone(
		const MTPmessages_ArchivedStickers &result,
		uint64 offsetId);
	void showAttachedStickers();

	const not_null<Window::SessionController*> _controller;
	MTP::Sender _api;

	object_ptr<Ui::SettingsSlider> _tabs = { nullptr };
	QList<Section> _tabIndices;

	class CounterWidget;
	object_ptr<CounterWidget> _unreadBadge = { nullptr };

	Section _section;

	Tab _installed;
	Tab _featured;
	Tab _archived;
	Tab _attached;
	Tab *_tab = nullptr;

	const MTPVector<MTPStickerSetCovered> _attachedSets;

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
