# توثيق واجهة برمجة التطبيقات (API Schemas) لنظام Smart Home IoT

يحتوي هذا الملف على المواصفات القياسية لجميع هياكل البيانات (JSON Schema) المستخدمة حالياً في الاتصال بين تطبيق Flutter ولوحة التحكم ESP32 عبر كل من بروتوكول HTTP REST وعبر WebSocket.

---

## 1. بروتوكول HTTP REST API

### 1.1 فحص الاتصال (`GET /ping`)
يستخدم للتحقق من استجابة لوحة ESP32.

#### 1.1.1 مخطط الاستجابة (Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "PingResponse",
  "type": "object",
  "required": ["status", "device", "version", "uptime"],
  "properties": {
    "status": {
      "type": "string",
      "enum": ["ok"]
    },
    "device": {
      "type": "string"
    },
    "version": {
      "type": "string"
    },
    "uptime": {
      "type": "integer",
      "minimum": 0
    }
  }
}
```

#### 1.1.2 مثال البيانات الصادرة (Response Example)
```json
{
  "status": "ok",
  "device": "SmartHome-ESP32",
  "version": "1.0.0",
  "uptime": 120
}
```

---

### 1.2 قراءة الحساسات وحالات المنافذ (`GET /sensors`)
يجلب البيانات البيئية وحالة منافذ الـ GPIO (الريليهات والـ PWM).

#### 1.2.1 مخطط الاستجابة (Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "SensorsResponse",
  "type": "object",
  "required": ["temperature", "humidity", "pins", "wifi_rssi", "heap_free", "target_temperature"],
  "properties": {
    "temperature": {
      "type": "number",
      "description": "درجة الحرارة الحالية بسيلسيوس"
    },
    "humidity": {
      "type": "number",
      "description": "نسبة الرطوبة الحالية"
    },
    "pins": {
      "type": "object",
      "required": ["relay_1", "relay_2", "relay_3", "relay_4", "pwm_lamp", "pwm_rgb_r", "pwm_rgb_g", "pwm_rgb_b"],
      "properties": {
        "relay_1": { "type": "integer", "enum": [0, 1] },
        "relay_2": { "type": "integer", "enum": [0, 1] },
        "relay_3": { "type": "integer", "enum": [0, 1] },
        "relay_4": { "type": "integer", "enum": [0, 1] },
        "pwm_lamp": { "type": "integer", "minimum": 0, "maximum": 255 },
        "pwm_rgb_r": { "type": "integer", "minimum": 0, "maximum": 255 },
        "pwm_rgb_g": { "type": "integer", "minimum": 0, "maximum": 255 },
        "pwm_rgb_b": { "type": "integer", "minimum": 0, "maximum": 255 }
      }
    },
    "wifi_rssi": {
      "type": "integer",
      "description": "قوة إشارة الواي فاي بالديسيبل"
    },
    "heap_free": {
      "type": "integer",
      "description": "المساحة الحرة المتبقية في الذاكرة (RAM) بالبايت"
    },
    "target_temperature": {
      "type": "integer",
      "description": "درجة الحرارة المستهدفة للمكيف"
    }
  }
}
```

#### 1.2.2 مثال البيانات الصادرة (Response Example)
```json
{
  "temperature": 24.5,
  "humidity": 58.2,
  "pins": {
    "relay_1": 0,
    "relay_2": 1,
    "relay_3": 0,
    "relay_4": 0,
    "pwm_lamp": 128,
    "pwm_rgb_r": 0,
    "pwm_rgb_g": 255,
    "pwm_rgb_b": 100
  },
  "wifi_rssi": -62,
  "heap_free": 184520,
  "target_temperature": 24
}
```

---

### 1.3 التحكم بالمنافذ الرقمية (`POST /control/digital`)
يستخدم للتحكم بتشغيل أو إطفاء ريليه معين.

#### 1.3.1 مخطط الطلب الوارد (Request Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "DigitalControlRequest",
  "type": "object",
  "required": ["pin", "value"],
  "properties": {
    "pin": {
      "type": "integer",
      "description": "رقم منفذ GPIO المطلوب التحكم به (مثال: 2 للريليه الأول)"
    },
    "value": {
      "type": "integer",
      "enum": [0, 1],
      "description": "الحالة المطلوبة (0 للإطفاء، 1 للتشغيل)"
    }
  }
}
```

#### 1.3.2 مثال الطلب الوارد (Request Example)
```json
{
  "pin": 2,
  "value": 1
}
```

#### 1.3.3 مخطط الاستجابة الصادرة (Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "DigitalControlResponse",
  "type": "object",
  "required": ["status", "pin", "label", "value"],
  "properties": {
    "status": { "type": "string", "enum": ["ok"] },
    "pin": { "type": "integer" },
    "label": { "type": "string" },
    "value": { "type": "integer", "enum": [0, 1] }
  }
}
```

