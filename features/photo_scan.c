#include "photo_scan.h"
#include "autodrive_game.h"
#include "logging.h"
#include "support.h"

/* RB in photo mode: run the photo collection's car detection and credit on
   the current photo camera, as the game does after a photo, without the
   shutter, capture or save dialog.

   Inspected FH2 release:
   - PhotoAnalysis (the page shown after a photo) runs 0xdb5b50: it takes the
     view-0 camera ([[[0x34aba60]+0x50] locked by 0x2dd410]+0x2d0][0] vcall
     +0x38, shared by 0x26ed00), queries the entities with a
     PhotoChallengeComponent (0xcbdcd0) and, for each, calls 0x5dc4d0.
   - 0x5dc4d0 walks every car entity (0x499930 query) with the lambda
     0x5d39c0 {&camera, component, &ordinals}: car ordinal 0xf60670, skips
     ordinal 1215 and MakeID 200, then the CPU visibility test 0x26d2c0
     (frustum, Min/MaxAllowedDistance, CarDetectionSphereRadius and
     occlusion rays, OcculusionSphereDivision) and pushes visible ordinals.
     It then calls 0x5d8a00 (component, &new, &detected), which credits each
     ordinal not yet in the collection (1000 CR per car plus rank bonus,
     reason PhotoChallengeCarCapture), records it, updates stats, raises
     PhotoChallengeCarPhotoed, saves the profile and marks the component for
     its Update, which posts the "Cars photographed" HUD notification.
   No rendered image is used: detection is geometric and ray based.

   This module calls the same lambda and credit function directly, with its
   own walk of the car entity map (the std::map the game's ForEach 0x19070d0
   walks), so it can log every detected car, not only the new ones.

   The trigger is the photo mode page's own RB (UI key 0x14) handler, the
   std::function registered by 0xdb4c40 (vtable 0x2e21b80, _Do_call
   0xde8070 -> 0xdb1640). Page state [page+0x1194]: 0 camera, 1 effects
   panel, 2 hidden UI, 3 pre-save. The game uses RB only in states 1
   (cycle vignette) and 3 (watermark position); in 0 and 2 it is unbound
   (empty RB help text, handler returns false). The scan answers RB in
   states 0 and 2 only and leaves the others to the game. */
#define RB_FUNCTOR_SLOT 0x2e21b90u
#define RB_DO_CALL 0xde8070u
#define RB_HANDLER 0xdb1640u
#define PAGE_STATE 0x1194
#define STATE_CAMERA 0
#define STATE_HIDDEN_UI 2
/* The native result: a real photo (A) goes to PreSave (0xdd0990), which
   pushes the PhotoAnalysis scene through 0xddf250 (page): when
   [page+0x1128] is 0 it creates scene kind 5 (0xde06a0 -> 0xda1820,
   PhotoAnalysis), sets its delay and registers the page's "may close"
   check (0xdd0a70: waits only while a capture is pending). PhotoAnalysis
   runs the detection and credit itself (0xdb5b50 -> 0x5dc4d0) and shows the
   game's own result: processing, each new car with its name, the cars
   photographed count, the payment and the next-bonus progress, or no new
   cars. RB calls 0xddf250 directly in the camera view, without PreSave and
   without the capture request (0x32ac90), so no picture is taken or saved
   and the page stays in its camera state. */
#define SHOW_ANALYSIS 0xddf250u
#define PAGE_OTHER_FLOW 0x1128
#define COLLECTION_HAS 0x2d2530u
#define DETECT_CAR 0x5d39c0u
#define CREDIT_CARS 0x5d8a00u
#define PHOTO_ENTITIES 0xcbdcd0u
#define PHOTO_COMPONENT 0xcbdd30u
#define CAR_ENTITIES 0x499930u
#define REGISTRY_COPY 0x5584e0u
#define LOCK_SCENE 0x2dd410u
#define CAMERA_SHARED 0x26ed00u
#define VIEW_TABLE 0xf9e6e0u
#define GAME_FREE 0x31c7b0u
#define GAME_GLOBALS 0x34aba60u
#define ENTITY_REGISTRY 0x34c0f40u
#define PHOTO_COMPONENT_VTABLE 0x2caace0u
/* Car names: the database the photo component itself queries Data_Car with
   ([[0x34aba60]+0x40], see 0x5d3a59), CDatabase::Query 0x108f720. Results
   are TRefCountedObjectThreadSafe<CRecordSet> (vtable 0x2e7b258), checked by
   their accessors: rows +0x148, integer cell +0x98, string cell +0x78 (the
   cell's own std::string). Text columns hold "_&<id>" keys that 0x123d100
   turns into a CLocString (vtable 0x2eb0e30; text +0x38, release +0x18), as
   the database's own comparer does (0x108d725). */
