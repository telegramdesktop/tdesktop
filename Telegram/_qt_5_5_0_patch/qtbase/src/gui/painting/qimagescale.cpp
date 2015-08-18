/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include <private/qimagescale_p.h>
#include <private/qdrawhelper_p.h>

#include "qimage.h"
#include "qcolor.h"

QT_BEGIN_NAMESPACE

/*
 * Copyright (C) 2004, 2005 Daniel M. Duley
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* OTHER CREDITS:
 *
 * This is the normal smoothscale method, based on Imlib2's smoothscale.
 *
 * Originally I took the algorithm used in NetPBM and Qt and added MMX/3dnow
 * optimizations. It ran in about 1/2 the time as Qt. Then I ported Imlib's
 * C algorithm and it ran at about the same speed as my MMX optimized one...
 * Finally I ported Imlib's MMX version and it ran in less than half the
 * time as my MMX algorithm, (taking only a quarter of the time Qt does).
 * After further optimization it seems to run at around 1/6th.
 *
 * Changes include formatting, namespaces and other C++'ings, removal of old
 * #ifdef'ed code, and removal of unneeded border calculation code.
 *
 * Imlib2 is (C) Carsten Haitzler and various contributors. The MMX code
 * is by Willem Monsuwe <willem@stack.nl>. All other modifications are
 * (C) Daniel M. Duley.
 */


namespace QImageScale {
    const unsigned int** qimageCalcYPoints(const unsigned int *src, int sw, int sh, int dh);
    int* qimageCalcXPoints(int sw, int dw);
    int* qimageCalcApoints(int s, int d, int up);
    QImageScaleInfo* qimageFreeScaleInfo(QImageScaleInfo *isi);
    QImageScaleInfo *qimageCalcScaleInfo(const QImage &img, int sw, int sh,
                                         int dw, int dh, char aa);
}

using namespace QImageScale;

//
// Code ported from Imlib...
//

const unsigned int** QImageScale::qimageCalcYPoints(const unsigned int *src,
                                              int sw, int sh, int dh)
{
    const unsigned int **p;
    int j = 0, rv = 0;
    qint64 val, inc;

    if(dh < 0){
        dh = -dh;
        rv = 1;
    }
    p = new const unsigned int* [dh+1];

    int up = qAbs(dh) >= sh;
    val = up ? 0x8000 * sh / dh - 0x8000 : 0;
    inc = (((qint64)sh) << 16) / dh;
    for (int i = 0; i < dh; i++) {
        p[j++] = src + qMax(0LL, val >> 16) * sw;
        val += inc;
    }
    if (rv) {
        for (int i = dh / 2; --i >= 0; ) {
            const unsigned int *tmp = p[i];
            p[i] = p[dh - i - 1];
            p[dh - i - 1] = tmp;
        }
    }
    return(p);
}

int* QImageScale::qimageCalcXPoints(int sw, int dw)
{
    int *p, j = 0, rv = 0;
    qint64 val, inc;

    if(dw < 0){
        dw = -dw;
        rv = 1;
    }
    p = new int[dw+1];

    int up = qAbs(dw) >= sw;
    val = up ? 0x8000 * sw / dw - 0x8000 : 0;
    inc = (((qint64)sw) << 16) / dw;
    for (int i = 0; i < dw; i++) {
        p[j++] = qMax(0LL, val >> 16);
        val += inc;
    }

    if (rv) {
        for (int i = dw / 2; --i >= 0; ) {
            int tmp = p[i];
            p[i] = p[dw - i - 1];
            p[dw - i - 1] = tmp;
        }
    }
   return(p);
}

