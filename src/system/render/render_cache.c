#include "system/render/render_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

#if defined(ESP_PLATFORM)
/************************************************
* render_cache_lock
* Serializes access to render-owned cache banks across render worker tasks.
* Parameters: state = render state that owns the cache mutex.
* Returns: void.
***************************************************/
static void render_cache_lock(render_state_t *state)
{
    if (state && state->cache_lock)
    {
        (void)xSemaphoreTake(state->cache_lock, portMAX_DELAY);
    }
}

/************************************************
* render_cache_unlock
* Releases the render cache mutex after one cache operation completes.
* Parameters: state = render state that owns the cache mutex.
* Returns: void.
***************************************************/
static void render_cache_unlock(render_state_t *state)
{
    if (state && state->cache_lock)
    {
        (void)xSemaphoreGive(state->cache_lock);
    }
}
#endif

#if defined(ESP_PLATFORM)
/************************************************
* render_cache_log_alloc_failure
* Prints allocator state when a cache slot cannot reserve backing storage.
* Parameters: expected_size = requested byte count.
* Returns: void.
***************************************************/
static void render_cache_log_alloc_failure(uint32_t expected_size)
{
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    const size_t heap8_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t heap8_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    printf("CA:F:%lu\n", (unsigned long)expected_size);
    printf("CA:P:%lu:%lu\n", (unsigned long)psram_free, (unsigned long)psram_largest);
    printf("CA:H:%lu:%lu\n", (unsigned long)heap8_free, (unsigned long)heap8_largest);
    printf("CA:I:%lu:%lu\n", (unsigned long)internal_free, (unsigned long)internal_largest);
}
#endif

/************************************************
* render_cache_find_in_bank
* Searches one cache bank for a matching file name and size.
* Parameters: entries = cache bank storage, entry_count = bank size,
*             name = file name, expected_size = byte count.
* Returns: matching cache entry, or NULL when not found.
***************************************************/
static render_cache_entry_t *render_cache_find_in_bank(render_cache_entry_t *entries,
                                                       uint8_t entry_count,
                                                       const char *name,
                                                       uint32_t expected_size)
{
    uint8_t i = 0;

    if (!entries || !name)
    {
        return 0;
    }

    for (i = 0; i < entry_count; i++)
    {
        render_cache_entry_t *entry = &entries[i];
        if (!entry->valid || entry->size != expected_size)
        {
            continue;
        }
        if (strncmp(entry->name, name, sizeof(entry->name)) == 0)
        {
            return entry;
        }
    }

    return 0;
}

/************************************************
* render_cache_prepare_entry
* Ensures a cache slot owns storage for the requested image metadata.
* Parameters: entry = cache slot, name = file name, expected_size = byte count.
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t render_cache_prepare_entry(render_cache_entry_t *entry,
                                          const char *name,
                                          uint32_t expected_size)
{
    uint8_t i = 0;

    if (!entry || !name)
    {
        return 0;
    }

    if (!entry->data || entry->size != expected_size)
    {
#if defined(ESP_PLATFORM)
        void *new_data = heap_caps_malloc(expected_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!new_data)
        {
            printf("CA:P0:%lu\n", (unsigned long)expected_size);
            new_data = malloc(expected_size);
        }
#else
        void *new_data = malloc(expected_size);
#endif
        if (!new_data)
        {
#if defined(ESP_PLATFORM)
            render_cache_log_alloc_failure(expected_size);
#endif
            entry->valid = 0;
            return 0;
        }
        if (entry->data)
        {
            free(entry->data);
        }
        entry->data = (uint8_t *)new_data;
        entry->size = expected_size;
    }

    for (i = 0; i < (uint8_t)(sizeof(entry->name) - 1u); i++)
    {
        entry->name[i] = name[i];
        if (!name[i])
        {
            break;
        }
    }
    entry->name[sizeof(entry->name) - 1u] = '\0';
    entry->valid = 1;
    return 1;
}

/************************************************
* render_cache_store_in_bank
* Stores image bytes into a rotating cache bank.
* Parameters: entries = cache bank storage, next_index = replacement cursor,
*             entry_count = bank size, name = file name, data = source bytes,
*             size = byte count.
* Returns: 1 on success, 0 on failure.
***************************************************/
static uint8_t render_cache_store_in_bank(render_cache_entry_t *entries,
                                          uint8_t *next_index,
                                          uint8_t entry_count,
                                          const char *name,
                                          const uint8_t *data,
                                          uint32_t size)
{
    render_cache_entry_t *entry = 0;

    if (!entries || !next_index || !name || !data || !entry_count)
    {
        return 0;
    }

    entry = &entries[*next_index];
    *next_index = (uint8_t)((*next_index + 1u) % entry_count);

    if (!render_cache_prepare_entry(entry, name, size))
    {
        return 0;
    }

    memcpy(entry->data, data, size);
    return 1;
}

