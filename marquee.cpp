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

        // 4. Control the Tick Rate (e.g. ~30 for 30 fps, ~16 for 60 fps)
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    return 0;
}