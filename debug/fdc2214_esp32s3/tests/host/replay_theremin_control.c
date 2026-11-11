/* Host-only replay adapter for the GUI11-column CSV. No device access. */
#include "theremin_control.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_row(char *line, double *ms, uint64_t values[10])
{
    char *end;
    errno=0;*ms=strtod(line,&end);
    if (errno!=0 || end==line || *end!=',' || !isfinite(*ms)
        || *ms<0.0 || *ms>9007199254740.0) return false;
    char *cursor=end+1;
    for (unsigned i=0;i<10;i++) {
        while(isspace((unsigned char)*cursor))cursor++;
        if (*cursor=='-' || *cursor=='+') return false;
        errno=0;
        unsigned long long value=strtoull(cursor,&end,10);
        if (errno!=0 || end==cursor) return false;
        values[i]=(uint64_t)value;
        if (i<9) {if (*end!=',')return false;cursor=end+1;}
        else {while(isspace((unsigned char)*end))end++;if (*end!='\0')return false;}
    }
    for (unsigned i=1;i<=4;i++)if(values[i]>UINT32_MAX)return false;
    for (unsigned i=5;i<=8;i++)if(values[i]>UINT8_MAX)return false;
    return values[9]<=15;
}

int main(int argc, char **argv)
{
    if (argc!=2) {fprintf(stderr,"usage: replay_theremin_control raw.csv\n");return 2;}
    if (strcmp(argv[1],"--config")==0) {
        theremin_config_t c=theremin_default_config();
        printf("{\"pitch\":{\"raw_far\":%"PRIu32",\"raw_near\":%"PRIu32",\"noise_sigma\":%.9f},"
               "\"volume\":{\"raw_far\":%"PRIu32",\"raw_near\":%"PRIu32",\"noise_sigma\":%.9f},"
               "\"pitch_min\":%u,\"pitch_max\":%u,\"reference\":%u,\"stale_timeout_us\":%"PRIu32","
               "\"min_cutoff_hz\":%.6f,\"beta\":%.6f,\"gain_slew_per_s\":%.6f}\n",
               c.pitch.raw_far,c.pitch.raw_near,(double)c.pitch.noise_sigma,
               c.volume.raw_far,c.volume.raw_near,(double)c.volume.noise_sigma,
               (unsigned)c.pitch_min,(unsigned)c.pitch_max,(unsigned)c.reference,c.stale_timeout_us,
               (double)c.min_cutoff_hz,(double)c.beta,(double)c.gain_slew_per_s);
        return 0;
    }
    FILE *input=fopen(argv[1],"r");
    if (!input) {perror("input");return 2;}
    char line[1024];
    const char *header="time_ms,sequence,ch0,ch1,ch2,ch3,flags0,flags1,flags2,flags3,valid_mask";
    if (!fgets(line,sizeof(line),input)) {fclose(input);return 2;}
    line[strcspn(line,"\r\n")]='\0';
    const char *actual=line;
    if (strncmp(actual,"\xef\xbb\xbf",3)==0)actual+=3;
    if (strcmp(actual,header)!=0) {fprintf(stderr,"Expected GUI11-column raw export\n");fclose(input);return 2;}
    theremin_t state;
    theremin_config_t config=theremin_default_config();
    if (!theremin_init(&state,&config)) {fclose(input);return 2;}
    uint64_t previous_us=0,previous_sequence=0;
    unsigned row=1,emitted=0,duplicates=0,session=0;
    puts("time_s,sequence,ch0,ch1,flags0,flags1,original_valid_mask,pitch_code,pitch_delta,frequency_hz,gain_q16,gain_linear,pitch_u,volume_u,pitch_accepted,volume_accepted,pitch_quality,volume_quality,pitch_age_us,volume_age_us,pitch_stale,volume_stale,muted,session");
    while (fgets(line,sizeof(line),input)) {
        row++;
        double ms;uint64_t v[10];
        if (!parse_row(line,&ms,v)) {fprintf(stderr,"Malformed row%u\n",row);fclose(input);return 2;}
        uint64_t now=(uint64_t)(ms*1000.0+.5);
        if (emitted && v[0]==previous_sequence && now>=previous_us) {duplicates++;continue;}
        if (emitted && (now<=previous_us || v[0]<previous_sequence)) {theremin_reset(&state);session++;}
        uint8_t pf=(uint8_t)v[5],vf=(uint8_t)v[6];
        /* Short exports omit STATUS/transport diagnostics. A clear mask with AW
         * may be the conservative firmware rule. Without AW its cause is unknown. */
        theremin_sample_t p={(uint32_t)v[1],pf,true,(v[9]&1u)==0 && (pf&1u)==0};
        theremin_sample_t q={(uint32_t)v[2],vf,true,(v[9]&2u)==0 && (vf&1u)==0};
        theremin_output_t out;
        if (!theremin_update(&state,now,&p,&q,&out)) {fclose(input);return 2;}
        double hz=440.0*pow(2.0,((double)out.pitch_code-5888.0)/1024.0);
        printf("%.6f,%"PRIu64",%"PRIu32",%"PRIu32",%u,%u,%"PRIu64",%u,%"PRId32",%.9f,%"PRIu32",%.9f,%.9f,%.9f,%u,%u,%s,%s,%"PRIu64",%"PRIu64",%u,%u,%u,%u\n",
            (double)now*.000001,v[0],p.raw,q.raw,(unsigned)pf,(unsigned)vf,v[9],
            (unsigned)out.pitch_code,out.pitch_delta,hz,out.gain_q16,(double)out.gain_linear,
            (double)out.pitch_u,(double)out.volume_u,(unsigned)out.pitch_accepted,(unsigned)out.volume_accepted,
            theremin_quality_name(out.pitch_quality),theremin_quality_name(out.volume_quality),
            out.pitch_age_us,out.volume_age_us,(unsigned)out.pitch_stale,(unsigned)out.volume_stale,(unsigned)out.muted,session);
        previous_us=now;previous_sequence=v[0];emitted++;
    }
    int bad=ferror(input) || ferror(stdout) || emitted==0;
    fclose(input);
    fprintf(stderr,"emitted=%u duplicate_rows_skipped=%u session_resets=%u\n",emitted,duplicates,session);
    return bad ? 2:0;
}