/************************************************
* render_cache_clear_bank
* Invalidates and frees every slot in a cache bank.
* Parameters: entries = cache bank storage, entry_count = bank size.
* Returns: void.
***************************************************/
static void render_cache_clear_bank(render_cache_entry_t *entries, uint8_t entry_count)
{
    uint8_t i = 0;

    if (!entries)
    {
        return;
    }

    for (i = 0; i < entry_count; i++)
    {
        render_cache_entry_t *entry = &entries[i];
        if (entry->data)
        {
            free(entry->data);
            entry->data = 0;
        }
        entry->name[0] = '\0';
        entry->size = 0;
        entry->valid = 0;
    }
}

/************************************************
* render_cache_init
* Initializes both render cache banks to an empty state.
* Parameters: state = render state that owns the caches.
* Returns: void.
***************************************************/
void render_cache_init(render_state_t *state)
{
    if (!state)
    {
        return;
    }

    memset(state->primary_cache, 0, sizeof(state->primary_cache));
    memset(state->secondary_cache, 0, sizeof(state->secondary_cache));
    state->primary_cache_next = 0;
    state->secondary_cache_next = 0;
}

/************************************************
* render_cache_reset_primary
* Clears the startup-managed primary cache bank.
* Parameters: state = render state that owns the caches.
* Returns: void.
***************************************************/
void render_cache_reset_primary(render_state_t *state)
{
    if (!state)
    {
        return;
    }

    render_cache_lock(state);
    render_cache_clear_bank(state->primary_cache, RENDER_PRIMARY_CACHE_ENTRIES);
    state->primary_cache_next = 0;
    render_cache_unlock(state);
}

/************************************************
* render_cache_reset_secondary
* Clears the on-demand secondary cache bank.
* Parameters: state = render state that owns the caches.
* Returns: void.
***************************************************/
void render_cache_reset_secondary(render_state_t *state)
{
    if (!state)
    {
        return;
    }

    if (!RENDER_SECONDARY_CACHE_ENABLED)
    {
        return;
    }

    render_cache_lock(state);
    render_cache_clear_bank(state->secondary_cache, RENDER_SECONDARY_CACHE_ENTRIES);
    state->secondary_cache_next = 0;
    render_cache_unlock(state);
}

/************************************************
* render_cache_reset_all
* Clears both the primary and secondary render cache banks.
* Parameters: state = render state that owns the caches.
* Returns: void.
***************************************************/
void render_cache_reset_all(render_state_t *state)
{
    if (!state)
    {
        return;
    }

    render_cache_reset_primary(state);
    render_cache_reset_secondary(state);
}

/************************************************
* render_cache_has_secondary
* Checks whether the secondary cache already contains one image.
* Parameters: state = render state, name = file name, expected_size = byte count.
* Returns: 1 when found, 0 otherwise.
***************************************************/
uint8_t render_cache_has_secondary(render_state_t *state, const char *name, uint32_t expected_size)
{
    uint8_t found = 0;

    if (!state)
    {
        return 0;
    }

    render_cache_lock(state);
    found = render_cache_find_in_bank(state->primary_cache,
                                      RENDER_PRIMARY_CACHE_ENTRIES,
                                      name,
                                      expected_size) != 0;
    if (!found && RENDER_SECONDARY_CACHE_ENABLED)
    {
        found = render_cache_find_in_bank(state->secondary_cache,
                                          RENDER_SECONDARY_CACHE_ENTRIES,
                                          name,
                                          expected_size) != 0;
    }
    render_cache_unlock(state);
    return found;
}

