#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include <filesystem>
#include <stdio.h>
#include <stdlib.h>
#include <string>

const char* SAVE_FILE = "sdmc:/switch/custom_wheels.dat";

enum AppState { WHEEL_MAIN, SPINNING, LAUNCHING, SIDEBAR_MENU, CREATE_GRID };

struct GameEntry {
    u64 titleId;
    SDL_Texture* iconTexture;
    bool selected = false; 
};

struct CustomWheel {
    std::string name;
    std::vector<u64> titleIds;
    SDL_Texture* coverIcon; 
};

// --- ADVANCED TINFOIL ERADICATOR ---
bool IsTinfoil(u64 titleId) {
    if (titleId == 0x050000000000100D || titleId == 0x010000000000100D) return true; 

    bool foundTinfoil = false;
    NsApplicationControlData* controlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
    size_t outSize = 0;
    
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, titleId, controlData, sizeof(NsApplicationControlData), &outSize))) {
        for (int i = 0; i < 16; ++i) { 
            // FIX: The correct struct member in libnx is 'lang', not 'display_title'
            std::string name = controlData->nacp.lang[i].name;
            
            if (name.empty()) continue;

            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return std::tolower(c); });
            if (name.find("tinfoil") != std::string::npos || name.find("lithium") != std::string::npos) {
                foundTinfoil = true;
                break;
            }
        }
    }
    free(controlData);
    return foundTinfoil;
}

// --- EFFICIENT GPU SHAPES ---
void DrawHardwareCircle(SDL_Renderer* renderer, int centerX, int centerY, int radius, SDL_Color color) {
    const int numSegments = 60; 
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    vertices.push_back({{ (float)centerX, (float)centerY }, {color.r, color.g, color.b, color.a}, {0, 0}});
    for (int i = 0; i <= numSegments; ++i) {
        float rads = (i * (360.0f / numSegments)) * (M_PI / 180.0f);
        vertices.push_back({{centerX + radius * std::cos(rads), centerY + radius * std::sin(rads)}, {color.r, color.g, color.b, color.a}, {0, 0}});
    }
    for (int i = 1; i <= numSegments; ++i) {
        indices.push_back(0); indices.push_back(i); indices.push_back(i + 1);
    }
    SDL_RenderGeometry(renderer, NULL, vertices.data(), vertices.size(), indices.data(), indices.size());
}

void DrawGeometryRing(SDL_Renderer* renderer, float centerX, float centerY, float innerRadius, float outerRadius, SDL_Color color) {
    int numSegments = 60;
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    for (int i = 0; i <= numSegments; ++i) {
        float theta = i * 2.0f * M_PI / numSegments;
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);
        vertices.push_back({{centerX + innerRadius * cosTheta, centerY + innerRadius * sinTheta}, {color.r, color.g, color.b, color.a}, {0.0f, 0.0f}});
        vertices.push_back({{centerX + outerRadius * cosTheta, centerY + outerRadius * sinTheta}, {color.r, color.g, color.b, color.a}, {1.0f, 1.0f}});
    }
    for (int i = 0; i < numSegments; ++i) {
        int v_i = i * 2;
        indices.push_back(v_i); indices.push_back(v_i + 1); indices.push_back(v_i + 2);
        indices.push_back(v_i + 2); indices.push_back(v_i + 1); indices.push_back(v_i + 3);
    }
    SDL_RenderGeometry(renderer, NULL, vertices.data(), vertices.size(), indices.data(), indices.size());
}

// --- TEXT RENDERING HELPER ---
void RenderText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
    if (!font || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_Rect rect = {x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, NULL, &rect);
        SDL_FreeSurface(surf);
        SDL_DestroyTexture(tex);
    }
}

// --- SOFTWARE KEYBOARD WRAPPER ---
std::string GetKeyboardInput(const char* guideText) {
    SwkbdConfig kbd;
    char outText[256] = {0};
    Result rc = swkbdCreate(&kbd, 0);
    if (R_SUCCEEDED(rc)) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetGuideText(&kbd, guideText);
        swkbdShow(&kbd, outText, sizeof(outText));
        swkbdClose(&kbd);
    }
    return std::string(outText);
}

// --- TEXTURE LOADER ---
SDL_Texture* LoadIconForTitle(SDL_Renderer* renderer, u64 titleId) {
    NsApplicationControlData* controlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
    size_t outSize = 0;
    SDL_Texture* tex = nullptr;
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, titleId, controlData, sizeof(NsApplicationControlData), &outSize))) {
        if (outSize > sizeof(NacpStruct)) {
            SDL_RWops* rw = SDL_RWFromMem(controlData->icon, outSize - sizeof(NacpStruct));
            SDL_Surface* surface = IMG_Load_RW(rw, 1);
            if (surface) {
                tex = SDL_CreateTextureFromSurface(renderer, surface);
                SDL_FreeSurface(surface);
            }
        }
    }
    free(controlData);
    return tex;
}

