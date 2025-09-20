#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_ttf.h>

#ifdef _WIN32
#include <windows.h>
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    return main(__argc, __argv);
}
#endif

// === ИСПРАВЛЕНИЕ 2: Убираем магические числа ===
#define MAX_INPUT_LENGTH 100
#define CONFIG_LINE_SIZE 256
#define CONFIG_KEY_SIZE 64

#define DAY_NIGHT_DURATION_SECONDS (48.0f * 60.0f)
#define SKY_RADIUS 100.0f
#define ASSET_TABLE_SIZE 128
#define WIDTH 1920
#define HEIGHT 1080

#define AIR_ACCELERATION 2.0f
#define AIR_DECELERATION 0.5f
#define JUMP_FORWARD_IMPULSE 0.15f
#define STANDING_HEIGHT 1.0f
#define CROUCHING_HEIGHT 0.5f
#define CROUCH_LERP_SPEED 8.0f
#define NEAR_PLANE 0.1f

#define BOSS_MAX_HEALTH 100.0f
#define BOSS_SPEED 2.0f
#define BOSS_VULNERABLE_DURATION 5.0f
#define MAX_GLITCHES 5
#define GLITCH_SPEED 3.0f

#define RAYCAST_MAX_DISTANCE 20.0f
#define PULL_FORCE 25.0f
#define TRAJECTORY_STEPS 40
#define TRAJECTORY_TIME_STEP 0.03f
#define THROW_POWER_DEFAULT 12.0f
#define THROW_POWER_MAX 25.0f
#define MAX_PICKUPS 20
#define MAX_SHARDS 100

#define PHONE_ANIM_SPEED 8.0f

// Структура для хранения одного ресурса (запись в таблице)
typedef enum { ASSET_FONT, ASSET_TEXTURE, ASSET_SOUND } AssetType;

typedef struct AssetNode {
    char* key;
    void* data;
    AssetType type;
    struct AssetNode* next;
} AssetNode;

// Сам менеджер
typedef struct {
    AssetNode* table[ASSET_TABLE_SIZE];
    SDL_Renderer* renderer_ref;
} AssetManager;

// Прототипы функций менеджера
static unsigned long asset_hash(const char* str);
void AssetManager_Init(AssetManager* am, SDL_Renderer* renderer);
TTF_Font* AssetManager_GetFont(AssetManager* am, const char* filename, int size);
void AssetManager_Destroy(AssetManager* am);

// Реализация функций менеджера
static unsigned long asset_hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % ASSET_TABLE_SIZE;
}

void AssetManager_Init(AssetManager* am, SDL_Renderer* renderer) {
    for (int i = 0; i < ASSET_TABLE_SIZE; i++) {
        am->table[i] = NULL;
    }
    am->renderer_ref = renderer;
    printf("Asset Manager initialized.\n");
}

TTF_Font* AssetManager_GetFont(AssetManager* am, const char* filename, int size) {
    char key[256];
    snprintf(key, sizeof(key), "%s_%d", filename, size);
    unsigned long index = asset_hash(key);

    AssetNode* current = am->table[index];
    while (current != NULL) {
        if (strcmp(current->key, key) == 0 && current->type == ASSET_FONT) {
            return (TTF_Font*)current->data;
        }
        current = current->next;
    }

    TTF_Font* font = TTF_OpenFont(filename, size);
    if (!font) {
        printf("Failed to load font: %s. Error: %s\n", filename, TTF_GetError());
        return NULL;
    }

    AssetNode* newNode = (AssetNode*)malloc(sizeof(AssetNode));
    newNode->key = strdup(key);
    newNode->data = font;
    newNode->type = ASSET_FONT;
    newNode->next = am->table[index];
    am->table[index] = newNode;

    printf("Loaded font '%s' (size %d) via Asset Manager.\n", filename, size);
    return font;
}

void AssetManager_Destroy(AssetManager* am) {
    for (int i = 0; i < ASSET_TABLE_SIZE; i++) {
        AssetNode* current = am->table[i];
        while (current != NULL) {
            AssetNode* next = current->next;
            
            switch (current->type) {
                case ASSET_FONT:
                    TTF_CloseFont((TTF_Font*)current->data);
                    break;
                case ASSET_TEXTURE:
                    // SDL_DestroyTexture((SDL_Texture*)current->data); // для будущего
                    break;
                case ASSET_SOUND:
                    // Mix_FreeChunk((Mix_Chunk*)current->data); // для будущего
                    break;
            }

            printf("Freed asset: %s\n", current->key);
            free(current->key);
            free(current);
      
            current = next;
        }
    }
    printf("Asset Manager destroyed.\n");
}

float g_fov = 200.0f;

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    int x, y;
} Vec2;

typedef struct {
    float mouseSensitivity;
    float walkSpeed;
    float runSpeed;
    float crouchSpeedMultiplier;
    float acceleration;
    float deceleration;
    float jumpForce;
    float gravity;
    float fov;
} GameConfig;

typedef struct {
    float timeOfDay;
    SDL_Color skyTopColor;
    SDL_Color skyBottomColor;
    SDL_Color ambientLightColor;
    SDL_Color fogColor;
    Vec3 sunPos;
    Vec3 moonPos;
} DayNightCycle;

typedef struct {
    float x, y, z;
    float rotY;
    float rotX;
    float vy;
    
    float vx, vz;
    float targetVx, targetVz;
    
    float bobPhase;
    float bobAmount;
    float currentBobY;
    float currentBobX;
    
    float height;
    float targetHeight;
    
    int isMoving;
    int isRunning;
    int isCrouching;
} Camera;

typedef struct {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
} AABB;

typedef struct {
    Vec3 pos;
    AABB bounds;
    SDL_Color color;
} CollisionBox;

typedef enum {
    PICKUP_TYPE_BOTTLE,
    PICKUP_TYPE_BRICK,
    PICKUP_TYPE_BOOK,
    PICKUP_TYPE_CAN
} PickupType;

typedef enum {
    PICKUP_STATE_IDLE,
    PICKUP_STATE_HELD,
    PICKUP_STATE_THROWN,
    PICKUP_STATE_BROKEN,
    PICKUP_STATE_PULLED
} PickupState;

typedef struct {
    Vec3 pos;
    Vec3 velocity;
    Vec3 rotation;
    Vec3 rotVelocity;
    
    PickupType type;
    PickupState state;
    
    float health;
    float mass;
    float bounciness;
    
    Vec3 size;
    SDL_Color color;
    SDL_Color originalColor;
    
    int breakable;
    float breakThreshold;
} PickupObject;

typedef struct {
    Vec3 pos;
    Vec3 velocity;
    Vec3 rotation;
    float lifetime;
    float size;
    SDL_Color color;
} GlassShard;

typedef enum {
    HAND_STATE_IDLE,
    HAND_STATE_WALKING,
    HAND_STATE_RUNNING,
    HAND_STATE_JUMPING,
    HAND_STATE_REACHING,
    HAND_STATE_HOLDING,
    HAND_STATE_THROWING,
    HAND_STATE_INSPECTING,
    HAND_STATE_AIMING,
    HAND_STATE_PULLING,
} HandState;

typedef struct {
    int isActive;
    float progress;
} HandFlickGesture;

typedef struct {
    Vec3 leftPos;
    Vec3 rightPos;
    
    Vec3 leftRot;
    Vec3 rightRot;
    
    HandState currentState;
    HandState targetState;
    float stateTransition;
    
    float walkPhase;
    float runPhase;
    float inspectPhase;
    float throwPhase;
    float idlePhase;
    
    PickupObject* heldObject;
    float reachDistance;
    PickupObject* targetedObject;
    PickupObject* pulledObject;
    HandFlickGesture flick;
    float currentThrowPower;
} HandsSystem;

typedef struct {
    Vec3 points[TRAJECTORY_STEPS];
    int numPoints;
    int didHit;
} Trajectory;

typedef struct {
    Vec3 pos;
    int collected;
    float rotationPhase;
    float bobPhase;
} Coin;

typedef struct {
    float mouseSensitivity;
    float walkSpeed, runSpeed, crouchSpeedMultiplier;
    float acceleration, deceleration;
    float jumpForce, gravity;
    float fov;
} GameplaySettings;

typedef struct {
    const char* name;
    float* value_ptr;
    float step;
    float min_val;
    float max_val;
} EditableVariable;

typedef enum {
    PHONE_STATE_HIDDEN,
    PHONE_STATE_SHOWING,
    PHONE_STATE_VISIBLE,
    PHONE_STATE_HIDING
} PhoneState;

typedef struct {
    PhoneState state;
    float animationProgress;
    int selectedOption;
} Phone;

// Структура для истребителя
typedef struct {
    Vec3 pos;
    Vec3 velocity;
    Vec3 startPos;
    Vec3 endPos;
    float progress;
    int hasDroppedBomb;
    int active;
} FighterJet;

// Структура для бомбы
typedef struct {
    Vec3 pos;
    Vec3 velocity;
    float fuse; // Таймер до взрыва
    int active;
} Bomb;

// Структура для взрыва
typedef struct {
    Vec3 pos;
    float currentRadius;
    float maxRadius;
    float lifetime;
    int active;
} Explosion;

// Состояние кинематографичной камеры
typedef struct {
    int isActive;
    Vec3 position;
    Vec3 target;
    float fov;
    float transitionProgress;
} CinematicState;

// Менеджер всего события
typedef struct {
    int isActive;
    float timer;
    FighterJet jets[3]; // Массив для 3-х истребителей
    Bomb bombs[3];
    Explosion explosions[3];
} AirstrikeEvent;

typedef enum {
    QUEST_LOCKED,
    QUEST_AVAILABLE,
    QUEST_ACTIVE,
    QUEST_COMPLETED
} QuestStatus;

typedef enum {
    OBJECTIVE_REACH_POINT,
    OBJECTIVE_COLLECT_ITEM,
    OBJECTIVE_KILL_ENEMY,
    OBJECTIVE_TALK_TO_NPC,
    OBJECTIVE_SURVIVE_TIME
} ObjectiveType;

typedef struct {
    ObjectiveType type;
    void* target;
    int currentProgress;
    int requiredProgress;
    char description[128];
} QuestObjective;

typedef struct QuestNode {
    int id;
    char name[64];
    char description[256];
    
    Vec3 worldPos;
    SDL_Color color;
    
    QuestStatus status;
    QuestObjective objectives[5];
    int numObjectives;
    
    int requiredQuests[5];
    int numRequired;
    int unlocksQuests[5];
    int numUnlocks;
    
    int expReward;
    int coinReward;
    
    float nodeScale;
    float pulsePhase;
} QuestNode;

typedef struct {
    QuestNode nodes[50];
    int numNodes;
    int activeQuestId;
    
    float connectionPulse;
} QuestSystem;

typedef enum {
    BOSS_STATE_IDLE,
    BOSS_STATE_CHASING,
    BOSS_STATE_ATTACKING_SLAM,
    BOSS_STATE_SUMMONING_GLITCHES,
    BOSS_STATE_VULNERABLE,
    BOSS_STATE_DEFEATED
} BossState;

typedef struct {
    Vec3 pos;
    float health;
    float maxHealth;
    
    BossState state;
    float stateTimer;

    Vec3 bodySize;
    Vec3 hammerSize;
} Boss;

typedef struct {
    Vec3 pos;
    Vec3 velocity;
    int active;
    float jitter;
} GlitchByte;

typedef enum {
    WORLD_STATE_WIREFRAME,
    WORLD_STATE_GRID_GROWING,
    WORLD_STATE_CUBE_COMPLETE,
    WORLD_STATE_MATERIALIZING,
    WORLD_STATE_TEXTURED,
    WORLD_STATE_REALISTIC
} WorldState;

typedef struct {
    WorldState currentState;
    float transitionProgress;
    
    float gridWallHeight;
    float gridDensity;
    float gridPulse;
    
    float polygonOpacity;
    float textureBlend;
    
    int skyboxEnabled;
    float skyboxAlpha;
    SDL_Color skyGradientTop;
    SDL_Color skyGradientBottom;
    
    float glitchIntensity;
    float chromaAberration;
} WorldEvolution;

WorldEvolution g_worldEvolution = {
    .currentState = WORLD_STATE_WIREFRAME,
    .transitionProgress = 0.0f,
    .gridWallHeight = 0.0f,
    .gridDensity = 1.0f,
    .gridPulse = 0.0f,
    .polygonOpacity = 0.0f,
    .textureBlend = 0.0f,
    .skyboxEnabled = 0,
    .skyboxAlpha = 0.0f,
    .skyGradientTop = {100, 120, 140, 255},
    .skyGradientBottom = {180, 190, 200, 255},
    .glitchIntensity = 0.0f,
    .chromaAberration = 0.0f
};

typedef enum {
    PROF_GAME_LOGIC,
    PROF_PHYSICS_COLLISIONS,
    PROF_RENDERING,
    PROF_OTHER,
    PROF_CATEGORY_COUNT
} ProfilerCategory;

typedef struct {
    const char* name;
    Uint64 startTime;
    Uint64 elapsedTicks;
    Uint32 calls;
    
    float percentage;
    Uint32 callsPerSecond;
    SDL_Color color;
} ProfilerData;

typedef enum {
    STATE_MAIN_MENU,
    STATE_SETTINGS,
    STATE_IN_GAME
} GameState;

GameState g_currentState;
int g_menuSelectedOption = 0;
int g_settingsSelectedOption = 0;
float g_menuRhombusAngle = 0.0f;

// === ИСПРАВЛЕНИЕ 3: Переименовываем глобальные переменные для ясности ===
ProfilerData g_profilerData[PROF_CATEGORY_COUNT];
int g_showProfiler = 0;
Uint64 g_perfFrequency;
Uint32 g_profilerUpdateTime = 0;

Boss g_rknChan;
GlitchByte g_glitchBytes[MAX_GLITCHES];
int g_activeGlitches = 0;
int g_bossFightActive = 0;

int g_coinsCollected = 0;
Coin g_coins[50];
int g_numCoins = 0;
Phone g_phone;
DayNightCycle g_dayNight;
float g_zBuffer[HEIGHT][WIDTH];
float g_timeScale = 1.0f;
PickupObject g_pickups[MAX_PICKUPS];
int g_numPickups = 0;

GlassShard g_shards[MAX_SHARDS];
int g_numShards = 0;
Trajectory g_trajectory;
HandsSystem g_hands;

AirstrikeEvent g_airstrike;
CinematicState g_cinematic;

int g_isExiting = 0;       // Флаг, что мы в процессе выхода
float g_exitFadeAlpha = 0.0f;

#define LUT_SIZE 3600 // Точность до 0.1 градуса
float sin_table[LUT_SIZE];
float cos_table[LUT_SIZE];

void init_fast_math() {
    printf("Initializing Math Look-Up Tables...\n");
    for (int i = 0; i < LUT_SIZE; i++) {
        float angle = (float)i * (2.0f * M_PI / LUT_SIZE);
        sin_table[i] = sinf(angle);
        cos_table[i] = cosf(angle);
    }
}

// Новые, быстрые функции
float fast_sin(float angle) {
    // Приводим угол к диапазону 0 - 2PI
    angle = fmodf(angle, 2.0f * M_PI);
    if (angle < 0) angle += 2.0f * M_PI;
    int index = (int)(angle * (LUT_SIZE / (2.0f * M_PI))) % LUT_SIZE;
    return sin_table[index];
}

float fast_cos(float angle) {
    angle = fmodf(angle, 2.0f * M_PI);
    if (angle < 0) angle += 2.0f * M_PI;
    int index = (int)(angle * (LUT_SIZE / (2.0f * M_PI))) % LUT_SIZE;
    return cos_table[index];
}
// === ИСПРАВЛЕНИЕ 4: Выносим логику в отдельные функции ===

// Функция для безопасного чтения строки из файла конфигурации
int readConfigLine(FILE* file, char* buffer, size_t bufferSize) {
    if (fgets(buffer, bufferSize, file) == NULL) {
        return 0;
    }
    // Удаляем символ новой строки
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = '\0';
    }
    return 1;
}

// Функция для парсинга значения из строки конфигурации
int parseConfigValue(const char* line, const char* key, float* value) {
    char lineKey[CONFIG_KEY_SIZE];
    float lineValue;
    
    if (sscanf(line, "%63[^=]=%f", lineKey, &lineValue) == 2) {
        if (strcmp(lineKey, key) == 0) {
            *value = lineValue;
            return 1;
        }
    }
    return 0;
}

void drawText(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y, SDL_Color color) {
    // ИСПРАВЛЕНИЕ: Используем UTF8 для поддержки кириллицы
    SDL_Surface* surface = TTF_RenderUTF8_Solid(font, text, color); 
    
    if (!surface) {
        // Если что-то пойдет не так, мы увидим ошибку в консоли
        printf("Не удалось создать поверхность для текста! Ошибка SDL_ttf: %s\n", TTF_GetError());
        return;
    }
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) { 
        SDL_FreeSurface(surface); 
        return; 
    }
    
    SDL_Rect destRect = { x, y, surface->w, surface->h };
    SDL_RenderCopy(renderer, texture, NULL, &destRect);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t);

void Profiler_Init() {
    g_perfFrequency = SDL_GetPerformanceFrequency();
    
    g_profilerData[PROF_GAME_LOGIC] = (ProfilerData){"game update", 0, 0, 0, 0.0f, 0, {100, 255, 100, 255}};
    g_profilerData[PROF_PHYSICS_COLLISIONS] = (ProfilerData){"collisions", 0, 0, 0, 0.0f, 0, {220, 200, 100, 255}};
    g_profilerData[PROF_RENDERING] = (ProfilerData){"projections", 0, 0, 0, 0.0f, 0, {150, 220, 150, 255}};
    g_profilerData[PROF_OTHER] = (ProfilerData){"other", 0, 0, 0, 0.0f, 0, {200, 200, 200, 255}};
    
    g_profilerUpdateTime = SDL_GetTicks();
}

void Profiler_Start(ProfilerCategory category) {
    if (!g_showProfiler) return;
    g_profilerData[category].startTime = SDL_GetPerformanceCounter();
}

void Profiler_End(ProfilerCategory category) {
    if (!g_showProfiler) return;
    g_profilerData[category].elapsedTicks += SDL_GetPerformanceCounter() - g_profilerData[category].startTime;
    g_profilerData[category].calls++;
}

void Profiler_Update() {
    if (!g_showProfiler) return;
    
    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - g_profilerUpdateTime >= 1000) {
        
        Uint64 totalTicks = 0;
        for (int i = 0; i < PROF_CATEGORY_COUNT; i++) {
            totalTicks += g_profilerData[i].elapsedTicks;
        }

        if (totalTicks > 0) {
            for (int i = 0; i < PROF_CATEGORY_COUNT; i++) {
                g_profilerData[i].percentage = ((double)g_profilerData[i].elapsedTicks / totalTicks) * 100.0;
                g_profilerData[i].callsPerSecond = g_profilerData[i].calls;
                
                g_profilerData[i].elapsedTicks = 0;
                g_profilerData[i].calls = 0;
            }
        }
        g_profilerUpdateTime = currentTime;
    }
}

void Profiler_Draw(SDL_Renderer* ren, TTF_Font* font) {
    if (!g_showProfiler) return;

    int x = 20, y = 20, w = 400, h = 25;
    
    for (int i = 0; i < PROF_CATEGORY_COUNT; i++) {
        ProfilerData* data = &g_profilerData[i];
        
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, data->color.r, data->color.g, data->color.b, 80);
        SDL_Rect bgRect = {x, y + i * h, w, h - 5};
        SDL_RenderFillRect(ren, &bgRect);
        
        int barWidth = (int)(w * (data->percentage / 100.0f));
        SDL_SetRenderDrawColor(ren, data->color.r, data->color.g, data->color.b, 255);
        SDL_Rect barRect = {x, y + i * h, barWidth, h - 5};
        SDL_RenderFillRect(ren, &barRect);
        
        char buffer[128];
        snprintf(buffer, 128, "%s: %u /s  (%.0f%%)", data->name, data->callsPerSecond, data->percentage);
        SDL_Color white = {255, 255, 255, 255};
        drawText(ren, font, buffer, x + 5, y + i * h + 2, white);
    }
}

// ИСПРАВЛЯЕМ: Инициализируем коллизии ДО квестов
    CollisionBox collisionBoxes[] = {
        { {0, 0, 0}, {-2.1, -2.1, -2.1, 2.1, 2.1, 2.1}, {255, 255, 255, 255} },
        { {8, -1, 5}, {-1.5, -1, -1.5, 1.5, 3, 1.5}, {100, 255, 100, 255} },  // Зелёная платформа
        { {-6, -1.5, 3}, {-2, -0.5, -2, 2, 0.5, 2}, {100, 100, 255, 255} },
        { {5, -1, -8}, {-3, -1, -1, 3, 1, 1}, {255, 100, 100, 255} },
        { {-10, 0, -5}, {-1, -2, -1, 1, 2, 1}, {255, 255, 100, 255} },
    };
    int numCollisionBoxes = sizeof(collisionBoxes) / sizeof(collisionBoxes[0]);

    // --- ПРОТОТИПЫ НОВЫХ ФУНКЦИЙ ---
void calculateTrajectory(Camera* cam, float power, Trajectory* traj, CollisionBox* boxes, int numBoxes, float gravity);
int intersectRayAABB(Vec3 rayOrigin, Vec3 rayDir, Vec3 boxMin, Vec3 boxMax, float* t);

Vec3 cross(Vec3 a, Vec3 b) {
    Vec3 r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    return r;
}
Vec3 normalize(Vec3 v) {
    float len = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len < 1e-6f) { Vec3 zero = {0, 0, 0}; return zero; }
    Vec3 r = {v.x/len, v.y/len, v.z/len};
    return r;
}
float dot(Vec3 a, Vec3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
float lerp(float a, float b, float t) {
    if (t > 1.0f) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    return a + (b - a) * t;
}

typedef struct {
    int x, y;
    float z;
} ProjectedPoint;

ProjectedPoint project_with_depth(Vec3 p, Camera cam) {
    float cameraEyeY = cam.y + cam.height + cam.currentBobY;
    float dx = p.x - cam.x;
    float dy = p.y - cameraEyeY;
    float dz = p.z - cam.z;

    float adjustedRotY = cam.rotY + cam.currentBobX * 0.02f;
    float sy = fast_sin(adjustedRotY), cy = fast_cos(adjustedRotY);
    float x_cam = cy * dx - sy * dz;
    float z_cam = sy * dx + cy * dz;

    float sx = fast_sin(cam.rotX), cx = fast_cos(cam.rotX);
    float y_cam = cx * dy - sx * z_cam;
    z_cam = sx * dy + cx * z_cam;

    float fov = g_fov;
    ProjectedPoint result;
    result.z = z_cam;

    if (z_cam <= 0.1f) {
        result.x = -9999;
        result.y = -9999;
    } else {
        result.x = (int)(WIDTH/2 + x_cam * fov / z_cam);
        result.y = (int)(HEIGHT/2 - y_cam * fov / z_cam);
    }
    
    return result;
}

void drawPixelWithZCheck_Fast(SDL_Renderer* ren, int x, int y, float z) {
    // Проверка границ и глубины
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z < g_zBuffer[y][x]) {
        // Просто рисуем точку. Цвет уже должен быть установлен снаружи.
        SDL_RenderDrawPoint(ren, x, y);
        // Обновляем Z-буфер
        g_zBuffer[y][x] = z;
    }
}

