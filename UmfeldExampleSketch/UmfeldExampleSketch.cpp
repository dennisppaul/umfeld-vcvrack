#include <iostream>
#include "Umfeld.h"

using namespace umfeld;

PFont* mFont   = nullptr;
int    mWidth  = static_cast<int>(323.666);
int    mHeight = static_cast<int>(220.789);

float mCircleDiameterA = 0;
float mCircleDiameterB = 0;
float mKnobA           = 0;
float mKnobB           = 0;

void settings() {
    size(mWidth, mHeight);
    AudioUnitInfo info;
    info.input_channels  = 0;
    info.output_channels = 2;
    info.threaded        = false;
    audio(info);
}

void setup() {
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

void draw() {
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

void audioEvent(const PAudio& audio) {
    float sample_buffer[audio.buffer_size];
    for (int i = 0; i < audio.buffer_size; i++) {
        float sample     = random(-0.1, 0.1);
        sample_buffer[i] = sample;
    }
    if (audio.output_channels == 2) {
        merge_interleaved_stereo(sample_buffer, sample_buffer, audio.output_buffer, audio.buffer_size);
    }
}

void beat(uint32_t beat_count) {
    // println("beat: ", beat_count);
}

void keyPressed() {
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

void event(float* data, const uint32_t length) {
    if (length == CV_POS_LEN) {
        mCircleDiameterA = map(data[LEFT_CV_INPUT_POS], -5, 5, 10, 30);
        mCircleDiameterB = map(data[RIGHT_CV_INPUT_POS], -5, 5, 10, 30);
        mKnobA           = data[KNOB_PARAM_A_POS];
        mKnobB           = data[KNOB_PARAM_B_POS];
    }
}

const char* name()  {
    return "Umfeld42";
}
