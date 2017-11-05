#include "Arduino.h"

//#define DEBUG

#ifdef DEBUG

// печать исходных данных
//#define DEBUG_PRINT_DATA

//печать min_level_counter_index и max_level_counter_index
//#define DEBUG_PRINT_MIN_MAX_COUNTER_INDEX

//печать массива счётчиков
//#define DEBUG_PRINT_LEVEL_COUNTERS

//печать состояния флага перегрузки
//#define DEBUG_PRINT_OVERLOAD_FLAG

// печать data после наложения окна Блэкмана
//#define DEBUG_PRINT_BLACKMAN_DATA

// печать данных data и im после выполнения fix_fft
//#define DEBUG_PRINT_FFT_DATA

//печать точности fft
//#define DEBUG_PRINT_FFT_PROBABILITY

// печать уровня постоянной составляющей
//#define DEBUG_PRINT_DC_LEVEL

// печать состояний каналов
//#define DEBUG_PRINT_CHANNELS

//печать значений ШИМ каналов
//#define DEBUG_PRINT_PWM_SIGNALS

// печать времени, затрачиваемого на расчёты (время выполнения loop)
//#define DEBUG_PRINT_LOOP_TIME

//
#define DEBUG_PRINT_TIME

// кол-во выполняемых итераций loop
#define COUNT 4

// выполнение loop COUNT итераций
#define DEBUG_COUNT

#endif

// full length of Sinewave[]
#define N_WAVE  1024
#define LOG2_N_WAVE 10   // log2(N_WAVE)  

// частота выборок, определённая экспериментальным путём
#define SAMPLE_FREQ 48166

// размер одного буфера
#define BUFFER_SIZE 1024 
// кол-во буферов для DMA
#define BUFFER_COUNT 4 

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
#define OVERLOAD_DETECTION_THRESHOLD 5
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

volatile int i_buf_index, o_buf_index; // индексы буферов

int pwm_pins[3] = {PWM_PIN_LOW, PWM_PIN_MID, PWM_PIN_HIGH}; // массив с номерами ШИМ каналов ардуино

int16_t dc_level; // уровень постоянной составляющей

int16_t buffer[BUFFER_COUNT][BUFFER_SIZE]; // буфер для внутренней памяти
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
  { start_band : 1, stop_band : 5, }, // 0 - 200 Гц
  { start_band : 6, stop_band : 20, }, // 200 - 800 Гц
  { start_band : 21, stop_band : 511, }, // 1000 - 20000 Гц
};

