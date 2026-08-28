//#include "dl_lib_matrix3d.h"
#include <esp32-hal-ledc.h>
#include <Arduino.h>
int speed = 80;
int noStop = 0;

int controlMode = 0;  // 0 = ручной, 1 = автономный
int new_command = 0; // 0 - стоп, 1 - вперед, 2 - назад, 3 - влево поворот, 4 - вправо поворот
bool new_start = false;  // false = не запущено, true = запущено
float set_distance = 0.0;  // Дистанция в метрах
extern bool savePhotosToSD;
extern String saveSessionFolder;
extern int photoCounter;

#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "Arduino.h"

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];
  //dl_matrix3du_t *image_matrix = NULL;

  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Не удалось захватить камеру");
      res = ESP_FAIL;
    } else {
      {
        if (fb->format != PIXFORMAT_JPEG) {
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if (!jpeg_converted) {
            Serial.println("Сбой сжатия JPEG");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
    int64_t fr_end = esp_timer_get_time();
    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;
    frame_time /= 1000;
    //Serial.printf("MJPG: %uB %ums (%.1ffps)\n",
    //              (uint32_t)(_jpg_buf_len),
    //              (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time
    //             );
  }

  last_frame = 0;
  return res;
}

enum state {fwd, rev, stp};
state actstate = stp;

static esp_err_t cmd_handler(httpd_req_t *req)
{
  char*  buf;
  size_t buf_len;
  char variable[32] = {0,};
  char value[32] = {0,};

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
          httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  int val = atoi(value);
  sensor_t * s = esp_camera_sensor_get();
  int res = 0;

  if (!strcmp(variable, "framesize"))
  {
    Serial.println("framesize");
    if (s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val);
  }
  else if (!strcmp(variable, "quality"))
  {
    Serial.println("quality");
    res = s->set_quality(s, val);
  }
  
  else if (!strcmp(variable, "flash"))
  {
    ledcWrite(7, val);
  }
  else if (!strcmp(variable, "speed"))
  {
    if      (val > 255) val = 255;
    else if (val <   0) val = 0;
    speed = val;
  }
  else if (!strcmp(variable, "nostop"))
  {
    noStop = val;
  }
//   else if (!strcmp(variable, "servo")) // 3250, 4875, 6500
//   {
//     if      (val > 650) val = 650;
//     else if (val < 325) val = 325;
//     ledcWrite(8, 10 * val);
//   }
  else if (!strcmp(variable, "car")) {
    if (val == 1) {
      Serial.println("Вперёд");
      actstate = fwd;
    //   ledcWrite(4, speed - speedBalansR); // pin 12
    //   ledcWrite(3, 0);     // pin 13
    //   ledcWrite(5, speed - speedBalansL); // pin 14
    //   ledcWrite(6, 0);     // pin 15

      new_command = 1;
      delay(200);
    }
    else if (val == 2) {
      Serial.println("Налево");
    //   ledcWrite(3, 0);
    //   ledcWrite(5, 0);
    //   ledcWrite(4, speed - speedBalansR);
    //   ledcWrite(6, speed - speedBalansL);
      new_command = 3;
      delay(100);
    }
    else if (val == 3) {
      Serial.println("Стоп");
      actstate = stp;
    //   ledcWrite(4, 0);
    //   ledcWrite(3, 0);
    //   ledcWrite(5, 0);
    //   ledcWrite(6, 0);

      new_command = 0;
    }
    else if (val == 4) {
      Serial.println("Направо");
    //   ledcWrite(4, 0);
    //   ledcWrite(6, 0);
    //   ledcWrite(3, speed - speedBalansR);
    //   ledcWrite(5, speed - speedBalansL);

      new_command = 4;
      delay(100);
    }
    else if (val == 5) {
      Serial.println("Назад");
      actstate = rev;
    //   ledcWrite(4, 0);
    //   ledcWrite(3, speed - speedBalansR);
    //   ledcWrite(5, 0);
    //   ledcWrite(6, speed - speedBalansL);

      new_command = 2;
      delay(200);
    }
    else if (val == 6) {
      Serial.println("Перезагрузка");
      ESP.restart();
    }
    if (noStop != 1) //Если включен режим с остановками
    {
    //   ledcWrite(3, 0);
    //   ledcWrite(4, 0);
    //   ledcWrite(5, 0);
    //   ledcWrite(6, 0);

      new_command = 0;
    }
  }
  else if (!strcmp(variable, "controlMode"))
  {
    controlMode = val;
    Serial.print("Режим управления: ");
    Serial.println(controlMode ? "Автономный" : "Ручной");
  }
  else if (!strcmp(variable, "new_start"))
  {
    new_start = (val == 1);
    Serial.print("Старт: ");
    Serial.println(new_start ? "АКТИВЕН" : "ВЫКЛЮЧЕН");
  }
  else if (!strcmp(variable, "set_distance"))
  {
    // Для float нужно преобразовывать иначе
    float distance_val = atof(value);  // atof для float (atoi для int)
    set_distance = distance_val;
    Serial.printf("Установлена дистанция: %.2f м\n", set_distance);
  }
  else if (!strcmp(variable, "save_photos"))
  {
    savePhotosToSD = (val == 1);
    if (savePhotosToSD) {
      Serial.println("Сохранение фото ВКЛЮЧЕНО");
      photoCounter = 0; // Сброс счетчика
      // Можно создать новую папку для каждой сессии
      // saveSessionFolder = "/session_" + String(millis());
      // SD_MMC.mkdir(saveSessionFolder);
    } else {
      Serial.println("Сохранение фото ВЫКЛЮЧЕНО");
    }
  }
  else
  {
    Serial.println("variable");
    res = -1;
  }

  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];

  sensor_t * s = esp_camera_sensor_get();
  char * p = json_response;
  *p++ = '{';

  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!doctype html>
<html>
<head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <title>Панель управления</title>
<style>
body{
    font-family:Arial,Helvetica,sans-serif;
    background:#ffffff;
    color:#000000;
    font-size:16px
}
h2{
    font-size:18px
}
section.main{
    display:flex
}
#menu,section.main{
    flex-direction:column
}
#menu{
    display:none;
    flex-wrap:nowrap;
    min-width:340px;
    background:#363636;
    padding:8px;
    border-radius:4px;
    margin-top:-10px;
    margin-right:10px
}
#content{
    display:flex;
    flex-wrap:wrap;
    align-items:stretch
}
figure{
    padding:0;
    margin:0;
    -webkit-margin-before:0;
    margin-block-start:0;
    -webkit-margin-after:0;
    margin-block-end:0;
    -webkit-margin-start:0;
    margin-inline-start:0;
    -webkit-margin-end:0;
    margin-inline-end:0
}
figure img{
    display:block;
    width:100%;
    height:auto;
    border-radius:4px;
    margin-top:8px
}
@media (min-width: 800px) and (orientation:landscape){
    #content{
        display:flex;
        flex-wrap:nowrap;
        align-items:stretch
    }
    figure img{
        display:block;
        max-width:100%;
        max-height:calc(100vh - 40px);
        width:auto;
        height:auto;
        margin-left: auto;
        margin-right: auto
    }
    figure{
        padding:0;
        margin:0;
        -webkit-margin-before:0;
        margin-block-start:0;
        -webkit-margin-after:0;
        margin-block-end:0;
        -webkit-margin-start:0;
        margin-inline-start:0;
        -webkit-margin-end:0;
        margin-inline-end:0
    }
}
section#buttons{
    display:flex;
    flex-wrap:nowrap;
    justify-content:space-between;
    margin-left: auto;
    margin-right: auto
}
#nav-toggle{
    cursor:pointer;
    display:block
}
#nav-toggle-cb{
    outline:0;
    opacity:0;
    width:0;
    height:0
}
#nav-toggle-cb:checked+#menu{
    display:flex
}
.input-group{
    display:flex;
    flex-wrap:nowrap;
    line-height:22px;
    margin:5px 0
}
.input-group>label{
    display:inline-block;
    padding-right:10px;
    min-width:47%
}
.input-group input,.input-group select{
    flex-grow:1
}
.range-max,.range-min{
    display:inline-block;
    padding:0 5px
}
button{
    display:block;
    margin:7px;
    padding:0 12px;
    border: 3px solid black;
    ;
    line-height:28px;
    cursor:pointer;
    color:#000000;
    background:#ffffff;
    border-radius:5px;
    font-size:16px;
    outline:0
}
button:hover{
    background:#c7c7c7
}
button:active{
    background:#9e9e9e
}
button.disabled{
    cursor:default;
    background:#a0a0a0
}
input[type=range]{
    -webkit-appearance:none;
    width:100%;
    height:22px;
    background:#ffffff;
    cursor:pointer;
    margin:0
}
input[type=range]:focus{
    outline:0
}
input[type=range]::-webkit-slider-runnable-track{
    width:100%;
    height:2px;
    cursor:pointer;
    background:#EFEFEF;
    border-radius:0;
    border:0 solid #EFEFEF
}
input[type=range]::-webkit-slider-thumb{
    border:1px solid rgba(0,0,30,0);
    height:22px;
    width:22px;
    border-radius:50px;
    background:#000000;
    cursor:pointer;
    -webkit-appearance:none;
    margin-top:-11.5px
}
input[type=range]:focus::-webkit-slider-runnable-track{
    background:#EFEFEF
}
input[type=range]::-moz-range-track{
    width:100%;
    height:2px;
    cursor:pointer;
    background:#EFEFEF;
    border-radius:0;
    border:0 solid #EFEFEF
}
input[type=range]::-moz-range-thumb{
    border:1px solid rgba(0,0,30,0);
    height:22px;
    width:22px;
    border-radius:50px;
    background:#ff3034;
    cursor:pointer
}
input[type=range]::-ms-track{
    width:100%;
    height:2px;
    cursor:pointer;
    background:0 0;
    border-color:transparent;
    color:transparent
}
input[type=range]::-ms-fill-lower{
    background:#EFEFEF;
    border:0 solid #EFEFEF;
    border-radius:0
}
input[type=range]::-ms-fill-upper{
    background:#EFEFEF;
    border:0 solid #EFEFEF;
    border-radius:0
}
input[type=range]::-ms-thumb{
    border:1px solid rgba(0,0,30,0);
    height:22px;
    width:22px;
    border-radius:50px;
    background:#ff3034;
    cursor:pointer;
    height:2px
}
input[type=range]:focus::-ms-fill-lower{
    background:#EFEFEF
}
input[type=range]:focus::-ms-fill-upper{
    background:#363636
}
.switch{
    display:block;
    position:relative;
    line-height:22px;
    font-size:16px;
    height:22px
}
.switch input{
    outline:0;
    opacity:0;
    width:0;
    height:0
}
.slider{
    width:50px;
    height:22px;
    border-radius:22px;
    cursor:pointer;
    background-color:grey
}
.slider,.slider:before{
    display:inline-block;
    transition:.4s
}
.slider:before{
    position:relative;
    content:"";
    border-radius:50%;
    height:16px;
    width:16px;
    left:4px;
    top:3px;
    background-color:#fff
}
input:checked+.slider{
    background-color:#ff3034
}
input:checked+.slider:before{
    -webkit-transform:translateX(26px);
    transform:translateX(26px)
}
select{
    border:1px solid #363636;
    font-size:14px;
    height:22px;
    outline:0;
    border-radius:5px
}
.image-container{
    position:relative;
    min-width:160px
}
.close{
    position:absolute;
    right:5px;
    top:5px;
    background:#ff3034;
    width:16px;
    height:16px;
    border-radius:100px;
    color:#fff;
    text-align:center;
    line-height:18px;
    cursor:pointer
}
.hidden{
    display:none
}
          
