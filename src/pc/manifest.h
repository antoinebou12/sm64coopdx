#pragma once

#include "pc/utils/cJSON.h"

cJSON *manifest_get_json_from_path(const char *path);
void manifest_destroy_json(cJSON *json);
char **manifest_get_array_of_string(cJSON *json, const char *key);
char *manifest_get_path(cJSON *json, const char *key);
char *manifest_get_string(cJSON *json, const char *key);
bool manifest_get_bool(cJSON *json, const char *key, bool defaultValue);