#define DATABASE_QUERY 0x108f720u
#define RECORDSET_ROWS 0x1091230u
#define RECORDSET_INTEGER 0x1090e80u
#define RECORDSET_STRING 0x10912b0u
#define MAKE_LOC_STRING 0x123d100u
#define LOC_STRING_VTABLE 0x2eb0e30u
#define LOC_STRING_TEXT 0x123d720u
#define LOC_STRING_RELEASE 0x1302470u
/* Help text: the photo mode button-bar update (0xde35c0, run when the page
   is built and when it enters or leaves the Effects panel) sets RB's text
   [page+0x418] to an empty string (call 0x140be20 at 0xde3735) outside the
   Effects panel; other states hide it anyway (FullScreenMode hides the help
   panel, PreSaveMode blanks RB in XAML). That call is routed through an int3
   cave to build the text with 0x140af10 (UI string from a loc string, as for
   every other label there) from a static literal loc string (vtable
   0x2eb0ea0: text +8, freed only when capacity +0x10 is non-zero, references
   +0x14). Language: the text tables' code [[0x3662990]+0x30]. */
#define RB_TEXT_CALL 0xde3735u
#define RB_TEXT_CAVE 0xdf2f92u
#define EMPTY_UI_STRING 0x140be20u
#define UI_STRING_FROM_LOC 0x140af10u
#define LITERAL_VTABLE 0x2eb0ea0u
#define TEXT_TABLES 0x3662990u
#define TEXT_TABLES_VTABLE 0x2eb0dc8u
#define NUM_CARS_CAPTURED 0x150
#define ENTITY_MAP 0xa0
#define MAX_ENTITIES 4096u
#define MAX_LOGGED 64u
#define DEBOUNCE_MS 400u

typedef struct {
    uint32_t rva;
    unsigned size;
    unsigned char bytes[24];
} Expected;

