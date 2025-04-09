#include <dlfcn.h>
#include <iostream>
#include <osdialog.h>
#include "plugin.hpp"

#define UMGB_VCV_DEBUG
#ifdef UMGB_VCV_DEBUG
#define UMGB_VCV_LOG(...) \
    printf("\033[32m");   \
    printf("+++ ");       \
    printf(__VA_ARGS__);  \
    printf("\033[0m");    \
    printf("\n");
#else
#define UMGB_VCV_LOG(...)
#endif

// uint8_t mInstanceCounter = 0;

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
    const std::string    mDefaultApp         = "UmfeldApp";
    LedDisplayTextField* mTextFieldAppName   = nullptr;
    bool                 mInitializeApp      = true;
    bool                 mAllocateAudioblock = true;

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
            UMGB_VCV_LOG("could not unload app");
        }
        try {
            load_app();
        } catch (Exception e) {
            UMGB_VCV_LOG("could not load app");
        }
        try {
            load_symbols();
        } catch (Exception e) {
            UMGB_VCV_LOG("could not load symbols");
        }
        create_app();
        update_app_name();
    }

    UmfeldModule() {
        // mInstanceCounter++;
        // if (mInstanceCounter > 1) {
        //     UMGB_VCV_LOG("mInstanceCounter: %i", mInstanceCounter);
        //     UMGB_VCV_LOG("WARNING multiple instances of plugins are currently not supported.");
        //     // throw Exception(string::f("multiple instances of plugins are currently not supported."));
        // }
        handle_umfeld_app();

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
    }

    ~UmfeldModule() override {
        try {
            unload_app();
        } catch (Exception e) {
            UMGB_VCV_LOG("could not unload app");
        }
        // if (mInstanceCounter > 0) {
        //     mInstanceCounter--;
        // }
    }

    void process_audio(const int length) {
        constexpr int numChannels = 2;
        auto**        input       = new float*[numChannels];
        auto**        output      = new float*[numChannels];

        for (int i = 0; i < numChannels; ++i) {
            input[i]  = new float[length];
            output[i] = new float[length];
        }

        for (int sample = 0; sample < length; ++sample) {
            // TODO this must be read from the input buffer
            input[0][sample]  = inputs[LEFT_INPUT].getVoltage() / 5.0f;
            input[1][sample]  = inputs[RIGHT_INPUT].getVoltage() / 5.0f;
            output[0][sample] = 0.0f; // Initialize output to 0
            output[1][sample] = 0.0f;
        }

        if (fAudioblockFunction) {
            fAudioblockFunction(umfeld_app, input, output, length);
        }

        for (int sample = 0; sample < length; ++sample) {
            // TODO this must be written to the output buffer
            outputs[LEFT_OUTPUT].setVoltage(output[0][sample] * 5.0f);
            outputs[RIGHT_OUTPUT].setVoltage(output[1][sample] * 5.0f);
        }

        for (int i = 0; i < numChannels; ++i) {
            delete[] input[i];
            delete[] output[i];
        }
        delete[] input;
        delete[] output;
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
            UMGB_VCV_LOG("loading app");
            // from Wavetable.hpp
            static const char WAVETABLE_FILTERS[] = "WAV (.wav):wav,WAV;Raw:f32,i8,i16,i24,i32,*";
            osdialog_filters* filters             = osdialog_filters_parse(WAVETABLE_FILTERS);
            DEFER({ osdialog_filters_free(filters); });

            // std::string       wavetableDir;
            char* pathC = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
            // char* pathC = osdialog_file(OSDIALOG_OPEN, wavetableDir.empty() ? NULL : wavetableDir.c_str(), NULL, filters);
            if (!pathC) {
                // Fail silently
                return;
            }
            std::string path = pathC;
            std::free(pathC);
            std::string wavetableDir = system::getDirectory(path);
            // load(path);
            std::string filename;
            filename = system::getFilename(path);
            UMGB_VCV_LOG("dir path: %s", wavetableDir.c_str());
            UMGB_VCV_LOG("filename: %s", filename.c_str());
        }
        mBangLoadAppButtonState = params[LOAD_APP_PARAM].getValue();

        // outputs[LEFT_OUTPUT].setVoltage(inputs[LEFT_INPUT].getVoltage());  // pass through
        // outputs[RIGHT_OUTPUT].setVoltage(inputs[RIGHT_INPUT].getVoltage()); // pass through

        //         float mSampleLeft                    = mLeftOutput[mSampleCollectorCounter];
        //         float mSampleRight                   = mRightOutput[mSampleCollectorCounter];
        //         mLeftInput[mSampleCollectorCounter]  = inputs[LEFT_IN_INPUT].getVoltage() / 5.f;
        //         mRightInput[mSampleCollectorCounter] = inputs[RIGHT_IN_INPUT].getVoltage() / 5.f;
        //         mSampleCollectorCounter++;
        //         if (mSampleCollectorCounter >= SAMPLES_PER_AUDIO_BLOCK) {
        //             mSampleCollectorCounter = 0;
        //             mAudioblockCallback(mLeftOutput, mRightOutput, mLeftInput, mRightInput);
        //         }

        //         std::cout << "inputs[LEFT_INPUT] : " << inputs[LEFT_INPUT].getVoltage() << std::endl;
        //         std::cout << "inputs[RIGHT_INPUT]: " << inputs[RIGHT_INPUT].getVoltage() << std::endl;
        //         std::cout << "params[PITCH_PARAM]: " << params[PITCH_PARAM].getValue() << std::endl;
        //         std::cout << std::endl;

        // TODO implement audio processing with SAMPLES_PER_AUDIO_BLOCK greater than 1 ;)
        process_audio(1);

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
            // TODO only send if connected to CV … would be nice to notify the sketch of which CV is connected
            // if (inputs[LEFT_CV_INPUT].isConnected() ||
            //     inputs[RIGHT_CV_INPUT].isConnected() ||
            //     outputs[LEFT_CV_OUTPUT].isConnected() ||
            //     outputs[RIGHT_CV_OUTPUT].isConnected()) {
            cv_event.data[LEFT_CV_INPUT_POS]  = inputs[LEFT_CV_INPUT].getVoltage();
            cv_event.data[RIGHT_CV_INPUT_POS] = inputs[RIGHT_CV_INPUT].getVoltage();
            cv_event.data[KNOB_PARAM_A_POS]   = params[KNOB_A_PARAM].getValue();
            cv_event.data[KNOB_PARAM_B_POS]   = params[KNOB_B_PARAM].getValue();
            fEventFunction(umfeld_app, cv_event.data, CVEvent::length);
            outputs[LEFT_CV_OUTPUT].setVoltage(cv_event.data[LEFT_CV_OUTPUT_POS]);
            outputs[RIGHT_CV_OUTPUT].setVoltage(cv_event.data[RIGHT_CV_OUTPUT_POS]);
            // }
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
            UMGB_VCV_LOG("failed to read '%s' symbol in %s", symbol_name, library_name.c_str());
        } else {
            UMGB_VCV_LOG("successfully read '%s' symbol in %s", symbol_name, library_name.c_str());
        }
    }

    typedef UmfeldApp* (*CreateUmfeldFunctionPtr)();
    typedef void         (*DestroyFunctionPtr)(UmfeldApp*);
    typedef void         (*SetupFunctionPtr)(UmfeldApp*);
    typedef void         (*DrawFunctionPtr)(UmfeldApp*);
    typedef void         (*BeatFunctionPtr)(UmfeldApp*, uint32_t);
    typedef void         (*AudioblockFunctionPtr)(UmfeldApp*, float**, float**, int);
    typedef const char*  (*NameFunctionPtr)(UmfeldApp*);
    typedef void         (*EventFunctionPtr)(UmfeldApp*, float*, uint32_t);

    CreateUmfeldFunctionPtr fCreateUmfeldFunction  = nullptr;
    DestroyFunctionPtr        fDestroyUmfeldFunction = nullptr;
    SetupFunctionPtr          fSettingsFunction        = nullptr;
    SetupFunctionPtr          fSetupFunction           = nullptr;
    DrawFunctionPtr           fDrawFunction            = nullptr;
    BeatFunctionPtr           fBeatFunction            = nullptr;
    AudioblockFunctionPtr     fAudioblockFunction      = nullptr;
    NameFunctionPtr           fNameFunction            = nullptr;
    EventFunctionPtr          fEventFunction           = nullptr;

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
#elif ARCH_MAC
        mFullDefaultAppPath = pluginInstance->path + "/dep/lib" + mDefaultApp + ".dylib";
