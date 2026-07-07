#include <dlfcn.h>
#include <osdialog.h>
#include "plugin.hpp"

#define UMFELD_VCV_DEBUG
#ifdef UMFELD_VCV_DEBUG
#define UMFELD_VCV_LOG(...) \
    printf("\033[32m");     \
    printf("+++ ");         \
    printf(__VA_ARGS__);    \
    printf("\033[0m");      \
    printf("\n");
#else
#define UMFELD_VCV_LOG(...)
#endif

class UmfeldApp;

struct UmfeldModule : Module {

    static constexpr uint32_t SAMPLES_PER_AUDIO_BLOCK               = 512;
    float                     mLeftOutput[SAMPLES_PER_AUDIO_BLOCK]  = {0};
    float                     mRightOutput[SAMPLES_PER_AUDIO_BLOCK] = {0};
    float                     mLeftInput[SAMPLES_PER_AUDIO_BLOCK]   = {0};
    float                     mRightInput[SAMPLES_PER_AUDIO_BLOCK]  = {0};

    float    mBangReloadButtonState  = 0.0f;
    float    mBangLoadAppButtonState = 0.0f;
    float    mBeatTriggerCounter     = 0.0f;
    float    mBeatDurationSec        = 0.125f;
    uint16_t mSampleCollectorCounter = 0;
    uint32_t mBeatCounter            = 0;

    std::string          fCurrentAppPath;
    const std::string    mDefaultApp       = "UmfeldApp";
    LedDisplayTextField* mTextFieldAppName = nullptr;
    bool                 mInitializeApp    = true;

#if defined ARCH_WIN
    HINSTANCE mHandleUmfeldSketch = 0;
#else
    void* mHandleUmfeldSketch = nullptr;
#endif

    enum ParamId {
        KNOB_A_PARAM,
        KNOB_B_PARAM,
        RELOAD_PARAM,
        LOAD_APP_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        LEFT_INPUT,
        RIGHT_INPUT,
        LEFT_CV_INPUT,
        RIGHT_CV_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        LEFT_OUTPUT,
        RIGHT_OUTPUT,
        LEFT_CV_OUTPUT,
        RIGHT_CV_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        BLINK_LIGHT,
        LIGHTS_LEN
    };

    enum { LEFT_CV_INPUT_POS,
           RIGHT_CV_INPUT_POS,
           LEFT_CV_OUTPUT_POS,
           RIGHT_CV_OUTPUT_POS,
           KNOB_PARAM_A_POS,
           KNOB_PARAM_B_POS,
           CV_POS_LEN };

    struct CVEvent {
        static constexpr uint32_t length       = CV_POS_LEN;
        float                     data[length] = {};
    };

    void handle_umfeld_app() {
        try {
            unload_app();
        } catch (Exception e) {
            UMFELD_VCV_LOG("could not unload app");
        }
        try {
            load_app();
        } catch (Exception e) {
            UMFELD_VCV_LOG("could not load app");
        }
        try {
            load_symbols();
        } catch (Exception e) {
            UMFELD_VCV_LOG("could not load symbols");
        }
        create_app();
        update_app_name();
    }

    UmfeldModule() {
        // Bug #4: config() must run before handle_umfeld_app() so ports/params are ready
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(KNOB_A_PARAM, 0.f, 1.f, 0.f, "");
        configParam(KNOB_B_PARAM, 0.f, 1.f, 0.f, "");
        configInput(LEFT_INPUT, "");
        configInput(RIGHT_INPUT, "");
        configInput(LEFT_CV_INPUT, "");
        configInput(RIGHT_CV_INPUT, "");
        configOutput(LEFT_OUTPUT, "");
        configOutput(RIGHT_OUTPUT, "");
        configOutput(LEFT_CV_OUTPUT, "");
        configOutput(RIGHT_CV_OUTPUT, "");
        handle_umfeld_app();
    }

    ~UmfeldModule() override {
        try {
            unload_app();
        } catch (Exception e) {
            UMFELD_VCV_LOG("could not unload app");
        }
    }

