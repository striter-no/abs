#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#ifndef ABS_INI

typedef struct {
    char *key;
    char *value;
} ini_entry;

typedef struct {
    char      *name;
    ini_entry *entries;
    size_t     n;
} ini_section;

typedef struct {
    ini_section *sections;
    size_t       n;
} ini_config;

typedef struct {
    const char *sec_name;
    const char *key;
    const char *value;
} ini_iter;

const char *struntilnot(char *str, char c){
	for (char *ch = str; *ch != '\0'; ch++){
		if (*ch != c) return ch;
	}
	return NULL;
}

char *get_before(const char *str, char c){
	if (!str) return NULL;
	
	const char  *st = strchrnul(str, c);

	char *result = malloc((int)(st - str) + 1);
	strncpy(result, str, (int)(st - str));
	result[(int)(st - str)] = '\0';

	return result;
}

static int ini_go_through_lines(
    const char *content,
    ini_config *config
){
    size_t cont_size = strlen(content);
	size_t passed_chars = 0;

    config->n = 0;
    config->sections = NULL;
    ini_section *last_sec = NULL;

	while (strchrnul(content, '\n') != NULL){
		char *line = get_before(content, '\n');
		if (line == NULL) break;
		
		if (strlen(line) == 0 || strlen(line) == 1) goto _continue;
        if (line[0] == '#') goto _continue;

		// check empty strings and skip
		char empty = 1;
		for (size_t i = 0; i < strlen(line) - 1; i++){
			if (line[i] != ' ' && line[i] != '\t') {
				empty = 0;
				break;
			}
		}

		if (empty) goto _continue;

        // new section
        if (line[0] == '['){
            config->sections = realloc(config->sections, sizeof(ini_section) * (++config->n));
            last_sec = &config->sections[config->n - 1];

            last_sec->n = 0;
            last_sec->entries = NULL;
            last_sec->name = get_before(line + 1, ']');
            goto _continue;
        }

        if (!last_sec){
            fprintf(stderr, "[ini] skipping line because of out-of-section: %s\n", line);
            goto _continue;
        }

        last_sec->entries = realloc(last_sec->entries, sizeof(ini_entry) * (++last_sec->n));
        char *tmp = get_before(line, '=');
        char *key_str = get_before(tmp, ' ');
        free(tmp);

	    const char *val_str = struntilnot(strchr(line, '=') + 1, ' ');

        last_sec->entries[last_sec->n - 1].key   = key_str;
        last_sec->entries[last_sec->n - 1].value = strdup(val_str);

_continue:
		passed_chars += strlen(line) + 1;
		content += strlen(line) + 1;
		free(line);

		if (passed_chars >= cont_size) break;
	}

    return 0;
}

int ini_load_file(ini_config *config, const char *confpath){
    FILE *fConfig = fopen(confpath, "r");
    if (!fConfig) return -1;

	int   nConfig = dup(fileno(fConfig));
	fclose(fConfig);

	ssize_t chunk;
    size_t total = 0, capacity = 1024;
    char *content = malloc(capacity);
    if (!content) return -1;

    while ((chunk = read(nConfig, content + total, 1024)) > 0) {
        total += chunk;
        if (total + 1024 > capacity) {
            capacity *= 2;
            char *tmp = realloc(content, capacity);
            if (!tmp) { free(content); return -1; }
            content = tmp;
        }
    }
    if (chunk < 0) { free(content); close(nConfig); return -1; }
	content[total] = '\0';

	close(nConfig);

    ini_go_through_lines(content, config);
    free(content);

    return 0;
}

char *ini_get_at(ini_config *config, const char *section, const char *key){
    for (size_t i = 0; i < config->n; i++){
        if (strcmp(config->sections[i].name, section))
            continue;
        
        for (size_t k = 0; k < config->sections[i].n; k++){
            if (strcmp(config->sections[i].entries[k].key, key))
                continue;
            return config->sections[i].entries[k].value;
        }
    }
    return NULL;
}

int ini_check(ini_config *config, const char *section){
    for (size_t i = 0; i < config->n; i++){
        if (strcmp(config->sections[i].name, section))
            continue;

        return 0;
    }

    return 1;
}

typedef struct {
    size_t sec_idx;
    size_t ent_idx;
    const ini_config *cfg;
} ini_iterator;

ini_iterator ini_iterator_init(ini_config *config){
    return (ini_iterator){
        .sec_idx = 0,
        .ent_idx = 0,
        .cfg     = config
    };
}

ini_iter ini_iterate(ini_iterator *it){
    if (it->sec_idx >= it->cfg->n) return (ini_iter){0};
    if (it->ent_idx >= it->cfg->sections[it->sec_idx].n){
        it->sec_idx++;
        it->ent_idx = 0;

        if (it->sec_idx >= it->cfg->n) return (ini_iter){0};
    }

    ini_entry e = it->cfg->sections[it->sec_idx].entries[it->ent_idx++];
    return (ini_iter){
        .sec_name = it->cfg->sections[it->sec_idx].name,
        .key = e.key,
        .value = e.value
    };
}

void ini_clear_config(ini_config *config){
    if (!config) return;

    for (size_t i = 0; i < config->n; i++){
        for (size_t k = 0; k < config->sections[i].n; k++){
            free(config->sections[i].entries[k].key);
            free(config->sections[i].entries[k].value);
        }
        free(config->sections[i].entries);
        free(config->sections[i].name);
    }
    free(config->sections);
    config->sections = 0;
    config->n = 0;
}

#endif
#define ABS_INI