/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "private/qbmphandler_p.h"

#ifndef QT_NO_IMAGEFORMAT_BMP

#include <qimage.h>
#include <qvariant.h>
#include <qvector.h>

QT_BEGIN_NAMESPACE

static void swapPixel01(QImage *image)        // 1-bpp: swap 0 and 1 pixels
{
    int i;
    if (image->depth() == 1 && image->colorCount() == 2) {
        uint *p = (uint *)image->bits();
        int nbytes = image->byteCount();
        for (i=0; i<nbytes/4; i++) {
            *p = ~*p;
            p++;
        }
        uchar *p2 = (uchar *)p;
        for (i=0; i<(nbytes&3); i++) {
            *p2 = ~*p2;
            p2++;
        }
        QRgb t = image->color(0);                // swap color 0 and 1
        image->setColor(0, image->color(1));
        image->setColor(1, t);
    }
}

/*
    QImageIO::defineIOHandler("BMP", "^BM", 0,
                               read_bmp_image, write_bmp_image);
*/

/*****************************************************************************
  BMP (DIB) image read/write functions
 *****************************************************************************/

const int BMP_FILEHDR_SIZE = 14;                // size of BMP_FILEHDR data

static QDataStream &operator>>(QDataStream &s, BMP_FILEHDR &bf)
{                                                // read file header
    s.readRawData(bf.bfType, 2);
    s >> bf.bfSize >> bf.bfReserved1 >> bf.bfReserved2 >> bf.bfOffBits;
    return s;
}

static QDataStream &operator<<(QDataStream &s, const BMP_FILEHDR &bf)
{                                                // write file header
    s.writeRawData(bf.bfType, 2);
    s << bf.bfSize << bf.bfReserved1 << bf.bfReserved2 << bf.bfOffBits;
    return s;
}


const int BMP_OLD  = 12;                        // old Windows/OS2 BMP size
const int BMP_WIN  = 40;                        // Windows BMP v3 size
const int BMP_OS2  = 64;                        // new OS/2 BMP size
const int BMP_WIN4 = 108;                       // Windows BMP v4 size
const int BMP_WIN5 = 124;                       // Windows BMP v5 size

const int BMP_RGB  = 0;                                // no compression
const int BMP_RLE8 = 1;                                // run-length encoded, 8 bits
const int BMP_RLE4 = 2;                                // run-length encoded, 4 bits
const int BMP_BITFIELDS = 3;                        // RGB values encoded in data as bit-fields


static QDataStream &operator>>(QDataStream &s, BMP_INFOHDR &bi)
{
    s >> bi.biSize;
    if (bi.biSize == BMP_WIN || bi.biSize == BMP_OS2 || bi.biSize == BMP_WIN4 || bi.biSize == BMP_WIN5) {
        s >> bi.biWidth >> bi.biHeight >> bi.biPlanes >> bi.biBitCount;
        s >> bi.biCompression >> bi.biSizeImage;
        s >> bi.biXPelsPerMeter >> bi.biYPelsPerMeter;
        s >> bi.biClrUsed >> bi.biClrImportant;
    }
    else {                                        // probably old Windows format
        qint16 w, h;
        s >> w >> h >> bi.biPlanes >> bi.biBitCount;
        bi.biWidth  = w;
        bi.biHeight = h;
        bi.biCompression = BMP_RGB;                // no compression
        bi.biSizeImage = 0;
        bi.biXPelsPerMeter = bi.biYPelsPerMeter = 0;
        bi.biClrUsed = bi.biClrImportant = 0;
    }
    return s;
}

static QDataStream &operator<<(QDataStream &s, const BMP_INFOHDR &bi)
{
    s << bi.biSize;
    s << bi.biWidth << bi.biHeight;
    s << bi.biPlanes;
    s << bi.biBitCount;
    s << bi.biCompression;
    s << bi.biSizeImage;
    s << bi.biXPelsPerMeter << bi.biYPelsPerMeter;
    s << bi.biClrUsed << bi.biClrImportant;
    return s;
}

