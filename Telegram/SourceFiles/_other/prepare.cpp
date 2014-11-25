/*
This file is part of Telegram Desktop,
an official desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "prepare.h"

int prepare(QFileInfo f, QStringList paths) {
	if (paths.isEmpty()) {
		cout << "No -path args were passed :(\n";
		return -1;
	}

	int lastVersion = 0;
	QString lastVersionStr;
	QFileInfo last;
	QFileInfoList l = f.absoluteDir().entryInfoList(QDir::Files);
	for (QFileInfoList::iterator i = l.begin(), e = l.end(); i != e; ++i) {
		QRegularExpressionMatch m = QRegularExpression("/tsetup.((\\d+).(\\d+).(\\d+)).exe$").match(i->absoluteFilePath());
		if (!m.hasMatch()) continue;

		int version = m.captured(2).toInt() * 1000000 + m.captured(3).toInt() * 1000 + m.captured(4).toInt();
		if (version > lastVersion) {
			lastVersion = version;
			lastVersionStr = m.captured(1);
			last = *i;
		}
	}

	if (!lastVersion) {
		cout << "No tsetup.X.Y.Z.exe found :(\n";
		return -1;
	}

	cout << "Last version: " << lastVersionStr.toUtf8().constData() << " (" << lastVersion << "), executing packer..\n";

	QDir dir("deploy/" + lastVersionStr);
	if (dir.exists()) {
		cout << "Version " << lastVersionStr.toUtf8().constData() << " already exists in /deploy..\n";
		return -1;
	}

	QString packer = QString("Packer.exe -version %1").arg(lastVersion);
	for (QStringList::iterator i = paths.begin(), e = paths.end(); i != e; ++i) {
		packer += " -path " + *i;
	}

	int res = system(packer.toUtf8().constData());

	if (res) return res;

	dir.mkpath(".");

	paths.push_back("Telegram.pdb");
	paths.push_back("Updater.pdb");
	paths.push_back("tsetup." + lastVersionStr + ".exe");
	paths.push_back(QString("tupdate%1").arg(lastVersion));
	for (QStringList::iterator i = paths.begin(), e = paths.end(); i != e; ++i) {
		if (!QFile::copy(*i, "deploy/" + lastVersionStr + "/" + *i)) {
			cout << "Could not copy " << i->toUtf8().constData() << " to deploy/" << lastVersionStr.toUtf8().constData() << "\n";
			return -1;
		}
		cout << "Copied " << i->toUtf8().constData() << "..\n";
	}
	for (QStringList::iterator i = paths.begin(), e = paths.end(); i != e; ++i) {
		QFile::remove(*i);
	}

	cout << "Update created in deploy/" << lastVersionStr.toUtf8().constData() << "\n";

	return 0;
}

int main(int argc, char *argv[])
{
	QFileInfo f(argv[0]);

	QStringList paths;
	for (int i = 1; i < argc; ++i) {
		if (string(argv[i]) == "-path" && i + 1 < argc) {
			paths.push_back(QString(argv[i + 1]));
		}
	}
	int res = prepare(f, paths);
	return res;
}
