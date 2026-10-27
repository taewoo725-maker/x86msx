/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* wpa.c 시험: 표준 시험값 (FIPS-197, RFC 2202, RFC 3394, RFC 3610, IEEE 802.11i H.4) 과 파이썬 표준 라이브러리 대조용 출력 */
#include <stdio.h>
#include <string.h>
#include "../uefi/wpa.h"
static int fails;
static void chk(const char *n, const uint8_t *a, const uint8_t *b, int len) { if (memcmp(a, b, len)) { printf("FAIL %s\n  got:", n); for (int i = 0; i < len; i++) printf("%02x", a[i]); printf("\n  exp:"); for (int i = 0; i < len; i++) printf("%02x", b[i]); printf("\n"); fails++; } else printf("ok   %s\n", n); }
static int hex(const char *s, uint8_t *o) { int n = 0; while (s[0] && s[1]) { unsigned v; sscanf(s, "%2x", &v); o[n++] = (uint8_t)v; s += 2; } return n; }
int main(int argc, char **argv)
{
    uint8_t k[32], x[64], y[64], o[64]; Aes128 a;
    hex("000102030405060708090a0b0c0d0e0f", k); hex("00112233445566778899aabbccddeeff", x); hex("69c4e0d86a7b0430d8cdb78070b4c55a", y);
    aes_key(&a, k); aes_enc(&a, x, o); chk("AES-128 encrypt (FIPS-197)", o, y, 16); aes_dec(&a, y, o); chk("AES-128 decrypt", o, x, 16);
    memset(k, 0x0b, 20); wpa_hmac_sha1(k, 20, (const uint8_t *)"Hi There", 8, o); hex("b617318655057264e28bc0b6fb378c8ef146be00", y); chk("HMAC-SHA1 (RFC 2202 #1)", o, y, 20);
    wpa_sha1((const uint8_t *)"abc", 3, o); hex("a9993e364706816aba3e25717850c26c9cd0d89d", y); chk("SHA-1 abc", o, y, 20);
    wpa_pmk("password", (const uint8_t *)"IEEE", 4, o); hex("f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e", y); chk("PBKDF2 PMK (802.11i H.4 #1)", o, y, 32);
    wpa_pmk("ThisIsAPassword", (const uint8_t *)"ThisIsASSID", 11, o); hex("0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af", y); chk("PBKDF2 PMK (802.11i H.4 #2)", o, y, 32);
    hex("000102030405060708090A0B0C0D0E0F", k); hex("00112233445566778899AABBCCDDEEFF", x); hex("1FA68B0A8112B447AEF34BD8FB5A7B829D3E862371D2CFE5", y);
    int n = wpa_unwrap(k, y, 24, o); chk("AES key unwrap (RFC 3394 4.1)", o, x, 16); if (n != 16) { printf("FAIL unwrap len %d\n", n); fails++; }
    /* RFC 3610 packet vector #1 */
    hex("C0C1C2C3C4C5C6C7C8C9CACBCCCDCECF", k); aes_key(&a, k);
    uint8_t nonce[13], aad[8], pt[23], exp[31]; hex("00000003020100A0A1A2A3A4A5", nonce); hex("0001020304050607", aad); hex("08090A0B0C0D0E0F101112131415161718191A1B1C1D1E", pt);
    hex("588C979A61C663D2F066D0C2C0F989806D5F6B61DAC38417E8D12CFDF926E0", exp);
    ccm_encrypt(&a, 8, 2, nonce, aad, 8, pt, 23, o); chk("CCM encrypt (RFC 3610 #1)", o, exp, 31);
    n = ccm_decrypt(&a, 8, 2, nonce, aad, 8, exp, 31, y); chk("CCM decrypt", y, pt, 23); if (n) { printf("FAIL ccm mic\n"); fails++; }
    exp[3] ^= 1; if (ccm_decrypt(&a, 8, 2, nonce, aad, 8, exp, 31, y) == 0) { printf("FAIL ccm tamper not detected\n"); fails++; } else printf("ok   CCM tamper detected\n");
    /* 파이썬 대조용: 인자 'dump' 이면 무작위 입력의 결과를 출력 */
    if (argc > 1) {
        uint8_t pmk[32], ptk[48], aa[6] = {1,2,3,4,5,6}, sp[6] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff}, an[32], sn[32], hdr[26], pn[6] = {1,2,3,4,5,6}, body[200], key[16];
        for (int i = 0; i < 32; i++) { an[i] = (uint8_t)(i * 7 + 1); sn[i] = (uint8_t)(i * 13 + 5); }
        wpa_pmk("12345678", (const uint8_t *)"AP-15-502", 9, pmk); printf("PMK "); for (int i = 0; i < 32; i++) printf("%02x", pmk[i]); printf("\n");
        wpa_ptk(pmk, aa, sp, an, sn, ptk); printf("PTK "); for (int i = 0; i < 48; i++) printf("%02x", ptk[i]); printf("\n");
        for (int i = 0; i < 26; i++) hdr[i] = (uint8_t)(i * 3 + 0x11); hdr[0] = 0x88; hdr[1] = 0x42; hdr[24] = 5; hdr[25] = 0;
        for (int i = 0; i < 16; i++) key[i] = (uint8_t)(i * 11 + 3);
        uint8_t msg[60]; for (int i = 0; i < 60; i++) msg[i] = (uint8_t)(i * 5); aes_key(&a, key);
        int bl = ccmp_encrypt(&a, hdr, 26, pn, 0, msg, 60, body); printf("CCMP_QOS "); for (int i = 0; i < bl; i++) printf("%02x", body[i]); printf("\n");
        uint8_t h2[24]; for (int i = 0; i < 24; i++) h2[i] = (uint8_t)(i * 5 + 7); h2[0] = 0x08; h2[1] = 0x41;
        bl = ccmp_encrypt(&a, h2, 24, pn, 0, msg, 33, body); printf("CCMP_NOQOS "); for (int i = 0; i < bl; i++) printf("%02x", body[i]); printf("\n");
        uint8_t pt2[60], pn2[6]; bl = ccmp_decrypt(&a, h2, 24, body, bl, pt2, pn2); printf("ROUNDTRIP %d %d\n", bl, memcmp(pt2, msg, 33));
    }
    printf(fails ? "FAILS: %d\n" : "ALL OK\n", fails);
    return fails != 0;
}
