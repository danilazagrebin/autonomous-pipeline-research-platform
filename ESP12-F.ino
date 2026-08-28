const int ledPin = 15;

  // PWM (ШИМ)
const int lPwm = 12;
const int RPwm = 13;
  // Direction
const int IN1 = 5;
const int IN2 = 4;
const int IN3 = 0;
const int IN4 = 2;

const double wheeldiametr = 0.068;      // диаметр колеса в метрах
const int tachometer_out = 14; // пин, на который приходит сигнал с датчика скорости
volatile int counter = 0;  // переменная-счётчик

int speed = 90;

const int turn_speed = 80;

int command = 0;  // 0 - стоп, 1 - вперед, 2 - назад, 3 - влево поворот, 4 - вправо поворот
int new_command;  // команда движения, полученная с приложения

// Инициализация для автономного режима
// Режим автоматический или ручной (вводится с приложения)
  bool automatic = false;
  bool automatic_temp = false;

  bool new_start = false; // Команда старта, полученная с приложения (true - значит запущено и работает)
  bool start = false;

  // Таймер для инициализации после включения
  bool start_procedure_completed = false; // Выполнена ли процедура старта
  bool init_time_stamp_is_ready = false; // Сделана ли первая отметка времени
  unsigned long  start_init_timer; // Метка времени для начала остчета

  // Переменные для определения прогресса автономного режима
  bool finish = false;
  bool halfpath_completed = false;
  bool turn_completed = false;

  // Пройденная текущая дистанция в метрах
  volatile double distance = 0;
  // Дистанция в метрах необходимая для поворота
  float set_turn_distance = 0.27;
  // Заданная с приложения дистанция, которую надо проехать вглубь трубопровода 
  float set_distance = 1; // в метрах

  // Таймер для смены команды
  unsigned long start_time_change_command;  // Метка времени начала смены команды
  bool time_stamp_is_ready = false;
  

// функция-прерывание для вычисления пройденного расстояния подсчета импульсов с датчика скорости
IRAM_ATTR void myIsr() {
  counter++;
  distance = 3.1415 * wheeldiametr / 20 * counter; // в метрах
}

void setup() {
  pinMode(15, OUTPUT);

  // PWM (ШИМ)
  pinMode(lPwm, OUTPUT);
  pinMode(RPwm, OUTPUT);
  
  // Direction  
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // Оптический датчик прерываний (измерение расстояния)
  attachInterrupt(tachometer_out, myIsr, RISING);  // GPIO14
  pinMode(tachometer_out, INPUT);

  // Связь с контроллером верхнего уровня
  Serial.begin(115200); //RX-GPIO3, TX-GPIO1
}

void control(int command) 
{
  if (command == 0)  // стоп
  {
    analogWrite(lPwm, 0); // от 0 до 255
    analogWrite(RPwm, 0);

    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);    
  }
  if (command == 1) // Вперед
  {
    analogWrite(lPwm, speed);
    analogWrite(RPwm, speed);

    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);  
  }
  if(command == 2) // Назад
  {
    analogWrite(lPwm, speed);
    analogWrite(RPwm, speed);

    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH);

  }
  if(command == 3) // Поворот влево
  {
    analogWrite(lPwm, turn_speed);
    analogWrite(RPwm, turn_speed);

    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, HIGH); 

  }
  if(command == 4) // Поворот вправо
  {
    analogWrite(lPwm, turn_speed);
    analogWrite(RPwm, turn_speed);

    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW); 
  }
}

