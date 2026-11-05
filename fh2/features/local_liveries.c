#include "local_liveries.h"
#include "logging.h"
#include "support.h"
#include "autodrive_game.h"

// Read the active profile through the native save-container API. In particular,
// do not walk Azimuth revision folders or change any saved design/garage data.
#define DESIGN_LIMIT 1024
#define NAME_LIMIT 96
typedef struct {
  union {
    wchar_t small[8];
    const wchar_t *pointer;
  };
  size_t length, capacity;
} Wide;
typedef struct {
  union {
    char small[16];
    const char *pointer;
  };
  size_t length, capacity;
} Narrow;
typedef union {
  uint64_t alignment;
  unsigned char bytes[96];
} Livery;
typedef struct {
  wchar_t name[NAME_LIMIT];
  unsigned ordinal;
} DesignName;
typedef struct {
  DesignName file;
  unsigned state;
  unsigned updated_scan;
  Livery handle;
} Design;
typedef struct {
  void *car, *component, *owner;
  unsigned ordinal, choice, seen_scan, eligible;
  Livery original;
} Applied;
typedef struct {
  const void **vtable;
  unsigned generation;
} Callback;

static unsigned char *image;
static int enabled, initialized;
static unsigned active_livery_limit = 6;
static CRITICAL_SECTION index_lock;
static Design designs[DESIGN_LIMIT];
static DesignName pending[DESIGN_LIMIT];
static Applied applied[256];
static unsigned count, pending_count, generation = 1, pending_generation,
                                      skipped_names;
static void *profile_identity;
static void *library_future[2];
static uint64_t next_scan;
static unsigned random_state = 0x4d3a219bu;
static unsigned scan_serial;
static const void *callback_vtable[6];
// Only a native loading-state update may open containers or prepare textures.
// Gameplay can reuse the resident handles, but cannot grow the cache.
static void *loading_state;
static unsigned loading_phase;
static int loading_freeroam, driving_started;
static uint64_t loading_deadline;

static void clear_bytes(void *destination, size_t length) {
  volatile unsigned char *p = destination;
  while (length--)
    *p++ = 0;
}

static void retain(void *object) {
  if (object)
    ((void (*)(void *))(*(void ***)object)[2])(object);
}
static void release(void *object) {
  if (object)
    ((void (*)(void *))(*(void ***)object)[3])(object);
}
static const wchar_t *wide_text(const Wide *text) {
  return text->capacity < 8 ? text->small : text->pointer;
}
static Wide wide_borrow(const wchar_t *name) {
  Wide result = {0};
  while (name[result.length])
    ++result.length;
  if (result.length < 8) {
    result.capacity = 7;
    memory_copy(result.small, name, (result.length + 1) * sizeof(wchar_t));
  } else {
    result.pointer = name;
    result.capacity = result.length;
  }
  return result;
}
static unsigned model_in_name(const wchar_t *name, size_t length) {
  static const wchar_t prefix[] = L"Livery_";
  if (length < 10 || length >= NAME_LIMIT)
    return 0;
  for (unsigned i = 0; i < 7; ++i)
    if (name[i] != prefix[i])
      return 0;
  unsigned ordinal = 0, i = 7;
  for (; i < length && name[i] >= L'0' && name[i] <= L'9'; ++i) {
    if (ordinal > 1000000)
      return 0;
    ordinal = ordinal * 10 + name[i] - L'0';
  }
  if (!ordinal || i == 7 || i + 1 >= length || name[i] != L'_')
    return 0;
  for (++i; i < length; ++i)
    if (name[i] < L'0' || name[i] > L'9')
      return 0;
  return ordinal;
}
static int same_name(const wchar_t *a, const wchar_t *b) {
  for (unsigned i = 0; i < NAME_LIMIT; ++i) {
    if (a[i] != b[i])
      return 0;
    if (!a[i])
      return 1;
  }
  return 0;
}

