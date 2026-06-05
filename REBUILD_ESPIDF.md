# دليل إعادة بناء مشروع Smart Home IoT باستخدام ESP-IDF

هذا الدليل مخصص لشرح هيكلية وعمل مشروع **Smart Home IoT** الحالي (المبني بإطار عمل Arduino) وكيفية إعادة كتابته وبنائه بالكامل باستخدام إطار العمل الرسمي من إسبريسيف **ESP-IDF** (v5.x).

يحتوي المشروع الحالي على دمج معقد بين بروتوكول **Matter**، وخادم ويب **HTTP REST API**، والتحكم بالملحقات الرقمية والـ **PWM**، والتحكم بالمكيف عبر الأشعة تحت الحمراء (**IR learning & emission**)، وحفظ الحالة في الذاكرة الدائمة (**NVS**)، وتحديثات النظام اللاسلكية (**OTA**)، وقراءة الحساسات (**DHT22**).

---

## 1. تحليل هيكلية المشروع الحالية (Architecture Overview)

يعمل المشروع الحالي في وضع **Matter Mode** كجهاز ذكي متعدد الوظائف. يقوم بتشغيل الخدمات التالية بشكل متوازٍ ومستقر بفضل نظام تشغيل FreeRTOS المدمج في ESP32:

```
                          ┌──────────────────────────┐
                          │    app_main() (ESP-IDF)  │
                          └─────────────┬────────────┘
                                        │
         ┌──────────────────────────────┼──────────────────────────────┐
         ▼                              ▼                              ▼
┌─────────────────┐            ┌─────────────────┐            ┌─────────────────┐
│  Matter Stack   │            │   HTTP Server   │            │   Peripherals   │
│  (BLE Comm. &   │            │ (REST API Port) │            │ (GPIO, PWM, IR, │
│  WiFi Control)  │            └────────┬────────┘            │  DHT22, Button) │
└────────┬────────┘                     │                     └────────┬────────┘
         │                              │                              │
         └──────────────────────────────┼──────────────────────────────┘
                                        ▼
                         ┌─────────────────────────────┐
                         │   NVS Flash State & OTA     │
                         └─────────────────────────────┘
```

### 📌 جدول توصيل الأطراف (GPIO Pin Mapping)

| GPIO | الوظيفة / الطرف | النوع (Type) | الوصف |
|:---:|:---|:---:|:---|
| **0** | BOOT Button | Input (Pull-up) | إعادة ضبط المصنع وإلغاء تهيئة Matter عند الضغط لمدة 5 ثوانٍ |
| **2** | relay_1 | Digital Output | التحكم بالمرحل الأول (المصباح الرئيسي) |
| **18** | relay_2 | Digital Output | التحكم بالمرحل الثاني (قفل الباب) |
| **19** | relay_3 / AC Power | Digital Output | التحكم بمرحل الطاقة الخاص بالمكيف |
| **21** | relay_4 | Digital Output | مرحل إضافي (احتياطي) |
| **22** | pwm_lamp | PWM Output | التحكم في خفوت المصباح (5kHz, 8-bit) |
| **23** | pwm_rgb_r | PWM Output | قناة اللون الأحمر لشريط الإضاءة RGB |
| **25** | pwm_rgb_g | PWM Output | قناة اللون الأخضر لشريط الإضاءة RGB |
| **26** | pwm_rgb_b | PWM Output | قناة اللون الأزرق لشريط الإضاءة RGB |
| **4** | DHT22 | Input / Output | خط البيانات لحساس الحرارة والرطوبة |
| **32** | IR Receiver | Input (Pull-up) | قراءة إشارات الأشعة تحت الحمراء المستقبلة |
| **33** | IR Transmitter | Output | إرسال إشارات الأشعة تحت الحمراء بتردد حامل 38kHz |

---

## 2. كيفية عمل النظام الحالي (Arduino Runtime Logic)

1. **الإقلاع وتهيئة الملحقات (Boot & GPIO Init)**:
   - يتم تهيئة خطوط الإخراج الرقمية للمرحلات، وضبط خطوط الـ PWM باستخدام مكتبة `ledc` للتحكم في المصباح الخافت وشريط الإضاءة RGB.
   - يتم استدعاء `restorePinStates()` لقراءة آخر حالة محفوظة للمخرجات ودرجة الحرارة المستهدفة للمكيف من الذاكرة الدائمة (NVS via Preferences) وتطبيقها فوراً على المخارج لتفادي انقطاع الخدمة عند انقطاع الكهرباء.

