#include "fh2/offline_events.h"
#include "fh2/custom_showcases.h"
#include "fh2/autodrive_game.h"
#include "shared/logging.h"
#include "shared/support.h"
#include "fh2/video_settings.h"
#include "fh2/offline_events_flow.h"
#include "fh2/offline_event_division.h"
#include "fh2/offline_event_setup.h"

typedef void (*Action)(void *);
typedef void (*ApplyOptions)(void *, void *);
typedef void *(*CopyOptions)(void *, const void *);
typedef void (*SetByte)(void *, unsigned char);

typedef struct {
    union {
        char text[16];
        const char *pointer;
    } storage;

    size_t length, capacity;
} GameString;

static unsigned char *image;
static int flow_status, last_enabled = -1;
static SRWLOCK options_lock = SRWLOCK_INIT;
static void *selected_options;
static int selected_division, selected_ai_stock_only;
static GameShared selected_owner;
static int in_network_session(void);
static int enabled(void);
static int preparing_event_id, active_event;
static int active_track, active_route;
static int grid_preparing, stock_race;
static int eligible_cars[4096];
static int eligible_pi[4096];
static unsigned eligible_count, next_car;
static int pi_center, pi_lower, pi_upper, pi_spread;

static GameString borrowed_string(const char *text) {
    size_t length = text_length(text);
    GameString string = {
        .storage.pointer = text, .length = length, .capacity = length < 16 ? 16 : length};
    return string;
}

static void release_object(void *object) {
    if (object) {
        ((Action)(*(void ***)object)[3])(object);
    }
}

static char *append_text(char *output, const char *text) {
    size_t length = text_length(text);
    memory_copy(output, text, length);
    output[length] = 0;
    return output + length;
}

static char *append_number(char *output, unsigned value) {
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) {
        *output++ = digits[--count];
    }
    *output = 0;
    return output;
}

static void *query_database(const char *sql) {
    void *manager;
    if (!memory_read_pointer(image + 0x34aba60, &manager) || !manager) {
        return NULL;
    }
    void *database = *(void **)((unsigned char *)manager + 0x40);
    if (!database) {
        return NULL;
    }
    void *rows = NULL;
    GameString query = borrowed_string(sql);
    ((void *(*)(void *, void **, const GameString *))(*(void ***)database)[9])(database, &rows,
                                                                               &query);
    return rows;
}

static int row_integer(void *rows, const char *name) {
    GameString column = borrowed_string(name);
    return ((int (*)(void *, const GameString *))(*(void ***)rows)[0x110 / 8])(rows, &column);
}

static void prepare_pi_balance(unsigned car_class) {
    pi_center = pi_lower = pi_upper = pi_spread = 0;
    void *manager = ((void *(*)(void))(image + 0x327270))();
    void *player = manager ? ((void *(*)(void *))(*(void ***)manager)[0x50 / 8])(manager) : NULL;
    void *choices = player ? ((void *(*)(void *))(*(void ***)player)[0x100 / 8])(player) : NULL;
    if (!choices) {
        return;
    }
    float rating = ((float (*)(void *))(*(void ***)choices)[0x120 / 8])(choices);
    if (!float_is_finite(rating) || rating <= 0 || rating > 1.1f) {
        return;
    }
    int player_pi = (int)(rating * 1000000.0f + 0.5f);
    void *rows =
        query_database("SELECT Id,CAST(ROUND(MaxPerformanceIndex*1000000) AS INTEGER) AS MaxPI,"
                       "MaxDisplayPerformanceIndex AS MaxDisplayPI FROM CarClasses ORDER BY Id");
    if (!rows) {
        return;
    }
    int count = ((int (*)(void *))(*(void ***)rows)[0x148 / 8])(rows);
    int previous = 0, previous_display = 100;
    for (int i = 0; i < count; ++i) {
        int id = row_integer(rows, "Id"), maximum = row_integer(rows, "MaxPI");
        int display = row_integer(rows, "MaxDisplayPI");
        if ((car_class < 8 ? id == (int)car_class : player_pi <= maximum) && maximum > previous &&
            display >= previous_display) {
            pi_lower = previous + 1;
            pi_upper = maximum;
            pi_center = player_pi < pi_lower   ? pi_lower
                        : player_pi > pi_upper ? pi_upper
                                               : player_pi;
            // Work in native normalized PI, using the runtime class table's scale.
            int width = display - previous_display;
            pi_spread = width > 0 ? (maximum - previous) * 15 / width : 0;
            break;
        }
        previous = maximum;
        previous_display = display;
        ((void (*)(void *, unsigned char))(*(void ***)rows)[0x160 / 8])(rows, 1);
    }
    release_object(rows);
}

static int balanced_pi(int stock_pi, unsigned slot) {
    static const int offsets[] = {-15, 6, -6, 12, 0, -9, 9, -12, 15, 3, -3};
    int target = pi_center + pi_spread * offsets[slot % ARRAY_COUNT(offsets)] / 15;
    if (target < pi_lower) {
        target = pi_lower;
    }
    if (target > pi_upper) {
        target = pi_upper;
    }
    // The native upgrade builder cannot downgrade a car below its factory PI.
    return target < stock_pi ? stock_pi : target;
}