void clipAndDrawLine(SDL_Renderer* r, Vec3 p1, Vec3 p2, Camera cam, SDL_Color color) {
    // --- Шаг 1 и 2: Трансформация и отсечение (остаются без изменений) ---
    float cameraEyeY = cam.y + cam.height + cam.currentBobY;
    float dx1 = p1.x - cam.x, dy1 = p1.y - cameraEyeY, dz1 = p1.z - cam.z;
    float sy = fast_sin(cam.rotY), cy = fast_cos(cam.rotY);
    float sx = fast_sin(cam.rotX), cx = fast_cos(cam.rotX);
    float x1_cam = cy * dx1 - sy * dz1;
    float z1_cam_temp = sy * dx1 + cy * dz1;
    float y1_cam = cx * dy1 - sx * z1_cam_temp;
    float z1_cam = sx * dy1 + cx * z1_cam_temp;
    float dx2 = p2.x - cam.x, dy2 = p2.y - cameraEyeY, dz2 = p2.z - cam.z;
    float x2_cam = cy * dx2 - sy * dz2;
    float z2_cam_temp = sy * dx2 + cy * dz2;
    float y2_cam = cx * dy2 - sx * z2_cam_temp;
    float z2_cam = sx * dy2 + cx * z2_cam_temp;
    const float near_plane = 0.1f;
    if (z1_cam < near_plane && z2_cam < near_plane) return;
    if (z1_cam < near_plane) {
        float t = (near_plane - z1_cam) / (z2_cam - z1_cam);
        x1_cam = lerp(x1_cam, x2_cam, t); y1_cam = lerp(y1_cam, y2_cam, t); z1_cam = near_plane;
    }
    if (z2_cam < near_plane) {
        float t = (near_plane - z2_cam) / (z1_cam - z2_cam);
        x2_cam = lerp(x2_cam, x1_cam, t); y2_cam = lerp(y2_cam, y1_cam, t); z2_cam = near_plane;
    }
    
    // --- Шаг 3: Проекция (без изменений) ---
    float fov = g_fov;
    int sx1 = (int)(WIDTH/2 + x1_cam * fov / z1_cam);
    int sy1 = (int)(HEIGHT/2 - y1_cam * fov / z1_cam);
    int sx2 = (int)(WIDTH/2 + x2_cam * fov / z2_cam);
    int sy2 = (int)(HEIGHT/2 - y2_cam * fov / z2_cam);

    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);

    int dx = abs(sx2 - sx1);
    int dy = abs(sy2 - sy1);
    int steps = (dx > dy) ? dx : dy;

    // <<< УДАР #2: БЫСТРЫЙ ПУТЬ ДЛЯ КОРОТКИХ ЛИНИЙ >>>
    if (steps < 2) {
        drawPixelWithZCheck_Fast(r, sx1, sy1, z1_cam);
        return; // ВЫХОДИМ, ИЗБЕГАЯ ДОРОГОГО ЦИКЛА
    }

    if (steps == 0) { // Эта проверка на всякий случай
        drawPixelWithZCheck_Fast(r, sx1, sy1, z1_cam);
        return;
    }

    float x_inc = (float)(sx2 - sx1) / (float)steps;
    float y_inc = (float)(sy2 - sy1) / (float)steps;
    float z1_inv = 1.0f / z1_cam;
    float z2_inv = 1.0f / z2_cam;
    float z_inv_inc = (z2_inv - z1_inv) / (float)steps;
    float current_x = sx1, current_y = sy1, current_z_inv = z1_inv;

    for (int i = 0; i <= steps; i++) {
        float current_z = 1.0f / current_z_inv;
        drawPixelWithZCheck_Fast(r, (int)current_x, (int)current_y, current_z); // <<< Используем быструю функцию
        current_x += x_inc;
        current_y += y_inc;
        current_z_inv += z_inv_inc;
    }
}
// Вспомогательная функция для сортировки 3-х вершин по оси Y
void sortVerticesAscendingByY(ProjectedPoint* v1, ProjectedPoint* v2, ProjectedPoint* v3) {
    ProjectedPoint temp;
    if (v1->y > v2->y) { temp = *v1; *v1 = *v2; *v2 = temp; }
    if (v1->y > v3->y) { temp = *v1; *v1 = *v3; *v3 = temp; }
    if (v2->y > v3->y) { temp = *v2; *v2 = *v3; *v3 = temp; }
}

void fillTriangle(SDL_Renderer* ren, ProjectedPoint v1, ProjectedPoint v2, ProjectedPoint v3, SDL_Color color) {
    sortVerticesAscendingByY(&v1, &v2, &v3);

    // Если весь треугольник - это одна горизонтальная линия, выходим
    if (v3.y == v1.y) return;

    SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);

    // --- Верхняя половина треугольника (от v1 к v2) ---
    // Проверяем, есть ли вообще у верхней части высота. Если v1 и v2 на одной линии, эту часть рисовать не нужно.
    if (v2.y > v1.y) {
        float invslope1 = (float)(v2.x - v1.x) / (float)(v2.y - v1.y);
        float invslope2 = (float)(v3.x - v1.x) / (float)(v3.y - v1.y);
        // <<< ИСПРАВЛЕНИЕ: Добавляем проверку на ноль для Z-интерполяции >>>
        float z_invslope1 = (v2.y > v1.y) ? (1.0f/v2.z - 1.0f/v1.z) / (float)(v2.y - v1.y) : 0.0f;
        float z_invslope2 = (v3.y > v1.y) ? (1.0f/v3.z - 1.0f/v1.z) / (float)(v3.y - v1.y) : 0.0f;

        float curx1 = v1.x;
        float curx2 = v1.x;
        float curz1_inv = 1.0f/v1.z;
        float curz2_inv = 1.0f/v1.z;

        for (int scanlineY = v1.y; scanlineY < v2.y; scanlineY++) {
            // Рисуем горизонтальную линию
            int startX = (int)curx1, endX = (int)curx2;
            float z_start_inv = curz1_inv, z_end_inv = curz2_inv;
            if (startX > endX) {
                int tmpX = startX; startX = endX; endX = tmpX;
                float tmpZ = z_start_inv; z_start_inv = z_end_inv; z_end_inv = tmpZ;
            }
            float z_inv_span = (endX > startX) ? (z_end_inv - z_start_inv) / (float)(endX - startX) : 0.0f;

            for (int x = startX; x < endX; x++) {
                float z_inv = z_start_inv + (float)(x - startX) * z_inv_span;
                drawPixelWithZCheck_Fast(ren, x, scanlineY, 1.0f/z_inv);
            }
            curx1 += invslope1;
            curx2 += invslope2;
            curz1_inv += z_invslope1;
            curz2_inv += z_invslope2;
        }
    }

    // --- Нижняя половина треугольника (от v2 к v3) ---
    // Аналогичная проверка для нижней части
    if (v3.y > v2.y) {
        float invslope1 = (float)(v3.x - v2.x) / (float)(v3.y - v2.y);
        float invslope2 = (float)(v3.x - v1.x) / (float)(v3.y - v1.y); // Наклон длинной стороны не меняется
        // <<< ИСПРАВЛЕНИЕ: Добавляем проверку на ноль для Z-интерполяции >>>
        float z_invslope1 = (v3.y > v2.y) ? (1.0f/v3.z - 1.0f/v2.z) / (float)(v3.y - v2.y) : 0.0f;
        float z_invslope2 = (v3.y > v1.y) ? (1.0f/v3.z - 1.0f/v1.z) / (float)(v3.y - v1.y) : 0.0f;

        float curx1 = v2.x;
        float curx2 = v1.x + invslope2 * (float)(v2.y - v1.y); // Посчитаем где должна быть вторая точка
        float curz1_inv = 1.0f/v2.z;
        float curz2_inv = 1.0f/v1.z + z_invslope2 * (float)(v2.y - v1.y);

        for (int scanlineY = v2.y; scanlineY <= v3.y; scanlineY++) {
            // Рисуем горизонтальную линию
            int startX = (int)curx1, endX = (int)curx2;
            float z_start_inv = curz1_inv, z_end_inv = curz2_inv;
            if (startX > endX) {
               int tmpX = startX; startX = endX; endX = tmpX;
               float tmpZ = z_start_inv; z_start_inv = z_end_inv; z_end_inv = tmpZ;
            }
            float z_inv_span = (endX > startX) ? (z_end_inv - z_start_inv) / (float)(endX - startX) : 0.0f;
            
            for (int x = startX; x < endX; x++) {
                float z_inv = z_start_inv + (float)(x - startX) * z_inv_span;
                drawPixelWithZCheck_Fast(ren, x, scanlineY, 1.0f/z_inv);
            }
            curx1 += invslope1;
            curx2 += invslope2;
            curz1_inv += z_invslope1;
            curz2_inv += z_invslope2;
        }
    }
}

void updateWorldEvolution(float deltaTime) {
    WorldState oldState = g_worldEvolution.currentState;
    
    // Определяем состояние на основе монет
    if (g_coinsCollected < 10) {
        g_worldEvolution.currentState = WORLD_STATE_WIREFRAME;
    } else if (g_coinsCollected < 15) {
        g_worldEvolution.currentState = WORLD_STATE_GRID_GROWING;
    } else if (g_coinsCollected < 20) {
        g_worldEvolution.currentState = WORLD_STATE_CUBE_COMPLETE;
    } else if (g_coinsCollected < 30) {
        g_worldEvolution.currentState = WORLD_STATE_MATERIALIZING;
    } else if (g_coinsCollected < 40) {
        g_worldEvolution.currentState = WORLD_STATE_TEXTURED;
    } else {
        g_worldEvolution.currentState = WORLD_STATE_REALISTIC;
    }
    
    // Если состояние изменилось, запускаем переход
    if (oldState != g_worldEvolution.currentState) {
        g_worldEvolution.transitionProgress = 0.0f;
        g_worldEvolution.glitchIntensity = 1.0f; // Глюки при переходе
        printf("WORLD EVOLUTION: Entering %d state\n", g_worldEvolution.currentState);
    }
    
    // Обновляем параметры в зависимости от состояния
    switch(g_worldEvolution.currentState) {
        case WORLD_STATE_WIREFRAME:
            // Базовое состояние - просто пульсация
            g_worldEvolution.gridPulse = fast_sin(SDL_GetTicks() * 0.001f) * 0.1f;
            break;
            
        case WORLD_STATE_GRID_GROWING:
    // --- МЕДЛЕННЕЕ И ПЛАВНЕЕ ---
    g_worldEvolution.gridWallHeight = lerp(g_worldEvolution.gridWallHeight, 50.0f, deltaTime * 0.2f); // Было 0.5f
    g_worldEvolution.gridDensity = lerp(g_worldEvolution.gridDensity, 2.0f, deltaTime * 0.15f); // Было 3.0f и 0.3f
    break;

case WORLD_STATE_CUBE_COMPLETE:
    g_worldEvolution.gridWallHeight = 50.0f;
    // --- МЕДЛЕННАЯ ПУЛЬСАЦИЯ ---
    g_worldEvolution.gridPulse = fast_sin(SDL_GetTicks() * 0.0008f) * 0.15f; // Было 0.002f и 0.2f
    g_worldEvolution.polygonOpacity = fast_sin(SDL_GetTicks() * 0.001f) * 0.05f + 0.02f; // Плавнее
    break;
            
        case WORLD_STATE_MATERIALIZING:
            // Появляются полупрозрачные полигоны
            g_worldEvolution.polygonOpacity = lerp(g_worldEvolution.polygonOpacity, 0.5f, deltaTime * 0.2f);
            g_worldEvolution.chromaAberration = fast_sin(SDL_GetTicks() * 0.01f) * 0.02f;
            break;
            
        case WORLD_STATE_TEXTURED:
            // Текстуры и начало скайбокса
            g_worldEvolution.polygonOpacity = lerp(g_worldEvolution.polygonOpacity, 0.8f, deltaTime * 0.3f);
            g_worldEvolution.textureBlend = lerp(g_worldEvolution.textureBlend, 0.7f, deltaTime * 0.2f);
            g_worldEvolution.skyboxEnabled = 1;
            g_worldEvolution.skyboxAlpha = lerp(g_worldEvolution.skyboxAlpha, 0.3f, deltaTime * 0.1f);
            break;
            
        case WORLD_STATE_REALISTIC:
            // Полный рендер
            g_worldEvolution.polygonOpacity = 1.0f;
            g_worldEvolution.textureBlend = 1.0f;
            g_worldEvolution.skyboxAlpha = lerp(g_worldEvolution.skyboxAlpha, 1.0f, deltaTime * 0.2f);
            g_worldEvolution.gridWallHeight = lerp(g_worldEvolution.gridWallHeight, 0.0f, deltaTime * 0.5f);
            break;
    }
    
    // Обновляем прогресс перехода
    g_worldEvolution.transitionProgress = lerp(g_worldEvolution.transitionProgress, 1.0f, deltaTime * 2.0f);
    
    // Затухание глюков
    g_worldEvolution.glitchIntensity = lerp(g_worldEvolution.glitchIntensity, 0.0f, deltaTime * 3.0f);
}

void drawEvolvingWalls(SDL_Renderer* ren, Camera cam) {
    if (g_worldEvolution.gridWallHeight <= 0.01f) return;
    
    float h = g_worldEvolution.gridWallHeight;
    float density = g_worldEvolution.gridDensity;
    
    SDL_Color wallColor = {80, 80, 90, 255};
    float worldSize = 30.0f;
    
    // --- ОПТИМИЗАЦИЯ 1: Увеличиваем шаг, чтобы было меньше линий ---
    float step = fmaxf(4.0f, 8.0f / density); // Шаг не меньше 4.0 юнитов!

    // Вертикальные линии
    for (float i = -worldSize; i <= worldSize; i += step) {
        float waveOffset = fast_sin(i * 0.1f + SDL_GetTicks() * 0.0005f) * g_worldEvolution.gridPulse * 0.5f;
        float currentHeight = -2.0f + h + waveOffset;

        // Передняя и задняя стены
        clipAndDrawLine(ren, (Vec3){i, -2.0f, -worldSize}, (Vec3){i, currentHeight, -worldSize}, cam, wallColor);
        clipAndDrawLine(ren, (Vec3){i, -2.0f, worldSize}, (Vec3){i, currentHeight, worldSize}, cam, wallColor);

        // Левая и правая стены
        clipAndDrawLine(ren, (Vec3){-worldSize, -2.0f, i}, (Vec3){-worldSize, currentHeight, i}, cam, wallColor);
        clipAndDrawLine(ren, (Vec3){worldSize, -2.0f, i}, (Vec3){worldSize, currentHeight, i}, cam, wallColor);
    }

    // --- ОПТИМИЗАЦИЯ 2: Горизонтальные линии рисуем еще реже ---
    for (float y = -2.0f; y <= -2.0f + h; y += step * 2.0f) { // Шаг по Y в 2 раза больше!
        clipAndDrawLine(ren, (Vec3){-worldSize, y, -worldSize}, (Vec3){worldSize, y, -worldSize}, cam, wallColor);
        clipAndDrawLine(ren, (Vec3){-worldSize, y, worldSize}, (Vec3){worldSize, y, worldSize}, cam, wallColor);
        clipAndDrawLine(ren, (Vec3){-worldSize, y, -worldSize}, (Vec3){-worldSize, y, worldSize}, cam, wallColor);
        clipAndDrawLine(ren, (Vec3){worldSize, y, -worldSize}, (Vec3){worldSize, y, worldSize}, cam, wallColor);
    }

    // --- ОПТИМИЗАЦИЯ 3: Потолок рисуем только контуром и диагоналями ---
    if (g_worldEvolution.currentState >= WORLD_STATE_CUBE_COMPLETE) {
        float ceilingY = -2.0f + h;
        Vec3 corners[4] = {
            {-worldSize, ceilingY, -worldSize}, {worldSize, ceilingY, -worldSize},
            {worldSize, ceilingY, worldSize}, {-worldSize, ceilingY, worldSize}
        };
        // Рисуем периметр
        for (int i = 0; i < 4; i++) {
            clipAndDrawLine(ren, corners[i], corners[(i+1)%4], cam, wallColor);
        }
        // Рисуем диагонали
        clipAndDrawLine(ren, corners[0], corners[2], cam, wallColor);
        clipAndDrawLine(ren, corners[1], corners[3], cam, wallColor);
    }
}

// [Продолжение следует в следующем сообщении...]

void drawSkybox(SDL_Renderer* ren) {
    // УДАЛИ СТАРЫЙ КОД С ЦВЕТАМИ, ОСТАВЬ ТОЛЬКО ЭТО:
    if (!g_worldEvolution.skyboxEnabled || g_worldEvolution.skyboxAlpha < 0.01f) return;
    
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    
    SDL_Color skyTop = g_dayNight.skyTopColor;
    SDL_Color skyBottom = g_dayNight.skyBottomColor;
    
    for (int y = 0; y < HEIGHT; y += 4) { // Шаг 4 для скорости
        float t = (float)y / HEIGHT;
        
        SDL_Color finalColor = lerpColor(skyTop, skyBottom, t);
        finalColor.a = (Uint8)(g_worldEvolution.skyboxAlpha * 255);
        
        SDL_SetRenderDrawColor(ren, finalColor.r, finalColor.g, finalColor.b, finalColor.a);
        SDL_Rect lineRect = {0, y, WIDTH, 4};
        SDL_RenderFillRect(ren, &lineRect);
    }
    
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}
// Эффект глюков при переходах
void applyGlitchEffect(SDL_Renderer* ren) {
    if (g_worldEvolution.glitchIntensity < 0.01f) return;
    
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
    
    for (int i = 0; i < 10; i++) {
        if (rand() % 100 < g_worldEvolution.glitchIntensity * 100) {
            int x = rand() % WIDTH;
            int y = rand() % HEIGHT;
            int w = rand() % 200 + 50;
            int h = rand() % 20 + 5;
            
            Uint8 color = rand() % 50;
            SDL_SetRenderDrawColor(ren, color, 0, color * 2, 100);
            SDL_Rect glitchRect = {x, y, w, h};
            SDL_RenderFillRect(ren, &glitchRect);
        }
    }
    
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

// Полигональная заливка для продвинутых состояний
void drawFilledTriangle(SDL_Renderer* ren, Vec3 p1, Vec3 p2, Vec3 p3, Camera cam, SDL_Color color) {
    if (g_worldEvolution.polygonOpacity < 0.01f) return;
    
    ProjectedPoint pp1 = project_with_depth(p1, cam);
    ProjectedPoint pp2 = project_with_depth(p2, cam);
    ProjectedPoint pp3 = project_with_depth(p3, cam);
    
    // Простая проверка видимости
    if (pp1.z <= 0.1f || pp2.z <= 0.1f || pp3.z <= 0.1f) return;
    if (pp1.x < -WIDTH || pp1.x > WIDTH*2) return;
    
    // Применяем прозрачность
    color.a = (Uint8)(g_worldEvolution.polygonOpacity * 255);
    
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, color.r, color.g, color.b, color.a);
    
    // Рисуем треугольник линиями (SDL не умеет заливать треугольники напрямую)
    // Но создаём иллюзию заливки частыми горизонтальными линиями
    int minY = fminf(pp1.y, fminf(pp2.y, pp3.y));
    int maxY = fmaxf(pp1.y, fmaxf(pp2.y, pp3.y));
    
    for (int y = minY; y <= maxY; y += 2) {
        // Тут нужна растеризация треугольника, но для простоты просто рисуем линии
        SDL_RenderDrawLine(ren, pp1.x, pp1.y, pp2.x, pp2.y);
        SDL_RenderDrawLine(ren, pp2.x, pp2.y, pp3.x, pp3.y);
        SDL_RenderDrawLine(ren, pp3.x, pp3.y, pp1.x, pp1.y);
    }
    
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

void initHandsSystem() {
    g_hands.leftPos = (Vec3){-0.45f, -0.9f, 0.35f};
    g_hands.rightPos = (Vec3){0.45f, -0.9f, 0.35f};
    
    float inwardAngle = 0.5f;
    g_hands.leftRot = (Vec3){0, inwardAngle, 0};
    g_hands.rightRot = (Vec3){0, -inwardAngle, 0};
    
    g_hands.currentState = HAND_STATE_IDLE;
    g_hands.targetState = HAND_STATE_IDLE;
    g_hands.stateTransition = 1.0f;
    
    g_hands.walkPhase = 0.0f;
    g_hands.runPhase = 0.0f;
    g_hands.inspectPhase = 0.0f;
    g_hands.throwPhase = 0.0f;
    g_hands.idlePhase = 0.0f;
    
    g_hands.heldObject = NULL;
    g_hands.pulledObject = NULL;
    g_hands.targetedObject = NULL;
    g_hands.flick.isActive = 0;
    g_hands.flick.progress = 0.0f;
    g_hands.reachDistance = 2.0f;
    g_hands.currentThrowPower = THROW_POWER_DEFAULT;
}

void spawnBottle(Vec3 pos) {
    if (g_numPickups >= MAX_PICKUPS) return;
    
    PickupObject* bottle = &g_pickups[g_numPickups];
    bottle->pos = pos;
    bottle->velocity = (Vec3){0, 0, 0};
    bottle->rotation = (Vec3){0, 0, 0};
    bottle->rotVelocity = (Vec3){0, 0, 0};
    
    bottle->type = PICKUP_TYPE_BOTTLE;
    bottle->state = PICKUP_STATE_IDLE;
    
    bottle->health = 1.0f;
    bottle->mass = 0.5f;
    bottle->bounciness = 0.3f;
    
    bottle->size = (Vec3){0.15f, 0.4f, 0.15f};
    bottle->color = (SDL_Color){100, 150, 100, 200}; // Зеленоватое стекло
    
    bottle->breakable = 1;
    bottle->breakThreshold = 5.0f; // Разбивается при скорости > 5
    
    g_numPickups++;
    printf("🍾 Bottle spawned at (%.1f, %.1f, %.1f)\n", pos.x, pos.y, pos.z);
}

void spawnGlassShards(Vec3 pos, int count) {
    for (int i = 0; i < count && g_numShards < MAX_SHARDS; i++) {
        GlassShard* shard = &g_shards[g_numShards];
        
        // Случайное направление разлёта
        float angle = ((float)rand() / RAND_MAX) * 2.0f * M_PI;
        float pitch = ((float)rand() / RAND_MAX - 0.5f) * M_PI;
        float speed = 2.0f + ((float)rand() / RAND_MAX) * 3.0f;
        
        shard->pos = pos;
        shard->velocity.x = fast_cos(angle) * fast_cos(pitch) * speed;
        shard->velocity.y = fast_sin(pitch) * speed + 2.0f; // Подлетают вверх
        shard->velocity.z = fast_sin(angle) * fast_cos(pitch) * speed;
        
        shard->rotation = (Vec3){
            ((float)rand() / RAND_MAX) * 2.0f * M_PI,
            ((float)rand() / RAND_MAX) * 2.0f * M_PI,
            ((float)rand() / RAND_MAX) * 2.0f * M_PI
        };
        
        shard->lifetime = 2.0f + ((float)rand() / RAND_MAX) * 2.0f;
        shard->size = 0.05f + ((float)rand() / RAND_MAX) * 0.1f;
        
        // Цвет стекла с вариациями
        int brightness = 100 + rand() % 100;
        shard->color = (SDL_Color){brightness, brightness + 50, brightness, 200};
        
        g_numShards++;
    }
}

// --- ОБНОВЛЕНИЕ ФИЗИКИ ---

void updatePickupPhysics(PickupObject* obj, float deltaTime, CollisionBox* boxes, int numBoxes, Camera* cam) {
    if (obj->state == PICKUP_STATE_BROKEN || obj->state == PICKUP_STATE_HELD) return;
    
    // [НОВЫЙ КОД] Логика притягивания
    if (obj->state == PICKUP_STATE_PULLED) {
        Vec3 targetPos = {cam->x, cam->y + cam->height, cam->z};
        Vec3 dir = {targetPos.x - obj->pos.x, targetPos.y - obj->pos.y, targetPos.z - obj->pos.z};
        float dist = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);

        if (dist < 1.0f) { // Объект пойман!
            obj->state = PICKUP_STATE_HELD;
            g_hands.heldObject = obj;
            g_hands.pulledObject = NULL;
            obj->velocity = (Vec3){0,0,0};
            printf("🤏 Object caught!\n");
            return;
        }

        dir = normalize(dir);
        // Ускоряем объект к игроку
        obj->velocity.x = lerp(obj->velocity.x, dir.x * PULL_FORCE, deltaTime * 5.0f);
        obj->velocity.y = lerp(obj->velocity.y, dir.y * PULL_FORCE, deltaTime * 5.0f);
        obj->velocity.z = lerp(obj->velocity.z, dir.z * PULL_FORCE, deltaTime * 5.0f);

        obj->pos.x += obj->velocity.x * deltaTime;
        obj->pos.y += obj->velocity.y * deltaTime;
        obj->pos.z += obj->velocity.z * deltaTime;
        return;
    }

    if (obj->state == PICKUP_STATE_IDLE || obj->state == PICKUP_STATE_THROWN) {
        // Гравитация
        obj->velocity.y -= 9.8f * deltaTime;
        
        // Обновляем позицию
        Vec3 newPos = {
            obj->pos.x + obj->velocity.x * deltaTime,
            obj->pos.y + obj->velocity.y * deltaTime,
            obj->pos.z + obj->velocity.z * deltaTime
        };
        
        // Проверка коллизии с полом
        if (newPos.y <= -2.0f + obj->size.y/2) {
            newPos.y = -2.0f + obj->size.y/2;
            
            // Проверяем, разбивается ли при ударе
            float impactSpeed = fabsf(obj->velocity.y);
            if (obj->breakable && impactSpeed > obj->breakThreshold) {
                printf("💥 Bottle shattered! Impact speed: %.2f\n", impactSpeed);
                obj->state = PICKUP_STATE_BROKEN;
                spawnGlassShards(obj->pos, 15 + rand() % 10);
                return;
            }
            
            // Отскок
            obj->velocity.y = -obj->velocity.y * obj->bounciness;
            
            // Трение
            obj->velocity.x *= 0.8f;
            obj->velocity.z *= 0.8f;
        }
        
        // Проверка коллизий с объектами
        for (int i = 0; i < numBoxes; i++) {
            CollisionBox* box = &boxes[i];
            
            // Простая проверка AABB
            if (newPos.x + obj->size.x/2 > box->pos.x + box->bounds.minX &&
                newPos.x - obj->size.x/2 < box->pos.x + box->bounds.maxX &&
                newPos.y + obj->size.y/2 > box->pos.y + box->bounds.minY &&
                newPos.y - obj->size.y/2 < box->pos.y + box->bounds.maxY &&
                newPos.z + obj->size.z/2 > box->pos.z + box->bounds.minZ &&
                newPos.z - obj->size.z/2 < box->pos.z + box->bounds.maxZ) {
                
                // Отталкиваем объект
                Vec3 center = box->pos;
                Vec3 toObj = {newPos.x - center.x, 0, newPos.z - center.z};
                float len = sqrtf(toObj.x*toObj.x + toObj.z*toObj.z);
                if (len > 0.01f) {
                    toObj.x /= len;
                    toObj.z /= len;
                    
                    newPos.x = center.x + toObj.x * (box->bounds.maxX + obj->size.x);
                    newPos.z = center.z + toObj.z * (box->bounds.maxZ + obj->size.z);
                }
                
                // Проверяем разбивание при ударе о стену
                float hitSpeed = sqrtf(obj->velocity.x*obj->velocity.x + 
                                      obj->velocity.z*obj->velocity.z);
                if (obj->breakable && hitSpeed > obj->breakThreshold) {
                    obj->state = PICKUP_STATE_BROKEN;
                    spawnGlassShards(obj->pos, 12);
                    return;
                }
                
                // Отскок от стены
                obj->velocity.x *= -obj->bounciness;
                obj->velocity.z *= -obj->bounciness;
            }
        }
        
        obj->pos = newPos;
        
        // Вращение в полёте
        if (obj->state == PICKUP_STATE_THROWN) {
            obj->rotation.x += obj->rotVelocity.x * deltaTime;
            obj->rotation.y += obj->rotVelocity.y * deltaTime;
            obj->rotation.z += obj->rotVelocity.z * deltaTime;
        }
        
        // Останавливаем, если почти не движется
        float speed = sqrtf(obj->velocity.x*obj->velocity.x + 
                          obj->velocity.z*obj->velocity.z);
        if (speed < 0.1f && fabsf(obj->velocity.y) < 0.1f && 
            obj->state == PICKUP_STATE_THROWN) {
            obj->state = PICKUP_STATE_IDLE;
            obj->velocity = (Vec3){0, 0, 0};
            obj->rotVelocity = (Vec3){0, 0, 0};
        }
    }
}