#### 1.3.4 مثال الاستجابة الصادرة (Response Example)
```json
{
  "status": "ok",
  "pin": 2,
  "label": "relay_1",
  "value": 1
}
```

---

### 1.4 التحكم بمنافذ التعديل النبضي (`POST /control/analog`)
التحكم في شدة الإضاءة (PWM Lamp) أو قنوات الإضاءة الملونة (RGB).

#### 1.4.1 مخطط الطلب الوارد (Request Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "AnalogControlRequest",
  "type": "object",
  "required": ["pin", "value"],
  "properties": {
    "pin": {
      "type": "integer",
      "description": "رقم منفذ GPIO المطلوب التحكم به (مثال: 22 لـ pwm_lamp)"
    },
    "value": {
      "type": "integer",
      "minimum": 0,
      "maximum": 255,
      "description": "دورة العمل (Duty Cycle) من 0 (مطفأ بالكامل) إلى 255 (أعلى سطوع)"
    }
  }
}
```

#### 1.4.2 مثال الطلب الوارد (Request Example)
```json
{
  "pin": 22,
  "value": 128
}
```

#### 1.4.3 مخطط الاستجابة الصادرة (Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "AnalogControlResponse",
  "type": "object",
  "required": ["status", "pin", "label", "value"],
  "properties": {
    "status": { "type": "string", "enum": ["ok"] },
    "pin": { "type": "integer" },
    "label": { "type": "string" },
    "value": { "type": "integer", "minimum": 0, "maximum": 255 }
  }
}
```

#### 1.4.4 مثال الاستجابة الصادرة (Response Example)
```json
{
  "status": "ok",
  "pin": 22,
  "label": "pwm_lamp",
  "value": 128
}
```

---

### 1.5 التحكم بالمكيف عبر بروتوكول خاص (`POST /control/ac`)
يُسخدم لإرسال أوامر تشغيل/إطفاء المكيف وتحديد درجة الحرارة.

#### 1.5.1 مخطط الطلب الوارد (Request Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "AcControlRequest",
  "type": "object",
  "required": ["isOn"],
  "properties": {
    "isOn": {
      "type": "boolean"
    },
    "target_temp": {
      "type": "integer",
      "minimum": 16,
      "maximum": 30
    }
  }
}
```

#### 1.5.2 مثال الطلب الوارد (Request Example)
```json
{
  "target_temp": 22,
  "isOn": true
}
```

#### 1.5.3 مخطط الاستجابة الصادرة (Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "AcControlResponse",
  "type": "object",
  "required": ["status", "target_temperature", "isOn"],
  "properties": {
    "status": { "type": "string", "enum": ["ok"] },
    "target_temperature": { "type": "integer" },
    "isOn": { "type": "boolean" }
  }
}
```

#### 1.5.4 مثال الاستجابة الصادرة (Response Example)
```json
{
  "status": "ok",
  "target_temperature": 22,
  "isOn": true
}
```

---

### 1.6 تسجيل إشارات الأشعة تحت الحمراء (`GET /control/ir/learn`)
يهيئ الـ ESP32 للاستماع لإشارة IR قادمة من جهاز تحكم وحفظها.

#### 1.6.1 مخطط الاستجابة الصادرة (Decoded Protocol Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "IrLearnProtocolResponse",
  "type": "object",
  "required": ["status", "protocol", "value", "bits"],
  "properties": {
    "status": { "type": "string", "enum": ["ok"] },
    "protocol": { "type": "string", "description": "بروتوكول الإشارة (مثل NEC, SAMSUNG, LG...)" },
    "value": { "type": "string", "description": "القيمة المستقبلة بالصيغة الست عشرية (Hex)" },
    "bits": { "type": "integer", "description": "عدد البتات المفكوكة" },
    "address": { "type": "integer" },
    "command": { "type": "integer" },
    "rawData": { "type": "integer" },
    "frequency": { "type": "integer", "default": 38 }
  }
}
```

#### 1.6.2 مثال الاستجابة (Decoded Protocol Response Example)
```json
{
  "status": "ok",
  "protocol": "NEC",
  "value": "0x1FE48B7",
  "bits": 32,
  "address": 0,
  "command": 72,
  "rawData": 1234567,
  "frequency": 38
}
```

#### 1.6.3 مخطط الاستجابة الصادرة للإشارات الخام (Raw Timings Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "IrLearnRawResponse",
  "type": "object",
  "required": ["status", "protocol", "value", "bits", "frequency"],
  "properties": {
    "status": { "type": "string", "enum": ["ok"] },
    "protocol": { "type": "string", "enum": ["RAW"] },
    "value": { "type": "string", "description": "سلسلة نصية تحتوي أزمنة النبضات مفصولة بفواصل" },
    "bits": { "type": "integer", "description": "عدد النبضات" },
    "frequency": { "type": "integer" }
  }
}
```