// Since we only use 3/4 of N_WAVE, we define only this many samples, in order to conserve data space.
const int16_t Sinewave[N_WAVE-N_WAVE/4] = {
 0,	202,	403,	604,	805,	1006,	1207,	1408,	
1608,	1809,	2010,	2211,	2411,	2612,	2812,	3012,	
3212,	3412,	3612,	3812,	4012,	4211,	4410,	4610,	
4809,	5007,	5206,	5404,	5603,	5801,	5998,	6196,	
6393,	6590,	6787,	6984,	7180,	7376,	7572,	7767,	
7962,	8157,	8352,	8546,	8740,	8934,	9127,	9320,	
9513,	9705,	9897,	10088,	10279,	10470,	10660,	10850,	
11040,	11229,	11417,	11606,	11794,	11981,	12168,	12354,	
12540,	12726,	12911,	13095,	13279,	13463,	13646,	13829,	
14011,	14192,	14373,	14553,	14733,	14913,	15091,	15270,	
15447,	15624,	15801,	15977,	16152,	16326,	16500,	16674,	
16847,	17019,	17190,	17361,	17531,	17701,	17870,	18038,	
18205,	18372,	18538,	18704,	18869,	19033,	19196,	19359,	
19520,	19682,	19842,	20002,	20160,	20319,	20476,	20632,	
20788,	20943,	21098,	21251,	21404,	21556,	21707,	21857,	
22006,	22155,	22302,	22449,	22595,	22741,	22885,	23028,	
23171,	23313,	23454,	23594,	23733,	23871,	24008,	24145,	
24280,	24415,	24548,	24681,	24813,	24944,	25074,	25202,	
25331,	25458,	25584,	25709,	25833,	25956,	26078,	26200,	
26320,	26439,	26558,	26675,	26791,	26906,	27021,	27134,	
27246,	27357,	27467,	27577,	27685,	27792,	27898,	28003,	
28107,	28209,	28311,	28412,	28512,	28610,	28708,	28804,	
28899,	28994,	29087,	29179,	29270,	29360,	29448,	29536,	
29622,	29708,	29792,	29875,	29957,	30038,	30118,	30197,	
30274,	30351,	30426,	30500,	30573,	30645,	30715,	30785,	
30853,	30920,	30986,	31051,	31115,	31177,	31238,	31299,	
31358,	31415,	31472,	31527,	31582,	31635,	31686,	31737,	
31786,	31835,	31882,	31928,	31972,	32016,	32058,	32099,	
32139,	32177,	32215,	32251,	32286,	32320,	32352,	32384,	
32414,	32443,	32470,	32497,	32522,	32546,	32569,	32590,	
32611,	32630,	32648,	32664,	32680,	32694,	32707,	32719,	
32729,	32738,	32746,	32753,	32759,	32763,	32766,	32767,	

32767,	32767,	32766,	32763,	32759,	32753,	32746,	32738,	
32729,	32719,	32707,	32694,	32680,	32664,	32648,	32630,	
32611,	32590,	32569,	32546,	32522,	32497,	32470,	32443,	
32414,	32384,	32352,	32320,	32286,	32251,	32215,	32177,	
32139,	32099,	32058,	32016,	31972,	31928,	31882,	31835,	
31786,	31737,	31686,	31635,	31582,	31527,	31472,	31415,	
31358,	31299,	31238,	31177,	31115,	31051,	30986,	30920,	
30853,	30785,	30715,	30645,	30573,	30500,	30426,	30351,	
30274,	30197,	30118,	30038,	29957,	29875,	29792,	29708,	
29622,	29536,	29448,	29360,	29270,	29179,	29087,	28994,	
28899,	28804,	28708,	28610,	28512,	28412,	28311,	28209,	
28107,	28003,	27898,	27792,	27685,	27577,	27467,	27357,	
27246,	27134,	27021,	26906,	26791,	26675,	26558,	26439,	
26320,	26200,	26078,	25956,	25833,	25709,	25584,	25458,	
25331,	25202,	25074,	24944,	24813,	24681,	24548,	24415,	
24280,	24145,	24008,	23871,	23733,	23594,	23454,	23313,	
23171,	23028,	22885,	22741,	22595,	22449,	22302,	22155,	
22006,	21857,	21707,	21556,	21404,	21251,	21098,	20943,	
20788,	20632,	20476,	20319,	20160,	20002,	19842,	19682,	
19520,	19359,	19196,	19033,	18869,	18704,	18538,	18372,	
18205,	18038,	17870,	17701,	17531,	17361,	17190,	17019,	
16847,	16674,	16500,	16326,	16152,	15977,	15801,	15624,	
15447,	15270,	15091,	14913,	14733,	14553,	14373,	14192,	
14011,	13829,	13646,	13463,	13279,	13095,	12911,	12726,	
12540,	12354,	12168,	11981,	11794,	11606,	11417,	11229,	
11040,	10850,	10660,	10470,	10279,	10088,	9897,	9705,	
9513,	9320,	9127,	8934,	8740,	8546,	8352,	8157,	
7962,	7767,	7572,	7376,	7180,	6984,	6787,	6590,	
6393,	6196,	5998,	5801,	5603,	5404,	5206,	5007,	
4809,	4610,	4410,	4211,	4012,	3812,	3612,	3412,	
3212,	3012,	2812,	2612,	2411,	2211,	2010,	1809,	
1608,	1408,	1207,	1006,	805,	604,	403,	202,
	
1,	-201,	-402,	-603,	-804,	-1005,	-1206,	-1407,	
-1607,	-1808,	-2009,	-2210,	-2410,	-2611,	-2811,	-3011,	
-3211,	-3411,	-3611,	-3811,	-4011,	-4210,	-4409,	-4609,	
-4808,	-5006,	-5205,	-5403,	-5602,	-5800,	-5997,	-6195,	
-6392,	-6589,	-6786,	-6983,	-7179,	-7375,	-7571,	-7766,	
-7961,	-8156,	-8351,	-8545,	-8739,	-8933,	-9126,	-9319,	
-9512,	-9704,	-9896,	-10087,	-10278,	-10469,	-10659,	-10849,	
-11039,	-11228,	-11416,	-11605,	-11793,	-11980,	-12167,	-12353,	
-12539,	-12725,	-12910,	-13094,	-13278,	-13462,	-13645,	-13828,	
-14010,	-14191,	-14372,	-14552,	-14732,	-14912,	-15090,	-15269,	
-15446,	-15623,	-15800,	-15976,	-16151,	-16325,	-16499,	-16673,	
-16846,	-17018,	-17189,	-17360,	-17530,	-17700,	-17869,	-18037,	
-18204,	-18371,	-18537,	-18703,	-18868,	-19032,	-19195,	-19358,	
-19519,	-19681,	-19841,	-20001,	-20159,	-20318,	-20475,	-20631,	
-20787,	-20942,	-21097,	-21250,	-21403,	-21555,	-21706,	-21856,	
-22005,	-22154,	-22301,	-22448,	-22594,	-22740,	-22884,	-23027,	
-23170,	-23312,	-23453,	-23593,	-23732,	-23870,	-24007,	-24144,	
-24279,	-24414,	-24547,	-24680,	-24812,	-24943,	-25073,	-25201,	
-25330,	-25457,	-25583,	-25708,	-25832,	-25955,	-26077,	-26199,	
-26319,	-26438,	-26557,	-26674,	-26790,	-26905,	-27020,	-27133,	
-27245,	-27356,	-27466,	-27576,	-27684,	-27791,	-27897,	-28002,	
-28106,	-28208,	-28310,	-28411,	-28511,	-28609,	-28707,	-28803,	
-28898,	-28993,	-29086,	-29178,	-29269,	-29359,	-29447,	-29535,	
-29621,	-29707,	-29791,	-29874,	-29956,	-30037,	-30117,	-30196,	
-30273,	-30350,	-30425,	-30499,	-30572,	-30644,	-30714,	-30784,	
-30852,	-30919,	-30985,	-31050,	-31114,	-31176,	-31237,	-31298,	
-31357,	-31414,	-31471,	-31526,	-31581,	-31634,	-31685,	-31736,	
-31785,	-31834,	-31881,	-31927,	-31971,	-32015,	-32057,	-32098,	
-32138,	-32176,	-32214,	-32250,	-32285,	-32319,	-32351,	-32383,	
-32413,	-32442,	-32469,	-32496,	-32521,	-32545,	-32568,	-32589,	
-32610,	-32629,	-32647,	-32663,	-32679,	-32693,	-32706,	-32718,	
-32728,	-32737,	-32745,	-32752,	-32758,	-32762,	-32765,	-32767,	

/* -32768,	-32767,	-32765,	-32762,	-32758,	-32752,	-32745,	-32737,	
-32728,	-32718,	-32706,	-32693,	-32679,	-32663,	-32647,	-32629,	
-32610,	-32589,	-32568,	-32545,	-32521,	-32496,	-32469,	-32442,	
-32413,	-32383,	-32351,	-32319,	-32285,	-32250,	-32214,	-32176,	
-32138,	-32098,	-32057,	-32015,	-31971,	-31927,	-31881,	-31834,	
-31785,	-31736,	-31685,	-31634,	-31581,	-31526,	-31471,	-31414,	
-31357,	-31298,	-31237,	-31176,	-31114,	-31050,	-30985,	-30919,	
-30852,	-30784,	-30714,	-30644,	-30572,	-30499,	-30425,	-30350,	
-30273,	-30196,	-30117,	-30037,	-29956,	-29874,	-29791,	-29707,	
-29621,	-29535,	-29447,	-29359,	-29269,	-29178,	-29086,	-28993,	
-28898,	-28803,	-28707,	-28609,	-28511,	-28411,	-28310,	-28208,	
-28106,	-28002,	-27897,	-27791,	-27684,	-27576,	-27466,	-27356,	
-27245,	-27133,	-27020,	-26905,	-26790,	-26674,	-26557,	-26438,	
-26319,	-26199,	-26077,	-25955,	-25832,	-25708,	-25583,	-25457,	
-25330,	-25201,	-25073,	-24943,	-24812,	-24680,	-24547,	-24414,	
-24279,	-24144,	-24007,	-23870,	-23732,	-23593,	-23453,	-23312,	
-23170,	-23027,	-22884,	-22740,	-22594,	-22448,	-22301,	-22154,	
-22005,	-21856,	-21706,	-21555,	-21403,	-21250,	-21097,	-20942,	
-20787,	-20631,	-20475,	-20318,	-20159,	-20001,	-19841,	-19681,	
-19519,	-19358,	-19195,	-19032,	-18868,	-18703,	-18537,	-18371,	
-18204,	-18037,	-17869,	-17700,	-17530,	-17360,	-17189,	-17018,	
-16846,	-16673,	-16499,	-16325,	-16151,	-15976,	-15800,	-15623,	
-15446,	-15269,	-15090,	-14912,	-14732,	-14552,	-14372,	-14191,	
-14010,	-13828,	-13645,	-13462,	-13278,	-13094,	-12910,	-12725,	
-12539,	-12353,	-12167,	-11980,	-11793,	-11605,	-11416,	-11228,	
-11039,	-10849,	-10659,	-10469,	-10278,	-10087,	-9896,	-9704,	
-9512,	-9319,	-9126,	-8933,	-8739,	-8545,	-8351,	-8156,	
-7961,	-7766,	-7571,	-7375,	-7179,	-6983,	-6786,	-6589,	
-6392,	-6195,	-5997,	-5800,	-5602,	-5403,	-5205,	-5006,	
-4808,	-4609,	-4409,	-4210,	-4011,	-3811,	-3611,	-3411,	
-3211,	-3011,	-2811,	-2611,	-2410,	-2210,	-2009,	-1808,	
-1607,	-1407,	-1206,	-1005,	-804,	-603,	-402,	-201,*/

};
 