</style>
</head>
  <body>
    <figure>
      <div id="stream-container" class="image-container hidden">
        <div class="close" id="close-stream">×</div>
        <img id="stream" src="">
      </div>
    </figure>
    <section class="main">
    <section id="buttons">
      <table>
         <tr>
            <td align="center" style="display: none;"><button id="get-still">Фото</button></td>
            </td>
            <td align="center"><button id="restart" onclick="fetch(document.location.origin+'/control?var=car&val=6');">Перезагрузка</button></td>
            <td align="center"><button id="toggle-stream">Вкл.трнасл</button></td>
         </tr>

         <tr>
            <td><input type="checkbox" id="nostop" onclick="var noStop=0;if (this.checked) noStop=1;fetch(document.location.origin+'/control?var=nostop&val='+noStop);">Без ост.</td>
            <td align="center"><button id="forward" onclick="fetch(document.location.origin+'/control?var=car&val=1');">Вперёд</button></td>
            <td></td>
         </tr>
         <tr>
            <td align="center"><button id="turnleft" onclick="fetch(document.location.origin+'/control?var=car&val=2');">Налево</button></td>
            <td align="center"><button id="stop" onclick="fetch(document.location.origin+'/control?var=car&val=3');">Стоп</button></td>
            <td align="center"><button id="turnright" onclick="fetch(document.location.origin+'/control?var=car&val=4');">Направо</button></td>
         </tr>
         <tr>
            <td>
            </td>
            <td align="center"><button id="backward" onclick="fetch(document.location.origin+'/control?var=car&val=5');">Назад</button></td>
            <td></td>
         </tr>
         
         <tr>
            <td id="modeLabel">Режим: Ручной</td>
            <td align="center" colspan="1">
            <button id="toggleMode" onclick="toggleControlMode()">АВТОНОМНЫЙ РЕЖИМ</button>
            </td>
        </tr>

        <tr>
            <td id="startStatus">Старт: ВЫКЛ</td>
            <td align="center" colspan="2">
            <button id="startButton" onclick="toggleStart()" 
                style="background: #ff5722; color: white; width: 100%;">
                СТАРТ
            </button>
            </td>
        </tr>

         <tr>
            <td>Скорость</td>
            <td align="center" colspan="2"><input type="range" id="speed" min="75" max="130" value="80" onchange="try{fetch(document.location.origin+'/control?var=speed&val='+this.value);}catch(e){}"></td>
         </tr>
         <tr>

        <tr>
            <td>Дистанция, м</td>
            <td align="center" colspan="2">
                <input type="number" id="distanceInput" 
                min="0" max="100" step="0.1" value="0.0"
                style="width: 80%; padding: 5px;"
                onchange="updateDistance()">
            <button onclick="applyDistance()" 
                style="margin-left: 5px; padding: 5px 10px;">
            Применить
            </button>
            </td>
        </tr>


        <tr>
            <td id="saveStatus">Сохранение: ВЫКЛ</td>
            <td align="center" colspan="2">
                <button id="saveButton" onclick="toggleSavePhotos()" 
                    style="background: #2196F3; color: white; width: 100%;">
                    Начать запись
                </button>
            </td>
         <tr>
            <td>Качество</td>
            <td align="center" colspan="2"><input type="range" id="quality" min="10" max="63" value="10" onchange="try{fetch(document.location.origin+'/control?var=quality&val='+this.value);}catch(e){}"></td>
         </tr>
         <tr>
            <td>Разрешение</td>
            <td align="center" colspan="2"><input type="range" id="framesize" min="0" max="6" value="5" onchange="try{fetch(document.location.origin+'/control?var=framesize&val='+this.value);}catch(e){}"></td>
         </tr>
      </table>
    </section>
    </section>
    <script>