    void process(const ProcessArgs& args) override {
        if (mInitializeApp) {
            if (umfeld_app && fSettingsFunction) {
                fSettingsFunction(umfeld_app);
            }
            if (umfeld_app && fSetupFunction) {
                fSetupFunction(umfeld_app);
            }
            mInitializeApp = false;
        }

        if (params[RELOAD_PARAM].getValue() > mBangReloadButtonState) {
            mBeatCounter = 0;
            reload_app();
        }
        mBangReloadButtonState = params[RELOAD_PARAM].getValue();

        if (params[LOAD_APP_PARAM].getValue() > mBangLoadAppButtonState) {
            UMFELD_VCV_LOG("loading app");
            // Bug #8: use correct file filter for sketch libraries
            static const char SKETCH_FILTERS[] = "Umfeld Sketch:dylib,so,dll";
            osdialog_filters* filters           = osdialog_filters_parse(SKETCH_FILTERS);
            DEFER({ osdialog_filters_free(filters); });

            char* pathC = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
            if (pathC) {
                // Bug #1: actually load the sketch after file dialog
                std::string path = pathC;
                std::free(pathC);
                set_app_path(path);
                reload_app();
            }
        }
        mBangLoadAppButtonState = params[LOAD_APP_PARAM].getValue();

        // Bug #5: replaced per-sample heap allocation with accumulator pattern
        // Output previous block's samples; accumulate input; flush every SAMPLES_PER_AUDIO_BLOCK
        outputs[LEFT_OUTPUT].setVoltage(mLeftOutput[mSampleCollectorCounter] * 5.0f);
        outputs[RIGHT_OUTPUT].setVoltage(mRightOutput[mSampleCollectorCounter] * 5.0f);
        mLeftInput[mSampleCollectorCounter]  = inputs[LEFT_INPUT].getVoltage() / 5.0f;
        mRightInput[mSampleCollectorCounter] = inputs[RIGHT_INPUT].getVoltage() / 5.0f;
        mSampleCollectorCounter++;
        if (mSampleCollectorCounter >= SAMPLES_PER_AUDIO_BLOCK) {
            mSampleCollectorCounter = 0;
            if (umfeld_app && fAudioblockFunction) {
                float* channelInput[2]  = {mLeftInput, mRightInput};
                float* channelOutput[2] = {mLeftOutput, mRightOutput};
                fAudioblockFunction(umfeld_app, channelInput, channelOutput, SAMPLES_PER_AUDIO_BLOCK);
            }
        }

        mBeatTriggerCounter += args.sampleTime;
        if (mBeatTriggerCounter >= mBeatDurationSec) {
            mBeatTriggerCounter -= mBeatDurationSec;
            if (umfeld_app && fBeatFunction) {
                fBeatFunction(umfeld_app, mBeatCounter);
                mBeatCounter++;
            }
        }

        // Blink light at 1Hz
        static float blinkPhase = 0.0f;
        blinkPhase += args.sampleTime;
        if (blinkPhase >= 1.f) {
            blinkPhase -= 1.f;
        }
        lights[BLINK_LIGHT].setBrightness(blinkPhase < 0.5f ? 1.f : 0.f);

        if (umfeld_app && fEventFunction) {
            cv_event.data[LEFT_CV_INPUT_POS]  = inputs[LEFT_CV_INPUT].getVoltage();
            cv_event.data[RIGHT_CV_INPUT_POS] = inputs[RIGHT_CV_INPUT].getVoltage();
            cv_event.data[KNOB_PARAM_A_POS]   = params[KNOB_A_PARAM].getValue();
            cv_event.data[KNOB_PARAM_B_POS]   = params[KNOB_B_PARAM].getValue();
            fEventFunction(umfeld_app, cv_event.data, CVEvent::length);
            outputs[LEFT_CV_OUTPUT].setVoltage(cv_event.data[LEFT_CV_OUTPUT_POS]);
            outputs[RIGHT_CV_OUTPUT].setVoltage(cv_event.data[RIGHT_CV_OUTPUT_POS]);
        }
    }

    void call_draw() const {
        if (umfeld_app && fDrawFunction) {
            fDrawFunction(umfeld_app);
        }
    }

    void set_app_path(const std::string& path) {
        fCurrentAppPath = path;
    }

