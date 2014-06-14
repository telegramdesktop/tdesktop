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

class mtpPublicRSA {
public:
	mtpPublicRSA(const char *key) : data(new mtpPublicRSAInner(PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(key), -1), 0, 0, 0), 0)) {
		if (!data->prsa) return;
			
		int32 nBytes = BN_num_bytes(data->prsa->n);
		int32 eBytes = BN_num_bytes(data->prsa->e);
		string nStr(nBytes, 0), eStr(eBytes, 0);
		BN_bn2bin(data->prsa->n, (uchar*)&nStr[0]);
		BN_bn2bin(data->prsa->e, (uchar*)&eStr[0]);

		mtpBuffer tmp;
		MTP_string(nStr).write(tmp);
		MTP_string(eStr).write(tmp);

		uchar sha1Buffer[20];
		data->fp = *(uint64*)(hashSha1(&tmp[0], tmp.size() * sizeof(mtpPrime), sha1Buffer) + 3);
	}

	mtpPublicRSA(const mtpPublicRSA &v) : data(v.data) {
		++data->cnt;
	}

	mtpPublicRSA &operator=(const mtpPublicRSA &v) {
		if (data != v.data) {
			destroy();
			data = v.data;
			++data->cnt;
		}
		return *this;
	}

	uint64 fingerPrint() const {
		return data->fp;
	}

	RSA *key() {
		return data->prsa;
	}

	~mtpPublicRSA() {
		destroy();
	}

private:
	void destroy() {
		if (!--data->cnt) {
			delete data;
		}
	}

	struct mtpPublicRSAInner {
		mtpPublicRSAInner(RSA *_prsa, uint64 _fp) : prsa(_prsa), cnt(1), fp(_fp) {
		}
		~mtpPublicRSAInner() {
			RSA_free(prsa);
		}
		RSA *prsa;
		uint32 cnt;
		uint64 fp;
	};
	mtpPublicRSAInner *data;
};