static const Expected expected[] = {
    /* RB _Do_call: mov edx,[rdx]; add rcx,8; jmp 0xdb1640 */
    {RB_DO_CALL, 11, {0x8b, 0x12, 0x48, 0x83, 0xc1, 0x08, 0xe9, 0xc5, 0x95, 0xfc, 0xff}},
    /* RB handler reads the page state [[rcx]+0x1194] */
    {RB_HANDLER, 18, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x09, 0x8b,
                      0x91, 0x94, 0x11, 0x00, 0x00}},
    {DETECT_CAR, 16, {0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48,
                      0x8d, 0xac, 0x24}},
    /* lambda reads CarDetectionSphereRadius [component+0xb0] */
    {0x5d3be2, 8, {0xc5, 0xfa, 0x10, 0x80, 0xb0, 0x00, 0x00, 0x00}},
    {CREDIT_CARS, 16, {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x55, 0x56,
                       0x57, 0x41, 0x54}},
    /* 0x5dc4d0 builds the closure {r14 camera, rbx component, &ordinals} */
    {0x5dc57b, 16, {0x4c, 0x89, 0x75, 0xbf, 0x48, 0x89, 0x5d, 0xc7, 0x48, 0x8d, 0x45, 0xe7, 0x48, 0x89,
                    0x45, 0xcf}},
    /* its invoker: lea rdx,[rsp+0x20]; add rcx,8; call 0x5d39c0 */
    {0x5ee4f3, 14, {0x48, 0x8d, 0x54, 0x24, 0x20, 0x48, 0x83, 0xc1, 0x08, 0xe8, 0xbf, 0x54, 0xfe,
                    0xff}},
    {PHOTO_ENTITIES, 16, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30, 0x8b, 0x05, 0xa8,
                          0x07, 0x80, 0x02}},
    {PHOTO_COMPONENT, 16, {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x83, 0xc1, 0x68, 0x48, 0x8b,
                           0xda, 0xc7, 0x44, 0x24}},
    {CAR_ENTITIES, 16, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30, 0x8b, 0x05, 0x10,
                        0xa1, 0x01, 0x03}},
    {REGISTRY_COPY, 16, {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x33, 0xc0, 0x48, 0x8b, 0xda, 0x48, 0x89,
                         0x02, 0x48, 0x89}},
    {LOCK_SCENE, 16, {0x48, 0x89, 0x54, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0xc7, 0x44,
                      0x24, 0x28, 0xfe}},
    {CAMERA_SHARED, 16, {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x33, 0xc0, 0x48, 0x8b, 0xda, 0x48, 0x89,
                         0x02, 0x48, 0x89}},
    {VIEW_TABLE, 14, {0x48, 0x8b, 0x81, 0xd0, 0x02, 0x00, 0x00, 0x8b, 0xd2, 0x48, 0x8b, 0x04, 0xd0,
                      0xc3}},
    {GAME_FREE, 13, {0x48, 0x85, 0xc9, 0x74, 0x1d, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9}},
    /* world [[0x34aba60]+0x48]+0x98 and the scene owner [[0x34aba60]+0x50] */
    {0x21dfc0, 5, {0x48, 0x8b, 0x41, 0x48, 0xc3}},
    {0x13f0ec0, 5, {0x48, 0x8b, 0x41, 0x50, 0xc3}},
    {0x2e1520, 8, {0x48, 0x8b, 0x81, 0x98, 0x00, 0x00, 0x00, 0xc3}},
    /* the photo lambda's Data_Car query: [[0x34aba60]+0x40] vcall +0x40 */
    {0x5d3a59, 18, {0x48, 0x8b, 0x05, 0x00, 0x80, 0xed, 0x02, 0x48, 0x8b, 0x48, 0x40, 0x48, 0x8b,
                    0x01, 0x4c, 0x8d, 0x45, 0xe0}},
    {MAKE_LOC_STRING, 16, {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x28, 0xfe,
                           0xff, 0xff, 0xff, 0x48}},
    {UI_STRING_FROM_LOC, 16, {0x48, 0x89, 0x4c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7,
                              0x44, 0x24, 0x28, 0xfe}},
    {EMPTY_UI_STRING, 16, {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x28, 0xfe,
                           0xff, 0xff, 0xff, 0x48}},
    /* lea rcx,[rbp-0x21]; call 0x140be20; mov r14,rax; mov rcx,[rbx+0x418] */
    {RB_TEXT_CALL - 4, 18, {0x48, 0x8d, 0x4d, 0xdf, 0xe8, 0xe6, 0x86, 0x62, 0x00, 0x4c, 0x8b, 0xf0,
                            0x48, 0x8b, 0x8b, 0x18, 0x04, 0x00}},
    /* 0xddf250: cmp [rcx+0x1128],0; jne; mov r8d,5 (PhotoAnalysis kind) */
    {SHOW_ANALYSIS, 15, {0x40, 0x57, 0x48, 0x83, 0xec, 0x50, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe, 0xff,
                         0xff, 0xff}},
    {SHOW_ANALYSIS + 0x17, 18, {0x83, 0xb9, 0x28, 0x11, 0x00, 0x00, 0x00, 0x0f, 0x85, 0x9d, 0x00,
                                0x00, 0x00, 0x41, 0xb8, 0x05, 0x00, 0x00}},
    /* 0xde06a0: cmp edi,5; jne; call 0xda1820 (create PhotoAnalysis) */
    {0xde06b8, 10, {0x83, 0xff, 0x05, 0x75, 0x07, 0xe8, 0x5e, 0x11, 0xfc, 0xff}},
    /* the collection's own "already captured" check in 0x5d8a00 */
    {0x5d8a6c, 20, {0x48, 0x8d, 0x81, 0x40, 0x01, 0x00, 0x00, 0x44, 0x8b, 0x3b, 0x44, 0x89, 0x7d, 0x67,
                    0x41, 0x8b, 0xd7, 0x48, 0x8b, 0xc8}},
    {COLLECTION_HAS, 16, {0x48, 0x8b, 0x41, 0x08, 0x4c, 0x8b, 0xc0, 0x48, 0x8b, 0x48, 0x08, 0x80, 0x79,
                          0x19, 0x00, 0x75}},
    /* literal loc string text getter: mov rax,[rcx+8]; ret */
    {0x1d9ca0, 5, {0x48, 0x8b, 0x41, 0x08, 0xc3}},
};

/* Code pointers that must sit in game vtables: {slot rva, target rva}. */
static const uint32_t expected_slots[][2] = {
    {LOC_STRING_VTABLE + 0x38, LOC_STRING_TEXT},
    {LOC_STRING_VTABLE + 0x18, LOC_STRING_RELEASE},
    {LITERAL_VTABLE, 0x1239e50},
    {LITERAL_VTABLE + 0x10, 0x74afa0},
    {LITERAL_VTABLE + 0x18, 0x7446a0},
    {LITERAL_VTABLE + 0x38, 0x1d9ca0},
    {0x2e7af18 + 0x40, DATABASE_QUERY},
    {0x2e7b258 + 0x78, RECORDSET_STRING},
    {0x2e7b258 + 0x98, RECORDSET_INTEGER},
    {0x2e7b258 + 0x148, RECORDSET_ROWS},
};

/* A loc string the game reads like any other but never frees: capacity 0
   keeps its destructor from freeing the text, and the reference count never
   reaches zero. */