document.addEventListener('DOMContentLoaded', function() {
    function b(B) {
        let C;
        switch (B.type) {
            case 'checkbox':
                C = B.checked ? 1 : 0;
                break;
            case 'range':
            case 'select-one':
                C = B.value;
                break;
            case 'button':
            case 'submit':
                C = '1';
                break;
            default:
                return;
        }
        const D = `${c}/control?var=${B.id}&val=${C}`;
        fetch(D).then(E => {
            console.log(`request to ${D} finished, status: ${E.status}`)
        })
    }
    var c = document.location.origin;
    const e = B => {
            B.classList.add('hidden')
        },
        f = B => {
            B.classList.remove('hidden')
        },
        g = B => {
            B.classList.add('disabled'), B.disabled = !0
        },
        h = B => {
            B.classList.remove('disabled'), B.disabled = !1
        },
        i = (B, C, D) => {
            D = !(null != D) || D;
            let E;
            'checkbox' === B.type ? (E = B.checked, C = !!C, B.checked = C) : (E = B.value, B.value = C), D && E !== C ? b(B) : !D && ('aec' === B.id ? C ? e(v) : f(v) : 'agc' === B.id ? C ? (f(t), e(s)) : (e(t), f(s)) : 'awb_gain' === B.id ? C ? f(x) : e(x) : 'face_recognize' === B.id && (C ? h(n) : g(n)))
        };
    document.querySelectorAll('.close').forEach(B => {
        B.onclick = () => {
            e(B.parentNode)
        }
    }), fetch(`${c}/status`).then(function(B) {
        return B.json()
    }).then(function(B) {
        document.querySelectorAll('.default-action').forEach(C => {
            i(C, B[C.id], !1)
        })
    });
    const j = document.getElementById('stream'),
        k = document.getElementById('stream-container'),
        l = document.getElementById('get-still'),
        m = document.getElementById('toggle-stream'),
        n = document.getElementById('face_enroll'),
        o = document.getElementById('close-stream'),
        p = () => {
            window.stop(), m.innerHTML = 'Вкл.стрим'
        },
        q = () => {
            j.src = `${c+':81'}/stream`, f(k), m.innerHTML = 'Выкл.стрим'
        };
    l.onclick = () => {
        p(), j.src = `${c}/capture?_cb=${Date.now()}`, f(k)
    }, o.onclick = () => {
        p(), e(k)
    }, m.onclick = () => {
        const B = 'Выкл.стрим' === m.innerHTML;
        B ? p() : q()
    }, n.onclick = () => {
        b(n)
    }, document.querySelectorAll('.default-action').forEach(B => {
        B.onchange = () => b(B)
    });
    const r = document.getElementById('agc'),
        s = document.getElementById('agc_gain-group'),
        t = document.getElementById('gainceiling-group');
    r.onchange = () => {
        b(r), r.checked ? (f(t), e(s)) : (e(t), f(s))
    };
    const u = document.getElementById('aec'),
        v = document.getElementById('aec_value-group');
    u.onchange = () => {
        b(u), u.checked ? e(v) : f(v)
    };
    const w = document.getElementById('awb_gain'),
        x = document.getElementById('wb_mode-group');
    w.onchange = () => {
        b(w), w.checked ? f(x) : e(x)
    };
    const y = document.getElementById('face_detect'),
        z = document.getElementById('face_recognize'),
        A = document.getElementById('framesize');
    A.onchange = () => {
        b(A), 5 < A.value && (i(y, !1), i(z, !1))
    }, y.onchange = () => {
        return 5 < A.value ? (alert('Перед включением этой функции выберите разрешение CIF или более низкое!'), void i(y, !1)) : void(b(y), !y.checked && (g(n), i(z, !1)))
    }, z.onchange = () => {
        return 5 < A.value ? (alert('Перед включением этой функции выберите разрешение CIF или более низкое!'), void i(z, !1)) : void(b(z), z.checked ? (h(n), i(y, !0)) : g(n))
    }
});
        
