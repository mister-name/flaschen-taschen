// -*- mode: c++; c-basic-offset: 4; indent-tabs-mode: nil; -*-
//
// Pong game.
// Leonardo Romor <leonardo.romor@gmail.com>
//
//  ./pong-game localhost
//
// .. or set the environment variable FT_DISPLAY to not worry about it
//
//  export FT_DISPLAY=localhost
//  ./pong-game

#include "udp-flaschen-taschen.h"
#include "bdf-font.h"

#include <assert.h>
#include <iostream>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

// Size of the display. 9 x 7 crates.
#define DISPLAY_WIDTH  (9 * 5)
#define DISPLAY_HEIGHT (7 * 5)

#define PLAYER_ROWS 5
#define PLAYER_COLUMN 1

#define BALL_ROWS 1
#define BALL_COLUMN 1
#define BALL_SPEED 15

#define FRAME_RATE 60

struct termios orig_termios;
static void reset_terminal_mode() {
    tcsetattr(0, TCSANOW, &orig_termios);
}

static void set_conio_terminal_mode() {
    struct termios new_termios;

    /* take two copies - one for now, one for later */
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));

    /* register cleanup handler, and set the new terminal mode */
    atexit(reset_terminal_mode);
    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
}

static const char * player[PLAYER_ROWS] = {
    "#",
    "#",
    "#",
    "#",
    "#",
};

static const char * ball[PLAYER_ROWS] = {
    "#",
};

class Timer {
public:
    Timer() : start_(0) {}

    void Start() {
        struct timeval tp;
        gettimeofday(&tp, NULL);
        start_ = tp.tv_sec * 1000000 + tp.tv_usec;
    }

    float GetElapsedInMilliseconds() {
        struct timeval tp;
        gettimeofday(&tp, NULL);
        return (tp.tv_sec * 1000000 + tp.tv_usec - start_) / 1e3;
    }

    inline void clear() { start_ = 0; }

private:
    int64_t start_;
};

class Actor {
public:
    Actor(const char *representation[],
          const int width, const int height)
        : repr_(representation), width_(width), height_(height) {}

    float pos[2];
    float speed[2];

    void print_on_buffer(UDPFlaschenTaschen * frame_buffer);
    bool IsOverMe(float x, float y);

private:
    const char **repr_;
    const int width_;
    const int height_;

    Actor(); // no default constructor.
};


class PongGame {
public:
    PongGame(const int socket, const int width, const int height,
             const ft::Font &font);
    ~PongGame();

    void Start(const int fps);

private:
    // Evaluate t + 1 of the game, takes care of collisions with the players
    // and set the score in case of the ball goes in the border limit of the game.
    void sim(const float dt);

    // Simply project to pixels the pong world.
    void next_frame();

    // Reset the ball in 0 with random velocity
    void reset_ball();

private:
    const int width_;
    const int height_;
    const ft::Font &font_;

    bool just_started_;
    int help_countdown_;

    UDPFlaschenTaschen * frame_buffer_;
    Actor ball_;
    Actor p1_;
    Actor p2_;
    int score_[2];

    PongGame(); // no default constructor.
};

void Actor::print_on_buffer(UDPFlaschenTaschen * frame_buffer) {
    for (int row = 0; row < height_; ++row) {
        const char *line = repr_[row];
        for (int x = 0; line[x]; ++x) {
            if (line[x] != ' ') {
                frame_buffer->SetPixel(x + pos[0], row + pos[1],  Color(255,255,255));
            }
        }
    }
}

bool Actor::IsOverMe(float x, float y) {
    return ((x - pos[0]) >= 0 && (x - pos[0]) < width_ &&
            (y - pos[1]) >= 0 && (y - pos[1]) < height_);
}

PongGame::PongGame(const int socket, const int width, const int height,
                   const ft::Font &font)
    : width_(width), height_(height), font_(font),
      just_started_(true), help_countdown_(4 * FRAME_RATE),
      frame_buffer_(new UDPFlaschenTaschen(socket, width, height)),
      ball_(ball, BALL_COLUMN, BALL_ROWS),
      p1_(player, PLAYER_COLUMN, PLAYER_ROWS),
      p2_(player, PLAYER_COLUMN, PLAYER_ROWS) {

    bzero(score_, sizeof(score_));

    reset_ball();  // center ball.

    p1_.pos[0] = 2;
    p1_.pos[1] = (height_ - PLAYER_ROWS) / 2;

    p2_.pos[0] = width_ - 2;
    p2_.pos[1] = (height_ - PLAYER_ROWS) / 2;
}

PongGame::~PongGame() {
    delete frame_buffer_;
}

void PongGame::reset_ball() {
    ball_.pos[0] = width_/2;
    ball_.pos[1] = height_/2-1;

    // Generate random angle in the range of 1/4 tau
    const float theta = 2 * M_PI / 4 * (rand() % 100 - 50) / 100.0;
    const int direction = rand() % 2 == 0 ? -1 : 1;
    ball_.speed[0] = direction * BALL_SPEED * cos(theta);
    ball_.speed[1] = direction * BALL_SPEED * sin(theta);
}