static int collect_eligible_cars(unsigned car_class, int division) {
    eligible_count = next_car = 0;
    if (car_class >= 8 && !division && !pi_center && !stock_race) {
        return 1;
    }
    char availability[256], sql[1024];
    char *at = append_text(availability, "IsCarInstalled(Id) AND HasCarMedia(MediaName)");
    if (division > 0) {
        at = append_text(at, " AND Id IN(SELECT CarId FROM Data_Car_Buckets WHERE CarBucket=");
        at = append_number(at, (unsigned)division);
        append_text(at, ")");
    }
    at = append_text(sql, "SELECT Id,CAST(ROUND(PerformanceIndex*1000000) AS INTEGER) AS StockPI "
                          "FROM Drivable_Data_Car WHERE ");
    at = append_text(at, availability);
    if (car_class < 8) {
        if (stock_race) {
            at = append_text(at, " AND ClassID=");
            at = append_number(at, car_class);
        } else if (division > 0 || pi_center) {
            // A small division may have just one model in its highest stock class.
            // Include every installed model under the cap, regardless of ownership.
            at = append_text(at, " AND ClassID<=");
            at = append_number(at, car_class);
        } else {
            at = append_text(at, " AND ClassID=(SELECT MAX(ClassID) FROM Drivable_Data_Car WHERE ");
            at = append_text(at, availability);
            at = append_text(at, " AND ClassID<=");
            at = append_number(at, car_class);
            at = append_text(at, ")");
        }
    }
    append_text(at, " ORDER BY Id");
    void *rows = query_database(sql);
    if (!rows) {
        return 0;
    }
    int count = ((int (*)(void *))(*(void ***)rows)[0x148 / 8])(rows);
    int nearest_id = 0, nearest_pi = 0;
    int ceiling = pi_center + pi_spread;
    if (ceiling > pi_upper) {
        ceiling = pi_upper;
    }
    for (int i = 0; i < count && eligible_count < ARRAY_COUNT(eligible_cars); ++i) {
        int id = row_integer(rows, "Id"), stock_pi = row_integer(rows, "StockPI");
        if (id > 0) {
            if (!nearest_id || stock_pi < nearest_pi) {
                nearest_id = id;
                nearest_pi = stock_pi;
            }
            if (!pi_center || stock_pi <= ceiling) {
                eligible_cars[eligible_count] = id;
                eligible_pi[eligible_count++] = stock_pi;
            }
        }
        ((void (*)(void *, unsigned char))(*(void ***)rows)[0x160 / 8])(rows, 1);
    }
    release_object(rows);
    if (!eligible_count && nearest_id) {
        eligible_cars[0] = nearest_id;
        eligible_pi[0] = nearest_pi;
        eligible_count = 1;
        log_append(
            "EVENTS: no stock model within player PI range; using nearest eligible model.\r\n");
    }
    unsigned random = (unsigned)GetTickCount64();
    for (unsigned i = eligible_count; i > 1; --i) {
        random = random * 1664525u + 1013904223u;
        unsigned j = random % i;
        int id = eligible_cars[i - 1];
        eligible_cars[i - 1] = eligible_cars[j];
        eligible_cars[j] = id;
        int rating = eligible_pi[i - 1];
        eligible_pi[i - 1] = eligible_pi[j];
        eligible_pi[j] = rating;
    }
    return eligible_count != 0;
}

static int matches_division(void *car, int division) {
    int car_id = ((int (*)(void *))(image + 0x1762b40))(car);
    if (car_id <= 0 || division <= 0) {
        return 0;
    }
    char sql[160];
    char *at = append_text(sql, "SELECT CarId FROM Data_Car_Buckets WHERE CarId=");
    at = append_number(at, (unsigned)car_id);
    at = append_text(at, " AND CarBucket=");
    append_number(at, (unsigned)division);
    void *rows = query_database(sql);
    if (!rows) {
        return 0;
    }
    int count = ((int (*)(void *))(*(void ***)rows)[0x148 / 8])(rows);
    release_object(rows);
    return count > 0;
}

static void build_event_menu(void *page) {
    ((Action)(image + 0xd78e50))(page);
    int offline = enabled() && !in_network_session();
    offline_event_division_add(page, offline);
    offline_event_setup_add(page, offline);
}

static void assign_ai_car(int car_id, void *settings) {
    int target_pi = 0;
    if (grid_preparing && eligible_count) {
        unsigned slot = next_car++, index = slot % eligible_count;
        car_id = eligible_cars[index];
        ((void (*)(void *, int))(*(void ***)settings)[0x1b8 / 8])(settings, car_id);
        ((Action)(*(void ***)settings)[0x1d8 / 8])(settings);
        target_pi = pi_center ? balanced_pi(eligible_pi[index], slot) : 0;
    }
    if (grid_preparing) {
        // Native ForzaPlayerChoices target-PI fields, also used by EventCars.
        *((unsigned char *)settings + 0x26c) = target_pi != 0;
        *(float *)((unsigned char *)settings + 0x270) = (float)target_pi / 1000000.0f;
    }
    ((void (*)(int, void *))(image + 0x994040))(car_id, settings);
}

static void *build_event_options(void *event, void *options) {
    preparing_event_id = *(int *)((unsigned char *)event + 0xb0);
    return ((void *(*)(void *, void *))(image + 0x7c8510))(event, options);
}

static int set_race_metadata(void *options) {
    void *cache, *career;
    if (!memory_read_pointer(image + 0x34d5df8, &cache) || !cache ||
        !memory_read_pointer(image + 0x34b6640, &career) || !career) {
        return 0;
    }
    void *event = ((void *(*)(void *, int))(image + 0x3aa8b0))(cache, preparing_event_id);
    if (!event) {
        return 0;
    }
    if (*(int *)((unsigned char *)event + 0x20) != 1) {
        ((SetByte)(image + 0x17d9de0))(options, 0);
    }
    void *current = (unsigned char *)career + 0x20;
    if (((unsigned char (*)(void *))(image + 0x3ac4a0))(current)) {
        return 0;
    }
    ((CopyOptions)(image + 0x3a2660))(current, event);
    active_track = *(int *)((unsigned char *)event + 8);
    active_route = *(int *)((unsigned char *)event + 12);
    active_event = 1;
    return 1;
}

static unsigned char has_offline_rewards(void *info) {
    if (((unsigned char (*)(void *))(image + 0x3ac4a0))(info)) {
        return 1;
    }
    return active_event && !in_network_session() &&
           *(int *)((unsigned char *)info + 8) == active_track &&
           *(int *)((unsigned char *)info + 12) == active_route;
}

static int is_standalone_race(void) {
    void *career;
    if (!active_event || !memory_read_pointer(image + 0x34b6640, &career) || !career) {
        return 0;
    }
    void *info = (unsigned char *)career + 0x20;
    return !((unsigned char (*)(void *))(image + 0x3ac4a0))(info) && has_offline_rewards(info);
}

static void start_finish_countdown(void *manager, float seconds) {
    // The native finish handler ignores bucket flags when there is only one bucket.
    // Keep its disabled timer sentinel for standalone offline races.
    if (!is_standalone_race()) {
        ((void (*)(void *, float))(image + 0xfc8700))(manager, seconds);
    } else {
        log_append("EVENTS: standalone race finish countdown suppressed.\r\n");
    }
}

static void update_finish_countdown(void *component, float delta) {
    // This component can also start a fallback timer and display its own warning.
    if (!is_standalone_race()) {
        ((void (*)(void *, float))(image + 0x783850))(component, delta);
    }
}

