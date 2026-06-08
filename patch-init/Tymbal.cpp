#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "calibrate.h"
#include "control.h"
#include "settings.h"
#include "TymbalVoice.h"

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// ── Hardware ──────────────────────────────────────────────────────────────────

DaisyPatchSM patch;
PersistentStorage<Settings> storage(patch.qspi);
Calibrate calibration_1, calibration_2;
Switch button, toggle;
Control control_1, control_2, control_3, control_4;

// ── DSP ───────────────────────────────────────────────────────────────────────

TymbalVoice dsp;

// ── State ─────────────────────────────────────────────────────────────────────

// Hardware knob/control state (separate from DSP voice state)
struct Controls
{
    float attack_knob    = 0.f;
    float decay_knob     = 0.f;
    float cutoff_knob    = 0.f;
    float resonance_knob = 0.f;

    float shape_knob      = 0.f;
    float filter_cv_amount = 0.f;   // shift-3: attenuates filter CV input
    float chorus_knob     = 0.f;
    float env_mod_knob    = 0.f;
} controls;

struct UI
{
    bool  calibration_mode     = false;
    bool  trigger_save         = false;
    bool  shift_params_changed = false;
    bool  audio_triggered      = false;
    float led_brightness       = 0.f;
    float last_button_press    = 0.f;
    float old_in_l             = 0.f;
    int   button_press_count   = 0;
} ui;

float volume = 0.5f;

// ── Audio processing ──────────────────────────────────────────────────────────

struct BlockParams
{
    float freq_1, freq_2;
    float filter_cv;   // 0–1 normalised
    float shape_cv;    // 0–1 normalised, added to shape knob
    bool  gate_1, gate_2;
    bool  osc_mode, pass_mode;
};

BlockParams ProcessControls(Settings &settings)
{
    toggle.Debounce();
    button.Debounce();
    bool shift_released = button.FallingEdge();
    bool button_pressed = button.RisingEdge();
    bool shift_mode     = button.Pressed();
    bool osc_mode       = toggle.Pressed();

    float voct_1 = patch.GetAdcValue(CV_5);
    float voct_2 = patch.GetAdcValue(CV_6);

    /** Calibration processing */
    if (ui.calibration_mode)
    {
        bool done_1 = calibration_1.ProcessCalibration(voct_1, button_pressed);
        bool done_2 = calibration_2.ProcessCalibration(voct_2, button_pressed);
        if (done_1 && done_2)
        {
            calibration_1.cal.GetData(settings.scale_1, settings.offset_1);
            calibration_2.cal.GetData(settings.scale_2, settings.offset_2);

            if (settings.scale_1 < 0) {
                settings.scale_1  = -settings.scale_1;
                settings.offset_1 = 48.f - settings.offset_1;
                calibration_1.cal.SetData(settings.scale_1, settings.offset_1);
            }
            if (settings.scale_2 < 0) {
                settings.scale_2  = -settings.scale_2;
                settings.offset_2 = 48.f - settings.offset_2;
                calibration_2.cal.SetData(settings.scale_2, settings.offset_2);
            }

            ui.trigger_save     = true;
            ui.calibration_mode = false;
        }
        ui.led_brightness = calibration_1.GetBrightness();
    }

    /** Calibration entry: 5 rapid taps */
    float time_now = System::GetNow();
    if (time_now - ui.last_button_press >= 1000.f && ui.button_press_count != 0)
        ui.button_press_count = 0;
    else if (button_pressed && !ui.calibration_mode)
    {
        ui.last_button_press = time_now;
        ui.button_press_count++;
    }

    if (ui.button_press_count >= 5 && !ui.calibration_mode)
    {
        ui.button_press_count = 0;
        ui.calibration_mode   = true;
        ui.led_brightness     = 1.f;
        calibration_1.Start();
        calibration_2.Start();
    }

    /** Gates */
    bool gate_1 = patch.gate_in_1.State();
    bool gate_2 = patch.gate_in_2.State();
    dsy_gpio_write(&patch.gate_out_1, gate_1);
    dsy_gpio_write(&patch.gate_out_2, gate_2);

    /** Knobs — normal and shift pairs
     *  Shift layout: 1=shape  2=chorus  3=filter CV amount  4=env mod */
    float prev_shape          = controls.shape_knob;
    float prev_chorus         = controls.chorus_knob;
    float prev_filter_cv_amt  = controls.filter_cv_amount;
    float prev_env_mod        = controls.env_mod_knob;

    float cv1 = patch.GetAdcValue(CV_1);
    float cv2 = patch.GetAdcValue(CV_2);
    float cv3 = patch.GetAdcValue(CV_3);
    float cv4 = patch.GetAdcValue(CV_4);

    control_1.Process(shift_mode, cv1, controls.attack_knob,    controls.shape_knob);
    control_2.Process(shift_mode, cv2, controls.decay_knob,     controls.chorus_knob);
    control_3.Process(shift_mode, cv3, controls.cutoff_knob,    controls.filter_cv_amount);
    control_4.Process(shift_mode, cv4, controls.resonance_knob, controls.env_mod_knob);

    /** Shift param persistence */
    if (shift_mode && (controls.shape_knob       != prev_shape         ||
                       controls.chorus_knob       != prev_chorus        ||
                       controls.filter_cv_amount  != prev_filter_cv_amt ||
                       controls.env_mod_knob      != prev_env_mod))
        ui.shift_params_changed = true;

    if (shift_released && ui.shift_params_changed)
    {
        settings.shape_knob       = controls.shape_knob;
        settings.chorus_knob      = controls.chorus_knob;
        settings.filter_cv_amount = controls.filter_cv_amount;
        settings.env_mod_knob     = controls.env_mod_knob;
        ui.trigger_save       = true;
        ui.shift_params_changed = false;
    }

    /** V/Oct → frequency */
    float midi_nn_1 = fclamp(calibration_1.cal.ProcessInput(voct_1), 0.f, 127.f);
    float midi_nn_2 = fclamp(calibration_2.cal.ProcessInput(voct_2), 0.f, 127.f);

    return {
        mtof(midi_nn_1),
        mtof(midi_nn_2),
        patch.GetAdcValue(CV_7),   // filter_cv
        patch.GetAdcValue(CV_8),   // shape_cv
        gate_1,
        gate_2,
        osc_mode,
        !osc_mode
    };
}

