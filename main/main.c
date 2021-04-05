/* --------------------------------------------------------------------------
  Autor: Vladimir Freire;
  Hardware: NodeMCU ESP32
  Espressif SDK-IDF: v4.2
 *  --------------------------------------------------------------------------
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "driver/sdmmc_host.h"
#include "soc/efuse_reg.h"
#include "esp_efuse.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_flash_encrypt.h"
#include "esp_efuse_table.h"
#include "esp_event.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>


static const char * TAG = "OS v1.0";


#define MOUNT_POINT "/sdcard"
#define TRUE         1 
#define DEBUG        1

#define LED_1 	     2  
#define INPUT_CD	 0      //simula o sinal CD do sdcard no botão da placa
#define ESP_INTR_FLAG_DEFAULT 0


int8_t _cry = 0;
uint8_t _firstiv[16] = {0};
uint8_t _iv[16] = {0};
mbedtls_aes_context aes;
FILE *f;
int version = 1;   

/**
 * Protótipos
 */
void app_main( void );
void vTask1(void *pvParameters); //Código Principal
void read_sd_card(void); //Função de leitura e verificação do firmware no Sd card
void decrypt(uint8_t *data, uint16_t size);
void crypto(const char *key, const char *iv);
void download(void);
void init(void);

/**
 * Variáveis Globais
 */
TaskHandle_t task1Handle = NULL;  /** Handle principal, Blink */ 
TaskHandle_t ISR = NULL;          /** Handle interrupção  */
bool flag = false;


/**
 * interrupçao chamanda ao detectar o cartão sd no pino INPUT_CD
 */
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    
	 xTaskResumeFromISR(ISR);

    }

// task chamada quando o sd car é detectado
 void vCd_card_task(void *arg)
{
     
  while(1){  

   vTaskSuspend(task1Handle); // Suspende o programa principal para verificar o conteúdo do sd card
   vTaskSuspend(NULL);        // Suspende a task chamada na interrupção para não entrar em loop
   flag=true;
   printf("Cartao SD Detectado...!!\n");
   vTaskDelay( 1000/portTICK_PERIOD_MS );
   read_sd_card();           // Lê o conteúdo do cartão
  
 }
}

void vTask1(void *pvParameters)
{    
    bool led_status = false;
     
    while (1)
    {
      led_status = !led_status;
      //gpio_set_level( LED_1 ,led_status ); 
      vTaskDelay( 400/portTICK_PERIOD_MS );
    }
}

// Decofica o arquivo bin
void decrypt(uint8_t *data, uint16_t size)
{
    if (_cry)
    {
        uint8_t aes_inp[16] = {0}, aes_out[16] = {0};
        for (uint16_t j = 0; j < size; j += 16)
        {
            for (int8_t i = 0; i < 16; i++)
            {
                aes_inp[i] = (j+i > size) ? 0 : data[j+i];
            }

            mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, 16, _iv, aes_inp, aes_out);

            for (int8_t i = 0; i < 16; i++)
            {
                data[j+i] = aes_out[i];
            }
        }
    }
}