static int calc_shift(uint mask)
{
    int result = 0;
    while (mask && !(mask & 1)) {
        result++;
        mask >>= 1;
    }
    return result;
}

static bool read_dib_fileheader(QDataStream &s, BMP_FILEHDR &bf)
{
    // read BMP file header
    s >> bf;
    if (s.status() != QDataStream::Ok)
        return false;

    // check header
    if (qstrncmp(bf.bfType,"BM",2) != 0)
        return false;

    return true;
}

static bool read_dib_infoheader(QDataStream &s, BMP_INFOHDR &bi)
{
    s >> bi;                                        // read BMP info header
    if (s.status() != QDataStream::Ok)
        return false;

    int nbits = bi.biBitCount;
    int comp = bi.biCompression;
    if (!(nbits == 1 || nbits == 4 || nbits == 8 || nbits == 16 || nbits == 24 || nbits == 32) ||
        bi.biPlanes != 1 || comp > BMP_BITFIELDS)
        return false;                                        // weird BMP image
    if (!(comp == BMP_RGB || (nbits == 4 && comp == BMP_RLE4) ||
        (nbits == 8 && comp == BMP_RLE8) || ((nbits == 16 || nbits == 32) && comp == BMP_BITFIELDS)))
         return false;                                // weird compression type

    return true;
}

