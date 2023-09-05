/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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
