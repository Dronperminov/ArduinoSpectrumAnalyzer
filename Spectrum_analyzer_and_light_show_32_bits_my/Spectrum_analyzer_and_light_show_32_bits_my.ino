#include "DueADC.h"
#include "Due.h"
#include "Arduino.h"

// частота выборок, определённая экспериментальным путём
#define SAMPLE_FREQ 48166

// накладывать ли окно на входные данные перед преобразованием Фурье
#define APPLY_WINDOW

// основане логарифма выходной шкалы частот спектроанализатора
// OUTPUT_LOG_BASE = (N_WAVE / 2) ^ (1 / N_OUTPUT), где N_OUTPUT - кол-во выходных полос
// для N_OUTPUT = 50, OUTPUT_LOG_BASE = 1.105
// для N_OUTPUT = 3, OUTPUT_LOG_BASE = 16
#define OUTPUT_LOG_BASE 1.105

// основание логарифма в представлении с фиксированной точкой
#define OUTPUT_LOG_BASE_FIX ((int16_t) ((int32_t) (INT16_MAX + 1) * (1.0 - 1.0 / OUTPUT_LOG_BASE)))

 // кол-во пропущенных циклов при калибровке
#define CALIBRATION_DUMMY_LOOPS 30
 // кол-во используемых циклов в вычислении среднего  при калибровке
#define CALIBRATION_WORK_LOOPS 30

// чувствительность к перегрузке по уровню
//#define OVERLOAD_TOLERANCE 200 
 // чувствительность к перегрузке по кол-ву выборок
#define OVERLOAD_DETECTION_THRESHOLD 8
// минимальное кол-во уровней, при котором включается алгоритм обнаружения перегрузки [1; NUM_LEVEL_COUNTERS]
#define MIN_OVERLOAD_DETECTION_LEVEL 128

#define NUM_LEVEL_COUNTERS 256

#if (UINT16_MAX + 1) % NUM_LEVEL_COUNTERS != 0
#  error 65536 должно нацело делиться на NUM_LEVEL_COUNTERS 
#endif

 // коэффициент для перевода в ШИМ сигнал
#define PWM_CHANNEL_RATIO (1 << 11)

 // номер пина низкочастотного канала
#define PWM_PIN_LOW 11
// номер пина среднечастотного канала
#define PWM_PIN_MID 12
// номер пина высокочастотного канала
#define PWM_PIN_HIGH 2 

#define OVERLOAD_LED 13

#ifdef DEBUG_COUNT
int count = 0;
#endif

int pwm_pins[3] = {PWM_PIN_LOW, PWM_PIN_MID, PWM_PIN_HIGH}; // массив с номерами ШИМ каналов ардуино

int16_t dc_level; // уровень постоянной составляющей

int16_t im[N_WAVE]; // массив im

// структура, определяющая диапазон частотных полос
typedef struct {
  int start_band;
  int stop_band;
} band_t;

typedef struct {
  int16_t band_count;
  int16_t level;
} band_data_t;

// массив с диапазонами частотных полос
const band_t band_defs[3] = {
  { start_band : 1, stop_band : 4, }, // 0 - 188 Гц
  { start_band : 5, stop_band : 18, }, // 188 - 846 Гц
  { start_band : 19, stop_band : 511, }, // 1000 - 24000 Гц
};

void word_to_bytes(uint16_t val, byte& high, byte& low) {
  high = (val >> 8) & 0xff;
  low = val & 0xff;
}

void write_word(uint16_t value) {
  byte high, low;
  word_to_bytes(value, high, low);
  Serial.write(high);
  Serial.write(low);  
}

void write_data(const band_data_t* data, int data_size) {
  for (int i = 0; i < data_size; i++) {
    write_word(data[i].band_count);
    write_word(data[i].level);
  }
}

void write_packet(const band_data_t* data, int data_size, uint16_t max_level, uint16_t peak_index, uint16_t log_base, uint16_t sample_freq, uint16_t n_wave) {
  write_word(0xffff);
  write_word(max_level);
  write_word(peak_index);
  write_word(log_base);
  write_word(sample_freq);
  write_word(n_wave);
  write_word(data_size);  
  write_data(data, data_size);  
  write_word(0x0000);
}

void reverse(band_data_t* data, int size) {
  for (int i = 0, j = size - 1; i < j; i++, j--) {
    band_data_t tmp = data[i];
    data[i] = data[j];
    data[j] = tmp;
  }
}