typedef struct {
    void *vtable;
    const wchar_t *text;
    uint32_t capacity;
    volatile LONG references;
} LiteralLocString;

static LiteralLocString scan_text_en = {0, L"Scan cars", 0, 0x10000000};
static LiteralLocString scan_text_pl = {0, L"Skanuj auta", 0, 0x10000000};
static int help_text_installed;

typedef struct {
    int32_t *begin, *end, *capacity;
} OrdinalVector;

typedef struct {
    GameShared *camera;
    void *component;
    OrdinalVector *ordinals;
} DetectClosure;

typedef struct {
    union {
        char text[16];
        char *pointer;
    } data;
    size_t size, capacity;
} GameString;

typedef unsigned char (*DoCall)(void *functor, int *argument);
typedef unsigned char (*DetectCar)(DetectClosure *closure, GameShared *entity);
typedef OrdinalVector *(*CreditCars)(void *component, OrdinalVector *fresh,
                                     const OrdinalVector *detected);
typedef GameShared *(*SharedQuery)(void *object, GameShared *out);
typedef void *(*ViewCamera)(void *view);
typedef void (*GameFree)(void *memory);
typedef void (*Query)(void *database, void **result, const char *sql);
typedef int (*RowCount)(void *result);
typedef int (*IntegerAt)(void *result, int row, int column, int wrap);
typedef const GameString *(*StringAt)(void *result, int row, int column, int wrap);
typedef void (*Release)(void *object);

static unsigned char *image;
static ULONGLONG last_scan;
static int faulted;

static void *function(unsigned rva) {
    return image + rva;
}

static int in_text(uintptr_t address) {
    return address >= (uintptr_t)image + 0x1000 && address < (uintptr_t)image + 0x1dcc000;
}

static int has_vtable(const void *object, unsigned rva) {
    void *table;
    return memory_read_pointer(object, &table) && table == image + rva;
}

static void *read_pointer(const void *address) {
    void *value = NULL;
    return memory_read_pointer(address, &value) ? value : NULL;
}

static void free_ordinals(OrdinalVector *vector) {
    if (vector->begin) {
        ((GameFree)function(GAME_FREE))(vector->begin);
    }
    *vector = (OrdinalVector){0};
}

static size_t ordinal_count(const OrdinalVector *vector) {
    return vector->begin && vector->end > vector->begin ? (size_t)(vector->end - vector->begin) : 0;
}

/* ---- log text ---- */

typedef struct {
    char text[256];
    size_t length;
} Line;

static void put(Line *line, const char *text) {
    while (text && *text && line->length + 1 < sizeof(line->text)) {
        line->text[line->length++] = *text++;
    }
    line->text[line->length] = 0;
}

static void put_number(Line *line, int value) {
    char digits[12];
    char *cursor = digits + sizeof(digits) - 1;
    unsigned magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    *cursor = 0;
    do {
        *--cursor = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) {
        *--cursor = '-';
    }
    put(line, cursor);
}

static uintptr_t slot(const void *object, unsigned offset) {
    void *table;
    uintptr_t value = 0;
    if (!memory_read_pointer(object, &table) ||
        !memory_read((unsigned char *)table + offset, &value, sizeof(value))) {
        return 0;
    }
    return value;
}

/* Appends wide text as UTF-8 (the log is UTF-8). */
static void put_wide(Line *line, const wchar_t *text) {
    for (unsigned i = 0; text && i < 96; ++i) {
        wchar_t unit = 0, next = 0;
        if (!memory_read(text + i, &unit, sizeof(unit)) || !unit) {
            break;
        }
        unsigned code = unit;
        if (code >= 0xd800 && code < 0xdc00 && memory_read(text + i + 1, &next, sizeof(next)) &&
            next >= 0xdc00 && next < 0xe000) {
            code = 0x10000 + ((code - 0xd800) << 10) + (next - 0xdc00u);
            ++i;
        }
        char bytes[4];
        unsigned length;
        if (code < 0x80) {
            bytes[0] = (char)code;
            length = 1;
        } else if (code < 0x800) {
            bytes[0] = (char)(0xc0 | code >> 6);
            bytes[1] = (char)(0x80 | (code & 0x3f));
            length = 2;
        } else if (code < 0x10000) {
            bytes[0] = (char)(0xe0 | code >> 12);
            bytes[1] = (char)(0x80 | ((code >> 6) & 0x3f));
            bytes[2] = (char)(0x80 | (code & 0x3f));
            length = 3;
        } else {
            bytes[0] = (char)(0xf0 | code >> 18);
            bytes[1] = (char)(0x80 | ((code >> 12) & 0x3f));
            bytes[2] = (char)(0x80 | ((code >> 6) & 0x3f));
            bytes[3] = (char)(0x80 | (code & 0x3f));
            length = 4;
        }
        if (line->length + length + 1 >= sizeof(line->text)) {
            break;
        }
        for (unsigned b = 0; b < length; ++b) {
            line->text[line->length++] = bytes[b];
        }
        line->text[line->length] = 0;
    }
}