// MSVC 2012 std::function's native copy/move/invoke/type/delete/destructor ABI.
static void *copy_callback(Callback *self, void *destination) {
  Callback *result = destination;
  if (!result)
    result = ((void *(*)(size_t))(image + 0x3229c0))(sizeof(*result));
  if (result)
    *result = *self;
  return result;
}
static void callback_destroy(Callback *self) { (void)self; }
static void delete_callback(Callback *self, unsigned char free_memory) {
  if (free_memory)
    ((void (*)(void *))(image + 0x31c7b0))(self);
}
static const void *callback_type(Callback *self) {
  (void)self;
  return image + 0x2c603e0;
}
static void collect_names(Callback *self, const void *vector) {
  const Wide *begin = *(const Wide *const *)vector;
  const Wide *end = *((const Wide *const *)vector + 1);
  if ((uintptr_t)end < (uintptr_t)begin ||
      (uintptr_t)end - (uintptr_t)begin > 32u * 65536u ||
      ((uintptr_t)end - (uintptr_t)begin) % sizeof(Wide)) {
    log_append("LOCAL DESIGNS: enumeration returned an unexpected result layout.\r\n");
    return;
  }
  log_append_value("LOCAL DESIGNS: enumeration returned slots=", (int)(end - begin));
  EnterCriticalSection(&index_lock);
  if (self->generation == generation) {
    pending_count = 0;
    skipped_names = 0;
    for (const Wide *p = begin; p != end && pending_count < DESIGN_LIMIT; ++p) {
      const wchar_t *name = wide_text(p);
      unsigned ordinal = model_in_name(name, p->length);
      if (!ordinal) {
        ++skipped_names;
        continue;
      }
      DesignName *entry = &pending[pending_count++];
      memory_copy(entry->name, name, p->length * sizeof(wchar_t));
      entry->name[p->length] = 0;
      entry->ordinal = ordinal;
    }
    pending_generation = self->generation;
  }
  LeaveCriticalSection(&index_lock);
}
static void release_future(void **future) {
  unsigned char *counter = future[1];
  if (counter && !InterlockedDecrement((volatile LONG *)(counter + 8))) {
    ((void (*)(void *))(*(void ***)counter)[0])(counter);
    if (!InterlockedDecrement((volatile LONG *)(counter + 12)))
      ((void (*)(void *))(*(void ***)counter)[1])(counter);
  }
  future[0] = future[1] = NULL;
}
static void cancel_index(void) {
  EnterCriticalSection(&index_lock);
  ++generation;
  pending_generation = pending_count = 0;
  LeaveCriticalSection(&index_lock);
  release_future(library_future);
}
static void index_library(void *profile) {
  void *manager = *(void **)(image + 0x36616b8);
  if (!manager)
    return;
  void **reference =
      ((void **(*)(void *))(*(void ***)profile)[0xa8 / 8])(profile);
  void *context = reference ? *reference : NULL;
  if (!context)
    return;
  if (library_future[1]) {
    log_append("LOCAL DESIGNS: enumeration timed out; retrying.\r\n");
    release_future(library_future);
  }
  EnterCriticalSection(&index_lock);
  ++generation;
  pending_generation = 0;
  LeaveCriticalSection(&index_lock);
  retain(context);
  Wide prefix = wide_borrow(L"Livery_");
  Callback callback = {callback_vtable, generation};
  // Inline function object; its +18 pointer is consumed/reset by the API.
  unsigned char function[32] = {0};
  memory_copy(function, &callback, sizeof(callback));
  *(void **)(function + 24) = function;
  // The queued native job holds only a WEAK reference to the request state.
  // Keep this strong reference exactly as UserContentDesignPopup does at +520.
  // Dropping it here cancels the callback before enumeration can finish.
  ((void *(*)(void *, void *, const Wide *, void **, void *, unsigned char))(
      image + 0x11fbf70))(manager, library_future, &prefix, &context, function, 1);
  log_append("LOCAL DESIGNS: native library enumeration requested.\r\n");
}
static void accept_index(void) {
  int completed = 0;
  EnterCriticalSection(&index_lock);
  if (pending_generation == generation) {
    unsigned added = 0;
    for (unsigned i = 0; i < pending_count && count < DESIGN_LIMIT; ++i) {
      unsigned j = 0;
      while (j < count && !same_name(designs[j].file.name, pending[i].name))
        ++j;
      if (j != count)
        continue;
      memory_copy(&designs[count++].file, &pending[i], sizeof(DesignName));
      ++added;
    }
    log_append_value("LOCAL DESIGNS: indexed designs=", (int)count);
    if (skipped_names)
      log_append_value("LOCAL DESIGNS: skipped nonstandard slot names=",
                       (int)skipped_names);
    if (added)
      log_append_value("LOCAL DESIGNS: newly discovered designs=", (int)added);
    pending_generation = 0;
    completed = 1;
  }
  LeaveCriticalSection(&index_lock);
  if (completed)
    release_future(library_future);
}
static void dispose_handle(Livery *handle) {
  unsigned char *p = handle->bytes;
  release(*(void **)(p + 0x28));
  if (*(size_t *)(p + 0x48) >= 8)
    ((void (*)(void *))(image + 0x31c7b0))(*(void **)(p + 0x30));
  DeleteCriticalSection((CRITICAL_SECTION *)p);
}
static void dispose_record(Applied *record) {
  if (record->owner)
    dispose_handle(&record->original);
  clear_bytes(record, sizeof(*record));
}
static void reset_profile(void *profile) {
  cancel_index();
  for (unsigned i = 0; i < ARRAY_COUNT(applied); ++i)
    dispose_record(&applied[i]);
  for (unsigned i = 0; i < count; ++i) {
    if (designs[i].state == 1)
      dispose_handle(&designs[i].handle);
    clear_bytes(&designs[i], sizeof(Design));
  }
  clear_bytes(applied, sizeof(applied));
  count = 0;
  profile_identity = profile;
}
static int load_design(Design *entry) {
  if (entry->state)
    return entry->state == 1;
  entry->state = 2; // Failed entries aren't reopened on every update.
  Wide name = wide_borrow(entry->file.name);
  void *container = NULL;
  ((unsigned char (*)(const Wide *, void **))(image + 0xe01830))(&name,
                                                                 &container);
  if (!container) {
    log_append_value("LOCAL DESIGNS: missing/unreadable design for model=",
                     (int)entry->file.ordinal);
    return 0;
  }
  char ascii[NAME_LIMIT];
  for (unsigned i = 0; i <= name.length; ++i)
    ascii[i] = (char)entry->file.name[i];
  Narrow narrow = {0};
  narrow.length = name.length;
  if (name.length < 16) {
    narrow.capacity = 15;
    memory_copy(narrow.small, ascii, name.length + 1);
  } else {
    narrow.pointer = ascii;
    narrow.capacity = name.length;
  }
  unsigned char *p = entry->handle.bytes;
  InitializeCriticalSectionAndSpinCount((CRITICAL_SECTION *)p, 0x600);
  *(size_t *)(p + 0x48) = 7;
  retain(container);
  void *argument = container; // Native loader consumes this retained reference.
  ((void (*)(void *, const Narrow *, void **))(image + 0x121e290))(p, &narrow,
                                                                   &argument);
  ((void (*)(void *, const Wide *))(image + 0x1231600))(p, &name);
  release(container);
  if (!*(void **)(p + 0x28)) {
    dispose_handle(&entry->handle);
    return 0;
  }
  entry->state = 1;
  log_append_value("LOCAL DESIGNS: cached native livery for model=",
                   (int)entry->file.ordinal);
  log_append_value("LOCAL DESIGNS: cache index=", (int)(entry - designs));
  log_append("LOCAL DESIGNS: source slot=");
  log_append(ascii);
  log_append("\r\n");
  return 1;
}
static unsigned random_next(void) {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return random_state;
}
static int design_ready(Design *entry) {
  unsigned char *owner = entry->state == 1
      ? *(void **)(entry->handle.bytes + 0x28) : NULL;
  return owner && owner[0x34] == 4 && owner[0x35] == 4 &&
         *(unsigned *)(owner + 0x200) == 14;
}
static unsigned choose_design(unsigned ordinal, int cached_only) {
  unsigned candidates = 0, selected = ~0u;
  for (unsigned i = 0; i < count; ++i) {
    if (designs[i].file.ordinal != ordinal || designs[i].state == 2 ||
        (cached_only && !design_ready(&designs[i])))
      continue;
    if (random_next() % ++candidates == 0)
      selected = i;
  }
  return selected;
}
static void update_design(Design *entry, void *player_car) {
  if (entry->updated_scan == scan_serial)
    return;
  entry->updated_scan = scan_serial;
  unsigned char *owner = *(void **)(entry->handle.bytes + 0x28);
  // Match the native non-player car path (f67dc0): request its livery in
  // display mode 5. This prepares/loads the saved texture streams as well as
  // paint. Calling the low-level CCarLivery update directly instead tries to
  // regenerate vinyls without those inputs and can crash on a textured decal.
  // The native request owns asynchronous preparation and completion; never
  // enter the editor's vector-regeneration state machine here.
  if (!owner || (player_car && owner == *(void **)((unsigned char *)player_car + 0x2e8)) ||
      owner[0x34] != 4 || owner[0x35] != 4)
    return;
  unsigned before = *(unsigned *)(owner + 0x200);
  if (before >= 14)
    return;
  ((void (*)(void *, unsigned))(image + 0x1232810))(&entry->handle, 5);
  unsigned after = *(unsigned *)(owner + 0x200);
  if (after != before) {
    log_append_value("LOCAL DESIGNS: native display request model=",
                     (int)entry->file.ordinal);
    log_append_value("LOCAL DESIGNS: native display state=", (int)after);
  }
}
static Applied *track_car(void *car, void *player_car, int allow) {
  unsigned char *p = car;
  if (!p || car == player_car || !*(void **)(p + 0x4d00) ||
      !*(void **)(p + 0x4d38))
    return NULL;
  unsigned ordinal = *(unsigned *)(p + 0x118);
  void *component = *(void **)(p + 0x4d00);
  Applied *record = NULL, *empty = NULL;
  for (unsigned i = 0; i < ARRAY_COUNT(applied); ++i) {
    Applied *candidate = &applied[i];
    if (candidate->car == car && candidate->component == component &&
        candidate->ordinal == ordinal) {
      record = candidate;
      break;
    }
    if (!candidate->car && !empty)
      empty = candidate;
  }
  if (!record) {
    if (!allow || !empty)
      return NULL;
    record = empty;
    clear_bytes(record, sizeof(*record));
    record->car = car;
    record->component = component;
    record->ordinal = ordinal;
    record->choice = ~0u;
    // One eligibility draw per spawn, never per update: undecorated cars
    // cannot eventually all become decorated through repeated rerolls.
    record->eligible = random_next() & 1;
  }
  record->seen_scan = scan_serial;
  if (record->owner && record->owner != *(void **)(p + 0x2e8)) {
    // Native code replaced this appearance. Release our snapshot and let the
    // new appearance win instead of repeatedly overwriting it.
    dispose_handle(&record->original);
    clear_bytes(&record->original, sizeof(Livery));
    record->owner = NULL;
    record->eligible = 0;
  }
  if (!allow && record->owner) {
    Livery copy;
    ((void *(*)(void *, const void *))(image + 0x215e10))(&copy,
                                                            &record->original);
    ((void (*)(void *, void *))(image + 0xf76f20))(car, &copy);
    dispose_handle(&record->original);
    clear_bytes(&record->original, sizeof(Livery));
    record->owner = NULL;
  }
  return record;
}
static void decorate_car(Applied *record) {
  unsigned char *p = record->car;
  // Wait for the native base appearance before taking the restore snapshot.
  if (!*(void **)(p + 0x2e8))
    return;
  unsigned choice = record->choice;
  if (choice >= count || !design_ready(&designs[choice]))
    return;
  ((void *(*)(void *, const void *))(image + 0x215e10))(&record->original,
                                                                p + 0x2c0);
  Livery copy;
  ((void *(*)(void *, const void *))(image + 0x215e10))(
      &copy, &designs[choice].handle);
  ((void (*)(void *, void *))(image + 0xf76f20))(record->car, &copy);
  record->owner = *(void **)(p + 0x2e8);
  record->choice = choice;
  log_append_value("LOCAL DESIGNS: applied to Drivatar model=",
                   (int)record->ordinal);
  log_append_value("LOCAL DESIGNS: selected design index=", (int)choice);
}

