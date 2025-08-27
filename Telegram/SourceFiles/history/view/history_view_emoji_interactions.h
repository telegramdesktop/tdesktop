/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Data {
class DocumentMedia;
} // namespace Data

namespace ChatHelpers {
struct EmojiInteractionPlayRequest;
} // namespace ChatHelpers

namespace Lottie {
class SinglePlayer;
} // namespace Lottie

namespace Main {
class Session;
} // namespace Main

namespace Stickers {
enum class EffectType : uint8;
} // namespace Stickers

namespace Ui {
class RpWidget;
} // namespace Ui

namespace HistoryView {

class Element;

class EmojiInteractions final {
public:
	EmojiInteractions(
		not_null<QWidget*> parent,
		not_null<QWidget*> layerParent,
		not_null<Main::Session*> session,
		Fn<int(not_null<const Element*>)> itemTop);
	~EmojiInteractions();

	void play(
		ChatHelpers::EmojiInteractionPlayRequest request,
		not_null<Element*> view);
	bool playPremiumEffect(
		not_null<const Element*> view,
		Element *replacing);
	void cancelPremiumEffect(not_null<const Element*> view);
	void visibleAreaUpdated(int visibleTop, int visibleBottom);

	void playEffectOnRead(not_null<const Element*> view);
	void playEffect(not_null<const Element*> view);

	void paint(not_null<QWidget*> layer, QRect clip);
	[[nodiscard]] rpl::producer<QString> playStarted() const;

private:
	struct Play {
		not_null<const Element*> view;
		std::unique_ptr<Lottie::SinglePlayer> lottie;
		mutable QRect lastTarget;
		QPoint shift;
		QSize inner;
		QSize outer;
		int frame = 0;
		int framesCount = 0;
		int frameRate = 0;
		Stickers::EffectType type = {};
		bool started = false;
		bool finished = false;
	};
	struct Delayed {
		QString emoticon;
		not_null<const Element*> view;
		std::shared_ptr<Data::DocumentMedia> media;
		crl::time shouldHaveStartedAt = 0;
		bool incoming = false;
	};
	struct ResolvedEffect {
		QString emoticon;
		DocumentData *document = nullptr;
		QByteArray content;
		QString filepath;

		explicit operator bool() const {
			return document && (!content.isEmpty() || !filepath.isEmpty());
		}
	};

	[[nodiscard]] QRect computeRect(const Play &play) const;

	void play(
		QString emoticon,
		not_null<const Element*> view,
		std::shared_ptr<Data::DocumentMedia> media,
		bool incoming);
	void play(
		QString emoticon,
		not_null<const Element*> view,
		not_null<DocumentData*> document,
		QByteArray data,
		QString filepath,
		bool incoming,
		Stickers::EffectType type);
	void checkDelayed();
	void addPendingEffect(not_null<const Element*> view);

	[[nodiscard]] ResolvedEffect resolveEffect(
		not_null<const Element*> view);
	void playEffect(
		not_null<const Element*> view,
		const ResolvedEffect &resolved);
	void checkPendingEffects();

	void refreshLayerShift();
	void refreshLayerGeometryAndUpdate(QRect rect);

	const not_null<QWidget*> _parent;
	const not_null<QWidget*> _layerParent;
	const not_null<Main::Session*> _session;
	const Fn<int(not_null<const Element*>)> _itemTop;

	base::unique_qptr<Ui::RpWidget> _layer;
	QPoint _layerShift;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	std::vector<Play> _plays;
	std::vector<Delayed> _delayed;
	rpl::event_stream<QString> _playStarted;

	std::vector<base::weak_ptr<const Element>> _pendingEffects;
	rpl::lifetime _downloadLifetime;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