// --- LIBRARY FETCHERS ---
std::vector<GameEntry> GetDefaultRandomWheel(SDL_Renderer* renderer) {
    std::vector<GameEntry> games;
    std::vector<NsApplicationRecord> records(100); 
    s32 totalRecords = 0;
    if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), 0, &totalRecords)) || totalRecords == 0) return games;
    
    std::mt19937 g(svcGetSystemTick()); 
    std::shuffle(records.begin(), records.begin() + totalRecords, g);
    
    for (int i = 0; i < totalRecords && (int)games.size() < 20; ++i) {
        if (IsTinfoil(records[i].application_id)) continue; 
        
        SDL_Texture* tex = LoadIconForTitle(renderer, records[i].application_id);
        if (tex) games.push_back({records[i].application_id, tex});
    }
    return games;
}

std::vector<GameEntry> LoadSpecificWheel(SDL_Renderer* renderer, const std::vector<u64>& titleIds) {
    std::vector<GameEntry> games;
    for (u64 id : titleIds) {
        SDL_Texture* tex = LoadIconForTitle(renderer, id);
        if (tex) games.push_back({id, tex});
    }
    return games;
}

std::vector<GameEntry> GetLibraryForGrid(SDL_Renderer* renderer) {
    std::vector<GameEntry> games;
    std::vector<NsApplicationRecord> records(100); 
    s32 totalRecords = 0;
    if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), 0, &totalRecords))) return games;
    
    for (int i = 0; i < totalRecords && (int)games.size() < 48; ++i) {
        if (IsTinfoil(records[i].application_id)) continue;
        
        SDL_Texture* tex = LoadIconForTitle(renderer, records[i].application_id);
        if (tex) games.push_back({records[i].application_id, tex});
    }
    return games;
}

// --- FILE I/O ---
void SaveCustomWheels(const std::vector<CustomWheel>& wheels) {
    FILE* f = fopen(SAVE_FILE, "wb");
    if (!f) return;
    size_t count = wheels.size();
    fwrite(&count, sizeof(size_t), 1, f);
    for (const auto& w : wheels) {
        size_t nameLen = w.name.length();
        fwrite(&nameLen, sizeof(size_t), 1, f);
        fwrite(w.name.c_str(), sizeof(char), nameLen, f);

        size_t wCount = w.titleIds.size();
        fwrite(&wCount, sizeof(size_t), 1, f);
        for (u64 id : w.titleIds) fwrite(&id, sizeof(u64), 1, f);
    }
    fclose(f);
}

void LoadCustomWheels(SDL_Renderer* renderer, std::vector<CustomWheel>& wheels) {
    FILE* f = fopen(SAVE_FILE, "rb");
    if (!f) return;
    size_t count = 0;
    if (fread(&count, sizeof(size_t), 1, f) == 1) {
        for (size_t i = 0; i < count; ++i) {
            CustomWheel wheel;
            size_t nameLen = 0;
            fread(&nameLen, sizeof(size_t), 1, f);
            char* nameBuf = (char*)malloc(nameLen + 1);
            fread(nameBuf, sizeof(char), nameLen, f);
            nameBuf[nameLen] = '\0';
            wheel.name = std::string(nameBuf);
            free(nameBuf);

            size_t wCount = 0;
            fread(&wCount, sizeof(size_t), 1, f);
            for (size_t j = 0; j < wCount; ++j) {
                u64 id = 0;
                fread(&id, sizeof(u64), 1, f);
                wheel.titleIds.push_back(id);
            }
            if (!wheel.titleIds.empty()) {
                wheel.coverIcon = LoadIconForTitle(renderer, wheel.titleIds[0]); 
                wheels.push_back(wheel);
            }
        }
    }
    fclose(f);
}