/* Appends a database text cell, localized when it is a "_&<id>" key. */
static int put_text_cell(Line *line, void *result, int column) {
    const GameString *cell = ((StringAt)((*(void ***)result)[0x78 / 8]))(result, 0, column, 0);
    GameString copy;
    char text[96];
    if (!memory_read(cell, &copy, sizeof(copy)) || !copy.size || copy.size >= sizeof(text) ||
        !memory_read(copy.capacity >= 16 ? copy.data.pointer : cell->data.text, text,
                     copy.size)) {
        return 0;
    }
    text[copy.size] = 0;
    if (copy.size > 2 && text[0] == '_' && text[1] == '&') {
        void *localized = NULL;
        ((void *(*)(void **, const GameString *))function(MAKE_LOC_STRING))(&localized, cell);
        if (!localized) {
            return 0;
        }
        size_t before = line->length;
        if (has_vtable(localized, LOC_STRING_VTABLE)) {
            put_wide(line, ((const wchar_t *(*)(void *))function(LOC_STRING_TEXT))(localized));
        }
        ((Release)function(LOC_STRING_RELEASE))(localized);
        return line->length > before;
    }
    put(line, text);
    return 1;
}

/* "<Year> <Make> <Model>", localized, as the photo collection list shows
   them (Year, List_CarMake.DisplayName, Data_Car.DisplayName). */
static void put_car_name(Line *line, int ordinal) {
    unsigned char *globals = read_pointer(image + GAME_GLOBALS);
    void *database = globals ? read_pointer(globals + 0x40) : NULL;
    if (!database || slot(database, 0x40) != (uintptr_t)function(DATABASE_QUERY)) {
        return;
    }
    Line sql;
    sql.length = 0;
    sql.text[0] = 0;
    put(&sql, "SELECT Data_Car.Year, List_CarMake.DisplayName, Data_Car.DisplayName FROM "
              "Data_Car LEFT JOIN List_CarMake ON List_CarMake.ID=Data_Car.MakeID WHERE "
              "Data_Car.Id=");
    put_number(&sql, ordinal);
    void *result = NULL;
    ((Query)function(DATABASE_QUERY))(database, &result, sql.text);
    if (!result) {
        return;
    }
    if (slot(result, 0x148) == (uintptr_t)function(RECORDSET_ROWS) &&
        slot(result, 0x98) == (uintptr_t)function(RECORDSET_INTEGER) &&
        slot(result, 0x78) == (uintptr_t)function(RECORDSET_STRING) &&
        ((RowCount)((*(void ***)result)[0x148 / 8]))(result) > 0) {
        int year = ((IntegerAt)((*(void ***)result)[0x98 / 8]))(result, 0, 0, 0);
        if (year > 0) {
            put_number(line, year);
            put(line, " ");
        }
        if (put_text_cell(line, result, 1)) {
            put(line, " ");
        }
        if (put_text_cell(line, result, 2)) {
            put(line, " ");
        }
    }
    uintptr_t release = slot(result, 0x18);
    if (in_text(release)) {
        ((Release)release)(result);
    }
}

/* ---- entity maps ---- */

/* Calls visit for each value of the entity std::map at collection+0xa0, in
   the order the game's ForEach (0x19070d0) visits them. Stops when visit
   returns 0. */
typedef int (*Visit)(void *context, unsigned char *node);

static void walk_entities(void *collection, Visit visit, void *context) {
    unsigned char *head = read_pointer((unsigned char *)collection + ENTITY_MAP);
    unsigned char *node = head ? read_pointer(head) : NULL;
    unsigned char nil = 1;
    for (unsigned count = 0; node && node != head && count < MAX_ENTITIES; ++count) {
        if (!memory_read(node + 0x19, &nil, 1) || nil || !visit(context, node)) {
            return;
        }
        unsigned char *right = read_pointer(node + 0x10);
        if (right && memory_read(right + 0x19, &nil, 1) && !nil) {
            node = right;
            for (unsigned char *left; (left = read_pointer(node)) &&
                                      memory_read(left + 0x19, &nil, 1) && !nil;) {
                node = left;
            }
        } else {
            unsigned char *parent = read_pointer(node + 8);
            while (parent && memory_read(parent + 0x19, &nil, 1) && !nil &&
                   node == read_pointer(parent + 0x10)) {
                node = parent;
                parent = read_pointer(node + 8);
            }
            node = parent;
        }
    }
}

