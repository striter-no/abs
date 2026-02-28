#include "configuration.h"
#include <dirent.h>
#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#ifndef ABS_COMPILATION

static void _free_str_array(char ***arr, size_t *n) {
    if (!arr || !*arr) return;
    for (size_t i = 0; i < *n; i++) {
        free((*arr)[i]);
    }
    free(*arr);
    *arr = NULL;
    *n = 0;
}

typedef struct {
    char **obj_paths;
    size_t obj_n;
    char **src_paths;
    size_t src_n;
} build_artifacts;

static void _init_artifacts(build_artifacts *art) {
    if (!art) return;
    art->obj_paths = NULL;
    art->obj_n = 0;
    art->src_paths = NULL;
    art->src_n = 0;
}

static void _free_artifacts(build_artifacts *art) {
    if (!art) return;
    _free_str_array(&art->obj_paths, &art->obj_n);
    _free_str_array(&art->src_paths, &art->src_n);
    art->src_n = 0;
    art->obj_n = 0;
    art->obj_paths = 0;
    art->src_paths = 0;
}

static int _add_artifact(build_artifacts *art, const char *src, const char *obj) {
    if (!art || !src || !obj) return -1;
    
    char **tmp_obj = realloc(art->obj_paths, sizeof(char*) * (art->obj_n + 1));
    char **tmp_src = realloc(art->src_paths, sizeof(char*) * (art->src_n + 1));
    
    if (!tmp_obj || !tmp_src) {
        free(tmp_obj);
        free(tmp_src);
        return -1;
    }
    
    art->obj_paths = tmp_obj;
    art->src_paths = tmp_src;
    
    art->obj_paths[art->obj_n] = strdup(obj);
    art->src_paths[art->src_n] = strdup(src);
    
    if (!art->obj_paths[art->obj_n] || !art->src_paths[art->obj_n]) {
        free(art->obj_paths[art->obj_n]);
        free(art->src_paths[art->src_n]);
        return -1;
    }
    
    art->obj_n++;
    art->src_n++;
    
    return 0;
}

static size_t build_common_flags(const compiler_conf *cfg, char *out_buf, size_t out_sz, size_t pos) {
    for (size_t i = 0; i < cfg->cflags_n; i++) {
        pos += snprintf(out_buf + pos, out_sz - pos, "%s ", cfg->cflags[i]);
    }
    for (size_t i = 0; i < cfg->defines_n; i++) {
        pos += snprintf(out_buf + pos, out_sz - pos, "%s ", cfg->defines[i]);
    }
    for (size_t i = 0; i < cfg->include_n; i++){
        pos += snprintf(out_buf + pos, out_sz - pos, "-I%s ", cfg->include_dirs[i]);
    }
    for (size_t i = 0; i < cfg->lib_dirs_n; i++){
        pos += snprintf(out_buf + pos, out_sz - pos, "-L%s ", cfg->lib_dirs[i]);
    }
    for (size_t i = 0; i < cfg->pkg_config_libs_n; i++) {
        pos += snprintf(out_buf + pos, out_sz - pos, 
            "$(pkg-config --cflags %s) ", cfg->pkg_config_libs[i]);
    }
    return pos;
}

static size_t build_ldlibs(const compiler_conf *cfg, char *out_buf, size_t out_sz, size_t pos) {
    for (size_t i = 0; i < cfg->pkg_config_libs_n; i++) {
        pos += snprintf(out_buf + pos, out_sz - pos, 
            "$(pkg-config --libs %s) ", cfg->pkg_config_libs[i]);
    }
    for (size_t i = 0; i < cfg->ldlibs_n; i++) {
        if (strchr(cfg->ldlibs[i], '/')) {
            pos += snprintf(out_buf + pos, out_sz - pos, "\"%s\" ", cfg->ldlibs[i]);
        } else {
            pos += snprintf(out_buf + pos, out_sz - pos, "-l\"%s\" ", cfg->ldlibs[i]);
        }
    }
    return pos;
}