static int scan_population(void *player_car, int restore) {
  ++scan_serial;
  void *service = ((void *(*)(void))(image + 0x327270))();
  void *profile =
      service ? ((void *(*)(void *))(*(void ***)service)[0x48 / 8])(service)
              : NULL;
  if (!profile)
    return 0;
  void *options = ((void *(*)(void *))(*(void ***)profile)[0xc8 / 8])(profile);
  int allow = enabled && active_livery_limit && options &&
      !((unsigned char (*)(void *))(image + 0x2e7d20))(options);
  if (!allow && library_future[1])
    cancel_index();
  void *manager = *(void **)(image + 0x34aba60);
  void *simulation =
      manager ? ((void *(*)(void *, unsigned))(image + 0x21e0c0))(manager, 0)
              : NULL;
  if (!simulation) {
    if (profile != profile_identity)
      reset_profile(profile);
    return allow;
  }
  void ***vector = ((void ***(*)(void *))(image + 0x2db130))(simulation);
  void **begin = vector[0], **end = vector[1];
  if ((uintptr_t)end < (uintptr_t)begin ||
      (uintptr_t)end - (uintptr_t)begin > 4096 * sizeof(void *) ||
      ((uintptr_t)end - (uintptr_t)begin) % sizeof(void *))
    return 0;
  if (profile != profile_identity) {
    // Restore only cars proven live before dropping the old profile's cache.
    for (void **i = begin; i != end; ++i) {
      void *car = *i;
      retain(car);
      track_car(car, player_car, 0);
      release(car);
    }
    reset_profile(profile);
  }
  for (void **i = begin; i != end; ++i) {
    void *car = *i;
    retain(car);
    track_car(car, player_car, allow && !restore);
    release(car);
  }
  // Despawned cars are identified from the complete live list. Their identity
  // pointers are never dereferenced here and never keep the car alive.
  for (unsigned i = 0; i < ARRAY_COUNT(applied); ++i)
    if (applied[i].car && (!allow || restore || applied[i].seen_scan != scan_serial))
      dispose_record(&applied[i]);
  if (!allow && count)
    reset_profile(profile);
  return allow;
}