static void populate_xp_breakdown(void *model) {
    ((Action)(image + 0xe58cd0))(model);
    unsigned char *data = model;
    if (is_standalone_race() && *(void **)(data + 0x80) == *(void **)(data + 0x88)) {
        ((Action)(image + 0xe673d0))(model);
        ((Action)(image + 0xe67640))(model);
        ((Action)(image + 0xe65e40))(model);
    }
}

static unsigned char use_single_race_presentation(void *manager, unsigned char mode) {
    return ((unsigned char (*)(void *, unsigned char))(image + 0xa2c920))(manager, mode) ||
           is_standalone_race();
}

// Keep the native per-race columns, then restore the offline action labels.
static unsigned char results_visual(void *page,const char *root,const char *state,unsigned char animate){
    typedef unsigned char (*Visual)(void *,const char *,const char *,unsigned char);
    Visual native=(Visual)(image+0x1410a70);
    unsigned char result=native(page,root,state,animate);
    native(page,root,is_standalone_race()?"EO_OfflineResultsHelp":"EO_NativeResultsHelp",0);
    return result;
}

size_t offline_results_xaml(const char *text,size_t length,char *out,size_t capacity){
    if(!text_find(text,length,"x:Class=\"ForzaUI.A_PostRaceEventResults\"") ||
       text_find(text,length,"EO_OfflineResultsHelp"))return 0;
    const char *end=text_find_unique(text,length,"</VisualStateManager.VisualStateGroups>");
    if(!end)return 0;
    static const char extra[]=
      "<VisualStateGroup x:Name=\"EO_OfflineResults\"><VisualState x:Name=\"EO_NativeResultsHelp\"/><VisualState x:Name=\"EO_OfflineResultsHelp\">"
      "<Storyboard>"
      "<ObjectAnimationUsingKeyFrames Duration=\"0:0:0\" Storyboard.TargetName=\"DefaultHelpPanel\" Storyboard.TargetProperty=\"(local:ControllerButtonPanel.ADescription)\"><DiscreteObjectKeyFrame KeyTime=\"0:0:0\" Value=\"{local:Loc HelpButtons.IDS_Continue}\"/></ObjectAnimationUsingKeyFrames>"
      "<ObjectAnimationUsingKeyFrames Duration=\"0:0:0\" Storyboard.TargetName=\"DefaultHelpPanel\" Storyboard.TargetProperty=\"(local:ControllerButtonPanel.XDescription)\"><DiscreteObjectKeyFrame KeyTime=\"0:0:0\" Value=\"{local:Loc HelpButtons.IDS_Restart}\"/></ObjectAnimationUsingKeyFrames>"
      "<ObjectAnimationUsingKeyFrames Duration=\"0:0:0\" Storyboard.TargetName=\"DefaultHelpPanel\" Storyboard.TargetProperty=\"(local:ControllerButtonPanel.YDescription)\"><DiscreteObjectKeyFrame KeyTime=\"0:0:0\" Value=\"{local:Loc HelpButtons.IDS_WatchReplay}\"/></ObjectAnimationUsingKeyFrames>"
      "</Storyboard></VisualState></VisualStateGroup>";
    size_t at=(size_t)(end-text),n=length+sizeof(extra)-1;
    if(n>=capacity)return 0;
    memory_copy(out,text,at);memory_copy(out+at,extra,sizeof(extra)-1);
    memory_copy(out+at+sizeof(extra)-1,text+at,length-at);out[n]=0;return n;
}

static void *localize_solo_mode(void *output, const char *table, unsigned key) {
    if (is_standalone_race()) {
        table = "PauseMenu";
        key = 32820;
    }
    return ((void *(*)(void *, const char *, unsigned))(image + 0x123a570))(output, table, key);
}

static void initialise_prerace_menu(void *page) {
    ((Action)(image + 0xcf8d80))(page);
    if (!is_standalone_race()) {
        return;
    }
    void *localized = NULL, *title = NULL;
    ((void *(*)(void *, int))(image + 0xb4e040))(&localized, active_track);
    if (localized) {
        // This native adapter consumes the incoming reference by value.
        ((void *(*)(void *, void *))(image + 0xb27dd0))(&title, &localized);
        if (title) {
            ((ApplyOptions)(image + 0xe6ecd0))(page, title);
            if (InterlockedDecrement((volatile LONG *)((unsigned char *)title + 0x10)) == 0) {
                ((Action)(image + 0x257730))(title);
            }
        }
    }
}

static int selected_car_class(int *division) {
    *division = 0;
    void *career;
    if (!enabled() || in_network_session() || !memory_read_pointer(image + 0x34b6640, &career) ||
        !career ||
        ((unsigned char (*)(void *))(image + 0x3ac4a0))((unsigned char *)career + 0x20)) {
        return -1;
    }
    AcquireSRWLockShared(&options_lock);
    int car_class = -1;
    if (selected_options && selected_owner.counter &&
        InterlockedCompareExchange((volatile LONG *)((unsigned char *)selected_owner.counter + 8),
                                   0, 0) > 0) {
        unsigned value = *((unsigned char *)selected_options + 0x4d8);
        if (value < 8) {
            car_class = (int)value;
        }
        *division = selected_division;
    }
    ReleaseSRWLockShared(&options_lock);
    return car_class;
}

typedef void (*BuildCarList)(void *, void *, const char *);

static void build_restricted_car_list(void *list, void *filters, const char *where) {
    BuildCarList original = (BuildCarList)(*(void ***)list)[0x80 / 8];
    int division;
    int car_class = selected_car_class(&division);
    size_t length = where ? text_length(where) : 0;
    if (car_class < 0 && !division) {
        original(list, filters, where);
        return;
    }
    char *restricted =
        length <= 65536 ? ((void *(*)(size_t))(image + 0x3229c0))(length + 160) : NULL;
    if (!restricted) {
        original(list, filters, "0=1");
        return;
    }
    char *at = append_text(restricted, "(");
    at = append_text(at, length ? where : "1=1");
    at = append_text(at, ")");
    if (car_class >= 0) {
        at = append_text(at, " AND Garage.ClassId=");
        at = append_number(at, (unsigned)car_class);
    }
    if (division > 0) {
        at = append_text(at, " AND Data_Car_Buckets.CarBucket=");
        append_number(at, (unsigned)division);
    }
    original(list, filters, restricted);
    ((Action)(image + 0x31c7b0))(restricted);
}