// [НОВЫЙ КОД] Функция для рейкастинга
// Проверяет пересечение луча с AABB (ограничивающим параллелепипедом)
// Возвращает 1 если есть пересечение, и записывает расстояние до него в t
int intersectRayAABB(Vec3 rayOrigin, Vec3 rayDir, Vec3 boxMin, Vec3 boxMax, float* t) {
    float tmin = (boxMin.x - rayOrigin.x) / rayDir.x;
    float tmax = (boxMax.x - rayOrigin.x) / rayDir.x;

    if (tmin > tmax) { float tmp = tmin; tmin = tmax; tmax = tmp; }

    float tymin = (boxMin.y - rayOrigin.y) / rayDir.y;
    float tymax = (boxMax.y - rayOrigin.y) / rayDir.y;

    if (tymin > tymax) { float tmp = tymin; tymin = tymax; tymax = tmp; }

    if ((tmin > tymax) || (tymin > tmax)) return 0;

    if (tymin > tmin) tmin = tymin;
    if (tymax < tmax) tmax = tymax;

    float tzmin = (boxMin.z - rayOrigin.z) / rayDir.z;
    float tzmax = (boxMax.z - rayOrigin.z) / rayDir.z;

    if (tzmin > tzmax) { float tmp = tzmin; tzmin = tzmax; tzmax = tmp; }

    if ((tmin > tzmax) || (tzmin > tmax)) return 0;
    
    if (tzmin > tmin) tmin = tzmin;
    if (tzmax < tmax) tmax = tzmax;

    *t = tmin;
    return tmax > 0;
}

// [НОВЫЙ КОД] Логика "гравитационных перчаток"
void updateGravityGlove(Camera* cam, float deltaTime) {
    // Сбрасываем подсветку с предыдущего объекта, если он больше не цель
    if (g_hands.targetedObject && g_hands.targetedObject->state != PICKUP_STATE_PULLED) {
        g_hands.targetedObject->color = g_hands.targetedObject->originalColor;
        g_hands.targetedObject = NULL;
    }

    // Если мы уже что-то держим или притягиваем, выходим
    if (g_hands.heldObject || g_hands.pulledObject) {
        return;
    }

    // Только в состоянии прицеливания ищем цель
    if (g_hands.currentState != HAND_STATE_AIMING) {
        return;
    }

    // 1. Создаем луч из камеры
    Vec3 rayOrigin = {cam->x, cam->y + cam->height, cam->z};
    Vec3 rayDir = {
        fast_sin(cam->rotY) * fast_cos(cam->rotX),
        -fast_sin(cam->rotX), // Направление по вертикали
        fast_cos(cam->rotY) * fast_cos(cam->rotX)
    };
    rayDir = normalize(rayDir);

    // 2. Ищем пересечение с объектами
    float closest_t = RAYCAST_MAX_DISTANCE;
    PickupObject* potentialTarget = NULL;

    for (int i = 0; i < g_numPickups; i++) {
        PickupObject* obj = &g_pickups[i];
        if (obj->state != PICKUP_STATE_IDLE) continue;

        Vec3 boxMin = {obj->pos.x - obj->size.x/2, obj->pos.y - obj->size.y/2, obj->pos.z - obj->size.z/2};
        Vec3 boxMax = {obj->pos.x + obj->size.x/2, obj->pos.y + obj->size.y/2, obj->pos.z + obj->size.z/2};
        
        float t;
        if (intersectRayAABB(rayOrigin, rayDir, boxMin, boxMax, &t)) {
            if (t < closest_t) {
                closest_t = t;
                potentialTarget = obj;
            }
        }
    }
    
    g_hands.targetedObject = potentialTarget;

    // 3. Если нашли цель, подсвечиваем ее
    if (g_hands.targetedObject) {
        g_hands.targetedObject->color = (SDL_Color){255, 165, 0, 255}; // Оранжевая подсветка
    }
}

void updateShards(float deltaTime) {
    for (int i = 0; i < g_numShards; i++) {
        GlassShard* shard = &g_shards[i];
        
        if (shard->lifetime <= 0) continue;
        
        shard->lifetime -= deltaTime;
        
        // Физика
        shard->velocity.y -= 15.0f * deltaTime; // Гравитация
        
        shard->pos.x += shard->velocity.x * deltaTime;
        shard->pos.y += shard->velocity.y * deltaTime;
        shard->pos.z += shard->velocity.z * deltaTime;
        
        // Отскок от пола
        if (shard->pos.y <= -2.0f) {
            shard->pos.y = -2.0f;
            shard->velocity.y *= -0.3f;
            shard->velocity.x *= 0.7f;
            shard->velocity.z *= 0.7f;
        }
        
        // Вращение
        shard->rotation.x += deltaTime * 5.0f;
        shard->rotation.y += deltaTime * 3.0f;
        
        // Затухание
        if (shard->lifetime < 0.5f) {
            shard->color.a = (Uint8)(shard->lifetime * 2.0f * 200);
        }
    }
    
    // Удаляем мёртвые осколки (сдвигаем живые в начало массива)
    int writeIdx = 0;
    for (int i = 0; i < g_numShards; i++) {
        if (g_shards[i].lifetime > 0) {
            if (writeIdx != i) {
                g_shards[writeIdx] = g_shards[i];
            }
            writeIdx++;
        }
    }
    g_numShards = writeIdx;
}

// --- ОБНОВЛЕНИЕ РУК ---

void updateHandsState(Camera* cam, float deltaTime) {
    HandState newState = HAND_STATE_IDLE;

    const Uint8* keyState = SDL_GetKeyboardState(NULL);

    if (g_hands.heldObject) {
        newState = HAND_STATE_HOLDING;
    } else if (keyState[SDL_SCANCODE_E]) { // Прицеливание на E
        newState = HAND_STATE_AIMING;
    } else if (g_hands.flick.isActive) {
        newState = HAND_STATE_PULLING;
    } else if (cam->isMoving) {
        newState = cam->isRunning ? HAND_STATE_RUNNING : HAND_STATE_WALKING;
    } else if (cam->vy > 0.1f) {
        newState = HAND_STATE_JUMPING;
    }
    
    // Плавный переход между состояниями
    if (newState != g_hands.targetState) {
        g_hands.targetState = newState;
        g_hands.stateTransition = 0.0f;
    }
    
    g_hands.stateTransition = lerp(g_hands.stateTransition, 1.0f, deltaTime * 5.0f);
    
    if (g_hands.stateTransition > 0.95f) {
        g_hands.currentState = g_hands.targetState;
        g_hands.stateTransition = 1.0f;
    }

    // Обновление анимации флика
    if (g_hands.flick.isActive) {
        g_hands.flick.progress += deltaTime * 8.0f;
        if (g_hands.flick.progress >= 2.0f) {
            g_hands.flick.isActive = 0;
            g_hands.flick.progress = 0.0f;
        }
    }
}

void updateHandsAnimation(Camera* cam, float deltaTime) {
    // Базовые позиции рук
    Vec3 idleLeft = {-0.3f, -0.3f, 0.4f};
    Vec3 idleRight = {0.3f, -0.3f, 0.4f};
    
    Vec3 targetLeft = idleLeft;
    Vec3 targetRight = idleRight;
    
    switch(g_hands.currentState) {
        case HAND_STATE_IDLE:
            // Лёгкое покачивание
            g_hands.idlePhase += deltaTime * 2.0f;
            targetLeft.y += fast_sin(g_hands.idlePhase) * 0.02f;
            targetRight.y += fast_sin(g_hands.idlePhase + 0.5f) * 0.02f;
            break;
            
        case HAND_STATE_WALKING:
            // Маятниковое движение при ходьбе
            g_hands.walkPhase += deltaTime * 6.0f;
            targetLeft.z += fast_sin(g_hands.walkPhase) * 0.1f;
            targetLeft.y += fabsf(fast_sin(g_hands.walkPhase * 2)) * 0.05f;
            targetRight.z += fast_sin(g_hands.walkPhase + M_PI) * 0.1f;
            targetRight.y += fabsf(fast_sin(g_hands.walkPhase * 2 + M_PI)) * 0.05f;
            break;
            
        case HAND_STATE_RUNNING:
            // Более активное движение при беге
            g_hands.runPhase += deltaTime * 10.0f;
            targetLeft.z += fast_sin(g_hands.runPhase) * 0.2f;
            targetLeft.x -= fabsf(fast_sin(g_hands.runPhase)) * 0.1f;
            targetLeft.y += fabsf(fast_sin(g_hands.runPhase * 2)) * 0.1f;
            
            targetRight.z += fast_sin(g_hands.runPhase + M_PI) * 0.2f;
            targetRight.x += fabsf(fast_sin(g_hands.runPhase + M_PI)) * 0.1f;
            targetRight.y += fabsf(fast_sin(g_hands.runPhase * 2 + M_PI)) * 0.1f;
            break;
            
        case HAND_STATE_JUMPING:
            // Руки поднимаются вверх
            targetLeft.y += 0.3f;
            targetLeft.x -= 0.1f;
            targetRight.y += 0.3f;
            targetRight.x += 0.1f;
            break;
            
        case HAND_STATE_REACHING:
            // Правая рука тянется вперёд
            targetRight.z += 0.3f;
            targetRight.y += 0.1f;
            // Пальцы "хватают" (имитация через позицию)
            targetRight.x += fast_sin(SDL_GetTicks() * 0.005f) * 0.02f;
            break;
            
        case HAND_STATE_HOLDING:
            // Держим объект
            targetRight.z += 0.2f;
            targetRight.y += 0.05f;
            targetRight.x += 0.05f;
            break;
            
        case HAND_STATE_THROWING:
            // Замах и бросок
            g_hands.throwPhase += deltaTime * 8.0f;
            if (g_hands.throwPhase < M_PI) {
                // Замах назад
                targetRight.z -= 0.3f * fast_sin(g_hands.throwPhase);
                targetRight.y += 0.2f * fast_sin(g_hands.throwPhase);
            } else {
                // Бросок вперёд
                targetRight.z += 0.5f * fast_sin(g_hands.throwPhase - M_PI);
                targetRight.y -= 0.1f * fast_sin(g_hands.throwPhase - M_PI);
            }
            
            if (g_hands.throwPhase > 2 * M_PI) {
                g_hands.throwPhase = 0;
                g_hands.currentState = HAND_STATE_IDLE;
            }
            break;
            
        case HAND_STATE_INSPECTING:
            // Осмотр рук
            g_hands.inspectPhase += deltaTime * 3.0f;
            
            float inspectProgress = g_hands.inspectPhase;
            
            if (inspectProgress < 1.0f) {
                // Поднимаем руки
                targetLeft.y += inspectProgress * 0.3f;
                targetLeft.z += inspectProgress * 0.2f;
                targetRight.y += inspectProgress * 0.3f;
                targetRight.z += inspectProgress * 0.2f;
            } else if (inspectProgress < 3.0f) {
                // Шевелим пальцами (имитация)
                targetLeft.y += 0.3f;
                targetLeft.z += 0.2f;
                targetLeft.x += fast_sin((inspectProgress - 1.0f) * 4.0f) * 0.05f;
                
                targetRight.y += 0.3f;
                targetRight.z += 0.2f;
                targetRight.x -= fast_sin((inspectProgress - 1.0f) * 4.0f) * 0.05f;
            } else if (inspectProgress < 4.0f) {
                // Поворот тыльной стороной
                float turnProgress = inspectProgress - 3.0f;
                targetLeft.y += 0.3f;
                targetLeft.z += 0.2f - turnProgress * 0.1f;
                g_hands.leftRot.y = turnProgress * M_PI;
                
                targetRight.y += 0.3f;
                targetRight.z += 0.2f - turnProgress * 0.1f;
                g_hands.rightRot.y = -turnProgress * M_PI;
            } else if (inspectProgress < 5.0f) {
                // Обратный поворот
                float turnProgress = 1.0f - (inspectProgress - 4.0f);
                targetLeft.y += 0.3f;
                targetLeft.z += 0.1f + turnProgress * 0.1f;
                g_hands.leftRot.y = turnProgress * M_PI;
                
                targetRight.y += 0.3f;
                targetRight.z += 0.1f + turnProgress * 0.1f;
                g_hands.rightRot.y = -turnProgress * M_PI;
            } else {
                // Опускаем руки обратно
                g_hands.inspectPhase = 0;
                g_hands.currentState = HAND_STATE_IDLE;
                g_hands.leftRot.y = 0;
                g_hands.rightRot.y = 0;
            }
            break;

            case HAND_STATE_AIMING:
            // Правая рука вытягивается вперед для прицеливания
            targetRight.z -= 0.2f;
            targetRight.y += 0.1f;
            targetRight.x -= 0.1f;
            // Левая рука чуть приподнимается
            targetLeft.y += 0.05f;
            targetLeft.x += 0.05f;
            break;

        case HAND_STATE_PULLING: {
            // Анимация "флика"
            float flickAmount = 0.0f;
            if (g_hands.flick.progress < 1.0f) {
                flickAmount = g_hands.flick.progress; // Движение назад
            } else {
                flickAmount = 1.0f - (g_hands.flick.progress - 1.0f); // Движение вперед
            }
            targetRight.z += flickAmount * 0.3f; // Рука дергается назад-вперед
            targetRight.y -= flickAmount * 0.1f;
            break;
        }
    }
    
    // Применяем плавную интерполяцию
    float lerpSpeed = 8.0f;
    if (g_hands.currentState == HAND_STATE_THROWING) {
        lerpSpeed = 15.0f; // Быстрее для броска
    }
    
    g_hands.leftPos.x = lerp(g_hands.leftPos.x, targetLeft.x, deltaTime * lerpSpeed);
    g_hands.leftPos.y = lerp(g_hands.leftPos.y, targetLeft.y, deltaTime * lerpSpeed);
    g_hands.leftPos.z = lerp(g_hands.leftPos.z, targetLeft.z, deltaTime * lerpSpeed);
    
    g_hands.rightPos.x = lerp(g_hands.rightPos.x, targetRight.x, deltaTime * lerpSpeed);
    g_hands.rightPos.y = lerp(g_hands.rightPos.y, targetRight.y, deltaTime * lerpSpeed);
    g_hands.rightPos.z = lerp(g_hands.rightPos.z, targetRight.z, deltaTime * lerpSpeed);
}

// [ИЗМЕНЕНО] Бросок объекта, теперь с набором силы
void throwObject(Camera* cam, float power) {
    if (!g_hands.heldObject) return;
    
    g_hands.heldObject->state = PICKUP_STATE_THROWN;
    
    Vec3 throwDir = {
        fast_sin(cam->rotY) * fast_cos(cam->rotX),
        -fast_sin(cam->rotX),
        fast_cos(cam->rotY) * fast_cos(cam->rotX)
    };
    throwDir = normalize(throwDir);

    g_hands.heldObject->velocity.x = throwDir.x * power;
    g_hands.heldObject->velocity.y = throwDir.y * power;
    g_hands.heldObject->velocity.z = throwDir.z * power;
    
    // Позиция броска
    g_hands.heldObject->pos.x = cam->x + throwDir.x * 1.5f;
    g_hands.heldObject->pos.y = cam->y + cam->height + throwDir.y;
    g_hands.heldObject->pos.z = cam->z + throwDir.z * 1.5f;

    g_hands.heldObject = NULL;
    g_hands.currentThrowPower = THROW_POWER_DEFAULT; // Сброс силы
    g_trajectory.numPoints = 0; // Скрыть траекторию

    printf("💨 Threw object with power %.1f!\n", power);
}

// [НОВЫЙ КОД] Расчет траектории
void calculateTrajectory(Camera* cam, float power, Trajectory* traj, CollisionBox* boxes, int numBoxes, float gravity) {
    traj->numPoints = 0;
    traj->didHit = 0;

    Vec3 pos;
    Vec3 vel;
    
    // Начальные условия
    Vec3 throwDir = {
        fast_sin(cam->rotY) * fast_cos(cam->rotX),
        -fast_sin(cam->rotX),
        fast_cos(cam->rotY) * fast_cos(cam->rotX)
    };
    throwDir = normalize(throwDir);

    pos.x = cam->x + throwDir.x * 1.5f;
    pos.y = cam->y + cam->height + throwDir.y;
    pos.z = cam->z + throwDir.z * 1.5f;
    
    vel.x = throwDir.x * power;
    vel.y = throwDir.y * power;
    vel.z = throwDir.z * power;

    for (int i = 0; i < TRAJECTORY_STEPS; i++) {
        // Простое симулирование физики
        vel.y -= gravity * TRAJECTORY_TIME_STEP;
        pos.x += vel.x * TRAJECTORY_TIME_STEP;
        pos.y += vel.y * TRAJECTORY_TIME_STEP;
        pos.z += vel.z * TRAJECTORY_TIME_STEP;
        
        traj->points[i] = pos;
        traj->numPoints++;

        // Проверка столкновений (упрощенная)
        if (pos.y < -2.0f) { // Столкновение с полом
             traj->didHit = 1;
             break;
        }
        for(int j=0; j < numBoxes; ++j) {
            // ... тут можно добавить проверку столкновений с collisionBoxes
        }
    }
}

// [НОВЫЙ КОД] Отрисовка траектории
void drawTrajectory(SDL_Renderer* ren, Camera cam, Trajectory* traj) {
    if (traj->numPoints < 2) return;

    SDL_Color color = traj->didHit ? (SDL_Color){255, 100, 100, 255} : (SDL_Color){150, 200, 255, 255};
    
    // Рисуем пунктирной линией
    for (int i = 0; i < traj->numPoints - 1; i += 2) {
        clipAndDrawLine(ren, traj->points[i], traj->points[i+1], cam, color);
    }
}

// --- ОТРИСОВКА ---

void drawPickupObject(SDL_Renderer* ren, PickupObject* obj, Camera cam) {
    if (obj->state == PICKUP_STATE_BROKEN) return;
    if (obj->state == PICKUP_STATE_HELD) return; // Не рисуем, если в руке
    
    // Вершины объекта (простой параллелепипед)
    Vec3 vertices[8];
    float sx = obj->size.x / 2;
    float sy = obj->size.y / 2;
    float sz = obj->size.z / 2;
    
    // Создаём вершины с учётом вращения
    Vec3 baseVerts[8] = {
        {-sx, -sy, -sz}, {sx, -sy, -sz}, {sx, sy, -sz}, {-sx, sy, -sz},
        {-sx, -sy, sz}, {sx, -sy, sz}, {sx, sy, sz}, {-sx, sy, sz}
    };
    
    // Применяем вращение
    for (int i = 0; i < 8; i++) {
        Vec3 v = baseVerts[i];
        
        // Вращение вокруг Y
        float cy = fast_cos(obj->rotation.y);
        float sy = fast_sin(obj->rotation.y);
        float newX = v.x * cy - v.z * sy;
        float newZ = v.x * sy + v.z * cy;
        v.x = newX;
        v.z = newZ;
        
        // Вращение вокруг X
        float cx = fast_cos(obj->rotation.x);
        float sx = fast_sin(obj->rotation.x);
        float newY = v.y * cx - v.z * sx;
        newZ = v.y * sx + v.z * cx;
        v.y = newY;
        v.z = newZ;
        
        // Добавляем позицию объекта
        vertices[i].x = obj->pos.x + v.x;
        vertices[i].y = obj->pos.y + v.y;
        vertices[i].z = obj->pos.z + v.z;
    }
    
    // Рёбра для отрисовки
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    
    // Рисуем бутылку
    for (int i = 0; i < 12; i++) {
        clipAndDrawLine(ren, vertices[edges[i][0]], vertices[edges[i][1]], cam, obj->color);
    }
    
    // Если это бутылка, добавляем горлышко
    if (obj->type == PICKUP_TYPE_BOTTLE) {
        Vec3 neckBottom = {obj->pos.x, obj->pos.y + sy, obj->pos.z};
        Vec3 neckTop = {obj->pos.x, obj->pos.y + sy + 0.1f, obj->pos.z};
        
        // Горлышко (простые линии)
        float neckRadius = sx * 0.5f;
        for (int i = 0; i < 4; i++) {
            float angle = (float)i / 4 * 2 * M_PI;
            Vec3 p1 = {
                neckBottom.x + fast_cos(angle) * neckRadius,
                neckBottom.y,
                neckBottom.z + fast_sin(angle) * neckRadius
            };
            Vec3 p2 = {
                neckTop.x + fast_cos(angle) * neckRadius * 0.7f,
                neckTop.y,
                neckTop.z + fast_sin(angle) * neckRadius * 0.7f
            };
            clipAndDrawLine(ren, p1, p2, cam, obj->color);
        }
    }
}