document.addEventListener('keypress', function(event) {                    // Поддержка управления с клавиатуры
    switch (event.charCode) {
        case 119:
            fetch(document.location.origin + '/control?var=car&val=1');
            break                                                          // Вперёд - W
        case 115:
            fetch(document.location.origin + '/control?var=car&val=5');
            break                                                          // Назад - S
        case 97:
            fetch(document.location.origin + '/control?var=car&val=2');
            break                                                          // Налево - A
        case 100:
            fetch(document.location.origin + '/control?var=car&val=4');
            break                                                          // Направо - D
        case 112:
            fetch(document.location.origin + '/control?var=car&val=3');
            break                                                          // Стоп - P

        case 107:
            fetch(document.location.origin + '/control?var=flash&val=0');
            break                                                          // Включить свет- K
    }
});

function toggleControlMode() {
    var modeButton = document.getElementById('toggleMode');
    var modeLabel = document.getElementById('modeLabel');
    
    // Определяем текущий режим по тексту кнопки
    var currentMode = (modeButton.innerHTML === 'Автономный режим') ? 0 : 1;
    var newMode = currentMode ? 0 : 1;
    
    // Отправляем запрос на сервер
    fetch(document.location.origin + '/control?var=controlMode&val=' + newMode)
        .then(response => {
            if (response.ok) {
                // Обновляем интерфейс
                if (newMode) {
                    modeButton.innerHTML = 'Ручной режим';
                    modeLabel.innerHTML = 'Режим: Автономный';
                    modeButton.style.background = '#4CAF50'; // Зеленый
                    modeButton.style.color = 'white';
                } else {
                    modeButton.innerHTML = 'Автономный режим';
                    modeLabel.innerHTML = 'Режим: Ручной';
                    modeButton.style.background = '#f44336'; // Красный
                    modeButton.style.color = 'white';
                }
            }
        })
        .catch(error => console.error('Ошибка:', error));
}