static bool read_dib_body(QDataStream &s, const BMP_INFOHDR &bi, int offset, int startpos, QImage &image)
{
    QIODevice* d = s.device();
    if (d->atEnd())                                // end of stream/file
        return false;
#if 0
    qDebug("offset...........%d", offset);
    qDebug("startpos.........%d", startpos);
    qDebug("biSize...........%d", bi.biSize);
    qDebug("biWidth..........%d", bi.biWidth);
    qDebug("biHeight.........%d", bi.biHeight);
    qDebug("biPlanes.........%d", bi.biPlanes);
    qDebug("biBitCount.......%d", bi.biBitCount);
    qDebug("biCompression....%d", bi.biCompression);
    qDebug("biSizeImage......%d", bi.biSizeImage);
    qDebug("biXPelsPerMeter..%d", bi.biXPelsPerMeter);
    qDebug("biYPelsPerMeter..%d", bi.biYPelsPerMeter);
    qDebug("biClrUsed........%d", bi.biClrUsed);
    qDebug("biClrImportant...%d", bi.biClrImportant);
#endif
    int w = bi.biWidth,         h = bi.biHeight,  nbits = bi.biBitCount;
    int t = bi.biSize,         comp = bi.biCompression;
    uint red_mask = 0;
    uint green_mask = 0;
    uint blue_mask = 0;
    uint alpha_mask = 0;
    int red_shift = 0;
    int green_shift = 0;
    int blue_shift = 0;
    int alpha_shift = 0;
    int red_scale = 0;
    int green_scale = 0;
    int blue_scale = 0;
    int alpha_scale = 0;

	if (!d->isSequential())
		d->seek(startpos + BMP_FILEHDR_SIZE + (bi.biSize >= BMP_WIN4 ? BMP_WIN : bi.biSize)); // goto start of colormap

    if (bi.biSize >= BMP_WIN4 || (comp == BMP_BITFIELDS && (nbits == 16 || nbits == 32))) {
        if (d->read((char *)&red_mask, sizeof(red_mask)) != sizeof(red_mask))
            return false;
        if (d->read((char *)&green_mask, sizeof(green_mask)) != sizeof(green_mask))
            return false;
        if (d->read((char *)&blue_mask, sizeof(blue_mask)) != sizeof(blue_mask))
            return false;

        // Read BMP v4+ header
        if (bi.biSize >= BMP_WIN4) {
            int CSType       = 0;
            int gamma_red    = 0;
            int gamma_green  = 0;
            int gamma_blue   = 0;
            int endpoints[9];

            if (d->read((char *)&alpha_mask, sizeof(alpha_mask)) != sizeof(alpha_mask))
                return false;
            if (d->read((char *)&CSType, sizeof(CSType)) != sizeof(CSType))
                return false;
            if (d->read((char *)&endpoints, sizeof(endpoints)) != sizeof(endpoints))
                return false;
            if (d->read((char *)&gamma_red, sizeof(gamma_red)) != sizeof(gamma_red))
                return false;
            if (d->read((char *)&gamma_green, sizeof(gamma_green)) != sizeof(gamma_green))
                return false;
            if (d->read((char *)&gamma_blue, sizeof(gamma_blue)) != sizeof(gamma_blue))
                return false;

            if (bi.biSize == BMP_WIN5) {
                qint32 intent      = 0;
                qint32 profileData = 0;
                qint32 profileSize = 0;
                qint32 reserved    = 0;

                if (d->read((char *)&intent, sizeof(intent)) != sizeof(intent))
                    return false;
                if (d->read((char *)&profileData, sizeof(profileData)) != sizeof(profileData))
                    return false;
                if (d->read((char *)&profileSize, sizeof(profileSize)) != sizeof(profileSize))
                    return false;
                if (d->read((char *)&reserved, sizeof(reserved)) != sizeof(reserved) || reserved != 0)
                    return false;
            }
        }
    }

    bool transp = (comp == BMP_BITFIELDS) && alpha_mask;
    int ncols = 0;
    int depth = 0;
    QImage::Format format;
    switch (nbits) {
        case 32:
        case 24:
        case 16:
            depth = 32;
            format = transp ? QImage::Format_ARGB32 : QImage::Format_RGB32;
            break;
        case 8:
        case 4:
            depth = 8;
            format = QImage::Format_Indexed8;
            break;
        default:
            depth = 1;
            format = QImage::Format_Mono;
    }

    if (bi.biHeight < 0)
        h = -h;                  // support images with negative height

    if (image.size() != QSize(w, h) || image.format() != format) {
        image = QImage(w, h, format);
        if (image.isNull())                        // could not create image
            return false;
    }

    if (depth != 32) {
        ncols = bi.biClrUsed ? bi.biClrUsed : 1 << nbits;
        if (ncols > 256) // sanity check - don't run out of mem if color table is broken
            return false;
        image.setColorCount(ncols);
    }

    image.setDotsPerMeterX(bi.biXPelsPerMeter);
    image.setDotsPerMeterY(bi.biYPelsPerMeter);

    if (ncols > 0) {                                // read color table
        uchar rgb[4];
        int   rgb_len = t == BMP_OLD ? 3 : 4;
        for (int i=0; i<ncols; i++) {
            if (d->read((char *)rgb, rgb_len) != rgb_len)
                return false;
            image.setColor(i, qRgb(rgb[2],rgb[1],rgb[0]));
            if (d->atEnd())                        // truncated file
                return false;
        }
    } else if (comp == BMP_BITFIELDS && (nbits == 16 || nbits == 32)) {
        red_shift = calc_shift(red_mask);
        red_scale = 256 / ((red_mask >> red_shift) + 1);
        green_shift = calc_shift(green_mask);
        green_scale = 256 / ((green_mask >> green_shift) + 1);
        blue_shift = calc_shift(blue_mask);
        blue_scale = 256 / ((blue_mask >> blue_shift) + 1);
        alpha_shift = calc_shift(alpha_mask);
        alpha_scale = 256 / ((alpha_mask >> alpha_shift) + 1);
    } else if (comp == BMP_RGB && (nbits == 24 || nbits == 32)) {
        blue_mask = 0x000000ff;
        green_mask = 0x0000ff00;
        red_mask = 0x00ff0000;
        blue_shift = 0;
        green_shift = 8;
        red_shift = 16;
        blue_scale = green_scale = red_scale = 1;
    } else if (comp == BMP_RGB && nbits == 16) {
        blue_mask = 0x001f;
        green_mask = 0x03e0;
        red_mask = 0x7c00;
        blue_shift = 0;
        green_shift = 2;
        red_shift = 7;
        red_scale = 1;
        green_scale = 1;
        blue_scale = 8;
    }

#if 0
    qDebug("Rmask: %08x Rshift: %08x Rscale:%08x", red_mask, red_shift, red_scale);
    qDebug("Gmask: %08x Gshift: %08x Gscale:%08x", green_mask, green_shift, green_scale);
    qDebug("Bmask: %08x Bshift: %08x Bscale:%08x", blue_mask, blue_shift, blue_scale);
    qDebug("Amask: %08x Ashift: %08x Ascale:%08x", alpha_mask, alpha_shift, alpha_scale);
#endif

    // offset can be bogus, be careful
    if (offset>=0 && startpos + offset > d->pos()) {
        if (!d->isSequential())
            d->seek(startpos + offset);                // start of image data
    }

    int             bpl = image.bytesPerLine();
    uchar *data = image.bits();

    if (nbits == 1) {                                // 1 bit BMP image
        while (--h >= 0) {
            if (d->read((char*)(data + h*bpl), bpl) != bpl)
                break;
        }
        if (ncols == 2 && qGray(image.color(0)) < qGray(image.color(1)))
            swapPixel01(&image);                // pixel 0 is white!
    }

    else if (nbits == 4) {                        // 4 bit BMP image
        int    buflen = ((w+7)/8)*4;
        uchar *buf    = new uchar[buflen];
        if (comp == BMP_RLE4) {                // run length compression
            int x=0, y=0, c, i;
            quint8 b;
            uchar *p = data + (h-1)*bpl;
            const uchar *endp = p + w;
            while (y < h) {
                if (!d->getChar((char *)&b))
                    break;
                if (b == 0) {                        // escape code
                    if (!d->getChar((char *)&b) || b == 1) {
                        y = h;                // exit loop
                    } else switch (b) {
                        case 0:                        // end of line
                            x = 0;
                            y++;
                            p = data + (h-y-1)*bpl;
                            break;
                        case 2:                        // delta (jump)
                        {
                            quint8 tmp;
                            d->getChar((char *)&tmp);
                            x += tmp;
                            d->getChar((char *)&tmp);
                            y += tmp;
                        }

                            // Protection
                            if ((uint)x >= (uint)w)
                                x = w-1;
                            if ((uint)y >= (uint)h)
                                y = h-1;

                            p = data + (h-y-1)*bpl + x;
                            break;
                        default:                // absolute mode
                            // Protection
                            if (p + b > endp)
                                b = endp-p;

                            i = (c = b)/2;
                            while (i--) {
                                d->getChar((char *)&b);
                                *p++ = b >> 4;
                                *p++ = b & 0x0f;
                            }
                            if (c & 1) {
                                unsigned char tmp;
                                d->getChar((char *)&tmp);
                                *p++ = tmp >> 4;
                            }
                            if ((((c & 3) + 1) & 2) == 2)
                                d->getChar(0);        // align on word boundary
                            x += c;
                    }
                } else {                        // encoded mode
                    // Protection
                    if (p + b > endp)
                        b = endp-p;

                    i = (c = b)/2;
                    d->getChar((char *)&b);                // 2 pixels to be repeated
                    while (i--) {
                        *p++ = b >> 4;
                        *p++ = b & 0x0f;
                    }
                    if (c & 1)
                        *p++ = b >> 4;
                    x += c;
                }
            }
        } else if (comp == BMP_RGB) {                // no compression
            memset(data, 0, h*bpl);
            while (--h >= 0) {
                if (d->read((char*)buf,buflen) != buflen)
                    break;
                uchar *p = data + h*bpl;
                uchar *b = buf;
                for (int i=0; i<w/2; i++) {        // convert nibbles to bytes
                    *p++ = *b >> 4;
                    *p++ = *b++ & 0x0f;
                }
                if (w & 1)                        // the last nibble
                    *p = *b >> 4;
            }
        }
        delete [] buf;
    }

    else if (nbits == 8) {                        // 8 bit BMP image
        if (comp == BMP_RLE8) {                // run length compression
            int x=0, y=0;
            quint8 b;
            uchar *p = data + (h-1)*bpl;
            const uchar *endp = p + w;
            while (y < h) {
                if (!d->getChar((char *)&b))
                    break;
                if (b == 0) {                        // escape code
                    if (!d->getChar((char *)&b) || b == 1) {
                            y = h;                // exit loop
                    } else switch (b) {
                        case 0:                        // end of line
                            x = 0;
                            y++;
                            p = data + (h-y-1)*bpl;
                            break;
                        case 2:                        // delta (jump)
                            // Protection
                            if ((uint)x >= (uint)w)
                                x = w-1;
                            if ((uint)y >= (uint)h)
                                y = h-1;

                            {
                                quint8 tmp;
                                d->getChar((char *)&tmp);
                                x += tmp;
                                d->getChar((char *)&tmp);
                                y += tmp;
                            }
                            p = data + (h-y-1)*bpl + x;
                            break;
                        default:                // absolute mode
                            // Protection
                            if (p + b > endp)
                                b = endp-p;

                            if (d->read((char *)p, b) != b)
                                return false;
                            if ((b & 1) == 1)
                                d->getChar(0);        // align on word boundary
                            x += b;
                            p += b;
                    }
                } else {                        // encoded mode
                    // Protection
                    if (p + b > endp)
                        b = endp-p;

                    char tmp;
                    d->getChar(&tmp);
                    memset(p, tmp, b); // repeat pixel
                    x += b;
                    p += b;
                }
            }
        } else if (comp == BMP_RGB) {                // uncompressed
            while (--h >= 0) {
                if (d->read((char *)data + h*bpl, bpl) != bpl)
                    break;
            }
        }
    }

    else if (nbits == 16 || nbits == 24 || nbits == 32) { // 16,24,32 bit BMP image
        QRgb *p;
        QRgb  *end;
        uchar *buf24 = new uchar[bpl];
        int    bpl24 = ((w*nbits+31)/32)*4;
        uchar *b;
        int c;

        while (--h >= 0) {
            p = (QRgb *)(data + h*bpl);
            end = p + w;
            if (d->read((char *)buf24,bpl24) != bpl24)
                break;
            b = buf24;
            while (p < end) {
                c = *(uchar*)b | (*(uchar*)(b+1)<<8);
                if (nbits > 16)
                    c |= *(uchar*)(b+2)<<16;
                if (nbits > 24)
                    c |= *(uchar*)(b+3)<<24;
                *p++ = qRgba(((c & red_mask) >> red_shift) * red_scale,
                                        ((c & green_mask) >> green_shift) * green_scale,
                                        ((c & blue_mask) >> blue_shift) * blue_scale,
                                        transp ? ((c & alpha_mask) >> alpha_shift) * alpha_scale : 0xff);
                b += nbits/8;
            }
        }
        delete[] buf24;
    }

    if (bi.biHeight < 0) {
        // Flip the image
        uchar *buf = new uchar[bpl];
        h = -bi.biHeight;
        for (int y = 0; y < h/2; ++y) {
            memcpy(buf, data + y*bpl, bpl);
            memcpy(data + y*bpl, data + (h-y-1)*bpl, bpl);
            memcpy(data + (h-y-1)*bpl, buf, bpl);
        }
        delete [] buf;
    }

    return true;
}