static void build_car_list(void *list, void *filters, const char *where) {
    if (custom_showcases_selecting()) {
        ((BuildCarList)(*(void ***)list)[0x80 / 8])(list, filters, "1=1");
        return;
    }
    build_restricted_car_list(list, filters, where);
}

static void build_buy_car_list(void *list, void *filters, const char *where) {
    build_restricted_car_list(list, filters, where);
}

static void *check_selected_car(unsigned char *result, void *garage, int race_id) {
    int division;
    int car_class = selected_car_class(&division);
    if (car_class < 0 && !division) {
        return ((void *(*)(void *, void *, int))(image + 0x3a6810))(result, garage, race_id);
    }
    void *car = NULL;
    ((void *(*)(void *, void **))(image + 0x2dca30))(garage, &car);
    result[0] =
        car &&
        (car_class < 0 ||
         ((int (*)(void *))(image + 0x2db060))(car) == car_class) &&
        (!division || matches_division(car, division));
    result[1] = 1;
    release_object(car);
    return result;
}

static void save_career_results(void *rewards, void *player) {
    void *info = *(void **)((unsigned char *)rewards + 0x190);
    if (active_event && info && !((unsigned char (*)(void *))(image + 0x3ac4a0))(info) &&
        has_offline_rewards(info)) {
        return;
    }
    ((ApplyOptions)(image + 0x62bf20))(rewards, player);
}

static int enabled(void) {
    return flow_status >= 0 && video_setting_get(SHOW_ALL_EVENTS);
}

static int in_network_session(void) {
    void *manager;
    if (!memory_read_pointer(image + 0x34d6348, &manager) || !manager) {
        return 1;
    }
    return ((unsigned char (*)(void *, unsigned char))(image + 0xa2c920))(manager, 1);
}

static void clear_selected_options(void) {
    if (selected_options) {
        ((Action)(image + 0x97def0))(selected_options);
        ((Action)(image + 0x31c7b0))(selected_options);
        selected_options = NULL;
    }
    if (selected_owner.counter &&
        InterlockedDecrement((volatile LONG *)((unsigned char *)selected_owner.counter + 12)) ==
            0) {
        ((Action)(*(void ***)selected_owner.counter)[1])(selected_owner.counter);
    }
    selected_owner = (GameShared){0};
    selected_division = 0;
    selected_ai_stock_only = 0;
}

static int remember_options(void *owner, void *options, int ai_stock_only) {
    GameShared reference;
    if (!memory_read((unsigned char *)owner + 0x30, &reference, sizeof(reference)) ||
        reference.object != (unsigned char *)owner + 0x28 || !reference.counter) {
        return 0;
    }
    void *copy = ((void *(*)(size_t))(image + 0x3229c0))(0x4940);
    if (!copy) {
        return 0;
    }
    ((CopyOptions)(image + 0x97d270))(copy, options);
    InterlockedIncrement((volatile LONG *)((unsigned char *)reference.counter + 12));
    AcquireSRWLockExclusive(&options_lock);
    clear_selected_options();
    selected_options = copy;
    selected_owner = reference;
    selected_division = offline_event_division_selected();
    selected_ai_stock_only = ai_stock_only;
    ReleaseSRWLockExclusive(&options_lock);
    return 1;
}

static void configure_grid(void *options, OfflineEventSetup setup) {
    ((Action)(image + 0x98f580))(options);
    ((SetByte)(image + 0x17d9e20))(options, (unsigned char)(setup.drivatars + 1));
    ((SetByte)(image + 0x17d9e00))(options, (unsigned char)setup.drivatars);
    // Do not activate cached online profiles when the native preset disabled them.
    if (*((unsigned char *)options + 0x4770) > setup.drivatars) {
        ((SetByte)(image + 0x17d9e10))(options, (unsigned char)setup.drivatars);
    }
    ((SetByte)(image + 0x17d9e30))(options, 1);
    ((SetByte)(image + 0x17d9cc0))(options, 1);
    ((SetByte)(image + 0x17e9b00))(options, 0);
    ((SetByte)(image + 0x17d9de0))(options, (unsigned char)(setup.street && setup.traffic));
    void *overrides = (unsigned char *)options + 0x4218;
    // The native stock override also strips the player's selected garage car.
    // Keep it off; the committed mod flag controls AI eligibility and upgrades.
    ((SetByte)(*(void ***)overrides)[0x28 / 8])(overrides, 0);
    if (setup.grid_order) {
        void *grid = (unsigned char *)options + 0x3bc0;
        ((SetByte)(*(void ***)grid)[0x28 / 8])(grid, setup.grid_order == 1 ? 5 : 1);
        ((SetByte)(*(void ***)grid)[0x38 / 8])(grid, setup.grid_order == 2);
    }
    unsigned buckets = *(unsigned char *)((unsigned char *)options + 0x268);
    for (unsigned i = 0; i < buckets && i < 8; ++i) {
        void *bucket = (unsigned char *)options + 0x360 + i * 0x700;
        ((SetByte)(*(void ***)bucket)[7])(bucket, 0);
        ((SetByte)(*(void ***)bucket)[9])(bucket, i ? 0 : 1);
        ((SetByte)(*(void ***)bucket)[0x58 / 8])(bucket, (unsigned char)(setup.drivatars + 1));
    }
}

static void apply_setup(void *owner, void *options) {
    if (!enabled() || in_network_session()) {
        ((ApplyOptions)(image + 0x615b90))(owner, options);
        return;
    }
    active_event = 0;
    OfflineEventSetup setup = offline_event_setup_selected();
    configure_grid(options, setup);
    if (!remember_options(owner, options, setup.stock_only)) {
        log_append("EVENTS: cannot retain race options; setup was not applied.\r\n");
        return;
    }
    ((ApplyOptions)(image + 0x615ae0))(owner, options);
    log_append_value("EVENTS: offline setup accepted; Drivatars",
                     *(unsigned char *)((unsigned char *)options + 0x248));
}

