/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme_preview.h"

#include "window/themes/window_theme.h"
#include "lang/lang_keys.h"
#include "platform/platform_window_title.h"
#include "ui/text/text_options.h"
#include "ui/image/image_prepare.h"
#include "ui/emoji_config.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_media_view.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"

namespace Window {
namespace Theme {
namespace {

QString fillLetters(const QString &name) {
	QList<QString> letters;
	QList<int> levels;
	auto level = 0;
	auto letterFound = false;
	auto ch = name.constData(), end = ch + name.size();
	while (ch != end) {
		auto emojiLength = 0;
		if (auto emoji = Ui::Emoji::Find(ch, end, &emojiLength)) {
			ch += emojiLength;
		} else if (ch->isHighSurrogate()) {
			++ch;
			if (ch != end && ch->isLowSurrogate()) {
				++ch;
			}
		} else if (!letterFound && ch->isLetterOrNumber()) {
			letterFound = true;
			if (ch + 1 != end && Ui::Text::IsDiac(*(ch + 1))) {
				letters.push_back(QString(ch, 2));
				levels.push_back(level);
				++ch;
			} else {
				letters.push_back(QString(ch, 1));
				levels.push_back(level);
			}
			++ch;
		} else {
			if (*ch == ' ') {
				level = 0;
				letterFound = false;
			} else if (letterFound && *ch == '-') {
				level = 1;
				letterFound = true;
			}
			++ch;
		}
	}

	// We prefer the second letter to be after ' ', but it can also be after '-'.
	auto result = QString();
	if (!letters.isEmpty()) {
		result += letters.front();
		auto bestIndex = 0;
		auto bestLevel = 2;
		for (auto i = letters.size(); i != 1;) {
			if (levels[--i] < bestLevel) {
				bestIndex = i;
				bestLevel = levels[i];
			}
		}
		if (bestIndex > 0) {
			result += letters[bestIndex];
		}
	}
	return result.toUpper();
}

class Generator {
public:
	Generator(
		const Instance &theme,
		CurrentData &&current,
		PreviewType type);

	[[nodiscard]] QImage generate();

private:
	enum class Status {
		None,
		Sent,
		Received
	};
	struct Row {
		Ui::Text::String name;
		QString letters;
		enum class Type {
			User,
			Group,
			Channel
		};
		Type type = Type::User;
		int peerIndex = 0;
		int unreadCounter = 0;
		bool muted = false;
		bool pinned = false;
		QString date;
		Ui::Text::String text;
		Status status = Status::None;
		bool selected = false;
		bool active = false;
	};
	struct Bubble {
		int width = 0;
		int height = 0;
		bool outbg = false;
		Status status = Status::None;
		QString date;
		bool attached = false;
		bool tail = true;
		Ui::Text::String text = { st::msgMinWidth };
		QVector<int> waveform;
		int waveactive = 0;
		QString wavestatus;
		QImage photo;
		int photoWidth = 0;
		int photoHeight = 0;
		Ui::Text::String replyName = { st::msgMinWidth };
		Ui::Text::String replyText = { st::msgMinWidth };
	};

	[[nodiscard]] bool extended() const;
	void prepare();

	void addRow(QString name, int peerIndex, QString date, QString text);
	void addBubble(Bubble bubble, int width, int height, QString date, Status status);
	void addAudioBubble(QVector<int> waveform, int waveactive, QString wavestatus, QString date, Status status);
	void addTextBubble(QString text, QString date, Status status);
	void addDateBubble(QString date);
	void addPhotoBubble(QString image, QString caption, QString date, Status status);
	QSize computeSkipBlock(Status status, QString date);
	int computeInfoWidth(Status status, QString date);

	void generateData();

	void paintHistoryList();
	void paintHistoryBackground();
	void paintTopBar();
	void paintComposeArea();
	void paintDialogs();
	void paintDialogsList();
	void paintHistoryShadows();
	void paintRow(const Row &row);
	void paintBubble(const Bubble &bubble);
	void paintService(QString text);

	void paintUserpic(int x, int y, Row::Type type, int index, QString letters);

	void setTextPalette(const style::TextPalette &st);
	void restoreTextPalette();

	const Instance &_theme;
	const style::palette &_palette;
	const CurrentData _current;
	const PreviewType _type;
	Painter *_p = nullptr;

	QRect _rect;
	QRect _inner;
	QRect _body;
	QRect _dialogs;
	QRect _dialogsList;
	QRect _topBar;
	QRect _composeArea;
	QRect _history;

	int _rowsTop = 0;
	std::vector<Row> _rows;

	Ui::Text::String _topBarName;
	QString _topBarStatus;
	bool _topBarStatusActive = false;

	int _historyBottom = 0;
	std::vector<Bubble> _bubbles;