// this is also used in qmime_win.cpp
bool qt_write_dib(QDataStream &s, QImage image)
{
    int        nbits;
    int        bpl_bmp;
    int        bpl = image.bytesPerLine();

    QIODevice* d = s.device();
    if (!d->isWritable())
        return false;

    if (image.depth() == 8 && image.colorCount() <= 16) {
        bpl_bmp = (((bpl+1)/2+3)/4)*4;
        nbits = 4;
    } else if (image.depth() == 32) {
        bpl_bmp = ((image.width()*24+31)/32)*4;
        nbits = 24;
    } else {
        bpl_bmp = bpl;
        nbits = image.depth();
    }

    BMP_INFOHDR bi;
    bi.biSize               = BMP_WIN;                // build info header
    bi.biWidth               = image.width();
    bi.biHeight               = image.height();
    bi.biPlanes               = 1;
    bi.biBitCount      = nbits;
    bi.biCompression   = BMP_RGB;
    bi.biSizeImage     = bpl_bmp*image.height();
    bi.biXPelsPerMeter = image.dotsPerMeterX() ? image.dotsPerMeterX()
                                                : 2834; // 72 dpi default
    bi.biYPelsPerMeter = image.dotsPerMeterY() ? image.dotsPerMeterY() : 2834;
    bi.biClrUsed       = image.colorCount();
    bi.biClrImportant  = image.colorCount();
    s << bi;                                        // write info header
    if (s.status() != QDataStream::Ok)
        return false;

    if (image.depth() != 32) {                // write color table
        uchar *color_table = new uchar[4*image.colorCount()];
        uchar *rgb = color_table;
        QVector<QRgb> c = image.colorTable();
        for (int i=0; i<image.colorCount(); i++) {
            *rgb++ = qBlue (c[i]);
            *rgb++ = qGreen(c[i]);
            *rgb++ = qRed  (c[i]);
            *rgb++ = 0;
        }
        if (d->write((char *)color_table, 4*image.colorCount()) == -1) {
            delete [] color_table;
            return false;
        }
        delete [] color_table;
    }

    if (image.format() == QImage::Format_MonoLSB)
        image = image.convertToFormat(QImage::Format_Mono);

    int y;

    if (nbits == 1 || nbits == 8) {                // direct output
        for (y=image.height()-1; y>=0; y--) {
            if (d->write((char*)image.constScanLine(y), bpl) == -1)
                return false;
        }
        return true;
    }

    uchar *buf        = new uchar[bpl_bmp];
    uchar *b, *end;
    const uchar *p;

    memset(buf, 0, bpl_bmp);
    for (y=image.height()-1; y>=0; y--) {        // write the image bits
        if (nbits == 4) {                        // convert 8 -> 4 bits
            p = image.constScanLine(y);
            b = buf;
            end = b + image.width()/2;
            while (b < end) {
                *b++ = (*p << 4) | (*(p+1) & 0x0f);
                p += 2;
            }
            if (image.width() & 1)
                *b = *p << 4;
        } else {                                // 32 bits
            const QRgb *p   = (const QRgb *)image.constScanLine(y);
            const QRgb *end = p + image.width();
            b = buf;
            while (p < end) {
                *b++ = qBlue(*p);
                *b++ = qGreen(*p);
                *b++ = qRed(*p);
                p++;
            }
        }
        if (bpl_bmp != d->write((char*)buf, bpl_bmp)) {
            delete[] buf;
            return false;
        }
    }
    delete[] buf;
    return true;
}