// Инициализация при загрузке страницы
document.addEventListener('DOMContentLoaded', function() {
    // Установить начальный цвет кнопки
    var modeButton = document.getElementById('toggleMode');
    modeButton.style.background = '#f44336';
    modeButton.style.color = 'white';
});

function toggleStart() {
    var startButton = document.getElementById('startButton');
    var startStatus = document.getElementById('startStatus');
    
    // Определяем текущее состояние по тексту кнопки
    var isActive = (startButton.innerHTML === 'СТОП');
    var newState = isActive ? 0 : 1;
    
    // Отправляем команду на сервер
    fetch(document.location.origin + '/control?var=new_start&val=' + newState)
        .then(response => {
            if (response.ok) {
                // Обновляем интерфейс
                if (newState) {
                    startButton.innerHTML = 'СТОП';
                    startButton.style.background = '#f44336'; // Красный
                    startStatus.innerHTML = 'Старт: АКТИВЕН';
                    startStatus.style.color = 'green';
                    startStatus.style.fontWeight = 'bold';
                } else {
                    startButton.innerHTML = 'СТАРТ';
                    startButton.style.background = '#4CAF50'; // Зеленый
                    startStatus.innerHTML = 'Старт: ВЫКЛ';
                    startStatus.style.color = 'black';
                    startStatus.style.fontWeight = 'normal';
                }
                startButton.style.color = 'white';
            }
        })
        .catch(error => console.error('Ошибка:', error));
}