#endif
        return mFullDefaultAppPath;
    }

    void load_app() {
        if (!check_app_path(fCurrentAppPath)) {
            UMGB_VCV_LOG("resetting app to default: %s", mDefaultApp.c_str());
            fCurrentAppPath = get_default_app_path();
        }

        /* Load app library */
        UMGB_VCV_LOG("loading app from file: %s", fCurrentAppPath.c_str());
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
            /* close the library */
            UMGB_VCV_LOG("unloading app: %p ", mHandleUmfeldSketch);
            destroy_app();

            fCreateUmfeldFunction  = nullptr;
            fDestroyUmfeldFunction = nullptr;
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
            UMGB_VCV_LOG("destroying app: %s", get_name());
            fDestroyUmfeldFunction(umfeld_app);
            if (mTextFieldAppName) {
                mTextFieldAppName->text = DEFAULT_NAME;
            }
        }
    }

    void reload_app() {
        handle_umfeld_app();
        mInitializeApp = true;
    }

private:
    UmfeldApp* umfeld_app = nullptr;
    CVEvent      cv_event;
};

struct UmfeldWidget : OpenGlWidget {

    UmfeldModule* module = nullptr;

    void step() override {
        // Render every frame
        dirty = true;
        FramebufferWidget::step();
    }

    void appendContextMenu(Menu* menu) {
        UMGB_VCV_LOG("appendContextMenu");
    }

