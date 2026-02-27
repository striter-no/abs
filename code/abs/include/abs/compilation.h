#include "configuration.h"

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

int build_config_emit_cmd(const compiler_conf *cfg, char *out_buf, size_t out_sz) {
    size_t pos = 0;
    
    if (cfg->pkg_config_path) {
        pos += snprintf(out_buf + pos, out_sz - pos, 
            "PKG_CONFIG_PATH=%s ", cfg->pkg_config_path);
    }
    
    pos += snprintf(out_buf + pos, out_sz - pos, "%s ", cfg->compiler);
    
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
    
    for (size_t i = 0; i < cfg->sources_n; i++) {
        pos += snprintf(out_buf + pos, out_sz - pos, "%s/%s ", cfg->src_dir, cfg->sources[i]);
    }
    
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
    
    pos += snprintf(out_buf + pos, out_sz - pos, "-o %s/%s", cfg->out_dir, cfg->output);
    
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
    if (cfg->sources) _free_str_array(&cfg->sources, &cfg->sources_n);

    if (cfg->output) free(cfg->output);
    if (cfg->src_dir) free(cfg->src_dir);
    if (cfg->out_dir) free(cfg->out_dir);
    if (cfg->pkg_config_path) free(cfg->pkg_config_path);

    memset(cfg, 0, sizeof(compiler_conf));
}

#endif
#define ABS_COMPILATION