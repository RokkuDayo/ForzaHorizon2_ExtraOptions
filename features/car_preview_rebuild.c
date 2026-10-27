#include "car_preview_rebuild.h"
#include "autodrive_game.h"
#include "bindings.h"
#include "logging.h"
#include "support.h"

/* The RebuildCarPreviews binding (F8) re-renders the garage thumbnail of
   listed cars, at the [Graphics] CarPreviewScale size, through the game's own
   save path. For each car it builds
   the list Car::BuildSaveTasks (0x2f3520) makes for a saved car (0x2d9820:
   VerifySpace, GarageThumbnail and GarageFinalize tasks) and hands it to a
   new CBackgroundSaveTask, as the game does for its background saves
   (0xa30310): the background task's save queue (0x351d50, ticked by the
   game every frame) runs each task, writes the files it produced and then
   lets it record the new thumbnail on the car. Running the tasks without
   that queue renders the picture but never writes it.

   The binding is read and the cars advanced from UserInterface's vtable
   slot 8, which the game calls once per frame on its main thread in menus
   and while driving.

   The cars are the Career_Garage rows whose thumbnail is a saved one
   ("CS:..."; stock cars show packaged pictures), read from the game's own
   database when the rebuild starts. */
#define UI_VTABLE 0x2ecfb80u
#define UI_TICK_SLOT 8
#define PLAYER_MANAGER 0x327270u
#define CAR_BY_ID 0x2dadd0u
#define BUILD_LIVERY_TASKS 0x2d9820u
#define GAME_ALLOCATE 0x3229c0u
#define GAME_FREE 0x31c7b0u
#define BACKGROUND_TASK_VTABLE 0x2d3bbb8u
#define BACKGROUND_TASK_SIZE 0x98u
#define SAVE_QUEUE_INIT 0x33b660u
#define BACKGROUND_TASK_ADD 0xa46ac0u
#define BACKGROUND_SCHEDULER 0x367a6d0u
#define BACKGROUND_SUBMIT 0x126c170u
#define THUMBNAIL_STATE 0x34af4b8u
#define THUMBNAIL_RENDER_FLAG 0xa0
#define CAPTURE_OWNER 0x34aba60u
#define CAPTURE_IDLE 5
#define CAPTURE_START 1
#define CAPTURE_FINISH 0xe1f6d0u
#define TASK_REFERENCES 0x70
#define PHASE_WAIT_MS 30000u
#define SAVE_WAIT_MS 120000u
#define GAME_DATABASE 0x34b3640u
#define MAX_CARS 256

typedef void *(*Getter)(void *);
typedef void **(*CarById)(void *garage, void **out, int id, int flag);
typedef void *(*BuildTasks)(void *list, void **car);
typedef void *(*Allocate)(size_t);
typedef void (*Initialize)(void *);
typedef void (*AddTask)(void *background, void **task);
typedef void (*Submit)(void *scheduler, void *background);
typedef void (*Release)(void *);
typedef void (*Free)(void *);

typedef struct {
    void **begin, **end, **capacity;
} RefVector;

static unsigned char *image, *stub;
enum { IDLE, NEXT_CAR, CAPTURE_READY, SAVING, SETTLE };

static int phase, key_down, rendered;
static volatile int start_requested;
static void *saving;
static int car_ids[MAX_CARS];
static unsigned car_count, next_car;
static ULONGLONG deadline;

static void release(void *object) {
    if (object) {
        ((Release)(*(void ***)object)[3])(object);
    }
}

static void add_reference(void *object) {
    ((Release)(*(void ***)object)[2])(object);
}

static void clear(RefVector *vector) {
    for (void **item = vector->begin; item && item < vector->end; ++item) {
        release(*item);
    }
    if (vector->begin) {
        ((Free)(image + GAME_FREE))(vector->begin);
    }
    vector->begin = vector->end = vector->capacity = NULL;
}

