/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include <iconv.h>

#ifdef iconv_open
#undef iconv_open
#endif // iconv_open

#ifdef iconv
#undef iconv
#endif // iconv

#ifdef iconv_close
#undef iconv_close
#endif // iconv_close

iconv_t iconv_open(const char* tocode, const char* fromcode) {
	return libiconv_open(tocode, fromcode);
}

size_t iconv(iconv_t cd, char** inbuf, size_t *inbytesleft, char** outbuf, size_t *outbytesleft) {
	return libiconv(cd, inbuf, inbytesleft, outbuf, outbytesleft);
}

int iconv_close(iconv_t cd) {
	return libiconv_close(cd);
}