int* QImageScale::qimageCalcApoints(int s, int d, int up)
{
    int *p, j = 0, rv = 0;

    if(d < 0){
        rv = 1;
        d = -d;
    }
    p = new int[d];

    /* scaling up */
    if(up){
        qint64 val, inc;

        val = 0x8000 * s / d - 0x8000;
        inc = (((qint64)s) << 16) / d;
        for (int i = 0; i < d; i++) {
            int pos = val >> 16;
            if (pos < 0)
                p[j++] = 0;
            else if (pos >= (s - 1))
                p[j++] = 0;
            else
                p[j++] = (val >> 8) - ((val >> 8) & 0xffffff00);
            val += inc;
        }
    }
    /* scaling down */
    else {
        qint64 val = 0;
        qint64 inc = (((qint64)s) << 16) / d;
        int Cp = (((d << 14) + s - 1) / s);
        for (int i = 0; i < d; i++) {
            int ap = ((0x10000 - (val & 0xffff)) * Cp) >> 16;
            p[j] = ap | (Cp << 16);
            j++;
            val += inc;
        }
    }
    if(rv){
        int tmp;
        for (int i = d / 2; --i >= 0; ) {
            tmp = p[i];
            p[i] = p[d - i - 1];
            p[d - i - 1] = tmp;
        }
    }
    return p;
}

QImageScaleInfo* QImageScale::qimageFreeScaleInfo(QImageScaleInfo *isi)
{
    if(isi){
        delete[] isi->xpoints;
        delete[] isi->ypoints;
        delete[] isi->xapoints;
        delete[] isi->yapoints;
        delete isi;
    }
    return 0;
}

QImageScaleInfo* QImageScale::qimageCalcScaleInfo(const QImage &img,
                                                  int sw, int sh,
                                                  int dw, int dh, char aa)
{
    QImageScaleInfo *isi;
    int scw, sch;

    scw = dw * qlonglong(img.width()) / sw;
    sch = dh * qlonglong(img.height()) / sh;

    isi = new QImageScaleInfo;
    if(!isi)
        return 0;
    memset(isi, 0, sizeof(QImageScaleInfo));

    isi->xup_yup = (qAbs(dw) >= sw) + ((qAbs(dh) >= sh) << 1);

    isi->xpoints = qimageCalcXPoints(img.width(), scw);
    if(!isi->xpoints)
        return(qimageFreeScaleInfo(isi));
    isi->ypoints = qimageCalcYPoints((const unsigned int *)img.scanLine(0),
                                     img.bytesPerLine() / 4, img.height(), sch);
    if (!isi->ypoints)
        return(qimageFreeScaleInfo(isi));
    if(aa) {
        isi->xapoints = qimageCalcApoints(img.width(), scw, isi->xup_yup & 1);
        if(!isi->xapoints)
            return(qimageFreeScaleInfo(isi));
        isi->yapoints = qimageCalcApoints(img.height(), sch, isi->xup_yup & 2);
        if(!isi->yapoints)
            return(qimageFreeScaleInfo(isi));
    }
    return(isi);
}


static void qt_qimageScaleAARGBA_up_x_down_y(QImageScaleInfo *isi, unsigned int *dest,
                                             int dxx, int dyy, int dx, int dy,
                                             int dw, int dh, int dow, int sow);

static void qt_qimageScaleAARGBA_down_x_up_y(QImageScaleInfo *isi, unsigned int *dest,
                                             int dxx, int dyy, int dx, int dy,
                                             int dw, int dh, int dow, int sow);

static void qt_qimageScaleAARGBA_down_xy(QImageScaleInfo *isi, unsigned int *dest,
                                         int dxx, int dyy, int dx, int dy, int dw,
                                         int dh, int dow, int sow);

#if defined(QT_COMPILER_SUPPORTS_SSE4_1)
template<bool RGB>
void qt_qimageScaleAARGBA_up_x_down_y_sse4(QImageScaleInfo *isi, unsigned int *dest,
                                           int dxx, int dyy, int dx, int dy,
                                           int dw, int dh, int dow, int sow);
template<bool RGB>
void qt_qimageScaleAARGBA_down_x_up_y_sse4(QImageScaleInfo *isi, unsigned int *dest,
                                           int dxx, int dyy, int dx, int dy,
                                           int dw, int dh, int dow, int sow);
template<bool RGB>
void qt_qimageScaleAARGBA_down_xy_sse4(QImageScaleInfo *isi, unsigned int *dest,
                                       int dxx, int dyy, int dx, int dy,
                                       int dw, int dh, int dow, int sow);
#endif

