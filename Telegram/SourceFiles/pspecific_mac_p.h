/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org
 
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
#pragma once

class PsMacWindowData;

class PsMacWindowPrivate {
public:

	PsMacWindowPrivate();

    void setWindowBadge(const char *utf8str);
    void startBounce();
    
    void holdOnTop(WId winId);
    void showOverAll(WId winId);
    void activateWnd(WId winId);
    void showNotify(unsigned long long peer, const char *utf8title, const char *subtitle, const char *utf8msg);
    void clearNotifies(unsigned long long peer = 0);
    
    void enableShadow(WId winId);
        
    virtual void activeSpaceChanged() {
    }
    virtual void notifyClicked(unsigned long long peer) {
    }
    virtual void notifyReplied(unsigned long long peer, const char *str) {
    }
    
	~PsMacWindowPrivate();

    PsMacWindowData *data;
    
};

void _debugShowAlert(const char *utf8str);
void _outputDebugString(const char *utf8str);
int64 _idleTime();
