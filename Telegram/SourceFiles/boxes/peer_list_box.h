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

#include "boxes/abstractbox.h"

namespace Ui {
class RippleAnimation;
class MultiSelect;
template <typename Widget>
class WidgetSlideWrap;
class FlatLabel;
} // namespace Ui

class PeerListBox : public BoxContent {
	Q_OBJECT

	class Inner;

public:
	class Row {
	public:
		Row(PeerData *peer);

		void setDisabled(bool disabled);

		void setActionLink(const QString &action);
		PeerData *peer() const {
			return _peer;
		}

		void setCustomStatus(const QString &status);
		void clearCustomStatus();

		virtual ~Row();

	private:
		// Inner interface.
		friend class Inner;

		void refreshName();
		const Text &name() const {
			return _name;
		}

		enum class StatusType {
			Online,
			LastSeen,
			Custom,
		};
		void refreshStatus();
		StatusType statusType() const;
		QString status() const;

		void refreshActionLink();
		QString action() const;
		int actionWidth() const;

		void setAbsoluteIndex(int index) {
			_absoluteIndex = index;
		}
		int absoluteIndex() const {
			return _absoluteIndex;
		}
		bool disabled() const {
			return _disabled;
		}
		bool isGlobalSearchResult() const {
			return _isGlobalSearchResult;
		}
		void setIsGlobalSearchResult(bool isGlobalSearchResult) {
			_isGlobalSearchResult = isGlobalSearchResult;
		}

		template <typename UpdateCallback>
		void addRipple(QSize size, QPoint point, UpdateCallback updateCallback);
		void stopLastRipple();
		void paintRipple(Painter &p, int x, int y, int outerWidth, TimeMs ms);

		void setNameFirstChars(const OrderedSet<QChar> &nameFirstChars) {
			_nameFirstChars = nameFirstChars;
		}
		const OrderedSet<QChar> &nameFirstChars() const {
			return _nameFirstChars;
		}

		void lazyInitialize();

	private:
		PeerData *_peer = nullptr;
		bool _initialized = false;
		std::unique_ptr<Ui::RippleAnimation> _ripple;
		Text _name;
		QString _status;
		StatusType _statusType = StatusType::Online;
		QString _action;
		int _actionWidth = 0;
		bool _disabled = false;
		int _absoluteIndex = -1;
		OrderedSet<QChar> _nameFirstChars;
		bool _isGlobalSearchResult = false;

	};

	class Controller {
	public:
		virtual void prepare() = 0;
		virtual void rowClicked(PeerData *peer) = 0;
		virtual void rowActionClicked(PeerData *peer) {
		}
		virtual void preloadRows() {
		}
		virtual std::unique_ptr<Row> createGlobalRow(PeerData *peer) {
			return std::unique_ptr<Row>();
		}

		virtual ~Controller() = default;

	protected:
		PeerListBox *view() const {
			return _view;
		}

	private:
		void setView(PeerListBox *box) {
			_view = box;
			prepare();
		}

		PeerListBox *_view = nullptr;

		friend class PeerListBox;

	};
	PeerListBox(QWidget*, std::unique_ptr<Controller> controller);

	// Interface for the controller.
	void appendRow(std::unique_ptr<Row> row);
	void prependRow(std::unique_ptr<Row> row);
	Row *findRow(PeerData *peer);
	void updateRow(Row *row);
	void removeRow(Row *row);
	int fullRowsCount() const;
	void setAboutText(const QString &aboutText);
	void setAbout(object_ptr<Ui::FlatLabel> about);
	void refreshRows();
	enum class SearchMode {
		None,
		Local,
		Global,
	};
	void setSearchMode(SearchMode mode);
	void setSearchNoResultsText(const QString &noResultsText);
	void setSearchNoResults(object_ptr<Ui::FlatLabel> searchNoResults);
	void setSearchLoadingText(const QString &searchLoadingText);
	void setSearchLoading(object_ptr<Ui::FlatLabel> searchLoading);

	// callback takes two iterators, like [](auto &begin, auto &end).
	template <typename ReorderCallback>
	void reorderRows(ReorderCallback &&callback) {
		_inner->reorderRows(std::forward<ReorderCallback>(callback));
	}

protected:
	void prepare() override;
	void setInnerFocus() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>> createMultiSelect();
	int getTopScrollSkip() const;
	void updateScrollSkips();
	void searchQueryChanged(const QString &query);

	object_ptr<Ui::WidgetSlideWrap<Ui::MultiSelect>> _select = { nullptr };

	class Inner;
	QPointer<Inner> _inner;

	std::unique_ptr<Controller> _controller;

};