2. **تهيئة بروتوكول Matter**:
   - يتم تسجيل 6 نقاط نهاية (Endpoints):
     - **EP 1-4**: مصابيح ذكية (OnOff Light) للتحكم بالمرحلات الأربعة.
     - **EP 5**: مصباح قابل لتعديل السطوع (Dimmable Light) للتحكم بالـ PWM الخاص بـ `pwm_lamp`.
     - **EP 6**: إضاءة ملونة (Color Light) للتحكم بـ RGB LED باستخدام تحويل الألوان من HSV إلى RGB.
   - تبدأ عملية الإعلان (Advertising) عبر البلوتوث منخفض الطاقة (BLE) لإقران الجهاز بالشبكة المنزلية (مثل Google Home) باستخدام رمز المرور الافتراضي `20202021`.

3. **خادم HTTP (REST API)**:
   - يعمل على منفذ `80` ليوفر لوحة تحكم محلية عبر طلبات JSON.
   - يحتوي على مسارات لقراءة البيانات اللحظية للحساسات `/sensors` وتغيير حالة الأطراف `/control/digital` و `/control/analog` والتحكم بالمكيف وسجل الأشعة تحت الحمراء والتحديث اللاسلكي OTA.

4. **التحكم بالـ IR (مستقبل ومرسل)**:
   - عند طلب `/control/ir/learn` يتم الاستماع لمدة 10 ثوانٍ لأي كود مرسل وحفظه إما كـ RAW timings (نبضات وفترات زمنية بالميكروثانية) أو كـ Protocol-encoded (مثل NEC، Samsung، Sony).
   - عند طلب `/control/ir/send` يتم إرسال الإشارة عبر الطرف 33.

5. **التحديث اللاسلكي (OTA)**:
   - يدعم التحديث المباشر من خلال دفع الملف (Push via ArduinoOTA) أو تنزيله من خادم خارجي عبر مسار الويب `/ota/update` والذي يتم تشغيله في مهمة FreeRTOS منفصلة لحماية الويب سيرفر من التوقف أثناء التنزيل والكتابة على الفلاش.

---

## 3. خارطة الانتقال والمطابقة مع إطار ESP-IDF

لإعادة بناء هذا المشروع باستخدام **ESP-IDF**، يجب استبدال مكتبات Arduino بالمكونات (Components) والتعريفات البرمجية الأصلية للنظام على النحو التالي:

| الوظيفة في Arduino | المكون البديل في ESP-IDF | ملفات الرأس الأساسية (Headers) |
|:---|:---|:---|
| `MatterOnOffLight`, `Matter.h` | **esp-matter SDK** | `esp_matter.h`, `esp_matter_endpoint.h` |
| `WiFi.h` | **ESP-IDF WiFi Driver + LwIP** | `esp_wifi.h`, `esp_event.h`, `esp_netif.h` |
| `ESPAsyncWebServer` | **ESP-IDF HTTP Server** | `esp_http_server.h` |
| `ArduinoJson` | **cJSON** (أو تفعيل C++ وإدراج ArduinoJson) | `cjson/cJSON.h` |
| `pinMode`, `digitalWrite` | **GPIO Driver** | `driver/gpio.h` |
| `ledcAttach`, `ledcWrite` | **LEDC Driver (PWM)** | `driver/ledc.h` |
| `Preferences` | **NVS Flash Driver** | `nvs_flash.h`, `nvs.h` |
| `IRremote` | **RMT (Remote Control) Driver** | `driver/rmt_tx.h`, `driver/rmt_rx.h` |
| `HTTPUpdate`, `Update` | **ESP HTTPS OTA Component** | `esp_https_ota.h`, `esp_ota_ops.h` |
| `DHT.h` | **Custom bit-bang or esp-idf-lib** | `dht.h` (مكون خارجي) |

---

## 4. التفاصيل التقنية للتنفيذ بـ ESP-IDF

### 🛠️ 1. إعداد خيارات البناء والتقسيم (CMake & Partition Table)

أولاً، في ملف `platformio.ini` الخاص بـ ESP-IDF، يجب إعداد المنصة وإضافة المسارات لملف التقسيم (Partition Table).

#### `platformio.ini`
```ini
[env:esp32doit-devkit-v1]
platform = espressif32
board = esp32doit-devkit-v1
framework = espidf
monitor_speed = 115200

; استخدام نفس تقسيم الفلاش الخاص بالـ OTA
board_build.partitions = partitions.csv
```