static void *garage(void) {
    void *manager = ((void *(*)(void))(image + PLAYER_MANAGER))();
    if (!manager) {
        return NULL;
    }
    void *player = ((Getter)(*(void ***)manager)[0x48 / 8])(manager);
    return player ? ((Getter)(*(void ***)player)[0xd8 / 8])(player) : NULL;
}

typedef struct {
    char *pointer_or_text[2];  // MSVC std::string: inline text up to 15 bytes
    size_t size, capacity;
} GameString;

typedef void (*Query)(void *database, void **result, const char *sql);
typedef int (*RowCount)(void *result);
typedef int (*IntegerAt)(void *result, int row, int column, int unused);

static const char *string_text(const GameString *text) {
    return text->capacity >= 16 ? text->pointer_or_text[0] : (const char *)text->pointer_or_text;
}

static void append(char *buffer, size_t capacity, size_t *length, const char *text) {
    while (*text && *length + 1 < capacity) {
        buffer[(*length)++] = *text++;
    }
    buffer[*length] = 0;
}

/* Reads the garage Ids with a saved thumbnail, as the game queries its
   garage table (0x2e0210: table name in the garage object's string at
   +0x10; the SQL is a plain C string, formatted by 0x2681e0 = vsnprintf;
   database [GAME_DATABASE], query vtable +0x40, row count +0x148, integer
   cell +0x98). */
static void read_ids(void *owner) {
    car_count = next_car = 0;
    void *database = NULL;
    memory_read_pointer(image + GAME_DATABASE, &database);
    if (!database) {
        return;
    }
    char sql[256];
    size_t length = 0;
    sql[0] = 0;
    append(sql, sizeof(sql), &length, "SELECT Id FROM ");
    append(sql, sizeof(sql), &length, string_text((const GameString *)((unsigned char *)owner + 0x10)));
    append(sql, sizeof(sql), &length, " WHERE Thumbnail LIKE 'CS:%' ORDER BY Id");
    void *result = NULL;
    ((Query)((*(void ***)database)[0x40 / 8]))(database, &result, sql);
    log_append("CAR PREVIEW REBUILD: query ");
    log_append(sql);
    log_append_value("\r\nCAR PREVIEW REBUILD: result present ", result != NULL);
    if (result) {
        int rows = ((RowCount)((*(void ***)result)[0x148 / 8]))(result);
        log_append_value("CAR PREVIEW REBUILD: rows ", rows);
        for (int row = 0; row < rows && car_count < MAX_CARS; ++row) {
            int id = ((IntegerAt)((*(void ***)result)[0x98 / 8]))(result, row, 0, 0);
            if (id > 0) {
                car_ids[car_count++] = id;
            }
        }
        release(result);
    }
}

/* The capture state machine that renders garage thumbnails, at
   [[CAPTURE_OWNER]+0x50]+0x24; null when it does not exist yet. */
static volatile int32_t *capture_state(void) {
    unsigned char *owner, *capture;
    if (!memory_read_pointer(image + CAPTURE_OWNER, (void **)&owner) || !owner ||
        !memory_read_pointer(owner + 0x50, (void **)&capture) || !capture) {
        return NULL;
    }
    return (volatile int32_t *)(capture + 0x24);
}

/* Starts a thumbnail capture as PopupSaveDataWithThumbnail does before its
   saves (0xe10cd9): once the capture state is idle (5), set the render flag
   and move it to 1. Without this the thumbnail task re-saves the existing
   pictures (flag clear) or waits forever for a render (capture idle). */
static int start_capture(void) {
    volatile int32_t *state = capture_state();
    unsigned char *thumbnails;
    if (!state || *state != CAPTURE_IDLE ||
        !memory_read_pointer(image + THUMBNAIL_STATE, (void **)&thumbnails) || !thumbnails) {
        return 0;
    }
    thumbnails[THUMBNAIL_RENDER_FLAG] = 1;
    *state = CAPTURE_START;
    return 1;
}

/* Hands one car's save tasks to a new background save. Returns the last
   task with an extra reference, to see when the save is done, or null if
   nothing was queued. */