void setup() {
  Serial.begin(115200); // Устанавливает скорость порта на 115200 Бод
  
  ADC_Init();
  
  // калибровка  
  // заполняем и чистим буфер информации - заряжаем конденсатор АЦП для последующего вычисления постоянной составляюей
  for(int i = 0; i < CALIBRATION_DUMMY_LOOPS; i++) {
    get_adc_buffer();
    free_adc_buffer();
  }
  
  // вычисляем постоянную составляющую сигнала
  int32_t sum = 0;
  
  for(int i = 0; i < CALIBRATION_WORK_LOOPS; i++) {
    int16_t *data = get_adc_buffer();
    for(int j = 0; j < BUFFER_SIZE; j++) { 
      sum += data[j];
    }
    
    free_adc_buffer();
  }
  
  dc_level = sum / (CALIBRATION_WORK_LOOPS * BUFFER_SIZE);
  
  // конфигурируем порты ШИМ на выход и подтягиваем их к 0
  for(int i = 0; i < 3; i++) {
    pinMode(pwm_pins[i], OUTPUT);
    digitalWrite(pwm_pins[i], 0);
  }
  
  pinMode(OVERLOAD_LED, OUTPUT);
  digitalWrite(OVERLOAD_LED, 0);
}

void loop() { 
  int16_t *data = get_adc_buffer(); // получаем данные преобазованич от АЦП в массив data
  int16_t channels[3]; // переменная, хранящая амплитуды сигналов
  
  // счётчики уровней сигнала
  int16_t level_counters[NUM_LEVEL_COUNTERS];
  
  // устанавливаем все счётчики в ноль
  memset(level_counters, 0, sizeof(level_counters));
  
  for (int i = 0; i < BUFFER_SIZE; i++) {  
    data[i] = ((data[i] - dc_level) << 4);
    im[i] = 0;
    int16_t level_counter_index = data[i] / ((UINT16_MAX + 1) / NUM_LEVEL_COUNTERS) + NUM_LEVEL_COUNTERS / 2;
    level_counters[level_counter_index]++;    
  }
  
  int min_level_counter_index;
  int max_level_counter_index;
  
  for (min_level_counter_index = 0; min_level_counter_index < NUM_LEVEL_COUNTERS && level_counters[min_level_counter_index] == 0; min_level_counter_index++)
    ;  
  
  for (max_level_counter_index = NUM_LEVEL_COUNTERS - 1; max_level_counter_index > min_level_counter_index && level_counters[max_level_counter_index] == 0; max_level_counter_index--)
    ;
  
  int is_overload = 0;
  
  if (max_level_counter_index - min_level_counter_index > MIN_OVERLOAD_DETECTION_LEVEL) {
    int16_t level_counters_max = 0;
  
    for (int i = 0; i < 4; i++) {
      int max_index = max_array_element(level_counters, NUM_LEVEL_COUNTERS);
      level_counters_max += level_counters[max_index];
      level_counters[max_index] = 0;
    }
        
    if (BUFFER_SIZE / level_counters_max < OVERLOAD_DETECTION_THRESHOLD) {
      is_overload = 1;
    }
  }
    
  //Перегрузка      
  digitalWrite(OVERLOAD_LED, is_overload);

#ifdef APPLY_WINDOW  
  apply_blackman_window(data, LOG2_N_WAVE); // накладываем окно Блэкмана
#endif

  fix_fft(data, im, LOG2_N_WAVE, 0); // выполняем преобразование Фурье

  for(int i = 0; i < 3; i++) {
    channels[i] = 0;
    for (int j = band_defs[i].start_band; j <= band_defs[i].stop_band; j++) {
      channels[i] += FIX_MPY(data[j], data[j]) + FIX_MPY(im[j], im[j]);
    }
   
    channels[i] = sqrt_i(channels[i]);
   
    analogWrite(pwm_pins[i], signal_to_pwm(channels[i], PWM_CHANNEL_RATIO));
  }
  
  for (int i = 1; i < N_WAVE / 2; i++) {
    data[i] = FIX_MPY(data[i], data[i]) + FIX_MPY(im[i], im[i]);
  }
  
  int16_t peak_index = 0, peak_value = 1;
  
  for (int i = 1; i < N_WAVE / 2; i++) {
    if (data[i] > peak_value) {
      peak_index = i;
      peak_value = data[i];
    }
  }
  
  band_data_t out[N_WAVE];
  int data_index = N_WAVE / 2 - 1;
  int out_index = 0;
  int16_t data_size = N_WAVE / 2 - 1;
  
  while (data_index > 0) {
    int16_t count = FIX_MPY(data_size, OUTPUT_LOG_BASE_FIX);
    
    if (count == 0) {
      count = 1;
    }
    
    out[out_index].band_count = count;
    
    int16_t next_data_size = data_size - count;
    int16_t sum = 0;
    
    while (data_index > 0 && count > 0) {
       sum += data[data_index--];
       count--;
    }
    
    out[out_index++].level = sqrt_i(sum);
    
    data_size = next_data_size; 
  }
  
  reverse(out, out_index);
  
  write_packet(out, out_index, INT16_MAX / 10, peak_index, OUTPUT_LOG_BASE_FIX, SAMPLE_FREQ, N_WAVE);
  
  free_adc_buffer();
}