    template<typename T>
    static void load_symbol(void* handle, const char* symbol_name, T& function_ptr, const std::string& library_name) {
#if defined ARCH_WIN
        function_ptr = (T) GetProcAddress((HINSTANCE) handle, symbol_name);
#else
        function_ptr = (T) dlsym(handle, symbol_name);
#endif
        if (!function_ptr) {
            UMFELD_VCV_LOG("failed to read '%s' symbol in %s", symbol_name, library_name.c_str());
        } else {
            UMFELD_VCV_LOG("successfully read '%s' symbol in %s", symbol_name, library_name.c_str());
        }
    }

    typedef UmfeldApp*  (*CreateUmfeldFunctionPtr)();
    typedef void        (*DestroyFunctionPtr)(UmfeldApp*);
    typedef void        (*SettingsFunctionPtr)(UmfeldApp*);
    typedef void        (*SetupFunctionPtr)(UmfeldApp*);
    typedef void        (*DrawFunctionPtr)(UmfeldApp*);
    typedef void        (*BeatFunctionPtr)(UmfeldApp*, uint32_t);
    typedef void        (*AudioblockFunctionPtr)(UmfeldApp*, float**, float**, int);
    typedef const char* (*NameFunctionPtr)(UmfeldApp*);
    typedef void        (*EventFunctionPtr)(UmfeldApp*, float*, uint32_t);

    CreateUmfeldFunctionPtr fCreateUmfeldFunction  = nullptr;
    DestroyFunctionPtr      fDestroyUmfeldFunction = nullptr;
    SettingsFunctionPtr     fSettingsFunction       = nullptr;
    SetupFunctionPtr        fSetupFunction          = nullptr;
    DrawFunctionPtr         fDrawFunction           = nullptr;
    BeatFunctionPtr         fBeatFunction           = nullptr;
    AudioblockFunctionPtr   fAudioblockFunction     = nullptr;
    NameFunctionPtr         fNameFunction           = nullptr;
    EventFunctionPtr        fEventFunction          = nullptr;

    void load_symbols() {
        const std::string mAppName = system::getFilename(fCurrentAppPath);
        load_symbol(mHandleUmfeldSketch, "create_umfeld", fCreateUmfeldFunction, mAppName);
        load_symbol(mHandleUmfeldSketch, "destroy_umfeld", fDestroyUmfeldFunction, mAppName);
        load_symbol(mHandleUmfeldSketch, "settings", fSettingsFunction, mAppName);
        load_symbol(mHandleUmfeldSketch, "setup", fSetupFunction, mAppName);
        load_symbol(mHandleUmfeldSketch, "draw", fDrawFunction, mAppName);
        load_symbol(mHandleUmfeldSketch, "beat", fBeatFunction, mAppName);
        load_symbol(mHandleUmfeldSketch, "audioblock", fAudioblockFunction, mAppName);
        load_symbol(mHandleUmfeldSketch, "name", fNameFunction, mAppName);
        load_symbol(mHandleUmfeldSketch, "event", fEventFunction, mAppName);
    }

    bool check_app_path(const std::string& path) {
        return !path.empty() && system::isFile(path);
    }

    std::string get_default_app_path() {
        std::string mFullDefaultAppPath;
#if defined ARCH_LIN
        mFullDefaultAppPath = pluginInstance->path + "/dep/lib" + mDefaultApp + ".so";
#elif defined ARCH_WIN
        mFullDefaultAppPath = pluginInstance->path + "/dep/lib" + mDefaultApp + ".dll";
#elif defined ARCH_MAC  // Bug #7: was `#elif ARCH_MAC` (missing `defined`)
        mFullDefaultAppPath = pluginInstance->path + "/dep/lib" + mDefaultApp + ".dylib";
#endif
        return mFullDefaultAppPath;
    }

    void load_app() {
        if (!check_app_path(fCurrentAppPath)) {
            UMFELD_VCV_LOG("resetting app to default: %s", mDefaultApp.c_str());
            fCurrentAppPath = get_default_app_path();
        }

        UMFELD_VCV_LOG("loading app from file: %s", fCurrentAppPath.c_str());
#if defined ARCH_WIN
        SetErrorMode(SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS);
        HINSTANCE handle = LoadLibrary(mCurrentAppPath.c_str());
        SetErrorMode(0);
        if (!handle) {
            int error = GetLastError();
            throw Exception(string::f("Failed to load library %s: code %d", mCurrentAppPath.c_str(), error));
        }
#else
        void* handle = dlopen(fCurrentAppPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            throw Exception(string::f("Failed to load library %s: %s", fCurrentAppPath.c_str(), dlerror()));
        }
#endif
        mHandleUmfeldSketch = handle;
    }

