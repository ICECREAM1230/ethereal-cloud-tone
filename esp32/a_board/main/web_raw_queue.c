#include "web_raw_queue.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static portMUX_TYPE s_raw_jobs_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    uint32_t id;
    web_raw_job_t job;
} s_raw_jobs[4];
static uint32_t s_raw_next_id = 1;

bool web_raw_queue_push(const web_raw_job_t *job, uint32_t *id)
{
    if (!id) return false;
    *id = 0;
    if (!job || !job->request.length || job->request.length > sizeof(job->request.bytes)) return false;
    portENTER_CRITICAL(&s_raw_jobs_lock);
    for (unsigned i=0;i<4 && s_raw_next_id;i++) {
        if (s_raw_jobs[i].id) continue;
        s_raw_jobs[i].job = *job;
        s_raw_jobs[i].id = s_raw_next_id++;
        *id = s_raw_jobs[i].id;
        break;
    }
    portEXIT_CRITICAL(&s_raw_jobs_lock);
    // Exhaustion at UINT32_MAX fails closed, preventing old IDs from aliasing.
    return *id != 0;
}

bool web_raw_queue_take(uint32_t id, web_raw_job_t *job)
{
    if (!job) return false;
    memset(job, 0, sizeof(*job));
    if (!id) return false;
    bool found = false;
    portENTER_CRITICAL(&s_raw_jobs_lock);
    for (unsigned i=0;i<4;i++) {
        if (s_raw_jobs[i].id != id) continue;
        *job = s_raw_jobs[i].job;
        memset(&s_raw_jobs[i],0,sizeof(s_raw_jobs[i]));
        found = true;
        break;
    }
    portEXIT_CRITICAL(&s_raw_jobs_lock);
    return found;
}

void web_raw_queue_reset(void)
{
    portENTER_CRITICAL(&s_raw_jobs_lock);
    memset(s_raw_jobs,0,sizeof(s_raw_jobs));
    portEXIT_CRITICAL(&s_raw_jobs_lock);
}
