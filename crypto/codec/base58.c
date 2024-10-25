#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <psa/crypto.h>
#include "base58.h"

static bool psa_sha256_wrapper(void *output, const void *input, size_t input_len)
{
	psa_status_t status;
	size_t output_len;

	status = psa_hash_compute(PSA_ALG_SHA_256,
				  input, input_len,
				  output, 32,
				  &output_len);

	return (status == PSA_SUCCESS && output_len == 32);
}

bool (*b58_sha256_impl)(void *, const void *, size_t) = psa_sha256_wrapper;

static const int8_t b58digits_map[] = {
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    -1,
    17,
    18,
    19,
    20,
    21,
    -1,
    22,
    23,
    24,
    25,
    26,
    27,
    28,
    29,
    30,
    31,
    32,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    33,
    34,
    35,
    36,
    37,
    38,
    39,
    40,
    41,
    42,
    43,
    -1,
    44,
    45,
    46,
    47,
    48,
    49,
    50,
    51,
    52,
    53,
    54,
    55,
    56,
    57,
    -1,
    -1,
    -1,
    -1,
    -1,
};

typedef uint64_t b58_uint64_t;
typedef uint32_t b58_uint32_t;
#define B58_UINT32_BITS (sizeof(b58_uint32_t) * 8)
static const b58_uint32_t B58_UINT32_MASK = ((((b58_uint64_t)1) << B58_UINT32_BITS) - 1);

bool b58tobin(void *bin, size_t *binszp, const char *b58, size_t b58sz)
{
	size_t binsz = *binszp;
	const unsigned char *b58u = (void *)b58;
	unsigned char *binu = bin;
	size_t outisz = (binsz + sizeof(b58_uint32_t) - 1) / sizeof(b58_uint32_t);
	b58_uint32_t outi[outisz];
	b58_uint64_t t;
	b58_uint32_t c;
	size_t i, j;
	uint8_t bytesleft = binsz % sizeof(b58_uint32_t);
	b58_uint32_t zeromask = bytesleft ? (B58_UINT32_MASK << (bytesleft * 8)) : 0;
	unsigned zerocount = 0;

	if (!b58sz)
		b58sz = strlen(b58);

	for (i = 0; i < outisz; ++i)
	{
		outi[i] = 0;
	}

	// Leading zeros, just count
	for (i = 0; i < b58sz && b58u[i] == '1'; ++i)
		++zerocount;

	for (; i < b58sz; ++i)
	{
		if (b58u[i] & 0x80)
			// High-bit set on invalid digit
			return false;
		if (b58digits_map[b58u[i]] == -1)
			// Invalid base58 digit
			return false;
		c = (unsigned)b58digits_map[b58u[i]];
		for (j = outisz; j--;)
		{
			t = ((b58_uint64_t)outi[j]) * 58 + c;
			c = t >> B58_UINT32_BITS;
			outi[j] = t & B58_UINT32_MASK;
		}
		if (c)
			// Output number too big (carry to the next int32)
			return false;
		if (outi[0] & zeromask)
			// Output number too big (last int32 filled too far)
			return false;
	}

	j = 0;
	if (bytesleft)
	{
		for (i = bytesleft; i > 0; --i)
		{
			*(binu++) = (outi[0] >> (8 * (i - 1))) & 0xff;
		}
		++j;
	}

	for (; j < outisz; ++j)
	{
		for (i = sizeof(*outi); i > 0; --i)
		{
			*(binu++) = (outi[j] >> (8 * (i - 1))) & 0xff;
		}
	}

	// Count canonical base58 byte count
	binu = bin;
	for (i = 0; i < binsz; ++i)
	{
		if (binu[i])
			break;
		--*binszp;
	}
	*binszp += zerocount;

	return true;
}

static bool b58_dbl_sha256(void *hash, const void *data, size_t datasz)
{
	uint8_t buf[0x20];
	return b58_sha256_impl(buf, data, datasz) && b58_sha256_impl(hash, buf, sizeof(buf));
}

int b58check(const void *bin, size_t binsz, const char *base58str, size_t b58sz)
{
	unsigned char buf[32];
	const uint8_t *binc = bin;
	unsigned i;
	if (binsz < 4)
		return -4;
	if (!b58_dbl_sha256(buf, bin, binsz - 4))
		return -2;
	if (memcmp(&binc[binsz - 4], buf, 4))
		return -1;

	// Check number of zeros is correct AFTER verifying checksum (to avoid possibility of accessing base58str beyond the end)
	for (i = 0; binc[i] == '\0' && base58str[i] == '1'; ++i)
	{
	} // Just finding the end of zeros, nothing to do in loop
	if (binc[i] == '\0' || base58str[i] == '1')
		return -3;

	return binc[0];
}

static const char b58digits_ordered[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

bool b58enc(char *b58, size_t *b58sz, const void *data, size_t binsz)
{
	const uint8_t *bin = data;
	int carry;
	size_t i, j, high, zcount = 0;
	size_t size;

	while (zcount < binsz && !bin[zcount])
		++zcount;

	size = (binsz - zcount) * 138 / 100 + 1;
	uint8_t buf[size];
	memset(buf, 0, size);

	for (i = zcount, high = size - 1; i < binsz; ++i, high = j)
	{
		for (carry = bin[i], j = size - 1; (j > high) || carry; --j)
		{
			carry += 256 * buf[j];
			buf[j] = carry % 58;
			carry /= 58;
			if (!j)
			{
				// Otherwise j wraps to maxint which is > high
				break;
			}
		}
	}

	for (j = 0; j < size && !buf[j]; ++j)
		;

	if (*b58sz <= zcount + size - j)
	{
		*b58sz = zcount + size - j + 1;
		return false;
	}

	if (zcount)
		memset(b58, '1', zcount);
	for (i = zcount; j < size; ++i, ++j)
		b58[i] = b58digits_ordered[buf[j]];
	b58[i] = '\0';
	*b58sz = i + 1;

	return true;
}

bool b58check_enc(char *b58c, size_t *b58c_sz, uint8_t ver, const void *data, size_t datasz)
{
	uint8_t buf[1 + datasz + 0x20];
	uint8_t *hash = &buf[1 + datasz];

	buf[0] = ver;
	memcpy(&buf[1], data, datasz);
	if (!b58_dbl_sha256(hash, buf, datasz + 1))
	{
		*b58c_sz = 0;
		return false;
	}

	return b58enc(b58c, b58c_sz, buf, 1 + datasz + 4);
}

bool b58check_enc_rel(char *b58c, size_t *b58c_sz, uint8_t ver, const void *data, size_t datasz)
{
	uint8_t buf[datasz + 4];
	uint8_t hash[32];
	memcpy(buf, data, datasz);

	if (!b58_dbl_sha256(hash, data, datasz))
	{
		*b58c_sz = 0;
		return false;
	}
	memcpy(buf + datasz, hash, 4);
	return b58enc(b58c, b58c_sz, buf, datasz + 4);
}