	style::TextPalette _textPalette;

};

bool Generator::extended() const {
	return (_type == PreviewType::Extended);
}

void Generator::prepare() {
	const auto size = extended()
		? QRect(
			QPoint(),
			st::themePreviewSize).marginsAdded(st::themePreviewMargin).size()
		: st::themePreviewSize;
	_rect = QRect(QPoint(), size);
	_inner = extended() ? _rect.marginsRemoved(st::themePreviewMargin) : _rect;
	_body = extended() ? _inner.marginsRemoved(QMargins(0, Platform::PreviewTitleHeight(), 0, 0)) : _inner;
	_dialogs = QRect(_body.x(), _body.y(), st::themePreviewDialogsWidth, _body.height());
	_dialogsList = _dialogs.marginsRemoved(QMargins(0, st::dialogsFilterPadding.y() + st::dialogsMenuToggle.height + st::dialogsFilterPadding.y(), 0, st::dialogsPadding.y()));
	_topBar = QRect(_dialogs.x() + _dialogs.width(), _dialogs.y(), _body.width() - _dialogs.width(), st::topBarHeight);
	_composeArea = QRect(_topBar.x(), _body.y() + _body.height() - st::historySendSize.height(), _topBar.width(), st::historySendSize.height());
	_history = QRect(_topBar.x(), _topBar.y() + _topBar.height(), _topBar.width(), _body.height() - _topBar.height() - _composeArea.height());

	generateData();
}

void Generator::addRow(QString name, int peerIndex, QString date, QString text) {
	Row row;
	row.name.setText(st::msgNameStyle, name, Ui::NameTextOptions());

	row.letters = fillLetters(name);

	row.peerIndex = peerIndex;
	row.date = date;
	row.text.setRichText(st::dialogsTextStyle, text, Ui::DialogTextOptions());
	_rows.push_back(std::move(row));
}

void Generator::addBubble(Bubble bubble, int width, int height, QString date, Status status) {
	bubble.width = width;
	bubble.height = height;
	bubble.date = date;
	bubble.status = status;
	_bubbles.push_back(std::move(bubble));
}

void Generator::addAudioBubble(QVector<int> waveform, int waveactive, QString wavestatus, QString date, Status status) {
	Bubble bubble;
	bubble.waveform = waveform;
	bubble.waveactive = waveactive;
	bubble.wavestatus = wavestatus;

	auto skipBlock = computeSkipBlock(status, date);

	auto width = st::msgFileMinWidth;
	auto tleft = 0, tright = 0;
	const auto &st = st::msgFileLayout;
	tleft = st.padding.left() + st.thumbSize + st.padding.right();
	tright = st.padding.left();
	accumulate_max(width, tleft + st::normalFont->width(wavestatus) + skipBlock.width() + st::msgPadding.right());
	accumulate_min(width, st::msgMaxWidth);

	auto height = st.padding.top() + st.thumbSize + st.padding.bottom();
	addBubble(std::move(bubble), width, height, date, status);
}

QSize Generator::computeSkipBlock(Status status, QString date) {
	auto infoWidth = computeInfoWidth(status, date);
	auto width = st::msgDateSpace + infoWidth - st::msgDateDelta.x();
	auto height = st::msgDateFont->height - st::msgDateDelta.y();
	return QSize(width, height);
}

int Generator::computeInfoWidth(Status status, QString date) {
	auto result = st::msgDateFont->width(date);
	if (status != Status::None) {
		result += st::historySendStateSpace;
	}
	return result;
}

void Generator::addTextBubble(QString text, QString date, Status status) {
	Bubble bubble;
	auto skipBlock = computeSkipBlock(status, date);
	bubble.text.setRichText(st::messageTextStyle, text + textcmdSkipBlock(skipBlock.width(), skipBlock.height()), Ui::ItemTextDefaultOptions());

	auto width = _history.width() - st::msgMargin.left() - st::msgMargin.right();
	accumulate_min(width, st::msgPadding.left() + bubble.text.maxWidth() + st::msgPadding.right());
	accumulate_min(width, st::msgMaxWidth);

	auto textWidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 1);
	auto textHeight = bubble.text.countHeight(textWidth);

	auto height = st::msgPadding.top() + textHeight + st::msgPadding.bottom();
	addBubble(std::move(bubble), width, height, date, status);
}

void Generator::addDateBubble(QString date) {
	Bubble bubble;
	addBubble(std::move(bubble), 0, 0, date, Status::None);
}

void Generator::addPhotoBubble(QString image, QString caption, QString date, Status status) {
	Bubble bubble;
	bubble.photo.load(image);
	bubble.photoWidth = style::ConvertScale(bubble.photo.width() / 2);
	bubble.photoHeight = style::ConvertScale(bubble.photo.height() / 2);
	auto skipBlock = computeSkipBlock(status, date);
	bubble.text.setRichText(st::messageTextStyle, caption + textcmdSkipBlock(skipBlock.width(), skipBlock.height()), Ui::ItemTextDefaultOptions());

	auto width = _history.width() - st::msgMargin.left() - st::msgMargin.right();
	accumulate_min(width, bubble.photoWidth);
	accumulate_min(width, st::msgMaxWidth);

	auto textWidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 1);
	auto textHeight = bubble.text.countHeight(textWidth);