static void apply_cached(void) {
  unsigned active = 0, candidates = 0;
  Applied *selected = NULL;
  for (unsigned i = 0; i < ARRAY_COUNT(applied); ++i) {
    Applied *record = &applied[i];
    if (record->owner) {
      ++active;
    } else if (record->car && record->eligible) {
      if (choose_design(record->ordinal, 1) < count &&
          random_next() % ++candidates == 0)
        selected = record;
    }
  }
  // Gameplay: handle clones only. No enumeration, container reads or native
  // texture preparation may occur here, including when a new car spawns.
  if (active < active_livery_limit && selected) {
    selected->choice = choose_design(selected->ordinal, 1);
    retain(selected->car);
    decorate_car(selected);
    release(selected->car);
  }
}

void local_liveries_tick(void *player_car) {
  if (!image || !player_car || loading_state)
    return;
  if ((!enabled || !active_livery_limit) && !count)
    return;
  uint64_t now = GetTickCount64();
  if (now < next_scan)
    return;
  next_scan = now + 250;
  if (scan_population(player_car, 0))
    apply_cached();
}

static void begin_loading(void *state, int freeroam) {
  if (!enabled)
    return;
  loading_state = state;
  loading_phase = 0;
  loading_freeroam = freeroam;
  driving_started = 0;
  loading_deadline = 0;
  log_append(freeroam ? "LOCAL DESIGNS: final free-roam loading gate entered.\r\n"
                     : "LOCAL DESIGNS: native race loading gate entered.\r\n");
}