static void qt_qimageScaleAARGBA_up_xy(QImageScaleInfo *isi, unsigned int *dest,
                                       int dxx, int dyy, int dx, int dy,
                                       int dw, int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    int end = dxx + dw;
    /* go through every scanline in the output buffer */
    for (int y = 0; y < dh; y++) {
        /* calculate the source line we'll scan from */
        const unsigned int *sptr = ypoints[dyy + y];
        unsigned int *dptr = dest + dx + ((y + dy) * dow);
        const int yap = yapoints[dyy + y];
        if (yap > 0) {
            for (int x = dxx; x < end; x++) {
                const unsigned int *pix = sptr + xpoints[x];
                const int xap = xapoints[x];
                if (xap > 0)
                    *dptr = interpolate_4_pixels(pix[0], pix[1], pix[sow], pix[sow + 1], xap, yap);
                else
                    *dptr = INTERPOLATE_PIXEL_256(pix[0], 256 - yap, pix[sow], yap);
                dptr++;
            }
        } else {
            for (int x = dxx; x < end; x++) {
                const unsigned int *pix = sptr + xpoints[x];
                const int xap = xapoints[x];
				if (xap > 0)
					*dptr = INTERPOLATE_PIXEL_256(pix[0], 256 - xap, pix[1], xap);
				else
					*dptr = pix[0];
                dptr++;
            }
        }
    }
}

