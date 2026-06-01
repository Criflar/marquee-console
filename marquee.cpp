// BREAK APART AND FIX THIS CODE TO UNDERSTAND

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>

struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    std::cout << "\033[?25h";   // Show cursor
    std::cout << "\033[?1049l"; // EXIT alternate screen buffer
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    
    struct termios raw = orig_termios;
    // Disable canonical mode and echo
    raw.c_lflag &= ~(ECHO | ICANON); 
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    
    std::cout << "\033[?1049h"; // ENTER alternate screen buffer
    std::cout << "\033[?25l";   // Hide cursor
    std::cout << "\033[2J";     // Clear screen
}

bool kbhit() {
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    // select() returns > 0 if there is data waiting to be read
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int main() {
    enableRawMode();

    std::vector<std::string> logo = {
        "  ____  _  _  ____   ",
        " (  _ \\( \\/ )(  _ \\  ",
        "  )(_) )\\  /  )(_) ) ",
        " (____/  \\/  (____/  ",
        "   .- V I D E O -.   ",
        "   '-------------'   "
    };
    int logo_w = logo[0].length();
    int logo_h = logo.size();

    int pos_x = 2, pos_y = 2;
    int dir_x = 1, dir_y = 1;
    
    std::string user_input = "";
    std::vector<std::string> history;
    const int max_history = 6; 
    bool running = true;

    while (running) {

        // Dynamic screen size
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        int current_width = w.ws_col;
        int current_height = w.ws_row;

        int marquee_height = current_height - 8;
        
        // Prevent crashes if the user makes the window incredibly small
        if (current_width < logo_w + 2) current_width = logo_w + 2;
        if (marquee_height < logo_h + 2) marquee_height = logo_h + 2;
        
        // 1. Poll Input using the traffic light (Draining the buffer)
        while (kbhit()) { 
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                if (c == 127) { // macOS Backspace
                    if (!user_input.empty()) user_input.pop_back();
                } else if (c == '\n' || c == '\r') { // Enter Key
                    if (!user_input.empty()) {
                        history.push_back(">> " + user_input);
                        if (history.size() > max_history) {
                            history.erase(history.begin());
                        }
                        user_input = ""; 
                    }
                } else {
                    user_input += c;
                }
            }
        }

        // 2. Update Physics State
        pos_x += dir_x;
        pos_y += dir_y;

        // Update collision detection to use the dynamic boundaries
        if (pos_x <= 0) { pos_x = 0; dir_x *= -1; }
        if (pos_x >= current_width - logo_w) { pos_x = current_width - logo_w; dir_x *= -1; }
        
        // Y-axis boundary pushed down to 2 to protect the header
        if (pos_y <= 2) { pos_y = 2; dir_y *= -1; }
        if (pos_y >= marquee_height - logo_h) { pos_y = marquee_height - logo_h; dir_y *= -1; }

        // 3. Render via 2D Frame Buffer
        std::vector<std::string> frame(current_height, std::string(current_width, ' '));
        
        // Header
        std::string header = "| This is a Marquee Console! |";
        int header_x = (current_width - header.length()) / 2;
        if (header_x < 0) header_x = 0; 
        frame[0].replace(header_x, header.length(), header);
        
        // Draw top border line directly under the header
        frame[1] = std::string(current_width, '-');

        // Stamp the multi-line logo into the grid
        for (int i = 0; i < logo_h; ++i) {
            for (int j = 0; j < logo_w; ++j) {
                frame[pos_y + i][pos_x + j] = logo[i][j];
            }
        }

        // Draw the divider line for the input area
        frame[marquee_height] = std::string(current_width, '=');

        // Draw the current input prompt
        std::string prompt = "Enter text: " + user_input;
        frame[marquee_height + 1].replace(0, prompt.length(), prompt);

        // Draw the input history below the prompt
        for (size_t i = 0; i < history.size(); ++i) {
            if (marquee_height + 2 + i < current_height) {
                frame[marquee_height + 2 + i].replace(0, history[i].length(), history[i]);
            }
        }
        
        // Blast the buffer to the screen
        std::string output_buffer = "\033[H"; 
        for (size_t i = 0; i < frame.size(); ++i) {
            output_buffer += frame[i];
            
            if (i < frame.size() - 1) {
                output_buffer += "\n";
            }
        }

        std::cout << output_buffer << std::flush;

        // 4. Control the Tick Rate
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    return 0;
}


/* 
Explanations:

Phase 1: The Terminal Hijack (termios.h)

Your first goal is to rip control away from the default macOS terminal. You need to transition it from a canonical, line-buffered state (where it waits for 'Enter') into a raw, immediate state.

    The Mission: Write a program that has no game loop yet. It should simply disable the ICANON and ECHO flags, hide the cursor, and clear the screen.

    The Test: When you run it, typing on your keyboard should do absolutely nothing visibly. You will need to write an exit condition (like explicitly checking for a 'q' to break the program) and ensure you restore the original terminal settings when the program closes, otherwise your terminal will remain broken until you restart it.

Phase 2: The Asynchronous Input Drain (sys/select.h)

Once you own the terminal, you need to build the traffic light system. Standard C++ input is a blocking operation; you need to build a non-blocking trap door.

    The Mission: Implement the select() function to check the STDIN_FILENO buffer. Wrap this in a while loop so that it completely drains the OS input buffer the moment it wakes up.

    The Test: Write a simple infinite loop that sleeps for 500ms at a time. During that sleep, mash your keyboard. When the loop wakes up, it should print exactly what you typed all at once, proving you successfully drained the backlog without pausing the loop.

Phase 3: The 2D Canvas Engine (std::vector<std::string>)

Do not touch the physics or the bouncing logic yet. First, build the renderer.

    The Mission: Use <sys/ioctl.h> to query the current window dimensions. Create a 2D grid in memory (a vector of strings) that perfectly matches those dimensions. Write a function that can "stamp" a multi-line ASCII art vector into specific X/Y coordinates of that grid.

    The Test: Hardcode the X/Y coordinates to the center of the screen. Run the program. You should see your ASCII DVD logo perfectly centered, and if you resize the window and rerun it, the canvas should adapt.

Phase 4: The Clock and State Loop (std::chrono)

This is where you bring the subsystems together and introduce time.

    The Mission: Create the master while(running) loop. Every tick, you will check your Phase 2 input drain, update the X and Y coordinates of your logo, enforce collision bounds based on your Phase 3 ioctl dimensions, render the frame, and then force the thread to sleep.

    The Test: This is where you manipulate the thread sleep duration to generate the data for your rubric (testing for screen tearing at 1ms vs. input lag at 150ms).


Explanations:

The marquee app redraws in a loop and sleeps for a fixed amount of time between frames, so the keyboard polling rate is tied to that sleep duration. On my 120 Hz laptop panel, a 8.3 ms sleep is the best fit because it gives exactly 120 frames per second, which is very close to the monitor’s refresh period of about 8.33 ms per frame.
In practice, changing the sleep duration mainly affects animation speed and input responsiveness. Around 33 ms gives roughly 30 Hz and is still usable, but typing starts to feel sluggish beyond that.

Slide 1: Identifying Limits (The Polling Friction)

    The Input Lag Threshold (High Delay): Pushing the thread delay above ~100ms introduces noticeable input latency. The polling mechanism (read()) is asleep for too long, causing rapid keystrokes to queue up in the OS buffer or drop entirely, resulting in a sluggish, unresponsive CLI.

    The Simulation Threshold (Low Delay): Dropping the thread delay to 1ms (< 120Hz) over-saturates the tick rate. Instead of increasing visual fluidity, it artificially accelerates the application logic (the position vector updates too many times between physical monitor refreshes), making the animation uncontrollably fast.

Slide 2: Architectural Observations (The Absence of Tearing)

    Expected vs. Actual: While hardware limits usually result in visual screen tearing at 0ms delays, this implementation produced a tear-free output even at maximum CPU execution speeds.

    The macOS Compositor Effect: The macOS WindowServer fundamentally prevents tearing by enforcing system-wide V-Sync and double-buffering. The application writes to a virtual buffer, and the OS orchestrates a clean swap to the physical display at a maximum of 120Hz (ProMotion limit), masking the visual artifacts typical of legacy console environments.

Slide 3: Recommended Configuration (The Balance)

    Target Tick Rate: 8.3ms to 33ms (equivalent to ~120 to ~30 FPS).

    Justification: This range provides the optimal equilibrium. It is fast enough that the physics animation remains entirely smooth and visually distinct, yet leaves a small enough sleep window that the while(kbhit()) queue-draining mechanism can process human typing speeds instantaneously without perceived latency.

*/