	auto height = st::mediaCaptionSkip + textHeight + st::msgPadding.bottom();
	addBubble(std::move(bubble), width, height, date, status);
}

void Generator::generateData() {
	_rows.reserve(9);
	addRow("Eva Summer", 0, "11:00", "Reminds me of a Chinese proverb: the best time to plant a tree was 20 years ago.");
	_rows.back().active = true;
	_rows.back().pinned = true;
	addRow("Alexandra Smith", 7, "10:00", "This is amazing!");
	_rows.back().unreadCounter = 2;
	addRow("Mike Apple", 2, "9:00", textcmdLink(1, QChar(55357) + QString() + QChar(56836) + " Sticker"));
	_rows.back().unreadCounter = 2;
	_rows.back().muted = true;
	addRow("Evening Club", 1, "8:00", textcmdLink(1, "Eva: Photo"));
	_rows.back().type = Row::Type::Group;
	addRow("Old Pirates", 6, "7:00", textcmdLink(1, "Max:") + " Yo-ho-ho!");
	_rows.back().type = Row::Type::Group;
	addRow("Max Bright", 3, "6:00", "How about some coffee?");
	_rows.back().status = Status::Received;
	addRow("Natalie Parker", 4, "5:00", "OK, great)");
	_rows.back().status = Status::Received;
	addRow("Davy Jones", 5, "4:00", textcmdLink(1, "Keynote.pdf"));

	_topBarName.setText(st::msgNameStyle, "Eva Summer", Ui::NameTextOptions());
	_topBarStatus = "online";
	_topBarStatusActive = true;

	addPhotoBubble(":/gui/art/sunrise.jpg", "Nearly missed this sunrise", "7:00", Status::None);
	int wavedata[] = { 0, 0, 0, 0, 27, 31, 4, 1, 0, 0, 23, 30, 18, 9, 7, 19, 4, 2, 2, 2, 0, 0, 15, 15, 15, 15, 3, 15, 19, 3, 2, 0, 0, 0, 0, 0, 3, 12, 16, 6, 4, 6, 14, 12, 2, 12, 12, 11, 3, 0, 7, 5, 7, 4, 7, 5, 2, 4, 0, 9, 5, 7, 6, 2, 2, 0, 0 };
	auto waveform = QVector<int>(base::array_size(wavedata));
	memcpy(waveform.data(), wavedata, sizeof(wavedata));
	addAudioBubble(waveform, 33, "0:07", "8:00", Status::None);
	_bubbles.back().outbg = true;
	_bubbles.back().status = Status::Received;
	addDateBubble("December 26");
	addTextBubble("Twenty years from now you will be more disappointed by the things that you didn't do than by the ones you did do, so throw off the bowlines, sail away from safe harbor, catch the trade winds in your sails.", "10:00", Status::Received);
	_bubbles.back().tail = false;
	_bubbles.back().outbg = true;
	addTextBubble("Mark Twain said that " + QString() + QChar(9757) + QChar(55356) + QChar(57339), "10:00", Status::Received);
	_bubbles.back().outbg = true;
	_bubbles.back().attached = true;
	_bubbles.back().tail = true;
	addTextBubble("Reminds me of a Chinese proverb: the best time to plant a tree was 20 years ago. The second best time is now.", "11:00", Status::None);
	_bubbles.back().replyName.setText(st::msgNameStyle, "Alex Cassio", Ui::NameTextOptions());
	_bubbles.back().replyText.setText(st::messageTextStyle, "Mark Twain said that " + QString() + QChar(9757) + QChar(55356) + QChar(57339), Ui::DialogTextOptions());
}

Generator::Generator(
	const Instance &theme,
	CurrentData &&current,
	PreviewType type)
: _theme(theme)
, _palette(_theme.palette)
, _current(std::move(current))
, _type(type) {
}

QImage Generator::generate() {
	prepare();

	auto result = QImage(
		_rect.size() * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(cRetinaFactor());
	result.fill(st::themePreviewBg->c);

	{
		Painter p(&result);
		PainterHighQualityEnabler hq(p);
		_p = &p;

		_p->fillRect(_body, QColor(0, 0, 0));
		_p->fillRect(_body, st::windowBg[_palette]);

		paintHistoryList();
		paintTopBar();
		paintComposeArea();
		paintDialogs();
		paintHistoryShadows();
	}
	if (extended()) {
		Platform::PreviewWindowFramePaint(result, _palette, _body, _rect.width());
	}

	return result;
}

void Generator::paintHistoryList() {
	paintHistoryBackground();

	_historyBottom = _history.y() + _history.height();
	_historyBottom -= st::historyPaddingBottom;
	_p->setClipping(true);
	for (auto i = _bubbles.size(); i != 0;) {
		auto &bubble = _bubbles[--i];
		if (bubble.width > 0) {
			paintBubble(bubble);
		} else {
			paintService(bubble.date);
		}
	}

	_p->setClipping(false);
}

void Generator::paintHistoryBackground() {
	auto fromy = (-st::topBarHeight);
	auto background = _theme.background;
	auto tiled = _theme.tiled;
	if (background.isNull()) {
		const auto fakePaper = Data::WallPaper(_current.backgroundId);
		if (Data::IsThemeWallPaper(fakePaper)) {
			background.load(qsl(":/gui/art/bg.jpg"));
			tiled = false;
		} else {
			background = std::move(_current.backgroundImage);
			tiled = _current.backgroundTiled;
		}
	}
	background = std::move(background).convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	background.setDevicePixelRatio(cRetinaFactor());
	_p->setClipRect(_history);
	if (tiled) {
		auto width = background.width();
		auto height = background.height();
		auto repeatTimesX = qCeil(_history.width() * cIntRetinaFactor() / float64(width));
		auto repeatTimesY = qCeil((_history.height() - fromy) * cIntRetinaFactor() / float64(height));
		auto imageForTiled = QImage(
			width * repeatTimesX,
			height * repeatTimesY,
			QImage::Format_ARGB32_Premultiplied);
		imageForTiled.setDevicePixelRatio(background.devicePixelRatio());
		auto imageForTiledBytes = imageForTiled.bits();
		auto bytesInLine = width * sizeof(uint32);
		for (auto timesY = 0; timesY != repeatTimesY; ++timesY) {
			auto imageBytes = background.constBits();
			for (auto y = 0; y != height; ++y) {
				for (auto timesX = 0; timesX != repeatTimesX; ++timesX) {
					memcpy(imageForTiledBytes, imageBytes, bytesInLine);
					imageForTiledBytes += bytesInLine;
				}
				imageBytes += background.bytesPerLine();
				imageForTiledBytes += imageForTiled.bytesPerLine()
					- (repeatTimesX * bytesInLine);
			}
		}
		_p->drawImage(_history.x(), _history.y() + fromy, imageForTiled);
	} else {
		PainterHighQualityEnabler hq(*_p);

		auto fill = QRect(_topBar.x(), _topBar.y(), _topBar.width(), _body.height());
		QRect to, from;
		ComputeBackgroundRects(fill, background.size(), to, from);
		to.moveTop(to.top() + fromy);
		to.moveTopLeft(to.topLeft() + _history.topLeft());
		_p->drawImage(to, background, from);
	}
	_p->setClipping(false);
}

void Generator::paintTopBar() {
	_p->fillRect(_topBar, st::topBarBg[_palette]);

	auto right = st::topBarMenuToggle.width;
	st::topBarMenuToggle.icon[_palette].paint(*_p, _topBar.x() + _topBar.width() - right + st::topBarMenuToggle.iconPosition.x(), _topBar.y() + st::topBarMenuToggle.iconPosition.y(), _rect.width());
	right += st::topBarSearch.width;
	st::topBarSearch.icon[_palette].paint(*_p, _topBar.x() + _topBar.width() - right + st::topBarSearch.iconPosition.x(), _topBar.y() + st::topBarSearch.iconPosition.y(), _rect.width());
	right += st::topBarCallSkip + st::topBarCall.width;
	st::topBarCall.icon[_palette].paint(*_p, _topBar.x() + _topBar.width() - right + st::topBarCall.iconPosition.x(), _topBar.y() + st::topBarCall.iconPosition.y(), _rect.width());

	auto decreaseWidth = st::topBarCall.width + st::topBarCallSkip + st::topBarSearch.width + st::topBarMenuToggle.width;
	auto nameleft = _topBar.x() + st::topBarArrowPadding.right();
	auto nametop = _topBar.y() + st::topBarArrowPadding.top();
	auto statustop = _topBar.y() + st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	auto namewidth = _topBar.x() + _topBar.width() - decreaseWidth - nameleft - st::topBarArrowPadding.right();
	_p->setFont(st::dialogsTextFont);
	_p->setPen(_topBarStatusActive ? st::historyStatusFgActive[_palette] : st::historyStatusFg[_palette]);
	_p->drawText(nameleft, statustop + st::dialogsTextFont->ascent, _topBarStatus);

	_p->setPen(st::dialogsNameFg[_palette]);
	_topBarName.drawElided(*_p, nameleft, nametop, namewidth);
}

void Generator::paintComposeArea() {
	_p->fillRect(_composeArea, st::historyReplyBg[_palette]);

	auto controlsTop = _composeArea.y() + _composeArea.height() - st::historySendSize.height();
	st::historyAttach.icon[_palette].paint(*_p, _composeArea.x() + st::historyAttach.iconPosition.x(), controlsTop + st::historyAttach.iconPosition.y(), _rect.width());
	auto right = st::historySendRight + st::historySendSize.width();
	st::historyRecordVoice[_palette].paintInCenter(*_p, QRect(_composeArea.x() + _composeArea.width() - right, controlsTop, st::historySendSize.width(), st::historySendSize.height()));

	const auto emojiIconLeft = (st::historyAttachEmoji.iconPosition.x() < 0)
		? ((st::historyAttachEmoji.width - st::historyAttachEmoji.icon.width()) / 2)
		: st::historyAttachEmoji.iconPosition.x();
	const auto emojiIconTop = (st::historyAttachEmoji.iconPosition.y() < 0)
		? ((st::historyAttachEmoji.height - st::historyAttachEmoji.icon.height()) / 2)
		: st::historyAttachEmoji.iconPosition.y();
	right += st::historyAttachEmoji.width;
	auto attachEmojiLeft = _composeArea.x() + _composeArea.width() - right;
	_p->fillRect(attachEmojiLeft, controlsTop, st::historyAttachEmoji.width, st::historyAttachEmoji.height, st::historyComposeAreaBg[_palette]);
	st::historyAttachEmoji.icon[_palette].paint(*_p, attachEmojiLeft + emojiIconLeft, controlsTop + emojiIconTop, _rect.width());

	auto pen = st::historyEmojiCircleFg[_palette]->p;
	pen.setWidth(st::historyEmojiCircleLine);
	pen.setCapStyle(Qt::RoundCap);
	_p->setPen(pen);
	_p->setBrush(Qt::NoBrush);

	PainterHighQualityEnabler hq(*_p);
	auto inner = QRect(QPoint(attachEmojiLeft + (st::historyAttachEmoji.width - st::historyEmojiCircle.width()) / 2, controlsTop + st::historyEmojiCircleTop), st::historyEmojiCircle);
	_p->drawEllipse(inner);

	auto fieldLeft = _composeArea.x() + st::historyAttach.width;
	auto fieldTop = _composeArea.y() + _composeArea.height() - st::historyAttach.height + st::historySendPadding;
	auto fieldWidth = _composeArea.width() - st::historyAttach.width - st::historySendSize.width() - st::historySendRight - st::historyAttachEmoji.width;
	auto fieldHeight = st::historySendSize.height() - 2 * st::historySendPadding;
	auto field = QRect(fieldLeft, fieldTop, fieldWidth, fieldHeight);
	_p->fillRect(field, st::historyComposeField.textBg[_palette]);

	_p->save();
	_p->setClipRect(field);
	_p->setFont(st::historyComposeField.font);
	_p->setPen(st::historyComposeField.placeholderFg[_palette]);

	auto placeholderRect = QRect(
		field.x() + st::historyComposeField.textMargins.left() + st::historyComposeField.placeholderMargins.left(),
		field.y() + st::historyComposeField.textMargins.top() + st::historyComposeField.placeholderMargins.top(),
		field.width() - st::historyComposeField.textMargins.left() - st::historyComposeField.textMargins.right(),
		field.height() - st::historyComposeField.textMargins.top() - st::historyComposeField.textMargins.bottom());
	_p->drawText(placeholderRect, tr::lng_message_ph(tr::now), QTextOption(st::historyComposeField.placeholderAlign));

	_p->restore();
	_p->setClipping(false);
}

void Generator::paintDialogs() {
	_p->fillRect(_dialogs, st::dialogsBg[_palette]);

	st::dialogsMenuToggle.icon[_palette].paint(*_p, _dialogs.x() + st::dialogsFilterPadding.x() + st::dialogsMenuToggle.iconPosition.x(), _dialogs.y() + st::dialogsFilterPadding.y() + st::dialogsMenuToggle.iconPosition.y(), _rect.width());

	auto filterLeft = _dialogs.x() + st::dialogsFilterPadding.x() + st::dialogsMenuToggle.width + st::dialogsFilterPadding.x();
	auto filterRight = st::dialogsFilterSkip + st::dialogsFilterPadding.x();
	auto filterWidth = _dialogs.x() + _dialogs.width() - filterLeft - filterRight;
	auto filterAreaHeight = st::topBarHeight;
	auto filterTop = _dialogs.y() + (filterAreaHeight - st::dialogsFilter.height) / 2;
	auto filter = QRect(filterLeft, filterTop, filterWidth, st::dialogsFilter.height);

	auto pen = st::dialogsFilter.borderColor[_palette]->p;
	pen.setWidth(st::dialogsFilter.borderWidth);
	_p->setPen(pen);
	_p->setBrush(st::dialogsFilter.bgColor[_palette]);
	{
		PainterHighQualityEnabler hq(*_p);
		_p->drawRoundedRect(QRectF(filter).marginsRemoved(QMarginsF(st::dialogsFilter.borderWidth / 2., st::dialogsFilter.borderWidth / 2., st::dialogsFilter.borderWidth / 2., st::dialogsFilter.borderWidth / 2.)), st::roundRadiusSmall - (st::dialogsFilter.borderWidth / 2.), st::roundRadiusSmall - (st::dialogsFilter.borderWidth / 2.));
	}

	if (!st::dialogsFilter.icon.empty()) {
		st::dialogsFilter.icon[_palette].paint(*_p, filter.x(), filter.y(), _rect.width());
	}

	_p->save();
	_p->setClipRect(filter);
	auto phRect = QRect(filter.x() + st::dialogsFilter.textMrg.left() + st::dialogsFilter.phPos.x(), filter.y() + st::dialogsFilter.textMrg.top() + st::dialogsFilter.phPos.y(), filter.width() - st::dialogsFilter.textMrg.left() - st::dialogsFilter.textMrg.right(), filter.height() - st::dialogsFilter.textMrg.top() - st::dialogsFilter.textMrg.bottom());;
	_p->setFont(st::dialogsFilter.font);
	_p->setPen(st::dialogsFilter.phColor[_palette]);
	_p->drawText(phRect, tr::lng_dlg_filter(tr::now), QTextOption(st::dialogsFilter.phAlign));
	_p->restore();
	_p->setClipping(false);

	paintDialogsList();
}

void Generator::paintDialogsList() {
	_p->setClipRect(_dialogsList);
	_rowsTop = _dialogsList.y();
	for (auto &row : _rows) {
		paintRow(row);
		_rowsTop += st::dialogsRowHeight;
	}
	_p->setClipping(false);
}

void Generator::paintRow(const Row &row) {
	auto x = _dialogsList.x();
	auto y = _rowsTop;
	auto fullWidth = _dialogsList.width();
	auto fullRect = QRect(x, y, fullWidth, st::dialogsRowHeight);
	if (row.active || row.selected) {
		_p->fillRect(fullRect, row.active ? st::dialogsBgActive[_palette] : st::dialogsBgOver[_palette]);
	}
	paintUserpic(x + st::dialogsPadding.x(), y + st::dialogsPadding.y(), row.type, row.peerIndex, row.letters);

	auto nameleft = x + st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPhotoPadding;
	auto namewidth = x + fullWidth - nameleft - st::dialogsPadding.x();
	auto rectForName = QRect(nameleft, y + st::dialogsPadding.y() + st::dialogsNameTop, namewidth, st::msgNameFont->height);

	auto chatTypeIcon = ([&row]() -> const style::icon * {
		if (row.type == Row::Type::Group) {
			return &(row.active ? st::dialogsChatIconActive : (row.selected ? st::dialogsChatIconOver : st::dialogsChatIcon));
		} else if (row.type == Row::Type::Channel) {
			return &(row.active ? st::dialogsChannelIconActive : (row.selected ? st::dialogsChannelIconOver : st::dialogsChannelIcon));
		}
		return nullptr;
	})();
	if (chatTypeIcon) {
		(*chatTypeIcon)[_palette].paint(*_p, rectForName.topLeft(), fullWidth);
		rectForName.setLeft(rectForName.left() + st::dialogsChatTypeSkip);
	}

	auto texttop = y + st::dialogsPadding.y() + st::msgNameFont->height + st::dialogsSkip;

	auto dateWidth = st::dialogsDateFont->width(row.date);
	rectForName.setWidth(rectForName.width() - dateWidth - st::dialogsDateSkip);
	_p->setFont(st::dialogsDateFont);
	_p->setPen(row.active ? st::dialogsDateFgActive[_palette] : (row.selected ? st::dialogsDateFgOver[_palette] : st::dialogsDateFg[_palette]));
	_p->drawText(rectForName.left() + rectForName.width() + st::dialogsDateSkip, rectForName.top() + st::msgNameFont->height - st::msgDateFont->descent, row.date);

	auto availableWidth = namewidth;
	if (row.unreadCounter) {
		auto counter = QString::number(row.unreadCounter);
		auto unreadRight = x + fullWidth - st::dialogsPadding.x();
		auto unreadTop = texttop + st::dialogsTextFont->ascent - st::dialogsUnreadFont->ascent - (st::dialogsUnreadHeight - st::dialogsUnreadFont->height) / 2;

		auto unreadWidth = st::dialogsUnreadFont->width(counter);
		auto unreadRectWidth = unreadWidth + 2 * st::dialogsUnreadPadding;
		auto unreadRectHeight = st::dialogsUnreadHeight;
		accumulate_max(unreadRectWidth, unreadRectHeight);

		auto unreadRectLeft = unreadRight - unreadRectWidth;
		auto unreadRectTop = unreadTop;
		availableWidth -= unreadRectWidth + st::dialogsUnreadPadding;

		style::color bg[] = {
			st::dialogsUnreadBg,
			st::dialogsUnreadBgOver,
			st::dialogsUnreadBgActive,
			st::dialogsUnreadBgMuted,
			st::dialogsUnreadBgMutedOver,
			st::dialogsUnreadBgMutedActive
		};

		auto index = (row.active ? 2 : row.selected ? 1 : 0) + (row.muted ? 3 : 0);
		_p->setPen(Qt::NoPen);
		_p->setBrush(bg[index][_palette]);
		_p->drawRoundedRect(QRectF(unreadRectLeft, unreadRectTop, unreadRectWidth, unreadRectHeight), unreadRectHeight / 2., unreadRectHeight / 2.);

		auto textTop = (unreadRectHeight - st::dialogsUnreadFont->height) / 2;
		_p->setFont(st::dialogsUnreadFont);
		_p->setPen(row.active ? st::dialogsUnreadFgActive[_palette] : (row.selected ? st::dialogsUnreadFgOver[_palette] : st::dialogsUnreadFg[_palette]));
		_p->drawText(unreadRectLeft + (unreadRectWidth - unreadWidth) / 2, unreadRectTop + textTop + st::dialogsUnreadFont->ascent, counter);
	} else if (row.pinned) {
		auto icon = (row.active ? st::dialogsPinnedIconActive[_palette] : (row.selected ? st::dialogsPinnedIconOver[_palette] : st::dialogsPinnedIcon[_palette]));
		icon.paint(*_p, x + fullWidth - st::dialogsPadding.x() - icon.width(), texttop, fullWidth);
		availableWidth -= icon.width() + st::dialogsUnreadPadding;
	}
	auto textRect = QRect(nameleft, texttop, availableWidth, st::dialogsTextFont->height);
	setTextPalette(row.active ? st::dialogsTextPaletteActive : (row.selected ? st::dialogsTextPaletteOver : st::dialogsTextPalette));
	_p->setFont(st::dialogsTextFont);
	_p->setPen(row.active ? st::dialogsTextFgActive[_palette] : (row.selected ? st::dialogsTextFgOver[_palette] : st::dialogsTextFg[_palette]));
	row.text.drawElided(*_p, textRect.left(), textRect.top(), textRect.width(), textRect.height() / st::dialogsTextFont->height);
	restoreTextPalette();

	auto sendStateIcon = ([&row]() -> const style::icon* {
		if (row.status == Status::Sent) {
			return &(row.active ? st::dialogsSentIconActive : (row.selected ? st::dialogsSentIconOver : st::dialogsSentIcon));
		} else if (row.status == Status::Received) {
			return &(row.active ? st::dialogsReceivedIconActive : (row.selected ? st::dialogsReceivedIconOver : st::dialogsReceivedIcon));
		}
		return nullptr;
	})();
	if (sendStateIcon) {
		rectForName.setWidth(rectForName.width() - st::dialogsSendStateSkip);
		(*sendStateIcon)[_palette].paint(*_p, rectForName.topLeft() + QPoint(rectForName.width(), 0), fullWidth);
	}
	_p->setPen(row.active ? st::dialogsNameFgActive[_palette] : (row.selected ? st::dialogsNameFgOver[_palette] : st::dialogsNameFg[_palette]));
	row.name.drawElided(*_p, rectForName.left(), rectForName.top(), rectForName.width());
}

void Generator::paintBubble(const Bubble &bubble) {
	auto height = bubble.height;
	if (!bubble.replyName.isEmpty()) {
		height += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
	}
	auto isPhoto = !bubble.photo.isNull();

	auto x = _history.x();
	auto y = _historyBottom - st::msgMargin.bottom() - height;
	auto bubbleTop = y;
	auto bubbleHeight = height;
	if (isPhoto) {
		bubbleTop -= st::historyMessageRadius + 1;
		bubbleHeight += st::historyMessageRadius + 1;
	}

	auto left = bubble.outbg ? st::msgMargin.right() : st::msgMargin.left();
	if (bubble.outbg) {
		left += _history.width() - st::msgMargin.left() - st::msgMargin.right() - bubble.width;
	}
	x += left;

	_p->setPen(Qt::NoPen);
	auto tailclip = st::historyMessageRadius + 1;
	if (bubble.tail) {
		if (bubble.outbg) {
			_p->setClipRegion(QRegion(_history) - QRect(x + bubble.width - tailclip, bubbleTop + bubbleHeight - tailclip, tailclip + st::historyMessageRadius, tailclip + st::historyMessageRadius));
		} else {
			_p->setClipRegion(QRegion(_history) - QRect(x - st::historyMessageRadius, bubbleTop + bubbleHeight - tailclip, tailclip + st::historyMessageRadius, tailclip + st::historyMessageRadius));
		}
	}
	auto sh = bubble.outbg ? st::msgOutShadow[_palette] : st::msgInShadow[_palette];
	_p->setBrush(sh);
	_p->drawRoundedRect(x, bubbleTop + st::msgShadow, bubble.width, bubbleHeight, st::historyMessageRadius, st::historyMessageRadius);
	auto bg = bubble.outbg ? st::msgOutBg[_palette] : st::msgInBg[_palette];
	_p->setBrush(bg);
	_p->drawRoundedRect(x, bubbleTop, bubble.width, bubbleHeight, st::historyMessageRadius, st::historyMessageRadius);
	if (bubble.tail) {
		_p->setClipRect(_history);
		if (bubble.outbg) {
			_p->fillRect(QRect(x + bubble.width - tailclip, bubbleTop + bubbleHeight - tailclip, tailclip, tailclip), bg);
			_p->fillRect(QRect(x + bubble.width - tailclip, bubbleTop + bubbleHeight, tailclip + st::historyBubbleTailOutRight.width(), st::msgShadow), sh);
			st::historyBubbleTailOutRight[_palette].paint(*_p, x + bubble.width, bubbleTop + bubbleHeight - st::historyBubbleTailOutRight.height(), _rect.width());
		} else {
			_p->fillRect(QRect(x, bubbleTop + bubbleHeight - tailclip, tailclip, tailclip), bg);
			_p->fillRect(QRect(x - st::historyBubbleTailInLeft.width(), bubbleTop + bubbleHeight, tailclip + st::historyBubbleTailInLeft.width(), st::msgShadow), sh);
			st::historyBubbleTailInLeft[_palette].paint(*_p, x - st::historyBubbleTailInLeft.width(), bubbleTop + bubbleHeight - st::historyBubbleTailOutRight.height(), _rect.width());
		}
	}

	auto trect = QRect(x, y, bubble.width, bubble.height);
	if (isPhoto) {
		trect = trect.marginsRemoved(QMargins(st::msgPadding.left(), st::mediaCaptionSkip, st::msgPadding.right(), st::msgPadding.bottom()));
	} else {
		trect = trect.marginsRemoved(st::msgPadding);
	}
	if (!bubble.replyName.isEmpty()) {
		auto h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();

		auto bar = (bubble.outbg ? st::msgOutReplyBarColor[_palette] : st::msgInReplyBarColor[_palette]);
		auto rbar = style::rtlrect(trect.x() + st::msgReplyBarPos.x(), trect.y() + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), _rect.width());
		_p->fillRect(rbar, bar);

		_p->setPen(bubble.outbg ? st::msgOutServiceFg[_palette] : st::msgInServiceFg[_palette]);
		bubble.replyName.drawLeftElided(*_p, trect.x() + st::msgReplyBarSkip, trect.y() + st::msgReplyPadding.top(), bubble.width - st::msgReplyBarSkip, _rect.width());

		_p->setPen(bubble.outbg ? st::historyTextOutFg[_palette] : st::historyTextInFg[_palette]);
		bubble.replyText.drawLeftElided(*_p, trect.x() + st::msgReplyBarSkip, trect.y() + st::msgReplyPadding.top() + st::msgServiceNameFont->height, bubble.width - st::msgReplyBarSkip, _rect.width());

		trect.setY(trect.y() + h);
	}

	if (!bubble.text.isEmpty()) {
		setTextPalette(bubble.outbg ? st::outTextPalette : st::inTextPalette);
		_p->setPen(bubble.outbg ? st::historyTextOutFg[_palette] : st::historyTextInFg[_palette]);
		_p->setFont(st::msgFont);
		bubble.text.draw(*_p, trect.x(), trect.y(), trect.width());
	} else if (!bubble.waveform.isEmpty()) {
		const auto &st = st::msgFileLayout;
		auto nameleft = x + st.padding.left() + st.thumbSize + st.padding.right();
		auto nametop = y + st.nameTop;
		auto nameright = st.padding.left();
		auto statustop = y + st.statusTop;
		auto bottom = y + st.padding.top() + st.thumbSize + st.padding.bottom();

		auto inner = style::rtlrect(x + st.padding.left(), y + st.padding.top(), st.thumbSize, st.thumbSize, _rect.width());
		_p->setPen(Qt::NoPen);
		_p->setBrush(bubble.outbg ? st::msgFileOutBg[_palette] : st::msgFileInBg[_palette]);

		_p->drawEllipse(inner);

		auto icon = ([&bubble] {
			return &(bubble.outbg ? st::historyFileOutPlay : st::historyFileInPlay);
		})();
		(*icon)[_palette].paintInCenter(*_p, inner);

		auto namewidth = x + bubble.width - nameleft - nameright;

		// rescale waveform by going in waveform.size * bar_count 1D grid
		auto active = bubble.outbg ? st::msgWaveformOutActive[_palette] : st::msgWaveformInActive[_palette];
		auto inactive = bubble.outbg ? st::msgWaveformOutInactive[_palette] : st::msgWaveformInInactive[_palette];
		auto wf_size = bubble.waveform.size();
		auto availw = namewidth + st::msgWaveformSkip;
		auto bar_count = qMin(availw / (st::msgWaveformBar + st::msgWaveformSkip), wf_size);
		auto max_value = 0;
		auto max_delta = st::msgWaveformMax - st::msgWaveformMin;
		auto wave_bottom = y + st::msgFileLayout.padding.top() + st::msgWaveformMax;
		_p->setPen(Qt::NoPen);
		auto norm_value = uchar(31);
		for (auto i = 0, bar_x = 0, sum_i = 0; i < wf_size; ++i) {
			auto value = bubble.waveform[i];
			if (sum_i + bar_count >= wf_size) { // draw bar
				sum_i = sum_i + bar_count - wf_size;
				if (sum_i < (bar_count + 1) / 2) {
					if (max_value < value) max_value = value;
				}
				auto bar_value = ((max_value * max_delta) + ((norm_value + 1) / 2)) / (norm_value + 1);

				if (i >= bubble.waveactive) {
					_p->fillRect(nameleft + bar_x, wave_bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, inactive);
				} else {
					_p->fillRect(nameleft + bar_x, wave_bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, active);
				}
				bar_x += st::msgWaveformBar + st::msgWaveformSkip;

				if (sum_i < (bar_count + 1) / 2) {
					max_value = 0;
				} else {
					max_value = value;
				}
			} else {
				if (max_value < value) max_value = value;

				sum_i += bar_count;
			}
		}

		auto status = bubble.outbg ? st::mediaOutFg[_palette] : st::mediaInFg[_palette];
		_p->setFont(st::normalFont);
		_p->setPen(status);
		_p->drawTextLeft(nameleft, statustop, _rect.width(), bubble.wavestatus);
	}

	_p->setFont(st::msgDateFont);
	auto infoRight = x + bubble.width - st::msgPadding.right() + st::msgDateDelta.x();
	auto infoBottom = y + height - st::msgPadding.bottom() + st::msgDateDelta.y();
	_p->setPen(bubble.outbg ? st::msgOutDateFg[_palette] : st::msgInDateFg[_palette]);
	auto infoWidth = computeInfoWidth(bubble.status, bubble.date);

	auto dateX = infoRight - infoWidth;
	auto dateY = infoBottom - st::msgDateFont->height;
	_p->drawText(dateX, dateY + st::msgDateFont->ascent, bubble.date);
	auto icon = ([&bubble]() -> const style::icon * {
		if (bubble.status == Status::Sent) {
			return &st::historySentIcon;
		} else if (bubble.status == Status::Received) {
			return &st::historyReceivedIcon;
		}
		return nullptr;
	})();
	if (icon) {
		(*icon)[_palette].paint(*_p, QPoint(infoRight, infoBottom) + st::historySendStatePosition, _rect.width());
	}

	_historyBottom = y - (bubble.attached ? st::msgMarginTopAttached : st::msgMargin.top());

	if (isPhoto) {
		auto image = bubble.photo.scaled(bubble.photoWidth * cIntRetinaFactor(), bubble.photoHeight * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		image.setDevicePixelRatio(cRetinaFactor());
		_p->drawImage(x, y - bubble.photoHeight, image);
		_historyBottom -= bubble.photoHeight;
	}
}

void Generator::paintService(QString text) {
	auto bubbleHeight = st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom();
	auto bubbleTop = _historyBottom - st::msgServiceMargin.bottom() - bubbleHeight;
	auto textWidth = st::msgServiceFont->width(text);
	auto bubbleWidth = st::msgServicePadding.left() + textWidth + st::msgServicePadding.right();
	auto radius = bubbleHeight / 2;
	_p->setPen(Qt::NoPen);
	_p->setBrush(st::msgServiceBg[_palette]);
	auto bubbleLeft = _history.x() + (_history.width() - bubbleWidth) / 2;
	_p->drawRoundedRect(bubbleLeft, bubbleTop, bubbleWidth, bubbleHeight, radius, radius);
	_p->setPen(st::msgServiceFg[_palette]);
	_p->setFont(st::msgServiceFont);
	_p->drawText(bubbleLeft + st::msgServicePadding.left(), bubbleTop + st::msgServicePadding.top() + st::msgServiceFont->ascent, text);
	_historyBottom = bubbleTop - st::msgServiceMargin.top();
}

void Generator::paintUserpic(int x, int y, Row::Type type, int index, QString letters) {
	style::color colors[] = {
		st::historyPeer1UserpicBg,
		st::historyPeer2UserpicBg,
		st::historyPeer3UserpicBg,
		st::historyPeer4UserpicBg,
		st::historyPeer5UserpicBg,
		st::historyPeer6UserpicBg,
		st::historyPeer7UserpicBg,
		st::historyPeer8UserpicBg,
	};
	auto color = colors[index % base::array_size(colors)];

	auto image = QImage(st::dialogsPhotoSize * cIntRetinaFactor(), st::dialogsPhotoSize * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(cRetinaFactor());
	image.fill(color[_palette]->c);
	{
		Painter p(&image);
		auto fontsize = (st::dialogsPhotoSize * 13) / 33;
		auto font = st::historyPeerUserpicFont->f;
		font.setPixelSize(fontsize);

		p.setFont(font);
		p.setBrush(Qt::NoBrush);
		p.setPen(st::historyPeerUserpicFg[_palette]);
		p.drawText(QRect(0, 0, st::dialogsPhotoSize, st::dialogsPhotoSize), letters, QTextOption(style::al_center));
	}
	Images::prepareCircle(image);
	_p->drawImage(rtl() ? (_rect.width() - x - st::dialogsPhotoSize) : x, y, image);
}

void Generator::paintHistoryShadows() {
	_p->fillRect(_history.x() + st::lineWidth, _history.y(), _history.width() - st::lineWidth, st::lineWidth, st::shadowFg[_palette]);
	_p->fillRect(_history.x() + st::lineWidth, _history.y() + _history.height() - st::lineWidth, _history.width() - st::lineWidth, st::lineWidth, st::shadowFg[_palette]);
	_p->fillRect(_history.x(), _body.y(), st::lineWidth, _body.height(), st::shadowFg[_palette]);
}

void Generator::setTextPalette(const style::TextPalette &st) {
	_textPalette.linkFg = st.linkFg[_palette].clone();
	_textPalette.monoFg = st.monoFg[_palette].clone();
	_textPalette.selectBg = st.selectBg[_palette].clone();
	_textPalette.selectFg = st.selectFg[_palette].clone();
	_textPalette.selectLinkFg = st.selectLinkFg[_palette].clone();
	_textPalette.selectMonoFg = st.selectMonoFg[_palette].clone();
	_textPalette.selectOverlay = st.selectOverlay[_palette].clone();
	_p->setTextPalette(_textPalette);
}

void Generator::restoreTextPalette() {
	_p->restoreTextPalette();
}

} // namespace

QString CachedThemePath(uint64 documentId) {
	return QString::fromLatin1("special://cached-%1").arg(documentId);
}

std::unique_ptr<Preview> PreviewFromFile(
		const QByteArray &bytes,
		const QString &filepath,
		const Data::CloudTheme &cloud) {
	auto result = std::make_unique<Preview>();
	auto &object = result->object;
	object.cloud = cloud;
	object.pathAbsolute = filepath.isEmpty()
		? CachedThemePath(cloud.documentId)
		: QFileInfo(filepath).absoluteFilePath();
	object.pathRelative = filepath.isEmpty()
		? object.pathAbsolute
		: QDir().relativeFilePath(filepath);
	const auto instance = &result->instance;
	const auto cache = &result->instance.cached;
	if (bytes.isEmpty()) {
		if (!LoadFromFile(filepath, instance, cache, &object.content)) {
			return nullptr;
		}
	} else {
		object.content = bytes;
		if (!LoadFromContent(bytes, instance, cache)) {
			return nullptr;
		}
	}
	return result;
}

std::unique_ptr<Preview> GeneratePreview(
		const QByteArray &bytes,
		const QString &filepath,
		const Data::CloudTheme &cloud,
		CurrentData &&data,
		PreviewType type) {
	auto result = PreviewFromFile(bytes, filepath, cloud);
	if (!result) {
		return nullptr;
	}
	result->preview = Generator(
		result->instance,
		std::move(data),
		type
	).generate();
	return result;
}

QImage GeneratePreview(
		const QByteArray &bytes,
		const QString &filepath) {
	const auto preview = GeneratePreview(
		bytes,
		filepath,
		Data::CloudTheme(),
		CurrentData{ Data::ThemeWallPaper().id() },
		PreviewType::Normal);
	return preview ? preview->preview : QImage();
}

int DefaultPreviewTitleHeight() {
	return st::defaultWindowTitle.height;
}

void DefaultPreviewWindowTitle(Painter &p, const style::palette &palette, QRect body, int outerWidth) {
	auto titleRect = QRect(body.x(), body.y() - st::defaultWindowTitle.height, body.width(), st::defaultWindowTitle.height);
	p.fillRect(titleRect, QColor(0, 0, 0));
	p.fillRect(titleRect, st::titleBgActive[palette]);
	auto right = st::windowTitleButtonClose.width;
	st::windowTitleButtonClose.icon[palette].paint(p, titleRect.x() + titleRect.width() - right + st::windowTitleButtonClose.iconPosition.x(), titleRect.y() + st::windowTitleButtonClose.iconPosition.y(), outerWidth);
	right += st::defaultWindowTitle.maximize.width;
	st::defaultWindowTitle.maximize.icon[palette].paint(p, titleRect.x() + titleRect.width() - right + st::defaultWindowTitle.maximize.iconPosition.x(), titleRect.y() + st::defaultWindowTitle.maximize.iconPosition.y(), outerWidth);
	right += st::defaultWindowTitle.minimize.width;
	st::defaultWindowTitle.minimize.icon[palette].paint(p, titleRect.x() + titleRect.width() - right + st::defaultWindowTitle.minimize.iconPosition.x(), titleRect.y() + st::defaultWindowTitle.minimize.iconPosition.y(), outerWidth);
	p.fillRect(titleRect.x(), titleRect.y() + titleRect.height() - st::lineWidth, titleRect.width(), st::lineWidth, st::titleShadow[palette]);
}

void DefaultPreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth) {
	auto mask = QImage(st::windowShadow.size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	mask.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&mask);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		st::windowShadow.paint(p, 0, 0, st::windowShadow.width(), QColor(0, 0, 0));
	}
	auto maxSize = 0;
	auto currentInt = static_cast<uint32>(0);
	auto lastLineInts = reinterpret_cast<const uint32*>(mask.constBits() + (mask.height() - 1) * mask.bytesPerLine());
	for (auto end = lastLineInts + mask.width(); lastLineInts != end; ++lastLineInts) {
		if (*lastLineInts < currentInt) {
			break;
		}
		currentInt = *lastLineInts;
		++maxSize;
	}
	if (maxSize % cIntRetinaFactor()) {
		maxSize -= (maxSize % cIntRetinaFactor());
	}
	auto size = maxSize / cIntRetinaFactor();
	auto bottom = size;
	auto left = size - st::windowShadowShift;
	auto right = left;
	auto top = size - 2 * st::windowShadowShift;

