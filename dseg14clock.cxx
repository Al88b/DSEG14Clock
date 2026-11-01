//g++ dseg14clock.cxx -o clock_app -lSDL2 -lSDL2_ttf -std=c++11

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <algorithm>
#include <ctime>

// Layout: the window is sized to fit the text at startup.
// Set PADDING to 0 for a window that hugs the text with no border at all.
const int PADDING = 10;   // empty space around the text (pixels)
const int LINE_GAP = 10;  // space between the two lines (pixels)

// Helper function to render text to an SDL_Texture
SDL_Texture* CreateTextTexture(TTF_Font* font, const std::string& text, SDL_Color color, SDL_Renderer* renderer, SDL_Rect& outRect) {
    // 1. Render text to a temporary CPU surface
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        std::cerr << "Failed to render text surface: " << TTF_GetError() << std::endl;
        return nullptr;
    }

    // 2. Upload surface data to GPU texture
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        std::cerr << "Failed to create texture from surface: " << SDL_GetError() << std::endl;
    } else {
        // Capture dimensions for accurate rendering positioning
        outRect.w = surface->w;
        outRect.h = surface->h;
    }

    // 3. Free temporary CPU memory
    SDL_FreeSurface(surface);
    return texture;
}

int main(int argc, char* argv[]) {
    // Initialize SDL2 Video Subsystem
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return -1;
    }

    // Initialize SDL2_ttf
    if (TTF_Init() == -1) {
        std::cerr << "SDL_ttf could not initialize! TTF_Error: " << TTF_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    // Load DSEG14 Font first so we can measure the text before creating the window
    // Adjust size 64 to fit your preferred layout size (the window follows it)
    TTF_Font* font = TTF_OpenFont("/usr/local/share/fonts/DSEG14-Classic/DSEG14Classic-Bold.ttf", 64);
    if (!font) {
        std::cerr << "Failed to load font! TTF_Error: " << TTF_GetError() << std::endl;
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    // Measure both lines. DSEG14 is monospaced, so any sample with the same
    // number of characters as the real strings gives the exact size.
    int dtW = 0, dtH = 0, cdW = 0, cdH = 0;
    TTF_SizeUTF8(font, "Wed 2026-11-01 15:30:22", &dtW, &dtH);
    TTF_SizeUTF8(font, "T- 10:00.000", &cdW, &cdH);

    const int SCREEN_WIDTH  = std::max(dtW, cdW) + 2 * PADDING;
    const int SCREEN_HEIGHT = dtH + LINE_GAP + cdH + 2 * PADDING;
    const int DATETIME_Y    = PADDING;
    const int COUNTDOWN_Y   = PADDING + dtH + LINE_GAP;

    // Create Window
    SDL_Window* window = SDL_CreateWindow("DSEG14 Clock & Countdown", 
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        TTF_CloseFont(font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    // Create Hardware Accelerated Renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        TTF_CloseFont(font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }

    // Define colors (Red)
    SDL_Color textColor = { 255, 0, 0, 255 }; 

    // Textures and Rectangles for layout containers
    SDL_Texture* dateTimeTexture = nullptr;
    SDL_Rect dateTimeRect = { 0, 0, 0, 0 };

    SDL_Texture* countdownTexture = nullptr;
    SDL_Rect countdownRect = { 0, 0, 0, 0 };

    // String caches to prevent redundant GPU allocations
    std::string lastDateTimeStr = "";
    std::string lastCountdownStr = "";

    // Target duration for the countdown: 10 minutes (600,000 milliseconds)
    long long remainingMs = 600000; 

    // Track time delta using high-precision steady clock
    auto prevTime = std::chrono::steady_clock::now();

    bool quit = false;
    SDL_Event e;

    // Main Game/Application Loop
    while (!quit) {
        // Handle OS Events (Window closing, keypresses)
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
        }

        // --- TIME DELTA CALCULATIONS ---
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - prevTime).count();
        prevTime = currentTime;

        // Process Countdown Timer
        if (remainingMs > 0) {
            remainingMs -= elapsedTime;
            if (remainingMs < 0) remainingMs = 0;
        }

        // --- GET CURRENT SYSTEM TIME ---
        auto sysNow = std::chrono::system_clock::now();
        std::time_t sysTime = std::chrono::system_clock::to_time_t(sysNow);
        std::tm* localTime = std::localtime(&sysTime);

        // Format Date and Time String (e.g., "SUN 2026-11-01 15:30:22")
        std::stringstream dtStream;
        dtStream << std::put_time(localTime, "%a %Y-%m-%d %H:%M:%S");
        std::string currentDateTimeStr = dtStream.str();

        // Format Countdown String (MM:SS:mmm)
        long long mins = (remainingMs / 60000) % 60;
        long long secs = (remainingMs / 1000) % 60;
        long long msecs = remainingMs % 1000;

        std::stringstream cdStream;
        cdStream << "T- " 
                 << std::setfill('0') << std::setw(2) << mins << ":"
                 << std::setfill('0') << std::setw(2) << secs << "."
                 << std::setfill('0') << std::setw(3) << msecs;
        std::string currentCountdownStr = cdStream.str();

        // --- OPTIMIZED TEXTURE UPDATING ---
        // Render Date/Time only when the second advances
        if (currentDateTimeStr != lastDateTimeStr) {
            if (dateTimeTexture) SDL_DestroyTexture(dateTimeTexture); // Clean old GPU memory
            dateTimeTexture = CreateTextTexture(font, currentDateTimeStr, textColor, renderer, dateTimeRect);
            lastDateTimeStr = currentDateTimeStr;

            // Center alignment math
            dateTimeRect.x = (SCREEN_WIDTH - dateTimeRect.w) / 2;
            dateTimeRect.y = DATETIME_Y;
        }

        // Render Countdown only when milliseconds update
        if (currentCountdownStr != lastCountdownStr) {
            if (countdownTexture) SDL_DestroyTexture(countdownTexture); // Clean old GPU memory
            countdownTexture = CreateTextTexture(font, currentCountdownStr, textColor, renderer, countdownRect);
            lastCountdownStr = currentCountdownStr;

            // Center alignment math
            countdownRect.x = (SCREEN_WIDTH - countdownRect.w) / 2;
            countdownRect.y = COUNTDOWN_Y;
        }

        // --- RENDERING PHASE ---
        SDL_SetRenderDrawColor(renderer, 15, 15, 15, 255); // Dark background for contrast
        SDL_RenderClear(renderer);

        // Blit text layers onto screen
        if (dateTimeTexture) {
            SDL_RenderCopy(renderer, dateTimeTexture, nullptr, &dateTimeRect);
        }
        if (countdownTexture) {
            SDL_RenderCopy(renderer, countdownTexture, nullptr, &countdownRect);
        }

        SDL_RenderPresent(renderer);
    }

    // --- CLEANUP & MEMORY DEALLOCATION ---
    if (dateTimeTexture) SDL_DestroyTexture(dateTimeTexture);
    if (countdownTexture) SDL_DestroyTexture(countdownTexture);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
