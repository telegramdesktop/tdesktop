/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class EditColorBox;

namespace Window {
namespace Theme {

class EditorBlock : public TWidget, private base::Subscriber {
public:
	enum class Type {
		Existing,
		New,
	};
	struct Context {
		QPointer<EditColorBox> box;
		QString name;
		QString possibleCopyOf;

		base::Observable<void> updated;
		base::Observable<void> resized;

		struct AppendData {
			QString name;
			QString possibleCopyOf;
			QColor value;
			QString description;
		};
		base::Observable<AppendData> appended;

		struct ChangeData {
			QStringList names;
			QColor value;
		};
		base::Observable<ChangeData> changed;

		struct EditionData {
			QString name;
			QString copyOf;
			QColor value;
		};
		base::Observable<EditionData> pending;

		struct ScrollData {
			Type type;
			int position;
			int height;
		};
		base::Observable<ScrollData> scroll;
	};
	EditorBlock(QWidget *parent, Type type, Context *context);

	void filterRows(const QString &query);
	void chooseRow();
	bool hasSelected() const {
		return (_selected >= 0);
	}
	void clearSelected() {
		setSelected(-1);
	}
	bool selectSkip(int direction);

	void feed(const QString &name, QColor value, const QString &copyOfExisting = QString());
	bool feedCopy(const QString &name, const QString &copyOf);
	const QColor *find(const QString &name);

	bool feedDescription(const QString &name, const QString &description);

	void sortByDistance(const QColor &to);

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	class Row;

	void addRow(const QString &name, const QString &copyOf, QColor value);
	void removeRow(const QString &name, bool removeCopyReferences = true);

	void addToSearch(const Row &row);
	void removeFromSearch(const Row &row);

	template <typename Callback>
	void enumerateRows(Callback callback);

	template <typename Callback>
	void enumerateRows(Callback callback) const;

	template <typename Callback>
	void enumerateRowsFrom(int top, Callback callback);

	template <typename Callback>
	void enumerateRowsFrom(int top, Callback callback) const;

	Row &rowAtIndex(int index);
	int findRowIndex(const QString &name) const;
	Row *findRow(const QString &name);
	int findRowIndex(const Row *row);
	void updateRow(const Row &row);
	void paintRow(Painter &p, int index, const Row &row);

	void updateSelected(QPoint localPosition);
	void setSelected(int selected);
	void setPressed(int pressed);
	void addRowRipple(int index);
	void stopLastRipple(int index);
	void scrollToSelected();

	bool isEditing() const {
		return !_context->name.isEmpty();
	}
	void saveEditing(QColor value);
	void cancelEditing();
	bool checkCopyOf(int index, const QString &possibleCopyOf);
	void checkCopiesChanged(int startIndex, QStringList names, QColor value);
	void activateRow(const Row &row);

	bool isSearch() const {
		return !_searchQuery.isEmpty();
	}
	void searchByQuery(QString query);
	void resetSearch() {
		searchByQuery(QString());
	}

	Type _type = Type::Existing;
	Context *_context = nullptr;

	std::vector<Row> _data;
	QMap<QString, int> _indices;

	QString _searchQuery;
	QVector<int> _searchResults;
	QMap<QChar, OrderedSet<int>> _searchIndex;

	int _selected = -1;
	int _pressed = -1;
	int _editing = -1;

	QPoint _lastGlobalPos;
	bool _mouseSelection = false;

	QBrush _transparent;

};

} // namespace Theme
} // namespace Window