static void prepare_race(void *owner, void *native_options) {
    if (!enabled() || in_network_session()) {
        ((ApplyOptions)(image + 0x615b90))(owner, native_options);
        return;
    }
    AcquireSRWLockShared(&options_lock);
    int matches = selected_options && selected_owner.object == (unsigned char *)owner + 0x28 &&
                  InterlockedCompareExchange(
                      (volatile LONG *)((unsigned char *)selected_owner.counter + 8), 0, 0) > 0;
    if (matches) {
        ((Action)(image + 0x97def0))(native_options);
        ((CopyOptions)(image + 0x97d270))(native_options, selected_options);
    }
    int ai_stock_only = matches && selected_ai_stock_only;
    ReleaseSRWLockShared(&options_lock);
    if (!matches) {
        log_append("EVENTS: race preparation rejected; no matching offline setup.\r\n");
        return;
    }
    unsigned car_class = *((unsigned char *)native_options + 0x4d8);
    stock_race = ai_stock_only;
    if (stock_race) {
        pi_center = pi_lower = pi_upper = pi_spread = 0;
    } else {
        prepare_pi_balance(car_class);
    }
    unsigned drivatars = *((unsigned char *)native_options + 0x248);
    eligible_count = next_car = 0;
    if ((drivatars && !collect_eligible_cars(car_class, selected_division)) ||
        !set_race_metadata(native_options)) {
        log_append("EVENTS: cannot resolve race metadata or eligible AI cars; race preparation "
                   "stopped.\r\n");
        return;
    }
    log_append_value("EVENTS: selected car class", (int)car_class);
    log_append_value("EVENTS: selected car division", selected_division);
    log_append_value("EVENTS: stock Drivatars only", stock_race);
    log_append_value("EVENTS: traffic", *((unsigned char *)native_options + 0x47d0));
    log_append_value("EVENTS: eligible AI car pool", (int)eligible_count);
    log_append_value("EVENTS: player target PI (normalized millionths)", pi_center);
    grid_preparing = 1;
    ((Action)(image + 0x97f0d0))(native_options);
    grid_preparing = 0;
    ((ApplyOptions)(image + 0x615ae0))(owner, native_options);
    log_append("EVENTS: native offline race options and AI grid ready.\r\n");
}

#define EXTRA_ROUTE_EVENT_BASE 1610612736
#define EXTRA_ROUTE_EVENT_SQL_BASE "1610612736"

/* The original start triggers already exist. Extend only the native event
 * cache query, using the career race's track, laps and mode. Location -1
 * leaves these entries outside the location-based road trip pools.
 * Keep Tracks first: the native SQLite 3.6 planner can drop both rows when it
 * reorders an ordinary join with the paired route/race predicates below. */
static void *load_route_events_query(void *out, const char *sql, size_t length) {
    static const char extra[] =
        " UNION ALL SELECT " EXTRA_ROUTE_EVENT_SQL_BASE "+t.Route, -1, t.Id, t.Length,"
        " t.Route, t.RibbonConfiguration, r.NumLaps, r.RaceModeId, t.DisplayName,"
        " t.ShortDisplayName, t.ShortDisplayNameAllCaps, t.MediaTrack"
        " FROM Tracks t CROSS JOIN AnthemEventRaces r ON r.TrackId=t.Id"
        " WHERE t.MediaTrack=542 AND ((t.Id=908 AND t.Route=2251 AND r.AnthemEventRaceId=200)"
        " OR (t.Id=1024 AND t.Route=2301 AND r.AnthemEventRaceId=1))"
        " AND NOT EXISTS (SELECT 1 FROM AnthemOnlineEvents e JOIN Tracks existing"
        " ON existing.Id=e.TrackId WHERE existing.Route=t.Route AND existing.MediaTrack=t.MediaTrack)"
        " AND NOT EXISTS (SELECT 1 FROM AnthemOnlineEvents e WHERE e.AnthemOnlineEventId IN ("
        EXTRA_ROUTE_EVENT_SQL_BASE "+t.Route-10000," EXTRA_ROUTE_EVENT_SQL_BASE "+t.Route,"
        EXTRA_ROUTE_EVENT_SQL_BASE "+t.Route+10000))";
    void *(*assign)(void *, const char *, size_t) = (void *)(image + 0x22c300);
    char query[1200];
    if (length > sizeof(query) - sizeof(extra)) return assign(out, sql, length);
    memory_copy(query, sql, length);
    memory_copy(query + length, extra, sizeof(extra));
    log_append("EVENTS: requesting Cliffside Sprint and Horizon Heats from the race cache.\r\n");
    return assign(out, query, length + sizeof(extra) - 1);
}

/* Give standalone route activations independent native FeatureToggle gates. The
 * original championship entities retain their own gates, markers and flows. */
typedef struct RouteGate {
    struct RouteGate *next;
    unsigned route;
    int island, playground, championship, supplemental, applied;
    char feature[48];
} RouteGate;
static RouteGate *route_gates;
static SRWLOCK route_gate_lock = SRWLOCK_INIT;
static volatile LONG route_generation;
static LONG applied_generation = -1;
static int show_playgrounds, native_routes[2];
static int last_map = -1, last_campaign = -1, last_network = -1, last_prologue = -1;
static ULONGLONG next_filter_poll;

void offline_events_configure(int playgrounds) {
    show_playgrounds = playgrounds != 0;
    last_enabled = -1;
}

/* The call-site thunk passes the native route descriptor (RDI) as argument 3.
 * The native constructor copies the supplied string into its own registry value. */
static void *route_feature(void *out, const GameString *original, const unsigned char *route) {
    unsigned id = *(const unsigned *)(route + 0x0c);
    int island = *(const unsigned *)(route + 0x14) == 0x225;
    AcquireSRWLockExclusive(&route_gate_lock);
    RouteGate *gate = route_gates;
    while (gate && (gate->route != id || gate->island != island)) gate = gate->next;
    if (!gate) {
        gate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*gate));
        if (gate) {
            gate->route = id;
            gate->island = island;
            append_number(append_text(gate->feature, island ? "EO_IslandRoute_Activation_"
                                                          : "EO_MainlandRoute_Activation_"), id);
            gate->next = route_gates;
            route_gates = gate;
        }
    }
    if (gate) {
        int was_supplemental = gate->supplemental;
        gate->supplemental = !island && (id == 2251 || id == 2301) &&
            *(const unsigned *)(route + 0x38) == EXTRA_ROUTE_EVENT_BASE + id &&
            *(const int *)(route + 0x3c) == -1;
        if (gate->supplemental && !was_supplemental)
            log_append_value("EVENTS: supplemental race activation registered; route", (int)id);
        gate->applied = -1;
    }
    InterlockedIncrement(&route_generation);
    ReleaseSRWLockExclusive(&route_gate_lock);
    GameString name = gate ? borrowed_string(gate->feature) : *original;
    return ((void *(*)(void *, const GameString *))(image + 0x30bda0))(out, &name);
}

