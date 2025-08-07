#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libltntstools/ltntstools.h>

#include <libavformat/avformat.h>
#include <libavutil/log.h>

#include "nal_h264.c"
#include "misc.c"
#include "bitreader.c"

/* Keep the linker happy for some off issue in older */
const uint8_t ff_golomb_vlc_len[512];
const uint8_t ff_ue_golomb_vlc_code[512];

static int gRunning = 1;

struct tool_stats_s
{
    time_t unixtime;                        /* Walltime, when the sample period ended and the stats were announced */

    unsigned int day_of_week;               /* 0-6, where 0 is sunday */
    unsigned int hrs;                       /* 0-23 */
    unsigned int mins;                      /* 0-59 */
    unsigned int secs;                      /* 0-59 */
    unsigned int avc_ibp_total_slice_count; /* Number of I/B/P slices counted in this reporting period */
    unsigned int avc_ibp_total_slice_size;  /* Size in bits of all summed NAL slices */
    unsigned int transport_bit_count;       /* Number of bits counted for the entire stream in this reporting period */

    unsigned int slice_i_count;             /* Number of I frames in this reporting period */
    unsigned int slice_b_count;             /* Number of B frames in this reporting period */
    unsigned int slice_p_count;             /* Number of P frames in this reporting period */

    int on_air;                             /* Boolean. Label issued by the probe that is human influence, used for supervised learning. */

    char json[256];                         /* Fully formed json string that announced stats to external mechanisms. */
};

struct tool_ctx_s
{
    int verbose;
    int humanOnAir;          /* Boolean. Defaults false. Drives the stats on_air boolean at the end of each collection period. */

    time_t now;              /* last walltime a buffer of transport packets was received. */
    time_t lastStatsReport;  /* Walltime of the last stats period ending. */

    AVIOContext *c;
    char *iname;             /* -i udp://227.1.1.1:4001 */

    char *oname;             /* /tmp/mynamedpipe */
    FILE *ofh;               /* filehandle of oname */

    int collectInterval;     /* Number of seconds in each reporting interval */
    int pid;                 /* Transport packet pid for the video stream, Eg. 0x31 */
    int streamId;            /* PMT estype for the video PES, typically 0xe0 */

    unsigned char *buf;      /* Buffer, typically 4K, where transport packets from AVIO are read into */

    void *pe;                /* PES Extractor handle */
    void *sm;                /* Stream Model handle */

    /* Transport stream statistics. Bitrates, CC loss etc. */
    struct ltntstools_stream_statistics_s *stream;

    /* Collections of stats / features this probe will expose */
    struct tool_stats_s stats_curr;
    struct tool_stats_s stats_next;
};

const char *slice_type_name(int slice_type)
{
    switch (slice_type % 5) {
        case 0: return "P";
        case 1: return "B";
        case 2: return "I";
        case 3: return "SP";
        case 4: return "SI";
        default: return "Unknown";
    }
}

struct tool_ctx_s *gCtx = NULL;
static void signal_handler(int signum)
{
    struct tool_ctx_s *ctx = gCtx;

    switch(signum) {
    case SIGUSR1: /* Human says - Off air */
        printf("Supervision: We're OFF AIR\n");
        ctx->humanOnAir = 0;
        break;
    case SIGUSR2: /* Human says - On air */
        printf("Supervision: We're ON AIR\n");
        ctx->humanOnAir = 1;
        break;
    case SIGINT:
    case SIGTERM:
        gRunning = 0;
        break;
    default:
        printf("Unhandled signal %d\n", signum);
    }
}

static void stats_reset(struct tool_stats_s *stats)
{
    memset(stats, 0, sizeof(*stats));
}

static int stats_to_json(struct tool_ctx_s *ctx, struct tool_stats_s *stats)
{
    sprintf(stats->json, "{ \"day_of_week\": %d, \"hour\": %d, \"minute\": %2d, \"second\": %2d, "
        "\"unixtime\": %lu, \"avc_ibp_total_slice_count\": %3d, \"avc_ibp_total_slice_size\": %9d, "
        "\"transport_bit_count\": %9d, "
        "\"i_count\": %3d, "
        "\"p_count\": %3d, "
        "\"b_count\": %3d, "
        "\"on_air\": %s }\n",
        stats->day_of_week,
        stats->hrs,
        stats->mins,
        stats->secs,
        stats->unixtime,
        stats->avc_ibp_total_slice_count,
        stats->avc_ibp_total_slice_size,
        stats->transport_bit_count,
        stats->slice_i_count,
        stats->slice_p_count,
        stats->slice_b_count,
        stats->on_air ? "true" : "false");

    return 0;
}

static int stats_publish(struct tool_ctx_s *ctx, struct tool_stats_s *stats)
{
    if (ctx->ofh) {
        fwrite(stats->json, 1, strlen(stats->json), ctx->ofh);
        fflush(ctx->ofh);
    } else {
        printf("%s", stats->json);
    }

    return -1;
}