void PongGame::Start(const int fps) {
    assert(fps > 0);

    Timer t;
    float dt = 0;
    char command;

    // Select stuff
    fd_set rfds;
    struct timeval tv;
    int retval;

    set_conio_terminal_mode();

    for (;;) {
        t.Start();

        // Assume the calculations to be instant for the sleep
        sim(dt);      // Sim the pong world and handle game logic
        next_frame(); // Clear the screen, set the pixels, and send them.

        // Handle keypress
        // TODO: make this a network event thing, so that people can play
        // over a network.
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 1e6 / (float) fps;
        retval = select(1, &rfds, NULL, NULL, &tv);

        if (retval == -1)
            std::cerr << "Error on select" << std::endl;
        else if (retval){
            read(0, &command, sizeof(char));
            switch (command) {
            case 's': case 'S':
                p1_.pos[1] +=1;
                break;
            case 'w': case 'W':
                p1_.pos[1] +=-1;
                break;
            case 'k': case 'K':
                p2_.pos[1] +=1;
                break;
            case 'i': case 'I':
                p2_.pos[1] +=-1;
                break;
            case '?': case '/':
                help_countdown_ = 2 * FRAME_RATE;
                break;
            case 'q':
                return;
            }
        }
        dt = t.GetElapsedInMilliseconds();
    }
}

void PongGame::sim(const float dt) {
    if (help_countdown_ > 0) --help_countdown_;

    // When we just started, we just show initial help.
    if (just_started_) {
        if (help_countdown_ == 0) {
            just_started_ = false;
        } else {
            return;
        }
    }

    // Evaluate the new ball delta
    ball_.pos[0] += dt * ball_.speed[0] / 1e3;
    ball_.pos[1] += dt * ball_.speed[1] / 1e3;

    // Check if bouncin on a player cursor, (careful, for low fps
    // QUANTUM TUNELING may arise)
    if( p1_.IsOverMe(ball_.pos[0], ball_.pos[1]) || p2_.IsOverMe(ball_.pos[0], ball_.pos[1])) {
        ball_.speed[0] *= -1;
        ball_.pos[0] += 2 * dt * ball_.speed[0] / 1e3;
    }

    // Wall
    if( ball_.pos[1] < 0 || ball_.pos[1] > height_ ) ball_.speed[1] *= -1;

    // Score
    if (ball_.pos[0] < 0) {
        score_[1] += 1;
        ball_.pos[0] = width_ / 2;
        ball_.pos[1] = height_ / 2;
        reset_ball();
    } else if (ball_.pos[0] > width_){
        score_[0] += 1;
        ball_.pos[0] = width_ / 2;
        ball_.pos[1] = height_ / 2;
        reset_ball();
    }
}

void PongGame::next_frame() {
    // Clear the frame
    frame_buffer_->Clear();

    // Centerline
    const Color gray(100, 100, 100);
    for (int y = 0; y < height_/2; ++y) {
        frame_buffer_->SetPixel(width_ / 2, 2 * y, gray);
    }

    // We have crates that are 5 pixels. We want the number aligned to the left
    // and right of the center crate.
    const int center_start = width_ / (2 * 5) * 5;
    const Color score_color(0, 0, 255);

    char score_string[5];
    int text_len = snprintf(score_string, sizeof(score_string), "%d", score_[0]);
    ft::DrawText(frame_buffer_, font_,
                 center_start - 5*text_len, 4,  // Right aligned
                 score_color, NULL, score_string);

    text_len = snprintf(score_string, sizeof(score_string), "%d", score_[1]);
    ft::DrawText(frame_buffer_, font_,
                 center_start + 5, 4,
                 score_color, NULL, score_string);

    if (help_countdown_) {
        const int kFadeDuration = 60;
        const float fade_fraction = (help_countdown_ < kFadeDuration
                               ? 1.0 * help_countdown_ / kFadeDuration
                               : 1.0);
        const Color help_col(fade_fraction * 255, fade_fraction * 255, 0);
        ft::DrawText(frame_buffer_, font_, 0, 5 - 1, help_col, NULL, "W");
        ft::DrawText(frame_buffer_, font_, 0, height_ - 1, help_col, NULL, "S");

        ft::DrawText(frame_buffer_, font_, width_-5, 5 - 1, help_col, NULL, "I");
        ft::DrawText(frame_buffer_, font_, width_-5, height_ - 1, help_col, NULL, "K");
    }

    // Print the actors
    ball_.print_on_buffer(frame_buffer_);
    p1_.print_on_buffer(frame_buffer_);
    p2_.print_on_buffer(frame_buffer_);

    // Push the frame
    frame_buffer_->Send();
}


int main(int argc, char *argv[]) {
    const char *hostname = NULL;   // Will use default if not set otherwise.
    if ( argc > 1 ) {
        hostname = argv[1];        // Hostname can be supplied as first arg
    }

    ft::Font font;
    font.LoadFont("fonts/5x5.bdf");  // TODO: don't hardcode.

    srand(time(NULL));

    // Open socket.
    const int socket = OpenFlaschenTaschenSocket(hostname);
    PongGame pong(socket, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                  font);

    fprintf(stderr, "Game keys:\n"
            "Left paddel:  'w' up, 's' down\n"
            "Right paddel: 'i' up, 'k' down\n\n"
            "Quit with 'q'. '?' for help.\n");
    // Start the game with x fps
    pong.Start(FRAME_RATE);

    return 0;
}
