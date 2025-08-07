#include <libltntstools/ts.h>
#include "nal_h264.h"
#include <inttypes.h>
#include "memmem.h"

//#include <libavutil/internal.h>
//#include <libavcodec/golomb.h>

int ltn_nal_h264_find_headers(const uint8_t *buf, int lengthBytes, struct ltn_nal_headers_s **array, int *arrayLength)
{
	int idx = 0;
	int maxitems = 64;
	struct ltn_nal_headers_s *a = malloc(sizeof(struct ltn_nal_headers_s) * maxitems);
	if (!a)
		return -1;

       const uint8_t start_code[3] = {0, 0, 1};
       const uint8_t *end = buf + lengthBytes;
       const uint8_t *p = buf;

       while (p < end - 3)
       {
               p = ltn_memmem(p, end - p, start_code, sizeof(start_code));
               if (!p)
                       break;

               if (idx >= maxitems)
               {
                       maxitems *= 2;
                       struct ltn_nal_headers_s *temp = realloc(a, sizeof(struct ltn_nal_headers_s) * maxitems);
                       if (!temp)
                       {
                               free(a);
                               return -1;
                       }
                       a = temp;
               }

               a[idx].ptr = p;
               a[idx].nalType = p[3] & 0x1f;
               a[idx].nalName = h264Nals_lookupName(a[idx].nalType);
               if (idx > 0)
               {
                       a[idx - 1].lengthBytes = p - a[idx - 1].ptr;
		}

		idx++;
               p += 3; // Move past start code
       }

       if (idx > 0)
       {
               a[idx - 1].lengthBytes = end - a[idx - 1].ptr;
	}

	*array = a;
	*arrayLength = idx;
	return 0; /* Success */
}

int ltn_nal_h264_findHeader(const uint8_t *buffer, int lengthBytes, int *offset)
{
	const uint8_t sig[] = { 0, 0, 1 };

	for (int i = (*offset + 1); i < lengthBytes - sizeof(sig); i++) {
		if (memcmp(buffer + i, sig, sizeof(sig)) == 0) {

			/* Check for the forbidden zero bit, it's illegal to be high in a nal (conflicts with PES headers. */
			if (*(buffer + i + 3) & 0x80)
				continue;

			*offset = i;
			return 0; /* Success */
		}
	}

	return -1; /* Not found */
}

static struct h264Nal_s {
	const char *name;
	const char *type;
} h264Nals[] = {
	[ 0] = { "UNSPECIFIED", .type = "AUTO" },
	[ 1] = { "slice_layer_without_partitioning_rbsp non-IDR", .type = "P" },
	[ 2] = { "slice_data_partition_a_layer_rbsp(", .type = "P" },
	[ 3] = { "slice_data_partition_b_layer_rbsp(", .type = "P" },
	[ 4] = { "slice_data_partition_c_layer_rbsp(", .type = "P" },
	[ 5] = { "slice_layer_without_partitioning_rbsp IDR", .type = "IDR" },
	[ 6] = { "SEI", .type = "" },
	[ 7] = { "SPS", .type = "" },
	[ 8] = { "PPS", .type = "" },
	[ 9] = { "AUD", .type = "" },
	[10] = { "EO SEQ", .type = "" },
	[11] = { "EO STREAM", .type = "" },
	[12] = { "FILLER", .type = "" },
	[13] = { "SPS-EX", .type = "" },
	[14] = { "PNU", .type = "" },
	[15] = { "SSPS", .type = "" },
	[16] = { "DPS", .type = "" },
	[19] = { "ACP", .type = "" },
	[20] = { "CSE", .type = "" },
	[21] = { "CSEDV", .type = "" },
};

const char *h264Nals_lookupName(int nalType)
{
	return h264Nals[nalType].name;
}

const char *h264Nals_lookupType(int nalType)
{
	return h264Nals[nalType].type;
}

