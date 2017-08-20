/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "chat_helpers/tabbed_selector.h"
#include "base/variant.h"

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class LinkButton;
} // namespace Ui

namespace ChatHelpers {

struct StickerIcon;

class StickersListWidget : public TabbedSelector::Inner, private base::Subscriber, private MTP::Sender {
	Q_OBJECT

public:
	StickersListWidget(QWidget *parent, not_null<Window::Controller*> controller);

	void refreshRecent() override;
	void preloadImages() override;
	void clearSelection() override;
	object_ptr<TabbedSelector::InnerFooter> createFooter() override;

	void showStickerSet(uint64 setId);
	void showMegagroupSet(ChannelData *megagroup);

	void refreshStickers();

	void fillIcons(QList<StickerIcon> &icons);
	bool preventAutoHide();

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	uint64 currentSet(int yOffset) const;

	void installedLocally(uint64 setId);
	void notInstalledLocally(uint64 setId);
	void clearInstalledLocally();

	~StickersListWidget();

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;
	void enterFromChildEvent(QEvent *e, QWidget *child) override;

	TabbedSelector::InnerFooter *getFooter() const override;
	void processHideFinished() override;
	void processPanelHideFinished() override;
	int countHeight() override;

private slots:
	void onSettings();
	void onPreview();

signals:
	void selected(DocumentData *sticker);
	void scrollUpdated();
	void checkForHide();

private:
	class Footer;

	enum class Section {
		Featured,
		Stickers,
	};

	struct OverSticker {
		int section;
		int index;
		bool overDelete;
	};
	struct OverSet {
		int section;
	};
	struct OverButton {
		int section;
	};
	struct OverGroupAdd {
	};
	friend inline bool operator==(OverSticker a, OverSticker b) {
		return (a.section == b.section) && (a.index == b.index) && (a.overDelete == b.overDelete);
	}
	friend inline bool operator==(OverSet a, OverSet b) {
		return (a.section == b.section);
	}
	friend inline bool operator==(OverButton a, OverButton b) {
		return (a.section == b.section);
	}
	friend inline bool operator==(OverGroupAdd a, OverGroupAdd b) {
		return true;
	}
	using OverState = base::optional_variant<OverSticker, OverSet, OverButton, OverGroupAdd>;

	struct SectionInfo {
		int section = 0;
		int count = 0;
		int top = 0;
		int rowsCount = 0;
		int rowsTop = 0;
		int rowsBottom = 0;
	};

	struct Set {
		Set(uint64 id, MTPDstickerSet::Flags flags, const QString &title, int32 hoversSize, const StickerPack &pack = StickerPack()) : id(id), flags(flags), title(title), pack(pack) {
		}
		uint64 id;
		MTPDstickerSet::Flags flags;
		QString title;
		StickerPack pack;
		QSharedPointer<Ui::RippleAnimation> ripple;
	};
	using Sets = QList<Set>;

	template <typename Callback>
	bool enumerateSections(Callback callback) const;
	SectionInfo sectionInfo(int section) const;
	SectionInfo sectionInfoByOffset(int yOffset) const;

	void displaySet(uint64 setId);
	void installSet(uint64 setId);
	void removeMegagroupSet(bool locally);
	void removeSet(uint64 setId);

	bool setHasTitle(const Set &set) const;
	bool stickerHasDeleteButton(const Set &set, int index) const;
	void refreshRecentStickers(bool resize = true);
	void refreshFavedStickers();
	enum class GroupStickersPlace {
		Visible,
		Hidden,
	};
	void refreshMegagroupStickers(GroupStickersPlace place);

	void updateSelected();
	void setSelected(OverState newSelected);
	void setPressed(OverState newPressed);
	QSharedPointer<Ui::RippleAnimation> createButtonRipple(int section);
	QPoint buttonRippleTopLeft(int section) const;

	enum class ValidateIconAnimations {
		Full,
		Scroll,
		None,
	};
	void validateSelectedIcon(ValidateIconAnimations animations);

	Sets &shownSets() {
		return (_section == Section::Featured) ? _featuredSets : _mySets;
	}
	const Sets &shownSets() const {
		return (_section == Section::Featured) ? _featuredSets : _mySets;
	}
	int featuredRowHeight() const;
	void readVisibleSets();

	void paintFeaturedStickers(Painter &p, QRect clip);
	void paintStickers(Painter &p, QRect clip);
	void paintMegagroupEmptySet(Painter &p, int y, bool buttonSelected, TimeMs ms);
	void paintSticker(Painter &p, Set &set, int y, int index, bool selected, bool deleteSelected);

	int stickersRight() const;
	bool featuredHasAddButton(int index) const;
	QRect featuredAddRect(int index) const;
	bool hasRemoveButton(int index) const;
	QRect removeButtonRect(int index) const;
	int megagroupSetInfoLeft() const;
	void refreshMegagroupSetGeometry();
	QRect megagroupSetButtonRectFinal() const;

	enum class AppendSkip {
		None,
		Archived,
		Installed,
	};
	void appendSet(Sets &to, uint64 setId, AppendSkip skip = AppendSkip::None);

	void selectEmoji(EmojiPtr emoji);
	int stickersLeft() const;
	QRect stickerRect(int section, int sel);

	void removeRecentSticker(int section, int index);
	void removeFavedSticker(int section, int index);

	ChannelData *_megagroupSet = nullptr;
	Sets _mySets;
	Sets _featuredSets;
	OrderedSet<uint64> _installedLocallySets;
	QList<bool> _custom;
	base::flat_set<not_null<DocumentData*>> _favedStickersMap;

	Section _section = Section::Stickers;

	uint64 _displayingSetId = 0;
	uint64 _removingSetId = 0;

	Footer *_footer = nullptr;

	OverState _selected;
	OverState _pressed;
	QPoint _lastMousePosition;

	Text _megagroupSetAbout;
	QString _megagroupSetButtonText;
	int _megagroupSetButtonTextWidth = 0;
	QRect _megagroupSetButtonRect;
	std::unique_ptr<Ui::RippleAnimation> _megagroupSetButtonRipple;

	QString _addText;
	int _addWidth;

	object_ptr<Ui::LinkButton> _settings;

	QTimer _previewTimer;
	bool _previewShown = false;

};

} // namespace ChatHelpers
