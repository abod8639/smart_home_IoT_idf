#include "captive_portal.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_manager.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>

static const char *TAG = "CAPTIVE_PORTAL";
static httpd_handle_t server = NULL;

// Basic CSS/HTML for the portal
static const char* index_html = 
"<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<title>Smart Home Setup</title>"
"<style>"
"body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #0f172a; color: #f8fafc; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }"
".container { background: #1e293b; padding: 2rem; border-radius: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); width: 90%; max-width: 400px; text-align: center; }"
"h1 { color: #38bdf8; margin-bottom: 1.5rem; }"
"select, input, button { width: 100%; padding: 12px; margin: 8px 0; border-radius: 8px; border: 1px solid #334155; background: #0f172a; color: white; box-sizing: border-box; font-size: 16px; }"
"button { background: #0284c7; border: none; font-weight: bold; cursor: pointer; transition: background 0.3s; }"
"button:hover { background: #0369a1; }"
".loader { border: 4px solid #334155; border-top: 4px solid #38bdf8; border-radius: 50%; width: 30px; height: 30px; animation: spin 1s linear infinite; margin: 20px auto; }"
"@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }"
"</style>"
"</head><body>"
"<div class=\"container\">"
"<h1>Wi-Fi Setup</h1>"
"<div id=\"scan-loader\" class=\"loader\"></div>"
"<form id=\"wifi-form\" style=\"display:none;\" method=\"POST\" action=\"/connect\">"
"<select id=\"ssid\" name=\"ssid\" required></select>"
"<input type=\"password\" id=\"password\" name=\"password\" placeholder=\"Password\">"
"<button type=\"submit\">Connect</button>"
"</form>"
"</div>"
"<script>"
"fetch('/scan').then(r => r.json()).then(data => {"
"  const select = document.getElementById('ssid');"
"  data.networks.forEach(net => {"
"    const opt = document.createElement('option');"
"    opt.value = net.ssid; opt.textContent = net.ssid + ' (' + net.rssi + ' dBm)';"
"    select.appendChild(opt);"
"  });"
"  document.getElementById('scan-loader').style.display = 'none';"
"  document.getElementById('wifi-form').style.display = 'block';"
"}).catch(e => alert('Error scanning networks'));"
"</script></body></html>";

static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
    wifi_scan_config_t scan_config = { 0 };
    esp_wifi_scan_start(&scan_config, true);

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t) * ap_count);
    esp_wifi_scan_get_ap_records(&ap_count, ap_info);

    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "networks", networks);

    for (int i = 0; i < ap_count; i++) {
        if (strlen((const char*)ap_info[i].ssid) > 0) {
            cJSON *net = cJSON_CreateObject();
            cJSON_AddStringToObject(net, "ssid", (const char*)ap_info[i].ssid);
            cJSON_AddNumberToObject(net, "rssi", ap_info[i].rssi);
            cJSON_AddItemToArray(networks, net);
        }
    }

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);

    free((void*)json_str);
    cJSON_Delete(root);
    free(ap_info);

    return ESP_OK;
}

static void urldecode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((unsigned char)a) && isxdigit((unsigned char)b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static esp_err_t connect_post_handler(httpd_req_t *req) {
    char buf[128];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Parse simple form data: ssid=MyNet&password=MyPass
    char ssid[64] = {0};
    char password[64] = {0};

    char *ssid_ptr = strstr(buf, "ssid=");
    char *pass_ptr = strstr(buf, "password=");
    
    if (ssid_ptr) {
        ssid_ptr += 5;
        char *end = strchr(ssid_ptr, '&');
        if (end) *end = '\0';
        urldecode(ssid, ssid_ptr);
    }
    
    if (pass_ptr) {
        pass_ptr += 9;
        char *end = strchr(pass_ptr, '&');
        if (end) *end = '\0';
        urldecode(password, pass_ptr);
    }

    ESP_LOGI(TAG, "Saving credentials... SSID: '%s'", ssid);
    nvs_save_wifi_credentials(ssid, password);

    const char* resp = "<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                       "<style>body{background:#0f172a;color:#fff;text-align:center;font-family:sans-serif;margin-top:20%;}</style>"
                       "</head><body><h2>Credentials Saved!</h2><p>Device is restarting to connect...</p></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    // Give time to send response before restart
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

// -------------------------------------------------------------------------
// DNS Server Task to hijack all requests
// -------------------------------------------------------------------------
static void dns_server_task(void *pvParameters) {
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS Server started on port 53");

    while (1) {
        char rx_buffer[128];
        struct sockaddr_storage source_addr;
        socklen_t socklen = sizeof(source_addr);

        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);

        if (len > 0) {
            // Check if it's a valid DNS query (at least 12 bytes header)
            if (len >= 12) {
                // Set Response flag
                rx_buffer[2] |= 0x80;
                // Answer count = 1
                rx_buffer[6] = 0; rx_buffer[7] = 1;

                // Copy original query + add answer record
                char tx_buffer[256];
                int tx_len = len;
                if (tx_len > sizeof(tx_buffer) - 16) tx_len = sizeof(tx_buffer) - 16;
                memcpy(tx_buffer, rx_buffer, tx_len);

                // Answer: Name pointer to query (0xC00C)
                tx_buffer[tx_len++] = 0xC0; tx_buffer[tx_len++] = 0x0C;
                // Type A (1)
                tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x01;
                // Class IN (1)
                tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x01;
                // TTL (60s)
                tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x3C;
                // RDLENGTH (4 bytes for IPv4)
                tx_buffer[tx_len++] = 0x00; tx_buffer[tx_len++] = 0x04;
                // RDATA (192.168.4.1)
                tx_buffer[tx_len++] = 192; tx_buffer[tx_len++] = 168; tx_buffer[tx_len++] = 4; tx_buffer[tx_len++] = 1;

                sendto(sock, tx_buffer, tx_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
            }
        }
    }
}

void captive_portal_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = index_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_get);

        httpd_uri_t uri_scan = {
            .uri      = "/scan",
            .method   = HTTP_GET,
            .handler  = scan_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_scan);

        httpd_uri_t uri_post = {
            .uri      = "/connect",
            .method   = HTTP_POST,
            .handler  = connect_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_post);
        
        // Catch-all for captive portal detection
        httpd_uri_t uri_catchall = {
            .uri      = "/*",
            .method   = HTTP_GET,
            .handler  = index_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_catchall);
    }

    // Start DNS Server Task
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, NULL);
}