    void unload_app() {
        if (umfeld_app) {
            UMFELD_VCV_LOG("unloading app: %p ", mHandleUmfeldSketch);
            destroy_app();

            // Bug #3: null ALL 9 function pointers before dlclose to prevent use-after-free
            fCreateUmfeldFunction  = nullptr;
            fDestroyUmfeldFunction = nullptr;
            fSettingsFunction      = nullptr;
            fSetupFunction         = nullptr;
            fDrawFunction          = nullptr;
            fBeatFunction          = nullptr;
            fAudioblockFunction    = nullptr;
            fNameFunction          = nullptr;
            fEventFunction         = nullptr;

            if (mHandleUmfeldSketch) {
#if defined ARCH_WIN
                FreeLibrary((HINSTANCE) mHandleUmfeldSketch);
#else
                dlclose(mHandleUmfeldSketch);
#endif
                mHandleUmfeldSketch = nullptr;
            }
            umfeld_app = nullptr;
        }
    }

    const char* get_name() const {
        return fNameFunction == nullptr ? DEFAULT_NAME : fNameFunction(umfeld_app);
    }

    const char* DEFAULT_NAME = "NOOP";

    void update_app_name() {
        if (umfeld_app && fNameFunction && mTextFieldAppName) {
            mTextFieldAppName->text = fNameFunction(umfeld_app);
        }
    }

    void create_app() {
        if (fCreateUmfeldFunction) {
            umfeld_app = fCreateUmfeldFunction();
        }
    }

    void destroy_app() {
        if (umfeld_app && fDestroyUmfeldFunction) {
            UMFELD_VCV_LOG("destroying app: %s", get_name());
            fDestroyUmfeldFunction(umfeld_app);
            if (mTextFieldAppName) {
                mTextFieldAppName->text = DEFAULT_NAME;
            }
        }
    }

    void reload_app() {
        handle_umfeld_app();
        mInitializeApp      = true;
        mSampleCollectorCounter = 0;
    }

private:
    UmfeldApp* umfeld_app = nullptr;
    CVEvent    cv_event;
};

struct UmfeldWidget : OpenGlWidget {

    UmfeldModule* module = nullptr;

    void step() override {
        // Render every frame
        dirty = true;
        FramebufferWidget::step();
    }

    void appendContextMenu(Menu* menu) {
        // TODO: add Load Sketch, Reload, and sketch info items
        UMFELD_VCV_LOG("appendContextMenu");
    }

    void onPathDrop(const PathDropEvent& e) override {
        if (e.paths.empty()) {
            return;
        }
        const std::string path = e.paths[0];
        UMFELD_VCV_LOG("onPathDrop: %s", path.c_str());
        if (module) {
            // Bug #2: actually trigger reload after setting path
            module->set_app_path(path);
            module->reload_app();
        }
        e.consume(this);
    }

    void drawFramebuffer() override {
        math::Vec fbSize = getFramebufferSize();
        glViewport(0, 0, (GLsizei) fbSize.x, (GLsizei) fbSize.y);

        // Bug #6: removed glMatrixMode/glOrtho/glScalef/glTranslatef — those are
        // fixed-function GL unavailable in OpenGL 3.2 Core Profile (macOS).
        // PGraphicsOpenGL manages its own projection via shaders.

        // Save GL state that NanoVG may depend on (Core Profile safe — no glPushAttrib)
        GLint prevFbo, prevVao, prevProgram;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

        if (module) {
            module->call_draw();
        } else {
            UMFELD_VCV_LOG("module not set");
        }

        // Restore GL state for NanoVG
        glBindVertexArray(prevVao);
        glUseProgram(prevProgram);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    }

    void onHover(const HoverEvent& e) override {}

    void onButton(const ButtonEvent& e) override {
        UMFELD_VCV_LOG("onButton: button: %i, action: %i", e.button, e.action);
    }