#### `partitions.csv`
يجب تهيئة تقسيم الفلاش ليتناسب مع متطلبات الـ OTA وبروتوكول Matter (الذي يتطلب مساحة nvs لا تقل عن 0x6000 بايت ومساحة تطبيق كافية):
```csv
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x6000,
otadata,  data, ota,     ,        0x2000,
phy_init, data, phy,     ,        0x1000,
app0,     app,  ota_0,   0x20000, 0x1E0000,
app1,     app,  ota_1,   ,        0x1E0000,
```

---

### 🌐 2. تهيئة الواي فاي والشبكة (WiFi & Netif)

في إطار عمل ESP-IDF، يتم تشغيل الواي فاي من خلال نظام الأحداث (Event Loop) بالخطوات التالية:

```c
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

static const char *TAG = "WIFI_MANAGER";
static int s_retry_num = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 10) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying connection to AP...");
        } else {
            ESP_LOGE(TAG, "Failed to connect, starting softAP fallback...");
            // كود تشغيل SoftAP الاختياري
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
    }
}

void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ">_",
            .password = "Qwertyuio0qwertyuio0",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
```

---

### 🔌 3. التحكم الرقمي والـ PWM (GPIO & LEDC)

- **المخارج الرقمية (Relays)**: نستخدم `driver/gpio.h`.
- **الـ PWM (pwm_lamp, RGB Red, Green, Blue)**: نستخدم `driver/ledc.h`.

```c
#include "driver/gpio.h"
#include "driver/ledc.h"

// تهيئة المخارج الرقمية
void gpio_init_outputs() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<2) | (1ULL<<18) | (1ULL<<19) | (1ULL<<21),
        .mode = GPIO_MODE_INPUT_OUTPUT, // لكي نتمكن من قراءة الحالة أيضاً
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

// تهيئة الـ PWM للمصباح والـ RGB LED
void ledc_init_pwm() {
    // 1. إعداد المؤقت (Timer Configuration)
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT, // 0-255
        .freq_hz          = 5000,             // 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // 2. إعداد قنوات البث (Channels Configuration)
    uint8_t pwm_pins[] = {22, 23, 25, 26};
    for(int i = 0; i < 4; i++) {
        ledc_channel_config_t ledc_channel = {
            .speed_mode     = LEDC_LOW_SPEED_MODE,
            .channel        = (ledc_channel_t)i,
            .timer_sel      = LEDC_TIMER_0,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = pwm_pins[i],
            .duty           = 0,
            .hpoint         = 0
        };
        ledc_channel_config(&ledc_channel);
    }
}

// تغيير إضاءة الـ PWM
void set_pwm_duty(uint8_t gpio, uint32_t duty) {
    ledc_channel_t channel = LEDC_CHANNEL_0;
    if (gpio == 22) channel = LEDC_CHANNEL_0;
    else if (gpio == 23) channel = LEDC_CHANNEL_1;
    else if (gpio == 25) channel = LEDC_CHANNEL_2;
    else if (gpio == 26) channel = LEDC_CHANNEL_3;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}
```

---

### 💾 4. حفظ واسترجاع الحالة (NVS Storage)

تخزين الحالة في ESP-IDF بسيط للغاية ويتم مباشرة عبر واجهة الـ NVS الأصلية:

```c
#include "nvs_flash.h"
#include "nvs.h"

void save_pin_state(uint8_t gpio, int value) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("pins_state", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "p%d", gpio);
        nvs_set_i32(my_handle, key, value);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

int get_pin_state(uint8_t gpio, int default_val) {
    nvs_handle_t my_handle;
    int32_t value = default_val;
    esp_err_t err = nvs_open("pins_state", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "p%d", gpio);
        nvs_get_i32(my_handle, key, &value);
        nvs_close(my_handle);
    }
    return value;
}
```

---

### 🌐 5. خادم الويب HTTP Server

يستخدم ESP-IDF محرك ويب مدمج وخفيف الوزن وفعّال ومبني على المهام المتزامنة.

