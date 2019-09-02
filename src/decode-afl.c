/* Copyright (C) 2007-2017 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "suricata-common.h"
#include "suricata.h"
#include "conf.h"
#include "decode.h"
#include "util-debug.h"
#include "util-mem.h"
#include "app-layer-detect-proto.h"
#include "app-layer.h"
#include "tm-threads.h"
#include "util-error.h"
#include "util-print.h"
#include "tmqh-packetpool.h"
#include "util-profiling.h"
#include "pkt-var.h"
#include "util-mpm-ac.h"

#include "output.h"
#include "output-flow.h"

#include "defrag.h"
#include "flow.h"

#ifdef AFLFUZZ_DECODER
int AFLDecodeIPV4(ThreadVars *tv, DecodeThreadVars *dtv, Packet *p,
        const uint8_t *pkt, uint32_t len, PacketQueue *pq)
{
    return DecodeIPV4(tv, dtv, p, pkt, (uint16_t)len, pq);
}
int AFLDecodeIPV6(ThreadVars *tv, DecodeThreadVars *dtv, Packet *p,
        const uint8_t *pkt, uint32_t len, PacketQueue *pq)
{
    return DecodeIPV6(tv, dtv, p, pkt, (uint16_t)len, pq);
}

/* stateful processing of data as packets. Because AFL in case of a
 * crash will only safe the last input, we dump all the inputs to a
 * directory 'dump' with a unique timestamp for the serie and an
 * incrementing 'id' so that we can 'replay' it in
 * DecoderParseDataFromFileSerie().
 */
int DecoderParseDataFromFile(char *filename, DecoderFunc Decoder) {
    uint8_t buffer[65536];

    struct timeval ts;
    memset(&ts, 0, sizeof(ts));
    gettimeofday(&ts, NULL);

    uint32_t cnt = 0;

    DefragInit();
    FlowInitConfig(FLOW_QUIET);

    ThreadVars tv;
    memset(&tv, 0, sizeof(tv));
    DecodeThreadVars *dtv = DecodeThreadVarsAlloc(&tv);
    DecodeRegisterPerfCounters(dtv, &tv);
    StatsSetupPrivate(&tv);
    PacketQueue pq;
    memset(&pq, 0, sizeof(pq));

#ifdef AFLFUZZ_PERSISTANT_MODE
    while (__AFL_LOOP(1000)) {
        /* reset state */
        memset(buffer, 0, sizeof(buffer));
#endif /* AFLFUZZ_PERSISTANT_MODE */


        FILE *fp = fopen(filename, "r");
        BUG_ON(fp == NULL);

        size_t size = fread(&buffer, 1, sizeof(buffer), fp);
        char outfilename[256];
        snprintf(outfilename, sizeof(outfilename), "dump/%u-%u.%u",
                (unsigned int)ts.tv_sec, (unsigned int)ts.tv_usec, cnt);
        FILE *out_fp = fopen(outfilename, "w");
        BUG_ON(out_fp == NULL);
        (void)fwrite(buffer, size, 1, out_fp);
        fclose(out_fp);

        Packet *p = PacketGetFromAlloc();
        if (p != NULL) {
            PacketSetData(p, buffer, size);
            (void) Decoder (&tv, dtv, p, buffer, size, &pq);
            while (1) {
                Packet *extra_p = PacketDequeue(&pq);
                if (unlikely(extra_p == NULL))
                    break;
                PacketFree(extra_p);
            }
            PacketFree(p);
        }
        fclose(fp);
        cnt++;

#ifdef AFLFUZZ_PERSISTANT_MODE
    }
#endif /* AFLFUZZ_PERSISTANT_MODE */

    /* if we get here there was no crash, so we can remove our files */
    uint32_t x = 0;
    for (x = 0; x < cnt; x++) {
        char rmfilename[256];
        snprintf(rmfilename, sizeof(rmfilename), "dump/%u-%u.%u",
                (unsigned int)ts.tv_sec, (unsigned int)ts.tv_usec, x);
        unlink(rmfilename);
    }

    DecodeThreadVarsFree(&tv, dtv);
    FlowShutdown();
    DefragDestroy();
    return 0;
}

/* load a serie of files generated by DecoderParseDataFromFile() in
 * the same order as it was produced. */
int DecoderParseDataFromFileSerie(char *fileprefix, DecoderFunc Decoder)
{
    uint8_t buffer[65536];
    uint32_t cnt = 0;

    DefragInit();
    FlowInitConfig(FLOW_QUIET);
    ThreadVars tv;
    memset(&tv, 0, sizeof(tv));
    DecodeThreadVars *dtv = DecodeThreadVarsAlloc(&tv);
    DecodeRegisterPerfCounters(dtv, &tv);
    StatsSetupPrivate(&tv);
    PacketQueue pq;
    memset(&pq, 0, sizeof(pq));

    char filename[256];
    snprintf(filename, sizeof(filename), "dump/%s.%u", fileprefix, cnt);
    FILE *fp;
    while ((fp = fopen(filename, "r")) != NULL)
    {
        memset(buffer, 0, sizeof(buffer));

        size_t size = fread(&buffer, 1, sizeof(buffer), fp);

        Packet *p = PacketGetFromAlloc();
        if (p != NULL) {
            PacketSetData(p, buffer, size);
            (void) Decoder (&tv, dtv, p, buffer, size, &pq);
            while (1) {
                Packet *extra_p = PacketDequeue(&pq);
                if (unlikely(extra_p == NULL))
                    break;
                PacketFree(extra_p);
            }
            PacketFree(p);
        }
        fclose(fp);
        cnt++;
        snprintf(filename, sizeof(filename), "dump/%s.%u", fileprefix, cnt);
    }
    DecodeThreadVarsFree(&tv, dtv);
    FlowShutdown();
    DefragDestroy();
    return 0;
}
#endif /* AFLFUZZ_DECODER */