static void stats_complete(struct tool_ctx_s *ctx)
{
    ctx->stats_next.unixtime = ctx->now;
    ctx->stats_curr = ctx->stats_next;

    struct tm *t = localtime(&ctx->now);
    ctx->stats_curr.day_of_week = t->tm_wday;
    ctx->stats_curr.hrs = t->tm_hour;
    ctx->stats_curr.mins = t->tm_min;
    ctx->stats_curr.secs = t->tm_sec;
    ctx->stats_curr.on_air = ctx->humanOnAir;

    stats_reset(&ctx->stats_next);
    stats_to_json(ctx, &ctx->stats_curr);
    stats_publish(ctx, &ctx->stats_curr);
}

static void *callback(void *userContext, struct ltn_pes_packet_s *pes)
{
    struct tool_ctx_s *ctx = (struct tool_ctx_s *)userContext;
    BitReader br;

    if (ctx->verbose > 1) {
        ltn_pes_packet_dump(pes, "");
    }

    int arrayLength = 0;
    struct ltn_nal_headers_s *array = NULL;
    if (ltn_nal_h264_find_headers(pes->data, pes->dataLengthBytes, &array, &arrayLength) == 0) {

        for (int i = 0; i < arrayLength; i++) {
            struct ltn_nal_headers_s *e = array + i;

            switch(e->nalType) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:  /* slice_layer_without_partitioning_rbsp */

                ltn_nal_h264_strip_emulation_prevention(e);

                init_bitreader(&br, e->ptr + 4, 4);
                int first_mb_in_slice = read_ue(&br);
                int slice_type = read_ue(&br);

                if (ctx->verbose) {
                    printf("slice_type %s (%d), first_mb_in_slice %d\n", slice_type_name(slice_type), slice_type, first_mb_in_slice);
                }

                ctx->stats_next.avc_ibp_total_slice_count++;
                ctx->stats_next.avc_ibp_total_slice_size += (e->lengthBytes * 8);
                if (slice_type % 5 == 0) {
                    ctx->stats_next.slice_p_count++;
                } else
                if (slice_type % 5 == 1) {
                    ctx->stats_next.slice_b_count++;
                } else
                if (slice_type % 5 == 2) {
                    ctx->stats_next.slice_i_count++;
                }

                break;
            case 6:  /* SEI */
            case 7:  /* SPS */
            case 8:  /* PPS */
            case 9:  /* AUD */
            case 12: /* FILLER */
            case 19: /* ACP */
                break;
            default:
                printf("nal %d\n", e->nalType);
            }
/*
        [ 0] = { "UNSPECIFIED", .type = "AUTO" }, 
        [ 1] = { "slice_layer_without_partitioning_rbsp non-IDR", .type = "P" },
        [ 2] = { "slice_data_partition_a_layer_rbsp(", .type = "P" },
        [ 3] = { "slice_data_partition_b_layer_rbsp(", .type = "P" },
        [ 4] = { "slice_data_partition_c_layer_rbsp(", .type = "P" },
        [ 5] = { "slice_layer_without_partitioning_rbsp IDR", .type = "IDR" },
        [ 6] = { "SEI" },
        [ 7] = { "SPS" },
        [ 8] = { "PPS" },
        [ 9] = { "AUD" }, 
        [10] = { "EO SEQ" },
        [11] = { "EO STREAM" },
        [12] = { "FILLER" },
        [13] = { "SPS-EX" },
        [14] = { "PNU" },
        [15] = { "SSPS" },
        [16] = { "DPS" },
        [19] = { "ACP" },
        [20] = { "CSE" }, 
        [21] = { "CSEDV" },
*/

        }

    }

    ltn_pes_packet_free(pes);

    return NULL;
}

static void usage(const char *prog)
{
    printf("Usage: %s -i <url> -v -P 0xnn (video pid) -S 0xe0 (estype) -I secs (collect_interval)\n", prog);
}