// this is also used in qmime_win.cpp
bool qt_read_dib(QDataStream &s, QImage &image)
{
    BMP_INFOHDR bi;
    if (!read_dib_infoheader(s, bi))
        return false;
    return read_dib_body(s, bi, -1, -BMP_FILEHDR_SIZE, image);
}

QBmpHandler::QBmpHandler(InternalFormat fmt) :
    m_format(fmt), state(Ready)
{
}

QByteArray QBmpHandler::formatName() const
{
    return m_format == BmpFormat ? "bmp" : "dib";
}

bool QBmpHandler::readHeader()
{
    state = Error;

    QIODevice *d = device();
    QDataStream s(d);
    startpos = d->pos();

    // Intel byte order
    s.setByteOrder(QDataStream::LittleEndian);

    // read BMP file header
    if (m_format == BmpFormat && !read_dib_fileheader(s, fileHeader))
        return false;

    // read BMP info header
    if (!read_dib_infoheader(s, infoHeader))
        return false;

    state = ReadHeader;
    return true;
}

bool QBmpHandler::canRead() const
{
    if (m_format == BmpFormat && state == Ready && !canRead(device()))
        return false;

    if (state != Error) {
        setFormat(formatName());
        return true;
    }

    return false;
}

bool QBmpHandler::canRead(QIODevice *device)
{
    if (!device) {
        qWarning("QBmpHandler::canRead() called with 0 pointer");
        return false;
    }

    char head[2];
    if (device->peek(head, sizeof(head)) != sizeof(head))
        return false;

    return (qstrncmp(head, "BM", 2) == 0);
}

