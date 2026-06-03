#include "daisysp.h"

using namespace daisy;

class Control {
  public:
    Control() {}
    ~Control() {}

    void Process(bool& shift_mode, float& knob_input, float& target_value, float& target_shift_value);

    inline void Start() {
      pickup_ = 0.0f;
    };

    void SetLastValue(float value) {
      last_value_ = value;
    };

    float GetLastValue() {
      return last_value_;
    };

  private:
    float last_value_;
    float pickup_ = 0.0f;
};

float CHANGE_THRESHOLD = 0.002f;

bool ValuesChangedThreshold(float last_value, float new_value) {
    bool is_changed = fabsf(new_value - last_value) > CHANGE_THRESHOLD;
    return is_changed;
}

void Control::Process(bool& shift_mode, float& knob_input, float& target_control, float& target_shift_control) {
    float current_knob_value = knob_input;
    bool cutoff_changed = ValuesChangedThreshold(last_value_, current_knob_value);

    if (cutoff_changed) {
        float difference = last_value_ - current_knob_value;
        if (shift_mode) {
            pickup_ = target_control;
            target_shift_control = current_knob_value;
        } else if (
            pickup_ == 0.0f ||
            (difference < 0.0f && current_knob_value >= pickup_) ||
            (difference > 0.0f && current_knob_value <= pickup_)
        ) {
            target_control = current_knob_value;
            pickup_ = 0.0f;
        }

        last_value_ = current_knob_value;
    }
}