/*
  FIX_MPY() - fixed-point multiplication & scaling.
  Substitute inline assembly for hardware-specific
  optimization suited to a particluar DSP processor.
  Scaling ensures that result remains 16-bit.
*/
inline int16_t FIX_MPY(int16_t a, int16_t b)
{   
    int32_t c = ((int32_t)a * (int32_t)b) >> (sizeof(a) * 8 - 2);

    b = c & 0x01;
    a = (c >> 1) + b;
 
    return a;
}
 
/*
  fix_fft() - perform forward/inverse fast Fourier transform.
  fr[n],fi[n] are real and imaginary arrays, both INPUT AND
  RESULT (in-place FFT), with 0 <= n < 2**m; set inverse to
  0 for forward transform (FFT), or 1 for iFFT.
*/
int16_t fix_fft(int16_t fr[], int16_t fi[], int m, int inverse)
{
  int16_t mr, nn, i, j, l, k, istep, n, scale, shift;
  int16_t qr, qi, tr, ti, wr, wi;
 
  int16_t max_val = (~((typeof(fr[0])) 0)) >> 2;
 
  n = 1 << m;
 
   /* max FFT size = N_WAVE */
  if (n > N_WAVE)
    return -1;

  mr = 0;
  nn = n - 1;
  scale = 0;
 
  /* decimation in time - re-order data */
  for (m=1; m<=nn; ++m) {
    l = n;
    do {
      l >>= 1;
    } while (mr+l > nn);
    mr = (mr & (l-1)) + l;
 
    if (mr <= m)
      continue;
    tr = fr[m];
    fr[m] = fr[mr];
    fr[mr] = tr;
    ti = fi[m];
    fi[m] = fi[mr];
    fi[mr] = ti;
  }
 
  l = 1;
  k = LOG2_N_WAVE-1;
  while (l < n) {
    if (inverse) {
      /* variable scaling, depending upon data */
      shift = 0;
      for (i=0; i<n; ++i) {
          j = fr[i];
          if (j < 0)
            j = -j;
          m = fi[i];
          if (m < 0)
            m = -m;
          if (j > max_val || m > max_val) {
            shift = 1;
            break;
          }
      }
      if (shift)
          ++scale;
    } else {
      /*
        fixed scaling, for proper normalization --
        there will be log2(n) passes, so this results
        in an overall factor of 1/n, distributed to
        maximize arithmetic accuracy.
      */
      shift = 1;
    }
    /*
      it may not be obvious, but the shift will be
      performed on each data point exactly once,
      during this pass.
    */
    istep = l << 1;
    for (m=0; m<l; ++m) {
      j = m << k;
      /* 0 <= j < N_WAVE/2 */
      wr = Sinewave[j+N_WAVE/4];
        
 
      wi = -Sinewave[j];
      if (inverse)
          wi = -wi;
      if (shift) {
          wr >>= 1;
          wi >>= 1;
      }
      for (i=m; i<n; i+=istep) {
          j = i + l;
          tr = FIX_MPY(wr,fr[j]) - FIX_MPY(wi,fi[j]);
          ti = FIX_MPY(wr,fi[j]) + FIX_MPY(wi,fr[j]);
          qr = fr[i];
          qi = fi[i];
          if (shift) {
            qr >>= 1;
            qi >>= 1;
          }
          fr[j] = qr - tr;
          fi[j] = qi - ti;
          fr[i] = qr + tr;
          fi[i] = qi + ti;
      }
    }
    --k;
    l = istep;
  }
  return scale;
}
 