static void add_model(unsigned *models, unsigned *number, uint64_t model) {
  if (!model || model > 1000000 || *number == 256)
    return;
  for (unsigned i = 0; i < *number; ++i)
    if (models[i] == (unsigned)model)
      return;
  models[(*number)++] = (unsigned)model;
}

static void preload_freeroam(void *player_car) {
  unsigned models[256], number = 0;
  void *manager = *(void **)(image + 0x34aba60);
  void *simulation = manager
      ? ((void *(*)(void *, unsigned))(image + 0x21e0c0))(manager, 0) : NULL;
  if (simulation) {
    void ***vector = ((void ***(*)(void *))(image + 0x2db130))(simulation);
    uintptr_t begin = (uintptr_t)vector[0], end = (uintptr_t)vector[1];
    if (end >= begin && end - begin <= 4096 * sizeof(void *) &&
        (end - begin) % sizeof(void *) == 0) {
      for (void **p = vector[0]; p != vector[1]; ++p) {
        unsigned char *car = *p;
        // Loading may have instantiated the model without a presentation
        // interface or base livery yet. Neither is needed to preload a design.
        if (car && car != player_car && *(void **)(car + 0x4d00))
          add_model(models, &number, *(unsigned *)(car + 0x118));
      }
    }
  }
  log_append_value("LOCAL DESIGNS: free-roam instantiated models=", (int)number);
  unsigned instantiated = number;
  // Native DrivatarDataManager contains the upcoming profile/car roster even
  // before its cars spawn. Match the same ordinal field native a13730 uses.
  void *roster = NULL;
  if (manager)
    ((void *(*)(void *, void **))(image + 0x21def0))(manager, &roster);
  if (roster) {
    uintptr_t begin = *(uintptr_t *)((unsigned char *)roster + 0x4d00);
    uintptr_t end = *(uintptr_t *)((unsigned char *)roster + 0x4d08);
    if (end >= begin && end - begin <= 256u * 0x2658u &&
        (end - begin) % 0x2658u == 0) {
      for (uintptr_t p = begin; p < end; p += 0x2658)
        add_model(models, &number, *(uint64_t *)(p + 0x40));
    }
    release((unsigned char *)roster + 0x18);
  }
  log_append_value("LOCAL DESIGNS: free-roam roster models=", (int)number);
  unsigned prepared = 0;
  // Prefer the actual current lineup before filling unused slots from upcoming
  // roster models. A large future roster must not crowd out cars already here.
  for (unsigned phase = 0; phase < 2; ++phase) {
    unsigned begin = phase ? instantiated : 0;
    unsigned end = phase ? number : instantiated;
    while (end > begin && prepared < active_livery_limit) {
      unsigned index = begin + random_next() % (end - begin);
      unsigned choice = choose_design(models[index], 0);
      models[index] = models[--end];
      if (choice < count && load_design(&designs[choice]))
        ++prepared;
    }
  }
  log_append_value("LOCAL DESIGNS: free-roam designs requested=", (int)prepared);
}