void drawGlassShards(SDL_Renderer* ren, Camera cam) {
    for (int i = 0; i < g_numShards; i++) {
        GlassShard* shard = &g_shards[i];
        if (shard->lifetime <= 0) continue;
        
        // Простой треугольник для осколка
        Vec3 p1 = {
            shard->pos.x + fast_cos(shard->rotation.x) * shard->size,
            shard->pos.y,
            shard->pos.z + fast_sin(shard->rotation.x) * shard->size
        };
        Vec3 p2 = {
            shard->pos.x + fast_cos(shard->rotation.y) * shard->size,
            shard->pos.y + shard->size,
            shard->pos.z + fast_sin(shard->rotation.y) * shard->size
        };
        Vec3 p3 = {
            shard->pos.x,
            shard->pos.y + shard->size * 0.5f,
            shard->pos.z
        };
        
        clipAndDrawLine(ren, p1, p2, cam, shard->color);
        clipAndDrawLine(ren, p2, p3, cam, shard->color);
        clipAndDrawLine(ren, p3, p1, cam, shard->color);
    }
}
// Вспомогательная функция для вращения точки вокруг центра
Vec3 rotatePoint(Vec3 point, Vec3 center, float angleX, float angleY, float angleZ) {
    Vec3 result = point;
    
    result.x -= center.x;
    result.y -= center.y;
    result.z -= center.z;
    
    if (fabsf(angleY) > 0.001f) {
        float cy = fast_cos(angleY); float sy = fast_sin(angleY);
        float newX = result.x * cy - result.z * sy;
        float newZ = result.x * sy + result.z * cy;
        result.x = newX; result.z = newZ;
    }
    if (fabsf(angleX) > 0.001f) {
        float cx = fast_cos(angleX); float sx = fast_sin(angleX);
        float newY = result.y * cx - result.z * sx;
        float newZ = result.y * sx + result.z * cx;
        result.y = newY; result.z = newZ;
    }
    if (fabsf(angleZ) > 0.001f) {
        float cz = fast_cos(angleZ); float sz = fast_sin(angleZ);
        float newX = result.x * cz - result.y * sz;
        float newY = result.x * sz + result.y * cz;
        result.x = newX; result.y = newY;
    }
    
    result.x += center.x;
    result.y += center.y;
    result.z += center.z;
    
    return result;
}

// Структура для пальца
typedef struct {
    Vec3 base;      // Основание пальца
    Vec3 middle;    // Средний сустав
    Vec3 tip;       // Кончик
    float bendAngle; // Угол сгиба
} Finger;

void draw3DFinger(SDL_Renderer* ren, Finger* finger, Camera cam, SDL_Color color, float thickness) {
    // Сегмент 1: База -> Средний сустав
    Vec3 seg1[8];
    float t = thickness;
    
    // Создаём 8 точек для первого сегмента (параллелепипед)
    seg1[0] = (Vec3){finger->base.x - t, finger->base.y - t, finger->base.z - t};
    seg1[1] = (Vec3){finger->base.x + t, finger->base.y - t, finger->base.z - t};
    seg1[2] = (Vec3){finger->base.x + t, finger->base.y - t, finger->base.z + t};
    seg1[3] = (Vec3){finger->base.x - t, finger->base.y - t, finger->base.z + t};
    
    seg1[4] = (Vec3){finger->middle.x - t*0.8f, finger->middle.y - t*0.8f, finger->middle.z - t*0.8f};
    seg1[5] = (Vec3){finger->middle.x + t*0.8f, finger->middle.y - t*0.8f, finger->middle.z - t*0.8f};
    seg1[6] = (Vec3){finger->middle.x + t*0.8f, finger->middle.y - t*0.8f, finger->middle.z + t*0.8f};
    seg1[7] = (Vec3){finger->middle.x - t*0.8f, finger->middle.y - t*0.8f, finger->middle.z + t*0.8f};
    
    // Рисуем первый сегмент
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, // Низ
        {4,5},{5,6},{6,7},{7,4}, // Верх
        {0,4},{1,5},{2,6},{3,7}  // Вертикальные
    };
    
    for (int i = 0; i < 12; i++) {
        clipAndDrawLine(ren, seg1[edges[i][0]], seg1[edges[i][1]], cam, color);
    }
    
    // Сегмент 2: Средний сустав -> Кончик
    Vec3 seg2[8];
    t *= 0.8f; // Сужаем к кончику
    
    seg2[0] = (Vec3){finger->middle.x - t, finger->middle.y - t, finger->middle.z - t};
    seg2[1] = (Vec3){finger->middle.x + t, finger->middle.y - t, finger->middle.z - t};
    seg2[2] = (Vec3){finger->middle.x + t, finger->middle.y - t, finger->middle.z + t};
    seg2[3] = (Vec3){finger->middle.x - t, finger->middle.y - t, finger->middle.z + t};
    
    seg2[4] = (Vec3){finger->tip.x - t*0.5f, finger->tip.y - t*0.5f, finger->tip.z - t*0.5f};
    seg2[5] = (Vec3){finger->tip.x + t*0.5f, finger->tip.y - t*0.5f, finger->tip.z - t*0.5f};
    seg2[6] = (Vec3){finger->tip.x + t*0.5f, finger->tip.y - t*0.5f, finger->tip.z + t*0.5f};
    seg2[7] = (Vec3){finger->tip.x - t*0.5f, finger->tip.y - t*0.5f, finger->tip.z + t*0.5f};
    
    for (int i = 0; i < 12; i++) {
        clipAndDrawLine(ren, seg2[edges[i][0]], seg2[edges[i][1]], cam, color);
    }
}
static Vec3 transform_hand_vertex(Vec3 localPoint, const Vec3* handPos, const Vec3* handRot, const Camera* cam) {
    // a) Применяем собственное вращение руки (поворот внутрь)
    Vec3 v = rotatePoint(localPoint, (Vec3){0,0,0}, handRot->x, handRot->y, handRot->z);

    // b) Смещаем руку в ее позицию относительно камеры (вниз и вбок)
    v.x += handPos->x;
    v.y += handPos->y;
    v.z += handPos->z;

    // c) Вращаем точку вместе с камерой
    float sinY = fast_sin(cam->rotY); float cosY = fast_cos(cam->rotY);
    float sinX = fast_sin(cam->rotX); float cosX = fast_cos(cam->rotX);

    // Вращение по Y (поворот влево-вправо)
    float rotatedX = v.x * cosY - v.z * sinY;
    float rotatedZ = v.x * sinY + v.z * cosY;
    
    // Вращение по X (взгляд вверх-вниз)
    float finalY = v.y * cosX - rotatedZ * sinX;
    float finalZ = v.y * sinX + rotatedZ * cosX;

    // d) Прибавляем к мировой позиции камеры
    return (Vec3){
        cam->x + rotatedX,
        (cam->y + cam->height + cam->currentBobY) + finalY,
        cam->z + finalZ
    };
}

void drawVolumetricSegment(SDL_Renderer* ren, Vec3 p1, Vec3 p2, float thickness, Camera cam, SDL_Color color) {
    Vec3 dir = normalize((Vec3){p2.x - p1.x, p2.y - p1.y, p2.z - p1.z});
    // Находим перпендикулярные векторы, чтобы "построить" объем
    Vec3 up = {0, 1, 0};
    Vec3 right = normalize(cross(dir, up));
    up = normalize(cross(right, dir));

    right.x *= thickness; right.y *= thickness; right.z *= thickness;
    up.x *= thickness; up.y *= thickness; up.z *= thickness;

    // 8 вершин призмы
    Vec3 verts[8] = {
        {p1.x + right.x + up.x, p1.y + right.y + up.y, p1.z + right.z + up.z},
        {p1.x - right.x + up.x, p1.y - right.y + up.y, p1.z - right.z + up.z},
        {p1.x - right.x - up.x, p1.y - right.y - up.y, p1.z - right.z - up.z},
        {p1.x + right.x - up.x, p1.y + right.y - up.y, p1.z + right.z - up.z},
        {p2.x + right.x + up.x, p2.y + right.y + up.y, p2.z + right.z + up.z},
        {p2.x - right.x + up.x, p2.y - right.y + up.y, p2.z - right.z + up.z},
        {p2.x - right.x - up.x, p2.y - right.y - up.y, p2.z - right.z - up.z},
        {p2.x + right.x - up.x, p2.y + right.y - up.y, p2.z + right.z - up.z},
    };
    
    int edges[12][2] = {{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}};
    for(int i=0; i<12; ++i) {
        clipAndDrawLine(ren, verts[edges[i][0]], verts[edges[i][1]], cam, color);
    }
}

// ФИНАЛЬНАЯ, ИСПРАВЛЕННАЯ ВЕРСЯ. РУКИ ЖЕСТКО ПРИВЯЗАНЫ К КАМЕРЕ.
void draw3DHand(SDL_Renderer* ren, Vec3 handPos, Vec3 handRot, Camera cam, int isRight) {
    SDL_Color skinColor = {220, 180, 140, 255};
    SDL_Color darkSkinColor = {200, 160, 120, 255};
    
    // ЛАДОНЬ
    float pW = 0.08f, pH = 0.12f, pD = 0.02f;
    Vec3 localPalmVerts[8] = {
        {-pW/2,-pH/2,-pD/2}, {pW/2,-pH/2,-pD/2}, {pW/2, pH/2,-pD/2}, {-pW/2, pH/2,-pD/2},
        {-pW/2,-pH/2, pD/2}, {pW/2,-pH/2, pD/2}, {pW/2, pH/2, pD/2}, {-pW/2, pH/2, pD/2}
    };
    
    Vec3 finalPalmVerts[8];
    for (int i = 0; i < 8; ++i) {
        finalPalmVerts[i] = transform_hand_vertex(localPalmVerts[i], &handPos, &handRot, &cam);
    }
    
    int palmEdges[12][2] = {{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}};
    for (int i=0; i<12; ++i) {
        clipAndDrawLine(ren, finalPalmVerts[palmEdges[i][0]], finalPalmVerts[palmEdges[i][1]], cam, skinColor);
    }

    // ПАЛЬЦЫ
    // ПАЛЬЦЫ
    float fLen = 0.08f, fSpace = pW / 4.0f;
    float thickness = 0.015f;
    float bend = 0.0f;

    if (g_hands.heldObject && isRight) bend = 0.8f;
    else if (g_hands.currentState == HAND_STATE_AIMING && isRight) bend = 0.2f;

    for (int i = 0; i < 4; i++) {
        float fX = (i - 1.5f) * fSpace;
        
        // Суставы
        Vec3 base = transform_hand_vertex((Vec3){fX, pH/2, 0}, &handPos, &handRot, &cam);
        Vec3 mid = transform_hand_vertex((Vec3){fX, pH/2 + fLen/2, fLen/2 * fast_sin(bend)}, &handPos, &handRot, &cam);
        Vec3 tip = transform_hand_vertex((Vec3){fX, pH/2 + fLen, fLen * fast_sin(bend)}, &handPos, &handRot, &cam);

        // Рисуем два сегмента
        drawVolumetricSegment(ren, base, mid, thickness, cam, skinColor);
        drawVolumetricSegment(ren, mid, tip, thickness * 0.8f, cam, skinColor);
    }
    
    // БОЛЬШОЙ ПАЛЕЦ
    float thumbSide = isRight ? pW/2 : -pW/2;
    Vec3 t_base = transform_hand_vertex((Vec3){thumbSide, -pH/4, 0.01f}, &handPos, &handRot, &cam);
    Vec3 t_mid = transform_hand_vertex((Vec3){thumbSide + (isRight?0.03f:-0.03f), 0, 0.04f}, &handPos, &handRot, &cam);
    Vec3 t_tip = transform_hand_vertex((Vec3){thumbSide + (isRight?0.05f:-0.05f), 0.02f, 0.05f}, &handPos, &handRot, &cam);
    clipAndDrawLine(ren, t_base, t_mid, cam, skinColor);
    clipAndDrawLine(ren, t_mid, t_tip, cam, skinColor);

    // ЗАПЯСТЬЕ
    Vec3 wristEnd = transform_hand_vertex((Vec3){0, -pH/2 - 0.08f, 0}, &handPos, &handRot, &cam);
    clipAndDrawLine(ren, finalPalmVerts[0], wristEnd, cam, darkSkinColor);
    clipAndDrawLine(ren, finalPalmVerts[1], wristEnd, cam, darkSkinColor);
    clipAndDrawLine(ren, finalPalmVerts[4], wristEnd, cam, darkSkinColor);
    clipAndDrawLine(ren, finalPalmVerts[5], wristEnd, cam, darkSkinColor);
}

// Обновлённая функция отрисовки обеих рук
void drawHands(SDL_Renderer* ren, Camera cam) {
    // Определяем, нужно ли показывать руки
    int shouldDrawHands = 0;
    float handsAlpha = 1.0f;
    
    // Всегда показываем при осмотре
    if (g_hands.currentState == HAND_STATE_INSPECTING) {
        shouldDrawHands = 1;
        handsAlpha = 1.0f;
    }
    // Показываем при взгляде вниз
    else if (cam.rotX < -0.3f) {
        shouldDrawHands = 1;
        handsAlpha = fmaxf(0.3f, fminf(1.0f, (-cam.rotX - 0.3f) * 2));
    }
    // Показываем при взаимодействии
    else if (g_hands.currentState == HAND_STATE_REACHING || 
             g_hands.currentState == HAND_STATE_HOLDING ||
             g_hands.currentState == HAND_STATE_THROWING) {
        shouldDrawHands = 1;
        handsAlpha = 1.0f;
    }
    // При ходьбе/беге показываем слегка
    else if (g_hands.currentState == HAND_STATE_WALKING || 
             g_hands.currentState == HAND_STATE_RUNNING) {
        shouldDrawHands = 1;
        handsAlpha = 0.5f;
    }
    
    if (!shouldDrawHands) return;
    
    // Рисуем обе руки
    draw3DHand(ren, g_hands.leftPos, g_hands.leftRot, cam, 0);
    draw3DHand(ren, g_hands.rightPos, g_hands.rightRot, cam, 1);
    
    // Если держим объект, рисуем его в правой руке
    if (g_hands.heldObject) {
        float cameraEyeY = cam.y + cam.height;
        
        // Позиционируем объект чуть впереди правой руки
        Vec3 holdOffset = {
            g_hands.rightPos.x + 0.05f,
            g_hands.rightPos.y + 0.1f,
            g_hands.rightPos.z + 0.15f
        };
        
        // Применяем поворот камеры
        float camSin = fast_sin(cam.rotY);
        float camCos = fast_cos(cam.rotY);
        
        g_hands.heldObject->pos.x = cam.x + camCos * holdOffset.x - camSin * holdOffset.z;
        g_hands.heldObject->pos.y = cameraEyeY + holdOffset.y;
        g_hands.heldObject->pos.z = cam.z + camSin * holdOffset.x + camCos * holdOffset.z;
        
        // Поворачиваем объект вместе с камерой
        g_hands.heldObject->rotation.y = -cam.rotY;
        g_hands.heldObject->rotation.x = -cam.rotX * 0.5f;
        
        drawPickupObject(ren, g_hands.heldObject, cam);
    }
}

void updateCameraBob(Camera* cam, float deltaTime) {
    float speed = sqrtf(cam->vx * cam->vx + cam->vz * cam->vz);
    
    if (speed > 0.01f) {
        cam->isMoving = 1;
        
        float bobFrequency = cam->isRunning ? 12.0f : 8.0f;
        float bobAmplitude = cam->isRunning ? 0.08f : (cam->isCrouching ? 0.03f : 0.05f);
        
        cam->bobPhase += bobFrequency * deltaTime * speed * 5.0f;
        cam->bobAmount = lerp(cam->bobAmount, 1.0f, deltaTime * 8.0f);
        
        float verticalBob = fast_sin(cam->bobPhase) * bobAmplitude * cam->bobAmount;
        float horizontalBob = fast_sin(cam->bobPhase * 0.5f) * bobAmplitude * 0.5f * cam->bobAmount;
        
        cam->currentBobY = lerp(cam->currentBobY, verticalBob, deltaTime * 15.0f);
        cam->currentBobX = lerp(cam->currentBobX, horizontalBob, deltaTime * 15.0f);
    } else {
        cam->isMoving = 0;
        
        cam->bobAmount = lerp(cam->bobAmount, 0.0f, deltaTime * 6.0f);
        cam->currentBobY = lerp(cam->currentBobY, 0.0f, deltaTime * 8.0f);
        cam->currentBobX = lerp(cam->currentBobX, 0.0f, deltaTime * 8.0f);
    }
    
    if (!cam->isMoving) {
        float breathe = fast_sin(SDL_GetTicks() * 0.001f) * 0.01f;
        cam->currentBobY += breathe;
    }
}

int checkCollision(float x, float y, float z, float radius, CollisionBox* boxes, int numBoxes) {
    for (int i = 0; i < numBoxes; i++) {
        CollisionBox* box = &boxes[i];
        
        float boxMinX = box->pos.x + box->bounds.minX;
        float boxMaxX = box->pos.x + box->bounds.maxX;
        float boxMinY = box->pos.y + box->bounds.minY;
        float boxMaxY = box->pos.y + box->bounds.maxY;
        float boxMinZ = box->pos.z + box->bounds.minZ;
        float boxMaxZ = box->pos.z + box->bounds.maxZ;
        
        boxMinX -= radius;
        boxMaxX += radius;
        boxMinY -= radius;
        boxMaxY += radius;
        boxMinZ -= radius;
        boxMaxZ += radius;
        
        if (x >= boxMinX && x <= boxMaxX &&
            y >= boxMinY && y <= boxMaxY &&
            z >= boxMinZ && z <= boxMaxZ) {
            return 1;
        }
    }
    return 0;
}

// Добавь после функции checkCollision:

// Проверяет, находится ли точка внутри какого-либо объекта
int isPositionInsideBox(Vec3 pos, CollisionBox* boxes, int numBoxes) {
    for (int i = 0; i < numBoxes; i++) {
        CollisionBox* box = &boxes[i];
        
        // Проверяем, находится ли точка внутри границ бокса
        if (pos.x >= box->pos.x + box->bounds.minX &&
            pos.x <= box->pos.x + box->bounds.maxX &&
            pos.y >= box->pos.y + box->bounds.minY &&
            pos.y <= box->pos.y + box->bounds.maxY &&
            pos.z >= box->pos.z + box->bounds.minZ &&
            pos.z <= box->pos.z + box->bounds.maxZ) {
            return 1; // Позиция занята
        }
    }
    return 0; // Позиция свободна
}

// Находит безопасную позицию для спавна монеты
Vec3 findSafeSpawnPosition(float baseX, float baseY, float baseZ, CollisionBox* boxes, int numBoxes) {
    Vec3 pos = {baseX, baseY, baseZ};
    
    // Если позиция свободна, возвращаем её
    if (!isPositionInsideBox(pos, boxes, numBoxes)) {
        return pos;
    }
    
    // Иначе ищем свободную позицию по спирали вокруг
    float searchRadius = 1.0f;
    int attempts = 0;
    
    while (attempts < 50) { // Максимум 50 попыток
        float angle = (float)attempts * 0.5f;
        float radius = searchRadius + (float)attempts * 0.2f;
        
        pos.x = baseX + fast_cos(angle) * radius;
        pos.z = baseZ + fast_sin(angle) * radius;
        pos.y = baseY;
        
        if (!isPositionInsideBox(pos, boxes, numBoxes)) {
            return pos; // Нашли свободную позицию
        }
        
        attempts++;
    }
    
    // Если не нашли, поднимаем выше
    pos.x = baseX;
    pos.z = baseZ;
    pos.y = baseY + 3.0f;
    return pos;
}

int checkWallCollision(float x, float y, float z, float radius, CollisionBox* boxes, int numBoxes) {
    for (int i = 0; i < numBoxes; i++) {
        CollisionBox* box = &boxes[i];
        
        float boxMinX = box->pos.x + box->bounds.minX;
        float boxMaxX = box->pos.x + box->bounds.maxX;
        float boxMinY = box->pos.y + box->bounds.minY;
        float boxMaxY = box->pos.y + box->bounds.maxY;
        float boxMinZ = box->pos.z + box->bounds.minZ;
        float boxMaxZ = box->pos.z + box->bounds.maxZ;
        
        if (y + radius > boxMinY && y - radius < boxMaxY) {
            if (x + radius > boxMinX && x - radius < boxMaxX &&
                z + radius > boxMinZ && z - radius < boxMaxZ) {
                
                float distToTop = fabsf(y - (boxMaxY + radius));
                if (distToTop < 0.2f) {
                    continue;
                }
                
                return 1;
            }
        }
    }
    return 0;
}

float getGroundHeight(float x, float z, float playerRadius, CollisionBox* boxes, int numBoxes, int* foundGround) {
    float highestGround = -999.0f;
    *foundGround = 0;
    
    for (int i = 0; i < numBoxes; i++) {
        CollisionBox* box = &boxes[i];
        
        if (x >= box->pos.x + box->bounds.minX - playerRadius &&
            x <= box->pos.x + box->bounds.maxX + playerRadius &&
            z >= box->pos.z + box->bounds.minZ - playerRadius &&
            z <= box->pos.z + box->bounds.maxZ + playerRadius) {
            
            float boxTop = box->pos.y + box->bounds.maxY;
            
            if (boxTop > highestGround) {
                highestGround = boxTop;
                *foundGround = 1;
            }
        }
    }
    
    return highestGround;
}

int isGrounded(Camera* cam, float playerRadius, CollisionBox* boxes, int numBoxes) {
    if (cam->y <= 0.01f && cam->vy <= 0) {
        return 1;
    }
    
    int foundGround;
    float groundHeight = getGroundHeight(cam->x, cam->z, playerRadius, boxes, numBoxes, &foundGround);
    
    if (foundGround) {
        float distToGround = cam->y - (groundHeight + playerRadius);
        if (distToGround <= 0.1f && distToGround >= -0.1f && cam->vy <= 0) {
            return 1;
        }
    }
    
    return 0;
}

void drawOptimizedBox(SDL_Renderer* ren, CollisionBox* box, Camera cam) {
    // 1. Получаем 8 вершин бокса в мировых координатах (как и раньше)
    Vec3 vertices[8];
    vertices[0] = (Vec3){box->pos.x + box->bounds.minX, box->pos.y + box->bounds.minY, box->pos.z + box->bounds.minZ};
    vertices[1] = (Vec3){box->pos.x + box->bounds.maxX, box->pos.y + box->bounds.minY, box->pos.z + box->bounds.minZ};
    vertices[2] = (Vec3){box->pos.x + box->bounds.maxX, box->pos.y + box->bounds.maxY, box->pos.z + box->bounds.minZ};
    vertices[3] = (Vec3){box->pos.x + box->bounds.minX, box->pos.y + box->bounds.maxY, box->pos.z + box->bounds.minZ};
    vertices[4] = (Vec3){box->pos.x + box->bounds.minX, box->pos.y + box->bounds.minY, box->pos.z + box->bounds.maxZ};
    vertices[5] = (Vec3){box->pos.x + box->bounds.maxX, box->pos.y + box->bounds.minY, box->pos.z + box->bounds.maxZ};
    vertices[6] = (Vec3){box->pos.x + box->bounds.maxX, box->pos.y + box->bounds.maxY, box->pos.z + box->bounds.maxZ};
    vertices[7] = (Vec3){box->pos.x + box->bounds.minX, box->pos.y + box->bounds.maxY, box->pos.z + box->bounds.maxZ};

    // 2. Определяем геометрию куба: 6 граней, каждая из 4 вершин.
    // Это индексы вершин из массива vertices[]
    static const int faces[6][4] = {
        {0, 1, 2, 3}, // Передняя
        {5, 4, 7, 6}, // Задняя
        {4, 0, 3, 7}, // Левая
        {1, 5, 6, 2}, // Правая
        {3, 2, 6, 7}, // Верхняя
        {4, 5, 1, 0}  // Нижняя
    };

    // 3. Нормали для каждой из 6 граней (векторы, "смотрящие" наружу)
    static const Vec3 face_normals[6] = {
        {0, 0, -1}, // Передняя
        {0, 0, 1},  // Задняя
        {-1, 0, 0}, // Левая
        {1, 0, 0},  // Правая
        {0, 1, 0},  // Верхняя
        {0, -1, 0}  // Нижняя
    };

    // 4. Главный цикл: проходим по 6 граням, а не 12 ребрам
    for (int i = 0; i < 6; i++) {
        // Вектор от камеры к центру объекта (можно использовать любую точку на грани, центр проще всего)
        Vec3 to_face = {
            box->pos.x - cam.x,
            box->pos.y - cam.y,
            box->pos.z - cam.z
        };

        // Скалярное произведение (dot product).
        // Если результат < 0, значит нормаль грани и вектор к камере смотрят
        // в противоположные стороны. Это значит, что грань "смотрит" на нас!
        if (dot(face_normals[i], to_face) < 0) {
            
            // Грань видима! Рисуем 4 ребра, которые ее составляют.
            const int* face_verts = faces[i];
            clipAndDrawLine(ren, vertices[face_verts[0]], vertices[face_verts[1]], cam, box->color);
            clipAndDrawLine(ren, vertices[face_verts[1]], vertices[face_verts[2]], cam, box->color);
            clipAndDrawLine(ren, vertices[face_verts[2]], vertices[face_verts[3]], cam, box->color);
            clipAndDrawLine(ren, vertices[face_verts[3]], vertices[face_verts[0]], cam, box->color);
        }
    }
}

