#include "b_runtime.h"
#include "esp_err.h"

void app_main(void)
{
    ESP_ERROR_CHECK(b_runtime_start());
}