bool QBmpHandler::read(QImage *image)
{
    if (state == Error)
        return false;

    if (!image) {
        qWarning("QBmpHandler::read: cannot read into null pointer");
        return false;
    }

    if (state == Ready && !readHeader()) {
        state = Error;
        return false;
    }

    QIODevice *d = device();
    QDataStream s(d);

    // Intel byte order
    s.setByteOrder(QDataStream::LittleEndian);

    // read image
    const bool readSuccess = m_format == BmpFormat ?
        read_dib_body(s, infoHeader, fileHeader.bfOffBits, startpos, *image) :
        read_dib_body(s, infoHeader, -1, startpos - BMP_FILEHDR_SIZE, *image);
    if (!readSuccess)
        return false;

    state = Ready;
    return true;
}

bool QBmpHandler::write(const QImage &img)
{
    if (m_format == DibFormat) {
        QDataStream dibStream(device());
        dibStream.setByteOrder(QDataStream::LittleEndian); // Intel byte order
        return qt_write_dib(dibStream, img);
    }

    QImage image;
    switch (img.format()) {
    case QImage::Format_ARGB8565_Premultiplied:
    case QImage::Format_ARGB8555_Premultiplied:
    case QImage::Format_ARGB6666_Premultiplied:
    case QImage::Format_ARGB4444_Premultiplied:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
        image = img.convertToFormat(QImage::Format_ARGB32);
        break;
    case QImage::Format_RGB16:
    case QImage::Format_RGB888:
    case QImage::Format_RGB666:
    case QImage::Format_RGB555:
    case QImage::Format_RGB444:
    case QImage::Format_RGBX8888:
        image = img.convertToFormat(QImage::Format_RGB32);
        break;
    default:
        image = img;
    }

    QIODevice *d = device();
    QDataStream s(d);
    BMP_FILEHDR bf;
    int bpl_bmp;
    int bpl = image.bytesPerLine();

    // Code partially repeated in qt_write_dib
    if (image.depth() == 8 && image.colorCount() <= 16) {
        bpl_bmp = (((bpl+1)/2+3)/4)*4;
    } else if (image.depth() == 32) {
        bpl_bmp = ((image.width()*24+31)/32)*4;
    } else {
        bpl_bmp = bpl;
    }

    // Intel byte order
    s.setByteOrder(QDataStream::LittleEndian);

    // build file header
    memcpy(bf.bfType, "BM", 2);

    // write file header
    bf.bfReserved1 = 0;
    bf.bfReserved2 = 0;
    bf.bfOffBits = BMP_FILEHDR_SIZE + BMP_WIN + image.colorCount() * 4;
    bf.bfSize = bf.bfOffBits + bpl_bmp*image.height();
    s << bf;

    // write image
    return qt_write_dib(s, image);
}

bool QBmpHandler::supportsOption(ImageOption option) const
{
    return option == Size
            || option == ImageFormat;
}

QVariant QBmpHandler::option(ImageOption option) const
{
    if (option == Size) {
        if (state == Error)
            return QVariant();
        if (state == Ready && !const_cast<QBmpHandler*>(this)->readHeader())
            return QVariant();
        return QSize(infoHeader.biWidth, infoHeader.biHeight);
    } else if (option == ImageFormat) {
        if (state == Error)
            return QVariant();
        if (state == Ready && !const_cast<QBmpHandler*>(this)->readHeader())
            return QVariant();
        QImage::Format format;
        switch (infoHeader.biBitCount) {
            case 32:
            case 24:
            case 16:
                format = QImage::Format_RGB32;
                break;
            case 8:
            case 4:
                format = QImage::Format_Indexed8;
                break;
            default:
                format = QImage::Format_Mono;
            }
        return format;
    }
    return QVariant();
}

void QBmpHandler::setOption(ImageOption option, const QVariant &value)
{
    Q_UNUSED(option);
    Q_UNUSED(value);
}

QByteArray QBmpHandler::name() const
{
    return formatName();
}

QT_END_NAMESPACE

#endif // QT_NO_IMAGEFORMAT_BMP