    void onPathDrop(const PathDropEvent& e) override {
        // if (!module) {
        //     return;
        // }
        if (e.paths.empty()) {
            return;
        }
        const std::string path = e.paths[0];
        // if (system::getExtension(path) != ".wav") {
        //     return;
        // }
        // module->wavetable.load(path);
        // module->wavetable.filename = system::getFilename(path);
        UMGB_VCV_LOG("onPathDrop: %s", path.c_str());
        // UMGB_VCV_LOG("          : %s", system::getFilename(path).c_str());
        module->set_app_path(path);
        e.consume(this);
    }

    void drawFramebuffer() override {
        math::Vec fbSize = getFramebufferSize();
        glViewport(0.0, 0.0, fbSize.x, fbSize.y);

        // glClearColor(0, 0, 0, 1.0);
        // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        // glClearColor(module->bg_color_red, module->bg_color_green, module->bg_color_blue, 1.0);
        // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, fbSize.x, 0.0, fbSize.y, -1.0, 1.0);
        glScalef(1.0, -1.0, 1.0);
        glTranslatef(0.0, -fbSize.y, 0.0);

        //         std::cout << "fbSize: " << fbSize.x << ", " << fbSize.y << std::endl; // 317, 245

        // constexpr float padding = 2 * 15.24;
        // glColor3f(1, 1, 1);
        // glBegin(GL_TRIANGLES);
        // glVertex3f(padding, padding, 0);
        // glVertex3f(fbSize.x - padding, padding, 0);
        // glVertex3f(padding, fbSize.y - padding, 0);
        // glVertex3f(fbSize.x - padding, fbSize.y - padding, 0);
        // glVertex3f(fbSize.x - padding, padding, 0);
        // glVertex3f(padding, fbSize.y - padding, 0);
        // glEnd();

        if (module) {
            module->call_draw();
        } else {
            UMGB_VCV_LOG("module not set");
        }
    }

    /** see `Widget.hpp` for all events */

    // look into `recursePositionEvent(&Widget::onXXX, e);`

    void onHover(const HoverEvent& e) override {
        /* occurs when mouse is hovering over widget */
        // UMGB_VCV_LOG("onHover: %f, %f p:(%f, %f)", e.pos.x, e.pos.y, e.mouseDelta.x, e.mouseDelta.y);
    }

    void onButton(const ButtonEvent& e) override {
        /* occurs when mouse is pressed or released over widget */
        // button: GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE
        // action: GLFW_PRESS or GLFW_RELEASE
        UMGB_VCV_LOG("onButton: button: %i, action: %i", e.button, e.action);
    }