/*
  fix_fftr() - forward/inverse FFT on array of real numbers.
  Real FFT/iFFT using half-size complex FFT by distributing
  even/odd samples into real/imaginary arrays respectively.
  In order to save data space (i.e. to avoid two arrays, one
  for real, one for imaginary samples), we proceed in the
  following two steps: a) samples are rearranged in the real
  array so that all even samples are in places 0-(N/2-1) and
  all imaginary samples in places (N/2)-(N-1), and b) fix_fft
  is called with fr and fi pointing to index 0 and index N/2
  respectively in the original array. The above guarantees
  that fix_fft "sees" consecutive real samples as alternating
  real and imaginary samples in the complex array.
*/
int fix_fftr(int16_t f[], int m, int inverse)
{
    int i, N = 1<<(m-1), scale = 0;
    int16_t tt, *fr=f, *fi=&f[N];
 
    if (inverse)
      scale = fix_fft(fi, fr, m-1, inverse);
    for (i=1; i<N; i+=2) {
      tt = f[N+i-1];
      f[N+i-1] = f[i];
      f[i] = tt;
    }
    if (! inverse)
      scale = fix_fft(fi, fr, m-1, inverse);
    return scale;
}

// Blackman window weights for 512 points.
static const int16_t blackman_window[N_WAVE / 2] = {
0, 0, 0, 0, 1, 2, 3, 5,
 7, 9, 11, 13, 16, 18, 21, 25,
 28, 32, 36, 40, 44, 49, 54, 59,
 64, 69, 75, 81, 87, 94, 101, 108,
 115, 122, 130, 138, 146, 154, 163, 172,
 181, 190, 200, 210, 220, 230, 241, 252,
 263, 275, 286, 298, 311, 323, 336, 349,
 362, 376, 390, 404, 418, 433, 448, 463,
 479, 495, 511, 527, 544, 561, 578, 596,
 614, 632, 651, 670, 689, 709, 728, 749,
 769, 790, 811, 832, 854, 876, 899, 922,
 945, 968, 992, 1016, 1041, 1066, 1091, 1116,
 1142, 1169, 1195, 1222, 1250, 1278, 1306, 1334,
 1363, 1393, 1422, 1452, 1483, 1514, 1545, 1576,
 1608, 1641, 1674, 1707, 1741, 1775, 1809, 1844,
 1879, 1915, 1951, 1988, 2025, 2062, 2100, 2138,
 2177, 2216, 2255, 2295, 2336, 2377, 2418, 2460,
 2502, 2545, 2588, 2632, 2676, 2720, 2765, 2811,
 2857, 2903, 2950, 2997, 3045, 3093, 3142, 3191,
 3241, 3291, 3342, 3393, 3445, 3497, 3550, 3603,
 3656, 3710, 3765, 3820, 3876, 3932, 3988, 4046,
 4103, 4161, 4220, 4279, 4339, 4399, 4459, 4521,
 4582, 4644, 4707, 4770, 4834, 4898, 4963, 5028,
 5094, 5160, 5227, 5294, 5362, 5430, 5499, 5568,
 5638, 5709, 5780, 5851, 5923, 5995, 6068, 6142,
 6216, 6290, 6365, 6441, 6517, 6593, 6671, 6748,
 6826, 6905, 6984, 7063, 7144, 7224, 7305, 7387,
 7469, 7552, 7635, 7718, 7802, 7887, 7972, 8058,
 8144, 8230, 8317, 8405, 8493, 8581, 8670, 8759,
 8849, 8940, 9030, 9122, 9213, 9306, 9398, 9491,
 9585, 9679, 9773, 9868, 9963, 10059, 10155, 10252,
 10349, 10446, 10544, 10643, 10741, 10840, 10940, 11040,
 11140, 11241, 11342, 11444, 11546, 11648, 11750, 11853,
 11957, 12061, 12165, 12269, 12374, 12479, 12585, 12690,
 12797, 12903, 13010, 13117, 13224, 13332, 13440, 13548,
 13657, 13766, 13875, 13985, 14094, 14204, 14315, 14425,
 14536, 14647, 14758, 14870, 14982, 15094, 15206, 15318,
 15431, 15543, 15656, 15770, 15883, 15996, 16110, 16224,
 16338, 16452, 16566, 16681, 16795, 16910, 17025, 17140,
 17255, 17370, 17485, 17600, 17716, 17831, 17947, 18062,
 18178, 18293, 18409, 18525, 18641, 18756, 18872, 18988,
 19104, 19219, 19335, 19451, 19566, 19682, 19797, 19913,
 20028, 20144, 20259, 20374, 20489, 20604, 20719, 20834,
 20949, 21063, 21178, 21292, 21406, 21520, 21634, 21747,
 21861, 21974, 22087, 22200, 22312, 22425, 22537, 22649,
 22760, 22872, 22983, 23094, 23204, 23315, 23425, 23534,
 23644, 23753, 23862, 23970, 24078, 24186, 24293, 24401,
 24507, 24613, 24719, 24825, 24930, 25035, 25139, 25243,
 25347, 25450, 25552, 25654, 25756, 25857, 25958, 26058,
 26158, 26257, 26356, 26454, 26552, 26649, 26746, 26842,
 26938, 27033, 27127, 27221, 27314, 27407, 27499, 27591,
 27682, 27772, 27862, 27951, 28040, 28128, 28215, 28301,
 28387, 28472, 28557, 28641, 28724, 28807, 28888, 28970,
 29050, 29130, 29209, 29287, 29364, 29441, 29517, 29592,
 29667, 29741, 29814, 29886, 29957, 30028, 30098, 30167,
 30235, 30303, 30369, 30435, 30500, 30564, 30627, 30690,
 30752, 30812, 30872, 30931, 30990, 31047, 31103, 31159,
 31214, 31268, 31321, 31373, 31424, 31474, 31523, 31572,
 31619, 31666, 31712, 31756, 31800, 31843, 31885, 31926,
 31966, 32005, 32043, 32080, 32117, 32152, 32186, 32220,
 32252, 32284, 32314, 32344, 32372, 32400, 32426, 32452,
 32476, 32500, 32522, 32544, 32565, 32584, 32603, 32621,
 32637, 32653, 32667, 32681, 32694, 32705, 32716, 32726,
 32734, 32742, 32748, 32754, 32758, 32762, 32764, 32766,
 
 /*32766, 32766, 32764, 32762, 32758, 32754, 32748, 32742,
 32734, 32726, 32716, 32705, 32694, 32681, 32667, 32653,
 32637, 32621, 32603, 32584, 32565, 32544, 32522, 32500,
 32476, 32452, 32426, 32400, 32372, 32344, 32314, 32284,
 32252, 32220, 32186, 32152, 32117, 32080, 32043, 32005,
 31966, 31926, 31885, 31843, 31800, 31756, 31712, 31666,
 31619, 31572, 31523, 31474, 31424, 31373, 31321, 31268,
 31214, 31159, 31103, 31047, 30990, 30931, 30872, 30812,
 30752, 30690, 30627, 30564, 30500, 30435, 30369, 30303,
 30235, 30167, 30098, 30028, 29957, 29886, 29814, 29741,
 29667, 29592, 29517, 29441, 29364, 29287, 29209, 29130,
 29050, 28970, 28888, 28807, 28724, 28641, 28557, 28472,
 28387, 28301, 28215, 28128, 28040, 27951, 27862, 27772,
 27682, 27591, 27499, 27407, 27314, 27221, 27127, 27033,
 26938, 26842, 26746, 26649, 26552, 26454, 26356, 26257,
 26158, 26058, 25958, 25857, 25756, 25654, 25552, 25450,
 25347, 25243, 25139, 25035, 24930, 24825, 24719, 24613,
 24507, 24401, 24293, 24186, 24078, 23970, 23862, 23753,
 23644, 23534, 23425, 23315, 23204, 23094, 22983, 22872,
 22760, 22649, 22537, 22425, 22312, 22200, 22087, 21974,
 21861, 21747, 21634, 21520, 21406, 21292, 21178, 21063,
 20949, 20834, 20719, 20604, 20489, 20374, 20259, 20144,
 20028, 19913, 19797, 19682, 19566, 19451, 19335, 19219,
 19104, 18988, 18872, 18756, 18641, 18525, 18409, 18293,
 18178, 18062, 17947, 17831, 17716, 17600, 17485, 17370,
 17255, 17140, 17025, 16910, 16795, 16681, 16566, 16452,
 16338, 16224, 16110, 15996, 15883, 15770, 15656, 15543,
 15431, 15318, 15206, 15094, 14982, 14870, 14758, 14647,
 14536, 14425, 14315, 14204, 14094, 13985, 13875, 13766,
 13657, 13548, 13440, 13332, 13224, 13117, 13010, 12903,
 12797, 12690, 12585, 12479, 12374, 12269, 12165, 12061,
 11957, 11853, 11750, 11648, 11546, 11444, 11342, 11241,
 11140, 11040, 10940, 10840, 10741, 10643, 10544, 10446,
 10349, 10252, 10155, 10059, 9963, 9868, 9773, 9679,
 9585, 9491, 9398, 9306, 9213, 9122, 9030, 8940,
 8849, 8759, 8670, 8581, 8493, 8405, 8317, 8230,
 8144, 8058, 7972, 7887, 7802, 7718, 7635, 7552,
 7469, 7387, 7305, 7224, 7144, 7063, 6984, 6905,
 6826, 6748, 6671, 6593, 6517, 6441, 6365, 6290,
 6216, 6142, 6068, 5995, 5923, 5851, 5780, 5709,
 5638, 5568, 5499, 5430, 5362, 5294, 5227, 5160,
 5094, 5028, 4963, 4898, 4834, 4770, 4707, 4644,
 4582, 4521, 4459, 4399, 4339, 4279, 4220, 4161,
 4103, 4046, 3988, 3932, 3876, 3820, 3765, 3710,
 3656, 3603, 3550, 3497, 3445, 3393, 3342, 3291,
 3241, 3191, 3142, 3093, 3045, 2997, 2950, 2903,
 2857, 2811, 2765, 2720, 2676, 2632, 2588, 2545,
 2502, 2460, 2418, 2377, 2336, 2295, 2255, 2216,
 2177, 2138, 2100, 2062, 2025, 1988, 1951, 1915,
 1879, 1844, 1809, 1775, 1741, 1707, 1674, 1641,
 1608, 1576, 1545, 1514, 1483, 1452, 1422, 1393,
 1363, 1334, 1306, 1278, 1250, 1222, 1195, 1169,
 1142, 1116, 1091, 1066, 1041, 1016, 992, 968,
 945, 922, 899, 876, 854, 832, 811, 790,
 769, 749, 728, 709, 689, 670, 651, 632,
 614, 596, 578, 561, 544, 527, 511, 495,
 479, 463, 448, 433, 418, 404, 390, 376,
 362, 349, 336, 323, 311, 298, 286, 275,
 263, 252, 241, 230, 220, 210, 200, 190,
 181, 172, 163, 154, 146, 138, 130, 122,
 115, 108, 101, 94, 87, 81, 75, 69,
 64, 59, 54, 49, 44, 40, 36, 32,
 28, 25, 21, 18, 16, 13, 11, 9,
 7, 5, 3, 2, 1, 0, 0, 0,
 0,*/
};