```c
#include <esp_http_server.h>
#include "cJSON.h"

// معالج طلب الـ Ping
static esp_err_t ping_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "device", "SmartHome-ESP32");
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);
    
    const char *sys_info = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, sys_info);
    
    free((void*)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

// معالج تغيير الحالة الرقمية (Control Digital)
static esp_err_t control_digital_post_handler(httpd_req_t *req) {
    char buf[128];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON too long");
        return ESP_FAIL;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *pin_obj = cJSON_GetObjectItem(json, "pin");
    cJSON *val_obj = cJSON_GetObjectItem(json, "value");
    
    if (pin_obj && val_obj) {
        int pin = pin_obj->valueint;
        int val = val_obj->valueint;
        
        gpio_set_level(pin, val);
        save_pin_state(pin, val);
        
        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "status", "ok");
        cJSON_AddNumberToObject(res, "pin", pin);
        cJSON_AddNumberToObject(res, "value", val);
        
        const char *response_str = cJSON_PrintUnformatted(res);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, response_str);
        
        free((void*)response_str);
        cJSON_Delete(res);
    }
    cJSON_Delete(json);
    return ESP_OK;
}

// تشغيل الخادم وتسجيل المسارات
httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        // تسجيل مسار GET /ping
        httpd_uri_t ping_uri = {
            .uri       = "/ping",
            .method    = HTTP_GET,
            .handler   = ping_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &ping_uri);

        // تسجيل مسار POST /control/digital
        httpd_uri_t control_digital_uri = {
            .uri       = "/control/digital",
            .method    = HTTP_POST,
            .handler   = control_digital_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &control_digital_uri);
    }
    return server;
}
```

---

### 📶 6. محاكي التحكم بالأشعة الحمراء (IR Receiver & Sender via RMT)

يحتوي الـ ESP32 على وحدة متخصصة للتحكم بالنبضات الزمنية تدعى **RMT** (Remote Control). في الإصدارات الحديثة من ESP-IDF يتم تشغيلها كالتالي:

```c
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"

// تهيئة وإرسال نبضات خام (Raw Timings)
void rmt_send_raw(uint16_t* durations, size_t length, uint32_t freq_hz) {
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = 33, // IR_SEND_PIN
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1us precision
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_channel_handle_t tx_chan = NULL;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan));

    // إعداد التردد الناقل (38 kHz Carrier)
    rmt_carrier_config_t carrier_config = {
        .frequency_hz = freq_hz,
        .duty_cycle = 0.33,
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(tx_chan, &carrier_config));
    ESP_ERROR_CHECK(rmt_enable(tx_chan));

    // تحويل الأزمان الزمنية إلى RMT Symbols
    rmt_symbol_type_t* rmt_data = malloc(sizeof(rmt_symbol_type_t) * length);
    for (size_t i = 0; i < length; i++) {
        // تحويل كل نبضة/فراغ إلى صيغة RMT
        rmt_data[i] = (rmt_symbol_type_t) {
            .level0 = (i % 2 == 0) ? 1 : 0,
            .duration0 = durations[i],
            .level1 = 0,
            .duration1 = 0
        };
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    // إرسال الإشارة
    rmt_transmit(tx_chan, rmt_data, length, &tx_config);

    // تنظيف الموارد
    free(rmt_data);
    rmt_disable(tx_chan);
    rmt_del_channel(tx_chan);
}
```

---

### 🌡️ 7. قراءة حساس DHT22

نظراً لعدم وجود إدخال مباشر لحساس DHT22 في حزمة إسبراسيف الرسمية، يمكنك إضافة المكون الشهير المفتوح المصدر `esp-idf-lib` أو كتابة قارئ يدوي دقيق معتمداً على فترات الانتظار الزمنية بالميكروثانية:

```c
#include "rom/ets_sys.h"
#include "driver/gpio.h"

#define DHT_PIN 4

// دالة مبسطة لقراءة نبضة DHT22
static esp_err_t read_dht_raw(float *temperature, float *humidity) {
    uint8_t data[5] = {0,0,0,0,0};
    
    // إرسال إشارة البدء (Start Signal)
    gpio_set_direction(DHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_PIN, 0);
    ets_delay_us(20000); // الانتظار 20ms
    gpio_set_level(DHT_PIN, 1);
    ets_delay_us(40);
    
    // التحول إلى وضع القراءة والاستجابة
    gpio_set_direction(DHT_PIN, GPIO_MODE_INPUT);
    
    // التحقق من استجابة الحساس (نفس منطق أردوينو)
    // انتظر استجابة الحساس المنخفضة ثم المرتفعة
    // ثم ابدأ بقراءة الـ 40 بت (5 بايت) من خلال قياس طول النبضة المرتفعة
    
    // ... تفاصيل قراءة البتات وتجميعها وتحويلها إلى درجات حرارة ورطوبة ...
    
    return ESP_OK;
}
```
> [!TIP]
> يفضل وبشدة إدراج مكتبة مثل `esp-idf-lib` (والتي تحتوي على مكون `dht`) كـ Git Submodule في مجلد `components/` لتفادي المشاكل المتعلقة بالاستجابة الزمنية للـ Interrupts على نواة المعالج.

---

### 🔄 8. نظام التحديث اللاسلكي OTA

