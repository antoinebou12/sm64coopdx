#include "types.h"

#include "pc/utils/cJSON.h"
#include "pc/debuglog.h"
#include "pc/fs/fmem.h"

#include "manifest.h"

#include "pc/mods/mods.h"
#include "pc/mods/mods_utils.h"

cJSON *manifest_get_json_from_path(const char *path) {
    // get file contents and size
    char *fileContents = NULL;
    size_t size = 0;

    if (!fs_sys_load_file(path, &fileContents, &size)) {
        return NULL;
    }

    // parse json
    cJSON *json = cJSON_ParseWithLength(fileContents, size);
    if (!json) { return NULL; }

    free(fileContents); // we no longer need this
    return json;
}

void manifest_destroy_json(cJSON *json) {
    if (json) { cJSON_Delete(json); }
}

char **manifest_get_array_of_string(cJSON *json, const char *key) {
    if (!json) { return NULL; }

    cJSON *jsonItem = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsArray(jsonItem)) { return NULL; }

    int size = cJSON_GetArraySize(jsonItem);

    // allocate array
    char **array = malloc((size + 1) * sizeof(char *)); // + 1 for null termination
    if (!array) { return NULL; }

    cJSON *element = NULL;
    int index = 0;

    // for each element....
    cJSON_ArrayForEach(element, jsonItem) {
        // if we are not a string, cleanup and bail, otherwise....
        if (!cJSON_IsString(element) || element->valuestring == NULL) {
            for (int i = 0; i < index; i++) {
                free(array[i]);
            }
            free(array);
            return NULL;
        }

        // set string to valuestring
        array[index] = strdup(element->valuestring);
        if (!array[index]) {
            for (int i = 0; i < index; i++) {
                free(array[i]);
            }
            free(array);
            return NULL;
        }
        index++;
    }

    // null terminate array
    array[index] = NULL;

    return array;
}

char *manifest_get_path(cJSON *json, const char *key) {
    if (!json) { return NULL; }

    cJSON *jsonItem = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsString(jsonItem) || jsonItem->valuestring == NULL) { return NULL; }

    // if it appears to be manipulating path with . and .. return NULL
    if (path_has_traversal(jsonItem->valuestring)) {
        cJSON_Delete(json);
        return NULL;
    }

    char *entryFile = jsonItem->valuestring;
    normalize_path(entryFile);
    return entryFile;
}

char *manifest_get_string(cJSON *json, const char *key) {
    if (!json) { return NULL; }

    cJSON *jsonItem = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsString(jsonItem) || jsonItem->valuestring == NULL) { return NULL; }

    return strdup(jsonItem->valuestring);
}

bool manifest_get_bool(cJSON *json, const char *key, bool defaultValue) {
    if (!json) { return defaultValue; }

    cJSON *jsonItem = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsBool(jsonItem)) { return defaultValue; }
    return cJSON_IsTrue(jsonItem);
}