#### 1.6.4 مثال الاستجابة للإشارات الخام (Raw Timings Response Example)
```json
{
  "status": "ok",
  "protocol": "RAW",
  "value": "9020,4480,560,560,560,1680",
  "bits": 6,
  "frequency": 38
}
```

---

### 1.7 إرسال إشارات الأشعة تحت الحمراء (`POST /control/ir/send`)
إعادة بث إشارة IR مخزنة لتشغيل أجهزة الاستقبال (مكيف، تلفاز، إلخ).

#### 1.7.1 مخطط الطلب الوارد (Request Schema - Decoded / Raw)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "IrSendRequest",
  "type": "object",
  "required": ["protocol", "value", "bits"],
  "properties": {
    "protocol": { "type": "string" },
    "value": { "type": "string" },
    "bits": { "type": "integer" },
    "address": { "type": "integer" },
    "command": { "type": "integer" },
    "frequency": { "type": "integer" },
    "headerMark": { "type": "integer" },
    "headerSpace": { "type": "integer" },
    "oneMark": { "type": "integer" },
    "oneSpace": { "type": "integer" },
    "zeroMark": { "type": "integer" },
    "zeroSpace": { "type": "integer" },
    "isMsb": { "type": "boolean" }
  }
}
```

#### 1.7.2 مثال الطلب الوارد (Protocol Encoded Request Example)
```json
{
  "protocol": "NEC",
  "value": "0x1FE48B7",
  "bits": 32,
  "address": 0,
  "command": 72
}
```

#### 1.7.3 مخطط الاستجابة (Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "IrSendResponse",
  "type": "object",
  "required": ["status", "message"],
  "properties": {
    "status": { "type": "string", "enum": ["ok"] },
    "message": { "type": "string" }
  }
}
```

#### 1.7.4 مثال الاستجابة (Response Example)
```json
{
  "status": "ok",
  "message": "IR signal transmitted successfully"
}
```

---

### 1.8 طلب تحديث النظام لاسلكياً (`POST /ota/update`)

#### 1.8.1 مخطط الطلب الوارد (Request Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "OtaUpdateRequest",
  "type": "object",
  "required": ["url"],
  "properties": {
    "url": {
      "type": "string",
      "format": "uri",
      "description": "رابط خادم التحديث المباشر للملف الثنائي firmware.bin"
    }
  }
}
```

#### 1.8.2 مثال الطلب الوارد (Request Example)
```json
{
  "url": "http://192.168.1.100/firmware.bin"
}
```

#### 1.8.3 مخطط الاستجابة الصادرة (Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "OtaUpdateResponse",
  "type": "object",
  "required": ["status", "message"],
  "properties": {
    "status": { "type": "string", "enum": ["accepted"] },
    "message": { "type": "string" }
  }
}
```

#### 1.8.4 مثال الاستجابة (Response Example)
```json
{
  "status": "accepted",
  "message": "OTA update started. Poll /ota/status for progress."
}
```

---

### 1.9 تتبع حالة التحديث (`GET /ota/status`)

#### 1.9.1 مخطط الاستجابة (Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "OtaStatusResponse",
  "type": "object",
  "required": ["state", "progress"],
  "properties": {
    "state": {
      "type": "string",
      "enum": ["idle", "in_progress", "success", "failed"]
    },
    "progress": {
      "type": "integer",
      "minimum": 0,
      "maximum": 100,
      "description": "نسبة التقدم المئوية للتنزيل والتثبيت"
    }
  }
}
```

#### 1.9.2 مثال الاستجابة (Response Example)
```json
{
  "state": "in_progress",
  "progress": 45
}
```

---

### 1.10 تفاصيل معلومات النظام (`GET /system/info`)

#### 1.10.1 مخطط الاستجابة (Response Schema)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "SystemInfoResponse",
  "type": "object",
  "required": [
    "firmware", "device", "chip_model", "chip_cores", "cpu_mhz",
    "flash_mb", "heap_total", "heap_free", "uptime_s", "ip_address", "mac"
  ],
  "properties": {
    "firmware": { "type": "string" },
    "device": { "type": "string" },
    "chip_model": { "type": "string" },
    "chip_cores": { "type": "integer" },
    "cpu_mhz": { "type": "integer" },
    "flash_mb": { "type": "integer" },
    "heap_total": { "type": "integer" },
    "heap_free": { "type": "integer" },
    "uptime_s": { "type": "integer" },
    "ip_address": { "type": "string", "format": "ipv4" },
    "mac": { "type": "string" }
  }
}
```