void loop() {

if (Serial.available() > 0) 
{

  // Читаем строку
  String data = Serial.readStringUntil('\n');
  data.trim();
  
  // Разбиваем строку на части по запятым
  int firstComma = data.indexOf(',');
  int secondComma = data.indexOf(',', firstComma + 1);
  int thirdComma = data.indexOf(',', secondComma + 1);
  int fourthComma = data.indexOf(',', thirdComma + 1);
  int speed_temp;

  // Извлекаем значения
  if (firstComma != -1 && secondComma != -1 && thirdComma != -1 && fourthComma != -1) 
  {
    speed_temp = data.substring(0, firstComma).toInt();
    if (speed_temp < 80)
    {
      speed = 0;
    }
    else if (speed > 100)
    {
      speed = 100;
    }
    else
    {
      speed = speed_temp;
    }  
    automatic = data.substring(firstComma + 1, secondComma).toInt() == 1;
    
    if (automatic == false) // Принимаем команды управления только в ручном режиме
    {
      new_command = data.substring(secondComma + 1, thirdComma).toInt();
    }
    else    // Принимаем команды старта и установки дистанции только автономном режиме
    {
      new_start = (data.substring(thirdComma + 1, fourthComma).toInt() == 1);
      set_distance = data.substring(fourthComma + 1).toFloat();
    }
  }
}  



if (start_procedure_completed == true)
{
  
  if ((automatic == true)&&(automatic_temp == false))
  {
    distance = 0;
    counter = 0;  
  }

  automatic_temp = automatic;

  if (automatic == true) // автономный режим
  {
    if ((start != new_start) && (new_start == true))
    {
      // инициализация при запуске процедры обследования
      finish = false;
      halfpath_completed = false;
      turn_completed = false;

      distance = 0;
      command = 0;
      new_command = 0;
    }
    
    start = new_start;

    if ((start == true) && (finish == false) && (start_procedure_completed == true)) //оператор нажал старт
    {
      if (halfpath_completed == false)
      {
        new_command = 1;     //вперёд
        if (distance > set_distance)
        {
          halfpath_completed = true;
          distance = 0;
          counter = 0;
          command = 0;
        }
      }
      // else if((halfpath_completed == true) && (turn_completed == false))
      // {
      //   new_command = 3;     //поворот влево
      //   if (abs(distance) > set_turn_distance)
      //   {
      //     turn_completed = true;
      //     distance = 0;
      //     counter = 0;
      //     command = 0;
      //   }
      // }
      else if((halfpath_completed == true))// && (turn_completed == true))
      {
        new_command = 2;     //назад
        if (distance > set_distance)
        {
          finish = true;
          distance = 0;
          counter = 0;
          command = 0;
        }
      }

      // Создаем строку с данными
      String dataToSend = (finish ? "1" : "0");
      // Отправляем
      Serial.println(dataToSend);

      // Смена направления движения через останов
      if (new_command != command)
      { 
        if (time_stamp_is_ready == false) // засекаем время смены команды
        {
          command = 0;                         // стоп
          start_time_change_command = millis(); 
          time_stamp_is_ready = true;
        }

        if (millis() - start_time_change_command > 1000)  // время останова прошло (1 секунда), выполняем новую команду
        {
          command = new_command;
          time_stamp_is_ready = false;
        }
      }

      control(command);
    }
  }
  else                   // ручной режим
  {
    // Смена направления движения через останов
      if (new_command != command)
      { 
        if (time_stamp_is_ready == false) // засекаем время смены команды
        {
          command = 0;                         // стоп
          start_time_change_command = millis(); 
          time_stamp_is_ready = true;
        }

        if (millis() - start_time_change_command > 100)  // время останова прошло (0.1 секунда), выполняем новую команду
        {
          command = new_command;
          time_stamp_is_ready = false;
        }
      }
        
      control(command);
  }
}
else //Выполняем паузу после включения питания
{
  // Таймер на 5 секунд, чтобы поставить платформу ровно на место старта
  if (init_time_stamp_is_ready == false)
  {
    start_init_timer = millis(); 
    init_time_stamp_is_ready = true;
  }
  if ((millis() - start_init_timer) > 5000)  // время инизиализации прошло (5 секунд), выполняем первую команду
  {
    start_procedure_completed = true;
    init_time_stamp_is_ready = false;
  }
}  
}