static void set_event_feature(GameString *name, unsigned char value) {
    const char *text = name->capacity < 16 ? name->storage.text : name->storage.pointer;
    unsigned group = name->length == sizeof("online_island_route_activations") - 1 &&
                     memory_equal(text, "online_island_route_activations", name->length);
    if (native_routes[group] != (value != 0)) {
        native_routes[group] = value != 0;
        last_enabled = -1;
    }
    ((void (*)(const GameString *, unsigned char))(image + 0x899010))(name, value);
}

static int mark_routes(RouteGate *head, const char *sql, int playground) {
    void *rows = query_database(sql);
    if (!rows) return 0;
    int count = ((int (*)(void *))(*(void ***)rows)[0x148 / 8])(rows);
    for (int i = 0; i < count; ++i) {
        unsigned route = (unsigned)row_integer(rows, "RouteId");
        for (RouteGate *gate = head; gate; gate = gate->next) {
            if (gate->route == route) {
                if (playground) gate->playground = 1;
                else gate->championship = 1;
            }
        }
        ((void (*)(void *, int))(*(void ***)rows)[0x160 / 8])(rows, 1);
    }
    release_object(rows);
    return count >= 0;
}

static int route_visible(const RouteGate *gate, int all, int network, int map, int ready,
                         int prologue_complete) {
    if (gate->supplemental && (!all || network || !prologue_complete)) return 0;
    /* Online and ordinary vanilla operation follow the original feature requests. */
    if (network || !all) return native_routes[gate->island];
    return ready && map == gate->island && !gate->championship &&
           (show_playgrounds || !gate->playground);
}

static void refresh_route_gates(int value, int network, int map, int campaign,
                                int prologue_complete) {
    LONG generation = InterlockedCompareExchange(&route_generation, 0, 0);
    if (last_enabled == value && last_map == map && last_campaign == campaign &&
        last_network == network && last_prologue == prologue_complete &&
        applied_generation == generation) return;
    AcquireSRWLockShared(&route_gate_lock);
    RouteGate *head = route_gates;
    ReleaseSRWLockShared(&route_gate_lock);
    if (!head) return;
    int ready = map == 0 || map == 1;
    if (value && !network) {
        for (RouteGate *gate = head; gate; gate = gate->next)
            gate->playground = gate->championship = 0;
        ready &= mark_routes(head,
            "SELECT DISTINCT t.Route AS RouteId FROM AnthemOnlineEvents e "
            "JOIN Tracks t ON t.id=e.TrackId WHERE e.RaceModeId IN (25,26)", 1);
        if (campaign > 0) {
            char sql[320];
            append_number(append_text(sql,
                "SELECT DISTINCT t.Route AS RouteId FROM AnthemEvents e "
                "JOIN AnthemEventRaces r ON r.AnthemEventId=e.AnthemEventId "
                "JOIN Tracks t ON t.id=r.TrackId WHERE e.CampaignId="), (unsigned)campaign);
            ready &= mark_routes(head, sql, 0);
        }
    }
    for (RouteGate *gate = head; gate; gate = gate->next) {
        int visible = route_visible(gate, value, network, map, ready, prologue_complete);
        if (gate->applied != visible) {
            GameString name = borrowed_string(gate->feature);
            ((void (*)(const GameString *, unsigned char))(image + 0x899010))(&name, visible);
            if (gate->supplemental)
                log_append_value(visible ? "EVENTS: supplemental race enabled; route"
                                         : "EVENTS: supplemental race hidden; route",
                                 (int)gate->route);
            gate->applied = visible;
        }
    }
    last_enabled = ready || !value || network ? value : -1; /* Retry unavailable career/DB state. */
    last_map = map;
    last_campaign = campaign;
    last_network = network;
    last_prologue = prologue_complete;
    applied_generation = generation;
}

/* CPlayerManager returns CPlayer. The player's native profile getter returns
 * CForzaProfile; CareerState lives in that profile's typed save registry. */
static int read_filter_career(int *map, int *campaign) {
    void *service = ((void *(*)(void))(image + 0x327270))();
    void *player = service ?
        ((void *(*)(void *))(*(void ***)service)[0x48 / 8])(service) : NULL;
    void *vtable, *registry;
    if (!player || !memory_read_pointer(player, &vtable) ||
        vtable != image + 0x2c62bf0) return 0;
    void *profile = ((void *(*)(void *))(*(void ***)player)[0xc0 / 8])(player);
    if (!profile || !memory_read_pointer(profile, &vtable) ||
        vtable != image + 0x2c5f870 ||
        !memory_read_pointer((unsigned char *)profile + 0xd0, &registry) || !registry ||
        !(image[0x34b67b0] & 1)) return 0;
    GameString key = borrowed_string("CareerState");
    GameShared value = {0};
    ((void *(*)(void *, GameShared *, const GameString *, void *))(image + 0x2de080))(
        registry, &value, &key, image + 0x34b6790);
    unsigned char *state = NULL;
    if (((unsigned char (*)(const GameShared *))(image + 0x18a20a0))(&value))
        state = ((void *(*)(const GameShared *))(image + 0x3a7e40))(&value);
    /* CareerState +0x1c is a progress counter, not the current world. Match
     * the native route-activation logic at 0x89938e: loaded MediaTrack +8. */
    void *world;
    unsigned media_track;
    int result = state && memory_read(state + 0x28, campaign, sizeof(*campaign)) &&
        memory_read_pointer(image + 0x365ad70, &world) && world &&
        memory_read((unsigned char *)world + 8, &media_track, sizeof(media_track)) &&
        (media_track == 0x21e || media_track == 0x225);
    if (result) *map = media_track == 0x225;
    release_object(value.counter);
    return result;
}

