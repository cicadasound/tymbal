#pragma once

struct Settings
{
    float volume;
    float fine_tune;
    float scale_1;
    float offset_1;
    float scale_2;
    float offset_2;
    float shape_knob;
    float filter_cv_amount;
    float chorus_knob;
    float env_mod_knob;

    bool operator==(const Settings &rhs)
    {
        return volume == rhs.volume &&
               fine_tune == rhs.fine_tune &&
               scale_1 == rhs.scale_1 &&
               offset_1 == rhs.offset_1 &&
               scale_2 == rhs.scale_2 &&
               offset_2 == rhs.offset_2 &&
               shape_knob == rhs.shape_knob &&
               filter_cv_amount == rhs.filter_cv_amount &&
               chorus_knob == rhs.chorus_knob &&
               env_mod_knob == rhs.env_mod_knob;
    }
    bool operator!=(const Settings &rhs) { return !operator==(rhs); }
};

static Settings default_settings{
    0.7f, // volume
    0.0f, // fine_tune
    0.0f, // scale_1
    0.0f, // offset_1
    0.0f, // scale_2
    0.0f, // offset_2
    0.0f, // shape_knob
    0.0f, // filter_cv_amount (0 = CV has no effect)
    0.0f, // chorus_knob
    0.0f, // env_mod_knob
};