static int apply_window(int16_t *data, int m, const int16_t *window)
{
  if (m < 1 || m > LOG2_N_WAVE) {
    return -1;
  }

  int half = 1 << (m - 1);
  int w_step = 1 << (LOG2_N_WAVE - m);

  int size = half;
  const int16_t *w_ptr = &window[w_step - 1];

  while (size-- > 0) {
    *data = FIX_MPY(*data, *w_ptr);
    data++;
    w_ptr += w_step;
  }

  size = half;
  w_ptr = &window[N_WAVE / 2 - 1];

  while (size-- > 0) {
    *data = FIX_MPY(*data, *w_ptr);
    data++;
    w_ptr -= w_step;
  }

  return 0;
}

// функция окна блэкмана
int apply_blackman_window(int16_t *data, int m) {
  return apply_window(data, m, blackman_window);
}

int16_t sin_i(int x, int t, int s, int16_t a) {
  x = ((x / s) % t) * N_WAVE / t;
    
  int16_t y = Sinewave[x % (N_WAVE / 2)];
    
  if (x >= N_WAVE / 2) {
    y = -y;
  }
  
  return FIX_MPY(y, a);
}

void ADC_Handler() {
  int adc_isr = ADC->ADC_ISR;
  
  if (adc_isr & ADC_ISR_ENDRX) {
    // заполнен очередной буфер, передаём контоллеру доступа следующий буфер

    int next_buf_index = (i_buf_index + 2) % BUFFER_COUNT;
    
    if (next_buf_index != o_buf_index) {
      // В кольцевом буфере есть место под новый "next" блок данных.
      // Продвигаем указатель.
      i_buf_index = (i_buf_index + 1) % BUFFER_COUNT;
    }
    else {
      // Переполнение кольцевого буфера.
      // "next" блок данных будет отброшен и заполнен заново.
      // i_buf_index не меняется.
      next_buf_index = (i_buf_index + 1) % BUFFER_COUNT;
    }
    
    ADC->ADC_RNPR = (uint32_t) buffer[next_buf_index];
    ADC->ADC_RNCR = BUFFER_SIZE;
  }
}

