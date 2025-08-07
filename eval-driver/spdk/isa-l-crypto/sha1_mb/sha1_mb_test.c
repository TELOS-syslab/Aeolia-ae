/**********************************************************************
  Copyright(c) 2011-2016 Intel Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha1_mb.h"
#include "endian_helper.h"

typedef uint32_t DigestSHA1[ISAL_SHA1_DIGEST_NWORDS];

#define MSGS     7
#define NUM_JOBS 1000

#define PSEUDO_RANDOM_NUM(seed) ((seed) * 5 + ((seed) * (seed)) / 64) % MSGS
static uint8_t msg1[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static DigestSHA1 expResultDigest1 = { 0x84983E44, 0x1C3BD26E, 0xBAAE4AA1, 0xF95129E5, 0xE54670F1 };

static uint8_t msg2[] = "0123456789:;<=>?@ABCDEFGHIJKLMNO";
static DigestSHA1 expResultDigest2 = { 0xB7C66452, 0x0FD122B3, 0x55D539F2, 0xA35E6FAA, 0xC2A5A11D };

static uint8_t msg3[] = "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<";
static DigestSHA1 expResultDigest3 = { 0x127729B6, 0xA8B2F8A0, 0xA4DDC819, 0x08E1D8B3, 0x67CEEA55 };

static uint8_t msg4[] = "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQR";
static DigestSHA1 expResultDigest4 = { 0xFDDE2D00, 0xABD5B7A3, 0x699DE6F2, 0x3FF1D1AC, 0x3B872AC2 };

static uint8_t msg5[] = "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?";
static DigestSHA1 expResultDigest5 = { 0xE7FCA85C, 0xA4AB3740, 0x6A180B32, 0x0B8D362C, 0x622A96E6 };

static uint8_t msg6[] = "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
                        "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTU";
static DigestSHA1 expResultDigest6 = { 0x505B0686, 0xE1ACDF42, 0xB3588B5A, 0xB043D52C, 0x6D8C7444 };

static uint8_t msg7[] = "";
static DigestSHA1 expResultDigest7 = { 0xDA39A3EE, 0x5E6B4B0D, 0x3255BFEF, 0x95601890, 0xAFD80709 };

static uint8_t *msgs[MSGS] = { msg1, msg2, msg3, msg4, msg5, msg6, msg7 };

static uint32_t *expResultDigest[MSGS] = { expResultDigest1, expResultDigest2, expResultDigest3,
                                           expResultDigest4, expResultDigest5, expResultDigest6,
                                           expResultDigest7 };

#define NUM_CHUNKS   4
#define DATA_BUF_LEN 4096
static int
non_blocksize_updates_test(ISAL_SHA1_HASH_CTX_MGR *mgr)
{
        ISAL_SHA1_HASH_CTX ctx_refer;
        ISAL_SHA1_HASH_CTX ctx_pool[NUM_CHUNKS];
        ISAL_SHA1_HASH_CTX *ctx = NULL;
        int rc;

        const int update_chunks[NUM_CHUNKS] = { 32, 64, 128, 256 };
        unsigned char data_buf[DATA_BUF_LEN];

        memset(data_buf, 0xA, DATA_BUF_LEN);

        // Init contexts before first use
        isal_hash_ctx_init(&ctx_refer);

        rc = isal_sha1_ctx_mgr_submit(mgr, &ctx_refer, &ctx, data_buf, DATA_BUF_LEN,
                                      ISAL_HASH_ENTIRE);
        if (rc)
                return -1;

        rc = isal_sha1_ctx_mgr_flush(mgr, &ctx);
        if (rc)
                return -1;

        for (int c = 0; c < NUM_CHUNKS; c++) {
                int chunk = update_chunks[c];
                isal_hash_ctx_init(&ctx_pool[c]);
                rc = isal_sha1_ctx_mgr_submit(mgr, &ctx_pool[c], &ctx, NULL, 0, ISAL_HASH_FIRST);
                if (rc)
                        return -1;
                rc = isal_sha1_ctx_mgr_flush(mgr, &ctx);
                if (rc)
                        return -1;
                for (int i = 0; i * chunk < DATA_BUF_LEN; i++) {
                        rc = isal_sha1_ctx_mgr_submit(mgr, &ctx_pool[c], &ctx, data_buf + i * chunk,
                                                      chunk, ISAL_HASH_UPDATE);
                        if (rc)
                                return -1;
                        rc = isal_sha1_ctx_mgr_flush(mgr, &ctx);
                        if (rc)
                                return -1;
                }
        }

        for (int c = 0; c < NUM_CHUNKS; c++) {
                rc = isal_sha1_ctx_mgr_submit(mgr, &ctx_pool[c], &ctx, NULL, 0, ISAL_HASH_LAST);
                if (rc)
                        return -1;
                rc = isal_sha1_ctx_mgr_flush(mgr, &ctx);
                if (rc)
                        return -1;
                if (ctx_pool[c].status != ISAL_HASH_CTX_STS_COMPLETE) {
                        return -1;
                }
                for (int i = 0; i < ISAL_SHA1_DIGEST_NWORDS; i++) {
                        if (ctx_refer.job.result_digest[i] != ctx_pool[c].job.result_digest[i]) {
                                printf("sm3 calc error! chunk %d, digest[%d], (%d) != (%d)\n",
                                       update_chunks[c], i, ctx_refer.job.result_digest[i],
                                       ctx_pool[c].job.result_digest[i]);
                                return -2;
                        }
                }
        }
        return 0;
}

int
main(void)
{
        ISAL_SHA1_HASH_CTX_MGR *mgr = NULL;
        ISAL_SHA1_HASH_CTX ctxpool[NUM_JOBS], *ctx = NULL;
        uint32_t i, j, k, t, checked = 0;
        uint32_t *good;
        int rc, ret = -1;

        rc = posix_memalign((void *) &mgr, 16, sizeof(ISAL_SHA1_HASH_CTX_MGR));
        if ((rc != 0) || (mgr == NULL)) {
                printf("posix_memalign failed test aborted\n");
                return 1;
        }

        rc = isal_sha1_ctx_mgr_init(mgr);
        if (rc)
                goto end;

        // Init contexts before first use
        for (i = 0; i < MSGS; i++) {
                isal_hash_ctx_init(&ctxpool[i]);
                ctxpool[i].user_data = (void *) ((uint64_t) i);
        }

        for (i = 0; i < MSGS; i++) {
                rc = isal_sha1_ctx_mgr_submit(mgr, &ctxpool[i], &ctx, msgs[i],
                                              (uint32_t) strlen((char *) msgs[i]),
                                              ISAL_HASH_ENTIRE);
                if (rc)
                        goto end;

                if (ctx) {
                        t = (uint32_t) ((uintptr_t) (ctx->user_data));
                        good = expResultDigest[t];
                        checked++;
                        for (j = 0; j < ISAL_SHA1_DIGEST_NWORDS; j++) {
                                if (good[j] != ctxpool[t].job.result_digest[j]) {
                                        printf("Test %d, digest %d is %08X, should be %08X\n", t, j,
                                               ctxpool[t].job.result_digest[j], good[j]);
                                        goto end;
                                }
                        }

                        if (ctx->error) {
                                printf("Something bad happened during the submit."
                                       " Error code: %d",
                                       ctx->error);
                                goto end;
                        }
                }
        }

        while (1) {
                rc = isal_sha1_ctx_mgr_flush(mgr, &ctx);
                if (rc)
                        goto end;

                if (ctx) {
                        t = (uint32_t) ((uintptr_t) (ctx->user_data));
                        good = expResultDigest[t];
                        checked++;
                        for (j = 0; j < ISAL_SHA1_DIGEST_NWORDS; j++) {
                                if (good[j] != ctxpool[t].job.result_digest[j]) {
                                        printf("Test %d, digest %d is %08X, should be %08X\n", t, j,
                                               ctxpool[t].job.result_digest[j], good[j]);
                                        goto end;
                                }
                        }

                        if (ctx->error) {
                                printf("Something bad happened during the submit."
                                       " Error code: %d",
                                       ctx->error);
                                goto end;
                        }
                } else {
                        break;
                }
        }

        // do larger test in pseudo-random order

        // Init contexts before first use
        for (i = 0; i < NUM_JOBS; i++) {
                isal_hash_ctx_init(&ctxpool[i]);
                ctxpool[i].user_data = (void *) ((uint64_t) i);
        }

        checked = 0;
        for (i = 0; i < NUM_JOBS; i++) {
                j = PSEUDO_RANDOM_NUM(i);
                rc = isal_sha1_ctx_mgr_submit(mgr, &ctxpool[i], &ctx, msgs[j],
                                              (uint32_t) strlen((char *) msgs[j]),
                                              ISAL_HASH_ENTIRE);
                if (rc)
                        goto end;
                if (ctx) {
                        t = (uint32_t) ((uintptr_t) (ctx->user_data));
                        k = PSEUDO_RANDOM_NUM(t);
                        good = expResultDigest[k];
                        checked++;
                        for (j = 0; j < ISAL_SHA1_DIGEST_NWORDS; j++) {
                                if (good[j] != ctxpool[t].job.result_digest[j]) {
                                        printf("Test %d, digest %d is %08X, should be %08X\n", t, j,
                                               ctxpool[t].job.result_digest[j], good[j]);
                                        goto end;
                                }
                        }

                        if (ctx->error) {
                                printf("Something bad happened during the"
                                       " submit. Error code: %d",
                                       ctx->error);
                                goto end;
                        }

                        t = (uint32_t) ((uintptr_t) (ctx->user_data));
                        k = PSEUDO_RANDOM_NUM(t);
                }
        }
        while (1) {
                rc = isal_sha1_ctx_mgr_flush(mgr, &ctx);

                if (rc)
                        goto end;
                if (ctx) {
                        t = (uint32_t) ((uintptr_t) (ctx->user_data));
                        k = PSEUDO_RANDOM_NUM(t);
                        good = expResultDigest[k];
                        checked++;
                        for (j = 0; j < ISAL_SHA1_DIGEST_NWORDS; j++) {
                                if (good[j] != ctxpool[t].job.result_digest[j]) {
                                        printf("Test %d, digest %d is %08X, should be %08X\n", t, j,
                                               ctxpool[t].job.result_digest[j], good[j]);
                                        goto end;
                                }
                        }

                        if (ctx->error) {
                                printf("Something bad happened during the submit."
                                       " Error code: %d",
                                       ctx->error);
                                goto end;
                        }
                } else {
                        break;
                }
        }

        if (checked != NUM_JOBS) {
                printf("only tested %d rather than %d\n", checked, NUM_JOBS);
                goto end;
        }

        rc = non_blocksize_updates_test(mgr);
        if (rc) {
                printf("multi updates test fail %d\n", rc);
                goto end;
        }
        ret = 0;

        printf(" multibinary_sha1 test: Pass\n");
end:
        aligned_free(mgr);

        return ret;
}