// Инициализация при загрузке
document.addEventListener('DOMContentLoaded', function() {
    var startButton = document.getElementById('startButton');
    if (startButton) {
        startButton.style.background = '#4CAF50'; // Зеленый по умолчанию
        startButton.style.color = 'white';
        startButton.style.fontWeight = 'bold';
    }
});

// Функция для обновления дистанции при изменении поля
function updateDistance() {
    var distanceInput = document.getElementById('distanceInput');
    var distance = parseFloat(distanceInput.value);
    
    // Валидация
    if (isNaN(distance) || distance < 0) {
        distanceInput.value = "0.0";
        distance = 0;
    }
    
    // Можно сразу отправлять или только по кнопке "Применить"
    // Для немедленной отправки раскомментируйте:
    // sendDistance(distance);
}

// Функция отправки значения на сервер
function sendDistance(distance) {
    fetch(document.location.origin + '/control?var=set_distance&val=' + distance)
        .then(response => {
            if (response.ok) {
                console.log('Дистанция установлена: ' + distance + ' м');
                // Визуальное подтверждение
                showDistanceNotification(distance);
            }
        })
        .catch(error => console.error('Ошибка:', error));
}

// Функция по кнопке "Применить"
function applyDistance() {
    var distanceInput = document.getElementById('distanceInput');
    var distance = parseFloat(distanceInput.value);
    
    if (isNaN(distance)) {
        alert("Введите числовое значение!");
        distanceInput.focus();
        return;
    }
    
    if (distance < 0) {
        alert("Дистанция не может быть отрицательной!");
        distanceInput.value = "0.0";
        distanceInput.focus();
        return;
    }
    
    sendDistance(distance);
}

