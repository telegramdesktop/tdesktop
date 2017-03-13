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

		void setIndex(int index) {
			_index = index;
		}
		int index() const {
			return _index;
		}

		template <typename UpdateCallback>
		void addRipple(QSize size, QPoint point, UpdateCallback updateCallback);
		void stopLastRipple();
		void paintRipple(Painter &p, int x, int y, int outerWidth, TimeMs ms);

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
		int _index = -1;

	};

	class Controller {
	public:
		virtual void prepare() = 0;
		virtual void rowClicked(PeerData *peer) = 0;
		virtual void rowActionClicked(PeerData *peer) {
		}
		virtual void preloadRows() {
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
	void setAboutText(const QString &aboutText);
	void refreshRows();

protected:
	void prepare() override;

	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	class Inner;
	QPointer<Inner> _inner;

	std::unique_ptr<Controller> _controller;

};

// This class is hold in header because it requires Qt preprocessing.
class PeerListBox::Inner : public TWidget, public RPCSender, private base::Subscriber {
	Q_OBJECT

public:
	Inner(QWidget *parent, Controller *controller);

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	void clearSelection();

	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	// Interface for the controller.
	void appendRow(std::unique_ptr<Row> row);
	void prependRow(std::unique_ptr<Row> row);
	Row *findRow(PeerData *peer);
	void updateRow(Row *row);
	void removeRow(Row *row);
	void setAboutText(const QString &aboutText);
	void refreshRows();

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
	struct SelectedRow {
		int index = -1;
		bool action = false;
	};
	friend inline bool operator==(SelectedRow a, SelectedRow b) {
		return (a.index == b.index) && (a.action == b.action);
	}
	friend inline bool operator!=(SelectedRow a, SelectedRow b) {
		return !(a == b);
	}

	void setPressed(SelectedRow pressed);

	void updateSelection();
	void loadProfilePhotos();
	void checkScrollForPreload();

	void updateRowWithIndex(int index);
	int getRowTop(int index) const;

	void paintRow(Painter &p, TimeMs ms, int index);

	Controller *_controller = nullptr;
	int _rowHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	SelectedRow _selected;
	SelectedRow _pressed;
	bool _mouseSelection = false;

	std::vector<std::unique_ptr<Row>> _rows;
	QMap<PeerData*, Row*> _rowsByPeer;

	int _aboutWidth = 0;
	int _aboutHeight = 0;
	Text _about;

	QPoint _lastMousePosition;

};