static void finish_loading(int timed_out) {
  if (library_future[1])
    cancel_index();
  // Incomplete resources never replace a native appearance. Native async jobs
  // keep their own references until completion/cancellation, as in normal play.
  unsigned ready = 0;
  for (unsigned i = 0; i < count; ++i) {
    if (design_ready(&designs[i])) {
      ++ready;
    } else if (designs[i].state == 1) {
      dispose_handle(&designs[i].handle);
      clear_bytes(&designs[i].handle, sizeof(Livery));
      designs[i].state = 2;
    }
  }
  loading_phase = 3;
  log_append_value(timed_out ? "LOCAL DESIGNS: loading timeout; ready designs="
                            : "LOCAL DESIGNS: loading complete; ready designs=",
                   (int)ready);
}

static int poll_loading(void *player_car) {
  if (!enabled || loading_phase == 3)
    return 1;
  uint64_t now = GetTickCount64();
  if (!loading_deadline)
    loading_deadline = now + 15000;
  if (now >= loading_deadline) {
    finish_loading(1);
    return 1;
  }
  if (!player_car && !loading_freeroam)
    return 0;
  if (!loading_phase) {
    // This is reached only AFTER the original native wait says the lineup is
    // ready. Restore any surviving cars before replacing the old session cache.
    if (!scan_population(player_car, 1)) {
      finish_loading(0);
      return 1;
    }
    reset_profile(profile_identity);
    if (!active_livery_limit) {
      finish_loading(0);
      return 1;
    }
    index_library(profile_identity);
    loading_phase = 1;
  }
  if (!scan_population(player_car, 0)) {
    finish_loading(0);
    return 1;
  }
  if (loading_phase == 1) {
    if (!library_future[1])
      return 0;
    accept_index();
    if (library_future[1])
      return 0;
    // Select up to the configured cap from the native lineup, preserving the
    // per-spawn undecorated subset. Open their saved designs only during loading.
    if (loading_freeroam)
      preload_freeroam(player_car);
    for (unsigned slot = 0; !loading_freeroam && slot < active_livery_limit; ++slot) {
      unsigned candidates = 0;
      Applied *selected = NULL;
      for (unsigned i = 0; i < ARRAY_COUNT(applied); ++i) {
        Applied *record = &applied[i];
        if (!record->car || !record->eligible || record->choice < count ||
            !*(void **)((unsigned char *)record->car + 0x2e8))
          continue;
        if (choose_design(record->ordinal, 0) < count &&
            random_next() % ++candidates == 0)
          selected = record;
      }
      if (!selected)
        break;
      selected->choice = choose_design(selected->ordinal, 0);
      load_design(&designs[selected->choice]);
    }
    loading_phase = 2;
  }
  unsigned waiting = 0;
  for (unsigned i = 0; i < count; ++i) {
    if (designs[i].state != 1)
      continue;
    update_design(&designs[i], player_car);
    waiting += !design_ready(&designs[i]);
  }
  if (waiting)
    return 0;
  for (unsigned i = 0; i < ARRAY_COUNT(applied); ++i) {
    Applied *record = &applied[i];
    if (!loading_freeroam && record->car && !record->owner && record->choice < count) {
      retain(record->car);
      decorate_car(record);
      release(record->car);
    }
  }
  finish_loading(0);
  return 1;
}