int16_t *get_adc_buffer() {
  while (o_buf_index == i_buf_index);
  
  return buffer[o_buf_index];
}

// процедуа полученеия свободного буфера
void free_adc_buffer() {
  if (o_buf_index != i_buf_index) {
    o_buf_index = (o_buf_index + 1) % BUFFER_COUNT;
  }
}

// функция извлечения квадратного корня из числа с фиксированной точкой
int16_t sqrt_i(int16_t x) {
 int16_t a, b;
 int32_t xdp = ((int32_t) x) << 16;

 a = 0;
 b = INT16_MAX;

  while (b - a > 1) {
    int16_t m = (a + b) / 2;
    int32_t sq = (((int32_t) m) * m) << 1;

    if (sq > xdp) {
      b = m;
    }
    else {
      a = m;
    }
  }

  if (abs(((((int32_t) a) * a) << 1) - xdp) < abs(((((int32_t) b) * b) << 1) - xdp)) {
    return a;
  }
  else {
    return b;
  }
}

/* функция расчёта значения x. Возвращает: 
  min_value, когда x < min_value;
  max_value, когда x > max_value
  в потивном случае x
*/
inline int limit(int x, int min_value, int max_value) {
  if (x < min_value) {
    return min_value;
  }
  else if (x > max_value) {
    return max_value;
  }
  
  return x;
}