void drawMaterializedFloor(SDL_Renderer* ren, Camera cam) {
    if (g_worldEvolution.currentState < WORLD_STATE_MATERIALIZING) return;
    
    float opacity = g_worldEvolution.polygonOpacity;
    if (opacity < 0.01f) return;
    
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    
    // Цвет пола меняется с прогрессом
    Uint8 baseColor = 40 + (Uint8)(g_worldEvolution.textureBlend * 60);
    
    // --- ОПТИМИЗАЦИЯ: Увеличиваем размер плиток ---
    float tileSize = 4.0f;  // Было 2.0f
    int viewRange = 12;      // Было 15
    
    for (float x = -viewRange; x < viewRange; x += tileSize) {
        for (float z = -viewRange; z < viewRange; z += tileSize) {
            // --- ОПТИМИЗАЦИЯ: Более строгая проверка расстояния ---
            float distToCam = sqrtf(powf(x - cam.x, 2) + powf(z - cam.z, 2));
            if (distToCam > 15.0f) continue;  // Было 20
            
            // Шахматный паттерн
            int checkX = (int)(x / tileSize);
            int checkZ = (int)(z / tileSize);
            int isEven = (checkX + checkZ) % 2 == 0;
            
            SDL_Color tileColor;
            if (g_worldEvolution.currentState >= WORLD_STATE_TEXTURED) {
                if (isEven) {
                    tileColor = (SDL_Color){baseColor, baseColor + 10, baseColor + 15, (Uint8)(opacity * 255)};
                } else {
                    tileColor = (SDL_Color){baseColor - 10, baseColor, baseColor + 5, (Uint8)(opacity * 255)};
                }
            } else {
                tileColor = (SDL_Color){baseColor, baseColor, baseColor + 20, (Uint8)(opacity * 200)};
            }
            
            Vec3 corners[4] = {
                {x, -2.0f, z},
                {x + tileSize, -2.0f, z},
                {x + tileSize, -2.0f, z + tileSize},
                {x, -2.0f, z + tileSize}
            };
            
            // --- СУПЕР ОПТИМИЗАЦИЯ: Вместо заливки рисуем крест и контур ---
            if (g_worldEvolution.currentState >= WORLD_STATE_REALISTIC) {
                // Только контур + диагонали для иллюзии заливки
                for (int i = 0; i < 4; i++) {
                    clipAndDrawLine(ren, corners[i], corners[(i+1)%4], cam, tileColor);
                }
                // Диагонали для эффекта заливки
                clipAndDrawLine(ren, corners[0], corners[2], cam, tileColor);
                clipAndDrawLine(ren, corners[1], corners[3], cam, tileColor);
                
                // Центральный крест
                Vec3 midH1 = {x + tileSize/2, -2.0f, z};
                Vec3 midH2 = {x + tileSize/2, -2.0f, z + tileSize};
                Vec3 midV1 = {x, -2.0f, z + tileSize/2};
                Vec3 midV2 = {x + tileSize, -2.0f, z + tileSize/2};
                clipAndDrawLine(ren, midH1, midH2, cam, tileColor);
                clipAndDrawLine(ren, midV1, midV2, cam, tileColor);
            } else {
                // Только контур для производительности
                for (int i = 0; i < 4; i++) {
                    clipAndDrawLine(ren, corners[i], corners[(i+1)%4], cam, tileColor);
                }
            }
        }
    }
    
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
}

void drawMaterializedBox(SDL_Renderer* ren, CollisionBox* box, Camera cam) {
    // В режиме реализма делаем грани непрозрачными
    float opacity = g_worldEvolution.polygonOpacity;
    
    if (g_worldEvolution.currentState >= WORLD_STATE_REALISTIC) {
        opacity = 1.0f;  // Полная непрозрачность в режиме реализма
    }
    
    // Каркас рисуем только если не в режиме реализма
    if (g_worldEvolution.currentState < WORLD_STATE_REALISTIC) {
        drawOptimizedBox(ren, box, cam);
    }
    
    // Материализованные грани
    if (g_worldEvolution.currentState >= WORLD_STATE_MATERIALIZING) {
        SDL_Color materialColor;
        
        if (g_worldEvolution.currentState >= WORLD_STATE_REALISTIC) {
            // В режиме реализма - полные насыщенные цвета
            materialColor = (SDL_Color){
                box->color.r,
                box->color.g,
                box->color.b,
                255  // Полностью непрозрачный
            };
        } else {
            // В промежуточных режимах - полупрозрачность
            materialColor = (SDL_Color){
                (Uint8)(box->color.r * 0.7f),
                (Uint8)(box->color.g * 0.7f),
                (Uint8)(box->color.b * 0.7f),
                (Uint8)(opacity * 150)
            };
        }
        
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        
        Vec3 center = box->pos;
        
        // --- ОПТИМИЗАЦИЯ: Рисуем только видимые грани ---
        Vec3 toCam = {cam.x - center.x, cam.y - center.y, cam.z - center.z};
        
        // Определяем какие грани видны
        int drawFront = (toCam.z < 0);
        int drawBack = (toCam.z > 0);
        int drawLeft = (toCam.x < 0);
        int drawRight = (toCam.x > 0);
        int drawTop = (toCam.y > center.y);
        
        float step = (g_worldEvolution.currentState >= WORLD_STATE_REALISTIC) ? 0.15f : 0.3f;
        
        // Передняя грань
        if (drawFront) {
            for (float y = box->bounds.minY; y < box->bounds.maxY; y += step) {
                Vec3 p1 = {center.x + box->bounds.minX, center.y + y, center.z + box->bounds.minZ};
                Vec3 p2 = {center.x + box->bounds.maxX, center.y + y, center.z + box->bounds.minZ};
                clipAndDrawLine(ren, p1, p2, cam, materialColor);
            }
        }
        
        // Верхняя грань (важна для платформ)
        if (drawTop) {
            SDL_Color topColor = materialColor;
            if (g_worldEvolution.currentState >= WORLD_STATE_REALISTIC) {
                // Верх светлее (освещение)
                topColor.r = fminf(255, topColor.r + 30);
                topColor.g = fminf(255, topColor.g + 30);
                topColor.b = fminf(255, topColor.b + 30);
            }
            
            for (float x = box->bounds.minX; x < box->bounds.maxX; x += step) {
                Vec3 p1 = {center.x + x, center.y + box->bounds.maxY, center.z + box->bounds.minZ};
                Vec3 p2 = {center.x + x, center.y + box->bounds.maxY, center.z + box->bounds.maxZ};
                clipAndDrawLine(ren, p1, p2, cam, topColor);
            }
        }
        
        // В режиме реализма добавляем контур для чёткости
        if (g_worldEvolution.currentState >= WORLD_STATE_REALISTIC) {
            SDL_Color outlineColor = {
                (Uint8)(box->color.r * 0.5f),
                (Uint8)(box->color.g * 0.5f),
                (Uint8)(box->color.b * 0.5f),
                255
            };
            drawOptimizedBox(ren, box, cam);  // Рисуем контур поверх
        }
        
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    }
}

// [Продолжение в следующем сообщении...]

// --- ФУНКЦИИ ДЛЯ МОНЕТ ---
void initCoins() {
    g_numCoins = 45;  // Много монет для эволюции
    
    // Счётчик реально созданных монет
    int actualCoins = 0;
    
    // Генерируем монеты по спирали, избегая коллизий
    for (int i = 0; i < g_numCoins && i < 50; i++) {
        float angle = ((float)i / 10.0f) * 2.0f * M_PI;
        float radius = 4.0f + (float)(i / 5) * 2.5f;  // Расширяющаяся спираль
        
        // Базовая позиция
        float baseX = fast_cos(angle) * radius;
        float baseZ = fast_sin(angle) * radius;
        float baseY = 0.5f + fast_sin((float)i * 0.3f) * 0.5f;  // Разная высота
        
        // Находим безопасную позицию
        Vec3 safePos = findSafeSpawnPosition(baseX, baseY, baseZ, collisionBoxes, numCollisionBoxes);
        
        // Дополнительная проверка - не слишком ли близко к другим монетам
        int tooClose = 0;
        for (int j = 0; j < actualCoins; j++) {
            float dist = sqrtf(
                powf(safePos.x - g_coins[j].pos.x, 2) +
                powf(safePos.y - g_coins[j].pos.y, 2) +
                powf(safePos.z - g_coins[j].pos.z, 2)
            );
            if (dist < 1.5f) { // Минимальное расстояние между монетами
                tooClose = 1;
                break;
            }
        }
        
        if (!tooClose) {
            g_coins[actualCoins].pos = safePos;
            g_coins[actualCoins].collected = 0;
            g_coins[actualCoins].rotationPhase = (float)i * 0.5f;
            g_coins[actualCoins].bobPhase = (float)i * 0.3f;
            actualCoins++;
        }
    }
    
    g_numCoins = actualCoins; // Обновляем реальное количество монет
    g_coinsCollected = 0;
    
    printf("🪙 Spawned %d coins in safe positions\n", g_numCoins);
}

void updateCoins(float deltaTime) {
    for (int i = 0; i < g_numCoins; i++) {
        if (!g_coins[i].collected) {
            g_coins[i].rotationPhase += deltaTime * 3.0f;
            g_coins[i].bobPhase += deltaTime * 2.0f;
        }
    }
}

// Добавь в drawCoin для визуальной обратной связи:

void drawCoin(SDL_Renderer* ren, Coin* coin, Camera cam) {
    if (coin->collected) return;
    
    float bobOffset = fast_sin(coin->bobPhase) * 0.2f;
    float rotation = coin->rotationPhase;
    
    Vec3 center = {coin->pos.x, coin->pos.y + bobOffset, coin->pos.z};
    
    // --- ПРОВЕРЯЕМ РАССТОЯНИЕ ДО ИГРОКА ---
    float distToPlayer = sqrtf(
        powf(cam.x - coin->pos.x, 2) +
        powf(cam.y - coin->pos.y, 2) +
        powf(cam.z - coin->pos.z, 2)
    );
    
    // Меняем размер и цвет в зависимости от расстояния
    float radius = 0.3f;
    SDL_Color goldColor = {255, 215, 0, 255};
    
    if (distToPlayer < 2.0f) {
        // Близко - монета пульсирует
        radius = 0.3f + fast_sin(SDL_GetTicks() * 0.01f) * 0.05f;
        
        if (distToPlayer < 1.5f) {
            // Очень близко - меняем цвет
            goldColor = (SDL_Color){255, 240, 100, 255};
            radius = 0.35f + fast_sin(SDL_GetTicks() * 0.02f) * 0.08f;
        }
    }
    
    int segments = 8;
    Vec3 points[8];
    
    for (int i = 0; i < segments; i++) {
        float angle = (float)i / segments * 2.0f * M_PI + rotation;
        points[i].x = center.x + fast_cos(angle) * radius * fabsf(fast_cos(rotation));
        points[i].y = center.y + fast_sin(angle) * radius * 0.3f;
        points[i].z = center.z + fast_sin(angle) * radius * fabsf(fast_sin(rotation));
    }
    
    // Рисуем грани монеты
    for (int i = 0; i < segments; i++) {
        clipAndDrawLine(ren, points[i], points[(i + 1) % segments], cam, goldColor);
    }
    
    // Центральные линии для объёма
    for (int i = 0; i < segments; i += 2) {
        clipAndDrawLine(ren, center, points[i], cam, goldColor);
    }
    
    // Если очень близко, рисуем "ауру" сбора
    if (distToPlayer < 1.5f) {
        SDL_Color auraColor = {255, 255, 150, 100};
        float auraRadius = radius * 1.5f;
        
        for (int i = 0; i < segments; i++) {
            float angle = (float)i / segments * 2.0f * M_PI;
            Vec3 auraPoint = {
                center.x + fast_cos(angle) * auraRadius,
                center.y,
                center.z + fast_sin(angle) * auraRadius
            };
            if (i % 2 == 0) {  // Рисуем через одну для эффекта
                clipAndDrawLine(ren, center, auraPoint, cam, auraColor);
            }
        }
    }
}

void checkCoinCollection(Camera* cam) {
    for (int i = 0; i < g_numCoins; i++) {
        if (g_coins[i].collected) continue; // Пропускаем собранные
        
        // --- УЛУЧШЕННАЯ ПРОВЕРКА РАССТОЯНИЯ ---
        // Учитываем высоту камеры и радиус игрока
        float playerFootY = cam->y;  // Позиция ног
        float playerHeadY = cam->y + cam->height;  // Позиция головы
        
        // Проверяем расстояние по горизонтали (XZ)
        float horizontalDist = sqrtf(
            powf(cam->x - g_coins[i].pos.x, 2) +
            powf(cam->z - g_coins[i].pos.z, 2)
        );
        
        // Текущая высота монеты с учётом анимации
        float coinY = g_coins[i].pos.y + fast_sin(g_coins[i].bobPhase) * 0.2f;
        
        // Проверяем, находится ли монета в пределах досягаемости игрока
        int inVerticalRange = (coinY >= playerFootY - 0.5f && coinY <= playerHeadY + 0.5f);
        int inHorizontalRange = (horizontalDist < 1.2f);  // Увеличенный радиус
        
        if (inHorizontalRange && inVerticalRange) {
            // --- ДВОЙНАЯ ПРОВЕРКА для надёжности ---
            // Полное 3D расстояние
            float distSq = powf(cam->x - g_coins[i].pos.x, 2) +
               powf(cam->y + cam->height/2 - coinY, 2) +
               powf(cam->z - g_coins[i].pos.z, 2);

const float collectRadiusSq = 1.5f * 1.5f; // Считаем квадрат радиуса ОДИН раз

if (distSq < collectRadiusSq) {  // Увеличенный радиус сбора
                g_coins[i].collected = 1;
                g_coinsCollected++;
                
                printf("💰 Coin collected! Total: %d/%d (Distance was: %.2f)\n", 
                       g_coinsCollected, g_numCoins, distSq
                    );
                
                // Особые сообщения при важных вехах
                switch(g_coinsCollected) {
                    case 10:
                        printf("🌐 REALITY STABILIZING... Grid walls emerging.\n");
                        break;
                    case 15:
                        printf("📦 SIMULATION BOUNDARIES DETECTED... Cube forming.\n");
                        break;
                    case 20:
                        printf("🎨 MATERIALIZING... Polygons phasing in.\n");
                        break;
                    case 30:
                        printf("🖼️ TEXTURES LOADING... World gaining substance.\n");
                        break;
                    case 40:
                        printf("☁️ BREAKING THROUGH... Sky becoming visible.\n");
                        printf("✨ WORLD FULLY MATERIALIZED!\n");
                        break;
                }
                
                break; // Важно! Выходим из цикла после сбора
            }
        }
    }
}

void initQuestSystem(QuestSystem* qs) {
    // Квест 0: Туториал - ИСПРАВЛЕНА ЦЕЛЕВАЯ ТОЧКА
    qs->nodes[0] = (QuestNode){
        .id = 0,
        .name = "First Steps",
        .description = "Learn basic movement",
        .worldPos = {0, 3, -5},
        .color = {100, 255, 100, 255},
        .status = QUEST_AVAILABLE,
        .numObjectives = 2,
        .expReward = 10,
        .nodeScale = 1.0f
    };
    
    // ИСПРАВЛЯЕМ: Правильная целевая точка - зелёная платформа
    qs->nodes[0].objectives[0] = (QuestObjective){
        .type = OBJECTIVE_REACH_POINT,
        .target = &collisionBoxes[1].pos, // Указатель на позицию зелёной платформы
        .description = "Reach the green platform",
        .requiredProgress = 1,
        .currentProgress = 0
    };
    qs->nodes[0].objectives[1] = (QuestObjective){
        .type = OBJECTIVE_COLLECT_ITEM,
        .description = "Collect 3 coins",
        .requiredProgress = 3,
        .currentProgress = 0
    };
    qs->nodes[0].unlocksQuests[0] = 1;
    qs->nodes[0].unlocksQuests[1] = 2;
    qs->nodes[0].numUnlocks = 2;
    
    // Остальные квесты остаются такими же
    qs->nodes[1] = (QuestNode){
        .id = 1,
        .name = "Parkour Master",
        .description = "Complete the jumping puzzle",
        .worldPos = {10, 4, 0},
        .color = {255, 150, 100, 255},
        .status = QUEST_LOCKED,
        .numRequired = 1,
        .expReward = 25,
        .nodeScale = 1.0f
    };
    qs->nodes[1].requiredQuests[0] = 0;
    qs->nodes[1].unlocksQuests[0] = 3;
    qs->nodes[1].numUnlocks = 1;
    
    qs->nodes[2] = (QuestNode){
        .id = 2,
        .name = "Explorer",
        .description = "Find all hidden areas",
        .worldPos = {-10, 4, 0},
        .color = {100, 150, 255, 255},
        .status = QUEST_LOCKED,
        .numRequired = 1,
        .expReward = 30,
        .nodeScale = 1.0f
    };
    qs->nodes[2].requiredQuests[0] = 0;
    qs->nodes[2].unlocksQuests[0] = 3;
    qs->nodes[2].numUnlocks = 1;
    
    qs->nodes[3] = (QuestNode){
        .id = 3,
        .name = "Final Challenge",
        .description = "Defeat the cube guardian",
        .worldPos = {0, 6, 10},
        .color = {255, 100, 100, 255},
        .status = QUEST_LOCKED,
        .numRequired = 2,
        .expReward = 100,
        .nodeScale = 1.5f
    };
    qs->nodes[3].requiredQuests[0] = 1;
    qs->nodes[3].requiredQuests[1] = 2;
    
    qs->numNodes = 4;
    qs->activeQuestId = -1;
}

void drawQuestNode(SDL_Renderer* ren, QuestNode* node, Camera cam, float time) {
    float pulse = (node->status == QUEST_ACTIVE) ? 
                fast_sin(time * 3.0f) * 0.2f + 1.0f : 1.0f;
    
    float scale = node->nodeScale * pulse;
    
    Vec3 top = {node->worldPos.x, node->worldPos.y + scale, node->worldPos.z};
    Vec3 bottom = {node->worldPos.x, node->worldPos.y - scale, node->worldPos.z};
    
    Vec3 corners[4] = {
        {node->worldPos.x - scale*0.7f, node->worldPos.y, node->worldPos.z - scale*0.7f},
        {node->worldPos.x + scale*0.7f, node->worldPos.y, node->worldPos.z - scale*0.7f},
        {node->worldPos.x + scale*0.7f, node->worldPos.y, node->worldPos.z + scale*0.7f},
        {node->worldPos.x - scale*0.7f, node->worldPos.y, node->worldPos.z + scale*0.7f}
    };
    
    SDL_Color color = node->color;
    if (node->status == QUEST_LOCKED) {
        color.r /= 3; color.g /= 3; color.b /= 3;
    } else if (node->status == QUEST_COMPLETED) {
        color.r = 200; color.g = 200; color.b = 200;
    }
    
    for (int i = 0; i < 4; i++) {
        clipAndDrawLine(ren, top, corners[i], cam, color);
        clipAndDrawLine(ren, corners[i], corners[(i+1)%4], cam, color);
    }
    
    for (int i = 0; i < 4; i++) {
        clipAndDrawLine(ren, bottom, corners[i], cam, color);
    }
    
    if (node->status == QUEST_AVAILABLE) {
        Vec3 markTop = {node->worldPos.x, node->worldPos.y + scale + 1.5f, node->worldPos.z};
        Vec3 markBottom = {node->worldPos.x, node->worldPos.y + scale + 0.5f, node->worldPos.z};
        SDL_Color yellow = {255, 255, 0, 255};
        clipAndDrawLine(ren, markTop, markBottom, cam, yellow);
    }
}

void drawQuestConnections(SDL_Renderer* ren, QuestSystem* qs, Camera cam, float time) {
    for (int i = 0; i < qs->numNodes; i++) {
        QuestNode* node = &qs->nodes[i];
        
        for (int j = 0; j < node->numUnlocks; j++) {
            int targetId = node->unlocksQuests[j];
            QuestNode* target = &qs->nodes[targetId];
            
            SDL_Color lineColor = {100, 100, 100, 255};
            
            if (node->status == QUEST_COMPLETED) {
                float glow = fast_sin(time * 2.0f + i) * 0.5f + 0.5f;
                lineColor.r = 100 + glow * 155;
                lineColor.g = 100 + glow * 155;
                lineColor.b = 255;
            }
            
            int segments = 10;
            for (int s = 0; s < segments; s += 2) {
                Vec3 start = {
                    node->worldPos.x + (target->worldPos.x - node->worldPos.x) * s / segments,
                    node->worldPos.y + (target->worldPos.y - node->worldPos.y) * s / segments,
                    node->worldPos.z + (target->worldPos.z - node->worldPos.z) * s / segments
                };
                Vec3 end = {
                    node->worldPos.x + (target->worldPos.x - node->worldPos.x) * (s+1) / segments,
                    node->worldPos.y + (target->worldPos.y - node->worldPos.y) * (s+1) / segments,
                    node->worldPos.z + (target->worldPos.z - node->worldPos.z) * (s+1) / segments
                };
                clipAndDrawLine(ren, start, end, cam, lineColor);
            }
        }
    }
}

void updateQuestSystem(QuestSystem* qs, Camera* cam, float playerRadius) {
    for (int i = 0; i < qs->numNodes; i++) {
        QuestNode* node = &qs->nodes[i];
        
        float dist = sqrtf(
            powf(cam->x - node->worldPos.x, 2) +
            powf(cam->y - node->worldPos.y, 2) +
            powf(cam->z - node->worldPos.z, 2)
        );
        
        if (dist < 2.0f && node->status == QUEST_AVAILABLE) {
            const Uint8* keys = SDL_GetKeyboardState(NULL);
            if (keys[SDL_SCANCODE_E]) {
                node->status = QUEST_ACTIVE;
                qs->activeQuestId = node->id;
                
                printf("Quest started: %s\n", node->name);
            }
        }
        
        if (node->status == QUEST_LOCKED) {
            int allCompleted = 1;
            for (int j = 0; j < node->numRequired; j++) {
                if (qs->nodes[node->requiredQuests[j]].status != QUEST_COMPLETED) {
                    allCompleted = 0;
                    break;
                }
            }
            if (allCompleted && node->numRequired > 0) {
                node->status = QUEST_AVAILABLE;
                printf("Quest unlocked: %s\n", node->name);
            }
        }
    }
}