static int needs_rebuild(const char *src_path, const char *obj_path) {
    struct stat src_st, obj_st;
    
    if (stat(obj_path, &obj_st) != 0) {
        return 1;
    }
    
    if (stat(src_path, &src_st) != 0) {
        fprintf(stderr, "%s[error]%s source file not found: %s\n", 
                abs_fore.red, abs_fore.normal, src_path);
        return 0;
    }
    
    if (src_st.st_mtime > obj_st.st_mtime) {
        return 1;
    }
    
    return 0;
}

static const char *src_extensions[] = {
    ".cpp", ".cxx", ".cc", ".C", ".CPP",  /* C++ */
    ".c", ".h", ".hpp", ".hxx",           /* C/C++ headers */
    NULL
};

static const char *get_ext_prefix(const char *filename) {
    if (!filename) return "unk";
    
    size_t len = strlen(filename);
    
    for (int i = 0; src_extensions[i] != NULL; i++) {
        const char *ext = src_extensions[i];
        size_t ext_len = strlen(ext);
        if (len > ext_len && strcmp(filename + len - ext_len, ext) == 0) {
            if (strcmp(ext, ".cpp") == 0 || strcmp(ext, ".CPP") == 0) return "cpp";
            if (strcmp(ext, ".cxx") == 0 || strcmp(ext, ".CXX") == 0) return "cxx";
            if (strcmp(ext, ".cc") == 0 || strcmp(ext, ".CC") == 0) return "cc";
            if (strcmp(ext, ".c") == 0 || strcmp(ext, ".C") == 0) return "c";
            if (strcmp(ext, ".h") == 0 || strcmp(ext, ".H") == 0) return "h";
            if (strcmp(ext, ".hpp") == 0 || strcmp(ext, ".HPP") == 0) return "hpp";
            if (strcmp(ext, ".hxx") == 0 || strcmp(ext, ".HXX") == 0) return "hxx";
        }
    }
    return "unk";
}

static void get_obj_path(const compiler_conf *cfg, const char *src, char *out, size_t out_sz) {
    if (!cfg || !src || !out || out_sz == 0) return;
    
    char src_copy[PATH_MAX];
    strncpy(src_copy, src, sizeof(src_copy) - 1);
    src_copy[sizeof(src_copy) - 1] = '\0';
    
    const char *prefix = get_ext_prefix(src_copy);
    
    char *dot = strrchr(src_copy, '.');
    if (dot) *dot = '\0';
    
    for (char *p = src_copy; *p; p++) {
        if (*p == '/') *p = '_';
        #ifdef __linux__
        if (*p == '\\') *p = '_'; 
        #endif
    }
    
    char *clean = src_copy;
    while (*clean == '_') clean++;
    
    snprintf(out, out_sz, "%s/%s_%s.o", cfg->obj_dir, prefix, clean);
}