يحتوي إطار ESP-IDF على محرك ذكي مدمج للتحديث المباشر من خلال HTTPS. يمكن تشغيله عبر خادم الويب عند تلقي طلب OTA من خلال تشغيل مهمة FreeRTOS كالتالي:

```c
#include "esp_https_ota.h"

void ota_task(void *pvParameter) {
    char *url = (char*)pvParameter;
    ESP_LOGI("OTA", "Starting OTA from URL: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        // .cert_pem = server_cert_pem, // للحماية عبر HTTPS
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI("OTA", "OTA Success! Rebooting in 2 seconds...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        ESP_LOGE("OTA", "OTA Failed!");
        // تحديث حالة النظام بالفشل
    }
    vTaskDelete(NULL);
}

// لتشغيله من خلال معالج طلب الويب:
// xTaskCreate(&ota_task, "ota_task", 8192, firmware_url, 5, NULL);
```

---

### 🛡️ 9. دمج بروتوكول Matter الأصلي (`esp-matter`)

يعتمد **esp-matter** على مفاهيم العقد (Nodes) ونقاط النهاية (Endpoints) والـ Clusters والـ Attributes.

إليك هيكل تهيئة نقاط النهاية لمشروعنا (OnOff Lights للمرحلات، Dimmable للمصباح، Color للـ RGB):

```cpp
#include <esp_matter.h>
#include <esp_matter_console.h>

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

// تعريف مستمع تغييرات الخصائص (Attribute Update Callback)
static esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data) {
    if (type == PRE_UPDATE) {
        if (cluster_id == chip::app::Clusters::OnOff::Id && attribute_id == chip::app::Clusters::OnOff::Attributes::OnOff::Id) {
            bool state = val->val.b;
            if (endpoint_id == 1) gpio_set_level(2, state);       // Relay 1
            else if (endpoint_id == 2) gpio_set_level(18, state);  // Relay 2
            else if (endpoint_id == 3) gpio_set_level(19, state);  // Relay 3
            else if (endpoint_id == 4) gpio_set_level(21, state);  // Relay 4
        }
        else if (cluster_id == chip::app::Clusters::LevelControl::Id && attribute_id == chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id) {
            uint8_t brightness = val->val.u8;
            if (endpoint_id == 5) set_pwm_duty(22, brightness); // Dimmable Lamp
        }
        // يمكن إضافة معالجة الألوان هنا لنقطة النهاية 6
    }
    return ESP_OK;
}

void app_main() {
    // 1. تهيئة الذاكرة وتهيئة النواة
    nvs_flash_init();
    
    // 2. إنشاء العقدة الرئيسية
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, NULL);

    // 3. إضافة نقاط النهاية (Endpoints)
    // Relay 1 (Endpoint 1)
    on_off_light::config_t light1_config;
    endpoint_t *ep1 = on_off_light::create(node, &light1_config, CLUSTER_MASK_SERVER, NULL);
    
    // Relay 2 (Endpoint 2)
    on_off_light::config_t light2_config;
    endpoint_t *ep2 = on_off_light::create(node, &light2_config, CLUSTER_MASK_SERVER, NULL);
    
    // Dimmable Lamp (Endpoint 5)
    dimmable_light::config_t dim_light_config;
    endpoint_t *ep5 = dimmable_light::create(node, &dim_light_config, CLUSTER_MASK_SERVER, NULL);
    
    // 4. بدء تشغيل Matter Stack
    esp_matter::start(node);
}
```

---

## 5. خطة وخطوات البناء للبدء فوراً

لإعادة بناء هذا المشروع بنفسك:

1. **التهيئة والتحضير**:
   - تأكد من تثبيت بيئة عمل ESP-IDF (نسخة 5.1 أو أحدث هي المفضلة لـ `esp-matter`).
   - قم بدمج كود تهيئة المخرجات الرقمية والـ LEDC في ملف `gpio_manager.c`.
2. **برمجة الشبكة والويب**:
   - أنشئ `wifi_manager.c` لتشغيل شبكة الواي فاي.
   - ابنِ `http_server.c` لإطلاق خادم الويب ونفذ مسار `/ping` أولاً كاختبار للاتصال.
3. **برمجة الملحقات الفرعية**:
   - اضف مكتبة `dht` الحساسة واقرأ البيانات لترسلها عبر مسار `/sensors`.
   - قم بإعداد RMT للـ IR وتوصيله بطلبات الويب.
4. **تجميع الـ Matter**:
   - اضف حزمة `esp-matter` كعنصر تابع للمشروع وقم بتعريف نقاط النهاية وتمرير التغييرات لوظائف GPIO و LEDC التي قمت ببنائها سابقاً.