static int find_component(void *context, unsigned char *node) {
    GameShared *component = context;
    void *entity = read_pointer(node + 0x30);
    if (!entity) {
        return 1;
    }
    GameShared found = {0};
    ((SharedQuery)function(PHOTO_COMPONENT))(entity, &found);
    if (found.object && has_vtable(found.object, PHOTO_COMPONENT_VTABLE)) {
        *component = found;
        return 0;
    }
    autodrive_game_release_shared(&found);
    return 1;
}

static int detect_car(void *context, unsigned char *node) {
    GameShared entity = {0};
    if (!memory_read(node + 0x30, &entity, sizeof(entity)) || !entity.object) {
        return 1;
    }
    // The lambda releases the entity reference it is given, as the game's
    // invoker hands it one moved out of ForEach's copy.
    if (entity.counter) {
        InterlockedIncrement((volatile LONG *)((unsigned char *)entity.counter + 8));
    }
    ((DetectCar)function(DETECT_CAR))(context, &entity);
    return 1;
}

/* The view-0 camera, as PhotoAnalysis takes it (0xdb5b50). */
static int photo_camera(GameShared *camera) {
    *camera = (GameShared){0};
    unsigned char *globals = read_pointer(image + GAME_GLOBALS);
    void *owner = globals ? read_pointer(globals + 0x50) : NULL;
    if (!owner) {
        return 0;
    }
    GameShared scene = {0};
    ((SharedQuery)function(LOCK_SCENE))(owner, &scene);
    unsigned char *views = scene.object ? read_pointer((unsigned char *)scene.object + 0x2d0) : NULL;
    void *view = views ? read_pointer(views) : NULL;
    void *table = view ? read_pointer(view) : NULL;
    uintptr_t getter = 0;
    void *holder = NULL;
    if (table && memory_read((unsigned char *)table + 0x38, &getter, sizeof(getter)) &&
        in_text(getter)) {
        holder = ((ViewCamera)getter)(view);
    }
    autodrive_game_release_shared(&scene);
    if (holder) {
        ((SharedQuery)function(CAMERA_SHARED))(holder, camera);
    }
    return camera->object != NULL;
}

/* fresh null: before the analysis credits them; new = not in the
   collection's set (component+0x140, as 0x5d8a00 checks). */
static void log_detected(const OrdinalVector *detected, const OrdinalVector *fresh,
                         void *component, unsigned *new_count) {
    size_t total = ordinal_count(detected), added = fresh ? ordinal_count(fresh) : 0;
    for (size_t i = 0; i < total; ++i) {
        int ordinal = detected->begin[i], is_new = 0;
        if (fresh) {
            for (size_t j = 0; j < added; ++j) {
                is_new |= fresh->begin[j] == ordinal;
            }
        } else {
            is_new = !((unsigned char (*)(void *, int))function(COLLECTION_HAS))(
                (unsigned char *)component + 0x140, ordinal);
        }
        *new_count += (unsigned)is_new;
        if (i >= MAX_LOGGED) {
            continue;
        }
        Line line;
        line.length = 0;
        line.text[0] = 0;
        put(&line, "PHOTO: detected ");
        put_car_name(&line, ordinal);
        put(&line, "(car ");
        put_number(&line, ordinal);
        put(&line, !is_new ? ") - already in collection\r\n"
                   : fresh  ? ") - new, credited\r\n"
                            : ") - new\r\n");
        log_append(line.text);
    }
}