void ProcessSample(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t i, const BlockParams &p, const TymbalVoice::Params &vp)
{
    float dry_l = IN_L[i];
    float dry_r = IN_R[i];

    if (ui.old_in_l - dry_l > 0.001f)
        ui.audio_triggered = true;
    ui.old_in_l = dry_l;

    TymbalVoice::Output s;
    if (p.osc_mode) {
        s = dsp.process(vp, p.freq_1, p.freq_2, p.gate_1, p.gate_2, p.filter_cv);
    } else {
        s = dsp.processPassthrough(vp, dry_l, dry_r, p.gate_1, p.gate_2, p.filter_cv);
    }

    if (!ui.audio_triggered && p.pass_mode)
    {
        OUT_L[i] = s.env;
        OUT_R[i] = s.env;
    }
    else
    {
        OUT_L[i] = s.left  * volume;
        OUT_R[i] = s.right * volume;
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    patch.ProcessAllControls();
    Settings &settings = storage.GetSettings();
    BlockParams p = ProcessControls(settings);

    TymbalVoice::Params vp;
    vp.attack    = controls.attack_knob;
    vp.release   = controls.decay_knob;
    vp.cutoff    = controls.cutoff_knob;
    vp.resonance = controls.resonance_knob;
    // Shape knob dual-function: direct shape when no CV, CV attenuator when CV is patched.
    // Hardware uses a signal threshold to detect patching (>0.01 ≈ 50mV at input).
    vp.shape = (p.shape_cv > 0.01f)
             ? fclamp(p.shape_cv * controls.shape_knob, 0.f, 1.f)
             : controls.shape_knob;
    vp.chorus           = controls.chorus_knob;
    vp.filter_cv_amount = controls.filter_cv_amount;
    vp.env_mod          = controls.env_mod_knob;
    vp.osc_mode         = p.osc_mode;

    for (size_t i = 0; i < size; i++)
        ProcessSample(in, out, i, p, vp);

    patch.WriteCvOut(CV_OUT_BOTH, ui.calibration_mode ? ui.led_brightness * 5.f
                                                      : dsp.env_out_1 * 5.f);
}

// ── Initialisation ────────────────────────────────────────────────────────────

int main(void)
{
    patch.Init();
    float sample_rate = patch.AudioSampleRate();

    storage.Init(default_settings);
    if (storage.GetState() == PersistentStorage<Settings>::State::FACTORY)
        storage.RestoreDefaults();

    Settings &settings = storage.GetSettings();

    if (settings.scale_1 < 0) {
        settings.scale_1  = -settings.scale_1;
        settings.offset_1 = 48.f - settings.offset_1;
    }
    if (settings.scale_2 < 0) {
        settings.scale_2  = -settings.scale_2;
        settings.offset_2 = 48.f - settings.offset_2;
    }
    calibration_1.cal.SetData(settings.scale_1, settings.offset_1);
    calibration_2.cal.SetData(settings.scale_2, settings.offset_2);

    auto load_knob = [](float v) { return (v >= 0.f && v <= 1.f) ? v : 0.f; };
    controls.shape_knob      = load_knob(settings.shape_knob);
    controls.filter_cv_amount = load_knob(settings.filter_cv_amount);
    controls.chorus_knob     = load_knob(settings.chorus_knob);
    controls.env_mod_knob    = load_knob(settings.env_mod_knob);

    button.Init(patch.B7, sample_rate);
    toggle.Init(patch.B8, sample_rate);

    dsp.init(sample_rate);

    patch.StartAudio(AudioCallback);

    while (1)
    {
        patch.Delay(1);
        if (ui.trigger_save)
        {
            storage.Save();
            ui.trigger_save = false;
        }
    }
}