char *ltn_nal_h264_findNalTypes(const uint8_t *buffer, int lengthBytes)
{
	char *arr = calloc(1, 128);
	arr[0] = 0;

	int items = 0;
	int offset = -1;
	while (ltn_nal_h264_findHeader(buffer, lengthBytes, &offset) == 0) {
		unsigned int nalType = buffer[offset + 3] & 0x1f;
		const char *nalName = h264Nals_lookupName(nalType);
		//const char *nalTypeDesc = h264Nals_lookupType(nalType);

		if (items++ > 0)
			sprintf(arr + strlen(arr), ", ");

		sprintf(arr + strlen(arr), "%s", nalName);
#if 0
		printf("%6d: %02x %02x %02x %02x : type %2d (%s)\n",
			offset,
			buffer[offset + 0],
			buffer[offset + 1],
			buffer[offset + 2],
			buffer[offset + 3],
			nalType,
			nalName);
#endif
	}
	
	if (items == 0) {
		free(arr);
		return NULL;
	}

	return arr;
}


struct h264_slice_data_s
{
	uint32_t  slice_type;
	uint64_t  count;
	char     *name;
};

#define MAX_H264_SLICE_TYPES 10
static struct h264_slice_data_s slice_defaults[MAX_H264_SLICE_TYPES] = {
	{ 0, 0, "P", },
	{ 1, 0, "B", },
	{ 2, 0, "I", },
	{ 3, 0, "p", },
	{ 4, 0, "i", },
	{ 5, 0, "P", },
	{ 6, 0, "B", },
	{ 7, 0, "I", },
	{ 8, 0, "p", },
	{ 9, 0, "i", },
};

const char *h264_slice_name_ascii(int slice_type)
{
	return &slice_defaults[ slice_type % MAX_H264_SLICE_TYPES ].name[0];
}

struct h264_slice_counter_s
{
	uint16_t pid;
	struct h264_slice_data_s slice[MAX_H264_SLICE_TYPES];

	int nextHistoryPos;
	char sliceHistory[H264_SLICE_COUNTER_HISTORY_LENGTH + 1];
};

void h264_slice_counter_reset(void *ctx)
{
	struct h264_slice_counter_s *s = (struct h264_slice_counter_s *)ctx;
	memcpy(s->slice, slice_defaults, sizeof(slice_defaults));
	for (int i = 0; i < H264_SLICE_COUNTER_HISTORY_LENGTH; i++) {
		s->sliceHistory[i] = ' ';
	}
	s->sliceHistory[H264_SLICE_COUNTER_HISTORY_LENGTH] = 0;
}

void *h264_slice_counter_alloc(uint16_t pid)
{
	struct h264_slice_counter_s *s = malloc(sizeof(*s));
	s->pid = pid;
	h264_slice_counter_reset(s);
	return (void *)s;
}

void h264_slice_counter_free(void *ctx)
{
	struct h264_slice_counter_s *s = (struct h264_slice_counter_s *)ctx;
	free(s);
}

void h264_slice_counter_update(void *ctx, int slice_type)
{
	struct h264_slice_counter_s *s = (struct h264_slice_counter_s *)ctx;
	s->slice[ slice_type ].count++;

    s->sliceHistory[s->nextHistoryPos++ % H264_SLICE_COUNTER_HISTORY_LENGTH] = s->slice[ slice_type ].name[0];
}

void h264_slice_counter_dprintf(void *ctx, int fd, int printZeroCounts)
{
	struct h264_slice_counter_s *s = (struct h264_slice_counter_s *)ctx;
	dprintf(fd, "Type  Name  Count (H264 slice types for pid 0x%04x)\n", s->pid);
	for (int i = MAX_H264_SLICE_TYPES - 1; i >= 0 ; i--) {
		struct h264_slice_data_s *sl = &s->slice[i];
		if (sl->count == 0 && !printZeroCounts)
			continue;
		dprintf(fd, "%4d  %4s  %" PRIu64 "\n", sl->slice_type, sl->name, sl->count);
	}
}