/* credit 0: detection and log only (the analysis scene credits). */
static void scan(int credit) {
    unsigned char *globals = read_pointer(image + GAME_GLOBALS);
    unsigned char *world = globals ? read_pointer(globals + 0x48) : NULL;
    if (!world || !read_pointer(world + 0x98)) {
        log_append("PHOTO: RB scan skipped; no open world.\r\n");
        return;
    }
    void *registry_holder = read_pointer(image + ENTITY_REGISTRY);
    GameShared registry = {0}, photo_entities = {0}, component = {0}, cars = {0}, camera = {0};
    if (registry_holder) {
        ((SharedQuery)function(REGISTRY_COPY))(registry_holder, &registry);
    }
    if (registry.object) {
        ((SharedQuery)function(PHOTO_ENTITIES))(registry.object, &photo_entities);
    }
    if (photo_entities.object) {
        walk_entities(photo_entities.object, find_component, &component);
    }
    if (!component.object) {
        log_append("PHOTO: RB scan skipped; photo collection is not loaded.\r\n");
    } else if (!photo_camera(&camera)) {
        log_append("PHOTO: RB scan skipped; no photo camera.\r\n");
    } else {
        ((SharedQuery)function(CAR_ENTITIES))(registry.object, &cars);
        OrdinalVector detected = {0}, fresh = {0};
        DetectClosure closure = {&camera, component.object, &detected};
        if (cars.object) {
            walk_entities(cars.object, detect_car, &closure);
        }
        unsigned fresh_count = 0;
        if (credit) {
            ((CreditCars)function(CREDIT_CARS))(component.object, &fresh, &detected);
        }
        log_detected(&detected, credit ? &fresh : NULL, component.object, &fresh_count);
        unsigned captured = 0;
        memory_read((unsigned char *)component.object + NUM_CARS_CAPTURED, &captured,
                    sizeof(captured));
        Line line;
        line.length = 0;
        line.text[0] = 0;
        put(&line, "PHOTO: RB scan: ");
        put_number(&line, (int)ordinal_count(&detected));
        put(&line, " car(s) in view, ");
        put_number(&line, (int)fresh_count);
        put(&line, credit ? " newly photographed; collection " : " new; collection before ");
        put_number(&line, (int)captured);
        put(&line, ".\r\n");
        log_append(line.text);
        free_ordinals(&fresh);
        free_ordinals(&detected);
    }
    autodrive_game_release_shared(&camera);
    autodrive_game_release_shared(&cars);
    autodrive_game_release_shared(&component);
    autodrive_game_release_shared(&photo_entities);
    autodrive_game_release_shared(&registry);
}

static int scan_fault(EXCEPTION_POINTERS *exception) {
    return exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

static void guarded_scan(int credit) {
    __try {
        scan(credit);
    } __except (scan_fault(GetExceptionInformation())) {
        faulted = 1;
        log_append("PHOTO: RB scan faulted; disabled until the game restarts.\r\n");
    }
}

static void show_analysis(unsigned char *page) {
    __try {
        ((void (*)(void *))function(SHOW_ANALYSIS))(page);
        log_append("PHOTO: RB opened the game's photo analysis (native result and credit).\r\n");
    } __except (scan_fault(GetExceptionInformation())) {
        faulted = 1;
        log_append("PHOTO: opening the photo analysis faulted; disabled until the game restarts.\r\n");
    }
}

/* Replaces the RB functor's _Do_call (rcx functor, rdx argument). The page
   is the functor's capture at +8. */
static unsigned char rb_pressed(void *functor, int *argument) {
    unsigned char *page = read_pointer((unsigned char *)functor + 8);
    int state = -1;
    if (!faulted && page && memory_read(page + PAGE_STATE, &state, sizeof(state)) &&
        (state == STATE_CAMERA || state == STATE_HIDDEN_UI)) {
        ULONGLONG now = GetTickCount64();
        if (now - last_scan >= DEBOUNCE_MS) {
            last_scan = now;
            int other_flow = 1;
            memory_read(page + PAGE_OTHER_FLOW, &other_flow, sizeof(other_flow));
            if (other_flow) {
                // Not the free-roam photo flow: credit silently.
                guarded_scan(1);
            } else {
                guarded_scan(0);
                show_analysis(page);
            }
        }
        return 1;
    }
    return ((DoCall)function(RB_DO_CALL))(functor, argument);
}

/* Replaces 0x140be20 (empty UI string) at the one call that blanks RB's
   help text outside the Effects panel. */
/* Azimuth's text language (data\azimuth.ini [launcher] language, e.g.
   "pl-PL"), read once at install: 1 Polish, 0 other, -1 unknown. */
static int launcher_polish = -1;

static int is_polish_code(const char *code, size_t length) {
    return length >= 2 && (code[0] == 'p' || code[0] == 'P') && (code[1] == 'l' || code[1] == 'L') &&
           (length == 2 || code[2] == '-' || code[2] == '_');
}

static void read_launcher_language(void) {
    HMODULE module = NULL;
    wchar_t path[MAX_PATH + 32], value[16];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(void *)read_launcher_language, &module)) {
        return;
    }
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        return;
    }
    while (length && path[length - 1] != L'\\' && path[length - 1] != L'/') {
        --length;
    }
    static const wchar_t name[] = L"data\\azimuth.ini";
    memory_copy(path + length, name, sizeof(name));
    DWORD count = GetPrivateProfileStringW(L"launcher", L"language", L"", value,
                                           ARRAY_COUNT(value), path);
    if (count) {
        char code[16];
        for (DWORD i = 0; i <= count && i < ARRAY_COUNT(code); ++i) {
            code[i] = value[i] < 0x80 ? (char)value[i] : '?';
        }
        launcher_polish = is_polish_code(code, count);
    }
}

/* The game's text language when the label is built: the text tables' code
   (small std::string at [[0x3662990]+0x30], size +0x40, capacity +0x48),
   "PL" or any case / "pl-PL". Falls back to the Azimuth setting when the
   tables are not readable or hold no code. */