static void race_enter(void *state) {
  ((void (*)(void *))(image + 0x865b70))(state);
  begin_loading(state, 0);
}
static void freeroam_enter(void *state) {
  // StartDriving is shared by race_flow.enable_car_control, road trips and
  // free roam. Races have already prepared their cache before the pre-race UI;
  // rebuilding it here stalls the countdown-to-Go transition and replaces it.
  // Read the native event context without requiring an unpaused simulation:
  // cold boot/return loading can still own game control at this point.
  if (!enabled || !active_livery_limit || autodrive_game_event_context() != 2) {
    ((void (*)(void *))(image + 0x93c660))(state);
    return;
  }
  // StartDriving is reached AFTER free_roam_start has loaded all cars and
  // completed world streaming. Do not hold the earlier LoadPlayerCar group.
  begin_loading(state, 1);
}
static unsigned char loading_update(void *state, float delta, unsigned native) {
  // Preserve the native ready condition. The mod may extend it, never bypass it.
  if (!((unsigned char (*)(void *, float))(image + native))(state, delta))
    return 0;
  if (!enabled || loading_state != state)
    return 1;
  void *car = autodrive_game_get_car();
  int ready = poll_loading(car);
  release(car);
  return (unsigned char)ready;
}
static unsigned char race_update(void *state, float delta) {
  return loading_update(state, delta, 0x257720);
}
static unsigned char freeroam_update(void *state, float delta) {
  unsigned char ready = loading_update(state, delta, 0x257720);
  if (ready && loading_state == state && !driving_started) {
    driving_started = 1;
    ((void (*)(void *))(image + 0x93c660))(state);
  }
  return ready;
}
static void loading_exit(void *state) {
  if (loading_state == state) {
    if (loading_phase != 3)
      finish_loading(1);
    loading_state = NULL;
  }
  ((void (*)(void *))(image + 0x8d4300))(state);
}

