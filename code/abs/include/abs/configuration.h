#include "abs/colors.h"
#include "ini.h"
#include <stdbool.h>
#include <stdio.h>
#include <linux/limits.h>
#include <libgen.h>
#include <string.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef ABS_CONFIGURATION

typedef struct {
	const char *compiler;

    char **cflags;
    size_t cflags_n;
    char **ldlibs;
    size_t ldlibs_n;

    char **lib_dirs;
    size_t lib_dirs_n;

    char **sources;
    size_t sources_n;
    char  *output;

    char *src_dir;
    char *out_dir;
    char **include_dirs;
    size_t include_n;

    char *pkg_config_path;
    char **pkg_config_libs;
    size_t pkg_config_libs_n;

    char **defines;
    size_t defines_n;

    char *build_type;
    char *obj_dir;

    char *active_mode;
    bool  hardening;
} compiler_conf;

static int has_glob_chars(const char *str) {
    return strpbrk(str, "*?[{") != NULL;
}

static int _cfg_append_str(char ***arr, size_t *n, const char *str) {
    if (!str) return 0;
    
    char **tmp = realloc(*arr, sizeof(char*) * (*n + 1));
    if (!tmp) return -1;
    
    *arr = tmp;
    (*arr)[*n] = strdup(str);
    if (!(*arr)[*n]) return -1;
    
    (*n)++;
    return 0;
}

static int _cfg_append_flags(char ***arr, size_t *n, const char *flags) {
    if (!flags || !*flags) return 0;
    
    char *buf = strdup(flags);
    if (!buf) return -1;
    
    char *saveptr = NULL;
    char *token = strtok_r(buf, " \t\n", &saveptr);
    
    while (token) {
        if (*token) {
            if (_cfg_append_str(arr, n, token) != 0) {
                free(buf);
                return -1;
            }
        }
        token = strtok_r(NULL, " \t\n", &saveptr);
    }
    
    free(buf);
    return 0;
}

static char **_str_split(const char *str, char delim, size_t *out_n) {
    if (!str || !out_n) return NULL;
    
    char **result = NULL;
    size_t count = 0;
    
    char *buf = strdup(str);
    if (!buf) return NULL;

    char delim_str[2] = {delim, '\0'};
    
    char *saveptr = NULL;
    char *token = strtok_r(buf, delim_str, &saveptr);
    
    while (token) {
        if (*token) {
            char **tmp = realloc(result, sizeof(char*) * (count + 1));
            if (!tmp) {
                for (size_t i = 0; i < count; i++) free(result[i]);
                free(result);
                free(buf);
                return NULL;
            }
            result = tmp;
            result[count++] = strdup(token);
        }
        token = strtok_r(NULL, delim_str, &saveptr);
    }
    
    free(buf);
    *out_n = count;
    return result;
}

char *get_dir_from_path(const char *filepath) {
    if (!filepath) return NULL;
    
    char *path_copy = strdup(filepath);
    if (!path_copy) return NULL;
    
    char *dir = dirname(path_copy);
    char *result = strdup(dir);
    
    free(path_copy);
    return result;
}

