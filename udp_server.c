/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "lwip/api.h"
#define UDP_SERVER_PORT         8265
#define UDP_SERVER_RX_BUFSIZE   1024

struct netconn *udp_server_netconn;
static ip_addr_t server_ipaddr;
static void udp_server_task(void *pvParameters);

#define UDP_SERVER_WIFI_SSID      "Udp Server"
#define UDP_SERVER_WIFI_PWD       "12345678"
#define UDP_SERVER_MAX_STA_CONN    4

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static const char *TAG_XLI = "Xli--->";

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG_XLI, "station:"MACSTR" join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG_XLI, "station:"MACSTR" leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
        break;
    default:
        break;
    }
    return ESP_OK;
}

void wifi_init_softap()
{
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = UDP_SERVER_WIFI_SSID,
            .ssid_len = strlen(UDP_SERVER_WIFI_SSID),
            .password = UDP_SERVER_WIFI_PWD,
            .max_connection = UDP_SERVER_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(UDP_SERVER_WIFI_PWD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_XLI, "wifi_init_softap finished.SSID:%s password:%s",
             UDP_SERVER_WIFI_SSID, UDP_SERVER_WIFI_PWD);
}

void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG_XLI, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    xTaskCreate(&udp_server_task, "udp_server_task", 4096, NULL, 4, NULL);
}

static esp_err_t udp_server_create(void *arg)
{
    err_t err = ERR_OK;
    udp_server_netconn = netconn_new(NETCONN_UDP);  //创建socket
    udp_server_netconn->recv_timeout = 10;
    if(udp_server_netconn == NULL) return ESP_FAIL;

    err = netconn_bind(udp_server_netconn, &server_ipaddr, UDP_SERVER_PORT);//绑定IP地址和端口号
    if (err != ERR_OK)
    {
        netconn_close(udp_server_netconn);
        netconn_delete(udp_server_netconn);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void udp_server_task(void *pvParameters)
{
    if(udp_server_create(NULL) == ESP_FAIL)
    {
        ESP_LOGI(TAG_XLI, "UDP server create socket failed!!!");
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG_XLI, "UDP server create socket succeed!!!");

    struct netbuf *recvbuf = NULL;
    struct netbuf *sendbuf = NULL;
    uint32_t data_len = 0;
    struct pbuf *q = NULL;
    uint8_t udp_server_recvbuf[UDP_SERVER_RX_BUFSIZE] = {0};
    while (1)
    {
        memset(udp_server_recvbuf, 0, sizeof(udp_server_recvbuf));
        if(netconn_recv(udp_server_netconn, &recvbuf) == ERR_OK)
        {
            for(q=recvbuf->p;q!=NULL;q=q->next)
            {
                if(q->len > (UDP_SERVER_RX_BUFSIZE - data_len))
                {
                    memcpy(udp_server_recvbuf+data_len,q->payload,(UDP_SERVER_RX_BUFSIZE-data_len));
                }
                else
                {
                    memcpy(udp_server_recvbuf+data_len,q->payload,q->len);
                }
                data_len += q->len;
                if(data_len > UDP_SERVER_RX_BUFSIZE)
                {
                    break;
                }
            }
            data_len=0;  

            ESP_LOGI(TAG_XLI, "Received data from ip:%s,port:%d are %s.\n",  ip4addr_ntoa(&recvbuf->addr.u_addr.ip4), recvbuf->port, udp_server_recvbuf);     

            
            //发送数据
            char send_data[] = "Config Network Succeed\n";
            sendbuf = netbuf_new();
            sendbuf->addr = recvbuf->addr;  //发送的地址和端口
            sendbuf->port = recvbuf->port;
            netbuf_alloc(sendbuf, strlen(send_data));
            sendbuf->p->payload = send_data;
            if(netconn_sendto(udp_server_netconn,  sendbuf, &sendbuf->addr, sendbuf->port) == ERR_OK)
            {
                ESP_LOGI(TAG_XLI, "Send data to ip:%s,port:%d succeed.\n", ip4addr_ntoa(&sendbuf->addr.u_addr.ip4), sendbuf->port);
            }
            netbuf_delete(sendbuf);
            netbuf_delete(recvbuf);
            /*
            if(netconn_sendto(udp_server_netconn,  recvbuf, &recvbuf->addr, recvbuf->port) == ERR_OK)
            {
                ESP_LOGI(TAG_XLI, "UDP server echo ok.");
            }
            */    
        }
    }
    
}