PatchDefinition local_liveries_hook_patches[6];
static unsigned char originals[6][8], replacements[6][8];
static const unsigned char *accepted[6][2];
static void prepare_hooks(unsigned char *base) {
  // Race assignment and the FINAL StartDriving handoff shared by mainland and
  // Storm Island. The early traffic/player-load waits remain entirely native.
  static const unsigned slots[] = {
      0x2cf8838, 0x2cf8848, 0x2cf8858,
      0x2d27100, 0x2d27110, 0x2d27120};
  static const unsigned native[] = {
      0x865b70, 0x257720, 0x8d4300,
      0x93c660, 0x257720, 0x8d4300};
  static const uintptr_t callbacks[] = {
      (uintptr_t)race_enter, (uintptr_t)race_update, (uintptr_t)loading_exit,
      (uintptr_t)freeroam_enter, (uintptr_t)freeroam_update, (uintptr_t)loading_exit};
  for (unsigned i = 0; i < ARRAY_COUNT(slots); ++i) {
    uintptr_t original = (uintptr_t)(base + native[i]);
    memory_copy(originals[i], &original, 8);
    memory_copy(replacements[i], &callbacks[i], 8);
    accepted[i][0] = originals[i];
    accepted[i][1] = replacements[i];
    local_liveries_hook_patches[i] = (PatchDefinition){slots[i], 0, 8,
        GROUP_VEHICLE_CONTROLS, "local_livery_loading", originals[i],
        replacements[i], replacements[i], accepted[i], 2};
  }
}

void local_liveries_bind(unsigned char *base, int enable, int limit) {
  if (!initialized) {
    InitializeCriticalSection(&index_lock);
    initialized = 1;
    callback_vtable[0] = (const void *)copy_callback;
    callback_vtable[1] = (const void *)copy_callback;
    callback_vtable[2] = (const void *)collect_names;
    callback_vtable[3] = (const void *)callback_type;
    callback_vtable[4] = (const void *)delete_callback;
    callback_vtable[5] = (const void *)callback_destroy;
  }
  if (image && image != base)
    reset_profile(NULL);
  if (enabled && !enable)
    cancel_index();
  active_livery_limit = limit >= 0 && limit <= 32 ? (unsigned)limit : 6;
  if (enable && (!enabled || image != base))
    log_append_value("LOCAL DESIGNS: random subset; decorated-car limit=",
                     (int)active_livery_limit);
  image = base;
  enabled = enable;
  prepare_hooks(base);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestLocalLiveriesTick(void *player_car) {
  next_scan = 0;
  local_liveries_tick(player_car);
}
__declspec(dllexport) void Fh2TestLocalLiveriesBegin(void *state) {
  begin_loading(state, 0);
}
__declspec(dllexport) int Fh2TestLocalLiveriesPoll(void *player_car) {
  return poll_loading(player_car);
}
__declspec(dllexport) void Fh2TestLocalLiveriesEnd(void *state) {
  loading_exit(state);
}
__declspec(dllexport) void Fh2TestLocalLiveriesTimeout(void) {
  loading_deadline = 1;
}
__declspec(dllexport) unsigned Fh2TestLocalLiveriesModel(const wchar_t *name,
                                                         size_t length) {
  return model_in_name(name, length);
}
__declspec(dllexport) int Fh2TestLocalLiveriesEligible(void *car) {
  for (unsigned i = 0; i < ARRAY_COUNT(applied); ++i)
    if (applied[i].car == car)
      return (int)applied[i].eligible;
  return -1;
}
#endif