static int expand_sources(const char *src_dir, const char *sources_str, compiler_conf *cfg) {
    if (!sources_str) return -1;

    char *buf = strdup(sources_str);
    if (!buf) return -1;

    char *saveptr = NULL;
    char *token = strtok_r(buf, " \t\n", &saveptr);

    while (token) {
        if (*token) {
            if (has_glob_chars(token)) {
                glob_t glob_result;
                char pattern[PATH_MAX];

                if (src_dir) {
                    snprintf(pattern, sizeof(pattern), "%s/%s", src_dir, token);
                } else {
                    snprintf(pattern, sizeof(pattern), "%s", token);
                }

                int ret = glob(pattern, GLOB_TILDE | GLOB_BRACE | GLOB_ERR, NULL, &glob_result);
                
                if (ret == 0) {
                    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                        char *full_path = glob_result.gl_pathv[i];
                        char *relative_path = NULL;

                        
                        if (src_dir) {
                            size_t len = strlen(src_dir);
                            if (strncmp(full_path, src_dir, len) == 0) {
                                char *start = full_path + len;
                                if (*start == '/') start++;
                                relative_path = strdup(start);
                            } else {
                                relative_path = strdup(full_path);
                            }
                        } else {
                            relative_path = strdup(full_path);
                        }

                        if (relative_path) {
                            _cfg_append_str(&cfg->sources, &cfg->sources_n, relative_path);
                            free(relative_path);
                        }
                    }
                    globfree(&glob_result);
                } else if (ret == GLOB_NOMATCH) {
                    fprintf(stderr, "%s[warn]%s no files matched pattern: %s\n", abs_fore.yellow, abs_fore.normal, token);
                }
            } else {
                _cfg_append_str(&cfg->sources, &cfg->sources_n, token);
            }
        }
        token = strtok_r(NULL, " \t\n", &saveptr);
    }

    free(buf);
    return 0;
}

static char *extract_lib_name(const char *filepath) {
    if (!filepath) return NULL;
    
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    if (strncmp(filename, "lib", 3) == 0) {
        filename += 3;
    }
    
    char *name = strdup(filename);
    if (!name) return NULL;
    
    char *dot = strrchr(name, '.');
    if (dot) {
        if (strcmp(dot, ".a") == 0 || strcmp(dot, ".so") == 0) {
            *dot = '\0';
        } else if (strncmp(dot, ".so.", 4) == 0) {
            *dot = '\0';
        }
    }
    
    return name;
}

static int expand_libs(const char *lib_dirs_str, const char *libs_str, compiler_conf *cfg) {
    if (!libs_str) return -1;
    
    char *buf = strdup(libs_str);
    if (!buf) return -1;
    
    char *saveptr = NULL;
    char *token = strtok_r(buf, " \t\n", &saveptr);
    
    while (token) {
        if (*token) {
            if (has_glob_chars(token)) {
                glob_t glob_result;
                char pattern[PATH_MAX];
                
                if (lib_dirs_str && *lib_dirs_str) {
                    char *libdirs_buf = strdup(lib_dirs_str);
                    char *libdirs_saveptr = NULL;
                    char *libdir = strtok_r(libdirs_buf, " \t\n", &libdirs_saveptr);
                    
                    int found = 0;
                    while (libdir) {
                        snprintf(pattern, sizeof(pattern), "%s/%s", libdir, token);
                        
                        int ret = glob(pattern, GLOB_TILDE | GLOB_BRACE | GLOB_ERR, NULL, &glob_result);
                        
                        if (ret == 0 && glob_result.gl_pathc > 0) {
                            for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                                char *full_path = glob_result.gl_pathv[i];
                                
                                
                                char *full_path_copy = strdup(full_path);
                                if (!full_path_copy) continue;
                                
                                char *dir = dirname(full_path_copy);
                                char *dir_copy = strdup(dir);  
                                char *lib_name = extract_lib_name(full_path);
                                
                                if (dir_copy) {
                                    _cfg_append_str(&cfg->lib_dirs, &cfg->lib_dirs_n, dir_copy);
                                    free(dir_copy);  
                                }
                                if (lib_name) {
                                    _cfg_append_str(&cfg->ldlibs, &cfg->ldlibs_n, lib_name);
                                    free(lib_name);
                                }
                                
                                free(full_path_copy);
                            }
                            found = 1;
                            globfree(&glob_result);
                            break;
                        }
                        
                        libdir = strtok_r(NULL, " \t\n", &libdirs_saveptr);
                    }
                    free(libdirs_buf);
                    
                    if (!found) {
                        fprintf(stderr, "%s[warn]%s no libs matched pattern: %s\n", 
                                abs_fore.yellow, abs_fore.normal, token);
                    }
                } else {
                    int ret = glob(token, GLOB_TILDE | GLOB_BRACE | GLOB_ERR, NULL, &glob_result);
                    
                    if (ret == 0) {
                        for (size_t i = 0; i < glob_result.gl_pathc; i++) {
                            char *full_path = glob_result.gl_pathv[i];
                            
                            char *full_path_copy = strdup(full_path);
                            if (!full_path_copy) continue;
                            
                            char *dir = dirname(full_path_copy);
                            char *dir_copy = strdup(dir);
                            char *lib_name = extract_lib_name(full_path);
                            
                            if (dir_copy) {
                                _cfg_append_str(&cfg->lib_dirs, &cfg->lib_dirs_n, dir_copy);
                                free(dir_copy);
                            }
                            if (lib_name) {
                                _cfg_append_str(&cfg->ldlibs, &cfg->ldlibs_n, lib_name);
                                free(lib_name);
                            }
                            
                            free(full_path_copy);
                        }
                        globfree(&glob_result);
                    }
                }
            } else {
                
                if (strchr(token, '/') || strstr(token, ".a") || strstr(token, ".so")) {
                    char *token_copy = strdup(token);
                    if (!token_copy) continue;
                    
                    char *dir = dirname(token_copy);
                    char *dir_copy = strdup(dir);
                    char *lib_name = extract_lib_name(token);
                    
                    if (strcmp(dir, ".") != 0 && dir_copy) {
                        _cfg_append_str(&cfg->lib_dirs, &cfg->lib_dirs_n, dir_copy);
                        free(dir_copy);
                    }
                    if (lib_name) {
                        _cfg_append_str(&cfg->ldlibs, &cfg->ldlibs_n, lib_name);
                        free(lib_name);
                    }
                    
                    free(token_copy);
                } else {
                    _cfg_append_str(&cfg->ldlibs, &cfg->ldlibs_n, token);
                }
            }
        }
        token = strtok_r(NULL, " \t\n", &saveptr);
    }
    
    free(buf);
    return 0;
}