// --- ИСПРАВЛЕННАЯ ФУНКЦИЯ ПРОВЕРКИ ЦЕЛЕЙ ---
void checkQuestObjectives(QuestSystem* qs, Camera* cam, CollisionBox* boxes, int numBoxes) {
    if (qs->activeQuestId >= 0) {
        QuestNode* quest = &qs->nodes[qs->activeQuestId];
        
        int allCompleted = 1;
        for (int i = 0; i < quest->numObjectives; i++) {
            QuestObjective* obj = &quest->objectives[i];
            
            switch(obj->type) {
                case OBJECTIVE_REACH_POINT:
                    // ИСПРАВЛЯЕМ: Проверяем, стоим ли мы на зелёной платформе
                    if (obj->target) {
                        Vec3* targetPos = (Vec3*)obj->target;
                        
                        // Проверяем расстояние до центра платформы
                        float distXZ = sqrtf(powf(cam->x - targetPos->x, 2) + 
                                           powf(cam->z - targetPos->z, 2));
                        
                        // Проверяем высоту - стоим ли мы НА платформе
                        float platformTop = targetPos->y + boxes[1].bounds.maxY; // Используем bounds зелёной платформы
                        float heightDiff = fabsf(cam->y - platformTop);
                        
                        // Засчитываем, если мы близко по горизонтали И стоим на платформе
                        if (distXZ < 2.0f && heightDiff < 0.5f) {
                            obj->currentProgress = 1;
                            printf("Reached green platform!\n");
                        }
                    }
                    break;
                    
                case OBJECTIVE_COLLECT_ITEM:
                    // ИСПРАВЛЯЕМ: Берём данные из глобального счётчика монет
                    obj->currentProgress = g_coinsCollected;
                    break;
            }
            
            if (obj->currentProgress < obj->requiredProgress) {
                allCompleted = 0;
            }
        }
        
        if (allCompleted) {
            quest->status = QUEST_COMPLETED;
            printf("Quest completed: %s! Reward: %d XP\n", quest->name, quest->expReward);
            qs->activeQuestId = -1;
        }
    }
}

void drawQuestUI(SDL_Renderer* ren, TTF_Font* font, QuestSystem* qs) {
    if (qs->activeQuestId >= 0) {
        QuestNode* quest = &qs->nodes[qs->activeQuestId];
        
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
        SDL_Rect bgRect = {WIDTH - 320, 20, 300, 100 + quest->numObjectives * 25};
        SDL_RenderFillRect(ren, &bgRect);
        
        SDL_Color white = {255, 255, 255, 255};
        SDL_Color yellow = {255, 255, 0, 255};
        SDL_Color green = {100, 255, 100, 255};
        
        drawText(ren, font, quest->name, WIDTH - 310, 30, yellow);
        
        for (int i = 0; i < quest->numObjectives; i++) {
            QuestObjective* obj = &quest->objectives[i];
            char buffer[256];
            
            SDL_Color objColor = (obj->currentProgress >= obj->requiredProgress) ? green : white;
            
            snprintf(buffer, 256, "[%c] %s (%d/%d)",
                    (obj->currentProgress >= obj->requiredProgress) ? 'X' : ' ',
                    obj->description,
                    obj->currentProgress,
                    obj->requiredProgress);
            
            drawText(ren, font, buffer, WIDTH - 310, 60 + i * 25, objColor);
        }
    }
    
    // ДОБАВЛЯЕМ: Показываем общий счётчик монет
    char coinCounter[64];
    snprintf(coinCounter, 64, "Coins: %d/%d", g_coinsCollected, g_numCoins);
    SDL_Color gold = {255, 215, 0, 255};
    drawText(ren, font, coinCounter, 20, HEIGHT - 40, gold);
}

void spawnGlitches(int count) {
    printf("RKN-chan is summoning glitches!\n");
    g_activeGlitches = 0;
    for (int i = 0; i < count && i < MAX_GLITCHES; i++) {
        g_glitchBytes[i].active = 1;
        float angle = ((float)rand() / RAND_MAX) * 2.0f * M_PI;
        float distance = 5.0f + ((float)rand() / RAND_MAX) * 3.0f;
        g_glitchBytes[i].pos.x = g_rknChan.pos.x + fast_cos(angle) * distance;
        g_glitchBytes[i].pos.y = 1.5f;
        g_glitchBytes[i].pos.z = g_rknChan.pos.z + fast_sin(angle) * distance;
        g_glitchBytes[i].velocity = (Vec3){0, 0, 0};
        g_glitchBytes[i].jitter = 0.0f;
        g_activeGlitches++;
    }
}

void initBoss() {
    g_rknChan.pos = (Vec3){0, 1, 15}; // Поставим её в дальнем конце арены
    g_rknChan.maxHealth = BOSS_MAX_HEALTH;
    g_rknChan.health = BOSS_MAX_HEALTH;
    g_rknChan.state = BOSS_STATE_IDLE;
    g_rknChan.stateTimer = 3.0f; // Через 3 секунды начнёт действовать
    g_bossFightActive = 1; // Битва началась!
    printf("A wild RKN-chan appeared!\n");
}

// Простая функция для отрисовки куба в любой точке мира
void drawWorldCube(SDL_Renderer* ren, Vec3 center, float size, Camera cam, SDL_Color color) {
    Vec3 vertices[8];
    float s = size / 2.0f;
    vertices[0] = (Vec3){center.x - s, center.y - s, center.z - s};
    vertices[1] = (Vec3){center.x + s, center.y - s, center.z - s};
    vertices[2] = (Vec3){center.x + s, center.y + s, center.z - s};
    vertices[3] = (Vec3){center.x - s, center.y + s, center.z - s};
    vertices[4] = (Vec3){center.x - s, center.y - s, center.z + s};
    vertices[5] = (Vec3){center.x + s, center.y - s, center.z + s};
    vertices[6] = (Vec3){center.x + s, center.y + s, center.z + s};
    vertices[7] = (Vec3){center.x - s, center.y + s, center.z + s};

    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    for (int i = 0; i < 12; i++) {
        clipAndDrawLine(ren, vertices[edges[i][0]], vertices[edges[i][1]], cam, color);
    }
}

void drawBoss(SDL_Renderer* ren, Camera cam) {
    if (!g_bossFightActive || g_rknChan.state == BOSS_STATE_DEFEATED) return;

    // Тело
    SDL_Color bodyColor = {50, 50, 80, 255};
    if (g_rknChan.state == BOSS_STATE_VULNERABLE) {
        bodyColor = (SDL_Color){255, 100, 100, 255}; // Краснеет, когда уязвима
    }
    drawWorldCube(ren, g_rknChan.pos, 2.0f, cam, bodyColor);

    // Голова
    Vec3 headPos = {g_rknChan.pos.x, g_rknChan.pos.y + 1.5f, g_rknChan.pos.z};
    drawWorldCube(ren, headPos, 1.0f, cam, (SDL_Color){200, 200, 220, 255});

    // Банхаммер
    Vec3 hammerPos = {g_rknChan.pos.x + 1.5f, g_rknChan.pos.y, g_rknChan.pos.z};
    drawWorldCube(ren, hammerPos, 1.5f, cam, (SDL_Color){100, 80, 70, 255});
}

void drawGlitches(SDL_Renderer* ren, Camera cam) {
    if (g_activeGlitches == 0) return;
    
    SDL_Color glitchColor = {255, 0, 255, 255};
    for (int i = 0; i < MAX_GLITCHES; i++) {
        if (g_glitchBytes[i].active) {
            Vec3 jitterPos = g_glitchBytes[i].pos;
            jitterPos.x += fast_sin(g_glitchBytes[i].jitter) * 0.2f;
            jitterPos.y += fast_cos(g_glitchBytes[i].jitter * 1.5f) * 0.2f;
            jitterPos.z += fast_sin(g_glitchBytes[i].jitter * 0.8f) * 0.2f;
            drawWorldCube(ren, jitterPos, 0.5f, cam, glitchColor);
        }
    }
}

void updateGlitches(float deltaTime, Camera* cam) {
    if (g_activeGlitches == 0) return;

    for (int i = 0; i < MAX_GLITCHES; i++) {
        if (g_glitchBytes[i].active) {
            g_glitchBytes[i].jitter += deltaTime * 20.0f;

            // Движение к игроку
            float dx = cam->x - g_glitchBytes[i].pos.x;
            float dy = cam->y - g_glitchBytes[i].pos.y;
            float dz = cam->z - g_glitchBytes[i].pos.z;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);

            if (dist > 0.1f) {
                g_glitchBytes[i].pos.x += (dx / dist) * GLITCH_SPEED * deltaTime;
                g_glitchBytes[i].pos.y += (dy / dist) * GLITCH_SPEED * deltaTime;
                g_glitchBytes[i].pos.z += (dz / dist) * GLITCH_SPEED * deltaTime;
            }

            // Проверка "очистки" (коллизии с игроком)
            if (dist < 1.0f) {
                g_glitchBytes[i].active = 0;
                g_activeGlitches--;
                printf("Glitch cleared! Remaining: %d\n", g_activeGlitches);
            }
        }
    }
    
    // Если все глюки зачищены, делаем босса уязвимым
    if (g_activeGlitches == 0 && g_rknChan.state != BOSS_STATE_VULNERABLE) {
        printf("All glitches cleared! RKN-chan is vulnerable!\n");
        g_rknChan.state = BOSS_STATE_VULNERABLE;
        g_rknChan.stateTimer = BOSS_VULNERABLE_DURATION;
    }
}


void updateBoss(float deltaTime, Camera* cam) {
    if (!g_bossFightActive || g_rknChan.state == BOSS_STATE_DEFEATED) return;

    g_rknChan.stateTimer -= deltaTime;

    // Вектор от босса к игроку
    float dx = cam->x - g_rknChan.pos.x;
    float dz = cam->z - g_rknChan.pos.z;
    float distance = sqrtf(dx*dx + dz*dz);
    
    switch(g_rknChan.state) {
        case BOSS_STATE_IDLE:
            if (g_rknChan.stateTimer <= 0) {
                g_rknChan.state = BOSS_STATE_CHASING;
                g_rknChan.stateTimer = 7.0f; // Преследовать 7 секунд
            }
            break;

        case BOSS_STATE_CHASING:
            // Двигается к игроку
            if (distance > 0.1f) {
                g_rknChan.pos.x += (dx / distance) * BOSS_SPEED * deltaTime;
                g_rknChan.pos.z += (dz / distance) * BOSS_SPEED * deltaTime;
            }
            if (g_rknChan.stateTimer <= 0) {
                g_rknChan.state = BOSS_STATE_SUMMONING_GLITCHES;
                g_rknChan.stateTimer = 2.0f; // 2 секунды каста
            }
            break;

        case BOSS_STATE_SUMMONING_GLITCHES:
            // Стоит на месте, "кастует"
            if (g_rknChan.stateTimer <= 0) {
                spawnGlitches(MAX_GLITCHES);
                g_rknChan.state = BOSS_STATE_IDLE;
                g_rknChan.stateTimer = 999.0f; // Ждет, пока не зачистят глюки
            }
            break;
            
        case BOSS_STATE_VULNERABLE:
            // Проверяем, наносит ли игрок урон
            if (distance < 3.0f) { // Нужно подойти вплотную для урона
                g_rknChan.health -= 20.0f * deltaTime; // Теряет 20 хп в секунду
                 if (g_rknChan.health <= 0) {
                    g_rknChan.health = 0;
                    g_rknChan.state = BOSS_STATE_DEFEATED;
                    printf("RKN-chan has been defeated! The Internet is free!\n");
                 }
            }
            if (g_rknChan.stateTimer <= 0) {
                printf("RKN-chan is no longer vulnerable.\n");
                g_rknChan.state = BOSS_STATE_IDLE;
                g_rknChan.stateTimer = 2.0f;
            }
            break;
        
        default: break;
    }
}

void drawBossUI(SDL_Renderer* ren) {
    if (!g_bossFightActive || g_rknChan.state == BOSS_STATE_DEFEATED) return;

    int barWidth = WIDTH / 2;
    int barHeight = 20;
    int barX = (WIDTH - barWidth) / 2;
    int barY = 30;

    // Фон
    SDL_SetRenderDrawColor(ren, 50, 50, 50, 200);
    SDL_Rect bgRect = {barX, barY, barWidth, barHeight};
    SDL_RenderFillRect(ren, &bgRect);

    // Полоска здоровья
    float healthPercent = g_rknChan.health / g_rknChan.maxHealth;
    SDL_SetRenderDrawColor(ren, 200, 40, 40, 220);
    SDL_Rect hpRect = {barX, barY, (int)(barWidth * healthPercent), barHeight};
    SDL_RenderFillRect(ren, &hpRect);
}

// Проверяет, находится ли точка в упрощенной "пирамиде видимости" камеры
int isPointInFrustum(Vec3 point, Camera cam) {
    // --- 1. Проверка расстояния (отсечение по ближней и дальней плоскости) ---
    float dx = point.x - cam.x;
    float dz = point.z - cam.z;
    float distanceSq = dx*dx + dz*dz; // Используем квадрат расстояния, чтобы не считать корень

    // Если объект дальше 50 юнитов, не рисуем (настройте значение)
    if (distanceSq > 50.0f * 50.0f) {
        return 0; // Слишком далеко
    }
    
    // --- 2. Проверка по углу обзора (отсечение по боковым плоскостям) ---
    // Вектор направления камеры
    Vec3 camDir = {fast_sin(cam.rotY), 0, fast_cos(cam.rotY)};
    
    // Вектор от камеры к точке
    Vec3 pointDir = {dx, 0, dz};
    
    // Нормализуем вектор к точке (для dot product)
    float pointDirLen = sqrtf(distanceSq);
    if (pointDirLen < 1e-6f) return 1; // Если точка в камере, считаем видимой
    pointDir.x /= pointDirLen;
    pointDir.z /= pointDirLen;
    
    // Скалярное произведение векторов. dot(A, B) = cos(угол между A и B), если векторы нормализованы.
    float dotProduct = dot(camDir, pointDir);
    
    // g_fov у вас большой, поэтому возьмем широкий угол. 
    // cos(60 градусов) ~ 0.5. Если dotProduct меньше этого, значит угол больше 60, и точка сбоку.
    // Настройте значение 0.3f под себя. Чем оно меньше, тем шире угол обзора для отсечения.
    if (dotProduct < 0.3f) {
        return 0; // Точка находится слишком сбоку от направления взгляда
    }

    return 1; // Точка видна
}

// Вычисляет радиус описывающей сферы для CollisionBox
float getBoundingSphereRadius(CollisionBox* box) {
    // Находим самую "дальнюю" точку от центра бокса по всем осям
    float dx = fmaxf(fabsf(box->bounds.minX), fabsf(box->bounds.maxX));
    float dy = fmaxf(fabsf(box->bounds.minY), fabsf(box->bounds.maxY));
    float dz = fmaxf(fabsf(box->bounds.minZ), fabsf(box->bounds.maxZ));
    
    // Радиус - это расстояние от центра (0,0,0) до этой дальней точки
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

int isBoxInFrustum_Improved(CollisionBox* box, Camera cam) {
    float radius = getBoundingSphereRadius(box);
    Vec3 pos = box->pos;

    // --- 1. Проверка на слишком близкое/далекое расстояние ---
    float dx = pos.x - cam.x;
    float dz = pos.z - cam.z;
    float distanceSq = dx*dx + dz*dz;
    float distance = sqrtf(distanceSq);

    // Если объект слишком далеко (с учетом его радиуса) или слишком близко, отсекаем
    if (distance > 50.0f + radius || distance < NEAR_PLANE - radius) {
        return 0;
    }
    
    // --- 2. Улучшенная проверка по углу обзора ---
    Vec3 camDir = {fast_sin(cam.rotY), 0, fast_cos(cam.rotY)}; // Вектор взгляда камеры
    Vec3 toObject = {dx, 0, dz}; // Вектор от камеры к центру объекта
    
    // Проекция вектора toObject на вектор camDir. Это расстояние 'd' вдоль луча взгляда.
    float d = dot(toObject, camDir);

    // Если d отрицательно, и объект далеко, то он точно сзади
    if (d < 0 && distance > radius) {
        return 0; // Объект сзади
    }

    // Находим ближайшую к центру объекта точку на линии взгляда камеры
    Vec3 closestPointOnRay;
    closestPointOnRay.x = cam.x + camDir.x * d;
    closestPointOnRay.y = cam.y; // Мы делаем проверку в 2D (XZ) для простоты
    closestPointOnRay.z = cam.z + camDir.z * d;

    // Расстояние от центра объекта до этой ближайшей точки
    float distToRaySq = powf(pos.x - closestPointOnRay.x, 2) + 
                        powf(pos.z - closestPointOnRay.z, 2);

    // Если квадрат этого расстояния меньше квадрата радиуса, то сфера объекта пересекает центральный луч взгляда
    if (distToRaySq < radius * radius) {
        return 1; // Мы точно видим объект
    }
    
    // --- 3. Проверка по краям поля зрения ---
    // Это более сложная часть, но мы можем схитрить.
    // Мы знаем 'd' (расстояние по лучу) и 'distToRay' (расстояние вбок от луча).
    // Это образует прямоугольный треугольник. Тангенс угла до объекта = distToRay / d.
    
    float distToRay = sqrtf(distToRaySq);
    
    // Угол обзора (FOV). g_fov у тебя - это не угол, а проекционный множитель.
    // Возьмем примерный угол в 45 градусов от центра в каждую сторону (общий 90).
    // tan(45) = 1.0
    float fovLimit = tanf(45.0f * M_PI / 180.0f); 

    // Если тангенс угла до объекта МЕНЬШЕ, чем тангенс половины FOV, то объект в поле зрения.
    // Мы также должны учесть радиус объекта, чтобы края не исчезали.
    if (distToRay - radius < d * fovLimit) {
        return 1;
    }
    
    return 0; // Объект за пределами поля зрения
}

// --- DAY/NIGHT CYCLE FUNCTIONS ---

// Вспомогательная функция для плавного смешивания цветов
SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    SDL_Color result;
    result.r = (Uint8)(a.r + (b.r - a.r) * t);
    result.g = (Uint8)(a.g + (b.g - a.g) * t);
    result.b = (Uint8)(a.b + (b.b - a.b) * t);
    result.a = 255;
    return result;
}

// Инициализация цикла
void initDayNightCycle() {
    g_dayNight.timeOfDay = 0.25f; // Начинаем с восхода
}

void SetTimeOfDay(float newTime) {
    // fmodf - это остаток от деления для float. Гарантирует, что время всегда будет в пределах 0.0-1.0
    g_dayNight.timeOfDay = fmodf(newTime, 1.0f); 
    if (g_dayNight.timeOfDay < 0.0f) {
        g_dayNight.timeOfDay += 1.0f;
    }
}

void updateDayNightCycle(float deltaTime, Camera cam) {
    // СТАНЕТ (умножаем deltaTime на наш множитель):
    g_dayNight.timeOfDay += (deltaTime * g_timeScale) / DAY_NIGHT_DURATION_SECONDS;

    if (g_dayNight.timeOfDay >= 1.0f) {
        g_dayNight.timeOfDay -= 1.0f;
    }

    // 2. НОВЫЕ, БОЛЕЕ ПРИГЛУШЕННЫЕ ЦВЕТА
    const SDL_Color midnightTop = {5, 5, 15, 255};
    const SDL_Color midnightBottom = {10, 10, 25, 255};
    const SDL_Color midnightLight = {30, 30, 45, 255}; // Немного светлее ночь

    const SDL_Color sunriseTop = {60, 70, 100, 255}; // Менее яркий восход
    const SDL_Color sunriseBottom = {200, 100, 50, 255}; 
    const SDL_Color sunriseLight = {200, 160, 140, 255};

    const SDL_Color noonTop = {100, 140, 180, 255}; // Приглушенный голубой день
    const SDL_Color noonBottom = {130, 170, 200, 255};
    const SDL_Color noonLight = {220, 220, 230, 255}; // Не чисто белый, а сероватый свет

    const SDL_Color sunsetBottom = {220, 90, 55, 255}; // Менее кричащий закат

    // 3. Смешиваем цвета (логика остается той же)
    float t = 0.0f;
    if (g_dayNight.timeOfDay >= 0.0f && g_dayNight.timeOfDay < 0.25f) { // Ночь -> Восход
        t = g_dayNight.timeOfDay / 0.25f;
        g_dayNight.skyTopColor = lerpColor(midnightTop, sunriseTop, t);
        g_dayNight.skyBottomColor = lerpColor(midnightBottom, sunriseBottom, t);
        g_dayNight.ambientLightColor = lerpColor(midnightLight, sunriseLight, t);
    } else if (g_dayNight.timeOfDay >= 0.25f && g_dayNight.timeOfDay < 0.5f) { // Восход -> Полдень
        t = (g_dayNight.timeOfDay - 0.25f) / 0.25f;
        g_dayNight.skyTopColor = lerpColor(sunriseTop, noonTop, t);
        g_dayNight.skyBottomColor = lerpColor(sunriseBottom, noonBottom, t);
        g_dayNight.ambientLightColor = lerpColor(sunriseLight, noonLight, t);
    } else if (g_dayNight.timeOfDay >= 0.5f && g_dayNight.timeOfDay < 0.75f) { // Полдень -> Закат
        t = (g_dayNight.timeOfDay - 0.5f) / 0.25f;
        g_dayNight.skyTopColor = lerpColor(noonTop, sunriseTop, t);
        g_dayNight.skyBottomColor = lerpColor(noonBottom, sunsetBottom, t);
        g_dayNight.ambientLightColor = lerpColor(noonLight, sunriseLight, t);
    } else { // Закат -> Ночь
        t = (g_dayNight.timeOfDay - 0.75f) / 0.25f;
        g_dayNight.skyTopColor = lerpColor(sunriseTop, midnightTop, t);
        g_dayNight.skyBottomColor = lerpColor(sunsetBottom, midnightBottom, t);
        g_dayNight.ambientLightColor = lerpColor(sunriseLight, midnightLight, t);
    }
    g_dayNight.fogColor = g_dayNight.skyBottomColor;

    // 4. Расчет позиций небесных тел (без изменений)
    float timeAngle = g_dayNight.timeOfDay * 2.0f * M_PI;
    g_dayNight.sunPos.y = cam.y + fast_sin(timeAngle) * SKY_RADIUS;
    g_dayNight.sunPos.z = cam.z + fast_cos(timeAngle) * SKY_RADIUS;
    g_dayNight.sunPos.x = cam.x; 

    g_dayNight.moonPos.y = cam.y + fast_sin(timeAngle + M_PI) * SKY_RADIUS;
    g_dayNight.moonPos.z = cam.z + fast_cos(timeAngle + M_PI) * SKY_RADIUS;
    g_dayNight.moonPos.x = cam.x;
}

// Рисуем Солнце и Луну
void drawSunAndMoon(SDL_Renderer* ren, Camera cam) {
    // Рисуем Солнце, если оно над горизонтом
    if (g_dayNight.sunPos.y > cam.y) {
        drawWorldCube(ren, g_dayNight.sunPos, 10.0f, cam, (SDL_Color){255, 255, 0, 255});
    }

    // Рисуем Луну, если она над горизонтом
    if (g_dayNight.moonPos.y > cam.y) {
        drawWorldCube(ren, g_dayNight.moonPos, 8.0f, cam, (SDL_Color){200, 200, 220, 255});
    }
}

// Инициализация телефона (вызвать один раз в main)
void initPhone() {
    g_phone.state = PHONE_STATE_HIDDEN;
    g_phone.animationProgress = 0.0f;
}

// Обновление логики и анимации телефона (вызывать каждый кадр)
void updatePhone(float deltaTime) {
    // Плавное движение к цели
    if (g_phone.state == PHONE_STATE_SHOWING) {
        g_phone.animationProgress = lerp(g_phone.animationProgress, 1.0f, deltaTime * PHONE_ANIM_SPEED);
        // Когда анимация почти закончилась, фиксируем состояние
        if (g_phone.animationProgress > 0.99f) {
            g_phone.animationProgress = 1.0f;
            g_phone.state = PHONE_STATE_VISIBLE;
        }
    } else if (g_phone.state == PHONE_STATE_HIDING) {
        g_phone.animationProgress = lerp(g_phone.animationProgress, 0.0f, deltaTime * PHONE_ANIM_SPEED);
        // Когда анимация почти закончилась, фиксируем состояние
        if (g_phone.animationProgress < 0.01f) {
            g_phone.animationProgress = 0.0f;
            g_phone.state = PHONE_STATE_HIDDEN;
        }
    }
}

