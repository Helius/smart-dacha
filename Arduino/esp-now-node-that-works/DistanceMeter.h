#pragma once

#define TRIG_PIN 0
#define ECHO_PIN 1

#define SAMPLES_CNT 5
unsigned long measurements[SAMPLES_CNT];
size_t measurementCount = 0;

unsigned long getMedian(unsigned long arr[]) {
  // Простая сортировка пузырьком (для 5 элементов это самый быстрый и легкочитаемый способ)
  for (int i = 0; i < SAMPLES_CNT - 1; i++) {
    for (int j = i + 1; j < SAMPLES_CNT; j++) {
      if (arr[i] > arr[j]) {
        // Меняем элементы местами
        unsigned long temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
      }
    }
  }

  // Возвращаем центральный элемент отсортированного массива
  return arr[SAMPLES_CNT / 2];
}

unsigned long takeUltrasonicMeasurement() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // pulseIn имеет таймаут. 30000 мкс = ~5 метров макс.
  // Это предотвратит зависание, если датчик не увидит эхо.
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 10000);
  return (duration / 2) / 29.1;
}