static void *queue_car(void *owner, int car_id) {
    void *car = NULL;
    ((CarById)(image + CAR_BY_ID))(owner, &car, car_id, 0);
    if (!car) {
        log_append_value("CAR PREVIEW REBUILD: no car for garage Id ", car_id);
        return NULL;
    }
    RefVector tasks = {0};
    // The builder takes over one reference to the car.
    add_reference(car);
    void *reference = car;
    ((BuildTasks)(image + BUILD_LIVERY_TASKS))(&tasks, &reference);
    release(car);
    void **background = ((Allocate)(image + GAME_ALLOCATE))(BACKGROUND_TASK_SIZE);
    if (!background || tasks.begin == tasks.end) {
        clear(&tasks);
        log_append_value("CAR PREVIEW REBUILD: no save tasks for garage Id ", car_id);
        return NULL;
    }
    background[0] = image + BACKGROUND_TASK_VTABLE;
    ((Initialize)(image + SAVE_QUEUE_INIT))(background + 1);
    for (void **task = tasks.begin; task < tasks.end; ++task) {
        // The queue takes over this reference.
        add_reference(*task);
        void *task_reference = *task;
        ((AddTask)(image + BACKGROUND_TASK_ADD))(background, &task_reference);
    }
    void *last = tasks.end[-1];
    add_reference(last);
    clear(&tasks);
    void *scheduler;
    memory_copy(&scheduler, image + BACKGROUND_SCHEDULER, sizeof(scheduler));
    ((Submit)(image + BACKGROUND_SUBMIT))(scheduler, background);
    log_append_value("CAR PREVIEW REBUILD: queued garage Id ", car_id);
    return last;
}

static int capture_idle(void) {
    volatile int32_t *state = capture_state();
    return state && *state == CAPTURE_IDLE;
}

static void stop(const char *message) {
    volatile int32_t *state = capture_state();
    log_append(message);
    log_append_value("CAR PREVIEW REBUILD: capture state ", state ? *state : -1);
    if (saving) {
        release(saving);
        saving = NULL;
    }
    phase = IDLE;
}

/* One car at a time, in PopupSaveDataWithThumbnail's order (0xe10c20):
   start a capture once the capture state is idle, wait for it to be ready
   (idle again), run the saves, let the presentation and thumbnail renderer
   settle with 0xe1f6d0 (it uses only globals), and wait for idle again.
   Queuing saves before the capture is ready renders the first car of a
   session but leaves the following ones blank. */
static void advance(void) {
    ULONGLONG now = GetTickCount64();
    switch (phase) {
        case NEXT_CAR:
            if (next_car >= car_count) {
                log_append_value("CAR PREVIEW REBUILD: finished; cars rendered ", rendered);
                phase = IDLE;
            } else if (start_capture()) {
                phase = CAPTURE_READY;
                deadline = now + PHASE_WAIT_MS;
            } else if (now > deadline) {
                stop("CAR PREVIEW REBUILD: capture never idle; stopped.\r\n");
            }
            break;
        case CAPTURE_READY:
            if (capture_idle()) {
                void *owner = garage();
                if (!owner) {
                    stop("CAR PREVIEW REBUILD: garage unavailable; stopped.\r\n");
                    break;
                }
                saving = queue_car(owner, car_ids[next_car++]);
                phase = saving ? SAVING : SETTLE;
                deadline = now + SAVE_WAIT_MS;
            } else if (now > deadline) {
                stop("CAR PREVIEW REBUILD: capture not ready; stopped.\r\n");
            }
            break;
        case SAVING: {
            // Done once the save queue has released the task (ours is the last reference).
            LONG references;
            memory_copy(&references, (unsigned char *)saving + TASK_REFERENCES, sizeof(references));
            if (references <= 1) {
                release(saving);
                saving = NULL;
                ++rendered;
                ((void (*)(void *))(image + CAPTURE_FINISH))(NULL);
                phase = SETTLE;
                deadline = now + PHASE_WAIT_MS;
            } else if (now > deadline) {
                stop("CAR PREVIEW REBUILD: save did not finish; stopped.\r\n");
            }
            break;
        }
        case SETTLE:
            if (capture_idle()) {
                phase = NEXT_CAR;
                deadline = now + PHASE_WAIT_MS;
            } else if (now > deadline) {
                stop("CAR PREVIEW REBUILD: capture did not settle; stopped.\r\n");
            }
            break;
    }
}