// --- MAIN ---
int main(int argc, char **argv) {
    appletInitializeGamePlayRecording(); 
    nsInitialize();                      
    romfsInit();                         

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    TTF_Init();

    SDL_Window* window = SDL_CreateWindow("Roulette", 0, 0, 1280, 720, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); 

    TTF_Font* font = TTF_OpenFont("romfs:/font.ttf", 22);
    if (!font) font = TTF_OpenFont("sdmc:/switch/font.ttf", 22); 

    TTF_Font* smallFont = TTF_OpenFont("romfs:/font.ttf", 16);
    if (!smallFont) smallFont = TTF_OpenFont("sdmc:/switch/font.ttf", 16);

    SDL_Texture* bgTexture = nullptr;
    
    // 1. Try to load the embedded background from romfs first
    bgTexture = IMG_LoadTexture(renderer, "romfs:/bg.jpg");
    
    // 2. Fallback to SD card if the romfs file is missing (or if you want a custom theme later)
    if (!bgTexture) {
        FILE* bgf = fopen("sdmc:/switch/bg.jpg", "r");
        if (bgf) { 
            fclose(bgf); 
            bgTexture = IMG_LoadTexture(renderer, "sdmc:/switch/bg.jpg"); 
        }
    }

    AppState currentState = WHEEL_MAIN;
    
    std::vector<GameEntry> centerWheel = GetDefaultRandomWheel(renderer);
    float currentAngle = 0.0f;
    float spinSpeed = 0.0f;
    int winningIndex = -1;

    std::vector<CustomWheel> savedWheels;
    LoadCustomWheels(renderer, savedWheels);
    int sidebarCursor = 0;

    std::vector<GameEntry> gridLibrary;
    int gridCursorX = 0, gridCursorY = 0, selectedGridGames = 0;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        
        bool navUp = (kDown & HidNpadButton_Up) || (kDown & HidNpadButton_StickLUp);
        bool navDown = (kDown & HidNpadButton_Down) || (kDown & HidNpadButton_StickLDown);
        bool navLeft = (kDown & HidNpadButton_Left) || (kDown & HidNpadButton_StickLLeft);
        bool navRight = (kDown & HidNpadButton_Right) || (kDown & HidNpadButton_StickLRight);

        if (kDown & HidNpadButton_Plus) break; 

        if (currentState == WHEEL_MAIN) {
            if (kDown & HidNpadButton_A && !centerWheel.empty()) {
                currentState = SPINNING;
                spinSpeed = 45.0f + ((svcGetSystemTick() % 200) / 10.0f); 
            }
            if (kDown & HidNpadButton_Minus) {
                currentState = SIDEBAR_MENU; 
            }
            if (kDown & HidNpadButton_R) {
                if (gridLibrary.empty()) gridLibrary = GetLibraryForGrid(renderer);
                currentState = CREATE_GRID;
                gridCursorX = 0; gridCursorY = 0; selectedGridGames = 0;
                for(auto& g : gridLibrary) g.selected = false;
            }
            if (kDown & HidNpadButton_B) {
                for (auto& g : centerWheel) SDL_DestroyTexture(g.iconTexture);
                centerWheel = GetDefaultRandomWheel(renderer);
            }
        } 
        else if (currentState == SIDEBAR_MENU) {
            if (kDown & HidNpadButton_Minus) {
                currentState = WHEEL_MAIN; 
            }
            if (kDown & HidNpadButton_B) {
                for (auto& g : centerWheel) SDL_DestroyTexture(g.iconTexture);
                centerWheel = GetDefaultRandomWheel(renderer);
                currentState = WHEEL_MAIN;
            }
            
            if (navDown) {
                sidebarCursor++;
                if (sidebarCursor >= (int)savedWheels.size()) sidebarCursor = (int)savedWheels.size() - 1;
            }
            if (navUp) {
                sidebarCursor--;
                if (sidebarCursor < 0) sidebarCursor = 0;
            }
            
            if (kDown & HidNpadButton_X && !savedWheels.empty()) {
                for (auto& g : centerWheel) SDL_DestroyTexture(g.iconTexture);
                centerWheel = LoadSpecificWheel(renderer, savedWheels[sidebarCursor].titleIds);
                currentState = WHEEL_MAIN;
            }
            if (kDown & HidNpadButton_Y && !savedWheels.empty()) {
                SDL_DestroyTexture(savedWheels[sidebarCursor].coverIcon);
                savedWheels.erase(savedWheels.begin() + sidebarCursor);
                SaveCustomWheels(savedWheels);
                if (sidebarCursor >= (int)savedWheels.size() && sidebarCursor > 0) sidebarCursor--;
            }
        }
        else if (currentState == CREATE_GRID) {
            if (kDown & HidNpadButton_B) currentState = WHEEL_MAIN;
            
            if (navRight) gridCursorX = (gridCursorX + 1) % 8;
            if (navLeft) gridCursorX = (gridCursorX - 1 < 0) ? 7 : gridCursorX - 1;
            if (navDown) gridCursorY++;
            if (navUp) gridCursorY = (gridCursorY - 1 < 0) ? 0 : gridCursorY - 1;
            
            int activeIndex = gridCursorY * 8 + gridCursorX;

            if (kDown & HidNpadButton_A && activeIndex < (int)gridLibrary.size()) {
                if (!gridLibrary[activeIndex].selected && selectedGridGames < 20) {
                    gridLibrary[activeIndex].selected = true;
                    selectedGridGames++;
                } else if (gridLibrary[activeIndex].selected) {
                    gridLibrary[activeIndex].selected = false;
                    selectedGridGames--;
                }
            }

            if (kDown & HidNpadButton_X && selectedGridGames > 0) {
                std::string wheelName = GetKeyboardInput("Name your Custom Roulette");
                if (wheelName.empty()) wheelName = "Custom Roulette";

                CustomWheel newWheel;
                newWheel.name = wheelName;
                for (auto& g : gridLibrary) {
                    if (g.selected) newWheel.titleIds.push_back(g.titleId);
                }
                newWheel.coverIcon = LoadIconForTitle(renderer, newWheel.titleIds[0]); 
                savedWheels.push_back(newWheel);
                SaveCustomWheels(savedWheels);
                
                sidebarCursor = (int)savedWheels.size() - 1; 
                currentState = SIDEBAR_MENU;
            }
        }

        // --- PHYSICS LOGIC ---
        if (currentState == SPINNING) {
            currentAngle += spinSpeed;
            spinSpeed -= 0.65f; 
            if (currentAngle >= 360.0f) currentAngle -= 360.0f;
            if (spinSpeed <= 0) {
                spinSpeed = 0;
                currentState = LAUNCHING;
                float sliceAngle = 360.0f / centerWheel.size();
                float normalizedStop = std::fmod((360.0f - currentAngle + 270.0f), 360.0f);
                winningIndex = (int)(normalizedStop / sliceAngle);
                if (winningIndex >= (int)centerWheel.size()) winningIndex = 0;
            }
        }

        // --- RENDER LOGIC ---
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); 
        SDL_RenderClear(renderer);
        if (bgTexture) SDL_RenderCopy(renderer, bgTexture, NULL, NULL);

        if (currentState == WHEEL_MAIN || currentState == SPINNING || currentState == LAUNCHING || currentState == SIDEBAR_MENU) {
            int centerX = 1280 / 2;
            int centerY = 720 / 2;
            int radius = 260; 

            DrawGeometryRing(renderer, centerX, centerY, radius + 80, radius + 82, {0, 0, 0, 255});       
            DrawGeometryRing(renderer, centerX, centerY, radius + 82, radius + 86, {255, 215, 0, 255});   
            DrawGeometryRing(renderer, centerX, centerY, radius + 86, radius + 88, {139, 69, 19, 255});   

            if (!centerWheel.empty()) {
                float anglePerSlice = 360.0f / centerWheel.size();
                for (int i = 0; i < (int)centerWheel.size(); ++i) {
                    float sliceAngle = (i * anglePerSlice) + currentAngle; 
                    float rads = sliceAngle * (M_PI / 180.0f);
                    
                    SDL_Rect iconRect = {0, 0, 96, 96};
                    iconRect.x = centerX + (radius * std::cos(rads)) - (iconRect.w / 2);
                    iconRect.y = centerY + (radius * std::sin(rads)) - (iconRect.h / 2);

                    if (currentState == LAUNCHING && i == winningIndex) {
                        iconRect.w += 20; iconRect.h += 20;
                        iconRect.x -= 10; iconRect.y -= 10;
                    }
                    SDL_RenderCopy(renderer, centerWheel[i].iconTexture, NULL, &iconRect);
                }
            }

            DrawGeometryRing(renderer, centerX, centerY, 50, 55, {150, 150, 150, 150}); 
            DrawHardwareCircle(renderer, centerX, centerY, 20, {255, 100, 0, 255}); 

            SDL_Vertex pointerVerts[3] = {
                {{(float)centerX, (float)centerY - radius - 60}, {255, 0, 0, 255}, {0,0}}, 
                {{(float)centerX - 20, (float)centerY - radius - 90}, {255, 0, 0, 255}, {0,0}}, 
                {{(float)centerX + 20, (float)centerY - radius - 90}, {255, 0, 0, 255}, {0,0}}  
            };
            SDL_RenderGeometry(renderer, NULL, pointerVerts, 3, NULL, 0);

            SDL_Rect hintBg = {20, 20, 240, 180}; 
            SDL_SetRenderDrawColor(renderer, 20, 20, 20, 200);
            SDL_RenderFillRect(renderer, &hintBg);
            SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255); 
            SDL_RenderDrawRect(renderer, &hintBg);

            SDL_Color textColor = {255, 255, 255, 255};
            RenderText(renderer, font, "CONTROLS", 30, 30, {255, 215, 0, 255});
            RenderText(renderer, smallFont, "(A) Spin Roulette", 30, 65, textColor);
            RenderText(renderer, smallFont, "(R) Create Custom Roulette", 30, 90, textColor);
            RenderText(renderer, smallFont, "(-) Saved Roulettes", 30, 115, textColor);
            RenderText(renderer, smallFont, "(B) Default Roulette", 30, 140, textColor);
            RenderText(renderer, smallFont, "(+) Exit", 30, 165, textColor);
        }

        if (currentState == SIDEBAR_MENU) {
            SDL_SetRenderDrawColor(renderer, 25, 25, 25, 235); 
            SDL_Rect sidebar = {1280 - 240, 0, 240, 720}; 
            SDL_RenderFillRect(renderer, &sidebar);
            
            RenderText(renderer, font, "SAVED ROULETTES", 1280 - 220, 20, {255, 215, 0, 255});

            for (int i = 0; i < (int)savedWheels.size(); ++i) {
                SDL_Rect box = {1280 - 220, 70 + (i * 70), 200, 60}; 
                
                if (i == sidebarCursor) {
                    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255); 
                    SDL_Rect border = {box.x-2, box.y-2, box.w+4, box.h+4};
                    SDL_RenderFillRect(renderer, &border);
                }
                
                SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); 
                SDL_RenderFillRect(renderer, &box);

                SDL_Rect iconRect = {box.x + 5, box.y + 5, 50, 50};
                SDL_RenderCopy(renderer, savedWheels[i].coverIcon, NULL, &iconRect);

                RenderText(renderer, smallFont, savedWheels[i].name, box.x + 65, box.y + 20, {255, 255, 255, 255});
            }

            RenderText(renderer, smallFont, "(X) Load  (Y) Delete", 1280 - 230, 665, {200, 200, 200, 255});
            RenderText(renderer, smallFont, "(B) Back to Default", 1280 - 230, 685, {200, 200, 200, 255});
        }

        if (currentState == CREATE_GRID) {
            SDL_SetRenderDrawColor(renderer, 20, 20, 20, 240); 
            SDL_RenderClear(renderer); 
            
            RenderText(renderer, font, "SELECT UP TO 20 GAMES FOR CUSTOM ROULETTE", 100, 40, {255, 215, 0, 255});
            RenderText(renderer, smallFont, "(A) Toggle Game   (X) Save Roulette   (B) Cancel", 100, 75, {255, 255, 255, 255});

            int startX = 100, startY = 120;
            for (int i = 0; i < (int)gridLibrary.size(); ++i) {
                int col = i % 8; int row = i / 8;
                SDL_Rect iconRect = {startX + (col * 130), startY + (row * 130), 100, 100};
                SDL_RenderCopy(renderer, gridLibrary[i].iconTexture, NULL, &iconRect);

                if (gridLibrary[i].selected) {
                    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255); 
                    for(int w=0; w<4; w++) {
                        SDL_Rect border = {iconRect.x-w, iconRect.y-w, iconRect.w+(w*2), iconRect.h+(w*2)};
                        SDL_RenderDrawRect(renderer, &border);
                    }
                }
                if (col == gridCursorX && row == gridCursorY) {
                    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255); 
                    for(int w=4; w<8; w++) {
                        SDL_Rect border = {iconRect.x-w, iconRect.y-w, iconRect.w+(w*2), iconRect.h+(w*2)};
                        SDL_RenderDrawRect(renderer, &border);
                    }
                }
            }
        }

        SDL_RenderPresent(renderer);

        if (currentState == LAUNCHING && winningIndex != -1) {
            SDL_Delay(1500); 
            appletRequestLaunchApplication(centerWheel[winningIndex].titleId, NULL);
            break; 
        }
    }

    // --- CLEANUP ---
    for (auto& game : centerWheel) SDL_DestroyTexture(game.iconTexture);
    for (auto& game : gridLibrary) SDL_DestroyTexture(game.iconTexture);
    for (auto& w : savedWheels) SDL_DestroyTexture(w.coverIcon);
    if (bgTexture) SDL_DestroyTexture(bgTexture);
    
    if (font) TTF_CloseFont(font);
    if (smallFont) TTF_CloseFont(smallFont);
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    romfsExit();
    nsExit();
    return 0;
}