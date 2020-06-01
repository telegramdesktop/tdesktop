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
#include "chat_helpers/stickers_set.h"
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

class StickersBox final
	: public Ui::BoxContent
	, public RPCSender
	, private base::Subscriber {
public:
	enum class Section {
		Installed,
		Featured,
		Archived,
		Attached,
	};

	StickersBox(
		QWidget*,
		not_null<Main::Session*> session,
		Section section);
	StickersBox(QWidget*, not_null<ChannelData*> megagroup);
	StickersBox(
		QWidget*,
		not_null<Main::Session*> session,
		const MTPVector<MTPStickerSetCovered> &attachedSets);

	void setInnerFocus() override;

	~StickersBox();

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

		Inner *widget() {
			return _weak;
		}
		int index() const {
			return _index;
		}

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
	bool installFail(uint64 setId, const RPCError &error);

	void preloadArchivedSets();
	void requestArchivedSets();
	void loadMoreArchived();
	void getArchivedDone(uint64 offsetId, const MTPmessages_ArchivedStickers &result);
	void showAttachedStickers();

	const not_null<Main::Session*> _session;

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

	Stickers::Order _localOrder;
	Stickers::Order _localRemoved;

};

// This class is hold in header because it requires Qt preprocessing.
class StickersBox::Inner
	: public Ui::RpWidget
	, private base::Subscriber {
	Q_OBJECT

public:
	using Section = StickersBox::Section;

	Inner(
		QWidget *parent,
		not_null<Main::Session*> session,
		Section section);
	Inner(QWidget *parent, not_null<ChannelData*> megagroup);

	base::Observable<int> scrollToY;
	void setInnerFocus();

	void saveGroupSet();

	void rebuild();
	void updateSize(int newWidth = 0);
	void updateRows(); // refresh only pack cover stickers
	bool appendSet(not_null<Stickers::Set*> set);

	Stickers::Order getOrder() const;
	Stickers::Order getFullOrder() const;
	Stickers::Order getRemovedSets() const;

	void setFullOrder(const Stickers::Order &order);
	void setRemovedSets(const Stickers::Order &removed);

	void setInstallSetCallback(Fn<void(uint64 setId)> callback) {
		_installSetCallback = std::move(callback);
	}
	void setLoadMoreCallback(Fn<void()> callback) {
		_loadMoreCallback = std::move(callback);
	}

	void setMinHeight(int newWidth, int minHeight);

	int getVisibleTop() const {
		return _visibleTop;
	}

	~Inner();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;

signals:
	void draggingScrollDelta(int delta);

public slots:
	void onUpdateSelected();

private:
	struct Row {
		Row(
			not_null<Stickers::Set*> set,
			DocumentData *sticker,
			int32 count,
			const QString &title,
			int titleWidth,
			bool installed,
			bool official,
			bool unread,
			bool archived,
			bool removed,
			int32 pixw,
			int32 pixh);
		~Row();

		bool isRecentSet() const;

		const not_null<Stickers::Set*> set;
		DocumentData *sticker = nullptr;
		std::shared_ptr<Data::DocumentMedia> stickerMedia;
		std::shared_ptr<Stickers::SetThumbnailView> thumbnailMedia;
		int32 count = 0;
		QString title;
		int titleWidth = 0;
		bool installed = false;
		bool official = false;
		bool unread = false;
		bool archived = false;
		bool removed = false;
		int32 pixw = 0;
		int32 pixh = 0;
		anim::value yadd;
		std::unique_ptr<Ui::RippleAnimation> ripple;
		std::unique_ptr<Lottie::SinglePlayer> lottie;
	};
	struct MegagroupSet {
		inline bool operator==(const MegagroupSet &other) const {
			return true;
		}
		inline bool operator!=(const MegagroupSet &other) const {
			return false;
		}
	};
	using SelectedRow = base::optional_variant<MegagroupSet, int>;
	class AddressField : public Ui::UsernameInput {
	public:
		using UsernameInput::UsernameInput;

	protected:
		void correctValue(
			const QString &was,
			int wasCursor,
			QString &now,
			int &nowCursor) override;

	};

	template <typename Check>
	Stickers::Order collectSets(Check check) const;

	void checkLoadMore();
	void updateScrollbarWidth();
	int getRowIndex(uint64 setId) const;
	void setRowRemoved(int index, bool removed);

	void setSelected(SelectedRow selected);
	void setActionDown(int newActionDown);
	void setPressed(SelectedRow pressed);
	void setup();
	QRect relativeButtonRect(bool removeButton) const;
	void ensureRipple(const style::RippleAnimation &st, QImage mask, bool removeButton);

	bool shiftingAnimationCallback(crl::time now);
	void paintRow(Painter &p, not_null<Row*> row, int index);
	void paintRowThumbnail(Painter &p, not_null<Row*> row, int left);
	void paintFakeButton(Painter &p, not_null<Row*> row, int index);
	void clear();
	void updateCursor();
	void setActionSel(int32 actionSel);
	float64 aboveShadowOpacity() const;
	void validateLottieAnimation(not_null<Row*> row);
	void updateRowThumbnail(not_null<Row*> row);

	void readVisibleSets();

	void updateControlsGeometry();
	void rebuildAppendSet(not_null<Stickers::Set*> set, int maxNameWidth);
	void fillSetCover(not_null<Stickers::Set*> set, DocumentData **outSticker, int *outWidth, int *outHeight) const;
	int fillSetCount(not_null<Stickers::Set*> set) const;
	QString fillSetTitle(not_null<Stickers::Set*> set, int maxNameWidth, int *outTitleWidth) const;
	void fillSetFlags(not_null<Stickers::Set*> set, bool *outInstalled, bool *outOfficial, bool *outUnread, bool *outArchived);
	void rebuildMegagroupSet();
	void fixupMegagroupSetAddress();
	void handleMegagroupSetAddressChange();
	void setMegagroupSelectedSet(const MTPInputStickerSet &set);

	int countMaxNameWidth() const;

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	Section _section;

	int32 _rowHeight;

	std::vector<std::unique_ptr<Row>> _rows;
	std::vector<crl::time> _shiftingStartTimes;
	crl::time _aboveShadowFadeStart = 0;
	anim::value _aboveShadowFadeOpacity;
	Ui::Animations::Basic _shiftingAnimation;

	Fn<void(uint64 setId)> _installSetCallback;
	Fn<void()> _loadMoreCallback;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _itemsTop = 0;

	int _actionSel = -1;
	int _actionDown = -1;

	QString _addText;
	int _addWidth = 0;
	QString _undoText;
	int _undoWidth = 0;

	int _buttonHeight = 0;

	QPoint _mouse;
	bool _inDragArea = false;
	SelectedRow _selected;
	SelectedRow _pressed;
	QPoint _dragStart;
	int _started = -1;
	int _dragging = -1;
	int _above = -1;

	int _minHeight = 0;

	int _scrollbar = 0;
	ChannelData *_megagroupSet = nullptr;
	MTPInputStickerSet _megagroupSetInput = MTP_inputStickerSetEmpty();
	std::unique_ptr<Row> _megagroupSelectedSet;
	object_ptr<AddressField> _megagroupSetField = { nullptr };
	object_ptr<Ui::PlainShadow> _megagroupSelectedShadow = { nullptr };
	object_ptr<Ui::CrossButton> _megagroupSelectedRemove = { nullptr };
	object_ptr<Ui::BoxContentDivider> _megagroupDivider = { nullptr };
	object_ptr<Ui::FlatLabel> _megagroupSubTitle = { nullptr };
	base::Timer _megagroupSetAddressChangedTimer;
	mtpRequestId _megagroupSetRequestId = 0;

};