#### 1.10.2 مثال الاستجابة (Response Example)
```json
{
  "firmware": "1.0.0",
  "device": "SmartHome-ESP32",
  "chip_model": "ESP32-D0WDQ6",
  "chip_cores": 2,
  "cpu_mhz": 240,
  "flash_mb": 4,
  "heap_total": 298124,
  "heap_free": 182390,
  "uptime_s": 240,
  "ip_address": "192.168.1.50",
  "mac": "AA:BB:CC:DD:EE:FF"
}
```

---

## 2. بروتوكول الاتصال ثنائي الاتجاه WebSocket API

يتم الاتصال عبر المسار الموحد `/ws` على خادم الـ ESP32.

### 2.1 الأحداث الصادرة من الخادم للعميل (Server to Client Events)

#### 2.1.1 تأكيد نجاح الاتصال الأول (Handshake Event)
* **الوصف**: يُرسل فور فتح قناة الاتصال.
* **مخطط البيانات (JSON Schema)**:
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "WsHandshake",
  "type": "object",
  "required": ["status"],
  "properties": {
    "status": { "type": "string", "enum": ["connected"] }
  }
}
```
* **مثال البيانات**:
```json
{
  "status": "connected"
}
```

#### 2.1.2 إرسال بيانات الحساسات البيئية بشكل دوري (Sensor Data Event)
* **الوصف**: يُبث بشكل دوري لتحديث واجهة المستخدم (كل 10 ثوانٍ).
* **مخطط البيانات (JSON Schema)**:
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "WsSensorDataEvent",
  "type": "object",
  "required": ["event", "temperature", "humidity"],
  "properties": {
    "event": { "type": "string", "enum": ["sensor_data"] },
    "temperature": { "type": "number" },
    "humidity": { "type": "number" }
  }
}
```
* **مثال البيانات**:
```json
{
  "event": "sensor_data",
  "temperature": 24.5,
  "humidity": 58.2
}
```

#### 2.1.3 تحديث حالة الريليهات المتزامنة (Relay Update Event)
* **الوصف**: يُرسل للعملاء عند تغيير حالة أي ريليه (سواء عن طريق Matter أو طلبات أخرى).
* **مخطط البيانات (JSON Schema)**:
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "WsRelayUpdateEvent",
  "type": "object",
  "required": ["event", "endpoint", "state"],
  "properties": {
    "event": { "type": "string", "enum": ["relay_update"] },
    "endpoint": { "type": "integer", "description": "رقم نقطة النهاية (Endpoint ID) في بروتوكول Matter" },
    "state": { "type": "integer", "enum": [0, 1] }
  }
}
```
* **مثال البيانات**:
```json
{
  "event": "relay_update",
  "endpoint": 1,
  "state": 1
}
```

#### 2.1.4 تحديث مستوى شدة الإضاءة المتزامنة (PWM Update Event)
* **الوصف**: يُرسل عند تعديل إضاءة قنوات التعديل النبضي.
* **مخطط البيانات (JSON Schema)**:
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "WsPwmUpdateEvent",
  "type": "object",
  "required": ["event", "endpoint", "level"],
  "properties": {
    "event": { "type": "string", "enum": ["pwm_update"] },
    "endpoint": { "type": "integer" },
    "level": { "type": "integer", "minimum": 0, "maximum": 255 }
  }
}
```
* **مثال البيانات**:
```json
{
  "event": "pwm_update",
  "endpoint": 5,
  "level": 128
}
```

---

### 2.2 الطلبات الواردة من العميل للخادم (Client to Server Messages)

#### 2.2.1 تعيين حالة ريليه رقمي (`set_relay`)
* **الوصف**: يرسله التطبيق لتشغيل أو إطفاء منفذ ريليه محدد.
* **مخطط البيانات (JSON Schema)**:
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "WsSetRelayAction",
  "type": "object",
  "required": ["action", "pin", "value"],
  "properties": {
    "action": { "type": "string", "enum": ["set_relay"] },
    "pin": { "type": "integer", "description": "رقم منفذ الـ GPIO" },
    "value": { "type": "integer", "enum": [0, 1] }
  }
}
```
* **مثال البيانات**:
```json
{
  "action": "set_relay",
  "pin": 2,
  "value": 1
}
```

#### 2.2.2 تعيين قيمة دورة العمل (`set_pwm`)
* **الوصف**: يرسله التطبيق للتحكم في مستوى السطوع.
* **مخطط البيانات (JSON Schema)**:
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "WsSetPwmAction",
  "type": "object",
  "required": ["action", "pin", "value"],
  "properties": {
    "action": { "type": "string", "enum": ["set_pwm"] },
    "pin": { "type": "integer", "description": "رقم منفذ الـ GPIO" },
    "value": { "type": "integer", "minimum": 0, "maximum": 255 }
  }
}
```
* **مثال البيانات**:
```json
{
  "action": "set_pwm",
  "pin": 22,
  "value": 128
}
```