// This class is hold in header because it requires Qt preprocessing.
class PeerListBox::Inner : public TWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, Controller *controller);

	void selectSkip(int direction);
	void selectSkipPage(int height, int direction);

	void clearSelection();

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void searchQueryChanged(QString query);
	void submitted();

	// Interface for the controller.
	void appendRow(std::unique_ptr<Row> row);
	void prependRow(std::unique_ptr<Row> row);
	Row *findRow(PeerData *peer);
	void updateRow(Row *row) {
		updateRow(row, RowIndex());
	}
	void removeRow(Row *row);
	int fullRowsCount() const;
	void setAbout(object_ptr<Ui::FlatLabel> about);
	void refreshRows();
	void setSearchMode(SearchMode mode);
	void setSearchNoResults(object_ptr<Ui::FlatLabel> searchNoResults);
	void setSearchLoading(object_ptr<Ui::FlatLabel> searchLoading);

	template <typename ReorderCallback>
	void reorderRows(ReorderCallback &&callback) {
		callback(_rows.begin(), _rows.end());
		for (auto &searchEntity : _searchIndex) {
			callback(searchEntity.second.begin(), searchEntity.second.end());
		}
		refreshIndices();
	}

signals:
	void mustScrollTo(int ymin, int ymax);

public slots:
	void peerUpdated(PeerData *peer);
	void onPeerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void refreshIndices();
	void appendGlobalSearchRow(std::unique_ptr<Row> row);

	struct RowIndex {
		RowIndex() = default;
		explicit RowIndex(int value) : value(value) {
		}
		int value = -1;
	};
	friend inline bool operator==(RowIndex a, RowIndex b) {
		return (a.value == b.value);
	}
	friend inline bool operator!=(RowIndex a, RowIndex b) {
		return !(a == b);
	}

	struct Selected {
		Selected() = default;
		Selected(RowIndex index, bool action) : index(index), action(action) {
		}
		Selected(int index, bool action) : index(index), action(action) {
		}
		RowIndex index;
		bool action = false;
	};
	friend inline bool operator==(Selected a, Selected b) {
		return (a.index == b.index) && (a.action == b.action);
	}
	friend inline bool operator!=(Selected a, Selected b) {
		return !(a == b);
	}

	void setSelected(Selected selected);
	void setPressed(Selected pressed);
	void restoreSelection();

	void updateSelection();
	void loadProfilePhotos();
	void checkScrollForPreload();

	void updateRow(Row *row, RowIndex hint);
	void updateRow(RowIndex row);
	int getRowTop(RowIndex row) const;
	Row *getRow(RowIndex element);
	RowIndex findRowIndex(Row *row, RowIndex hint = RowIndex());

	void paintRow(Painter &p, TimeMs ms, RowIndex index);

	void addRowEntry(Row *row);
	void addToSearchIndex(Row *row);
	bool addingToSearchIndex() const;
	void removeFromSearchIndex(Row *row);
	bool showingSearch() const {
		return !_searchQuery.isEmpty();
	}
	int shownRowsCount() const {
		return showingSearch() ? _filterResults.size() : _rows.size();
	}
	template <typename Callback>
	bool enumerateShownRows(Callback callback);
	template <typename Callback>
	bool enumerateShownRows(int from, int to, Callback callback);

	int labelHeight() const;

	void needGlobalSearch();
	bool globalSearchInCache();
	void globalSearchOnServer();
	void globalSearchDone(const MTPcontacts_Found &result, mtpRequestId requestId);
	bool globalSearchFail(const RPCError &error, mtpRequestId requestId);
	bool globalSearchLoading() const;
	void clearGlobalSearchRows();

	Controller *_controller = nullptr;
	int _rowHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	Selected _selected;
	Selected _pressed;
	bool _mouseSelection = false;

	std::vector<std::unique_ptr<Row>> _rows;
	std::map<PeerData*, Row*> _rowsByPeer;

	SearchMode _searchMode = SearchMode::None;
	std::map<QChar, std::vector<Row*>> _searchIndex;
	QString _searchQuery;
	std::vector<Row*> _filterResults;

	object_ptr<Ui::FlatLabel> _about = { nullptr };
	object_ptr<Ui::FlatLabel> _searchNoResults = { nullptr };
	object_ptr<Ui::FlatLabel> _searchLoading = { nullptr };

	QPoint _lastMousePosition;

	std::vector<std::unique_ptr<Row>> _globalSearchRows;
	object_ptr<SingleTimer> _globalSearchTimer = { nullptr };
	QString _globalSearchQuery;
	QString _globalSearchHighlight;
	mtpRequestId _globalSearchRequestId = 0;
	std::map<QString, MTPcontacts_Found> _globalSearchCache;
	std::map<mtpRequestId, QString> _globalSearchQueries;

};