/* scale by area sampling */
static void qt_qimageScaleAARGBA(QImageScaleInfo *isi, unsigned int *dest,
                                 int dxx, int dyy, int dx, int dy, int dw,
                                 int dh, int dow, int sow)
{
    /* scaling up both ways */
    if (isi->xup_yup == 3){
        qt_qimageScaleAARGBA_up_xy(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
    }
    /* if we're scaling down vertically */
    else if (isi->xup_yup == 1) {
#ifdef QT_COMPILER_SUPPORTS_SSE4_1
        if (qCpuHasFeature(SSE4_1))
            qt_qimageScaleAARGBA_up_x_down_y_sse4<false>(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
        else
#endif
        qt_qimageScaleAARGBA_up_x_down_y(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
    }
    /* if we're scaling down horizontally */
    else if (isi->xup_yup == 2) {
#ifdef QT_COMPILER_SUPPORTS_SSE4_1
        if (qCpuHasFeature(SSE4_1))
            qt_qimageScaleAARGBA_down_x_up_y_sse4<false>(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
        else
#endif
        qt_qimageScaleAARGBA_down_x_up_y(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
    }
    /* if we're scaling down horizontally & vertically */
    else {
#ifdef QT_COMPILER_SUPPORTS_SSE4_1
        if (qCpuHasFeature(SSE4_1))
            qt_qimageScaleAARGBA_down_xy_sse4<false>(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
        else
#endif
        qt_qimageScaleAARGBA_down_xy(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
    }
}

inline static void qt_qimageScaleAARGBA_helper(const unsigned int *pix, int xyap, int Cxy, int step, int &r, int &g, int &b, int &a)
{
    r = qRed(*pix)   * xyap;
    g = qGreen(*pix) * xyap;
    b = qBlue(*pix)  * xyap;
    a = qAlpha(*pix) * xyap;
    int j;
    for (j = (1 << 14) - xyap; j > Cxy; j -= Cxy) {
        pix += step;
        r += qRed(*pix)   * Cxy;
        g += qGreen(*pix) * Cxy;
        b += qBlue(*pix)  * Cxy;
        a += qAlpha(*pix) * Cxy;
    }
    pix += step;
    r += qRed(*pix)   * j;
    g += qGreen(*pix) * j;
    b += qBlue(*pix)  * j;
    a += qAlpha(*pix) * j;
}

static void qt_qimageScaleAARGBA_up_x_down_y(QImageScaleInfo *isi, unsigned int *dest,
                                             int dxx, int dyy, int dx, int dy,
                                             int dw, int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    int end = dxx + dw;

    /* go through every scanline in the output buffer */
    for (int y = 0; y < dh; y++) {
        int Cy = (yapoints[dyy + y]) >> 16;
        int yap = (yapoints[dyy + y]) & 0xffff;

        unsigned int *dptr = dest + dx + ((y + dy) * dow);
        for (int x = dxx; x < end; x++) {
            const unsigned int *sptr = ypoints[dyy + y] + xpoints[x];
            int r, g, b, a;
            qt_qimageScaleAARGBA_helper(sptr, yap, Cy, sow, r, g, b, a);

            int xap = xapoints[x];
            if (xap > 0) {
                int rr, gg, bb, aa;
                qt_qimageScaleAARGBA_helper(sptr + 1, yap, Cy, sow, rr, gg, bb, aa);

                r = r * (256 - xap);
                g = g * (256 - xap);
                b = b * (256 - xap);
                a = a * (256 - xap);
                r = (r + (rr * xap)) >> 8;
                g = (g + (gg * xap)) >> 8;
                b = (b + (bb * xap)) >> 8;
                a = (a + (aa * xap)) >> 8;
            }
            *dptr++ = qRgba(r >> 14, g >> 14, b >> 14, a >> 14);
        }
    }
}

static void qt_qimageScaleAARGBA_down_x_up_y(QImageScaleInfo *isi, unsigned int *dest,
                                             int dxx, int dyy, int dx, int dy,
                                             int dw, int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    int end = dxx + dw;

    /* go through every scanline in the output buffer */
    for (int y = 0; y < dh; y++) {
        unsigned int *dptr = dest + dx + ((y + dy) * dow);
        for (int x = dxx; x < end; x++) {
            int Cx = xapoints[x] >> 16;
            int xap = xapoints[x] & 0xffff;

            const unsigned int *sptr = ypoints[dyy + y] + xpoints[x];
            int r, g, b, a;
            qt_qimageScaleAARGBA_helper(sptr, xap, Cx, 1, r, g, b, a);

            int yap = yapoints[dyy + y];
            if (yap > 0) {
                int rr, gg, bb, aa;
                qt_qimageScaleAARGBA_helper(sptr + sow, xap, Cx, 1, rr, gg, bb, aa);

                r = r * (256 - yap);
                g = g * (256 - yap);
                b = b * (256 - yap);
                a = a * (256 - yap);
                r = (r + (rr * yap)) >> 8;
                g = (g + (gg * yap)) >> 8;
                b = (b + (bb * yap)) >> 8;
                a = (a + (aa * yap)) >> 8;
            }
            *dptr = qRgba(r >> 14, g >> 14, b >> 14, a >> 14);
            dptr++;
        }
    }
}

static void qt_qimageScaleAARGBA_down_xy(QImageScaleInfo *isi, unsigned int *dest,
                                         int dxx, int dyy, int dx, int dy, int dw,
                                         int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    int end = dxx + dw;

    for (int y = 0; y < dh; y++) {
        int Cy = (yapoints[dyy + y]) >> 16;
        int yap = (yapoints[dyy + y]) & 0xffff;

        unsigned int *dptr = dest + dx + ((y + dy) * dow);
        for (int x = dxx; x < end; x++) {
            int Cx = xapoints[x] >> 16;
            int xap = xapoints[x] & 0xffff;

            const unsigned int *sptr = ypoints[dyy + y] + xpoints[x];
            int rx, gx, bx, ax;
            qt_qimageScaleAARGBA_helper(sptr, xap, Cx, 1, rx, gx, bx, ax);

            int r = ((rx>>4) * yap);
            int g = ((gx>>4) * yap);
            int b = ((bx>>4) * yap);
            int a = ((ax>>4) * yap);

            int j;
            for (j = (1 << 14) - yap; j > Cy; j -= Cy) {
                sptr += sow;
                qt_qimageScaleAARGBA_helper(sptr, xap, Cx, 1, rx, gx, bx, ax);
                r += ((rx>>4) * Cy);
                g += ((gx>>4) * Cy);
                b += ((bx>>4) * Cy);
                a += ((ax>>4) * Cy);
            }
            sptr += sow;
            qt_qimageScaleAARGBA_helper(sptr, xap, Cx, 1, rx, gx, bx, ax);

            r += ((rx>>4) * j);
            g += ((gx>>4) * j);
            b += ((bx>>4) * j);
            a += ((ax>>4) * j);

            *dptr = qRgba(r >> 24, g >> 24, b >> 24, a >> 24);
            dptr++;
        }
    }
}

static void qt_qimageScaleAARGB_up_x_down_y(QImageScaleInfo *isi, unsigned int *dest,
                                            int dxx, int dyy, int dx, int dy, int dw,
                                            int dh, int dow, int sow);

static void qt_qimageScaleAARGB_down_x_up_y(QImageScaleInfo *isi, unsigned int *dest,
                                            int dxx, int dyy, int dx, int dy, int dw,
                                            int dh, int dow, int sow);

static void qt_qimageScaleAARGB_down_xy(QImageScaleInfo *isi, unsigned int *dest,
                                        int dxx, int dyy, int dx, int dy, int dw,
                                        int dh, int dow, int sow);

/* scale by area sampling - IGNORE the ALPHA byte*/
static void qt_qimageScaleAARGB(QImageScaleInfo *isi, unsigned int *dest,
                                int dxx, int dyy, int dx, int dy,
                                int dw, int dh, int dow, int sow)
{
    /* scaling up both ways */
    if (isi->xup_yup == 3) {
        qt_qimageScaleAARGBA_up_xy(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
    }
    /* if we're scaling down vertically */
    else if (isi->xup_yup == 1) {
#ifdef QT_COMPILER_SUPPORTS_SSE4_1
        if (qCpuHasFeature(SSE4_1))
            qt_qimageScaleAARGBA_up_x_down_y_sse4<true>(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
        else
#endif
        qt_qimageScaleAARGB_up_x_down_y(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
    }
    /* if we're scaling down horizontally */
    else if (isi->xup_yup == 2) {
#ifdef QT_COMPILER_SUPPORTS_SSE4_1
        if (qCpuHasFeature(SSE4_1))
            qt_qimageScaleAARGBA_down_x_up_y_sse4<true>(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
        else
#endif
        qt_qimageScaleAARGB_down_x_up_y(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
    }
    /* if we're scaling down horizontally & vertically */
    else {
#ifdef QT_COMPILER_SUPPORTS_SSE4_1
        if (qCpuHasFeature(SSE4_1))
            qt_qimageScaleAARGBA_down_xy_sse4<true>(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
        else
#endif
        qt_qimageScaleAARGB_down_xy(isi, dest, dxx, dyy, dx, dy, dw, dh, dow, sow);
    }
}


inline static void qt_qimageScaleAARGB_helper(const unsigned int *pix, int xyap, int Cxy, int step, int &r, int &g, int &b)
{
    r = qRed(*pix)   * xyap;
    g = qGreen(*pix) * xyap;
    b = qBlue(*pix)  * xyap;
    int j;
    for (j = (1 << 14) - xyap; j > Cxy; j -= Cxy) {
        pix += step;
        r += qRed(*pix)   * Cxy;
        g += qGreen(*pix) * Cxy;
        b += qBlue(*pix)  * Cxy;
    }
    pix += step;
    r += qRed(*pix)   * j;
    g += qGreen(*pix) * j;
    b += qBlue(*pix)  * j;
}

static void qt_qimageScaleAARGB_up_x_down_y(QImageScaleInfo *isi, unsigned int *dest,
                                            int dxx, int dyy, int dx, int dy, int dw,
                                            int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    int end = dxx + dw;

    /* go through every scanline in the output buffer */
    for (int y = 0; y < dh; y++) {
        int Cy = (yapoints[dyy + y]) >> 16;
        int yap = (yapoints[dyy + y]) & 0xffff;

        unsigned int *dptr = dest + dx + ((y + dy) * dow);
        for (int x = dxx; x < end; x++) {
            const unsigned int *sptr = ypoints[dyy + y] + xpoints[x];
            int r, g, b;
            qt_qimageScaleAARGB_helper(sptr, yap, Cy, sow, r, g, b);

            int xap = xapoints[x];
            if (xap > 0) {
                int rr, bb, gg;
                qt_qimageScaleAARGB_helper(sptr + 1, yap, Cy, sow, rr, gg, bb);

                r = r * (256 - xap);
                g = g * (256 - xap);
                b = b * (256 - xap);
                r = (r + (rr * xap)) >> 8;
                g = (g + (gg * xap)) >> 8;
                b = (b + (bb * xap)) >> 8;
            }
            *dptr++ = qRgb(r >> 14, g >> 14, b >> 14);
        }
    }
}

static void qt_qimageScaleAARGB_down_x_up_y(QImageScaleInfo *isi, unsigned int *dest,
                                            int dxx, int dyy, int dx, int dy, int dw,
                                            int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    int end = dxx + dw;

    /* go through every scanline in the output buffer */
    for (int y = 0; y < dh; y++) {
        unsigned int *dptr = dest + dx + ((y + dy) * dow);
        for (int x = dxx; x < end; x++) {
            int Cx = xapoints[x] >> 16;
            int xap = xapoints[x] & 0xffff;

            const unsigned int *sptr = ypoints[dyy + y] + xpoints[x];
            int r, g, b;
            qt_qimageScaleAARGB_helper(sptr, xap, Cx, 1, r, g, b);

            int yap = yapoints[dyy + y];
            if (yap > 0) {
                int rr, bb, gg;
                qt_qimageScaleAARGB_helper(sptr + sow, xap, Cx, 1, rr, gg, bb);

                r = r * (256 - yap);
                g = g * (256 - yap);
                b = b * (256 - yap);
                r = (r + (rr * yap)) >> 8;
                g = (g + (gg * yap)) >> 8;
                b = (b + (bb * yap)) >> 8;
            }
            *dptr++ = qRgb(r >> 14, g >> 14, b >> 14);
        }
    }
}

static void qt_qimageScaleAARGB_down_xy(QImageScaleInfo *isi, unsigned int *dest,
                                        int dxx, int dyy, int dx, int dy, int dw,
                                        int dh, int dow, int sow)
{
    const unsigned int **ypoints = isi->ypoints;
    int *xpoints = isi->xpoints;
    int *xapoints = isi->xapoints;
    int *yapoints = isi->yapoints;

    int end = dxx + dw;

    for (int y = 0; y < dh; y++) {
        int Cy = (yapoints[dyy + y]) >> 16;
        int yap = (yapoints[dyy + y]) & 0xffff;

        unsigned int *dptr = dest + dx + ((y + dy) * dow);
        for (int x = dxx; x < end; x++) {
            int Cx = xapoints[x] >> 16;
            int xap = xapoints[x] & 0xffff;

            const unsigned int *sptr = ypoints[dyy + y] + xpoints[x];
            int rx, gx, bx;
            qt_qimageScaleAARGB_helper(sptr, xap, Cx, 1, rx, gx, bx);

            int r = (rx >> 4) * yap;
            int g = (gx >> 4) * yap;
            int b = (bx >> 4) * yap;

            int j;
            for (j = (1 << 14) - yap; j > Cy; j -= Cy) {
                sptr += sow;
                qt_qimageScaleAARGB_helper(sptr, xap, Cx, 1, rx, gx, bx);

                r += (rx >> 4) * Cy;
                g += (gx >> 4) * Cy;
                b += (bx >> 4) * Cy;
            }
            sptr += sow;
            qt_qimageScaleAARGB_helper(sptr, xap, Cx, 1, rx, gx, bx);

            r += (rx >> 4) * j;
            g += (gx >> 4) * j;
            b += (bx >> 4) * j;

            *dptr = qRgb(r >> 24, g >> 24, b >> 24);
            dptr++;
        }
    }
}

QImage qSmoothScaleImage(const QImage &src, int dw, int dh)
{
    QImage buffer;
    if (src.isNull() || dw <= 0 || dh <= 0)
        return buffer;

    int w = src.width();
    int h = src.height();
    QImageScaleInfo *scaleinfo =
        qimageCalcScaleInfo(src, w, h, dw, dh, true);
    if (!scaleinfo)
        return buffer;

    buffer = QImage(dw, dh, src.format());
    if (buffer.isNull()) {
        qWarning("QImage: out of memory, returning null");
        qimageFreeScaleInfo(scaleinfo);
        return QImage();
    }

    if (src.hasAlphaChannel())
        qt_qimageScaleAARGBA(scaleinfo, (unsigned int *)buffer.scanLine(0),
                             0, 0, 0, 0, dw, dh, dw, src.bytesPerLine() / 4);
    else
        qt_qimageScaleAARGB(scaleinfo, (unsigned int *)buffer.scanLine(0),
                            0, 0, 0, 0, dw, dh, dw, src.bytesPerLine() / 4);

    qimageFreeScaleInfo(scaleinfo);
    return buffer;
}

QT_END_NAMESPACE
