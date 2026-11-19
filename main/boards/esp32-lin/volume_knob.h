#ifndef _VOLUME_KNOB_H_
#define _VOLUME_KNOB_H_

#include <esp_adc/adc_oneshot.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Samples the on-board 5 kOhm volume potentiometer on GPIO36 (ADC1_CH0).
// The wiper voltage is mapped linearly to the WM8978 output volume (0..100).
// Only knob movement changes the volume: a stationary knob never overwrites
// a volume set by the server, so "last action wins" applies in both directions.
class VolumeKnob {
public:
    VolumeKnob();
    ~VolumeKnob();

private:
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    int samples_[5] = {};
    int sample_count_ = 0;
    int sample_index_ = 0;
    int last_target_ = -1;
    int64_t last_apply_us_ = 0;

    static void PollTask(void* arg);
    void PollLoop();
    int ReadAveragedRaw();
};

#endif  // _VOLUME_KNOB_H_
