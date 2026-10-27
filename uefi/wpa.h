/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* wpa.h - WPA2-PSK(CCMP) 에 필요한 암호 부품 (직접 작성). 하드웨어와 무관하므로 PC 에서도 시험 가능 (tests/wpatest.c).
 * SHA-1, HMAC-SHA1, PBKDF2, PRF(PTK), AES-128, AES 키 풀기(RFC 3394), CCM(RFC 3610) / CCMP(IEEE 802.11) */
#ifndef WPA_H
#define WPA_H
#include <stdint.h>
void wpa_sha1(const uint8_t *d, uint32_t n, uint8_t out[20]);
void wpa_hmac_sha1(const uint8_t *key, int klen, const uint8_t *d, int n, uint8_t out[20]);
void wpa_pmk(const char *pass, const uint8_t *ssid, int ssidlen, uint8_t pmk[32]);                 /* PBKDF2-HMAC-SHA1, 4096회 */
void wpa_ptk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6], const uint8_t an[32], const uint8_t sn[32], uint8_t ptk[48]);
void wpa_mic(const uint8_t kck[16], const uint8_t *eapol, int len, uint8_t mic[16]);               /* eapol 의 MIC 칸은 0 이어야 함 */
int wpa_unwrap(const uint8_t kek[16], const uint8_t *in, int inlen, uint8_t *out);                 /* RFC 3394, 성공 시 출력 바이트 수 (inlen-8), 실패 -1 */
typedef struct { uint32_t rk[44]; } Aes128;
void aes_key(Aes128 *a, const uint8_t key[16]);
void aes_enc(const Aes128 *a, const uint8_t in[16], uint8_t out[16]);
void aes_dec(const Aes128 *a, const uint8_t in[16], uint8_t out[16]);
/* CCM: M(mic 길이), L(길이칸 바이트 수), nonce 는 15-L 바이트. out 은 len+M 바이트 */
void ccm_encrypt(const Aes128 *a, int M, int L, const uint8_t *nonce, const uint8_t *aad, int aadlen, const uint8_t *pt, int len, uint8_t *out);
int ccm_decrypt(const Aes128 *a, int M, int L, const uint8_t *nonce, const uint8_t *aad, int aadlen, const uint8_t *ct, int ctlen, uint8_t *out);   /* ctlen=len+M. 0=성공 */
/* CCMP: hdr 는 802.11 헤더(24 또는 26바이트, QoS 이면 26), pn[0..5]=PN0..PN5. 암호화: out = CCMP헤더8 + 암호문 + MIC8 */
int ccmp_encrypt(const Aes128 *a, const uint8_t *hdr, int hlen, const uint8_t pn[6], int keyid, const uint8_t *pt, int len, uint8_t *out);
int ccmp_decrypt(const Aes128 *a, const uint8_t *hdr, int hlen, const uint8_t *body, int blen, uint8_t *out, uint8_t pn[6]);   /* body=CCMP헤더+암호문+MIC. 반환 평문 길이, 실패 -1 */
/* TKIP 복호 (수신 전용): body=IV8+암호문+MIC8+ICV4. 반환 평문 길이(MIC 제외), -2=ICV 불일치, -1=형식 오류. Michael MIC 은 검사 안 함 */
int tkip_decrypt(const uint8_t tk[16], const uint8_t *hdr, int hlen, const uint8_t *body, int blen, uint8_t *out);
#endif
