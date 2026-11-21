// This file is part of mokaGram, https://github.com/mokagram/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// Mokagram: локальное распознавание речи через tinyasr. Движок, модель,
// декодирование голосовых сообщений и доступ к результатам. Звук не уходит
// в сеть.
#pragma once

#include "base/weak_ptr.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace Main {
class Session;
} // namespace Main

struct FullMsgId;

namespace Mokagram::Asr {

// Один результат на сообщение.
struct Result {
	QString text;
	bool failed = false;
	// Текст уже показан пользователю: повторно расшифровывать не нужно.
	bool shown = false;
};

// Движок живёт в фоновом потоке. Методы result() и subscribe() вызываются
// только в главном потоке, transcribe() потокобезопасен.
class Engine final {
public:
	static Engine &Instance();

	// Загружает модель и VAD из ресурсов клиента, один раз за процесс.
	bool init();
	[[nodiscard]] bool ready() const;

	// Ставит задачу в очередь расшифровки. bytes это содержимое .ogg/opus.
	void transcribe(not_null<Main::Session*> session, FullMsgId id, const QByteArray &bytes);

	[[nodiscard]] std::optional<Result> result(FullMsgId id) const;
	void markShown(FullMsgId id);

	~Engine();

private:
	Engine();
	class Impl;
	std::unique_ptr<Impl> _impl;

	Engine(const Engine&) = delete;
	Engine &operator=(const Engine&) = delete;
};

// Регистрирует настройку и подключает обработчик входящих сообщений.
void RegisterSettings();
void Start();

} // namespace Mokagram::Asr