void offline_events_tick(void) {
    if (!image) return;
    ULONGLONG now = GetTickCount64();
    if (now < next_filter_poll) return;
    next_filter_poll = now + 250;
    /* Restore the native gates when leaving free roam. Use the event context
       rather than the simulation pause state so the world map keeps its markers. */
    int value = enabled() && autodrive_game_event_context() == 2;
    int network = value && in_network_session(), map = -1, campaign = -1;
    unsigned stage = 0;
    /* After the profile and world are ready, use the native stage reader.
     * It returns stage 4 when a completed save has no iestate record. */
    if (value && !network && read_filter_career(&map, &campaign))
        stage = ((unsigned (*)(void))(image + 0xa65d90))();
    refresh_route_gates(value, network, map, campaign, stage == 4);
}

void offline_events_resource_parsed(int success) {
    flow_status = success && flow_status >= 0 ? 1 : -1;
    last_enabled = -1;
}

PatchDefinition offline_events_hook_patches[42];
static unsigned char originals[ARRAY_COUNT(offline_events_hook_patches)][15];
static unsigned char targets[ARRAY_COUNT(offline_events_hook_patches)][15];
static const unsigned char *accepted[ARRAY_COUNT(offline_events_hook_patches)][2];

void offline_events_prepare_hooks(unsigned char *base) {
    if (image && image != base) {
        AcquireSRWLockExclusive(&options_lock);
        clear_selected_options();
        ReleaseSRWLockExclusive(&options_lock);
    }
    if (image != base) {
        flow_status = 0;
        last_enabled = -1;
        active_event = preparing_event_id = grid_preparing = 0;
        last_map = last_campaign = last_network = last_prologue = -1;
        native_routes[0] = native_routes[1] = 0;
        next_filter_poll = 0;
        for (RouteGate *gate = route_gates; gate; gate = gate->next) gate->applied = -1;
        InterlockedIncrement(&route_generation);
    }
    image = base;
    offline_event_division_initialize(base);
    offline_event_setup_initialize(base);

    static const struct {
        unsigned site, original_call, destination;
        uintptr_t callback;
        const char *name;
    } hooks[] = {
        {0x816454, 0, 0, (uintptr_t)apply_setup, "offline_events_setup_thunk"},
        {0x816514, 0, 0, (uintptr_t)prepare_race, "offline_events_prepare_thunk"},
        {0x816f04, 0, 0, (uintptr_t)set_event_feature, "offline_events_feature_thunk"},
        {0x8161c1, 0x615b90, 0x816454, 0, "offline_events_apply_setup"},
        {0x7d7ee8, 0x615b90, 0x816514, 0, "offline_events_apply_grid"},
        {0x899335, 0x899010, 0x816f04, 0, "offline_events_mainland_points"},
        {0x899378, 0x899010, 0x816f04, 0, "offline_events_island_points"},
        {0x7d83a4, 0, 0, (uintptr_t)build_event_options, "offline_events_metadata_thunk"},
        {0x7d7ec8, 0x7c8510, 0x7d83a4, 0, "offline_events_metadata"},
        {0x626c64, 0, 0, (uintptr_t)has_offline_rewards, "offline_events_rewards_thunk"},
        {0x626748, 0x3ac4a0, 0x626c64, 0, "offline_events_rewards"},
        {0x97fc44, 0, 0, (uintptr_t)assign_ai_car, "offline_events_ai_class_thunk"},
        {0x97f5c2, 0x994040, 0x97fc44, 0, "offline_events_ai_class"},
        {0x7d90d4, 0, 0, (uintptr_t)save_career_results, "offline_events_career_results_thunk"},
        {0x626bf1, 0x62bf20, 0x7d90d4, 0, "offline_events_career_results"},
        {0x817152, 0, 0, (uintptr_t)populate_xp_breakdown, "offline_events_xp_ui_thunk"},
        {0xe58739, 0xe58cd0, 0x817152, 0, "offline_events_xp_ui"},
        {0x817182, 0, 0, (uintptr_t)use_single_race_presentation, "offline_events_title_thunk"},
        {0xe5b6c2, 0xa2c920, 0x817182, 0, "offline_events_prerace_title"},
        {0xe35620, 0x3ac4a0, 0x626c64, 0, "offline_events_results_title"},
        {0xcca922, 0, 0, (uintptr_t)build_car_list, "offline_events_garage_class_thunk"},
        {0xcca8d3, 0, 0xcca922, 0, "offline_events_garage_class"},
        {0xcca732, 0, 0, (uintptr_t)build_buy_car_list, "offline_events_buy_class_thunk"},
        {0xcca7fe, 0, 0xcca732, 0, "offline_events_buy_class"},
        {0x89ac44, 0, 0, (uintptr_t)check_selected_car, "offline_events_eligibility_thunk"},
        {0x89abe4, 0x3a6810, 0x89ac44, 0, "offline_events_eligibility"},
        {0x8171b2, 0, 0, (uintptr_t)initialise_prerace_menu, "offline_events_menu_title_thunk"},
        {0xe5be37, 0xcf8d80, 0x8171b2, 0, "offline_events_menu_title"},
        {0x817244, 0, 0, (uintptr_t)localize_solo_mode, "offline_events_solo_label_thunk"},
        {0xe591c6, 0x123a570, 0x817244, 0, "offline_events_solo_label"},
        {0xe3edd1, 0xa2c920, 0x817182, 0, "offline_events_single_race_results"},
        {0x817484, 0, 0, (uintptr_t)start_finish_countdown, "offline_events_countdown_thunk"},
        {0x2d0db5, 0xfc8700, 0x817484, 0, "offline_events_finish_countdown"},
        {0x817694, 0, 0, (uintptr_t)results_visual, "offline_events_results_help_thunk"},
        {0xe3eee6, 0x1410a70, 0x817694, 0, "offline_events_results_help"},
        {0x8175a4, 0, 0, (uintptr_t)build_event_menu, "offline_events_division_thunk"},
        {0xd8200d, 0xd78e50, 0x8175a4, 0, "offline_events_division_menu"},
        {0x810ce1, 0, 0, (uintptr_t)route_feature, "offline_events_route_gate_thunk"},
        {0x7cacea, 0x30bda0, 0x810ce1, 0, "offline_events_route_gate"},
        {0x9eea74, 0, 0, (uintptr_t)load_route_events_query, "offline_events_extra_routes_thunk"},
        {0x9ee766, 0x22c300, 0x9eea74, 0, "offline_events_extra_routes"}};

    _Static_assert(ARRAY_COUNT(hooks) + 1 == ARRAY_COUNT(offline_events_hook_patches),
                   "Offline event patch count");

    for (unsigned i = 0; i < ARRAY_COUNT(hooks); ++i) {
        unsigned size;
        if (hooks[i].callback) {
            size = hooks[i].site == 0x810ce1 ? 15 : 12;
            for (unsigned j = 0; j < size; ++j) {
                originals[i][j] = 0xcc;
            }
            static const unsigned char code[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
            unsigned prefix = size == 15 ? 3 : 0;
            if (prefix) { /* mov r8,rdi: route descriptor in this native constructor. */
                targets[i][0] = 0x49; targets[i][1] = 0x89; targets[i][2] = 0xf8;
            }
            memory_copy(targets[i] + prefix, code, 12);
            memory_copy(targets[i] + prefix + 2, &hooks[i].callback, 8);
        } else {
            size = hooks[i].original_call ? 5 : 6;
            if (hooks[i].original_call) {
                int32_t original = (int32_t)(hooks[i].original_call - hooks[i].site - 5);
                originals[i][0] = 0xe8;
                memory_copy(originals[i] + 1, &original, 4);
            } else {
                static const unsigned char indirect[] = {0xff, 0x90, 0x80, 0, 0, 0};
                memory_copy(originals[i], indirect, 6);
                targets[i][5] = 0x90;
            }
            int32_t distance = (int32_t)(hooks[i].destination - hooks[i].site - 5);
            targets[i][0] = 0xe8;
            memory_copy(targets[i] + 1, &distance, 4);
        }
        accepted[i][0] = originals[i];
        accepted[i][1] = targets[i];
        offline_events_hook_patches[i] = (PatchDefinition){
            hooks[i].site, 0,          size,       GROUP_FRONTEND_XML, hooks[i].name,
            originals[i],  targets[i], targets[i], accepted[i],        2};
    }
    const unsigned hud = ARRAY_COUNT(hooks);
    uintptr_t original_update = (uintptr_t)(image + 0x783850);
    uintptr_t replacement_update = (uintptr_t)update_finish_countdown;
    memory_copy(originals[hud], &original_update, sizeof(original_update));
    memory_copy(targets[hud], &replacement_update, sizeof(replacement_update));
    accepted[hud][0] = originals[hud];
    accepted[hud][1] = targets[hud];
    offline_events_hook_patches[hud] = (PatchDefinition){0x2cdcdc8,
                                                         0,
                                                         8,
                                                         GROUP_FRONTEND_XML,
                                                         "offline_events_countdown_hud_update",
                                                         originals[hud],
                                                         targets[hud],
                                                         targets[hud],
                                                         accepted[hud],
                                                         2};
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestPollEventFilters(void) {
    next_filter_poll = 0;
    offline_events_tick();
}

__declspec(dllexport) int Fh2TestReadEventCareer(int *map, int *campaign) {
    return read_filter_career(map, campaign);
}
__declspec(dllexport) void Fh2TestEventFilters(int all, int network, int map, int campaign,
                                             int playgrounds) {
    offline_events_configure(playgrounds);
    refresh_route_gates(all, network, map, campaign, 1);
}
__declspec(dllexport) void Fh2TestEventFiltersRepeat(int all, int network, int map, int campaign) {
    refresh_route_gates(all, network, map, campaign, 1);
}
__declspec(dllexport) size_t Fh2TestOfflineResultsXaml(const char *text,size_t n,char *out,size_t cap){return offline_results_xaml(text,n,out,cap);}
__declspec(dllexport) void Fh2TestOfflineEventsReady(int value) {
    offline_events_resource_parsed(value);
}

__declspec(dllexport) void *Fh2TestOfflineEventsHook(unsigned index) {
    const uintptr_t callbacks[] = {
        (uintptr_t)apply_setup,           (uintptr_t)prepare_race,
        (uintptr_t)set_event_feature,     (uintptr_t)has_offline_rewards,
        (uintptr_t)populate_xp_breakdown, (uintptr_t)use_single_race_presentation,
        (uintptr_t)build_car_list,        (uintptr_t)check_selected_car};
    return index < ARRAY_COUNT(callbacks) ? (void *)callbacks[index] : NULL;
}
#endif

int offline_events_transform(void *game_resource) {
    unsigned char *resource = game_resource;
    char *source = *(char **)(resource + 0xe0);
    unsigned length = *(unsigned *)(resource + 0xe8);
    if (!source || !length || length > 8u * 1024u * 1024u) {
        return 0;
    }
    static const char original_entry[] = "<state id=\"online_freeroam_activation_flow\">";
    static const char unavailable[] =
        "<exit id=\"not_in_network_session\" target=\".Unavailable\" />";
    static const char available[] = "<exit id=\"not_in_network_session\" target=\".Available\" />";
    const char *entry = text_find_unique(source, length, original_entry);
    const char *gate = text_find_unique(source, length, unavailable);
    const char *end = text_find_unique(source, length, "</stategroup>");
    if (!entry || !gate || !end || entry >= gate || gate >= end ||
        text_find(source, length, "_fh2_offline_activation")) {
        return 0;
    }
    size_t size =
        length + sizeof(offline_event_dispatch) + sizeof(offline_event_flow) + sizeof(available);
    char *replacement = ((void *(*)(size_t))(image + 0x3229c0))(size);
    if (!replacement) {
        return 0;
    }
    const char *starts[] = {entry, gate, end};
    const size_t removed[] = {sizeof(original_entry) - 1, sizeof(unavailable) - 1, 0};
    const char *inserted[] = {offline_event_dispatch, available, offline_event_flow};
    size_t cursor = 0, written = 0;
    for (unsigned i = 0; i < 3; ++i) {
        size_t at = (size_t)(starts[i] - source), n = at - cursor;
        memory_copy(replacement + written, source + cursor, n);
        written += n;
        n = text_length(inserted[i]);
        memory_copy(replacement + written, inserted[i], n);
        written += n;
        cursor = at + removed[i];
    }
    memory_copy(replacement + written, source + cursor, length - cursor);
    written += length - cursor;
    replacement[written] = 0;
    *(char **)(resource + 0xe0) = replacement;
    *(unsigned *)(resource + 0xe8) = (unsigned)written;
    ((Action)(image + 0x31c7b0))(source);
    return 1;
}