int car_preview_rebuild_running(void) {
    return phase != IDLE || start_requested;
}

void car_preview_rebuild_start(void) {
    start_requested = 1;
}

static void tick(void) {
    void *controller;
    unsigned short buttons;
    int pressed = binding_key_down(BIND_REBUILD_CAR_PREVIEWS) ||
                  (binding_xbox(BIND_REBUILD_CAR_PREVIEWS) && autodrive_game_window_focused() &&
                   autodrive_game_buttons(&controller, &buttons) == 0 &&
                   binding_pad_down(BIND_REBUILD_CAR_PREVIEWS, buttons));
    if (pressed && !key_down) {
        start_requested = 1;
    }
    key_down = pressed;
    if (start_requested && phase == IDLE) {
        start_requested = 0;
        void *owner = garage();
        if (owner) {
            read_ids(owner);
        }
        rendered = 0;
        phase = car_count ? NEXT_CAR : IDLE;
        deadline = GetTickCount64() + PHASE_WAIT_MS;
        log_append_value("CAR PREVIEW REBUILD: started; cars with saved thumbnails ",
                         (int)car_count);
    }
    if (phase != IDLE) {
        advance();
    }
}

void car_preview_rebuild_install(unsigned char *game_image) {
    image = game_image;
    void **slot = (void **)(image + UI_VTABLE) + UI_TICK_SLOT;
    stub = VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!stub) {
        return;
    }
    /* push rcx/rdx/r8/r9; sub rsp, 0x68; save xmm0-3; call tick; restore;
       add rsp, 0x68; pop r9/r8/rdx/rcx; jmp [rip] original. */
    static const unsigned char head[] = {
        0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x48, 0x83, 0xec, 0x68, 0xf3, 0x0f, 0x7f, 0x44,
        0x24, 0x20, 0xf3, 0x0f, 0x7f, 0x4c, 0x24, 0x30, 0xf3, 0x0f, 0x7f, 0x54, 0x24, 0x40,
        0xf3, 0x0f, 0x7f, 0x5c, 0x24, 0x50, 0x48, 0xb8};
    static const unsigned char tail[] = {
        0xff, 0xd0, 0xf3, 0x0f, 0x6f, 0x44, 0x24, 0x20, 0xf3, 0x0f, 0x6f, 0x4c, 0x24,
        0x30, 0xf3, 0x0f, 0x6f, 0x54, 0x24, 0x40, 0xf3, 0x0f, 0x6f, 0x5c, 0x24, 0x50,
        0x48, 0x83, 0xc4, 0x68, 0x41, 0x59, 0x41, 0x58, 0x5a, 0x59, 0xff, 0x25, 0, 0, 0, 0};
    unsigned char *p = stub;
    void *callback = (void *)(uintptr_t)tick;
    memory_copy(p, head, sizeof(head));
    p += sizeof(head);
    memory_copy(p, &callback, 8);
    p += 8;
    memory_copy(p, tail, sizeof(tail));
    p += sizeof(tail);
    memory_copy(p, slot, 8);
    FlushInstructionCache(GetCurrentProcess(), stub, 0x1000);
    DWORD previous, ignored;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &previous)) {
        return;
    }
    InterlockedExchangePointer(slot, stub);
    VirtualProtect(slot, 8, previous, &ignored);
    log_append("CAR PREVIEW REBUILD: the RebuildCarPreviews binding (F8) re-renders garage "
               "thumbnails.\r\n");
}
