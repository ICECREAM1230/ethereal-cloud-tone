#include "b_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
typedef struct { uint32_t id;b_config_t config; } save_job_t;
static QueueHandle_t jobs,results;
static bool storage_available;
static const char *TAG="b_storage";
static void storage_task(void *arg)
{
    (void)arg;save_job_t job;
    for(;;) {
        if(xQueueReceive(jobs,&job,portMAX_DELAY)!=pdTRUE) continue;
        b_storage_result_t result={.id=job.id,.result=ESP_ERR_INVALID_STATE};
        uint8_t blob[B_CONFIG_BLOB_SIZE];size_t size=b_config_encode(&job.config,blob,sizeof(blob));
        if(storage_available && size) {
            nvs_handle_t nvs;result.result=nvs_open("harp_b",NVS_READWRITE,&nvs);
            if(result.result==ESP_OK) {
                result.result=nvs_set_blob(nvs,"settings",blob,size);
                if(result.result==ESP_OK) result.result=nvs_commit(nvs);
                nvs_close(nvs);
            }
        }
        xQueueOverwrite(results,&result);
    }
}
esp_err_t b_storage_start(const b_config_t *defaults,b_config_t *loaded,bool *available,bool *migrated)
{
    *loaded=*defaults;*migrated=false;
    esp_err_t rc=nvs_flash_init();storage_available=rc==ESP_OK;*available=storage_available;
    if(storage_available) {
        nvs_handle_t nvs;rc=nvs_open("harp_b",NVS_READONLY,&nvs);
        if(rc==ESP_OK) {
            uint8_t blob[B_CONFIG_BLOB_SIZE];size_t size=sizeof(blob);
            rc=nvs_get_blob(nvs,"settings",blob,&size);nvs_close(nvs);
            if(rc==ESP_OK && !b_config_decode(blob,size,defaults,loaded,migrated))
                ESP_LOGW(TAG,"Stored settings rejected; build defaults retained");
            else if(rc!=ESP_OK && rc!=ESP_ERR_NVS_NOT_FOUND)
                ESP_LOGW(TAG,"Settings read: %s",esp_err_to_name(rc));
        } else if(rc!=ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG,"Settings open: %s",esp_err_to_name(rc));
    } else ESP_LOGW(TAG,"NVS unavailable: %s; RAM-only, no erase",esp_err_to_name(rc));
    jobs=xQueueCreate(1,sizeof(save_job_t));results=xQueueCreate(1,sizeof(b_storage_result_t));
    if(!jobs || !results) return ESP_ERR_NO_MEM;
    return xTaskCreate(storage_task,"b_store",4096,NULL,2,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
bool b_storage_save(uint32_t id,const b_config_t *config)
{
    save_job_t job={.id=id,.config=*config};return jobs && xQueueSend(jobs,&job,0)==pdTRUE;
}
bool b_storage_poll(b_storage_result_t *out)
{ return results && xQueueReceive(results,out,0)==pdTRUE; }