    void onHoverScroll(const HoverScrollEvent& e) override {
        UMGB_VCV_LOG("onMouseScroll: delta: %f, %f", e.scrollDelta.x, e.scrollDelta.y);
    }

    void onHoverKey(const HoverKeyEvent& e) override {
        // action: GLFW_RELEASE, GLFW_PRESS, GLFW_REPEAT, or RACK_HELD
        if (e.action == GLFW_PRESS) {
            UMGB_VCV_LOG("keyPress    : key: %s", e.keyName.c_str());
        } else if (e.action == GLFW_RELEASE) {
            UMGB_VCV_LOG("keyReleased : key: %s", e.keyName.c_str());
        } else if (e.action == GLFW_REPEAT) {
            UMGB_VCV_LOG("keyRepeat   : key: %s", e.keyName.c_str());
        } else if (e.action == RACK_HELD) {
            // UMGB_VCV_LOG("keyHeld     : key: %s", e.keyName.c_str());
        } else {
            UMGB_VCV_LOG("onHoverKey  : key: %c, scancode: %i, keyName: %s, action: %i, mods: %i", e.key, e.scancode, e.keyName.c_str(), e.action, e.mods);
        }
    }

    /* these methods below seem to have no effect: */
    // void onHoverText(const HoverTextEvent& e) override { UMGB_VCV_LOG("onHoverText: codepoint: %i", e.codepoint); }
    // void onEnter(const EnterEvent& e) override { UMGB_VCV_LOG("onEnter"); }
    // void onLeave(const LeaveEvent& e) override { UMGB_VCV_LOG("onLeave"); }
    // void onSelect(const SelectEvent& e) override { UMGB_VCV_LOG("onSelect"); }
    // void onDragStart(const DragStartEvent& e) override { UMGB_VCV_LOG("onDragStart"); }
    // void onDragEnd(const DragEndEvent& e) override { UMGB_VCV_LOG("onDragEnd"); }
    // void onDragDrop(const DragDropEvent& e) override { UMGB_VCV_LOG("onDragDrop"); }
};

struct UmfeldModuleWidget : ModuleWidget {
    explicit UmfeldModuleWidget(UmfeldModule* module) {
        UMGB_VCV_LOG("Umfeld × VCV Rack");

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

        std::cout << "RACK_GRID_WIDTH: " << RACK_GRID_WIDTH << std::endl;
        std::cout << "box.pos        : " << box.pos.x << ", " << box.pos.y << std::endl;
        std::cout << "box.size       : " << box.size.x << ", " << box.size.y << std::endl;

        display->box.pos  = Vec(M_GRID_SIZE, (RACK_GRID_HEIGHT / 8) - 10);
        display->box.size = Vec(box.size.x - 190, RACK_GRID_HEIGHT - 80);

        std::cout << "display->box.pos: " << display->box.pos.x << ", " << display->box.pos.y << std::endl;
        std::cout << "display->box.size: " << display->box.size.x << ", " << display->box.size.y << std::endl;

        /*
        box.pos: 0, 0
        box.size: 450, 380
        display->box.pos: 15.24, 37.5
        display->box.size: 260, 300
        mDisplaySize: 323.666, 220.789
        */

        Vec mDisplaySize     = mm2px(Vec(109.615, 74.774)); // actual sketch size 323.666, 220.789
        Vec mDisplayPosition = mm2px(Vec(32.595, 21.684));
        std::cout << "mDisplaySize    : " << mDisplaySize.x << ", " << mDisplaySize.y << std::endl;
        std::cout << "mDisplayPosition: " << mDisplayPosition.x << ", " << mDisplayPosition.y << std::endl;

        display->setSize(mDisplaySize);
        display->setPosition(mDisplayPosition);
        // display->setSize(Vec(158 * 2, 122 * 2)); // fbSize 1580,1220
        // display->setPosition(Vec(M_GRID_SIZE * 6, M_GRID_SIZE * 3));
        addChild(display);
        std::cout << "framebuffer size: " << display->getFramebufferSize().x << ", " << display->getFramebufferSize().y << std::endl;
    }
};


Model* modelUmfeldModule = createModel<UmfeldModule, UmfeldModuleWidget>("UmfeldModule");