	auto sprite = st::windowShadow[palette];
	auto topLeft = QImage(sprite.size() * cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	topLeft.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&topLeft);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		sprite.paint(p, 0, 0, sprite.width());
	}
	auto width = sprite.width();
	auto height = sprite.height();
	auto topRight = topLeft.mirrored(true, false);
	auto bottomRight = topLeft.mirrored(true, true);
	auto bottomLeft = topLeft.mirrored(false, true);

	Painter p(&preview);
	DefaultPreviewWindowTitle(p, palette, body, outerWidth);

	auto inner = QRect(body.x(), body.y() - st::defaultWindowTitle.height, body.width(), body.height() + st::defaultWindowTitle.height);
	p.setClipRegion(QRegion(inner.marginsAdded(QMargins(size, size, size, size))) - inner);
	p.drawImage(inner.x() - left, inner.y() - top, topLeft);
	p.drawImage(inner.x() + inner.width() + right - width, inner.y() - top, topRight);
	p.drawImage(inner.x() + inner.width() + right - width, inner.y() + inner.height() + bottom - height, bottomRight);
	p.drawImage(inner.x() - left, inner.y() + inner.height() + bottom - height, bottomLeft);
	p.drawImage(QRect(inner.x() - left, inner.y() - top + height, left, top + inner.height() + bottom - 2 * height), topLeft, QRect(0, topLeft.height() - cIntRetinaFactor(), left * cIntRetinaFactor(), cIntRetinaFactor()));
	p.drawImage(QRect(inner.x() - left + width, inner.y() - top, left + inner.width() + right - 2 * width, top), topLeft, QRect(topLeft.width() - cIntRetinaFactor(), 0, cIntRetinaFactor(), top * cIntRetinaFactor()));
	p.drawImage(QRect(inner.x() + inner.width(), inner.y() - top + height, right, top + inner.height() + bottom - 2 * height), topRight, QRect(topRight.width() - right * cIntRetinaFactor(), topRight.height() - cIntRetinaFactor(), right * cIntRetinaFactor(), cIntRetinaFactor()));
	p.drawImage(QRect(inner.x() - left + width, inner.y() + inner.height(), left + inner.width() + right - 2 * width, bottom), bottomRight, QRect(0, bottomRight.height() - bottom * cIntRetinaFactor(), cIntRetinaFactor(), bottom * cIntRetinaFactor()));
}

} // namespace Theme
} // namespace Window