// функция перевода из уровня сигнала в уровень ШИМ
inline uint8_t signal_to_pwm(int16_t sig_value) {
  return limit(FIX_MPY(sig_value, PWM_CHANNEL_RATIO), 0, 255);
}

// поиск индекса максимума в массиве x[size]
size_t max_array_element(const int16_t *x, size_t size) {
  int16_t max_value = x[0];
  size_t max_index = 0;
  
  for (size_t i = 1; i < size; i++) {
    if (x[i] > max_value) {
      max_value = x[i];
      max_index = i;
    }
  }
  
  return max_index;
}

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
  
  i_buf_index = o_buf_index = 0; // начальные установки АЦП
  
  adc_init(ADC, SystemCoreClock, ADC_FREQ_MIN, ADC_STARTUP_NORM); // инициализация АЦП ADC_FREQ_MIN = 1000000 (1 MHz)
  adc_configure_timing(ADC, 0, ADC_SETTLING_TIME_3, 1); // 23 periods ADC_FREQ per 1 conversion (43478.26 Hz)
  adc_configure_trigger(ADC, ADC_TRIG_SW, 1);
  adc_disable_interrupt(ADC, 0xFFFFFFFF);
  adc_disable_all_channel(ADC); // запрет АЦП на всех каналах
  
  adc_enable_channel(ADC, ADC_CHANNEL_7); // разрешение АЦП на 7 канале МК (А0)
  
  adc_enable_interrupt(ADC, ADC_IER_ENDRX); // разрешаем прерывание по готовности буфера прямого доступа в память
  
  // инициализиуем прямой доступ в память
  ADC->ADC_RPR = (uint32_t) buffer[0]; 
  ADC->ADC_RCR = BUFFER_SIZE;
  ADC->ADC_RNPR = (uint32_t) buffer[1];
  ADC->ADC_RNCR = BUFFER_SIZE;
  ADC->ADC_PTCR = ADC_PTCR_RXTEN; // разрешаем прямой доступ в память
  
  NVIC_EnableIRQ(ADC_IRQn); // разрешение прерывания от АЦП в контроллере прерываний
  
  adc_start(ADC); // запуск АЦП
  
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
 
#ifdef DEBUG_PRINT_DC_LEVEL
  Serial.print("dc_level = ");  
  Serial.print(dc_level);
  Serial.println();   
#endif
}

