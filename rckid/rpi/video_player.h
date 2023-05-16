#pragma once

#include "utils/process.h"

#include "widget.h"
#include "window.h"

/** A video player, frontend to cvlc controlled the same way as retroarch is. 
 
    Play/Paue = btnA
    Dpad Left/Right = 10 seconds back/forward
    Left/Right = 1 minute back/forward
 */
class VideoPlayer : public Widget {
public:

    VideoPlayer(Window * window): Widget{window} {}

    bool fullscreen() const { return true; }

protected:

    void draw() override {
        if (player_.done())
            window()->back();
    }

    void onNavigationPush() override {
        player_ = utils::Process::capture(utils::Command{"cvlc", { "-I", "rc", "/rckid/videos/test.mkv"}});
        playing_ = true;
        window()->enableBackground(false);
    }

    void onNavigationPop() override {
        if (!player_.done())
            player_.kill();
        window()->enableBackground(true);
    }

    void onFocus() {
        if (!playing_) {
            playing_ = true;
            player_.tx("play\n");
        }
    }

    void onBlur() {
        if (playing_) {
            playing_ = false;
            player_.tx("pause\n");
        }
    }

    // play/pause
    void btnA(bool state) override {
        if (state) {
            if (playing_) {
                playing_ = false;
                player_.tx("pause\n");
            } else {
                playing_ = true;
                player_.tx("play\n");
            }
        }
    }

    // back 10 seconds
    void dpadLeft(bool state) override {
        if (state)
            player_.tx("seek -10");
    }

    // forward 10 seconds
    void dpadRight(bool state) override {
        if (state)
            player_.tx("seek +10");
    }

    // back one minute
    void btnL(bool state) override {
        if (state)
            player_.tx("seek -60");
    }

    // forward one minute
    void btnR(bool state) override {
        if (state)
            player_.tx("seek +60");
    }

    utils::Process player_;
    bool playing_ = false;
    
}; // Video