//Faz o download do arquivo bin no sd card
void download(void){

    esp_err_t err1;
    int64_t t1 = 0, t2 = 0;
    uint32_t total = 0;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *ota_partition = NULL;
     ota_partition = esp_ota_get_next_update_partition(NULL);
     

 for (uint8_t i = 0; i < 16; i++)
    {
        _iv[i] = _firstiv[i];
    }

    err1 = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err1 != ESP_OK)
    {
        ESP_LOGE(TAG, "OTA begin fail [0x%x]", err1);
        return;
    }
    
    fseek(f, 0, SEEK_END);
    uint32_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "File size = %d", fsize);
     t1 = esp_timer_get_time()/1000;

     ESP_LOGE(TAG, "OTA begin");
    while (1)
    {
        uint8_t data[1024] = {0};
        int16_t avl = fread(data, 1, sizeof(data), f);
        total += avl;

        ESP_LOGE(TAG, "OTA write [0x%x]", total);
        decrypt(data, avl);

        err1 = esp_ota_write(ota_handle, data, avl);
        if (err1 != ESP_OK)
        {
            ESP_LOGE(TAG, "OTA write fail [0x%x]", err1);
             break;
        }

        if (total >= fsize)
        {
            ESP_LOGI(TAG, "Read end"); 
            break;
        }


        if (total % 51200 <= 500) {ESP_LOGI(TAG, "Downloaded %dB", total);}
        esp_task_wdt_reset();
    }
    t2 = (esp_timer_get_time()/1000);
    //ESP_LOGI(TAG, "Downloaded %dB in %dms", total, int32_t(t2-t1));
    fclose(f);

    err1 = esp_ota_end(ota_handle);
    if (err1 == ESP_OK)
    {
        err1 = esp_ota_set_boot_partition(ota_partition);
        if (err1 == ESP_OK)
        {
            ESP_LOGW(TAG, "OTA OK, restarting...");
            esp_restart();
        }
        else
        {
            ESP_LOGE(TAG, "OTA set boot partition fail [0x%x]", err1);
        }
    }
    else
    {
        ESP_LOGE(TAG, "OTA end fail [0x%x]", err1);
    }
  

}

void read_sd_card (void)
{
    
 esp_err_t ret;
 esp_vfs_fat_sdmmc_mount_config_t mount_config = {
//Modificado no Menuconfig para formatar caso não tenha partição
#ifdef CONFIG_FORMAT_IF_MOUNT_FAILED  
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif //FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t* card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  
    // To use 1-line SD mode, uncomment the following line:
    slot_config.width = 1;

    // GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
    // Internal pull-ups are not sufficient. However, enabling internal pull-ups
    // does make a difference some boards, so we do that here.
    gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);   // CMD, needed in 4- and 1- line modes
    gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);    // D0, needed in 4- and 1-line modes
    gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);    // D1, needed in 4-line mode only
    gpio_set_pull_mode(12, GPIO_PULLUP_ONLY);   // D2, needed in 4-line mode only
    gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);   // D3, needed in 4- and 1-line modes

    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
   

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                "If you want the card to be formatted, set the FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
    
        return;
    }
   
     // imprime as propriedades do cartão
     sdmmc_card_print_info(stdout, card);

   
    printf("Looking for a new firmware...\n");
    vTaskDelay( 500/portTICK_PERIOD_MS );
    // Abre o arquivo de configuração com a versão do firmware
    ESP_LOGI(TAG, "Lendo arquivo de configuracao..");
    FILE* f1 = fopen(MOUNT_POINT"/firmware.txt", "r");
    if (f1 == NULL) {
        ESP_LOGE(TAG, "Arquivo nao encontrado");
        esp_restart();
    }
    char line[64];
    fgets(line, sizeof(line), f1);
    fclose(f1);
    // strip newline
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
     // /Compara as versões do firmware
     int n = atoi(line);
     if (n>version){
        ESP_LOGI(TAG, "Versao do firmware encontrado '%s'", line);
        const esp_partition_t* partition = esp_ota_get_running_partition();
        printf("Partition: %s\n", partition->label);
        download();
       
     }
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card desmontado sem atualização");
 }
/**
 * Função Principal da Aplicação;
 * Chamada logo após a execução do bootloader do ESP32;
 */
void app_main( void )

{	  
    gpio_pad_select_gpio(LED_1);
    gpio_set_direction(LED_1, GPIO_MODE_OUTPUT );
    gpio_pad_select_gpio(INPUT_CD);
    gpio_set_direction(INPUT_CD, GPIO_MODE_INPUT);
    gpio_set_pull_mode(INPUT_CD, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(INPUT_CD, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(INPUT_CD, gpio_isr_handler, NULL);
	
    xTaskCreate(vTask1,"TASK1",configMINIMAL_STACK_SIZE,NULL,3,&task1Handle); //Programa principal
    xTaskCreate(vCd_card_task, "cd_card_task", 4096, NULL , 5,&ISR ); //interupção do SDcard Switch

 }
	
