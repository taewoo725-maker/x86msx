/* X86MSX 용 최소 틀: Simon Howard 의 lhasa(ISC 라이선스, LICENSE 참조) 중 PMarc 해독기(pm1·pm2)만 가져다 쓰기 위한 머리글.
 * 원래 lib/lha_decoder.h + lib/public/lha_decoder.h 에서 해독기가 쓰는 부분만 남김. */
#ifndef LHASA_LHA_DECODER_H
#define LHASA_LHA_DECODER_H
#include <stddef.h>
#include <stdint.h>

typedef size_t (*LHADecoderCallback)(void *buf, size_t buf_len, void *user_data);
typedef struct _LHADecoderType LHADecoderType;
struct _LHADecoderType {
	int (*init)(void *extra_data, LHADecoderCallback callback, void *callback_data);
	void (*free)(void *extra_data);
	size_t (*read)(void *extra_data, uint8_t *buf);
	size_t extra_size;      /* 해독기가 쓸 상태 크기 */
	size_t max_read;        /* read() 한 번에 나올 수 있는 최대 바이트 */
	size_t block_size;
};
extern const LHADecoderType lha_pm1_decoder;
extern const LHADecoderType lha_pm2_decoder;
extern const LHADecoderType lha_lh1_decoder, lha_lz5_decoder, lha_lzs_decoder;
#endif
