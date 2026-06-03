#include "plugin.hpp"
#include "TymbalVoice.h"

// ── Module ────────────────────────────────────────────────────────────────────

struct TymbalModule : Module {
    enum ParamId {
        ATTACK_PARAM,
        RELEASE_PARAM,
        CUTOFF_PARAM,
        RESONANCE_PARAM,
        SHAPE_PARAM,
        PWM_PARAM,
        CHORUS_PARAM,
        ENV_MOD_PARAM,
        OSC_MODE_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        VOCT_1_INPUT,
        VOCT_2_INPUT,
        GATE_1_INPUT,
        GATE_2_INPUT,
        FILTER_CV_INPUT,
        PWM_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT_L_OUTPUT,
        OUT_R_OUTPUT,
        ENV_OUTPUT,
        OUTPUTS_LEN
    };

    TymbalVoice dsp;

    TymbalModule() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN);

        configParam(ATTACK_PARAM,    0.f, 1.f, 0.f,   "Attack");
        configParam(RELEASE_PARAM,   0.f, 1.f, 0.25f, "Release");
        configParam(CUTOFF_PARAM,    0.f, 1.f, 0.8f,  "Cutoff");
        configParam(RESONANCE_PARAM, 0.f, 1.f, 0.f,   "Resonance");
        configParam(SHAPE_PARAM,     0.f, 1.f, 0.f,   "Shape");
        configParam(PWM_PARAM,       0.f, 1.f, 0.f,   "Chorus");
        configParam(CHORUS_PARAM,    0.f, 1.f, 0.f,   "Filter CV Amount");
        configParam(ENV_MOD_PARAM,   0.f, 1.f, 0.f,   "Env Mod");
        configSwitch(OSC_MODE_PARAM, 0.f, 1.f, 1.f,   "Osc Mode", {"Pass", "Osc"});

        configInput(VOCT_1_INPUT,    "V/Oct 1");
        configInput(VOCT_2_INPUT,    "V/Oct 2");
        configInput(GATE_1_INPUT,    "Gate 1");
        configInput(GATE_2_INPUT,    "Gate 2");
        configInput(FILTER_CV_INPUT, "Filter CV");
        configInput(PWM_CV_INPUT,    "Shape CV");

        configOutput(OUT_L_OUTPUT, "Left");
        configOutput(OUT_R_OUTPUT, "Right");
        configOutput(ENV_OUTPUT,   "Envelope");

        dsp.init(44100.f);
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        dsp.init(e.sampleRate);
    }

    void process(const ProcessArgs& args) override {
        // ── Build voice params from knobs ─────────────────────────────────────
        TymbalVoice::Params vp;
        vp.attack    = params[ATTACK_PARAM].getValue();
        vp.release   = params[RELEASE_PARAM].getValue();
        vp.cutoff    = params[CUTOFF_PARAM].getValue();
        vp.resonance = params[RESONANCE_PARAM].getValue();
        // Shape trimpot dual-function:
        //   No CV patched → sets shape directly
        //   CV patched    → attenuates the CV (knob = 0 means no CV effect, 1 = full CV)
        float shape_trimpot = params[SHAPE_PARAM].getValue();
        if (inputs[PWM_CV_INPUT].isConnected()) {
            float shape_cv = inputs[PWM_CV_INPUT].getVoltage() / 5.f;
            vp.shape = fclamp(shape_cv * shape_trimpot, 0.f, 1.f);
        } else {
            vp.shape = shape_trimpot;
        }
        // Shift layout: 1=shape  2=chorus  3=filter CV amount  4=env mod
        vp.chorus           = params[PWM_PARAM].getValue();       // top-right trimpot
        vp.filter_cv_amount = params[CHORUS_PARAM].getValue();    // bottom-left trimpot
        vp.env_mod          = params[ENV_MOD_PARAM].getValue();   // bottom-right trimpot
        vp.osc_mode  = params[OSC_MODE_PARAM].getValue() > 0.5f;

        // ── V/Oct → Hz (Rack standard: 0V = C4 = 261.63Hz, 1V/octave) ────────
        float voct_1 = inputs[VOCT_1_INPUT].getVoltage();
        float voct_2 = inputs[VOCT_2_INPUT].isConnected()
                     ? inputs[VOCT_2_INPUT].getVoltage()
                     : voct_1;

        float freq_1 = mtof(fclamp(60.f + voct_1 * 12.f, 0.f, 127.f));
        float freq_2 = mtof(fclamp(60.f + voct_2 * 12.f, 0.f, 127.f));

        // ── Gates ─────────────────────────────────────────────────────────────
        bool gate_1 = inputs[GATE_1_INPUT].getVoltage() > 1.f;
        bool gate_2 = inputs[GATE_2_INPUT].isConnected()
                    ? inputs[GATE_2_INPUT].getVoltage() > 1.f
                    : gate_1;

        // ── CV inputs (normalised to 0–1) ─────────────────────────────────────
        float filter_cv = inputs[FILTER_CV_INPUT].getVoltage() / 5.f;

        // ── Process ───────────────────────────────────────────────────────────
        auto s = dsp.process(vp, freq_1, freq_2, gate_1, gate_2, filter_cv);

        // ── Outputs (audio ±5V, CV 0–10V) ─────────────────────────────────────
        outputs[OUT_L_OUTPUT].setVoltage(s.left  * 5.f);
        outputs[OUT_R_OUTPUT].setVoltage(s.right * 5.f);
        outputs[ENV_OUTPUT].setVoltage(s.env * 10.f);
    }
};