int build_modules(const char *prog, const char *config_dir, ini_config *ini){
    if (ini_check(ini, "modules") != 0) return 1;

    ini_iterator it = ini_iterator_init(ini);
    for (
        ini_iter i = ini_iterate(&it); 
        i.sec_name != NULL;
        i = ini_iterate(&it)
    ){
        if (0 != strcmp(i.sec_name, "modules")) continue;
        printf("%s[modules]%s building module: %s (%s)\n", abs_fore.green, abs_fore.normal, i.key, i.value);
        
        char command[PATH_MAX + 512] = "";
        
        char *inconf_path = get_before(i.value, ',');
        char cdpath_buf[PATH_MAX] = {0};
        snprintf(cdpath_buf, PATH_MAX, "%s/%s", config_dir, inconf_path);
        free(inconf_path);
        
        const char *inconf_confpath = struntilnot(strchr(i.value, ',') + 1, ' ');

        snprintf(command, PATH_MAX + 512, "cd %s && MAIN_DIR=%s %s %s", cdpath_buf, config_dir, prog, inconf_confpath);
        int r = system(command);

        if (r == 0){
            printf("%s[modules][%s]%s: build %sSUCCESS%s\n", abs_fore.yellow, i.key, abs_fore.normal, abs_fore.green, abs_fore.normal);
        } else {
            printf("%s[modules][%s]%s: build %sFAIL%s\n", abs_fore.yellow, i.key, abs_fore.normal, abs_fore.red, abs_fore.normal);
            exit(-1);
        }
    }

    return 0;
}

char *nstrdup(const char *str){
    if (!str) return NULL;
    return strdup(str);
}