// Отрисовка телефона (вызывать в конце цикла рендеринга)
void drawPhone(SDL_Renderer* ren, TTF_Font* font) {
    // Если телефон полностью убран, ничего не рисуем
    if (g_phone.state == PHONE_STATE_HIDDEN) {
        return;
    }

    // --- Расчеты размеров и позиций ---
    const int phoneWidth = 250;
    const int phoneHeight = 500;
    const int phoneOnScreenX = WIDTH - phoneWidth - 50;
    const int phoneOnScreenY = HEIGHT - phoneHeight - 50;
    const int phoneOffScreenY = HEIGHT; // Стартует за нижней границей экрана

    // --- Анимация ---
    // Вычисляем текущую позицию Y на основе прогресса анимации
    int currentY = (int)lerp((float)phoneOffScreenY, (float)phoneOnScreenY, g_phone.animationProgress);

    // --- Отрисовка ---
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    // 1. Корпус телефона (темно-серый)
    SDL_SetRenderDrawColor(ren, 25, 25, 30, 230);
    SDL_Rect phoneBody = { phoneOnScreenX, currentY, phoneWidth, phoneHeight };
    SDL_RenderFillRect(ren, &phoneBody);

    // 2. Экран телефона (черный)
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_Rect phoneScreen = { phoneOnScreenX + 15, currentY + 15, phoneWidth - 30, phoneHeight - 30 };
    SDL_RenderFillRect(ren, &phoneScreen);

    // 3. Отображение времени
    // Конвертируем время суток (0.0-1.0) в часы и минуты
    float totalHours = g_dayNight.timeOfDay * 24.0f;
    int hours = (int)totalHours;
    int minutes = (int)((totalHours - hours) * 60.0f);

    char timeString[6];
    snprintf(timeString, 6, "%02d:%02d", hours, minutes); // Форматируем в "ЧЧ:ММ"

    // Рисуем время на экране телефона
    SDL_Color white = {255, 255, 255, 255};
    drawText(ren, font, timeString, phoneScreen.x + 85, phoneScreen.y + 30, white);

    if (g_timeScale != 1.0f) { // Показываем, только если скорость не стандартная
        char scaleString[32];
        snprintf(scaleString, 32, "Time Scale: x%.1f", g_timeScale);

        SDL_Color yellow = {255, 255, 0, 255};
        // Рисуем внизу экрана телефона
        drawText(ren, font, scaleString, phoneScreen.x + 60, phoneScreen.y + phoneScreen.h - 50, yellow);
    }
}

// === ЗАМЕНИ СТАРУЮ drawFloor НА ЭТУ ===
void drawFloor(SDL_Renderer* ren, Camera cam) {
    // Если мир еще на ранней стадии, рисуем простую сетку
    if (g_worldEvolution.currentState < WORLD_STATE_MATERIALIZING) {
        SDL_Color gridColor = {60, 60, 70, 255};
        for (int i = -20; i <= 20; i += 2) {
            Vec3 p1_v = {(float)i, -2.0f, -20.0f}, p2_v = {(float)i, -2.0f, 20.0f};
            clipAndDrawLine(ren, p1_v, p2_v, cam, gridColor);
            Vec3 p1_h = {-20.0f, -2.0f, (float)i}, p2_h = {20.0f, -2.0f, (float)i};
            clipAndDrawLine(ren, p1_h, p2_h, cam, gridColor);
        }
        return;
    }

    // --- НОВАЯ СУПЕР-ОПТИМИЗИРОВАННАЯ ЛОГИКА ---
    float tileSize = 4.0f;
    int viewRange = 8; // Уменьшаем дальность прорисовки, но увеличиваем размер плитки

    // Вычисляем, на какой плитке стоит камера
    int camTileX = (int)floorf(cam.x / tileSize);
    int camTileZ = (int)floorf(cam.z / tileSize);

    for (int x = -viewRange; x <= viewRange; x++) {
        for (int z = -viewRange; z <= viewRange; z++) {
            // Рисуем от плитки, где стоит игрок, наружу
            float worldX = (camTileX + x) * tileSize;
            float worldZ = (camTileZ + z) * tileSize;

            // Простое отсечение по расстоянию, чтобы не рисовать углы
            float distSq = x*x + z*z;
            if (distSq > viewRange * viewRange) continue;

            Vec3 corners[4] = {
                {worldX, -2.0f, worldZ},
                {worldX + tileSize, -2.0f, worldZ},
                {worldX + tileSize, -2.0f, worldZ + tileSize},
                {worldX, -2.0f, worldZ + tileSize}
            };

            // Проверяем, находится ли хотя бы один угол плитки перед нами
            ProjectedPoint pp = project_with_depth(corners[0], cam);
            if (pp.z < NEAR_PLANE) continue;

            SDL_Color tileColor = (( (int)(worldX/tileSize) + (int)(worldZ/tileSize) ) % 2 == 0) ? 
                                  (SDL_Color){45, 45, 55, 255} : 
                                  (SDL_Color){35, 35, 45, 255};
            
            // Вместо заливки рисуем только контур и диагонали - это в 100 раз быстрее
            clipAndDrawLine(ren, corners[0], corners[1], cam, tileColor);
            clipAndDrawLine(ren, corners[1], corners[2], cam, tileColor);
            clipAndDrawLine(ren, corners[2], corners[3], cam, tileColor);
            clipAndDrawLine(ren, corners[3], corners[0], cam, tileColor);
            clipAndDrawLine(ren, corners[0], corners[2], cam, tileColor);
        }
    }
}

// === ВСТАВЬ ЭТОТ БЛОК ПЕРЕД main() ===

void drawJet(SDL_Renderer* ren, FighterJet* jet, Camera cam) {
    if (!jet->active) return;
    
    Vec3 body_front = {jet->pos.x, jet->pos.y, jet->pos.z + 2.0f};
    Vec3 body_rear = {jet->pos.x, jet->pos.y, jet->pos.z - 2.0f};
    Vec3 wing_left = {jet->pos.x - 2.5f, jet->pos.y, jet->pos.z - 1.0f};
    Vec3 wing_right = {jet->pos.x + 2.5f, jet->pos.y, jet->pos.z - 1.0f};
    
    SDL_Color jetColor = {200, 200, 210, 255};

    clipAndDrawLine(ren, body_front, wing_left, cam, jetColor);
    clipAndDrawLine(ren, wing_left, body_rear, cam, jetColor);
    clipAndDrawLine(ren, body_rear, wing_right, cam, jetColor);
    clipAndDrawLine(ren, wing_right, body_front, cam, jetColor);
}

void drawBomb(SDL_Renderer* ren, Bomb* bomb, Camera cam) {
    if (!bomb->active) return;
    
    Vec3 top = {bomb->pos.x, bomb->pos.y + 0.3f, bomb->pos.z};
    Vec3 bottom = {bomb->pos.x, bomb->pos.y - 0.3f, bomb->pos.z};
    SDL_Color bombColor = {50, 50, 50, 255};
    clipAndDrawLine(ren, top, bottom, cam, bombColor);
}

void drawExplosion(SDL_Renderer* ren, Explosion* explosion, Camera cam) {
    if (!explosion->active) return;
    
    int segments = 12;
    float radius = explosion->currentRadius;
    float opacity = (explosion->lifetime / 1.5f);
    if (opacity > 1.0f) opacity = 1.0f;
    
    SDL_Color color = {255, (int)(150 * opacity), 0, (int)(255 * opacity)};

    Vec3 center = explosion->pos;
    Vec3 points_xy[segments], points_xz[segments];

    for (int i = 0; i < segments; i++) {
        float angle = 2.0f * M_PI * i / segments;
        points_xy[i] = (Vec3){center.x + fast_cos(angle) * radius, center.y + fast_sin(angle) * radius, center.z};
        points_xz[i] = (Vec3){center.x + fast_cos(angle) * radius, center.y, center.z + fast_sin(angle) * radius};
    }

    for (int i = 0; i < segments; i++) {
        clipAndDrawLine(ren, points_xy[i], points_xy[(i + 1) % segments], cam, color);
        clipAndDrawLine(ren, points_xz[i], points_xz[(i + 1) % segments], cam, color);
    }
}

void startAirstrike(Vec3 start, Vec3 end, Camera* playerCam) {
    if (g_airstrike.isActive) return;
    
    printf("Calling for Democracy!\n");
    g_airstrike.isActive = 1;
    g_airstrike.timer = 0.0f;
    
    for (int i = 0; i < 3; i++) {
        Vec3 offset = {(float)(i-1) * 6.0f, 0, (float)(i-1) * 3.0f};
        g_airstrike.jets[i].startPos = (Vec3){start.x + offset.x, start.y, start.z + offset.z};
        g_airstrike.jets[i].endPos = (Vec3){end.x + offset.x, end.y, end.z + offset.z};
        g_airstrike.jets[i].pos = g_airstrike.jets[i].startPos;
        g_airstrike.jets[i].progress = 0.0f;
        g_airstrike.jets[i].velocity = normalize((Vec3){end.x-start.x, 0, end.z-start.z});
        g_airstrike.jets[i].active = 1;
        g_airstrike.jets[i].hasDroppedBomb = 0;
        
        g_airstrike.bombs[i].active = 0;
        g_airstrike.explosions[i].active = 0;
    }
    
    g_cinematic.isActive = 1;
    g_cinematic.transitionProgress = 0.0f;
    g_cinematic.target = (Vec3){ (start.x + end.x)/2, (start.y + end.y)/2 - 10, (start.z + end.z)/2 };
    g_cinematic.position = (Vec3){ playerCam->x, playerCam->y + 5, playerCam->z };
    g_cinematic.fov = g_fov;
}

void updateAirstrike(float deltaTime, Camera* playerCam) {
    if (!g_airstrike.isActive) return;

    g_airstrike.timer += deltaTime;

    for (int i = 0; i < 3; i++) {
        if (!g_airstrike.jets[i].active) continue;
        
        g_airstrike.jets[i].progress += deltaTime * 0.2f;
        g_airstrike.jets[i].pos.x = lerp(g_airstrike.jets[i].startPos.x, g_airstrike.jets[i].endPos.x, g_airstrike.jets[i].progress);
        g_airstrike.jets[i].pos.y = lerp(g_airstrike.jets[i].startPos.y, g_airstrike.jets[i].endPos.y, g_airstrike.jets[i].progress);
        g_airstrike.jets[i].pos.z = lerp(g_airstrike.jets[i].startPos.z, g_airstrike.jets[i].endPos.z, g_airstrike.jets[i].progress);
        
        if (g_airstrike.jets[i].progress > 0.5f && !g_airstrike.jets[i].hasDroppedBomb) {
            g_airstrike.jets[i].hasDroppedBomb = 1;
            g_airstrike.bombs[i].active = 1;
            g_airstrike.bombs[i].pos = g_airstrike.jets[i].pos;
            g_airstrike.bombs[i].velocity = (Vec3){0, -15.0f, 0};
            g_airstrike.bombs[i].fuse = 2.0f;
        }

        if (g_airstrike.jets[i].progress >= 1.0f) g_airstrike.jets[i].active = 0;
    }
    
    for (int i = 0; i < 3; i++) {
        if (!g_airstrike.bombs[i].active) continue;
        
        g_airstrike.bombs[i].fuse -= deltaTime;
        g_airstrike.bombs[i].velocity.y -= 12.8f * deltaTime;
        g_airstrike.bombs[i].pos.y += g_airstrike.bombs[i].velocity.y * deltaTime;
        
        if (g_airstrike.bombs[i].pos.y < -1.9f || g_airstrike.bombs[i].fuse <= 0) {
            g_airstrike.bombs[i].active = 0;
            g_airstrike.explosions[i].active = 1;
            g_airstrike.explosions[i].pos = (Vec3){g_airstrike.bombs[i].pos.x, -1.9f, g_airstrike.bombs[i].pos.z};
            g_airstrike.explosions[i].currentRadius = 0.0f;
            g_airstrike.explosions[i].maxRadius = 12.0f;
            g_airstrike.explosions[i].lifetime = 1.5f;
            
            float dist = sqrtf(powf(playerCam->x - g_airstrike.explosions[i].pos.x, 2) + powf(playerCam->z - g_airstrike.explosions[i].pos.z, 2));
            if (dist < g_airstrike.explosions[i].maxRadius) {
                printf("Player was hit by Democracy! Distance: %.2f\n", dist);
            }
        }
    }
    
    for (int i = 0; i < 3; i++) {
        if (!g_airstrike.explosions[i].active) continue;
        
        g_airstrike.explosions[i].lifetime -= deltaTime;
        g_airstrike.explosions[i].currentRadius = lerp(0.0f, g_airstrike.explosions[i].maxRadius, 1.0f - (g_airstrike.explosions[i].lifetime / 1.5f));
        
        if (g_airstrike.explosions[i].lifetime <= 0) g_airstrike.explosions[i].active = 0;
    }

    if (g_airstrike.timer > 15.0f) {
        g_airstrike.isActive = 0;
        g_cinematic.isActive = 0;
    }
}

void drawMainMenu(SDL_Renderer* ren, TTF_Font* font) {
    SDL_SetRenderDrawColor(ren, 10, 10, 15, 255);
    SDL_RenderClear(ren);

    SDL_Color titleColor = {0, 255, 100, 255};
    SDL_Color optionColor = {200, 200, 200, 255};
    SDL_Color selectedColor = {255, 255, 0, 255};

    // --- Рисуем название "G OMETRICA" ---
    drawText(ren, font, "G", WIDTH/2 - 150, HEIGHT/2 - 100, titleColor);
    // Пропускаем место для 'E'
    drawText(ren, font, "OMETRICA", WIDTH/2 - 80, HEIGHT/2 - 100, titleColor);

    // --- Рисуем вращающийся ромб вместо 'E' ---
    int rhombus_cx = WIDTH/2 - 115;
    int rhombus_cy = HEIGHT/2 - 90;
    int size = 20;
    
    Vec3 points[4] = {
        {rhombus_cx, rhombus_cy - size, 0}, // top
        {rhombus_cx + size, rhombus_cy, 0}, // right
        {rhombus_cx, rhombus_cy + size, 0}, // bottom
        {rhombus_cx - size, rhombus_cy, 0}  // left
    };
    
    // Вращаем точки
    for(int i=0; i<4; i++){
        float x = points[i].x - rhombus_cx;
        float y = points[i].y - rhombus_cy;
        points[i].x = x * fast_cos(g_menuRhombusAngle) - y * fast_sin(g_menuRhombusAngle) + rhombus_cx;
        points[i].y = x * fast_sin(g_menuRhombusAngle) + y * fast_cos(g_menuRhombusAngle) + rhombus_cy;
    }

    SDL_SetRenderDrawColor(ren, titleColor.r, titleColor.g, titleColor.b, 255);
    for(int i=0; i<4; i++){
        SDL_RenderDrawLine(ren, (int)points[i].x, (int)points[i].y, (int)points[(i+1)%4].x, (int)points[(i+1)%4].y);
    }
    
    // --- Рисуем пункты меню ---
    const char* menuItems[] = { "Start Game", "Settings", "Exit" };
    for (int i = 0; i < 3; i++) {
        drawText(ren, font, menuItems[i], WIDTH/2 - 80, HEIGHT/2 + i * 40, (i == g_menuSelectedOption) ? selectedColor : optionColor);
    }
}

void drawSettingsMenu(SDL_Renderer* ren, TTF_Font* font, EditableVariable* vars, int numVars) {
    SDL_SetRenderDrawColor(ren, 10, 10, 15, 255);
    SDL_RenderClear(ren);
    
    SDL_Color titleColor = {0, 255, 100, 255};
    SDL_Color optionColor = {200, 200, 200, 255};
    SDL_Color selectedColor = {255, 255, 0, 255};
    
    drawText(ren, font, "SETTINGS", 20, 20, titleColor);

    for (int i = 0; i < numVars; ++i) {
        char buffer[128];
        snprintf(buffer, 128, "%s: < %.4f >", vars[i].name, *vars[i].value_ptr);
        drawText(ren, font, buffer, 50, 80 + i * 30, (i == g_settingsSelectedOption) ? selectedColor : optionColor);
    }
    
    drawText(ren, font, "Back", 50, 80 + numVars * 30, (numVars == g_settingsSelectedOption) ? selectedColor : optionColor);
}

void clearZBuffer() {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            // Заполняем буфер "бесконечностью", чтобы любой первый пиксель
            // был ближе, чем это значение.
            g_zBuffer[y][x] = INFINITY;
        }
    }
}


void saveConfig(const char* filename, GameConfig* config) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        printf("Не удалось сохранить настройки в %s\n", filename);
        return;
    }
    
    fprintf(file, "mouseSensitivity=%.6f\n", config->mouseSensitivity);
    fprintf(file, "walkSpeed=%.6f\n", config->walkSpeed);
    fprintf(file, "runSpeed=%.6f\n", config->runSpeed);
    fprintf(file, "crouchSpeedMultiplier=%.6f\n", config->crouchSpeedMultiplier);
    fprintf(file, "acceleration=%.6f\n", config->acceleration);
    fprintf(file, "deceleration=%.6f\n", config->deceleration);
    fprintf(file, "jumpForce=%.6f\n", config->jumpForce);
    fprintf(file, "gravity=%.6f\n", config->gravity);
    fprintf(file, "fov=%.6f\n", config->fov);
    
    fclose(file);
    printf("Настройки сохранены в %s\n", filename);
}

void loadConfig(const char* filename, GameConfig* config) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Файл настроек %s не найден, используем значения по умолчанию\n", filename);
        return;
    }
    
    // === ИСПРАВЛЕНИЕ 1: Используем безопасное чтение ===
    char line[CONFIG_LINE_SIZE];
    while (readConfigLine(file, line, sizeof(line))) {
        // Пробуем распарсить каждое возможное значение
        parseConfigValue(line, "mouseSensitivity", &config->mouseSensitivity);
        parseConfigValue(line, "walkSpeed", &config->walkSpeed);
        parseConfigValue(line, "runSpeed", &config->runSpeed);
        parseConfigValue(line, "crouchSpeedMultiplier", &config->crouchSpeedMultiplier);
        parseConfigValue(line, "acceleration", &config->acceleration);
        parseConfigValue(line, "deceleration", &config->deceleration);
        parseConfigValue(line, "jumpForce", &config->jumpForce);
        parseConfigValue(line, "gravity", &config->gravity);
        parseConfigValue(line, "fov", &config->fov);
    }
    
    fclose(file);
    printf("Настройки загружены из %s\n", filename);
}

int main(int argc, char* argv[]) {
    // --- ЭТАП 1: МИНИМАЛЬНЫЙ ЗАПУСК ДЛЯ ОКНА ---
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    TTF_Init();
    init_fast_math(); // Математику считаем до окна, это быстро

    SDL_Window* win = SDL_CreateWindow("GEOMETRICA", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if (!win) return 1;
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    
    SDL_SetRenderDrawColor(ren, 10, 10, 15, 255);
    SDL_RenderClear(ren);
    TTF_Font* font = TTF_OpenFont("arial.ttf", 24); 
    if (font) {
        drawText(ren, font, "INITIALIZING REALITY KERNEL...", WIDTH/2 - 200, HEIGHT/2, (SDL_Color){0, 255, 100, 255});
    }
    SDL_RenderPresent(ren);

    // --- ЭТАП 3: ВСЯ ТВОЯ СТАРАЯ ЗАГРУЗКА ИДЕТ ЗДЕСЬ, В ФОНЕ ---
    // <<< Весь твой код, который ты прислал, теперь здесь >>>
    
    Profiler_Init();
    initDayNightCycle();
    initPhone();
    
    AssetManager assetManager;
    AssetManager_Init(&assetManager, ren);
    
    // Перезагружаем/получаем шрифты через менеджер для остальной игры
    font = AssetManager_GetFont(&assetManager, "arial.ttf", 16);
    if (!font) {
        printf("Не удалось загрузить основной шрифт, выход.\n");
        return 1;
    }
    TTF_Font* large_font = AssetManager_GetFont(&assetManager, "arial.ttf", 24);

    GameConfig config = {
        .mouseSensitivity = 0.003f, .walkSpeed = 0.3f, .runSpeed = 0.5f,
        .crouchSpeedMultiplier = 0.5f, .acceleration = 10.0f, .deceleration = 15.0f,
        .jumpForce = 0.35f, .gravity = 1.2f, .fov = 500.0f
    };
    loadConfig("settings.cfg", &config);
    
    EditableVariable editorVars[] = {
        { "Mouse Sensitivity", &config.mouseSensitivity, 0.0001f, 0.001f, 0.01f },
        { "Walk Speed",        &config.walkSpeed,        0.05f,   0.1f,   1.0f  },
        { "Run Speed",         &config.runSpeed,         0.05f,   0.5f,   2.0f  },
        { "Jump Force",        &config.jumpForce,        0.05f,   0.1f,   1.0f  },
        { "Gravity",           &config.gravity,          0.1f,    0.1f,   5.0f  },
        { "Field of View",     &config.fov,              5.0f,    50.0f,  500.0f}
    };
    const int numEditorVars = sizeof(editorVars) / sizeof(editorVars[0]);

    QuestSystem questSystem = {0};
    initQuestSystem(&questSystem);
    
    initCoins();
    initHandsSystem();
    initBoss();

    spawnBottle((Vec3){3, -1, -3});
    spawnBottle((Vec3){-4, -1, 2});
    
    // --- ЭТАП 4: ПОДГОТОВКА К ГЛАВНОМУ ЦИКЛУ ---
    SDL_SetRelativeMouseMode(SDL_TRUE);

    spawnBottle((Vec3){3, -1, -3});
spawnBottle((Vec3){-4, -1, 2});
spawnBottle((Vec3){7, 2, 5});
spawnBottle((Vec3){-2, 0, -6});

    Vec3 cube[8] = {
        {-2,-2,-2}, {2,-2,-2}, {2,2,-2}, {-2,2,-2},
        {-2,-2,2},  {2,-2,2},  {2,2,2},  {-2,2,2}
    };
    int edges[12][2] = { {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7} };
    Vec3 faceNormals[6] = { {0,0,-1}, {0,0,1}, {0,-1,0}, {0,1,0}, {-1,0,0}, {1,0,0} };
    int edgeFaces[12][2] = { {0,2},{0,4},{0,3},{0,5}, {1,2},{1,4},{1,3},{1,5}, {2,5},{2,4},{3,4},{3,5} };
    
    Camera cam = { .x = 0, .y = 0, .z = -8, .height = STANDING_HEIGHT, .targetHeight = STANDING_HEIGHT };
    float playerRadius = 0.3f;

    g_currentState = STATE_MAIN_MENU;
    
    SDL_SetRelativeMouseMode(SDL_TRUE);

    int running = 1;
    SDL_Event e;
    Uint32 lastTime = SDL_GetTicks();
    const Uint8* keyState = SDL_GetKeyboardState(NULL);

    int show_editor = 1;
    int selected_item = 0;

    int rightMouseButtonHeld = 0; // [НОВЫЙ КОД]
    Uint32 rightMouseHoldStartTime = 0;

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
    float deltaTime = (currentTime - lastTime) / 1000.0f;
    if (deltaTime > 0.1f) deltaTime = 0.1f;
    lastTime = currentTime;

    const Uint8* keyState = SDL_GetKeyboardState(NULL);

    // --- ОБРАБОТКА ВВОДА В ЗАВИСИМОСТИ ОТ СОСТОЯНИЯ ---
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_isExiting = 1;
        
        if (e.type == SDL_KEYDOWN) {
            switch (g_currentState) {
                case STATE_MAIN_MENU:
                    if (e.key.keysym.sym == SDLK_UP) g_menuSelectedOption = (g_menuSelectedOption - 1 + 3) % 3;
                    if (e.key.keysym.sym == SDLK_DOWN) g_menuSelectedOption = (g_menuSelectedOption + 1) % 3;
                    if (e.key.keysym.sym == SDLK_RETURN) {
                        if (g_menuSelectedOption == 0) { // Start
                            g_currentState = STATE_IN_GAME;
                            SDL_SetRelativeMouseMode(SDL_TRUE); // Захватываем мышь
                        } else if (g_menuSelectedOption == 1) { // Settings
                            g_currentState = STATE_SETTINGS;
                        } else if (g_menuSelectedOption == 2) { // Exit
                            g_isExiting = 1; // Запускаем плавный выход
                        }
                    }
                    break;
                case STATE_SETTINGS:
                    if (e.key.keysym.sym == SDLK_UP) g_settingsSelectedOption = (g_settingsSelectedOption - 1 + numEditorVars + 1) % (numEditorVars + 1);
                    if (e.key.keysym.sym == SDLK_DOWN) g_settingsSelectedOption = (g_settingsSelectedOption + 1) % (numEditorVars + 1);
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_currentState = STATE_MAIN_MENU;
                        saveConfig("settings.cfg", &config); // Сохраняем при выходе
                    }
                    if (g_settingsSelectedOption < numEditorVars) { // Если выбрана настройка
                        if (e.key.keysym.sym == SDLK_LEFT) *editorVars[g_settingsSelectedOption].value_ptr -= editorVars[g_settingsSelectedOption].step;
                        if (e.key.keysym.sym == SDLK_RIGHT) *editorVars[g_settingsSelectedOption].value_ptr += editorVars[g_settingsSelectedOption].step;
                    } else if (e.key.keysym.sym == SDLK_RETURN) { // Если выбрано "Back"
                        g_currentState = STATE_MAIN_MENU;
                        saveConfig("settings.cfg", &config); // Сохраняем при выходе
                    }
                    break;
                case STATE_IN_GAME:
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_currentState = STATE_MAIN_MENU;
                        SDL_SetRelativeMouseMode(SDL_FALSE); // Возвращаем мышь в меню
                    }
                    // Вся остальная обработка ввода для игры
                    if (e.key.keysym.sym == SDLK_p) {
                        if (g_phone.state == PHONE_STATE_HIDDEN || g_phone.state == PHONE_STATE_HIDING) g_phone.state = PHONE_STATE_SHOWING;
                        else if (g_phone.state == PHONE_STATE_VISIBLE || g_phone.state == PHONE_STATE_SHOWING) g_phone.state = PHONE_STATE_HIDING;
                    }
                    if (g_phone.state == PHONE_STATE_VISIBLE && e.key.keysym.sym == SDLK_RETURN) {
                        Vec3 start = {cam.x - 40, 25, cam.z + 20};
                        Vec3 end = {cam.x + 40, 25, cam.z - 20};
                        startAirstrike(start, end, &cam);
                        g_phone.state = PHONE_STATE_HIDING;
                    }
                    // <<< ВОТ ОН, ПРЫЖОК! >>>
                if (e.key.keysym.sym == SDLK_SPACE && !cam.isCrouching) {
                    if (isGrounded(&cam, playerRadius, collisionBoxes, numCollisionBoxes)) {
                        cam.vy = config.jumpForce;
                    }
                }
                // <<< И F-КЛАВИШИ! >>>
                if (e.key.keysym.sym == SDLK_F1) show_editor = !show_editor;
                if (e.key.keysym.sym == SDLK_F3) g_showProfiler = !g_showProfiler;
                break;
            }
        }
         if (g_currentState == STATE_IN_GAME && e.type == SDL_MOUSEMOTION) {
            cam.rotY += e.motion.xrel * config.mouseSensitivity;
            cam.rotX -= e.motion.yrel * config.mouseSensitivity;
            if (cam.rotX > 1.5f) cam.rotX = 1.5f;
            if (cam.rotX < -1.5f) cam.rotX = -1.5f;
        }
    }
        Profiler_End(PROF_OTHER);
        Profiler_Start(PROF_PHYSICS_COLLISIONS);
        
        for (int i = 0; i < g_numPickups; i++) {
    updatePickupPhysics(&g_pickups[i], deltaTime, collisionBoxes, numCollisionBoxes, &cam);
}
updateShards(deltaTime);

