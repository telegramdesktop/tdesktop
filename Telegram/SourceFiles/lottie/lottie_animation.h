/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lottie/lottie_common.h"
#include "base/weak_ptr.h"

#include <QtGui/QImage>

class QString;
class QByteArray;

namespace rlottie {
class Animation;
} // namespace rlottie

namespace Lottie {

class Player;
class SharedState;
class FrameRenderer;

std::shared_ptr<FrameRenderer> MakeFrameRenderer();

QImage ReadThumbnail(const QByteArray &content);

namespace details {

using InitData = base::variant<std::unique_ptr<SharedState>, Error>;

std::unique_ptr<rlottie::Animation> CreateFromContent(
	const QByteArray &content,
	const ColorReplacements *replacements);

} // namespace details

class Animation final : public base::has_weak_ptr {
public:
	struct FrameInfo {
		QImage image;
		int index = 0;
	};

	Animation(
		not_null<Player*> player,
		const QByteArray &content,
		const FrameRequest &request,
		Quality quality,
		const ColorReplacements *replacements = nullptr);
	Animation(
		not_null<Player*> player,
		FnMut<void(FnMut<void(QByteArray &&cached)>)> get, // Main thread.
		FnMut<void(QByteArray &&cached)> put, // Unknown thread.
		const QByteArray &content,
		const FrameRequest &request,
		Quality quality,
		const ColorReplacements *replacements = nullptr);

	[[nodiscard]] bool ready() const;
	[[nodiscard]] QImage frame() const;
	[[nodiscard]] QImage frame(const FrameRequest &request) const;
	[[nodiscard]] FrameInfo frameInfo(const FrameRequest &request) const;

private:
	void initDone(details::InitData &&data);
	void parseDone(std::unique_ptr<SharedState> state);
	void parseFailed(Error error);

	not_null<Player*> _player;
	SharedState *_state = nullptr;

};

} // namespace Lottie