// ── Panel label widget ────────────────────────────────────────────────────────

struct PanelLabel : Widget {
    std::string text;
    NVGcolor    color;
    float       fontSize;

    PanelLabel(Vec pos, std::string t, NVGcolor c, float size = 9.f)
        : text(t), color(c), fontSize(size) {
        box.pos  = pos;
        box.size = Vec(0, 0);
    }

    void draw(const DrawArgs& args) override {
        std::shared_ptr<Font> font =
            APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);
        nvgFontSize(args.vg, fontSize);
        nvgFillColor(args.vg, color);
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, 0, 0, text.c_str(), NULL);
    }
};

// ── Widget ────────────────────────────────────────────────────────────────────

struct TymbalWidget : ModuleWidget {
    TymbalWidget(TymbalModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Tymbal.svg")));

        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Primary knobs
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(18, 30)), module, TymbalModule::ATTACK_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(53, 30)), module, TymbalModule::RELEASE_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(18, 65)), module, TymbalModule::CUTOFF_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(53, 65)), module, TymbalModule::RESONANCE_PARAM));

        // Shift knobs
        addParam(createParamCentered<Trimpot>(mm2px(Vec(18, 44)), module, TymbalModule::SHAPE_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(53, 44)), module, TymbalModule::PWM_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(18, 79)), module, TymbalModule::CHORUS_PARAM));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(53, 79)), module, TymbalModule::ENV_MOD_PARAM));

        // Toggle
        addParam(createParamCentered<CKSS>(mm2px(Vec(35.5, 90)), module, TymbalModule::OSC_MODE_PARAM));

        // Inputs — row 1
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 104)), module, TymbalModule::VOCT_1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(26, 104)), module, TymbalModule::VOCT_2_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(42, 104)), module, TymbalModule::GATE_1_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(58, 104)), module, TymbalModule::GATE_2_INPUT));

        // Inputs — row 2
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 118)), module, TymbalModule::FILTER_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(22, 118)), module, TymbalModule::PWM_CV_INPUT));

        // Outputs — row 2
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(35.5, 118)), module, TymbalModule::ENV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(50,   118)), module, TymbalModule::OUT_L_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(62,   118)), module, TymbalModule::OUT_R_OUTPUT));

        // ── Labels ───────────────────────────────────────────────────────────
        NVGcolor white  = nvgRGB(220, 220, 220);
        NVGcolor orange = nvgRGB(232, 120, 48);
        NVGcolor dim    = nvgRGB(140, 140, 140);
        NVGcolor green  = nvgRGB(160, 210, 160);

        addChild(new PanelLabel(mm2px(Vec(35.5, 7)),  "TYMBAL",  white, 14.f));

        addChild(new PanelLabel(mm2px(Vec(18, 22)),   "ATTACK",  white, 8.5f));
        addChild(new PanelLabel(mm2px(Vec(53, 22)),   "RELEASE", white, 8.5f));
        addChild(new PanelLabel(mm2px(Vec(18, 57)),   "CUTOFF",  white, 8.5f));
        addChild(new PanelLabel(mm2px(Vec(53, 57)),   "RESO",    white, 8.5f));

        addChild(new PanelLabel(mm2px(Vec(18, 37)),   "SHAPE",   orange, 7.f));
        addChild(new PanelLabel(mm2px(Vec(53, 37)),   "CHORUS",  orange, 7.f));
        addChild(new PanelLabel(mm2px(Vec(18, 72)),   "FILT CV", orange, 7.f));
        addChild(new PanelLabel(mm2px(Vec(53, 72)),   "ENV MOD", orange, 7.f));

        addChild(new PanelLabel(mm2px(Vec(35.5, 84.5)), "OSC",  white, 7.f));
        addChild(new PanelLabel(mm2px(Vec(35.5, 95.5)), "PASS", white, 7.f));

        addChild(new PanelLabel(mm2px(Vec(10,  99.5)), "V/OCT1", dim, 5.5f));
        addChild(new PanelLabel(mm2px(Vec(26,  99.5)), "V/OCT2", dim, 5.5f));
        addChild(new PanelLabel(mm2px(Vec(42,  99.5)), "GATE 1", dim, 5.5f));
        addChild(new PanelLabel(mm2px(Vec(58,  99.5)), "GATE 2", dim, 5.5f));

        addChild(new PanelLabel(mm2px(Vec(10,   113.5)), "FILT",  dim,   5.5f));
        addChild(new PanelLabel(mm2px(Vec(22,   113.5)), "SHP",   dim,   5.5f));
        addChild(new PanelLabel(mm2px(Vec(35.5, 113.5)), "ENV",   green, 5.5f));
        addChild(new PanelLabel(mm2px(Vec(50,   113.5)), "OUT L", green, 5.5f));
        addChild(new PanelLabel(mm2px(Vec(62,   113.5)), "OUT R", green, 5.5f));

        addChild(new PanelLabel(mm2px(Vec(35.5, 125)), "Cicada Sound", dim, 8.f));
    }
};

Model* modelTymbal = createModel<TymbalModule, TymbalWidget>("Tymbal");