/************************************************
* render_cache_find_any
* Searches the primary cache first, then the secondary cache.
* Parameters: state = render state, name = file name, expected_size = byte count.
* Returns: matching cache entry, or NULL when not found.
***************************************************/
render_cache_entry_t *render_cache_find_any(render_state_t *state, const char *name, uint32_t expected_size)
{
    render_cache_entry_t *entry = 0;

    if (!state)
    {
        return 0;
    }

    render_cache_lock(state);
    entry = render_cache_find_in_bank(state->primary_cache,
                                      RENDER_PRIMARY_CACHE_ENTRIES,
                                      name,
                                      expected_size);
    if (entry)
    {
        render_cache_unlock(state);
        return entry;
    }

    if (RENDER_SECONDARY_CACHE_ENABLED)
    {
        entry = render_cache_find_in_bank(state->secondary_cache,
                                          RENDER_SECONDARY_CACHE_ENTRIES,
                                          name,
                                          expected_size);
    }
    render_cache_unlock(state);
    return entry;
}

/************************************************
* render_cache_copy_any
* Copies one cached image into a caller buffer while holding the cache lock.
* Parameters: state = render state, name = file name, expected_size = byte count,
*             dst = destination buffer, capacity = destination size.
* Returns: 1 on success, 0 otherwise.
***************************************************/
uint8_t render_cache_copy_any(render_state_t *state,
                              const char *name,
                              uint32_t expected_size,
                              uint8_t *dst,
                              uint32_t capacity)
{
    render_cache_entry_t *entry = 0;

    if (!state || !name || !dst || capacity < expected_size)
    {
        return 0;
    }

    render_cache_lock(state);
    entry = render_cache_find_in_bank(state->primary_cache,
                                      RENDER_PRIMARY_CACHE_ENTRIES,
                                      name,
                                      expected_size);
    if (!entry && RENDER_SECONDARY_CACHE_ENABLED)
    {
        entry = render_cache_find_in_bank(state->secondary_cache,
                                          RENDER_SECONDARY_CACHE_ENTRIES,
                                          name,
                                          expected_size);
    }
    if (entry)
    {
        memcpy(dst, entry->data, expected_size);
    }
    render_cache_unlock(state);
    return entry != 0;
}

/************************************************
* render_cache_store_primary
* Stores image bytes into the startup-loaded primary cache bank.
* Parameters: state = render state, name = file name, data = source bytes,
*             size = byte count.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_cache_store_primary(render_state_t *state,
                                   const char *name,
                                   const uint8_t *data,
                                   uint32_t size)
{
    uint8_t ok = 0;

    if (!state)
    {
        return 0;
    }

    render_cache_lock(state);
    ok = render_cache_store_in_bank(state->primary_cache,
                                    &state->primary_cache_next,
                                    RENDER_PRIMARY_CACHE_ENTRIES,
                                    name,
                                    data,
                                    size);
    render_cache_unlock(state);
    return ok;
}

/************************************************
* render_cache_store_secondary
* Stores image bytes into the short-lived secondary cache bank.
* Parameters: state = render state, name = file name, data = source bytes,
*             size = byte count.
* Returns: 1 on success, 0 on failure.
***************************************************/
uint8_t render_cache_store_secondary(render_state_t *state,
                                     const char *name,
                                     const uint8_t *data,
                                     uint32_t size)
{
    uint8_t ok = 0;

    if (!state)
    {
        return 0;
    }

    if (!RENDER_SECONDARY_CACHE_ENABLED)
    {
        return render_cache_store_primary(state, name, data, size);
    }

    render_cache_lock(state);
    ok = render_cache_store_in_bank(state->secondary_cache,
                                    &state->secondary_cache_next,
                                    RENDER_SECONDARY_CACHE_ENTRIES,
                                    name,
                                    data,
                                    size);
    render_cache_unlock(state);
    return ok;
}