    void onHoverScroll(const HoverScrollEvent& e) override {
        UMFELD_VCV_LOG("onMouseScroll: delta: %f, %f", e.scrollDelta.x, e.scrollDelta.y);
    }

    void onHoverKey(const HoverKeyEvent& e) override {
        if (e.action == GLFW_PRESS) {
            UMFELD_VCV_LOG("keyPress    : key: %s", e.keyName.c_str());
        } else if (e.action == GLFW_RELEASE) {
            UMFELD_VCV_LOG("keyReleased : key: %s", e.keyName.c_str());
        } else if (e.action == GLFW_REPEAT) {
            UMFELD_VCV_LOG("keyRepeat   : key: %s", e.keyName.c_str());
        }
    }
};

struct UmfeldModuleWidget : ModuleWidget {
    explicit UmfeldModuleWidget(UmfeldModule* module) {
        UMFELD_VCV_LOG("Umfeld × VCV Rack");

        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/UmfeldModule.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        constexpr float M_GRID_SIZE            = 15.24;
        constexpr float M_GRID_ROW_1           = M_GRID_SIZE;
        constexpr float M_GRID_ROW_2           = 26.529;
        constexpr float M_GRID_CONNECTOR_SPACE = 10.127;

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(M_GRID_ROW_1, M_GRID_SIZE * 2)), module, UmfeldModule::LEFT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(M_GRID_ROW_2, M_GRID_SIZE * 2)), module, UmfeldModule::RIGHT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(M_GRID_ROW_1, M_GRID_SIZE * 2 + M_GRID_CONNECTOR_SPACE)), module, UmfeldModule::LEFT_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(M_GRID_ROW_2, M_GRID_SIZE * 2 + M_GRID_CONNECTOR_SPACE)), module, UmfeldModule::RIGHT_CV_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(M_GRID_ROW_1, M_GRID_SIZE * 6)), module, UmfeldModule::LEFT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(M_GRID_ROW_2, M_GRID_SIZE * 6)), module, UmfeldModule::RIGHT_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(M_GRID_ROW_1, M_GRID_SIZE * 6 + M_GRID_CONNECTOR_SPACE)), module, UmfeldModule::LEFT_CV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(M_GRID_ROW_2, M_GRID_SIZE * 6 + M_GRID_CONNECTOR_SPACE)), module, UmfeldModule::RIGHT_CV_OUTPUT));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(M_GRID_ROW_1, M_GRID_SIZE * 4)), module, UmfeldModule::KNOB_A_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(M_GRID_ROW_2, M_GRID_SIZE * 4)), module, UmfeldModule::KNOB_B_PARAM));
        addParam(createParamCentered<CKD6>(mm2px(Vec(M_GRID_ROW_1, M_GRID_SIZE * 4 + M_GRID_CONNECTOR_SPACE)), module, UmfeldModule::RELOAD_PARAM));
        addParam(createParamCentered<CKD6>(mm2px(Vec(M_GRID_ROW_2, M_GRID_SIZE * 4 + M_GRID_CONNECTOR_SPACE)), module, UmfeldModule::LOAD_APP_PARAM));

        if (module) {
            module->mTextFieldAppName            = createWidget<LedDisplayTextField>(mm2px(Vec(32.595, 107.466)));
            module->mTextFieldAppName->box.size  = mm2px(Vec(M_GRID_SIZE * 3.5f, M_GRID_SIZE * 0.66f));
            module->mTextFieldAppName->multiline = false;
            module->mTextFieldAppName->color     = color::BLACK;
            module->mTextFieldAppName->bgColor   = color::YELLOW;
            addChild(module->mTextFieldAppName);
            module->update_app_name();
        }

        auto* display   = new UmfeldWidget();
        display->module = module;

        Vec mDisplaySize     = mm2px(Vec(109.615, 74.774));
        Vec mDisplayPosition = mm2px(Vec(32.595, 21.684));
        display->setSize(mDisplaySize);
        display->setPosition(mDisplayPosition);
        addChild(display);
    }
};


Model* modelUmfeldModule = createModel<UmfeldModule, UmfeldModuleWidget>("UmfeldModule");