int main(int argc, char *argv[])
{
    int bsize = 4096, blen = 7 * 188;

    if (argc == 1) {
        usage(argv[0]);
        exit(1);
    }

    struct tool_ctx_s *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        perror("calloc");
        exit(1);
    }
    gCtx = ctx;

    ctx->buf = malloc(bsize);
    ctx->iname = strdup("udp://239.255.0.1:1234?fifo_size=1000000&overrun_nonfatal=1");
    ctx->verbose = 0;
    ctx->collectInterval = 1;
    ctx->pid = 0x31;
    ctx->streamId = 0xe0;

    ltntstools_pid_stats_alloc(&ctx->stream);

    ltntstools_streammodel_alloc(&ctx->sm, ctx);

    int ch;
    while ((ch = getopt(argc, argv, "?hi:o:I:P:S:v")) != -1) {
        switch(ch) {
        case 'i':
            free(ctx->iname);
            ctx->iname = strdup(optarg);
            break;
        case 'I':
            ctx->collectInterval = atoi(optarg);
            if (ctx->collectInterval < 1) {
                ctx->collectInterval = 1;
            } else
            if (ctx->collectInterval > 15) {
                ctx->collectInterval = 15;
            }
            break;
        case '?':
        case 'h':
            usage(argv[0]);
            exit(1);
        case 'o':
            free(ctx->oname);
            ctx->oname = strdup(optarg);
            if (mkfifo(ctx->oname, 0644) == -1) {
               if (errno != EEXIST) {
                   perror("mkfifo");
                   exit(1);
               }
            }
            break;
        case 'P':
            if ((sscanf(optarg, "0x%x", &ctx->pid) != 1) || (ctx->pid > 0x1fff)) {
                if ((sscanf(optarg, "%d", &ctx->pid) != 1) || (ctx->pid > 0x1fff)) {
                    usage(argv[0]);
                    exit(1);
                }
            }
            break;
        case 'S':
            if ((sscanf(optarg, "0x%x", &ctx->streamId) != 1) || (ctx->streamId > 0xff)) {
                usage(argv[0]);
                exit(1);
            }
            break;
        case 'v':
            ctx->verbose++;
            break;
        default:
            usage(argv[0]);
            exit(1);
        }
    }

    if (ctx->oname) {
        ctx->ofh = fopen(ctx->oname, "wb");
        if (!ctx->ofh) {
            perror("fopen");
            exit(1);
        }
    }

    av_log_set_level(AV_LOG_INFO);
    avformat_network_init();

    int ret = avio_open2(&ctx->c, ctx->iname, AVIO_FLAG_READ | AVIO_FLAG_NONBLOCK | AVIO_FLAG_DIRECT, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Unabled to open url\n");
        exit(1);
    }

    signal(SIGINT, signal_handler);
    signal(SIGUSR1, signal_handler);
    signal(SIGUSR2, signal_handler);

    while (gRunning) {
        time(&ctx->now);
        if (ctx->lastStatsReport == 0) {
            ctx->lastStatsReport = ctx->now;
        }

        int rlen = avio_read(ctx->c, ctx->buf, blen);
        if (rlen == -EAGAIN) {
            usleep(20 * 1000);
            continue;
        }
        if (rlen < 0) {
            break;
        }

	/* TODO: The more processing we do here, the more we're likely to overflow the AVIO fifo. */

        if (ctx->verbose > 2) {
            printf("avio %4d : ", rlen);
            for (int i = 0; i < 16; i++) {
                printf("%02x ", ctx->buf[i]);
            }
            printf("\n");
        }

        if (ctx->lastStatsReport + ctx->collectInterval <= ctx->now) {
            if (ctx->verbose > 1) {
                printf("%d: Creating report\n", (unsigned int)ctx->now);
            }
            ctx->lastStatsReport = ctx->now;
            stats_complete(ctx);
        }

        ctx->stats_next.transport_bit_count += (rlen * 8);

        ltntstools_pid_stats_update(ctx->stream, ctx->buf, rlen / 188);

        int complete;
        struct timeval ts;
        gettimeofday(&ts, NULL);
        ltntstools_streammodel_write(ctx->sm, ctx->buf, rlen / 188, &complete, &ts);

        if (complete) {

            struct ltntstools_pat_s *pat;
            int r = ltntstools_streammodel_query_model(ctx->sm, &pat);
            if (r == 0) {

                int e = 0;
                struct ltntstools_pmt_s *pmt;
                while (ltntstools_pat_enum_services_video(pat, &e, &pmt) == 0) {
                    uint8_t estype;
                    uint16_t videopid;
                    if (ltntstools_pmt_query_video_pid(pmt, &videopid, &estype) < 0)
                        continue;

                    printf("Discovered program %5d, video pid 0x%04x\n", pmt->program_number, videopid);
                    ctx->pid = videopid;
                    ctx->streamId = 0xe0;
                    break;
                }

                if (ctx->pe) {
                    ltntstools_pes_extractor_free(ctx->pe);
                    ctx->pe = NULL;
                }

                if (ltntstools_pes_extractor_alloc(&ctx->pe, ctx->pid, ctx->streamId, (pes_extractor_callback)callback, ctx, (1024 * 1024), (2 * 1024 * 1024)) < 0) {
                    fprintf(stderr, "\nUnable to allocate pes_extractor object.\n\n");
                    exit(1);
                }

                ltntstools_pat_free(pat);
            }
        }

        if (ctx->pe) {
            ltntstools_pes_extractor_write(ctx->pe, ctx->buf, rlen / 188);
        }

    }

    /* Teardown */
    if (ctx->sm) {
        ltntstools_streammodel_free(ctx->sm);
    }
    if (ctx->ofh) {
        fclose(ctx->ofh);
    }
    if (ctx->pe) {
        ltntstools_pes_extractor_free(ctx->pe);
    }
    if (ctx->buf) {
        free(ctx->buf);
    }
    if (ctx->c) {
        avio_close(ctx->c);
    }
    if (ctx->stream) {
        ltntstools_pid_stats_free(ctx->stream);
    }
    free(ctx);

    return 0;
}