int build_config_emit_cmd(int force_recompile, const compiler_conf *cfg, char *out_buf, size_t out_sz) {
    size_t pos = 0;
    build_artifacts artifacts;
    _init_artifacts(&artifacts);
    
    struct stat st = {0};
    if (cfg->obj_dir && stat(cfg->obj_dir, &st) == -1) {
        mkdir(cfg->obj_dir, 0755);
    }
    if (cfg->out_dir && stat(cfg->out_dir, &st) == -1) {
        mkdir(cfg->out_dir, 0755);
    }
    
    int is_library = (cfg->build_type && 
                     (strcmp(cfg->build_type, "static") == 0 || 
                      strcmp(cfg->build_type, "shared") == 0));
    
    const char *phase = cfg->build_phase ? cfg->build_phase : "all";
    int phase_compile = (strcmp(phase, "compile") == 0 || strcmp(phase, "all") == 0);
    int phase_link = (strcmp(phase, "link") == 0 || strcmp(phase, "all") == 0);
    
    int cmd_started = 0;
    int any_compiled = 0;
    int need_link = 0;
    
    if (cfg->pkg_config_path) {
        pos += snprintf(out_buf + pos, out_sz - pos, 
            "PKG_CONFIG_PATH=%s ", cfg->pkg_config_path);
    }
    

    if (phase_compile) {
        for (size_t i = 0; i < cfg->sources_n; i++) {
            const char *src = cfg->sources[i];
            char obj_path[PATH_MAX];
            char src_full_path[PATH_MAX];
            
            get_obj_path(cfg, src, obj_path, sizeof(obj_path));
            snprintf(src_full_path, sizeof(src_full_path), "%s/%s", cfg->src_dir, src);
            
            if (!force_recompile && !needs_rebuild(src_full_path, obj_path)) {
                printf("%s[skip]%s %s (up to date)\n", 
                       abs_fore.cyan, abs_fore.normal, src);
                _add_artifact(&artifacts, src_full_path, obj_path);
                continue;
            }
            
            printf("%s[compile]%s %s\n", 
                   abs_fore.green, abs_fore.normal, src);
            

            if (cmd_started) {
                pos += snprintf(out_buf + pos, out_sz - pos, "&& ");
            }
            
            pos += snprintf(out_buf + pos, out_sz - pos, "%s ", cfg->compiler);
            
            if (is_library && strcmp(cfg->build_type, "shared") == 0) {
                pos += snprintf(out_buf + pos, out_sz - pos, "-fPIC ");
            }
            
            pos = build_common_flags(cfg, out_buf, out_sz, pos);
            pos += snprintf(out_buf + pos, out_sz - pos, "-c ");
            pos += snprintf(out_buf + pos, out_sz - pos, "\"%s\" -o \"%s\" ", 
                           src_full_path, obj_path);
            
            _add_artifact(&artifacts, src_full_path, obj_path);
            any_compiled = 1;
            cmd_started = 1;
        }
    }
    

     if (phase_link) {
    
        
    
        if (any_compiled) {
            need_link = 1;
            printf("%s[link]%s %s (objects updated)\n", 
                   abs_fore.blue, abs_fore.normal, cfg->output);
        } 
    
        else {
            need_link = 0;
            
            char out_path[PATH_MAX];
            if (is_library) {
                snprintf(out_path, sizeof(out_path), "%s/%slib%s.%s", 
                        cfg->out_dir, 
                        cfg->output ? "" : "lib", 
                        cfg->output,
                        strcmp(cfg->build_type, "static") == 0 ? "a" : "so");
            } else {
                snprintf(out_path, sizeof(out_path), "%s/%s", cfg->out_dir, cfg->output);
            }
            
            struct stat out_st;
        
            if (stat(out_path, &out_st) != 0) {
                need_link = 1;
                printf("%s[link]%s %s (binary missing)\n", 
                       abs_fore.blue, abs_fore.normal, cfg->output);
            } else {
            
                for (size_t i = 0; i < artifacts.obj_n; i++) {
                    struct stat obj_st;
                    if (stat(artifacts.obj_paths[i], &obj_st) == 0) {
                        if (obj_st.st_mtime > out_st.st_mtime) {
                            need_link = 1;
                            printf("%s[link]%s %s (object newer than binary)\n", 
                                   abs_fore.blue, abs_fore.normal, cfg->output);
                            break;
                        }
                    }
                }
                
                if (!need_link) {
                    printf("%s[skip]%s %s (up to date)\n", 
                           abs_fore.cyan, abs_fore.normal, cfg->output);
                }
            }
        }
    

        if (need_link) {
        
            if (cmd_started) {
                pos += snprintf(out_buf + pos, out_sz - pos, "&& ");
            }
            
            if (is_library) {
                if (strcmp(cfg->build_type, "static") == 0) {
                    char lib_path[PATH_MAX];
                    snprintf(lib_path, sizeof(lib_path), "%s/%slib%s.a", 
                            cfg->out_dir, cfg->output ? "" : "lib", cfg->output);
                    
                    pos += snprintf(out_buf + pos, out_sz - pos, "ar rcs \"%s\" ", lib_path);
                    
                    for (size_t i = 0; i < artifacts.obj_n; i++) {
                        pos += snprintf(out_buf + pos, out_sz - pos, "\"%s\" ", 
                                       artifacts.obj_paths[i]);
                    }
                } else if (strcmp(cfg->build_type, "shared") == 0) {
                    char lib_path[PATH_MAX];
                    snprintf(lib_path, sizeof(lib_path), "%s/%slib%s.so", 
                            cfg->out_dir, cfg->output ? "" : "lib", cfg->output);
                    
                    pos += snprintf(out_buf + pos, out_sz - pos, "%s -shared -o \"%s\" ", 
                                   cfg->compiler, lib_path);
                    
                    for (size_t i = 0; i < artifacts.obj_n; i++) {
                        pos += snprintf(out_buf + pos, out_sz - pos, "\"%s\" ", 
                                       artifacts.obj_paths[i]);
                    }
                    
                    pos = build_ldlibs(cfg, out_buf, out_sz, pos);
                }
            } else {
                char bin_path[PATH_MAX];
                snprintf(bin_path, sizeof(bin_path), "%s/%s", cfg->out_dir, cfg->output);
                
                pos += snprintf(out_buf + pos, out_sz - pos, "%s ", cfg->compiler);
                pos = build_common_flags(cfg, out_buf, out_sz, pos);
                
                for (size_t i = 0; i < artifacts.obj_n; i++) {
                    pos += snprintf(out_buf + pos, out_sz - pos, "\"%s\" ", 
                                   artifacts.obj_paths[i]);
                }
                
                pos = build_ldlibs(cfg, out_buf, out_sz, pos);
                
                pos += snprintf(out_buf + pos, out_sz - pos, "-o \"%s\"", bin_path);
            }
            
            cmd_started = 1;
        }
    }
    
    if (!cmd_started) {
        printf("%s[info]%s nothing to do\n", abs_fore.yellow, abs_fore.normal);
        pos += snprintf(out_buf + pos, out_sz - pos, "true");
    }
    
    _free_artifacts(&artifacts);
    return 0;
}

