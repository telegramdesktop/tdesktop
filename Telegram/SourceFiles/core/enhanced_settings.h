/*
This file is part of Telegram Desktop x64,
the unofficial app based on Telegram Desktop.
For license and copyright information please follow this link:
https://github.com/TDesktop-x64/tdesktop/blob/dev/LEGAL
*/
#pragma once

#include <QtCore/QTimer>

namespace EnhancedSettings {

    class Manager : public QObject {
    Q_OBJECT

    public:
        Manager();

        void fill();

        void write(bool force = false);

    public slots:

        void writeTimeout();

    private:
        void writeDefaultFile();

        void writeCurrentSettings();

        bool readCustomFile();

        void writing();

        QTimer _jsonWriteTimer;

    };

    void Start();

    void Write();

    void Finish();

} // namespace EnhancedSettings