static int text_is_polish(void) {
    unsigned char *tables = read_pointer(image + TEXT_TABLES);
    char code[16] = {0};
    size_t length = 0, capacity = 0;
    if (tables && has_vtable(tables, TEXT_TABLES_VTABLE) &&
        memory_read(tables + 0x40, &length, sizeof(length)) &&
        memory_read(tables + 0x48, &capacity, sizeof(capacity)) && length >= 2 &&
        length < sizeof(code)) {
        const void *text = tables + 0x30;
        if (capacity >= 16) {
            text = read_pointer(tables + 0x30);
        }
        if (text && memory_read(text, code, length) && is_polish_code(code, length)) {
            return 1;
        }
    }
    return launcher_polish == 1;
}

static void *rb_help_text(void *out) {
    LiteralLocString *text = text_is_polish() ? &scan_text_pl : &scan_text_en;
    return ((void *(*)(void *, LiteralLocString **))function(UI_STRING_FROM_LOC))(out, &text);
}

static int write_code(unsigned rva, const void *data, size_t size) {
    DWORD previous, ignored;
    if (!VirtualProtect(image + rva, size, PAGE_EXECUTE_READWRITE, &previous)) {
        return 0;
    }
    memory_copy(image + rva, data, size);
    VirtualProtect(image + rva, size, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), image + rva, size);
    return 1;
}

static void install_help_text(void) {
    unsigned char cave[14];
    if (!memory_read(image + RB_TEXT_CAVE - 1, cave, sizeof(cave))) {
        return;
    }
    for (unsigned i = 0; i < sizeof(cave); ++i) {
        if (cave[i] != 0xcc) {
            log_append("PHOTO: RB help text cave is in use; help text not added.\r\n");
            return;
        }
    }
    scan_text_en.vtable = scan_text_pl.vtable = image + LITERAL_VTABLE;
    read_launcher_language();
    unsigned char thunk[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t target = (uintptr_t)rb_help_text;
    memory_copy(thunk + 2, &target, sizeof(target));
    int32_t displacement = (int32_t)(RB_TEXT_CAVE - (RB_TEXT_CALL + 5));
    unsigned char call[5] = {0xe8, 0, 0, 0, 0};
    memory_copy(call + 1, &displacement, 4);
    if (!write_code(RB_TEXT_CAVE, thunk, sizeof(thunk)) ||
        !write_code(RB_TEXT_CALL, call, sizeof(call))) {
        log_append("PHOTO: RB help text could not be written.\r\n");
        return;
    }
    help_text_installed = 1;
    log_append("PHOTO: RB help text added (\"Scan cars\" / \"Skanuj auta\").\r\n");
}

void photo_scan_install(unsigned char *game_image) {
    image = game_image;
    for (unsigned i = 0; i < ARRAY_COUNT(expected); ++i) {
        unsigned char actual[24];
        if (!memory_read(image + expected[i].rva, actual, expected[i].size) ||
            !memory_equal(actual, expected[i].bytes, expected[i].size)) {
            log_append_value("PHOTO: game code did not match; RB car scan not installed. RVA ",
                             (int)expected[i].rva);
            return;
        }
    }
    for (unsigned i = 0; i < ARRAY_COUNT(expected_slots); ++i) {
        uintptr_t value = 0;
        if (!memory_read(image + expected_slots[i][0], &value, sizeof(value)) ||
            value != (uintptr_t)(image + expected_slots[i][1])) {
            log_append_value("PHOTO: game tables did not match; RB car scan not installed. RVA ",
                             (int)expected_slots[i][0]);
            return;
        }
    }
    uintptr_t slot = 0, replacement = (uintptr_t)rb_pressed;
    if (!memory_read(image + RB_FUNCTOR_SLOT, &slot, sizeof(slot)) ||
        slot != (uintptr_t)(image + RB_DO_CALL)) {
        log_append("PHOTO: RB handler slot not as expected; RB car scan not installed.\r\n");
        return;
    }
    DWORD previous, ignored;
    if (!VirtualProtect(image + RB_FUNCTOR_SLOT, sizeof(slot), PAGE_READWRITE, &previous)) {
        log_append("PHOTO: RB handler slot not writable; RB car scan not installed.\r\n");
        return;
    }
    memory_copy(image + RB_FUNCTOR_SLOT, &replacement, sizeof(replacement));
    VirtualProtect(image + RB_FUNCTOR_SLOT, sizeof(slot), previous, &ignored);
    install_help_text();
    log_append("PHOTO: RB car scan installed; RB in photo mode (camera or hidden-UI view) "
               "runs the photo collection detection.\r\n");
}