void compiler_conf_free(compiler_conf *cfg) {
    if (!cfg) return;

    if (cfg->cflags) _free_str_array(&cfg->cflags, &cfg->cflags_n);
    if (cfg->ldlibs) _free_str_array(&cfg->ldlibs, &cfg->ldlibs_n);
    if (cfg->sources) _free_str_array(&cfg->sources, &cfg->sources_n);
    if (cfg->include_dirs) _free_str_array(&cfg->include_dirs, &cfg->include_n);
    if (cfg->pkg_config_libs) _free_str_array(&cfg->pkg_config_libs, &cfg->pkg_config_libs_n);
    if (cfg->defines) _free_str_array(&cfg->defines, &cfg->defines_n);
    if (cfg->lib_dirs) _free_str_array(&cfg->lib_dirs, &cfg->lib_dirs_n);

    if (cfg->output) free(cfg->output);
    if (cfg->src_dir) free(cfg->src_dir);
    if (cfg->out_dir) free(cfg->out_dir);
    if (cfg->pkg_config_path) free(cfg->pkg_config_path);
    if (cfg->build_type) free(cfg->build_type);
    if (cfg->build_phase) free(cfg->build_phase);
    
    if (cfg->obj_dir && cfg->cleanup){
        DIR* dp;
        struct dirent* ep;

        dp = opendir(cfg->obj_dir);
        if (dp != NULL){
            while((ep = readdir(dp))){
                char path[PATH_MAX];
                snprintf(path, PATH_MAX, "%s/%s", cfg->obj_dir, ep->d_name);
                remove(path);
            }
        }
        closedir(dp);
        rmdir(cfg->obj_dir);
    }
    
    if (cfg->obj_dir) free(cfg->obj_dir);

    memset(cfg, 0, sizeof(compiler_conf));
}

#endif
#define ABS_COMPILATION