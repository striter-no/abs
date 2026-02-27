#include "configuration.h"
#include <dirent.h>
#include <linux/limits.h>
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

static size_t build_sources(const compiler_conf *cfg, char *out_buf, size_t out_sz, size_t pos) {
    for (size_t i = 0; i < cfg->sources_n; i++) {
        pos += snprintf(out_buf + pos, out_sz - pos, "\"%s/%s\" ", cfg->src_dir, cfg->sources[i]);
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

int build_config_emit_cmd(const compiler_conf *cfg, char *out_buf, size_t out_sz) {
    struct stat st = {0};
    if (stat(cfg->obj_dir, &st) == -1) {
        mkdir(cfg->obj_dir, 0700);
    }

    if (stat(cfg->out_dir, &st) == -1) {
        mkdir(cfg->out_dir, 0700);
    }

    size_t pos = 0;
    int is_library = (cfg->build_type && 
                     (strcmp(cfg->build_type, "static") == 0 || 
                      strcmp(cfg->build_type, "shared") == 0));
    
    if (cfg->pkg_config_path) {
        pos += snprintf(out_buf + pos, out_sz - pos, 
            "PKG_CONFIG_PATH=%s ", cfg->pkg_config_path);
    }
    
    if (is_library) {
        pos += snprintf(out_buf + pos, out_sz - pos, 
            "%s ", cfg->compiler);
        
        if (strcmp(cfg->build_type, "shared") == 0) {
            pos += snprintf(out_buf + pos, out_sz - pos, "-fPIC ");
        }
        
        pos = build_common_flags(cfg, out_buf, out_sz, pos);
        pos += snprintf(out_buf + pos, out_sz - pos, "-c ");
        
        char obj_files[cfg->sources_n][PATH_MAX];
        for (size_t i = 0; i < cfg->sources_n; i++) {
            const char *src = cfg->sources[i];
            char obj_name[PATH_MAX];
            strncpy(obj_name, src, sizeof(obj_name) - 3);
            char *dot = strrchr(obj_name, '.');
            if (dot) *dot = '\0';
            strcat(obj_name, ".o");
            
            snprintf(obj_files[i], PATH_MAX, "%s/%s", cfg->obj_dir, obj_name);
            
            pos += snprintf(out_buf + pos, out_sz - pos,
                "\"%s/%s\" -o \"%s\" ", cfg->src_dir, src, obj_files[i]);
        }
        
        if (strcmp(cfg->build_type, "static") == 0) {
            pos += snprintf(out_buf + pos, out_sz - pos,
                "&& ar rcs \"%s/%slib%s.a\" ", cfg->out_dir,
                cfg->output ? "" : "lib", cfg->output);
            
            for (size_t i = 0; i < cfg->sources_n; i++) {
                pos += snprintf(out_buf + pos, out_sz - pos, "\"%s\" ", obj_files[i]);
            }
        } else if (strcmp(cfg->build_type, "shared") == 0) {
            pos += snprintf(out_buf + pos, out_sz - pos,
                "&& %s -shared -o \"%s/%slib%s.so\" ", cfg->compiler, cfg->out_dir,
                cfg->output ? "" : "lib", cfg->output);
            
            for (size_t i = 0; i < cfg->sources_n; i++) {
                pos += snprintf(out_buf + pos, out_sz - pos, "\"%s\" ", obj_files[i]);
            }
            
            pos = build_ldlibs(cfg, out_buf, out_sz, pos);
        }
        
    } else {
        pos += snprintf(out_buf + pos, out_sz - pos, "%s ", cfg->compiler);
        pos = build_common_flags(cfg, out_buf, out_sz, pos);
        pos = build_sources(cfg, out_buf, out_sz, pos);
        pos = build_ldlibs(cfg, out_buf, out_sz, pos);

        pos += snprintf(out_buf + pos, out_sz - pos, "-o \"%s/%s\"", cfg->out_dir, cfg->output);
    }
    
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
    
    if (cfg->obj_dir){
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