// Функция для включения/выключения сохранения фото
function toggleSavePhotos() {
    var saveButton = document.getElementById('saveButton');
    var saveStatus = document.getElementById('saveStatus');
    
    // Определяем текущее состояние
    var isActive = (saveButton.innerHTML === 'СТОП СОХРАНЕНИЕ');
    var newState = isActive ? 0 : 1;
    
    // Отправляем команду на сервер
    fetch(document.location.origin + '/control?var=save_photos&val=' + newState)
        .then(response => {
            if (response.ok) {
                // Обновляем интерфейс
                if (newState) {
                    saveButton.innerHTML = 'СТОП СОХРАНЕНИЕ';
                    saveButton.style.background = '#f44336'; // Красный
                    saveStatus.innerHTML = 'Сохранение: АКТИВНО';
                    saveStatus.style.color = 'green';
                    saveStatus.style.fontWeight = 'bold';
                    
                    // Показываем уведомление
                    showSaveNotification('Сохранение фото начато (200 мс)');
                } else {
                    saveButton.innerHTML = 'СТАРТ СОХРАНЕНИЕ';
                    saveButton.style.background = '#2196F3'; // Синий
                    saveStatus.innerHTML = 'Сохранение: ВЫКЛ';
                    saveStatus.style.color = 'black';
                    saveStatus.style.fontWeight = 'normal';
                    
                    showSaveNotification('Сохранение фото остановлено');
                }
                saveButton.style.color = 'white';
            }
        })
        .catch(error => console.error('Ошибка:', error));
}

// Функция для отображения уведомления
function showSaveNotification(message) {
    // Создаем временное уведомление
    var notification = document.createElement('div');
    notification.innerHTML = message;
    notification.style.cssText = `
        position: fixed;
        top: 80px;
        right: 20px;
        background: #2196F3;
        color: white;
        padding: 15px;
        border-radius: 5px;
        z-index: 1000;
        box-shadow: 0 4px 8px rgba(0,0,0,0.2);
        animation: fadeInOut 3s ease-in-out;
    `;
    
    // Добавляем CSS анимацию если её нет
    if (!document.querySelector('#saveNotificationStyle')) {
        var style = document.createElement('style');
        style.id = 'saveNotificationStyle';
        style.innerHTML = `
            @keyframes fadeInOut {
                0% { opacity: 0; transform: translateY(-20px); }
                20% { opacity: 1; transform: translateY(0); }
                80% { opacity: 1; transform: translateY(0); }
                100% { opacity: 0; transform: translateY(-20px); }
            }
        `;
        document.head.appendChild(style);
    }
    
    document.body.appendChild(notification);
    
    // Удаляем через 3 секунды
    setTimeout(function() {
        notification.remove();
    }, 3000);
}

// Инициализация при загрузке
document.addEventListener('DOMContentLoaded', function() {
    var saveButton = document.getElementById('saveButton');
    if (saveButton) {
        saveButton.style.background = '#2196F3';
        saveButton.style.color = 'white';
        saveButton.style.fontWeight = 'bold';
    }
    
    // Поддержка горячей клавиши для сохранения (например, 'R')
    document.addEventListener('keypress', function(event) {
        if (event.charCode === 114 || event.charCode === 82) { // 'r' или 'R'
            toggleSavePhotos();
        }
    });
});


// Визуальное уведомление
function showDistanceNotification(distance) {
    // Создаем временное уведомление
    var notification = document.createElement('div');
    notification.innerHTML = 'Дистанция установлена: ' + distance.toFixed(2) + ' м';
    notification.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        background: #4CAF50;
        color: white;
        padding: 15px;
        border-radius: 5px;
        z-index: 1000;
        box-shadow: 0 4px 8px rgba(0,0,0,0.2);
        animation: fadeInOut 3s ease-in-out;
    `;
    
    document.body.appendChild(notification);
    
    // Удаляем через 3 секунды
    setTimeout(function() {
        notification.remove();
    }, 3000);
}

// Инициализация при загрузке
document.addEventListener('DOMContentLoaded', function() {
    var distanceInput = document.getElementById('distanceInput');
    if (distanceInput) {
        // Устанавливаем начальное значение
        distanceInput.value = "0.0";
        
        // Можно добавить валидацию на лету
        distanceInput.addEventListener('input', function() {
            var value = parseFloat(this.value);
            if (isNaN(value) || value < 0) {
                this.style.borderColor = 'red';
            } else {
                this.style.borderColor = '#4CAF50';
            }
        });
    }
});
    </script>
  </body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t status_uri = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t cmd_uri = {
        .uri       = "/control",
        .method    = HTTP_GET,
        .handler   = cmd_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
//        .handler   = capture_handler,
        .user_ctx  = NULL
    };

   httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };
    
    Serial.printf("Запуск веб-сервера на порту: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    Serial.printf("Запуск потокового сервера на порту: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
