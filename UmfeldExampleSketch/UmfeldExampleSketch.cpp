#include <iostream>
#include "Umfeld.h"

using namespace umfeld;

class UmfeldApp : public PApplet {

    PFont* mFont   = nullptr;
    int    mWidth  = static_cast<int>(323.666);
    int    mHeight = static_cast<int>(220.789);

    void settings() override {
        size(mWidth, mHeight);
        audio_input_device    = DEFAULT_AUDIO_DEVICE;
        audio_output_device   = DEFAULT_AUDIO_DEVICE;
        audio_input_channels  = DEFAULT_NUMBER_OF_INPUT_CHANNELS;
        audio_output_channels = DEFAULT_NUMBER_OF_OUTPUT_CHANNELS;
    }

    void setup() override {
        const std::vector search_paths = {
            get_executable_location() + "../..",
            get_executable_location() + "..",
            get_executable_location() + "."};
        const std::string mFontFile = find_file_in_paths(search_paths, "RobotoMono-Regular.ttf");
        if (!mFontFile.empty()) {
            std::cout << "found font at: " << mFontFile << std::endl;
            mFont = loadFont(mFontFile, 200);
            textFont(mFont);
        }

        println("width : ", width);
        println("height: ", height);
    }

    void draw() override {
        background(1);

        stroke(0);
        noFill();
        rect(10, 10, width / 2 - 20, height / 2 - 20);

        noStroke();
        fill(random(0, 0.2));
        rect(20, 20, width / 2 - 40, height / 2 - 40);

        fill(0, 0.5f, 1);
        text("23", 20, height - 20);

        fill(0, 0.5f, 1);
        circle(0, 0, 20);
        circle(width, 0, 20);
        circle(width, height, 20);
        circle(0, height, 20);

        circle(width / 2 - 15, height / 2 + mKnobA * 100, mCircleDiameterA);
        circle(width / 2 + 15, height / 2 + mKnobB * 100, mCircleDiameterB);
    }

    void audioblock(float** input, float** output, int length) override {
        for (int i = 0; i < length; i++) {
            float sample = random(-0.1, 0.1);
            for (int j = 0; j < audio_output_channels; ++j) { output[j][i] = sample; }
        }
    }

    void beat(uint32_t beat_count) override {
        // println("beat: ", beat_count);
    }

    void keyPressed() override {
        if (key == 'q') { exit(); }
        println((char) key, " pressed");
    }

    enum { LEFT_CV_INPUT_POS,
           RIGHT_CV_INPUT_POS,
           LEFT_CV_OUTPUT_POS,
           RIGHT_CV_OUTPUT_POS,
           KNOB_PARAM_A_POS,
           KNOB_PARAM_B_POS,
           CV_POS_LEN };

    float mCircleDiameterA = 0;
    float mCircleDiameterB = 0;
    float mKnobA           = 0;
    float mKnobB           = 0;

    void event(float* data, const uint32_t length) override {
        if (length == CV_POS_LEN) {
            mCircleDiameterA = map(data[LEFT_CV_INPUT_POS], -5, 5, 10, 30);
            mCircleDiameterB = map(data[RIGHT_CV_INPUT_POS], -5, 5, 10, 30);
            mKnobA           = data[KNOB_PARAM_A_POS];
            mKnobB           = data[KNOB_PARAM_B_POS];
        }
    }

    const char* name() override {
        return "Umfeld42";
    }
};

PApplet* umfeld::instance() { return new UmfeldApp(); }