void loop() {   
#ifdef DEBUG_COUNT
  int16_t time[COUNT];
#endif
  
#ifdef DEBUG_PRINT_TIME
  unsigned long time0 = millis();
#endif
  
  int16_t *data = get_adc_buffer(); // получаем данные преобазованич от АЦП в массив data
  
  unsigned long time0 = micros();
  
  int16_t channels[3]; // переменная, хранящая амплитуды сигналов

#ifdef DEBUG_PRINT_TIME
  unsigned long time1 = millis();

  time[count] = time1 - time0;
#endif // DEBUG_PRINT_TIME
  
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

#ifdef DEBUG_PRINT_LEVEL_COUNTERS
  for (int i = 0; i < NUM_LEVEL_COUNTERS; i++) {
    Serial.print("level_counters[");
    Serial.print(i);
    Serial.print("] = ");
    Serial.print(level_counters[i]);
    Serial.println("");
  }
#endif
  
  int min_level_counter_index;
  int max_level_counter_index;
  
  for (min_level_counter_index = 0; min_level_counter_index < NUM_LEVEL_COUNTERS && level_counters[min_level_counter_index] == 0; min_level_counter_index++)
    ;  
  
  for (max_level_counter_index = NUM_LEVEL_COUNTERS - 1; max_level_counter_index > min_level_counter_index && level_counters[max_level_counter_index] == 0; max_level_counter_index--)
    ;
    
#ifdef DEBUG_PRINT_MIN_MAX_COUNTER_INDEX
  Serial.print("min_level_counter_index: ");
  Serial.print(min_level_counter_index);
  Serial.print("; max_level_counter_index: ");
  Serial.print(max_level_counter_index);
  Serial.println("");
#endif
  
  int is_overload = 0;
  
  if (max_level_counter_index - min_level_counter_index > MIN_OVERLOAD_DETECTION_LEVEL) {
    int16_t level_counters_max = 0;
  
    for (int i = 0; i < 4; i++) {
      int max_index = max_array_element(level_counters, NUM_LEVEL_COUNTERS);
      level_counters_max += level_counters[max_index];
      level_counters[max_index] = 0;
      
#ifdef DEBUG_PRINT_LEVEL_COUNTERS
      Serial.print("max_index = ");
      Serial.print(max_index);
      Serial.println("");
#endif // DEBUG_PRINT_LEVEL_COUNTERS
    }
    
#ifdef DEBUG_PRINT_LEVEL_COUNTERS
      Serial.print("level_counters_max = ");
      Serial.print(level_counters_max);
      Serial.println("");
#endif // DEBUG_PRINT_LEVEL_COUNTERS 
    
    if (BUFFER_SIZE / level_counters_max < OVERLOAD_DETECTION_THRESHOLD) {
      is_overload = 1;
    }
  }
  
#ifdef DEBUG_PRINT_OVERLOAD_FLAG
  Serial.print("is_overload = ");
  Serial.print(is_overload);
  Serial.println("");
#endif
  
  //Перегрузка      
  digitalWrite(OVERLOAD_LED, is_overload);
  
#ifdef DEBUG_PRINT_DATA
  for(int i = 0; i < 1024; i++) {
    Serial.print("data[");
    Serial.print(i);
    Serial.print("] = ");  
    Serial.print(data[i]);
    Serial.println();   
  }
#endif

#ifdef APPLY_WINDOW  
  apply_blackman_window(data, LOG2_N_WAVE); // накладываем окно Блэкмана
#endif

#ifdef DEBUG_PRINT_BLACKMAN_DATA  
  for(int i = 0; i < 1024; i++) {
    Serial.print("(Blackman) data[");
    Serial.print(i);
    Serial.print("] = ");  
    Serial.print(data[i]);
    Serial.println();   
  }
#endif

  fix_fft(data, im, LOG2_N_WAVE, 0); // выполняем преобразование Фурье

#ifdef DEBUG_PRINT_FFT_DATA 
  for(int i = 0; i < N_WAVE; i++) {
    Serial.print("(fft) data[");
    Serial.print(i);
    Serial.print("] = ");  
    Serial.print(data[i]);
    Serial.print(",  (fft) im[");
    Serial.print(i);
    Serial.print("] = ");  
    Serial.print(im[i]);
    Serial.println();   
  }
#endif
  
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

#ifdef DEBUG_PRINT_CHANNELS
  Serial.print("channels[0] = ");
  Serial.print(channels[0]);
  Serial.print("; channels[1] = ");
  Serial.print(channels[1]);
  Serial.print("; channels[2] = ");
  Serial.print(channels[2]);
  Serial.println();
#endif
  
  free_adc_buffer();
  
#ifdef LOOP_TIME    
  unsigned long time1 = micros();
#endif
  
#ifdef DEBUG_COUNT
  count++;
  
  if (count >= COUNT) {
    
#ifdef DEBUG_PRINT_TIME
    for(int i = 0; i < COUNT; i++) {
      Serial.print("time[");
      Serial.print(i);
      Serial.print("] = ");
      Serial.print(time[i]);
      Serial.println();
    }
#endif 
    
    for(;;);    
  }
#endif

#ifdef LOOP_TIME  
  Serial.println();
  Serial.print("loop time: ");
  Serial.print(time1 - time0);
  Serial.println(" us");
#endif
}