int config_ini_parse(ini_config *ini, compiler_conf *cfg){
    cfg->active_mode = ini_get_at(ini, "modes", "active");
    if (!cfg->active_mode) cfg->active_mode = "debug";

    cfg->compiler = ini_get_at(ini, "compiler", "cc");
    if (!cfg->compiler) cfg->compiler = "gcc";
    
    _cfg_append_flags(&cfg->cflags, &cfg->cflags_n, 
                      ini_get_at(ini, "flags", "common"));
    
    const char *mode_name = (strcmp(cfg->active_mode, "debug") == 0) 
                            ? "mode.debug" : "mode.release";
    if (mode_name)
        _cfg_append_flags(&cfg->cflags, &cfg->cflags_n, 
                        ini_get_at(ini, mode_name, "flags"));
    
    cfg->build_type = nstrdup(ini_get_at(ini, "compiler", "build"));
    if (!cfg->build_type) cfg->build_type = strdup("binary");

    const char *sec = ini_get_at(ini, 
        cfg->active_mode[0] == 'd' ? "mode.debug" : "mode.release", 
        "security");
    cfg->hardening = sec && strcmp(sec, "true") == 0;

    if (cfg->hardening) {
        _cfg_append_flags(&cfg->cflags, &cfg->cflags_n, 
                        ini_get_at(ini, "flags", "hardening"));
    }

    cfg->obj_dir = nstrdup(ini_get_at(ini, "dirs", "objects"));
    if (!cfg->obj_dir) {
        cfg->obj_dir = strdup(".objs");
    }
    
    const char *lib_dirs_ini = ini_get_at(ini, "dirs", "libs");
    const char *libs_ini = ini_get_at(ini, "dependencies", "libs");

    if (libs_ini) {
        if (expand_libs(lib_dirs_ini, libs_ini, cfg) != 0) {
            fprintf(stderr, "%s[error]%s failed to process libs\n", 
                    abs_fore.red, abs_fore.normal);
            exit(-1);
        }
    }
    
    if (lib_dirs_ini) {
        _cfg_append_flags(&cfg->lib_dirs, &cfg->lib_dirs_n, lib_dirs_ini);
    }

    cfg->output = nstrdup(ini_get_at(ini, "files", "output"));
    if (!cfg->output){
        fprintf(stderr, "%s[error]%s no output file set\n", 
                    abs_fore.red, abs_fore.normal);
        exit(-1);
    }

    cfg->src_dir = nstrdup(ini_get_at(ini, "dirs", "src"));
    if (!cfg->src_dir){
        cfg->src_dir = strdup(".");
    }

    if (ini_get_at(ini, "dirs", "includes")) {
        _cfg_append_flags(&cfg->include_dirs, &cfg->include_n, 
                        ini_get_at(ini, "dirs", "includes"));
    }

    cfg->out_dir = nstrdup(ini_get_at(ini, "dirs", "output"));
    if (!cfg->out_dir){
        cfg->out_dir = strdup(".");
    }
    
    const char *src_list = ini_get_at(ini, "files", "sources");
    if (src_list) {
        if (expand_sources(cfg->src_dir, src_list, cfg) != 0) {
            fprintf(stderr, "%s[error]%s failed to process sources\n", abs_fore.red, abs_fore.normal);
            exit(-1);
        }
        if (cfg->sources_n == 0) {
             fprintf(stderr, "%s[error]%s no sources resolved\n", abs_fore.red, abs_fore.normal);
             exit(-1);
        }
    } else {
        fprintf(stderr, "%s[error]%s no sources provided\n", abs_fore.red, abs_fore.normal);
        exit(-1);
    }

    cfg->pkg_config_path = nstrdup(ini_get_at(ini, "dependencies", "pkg_config_path"));
    const char *pkg_list = ini_get_at(ini, "dependencies", "pkg_config_libs");
    if (pkg_list) {
        cfg->pkg_config_libs = _str_split(pkg_list, ' ', &cfg->pkg_config_libs_n);
    }
    
    ini_iterator dit = ini_iterator_init(ini);
    for (ini_iter d = ini_iterate(&dit); d.sec_name; d = ini_iterate(&dit)) {
        if (strcmp(d.sec_name, "defines") == 0) {
            char def[256];
            snprintf(def, sizeof(def), "-D%s=%s", d.key, d.value);
            _cfg_append_str(&cfg->defines, &cfg->defines_n, def);
        }
    }

    return 0;
}

#endif
#define ABS_CONFIGURATION