switch (g_currentState) {
        case STATE_MAIN_MENU:
            g_menuRhombusAngle += deltaTime * 2.0f;
            drawMainMenu(ren, large_font);
            break;
        case STATE_SETTINGS:
            drawSettingsMenu(ren, font, editorVars, numEditorVars);
            break;
        case STATE_IN_GAME:
        g_fov = config.fov;
        
        cam.isCrouching = keyState[SDL_SCANCODE_LCTRL];
        
        if (cam.isCrouching) {
            cam.targetHeight = CROUCHING_HEIGHT;
        } else {
            cam.targetHeight = STANDING_HEIGHT;
        }
        
        cam.height = lerp(cam.height, cam.targetHeight, deltaTime * CROUCH_LERP_SPEED);
        
        cam.isRunning = (keyState[SDL_SCANCODE_LSHIFT] || keyState[SDL_SCANCODE_RSHIFT]) && !cam.isCrouching;

        float crouchSpeedMultiplier = config.crouchSpeedMultiplier;
        float walkSpeed = config.walkSpeed;
        float runSpeed = config.runSpeed;
        
        float moveSpeed = cam.isRunning ? runSpeed : walkSpeed;
        if (cam.isCrouching) {
            moveSpeed *= crouchSpeedMultiplier;
        }
        
        cam.targetVx = 0;
        cam.targetVz = 0;
        float forwardX = fast_sin(cam.rotY); float forwardZ = fast_cos(cam.rotY);
        float rightX = fast_cos(cam.rotY); float rightZ = -fast_sin(cam.rotY);
        if (keyState[SDL_SCANCODE_W]) { cam.targetVx += forwardX * moveSpeed; cam.targetVz += forwardZ * moveSpeed; }
        if (keyState[SDL_SCANCODE_S]) { cam.targetVx -= forwardX * moveSpeed; cam.targetVz -= forwardZ * moveSpeed; }
        if (keyState[SDL_SCANCODE_A]) { cam.targetVx -= rightX * moveSpeed; cam.targetVz -= rightZ * moveSpeed; }
        if (keyState[SDL_SCANCODE_D]) { cam.targetVx += rightX * moveSpeed; cam.targetVz += rightZ * moveSpeed; }
        
        // --- НОВЫЙ БЛОК ДВИЖЕНИЯ С ИНЕРЦИЕЙ ---
        if (!g_cinematic.isActive) {
        // Сначала проверяем, стоит ли камера на земле
        int grounded = isGrounded(&cam, playerRadius, collisionBoxes, numCollisionBoxes);

        if (grounded) {
            // --- ЛОГИКА ДВИЖЕНИЯ НА ЗЕМЛЕ (как было раньше) ---
            // Быстрое ускорение и резкое торможение для отзывчивости
            float accel = cam.isRunning ? config.acceleration * 1.2f : config.acceleration;
            float decel = config.deceleration;

            if (fabsf(cam.targetVx) > 0.001f || fabsf(cam.targetVz) > 0.001f) {
                // Если нажаты клавиши - ускоряемся к целевой скорости
                cam.vx = lerp(cam.vx, cam.targetVx, deltaTime * accel);
                cam.vz = lerp(cam.vz, cam.targetVz, deltaTime * accel);
            } else {
                // Если клавиши отпущены - быстро тормозим
                cam.vx = lerp(cam.vx, 0, deltaTime * decel);
                cam.vz = lerp(cam.vz, 0, deltaTime * decel);
            }
        } else {
            // --- ЛОГИКА ДВИЖЕНИЯ В ВОЗДУХЕ (новая) ---
            // Слабый контроль и очень медленное торможение для сохранения инерции
            
            if (fabsf(cam.targetVx) > 0.001f || fabsf(cam.targetVz) > 0.001f) {
                // Если в полете нажаты клавиши - лишь СЛЕГКА меняем траекторию
                cam.vx = lerp(cam.vx, cam.targetVx, deltaTime * AIR_ACCELERATION);
                cam.vz = lerp(cam.vz, cam.targetVz, deltaTime * AIR_ACCELERATION);
            } else {
                // Если клавиши отпущены - скорость почти не гасится (ИНЕРЦИЯ!)
                cam.vx = lerp(cam.vx, 0, deltaTime * AIR_DECELERATION);
                cam.vz = lerp(cam.vz, 0, deltaTime * AIR_DECELERATION);
            }
        }
        
        float newX = cam.x + cam.vx * deltaTime * 60.0f;
        float newZ = cam.z + cam.vz * deltaTime * 60.0f;

        if (!checkWallCollision(newX, cam.y, cam.z, playerRadius, collisionBoxes, numCollisionBoxes)) {
            cam.x = newX;
        } else {
            if (!checkWallCollision(cam.x, cam.y, newZ, playerRadius, collisionBoxes, numCollisionBoxes)) {
                cam.vx = 0;
            }
        }

        if (!checkWallCollision(cam.x, cam.y, newZ, playerRadius, collisionBoxes, numCollisionBoxes)) {
            cam.z = newZ;
        } else {
            if (!checkWallCollision(newX, cam.y, cam.z, playerRadius, collisionBoxes, numCollisionBoxes)) {
                cam.vz = 0;
            }
        }
        
        cam.vy -= config.gravity * deltaTime;
        
        float newY = cam.y + cam.vy * deltaTime * 60.0f;
        
        if (cam.vy > 0) {
            if (!checkCollision(cam.x, newY + cam.height, cam.z, playerRadius, collisionBoxes, numCollisionBoxes)) {
                cam.y = newY;
            } else {
                cam.vy = 0;
            }
        } else {
            if (newY <= 0) {
                cam.y = 0;
                if (cam.vy < -0.2f) {
                    cam.currentBobY -= fabsf(cam.vy) * 0.15f;
                }
                cam.vy = 0;
            } else {
                int foundGround;
                float groundHeight = getGroundHeight(cam.x, cam.z, playerRadius, collisionBoxes, numCollisionBoxes, &foundGround);
                
                if (foundGround) {
                    float targetY = groundHeight + playerRadius;
                    
                    if (newY <= targetY && cam.y > targetY - 0.5f) {
                        cam.y = targetY;
                        if (cam.vy < -0.2f) {
                            cam.currentBobY -= fabsf(cam.vy) * 0.15f;
                        }
                        cam.vy = 0;
                    } else if (newY > targetY || !foundGround) {
                        cam.y = newY;
                    }
                } else {
                    cam.y = newY;
                }
            }
        }
        
        if ((fabsf(cam.vx) > 0.01f || fabsf(cam.vz) > 0.01f) && isGrounded(&cam, playerRadius, collisionBoxes, numCollisionBoxes)) {
            float checkDist = 0.5f;
            float checkX = cam.x + (cam.vx > 0 ? checkDist : -checkDist) * (fabsf(cam.vx) > 0.01f ? 1 : 0);
            float checkZ = cam.z + (cam.vz > 0 ? checkDist : -checkDist) * (fabsf(cam.vz) > 0.01f ? 1 : 0);
            
            if (checkWallCollision(checkX, cam.y, checkZ, playerRadius, collisionBoxes, numCollisionBoxes)) {
                float stepUpHeight = 0.6f;
                
                if (!checkWallCollision(checkX, cam.y + stepUpHeight, checkZ, playerRadius, collisionBoxes, numCollisionBoxes)) {
                    int foundGround;
                    float groundAhead = getGroundHeight(checkX, checkZ, playerRadius, collisionBoxes, numCollisionBoxes, &foundGround);
                    
                    if (foundGround && groundAhead > cam.y - playerRadius && groundAhead < cam.y + stepUpHeight) {
                        cam.y = lerp(cam.y, groundAhead + playerRadius, deltaTime * 8.0f);
                    }
                }
            }
        }
        }

        updateCameraBob(&cam, deltaTime);

        // Обновляем состояние и анимацию рук
        updateHandsState(&cam, deltaTime);
        updateHandsAnimation(&cam, deltaTime);
        updateGravityGlove(&cam, deltaTime);

        Profiler_End(PROF_PHYSICS_COLLISIONS);
        Profiler_Start(PROF_GAME_LOGIC);

        
        updatePhone(deltaTime);
        updateDayNightCycle(deltaTime, cam);
        updateWorldEvolution(deltaTime);
        updateAirstrike(deltaTime, &cam);

        if (g_bossFightActive) {
        updateBoss(deltaTime, &cam);
        updateGlitches(deltaTime, &cam);
    }
        
        // ДОБАВЛЯЕМ: Обновляем монеты и проверяем сбор
        updateCoins(deltaTime);
        checkCoinCollection(&cam);
        
        updateQuestSystem(&questSystem, &cam, playerRadius);
        // ИСПРАВЛЯЕМ: Передаём collisionBoxes в функцию проверки целей
        checkQuestObjectives(&questSystem, &cam, collisionBoxes, numCollisionBoxes);

        Profiler_End(PROF_GAME_LOGIC);
    Profiler_Start(PROF_RENDERING);
        Uint8 bgR = 20 + (Uint8)(g_worldEvolution.transitionProgress * 20);
Uint8 bgG = 20 + (Uint8)(g_worldEvolution.transitionProgress * 25);
Uint8 bgB = 30 + (Uint8)(g_worldEvolution.transitionProgress * 30);

        // Определяем цвет фона (как и было)
        const SDL_Color baseBackgroundColor = {20, 20, 30, 255}; 
        SDL_Color targetBackgroundColor = g_dayNight.fogColor;
        SDL_Color finalClearColor = lerpColor(baseBackgroundColor, targetBackgroundColor, g_worldEvolution.skyboxAlpha);
        SDL_SetRenderDrawColor(ren, finalClearColor.r, finalClearColor.g, finalClearColor.b, 255);
        SDL_RenderClear(ren);
        clearZBuffer();

        // Выбираем, какую камеру использовать для рендера
        Camera renderCam = cam;

        // 2. Применяем эффекты покачивания и тряски к этой временной камере
        renderCam.y += cam.currentBobY;
        renderCam.rotY += cam.currentBobX * 0.02f;
        if (g_cinematic.isActive) {
            g_cinematic.fov = lerp(g_cinematic.fov, 150.0f, deltaTime * 2.0f); // Зум
            
            Camera cinematicRenderCam = {0};
            cinematicRenderCam.x = g_cinematic.position.x;
            cinematicRenderCam.y = g_cinematic.position.y;
            cinematicRenderCam.z = g_cinematic.position.z;
            
            float dx = g_cinematic.target.x - cinematicRenderCam.x;
            float dy = g_cinematic.target.y - cinematicRenderCam.y;
            float dz = g_cinematic.target.z - cinematicRenderCam.z;
            cinematicRenderCam.rotY = atan2f(dx, dz);
            cinematicRenderCam.rotX = -atan2f(dy, sqrtf(dx*dx + dz*dz));
            
            renderCam = cinematicRenderCam;
            g_fov = g_cinematic.fov; // Временно меняем FOV
        } else {
             g_fov = config.fov;
        }

        // 3. Передаем renderCam ВО ВСЕ ФУНКЦИИ ОТРИСОВКИ
        drawSkybox(ren);
        drawSunAndMoon(ren, renderCam); 

        drawFloor(ren, renderCam);
        
        // ОТРИСОВКА ПЛАТФОРМ С ОТСЕЧЕНИЕМ
for (int i = 0; i < numCollisionBoxes; i++) {
    // <<< ВОТ ОНО! Рисуем только то, что в кадре. >>>
    if (isBoxInFrustum_Improved(&collisionBoxes[i], renderCam)) {
        drawOptimizedBox(ren, &collisionBoxes[i], renderCam);
    }
}

drawEvolvingWalls(ren, renderCam); // Для этого отсечение не так важно

// ОТРИСОВКА МОНЕТ С ОТСЕЧЕНИЕМ
for (int i = 0; i < g_numCoins; i++) {
    if (!g_coins[i].collected) {
        // <<< И здесь тоже! Отсекаем по простой точке >>>
        if (isPointInFrustum(g_coins[i].pos, renderCam)) {
            drawCoin(ren, &g_coins[i], renderCam);
        }
    }
}

// ОТРИСОВКА ПРЕДМЕТОВ С ОТСЕЧЕНИЕМ
for (int i = 0; i < g_numPickups; i++) {
    if (g_pickups[i].state != PICKUP_STATE_BROKEN) {
        // <<< И здесь! >>>
        if (isPointInFrustum(g_pickups[i].pos, renderCam)) {
            drawPickupObject(ren, &g_pickups[i], renderCam);
        }
    }
}
        // [НОВЫЙ КОД] Рассчитываем траекторию, если держим объект
        if (g_hands.heldObject && rightMouseButtonHeld) {
            calculateTrajectory(&cam, g_hands.currentThrowPower, &g_trajectory, collisionBoxes, numCollisionBoxes, config.gravity);
        } else {
            g_trajectory.numPoints = 0; // Прячем траекторию
        }

        // [НОВЫЙ КОД] Отрисовка луча прицеливания
        if (g_hands.currentState == HAND_STATE_AIMING) {
            Vec3 rayStart = {cam.x, cam.y + cam.height, cam.z};
            Vec3 rayDir = {fast_sin(cam.rotY)*fast_cos(cam.rotX), -fast_sin(cam.rotX), fast_cos(cam.rotY)*fast_cos(cam.rotX)};
            rayDir = normalize(rayDir);

            float rayLen = g_hands.targetedObject ? 
                sqrtf(powf(g_hands.targetedObject->pos.x - rayStart.x, 2)) : // сокращенное расстояние до объекта
                RAYCAST_MAX_DISTANCE;
            
            Vec3 rayEnd = {rayStart.x + rayDir.x * rayLen, rayStart.y + rayDir.y * rayLen, rayStart.z + rayDir.z * rayLen};
            clipAndDrawLine(ren, rayStart, rayEnd, cam, (SDL_Color){255,165,0,100});
        }

        // Рендерим авиаудар
        if (g_airstrike.isActive) {
            for (int i = 0; i < 3; i++) {
                drawJet(ren, &g_airstrike.jets[i], renderCam);
                drawBomb(ren, &g_airstrike.bombs[i], renderCam);
                drawExplosion(ren, &g_airstrike.explosions[i], renderCam);
            }
        }

        drawGlassShards(ren, renderCam);
        applyGlitchEffect(ren);
        drawGlitches(ren, renderCam);
        drawBoss(ren, renderCam);
        drawTrajectory(ren,renderCam, &g_trajectory);
        //drawHands(ren, renderCam);
        
        float lightAngle = SDL_GetTicks() * 0.0003f;
        Vec3 lightDir = normalize((Vec3){fast_cos(lightAngle) * 1.5f, 2, fast_sin(lightAngle) * 1.5f});
        for (int i = 0; i < 12; ++i) {
            float b1 = dot(faceNormals[edgeFaces[i][0]], lightDir);
            float b2 = dot(faceNormals[edgeFaces[i][1]], lightDir);
            float brightness = fmaxf(0.4f, fmaxf(b1, b2));
            int c = (int)(brightness * 255);
            if (c > 255) c = 255;
            if (c < 60) c = 60;
            SDL_Color edgeColor = {(Uint8)c, (Uint8)c, (Uint8)c, 255};

            Vec3 p1 = cube[edges[i][0]];
            Vec3 p2 = cube[edges[i][1]];
            clipAndDrawLine(ren, p1, p2, renderCam, edgeColor);
        }

        Profiler_End(PROF_RENDERING);

    // --- PROFILER: Начинаем снова замер "прочего" времени (для UI) ---
    Profiler_Start(PROF_OTHER);

        if (show_editor) {
            char evolutionStatus[128];
            const char* stateName[] = {
                "WIREFRAME", "GRID GROWING", "CUBE COMPLETE",
                "MATERIALIZING", "TEXTURED", "REALISTIC"
            };
            snprintf(evolutionStatus, 128, "Evolution: %s (%.0f%%)",
                    stateName[g_worldEvolution.currentState],
                    g_worldEvolution.transitionProgress * 100);
            SDL_Color cyan = {0, 255, 255, 255};
            drawText(ren, font, evolutionStatus, WIDTH/2 - 100, 10, cyan);
        }

        drawQuestConnections(ren, &questSystem, renderCam, SDL_GetTicks() * 0.001f);
        for (int i = 0; i < questSystem.numNodes; i++) {
            drawQuestNode(ren, &questSystem.nodes[i], renderCam, SDL_GetTicks() * 0.001f);
        }
        
        if (cam.isRunning && cam.isMoving) {
            SDL_SetRenderDrawColor(ren, 255, 100, 100, 255);
            SDL_Rect runIndicator = {10, 10, 20, 20};
            SDL_RenderFillRect(ren, &runIndicator);
        }
        
        if (show_editor) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 150);
            SDL_Rect bgRect = { 10, 10, 300, numEditorVars * 20 + 10 };
            SDL_RenderFillRect(ren, &bgRect);

            SDL_Color white = {255, 255, 255, 255};
            SDL_Color yellow = {255, 255, 0, 255};

            for (int i = 0; i < numEditorVars; ++i) {
                char buffer[128];
                snprintf(buffer, 128, "%s %s: %.4f", 
                         (i == selected_item) ? ">" : " ",
                         editorVars[i].name,
                         *editorVars[i].value_ptr);
                
                drawText(ren, font, buffer, 20, 15 + i * 20, (i == selected_item) ? yellow : white);
            }
        }

        Profiler_Draw(ren, font);
        drawQuestUI(ren, font, &questSystem);

        for (int i = 0; i < questSystem.numNodes; i++) {
            QuestNode* node = &questSystem.nodes[i];
            float dist = sqrtf(powf(cam.x - node->worldPos.x, 2) +
                            powf(cam.y - node->worldPos.y, 2) +
                            powf(cam.z - node->worldPos.z, 2));
            
            if (dist < 2.0f && node->status == QUEST_AVAILABLE) {
                SDL_Color white = {255, 255, 255, 255};
                drawText(ren, font, "Press [E] to accept quest", WIDTH/2 - 100, HEIGHT - 50, white);
                break;
            }
        }
        Profiler_End(PROF_OTHER);
        Profiler_Update();
        drawPhone(ren, font);

        if (g_phone.state == PHONE_STATE_VISIBLE || g_phone.state == PHONE_STATE_SHOWING) {
             const int phoneWidth = 250, phoneHeight = 500;
             int currentY = (int)lerp((float)HEIGHT, (float)(HEIGHT - phoneHeight - 50), g_phone.animationProgress);

             SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
             SDL_SetRenderDrawColor(ren, 25, 25, 30, 230);
             SDL_Rect phoneBody = { WIDTH - phoneWidth - 50, currentY, phoneWidth, phoneHeight };
             SDL_RenderFillRect(ren, &phoneBody);
             
             SDL_Color textColor = {200, 200, 200, 255};
             SDL_Color selectedColor = {255, 255, 0, 255};
             drawText(ren, font, "DEMOCRACY OS", phoneBody.x + 20, phoneBody.y + 20, textColor);
             drawText(ren, font, "> Вызвать демократию", phoneBody.x + 30, phoneBody.y + 70, selectedColor);
        }
        
        // Кинематографичные полосы
        if (g_cinematic.isActive) {
            int barHeight = HEIGHT / 8;
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_Rect topBar = {0, 0, WIDTH, barHeight};
            SDL_Rect bottomBar = {0, HEIGHT - barHeight, WIDTH, barHeight};
            SDL_RenderFillRect(ren, &topBar);
            SDL_RenderFillRect(ren, &bottomBar);
        }
        break;
    }
        if (g_isExiting) {
    g_exitFadeAlpha = lerp(g_exitFadeAlpha, 255.0f, deltaTime * 4.0f);
    if (g_exitFadeAlpha > 254.0f) {
        running = 0; // Когда экран полностью черный, выходим по-настоящему
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)g_exitFadeAlpha);
    SDL_Rect fadeRect = {0, 0, WIDTH, HEIGHT};
    SDL_RenderFillRect(ren, &fadeRect);
}

        SDL_RenderPresent(ren);
    }
    AssetManager_Destroy(&assetManager);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    
    return 0;
}
