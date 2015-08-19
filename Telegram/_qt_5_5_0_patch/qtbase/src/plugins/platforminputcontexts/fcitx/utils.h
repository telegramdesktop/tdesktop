/***************************************************************************
 *   Copyright (C) 2012~2013 by CSSlayer                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

int
_utf8_get_char_extended(const char *s,
                             int max_len);
int _utf8_get_char_validated(const char *p,
                                  int max_len);
char *
_utf8_get_char(const char *i, uint32_t *chr);
int _utf8_check_string(const char *s);


#endif // UTILS_H
